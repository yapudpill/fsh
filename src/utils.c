#include <errno.h>
#include <stdio.h>
#include <string.h>

// Like perror, but writes directly on file descriptor fd
int dperror(int fd, char *msg) {
  return dprintf(fd, "%s: %s\n", msg, strerror(errno));
}
