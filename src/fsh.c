#include "fsh.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "cmd_types.h"
#include "commands.h"
#include "execution.h"
#include "parsing.h"
#ifdef DEBUG
#include "debug.h"
#endif

// global variables' declaration
char *CWD, *PREV_WORKING_DIR, *HOME;
volatile sig_atomic_t STOP_SIG;
// If positive, then is the previous return value. If negative, then is the
// opposite of the signal number that interrupted the previous command
int PREV_RETURN_VALUE;

char prompt[52];
char *vars[128];

// Updates the prompt (without printing it) according to the current state
void update_prompt(void) {
  char *head = prompt;

  // Return code
  head += sprintf(head, "\001\033[%dm\002", PREV_RETURN_VALUE ? 91 : 32);

  int code_len;
  if (PREV_RETURN_VALUE < 0) {
    strcpy(head, "[SIG]");
    code_len = 5;
  } else {
    code_len = sprintf(head, "[%d]", PREV_RETURN_VALUE);
  }
  head += code_len;

  // CWD
  strcpy(head, "\001\033[36m\002");
  head += 7;

  int space_left = 30 - code_len - 2; // Keep 2 bytes for "$ " at the end
  int len = strlen(CWD);
  if (len <= space_left) {
    strcpy(head, CWD);
    head += len;
  } else { // CWD is too big
    /* Note : 'str + strlen(str) - n' points to the nth character of str
     * starting from the end */
    head += sprintf(head, "...%s", CWD + len - (space_left - 3));
  }

  strcpy(head, "\001\033[00m\002$ ");
  return;
}

int init_env_vars() {
  HOME = getenv("HOME");
  return EXIT_SUCCESS;
}

int init_wd_vars() {
  PREV_WORKING_DIR = NULL;

  CWD = getcwd(NULL, 0);
  if (!CWD) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

void handle(int signo) {
  STOP_SIG = 1;
}

void reset_handlers(void) {
  struct sigaction sa = { .sa_handler = SIG_DFL };
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGINT, &sa, NULL);
}

int main(int argc, char* argv[]) {
  rl_outstream = stderr;

  struct sigaction sa = { .sa_handler = SIG_IGN };
  sigaction(SIGTERM, &sa, NULL);
  sa.sa_handler = handle;
  sigaction(SIGINT, &sa, NULL);

  char *line;
  struct cmd *cmd;

  if(init_wd_vars() == EXIT_FAILURE ||
    init_env_vars() == EXIT_FAILURE) return EXIT_FAILURE;

  while(1) {
    update_prompt();
    line = readline(prompt);
    if (line == NULL) {
      cmd_exit(1, NULL);
    }

    if (*line != '\0') {
      add_history(line);
      cmd = parse(line);
      if (cmd == NULL) {
        PREV_RETURN_VALUE = 2; // syntax error
      } else {
#ifdef DEBUG
        print_cmd(cmd);
#endif
        STOP_SIG = 0;
        PREV_RETURN_VALUE = exec_cmd_chain(cmd, vars);
        if (STOP_SIG) PREV_RETURN_VALUE = -SIGINT;
        free_cmd(cmd);
      }
    }

    free(line);
  }
}
