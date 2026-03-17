/**
 * @file c_orm_query_builder.h
 * @brief Dynamic SQL Query builder definition.
 */

#ifndef C_ORM_QUERY_BUILDER_H
#define C_ORM_QUERY_BUILDER_H
/* clang-format off */
#include "c_orm_db.h"
#include "c_orm_string_builder.h"
/* clang-format on */

#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief Opaque select builder handle.
 */
typedef struct c_orm_select_builder c_orm_select_builder_t;

/**
 * @brief Opaque insert builder handle.
 */
typedef struct c_orm_insert_builder c_orm_insert_builder_t;

/**
 * @brief Opaque update builder handle.
 */
typedef struct c_orm_update_builder c_orm_update_builder_t;

/**
 * @brief Initialize a new SELECT builder.
 *
 * @param meta Table metadata.
 * @param out_builder Pointer to receive the new builder.
 * @return 0 on success.
 */
C_ORM_EXPORT int
c_orm_select_builder_init(const c_orm_table_meta_t *meta,
                          c_orm_select_builder_t **out_builder);

/**
 * @brief Free resources associated with a select builder.
 */
C_ORM_EXPORT void c_orm_select_builder_free(c_orm_select_builder_t *builder);

/**
 * @brief Compile the select builder into a final string.
 *
 * @param builder The builder.
 * @param out_sql Pointer to receive the generated SQL string (must free).
 * @return 0 on success.
 */
C_ORM_EXPORT int c_orm_select_builder_compile(c_orm_select_builder_t *builder,
                                              char **out_sql);

/**
 * @brief Add WHERE column = ?
 */
C_ORM_EXPORT int c_orm_select_where_eq(c_orm_select_builder_t *builder,
                                       const char *column);

/**
 * @brief Add WHERE column != ?
 */
C_ORM_EXPORT int c_orm_select_where_neq(c_orm_select_builder_t *builder,
                                        const char *column);

/**
 * @brief Add WHERE column < ?
 */
C_ORM_EXPORT int c_orm_select_where_lt(c_orm_select_builder_t *builder,
                                       const char *column);

/**
 * @brief Add WHERE column > ?
 */
C_ORM_EXPORT int c_orm_select_where_gt(c_orm_select_builder_t *builder,
                                       const char *column);

/**
 * @brief Add WHERE column <= ?
 */
C_ORM_EXPORT int c_orm_select_where_lte(c_orm_select_builder_t *builder,
                                        const char *column);

/**
 * @brief Add WHERE column >= ?
 */
C_ORM_EXPORT int c_orm_select_where_gte(c_orm_select_builder_t *builder,
                                        const char *column);

/**
 * @brief Add WHERE column > CURRENT_TIMESTAMP
 */
C_ORM_EXPORT int
c_orm_select_where_gt_current_timestamp(c_orm_select_builder_t *builder,
                                        const char *column);

/**
 * @brief Add WHERE column < CURRENT_TIMESTAMP
 */
C_ORM_EXPORT int
c_orm_select_where_lt_current_timestamp(c_orm_select_builder_t *builder,
                                        const char *column);

/**
 * @brief Add WHERE column LIKE ?
 */
C_ORM_EXPORT int c_orm_select_where_like(c_orm_select_builder_t *builder,
                                         const char *column);

/**
 * @brief Add WHERE column IN (?, ?, ...)
 */
C_ORM_EXPORT int c_orm_select_where_in(c_orm_select_builder_t *builder,
                                       const char *column, size_t count);

/**
 * @brief Add ORDER BY column ASC/DESC
 */
C_ORM_EXPORT int c_orm_select_order_by(c_orm_select_builder_t *builder,
                                       const char *column, int is_desc);

/**
 * @brief Add LIMIT n
 */
C_ORM_EXPORT int c_orm_select_limit(c_orm_select_builder_t *builder,
                                    size_t limit);

/**
 * @brief Add OFFSET n
 */
C_ORM_EXPORT int c_orm_select_offset(c_orm_select_builder_t *builder,
                                     size_t offset);

/* INSERT BUILDER */
C_ORM_EXPORT int
c_orm_insert_builder_init(const c_orm_table_meta_t *meta,
                          c_orm_insert_builder_t **out_builder);
C_ORM_EXPORT void c_orm_insert_builder_free(c_orm_insert_builder_t *builder);
C_ORM_EXPORT int c_orm_insert_builder_compile(c_orm_insert_builder_t *builder,
                                              char **out_sql);

/* UPDATE BUILDER */
C_ORM_EXPORT int
c_orm_update_builder_init(const c_orm_table_meta_t *meta,
                          c_orm_update_builder_t **out_builder);
C_ORM_EXPORT void c_orm_update_builder_free(c_orm_update_builder_t *builder);
C_ORM_EXPORT int c_orm_update_set(c_orm_update_builder_t *builder,
                                  const char *column);
C_ORM_EXPORT int c_orm_update_where_eq(c_orm_update_builder_t *builder,
                                       const char *column);
C_ORM_EXPORT int c_orm_update_builder_compile(c_orm_update_builder_t *builder,
                                              char **out_sql);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* C_ORM_QUERY_BUILDER_H */
