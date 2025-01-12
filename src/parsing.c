#include "parsing.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cmd_types.h"
#include "fsh.h"

/* PARSING FUNCTIONS:
parse and free_cmd are the only exposed functions of this file, they are the
ones that must be called from the main loop.

parse_* functions except consider that `out` points to an empty already
allocated cmd that is ready to be filled.

After the execution of any parse_* function, `token` points to the next token to
be scanned, in particular, it is not be part of the command that have just been
scanned.

In case of error, -1 is propagated up to parse that calls free_cmd on the root
of the syntax tree. Because of this, parse_* functions must ensure that the
syntax tree remains valid even after an error.
*/

// Parses a command of unknown type (calls the other parse_* functions)
int parse_cmd(struct cmd *out);

// Parses a simple command with eventual redirections
int parse_simple(struct cmd *out);

// Parses a for loop
int parse_for(struct cmd *out);

// Parses an if-else construct
int parse_if_else(struct cmd *out);

// Save the error code, only if it is the first error encountered
void update_status(int error_code) {
  if (!parsing_errno)
    parsing_errno = error_code;
}

int parsing_errno;
char *token;

struct cmd *parse(char *line) {
  // create the root of the syntax tree
  parsing_errno = 0;
  struct cmd *root = calloc(1, sizeof(struct cmd));
  if (!root) return NULL;

  // load the line in strtok
  token = strtok(line, " ");

  int ret = parse_cmd(root);
  if (ret == -1 || token) {
    // if parse_cmd failed or if there is still a token to parse, then the
    // command was malformed
    if (ret != -1) {
      // Nothing bad happened during parsing but it stopped to early
      dprintf(2, "parsing: malformed command\n");
    }
    free_cmd(root);
    update_status(ERROR_SYNTAX);
    return NULL;
  }

  return root;
}

int parse_cmd(struct cmd *root) {
  while (token && strcmp(token, "{") && strcmp(token, "}")) {

    if (strcmp(token, "|") == 0 || strcmp(token, ";") == 0) {
      // if we see ; or |, we add a new command to the chained list of commands
      int pipe = (strcmp(token, "|") == 0);

      if ((pipe && root->cmd_type != CMD_SIMPLE) || root->cmd_type == CMD_EMPTY) return -1;

      // create and fill new root
      struct cmd *new_root = calloc(1, sizeof(struct cmd));
      if (!new_root) return -1;

      // link new root to the old one
      root->next_type = pipe ? NEXT_PIPE : NEXT_SEMICOLON;
      root->next = new_root;
      root = new_root;

      token = strtok(NULL, " ");

    } else if (root->cmd_type != CMD_EMPTY) {
        // everything except | and ; must be parsed on an empty root
        dprintf(2, "parsing: malformed command\n");
        return -1;

    } else if (strcmp(token, "for") == 0) {
      if (parse_for(root) == -1) return -1;

    } else if (strcmp(token, "if") == 0) {
      if (parse_if_else(root) == -1) return -1;

    } else {
      if (parse_simple(root) == -1) return -1;
    }
  }
  return 0;
}

int is_redir(char *token) {
  return (
    strcmp(token, "<") == 0 ||
    strcmp(token, ">") == 0 ||
    strcmp(token, ">>") == 0 ||
    strcmp(token, ">|") == 0 ||
    strcmp(token, "2>") == 0 ||
    strcmp(token, "2>>") == 0 ||
    strcmp(token, "2>|") == 0
  );
}

int is_simple_end(char *token) {
  return (
    strcmp(token, ";") == 0 ||
    strcmp(token, "|") == 0 ||
    strcmp(token, "{") == 0 ||
    strcmp(token, "}") == 0
  );
}

