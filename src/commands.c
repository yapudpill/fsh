#include <commands.h>
#include <fsh.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>


int pwd() {
  char *cwd = getcwd(NULL, 0);
  if(cwd == NULL) {
    perror("pwd-getcwd");
    return EXIT_FAILURE;
  }
  printf("%s\n", cwd);
  return EXIT_SUCCESS;
}

int cd(char *arg) {
  int ret = 0;

  if(arg == NULL) {
    if (HOME == NULL) {
      fprintf(stderr, "fsh: cd: HOME not set\n");
      return EXIT_FAILURE;
    }
    ret = chdir(HOME);
  }
  else if(strcmp(arg, "-") == 0) ret = chdir(PREV_WORKING_DIR);
  else ret = chdir(arg);

  if(ret == -1) {
    perror("chdir");
    return EXIT_FAILURE;
  }

  strcpy(PREV_WORKING_DIR, CWD);
  strcpy(CWD, getcwd(NULL, 0));

  return EXIT_SUCCESS;
}

int ftype(char *arg) {
  if(arg == NULL) return EXIT_FAILURE;
  struct stat sb;
  if(stat(arg, &sb) == -1) {
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

int exec_internal_cmd(char *cmd, char *arg) {
  if(cmd == NULL) goto error;
  if(strcmp(cmd, "ftype") == 0) {
    if(ftype(arg) == EXIT_FAILURE) goto error;
  } else if (strcmp(cmd, "exit") == 0) {
    int val;
    if(arg == NULL || sscanf(arg, "%d", &val) == 0) goto error;
    exit(val);
  } else if (strcmp(cmd, "cd") == 0) {
    if(cd(arg) == EXIT_FAILURE) goto error;
  } else if (strcmp(cmd, "pwd") == 0) {
    if(pwd() == EXIT_FAILURE) goto error;
  } else goto error;

  return EXIT_SUCCESS;
  error:
  fprintf(stderr, "Internal command error\n");
  return EXIT_FAILURE;
}

int exec_external_cmd(char *cmd, char **argv) {
  char *err;
  int r, wstat;
  if(cmd == NULL) return EXIT_FAILURE;
  switch(r = fork()) {
    case -1:
      err = "fork";
      goto error;
    case 0:
      if(execvp(cmd, argv) == -1){
        err = "execvp";
        goto error;
      }
      break;
    default:
      if(wait(&wstat) == -1) { // FIXME: We should probably use wstat, right ?
        err = "wait";
        goto error;
      }
      break;
  }

  return EXIT_SUCCESS;

  error:
  perror(err);
  return EXIT_FAILURE;
}


int exec_simple_cmd(char *input) { // FIXME: Make this support external commands as well
  char *cmd, *arg;
  cmd = strtok(input, " \n");
  arg = strtok(NULL, " \n");
  return exec_internal_cmd(cmd, arg);
}