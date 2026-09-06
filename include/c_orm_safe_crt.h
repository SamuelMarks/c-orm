#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file c_orm_safe_crt.h
 * @brief Safe CRT wrappers and fallback macros.
 */

#ifndef C_ORM_SAFE_CRT_H
#define C_ORM_SAFE_CRT_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_meta.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

#if defined(__clang__) || defined(__GNUC__)
__attribute__((__format__(__printf__, 3, 4)))
#endif
C_ORM_EXPORT int
c_orm_sprintf(char *buf, size_t size, const char *format, ...);
#define C_ORM_SPRINTF c_orm_sprintf

#if defined(_MSC_VER)
#define C_ORM_STRCPY(dest, size, src) strcpy_s(dest, size, src)
#define C_ORM_STRNCPY(dest, size, src, count) strncpy_s(dest, size, src, count)
#define C_ORM_STRCAT(dest, size, src) strcat_s(dest, size, src)
#define C_ORM_STRNCAT(dest, size, src, count) strncat_s(dest, size, src, count)
#define C_ORM_STRTOLL(nptr, endptr, base) _strtoi64(nptr, endptr, base)
#define C_ORM_FOPEN(fp_ptr, filename, mode) fopen_s(fp_ptr, filename, mode)
#define C_ORM_TMPFILE(fp_ptr) tmpfile_s(fp_ptr)
#define C_ORM_SSCANF sscanf_s
#define C_ORM_GETENV(dest, size, var_name)                                     \
  do {                                                                         \
    size_t _len;                                                               \
    getenv_s(&_len, dest, size, var_name);                                     \
  } while (0)
#define C_ORM_UNSETENV(var_name) _putenv_s(var_name, "")
#elif defined(__GNUC__) || defined(__clang__)
#if defined(_WIN32)
#define C_ORM_UNSETENV(var_name) _putenv_s(var_name, "")
#else
int unsetenv(const char *name);
#define C_ORM_UNSETENV(var_name) unsetenv(var_name)
#endif
#define C_ORM_STRCPY(dest, size, src) strcpy(dest, src)
#define C_ORM_STRNCPY(dest, size, src, count) strncpy(dest, src, count)
#define C_ORM_STRCAT(dest, size, src) strcat(dest, src)
#define C_ORM_STRNCAT(dest, size, src, count) strncat(dest, src, count)
#define C_ORM_STRTOLL(nptr, endptr, base) strtoll(nptr, endptr, base)
#define C_ORM_FOPEN(fp_ptr, filename, mode)                                    \
  (*(fp_ptr) = fopen(filename, mode), *(fp_ptr) == NULL ? 1 : 0)
#define C_ORM_TMPFILE(fp_ptr) (*(fp_ptr) = tmpfile(), *(fp_ptr) == NULL ? 1 : 0)
#define C_ORM_SSCANF sscanf
#define C_ORM_GETENV(dest, size, var_name)                                     \
  do {                                                                         \
    const char *_tmp = getenv(var_name);                                       \
    if (_tmp) {                                                                \
      C_ORM_STRNCPY(dest, size, _tmp, size - 1);                               \
      (dest)[(size) - 1] = '\0';                                               \
    } else {                                                                   \
      (dest)[0] = '\0';                                                        \
    }                                                                          \
  } while (0)
#else
#define C_ORM_STRCPY(dest, size, src) strcpy(dest, src)
#define C_ORM_STRNCPY(dest, size, src, count) strncpy(dest, src, count)
#define C_ORM_STRCAT(dest, size, src) strcat(dest, src)
#define C_ORM_STRNCAT(dest, size, src, count) strncat(dest, src, count)
#define C_ORM_STRTOLL(nptr, endptr, base) strtol(nptr, endptr, base)
#define C_ORM_FOPEN(fp_ptr, filename, mode)                                    \
  (*(fp_ptr) = fopen(filename, mode), *(fp_ptr) == NULL ? 1 : 0)
#define C_ORM_TMPFILE(fp_ptr) (*(fp_ptr) = tmpfile(), *(fp_ptr) == NULL ? 1 : 0)
#define C_ORM_SSCANF sscanf
#define C_ORM_GETENV(dest, size, var_name)                                     \
  do {                                                                         \
    const char *_tmp = getenv(var_name);                                       \
    if (_tmp) {                                                                \
      C_ORM_STRNCPY(dest, size, _tmp, size - 1);                               \
      (dest)[(size) - 1] = '\0';                                               \
    } else {                                                                   \
      (dest)[0] = '\0';                                                        \
    }                                                                          \
  } while (0)
#define C_ORM_UNSETENV(var_name) unsetenv(var_name)
#endif

#if defined(_MSC_VER)
typedef __int64 c_orm_int64_t;
typedef unsigned __int64 c_orm_uint64_t;
#elif defined(__GNUC__) || defined(__clang__)
__extension__ typedef long long c_orm_int64_t;
__extension__ typedef unsigned long long c_orm_uint64_t;
#else
/* Fallback for other strictly C89 compilers if they have no 64-bit int */
typedef long c_orm_int64_t;
typedef unsigned long c_orm_uint64_t;
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* C_ORM_SAFE_CRT_H */

#if defined(__clang__) || defined(__GNUC__)
#endif
