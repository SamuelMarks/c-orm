/* clang-format off */
#include <stdarg.h>
#include <stdio.h>
/* clang-format on */

int C_CDD_LOG_DEBUG(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  return 0;
}