/**
 * @file c_orm_api.h
 * @brief High-level API for c-orm: find, insert, update, delete.
 */

#ifndef C_ORM_API_H
#define C_ORM_API_H
/* clang-format off */
#include "c_orm_db.h"
#include "c_orm_meta.h"
/* clang-format on */

#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief Find a single record by its primary key.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param id_val Primary key value to search for (assumes string/int is passed
 * appropriately, but currently expects int32 for basic testing, we can pass as
 * void*). For safety we will pass as int32_t for now.
 * @param out_struct Pointer to an already allocated struct to hydrate.
 * @return C_ORM_OK on success, C_ORM_ERROR_NOT_FOUND if no row.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_find_by_id_int32(c_orm_db_t *db, const c_orm_table_meta_t *meta,
                       int32_t id_val, void *out_struct);

/**
 * @brief Find all records.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param out_array Pointer to the generic Array struct. Data will be allocated
 * dynamically.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_find_all(c_orm_db_t *db,
                                          const c_orm_table_meta_t *meta,
                                          void *out_array);

/**
 * @brief Insert a new record into the database.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param in_struct Pointer to the struct containing data to insert.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_insert(c_orm_db_t *db,
                                        const c_orm_table_meta_t *meta,
                                        const void *in_struct);

/**
 * @brief Update an existing record in the database by its primary key.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param in_struct Pointer to the struct containing the updated data (and PK).
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_update(c_orm_db_t *db,
                                        const c_orm_table_meta_t *meta,
                                        const void *in_struct);

/**
 * @brief Delete a record from the database by its primary key.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param id_val Primary key value to delete.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_delete_by_id_int32(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, int32_t id_val);

/**
 * @brief Delete a record from the database by its string primary key.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param id_val Primary key string to delete.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_delete_by_id_string(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, const char *id_val);

/**
 * @brief Find a single record by its string primary key.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param id_val Primary key value to search for.
 * @param out_struct Pointer to an already allocated struct to hydrate.
 * @return C_ORM_OK on success, C_ORM_ERROR_NOT_FOUND if no row.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_find_by_id_string(c_orm_db_t *db, const c_orm_table_meta_t *meta,
                        const char *id_val, void *out_struct);

/**
 * @brief Find a single record by a specific string column.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param column_name The column to filter by.
 * @param value The value to search for.
 * @param out_struct Pointer to an already allocated struct to hydrate.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_find_one_by_string(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, const char *column_name,
    const char *value, void *out_struct);

/**
 * @brief Hydrate an array from an already prepared and optionally bound query.
 *
 * @param db Database connection.
 * @param query Prepared and optionally bound query.
 * @param meta Table metadata.
 * @param out_array Pointer to the generic Array struct.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_hydrate_all(c_orm_db_t *db,
                                             c_orm_query_t *query,
                                             const c_orm_table_meta_t *meta,
                                             void *out_array);

/**
 * @brief Execute a raw query string that returns no results.
 */
C_ORM_EXPORT c_orm_error_t c_orm_execute_raw(c_orm_db_t *db, const char *sql);

/* Transaction APIs */
C_ORM_EXPORT c_orm_error_t c_orm_transaction_begin(c_orm_db_t *db);
C_ORM_EXPORT c_orm_error_t c_orm_transaction_commit(c_orm_db_t *db);
C_ORM_EXPORT c_orm_error_t c_orm_transaction_rollback(c_orm_db_t *db);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* C_ORM_API_H */
