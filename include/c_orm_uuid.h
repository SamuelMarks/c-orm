/**
 * @file c_orm_uuid.h
 * @brief UUID generation utilities for c-orm.
 */

#ifndef C_ORM_UUID_H
#define C_ORM_UUID_H

/* clang-format off */
#include "c_orm_api.h"
/* clang-format on */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @brief Generate a random UUID v4 string (Step 169).
 *
 * @param out_uuid Buffer to receive the null-terminated UUID string.
 *                 Must be at least 37 bytes long (36 chars + 1 null).
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_uuid_v4(char out_uuid[37]);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* C_ORM_UUID_H */
