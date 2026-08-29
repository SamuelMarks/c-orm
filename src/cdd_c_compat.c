#if defined(__clang__) || defined(__GNUC__)
#endif
/* clang-format off */
#include <stdarg.h>
#include <stdio.h>
#include "c_orm_meta.h"
/* clang-format on */

/* Fix undefined reference to g_fail_io_after in cdd-c when built without tests
   (Now provided by cdd-c master directly)
 */

#if defined(__clang__) || defined(__GNUC__)
__attribute__((__format__(__printf__, 1, 2)))
#endif
C_ORM_EXPORT c_orm_error_t
C_CDD_LOG_DEBUG(const char *fmt, ...);

C_ORM_EXPORT c_orm_error_t C_CDD_LOG_DEBUG(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  return 0;
}

#if defined(__clang__) || defined(__GNUC__)
#endif
