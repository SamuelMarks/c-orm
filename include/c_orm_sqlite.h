/**
 * @file c_orm_sqlite.h
 * @brief SQLite3 driver implementation for c-orm.
 */

#ifndef C_ORM_SQLITE_H
#define C_ORM_SQLITE_H

/* clang-format off */
#include "c_orm_db.h"
/* clang-format on */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
/**
 * @brief Create a new SQLite database connection.
 *
 * @param url The file path to the SQLite database.
 * @param out_db The resulting c-orm database handle.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_sqlite_connect(const char *url,
                                                c_orm_db_t **out_db);

/**
 * @brief Initialize SQLite backend implicitly. Gets vtable.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_sqlite_get_vtable(const c_orm_driver_vtable_t **out_vtable);

/**
 * @brief Open a SQLite BLOB for streaming read/write operations (Step 261).
 *
 * @param db The database handle.
 * @param db_name The logical database name (usually "main").
 * @param table The table name containing the BLOB.
 * @param column The column name of the BLOB.
 * @param row_id The primary key (ROWID) of the row.
 * @param is_read_write Set to 1 for write access, 0 for read-only.
 * @param out_blob_handle Returns a handle to the opened BLOB (sqlite3_blob*
 * cast to void*).
 */
C_ORM_EXPORT c_orm_error_t c_orm_sqlite_blob_open(
    c_orm_db_t *db, const char *db_name, const char *table, const char *column,
    int64_t row_id, int is_read_write, void **out_blob_handle);

/**
 * @brief Read data from an opened SQLite BLOB.
 *
 * @param blob_handle The BLOB handle from c_orm_sqlite_blob_open.
 * @param buffer The buffer to read into.
 * @param n The number of bytes to read.
 * @param offset The byte offset to read from.
 */
C_ORM_EXPORT c_orm_error_t c_orm_sqlite_blob_read(void *blob_handle,
                                                  void *buffer, int n,
                                                  int offset);

/**
 * @brief Write data to an opened SQLite BLOB.
 *
 * @param blob_handle The BLOB handle from c_orm_sqlite_blob_open.
 * @param buffer The buffer to write from.
 * @param n The number of bytes to write.
 * @param offset The byte offset to write to.
 */
C_ORM_EXPORT c_orm_error_t c_orm_sqlite_blob_write(void *blob_handle,
                                                   const void *buffer, int n,
                                                   int offset);

/**
 * @brief Close an opened SQLite BLOB handle.
 */
C_ORM_EXPORT c_orm_error_t c_orm_sqlite_blob_close(void *blob_handle);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* C_ORM_SQLITE_H */
