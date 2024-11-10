#ifndef FSH_CMD
#define FSH_CMD

typedef int (*cmd_func)(int, char **);

// internal commands
extern int cmd_pwd(int _argc, char **_argv);
extern int cmd_cd(int argc, char **argv);
extern int cmd_ftype(int argc, char **argv);
extern int cmd_exit(int argc, char **argv);

extern int is_built_in(char *cmd); // TODO: implement this
extern int exec_cmd(int argc, char **argv);
extern int exec_external_cmd(int _argc, char **argv);
extern int parse_and_exec_simple_cmd(char *input);
#endif