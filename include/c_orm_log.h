#if defined(__clang__) || defined(__GNUC__)
#endif
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

#ifndef LOG_DEBUG
/**
 * @brief Logs a debug message to standard error.
 *
 * @param fmt The format string.
 * @param ... The format arguments.
 */
#ifdef DEBUG
#if defined(__clang__) || defined(__GNUC__)
__attribute__((__format__(__printf__, 1, 2)))
#endif
C_ORM_EXPORT void
c_orm_log_debug(const char *fmt, ...);
#define LOG_DEBUG c_orm_log_debug
#else
#if defined(__clang__) || defined(__GNUC__)
__attribute__((__format__(__printf__, 1, 2)))
#endif
C_ORM_EXPORT void
c_orm_log_debug(const char *fmt, ...);
#define LOG_DEBUG 1 ? (void)0 : c_orm_log_debug
#endif /* DEBUG */
#endif /* !LOG_DEBUG */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* C_ORM_LOG_H */

#if defined(__clang__) || defined(__GNUC__)
#endif
