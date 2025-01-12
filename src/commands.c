#include "commands.h"

#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "fsh.h"
#include "execution.h"

typedef int (*cmd_func)(int argc, char **argv);

/**
 * Internal command. Takes no argument, and prints on stdout the current
 * working directory of the shell.
 *
 * @return EXIT_SUCCESS on success, EXIT_FAILURE on failure.
 */
int cmd_pwd(int argc, char **argv) {
  // Considering that the variable CWD is always updated using `getcwd` at
  // every cwd change, it should be safe to assume it already contains the right
  // path, so there is no need for another `getcwd()` call
  if (argc > 1) {
    dprintf(2, "pwd: too many arguments");
    return EXIT_FAILURE;
  }
  dprintf(1, "%s\n", g_cwd);
  return EXIT_SUCCESS;
}


/**
 * Internal command. Takes a directory reference, and changes the working
 * directory of the shell to that directory.
 * Only applies to the subshell it is executed in. For exemple, it does not
 * apply the directory change to the main shell when called in a parallel loop.
 *
 * When called without an argument, defaults to moving to the HOME directory,
 * and fails if the variable is not set.
 *
 * When called with `-`, moves to the previous working directory, and fails if
 * there is no previous working directory.
 *
 * @return EXIT_SUCCESS on success, EXIT_FAILURE on failure.
 *         If the execution results in an invalid state of the subshell, exits the
 *         subshell entirely.
 */
int cmd_cd(int argc, char **argv) {
  if (argc > 2) {
    dprintf(2, "cd: too many arguments");
    return EXIT_FAILURE;
  }

  // change directory
  int ret;
  if (argc == 1) {
    if (g_home == NULL) {
      dprintf(2, "cd: HOME not set\n");
      return EXIT_FAILURE;
    }
    ret = chdir(g_home);
  } else if (strcmp(argv[1], "-") == 0) {
    if (g_prev_wd == NULL) {
      dprintf(2, "cd: no previous working directory\n");
      return EXIT_FAILURE;
    }
    ret = chdir(g_prev_wd);
  } else {
    ret = chdir(argv[1]);
  }

  if (ret == -1) {
    perror("cd");
    return EXIT_FAILURE;
  }

  // update variables
  if (g_prev_wd) free(g_prev_wd);
  g_prev_wd = g_cwd;

  g_cwd = getcwd(NULL, 0);
  if (g_cwd == NULL) {
    // If we ever meet this condition, it means something very wrong has
    // happened, or the directory has been altered at the wrong moment.

    // FIXME: decide what to do in this situation. Maybe try to revert to the
    // previous directory, and if that fails too, give up and exit the shell.
    perror("cd: getcwd");
    exit(EXIT_FAILURE);
  }

  return EXIT_SUCCESS;
}


/**
 * Internal command. Takes a file reference, and prints its type.
 *
 * @return EXIT_SUCCESS on success, EXIT_FAILURE on failure.
 */
int cmd_ftype(int argc, char **argv) {
  if (argc != 2) {
    dprintf(2, "ftype: this command takes exactly one argument");
    return EXIT_FAILURE;
  }

  struct stat sb;
  if (lstat(argv[1], &sb) == -1) {
    perror("ftype");
    return EXIT_FAILURE;
  }

  switch(sb.st_mode & S_IFMT) {
    case S_IFREG:
      dprintf(1, "regular file\n");
      break;
    case S_IFDIR:
      dprintf(1, "directory\n");
      break;
    case S_IFLNK:
      dprintf(1, "symbolic link\n");
      break;
    case S_IFIFO:
      dprintf(1, "named pipe\n");
      break;
    default:
      dprintf(1, "other\n");
  }

  return EXIT_SUCCESS;
}


/**
 * Internal command. Exits the current fsh subshell, using the code passed in
 * argument if available, otherwise the previous command return code.
 *
 * @return EXIT_FAILURE in case of invalid usage.
 */
