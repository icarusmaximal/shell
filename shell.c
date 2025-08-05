#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "tokenizer.h"

/* Convenience macro to silence compiler warnings about unused function
 * parameters. */
#define unused __attribute__((unused))

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

int cmd_exit(struct tokens *tokens);
int cmd_help(struct tokens *tokens);
int cmd_pwd(struct tokens *tokens);
int cmd_cd(struct tokens *tokens);

/* Built-in command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(struct tokens *tokens);

/* Built-in command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t *fun;
  char *cmd;
  char *doc;
} fun_desc_t;

fun_desc_t cmd_table[] = {
    {cmd_exit, "exit", "exit the command shell"},
    {cmd_help, "?", "show this help menu"},
    {cmd_pwd, "pwd", "prints working directory"},
    {cmd_cd, "cd", "changes directory"},
};

/* Prints a helpful description for the given command */
int cmd_help(unused struct tokens *tokens) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  return 1;
}

/* Exits this shell */
int cmd_exit(unused struct tokens *tokens) { exit(0); }

int cmd_pwd(unused struct tokens *tokens) {
  char cwd[1024];
  if (getcwd(cwd, sizeof(cwd)) != NULL) {
    printf("%s\n", cwd);
  } else {
    perror("getcwd() error");
  }
  return 1;
}

int cmd_cd(struct tokens *tokens) {
  /* If no argument is given, change to the home directory */
  if (tokens_get_length(tokens) == 1) {
    const char *home = getenv("HOME");
    if (home) {
      if (chdir(home) != 0) {
        perror("cd");
      }
    } else {
      fprintf(stderr, "No HOME environment variable set.\n");
    }
  } else {
    /* Change to the directory specified in the first token */
    const char *dir = tokens_get_token(tokens, 1);
    if (chdir(dir) != 0) {
      perror("cd");
    }
  }
  return 1;
}

/* Looks up the built-in command, if it exists. */
int lookup(char cmd[]) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0))
      return i;
  return -1;
}

/* Intialization procedures for this shell */
void init_shell() {
  /* Our shell is connected to standard input. */
  shell_terminal = STDIN_FILENO;

  /* Check if we are running interactively */
  shell_is_interactive = isatty(shell_terminal);

  if (shell_is_interactive) {
    /* If the shell is not currently in the foreground, we must pause the shell
     * until it becomes a foreground process. We use SIGTTIN to pause the shell.
     * When the shell gets moved to the foreground, we'll receive a SIGCONT. */
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
      kill(-shell_pgid, SIGTTIN);

    /* Saves the shell's process id */
    shell_pgid = getpid();

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);

    /* Save the current termios to a variable, so it can be restored later. */
    tcgetattr(shell_terminal, &shell_tmodes);
  }
}

int main(unused int argc, unused char *argv[]) {
  init_shell();

  static char line[4096];
  int line_num = 0;

  /* Please only print shell prompts when standard input is not a tty */
  if (shell_is_interactive)
    fprintf(stdout, "%d: ", line_num);

  while (fgets(line, 4096, stdin)) {
    /* Split our line into words. */
    struct tokens *tokens = tokenize(line);

    /* Find which built-in function to run. */
    int fundex = lookup(tokens_get_token(tokens, 0));

    if (fundex >= 0) {
      cmd_table[fundex].fun(tokens);
    } else {

      pid_t pid = fork();
      if (pid == 0) {
        // --- CHILD PROCESS ---

        // 1. Prepare the argument vector (argv) for execv.
        //    We need a NULL-terminated array of strings.
        size_t num_tokens = tokens_get_length(tokens);
        char *argv_exec[num_tokens + 1];
        for (size_t i = 0; i < num_tokens; i++) {
          argv_exec[i] = tokens_get_token(tokens, i);
        }
        argv_exec[num_tokens] = NULL; // The array must end with NULL

        // 2. Execute the program.
        //    argv_exec[0] is the full path to the program.
        execv(argv_exec[0], argv_exec);

        // If execv returns, it means an error occurred.
        perror(argv_exec[0]);
        exit(EXIT_FAILURE); // Terminate the child process on error.

      } else {
        // --- PARENT PROCESS ---

        // 3. Wait for the child process to complete.
        wait(NULL);
      }
    }

    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);

    /* Clean up memory */
    tokens_destroy(tokens);
  }

  return 0;
}
