#include "vtpc_internal.h"

typedef struct vtpc_node {
  int fd;
  vtpc_state* st;
  struct vtpc_node* next;
} vtpc_node;

static vtpc_node* g_head = NULL;

int vtpc_table_insert(int fd, vtpc_state* st) {
  vtpc_node* n = (vtpc_node*)malloc(sizeof(vtpc_node));
  if (n == NULL) {
    errno = ENOMEM;
    return -1;
  }
  n->fd = fd;
  n->st = st;
  n->next = g_head;
  g_head = n;
  return 0;
}

vtpc_state* vtpc_table_find(int fd) {
  for (vtpc_node* n = g_head; n != NULL; n = n->next) {
    if (n->fd == fd) {
      return n->st;
    }
  }
  return NULL;
}

vtpc_state* vtpc_table_remove(int fd) {
  vtpc_node** pp = &g_head;
  while (*pp != NULL) {
    if ((*pp)->fd == fd) {
      vtpc_node* dead = *pp;
      vtpc_state* st = dead->st;
      *pp = dead->next;
      free(dead);
      return st;
    }
    pp = &((*pp)->next);
  }
  return NULL;
}
