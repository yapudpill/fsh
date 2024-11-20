#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stddef.h>

#include "fsh.h"
#include "commands.h"
#include "utils.h"



int cmd_pwd(int argc, char **argv, int fd_in, int fd_out, int fd_err) {
  // Considering that the variable CWD is always updated using `getcwd` at every cwd change, it should be safe to assume
  // it already contains the right path, so there is no need for another `getcwd`
  dprintf(fd_out, "%s\n", CWD);
  return EXIT_SUCCESS;
}

int cmd_cd(int argc, char **argv, int fd_in, int fd_out, int fd_err) {
  int ret = 0;

  char *dir;
  if(argc == 1) {
    if (HOME == NULL) {
      dprintf(fd_err, "cd: HOME not set\n");
      return EXIT_FAILURE;
    }
    dir = HOME;
  }
  else if(strcmp(argv[1], "-") == 0) dir = PREV_WORKING_DIR;
  else dir = argv[1];

  ret = chdir(dir);
  if(ret == -1) {
    dperror(fd_err, "cd");
    return EXIT_FAILURE;
  }

  strcpy(PREV_WORKING_DIR, CWD);
  if (getcwd(CWD, PATH_MAX) == NULL) {
    // If we ever meet this condition, it means something very wrong has happened, or the directory has been altered at
    // the wrong moment.

    // FIXME: decide what to do in this situation. Maybe try to revert to the previous directory, and if that fails too, give up and exit the shell.
    dperror(fd_err, "cd: getcwd");
    exit(EXIT_FAILURE);
  }

  return EXIT_SUCCESS;
}

// Prints the type of the file passed in argument
int cmd_ftype(int argc, char **argv, int fd_in, int fd_out, int fd_err) {
  if(argc < 2) return EXIT_FAILURE;
  struct stat sb;
  if (lstat(argv[1], &sb) == -1) {
    dperror(STDERR_FILENO, "ftype");
    return EXIT_FAILURE;
  }

  switch(sb.st_mode & __S_IFMT) {
    case S_IFREG:
      dprintf(fd_out, "regular file\n");
      break;
    case S_IFDIR:
      dprintf(fd_out, "directory\n");
      break;
    case S_IFLNK:
      dprintf(fd_out, "symbolic link\n");
      break;
    case S_IFIFO:
      dprintf(fd_out, "named pipe\n");
      break;
    default:
      dprintf(fd_out, "other\n");
  }

  return EXIT_SUCCESS;
}

// Exits fsh, using the code passed in argument or the previous command return code
int cmd_exit(int argc, char **argv) {
  int val;
  if (argc <= 1)
    val = PREV_RETURN_VALUE;
  else if (sscanf(argv[1], "%d", &val) == 0)
    return EXIT_FAILURE;
  exit(val);
}

// Debug command, useful to debug I/O. For every char it receives in stdin, slowly repeat it twice on stdout and add a new line
int cmd_autotune(int argc, char **argv, int fd_in, int fd_out, int fd_err) {
  size_t ret = 0;
  char c;
  while ((ret = read(fd_in, &c, 1)) != 0) {
    if (ret == -1) {
      if (errno == EINTR) continue;
      dperror(STDERR_FILENO, "autotune: read");
      return EXIT_FAILURE;
    }
    if (c == '\n') continue;
    write(fd_out, &c, 1);
    usleep(200000);
    write(fd_out, &c, 1);
    usleep(200000);
    write(fd_out, "\n", 1);
  }
  return EXIT_SUCCESS;
}

// Debug command, simply returns the code passed in argument, or 1 by default
int cmd_oopsie(int argc, char **argv, int fd_in, int fd_out, int fd_err) {
  int val;
  if (argc < 2) {
    val = EXIT_FAILURE;
  } else {
    errno = 0;
    val = (int) strtol(argv[1], NULL, 10);
    if (errno) {
      dperror(fd_err, "exit");
      return EXIT_FAILURE;
    }
  }
  return val;
}

// Executes an external command, using execvp
int call_external_cmd(int argc, char **argv, int fd_in, int fd_out, int fd_err) {
  int wstat;
  char *cmd = argv[0];
  int pid;
  switch ((pid = fork())) {
    case -1:
      dperror(fd_err, "fork");
    return EXIT_FAILURE;
    case 0:
      if (dup2(fd_in, STDIN_FILENO) == -1 || dup2(fd_out, STDOUT_FILENO) == -1 || dup2(fd_err, STDERR_FILENO) == -1) {
        dperror(fd_err, "dup2");
        exit(EXIT_FAILURE);
      }

      execvp(cmd, argv);
      dperror(fd_err, "execvp");
      // We are in the child process, so we have to immediately exit if something goes wrong, otherwise there will be
      // an additional child `fsh` process every time we enter a non-existent command
      exit(EXIT_FAILURE);
    default:
      if (waitpid(pid, &wstat, 0) == -1) {
        perror("waitpid");
        return EXIT_FAILURE;
      }
      if (WIFEXITED(wstat)) {
        return WEXITSTATUS(wstat);
      }
      if (WIFSIGNALED(wstat)) {
        // We want fsh to exit with exit code 255 if we exit right after a process dies because of a signal.
        // However, we would still like to differentiate inside fsh if the previous process died because of a signal
        // or if it simply returned 255. So we use return code -1 to indicate a death by signal. Because exit codes are
        // encoded on 8 bits, it will automatically be converted to 255 when exiting !
        return -1;
      }
      return EXIT_FAILURE;
  }
}

// Runs a command (internal or external) and wait for it to finish
int call_command_and_wait(int argc, char **argv, int fd_in, int fd_out, int fd_err) {
  cmd_func cmd_function;
  char *cmd = argv[0];
  if (strcmp(cmd, "ftype") == 0) {
    cmd_function = &cmd_ftype;
  } else if (strcmp(cmd, "exit") == 0) {
    cmd_function = &cmd_exit;
  } else if (strcmp(cmd, "cd") == 0) {
    cmd_function = &cmd_cd;
  } else if (strcmp(cmd, "pwd") == 0) {
    cmd_function = &cmd_pwd;
  } else if (strcmp(cmd, "autotune") == 0) {
    cmd_function = &cmd_autotune;
  } else if (strcmp(cmd, "oopsie") == 0) {
    cmd_function = &cmd_oopsie;
  }
  else cmd_function = &call_external_cmd;

  int res = cmd_function(argc, argv, fd_in, fd_out, fd_err);
  return res;
}