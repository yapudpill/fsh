// internal commands
extern int pwd();
extern int cd(char *arg);
extern int ftype(char *arg);

extern int is_built_in(char *cmd); // TODO: implement this
extern int exec_internal_cmd(char *cmd, char *arg);
extern int exec_external_cmd(char *cmd, char **argv);
extern int exec_simple_cmd(char *input);