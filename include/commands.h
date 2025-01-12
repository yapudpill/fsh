#ifndef FSH_CMD
#define FSH_CMD

int call_command_and_wait(int argc, char **argv, int redir[3]);
int cmd_exit(int argc, char **argv);

#endif
