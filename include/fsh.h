#ifndef FSH_H
#define FSH_H
#include <signal.h>
#define MAX(x, y) (((x) > (y)) ? (x) : (y))

extern char *g_cwd;
extern char *g_prev_wd;
extern char *g_home;
extern int g_prev_ret_val;
extern volatile sig_atomic_t g_sig_received;

#endif
