/**
 * @file c_orm_arena.c
 * @brief Implementation of memory arena for AST nodes.
 */

/* clang-format off */
#include "c_orm_ast.h"
#include <stdlib.h>
/* clang-format on */

#define C_ORM_ARENA_BLOCK_SIZE 4096

typedef struct c_orm_arena_block {
  struct c_orm_arena_block *next;
  size_t used;
  char data[C_ORM_ARENA_BLOCK_SIZE];
} c_orm_arena_block_t;

struct c_orm_arena {
  c_orm_arena_block_t *head;
};

C_ORM_EXPORT int c_orm_arena_new(c_orm_arena_t **out_arena) {
  c_orm_arena_t *arena;
  int rc;

  if (!out_arena) {
    rc = 1;
    return rc;
  }

  arena = (c_orm_arena_t *)C_ORM_MALLOC(sizeof(c_orm_arena_t));
  if (!arena) {
    rc = 1;
    return rc;
  }

  arena->head = NULL;
  *out_arena = arena;
  rc = 0;
  return rc;
}

C_ORM_EXPORT int c_orm_arena_alloc(c_orm_arena_t *arena, size_t size,
                                   void **out_ptr) {
  c_orm_arena_block_t *block;
  int rc;

  if (!arena || !out_ptr || size == 0) {
    rc = 1;
    return rc;
  }

  /* Simple alignment */
  size = (size + 7) & ~7;

  if (!arena->head || arena->head->used + size > C_ORM_ARENA_BLOCK_SIZE) {
    /* Need a new block */
    /* If size is larger than a standard block, we just allocate a special large
     * block. For simplicity here, we assume AST nodes are small. */
    size_t alloc_size = sizeof(c_orm_arena_block_t);
    if (size > C_ORM_ARENA_BLOCK_SIZE) {
      alloc_size = sizeof(c_orm_arena_block_t) - C_ORM_ARENA_BLOCK_SIZE + size;
    }

    block = (c_orm_arena_block_t *)C_ORM_MALLOC(alloc_size);
    if (!block) {
      rc = 1;
      return rc;
    }

    block->used = 0;
    block->next = arena->head;
    arena->head = block;
  }

  *out_ptr = arena->head->data + arena->head->used;
  arena->head->used += size;
  rc = 0;
  return rc;
}

C_ORM_EXPORT void c_orm_arena_free(c_orm_arena_t *arena) {
  c_orm_arena_block_t *block;
  c_orm_arena_block_t *next;

  if (!arena) {
    return;
  }

  block = arena->head;
  while (block) {
    next = block->next;
    C_ORM_FREE(block);
    block = next;
  }

  C_ORM_FREE(arena);
}
