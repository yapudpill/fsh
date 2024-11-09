#include <fsh.h>
#include <commands.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <readline/readline.h>
#include <readline/history.h>

// global variables' declaration
char PREV_WORKING_DIR[PATH_MAX];
char HOME[PATH_MAX];
int PREV_RETURN_VALUE;
char CWD[PATH_MAX];

char *construct_prompt() {
  char *prompt = malloc((strlen(CWD)+6)*sizeof(char));
  snprintf(prompt, strlen(CWD)+6,"[%d]%s$ ",PREV_RETURN_VALUE, CWD); // FIXME : Should handle coloring and cutting the string if too long
  return prompt;
}

int init_env_vars(char *envp[]) {
  while(strncmp(*envp, "HOME=",5) != 0) {
    envp++;
  }
  strcpy(HOME, *(envp)+5);
  return EXIT_SUCCESS;
}

int init_wd_vars() {
  char *cwd = getcwd(NULL, 0);

  if(cwd == NULL) return EXIT_FAILURE;

  strcpy(CWD, cwd);
  strcpy(PREV_WORKING_DIR, CWD);

  return EXIT_SUCCESS;
}


int main(int argc, char* argv[], char *envp[]) {
  char *line, *prompt;

  if(init_wd_vars() == EXIT_FAILURE ||
   init_env_vars(envp) == EXIT_FAILURE) return EXIT_FAILURE;

  while(1) {
    prompt = construct_prompt(); // FIXME : Should not be reconstructed every time
    line = readline(prompt);
    add_history(line);
    PREV_RETURN_VALUE = parse_and_exec_simple_cmd(line);
  }

  return EXIT_SUCCESS;
}