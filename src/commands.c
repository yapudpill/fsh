#include <commands.h>
#include <fsh.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>


int cmd_pwd(int _argc, char **_argv) {
  // Considering that the variable CWD is always updated using `getcwd` at every cwd change, it should be safe to assume
  // it already contains the right path, so there is no need for another `getcwd`
  printf("%s\n", CWD);
  return EXIT_SUCCESS;
}

int cmd_cd(int argc, char **argv) {
  int ret = 0;

  if(argc == 1) {
    if (HOME == NULL) {
      fprintf(stderr, "fsh: cd: HOME not set\n");
      return EXIT_FAILURE;
    }
    ret = chdir(HOME);
  }
  else if(strcmp(argv[1], "-") == 0) ret = chdir(PREV_WORKING_DIR);
  else ret = chdir(argv[1]);

  if(ret == -1) {
    perror("chdir");
    return EXIT_FAILURE;
  }

  strcpy(PREV_WORKING_DIR, CWD);
  if (getcwd(CWD, PATH_MAX) == NULL) {
    // If we ever meet this condition, it means something very wrong has happened, or the directory has been altered at
    // the wrong moment.

    // FIXME: decide what to do in this situation. Maybe try to revert to the previous directory, and if that fails too, give up and exit the shell.
    perror("getcwd");
    exit(EXIT_FAILURE);
  }

  return EXIT_SUCCESS;
}

int cmd_ftype(int argc, char **argv) {
  if(argc < 2) return EXIT_FAILURE;
  struct stat sb;
  if(stat(argv[1], &sb) == -1) {
    perror("ftype-stat");
    return EXIT_FAILURE;
  }

  switch(sb.st_mode & __S_IFMT) {
    case __S_IFREG:
      printf("regular file\n");
      break;
    case __S_IFDIR:
      printf("directory\n");
      break;
    case __S_IFLNK:
      printf("symbolic link\n");
      break;
    case __S_IFIFO:
      printf("named pipe\n");
      break;
    default:
      printf("other\n");
  }

  return EXIT_SUCCESS;
}

int cmd_exit(int argc, char **argv) {
  int val;
  if (argc <= 1)
    val = 0;
  else if (sscanf(argv[1], "%d", &val) == 0)
    return EXIT_FAILURE;
  exit(val);
}

int exec_external_cmd(int _argc, char **argv) {
  int wstat;
  char *cmd = argv[0];
  switch (fork()) {
    case -1:
      perror("fork");
      return EXIT_FAILURE;
    case 0:
      if (execvp(cmd, argv) == -1) {
        perror("execvp");
        // We are in the child process, so we have to immediately exit if something goes wrong, otherwise there will be
        // an additional child `fsh` process every time we enter a non-existent command
        exit(EXIT_FAILURE);
      }
    default:
      if (wait(&wstat) == -1) {
        perror("wait");
        return EXIT_FAILURE;
      }
      if (WIFEXITED(wstat)) {
        return WEXITSTATUS(wstat);
      }
      if (WIFSIGNALED(wstat)) {
        int sig = WTERMSIG(wstat);
        printf("Process terminated by signal %d\n", sig);
        return 128 + sig; // Convention used by bash
      }
      return EXIT_FAILURE;
  }
}

int exec_cmd(int argc, char **argv) {
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
  } else cmd_function = exec_external_cmd;

  int res = cmd_function(argc, argv);

  return res;
}

int parse_and_exec_simple_cmd(char *input) { // TODO: use the syntax tree once we implement it instead of doing the parsing here
  if (input == NULL) exit(EXIT_SUCCESS);
  int argc = 1;
  char *first_token;
  first_token = strtok(input, " \n");
  if (first_token == NULL) {
    return EXIT_SUCCESS;
  }
  while (strtok(NULL, " \n") != NULL) {
    argc++;
  }
  char **argv = malloc(argc + 1);
  argv[0] = first_token;
  argv[argc] = NULL; // per the POSIX spec: The argv arrays are each terminated by a null pointer. The null pointer terminating the argv array is not counted in argc
  int i=1;
  for (char *p = input; i < argc; p++) {
    if (*p == '\0') {
      argv[i] = p + 1;
      i++;
    }
  }

  int res = exec_cmd(argc, argv);
  free(argv);
  return res;
}