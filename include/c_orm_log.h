/**
 * @file c_orm_log.h
 * @brief Logging utilities and debug macros for c-orm.
 */

#ifndef C_ORM_LOG_H
#define C_ORM_LOG_H

/* clang-format off */
#include "c_orm_api.h"
#include <stdio.h>
#include <stdarg.h>
/* clang-format on */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#if (defined(__clang__) || defined(__GNUC__)) && !defined(__MINGW32__)
__attribute__((__format__(__printf__, 1, 2)))
#endif
/**
 * @brief Logs a debug message to standard error.
 *
 * @param fmt The format string.
 * @param ... The format arguments.
 * @return C_ORM_OK on success, or C_ORM_ERROR_VALIDATION if `fmt` is NULL.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_log_debug(const char *fmt, ...);

#ifndef LOG_DEBUG
#ifdef DEBUG
#define LOG_DEBUG(msg)                                                         \
  do {                                                                         \
    if (c_orm_log_debug("%s", (msg)) != C_ORM_OK) {                            \
    }                                                                          \
  } while (0)
#else
#define LOG_DEBUG(msg)                                                         \
  do {                                                                         \
  } while (0)
#endif /* DEBUG */
#endif /* !LOG_DEBUG */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* C_ORM_LOG_H */
