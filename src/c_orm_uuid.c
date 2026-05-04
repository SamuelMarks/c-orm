

/**
 * @file c_orm_uuid.c
 * @brief Implementation of UUID generation.
 */

/* clang-format off */
#include "c_orm_uuid.h"
#include "c_orm_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
/* clang-format on */

/**
 * @brief Flag indicating if the PRNG has been seeded.
 */
static int c_orm_uuid_seeded = 0;

C_ORM_EXPORT c_orm_error_t c_orm_uuid_v4(char out_uuid[37]) {
  int rc;
  int i;
  unsigned char bytes[16];

  LOG_DEBUG("c_orm_uuid_v4: entry");

  if (!out_uuid) {
    LOG_DEBUG("c_orm_uuid_v4: out_uuid is NULL, returning C_ORM_ERROR_MEMORY");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  if (!c_orm_uuid_seeded) {
    LOG_DEBUG("c_orm_uuid_v4: seeding PRNG");
    srand((unsigned int)time(NULL));
    c_orm_uuid_seeded = 1;
  }

  for (i = 0; i < 16; i++) {
    bytes[i] = (unsigned char)(rand() % 256);
  }

  /* Set version to 4 (random) */
  bytes[6] = (unsigned char)((bytes[6] & 0x0f) | 0x40);
  /* Set variant to RFC 4122 */
  bytes[8] = (unsigned char)((bytes[8] & 0x3f) | 0x80);

#if defined(_MSC_VER)
  sprintf_s(
      out_uuid, 37,
      "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
      bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6],
      bytes[7], bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13],
      bytes[14], bytes[15]);
#else
  sprintf(
      out_uuid,
      "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
      bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6],
      bytes[7], bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13],
      bytes[14], bytes[15]);
#endif

  LOG_DEBUG("c_orm_uuid_v4: exit with C_ORM_OK. uuid=%s", out_uuid);
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}
