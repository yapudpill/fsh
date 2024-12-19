#ifndef FSH_H
#define FSH_H

#define MAX(x, y) (((x) > (y)) ? (x) : (y))

extern char *CWD;
extern char *PREV_WORKING_DIR;
extern char *HOME;
extern int PREV_RETURN_VALUE;

extern void update_prompt();
extern int init_env_vars();
extern int init_wd_vars();

#endif
