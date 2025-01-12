#include "execution.h"

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "commands.h"
#include "fsh.h"

// Number of currently launched parallel loops
int nb_parallel = 0;

/**
 * Function to be executed by a subshell if it detects one of its executed
 * commands was terminated by SIGINT. Kills the subshell itself with SIGINT,
 * allowing the information that a subprocess has died of SIGINT to be
 * forwarded to the parent.
 */
void raise_sigint() {
  struct sigaction sa = { 0 };
  sa.sa_handler = SIG_DFL;
  if (sigaction(SIGINT, &sa, NULL) != EXIT_SUCCESS) exit(EXIT_FAILURE);
  raise(SIGINT);
}

/**
 * Returns the maximum of two integers, unless one of them is negative. If either
 * integer is negative, the negative value is returned.
 */
int max_or_neg(int a, int b) {
  if (a < 0) return a;
  if (b < 0) return b;
  return a > b ? a : b;
}

/**
 * Sets up output redirection for a given file, based on the specified type of
 * redirection.
 *
 * @param file_name The name of the file to redirect the output to.
 * @param type A `enum redir_type` value that determines the type of redirection.
 *             Possible values are:
 *               - `REDIR_NORMAL`: Creates the file if it does not exist. Fails
 *                 if the file already exists.
 *               - `REDIR_APPEND`: Appends output to the file if it exists.
 *                 Creates the file otherwise.
 *               - `REDIR_OVERWRITE`: Truncates the file if it exists. Creates
 *                 the file otherwise.
 *               - `REDIR_NONE`: Invalid value, should not be used.
 *
 * @return The file descriptor of the opened file on success, -1 on failure.
 *
 * @note The file is opened in write mode (`O_WRONLY`) and allows read/write
 *       access for the user, group, and others (file permissions 0666).
 * @note It is the caller's responsibility to ensure the `file_name` is valid
 *       and to close the file descriptor returned.
 */
int setup_out_redir(char *file_name, enum redir_type type) {
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
      /* This should happen only if setup_out_dir was called without checking if
       * a command has a redirection (a bad idea). Or if the parsing was
       * incorrect and a command is malformed (i.e. has a filename to redirect
       * to but empty redirection type) */
      dprintf(2, "fsh: internal error (file redirection is none)");
      return -1;
  }

  int fd = open(file_name, oflags, 0666);
  if (fd == -1) {
    perror("open");
  }
  return fd;
}


/**
 * Opens a file for input redirection.
 *
 * @param name The name of the file to be opened.
 *
 * @return The file descriptor of the opened file on success, -1 on failure.
 *
 * @note The file is opened in read-only mode (`O_RDONLY`).
 * @note It is the caller's responsibility to ensure the file name is valid and
 *       to close the file descriptor returned.
 */
int setup_in_redir(char *name) {
  int fd = open(name, O_RDONLY);
  if (fd == -1) {
    perror("open");
  }
  return fd;
}


/**
 * Replaces occurrences of variables in the form `$F` (where F is a character)
 * in a given string with their corresponding values from an array of
 * variables. If no replacements are needed, returns the original string.
 * Otherwise, allocates a new string.
 *
 * @param dependent_str The input string containing potential variable references.
 * @param vars An array of strings, where `vars[F]` provides the value for
 *             the variable referenced by `F`. Unset variables are denoted by
 *             NULL.
 *
 * @return A pointer to the modified string with variables replaced, or NULL on
 *         error. Returns the same pointer as `dependent_str` if no replacements
 *         are needed.
 *
 * @note The caller is responsible for freeing the returned string if it is not
 *       the same as the input string.
 */
