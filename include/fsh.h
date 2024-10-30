#ifndef FSH_H
#include <linux/limits.h>
#define FSH_H
extern char PREV_WORKING_DIR[PATH_MAX];
extern int PREV_RETURN_VALUE;
extern char HOME[PATH_MAX];
extern char CWD[PATH_MAX];

extern char *construct_prompt();
#endif