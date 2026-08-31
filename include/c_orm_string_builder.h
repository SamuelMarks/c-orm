#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file c_orm_string_builder.h
 * @brief Dynamic string builder for safe SQL generation.
 */

#ifndef C_ORM_STRING_BUILDER_H
#define C_ORM_STRING_BUILDER_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_meta.h"
#include <stddef.h>
#include "c_orm_db.h"
/* clang-format on */

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
 */
C_ORM_EXPORT void c_orm_string_builder_free(c_orm_string_builder_t *builder);

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
 * @return Null-terminated string buffer. Do not free directly.
 */
C_ORM_EXPORT c_orm_error_t c_orm_string_builder_get(
    const c_orm_string_builder_t *builder, const char **out_str);

/**
 * @brief Get the current length of the generated string.
 *
 * @param builder The builder.
 * @return Length of the string.
 */
C_ORM_EXPORT c_orm_error_t c_orm_string_builder_len(
    const c_orm_string_builder_t *builder, size_t *out_len);

#if defined(__cplusplus)
}
#endif /* __cplusplus */
#endif /* C_ORM_STRING_BUILDER_H */

#if defined(__clang__) || defined(__GNUC__)
#endif
