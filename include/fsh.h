#ifndef FSH_H
#define FSH_H

#include <signal.h>

extern char *CWD;
extern char *PREV_WORKING_DIR;
extern char *HOME;
extern int PREV_RETURN_VALUE;
extern volatile sig_atomic_t STOP_SIG;

void reset_handlers(void);

#endif
