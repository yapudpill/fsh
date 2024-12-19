#ifndef FSH_PARSING
#define FSH_PARSING

#include <cmd_types.h>

#define ERROR_SYNTAX 2
#define ERROR_FOR_ARG 1

extern int parsing_errno;

struct cmd *parse(char *line);
void free_cmd(struct cmd *cmd);

#endif
