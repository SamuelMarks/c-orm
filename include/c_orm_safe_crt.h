/**
 * @file c_orm_safe_crt.h
 * @brief Safe CRT wrappers and fallback macros.
 */

#ifndef C_ORM_SAFE_CRT_H
#define C_ORM_SAFE_CRT_H

/* clang-format off */
#include "c_orm_meta.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#if defined(__clang__) || defined(__GNUC__)
__attribute__((__format__(__printf__, 3, 4)))
#endif
/**
 * @brief Safe sprintf wrapper.
 *
 * @param buf Output buffer.
 * @param size Buffer size.
 * @param format Printf format string.
 * @param ... Format arguments.
 * @return C_ORM_OK on success, or C_ORM_ERROR_UNKNOWN on error.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_sprintf(char *buf, size_t size, const char *format, ...);
#define C_ORM_SPRINTF c_orm_sprintf

/**
 * @brief Safe fopen wrapper.
 *
 * @param fp_ptr Pointer to file handle pointer.
 * @param filename Path of file to open.
 * @param mode Access mode.
 * @return C_ORM_OK on success, or C_ORM_ERROR_UNKNOWN on error.
 */
C_ORM_EXPORT c_orm_error_t c_orm_fopen(FILE **fp_ptr, const char *filename,
                                       const char *mode);

/**
 * @brief Safe tmpfile wrapper.
 *
 * @param fp_ptr Pointer to file handle pointer.
 * @return C_ORM_OK on success, or C_ORM_ERROR_UNKNOWN on error.
 */
C_ORM_EXPORT c_orm_error_t c_orm_tmpfile(FILE **fp_ptr);

/**
 * @brief Cross-platform environment variable setter returning c_orm_error_t.
 *
 * @param name Environment variable name.
 * @param value Environment variable value.
 * @return C_ORM_OK on success, or C_ORM_ERROR_UNKNOWN on failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_setenv(const char *name, const char *value);

/**
 * @brief Cross-platform environment variable unsetter returning c_orm_error_t.
 *
 * @param name Environment variable name to unset.
 * @return C_ORM_OK on success, or C_ORM_ERROR_UNKNOWN on failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_unsetenv(const char *name);

/**
 * @brief Parses a 64-bit signed integer in strict ISO C89 without relying on
 * C99 strtoll.
 *
 * @param nptr Null-terminated input string.
 * @param endptr Optional output pointer to the first unconsumed character.
 * @param base Numeric base (0, 8, 10, or 16).
 * @param out_val Output pointer receiving the parsed 64-bit integer.
 * @return C_ORM_OK on valid parse, C_ORM_ERROR_NULL_POINTER on NULL args, or
 * C_ORM_ERROR_PARSE if no digits were parsed.
 */
C_ORM_EXPORT c_orm_error_t c_orm_strtoint64(const char *nptr,
                                            const char **endptr, int base,
                                            int64_t *out_val);

/**
 * @brief Bounds-checked string copy returning c_orm_error_t (`strcpy_s` on
 * MSVC, bounded C89 copy elsewhere).
 *
 * @param dest Destination character buffer.
 * @param dest_size Capacity of `dest` in bytes.
 * @param src Null-terminated source string.
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER on NULL args, or
 * C_ORM_ERROR_OVERFLOW if `dest_size` is too small.
 */
C_ORM_EXPORT c_orm_error_t c_orm_strcpy_s(char *dest, size_t dest_size,
                                          const char *src);

/**
 * @brief Bounds-checked bounded string copy returning c_orm_error_t
 * (`strncpy_s` on MSVC, bounded C89 copy elsewhere).
 *
 * @param dest Destination character buffer.
 * @param dest_size Capacity of `dest` in bytes.
 * @param src Source string.
 * @param count Maximum characters to copy before NUL terminator.
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER on NULL args, or
 * C_ORM_ERROR_OVERFLOW if `dest_size == 0`.
 */
C_ORM_EXPORT c_orm_error_t c_orm_strncpy_s(char *dest, size_t dest_size,
                                           const char *src, size_t count);

/**
 * @brief Bounds-checked string concatenation returning c_orm_error_t
 * (`strcat_s` on MSVC, bounded C89 append elsewhere).
 *
 * @param dest Null-terminated destination buffer.
 * @param dest_size Total capacity of `dest` in bytes.
 * @param src Null-terminated source string to append.
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER on NULL args, or
 * C_ORM_ERROR_OVERFLOW if combined length exceeds `dest_size - 1`.
 */
