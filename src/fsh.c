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

char prompt[52] = {0};

void update_prompt(void) {
  int space_left = 30;
  char *head = prompt;

  // Color of the return code
  int color = PREV_RETURN_VALUE ? 91 : 32;

  // Return code itself
  char code[4];
  if (PREV_RETURN_VALUE == -1) strcpy(code, "SIG");
  else snprintf(code, 3, "%d", PREV_RETURN_VALUE);

  // Add colors and "[code]" to the prompt
  int written = sprintf(head, "\001\033[%dm\002[%s]\001\033[36m\002", color, code);
  head += written;
  space_left -= strlen(code) + 2;

  // Add cwd to the prompt
  int len = strlen(CWD);
  if (len <= space_left - 2) { // Keep 2 char for "$ " at the end
    strcpy(head, CWD);
    head += len;
  } else { // CWD is too big
    strcpy(head, "...");
    head += 3;
    space_left -= 3;
    strcpy(head, CWD + len - space_left + 2);
    head += space_left - 2;
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
  char *line;

  if(init_wd_vars() == EXIT_FAILURE ||
   init_env_vars() == EXIT_FAILURE) return EXIT_FAILURE;

  while(1) {
    update_prompt();
    line = readline(prompt);
    add_history(line);
    PREV_RETURN_VALUE = exec_simple_cmd(line);
    free(line);
  }

  return EXIT_SUCCESS;
}