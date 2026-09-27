#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file cdd_c_compat.c
 * @brief Compatibility stubs and safe formatting utilities.
 * @defgroup cdd_c_compat CDD-C Compatibility Layer
 * @{
 */

/* clang-format off */
#include <stdarg.h>
#include <stdio.h>
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
 * @return 0 on success.
 */
C_ORM_EXPORT c_orm_error_t C_CDD_LOG_DEBUG(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  return 0;
}

#if defined(__clang__) || defined(__GNUC__)
__attribute__((__format__(__printf__, 3, 4)))
#endif
/**
 * @brief Formats a string safely into a buffer.
 * @param buf Output character buffer.
 * @param size Capacity of buffer in bytes.
 * @param format Printf format string.
 * @param ... Additional format arguments.
 * @return Number of characters written, or negative on error.
 */
C_ORM_EXPORT int
c_orm_sprintf(char *buf, size_t size, const char *format, ...) {
  int ret;
  va_list args;
  va_start(args, format);
#if defined(_MSC_VER)
  ret = vsprintf_s(buf, size, format, args);
#else
  ret = c89stringutils_vsnprintf(buf, size, format, args);
#endif
  va_end(args);
  return ret;
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

#if defined(__clang__) || defined(__GNUC__)
#endif
