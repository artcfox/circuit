/*

  rbtree.r

  Implements the Red-Black Tree algorithm as described in
  "Introduction to Algorithms"

  Note: This is a private header file, which should only be included
  by the header files of extensions to the Red-Black Tree
  implementation which need to call these inline functions. For
  example: rbtree+setinsert.h should include this header, but main.c
  should not.

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

#pragma once

#include "rbtree.h"

static inline void LeftRotate(rbtree_t *self, rbtree_node_t *x) {
  rbtree_node_t *y = x->right;
  x->right = y->left;
  if (y->left != self->nil)
    y->left->parent = x;
  y->parent = x->parent;
  if (x->parent == self->nil)
    self->root = y;
  else {
    if (x == x->parent->left)
      x->parent->left = y;
    else
      x->parent->right = y;
  }
  y->left = x;
  x->parent = y;
}

static inline void RightRotate(rbtree_t *self, rbtree_node_t *y) {
  rbtree_node_t *x = y->left;
  y->left = x->right;
  if (x->right != self->nil)
    x->right->parent = y;
  x->parent = y->parent;
  if (y->parent == self->nil)
    self->root = x;
  else {
    if (y == y->parent->right)
      y->parent->right = x;
    else
      y->parent->left = x;
  }
  x->right = y;
  y->parent = x;
}

static inline void InsertFixup(rbtree_t *self, rbtree_node_t *z) {
  rbtree_node_t *y;
  while (z->parent->color == RBTREE_NODE_COLOR_RED) {
    if (z->parent == z->parent->parent->left) {
      y = z->parent->parent->right;
      if (y->color == RBTREE_NODE_COLOR_RED) {
        z->parent->color = RBTREE_NODE_COLOR_BLACK;
        y->color = RBTREE_NODE_COLOR_BLACK;
        z->parent->parent->color = RBTREE_NODE_COLOR_RED;
        z = z->parent->parent;
      } else {
        if (z == z->parent->right) {
          z = z->parent;
          LeftRotate(self, z);
        }
        z->parent->color = RBTREE_NODE_COLOR_BLACK;
        z->parent->parent->color = RBTREE_NODE_COLOR_RED;
        RightRotate(self, z->parent->parent);
      }
    } else {
      y = z->parent->parent->left;
      if (y->color == RBTREE_NODE_COLOR_RED) {
        z->parent->color = RBTREE_NODE_COLOR_BLACK;
        y->color = RBTREE_NODE_COLOR_BLACK;
        z->parent->parent->color = RBTREE_NODE_COLOR_RED;
        z = z->parent->parent;
      } else {
        if (z == z->parent->left) {
          z = z->parent;
          RightRotate(self, z);
        }
        z->parent->color = RBTREE_NODE_COLOR_BLACK;
        z->parent->parent->color = RBTREE_NODE_COLOR_RED;
        LeftRotate(self, z->parent->parent);
      }
    }
  }
  self->root->color = RBTREE_NODE_COLOR_BLACK;
}

static inline rbtree_node_t *Minimum(rbtree_t *self, rbtree_node_t *x) {
  while (x->left != self->nil)
    x = x->left;
  return x;
}

static inline rbtree_node_t *Maximum(rbtree_t *self, rbtree_node_t *x) {
  while (x->right != self->nil)
    x = x->right;
  return x;
}

static inline rbtree_node_t *Predecessor(rbtree_t *self, rbtree_node_t *x) {
  if (x->left != self->nil)
    return Maximum(self, x->left);
  rbtree_node_t *y = x->parent;
  while (y != self->nil && x == y->left) {
    x = y;
    y = y->parent;
  }
  return y;
}

static inline rbtree_node_t *Successor(rbtree_t *self, rbtree_node_t *x) {
  if (x->right != self->nil)
    return Minimum(self, x->right);
  rbtree_node_t *y = x->parent;
  while (y != self->nil && x == y->right) {
    x = y;
    y = y->parent;
  }
  return y;
}

static inline void DeleteFixup(rbtree_t *self, rbtree_node_t *x) {
  rbtree_node_t *w;
  while (x != self->root && x->color == RBTREE_NODE_COLOR_BLACK) {
    if (x == x->parent->left) {
      w = x->parent->right;
      if (w->color == RBTREE_NODE_COLOR_RED) {
        w->color = RBTREE_NODE_COLOR_BLACK;
        x->parent->color = RBTREE_NODE_COLOR_RED;
        LeftRotate(self, x->parent);
        w = x->parent->right;
      }
      if (w->left->color == RBTREE_NODE_COLOR_BLACK &&
	  w->right->color == RBTREE_NODE_COLOR_BLACK) {
        w->color = RBTREE_NODE_COLOR_RED;
        x = x->parent;
      } else {
        if (w->right->color == RBTREE_NODE_COLOR_BLACK) {
          w->left->color = RBTREE_NODE_COLOR_BLACK;
          w->color = RBTREE_NODE_COLOR_RED;
          RightRotate(self, w);
          w = x->parent->right;
        }
        w->color = x->parent->color;
        x->parent->color = RBTREE_NODE_COLOR_BLACK;
        w->right->color = RBTREE_NODE_COLOR_BLACK;
        LeftRotate(self, x->parent);
        x = self->root;
      }
    } else {
      w = x->parent->left;
      if (w->color == RBTREE_NODE_COLOR_RED) {
        w->color = RBTREE_NODE_COLOR_BLACK;
        x->parent->color = RBTREE_NODE_COLOR_RED;
        RightRotate(self, x->parent);
        w = x->parent->left;
      }
      if (w->right->color == RBTREE_NODE_COLOR_BLACK &&
	  w->left->color == RBTREE_NODE_COLOR_BLACK) {
        w->color = RBTREE_NODE_COLOR_RED;
        x = x->parent;
      } else {
        if (w->left->color == RBTREE_NODE_COLOR_BLACK) {
          w->right->color = RBTREE_NODE_COLOR_BLACK;
          w->color = RBTREE_NODE_COLOR_RED;
          LeftRotate(self, w);
          w = x->parent->left;
        }
        w->color = x->parent->color;
        x->parent->color = RBTREE_NODE_COLOR_BLACK;
        w->left->color = RBTREE_NODE_COLOR_BLACK;
        RightRotate(self, x->parent);
        x = self->root;
      }
    }
  }
  x->color = RBTREE_NODE_COLOR_BLACK;
}
