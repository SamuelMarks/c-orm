#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file perfect.c
 * @brief Minimal compilation and verification target.
 */

/* clang-format off */
#include "c_orm_api.h"
#include <stddef.h>
/* clang-format on */

int main(void);

/**
 * @brief Main entry point for perfect compilation test.
 *
 * @return 0 on success.
 */
int main(void) {
  c_orm_error_t rc;
  rc = C_ORM_OK;
  return (int)rc;
}

#if defined(__clang__) || defined(__GNUC__)
#endif
