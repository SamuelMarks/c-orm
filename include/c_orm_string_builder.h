/**
 * @file c_orm_string_builder.h
 * @brief Dynamic string builder for safe SQL generation.
 */

#ifndef C_ORM_STRING_BUILDER_H
#define C_ORM_STRING_BUILDER_H

/* clang-format off */
#include "c_orm_meta.h"
#include <stddef.h>
#include "c_orm_db.h"
/* clang-format on */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
/**
 * @brief Dynamic string builder opaque struct.
 */
typedef struct c_orm_string_builder c_orm_string_builder_t;

/**
 * @brief Initialize a new string builder.
 *
 * @param out_builder Pointer to receive the new builder instance.
 * @return 0 on success, non-zero on allocation failure.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_string_builder_init(c_orm_string_builder_t **out_builder);

/**
 * @brief Free resources associated with a string builder.
 *
 * @param builder The builder to free.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_string_builder_free(c_orm_string_builder_t *builder);

/**
 * @brief Append a string to the builder.
 *
 * @param builder The builder.
 * @param str The string to append.
 * @return 0 on success, non-zero on failure.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_string_builder_append(c_orm_string_builder_t *builder, const char *str);

/**
 * @brief Get the generated string.
 *
 * @param builder The builder.
 * @param out_str Pointer to store null-terminated string buffer. Do not free
 * directly.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_string_builder_get(
    const c_orm_string_builder_t *builder, const char **out_str);

/**
 * @brief Get the current length of the generated string.
 *
 * @param builder The builder.
 * @param out_len Pointer to store length of the string.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_string_builder_len(
    const c_orm_string_builder_t *builder, size_t *out_len);

/**
 * @brief Validate and append a quoted SQL identifier (`"ident"` or `` `ident`
 * ``), rejecting empty strings or control characters and doubling embedded
 * quotes.
 *
 * @param builder Destination string builder.
 * @param ident Null-terminated SQL identifier string.
 * @param quote_char Delimiter character (`'"'` or `'`'`).
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER on NULL args,
 * C_ORM_ERROR_VALIDATION on invalid identifier, or C_ORM_ERROR_MEMORY on OOM.
 */
C_ORM_EXPORT c_orm_error_t c_orm_quote_identifier(
    c_orm_string_builder_t *builder, const char *ident, char quote_char);

/**
 * @brief Validate and append a possibly dot-qualified SQL identifier
 * (`SCHEMA.TABLE` -> `"SCHEMA"."TABLE"`, `t.*` -> `"t".*`), doubling embedded
 * quotes inside each identifier segment.
 *
 * @param builder Destination string builder.
 * @param ident Null-terminated identifier or qualified identifier string.
 * @param quote_char Delimiter character (`'"'` or `'`'`).
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER on NULL args,
 * C_ORM_ERROR_VALIDATION on invalid identifier, or C_ORM_ERROR_MEMORY on OOM.
 */
C_ORM_EXPORT c_orm_error_t c_orm_quote_qualified_identifier(
    c_orm_string_builder_t *builder, const char *ident, char quote_char);

/**
 * @brief Appends a dialect-appropriate positional parameter placeholder (`?` or
 * `$N`).
 *
 * @param builder Destination string builder.
 * @param is_postgres Non-zero for PostgreSQL `$N` placeholders, 0 for `?`.
 * @param param_index 1-based parameter index used when `is_postgres` is
 * non-zero.
 * @return C_ORM_OK on success, or an error enum on invalid index or OOM.
 */
C_ORM_EXPORT c_orm_error_t c_orm_append_placeholder(
    c_orm_string_builder_t *builder, int is_postgres, size_t param_index);

/**
 * @brief Appends a single-quoted SQL string literal with `'` escaped as `''`.
 *
 * @param builder Destination string builder.
 * @param str Input string literal content (must not be NULL).
 * @return C_ORM_OK on success, or an error enum on NULL input or OOM.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_append_string_literal(c_orm_string_builder_t *builder, const char *str);

#ifdef C_ORM_TEST_ALLOCATOR
/**
 * @brief Countdown counter to simulate string builder append allocation
 * failure.
 */
C_ORM_EXPORT extern int c_orm_mock_string_builder_append_countdown;

/**
 * @brief Flag to simulate string builder get failure.
 */
C_ORM_EXPORT extern int c_orm_mock_string_builder_get_fail;
#endif /* C_ORM_TEST_ALLOCATOR */

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* C_ORM_STRING_BUILDER_H */
