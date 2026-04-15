#ifndef C_ORM_LOG_H
#define C_ORM_LOG_H

/* clang-format off */
#include "c_orm_api.h"
#include <stdio.h>
#include <stdarg.h>
/* clang-format on */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef LOG_DEBUG
#ifdef DEBUG
C_ORM_EXPORT void c_orm_log_debug(const char *fmt, ...);
#define LOG_DEBUG c_orm_log_debug
#else
C_ORM_EXPORT void c_orm_log_debug(const char *fmt, ...);
#define LOG_DEBUG 1 ? (void)0 : c_orm_log_debug
#endif /* DEBUG */
#endif /* !LOG_DEBUG */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* C_ORM_LOG_H */
