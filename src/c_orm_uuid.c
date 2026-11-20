/**
 * @file c_orm_uuid.c
 * @brief Implementation of UUID generation.
 */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_uuid.h"
#include "c_orm_log.h"
#include <stdio.h>
#include <stdlib.h>
/* clang-format on */

#if defined(_WIN32) || defined(_WIN64)
/**
 * @brief Forward declaration of Windows RtlGenRandom (SystemFunction036) in
 * advapi32.dll without including windows.h.
 * @param RandomBuffer Output buffer receiving random bytes.
 * @param RandomBufferLength Number of random bytes to generate.
 * @return Non-zero on success, 0 on failure.
 */
extern __declspec(dllimport) unsigned char __stdcall SystemFunction036(
    void *RandomBuffer, unsigned long RandomBufferLength);
#endif

#ifdef C_ORM_TEST_ALLOCATOR
/**
 * @brief Optional override path for `/dev/urandom` in fault-injection tests.
 */
C_ORM_EXPORT const char *c_orm_mock_urandom_path = NULL;
#endif /* C_ORM_TEST_ALLOCATOR */

C_ORM_EXPORT c_orm_error_t c_orm_random_bytes(unsigned char *out_bytes,
                                              size_t len) {
  if (len == 0) {
    return C_ORM_OK;
  }
  if (!out_bytes) {
    return C_ORM_ERROR_NULL_POINTER;
  }
#if defined(_WIN32) || defined(_WIN64)
  if (!SystemFunction036(out_bytes, (unsigned long)len)) {
    return C_ORM_ERROR_CRYPTO;
  }
  return C_ORM_OK;
#else
  {
    FILE *fp = NULL;
    size_t nread;
    const char *rng_path = "/dev/urandom";
    c_orm_error_t rc;

#ifdef C_ORM_TEST_ALLOCATOR
    if (c_orm_mock_urandom_path) {
      rng_path = c_orm_mock_urandom_path;
    }
#endif
    rc = C_ORM_FOPEN(&fp, rng_path, "rb");
    if (rc != C_ORM_OK) {
      return C_ORM_ERROR_IO;
    }
    nread = fread(out_bytes, 1, len, fp);
    fclose(fp);
    if (nread != len) {
      return C_ORM_ERROR_IO;
    }
    return C_ORM_OK;
  }
#endif
}

/**
 * @brief Fills a 16-byte buffer with cryptographically secure random bytes.
 * @param out_bytes 16-byte output buffer.
 * @return C_ORM_OK on success, or C_ORM_ERROR_IO / C_ORM_ERROR_CRYPTO on
 * failure.
 */
static c_orm_error_t c_orm_fill_random_16(unsigned char out_bytes[16]) {
  return c_orm_random_bytes(out_bytes, 16);
}

C_ORM_EXPORT c_orm_error_t c_orm_uuid_v4(char out_uuid[37]) {
  static const char hex_digits[] = "0123456789abcdef";
  c_orm_error_t rc;
  unsigned char bytes[16];
  size_t i;
  size_t out_pos = 0;

  LOG_DEBUG("c_orm_uuid_v4: entry");

  if (!out_uuid) {
    LOG_DEBUG("c_orm_uuid_v4: out_uuid is NULL, returning C_ORM_ERROR_MEMORY");
    rc = C_ORM_ERROR_MEMORY;
    return rc;
  }

  rc = c_orm_fill_random_16(bytes);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_uuid_v4: CSPRNG failure");
    return rc;
  }

  /* Set version to 4 (random) */
  bytes[6] = (unsigned char)((bytes[6] & 0x0f) | 0x40);
  /* Set variant to RFC 4122 */
  bytes[8] = (unsigned char)((bytes[8] & 0x3f) | 0x80);

  for (i = 0; i < 16; ++i) {
    if (i == 4 || i == 6 || i == 8 || i == 10) {
      out_uuid[out_pos++] = '-';
    }
    out_uuid[out_pos++] = hex_digits[(bytes[i] >> 4) & 0x0f];
    out_uuid[out_pos++] = hex_digits[bytes[i] & 0x0f];
  }
  out_uuid[out_pos] = '\0';

  LOG_DEBUG("c_orm_uuid_v4: exit with C_ORM_OK. uuid=%s", out_uuid);
  return C_ORM_OK;
}
