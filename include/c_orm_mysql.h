/**
 * @file c_orm_mysql.h
 * @brief MySQL/MariaDB driver implementation for c-orm.
 */

#ifndef C_ORM_MYSQL_H
#define C_ORM_MYSQL_H

/* clang-format off */
#include "c_orm_db.h"
/* clang-format on */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @brief Parsed MySQL connection URL components.
 */
typedef struct c_orm_mysql_url_info {
  /** @brief Server hostname or IP address. */
  char host[128];
  /** @brief Username credential. */
  char user[64];
  /** @brief Password credential. */
  char password[64];
  /** @brief Target database schema name. */
  char dbname[64];
  /** @brief Optional Unix domain socket path from query string. */
  char unix_socket[256];
  /** @brief TCP port (0 for default). */
  unsigned int port;
  /** @brief Non-zero when SSL/TLS is requested via URL query string. */
  int use_ssl;
} c_orm_mysql_url_info_t;

/**
 * @brief Parse a `mysql://` connection URL including `unix_socket` and `ssl`
 * query parameters.
 *
 * @param url Connection URL string.
 * @param out_info Output structure populated with parsed parameters.
 * @return C_ORM_OK on success, or C_ORM_ERROR_NULL_POINTER on NULL arguments.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_mysql_parse_url(const char *url, c_orm_mysql_url_info_t *out_info);

/**
 * @brief Get the MySQL driver vtable.
 *
 * @param out_vtable Output parameter to store the vtable pointer.
 * @return C_ORM_OK on success, non-zero on failure.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_mysql_get_vtable(const c_orm_driver_vtable_t **out_vtable);

/**
 * @brief Connect to a MySQL database.
 *
 * @param url Connection string.
 * @param out_db Output parameter to store the database handle.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_mysql_connect(const char *url,
                                               c_orm_db_t **out_db);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* C_ORM_MYSQL_H */
