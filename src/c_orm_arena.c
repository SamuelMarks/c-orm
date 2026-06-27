/**
 * @file c_orm_arena.c
 * @brief Implementation of memory arena for AST nodes.
 */

/* clang-format off */
#include "c_orm_ast.h"
#include "c_orm_log.h"
#include <stdlib.h>
/* clang-format on */

/**
 * @brief Default size for a new arena block.
 */
#define C_ORM_ARENA_BLOCK_SIZE 4096

/**
 * @brief Structure representing a single block of memory in the arena.
 */
typedef struct c_orm_arena_block {
  struct c_orm_arena_block
      *next;   /**< @brief Pointer to the next block in the arena */
  size_t used; /**< @brief Number of bytes currently used in this block */
  char data[C_ORM_ARENA_BLOCK_SIZE]; /**< @brief Memory buffer for the block */
} c_orm_arena_block_t;

/**
 * @brief Structure representing the memory arena.
 */
struct c_orm_arena {
  c_orm_arena_block_t
      *head; /**< @brief Pointer to the first block in the arena */
};

/**
 * @brief Creates a new memory arena.
 *
 * @param out_arena Pointer to store the newly created arena.
 * @return 0 on success, non-zero on failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_arena_new(c_orm_arena_t **out_arena) {
  c_orm_arena_t *arena;
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_arena_new: entry");

  if (!out_arena) {
    LOG_DEBUG("c_orm_arena_new: invalid arguments");
    rc = C_ORM_ERROR_UNKNOWN;
    return rc;
  }

  arena = (c_orm_arena_t *)C_ORM_MALLOC(sizeof(c_orm_arena_t));
  if (!arena) {
    LOG_DEBUG("c_orm_arena_new: OOM");
    rc = C_ORM_ERROR_UNKNOWN;
    return rc;
  }

  arena->head = NULL;
  *out_arena = arena;
  rc = C_ORM_OK;
  LOG_DEBUG("c_orm_arena_new: exit");
  return rc;
}

/**
 * @brief Allocates memory from the given arena.
 *
 * @param arena The arena to allocate from.
 * @param size The number of bytes to allocate.
 * @param out_ptr Pointer to store the allocated memory address.
 * @return 0 on success, non-zero on failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_arena_alloc(c_orm_arena_t *arena, size_t size,
                                             void **out_ptr) {
  c_orm_arena_block_t *block;
  size_t alloc_size;
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_arena_alloc: entry");

  if (!arena || !out_ptr || size == 0) {
    LOG_DEBUG("c_orm_arena_alloc: invalid arguments");
    rc = C_ORM_ERROR_UNKNOWN;
    return rc;
  }

  /* Simple alignment */
  size = (size + 7) & ~7;

  if (!arena->head || arena->head->used + size > C_ORM_ARENA_BLOCK_SIZE) {
    /* Need a new block */
    /* If size is larger than a standard block, we just allocate a special large
     * block. For simplicity here, we assume AST nodes are small. */
    alloc_size = sizeof(c_orm_arena_block_t);
    if (size > C_ORM_ARENA_BLOCK_SIZE) {
      alloc_size = sizeof(c_orm_arena_block_t) - C_ORM_ARENA_BLOCK_SIZE + size;
    }

    block = (c_orm_arena_block_t *)C_ORM_MALLOC(alloc_size);
    if (!block) {
      LOG_DEBUG("c_orm_arena_alloc: OOM");
      rc = C_ORM_ERROR_UNKNOWN;
      return rc;
    }

    block->used = 0;
    block->next = arena->head;
    arena->head = block;
  }

  *out_ptr = arena->head->data + arena->head->used;
  arena->head->used += size;
  rc = C_ORM_OK;
  LOG_DEBUG("c_orm_arena_alloc: exit");
  return rc;
}

/**
 * @brief Frees all memory associated with the given arena.
 *
 * @param arena The arena to free.
 */
C_ORM_EXPORT void c_orm_arena_free(c_orm_arena_t *arena) {
  c_orm_arena_block_t *block;
  c_orm_arena_block_t *next;

  LOG_DEBUG("c_orm_arena_free: entry");

  if (!arena) {
    LOG_DEBUG("c_orm_arena_free: invalid arguments");
    return;
  }

  block = arena->head;
  while (block) {
    next = block->next;
    C_ORM_FREE(block);
    block = next;
  }

  C_ORM_FREE(arena);
  LOG_DEBUG("c_orm_arena_free: exit");
}
