#ifndef FSH_EXECUTION_H
#define FSH_EXECUTION_H

#include "cmd_types.h"

int exec_cmd_chain(struct cmd *cmd_chain, char **vars);

#endif
