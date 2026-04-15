/**
 * @file c_orm_string_builder.c
 * @brief Implementation of dynamic string builder.
 */

/* clang-format off */
#include "c_orm_string_builder.h"
#include <stdlib.h>
#include <string.h>
/* clang-format on */

struct c_orm_string_builder {
  char *buffer;
  size_t length;
  size_t capacity;
};

C_ORM_EXPORT int
c_orm_string_builder_init(c_orm_string_builder_t **out_builder) {
  c_orm_string_builder_t *sb;
  int rc;

  if (!out_builder) {
    rc = 1;
    return rc;
  }

  sb = (c_orm_string_builder_t *)malloc(sizeof(c_orm_string_builder_t));
  if (!sb) {
    rc = 1;
    return rc;
  }

  sb->capacity = 64;
  sb->length = 0;
  sb->buffer = (char *)malloc(sb->capacity);
  if (!sb->buffer) {
    free(sb);
    rc = 1;
    return rc;
  }
  sb->buffer[0] = '\0';

  *out_builder = sb;
  rc = 0;
  return rc;
}

C_ORM_EXPORT void c_orm_string_builder_free(c_orm_string_builder_t *builder) {
  if (builder) {
    if (builder->buffer) {
      free(builder->buffer);
    }
    free(builder);
  }
}

C_ORM_EXPORT int c_orm_string_builder_append(c_orm_string_builder_t *builder,
                                             const char *str) {
  size_t len;
  size_t required_capacity;
  int rc;

  if (!builder || !str) {
    rc = 1;
    return rc;
  }

  len = strlen(str);
  if (len == 0) {
    rc = 0;
    return rc;
  }

  required_capacity = builder->length + len + 1;
  if (required_capacity > builder->capacity) {
    size_t new_capacity = builder->capacity * 2;
    char *new_buffer;
    while (new_capacity < required_capacity) {
      new_capacity *= 2;
    }
    new_buffer = (char *)realloc(builder->buffer, new_capacity);
    if (!new_buffer) {
      rc = 1;
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
  return rc;
}

C_ORM_EXPORT int c_orm_string_builder_get(const c_orm_string_builder_t *builder,
                                          const char **out_str) {
  int rc;
  if (!builder || !out_str) {
    rc = 1;
    return rc;
  }
  *out_str = builder->buffer ? builder->buffer : "";
  rc = 0;
  return rc;
}

C_ORM_EXPORT int c_orm_string_builder_len(const c_orm_string_builder_t *builder,
                                          size_t *out_len) {
  int rc;
  if (!builder || !out_len) {
    rc = 1;
    return rc;
  }
  *out_len = builder->length;
  rc = 0;
  return rc;
}
