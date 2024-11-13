#ifndef FSH_TYPES
#define FSH_TYPES

enum cmd_type {
  CMD_EMPTY, // MUST be number 0
  CMD_SIMPLE,
  CMD_PIPE,
  CMD_SEMICOLON,
  CMD_IF_ELSE,
  CMD_FOR
};

enum redir_type {
  REDIR_NORMAL,
  REDIR_APPEND,
  REDIR_OVERWRITE
};

struct cmd {
  enum cmd_type type;
  void *detail;
};

struct cmd_simple {
  int argc;
  char **argv;
  char *in;
  char *out;
  enum redir_type out_type;
  char *err;
  enum redir_type err_type;
};

struct cmd_pair {
  struct cmd *cmd1;
  struct cmd *cmd2;
};

struct cmd_if_else {
  struct cmd *cmd_test;
  struct cmd *cmd_then;
  struct cmd *cmd_else;
};

struct cmd_for {
  char *var_name;
  char *dir_name;
  int argc;
  char **argv;
  struct cmd *body;
};

#endif