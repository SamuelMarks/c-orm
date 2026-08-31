#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file c_orm_postgres.h
 * @brief PostgreSQL driver implementation for c-orm.
 */

#ifndef C_ORM_POSTGRES_H
#define C_ORM_POSTGRES_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_db.h"
/* clang-format on */

/**
 * @brief Get the PostgreSQL driver vtable.
 *
 * @param out_vtable Output parameter to store the vtable pointer.
 * @return 0 on success, non-zero on failure.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_postgres_get_vtable(const c_orm_driver_vtable_t **out_vtable);

/**
 * @brief Connect to a PostgreSQL database.
 *
 * @param url Connection string (e.g., "host=localhost dbname=test user=test").
 * @param out_db Output parameter to store the database handle.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_postgres_connect(const char *url,
                                                  c_orm_db_t **out_db);

/**
 * @brief Create a new Large Object in PostgreSQL (Step 262).
 *
 * @param db The database connection.
 * @param out_oid Returns the OID of the newly created Large Object.
 */
C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_create(c_orm_db_t *db,
                                                    unsigned int *out_oid);

/**
 * @brief Open a PostgreSQL Large Object for streaming.
 *
 * @param db The database connection.
 * @param oid The OID of the Large Object.
 * @param mode The mode (e.g., INV_READ or INV_WRITE).
 * @param out_fd Returns the file descriptor (int) cast to void*.
 */
C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_open(c_orm_db_t *db,
                                                  unsigned int oid, int mode,
                                                  void **out_fd);

/**
 * @brief Read from a PostgreSQL Large Object.
 *
 * @param db The database connection.
 * @param fd The file descriptor returned by lo_open.
 * @param buffer The buffer to read into.
 * @param len The number of bytes to read.
 * @param out_read Returns the actual number of bytes read.
 */
C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_read(c_orm_db_t *db, void *fd,
                                                  void *buffer, size_t len,
                                                  size_t *out_read);

/**
 * @brief Write to a PostgreSQL Large Object.
 *
 * @param db The database connection.
 * @param fd The file descriptor returned by lo_open.
 * @param buffer The buffer to write from.
 * @param len The number of bytes to write.
 * @param out_written Returns the actual number of bytes written.
 */
C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_write(c_orm_db_t *db, void *fd,
                                                   const void *buffer,
                                                   size_t len,
                                                   size_t *out_written);

/**
 * @brief Close a PostgreSQL Large Object.
 *
 * @param db The database connection.
 * @param fd The file descriptor returned by lo_open.
 */
C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_close(c_orm_db_t *db, void *fd);

#if defined(__cplusplus)
}
#endif /* __cplusplus */
#endif /* C_ORM_POSTGRES_H */

#if defined(__clang__) || defined(__GNUC__)
#endif
