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
#endif /* __cplusplus */
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
C_ORM_EXPORT c_orm_error_t c_orm_select_builder_init(
    const c_orm_table_meta_t *meta, c_orm_select_builder_t **out_builder);

/**
 * @brief Free resources associated with a select builder.
 *
 * @param builder Select builder instance to free.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_select_builder_free(c_orm_select_builder_t *builder);

/**
 * @brief Compile the select builder into a final string.
 *
 * @param builder The builder.
 * @param out_sql Pointer to receive the generated SQL string (must free).
 * @return 0 on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_select_builder_compile(c_orm_select_builder_t *builder, char **out_sql);

/**
 * @brief Add WHERE column = ?
 *
 * @param builder Select builder instance.
 * @param column Column name string.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_select_where_eq(c_orm_select_builder_t *builder, const char *column);

/**
 * @brief Add WHERE column != ?
 *
 * @param builder Select builder instance.
 * @param column Column name string.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_select_where_neq(c_orm_select_builder_t *builder, const char *column);

/**
 * @brief Add WHERE column < ?
 *
 * @param builder Select builder instance.
 * @param column Column name string.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_select_where_lt(c_orm_select_builder_t *builder, const char *column);

/**
 * @brief Add WHERE column > ?
 *
 * @param builder Select builder instance.
 * @param column Column name string.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_select_where_gt(c_orm_select_builder_t *builder, const char *column);

/**
 * @brief Add WHERE column <= ?
 *
 * @param builder Select builder instance.
 * @param column Column name string.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_select_where_lte(c_orm_select_builder_t *builder, const char *column);

/**
 * @brief Add WHERE column >= ?
 *
 * @param builder Select builder instance.
 * @param column Column name string.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_select_where_gte(c_orm_select_builder_t *builder, const char *column);

/**
 * @brief Add WHERE column > CURRENT_TIMESTAMP
 *
 * @param builder Select builder instance.
 * @param column Column name string.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_select_where_gt_current_timestamp(
    c_orm_select_builder_t *builder, const char *column);

/**
 * @brief Add WHERE column < CURRENT_TIMESTAMP
 *
 * @param builder Select builder instance.
 * @param column Column name string.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_select_where_lt_current_timestamp(
    c_orm_select_builder_t *builder, const char *column);

/**
 * @brief Add WHERE column LIKE ?
 *
 * @param builder Select builder instance.
 * @param column Column name string.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_select_where_like(c_orm_select_builder_t *builder, const char *column);

/**
 * @brief Add WHERE column IN (?, ?, ...)
 *
 * @param builder Select builder instance.
 * @param column Column name string.
 * @param count Number of placeholder items in IN list.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_select_where_in(
    c_orm_select_builder_t *builder, const char *column, size_t count);

/**
 * @brief Support for array arguments dynamically bridging IN clauses (Step
 * 123).
 * @param builder Select builder instance.
 * @param column Column name string.
 * @param array Native generic array.
 * @param meta Type of array elements.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_select_where_in_array(c_orm_select_builder_t *builder, const char *column,
                            void *array, const c_orm_table_meta_t *meta);

/**
 * @brief Add WHERE column BETWEEN ? AND ? (Step 124)
 *
 * @param builder Select builder instance.
 * @param column Column name string.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_select_where_between(c_orm_select_builder_t *builder, const char *column);

/**
 * @brief Add WHERE column ILIKE ? (Case-insensitive LIKE) (Step 125)
 *
 * @param builder Select builder instance.
 * @param column Column name string.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_select_where_ilike(c_orm_select_builder_t *builder, const char *column);

/**
 * @brief Extend query builder for relationship filtering bridging JOIN
 * resolutions (Step 121, 122)
 *
 * @param builder Select builder instance.
 * @param relation_name The dot-separated relationship path (e.g.
 * `profile.bio`).
 * @param operator_str Raw operator (e.g. `ILIKE`, `=`, `>`).
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_select_where_relation(
    c_orm_select_builder_t *builder, const char *relation_name,
    const char *operator_str);

/**
 * @brief Add GROUP BY aggregation query logic (Step 126).
 *
 * @param builder Select builder instance.
 * @param column Column name string.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_select_group_by(c_orm_select_builder_t *builder, const char *column);

/**
 * @brief Add HAVING API logic (Step 127).
 *
 * @param builder Select builder instance.
 * @param clause HAVING condition clause string.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_select_having(c_orm_select_builder_t *builder,
                                               const char *clause);

/**
 * @brief Add Support for COUNT, SUM, AVG, MIN, MAX aggregations (Step 128).
 * @param builder Select builder instance.
 * @param func String representing aggregation (e.g. `COUNT`, `MAX`).
 * @param column Column name to aggregate.
 * @param alias Output column alias name.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_select_aggregate(c_orm_select_builder_t *builder, const char *func,
                       const char *column, const char *alias);

/**
 * @brief Add ORDER BY column ASC/DESC
 *
 * @param builder Select builder instance.
 * @param column Column name string.
 * @param is_desc Nonzero for DESC, zero for ASC.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_select_order_by(
    c_orm_select_builder_t *builder, const char *column, int is_desc);

/**
 * @brief Add LIMIT n
 *
 * @param builder Select builder instance.
 * @param limit Maximum rows to return.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_select_limit(c_orm_select_builder_t *builder,
                                              size_t limit);

/**
 * @brief Add OFFSET n
 *
 * @param builder Select builder instance.
 * @param offset Number of rows to skip.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_select_offset(c_orm_select_builder_t *builder,
                                               size_t offset);

/**
 * @brief Initialize an insert query builder.
 *
 * @param meta Table metadata.
 * @param out_builder Output pointer for the created builder.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_insert_builder_init(
    const c_orm_table_meta_t *meta, c_orm_insert_builder_t **out_builder);

/**
 * @brief Free resources associated with an insert query builder.
 *
 * @param builder The insert builder to free.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_insert_builder_free(c_orm_insert_builder_t *builder);

/**
 * @brief Compile the insert builder into a final string.
 *
 * @param builder The insert builder to compile.
 * @param out_sql Pointer to receive the compiled SQL string.
 * @return 0 on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_insert_builder_compile(c_orm_insert_builder_t *builder, char **out_sql);

/**
 * @brief Initialize an update query builder.
 *
 * @param meta Table metadata.
 * @param out_builder Output pointer for the created builder.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_update_builder_init(
    const c_orm_table_meta_t *meta, c_orm_update_builder_t **out_builder);

/**
 * @brief Free resources associated with an update query builder.
 *
 * @param builder The update builder to free.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_update_builder_free(c_orm_update_builder_t *builder);

/**
 * @brief Add a SET clause to the update builder.
 *
 * @param builder The update builder.
 * @param column The column to set.
 * @return 0 on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_update_set(c_orm_update_builder_t *builder,
                                            const char *column);

/**
 * @brief Add a WHERE equal clause to the update builder.
 *
 * @param builder The update builder.
 * @param column The column to compare.
 * @return 0 on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_update_where_eq(c_orm_update_builder_t *builder, const char *column);

/**
 * @brief Compile the update builder into a final string.
 *
 * @param builder The update builder to compile.
 * @param out_sql Pointer to receive the compiled SQL string.
 * @return 0 on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_update_builder_compile(c_orm_update_builder_t *builder, char **out_sql);

/**
 * @brief Configure whether a select builder emits PostgreSQL `$N` placeholders.
 *
 * @param builder Select builder instance.
 * @param is_postgres Non-zero for PostgreSQL `$N` placeholders, 0 for `?`.
 * @return C_ORM_OK on success, or C_ORM_ERROR_UNKNOWN if builder is NULL.
 */
