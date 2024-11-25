#include <fsh.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <readline/readline.h>
#include <readline/history.h>

#include <cmd_types.h>
#include <execution.h>
#include <parsing.h>


// global variables' declaration
char PREV_WORKING_DIR[PATH_MAX];
char *HOME;
int PREV_RETURN_VALUE;
char CWD[PATH_MAX];

char prompt[52], **vars;


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
  if (getcwd(CWD, PATH_MAX) == NULL) {
    return EXIT_FAILURE;
  }

  strcpy(PREV_WORKING_DIR, CWD);
  return EXIT_SUCCESS;
}

// Executes a single command line
int run_line(char *line) {
  struct cmd *cmd = parse(line);
  if (cmd != NULL) {
    // print_cmd(cmd);
    PREV_RETURN_VALUE = exec_cmd_chain(cmd, STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO, vars);
    free_cmd(cmd);
  }
  return PREV_RETURN_VALUE;
}

int main(int argc, char* argv[]) {
  rl_outstream = stderr;

  char *line;

  if(init_wd_vars() == EXIT_FAILURE ||
    init_env_vars() == EXIT_FAILURE) return EXIT_FAILURE;

  vars = calloc(128, sizeof(char *));

  while(1) {
    update_prompt();
    line = readline(prompt);
    if (line == NULL) {
      exit(PREV_RETURN_VALUE);
    }

    if (*line != '\0') {
      add_history(line);
      run_line(line);
    }

    free(line);
  }
}