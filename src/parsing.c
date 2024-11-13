#include <parsing.h>

#include <stddef.h>

#include <cmd_types.h>

int parse_cmd(struct cmd *out);

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
