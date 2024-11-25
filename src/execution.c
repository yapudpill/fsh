#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <fcntl.h>
#include <sys/wait.h>

#include "execution.h"
#include "utils.h"
#include "commands.h"

// Returns a file descriptor to redirect a command output to a file, using options matching the characteristics of
// the redirection.
// Uses fd_err to know where to log errors.
int setup_out_redir(char *file_name, enum redir_type type, int fd_err) {
    int oflags = O_WRONLY | O_CREAT;
    switch (type) {
      case REDIR_NORMAL:
        oflags |= O_EXCL;
        break;
      case REDIR_APPEND:
        oflags |= O_APPEND;
        break;
      case REDIR_OVERWRITE:
        oflags |= O_TRUNC;
        break;
      case REDIR_NONE:
        // This should happen only if setup_out_dir was called without checking if a command has a
        // redirection (a bad idea). Or if the parsing was incorrect and a command is malformed
        // (i.e. has a filename to redirect to but empty redirection type)
        dprintf(fd_err, "fsh: internal error (file redirection is none)");
        return -1;
    }

    int fd = open(file_name, oflags, 0644);
    if (fd == -1) {
      dperror(fd_err, "open");
    }
    return fd;
}

// Returns a file descriptor to redirect a file to a command input
// Uses fd_err to know where to log errors.
int setup_in_redir(char *name, int fd_err) {
  int fd = open(name, O_RDONLY);
  if (fd == -1) {
    dperror(fd_err, "open");
  }
  return fd;
}

char *inject_dependencies(char *dependent_str, char **vars) {
  if (dependent_str == NULL) return NULL;
  // figure out the size of the new string and if there are no dependencies return the same pointer
  int size = strlen(dependent_str), changed = 0;
  char *cur = dependent_str, *res, *tmp;
  for(;(cur = strchr(cur, '$')); cur++) {
    tmp = vars[(int) cur[1]];
    if(tmp == NULL) continue;
    if(!changed) changed = 1;
    size += strlen(tmp) - 2;
  }
  if(!changed) return dependent_str;
  // allocate memory for the string
  res = calloc(size+1, sizeof(char));
  if(res == NULL) return NULL;
  // build the string
  cur = dependent_str;
  int i, j;
  for(i = 0, j = 0; cur[i] ;) {
    if(cur[i] != '$' || (vars[(int) cur[i+1]] == NULL)) res[j] = cur[i], i++, j++;
    else {
      tmp = vars[(int) cur[i+1]];
      size = strlen(tmp);
      strncpy(res+j, tmp, size);
      i+=2, j+=size;
    }
  }
  res[j] = '\0';

  return res;
}


int exec_for_cmd(struct cmd_for *cmd_for, int fd_in, int fd_out, int fd_err, char **vars) {
  // TODO
  return 1;
}

// Executes a simple command, i.e. either one external command or one internal command, opening files for redirections
// if necessary.
int exec_simple_cmd(struct cmd_simple *cmd_simple, int fd_in, int fd_out, int fd_err, char **vars) {
  int ret;
  char **injected_argv = NULL, *injected_in = NULL, *injected_out = NULL, *injected_err = NULL;

  injected_argv = inject_arg_dependencies(cmd_simple->argc, cmd_simple->argv, vars);

  // Setup redirections if necessary
  if (cmd_simple->in != NULL) fd_in = setup_in_redir(injected_in, fd_err);
  if (cmd_simple->out != NULL) fd_out = setup_out_redir(injected_out, cmd_simple->out_type, fd_err);
  if (cmd_simple->err != NULL) fd_err = setup_out_redir(injected_err, cmd_simple->err_type, fd_err);

  if (fd_in == -1 || fd_out == -1 || fd_err == -1) {
    ret = EXIT_FAILURE;
    goto cleanup_fd;
  }

  ret = call_command_and_wait(cmd_simple->argc, injected_argv, fd_in, fd_out, fd_err);

  cleanup_fd:
  // Cleanup redirections if necessary
  if (cmd_simple->in != NULL) close(fd_in);
  if (cmd_simple->out != NULL) close(fd_out);
  if (cmd_simple->err != NULL) close(fd_err);

  return ret;
}