char *replace_variables(char *dependent_str, char **vars) {
  if (dependent_str == NULL) return NULL;

  int size = strlen(dependent_str); // will contain the length of the final string
  int changed = 0; // whether the string is going to be changed (we will need a malloc)
  char *cur, *var_value;
  for (cur = strchr(dependent_str, '$'); cur; cur = strchr(cur, '$')) {
    // Go to the next character after the $ (i.e. the variable name).
    // No risk of accessing invalid memory, as strings are null-terminated
    cur++;

    var_value = vars[(int) *cur];
    if (var_value == NULL) continue; // If the var is unset, we won't alter the string
    if (!changed) changed = 1;
    size += strlen(var_value) - 2; // Remove two chars representing `$F`, and add the length of the value of the variable
  }

  if (!changed) return dependent_str; // If no change is needed, just return the string

  char *res = malloc(size + 1);
  if (res == NULL) return NULL;

  int j = 0;
  int var_size;
  for (cur = dependent_str; *cur; cur++) {
    if (*cur != '$' || !vars[(int) *(cur+1)]) { // No substitution needed.
      res[j] = *cur;
      j++;
    } else {
      var_value = vars[(int) *(cur+1)];
      var_size = strlen(var_value);
      strncpy(res+j, var_value, var_size);
      j += var_size;
      cur++;
    }
  }

  res[j] = '\0';

  return res;
}


/**
 * Creates a new argument vector (argv) where every variable of the form
 * `$C` in each argument is replaced with its corresponding value
 * from the provided variable array.
 *
 * @param argc The number of arguments in the `argv` array.
 * @param argv The input argument vector. Each argument may contain variables
 *             to be replaced.
 * @param vars An array of strings, where `vars[F]` provides the value for
 *             the variable referenced by `F`. Unset variables are denoted by
 *             NULL.
 *
 * @return A pointer to the newly allocated argument vector with variables
 *         replaced, or NULL on error. The returned vector is NULL-terminated.
 *
 * @note The caller is responsible for freeing the memory allocated for the
 *       returned argument vector and its individual arguments.
 *
 * @warning If `replace_variables` fails for any argument, this function will
 *          return NULL.
 */
char **replace_arg_variables(int argc, char **argv, char **vars) {
  char **res_argv = malloc((argc + 1) * sizeof(char *));
  if (res_argv == NULL) return NULL;

  for (int i = 0 ; i < argc ; i++) {
    res_argv[i] = replace_variables(argv[i], vars);
    if (res_argv[i] == NULL) { // Variable substitution failed
      for (int j=0; j < i; j++) free(res_argv[j]); // Free the previous arguments
      return NULL;
    }
  }

  res_argv[argc] = NULL;

  return res_argv;
}


/**
 * Waits for a child process to finish and returns its exit status. If the child
 * process terminates due to a signal, the function returns -1. Otherwise, it returns
 * the exit code of the child process.
 *
 * @param pid The process ID of the child process to wait for.
 *
 * @return The exit status of the child process. Returns -1 if the process was
 *         terminated by a signal, or its exit code if the process exited
 *         normally. If `waitpid` fails, `256` is returned (a value out of the
 *         range [0; 255] used for return codes, and different from -1, used when
 *         terminated by signal)
 */
int wait_cmd(int pid) {
  int wstat, ret;

  do {
    ret = waitpid(pid, &wstat, 0);
  } while ( ret == -1 && errno == EINTR); // interruption of wait can lead to problems in parallel execution and much more

  if (ret == -1) {
    if(!sig_received) perror("waitpid");
    return 256;  // to differentiate between error and actual return value
  }

  // We want fsh to exit with exit code 255 after a process dies because of a
  // signal. However, we would still like to differentiate inside fsh if the
  // previous process died because of a signal or if it simply returned 255.
  // So we use return code -1 to indicate a death by signal.
  // Because exit codes are encoded on 8 bits, it will automatically be
  // converted to 255 when exiting !
  if(WIFSIGNALED(wstat)) sig_received = WTERMSIG(wstat);
  return WIFEXITED(wstat) ? WEXITSTATUS(wstat) : -1;
}


/**
 * Checks if the file type matches the specified filter type.
 *
 * @param filter_type The type of filter to apply. This should be one of the
 *                    following characters:
 *                    - 'f': Regular file
 *                    - 'd': Directory
 *                    - 'l': Symbolic link
 *                    - 'p': FIFO (named pipe)
 * @param file_type The actual type of the file to check against, represented
 *                  by one of the `DT_*` constants (e.g., `DT_REG`, `DT_DIR`).
 *
 * @return 1 if the file type matches the filter type, otherwise 0.
 */