int cmd_exit(int argc, char **argv) {
  if (argc > 2) {
    dprintf(2, "exit: too many arguments");
    return EXIT_FAILURE;
  }

  int val;
  if (argc == 1) {
    val = g_prev_ret_val;
  } else if (sscanf(argv[1], "%d", &val) != 1) {
    dprintf(2, "exit: invalid argument");
    return EXIT_FAILURE;
  }

  if (g_prev_wd) free(g_prev_wd);
  free(g_cwd);

  exit(val);
}


/**
 * Internal debug command. Useful to debug I/O. For every char it receives in
 * stdin, slowly repeats it twice on stdout and adds a new line.
 *
 * @return EXIT_SUCCESS on success, EXIT_FAILURE on failure.
 */
int cmd_autotune(int argc, char **argv) {
  size_t ret;
  char c;
  while ((ret = read(0, &c, 1)) > 0) {
    if (c == '\n') continue;
    write(1, &c, 1);
    usleep(200000);
    write(1, &c, 1);
    usleep(200000);
    write(1, "\n", 1);
  }

  if (ret == -1) {
    perror("autotune: read");
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}


/**
 * Internal debug command. Simply returns the code passed in argument, or 1 by
 * default.
 *
 * @return the value passed in argument
 */
int cmd_oopsie(int argc, char **argv) {
  if (argc > 2) {
    dprintf(2, "oopsie: too many arguments");
    return EXIT_FAILURE;
  }

  int val;
  if (argc == 1) {
    val = EXIT_FAILURE;
  } else {
    errno = 0;
    val = (int) strtol(argv[1], NULL, 10);
    if (errno) {
      perror("oopsie");
      return EXIT_FAILURE;
    }
  }
  return val;
}


/**
 * Executes an external command, using execvp, and forwarding to the command
 * the arguments in argv.
 *
 * Also performs the necessary redirections so that the fd i refers to
 * redir[i], for each i in {0, 1, 2}
 *
 * @return the return code of the command, or -1 if it was terminated by a
 *         signal
 */
int call_external_cmd(int argc, char **argv, int redir[3]) {
  int pid, i;
  switch ((pid = fork())) {
    case -1:
      perror("fork");
      return EXIT_FAILURE;
    case 0:
        struct sigaction sa = { 0 };
        sa.sa_handler = SIG_DFL;
        sigaction(SIGTERM, &sa, NULL);
      for (i = 0; i < 3; i++) {
        if (redir[i] != -2) {
          if (dup2(redir[i], i) == -1) {
            perror("dup2");
            exit(EXIT_FAILURE);
          }
          close(redir[i]);
        }
      }
      execvp(argv[0], argv);
      // We are in the child process, so we have to immediately exit if
      // something goes wrong, otherwise there will be an additional child `fsh`
      // process every time we enter a non-existent command
      perror("redirect_exec");
      exit(EXIT_FAILURE);
    default:
      return wait_cmd(pid);
  }
}


// Runs a command (internal or external) and wait for it to finish
int call_command_and_wait(int argc, char **argv, int redir[3]) {
  char *cmd = argv[0];

  cmd_func internal_function;
  if (strcmp(cmd, "ftype") == 0) {
    internal_function = cmd_ftype;
  } else if (strcmp(cmd, "exit") == 0) {
    internal_function = cmd_exit;
  } else if (strcmp(cmd, "cd") == 0) {
    internal_function = cmd_cd;
  } else if (strcmp(cmd, "pwd") == 0) {
    internal_function = cmd_pwd;
  } else if (strcmp(cmd, "autotune") == 0) {
    internal_function = cmd_autotune;
  } else if (strcmp(cmd, "oopsie") == 0) {
    internal_function = cmd_oopsie;
  } else {
    internal_function = NULL;
  }

  int ret;
  if (internal_function) {
    int i, saves[3];
    for (i = 0; i < 3; i++) {
      if (redir[i] != -2) {
        saves[i] = dup(i);
        dup2(redir[i], i);
      }
    }
    ret = internal_function(argc, argv);
    for (i = 0; i < 3; i++) {
      if (redir[i] != -2) {
        dup2(saves[i], i);
        close(saves[i]);
      }
    }
  } else {
    ret = call_external_cmd(argc, argv, redir);
  }
  return ret;
}
