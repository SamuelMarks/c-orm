/**
 * @file c_orm_string_builder.c
 * @brief Implementation of dynamic string builder.
 */

/* clang-format off */
#include "c_orm_safe_crt.h"
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
  /** @brief Pointer to allocated string buffer. */
  char *buffer;
  /** @brief Current string length. */
  size_t length;
  /** @brief Allocated capacity of string buffer. */
  size_t capacity;
  /** @brief Validity flag (1 if valid, 0 on error). */
  int valid;
};

/**
 * @brief Initialize a new string builder.
 *
 * @param out_builder Pointer to receive the new builder instance.
 * @return 0 on success, non-zero on allocation failure.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_string_builder_init(c_orm_string_builder_t **out_builder) {
  c_orm_string_builder_t *sb;
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_string_builder_init: entry");

  if (!out_builder) {
    LOG_DEBUG("c_orm_string_builder_init: out_builder is NULL");
    rc = C_ORM_ERROR_UNKNOWN;
    return rc;
  }

  sb = NULL;
  rc = c_orm_system_malloc(sizeof(c_orm_string_builder_t), (void **)&sb);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_string_builder_init: OOM");
    return rc;
  }

  sb->capacity = 1;
  sb->length = 0;
  sb->valid = 1;
  sb->buffer = NULL;
  rc = c_orm_system_malloc(sb->capacity, (void **)&sb->buffer);
  if (rc != C_ORM_OK) {
    c_orm_system_free(sb);
    LOG_DEBUG("c_orm_string_builder_init: OOM buffer");
    return rc;
  }
  sb->buffer[0] = '\0';

  *out_builder = sb;
  rc = C_ORM_OK;
  LOG_DEBUG("c_orm_string_builder_init: exit");
  return rc;
}

/**
 * @brief Free resources associated with a string builder.
 *
 * @param builder The builder to free.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_string_builder_free(c_orm_string_builder_t *builder) {
  c_orm_error_t rc = C_ORM_OK;
  c_orm_error_t free_rc;
  LOG_DEBUG("c_orm_string_builder_free: entry");
  if (builder) {
    if (builder->buffer) {
      free_rc = c_orm_system_free(builder->buffer);
      if (free_rc != C_ORM_OK) {
        rc = free_rc;
      }
    }
    free_rc = c_orm_system_free(builder);
    if (free_rc != C_ORM_OK && rc == C_ORM_OK) {
      rc = free_rc;
    }
  }
  LOG_DEBUG("c_orm_string_builder_free: exit");
  return rc;
}

#ifdef C_ORM_TEST_ALLOCATOR
/**
 * @brief Countdown counter to simulate string builder append allocation
 * failure.
 */
C_ORM_EXPORT int c_orm_mock_string_builder_append_countdown = -1;

/**
 * @brief Flag to simulate string builder get failure.
 */
C_ORM_EXPORT int c_orm_mock_string_builder_get_fail = 0;
#endif /* C_ORM_TEST_ALLOCATOR */