int same_type(char filter_type, char file_type) {
  switch (filter_type) {
    case 'f': return file_type == DT_REG;
    case 'd': return file_type == DT_DIR;
    case 'l': return file_type == DT_LNK;
    case 'p': return file_type == DT_FIFO;
    default : return 0;
  }
}


/**
 * Tries to spawn a command in parallel, respecting a limit on the maximum
 * number of parallel processes. If the limit is reached, it waits for one of
 * the previously launched processes to finish before starting the new one.
 *
 * @param cmd The command to execute
 * @param vars An array of variables that can be used by the command being executed.
 * @param max The maximum number of parallel processes allowed.
 *
 * @return The return value of the last spawned parallel command.
 */
int exec_parallel(struct cmd *cmd, char **vars, int max) {
  int ret = 0;

  if (nb_parallel == max) {
    ret = wait_cmd(-1);
    if(ret == 256) return EXIT_FAILURE;
    nb_parallel--;
  }

  switch (fork()) {
    case -1:
      perror("fork");
      return EXIT_FAILURE;
    case 0:
      ret = exec_cmd_chain(cmd, vars);
      if (sig_received == SIGINT) raise_sigint();
      exit(ret);
    default:
      nb_parallel++;
  }

  return ret;
}


/**
 * Executes a command for each file in a directory, with optional filters and
 * parallel execution. Supports recursion, file type filtering, and extension
 * filtering.
 *
 * As it may return even when parallel processes are still running, this
 * function should not be used directly. Use exec_for_cmd instead.
 *
 * @param cmd_for The `struct cmd_for` containing the command details and options.
 * @param vars An array of variables usable by the commands and the for loop
 *             itself. Modified during execution to store the new variable of
 *             the current loop
 *
 * @return The highest return value from executing the command on each file. Returns
 *         `EXIT_FAILURE` on error.
 *
 * @note The function modifies the `vars` array temporarily and restores it afterward.
 */
int exec_for_aux(struct cmd_for *cmd_for, char **vars) {
  // substitute the variables in the for loop argument
  char *dir_name = replace_variables(cmd_for->dir_name, vars);
  if (dir_name == NULL) return EXIT_FAILURE; // means allocation error
  int dir_len = strlen(dir_name);

  DIR *dirp = opendir(dir_name);
  if (dirp == NULL) {
    perror("opendir");
    return EXIT_FAILURE;
  }

  // save the original value to avoid nested for loops overwriting the original
  char *original_var_value = vars[(int) cmd_for->var_name];

  int ret = 0, tmp_ret, file_len, var_size;
  struct dirent *dentry;
  while ((dentry = readdir(dirp)) && sig_received != SIGINT) {
    if (strcmp(dentry->d_name, ".") == 0 || strcmp(dentry->d_name, "..") == 0)
      continue;
    if (!cmd_for->list_all && dentry->d_name[0] == '.') // -A
      continue;

    // make the variable
    file_len = strlen(dentry->d_name);
    var_size = dir_len + file_len + 2;
    char var[var_size];
    snprintf(var, var_size, "%s/%s", dir_name, dentry->d_name);
    vars[(int) (cmd_for->var_name)] = var;

    if (cmd_for->recursive && dentry->d_type == DT_DIR) { // -r
      char *old_dir = cmd_for->dir_name;
      cmd_for->dir_name = var;
      tmp_ret = exec_for_aux(cmd_for, vars);
      ret = max_or_neg(ret, tmp_ret);
      cmd_for->dir_name = old_dir;
    }

    if(sig_received == SIGINT) break; // shouldn't move on to executing the body on the directory if the recursion was interrupted

    if (cmd_for->filter_ext) { // -e
      int ext_len = strlen(cmd_for->filter_ext);
      if (ext_len >= file_len) continue; // too big to be an extension
      char *ext_start = var + var_size - ext_len - 2;
      if (*ext_start != '.' || strcmp(ext_start + 1, cmd_for->filter_ext) != 0)
        continue;
      *ext_start = '\0';
    }

    if (cmd_for->filter_type && !same_type(cmd_for->filter_type, dentry->d_type)) // -t
      continue;

    if (cmd_for->parallel) { // -p
      tmp_ret = exec_parallel(cmd_for->body, vars, cmd_for->parallel);
    } else {
      tmp_ret = exec_cmd_chain(cmd_for->body, vars);
    }
    ret = max_or_neg(ret, tmp_ret);
  }

  vars[(int) cmd_for->var_name] = original_var_value; // restore the old variable

  // make sure we don't free the original (see the doc of replace_variables)
  if (dir_name != cmd_for->dir_name) free(dir_name);
  closedir(dirp);

  if (sig_received == SIGINT) return -1;
  return ret;
}


