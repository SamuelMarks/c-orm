/**
 * @file cdd_c_compat.c
 * @brief Compatibility stubs and safe formatting utilities.
 * @defgroup cdd_c_compat CDD-C Compatibility Layer
 * @{
 */

/* clang-format off */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200112L
#endif
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "c_orm_safe_crt.h"
#include <c89stringutils_string_extras.h>
/* clang-format on */

/* Fix undefined reference to g_fail_io_after in cdd-c when built without tests
   (Now provided by cdd-c master directly)
 */

#if defined(__clang__) || defined(__GNUC__)
__attribute__((__format__(__printf__, 1, 2)))
#endif
/**
 * @brief Logs a debug message for cdd-c compatibility.
 * @param fmt Format string.
 * @param ... Additional arguments.
 * @return 0 on success.
 */
C_ORM_EXPORT c_orm_error_t
C_CDD_LOG_DEBUG(const char *fmt, ...);

/**
 * @brief Logs a debug message for cdd-c compatibility.
 * @param fmt Format string.
 * @param ... Additional arguments.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t C_CDD_LOG_DEBUG(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  return C_ORM_OK;
}

#ifdef C_ORM_TEST_ALLOCATOR
/** @brief Countdown counter to simulate Safe CRT formatting/copy failure. */
C_ORM_EXPORT int c_orm_mock_safe_crt_countdown = -1;
#endif /* C_ORM_TEST_ALLOCATOR */

#if defined(__clang__) || defined(__GNUC__)
__attribute__((__format__(__printf__, 3, 4)))
#endif
/**
 * @brief Formats a string safely into a buffer.
 * @param buf Output character buffer.
 * @param size Capacity of buffer in bytes.
 * @param format Printf format string.
 * @param ... Additional format arguments.
 * @return C_ORM_OK on success, or C_ORM_ERROR_UNKNOWN on error.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_sprintf(char *buf, size_t size, const char *format, ...) {
  int ret;
  va_list args;
  if (!buf || size == 0 || !format) {
    return C_ORM_ERROR_UNKNOWN;
  }
#ifdef C_ORM_TEST_ALLOCATOR
  if (c_orm_mock_safe_crt_countdown >= 0) {
    if (c_orm_mock_safe_crt_countdown == 0) {
      c_orm_mock_safe_crt_countdown = -1;
      return C_ORM_ERROR_UNKNOWN;
    }
    c_orm_mock_safe_crt_countdown--;
  }
#endif
  va_start(args, format);
#if defined(_MSC_VER)
  ret = _vsnprintf_s(buf, size, _TRUNCATE, format, args);
#else
  ret = c89stringutils_vsnprintf(buf, size, format, args);
#endif
  va_end(args);
  return (ret < 0) ? C_ORM_ERROR_UNKNOWN : C_ORM_OK;
}

/**
 * @brief Safe fopen wrapper.
 * @param fp_ptr Pointer to file handle pointer.
 * @param filename Path of file to open.
 * @param mode Access mode.
 * @return C_ORM_OK on success, or C_ORM_ERROR_UNKNOWN on error.
 */
C_ORM_EXPORT c_orm_error_t c_orm_fopen(FILE **fp_ptr, const char *filename,
                                       const char *mode) {
  if (!fp_ptr) {
    return C_ORM_ERROR_UNKNOWN;
  }
  if (!filename || !mode || filename[0] == '\0' || mode[0] == '\0') {
    *fp_ptr = NULL;
    return C_ORM_ERROR_UNKNOWN;
  }
#if defined(_MSC_VER)
  return (fopen_s(fp_ptr, filename, mode) == 0) ? C_ORM_OK
                                                : C_ORM_ERROR_UNKNOWN;
#else
  *fp_ptr = fopen(filename, mode);
  return (*fp_ptr == NULL) ? C_ORM_ERROR_UNKNOWN : C_ORM_OK;
#endif
}

/**
 * @brief Safe tmpfile wrapper.
 * @param fp_ptr Pointer to file handle pointer.
 * @return C_ORM_OK on success, or C_ORM_ERROR_UNKNOWN on error.
 */
C_ORM_EXPORT c_orm_error_t c_orm_tmpfile(FILE **fp_ptr) {
  if (!fp_ptr) {
    return C_ORM_ERROR_UNKNOWN;
  }
#if defined(_MSC_VER)
  return (tmpfile_s(fp_ptr) == 0) ? C_ORM_OK : C_ORM_ERROR_UNKNOWN;
#else
  *fp_ptr = tmpfile();
  return (*fp_ptr == NULL) ? C_ORM_ERROR_UNKNOWN : C_ORM_OK;
#endif
}

