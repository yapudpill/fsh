#include <execution.h>

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <commands.h>
#include <fsh.h>

// Returns a file descriptor to redirect a command output to a file,
// using options matching the characteristics of the redirection.
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

  int fd = open(file_name, oflags, 0644);
  if (fd == -1) {
    perror("open");
  }
  return fd;
}

// Returns a file descriptor to redirect a file to a command input
int setup_in_redir(char *name) {
  int fd = open(name, O_RDONLY);
  if (fd == -1) {
    perror("open");
  }
  return fd;
}

// Create a new string if necessary where anything of the form `$(character)` is
// replaced with var[character]. Returns NULL if the string passed is NULL or on
// error. Returns the same pointer if no possible substitutions are found.
char *inject_dependencies(char *dependent_str, char **vars) {
  if (dependent_str == NULL) return NULL;

  int size = strlen(dependent_str), changed = 0;
  char *cur, *tmp;
  for(cur = strchr(dependent_str, '$'); cur; cur = strchr(cur, '$')) {
    cur++;
    tmp = vars[(int) *cur];
    if(tmp == NULL) continue; // if the var is unset
    if(!changed) changed = 1;
    size += strlen(tmp) - 2; // remove two chars representing $F, add the length of the actual value of the var
  }
  if(!changed) return dependent_str;

  char *res = malloc(size + 1);
  if(res == NULL) return NULL;

  int j = 0;
  for(cur = dependent_str; *cur; cur++) {
    if(*cur != '$' || !vars[(int) *(cur+1)]) {
      res[j] = *cur;
      j++;
    } else {
      tmp = vars[(int) *(cur+1)];
      size = strlen(tmp);
      strncpy(res+j, tmp, size);
      j += size;
      cur++;
    }
  }

  res[j] = '\0';

  return res;
}

// Create a new argv where every variable is replaced by its value.
// Returns NULL on error.
char **inject_arg_dependencies(int argc, char **argv, char **vars) {
  char **res_argv = malloc((argc + 1) * sizeof(char *));
  if (res_argv == NULL) return NULL;

  for (int i = 0 ; i < argc ; i++) {
    res_argv[i] = inject_dependencies(argv[i], vars);
    if (res_argv[i] == NULL) return NULL;
  }

  res_argv[argc] = NULL;

  return res_argv;
}

int same_type(char filter_type, char file_type) {
  switch (filter_type) {
    case 'f': return file_type == DT_REG;
    case 'd': return file_type == DT_DIR;
    case 'l': return file_type == DT_LNK;
    case 'p': return file_type == DT_FIFO;
    default : return 0;
  }
}


int exec_for_cmd(struct cmd_for *cmd_for, char **vars) {
  char *dir_name = inject_dependencies(cmd_for->dir_name, vars);
  if (dir_name == NULL) return EXIT_FAILURE; // means allocation error
  int dir_len = strlen(dir_name);

  DIR *dirp = opendir(dir_name);
  if (dirp == NULL) {
    perror("opendir");
    return EXIT_FAILURE;
  }

  // save the original value to avoid nested for loops overwriting the original
  char *original_var_value = vars[(int) cmd_for->var_name];

  int ret = EXIT_SUCCESS, tmp, var_size;
  struct dirent *dentry;
  while ((dentry = readdir(dirp))) {
    if (strcmp(dentry->d_name, ".") == 0 || strcmp(dentry->d_name, "..") == 0) continue;
    if (!cmd_for->list_all && dentry->d_name[0] == '.') continue; // -A
    if (cmd_for->filter_type && !same_type(cmd_for->filter_type, dentry->d_type)) continue; // -t
    // TODO: -p -e -r

    var_size = dir_len + strlen(dentry->d_name) + 2;
    char var[var_size];
    snprintf(var, var_size, "%s/%s", dir_name, dentry->d_name);
    vars[(int) (cmd_for->var_name)] = var;

    tmp = exec_cmd_chain(cmd_for->body, vars);
    ret = (tmp > ret) ? tmp : ret; // take the max of all the return values
  }

  vars[(int) cmd_for->var_name] = original_var_value; // reestablish the old

  // make sure we don't free the original
  if(dir_name != cmd_for->dir_name) free(dir_name);
  closedir(dirp);

  return ret;
}

// Executes a simple command, i.e. either one external command or one internal
// command, opening files for redirections if necessary.
int exec_simple_cmd(struct cmd_simple *cmd_simple, char **vars) {
  char **injected_argv = inject_arg_dependencies(cmd_simple->argc, cmd_simple->argv, vars);
  if(injected_argv == NULL) return EXIT_FAILURE; // nothing to free

  int ret, i;
  char *redir_name[3] = { cmd_simple->in, cmd_simple->out, cmd_simple->err };

  // inject in redirections
  char *injected_redir[3] = { "", "", "" };
  for (i = 0; i < 3; i++) {
    if (redir_name[i]) {
      injected_redir[i] = inject_dependencies(redir_name[i], vars);
    }
    if (injected_redir[i] == NULL) {
      ret = EXIT_FAILURE;
      goto cleanup_injections;
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
  // Cleanup redirections if necessary
  for (i = 0; i < 3; i++) {
    if (redir[i] != -2) {
      close(redir[i]);
    }
  }

  cleanup_injections:
  for (i = 0;i < cmd_simple->argc; i++) {
    if (cmd_simple->argv[i] != injected_argv[i]) {
      free(injected_argv[i]);
    }
  }
  free(injected_argv);
  for (i = 0; i < 3; i++) {
    if (*injected_redir[i] != '\0' && injected_redir[i] != redir_name[i]) {
      free(injected_redir[i]);
    }
  }

  return ret;
}

// Executes an if/else statement
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


// Executes the first (and only the first) command in a command chain
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

// Executes a chain of commands, i.e. a pipeline or a structured command with semicolons
int exec_cmd_chain(struct cmd *cmd_chain, char **vars) {
  int ret, pipe_count, next_in, pid, i, p[2];
  struct cmd *tmp;

  while (cmd_chain) {
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
          close(p[0]);
          ret = exec_head_cmd(cmd_chain, vars);
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
      waitpid(pids[i], NULL, 0);
    }

    cmd_chain = cmd_chain->next;
  }

  return ret;
}
