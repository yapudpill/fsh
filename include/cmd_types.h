#ifndef FSH_TYPES
#define FSH_TYPES

enum cmd_type {
  CMD_EMPTY, // MUST be number 0
  CMD_SIMPLE,
  CMD_IF_ELSE,
  CMD_FOR
};

enum redir_type {
  REDIR_NORMAL,
  REDIR_APPEND,
  REDIR_OVERWRITE
};

enum next_type {
  NEXT_NONE, // MUST be number 0
  NEXT_PIPE,
  NEXT_SEMICOLON
};

struct cmd {
  enum cmd_type cmd_type;
  void *detail;
  enum next_type next_type;
  struct cmd *next;
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