/**
 * @brief Cross-platform environment variable setter returning c_orm_error_t.
 * @param name Environment variable name.
 * @param value Environment variable value.
 * @return C_ORM_OK on success, or C_ORM_ERROR_UNKNOWN on failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_setenv(const char *name, const char *value) {
  if (!name || !value || name[0] == '\0' || strchr(name, '=') != NULL) {
    return C_ORM_ERROR_UNKNOWN;
  }
#if defined(_WIN32)
  return (_putenv_s(name, value) == 0) ? C_ORM_OK : C_ORM_ERROR_UNKNOWN;
#else
  return (setenv(name, value, 1) == 0) ? C_ORM_OK : C_ORM_ERROR_UNKNOWN;
#endif
}

/**
 * @brief Cross-platform environment variable unsetter returning c_orm_error_t.
 * @param name Environment variable name to unset.
 * @return C_ORM_OK on success, or C_ORM_ERROR_UNKNOWN on failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_unsetenv(const char *name) {
  if (!name || name[0] == '\0' || strchr(name, '=') != NULL) {
    return C_ORM_ERROR_UNKNOWN;
  }
#if defined(_WIN32)
  return (_putenv_s(name, "") == 0) ? C_ORM_OK : C_ORM_ERROR_UNKNOWN;
#else
  return (unsetenv(name) == 0) ? C_ORM_OK : C_ORM_ERROR_UNKNOWN;
#endif
}

/**
 * @brief Parses a 64-bit signed integer in strict ISO C89 without C99 strtoll.
 * @param nptr Null-terminated input string.
 * @param endptr Optional output pointer to the first unconsumed character.
 * @param base Numeric base (0, 8, 10, or 16).
 * @param out_val Output pointer receiving the parsed 64-bit integer.
 * @return C_ORM_OK on valid parse, C_ORM_ERROR_NULL_POINTER on NULL args, or
 * C_ORM_ERROR_PARSE if no digits were parsed.
 */
C_ORM_EXPORT c_orm_error_t c_orm_strtoint64(const char *nptr,
                                            const char **endptr, int base,
                                            int64_t *out_val) {
  if (!nptr || !out_val) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  *out_val = 0;
  if (base != 0 && (base < 2 || base > 36)) {
    if (endptr) {
      *endptr = nptr;
    }
    return C_ORM_ERROR_VALIDATION;
  }
#if defined(_MSC_VER)
  {
    char *ep = NULL;
    *out_val = (int64_t)_strtoi64(nptr, &ep, base);
    if (endptr) {
      *endptr = ep;
    }
    return (ep == nptr) ? C_ORM_ERROR_PARSE : C_ORM_OK;
  }
#else
  {
    const char *s = nptr;
    int neg = 0;
    int any = 0;
    uint64_t acc = 0;

    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f' ||
           *s == '\v') {
      s++;
    }
    if (*s == '-') {
      neg = 1;
      s++;
    } else if (*s == '+') {
      s++;
    }
    if ((base == 0 || base == 16) && s[0] == '0' &&
        (s[1] == 'x' || s[1] == 'X')) {
      base = 16;
      s += 2;
    } else if (base == 0) {
      base = (s[0] == '0') ? 8 : 10;
    }
    while (*s) {
      int digit;
      if (*s >= '0' && *s <= '9') {
        digit = *s - '0';
      } else if (*s >= 'a' && *s <= 'z') {
        digit = *s - 'a' + 10;
      } else if (*s >= 'A' && *s <= 'Z') {
        digit = *s - 'A' + 10;
      } else {
        break;
      }
      if (digit >= base) {
        break;
      }
      acc = acc * (uint64_t)base + (uint64_t)digit;
      any = 1;
      s++;
    }
    if (!any) {
      if (endptr) {
        *endptr = nptr;
      }
      return C_ORM_ERROR_PARSE;
    }
    if (endptr) {
      *endptr = s;
    }
    if (neg) {
      *out_val = (int64_t)(0 - acc);
    } else {
      *out_val = (int64_t)acc;
    }
    return C_ORM_OK;
  }
#endif
}

