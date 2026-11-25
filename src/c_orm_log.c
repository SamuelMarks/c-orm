/**
 * @file c_orm_log.c
 * @brief Implementation of logging utilities.
 */

/* clang-format off */
#include "c_orm_log.h"
#include <stdio.h>
#include <stdarg.h>
/* clang-format on */

/**
 * @brief Logs a debug message to standard error.
 * @param fmt The format string.
 * @param ... Format arguments.
 * @return C_ORM_OK on success, or C_ORM_ERROR_VALIDATION if fmt is NULL.
 */
C_ORM_EXPORT c_orm_error_t c_orm_log_debug(const char *fmt, ...) {
  va_list args;
  if (!fmt) {
    return C_ORM_ERROR_VALIDATION;
  }
  va_start(args, fmt);
  fprintf(stderr, "[DEBUG] ");
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
  va_end(args);
  return C_ORM_OK;
}
