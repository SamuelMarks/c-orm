/**
 * @file c_orm_memory.h
 * @brief Ephemeral Memory driver implementation for c-orm.
 */

#ifndef C_ORM_MEMORY_H
#define C_ORM_MEMORY_H

/* clang-format off */
#include "c_orm_db.h"
/* clang-format on */

#ifdef __cplusplus
extern "C" {
#endif
/**
 * @brief Create a new Ephemeral Memory database connection.
 *
 * @param url The connection string (ignored for memory).
 * @param out_db The resulting c-orm database handle.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_memory_connect(const char *url,
                                                c_orm_db_t **out_db);

/**
 * @brief Get the Memory vtable.
 */
C_ORM_EXPORT int
c_orm_memory_get_vtable(const c_orm_driver_vtable_t **out_vtable);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* C_ORM_MEMORY_H */