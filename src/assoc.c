#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assoc.h>

// A simple, associative chained list keyed by char

struct assoc assoc_cons(char key, void *value, struct assoc *next) {
  struct assoc node = {
    .key = key,
    .value = value,
    .tail = next,
  };
  return node;
}

// Retrieve a value from the assoc list
char *assoc_find(struct assoc node, char key) {
  struct assoc *current = &node;
  while (current != NULL) {
    if (current->key == key)
      return current->value;
    current = current->tail;
  }
  return NULL;
}