// Executes an if/else statement
int exec_if_else_cmd(struct cmd_if_else *cmd_if_else, int fd_in, int fd_out, int fd_err, char **vars) {
  int ret = EXIT_SUCCESS; // Default return value, in case the test fails and there is no "else" command

  // Run the "test" command of the statement
  int test_ret = exec_cmd_chain(cmd_if_else->cmd_test, fd_in, fd_out, fd_err, vars);

  if (test_ret == EXIT_SUCCESS) { // Test succeeded
    ret = exec_cmd_chain(cmd_if_else->cmd_then, fd_in, fd_out, fd_err, vars); // Run the "then" command of the statement
  } else if (cmd_if_else->cmd_else != NULL) { // Test failed and there is an "else" command in the statement
    ret = exec_cmd_chain(cmd_if_else->cmd_else, fd_in, fd_out, fd_err, vars); // Run the "else" command of the statement
  }

  return ret;
}


// Executes the first (and only the first) command in a command chain
int exec_head_cmd(struct cmd *cmd_chain, int fd_in, int fd_out, int fd_err, char **vars) {
  switch (cmd_chain->cmd_type) {
    case CMD_EMPTY:
      return EXIT_SUCCESS;
    case CMD_SIMPLE:
      return exec_simple_cmd(cmd_chain->detail, fd_in, fd_out, fd_err, vars);
    case CMD_IF_ELSE:
      return exec_if_else_cmd(cmd_chain->detail, fd_in, fd_out, fd_err, vars);
    case CMD_FOR:
      return exec_for_cmd(cmd_chain->detail, fd_in, fd_out, fd_err, vars);
    default:
      dprintf(fd_err, "fsh: Not implemented\n");
      return EXIT_FAILURE;
  }
}

// Pipes the stdout of the head of cmd_chain into the stdin of the following command
int exec_piped_cmd(struct cmd *cmd_chain, int fd_in, int fd_out, int fd_err, char **vars) {
  int p[2]; // To store a pipe: { read_fd, write_fd }
  if (pipe(p) == -1) {
    perror("pipe");
    return EXIT_FAILURE;
  }

  // Fork ourselves, make the fork run the head of cmd_chain and output to the pipe. At the same time, continue
  // processing the next commands, but using the pipe as input
  int pid;
  int ret;
  switch (pid = fork()) {
    case -1:
      perror("fork");
      return EXIT_FAILURE;
    case 0:
      close(p[0]); // We are not going to use the read end of the pipe in the fork
      fd_out = p[1]; // Set the output to use the pipe

      ret = exec_head_cmd(cmd_chain, fd_in, fd_out, fd_err, vars);

      close(p[1]); // (not strictly necessary)
      exit(ret);
    default:
      close(p[1]); // We are not going to use the write end of the pipe in the parent
      fd_in = p[0]; // Set the input to use the pipe

      ret = exec_cmd_chain(cmd_chain->next, fd_in, fd_out, fd_err, vars);

      close(p[0]); // Necessary to make sure the head command gets SIGPIPE if the next commands stop first
      waitpid(pid, NULL, 0); // Remove the fork from process table
      return ret;
  }
}

// Executes a chain of commands, i.e. a pipeline or a structured command with semicolons
int exec_cmd_chain(struct cmd *cmd_chain, int fd_in, int fd_out, int fd_err, char **vars) {
    switch (cmd_chain->next_type) {
      case NEXT_NONE:
        return exec_head_cmd(cmd_chain, fd_in, fd_out, fd_err, vars);
      case NEXT_SEMICOLON:
        exec_head_cmd(cmd_chain, fd_in, fd_out, fd_err, vars);
        return exec_cmd_chain(cmd_chain->next, fd_in, fd_out, fd_err, vars);
      case NEXT_PIPE:
        return exec_piped_cmd(cmd_chain, fd_in, fd_out, fd_err, vars);
      default:
        fprintf(stderr, "fsh: Not implemented\n");
        return EXIT_FAILURE;
  }
}