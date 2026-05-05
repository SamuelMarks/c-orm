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
#endif

/**
 * @brief Get the MySQL driver vtable.
 *
 * @param out_vtable Output parameter to store the vtable pointer.
 * @return 0 on success, non-zero on failure.
 */
C_ORM_EXPORT int
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

