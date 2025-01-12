#ifndef FSH_H
#define FSH_H
#include <signal.h>
#define MAX(x, y) (((x) > (y)) ? (x) : (y))

extern char *CWD;
extern char *PREV_WORKING_DIR;
extern char *HOME;
extern int PREV_RETURN_VALUE;
extern volatile sig_atomic_t sig_received;

#endif
