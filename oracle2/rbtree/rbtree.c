/*

  rbtree.c

  Implements the Red-Black Tree algorithm as described in
  "Introduction to Algorithms"

  Copyright 2009-2020 Matthew T. Pandina. All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

  1. Redistributions of source code must retain the above copyright
  notice, this list of conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions and the following disclaimer in the
  documentation and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY MATTHEW T. PANDINA "AS IS" AND ANY
  EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL MATTHEW T. PANDINA OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
  USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
  ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
  OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
  SUCH DAMAGE.

*/

#include <stdlib.h>
#include <string.h>

#include "rbtree.r"

void rbtree_init(rbtree_t *self,
                 rbtree_node_t *nil,
                 unsigned int rbtree_node_t_size,
                 int (*CompareFunc)(const rbtree_node_t *,
                                    const rbtree_node_t *)) {
  self->nil = nil;
  self->nil->parent = self->nil->left = self->nil->right = self->nil;
  self->nil->color = RBTREE_NODE_COLOR_BLACK;

  self->root = self->nil;
  self->rbtree_node_t_size = rbtree_node_t_size;
  self->Compare = CompareFunc;
}

void rbtree_destroy(rbtree_t *self) {
  memset(self, 0, sizeof(rbtree_t));
}

rbtree_node_t *rbtree_search(rbtree_t *self, rbtree_node_t *k) {
  rbtree_node_t *x = self->root;
  while (x != self->nil && self->Compare(k, x)) {
    if (self->Compare(k, x) < 0)
      x = x->left;
    else
      x = x->right;
  }
  return x;
}

void rbtree_insert(rbtree_t *self, rbtree_node_t *z) {
  rbtree_node_t *y = self->nil;
  rbtree_node_t *x = self->root;
  while (x != self->nil) {
    y = x;
    if (self->Compare(z, x) < 0)
      x = x->left;
    else
      x = x->right;
  }
  z->parent = y;
  if (y == self->nil)
    self->root = z;
  else {
    if (self->Compare(z, y) < 0)
      y->left = z;
    else
      y->right = z;
  }
  z->left = self->nil;
  z->right = self->nil;
  z->color = RBTREE_NODE_COLOR_RED;
  InsertFixup(self, z);
}

rbtree_node_t *rbtree_minimum(rbtree_t *self) {
  return Minimum(self, self->root);
}

rbtree_node_t *rbtree_maximum(rbtree_t *self) {
  return Maximum(self, self->root);
}

rbtree_node_t *rbtree_predecessor(rbtree_t *self, rbtree_node_t *x) {
  return Predecessor(self, x);
}

rbtree_node_t *rbtree_successor(rbtree_t *self, rbtree_node_t *x) {
  return Successor(self, x);
}

rbtree_node_t *rbtree_delete(rbtree_t *self, rbtree_node_t *z) {
  rbtree_node_t *x, *y;
  if (z->left == self->nil || z->right == self->nil)
    y = z;
  else
    y = Successor(self, z);
  if (y->left != self->nil)
    x = y->left;
  else
    x = y->right;
  x->parent = y->parent;
  if (y->parent == self->nil)
    self->root = x;
  else {
    if (y == y->parent->left)
      y->parent->left = x;
    else
      y->parent->right = x;
  }
  if (y != z && self->rbtree_node_t_size > sizeof(rbtree_node_t))
    memcpy(((unsigned char *)z) + sizeof(rbtree_node_t),
           ((unsigned char *)y) + sizeof(rbtree_node_t),
           self->rbtree_node_t_size - sizeof(rbtree_node_t));
  if (y->color == RBTREE_NODE_COLOR_BLACK)
    DeleteFixup(self, x);
  return y;
}

static void InorderTreeWalk(rbtree_t *self,
                            rbtree_node_t *x,
                            void (*ApplyFunc)(rbtree_node_t *, void *),
                            void *context) {
  if (x != self->nil) {
    InorderTreeWalk(self, x->left, ApplyFunc, context);
    ApplyFunc(x, context);
    InorderTreeWalk(self, x->right, ApplyFunc, context);
  }
}

void rbtree_inorderwalk(rbtree_t *self,
                        void (*ApplyFunc)(rbtree_node_t *, void *),
                        void *context) {
  InorderTreeWalk(self, self->root, ApplyFunc, context);
}

static void PreorderTreeWalk(rbtree_t *self,
                             rbtree_node_t *x,
                             void (*ApplyFunc)(rbtree_node_t *, void *),
                             void *context) {
  if (x != self->nil) {
    ApplyFunc(x, context);
    PreorderTreeWalk(self, x->left, ApplyFunc, context);
    PreorderTreeWalk(self, x->right, ApplyFunc, context);
  }
}

void rbtree_preorderwalk(rbtree_t *self,
                         void (*ApplyFunc)(rbtree_node_t *, void *),
                         void *context) {
  PreorderTreeWalk(self, self->root, ApplyFunc, context);
}

static void PostorderTreeWalk(rbtree_t *self,
                              rbtree_node_t *x,
                              void (*ApplyFunc)(rbtree_node_t *, void *),
                              void *context) {
  if (x != self->nil) {
    PostorderTreeWalk(self, x->left, ApplyFunc, context);
    PostorderTreeWalk(self, x->right, ApplyFunc, context);
    ApplyFunc(x, context);
  }
}

void rbtree_postorderwalk(rbtree_t *self,
                          void (*ApplyFunc)(rbtree_node_t *, void *),
                          void *context) {
  PostorderTreeWalk(self, self->root, ApplyFunc, context);
}
