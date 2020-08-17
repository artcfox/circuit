/*

  rbtree+debug.c

  Adds debugging methods based on code by Eli Bendersky to the
  Red-Black Tree implementation that generate .dot files which can be
  used to visualize the tree.

  To turn a .dot file into a .png file use the command:
    dot -Tpng input.dot -o output.png

  Copyright 2020 Matthew T. Pandina. All rights reserved.

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

#include "rbtree+debug.h"

static void PrintDotNil(rbtree_node_t *node, int nilcount, FILE *stream) {
  fprintf(stream, "    \"nil%u\" [shape=point];\n    \"%p\" -> \"nil%u\";\n", nilcount, node, nilcount);
}

static void PrintDotAux(rbtree_t *self, rbtree_node_t *node, FILE *stream) {
  static unsigned int nilcount = 0;

  if (node->left != self->nil) {
    fprintf(stream, "    \"%p\" -> \"%p\";\n", node, node->left);
    PrintDotAux(self, node->left, stream);
  } else
    PrintDotNil(node, nilcount++, stream);

  if (node->right != self->nil) {
    fprintf(stream, "    \"%p\" -> \"%p\";\n", node, node->right);
    PrintDotAux(self, node->right, stream);
  } else
    PrintDotNil(node, nilcount++, stream);
}

void rbtree_print_dot(rbtree_t *self, FILE *stream, void (*PrintNodeFunc)(FILE *s, const rbtree_node_t *n), char *black_style, char *red_style) {
  char default_black_style[] = "style=filled fillcolor=black fontcolor=white";
  char default_red_style[] = "fillcolor=red";
  if (!black_style)
    black_style = default_black_style;
  if (!red_style)
    red_style = default_red_style;

  fprintf(stream, "digraph rbtree {\n    node [%s];\n", black_style);

  if (self->root == self->nil)
    fprintf(stream, "\n");
  else if (self->root->right == self->nil && self->root->left == self->nil)
    fprintf(stream, "    \"%p\";\n", self->root);
  else
    PrintDotAux(self, self->root, stream);

  // Label the nodes
  if (PrintNodeFunc)
    for (rbtree_node_t *itr = rbtree_minimum(self); itr != self->nil; itr = rbtree_successor(self, itr)) {
      fprintf(stream, "    \"%p\" [label=", itr);
      PrintNodeFunc(stream, itr);
      fprintf(stream, "];\n");
    }

  // Color red nodes
  rbtree_node_t *first_red_node = 0;
  for (rbtree_node_t *itr = rbtree_minimum(self); itr != self->nil; itr = rbtree_successor(self, itr))
    if (itr->color == RBTREE_NODE_COLOR_RED) {
      if (!first_red_node) {
        first_red_node = itr;
        fprintf(stream, "    ");
        continue; // print this last, to avoid an invalid trailing comma
      }
      fprintf(stream, "\"%p\", ", itr);
    }
  if (first_red_node)
    fprintf(stream, "\"%p\" [%s];\n", first_red_node, red_style);

  fprintf(stream, "}\n");
}
