/**
 * @file c_orm_log.c
 * @brief Implementation of logging utilities.
 */

/* clang-format off */
#include "c_orm_log.h"
#include <stdio.h>
#include <stdarg.h>
/* clang-format on */

C_ORM_EXPORT void c_orm_log_debug(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  fprintf(stderr, "[DEBUG] ");
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
  vfprintf(stderr, fmt, args);
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
  fprintf(stderr, "\n");
  va_end(args);
}

