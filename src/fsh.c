#include <fsh.h>
#include <commands.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <readline/readline.h>
#include <readline/history.h>

// global variables' declaration
char PREV_WORKING_DIR[PATH_MAX];
char *HOME;
int PREV_RETURN_VALUE;
char CWD[PATH_MAX];

char prompt[52];


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
  if (getcwd(CWD, PATH_MAX) == NULL) {
    return EXIT_FAILURE;
  }

  strcpy(PREV_WORKING_DIR, CWD);
  return EXIT_SUCCESS;
}


int main(int argc, char* argv[]) {
  rl_outstream = stderr;

  char *line;

  if(init_wd_vars() == EXIT_FAILURE ||
   init_env_vars() == EXIT_FAILURE) return EXIT_FAILURE;

  while(1) {
    update_prompt();
    line = readline(prompt);
    add_history(line);
    PREV_RETURN_VALUE = parse_and_exec_simple_cmd(line);
    free(line);
  }

  return EXIT_SUCCESS;
}