/**
 * @brief Append a string to the builder.
 *
 * @param builder The builder.
 * @param str The string to append.
 * @return 0 on success, non-zero on failure.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_string_builder_append(c_orm_string_builder_t *builder, const char *str) {
  size_t len;
  size_t required_capacity;
  c_orm_error_t rc;
  size_t new_capacity;
  char *new_buffer;

  LOG_DEBUG("c_orm_string_builder_append: entry");

  if (!builder || !str) {
    LOG_DEBUG("c_orm_string_builder_append: null argument");
    rc = C_ORM_ERROR_UNKNOWN;
    return rc;
  }

  if (!builder->valid) {
    LOG_DEBUG("c_orm_string_builder_append: builder in invalid state");
    rc = C_ORM_ERROR_MEMORY;
    return rc;
  }

#ifdef C_ORM_TEST_ALLOCATOR
  if (c_orm_mock_string_builder_append_countdown >= 0) {
    if (c_orm_mock_string_builder_append_countdown == 0) {
      c_orm_mock_string_builder_append_countdown = -1;
      builder->valid = 0;
      rc = C_ORM_ERROR_MEMORY;
      return rc;
    }
    c_orm_mock_string_builder_append_countdown--;
  }
#endif

  len = strlen(str);
  if (len == 0) {
    rc = C_ORM_OK;
    LOG_DEBUG("c_orm_string_builder_append: empty string, returning early");
    return rc;
  }

  required_capacity = builder->length + len + 1;
  if (required_capacity > builder->capacity) {
    new_capacity = builder->capacity * 2;
    while (new_capacity < required_capacity) {
      new_capacity *= 2;
    }
    new_buffer = NULL;
    rc = c_orm_system_realloc(builder->buffer, new_capacity,
                              (void **)&new_buffer);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_string_builder_append: OOM realloc");
      builder->valid = 0;
      return rc;
    }
    builder->buffer = new_buffer;
    builder->capacity = new_capacity;
  }

  memcpy(builder->buffer + builder->length, str, len + 1);
  builder->length += len;
  rc = C_ORM_OK;
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
C_ORM_EXPORT c_orm_error_t c_orm_string_builder_get(
    const c_orm_string_builder_t *builder, const char **out_str) {
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_string_builder_get: entry");

  if (!builder || !out_str) {
    LOG_DEBUG("c_orm_string_builder_get: null argument");
    rc = C_ORM_ERROR_UNKNOWN;
    return rc;
  }

  if (!builder->valid) {
    LOG_DEBUG("c_orm_string_builder_get: builder in invalid state");
    rc = C_ORM_ERROR_MEMORY;
    return rc;
  }

#ifdef C_ORM_TEST_ALLOCATOR
  if (c_orm_mock_string_builder_get_fail > 1) {
    c_orm_mock_string_builder_get_fail--;
  } else if (c_orm_mock_string_builder_get_fail) {
    LOG_DEBUG("c_orm_string_builder_get: mock failure");
    rc = C_ORM_ERROR_MEMORY;
    return rc;
  }
#endif

  *out_str = builder->buffer ? builder->buffer : "";
  rc = C_ORM_OK;
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
C_ORM_EXPORT c_orm_error_t c_orm_string_builder_len(
    const c_orm_string_builder_t *builder, size_t *out_len) {
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_string_builder_len: entry");

  if (!builder || !out_len) {
    LOG_DEBUG("c_orm_string_builder_len: null argument");
    rc = C_ORM_ERROR_UNKNOWN;
    return rc;
  }

  if (!builder->valid) {
    LOG_DEBUG("c_orm_string_builder_len: builder in invalid state");
    rc = C_ORM_ERROR_MEMORY;
    return rc;
  }

  *out_len = builder->length;
  rc = C_ORM_OK;
  LOG_DEBUG("c_orm_string_builder_len: exit");
  return rc;
}

C_ORM_EXPORT c_orm_error_t c_orm_quote_identifier(
    c_orm_string_builder_t *builder, const char *ident, char quote_char) {
  const char *p;
  char qbuf[2];
  char escbuf[3];
  char chbuf[2];
  c_orm_error_t rc;

  if (!builder || !ident) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (ident[0] == '\0' || (quote_char != '"' && quote_char != '`')) {
    return C_ORM_ERROR_VALIDATION;
  }

  for (p = ident; *p; p++) {
    if ((unsigned char)*p < 0x20 || (unsigned char)*p == 0x7F) {
      return C_ORM_ERROR_VALIDATION;
    }
  }

  qbuf[0] = quote_char;
  qbuf[1] = '\0';
  escbuf[0] = quote_char;
  escbuf[1] = quote_char;
  escbuf[2] = '\0';
  chbuf[1] = '\0';

  rc = c_orm_string_builder_append(builder, qbuf);
  if (rc != C_ORM_OK) {
    return rc;
  }

  for (p = ident; *p; p++) {
    if (*p == quote_char) {
      rc = c_orm_string_builder_append(builder, escbuf);
      if (rc != C_ORM_OK) {
        return rc;
      }
    } else {
      chbuf[0] = *p;
      rc = c_orm_string_builder_append(builder, chbuf);
      if (rc != C_ORM_OK) {
        return rc;
      }
    }
  }

  return c_orm_string_builder_append(builder, qbuf);
}

C_ORM_EXPORT c_orm_error_t c_orm_quote_qualified_identifier(
    c_orm_string_builder_t *builder, const char *ident, char quote_char) {
  const char *seg_start;
  const char *p;
  c_orm_error_t rc;

  if (!builder || !ident) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (ident[0] == '\0' || (quote_char != '"' && quote_char != '`')) {
    return C_ORM_ERROR_VALIDATION;
  }

  seg_start = ident;
  for (p = ident;; ++p) {
    if (*p == '.' || *p == '\0') {
      size_t seg_len = (size_t)(p - seg_start);
      size_t k;
      if (seg_len == 0) {
        return C_ORM_ERROR_VALIDATION;
      }
      if (!(seg_len == 1 && seg_start[0] == '*' && *p == '\0')) {
        for (k = 0; k < seg_len; ++k) {
          unsigned char uch = (unsigned char)seg_start[k];
          if (uch < 32U || uch == 127U) {
            return C_ORM_ERROR_VALIDATION;
          }
        }
      }
      if (*p == '\0') {
        break;
      }
      seg_start = p + 1;
    }
  }

  seg_start = ident;
  for (p = ident;; ++p) {
    if (*p == '.' || *p == '\0') {
      size_t seg_len = (size_t)(p - seg_start);
      if (seg_len == 1 && seg_start[0] == '*' && *p == '\0') {
        rc = c_orm_string_builder_append(builder, "*");
        if (rc != C_ORM_OK) {
          return rc;
        }
      } else {
        char *seg_buf = NULL;
        rc = c_orm_system_malloc(seg_len + 1, (void **)&seg_buf);
        if (rc != C_ORM_OK) {
          return rc;
        }
        memcpy(seg_buf, seg_start, seg_len);
        seg_buf[seg_len] = '\0';
        rc = c_orm_quote_identifier(builder, seg_buf, quote_char);
        if (rc != C_ORM_OK) {
          c_orm_system_free(seg_buf);
          return rc;
        }
        c_orm_system_free(seg_buf);
      }
      if (*p == '\0') {
        break;
      }
      rc = c_orm_string_builder_append(builder, ".");
      if (rc != C_ORM_OK) {
        return rc;
      }
      seg_start = p + 1;
    }
  }

  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_append_placeholder(
    c_orm_string_builder_t *builder, int is_postgres, size_t param_index) {
  char ph[32];
  char rev[32];
  size_t rlen = 0;
  size_t pos = 0;
  size_t v;

  if (!builder) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (is_postgres) {
    if (param_index == 0) {
      return C_ORM_ERROR_VALIDATION;
    }
    v = param_index;
    while (v > 0) {
      rev[rlen++] = (char)('0' + (v % 10U));
      v /= 10U;
    }
    ph[pos++] = '$';
    while (rlen > 0) {
      ph[pos++] = rev[--rlen];
    }
    ph[pos] = '\0';
    return c_orm_string_builder_append(builder, ph);
  }
  return c_orm_string_builder_append(builder, "?");
}

C_ORM_EXPORT c_orm_error_t
c_orm_append_string_literal(c_orm_string_builder_t *builder, const char *str) {
  c_orm_error_t rc;
  const char *p;
  char chbuf[2];

  if (!builder || !str) {
    return C_ORM_ERROR_NULL_POINTER;
  }

  rc = c_orm_string_builder_append(builder, "'");
  if (rc != C_ORM_OK) {
    return rc;
  }

  chbuf[1] = '\0';
  for (p = str; *p != '\0'; ++p) {
    if (*p == '\'') {
      rc = c_orm_string_builder_append(builder, "''");
      if (rc != C_ORM_OK) {
        return rc;
      }
    } else {
      chbuf[0] = *p;
      rc = c_orm_string_builder_append(builder, chbuf);
      if (rc != C_ORM_OK) {
        return rc;
      }
    }
  }

  return c_orm_string_builder_append(builder, "'");
}
