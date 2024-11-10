#ifndef FSH_H
#define FSH_H
#include <linux/limits.h>

extern char PREV_WORKING_DIR[PATH_MAX];
extern int PREV_RETURN_VALUE;
extern char *HOME;
extern char CWD[PATH_MAX];

extern char *construct_prompt();
extern int init_env_vars();
extern int init_wd_vars();

#endif