/*

  rbtree.h

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

#pragma once

typedef enum _rbtree_node_color_t rbtree_node_color_t;
enum _rbtree_node_color_t {
  RBTREE_NODE_COLOR_RED,
  RBTREE_NODE_COLOR_BLACK
};

typedef struct _rbtree_node_t rbtree_node_t;
struct _rbtree_node_t {
  rbtree_node_color_t color;
  rbtree_node_t *left;
  rbtree_node_t *right;
  rbtree_node_t *parent;
} __attribute__ ((packed));

typedef struct _rbtree_t rbtree_t;
struct _rbtree_t {
  rbtree_node_t *nil;
  rbtree_node_t *root;
  unsigned int rbtree_node_t_size;
  int (*Compare)(const rbtree_node_t *x, const rbtree_node_t *y);
} __attribute__ ((packed));

void rbtree_init(rbtree_t *self,
                 rbtree_node_t *nil,
                 unsigned int rbtree_node_t_size,
                 int (*CompareFunc)(const rbtree_node_t *,
                                    const rbtree_node_t *));
void rbtree_destroy(rbtree_t *self);
rbtree_node_t *rbtree_search(rbtree_t *self, rbtree_node_t *k);
rbtree_node_t *rbtree_predecessor(rbtree_t *self, rbtree_node_t *x);
rbtree_node_t *rbtree_successor(rbtree_t *self, rbtree_node_t *x);
rbtree_node_t *rbtree_minimum(rbtree_t *self);
rbtree_node_t *rbtree_maximum(rbtree_t *self);
void rbtree_insert(rbtree_t *self, rbtree_node_t *z);
rbtree_node_t *rbtree_delete(rbtree_t *self, rbtree_node_t *z);

void rbtree_inorderwalk(rbtree_t *self,
                        void (*ApplyFunc)(rbtree_node_t *, void *),
                        void *context);
void rbtree_preorderwalk(rbtree_t *self,
                         void (*ApplyFunc)(rbtree_node_t *, void *),
                         void *context);
void rbtree_postorderwalk(rbtree_t *self,
                          void (*ApplyFunc)(rbtree_node_t *, void *),
                          void *context);
