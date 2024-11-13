#include <parsing.h>

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <cmd_types.h>

/* PARSING FUNCTIONS:
parse and free_cmd are the only exposed functions of this file, they are the
ones that must be called from the main loop.

parse_* functions except parse_semicolon and parse_pipe consider that `out`
points to an empty already allocated cmd that is ready to be filled.

parse_semicolon and parse_pipe considers that `out` already contains the first
command of the pair. They use insert_pair that modifies `out` so that is becomes
a pair containing the original `out` and an empty second field. They then fill
this second fiels.

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

// Parses a semicolon. Assumes `out' is cmd1 and make it `cmd1 ; (rest)'
int parse_semicolon(struct cmd *out);

// Parses a pipe. Assumes `out' is cmd1 and make it `(cmd1 | next_cmd)'
int parse_pipe(struct cmd *out);

int parse_for(struct cmd *out);
int parse_if_else(struct cmd *out);

char *token;

struct cmd *parse(char *line) {
  // create the root of the syntax tree
  struct cmd *root = calloc(1, sizeof(struct cmd));
  if (!root) return NULL;

  // load the line in strtok
  token = strtok(line, " ");

  if (parse_cmd(root) == -1) {
    free_cmd(root);
    return NULL;
  }
  return root;
}

int parse_cmd(struct cmd *out) {
  while (token && strcmp(token, "{") && strcmp(token, "}")) {
    if (strcmp(token, ";") == 0) {
      if (out->type == CMD_EMPTY || parse_semicolon(out) == -1) return -1;

    } else if (strcmp(token, "|") == 0) {
      if ((out->type != CMD_SIMPLE && out->type != CMD_PIPE) || parse_pipe(out) == -1) return -1;

    } else if (strcmp(token, "for") == 0) {
      // if (out->type != CMD_EMPTY || parse_for(out) == -1) return -1;
      return -1;

    } else if (strcmp(token, "if") == 0) {
      // if (out->type != CMD_EMPTY || parse_if_else(out) == -1) return -1;
      return -1;

    } else {
      if (out->type != CMD_EMPTY || parse_simple(out) == -1) return -1;
    }
  }
  return 0;
}

int is_redir(char *token) {
  return (
    strcmp(token, "<") == 0
    || strcmp(token, ">") == 0
    || strcmp(token, ">>") == 0
    || strcmp(token, ">|") == 0
    || strcmp(token, "2>") == 0
    || strcmp(token, "2>>") == 0
    || strcmp(token, "2>|") == 0
  );
}

int is_simple_end(char *token) {
  return (
    strcmp(token, ";") == 0
    || strcmp(token, "|") == 0
    || strcmp(token, "{") == 0
    || strcmp(token, "}") == 0
  );
}

int parse_simple(struct cmd *out) {
  if (!token) return -1;

  struct cmd_simple *detail = calloc(1, sizeof(struct cmd_simple));
  if (!detail) return -1;
  out->type = CMD_SIMPLE;
  out->detail = detail;

  char *first_token = token;
  detail->argc = 0;
  while (token && !is_redir(token) && !is_simple_end(token)) {
    detail->argc++;
    token = strtok(NULL, " ");
  }

  char **argv = malloc((detail->argc + 1) * sizeof(char *));
  if (argv == NULL) return -1;
  detail->argv = argv;
  argv[0] = first_token;
  argv[detail->argc] = NULL;

  int i = 1;
  for (char *p = first_token; i < detail->argc; p++) {
    if (*p == '\0') {
      argv[i] = p+1;
      i++;
    }
  }

  while (token && !is_simple_end(token)) {
    if (strcmp(token, "<") == 0) {
      detail->in = strtok(NULL, " ");
    } else {
      char **name;
      enum redir_type* type;
      if (token[0] == '2') {
        name = &(detail->err);
        type = &(detail->err_type);
        token++;
      } else {
        name = &(detail->out);
        type = &(detail->out_type);
      }

      *name = strtok(NULL, " ");

      if (strcmp(token, ">") == 0) {
        *type = REDIR_NORMAL;
      } else if (strcmp(token, ">>") == 0) {
        *type = REDIR_APPEND;
      } else if (strcmp(token, ">|") == 0) {
        *type = REDIR_OVERWRITE;
      } else {
        return -1; // Unknown redirection symbol
      }
    }
    token = strtok(NULL, " ");
  }
  return 0;
}

int insert_pair(struct cmd *root, enum cmd_type type) {
  /* We assume that `out' already contains the first command of the pair.
   * We modify it so that is becomes a pair containing the original `out'
   * and an empty second member ready to be filled. */

  struct cmd *copy = malloc(sizeof(struct cmd));
  if (!copy) return -1;
  copy->type = root->type;
  copy->detail = root->detail;

  struct cmd_pair *new_detail = malloc(sizeof(struct cmd_pair));
  if (!new_detail) {
    // No need to recursively free because `out' haven't changed yet
    free(copy);
    return -1;
  }
  new_detail->cmd1 = copy;

  root->type = type;
  root->detail = new_detail;

  new_detail->cmd2 = calloc(1, sizeof(struct cmd));
  if (!new_detail->cmd2) return -1;
  return 0;
}

int parse_semicolon(struct cmd *out) {
  /* We make a pair containing the original `out' and the rest of the
   * expression (because ; has low priority). */

  if (insert_pair(out, CMD_SEMICOLON) == -1) return -1;

  struct cmd *next = ((struct cmd_pair *)(out->detail))->cmd2;
  token = strtok(NULL, " ");
  return parse_cmd(next);
}

int parse_pipe(struct cmd *out) {
  /* We make a pair containing the original `out' and only the next command
   * (because | has high priority). */

  if (insert_pair(out, CMD_PIPE) == -1) return -1;

  struct cmd *next = ((struct cmd_pair *)(out->detail))->cmd2;
  token = strtok(NULL, " ");
  return parse_simple(next);
}

void free_cmd(struct cmd *cmd) {
  switch (cmd->type) {
    case CMD_EMPTY:
      break;

    case CMD_SIMPLE:
      struct cmd_simple *simple = (struct cmd_simple *)(cmd->detail);
      if (simple->argv != NULL) free(simple->argv);
      free(simple);
      break;

    case CMD_PIPE:
    case CMD_SEMICOLON:
      struct cmd_pair *pair = (struct cmd_pair *)(cmd->detail);
      if (pair->cmd1 != NULL) free_cmd(pair->cmd1);
      if (pair->cmd2 != NULL) free_cmd(pair->cmd2);
      free(pair);
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
      if (cmd_for->argv != NULL) free(cmd_for->argv);
      if (cmd_for->body != NULL) free_cmd(cmd_for->body);
      free(cmd_for);
      break;
  }
  free(cmd);
  return;
}
