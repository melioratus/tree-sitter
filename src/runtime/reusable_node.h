#include "runtime/tree.h"

typedef struct {
  Tree *tree;
  uint32_t byte_index;
  Tree *last_external_token;
} ReusableNode;

static inline ReusableNode reusable_node_new(Tree *tree) {
  ReusableNode result = {tree, 0, NULL};
  return result;
}

static inline void reusable_node_pop(ReusableNode *self) {
  self->byte_index += ts_tree_total_bytes(self->tree);
  if (self->tree->has_external_tokens) {
    self->last_external_token = ts_tree_last_external_token(self->tree);
  }

  while (self->tree) {
    Tree *parent = self->tree->context.parent;
    uint32_t next_index = self->tree->context.index + 1;
    if (parent && parent->child_count > next_index) {
      self->tree = parent->children[next_index];
      return;
    }
    self->tree = parent;
  }
}

static inline ReusableNode reusable_node_after_leaf(const ReusableNode *self) {
  ReusableNode result = *self;
  while (result.tree->child_count > 0)
    result.tree = result.tree->children[0];
  reusable_node_pop(&result);
  return result;
}

static inline bool reusable_node_has_leading_changes(const ReusableNode *self) {
  Tree *tree = self->tree;
  while (tree->has_changes) {
    if (tree->child_count == 0) return false;
    tree = tree->children[0];
    if (tree->size.bytes == 0) return false;
  }
  return true;
}

static inline bool reusable_node_breakdown(ReusableNode *self) {
  if (self->tree->child_count == 0) {
    return false;
  } else {
    self->tree = self->tree->children[0];
    return true;
  }
}