int parse_simple(struct cmd *out) {
  // create detail node and link it to the root
  struct cmd_simple *detail = calloc(1, sizeof(struct cmd_simple));
  if (!detail) return -1;
  out->cmd_type = CMD_SIMPLE;
  out->detail = detail;

  // count argc and make argv
  char *first_token = token;
  int argc = 0;
  while (token && !is_redir(token) && !is_simple_end(token)) {
    argc++;
    token = strtok(NULL, " ");
  }

  detail->argv = malloc((argc + 1) * sizeof(char *));
  if (!(detail->argv)) return -1;
  detail->argc = argc;

  detail->argv[0] = first_token;
  detail->argv[argc] = NULL;

  int i = 1;
  for (char *p = first_token; i < argc; p++) {
    if (*p == '\0') {
      do { p++; } while (*p == ' ');
      detail->argv[i] = p;
      i++;
    }
  }

  // parse redirections
  while (token && !is_simple_end(token)) {
    if (strcmp(token, "<") == 0) {
      detail->in = strtok(NULL, " ");
      if (!(detail->in)) {
        dprintf(2, "parsing: missing file name after <\n");
        return -1;
      }
    } else {
      char **name;
      enum redir_type *type;
      if (token[0] == '2') {
        name = &(detail->err);
        type = &(detail->err_type);
        token++;
      } else {
        name = &(detail->out);
        type = &(detail->out_type);
      }

      if (strcmp(token, ">") == 0) {
        *type = REDIR_NORMAL;
      } else if (strcmp(token, ">>") == 0) {
        *type = REDIR_APPEND;
      } else if (strcmp(token, ">|") == 0) {
        *type = REDIR_OVERWRITE;
      } else {
        dprintf(2, "parsing: unknown redirection symbol\n");
        return -1;
      }

      *name = strtok(NULL, " ");
      if (!*name) {
        dprintf(2, "parsing: missing file name after redirection\n");
        return -1;
      }
    }

    token = strtok(NULL, " ");
  }

  return 0;
}

// Parses a body surrounded by braces
struct cmd *parse_body() {
  // check that we have "{" before the body
  if (!token || strcmp(token, "{")) {
    dprintf(2, "parsing: missing { before body\n");
    return NULL;
  }
  token = strtok(NULL, " ");

  // alloc and parse the body
  struct cmd *body = calloc(1, sizeof(struct cmd));
  if (!body) return NULL;
  if (parse_cmd(body) == -1) {
    free(body);
    return NULL;
  }

  // check that we have "}" at the end of the body
  if (!token || strcmp(token, "}")) {
    dprintf(2, "parsing: missing } after body\n");
    free(body);
    return NULL;
  }
  token = strtok(NULL, " ");

  return body;
}

int is_ftype(char c) {
  return (
    c == 'f' ||
    c == 'd' ||
    c == 'l' ||
    c == 'p'
  );
}

int check_duplicate(struct cmd_for *detail, char *option) {
  long ptr;
  if (strcmp(token, "-A") == 0) {
    ptr = detail->list_all;
  } else if (strcmp(token, "-r") == 0) {
    ptr = detail->recursive;
  } else if (strcmp(token, "-e") == 0) {
    ptr = (long)(detail->filter_ext);
  } else if (strcmp(token, "-t") == 0) {
    ptr = detail->filter_type;
  } else if (strcmp(token, "-p") == 0) {
    ptr = detail->parallel;
  } else {
    dprintf(2, "parsing: unknown loop option %s\n", option);
    update_status(ERROR_FOR_ARG);
    return -1;
  }

  if (ptr) {
    dprintf(2, "parsing: duplicate for loop option %s\n", option);
    update_status(ERROR_FOR_ARG);
    return -1;
  }
  return 0;
}