C_ORM_EXPORT c_orm_error_t c_orm_strcpy_s(char *dest, size_t dest_size,
                                          const char *src) {
  size_t len;
  if (!dest || !src) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (dest_size == 0) {
    return C_ORM_ERROR_OVERFLOW;
  }
#ifdef C_ORM_TEST_ALLOCATOR
  if (c_orm_mock_safe_crt_countdown >= 0) {
    if (c_orm_mock_safe_crt_countdown == 0) {
      c_orm_mock_safe_crt_countdown = -1;
      dest[0] = '\0';
      return C_ORM_ERROR_OVERFLOW;
    }
    c_orm_mock_safe_crt_countdown--;
  }
#endif
  len = strlen(src);
  if (len >= dest_size) {
    dest[0] = '\0';
    return C_ORM_ERROR_OVERFLOW;
  }
#if defined(_MSC_VER)
  return (strcpy_s(dest, dest_size, src) == 0) ? C_ORM_OK
                                               : C_ORM_ERROR_OVERFLOW;
#else
  memcpy(dest, src, len + 1);
  return C_ORM_OK;
#endif
}

C_ORM_EXPORT c_orm_error_t c_orm_strncpy_s(char *dest, size_t dest_size,
                                           const char *src, size_t count) {
  size_t copy_len;
  size_t i;
  if (!dest || !src) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (dest_size == 0) {
    return C_ORM_ERROR_OVERFLOW;
  }
#ifdef C_ORM_TEST_ALLOCATOR
  if (c_orm_mock_safe_crt_countdown >= 0) {
    if (c_orm_mock_safe_crt_countdown == 0) {
      c_orm_mock_safe_crt_countdown = -1;
      dest[0] = '\0';
      return C_ORM_ERROR_OVERFLOW;
    }
    c_orm_mock_safe_crt_countdown--;
  }
#endif
  copy_len = 0;
  while (copy_len < count && src[copy_len] != '\0') {
    copy_len++;
  }
  if (copy_len >= dest_size) {
    copy_len = dest_size - 1;
  }
  for (i = 0; i < copy_len; ++i) {
    dest[i] = src[i];
  }
  dest[copy_len] = '\0';
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_strcat_s(char *dest, size_t dest_size,
                                          const char *src) {
  size_t dlen;
  size_t slen;
  if (!dest || !src) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (dest_size == 0) {
    return C_ORM_ERROR_OVERFLOW;
  }
  dlen = 0;
  while (dlen < dest_size && dest[dlen] != '\0') {
    dlen++;
  }
  if (dlen == dest_size) {
    return C_ORM_ERROR_OVERFLOW;
  }
  slen = strlen(src);
  if (dlen + slen >= dest_size) {
    return C_ORM_ERROR_OVERFLOW;
  }
#if defined(_MSC_VER)
  return (strcat_s(dest, dest_size, src) == 0) ? C_ORM_OK
                                               : C_ORM_ERROR_OVERFLOW;
#else
  memcpy(dest + dlen, src, slen + 1);
  return C_ORM_OK;
#endif
}

C_ORM_EXPORT c_orm_error_t c_orm_strncat_s(char *dest, size_t dest_size,
                                           const char *src, size_t count) {
  size_t dlen;
  size_t copy_len;
  if (!dest || !src) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (dest_size == 0) {
    return C_ORM_ERROR_OVERFLOW;
  }
  dlen = 0;
  while (dlen < dest_size && dest[dlen] != '\0') {
    dlen++;
  }
  if (dlen == dest_size) {
    return C_ORM_ERROR_OVERFLOW;
  }
  copy_len = 0;
  while (copy_len < count && src[copy_len] != '\0') {
    copy_len++;
  }
  if (dlen + copy_len >= dest_size) {
    copy_len = dest_size - 1 - dlen;
  }
  if (copy_len > 0) {
    memcpy(dest + dlen, src, copy_len);
  }
  dest[dlen + copy_len] = '\0';
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_getenv_s(char *dest, size_t dest_size,
                                          const char *var_name) {
  if (!dest || !var_name) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (dest_size == 0) {
    return C_ORM_ERROR_OVERFLOW;
  }
#if defined(_MSC_VER)
  {
    char *val = NULL;
    size_t req_len = 0;
    c_orm_error_t rc;
    if (_dupenv_s(&val, &req_len, var_name) != 0 || !val) {
      dest[0] = '\0';
      return C_ORM_OK;
    }
    rc = c_orm_strncpy_s(dest, dest_size, val, dest_size - 1);
    free(val);
    return rc;
  }
#else
  {
    const char *val = getenv(var_name);
    if (!val) {
      dest[0] = '\0';
      return C_ORM_OK;
    }
    return c_orm_strncpy_s(dest, dest_size, val, dest_size - 1);
  }
#endif
}
