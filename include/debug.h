#ifndef FSH_DEBUG
#define FSH_DEBUG

#include <cmd_types.h>
#ifndef DEBUG
#define DEBUG 0
#endif
void print_cmd(struct cmd *cmd);

#endif
