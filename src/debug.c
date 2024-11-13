#include <debug.h>

#include <stdio.h>

#include <cmd_types.h>

void print_redir(int device, char *name, enum redir_type type) {
  if (device == 0) {
    printf(" < %s", name);
  } else {
    printf(" ");
    if (device == 2) printf("2");
    switch (type) {
      case REDIR_NORMAL:
        printf("> %s", name);
        break;
      case REDIR_APPEND:
        printf(">> %s", name);
        break;
      case REDIR_OVERWRITE:
        printf(">| %s", name);
        break;
    }
  }
  return;
}

void print_cmd_aux(struct cmd *cmd) {
  switch (cmd->cmd_type) {
    case CMD_EMPTY:
      printf("<empty>");
      break;

    case CMD_IF_ELSE:
      struct cmd_if_else *if_else = (struct cmd_if_else *)(cmd->detail);
      printf("if ");
      print_cmd_aux(if_else->cmd_test);
      printf(" { ");
      print_cmd_aux(if_else->cmd_then);
      printf(" }");
      if (if_else->cmd_else) {
        printf(" else {");
        print_cmd_aux(if_else->cmd_then);
        printf(" }");
      }
      break;

    case CMD_FOR:
      struct cmd_for *cmd_for = (struct cmd_for *)(cmd->detail);
      printf("for %s in %s ", cmd_for->var_name, cmd_for->dir_name);
      for (char **arg = cmd_for->argv; *arg; arg++) printf("%s ", *arg);
      printf("{ ");
      print_cmd_aux(cmd_for->body);
      printf(" }");
      break;

    case CMD_SIMPLE:
      struct cmd_simple *simple = (struct cmd_simple *)(cmd->detail);
      printf("%s", simple->argv[0]);
      for (char **arg = simple->argv + 1; *arg; arg++) printf(" %s", *arg);
      if (simple->in) print_redir(0, simple->in, 0);
      if (simple->out) print_redir(1, simple->out, simple->out_type);
      if (simple->err) print_redir(2, simple->err, simple->err_type);
      break;
  }

  if (cmd->next) {
    printf(" %c ", cmd->next_type == NEXT_PIPE ? '|' : ';');
    print_cmd_aux(cmd->next);
  }
  return;
}

void print_cmd(struct cmd *cmd) {
  print_cmd_aux(cmd);
  printf("\n");
}