/**
 * Executes a `for` loop command, ensuring all parallel processes are complete
 * before returning.
 *
 * @param cmd_for The `struct cmd_for` containing the loop command and options.
 * @param vars An array of variables usable by the command, and modified during
 *             execution
 *
 * @return The highest return value from the loop executions.
 *
 * @note This function ensures that `nb_parallel` is 0 at the end of execution.
 */
int exec_for_cmd(struct cmd_for *cmd_for, char **vars) {
  int ret, tmp_ret;

  ret = exec_for_aux(cmd_for, vars);

  if (cmd_for->parallel) { // clean remaining parallel loops
    while (nb_parallel) {
      tmp_ret = wait_cmd(-1);
      if(ret == 256) return EXIT_FAILURE;
      ret = max_or_neg(ret, tmp_ret);
      nb_parallel--;
    }
  }

  return ret;
}


/**
 * Executes a simple command (external or internal), which may involve
 * redirections for stdin, stdout, and stderr. Will open files for
 * redirections if necessary.
 *
 * @param cmd_simple The `struct cmd_simple` containing the command and
 *                   its arguments, along with redirection information.
 * @param vars An array of variables usable by the command.
 *
 * @return The return code from the command execution. Returns `EXIT_FAILURE`
 *         if any error occurs.
 *
 * @note This function already handles cleanup of allocated memory and file
 *       descriptors.
 */
int exec_simple_cmd(struct cmd_simple *cmd_simple, char **vars) {
  // inject the variables in the argv
  char **injected_argv = replace_arg_variables(cmd_simple->argc, cmd_simple->argv, vars);
  if (injected_argv == NULL) return EXIT_FAILURE; // nothing to free

  int ret, i;
  char *redir_name[3] = { cmd_simple->in, cmd_simple->out, cmd_simple->err };

  // inject the variables in the redirections file names
  char *injected_redir[3] = {0};
  for (i = 0; i < 3; i++) {
    if (redir_name[i]) {
      injected_redir[i] = replace_variables(redir_name[i], vars); // will be free'd at the end
      if (injected_redir[i] == NULL) {
        ret = EXIT_FAILURE;
        goto cleanup_injections;
      }
    }
  }

  // Setup redirections if necessary
  int redir[3] = { -2, -2, -2 };
  if (redir_name[0]) redir[0] = setup_in_redir(injected_redir[0]);
  if (redir_name[1]) redir[1] = setup_out_redir(injected_redir[1], cmd_simple->out_type);
  if (redir_name[2]) redir[2] = setup_out_redir(injected_redir[2], cmd_simple->err_type);
  for (i = 0; i < 3; i++) {
    if (redir[i] == -1) {
      ret = EXIT_FAILURE;
      goto cleanup_fd;
    }
  }

  ret = call_command_and_wait(cmd_simple->argc, injected_argv, redir);

  cleanup_fd:
  // Cleanup redirections file descriptors if necessary
  for (i = 0; i < 3; i++) {
    if (redir[i] >= 0) {
      close(redir[i]);
    }
  }

  cleanup_injections:
  for (i = 0; i < cmd_simple->argc; i++) {
    if (cmd_simple->argv[i] != injected_argv[i]) {
      free(injected_argv[i]);
    }
  }
  free(injected_argv);
  for (i = 0; i < 3; i++) {
    if (injected_redir[i] && injected_redir[i] != redir_name[i]) {
      free(injected_redir[i]);
    }
  }

  return ret;
}


