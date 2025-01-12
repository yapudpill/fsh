#include "fsh.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <sys/types.h>
#include <sys/wait.h>


#include "cmd_types.h"
#include "execution.h"
#include "parsing.h"
#ifdef DEBUG
#include "debug.h"
#endif

void sig_handler(int sig) {
  g_sig_received = sig;
}

// global variables' declaration, prefixed with `g_` to recognize them
char *g_cwd; // Current working directory
char *g_prev_wd; // Previous working directory
char *g_home; // Path to the user's home
int g_prev_ret_val; // Previous return value
volatile sig_atomic_t g_sig_received = 0;

char g_prompt[52];
char *g_vars[128];

/**
 * Updates the shell prompt to reflect the current state, including the previous
 * command's return value and the current working directory. The prompt
 * is updated but not printed to the screen.
 */
void update_prompt(void) {
  char *head = g_prompt;

  // Return code
  head += sprintf(head, "\001\033[%dm\002", g_prev_ret_val ? 91 : 32);

  int code_len;
  if (g_prev_ret_val < 0) {
    strcpy(head, "[SIG]");
    code_len = 5;
  } else {
    code_len = sprintf(head, "[%d]", g_prev_ret_val);
  }
  head += code_len;

  // CWD
  strcpy(head, "\001\033[36m\002");
  head += 7;

  int space_left = 30 - code_len - 2; // Keep 2 bytes for "$ " at the end
  int len = strlen(g_cwd);
  if (len <= space_left) {
    strcpy(head, g_cwd);
    head += len;
  } else { // CWD is too big
    /* Note : 'str + strlen(str) - n' points to the nth character of str
     * starting from the end */
    head += sprintf(head, "...%s", g_cwd + len - (space_left - 3));
  }

  strcpy(head, "\001\033[00m\002$ ");
}

int init_env_vars() {
  g_home = getenv("HOME");
  return EXIT_SUCCESS;
}

int init_wd_vars() {
  g_prev_wd = NULL;

  g_cwd = getcwd(NULL, 0); // This is possible in glibc, but it has to be free'd later
  if (!g_cwd) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

int main(int argc, char* argv[]) {
  struct sigaction sa = { 0 };
  sa.sa_handler = SIG_IGN;
  sigaction(SIGTERM, &sa, NULL);
  sa.sa_handler = sig_handler;
  sigaction(SIGINT, &sa, NULL);

  rl_outstream = stderr;

  char *line;
  struct cmd *cmd;

  if (init_wd_vars() == EXIT_FAILURE ||
    init_env_vars() == EXIT_FAILURE) return EXIT_FAILURE;

  while (1) {
    update_prompt();
    line = readline(g_prompt);
    if (line == NULL) {
      break;
    }

    if (*line != '\0') {
      add_history(line);
      cmd = parse(line);
      if (cmd == NULL) {
        g_prev_ret_val = parsing_errno;
      } else {
#ifdef DEBUG
        print_cmd(cmd);
#endif
        g_prev_ret_val = exec_cmd_chain(cmd, g_vars);
        free_cmd(cmd);
      }
    }

    if (g_sig_received) {
      while(wait(NULL) > 0);
      if(errno == ECHILD) g_sig_received = 0;
      else {
        perror("wait");
        return EXIT_FAILURE;
      }
    }

    free(line);
  }

  if (g_prev_wd) free(g_prev_wd);
  free(g_cwd);
  return g_prev_ret_val;
}
