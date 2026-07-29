/* clang-format off */
#include <stdarg.h>
#include <stdio.h>
#include "c_orm_safe_crt.h"
/* clang-format on */

/* Fix undefined reference to g_fail_io_after in cdd-c when built without tests
 */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak)) volatile int g_fail_io_after = -1;
#elif defined(_MSC_VER)
C_ORM_EXPORT volatile int g_fail_io_after = -1;
#endif

#if defined(__clang__) || defined(__GNUC__)
__attribute__((__format__(__printf__, 1, 2)))
#endif
C_ORM_EXPORT c_orm_error_t C_CDD_LOG_DEBUG(const char *fmt, ...);

C_ORM_EXPORT c_orm_error_t C_CDD_LOG_DEBUG(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
  vfprintf(stderr, fmt, args);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
  va_end(args);
  return 0;
}

C_ORM_EXPORT c_orm_error_t c_orm_sprintf(char *buf, size_t size,
                                         const char *format, ...) {
  int ret;
  va_list args;
  va_start(args, format);
#if defined(_MSC_VER)
  ret = vsprintf_s(buf, size, format, args);
#else
  (void)size;
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
  ret = vsprintf(buf, format, args);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#endif
  va_end(args);
  return ret;
}
