/**
 * @file c_orm_arena.c
 * @brief Implementation of memory arena for AST nodes.
 */

/* clang-format off */
#include "c_orm_ast.h"
#include "c_orm_log.h"
#include <stdlib.h>
#include <string.h>
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

  arena = NULL;
  rc = c_orm_system_malloc(sizeof(c_orm_arena_t), (void **)&arena);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_arena_new: OOM");
    return rc;
  }

  arena->head = NULL;
  *out_arena = arena;
  LOG_DEBUG("c_orm_arena_new: exit");
  return C_ORM_OK;
}

/**
 * @brief Allocates memory from the given arena.
 *
 * @param arena The arena to allocate from.
 * @param size The number of bytes to allocate.
 * @param out_ptr Pointer to store the allocated memory address.
 * @return C_ORM_OK on success, or error code on failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_arena_alloc(c_orm_arena_t *arena, size_t size,
                                             void **out_ptr) {
  c_orm_arena_block_t *block = NULL;
  size_t alloc_size;
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_arena_alloc: entry");

  if (!arena || !out_ptr || size == 0) {
    LOG_DEBUG("c_orm_arena_alloc: invalid arguments");
    return C_ORM_ERROR_UNKNOWN;
  }

  /* Simple alignment */
  size = (size + 7) & ~((size_t)7);

  if (!arena->head || arena->head->used + size > C_ORM_ARENA_BLOCK_SIZE) {
    /* Need a new block */
    /* If size is larger than a standard block, we just allocate a special large
     * block. For simplicity here, we assume AST nodes are small. */
    alloc_size = sizeof(c_orm_arena_block_t);
    if (size > C_ORM_ARENA_BLOCK_SIZE) {
      alloc_size = sizeof(c_orm_arena_block_t) - C_ORM_ARENA_BLOCK_SIZE + size;
    }

    rc = c_orm_system_malloc(alloc_size, (void **)&block);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_arena_alloc: OOM");
      return rc;
    }

    block->used = 0;
    block->next = arena->head;
    arena->head = block;
  }

  *out_ptr = arena->head->data + arena->head->used;
  arena->head->used += size;
  LOG_DEBUG("c_orm_arena_alloc: exit");
  return C_ORM_OK;
}

/**
 * @brief Duplicate a string using memory allocated from the arena.
 *
 * @param arena The arena.
 * @param str String to duplicate.
 * @param out_copy Pointer to receive the duplicated string.
 * @return C_ORM_OK on success, or error code on failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_arena_strdup(c_orm_arena_t *arena,
                                              const char *str,
                                              const char **out_copy) {
  char *copy;
  size_t len;
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_arena_strdup: entry");

  if (!out_copy) {
    LOG_DEBUG("c_orm_arena_strdup: invalid arguments");
    return C_ORM_ERROR_UNKNOWN;
  }
  *out_copy = NULL;
  if (!str) {
    LOG_DEBUG("c_orm_arena_strdup: null string");
    return C_ORM_OK;
  }
  if (!arena) {
    LOG_DEBUG("c_orm_arena_strdup: null arena");
    return C_ORM_ERROR_UNKNOWN;
  }

  len = strlen(str) + 1;
  rc = c_orm_arena_alloc(arena, len, (void **)&copy);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_arena_strdup: OOM");
    return rc;
  }

  memcpy(copy, str, len);
  *out_copy = copy;
  LOG_DEBUG("c_orm_arena_strdup: exit");
  return C_ORM_OK;
}

/**
 * @brief Frees all memory associated with the given arena.
 *
 * @param arena The arena to free.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_arena_free(c_orm_arena_t *arena) {
  c_orm_arena_block_t *block;
  c_orm_arena_block_t *next;
  c_orm_error_t rc = C_ORM_OK;
  c_orm_error_t free_rc;

  LOG_DEBUG("c_orm_arena_free: entry");

  if (!arena) {
    LOG_DEBUG("c_orm_arena_free: invalid arguments");
    return C_ORM_OK;
  }

  block = arena->head;
  while (block) {
    next = block->next;
    free_rc = c_orm_system_free(block);
    if (free_rc != C_ORM_OK && rc == C_ORM_OK) {
      rc = free_rc;
    }
    block = next;
  }

  free_rc = c_orm_system_free(arena);
  if (free_rc != C_ORM_OK && rc == C_ORM_OK) {
    rc = free_rc;
  }
  LOG_DEBUG("c_orm_arena_free: exit");
  return rc;
}
