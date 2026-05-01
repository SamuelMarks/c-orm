/**
 * @file c_orm_string_builder.c
 * @brief Implementation of dynamic string builder.
 */

/* clang-format off */
#include "c_orm_string_builder.h"
#include "c_orm_db.h"
#include "c_orm_log.h"
#include <stdlib.h>
#include <string.h>
/* clang-format on */

/**
 * @brief Internal string builder structure.
 */
struct c_orm_string_builder {
  char *buffer;
  size_t length;
  size_t capacity;
};

/**
 * @brief Initialize a new string builder.
 *
 * @param out_builder Pointer to receive the new builder instance.
 * @return 0 on success, non-zero on allocation failure.
 */
C_ORM_EXPORT int
c_orm_string_builder_init(c_orm_string_builder_t **out_builder) {
  c_orm_string_builder_t *sb;
  int rc;

  LOG_DEBUG("c_orm_string_builder_init: entry");

  if (!out_builder) {
    LOG_DEBUG("c_orm_string_builder_init: out_builder is NULL");
    rc = 1;
    return rc;
  }

  sb = (c_orm_string_builder_t *)C_ORM_MALLOC(sizeof(c_orm_string_builder_t));
  if (!sb) {
    LOG_DEBUG("c_orm_string_builder_init: OOM");
    rc = C_ORM_ERROR_MEMORY;
    return rc;
  }

  sb->capacity = 64;
  sb->length = 0;
  sb->buffer = (char *)C_ORM_MALLOC(sb->capacity);
  if (!sb->buffer) {
    C_ORM_FREE(sb);
    LOG_DEBUG("c_orm_string_builder_init: OOM buffer");
    rc = C_ORM_ERROR_MEMORY;
    return rc;
  }
  sb->buffer[0] = '\0';

  *out_builder = sb;
  rc = 0;
  LOG_DEBUG("c_orm_string_builder_init: exit");
  return rc;
}

/**
 * @brief Free resources associated with a string builder.
 *
 * @param builder The builder to free.
 */
C_ORM_EXPORT void c_orm_string_builder_free(c_orm_string_builder_t *builder) {
  LOG_DEBUG("c_orm_string_builder_free: entry");
  if (builder) {
    if (builder->buffer) {
      C_ORM_FREE(builder->buffer);
    }
    C_ORM_FREE(builder);
  }
  LOG_DEBUG("c_orm_string_builder_free: exit");
}

/**
 * @brief Append a string to the builder.
 *
 * @param builder The builder.
 * @param str The string to append.
 * @return 0 on success, non-zero on failure.
 */
C_ORM_EXPORT int c_orm_string_builder_append(c_orm_string_builder_t *builder,
                                             const char *str) {
  size_t len;
  size_t required_capacity;
  int rc;
  size_t new_capacity;
  char *new_buffer;

  LOG_DEBUG("c_orm_string_builder_append: entry");

  if (!builder || !str) {
    LOG_DEBUG("c_orm_string_builder_append: null argument");
    rc = 1;
    return rc;
  }

  len = strlen(str);
  if (len == 0) {
    rc = 0;
    LOG_DEBUG("c_orm_string_builder_append: empty string, returning early");
    return rc;
  }

  required_capacity = builder->length + len + 1;
  if (required_capacity > builder->capacity) {
    new_capacity = builder->capacity * 2;
    while (new_capacity < required_capacity) {
      new_capacity *= 2;
    }
    new_buffer = (char *)C_ORM_REALLOC(builder->buffer, new_capacity);
    if (!new_buffer) {
      LOG_DEBUG("c_orm_string_builder_append: OOM realloc");
      rc = C_ORM_ERROR_MEMORY;
      return rc;
    }
    builder->buffer = new_buffer;
    builder->capacity = new_capacity;
  }

#if defined(_MSC_VER)
  strcpy_s(builder->buffer + builder->length,
           builder->capacity - builder->length, str);
#else
  strcpy(builder->buffer + builder->length, str);
#endif

  builder->length += len;
  rc = 0;
  LOG_DEBUG("c_orm_string_builder_append: exit");
  return rc;
}

/**
 * @brief Get the generated string.
 *
 * @param builder The builder.
 * @param out_str Pointer to receive the string.
 * @return 0 on success, non-zero on failure.
 */
C_ORM_EXPORT int c_orm_string_builder_get(const c_orm_string_builder_t *builder,
                                          const char **out_str) {
  int rc;

  LOG_DEBUG("c_orm_string_builder_get: entry");

  if (!builder || !out_str) {
    LOG_DEBUG("c_orm_string_builder_get: null argument");
    rc = 1;
    return rc;
  }
  *out_str = builder->buffer ? builder->buffer : "";
  rc = 0;
  LOG_DEBUG("c_orm_string_builder_get: exit");
  return rc;
}

/**
 * @brief Get the current length of the generated string.
 *
 * @param builder The builder.
 * @param out_len Pointer to receive the length.
 * @return 0 on success, non-zero on failure.
 */
C_ORM_EXPORT int c_orm_string_builder_len(const c_orm_string_builder_t *builder,
                                          size_t *out_len) {
  int rc;

  LOG_DEBUG("c_orm_string_builder_len: entry");

  if (!builder || !out_len) {
    LOG_DEBUG("c_orm_string_builder_len: null argument");
    rc = 1;
    return rc;
  }
  *out_len = builder->length;
  rc = 0;
  LOG_DEBUG("c_orm_string_builder_len: exit");
  return rc;
}
