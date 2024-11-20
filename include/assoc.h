#ifndef FSH_ASSOC_H
#define FSH_ASSOC_H

typedef int (*cmp_func)(const void *, const void *);

struct assoc {
  char key;
  void *value;
  struct assoc *tail;
};

struct assoc assoc_cons(char key, void *value, struct assoc *next);
char *assoc_find(struct assoc node, char key);
#endif