int parse_for(struct cmd *out) {
  // create detail node and link it to the root
  struct cmd_for *detail = calloc(1, sizeof(struct cmd_for));
  if (!detail) return -1;
  out->cmd_type = CMD_FOR;
  out->detail = detail;

  // get variable name
  token = strtok(NULL, " ");
  if (!token) {
    dprintf(2, "parsing: missing variable name in for loop\n");
    return -1;
  } else if (token[0] == '\0' || token[1] != '\0') {
    dprintf(2, "parsing: variable name must be one character long\n");
    return -1;
  }
  detail->var_name = token[0];
  token = strtok(NULL, " ");

  // check that we have "in" after the variable
  if (!token || strcmp(token, "in")) {
    dprintf(2, "parsing: missing \"in\" in for loop\n");
    return -1;
  }

  // get directory name
  detail->dir_name = strtok(NULL, " ");
  if (!(detail->dir_name)) {
    dprintf(2, "parsing: missing directory name in for loop\n");
    return -1;
  }
  token = strtok(NULL, " ");

  // parse options
  while (token && strcmp(token, "{")) {
    if (check_duplicate(detail, token) == -1) return -1;
    if (strcmp(token, "-A") == 0) {
      detail->list_all = 1;
    } else if (strcmp(token, "-r") == 0) {
      detail->recursive = 1;
    } else if (strcmp(token, "-e") == 0) {
      detail->filter_ext = strtok(NULL, " ");
      if (!(detail->filter_ext)) {
        dprintf(2, "parsing: missing or invalid argument for loop option -e\n");
        update_status(ERROR_FOR_ARG);
        return -1;
      }
    } else if (strcmp(token, "-t") == 0) {
      token = strtok(NULL, " ");
      if (!token || strlen(token) != 1 || !is_ftype(token[0])) {
        dprintf(2, "parsing: missing or invalid argument for loop option -t\n");
        update_status(ERROR_FOR_ARG);
        return -1;
      }
      detail->filter_type = token[0];
    } else if (strcmp(token, "-p") == 0) {
      token = strtok(NULL, " ");
      if (!token || sscanf(token, "%d", &(detail->parallel)) != 1) {
        dprintf(2, "parsing: missing or invalid argument for loop option -p\n");
        update_status(ERROR_FOR_ARG);
        return -1;
      }
    }
    token = strtok(NULL, " ");
  }

  // parse the body
  detail->body = parse_body();
  if (!(detail->body)) return -1;

  return 0;
}

int parse_if_else(struct cmd *out) {
  // create detail node and link it to the root
  struct cmd_if_else *detail = calloc(1, sizeof(struct cmd_if_else));
  if (!detail) return -1;
  out->cmd_type = CMD_IF_ELSE;
  out->detail = detail;

  // parse and fill the test command
  token = strtok(NULL, " ");
  detail->cmd_test = calloc(1, sizeof(struct cmd));
  if (!(detail->cmd_test) || parse_cmd(detail->cmd_test) == -1) return -1;

  // parse the first body
  detail->cmd_then = parse_body();
  if (!(detail->cmd_then)) return -1;

  // check if there is an else branch, if no return normally
  if (!token || strcmp(token, "else")) return 0;
  token = strtok(NULL, " ");

  // parse the else body
  detail->cmd_else = parse_body();
  if (!(detail->cmd_else)) return -1;

  return 0;
}

void free_cmd(struct cmd *cmd) {
  switch (cmd->cmd_type) {
    case CMD_EMPTY:
      break;

    case CMD_SIMPLE:
      struct cmd_simple *simple = (struct cmd_simple *)(cmd->detail);
      if (simple->argv != NULL) free(simple->argv);
      free(simple);
      break;

    case CMD_IF_ELSE:
      struct cmd_if_else *if_else = (struct cmd_if_else *)(cmd->detail);
      if (if_else->cmd_test != NULL) free_cmd(if_else->cmd_test);
      if (if_else->cmd_then != NULL) free_cmd(if_else->cmd_then);
      if (if_else->cmd_else != NULL) free_cmd(if_else->cmd_else);
      free(if_else);
      break;

    case CMD_FOR:
      struct cmd_for *cmd_for = (struct cmd_for *)(cmd->detail);
      if (cmd_for->body != NULL) free_cmd(cmd_for->body);
      free(cmd_for);
      break;
  }

  if (cmd->next_type != NEXT_NONE) free_cmd(cmd->next);
  free(cmd);
  return;
}