/**
 * Executes an if/else statement based on the result of a test command. If the
 * test command succeeds, the "then" command is executed; otherwise, the "else"
 * command (if present) is executed.
 *
 * @param cmd_if_else The `struct cmd_if_else` containing the "test", "then",
 *                    and "else" commands.
 * @param vars The variable array used by the commands.
 *
 * @return The return code from the executed "then"/"else" command, or
 *         `EXIT_SUCCESS` if the test command fails and there is no "else"
 *         command.
 */
int exec_if_else_cmd(struct cmd_if_else *cmd_if_else, char **vars) {
  // default return value, in case the test fails and there is no "else" command
  int ret = EXIT_SUCCESS;

  int test_ret = exec_cmd_chain(cmd_if_else->cmd_test, vars);

  if (test_ret == EXIT_SUCCESS) {
    // Test succeeded
    ret = exec_cmd_chain(cmd_if_else->cmd_then, vars);
  } else if (cmd_if_else->cmd_else != NULL) {
    // Test failed and there is an "else" command in the statement
    ret = exec_cmd_chain(cmd_if_else->cmd_else, vars);
  }

  return ret;
}


/**
 * Executes the first, and only first command in a command chain
 *
 * @param cmd_chain The `struct cmd` representing the command chain.
 * @param vars An array of variables usable by the command.
 *
 * @return The return code from executing the first command in the chain.
 *         If the command type is not implemented, `EXIT_FAILURE` is returned.
 */
int exec_head_cmd(struct cmd *cmd_chain, char **vars) {
  switch (cmd_chain->cmd_type) {
    case CMD_EMPTY:
      return PREV_RETURN_VALUE;
    case CMD_SIMPLE:
      return exec_simple_cmd(cmd_chain->detail, vars);
    case CMD_IF_ELSE:
      return exec_if_else_cmd(cmd_chain->detail, vars);
    case CMD_FOR:
      return exec_for_cmd(cmd_chain->detail, vars);
    default:
      dprintf(2, "fsh: Not implemented\n");
      return EXIT_FAILURE;
  }
}


/**
 * Executes a chain of commands, i.e. pipelines and commands separated by
 * `;`. It processes commands in parallel (with subshells) when a pipeline
 * is encountered, but always executes the last command in the chain in the
 * current process.
 *
 * @param cmd_chain The `cmd` structure representing the command chain.
 * @param vars The variable array used by the commands.
 *
 * @return The return code from the last executed command in the chain. Returns
 *         `EXIT_FAILURE` if any error occurs during command execution or pipeline setup.
 */
int exec_cmd_chain(struct cmd *cmd_chain, char **vars) {
  int ret = 0, pipe_count, next_in, pid, i, p[2];
  struct cmd *tmp;

  while (cmd_chain && sig_received != SIGINT) {
    // count number of pipes
    pipe_count = 0;
    tmp = cmd_chain;
    while (tmp->next_type == NEXT_PIPE) {
      pipe_count++;
      tmp = tmp->next;
    }

    // exec in parallel everything that outputs into a pipe
    next_in = dup(0);
    int pids[pipe_count];
    for (i = 0; i < pipe_count; i++) {
      if (pipe(p) == -1) {
        perror("pipe");
        return EXIT_FAILURE;
      }
      switch (pid = fork()) {
        case -1:
          perror("fork");
          return EXIT_FAILURE;
        case 0:
          dup2(next_in, 0);
          dup2(p[1], 1);
          close(p[1]);
          close(p[0]);
          close(next_in);
          ret = exec_head_cmd(cmd_chain, vars);

          if (sig_received == SIGINT) raise_sigint();
          exit(ret);
        default:
          pids[i] = pid;
          close(p[1]);
          close(next_in);
          next_in = p[0];
          cmd_chain = cmd_chain->next;
      }
    }

    // exec the last command of the pipeline in the fsh process itself
    int in_save = dup(0);
    dup2(next_in, 0);
    close(next_in);
    ret = exec_head_cmd(cmd_chain, vars);
    dup2(in_save, 0);
    close(in_save);

    // wait of all commands of the pipeline to finish
    for (int i = 0; i < pipe_count; i++) {
      if(wait_cmd(pids[i]) == 256) return EXIT_FAILURE;
    }

    cmd_chain = cmd_chain->next;
  }

  return (sig_received == SIGINT) ? -1 : ret;
}
