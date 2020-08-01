#pragma once

#include <string.h>

#include "rbtree/rbtree.h"
#include "rbtree/rbtree+setinsert.h"

#include "netlist.h"

typedef struct _netlist_node_t { rbtree_node_t super;
  struct netlist n;
} netlist_node_t;

rbtree_node_t *netlist_node_new(struct netlist *n) {
  netlist_node_t *self = malloc(sizeof(netlist_node_t));
  memcpy(&self->n, n, sizeof(self->n));
  return (rbtree_node_t *)self;
}

void netlist_node_delete(rbtree_node_t *self, void *context) {
  (void)context;
  free(self);
  self = NULL;
}

int netlist_node_compare(const rbtree_node_t *x, const rbtree_node_t *y) {
  uint32_t x_netlist_and_flags = ((const netlist_node_t *)x)->n.netlist_and_flags;
  uint32_t y_netlist_and_flags = ((const netlist_node_t *)y)->n.netlist_and_flags;
  uint32_t x_netlist = x_netlist_and_flags & NETLIST_NETLIST_MASK;
  uint32_t y_netlist = y_netlist_and_flags & NETLIST_NETLIST_MASK;
  /* uint32_t x_flags = x_netlist_and_flags & NETLIST_FLAG_MASK; */
  /* uint32_t y_flags = y_netlist_and_flags & NETLIST_FLAG_MASK; */

  if (x_netlist == y_netlist)
    return 0;
  else if (x_netlist < y_netlist)
    return -1;
  else /*if (x_netlist > y_netlist)*/
    return 1;
}