C_ORM_EXPORT c_orm_error_t c_orm_select_builder_set_dialect(
    c_orm_select_builder_t *builder, int is_postgres);

/**
 * @brief Configure whether an insert builder emits PostgreSQL `$N`
 * placeholders.
 *
 * @param builder Insert builder instance.
 * @param is_postgres Non-zero for PostgreSQL `$N` placeholders, 0 for `?`.
 * @return C_ORM_OK on success, or C_ORM_ERROR_UNKNOWN if builder is NULL.
 */
C_ORM_EXPORT c_orm_error_t c_orm_insert_builder_set_dialect(
    c_orm_insert_builder_t *builder, int is_postgres);

/**
 * @brief Configure whether an update builder emits PostgreSQL `$N`
 * placeholders.
 *
 * @param builder Update builder instance.
 * @param is_postgres Non-zero for PostgreSQL `$N` placeholders, 0 for `?`.
 * @return C_ORM_OK on success, or C_ORM_ERROR_UNKNOWN if builder is NULL.
 */
C_ORM_EXPORT c_orm_error_t c_orm_update_builder_set_dialect(
    c_orm_update_builder_t *builder, int is_postgres);

/**
 * @brief Configure the target SQL dialect on a select builder.
 *
 * @param builder Select builder instance.
 * @param dialect Target SQL dialect enum (`C_ORM_DIALECT_DB2`, etc.).
 * @return C_ORM_OK on success, or C_ORM_ERROR_UNKNOWN if builder is NULL.
 */
C_ORM_EXPORT c_orm_error_t c_orm_select_builder_set_dialect_kind(
    c_orm_select_builder_t *builder, c_orm_dialect_t dialect);

/**
 * @brief Configure the target SQL dialect on an insert builder.
 *
 * @param builder Insert builder instance.
 * @param dialect Target SQL dialect enum (`C_ORM_DIALECT_DB2`, etc.).
 * @return C_ORM_OK on success, or C_ORM_ERROR_UNKNOWN if builder is NULL.
 */
C_ORM_EXPORT c_orm_error_t c_orm_insert_builder_set_dialect_kind(
    c_orm_insert_builder_t *builder, c_orm_dialect_t dialect);

/**
 * @brief Configure the target SQL dialect on an update builder.
 *
 * @param builder Update builder instance.
 * @param dialect Target SQL dialect enum (`C_ORM_DIALECT_DB2`, etc.).
 * @return C_ORM_OK on success, or C_ORM_ERROR_UNKNOWN if builder is NULL.
 */
C_ORM_EXPORT c_orm_error_t c_orm_update_builder_set_dialect_kind(
    c_orm_update_builder_t *builder, c_orm_dialect_t dialect);

/**
 * @brief Enable or disable upsert (`MERGE INTO` on Db2/ODBC, `ON CONFLICT` on
 * SQLite/Postgres, `ON DUPLICATE KEY UPDATE` on MySQL) on an insert builder.
 *
 * @param builder Insert builder instance.
 * @param is_upsert Non-zero to emit upsert SQL on compile, 0 for plain INSERT.
 * @return C_ORM_OK on success, or C_ORM_ERROR_UNKNOWN if builder is NULL.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_insert_builder_set_upsert(c_orm_insert_builder_t *builder, int is_upsert);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* C_ORM_QUERY_BUILDER_H */
