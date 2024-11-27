#include <commands.h>

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <fsh.h>

typedef int (*cmd_func)(int argc, char **argv);

int cmd_pwd(int argc, char **argv) {
  // Considering that the variable CWD is always updated using `getcwd` at
  // every cwd change, it should be safe to assume it already contains the right
  // path, so there is no need for another `getcwd`
  if (argc > 1) {
    dprintf(2, "pwd: too many arguments");
    return EXIT_FAILURE;
  }
  dprintf(1, "%s\n", CWD);
  return EXIT_SUCCESS;
}

int cmd_cd(int argc, char **argv) {
  if (argc > 2) {
    dprintf(2, "cd: too many arguments");
    return EXIT_FAILURE;
  }

  // change directory
  int ret;
  if(argc == 1) {
    if (HOME == NULL) {
      dprintf(2, "cd: HOME not set\n");
      return EXIT_FAILURE;
    }
    ret = chdir(HOME);
  } else if(strcmp(argv[1], "-") == 0) {
    if (PREV_WORKING_DIR == NULL) {
      dprintf(2, "cd: no previous working directory\n");
      return EXIT_FAILURE;
    }
    ret = chdir(PREV_WORKING_DIR);
  } else {
    ret = chdir(argv[1]);
  }

  if(ret == -1) {
    perror("cd");
    return EXIT_FAILURE;
  }

  // update variables
  if (PREV_WORKING_DIR) free(PREV_WORKING_DIR);
  PREV_WORKING_DIR = CWD;

  CWD = getcwd(NULL, 0);
  if (CWD == NULL) {
    // If we ever meet this condition, it means something very wrong has
    // happened, or the directory has been altered at the wrong moment.

    // FIXME: decide what to do in this situation. Maybe try to revert to the
    // previous directory, and if that fails too, give up and exit the shell.
    perror("cd: getcwd");
    exit(EXIT_FAILURE);
  }

  return EXIT_SUCCESS;
}

// Prints the type of the file passed in argument
int cmd_ftype(int argc, char **argv) {
  if(argc != 2) {
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

// Exits fsh, using the code passed in argument or the previous command return code
int cmd_exit(int argc, char **argv) {
  if (argc > 2) {
    dprintf(2, "exit: too many arguments");
    return EXIT_FAILURE;
  }

  int val;
  if (argc == 1) {
    val = PREV_RETURN_VALUE;
  } else if (sscanf(argv[1], "%d", &val) != 1) {
    dprintf(2, "exit: invalid argument");
    return EXIT_FAILURE;
  }

  if (PREV_WORKING_DIR) free(PREV_WORKING_DIR);
  free(CWD);

  exit(val);
}

// Debug command, useful to debug I/O. For every char it receives in stdin,
// slowly repeat it twice on stdout and add a new line
int cmd_autotune(int argc, char **argv) {
  size_t ret;
  char c;
  while ((ret = read(1, &c, 1)) != 0) {
    if (ret == -1) {
      if (errno == EINTR) continue;
      perror("autotune: read");
      return EXIT_FAILURE;
    }
    if (c == '\n') continue;
    write(1, &c, 1);
    usleep(200000);
    write(1, &c, 1);
    usleep(200000);
    write(1, "\n", 1);
  }
  return EXIT_SUCCESS;
}

// Debug command, simply returns the code passed in argument, or 1 by default
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

// Executes an external command, using execvp
int call_external_cmd(int argc, char **argv, int redir[3]) {
  int pid, i;
  switch ((pid = fork())) {
    case -1:
      perror("fork");
      return EXIT_FAILURE;
    case 0:
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
      dprintf(2, "fsh: unknown command %s\n", argv[0]);
      exit(EXIT_FAILURE);
    default:
      int wstat;
      if (waitpid(pid, &wstat, 0) == -1) {
        perror("waitpid");
        return EXIT_FAILURE;
      }
      if (WIFEXITED(wstat)) {
        return WEXITSTATUS(wstat);
      }
      if (WIFSIGNALED(wstat)) {
        // We want fsh to exit with exit code 255 if we exit right after a
        // process dies because of a signal. However, we would still like to
        // differentiate inside fsh if the previous process died because of a
        // signal or if it simply returned 255. So we use return code -1 to
        // indicate a death by signal. Because exit codes are encoded on 8 bits,
        // it will automatically be converted to 255 when exiting !
        return -1;
      }
      return EXIT_FAILURE;
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
