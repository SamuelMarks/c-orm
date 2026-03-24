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
 * @brief Support for array arguments dynamically bridging IN clauses (Step
 * 123).
 * @param array Native generic array.
 * @param meta Type of array elements.
 */
C_ORM_EXPORT int c_orm_select_where_in_array(c_orm_select_builder_t *builder,
                                             const char *column, void *array,
                                             const c_orm_table_meta_t *meta);

/**
 * @brief Add WHERE column BETWEEN ? AND ? (Step 124)
 */
C_ORM_EXPORT int c_orm_select_where_between(c_orm_select_builder_t *builder,
                                            const char *column);

/**
 * @brief Add WHERE column ILIKE ? (Case-insensitive LIKE) (Step 125)
 */
C_ORM_EXPORT int c_orm_select_where_ilike(c_orm_select_builder_t *builder,
                                          const char *column);

/**
 * @brief Extend query builder for relationship filtering bridging JOIN
 * resolutions (Step 121, 122)
 *
 * @param relation_name The dot-separated relationship path (e.g.
 * `profile.bio`).
 * @param operator_str Raw operator (e.g. `ILIKE`, `=`, `>`).
 */
C_ORM_EXPORT int c_orm_select_where_relation(c_orm_select_builder_t *builder,
                                             const char *relation_name,
                                             const char *operator_str);

/**
 * @brief Add GROUP BY aggregation query logic (Step 126).
 */
C_ORM_EXPORT int c_orm_select_group_by(c_orm_select_builder_t *builder,
                                       const char *column);

/**
 * @brief Add HAVING API logic (Step 127).
 */
C_ORM_EXPORT int c_orm_select_having(c_orm_select_builder_t *builder,
                                     const char *clause);

/**
 * @brief Add Support for COUNT, SUM, AVG, MIN, MAX aggregations (Step 128).
 * @param func String representing aggregation (e.g. `COUNT`, `MAX`).
 */
C_ORM_EXPORT int c_orm_select_aggregate(c_orm_select_builder_t *builder,
                                        const char *func, const char *column,
                                        const char *alias);

/**
 * @brief Add ORDER BY column ASC/DESC */
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