C_ORM_EXPORT c_orm_error_t c_orm_strcat_s(char *dest, size_t dest_size,
                                          const char *src);

/**
 * @brief Bounds-checked bounded string concatenation returning c_orm_error_t
 * (`strncat_s` on MSVC, bounded C89 append elsewhere).
 *
 * @param dest Null-terminated destination buffer.
 * @param dest_size Total capacity of `dest` in bytes.
 * @param src Source string to append.
 * @param count Maximum characters from `src` to append.
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER on NULL args, or
 * C_ORM_ERROR_OVERFLOW if combined length exceeds `dest_size - 1`.
 */
C_ORM_EXPORT c_orm_error_t c_orm_strncat_s(char *dest, size_t dest_size,
                                           const char *src, size_t count);

/**
 * @brief Bounds-checked environment variable lookup returning c_orm_error_t
 * (`getenv_s` on MSVC, bounded `getenv` copy elsewhere). Writes `""` if the
 * variable is unset.
 *
 * @param dest Destination character buffer.
 * @param dest_size Capacity of `dest` in bytes.
 * @param var_name Environment variable name.
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER on NULL args, or
 * C_ORM_ERROR_OVERFLOW if `dest_size == 0`.
 */
C_ORM_EXPORT c_orm_error_t c_orm_getenv_s(char *dest, size_t dest_size,
                                          const char *var_name);

#ifdef C_ORM_TEST_ALLOCATOR
/**
 * @brief Countdown counter to simulate Safe CRT formatting/copy failure in
 * fault-injection tests (`-1` = disabled).
 */
C_ORM_EXPORT extern int c_orm_mock_safe_crt_countdown;
#endif /* C_ORM_TEST_ALLOCATOR */

#define C_ORM_SETENV(name, val) c_orm_setenv((name), (val))
#define C_ORM_UNSETENV(var_name) c_orm_unsetenv((var_name))
#define C_ORM_STRCPY(dest, size, src) c_orm_strcpy_s((dest), (size), (src))
#define C_ORM_STRNCPY(dest, size, src, count)                                  \
  c_orm_strncpy_s((dest), (size), (src), (count))
#define C_ORM_STRCAT(dest, size, src) c_orm_strcat_s((dest), (size), (src))
#define C_ORM_STRNCAT(dest, size, src, count)                                  \
  c_orm_strncat_s((dest), (size), (src), (count))
#define C_ORM_FOPEN(fp_ptr, filename, mode)                                    \
  c_orm_fopen((fp_ptr), (filename), (mode))
#define C_ORM_TMPFILE(fp_ptr) c_orm_tmpfile((fp_ptr))
#define C_ORM_GETENV(dest, size, var_name)                                     \
  c_orm_getenv_s((dest), (size), (var_name))

#if defined(_MSC_VER)
#define C_ORM_SSCANF sscanf_s
#else
#define C_ORM_SSCANF sscanf
#endif

#if defined(_MSC_VER)
typedef __int64 c_orm_int64_t;
typedef unsigned __int64 c_orm_uint64_t;
#define C_ORM_INT64_FORMAT "%I64d"
#define C_ORM_UINT64_FORMAT "%I64u"
#elif defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
__extension__ typedef long long c_orm_int64_t;
__extension__ typedef unsigned long long c_orm_uint64_t;
#if defined(__USE_MINGW_ANSI_STDIO) && __USE_MINGW_ANSI_STDIO
#define C_ORM_INT64_FORMAT "%lld"
#define C_ORM_UINT64_FORMAT "%llu"
#else
#define C_ORM_INT64_FORMAT "%I64d"
#define C_ORM_UINT64_FORMAT "%I64u"
#endif
#elif defined(__LP64__) || defined(_LP64)
typedef long c_orm_int64_t;
typedef unsigned long c_orm_uint64_t;
#define C_ORM_INT64_FORMAT "%ld"
#define C_ORM_UINT64_FORMAT "%lu"
#elif defined(__GNUC__) || defined(__clang__)
__extension__ typedef long long c_orm_int64_t;
__extension__ typedef unsigned long long c_orm_uint64_t;
#define C_ORM_INT64_FORMAT "%lld"
#define C_ORM_UINT64_FORMAT "%llu"
#else
/* Fallback for other strictly C89 compilers if they have no 64-bit int */
typedef long c_orm_int64_t;
typedef unsigned long c_orm_uint64_t;
#define C_ORM_INT64_FORMAT "%ld"
#define C_ORM_UINT64_FORMAT "%lu"
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* C_ORM_SAFE_CRT_H */
