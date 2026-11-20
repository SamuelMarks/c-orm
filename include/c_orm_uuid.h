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
 * @brief Fill a buffer with cryptographically secure random bytes from the OS
 * CSPRNG (`RtlGenRandom` on Windows, `/dev/urandom` on POSIX,
 * `crypto.getRandomValues` on Emscripten).
 *
 * @param out_bytes Destination byte buffer.
 * @param len Number of random bytes to generate.
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER if `out_bytes` is NULL
 * with `len > 0`, or C_ORM_ERROR_IO / C_ORM_ERROR_CRYPTO on OS CSPRNG failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_random_bytes(unsigned char *out_bytes,
                                              size_t len);

/**
 * @brief Generate a random UUID v4 string (Step 169).
 *
 * @param out_uuid Buffer to receive the null-terminated UUID string.
 *                 Must be at least 37 bytes long (36 chars + 1 null).
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_uuid_v4(char out_uuid[37]);

#ifdef C_ORM_TEST_ALLOCATOR
/**
 * @brief Optional override path for `/dev/urandom` in fault-injection tests.
 */
C_ORM_EXPORT extern const char *c_orm_mock_urandom_path;
#endif /* C_ORM_TEST_ALLOCATOR */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* C_ORM_UUID_H */
