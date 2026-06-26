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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

int c_orm_sprintf(char *buf, size_t size, const char *format, ...);
#define C_ORM_SPRINTF c_orm_sprintf

#if defined(_MSC_VER)
#define C_ORM_STRCPY(dest, size, src) strcpy_s(dest, size, src)
#define C_ORM_STRNCPY(dest, size, src, count) strncpy_s(dest, size, src, count)
#define C_ORM_STRCAT(dest, size, src) strcat_s(dest, size, src)
#define C_ORM_STRTOLL(nptr, endptr, base) _strtoi64(nptr, endptr, base)
#else
#define C_ORM_STRCPY(dest, size, src) strcpy(dest, src)
#define C_ORM_STRNCPY(dest, size, src, count) strncpy(dest, src, count)
#define C_ORM_STRCAT(dest, size, src) strcat(dest, src)
#define C_ORM_STRTOLL(nptr, endptr, base) strtoll(nptr, endptr, base)
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* C_ORM_SAFE_CRT_H */

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
