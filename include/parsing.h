#ifndef FSH_PARSING
#define FSH_PARSING

#include <cmd_types.h>

struct cmd *parse(char *line);
void free_cmd(struct cmd *cmd);

#endif
