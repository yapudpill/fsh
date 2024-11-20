#ifndef FSH_CMD
#define FSH_CMD

typedef int (*cmd_func)(int argc, char **argv, int fd_in, int fd_out, int fd_err);

int call_command_and_wait(int argc, char **argv, int fd_in, int fd_out, int fd_err);
#endif