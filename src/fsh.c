#include <fsh.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <readline/readline.h>
#include <readline/history.h>

#include <cmd_types.h>
#include <execution.h>
#include <parsing.h>
#ifdef DEBUG
#include <debug.h>
#endif

// global variables' declaration
char *CWD, *PREV_WORKING_DIR, *HOME;
int PREV_RETURN_VALUE;

char prompt[52];

char *vars[128];


// Updates the prompt (without printing it) according to the current state
void update_prompt(void) {
  char *head = prompt;

  // Return code
  head += sprintf(head, "\001\033[%dm\002", PREV_RETURN_VALUE ? 91 : 32);

  int code_len;
  if (PREV_RETURN_VALUE == -1) {
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
    /* Note : 'str - strlen(str) - n' points to the nth character of str
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

int main(int argc, char* argv[]) {
  rl_outstream = stderr;

  char *line;
  struct cmd *cmd;

  if(init_wd_vars() == EXIT_FAILURE ||
    init_env_vars() == EXIT_FAILURE) return EXIT_FAILURE;

  while(1) {
    update_prompt();
    line = readline(prompt);
    if (line == NULL) {
      break;
    }

    if (*line != '\0') {
      add_history(line);

      cmd = parse(line);
      if (cmd != NULL) {
#ifdef DEBUG
        print_cmd(cmd);
#endif
        PREV_RETURN_VALUE = exec_cmd_chain(cmd, vars);
        free_cmd(cmd);
      }
    }

    free(line);
  }

  if (PREV_WORKING_DIR) free(PREV_WORKING_DIR);
  free(CWD);
  return PREV_RETURN_VALUE;
}
