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
#endif
/**
 * @brief Create a new SQLite database connection.
 *
 * @param url The file path to the SQLite database.
 * @param out_db The resulting c-orm database handle.
 * @return C_ORM_OK on success.
 */
c_orm_error_t c_orm_sqlite_connect(const char *url, c_orm_db_t **out_db);

/**
 * @brief Initialize SQLite backend implicitly. Gets vtable.
 */
int c_orm_sqlite_get_vtable(const c_orm_driver_vtable_t **out_vtable);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* C_ORM_SQLITE_H */
