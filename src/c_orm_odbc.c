/**
 * @file c_orm_odbc.c
 * @brief Generic ODBC 3.52 driver implementation for c-orm (zero <windows.h>).
 */

/* clang-format off */
#include "c_orm_odbc.h"
#include "c_orm_api.h"
#include "c_orm_ebcdic.h"
#include "c_orm_log.h"
#include "c_orm_safe_crt.h"
#include "c_orm_string_builder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32) || defined(_WIN64)
/**
 * @brief Minimal Win32 64-bit performance counter union without <windows.h>.
 */
typedef union c_orm_odbc_qpc_val {
  /** @brief Split 32-bit low/high halves. */
  struct {
    /** @brief Low 32 bits. */
    unsigned long LowPart;
    /** @brief High 32 bits. */
    long HighPart;
  } u;
  /** @brief Signed 64-bit tick count. */
  c_orm_int64_t QuadPart;
} c_orm_odbc_qpc_t;
extern __declspec(dllimport) int __stdcall QueryPerformanceCounter(
    c_orm_odbc_qpc_t *lpPerformanceCount);
extern __declspec(dllimport) int __stdcall QueryPerformanceFrequency(
    c_orm_odbc_qpc_t *lpFrequency);
#else
#include <sys/time.h>
#endif
/* clang-format on */

/** @brief Magic tag identifying an ODBC/Db2 driver_data payload. */
#define C_ORM_ODBC_CONN_MAGIC 0x4F444243UL

/** @brief Unfetched cell state. */
#define ODBC_CELL_UNFETCHED 0
/** @brief SQL NULL cell state. */
#define ODBC_CELL_NULL 1
/** @brief Materialized UTF-8 / binary buffer cell state. */
#define ODBC_CELL_BYTES 2

/**
 * @brief Per-connection state shared by Generic ODBC and IBM Db2 CLI drivers.
 */
typedef struct c_orm_odbc_conn {
  /** @brief Integrity tag (`C_ORM_ODBC_CONN_MAGIC`). */
  unsigned long magic;
  /** @brief Active ODBC / Db2 CLI function pointer table. */
  const c_orm_odbc_api_t *api;
  /** @brief Allocated ODBC environment handle. */
  c_orm_sqlhenv_t henv;
  /** @brief Allocated ODBC connection handle. */
  c_orm_sqlhdbc_t hdbc;
  /** @brief Non-zero when wide (`*W`) APIs are preferred. */
  int prefer_wide;
  /** @brief Base autocommit setting configured at connect time (1 or 0). */
  int base_autocommit;
  /** @brief Non-zero while inside an explicit `c_orm_odbc_begin_transaction`.
   */
  int in_tx;
  /** @brief 5-char ANSI SQLSTATE from most recent diagnostic. */
  char last_sqlstate[6];
  /** @brief Vendor native error code (e.g. Db2 SQLCODE) from last diag. */
  c_orm_sqlinteger_t last_native_error;
  /** @brief Primary human-readable diagnostic summary. */
  char last_error[512];
  /** @brief Multi-record diagnostic trace buffer. */
  char last_trace[1024];
} c_orm_odbc_conn_t;

/**
 * @brief Retained parameter slot bound to an `SQLHSTMT`.
 *
 * ODBC `SQLBindParameter` stores application buffer addresses and reads them at
 * `SQLExecute` time, so all bound values and `StrLen_or_Ind` indicators must
 * outlive the `bind_*` call.
 */
typedef struct c_orm_odbc_param_slot {
  /** @brief Retained 32-bit signed integer value. */
  int32_t i32_val;
  /** @brief Retained 64-bit signed integer value. */
  c_orm_int64_t i64_val;
  /** @brief Retained IEEE-754 double value. */
  double dbl_val;
  /** @brief Retained ODBC binary numeric struct. */
  c_orm_sql_numeric_struct_t num_val;
  /** @brief Retained canonical decimal ASCII buffer for fallback binding. */
  char dec_str[48];
  /** @brief Heap-allocated copy for string, wide string, or binary blob. */
  void *heap_buf;
  /** @brief Retained ODBC length or `C_ORM_SQL_NULL_DATA` indicator. */
  c_orm_sqllen_t ind;
} c_orm_odbc_param_slot_t;

/**
 * @brief Cached descriptor and current-row value buffer for one result column.
 */
typedef struct c_orm_odbc_col_slot {
  /** @brief Null-terminated UTF-8 column name. */
  char name[128];
  /** @brief Reported SQL data type (`C_ORM_SQL_*`). */
  c_orm_sqlsmallint_t sql_type;
  /** @brief Reported column precision/size. */
  c_orm_sqlulen_t col_size;
  /** @brief Reported fractional scale. */
  c_orm_sqlsmallint_t decimal_digits;
  /** @brief Reported nullability indicator. */
  c_orm_sqlsmallint_t nullable;
  /** @brief Current row state (`ODBC_CELL_*`). */
  int fetched_kind;
  /** @brief Non-zero if `dec_val` holds a decoded `c_orm_decimal_t`. */
  int has_dec;
  /** @brief Non-zero if active value is stored in `heap_buf`. */
  int used_heap;
  /** @brief Decoded exact decimal value when fetched via `SQL_C_NUMERIC`. */
  c_orm_decimal_t dec_val;
  /** @brief Inline zero-malloc buffer for short strings/numbers/blobs. */
  unsigned char inline_buf[128];
  /** @brief Dynamic buffer for >127-byte CLOB/BLOB/VARGRAPHIC payloads. */
  unsigned char *heap_buf;
  /** @brief Allocated capacity of `heap_buf` in bytes. */
  size_t heap_cap;
  /** @brief Active payload byte length (excluding trailing NUL on strings). */
  size_t byte_len;
} c_orm_odbc_col_slot_t;

/**
 * @brief Prepared statement state implementing `struct c_orm_driver_stmt`.
 */
typedef struct c_orm_odbc_stmt {
  /** @brief Parent c-orm database connection. */
  c_orm_db_t *db;
  /** @brief Parent ODBC connection state. */
  c_orm_odbc_conn_t *conn;
  /** @brief Underlying `SQLHSTMT` handle. */
  c_orm_sqlhstmt_t hstmt;
  /** @brief Heap-allocated SQL text retained for telemetry/diagnostics. */
  char *sql_text;
  /** @brief Dynamic array of retained parameter buffers. */
  c_orm_odbc_param_slot_t *params;
  /** @brief Allocated element capacity of `params`. */
  size_t param_cap;
  /** @brief Cached result column slots for the current execution. */
  c_orm_odbc_col_slot_t *cols;
  /** @brief Number of result columns (`0` for non-SELECT DML). */
  int num_cols;
  /** @brief Allocated element capacity of `cols`. */
  int col_cap;
  /** @brief Non-zero once `SQLExecute` has run since prepare/reset. */
  int executed;
  /** @brief Non-zero when cursor is positioned on a valid fetched row. */
  int has_active_row;
} c_orm_odbc_stmt_t;

/**
 * @brief Converts an ASCII hex digit into its `0..15` value.
 * @param ch Input character.
 * @param out_val Output nybble (`0..15`).
 * @return C_ORM_OK on valid hex character, or C_ORM_ERROR_VALIDATION.
 */
static c_orm_error_t hex_digit_val(char ch, unsigned char *out_val) {
  if (ch >= '0' && ch <= '9') {
    *out_val = (unsigned char)(ch - '0');
    return C_ORM_OK;
  }
  if (ch >= 'a' && ch <= 'f') {
    *out_val = (unsigned char)(10 + (ch - 'a'));
    return C_ORM_OK;
  }
  if (ch >= 'A' && ch <= 'F') {
    *out_val = (unsigned char)(10 + (ch - 'A'));
    return C_ORM_OK;
  }
  return C_ORM_ERROR_VALIDATION;
}

/**
 * @brief Case-insensitive ASCII prefix comparison.
 * @param str Input string.
 * @param pfx ASCII prefix.
 * @param n Number of characters to compare.
 * @return 1 if equal, 0 otherwise.
 */
static int odbc_cmp_istarts_with(const char *str, const char *pfx, size_t n) {
  size_t i;
  for (i = 0; i < n; i++) {
    char a = str[i];
    char b = pfx[i];
    if (a == '\0') {
      return 0;
    }
    if (a >= 'A' && a <= 'Z') {
      a = (char)(a + ('a' - 'A'));
    }
    if (b >= 'A' && b <= 'Z') {
      b = (char)(b + ('a' - 'A'));
    }
    if (a != b) {
      return 0;
    }
  }
  return 1;
}

/**
 * @brief Case-insensitive exact ASCII string equality check.
 * @param a First string.
 * @param b Second string.
 * @return 1 if equal, 0 otherwise.
 */
static int odbc_cmp_ieq(const char *a, const char *b) {
  size_t la = strlen(a);
  size_t lb = strlen(b);
  if (la != lb) {
    return 0;
  }
  return odbc_cmp_istarts_with(a, b, la);
}

/**
 * @brief Base-10 integer parse predicate wrapping `c_orm_strtoint64`.
 * @param s Input string.
 * @param endp Output pointer to first unconsumed character.
 * @param out_val Output 64-bit integer.
 * @return 1 if at least one base-10 digit was parsed, 0 otherwise.
 */
static int odbc_calc_parse_int64(const char *s, const char **endp,
                                 int64_t *out_val) {
  return (c_orm_strtoint64(s, endp, 10, out_val) == C_ORM_OK) ? 1 : 0;
}

/**
 * @brief Decimal/scientific string to truncated int64 parse predicate.
 * @param s Input string containing `.`, `e`, or `E`.
 * @param out_val Output 64-bit integer.
 * @return 1 if valid and within int64 range, 0 otherwise.
 */
static int odbc_calc_parse_decimal_int64(const char *s, int64_t *out_val) {
  c_orm_decimal_t dtmp;
  c_orm_int64_t dec_parsed = 0;
  return (c_orm_decimal_from_str(s, &dtmp) == C_ORM_OK &&
          c_orm_decimal_to_int64(&dtmp, C_ORM_ROUND_TRUNCATE, &dec_parsed) ==
              C_ORM_OK)
             ? (*out_val = (int64_t)dec_parsed, 1)
             : 0;
}

C_ORM_EXPORT c_orm_error_t
c_orm_odbc_percent_decode(const char *src, size_t src_len, int plus_as_space,
                          char *out_buf, size_t buf_size, size_t *out_len) {
  size_t i = 0;
  size_t w = 0;

  if (!src || !out_buf) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (buf_size == 0) {
    return C_ORM_ERROR_OVERFLOW;
  }

  while (i < src_len) {
    char ch = src[i];
    if (ch == '\0') {
      return C_ORM_ERROR_VALIDATION;
    }
    if (ch == '%') {
      unsigned char hi = 0;
      unsigned char lo = 0;
      unsigned char decoded;
      c_orm_error_t rc;
      if (i + 2 >= src_len) {
        return C_ORM_ERROR_VALIDATION;
      }
      rc = hex_digit_val(src[i + 1], &hi);
      if (rc != C_ORM_OK) {
        return rc;
      }
      rc = hex_digit_val(src[i + 2], &lo);
      if (rc != C_ORM_OK) {
        return rc;
      }
      decoded = (unsigned char)((hi << 4) | lo);
      if (decoded == 0) {
        return C_ORM_ERROR_VALIDATION;
      }
      if (w + 1 >= buf_size) {
        return C_ORM_ERROR_OVERFLOW;
      }
      out_buf[w++] = (char)decoded;
      i += 3;
    } else if (ch == '+' && plus_as_space) {
      if (w + 1 >= buf_size) {
        return C_ORM_ERROR_OVERFLOW;
      }
      out_buf[w++] = ' ';
      i++;
    } else {
      if (w + 1 >= buf_size) {
        return C_ORM_ERROR_OVERFLOW;
      }
      out_buf[w++] = ch;
      i++;
    }
  }

  out_buf[w] = '\0';
  if (out_len) {
    *out_len = w;
  }
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_odbc_escape_conn_value(const char *value,
                                                        char *out_buf,
                                                        size_t buf_size,
                                                        size_t *out_len) {
  size_t i;
  size_t len;
  int needs_braces = 0;
  size_t w = 0;

  if (!value || !out_buf) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (buf_size == 0) {
    return C_ORM_ERROR_OVERFLOW;
  }

  len = strlen(value);
  for (i = 0; i < len; i++) {
    unsigned char uch = (unsigned char)value[i];
    if (uch < 0x20U && uch != 0x09U) {
      return C_ORM_ERROR_VALIDATION;
    }
    if (uch == ';' || uch == '{' || uch == '}' || uch == '=' || uch == ' ' ||
        uch == 0x09U || uch == '"' || uch == '\x27') {
      needs_braces = 1;
    }
  }

  if (!needs_braces) {
    if (len + 1 > buf_size) {
      return C_ORM_ERROR_OVERFLOW;
    }
    memcpy(out_buf, value, len + 1);
    if (out_len) {
      *out_len = len;
    }
    return C_ORM_OK;
  }

  if (w + 1 >= buf_size) {
    return C_ORM_ERROR_OVERFLOW;
  }
  out_buf[w++] = '{';
  for (i = 0; i < len; i++) {
    char ch = value[i];
    if (ch == '}') {
      if (w + 2 >= buf_size) {
        return C_ORM_ERROR_OVERFLOW;
      }
      out_buf[w++] = '}';
      out_buf[w++] = '}';
    } else {
      if (w + 1 >= buf_size) {
        return C_ORM_ERROR_OVERFLOW;
      }
      out_buf[w++] = ch;
    }
  }
  if (w + 1 >= buf_size) {
    return C_ORM_ERROR_OVERFLOW;
  }
  out_buf[w++] = '}';
  out_buf[w] = '\0';
  if (out_len) {
    *out_len = w;
  }
  return C_ORM_OK;
}

/**
 * @brief Validates an ODBC connection attribute key (`[A-Za-z0-9_.-]+`).
 * @param key Null-terminated key string.
 * @return C_ORM_OK if valid, or C_ORM_ERROR_VALIDATION.
 */
static c_orm_error_t validate_conn_key(const char *key) {
  size_t i;
  for (i = 0; key[i] != '\0'; i++) {
    char ch = key[i];
    if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
          (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.')) {
      return C_ORM_ERROR_VALIDATION;
    }
  }
  return C_ORM_OK;
}

/**
 * @brief Appends `KEY=<escaped_val>;` to an ODBC connection-string buffer.
 * @param dst Destination connection string buffer.
 * @param dst_cap Capacity of `dst` in bytes.
 * @param io_pos Input/output write offset in `dst`.
 * @param key Validated attribute key.
 * @param raw_val Unescaped attribute value.
 * @param force_braces Non-zero to always brace-wrap (e.g. `DRIVER={...}`).
 * @return C_ORM_OK on success, C_ORM_ERROR_VALIDATION, or C_ORM_ERROR_OVERFLOW.
 */
static c_orm_error_t append_conn_kv(char *dst, size_t dst_cap, size_t *io_pos,
                                    const char *key, const char *raw_val,
                                    int force_braces) {
  char escaped[512];
  size_t klen;
  size_t vlen = 0;
  c_orm_error_t rc;

  rc = validate_conn_key(key);
  if (rc != C_ORM_OK) {
    return rc;
  }

  if (force_braces && raw_val[0] != '{') {
    size_t i;
    size_t rlen = strlen(raw_val);
    size_t w = 0;
    escaped[w++] = '{';
    for (i = 0; i < rlen; i++) {
      unsigned char uch = (unsigned char)raw_val[i];
      if (uch < 0x20U && uch != 0x09U) {
        return C_ORM_ERROR_VALIDATION;
      }
      if (raw_val[i] == '}') {
        if (w + 2 >= sizeof(escaped)) {
          return C_ORM_ERROR_OVERFLOW;
        }
        escaped[w++] = '}';
        escaped[w++] = '}';
      } else {
        if (w + 1 >= sizeof(escaped)) {
          return C_ORM_ERROR_OVERFLOW;
        }
        escaped[w++] = raw_val[i];
      }
    }
    if (w + 1 >= sizeof(escaped)) {
      return C_ORM_ERROR_OVERFLOW;
    }
    escaped[w++] = '}';
    escaped[w] = '\0';
    vlen = w;
  } else {
    rc = c_orm_odbc_escape_conn_value(raw_val, escaped, sizeof(escaped), &vlen);
    if (rc != C_ORM_OK) {
      return rc;
    }
  }

  klen = strlen(key);
  if (*io_pos + klen + 1 + vlen + 2 > dst_cap) {
    return C_ORM_ERROR_OVERFLOW;
  }
  memcpy(dst + *io_pos, key, klen);
  *io_pos += klen;
  dst[(*io_pos)++] = '=';
  memcpy(dst + *io_pos, escaped, vlen);
  *io_pos += vlen;
  dst[(*io_pos)++] = ';';
  dst[*io_pos] = '\0';
  return C_ORM_OK;
}

/**
 * @brief Parses boolean query string value (`1`/`true`/`yes`/`on` vs
 * `0`/`false`/`no`/`off`).
 * @param val Null-terminated string value.
 * @param out_bool Output set to 1 or 0.
 * @return C_ORM_OK on valid boolean, or C_ORM_ERROR_VALIDATION.
 */
static c_orm_error_t parse_bool_flag(const char *val, int *out_bool) {
  static const char *const trues[4] = {"1", "true", "yes", "on"};
  static const char *const falses[4] = {"0", "false", "no", "off"};
  size_t i;
  for (i = 0; i < 4U; ++i) {
    if (odbc_cmp_ieq(val, trues[i])) {
      *out_bool = 1;
      return C_ORM_OK;
    }
    if (odbc_cmp_ieq(val, falses[i])) {
      *out_bool = 0;
      return C_ORM_OK;
    }
  }
  return C_ORM_ERROR_VALIDATION;
}

C_ORM_EXPORT c_orm_error_t c_orm_odbc_build_conn_string(const char *url,
                                                        char *out_conn_str,
                                                        size_t buf_size,
                                                        int *out_login_timeout,
                                                        int *out_autocommit) {
  const char *body;
  int has_scheme = 0;
  int is_dsn_raw = 0;
  int is_drv_raw = 0;
  size_t pos = 0;
  int login_timeout = 0;
  int autocommit = 1;
  c_orm_error_t rc;

  if (!url || !out_conn_str) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (buf_size < 2 || url[0] == '\0') {
    return buf_size < 2 ? C_ORM_ERROR_OVERFLOW : C_ORM_ERROR_VALIDATION;
  }

  out_conn_str[0] = '\0';
  if (out_login_timeout) {
    *out_login_timeout = 0;
  }
  if (out_autocommit) {
    *out_autocommit = 1;
  }

  has_scheme = odbc_cmp_istarts_with(url, "odbc://", 7);
  body = has_scheme ? (url + 7) : url;
  if (body[0] == '\0') {
    return C_ORM_ERROR_VALIDATION;
  }

  is_dsn_raw = odbc_cmp_istarts_with(body, "DSN=", 4);
  is_drv_raw = odbc_cmp_istarts_with(body, "DRIVER=", 7);
  if (is_dsn_raw || is_drv_raw || (!has_scheme && strchr(body, '=') != NULL)) {
    size_t blen = strlen(body);
    size_t i;
    for (i = 0; i < blen; i++) {
      unsigned char uch = (unsigned char)body[i];
      if (uch < 0x20U && uch != 0x09U) {
        return C_ORM_ERROR_VALIDATION;
      }
    }
    if (blen + 1 > buf_size) {
      return C_ORM_ERROR_OVERFLOW;
    }
    memcpy(out_conn_str, body, blen + 1);
    return C_ORM_OK;
  }

  if (!has_scheme) {
    return C_ORM_ERROR_VALIDATION;
  }

  /* Parse RFC-style odbc://[user[:pass]@]host[:port][/db][?query] */
  {
    const char *qmark = strchr(body, '?');
    size_t auth_path_len = qmark ? (size_t)(qmark - body) : strlen(body);
    const char *slash = NULL;
    const char *at = NULL;
    const char *host_start;
    size_t host_port_len;
    size_t i;
    char user_dec[256];
    char pass_dec[256];
    char host_dec[256];
    char port_dec[32];
    char db_dec[256];
    char extra_kv[1024];
    size_t extra_pos = 0;
    int has_user = 0;
    int has_pass = 0;
    int has_port = 0;
    int has_db = 0;

    user_dec[0] = '\0';
    pass_dec[0] = '\0';
    host_dec[0] = '\0';
    port_dec[0] = '\0';
    db_dec[0] = '\0';
    extra_kv[0] = '\0';

    for (i = 0; i < auth_path_len; i++) {
      if (body[i] == '/') {
        slash = body + i;
        break;
      }
    }
    {
      size_t authority_len = slash ? (size_t)(slash - body) : auth_path_len;
      for (i = 0; i < authority_len; i++) {
        if (body[i] == '@') {
          at = body + i;
        }
      }
      if (at) {
        size_t cred_len = (size_t)(at - body);
        const char *colon = NULL;
        for (i = 0; i < cred_len; i++) {
          if (body[i] == ':') {
            colon = body + i;
            break;
          }
        }
        if (colon) {
          rc = c_orm_odbc_percent_decode(body, (size_t)(colon - body), 0,
                                         user_dec, sizeof(user_dec), NULL);
          if (rc != C_ORM_OK) {
            return rc;
          }
          rc = c_orm_odbc_percent_decode(colon + 1, (size_t)(at - (colon + 1)),
                                         0, pass_dec, sizeof(pass_dec), NULL);
          if (rc != C_ORM_OK) {
            return rc;
          }
          has_user = 1;
          has_pass = 1;
        } else {
          rc = c_orm_odbc_percent_decode(body, cred_len, 0, user_dec,
                                         sizeof(user_dec), NULL);
          if (rc != C_ORM_OK) {
            return rc;
          }
          has_user = 1;
        }
        host_start = at + 1;
        host_port_len = authority_len - (size_t)((at + 1) - body);
      } else {
        host_start = body;
        host_port_len = authority_len;
      }
    }

    /* Extract host and optional :port (handling IPv6 [::1]:port) */
    if (host_port_len > 0) {
      const char *port_colon = NULL;
      if (host_start[0] == '[') {
        const char *rb = NULL;
        for (i = 1; i < host_port_len; i++) {
          if (host_start[i] == ']') {
            rb = host_start + i;
            break;
          }
        }
        if (!rb) {
          return C_ORM_ERROR_VALIDATION;
        }
        rc = c_orm_odbc_percent_decode(host_start + 1,
                                       (size_t)(rb - (host_start + 1)), 0,
                                       host_dec, sizeof(host_dec), NULL);
        if (rc != C_ORM_OK) {
          return rc;
        }
        if ((size_t)((rb + 1) - host_start) < host_port_len) {
          if (rb[1] != ':') {
            return C_ORM_ERROR_VALIDATION;
          }
          port_colon = rb + 1;
        }
      } else {
        for (i = 0; i < host_port_len; i++) {
          if (host_start[i] == ':') {
            port_colon = host_start + i;
            break;
          }
        }
        if (port_colon) {
          rc = c_orm_odbc_percent_decode(host_start,
                                         (size_t)(port_colon - host_start), 0,
                                         host_dec, sizeof(host_dec), NULL);
          if (rc != C_ORM_OK) {
            return rc;
          }
        } else {
          rc = c_orm_odbc_percent_decode(host_start, host_port_len, 0, host_dec,
                                         sizeof(host_dec), NULL);
          if (rc != C_ORM_OK) {
            return rc;
          }
        }
      }

      if (port_colon) {
        size_t plen = host_port_len - (size_t)((port_colon + 1) - host_start);
        int64_t pnum = 0;
        const char *endp = NULL;
        if (plen == 0) {
          return C_ORM_ERROR_VALIDATION;
        }
        rc = c_orm_odbc_percent_decode(port_colon + 1, plen, 0, port_dec,
                                       sizeof(port_dec), NULL);
        if (rc != C_ORM_OK) {
          return rc;
        }
        if (!odbc_calc_parse_int64(port_dec, &endp, &pnum) || *endp != '\0' ||
            pnum < 1 || pnum > 65535) {
          return C_ORM_ERROR_VALIDATION;
        }
        has_port = 1;
      }
    }

    if (slash && (size_t)((slash + 1) - body) < auth_path_len) {
      size_t dlen = auth_path_len - (size_t)((slash + 1) - body);
      rc = c_orm_odbc_percent_decode(slash + 1, dlen, 0, db_dec, sizeof(db_dec),
                                     NULL);
      if (rc != C_ORM_OK) {
        return rc;
      }
      has_db = 1;
    }

    /* Parse query string parameters (?driver=...&timeout=...&autocommit=...) */
    if (qmark && qmark[1] != '\0') {
      const char *cur = qmark + 1;
      while (*cur != '\0') {
        const char *amp = strchr(cur, '&');
        size_t pair_len = amp ? (size_t)(amp - cur) : strlen(cur);
        const char *eq_ptr = NULL;
        char qkey[128];
        char qval[512];

        for (i = 0; i < pair_len; i++) {
          if (cur[i] == '=') {
            eq_ptr = cur + i;
            break;
          }
        }
        if (!eq_ptr || eq_ptr == cur) {
          return C_ORM_ERROR_VALIDATION;
        }
        rc = c_orm_odbc_percent_decode(cur, (size_t)(eq_ptr - cur), 1, qkey,
                                       sizeof(qkey), NULL);
        if (rc != C_ORM_OK) {
          return rc;
        }
        rc = c_orm_odbc_percent_decode(eq_ptr + 1,
                                       pair_len - (size_t)((eq_ptr + 1) - cur),
                                       1, qval, sizeof(qval), NULL);
        if (rc != C_ORM_OK) {
          return rc;
        }

        if (odbc_cmp_ieq(qkey, "driver")) {
          rc = append_conn_kv(out_conn_str, buf_size, &pos, "DRIVER", qval, 1);
          if (rc != C_ORM_OK) {
            return rc;
          }
        } else if (odbc_cmp_ieq(qkey, "dsn")) {
          rc = append_conn_kv(out_conn_str, buf_size, &pos, "DSN", qval, 0);
          if (rc != C_ORM_OK) {
            return rc;
          }
        } else if (odbc_cmp_ieq(qkey, "timeout") ||
                   odbc_cmp_ieq(qkey, "login_timeout")) {
          int64_t tval = 0;
          const char *endp = NULL;
          if (!odbc_calc_parse_int64(qval, &endp, &tval) || *endp != '\0' ||
              tval < 0 || tval > 86400) {
            return C_ORM_ERROR_VALIDATION;
          }
          login_timeout = (int)tval;
        } else if (odbc_cmp_ieq(qkey, "autocommit")) {
          rc = parse_bool_flag(qval, &autocommit);
          if (rc != C_ORM_OK) {
            return rc;
          }
        } else {
          rc = append_conn_kv(extra_kv, sizeof(extra_kv), &extra_pos, qkey,
                              qval, 0);
          if (rc != C_ORM_OK) {
            return rc;
          }
        }

        if (!amp) {
          break;
        }
        cur = amp + 1;
      }
    }

    if (host_dec[0] != '\0') {
      rc = append_conn_kv(out_conn_str, buf_size, &pos, "SERVER", host_dec, 0);
      if (rc != C_ORM_OK) {
        return rc;
      }
    }
    if (has_port) {
      rc = append_conn_kv(out_conn_str, buf_size, &pos, "PORT", port_dec, 0);
      if (rc != C_ORM_OK) {
        return rc;
      }
    }
    if (has_db) {
      rc = append_conn_kv(out_conn_str, buf_size, &pos, "DATABASE", db_dec, 0);
      if (rc != C_ORM_OK) {
        return rc;
      }
    }
    if (has_user && user_dec[0] != '\0') {
      rc = append_conn_kv(out_conn_str, buf_size, &pos, "UID", user_dec, 0);
      if (rc != C_ORM_OK) {
        return rc;
      }
    }
    if (has_pass) {
      rc = append_conn_kv(out_conn_str, buf_size, &pos, "PWD", pass_dec, 0);
      if (rc != C_ORM_OK) {
        return rc;
      }
    }
    if (extra_pos > 0) {
      if (pos + extra_pos + 1 > buf_size) {
        return C_ORM_ERROR_OVERFLOW;
      }
      memcpy(out_conn_str + pos, extra_kv, extra_pos + 1);
      pos += extra_pos;
    }

    if (pos == 0) {
      return C_ORM_ERROR_VALIDATION;
    }
  }

  if (out_login_timeout) {
    *out_login_timeout = login_timeout;
  }
  if (out_autocommit) {
    *out_autocommit = autocommit;
  }
  return C_ORM_OK;
}

/**
 * @brief Maps a 5-char SQLSTATE (and optional Db2 SQLCODE) to a specific
 * `c_orm_error_t` enum variant.
 * @param sqlstate 5-character ANSI SQLSTATE.
 * @param native_err Vendor native error code.
 * @param is_db2 Non-zero if connection is an IBM Db2 session.
 * @param fallback_err Default error code when SQLSTATE is generic.
 * @return Mapped `c_orm_error_t`.
 */
static c_orm_error_t map_sqlstate_to_error(const char *sqlstate,
                                           c_orm_sqlinteger_t native_err,
                                           int is_db2,
                                           c_orm_error_t fallback_err) {
  if (strncmp(sqlstate, "23", 2) == 0) {
    return C_ORM_ERROR_CONSTRAINT;
  }
  if (strcmp(sqlstate, "40001") == 0) {
    return C_ORM_ERROR_DEADLOCK;
  }
  if (strcmp(sqlstate, "HYT00") == 0 || strcmp(sqlstate, "HYT01") == 0 ||
      strcmp(sqlstate, "57033") == 0) {
    return C_ORM_ERROR_TIMEOUT;
  }
  if (strncmp(sqlstate, "08", 2) == 0) {
    return C_ORM_ERROR_CONNECTION;
  }
  if (strcmp(sqlstate, "01004") == 0 || strcmp(sqlstate, "22001") == 0) {
    return C_ORM_ERROR_TRUNCATION;
  }
  if (strcmp(sqlstate, "22003") == 0 || strcmp(sqlstate, "22012") == 0) {
    return C_ORM_ERROR_DECIMAL_OVERFLOW;
  }
  if (strncmp(sqlstate, "42", 2) == 0) {
    return C_ORM_ERROR_SQL;
  }

  if (is_db2) {
    if (native_err == -803 || native_err == -530 || native_err == -532 ||
        native_err == -545) {
      return C_ORM_ERROR_CONSTRAINT;
    }
    if (native_err == -911) {
      return C_ORM_ERROR_DEADLOCK;
    }
    if (native_err == -913) {
      return C_ORM_ERROR_TIMEOUT;
    }
    if (native_err == -302 || native_err == -433) {
      return C_ORM_ERROR_TRUNCATION;
    }
    if (native_err == -802) {
      return C_ORM_ERROR_DECIMAL_OVERFLOW;
    }
    if (native_err == 100) {
      return C_ORM_ERROR_NOT_FOUND;
    }
  }

  return fallback_err;
}

/**
 * @brief Extracts and validates the `c_orm_odbc_conn_t` pointer from `db`.
 * @param db Database connection handle.
 * @param out_conn Output connection state pointer.
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER, or
 * C_ORM_ERROR_VALIDATION.
 */
static c_orm_error_t extract_odbc_conn(c_orm_db_t *db,
                                       c_orm_odbc_conn_t **out_conn) {
  c_orm_odbc_conn_t *conn;
  if (!db) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  conn = (c_orm_odbc_conn_t *)db->driver_data;
  if (!conn || conn->magic != C_ORM_ODBC_CONN_MAGIC) {
    return C_ORM_ERROR_VALIDATION;
  }
  *out_conn = conn;
  return C_ORM_OK;
}

/**
 * @brief Releases ODBC connection/environment handles and `db` wrapper memory.
 * @param db Database connection handle.
 * @return C_ORM_OK.
 */
static c_orm_error_t odbc_conn_and_db_free(c_orm_db_t *db) {
  c_orm_odbc_conn_t *conn = (c_orm_odbc_conn_t *)db->driver_data;
  if (conn && conn->magic == C_ORM_ODBC_CONN_MAGIC) {
    if (conn->hdbc) {
      if (conn->in_tx) {
        conn->api->SQLEndTran(C_ORM_SQL_HANDLE_DBC, conn->hdbc,
                              C_ORM_SQL_ROLLBACK);
      }
      conn->api->SQLDisconnect(conn->hdbc);
      conn->api->SQLFreeHandle(C_ORM_SQL_HANDLE_DBC, conn->hdbc);
      conn->hdbc = C_ORM_SQL_NULL_HANDLE;
    }
    conn->api->SQLFreeHandle(C_ORM_SQL_HANDLE_ENV, conn->henv);
    conn->henv = C_ORM_SQL_NULL_HANDLE;
    conn->magic = 0;
    c_orm_system_free(conn);
  }
  c_orm_system_free(db);
  return C_ORM_OK;
}

/**
 * @brief Internal diagnostic harvester with optional post-harvest handle
 * cleanup.
 * @param db Database connection handle.
 * @param handle_type ODBC handle type (`ENV`, `DBC`, or `STMT`).
 * @param handle ODBC handle to inspect.
 * @param context Diagnostic context label.
 * @param fallback_err Fallback error code when no SQLSTATE maps.
 * @param cleanup_mode 0 = none, 1 = free STMT `handle`, 2 = disconnect `db`,
 * 3 = drop unconnected `hdbc` and disconnect `db`.
 * @return Mapped `c_orm_error_t`.
 */
static c_orm_error_t
odbc_harvest_diag_impl(c_orm_db_t *db, c_orm_sqlsmallint_t handle_type,
                       c_orm_sqlhandle_t handle, const char *context,
                       c_orm_error_t fallback_err, int cleanup_mode) {
  c_orm_odbc_conn_t *conn = NULL;
  c_orm_sqlsmallint_t rec_num = 1;
  int has_rec1 = 0;
  int is_db2 = 0;
  size_t trace_pos = 0;
  c_orm_error_t rc;

  rc = extract_odbc_conn(db, &conn);
  if (rc != C_ORM_OK) {
    if (fallback_err != C_ORM_OK) {
      return fallback_err;
    }
    return rc;
  }

  is_db2 = (strcmp(db->driver_name, "db2") == 0) ? 1 : 0;
  conn->last_error[0] = '\0';
  conn->last_trace[0] = '\0';
  rc = C_ORM_STRCPY(conn->last_sqlstate, sizeof(conn->last_sqlstate), "00000");
  if (rc != C_ORM_OK) {
    goto diag_cleanup_err;
  }
  conn->last_native_error = 0;

  if (context) {
    rc = C_ORM_SPRINTF(conn->last_trace, sizeof(conn->last_trace), "[%s] ",
                       context);
    if (rc != C_ORM_OK) {
      goto diag_cleanup_err;
    }
    trace_pos = strlen(conn->last_trace);
  }

  if (!handle) {
    rc = C_ORM_SPRINTF(conn->last_error, sizeof(conn->last_error),
                       "[%s] invalid NULL ODBC handle",
                       context ? context : "odbc");
    if (rc != C_ORM_OK) {
      goto diag_cleanup_err;
    }
    return fallback_err;
  }

  while (rec_num <= 8) {
    char state_utf8[8];
    char msg_utf8[384];
    c_orm_sqlinteger_t native_err = 0;
    c_orm_sqlsmallint_t text_len = 0;
    c_orm_sqlreturn_t sret = C_ORM_SQL_NO_DATA;

    state_utf8[0] = '\0';
    msg_utf8[0] = '\0';

    if (conn->prefer_wide && conn->api->SQLGetDiagRecW) {
      c_orm_sqlwchar_t wstate[8];
      c_orm_sqlwchar_t wmsg[256];
      memset(wstate, 0, sizeof(wstate));
      memset(wmsg, 0, sizeof(wmsg));
      sret = conn->api->SQLGetDiagRecW(handle_type, handle, rec_num, wstate,
                                       &native_err, wmsg, 256, &text_len);
      if (C_ORM_SQL_SUCCEEDED(sret)) {
        size_t wlen = text_len > 0 ? (size_t)text_len : 0;
        if (wlen == 0) {
          while (wmsg[wlen] != 0) {
            wlen++;
          }
        }
        rc = c_orm_utf16_to_utf8(wstate, 5, 0, state_utf8, sizeof(state_utf8),
                                 NULL);
        if (rc != C_ORM_OK) {
          goto diag_cleanup_err;
        }
        rc = c_orm_utf16_to_utf8(wmsg, wlen, 0, msg_utf8, sizeof(msg_utf8),
                                 NULL);
        if (rc != C_ORM_OK) {
          goto diag_cleanup_err;
        }
      }
    } else if (conn->api->SQLGetDiagRec) {
      c_orm_sqlchar_t astate[8];
      c_orm_sqlchar_t amsg[384];
      memset(astate, 0, sizeof(astate));
      memset(amsg, 0, sizeof(amsg));
      sret = conn->api->SQLGetDiagRec(
          handle_type, handle, rec_num, astate, &native_err, amsg,
          (c_orm_sqlsmallint_t)sizeof(amsg), &text_len);
      if (C_ORM_SQL_SUCCEEDED(sret)) {
        rc = C_ORM_STRNCPY(state_utf8, sizeof(state_utf8), (const char *)astate,
                           5);
        if (rc != C_ORM_OK) {
          goto diag_cleanup_err;
        }
        rc = C_ORM_STRCPY(msg_utf8, sizeof(msg_utf8), (const char *)amsg);
        if (rc != C_ORM_OK) {
          goto diag_cleanup_err;
        }
      }
    }

    if (!C_ORM_SQL_SUCCEEDED(sret)) {
      break;
    }

    if (rec_num == 1) {
      has_rec1 = 1;
      if (strlen(state_utf8) == 5) {
        rc = C_ORM_STRCPY(conn->last_sqlstate, sizeof(conn->last_sqlstate),
                          state_utf8);
        if (rc != C_ORM_OK) {
          goto diag_cleanup_err;
        }
      }
      conn->last_native_error = native_err;
      rc = C_ORM_SPRINTF(conn->last_error, sizeof(conn->last_error),
                         "[SQLSTATE=%s][Native=%ld] %s", conn->last_sqlstate,
                         (long)native_err, msg_utf8);
      if (rc != C_ORM_OK) {
        goto diag_cleanup_err;
      }
    }

    {
      char frame[448];
      size_t flen;
      rc = C_ORM_SPRINTF(frame, sizeof(frame), "%s[SQLSTATE=%s][Native=%ld] %s",
                         rec_num > 1 ? " | " : "", state_utf8, (long)native_err,
                         msg_utf8);
      if (rc != C_ORM_OK) {
        goto diag_cleanup_err;
      }
      flen = strlen(frame);
      if (trace_pos + flen + 1 < sizeof(conn->last_trace)) {
        memcpy(conn->last_trace + trace_pos, frame, flen + 1);
        trace_pos += flen;
      }
    }
    rec_num++;
  }

  if (conn->last_error[0] == '\0') {
    rc = C_ORM_SPRINTF(conn->last_error, sizeof(conn->last_error),
                       "[%s] ODBC diagnostic failure",
                       context ? context : "odbc");
    if (rc != C_ORM_OK) {
      goto diag_cleanup_err;
    }
    rc = C_ORM_STRCPY(conn->last_trace, sizeof(conn->last_trace),
                      conn->last_error);
    if (rc != C_ORM_OK) {
      goto diag_cleanup_err;
    }
  }

  {
    char saved_state[6];
    c_orm_sqlinteger_t saved_native = conn->last_native_error;
    memcpy(saved_state, conn->last_sqlstate, sizeof(saved_state));
    if (cleanup_mode == 1) {
      conn->api->SQLFreeHandle(C_ORM_SQL_HANDLE_STMT, handle);
    } else if (cleanup_mode >= 2) {
      if (cleanup_mode == 3) {
        conn->api->SQLFreeHandle(C_ORM_SQL_HANDLE_DBC, conn->hdbc);
        conn->hdbc = C_ORM_SQL_NULL_HANDLE;
      }
      odbc_conn_and_db_free(db);
    }
    if (has_rec1) {
      return map_sqlstate_to_error(saved_state, saved_native, is_db2,
                                   fallback_err);
    }
    return fallback_err;
  }

diag_cleanup_err:
  if (cleanup_mode == 1) {
    conn->api->SQLFreeHandle(C_ORM_SQL_HANDLE_STMT, handle);
  } else if (cleanup_mode >= 2) {
    if (cleanup_mode == 3) {
      conn->api->SQLFreeHandle(C_ORM_SQL_HANDLE_DBC, conn->hdbc);
      conn->hdbc = C_ORM_SQL_NULL_HANDLE;
    }
    odbc_conn_and_db_free(db);
  }
  return rc;
}

C_ORM_EXPORT c_orm_error_t c_orm_odbc_harvest_diag(
    c_orm_db_t *db, c_orm_sqlsmallint_t handle_type, c_orm_sqlhandle_t handle,
    const char *context, c_orm_error_t fallback_err) {
  return odbc_harvest_diag_impl(db, handle_type, handle, context, fallback_err,
                                0);
}

C_ORM_EXPORT c_orm_error_t c_orm_odbc_harvest_diag_free_stmt(
    c_orm_db_t *db, c_orm_sqlhandle_t hstmt, const char *context,
    c_orm_error_t fallback_err) {
  return odbc_harvest_diag_impl(db, C_ORM_SQL_HANDLE_STMT, hstmt, context,
                                fallback_err, 1);
}

C_ORM_EXPORT c_orm_error_t
c_orm_odbc_get_handles(c_orm_db_t *db, const c_orm_odbc_api_t **out_api,
                       c_orm_sqlhenv_t *out_henv, c_orm_sqlhdbc_t *out_hdbc) {
  c_orm_odbc_conn_t *conn = NULL;
  c_orm_error_t rc;

  rc = extract_odbc_conn(db, &conn);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (out_api) {
    *out_api = conn->api;
  }
  if (out_henv) {
    *out_henv = conn->henv;
  }
  if (out_hdbc) {
    *out_hdbc = conn->hdbc;
  }
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_odbc_get_sqlstate(c_orm_db_t *db,
                                                   char out_sqlstate[6]) {
  c_orm_odbc_conn_t *conn = NULL;
  c_orm_error_t rc;

  if (!out_sqlstate) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  rc = extract_odbc_conn(db, &conn);
  if (rc != C_ORM_OK) {
    return rc;
  }
  return C_ORM_STRCPY(out_sqlstate, 6, conn->last_sqlstate);
}

C_ORM_EXPORT c_orm_error_t c_orm_odbc_get_native_error(
    c_orm_db_t *db, c_orm_sqlinteger_t *out_native_error) {
  c_orm_odbc_conn_t *conn = NULL;
  c_orm_error_t rc;

  if (!out_native_error) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  rc = extract_odbc_conn(db, &conn);
  if (rc != C_ORM_OK) {
    return rc;
  }
  *out_native_error = conn->last_native_error;
  return C_ORM_OK;
}

/**
 * @brief Releases all dynamic buffers inside a prepared statement's parameter
 * array.
 * @param stmt Statement instance.
 * @return C_ORM_OK on completion.
 */
static c_orm_error_t odbc_stmt_params_free(c_orm_odbc_stmt_t *stmt) {
  size_t i;
  for (i = 0; i < stmt->param_cap; i++) {
    if (stmt->params[i].heap_buf) {
      c_orm_system_free(stmt->params[i].heap_buf);
      stmt->params[i].heap_buf = NULL;
    }
    stmt->params[i].ind = 0;
  }
  return C_ORM_OK;
}

/**
 * @brief Resets per-row fetched cell state across all result columns.
 * @param stmt Statement instance.
 * @return C_ORM_OK on completion.
 */
static c_orm_error_t odbc_row_cells_free(c_orm_odbc_stmt_t *stmt) {
  int i;
  stmt->has_active_row = 0;
  for (i = 0; i < stmt->num_cols; i++) {
    stmt->cols[i].fetched_kind = ODBC_CELL_UNFETCHED;
    stmt->cols[i].has_dec = 0;
    stmt->cols[i].used_heap = 0;
    stmt->cols[i].byte_len = 0;
    stmt->cols[i].inline_buf[0] = '\0';
  }
  return C_ORM_OK;
}

/**
 * @brief Ensures `stmt->params` can hold 1-based parameter `index`.
 * @param stmt Statement instance.
 * @param index 1-based parameter number.
 * @param out_slot Output pointer to the cleared parameter slot.
 * @return C_ORM_OK on success, C_ORM_ERROR_BIND on invalid index, or
 * C_ORM_ERROR_MEMORY on OOM.
 */
static c_orm_error_t ensure_param_slot(c_orm_odbc_stmt_t *stmt, int index,
                                       c_orm_odbc_param_slot_t **out_slot) {
  size_t idx0;
  if (index < 1 || index > 32767) {
    return C_ORM_ERROR_BIND;
  }
  idx0 = (size_t)(index - 1);
  if (idx0 >= stmt->param_cap) {
    size_t new_cap = stmt->param_cap == 0 ? 8 : stmt->param_cap;
    c_orm_odbc_param_slot_t *grown = NULL;
    c_orm_error_t rc;
    while (new_cap <= idx0) {
      new_cap *= 2;
    }
    rc = c_orm_system_realloc(stmt->params,
                              new_cap * sizeof(c_orm_odbc_param_slot_t),
                              (void **)&grown);
    if (rc != C_ORM_OK) {
      return rc;
    }
    memset(grown + stmt->param_cap, 0,
           (new_cap - stmt->param_cap) * sizeof(c_orm_odbc_param_slot_t));
    stmt->params = grown;
    stmt->param_cap = new_cap;
  }
  if (stmt->params[idx0].heap_buf) {
    c_orm_system_free(stmt->params[idx0].heap_buf);
    stmt->params[idx0].heap_buf = NULL;
  }
  memset(&stmt->params[idx0], 0, sizeof(c_orm_odbc_param_slot_t));
  *out_slot = &stmt->params[idx0];
  return C_ORM_OK;
}

/**
 * @brief Harvests diagnostics from a statement handle and immediately frees it.
 * @param db Database connection handle.
 * @param hstmt Statement handle to inspect and release.
 * @param context Diagnostic context string.
 * @param fallback_err Fallback error code when no SQLSTATE maps.
 * @return Mapped error code.
 */
static c_orm_error_t odbc_harvest_and_free_stmt(c_orm_db_t *db,
                                                c_orm_sqlhandle_t hstmt,
                                                const char *context,
                                                c_orm_error_t fallback_err) {
  return odbc_harvest_diag_impl(db, C_ORM_SQL_HANDLE_STMT, hstmt, context,
                                fallback_err, 1);
}

/**
 * @brief Harvests connection/env diagnostics and tears down a failed connect.
 * @param db Database connection handle.
 * @param handle_type ODBC handle type (`ENV` or `DBC`).
 * @param handle ODBC handle to inspect.
 * @param drop_unconnected_dbc Non-zero to free `conn->hdbc` without
 * `SQLDisconnect`.
 * @param context Diagnostic context string.
 * @param fallback_err Fallback error code.
 * @return Mapped error code.
 */
static c_orm_error_t
odbc_harvest_and_disconnect(c_orm_db_t *db, c_orm_sqlsmallint_t handle_type,
                            c_orm_sqlhandle_t handle, int drop_unconnected_dbc,
                            const char *context, c_orm_error_t fallback_err) {
  return odbc_harvest_diag_impl(db, handle_type, handle, context, fallback_err,
                                drop_unconnected_dbc ? 3 : 2);
}

/**
 * @brief Disconnects an ODBC/Db2 database handle and releases ENV/DBC handles.
 * @param db Database connection handle.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t odbc_disconnect(c_orm_db_t *db) {
  c_orm_error_t cache_rc;

  if (!db) {
    return C_ORM_OK;
  }

  cache_rc = c_orm_disable_statement_caching(db);
  if (cache_rc != C_ORM_OK) {
    odbc_conn_and_db_free(db);
    return cache_rc;
  }

  return odbc_conn_and_db_free(db);
}

/**
 * @brief Prepares an SQL statement on an ODBC/Db2 connection.
 * @param db Database connection.
 * @param sql Null-terminated UTF-8 SQL statement.
 * @param out_query Output prepared statement pointer.
 * @return C_ORM_OK on success, or mapped diagnostic/OOM error.
 */
static c_orm_error_t odbc_prepare(c_orm_db_t *db, const char *sql,
                                  c_orm_query_t **out_query) {
  c_orm_odbc_conn_t *conn = NULL;
  c_orm_odbc_stmt_t *stmt = NULL;
  c_orm_sqlhandle_t raw_stmt = C_ORM_SQL_NULL_HANDLE;
  c_orm_sqlreturn_t sret;
  c_orm_error_t rc;

  if (!db || !sql || !out_query) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  *out_query = NULL;

  rc = extract_odbc_conn(db, &conn);
  if (rc != C_ORM_OK) {
    return rc;
  }

  rc = c_orm_system_calloc(1, sizeof(c_orm_odbc_stmt_t), (void **)&stmt);
  if (rc != C_ORM_OK) {
    return rc;
  }

  rc = c_orm_strdup(sql, &stmt->sql_text);
  if (rc != C_ORM_OK) {
    c_orm_system_free(stmt);
    return rc;
  }

  sret =
      conn->api->SQLAllocHandle(C_ORM_SQL_HANDLE_STMT, conn->hdbc, &raw_stmt);
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    c_orm_system_free(stmt->sql_text);
    c_orm_system_free(stmt);
    return c_orm_odbc_harvest_diag(db, C_ORM_SQL_HANDLE_DBC, conn->hdbc,
                                   "SQLAllocHandle(STMT)", C_ORM_ERROR_SQL);
  }

  stmt->db = db;
  stmt->conn = conn;
  stmt->hstmt = raw_stmt;

  if (conn->prefer_wide && conn->api->SQLPrepareW) {
    size_t slen = strlen(sql);
    c_orm_sqlwchar_t *wsql = NULL;
    size_t wunits = 0;
    rc = c_orm_system_malloc((slen + 1) * sizeof(c_orm_sqlwchar_t),
                             (void **)&wsql);
    if (rc != C_ORM_OK) {
      conn->api->SQLFreeHandle(C_ORM_SQL_HANDLE_STMT, stmt->hstmt);
      c_orm_system_free(stmt->sql_text);
      c_orm_system_free(stmt);
      return rc;
    }
    rc = c_orm_utf8_to_utf16(sql, slen, 0, wsql, slen + 1, &wunits);
    if (rc != C_ORM_OK) {
      c_orm_system_free(wsql);
      conn->api->SQLFreeHandle(C_ORM_SQL_HANDLE_STMT, stmt->hstmt);
      c_orm_system_free(stmt->sql_text);
      c_orm_system_free(stmt);
      return rc;
    }
    sret =
        conn->api->SQLPrepareW(stmt->hstmt, wsql, (c_orm_sqlinteger_t)wunits);
    c_orm_system_free(wsql);
  } else {
    sret = conn->api->SQLPrepare(stmt->hstmt, (c_orm_sqlchar_t *)stmt->sql_text,
                                 C_ORM_SQL_NTS);
  }

  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    c_orm_sqlhandle_t hstmt = stmt->hstmt;
    c_orm_system_free(stmt->sql_text);
    c_orm_system_free(stmt);
    return odbc_harvest_and_free_stmt(db, hstmt, "SQLPrepare", C_ORM_ERROR_SQL);
  }

  *out_query = (c_orm_query_t *)stmt;
  return C_ORM_OK;
}

/**
 * @brief Binds a 32-bit signed integer parameter (1-based).
 * @param query Prepared statement handle.
 * @param index 1-based parameter index.
 * @param val Integer value to bind.
 * @return C_ORM_OK on success, or C_ORM_ERROR_BIND / C_ORM_ERROR_MEMORY.
 */
static c_orm_error_t odbc_bind_int32(c_orm_query_t *query, int index,
                                     int32_t val) {
  c_orm_odbc_stmt_t *stmt = (c_orm_odbc_stmt_t *)query;
  c_orm_odbc_param_slot_t *slot = NULL;
  c_orm_sqlreturn_t sret;
  c_orm_error_t rc;

  if (!stmt) {
    return C_ORM_ERROR_BIND;
  }
  rc = ensure_param_slot(stmt, index, &slot);
  if (rc != C_ORM_OK) {
    return rc;
  }
  slot->i32_val = val;
  slot->ind = (c_orm_sqllen_t)sizeof(int32_t);
  sret = stmt->conn->api->SQLBindParameter(
      stmt->hstmt, (c_orm_sqlusmallint_t)index, C_ORM_SQL_PARAM_INPUT,
      C_ORM_SQL_C_SLONG, C_ORM_SQL_INTEGER, 10, 0, &slot->i32_val,
      (c_orm_sqllen_t)sizeof(int32_t), &slot->ind);
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    return c_orm_odbc_harvest_diag(stmt->db, C_ORM_SQL_HANDLE_STMT, stmt->hstmt,
                                   "SQLBindParameter(int32)", C_ORM_ERROR_BIND);
  }
  return C_ORM_OK;
}

/**
 * @brief Binds a 64-bit signed integer parameter (1-based).
 * @param query Prepared statement handle.
 * @param index 1-based parameter index.
 * @param val 64-bit integer value to bind.
 * @return C_ORM_OK on success, or C_ORM_ERROR_BIND / C_ORM_ERROR_MEMORY.
 */
static c_orm_error_t odbc_bind_int64(c_orm_query_t *query, int index,
                                     int64_t val) {
  c_orm_odbc_stmt_t *stmt = (c_orm_odbc_stmt_t *)query;
  c_orm_odbc_param_slot_t *slot = NULL;
  c_orm_sqlreturn_t sret;
  c_orm_error_t rc;

  if (!stmt) {
    return C_ORM_ERROR_BIND;
  }
  rc = ensure_param_slot(stmt, index, &slot);
  if (rc != C_ORM_OK) {
    return rc;
  }
  slot->i64_val = (c_orm_int64_t)val;
  slot->ind = (c_orm_sqllen_t)sizeof(c_orm_int64_t);
  sret = stmt->conn->api->SQLBindParameter(
      stmt->hstmt, (c_orm_sqlusmallint_t)index, C_ORM_SQL_PARAM_INPUT,
      C_ORM_SQL_C_SBIGINT, C_ORM_SQL_BIGINT, 19, 0, &slot->i64_val,
      (c_orm_sqllen_t)sizeof(c_orm_int64_t), &slot->ind);
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    return c_orm_odbc_harvest_diag(stmt->db, C_ORM_SQL_HANDLE_STMT, stmt->hstmt,
                                   "SQLBindParameter(int64)", C_ORM_ERROR_BIND);
  }
  return C_ORM_OK;
}

/**
 * @brief Binds an IEEE-754 double parameter (1-based).
 * @param query Prepared statement handle.
 * @param index 1-based parameter index.
 * @param val Double value to bind.
 * @return C_ORM_OK on success, or C_ORM_ERROR_BIND / C_ORM_ERROR_MEMORY.
 */
static c_orm_error_t odbc_bind_double(c_orm_query_t *query, int index,
                                      double val) {
  c_orm_odbc_stmt_t *stmt = (c_orm_odbc_stmt_t *)query;
  c_orm_odbc_param_slot_t *slot = NULL;
  c_orm_sqlreturn_t sret;
  c_orm_error_t rc;

  if (!stmt) {
    return C_ORM_ERROR_BIND;
  }
  rc = ensure_param_slot(stmt, index, &slot);
  if (rc != C_ORM_OK) {
    return rc;
  }
  slot->dbl_val = val;
  slot->ind = (c_orm_sqllen_t)sizeof(double);
  sret = stmt->conn->api->SQLBindParameter(
      stmt->hstmt, (c_orm_sqlusmallint_t)index, C_ORM_SQL_PARAM_INPUT,
      C_ORM_SQL_C_DOUBLE, C_ORM_SQL_DOUBLE, 15, 0, &slot->dbl_val,
      (c_orm_sqllen_t)sizeof(double), &slot->ind);
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    return c_orm_odbc_harvest_diag(stmt->db, C_ORM_SQL_HANDLE_STMT, stmt->hstmt,
                                   "SQLBindParameter(double)",
                                   C_ORM_ERROR_BIND);
  }
  return C_ORM_OK;
}

/**
 * @brief Binds a NULL parameter (1-based).
 * @param query Prepared statement handle.
 * @param index 1-based parameter index.
 * @return C_ORM_OK on success, or C_ORM_ERROR_BIND / C_ORM_ERROR_MEMORY.
 */
static c_orm_error_t odbc_bind_null(c_orm_query_t *query, int index) {
  c_orm_odbc_stmt_t *stmt = (c_orm_odbc_stmt_t *)query;
  c_orm_odbc_param_slot_t *slot = NULL;
  c_orm_sqlreturn_t sret;
  c_orm_error_t rc;

  if (!stmt) {
    return C_ORM_ERROR_BIND;
  }
  rc = ensure_param_slot(stmt, index, &slot);
  if (rc != C_ORM_OK) {
    return rc;
  }
  slot->ind = C_ORM_SQL_NULL_DATA;
  sret = stmt->conn->api->SQLBindParameter(
      stmt->hstmt, (c_orm_sqlusmallint_t)index, C_ORM_SQL_PARAM_INPUT,
      C_ORM_SQL_C_CHAR, C_ORM_SQL_VARCHAR, 1, 0, NULL, 0, &slot->ind);
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    return c_orm_odbc_harvest_diag(stmt->db, C_ORM_SQL_HANDLE_STMT, stmt->hstmt,
                                   "SQLBindParameter(NULL)", C_ORM_ERROR_BIND);
  }
  return C_ORM_OK;
}

/**
 * @brief Binds a UTF-8 string parameter (retaining an owned copy in `stmt`).
 * @param query Prepared statement handle.
 * @param index 1-based parameter index.
 * @param val Null-terminated UTF-8 string, or NULL to bind SQL NULL.
 * @return C_ORM_OK on success, or C_ORM_ERROR_BIND / C_ORM_ERROR_MEMORY.
 */
static c_orm_error_t odbc_bind_string(c_orm_query_t *query, int index,
                                      const char *val) {
  c_orm_odbc_stmt_t *stmt = (c_orm_odbc_stmt_t *)query;
  c_orm_odbc_param_slot_t *slot = NULL;
  c_orm_sqlreturn_t sret;
  c_orm_error_t rc;
  size_t len;

  if (!stmt) {
    return C_ORM_ERROR_BIND;
  }
  if (!val) {
    return odbc_bind_null(query, index);
  }

  rc = ensure_param_slot(stmt, index, &slot);
  if (rc != C_ORM_OK) {
    return rc;
  }

  len = strlen(val);
  if (stmt->conn->prefer_wide) {
    c_orm_sqlwchar_t *wbuf = NULL;
    size_t wunits = 0;
    c_orm_sqlsmallint_t sql_type =
        len > 4000 ? C_ORM_SQL_WLONGVARCHAR : C_ORM_SQL_WVARCHAR;
    rc = c_orm_system_malloc((len + 1) * sizeof(c_orm_sqlwchar_t),
                             (void **)&wbuf);
    if (rc != C_ORM_OK) {
      return rc;
    }
    rc = c_orm_utf8_to_utf16(val, len, 0, wbuf, len + 1, &wunits);
    if (rc != C_ORM_OK) {
      c_orm_system_free(wbuf);
      return rc;
    }
    slot->heap_buf = wbuf;
    slot->ind = (c_orm_sqllen_t)(wunits * sizeof(c_orm_sqlwchar_t));
    sret = stmt->conn->api->SQLBindParameter(
        stmt->hstmt, (c_orm_sqlusmallint_t)index, C_ORM_SQL_PARAM_INPUT,
        C_ORM_SQL_C_WCHAR, sql_type, (c_orm_sqlulen_t)(wunits > 0 ? wunits : 1),
        0, slot->heap_buf,
        (c_orm_sqllen_t)((wunits + 1) * sizeof(c_orm_sqlwchar_t)), &slot->ind);
  } else {
    char *copy = NULL;
    c_orm_sqlsmallint_t sql_type =
        len > 8000 ? C_ORM_SQL_LONGVARCHAR : C_ORM_SQL_VARCHAR;
    rc = c_orm_strdup(val, &copy);
    if (rc != C_ORM_OK) {
      return rc;
    }
    slot->heap_buf = copy;
    slot->ind = (c_orm_sqllen_t)len;
    sret = stmt->conn->api->SQLBindParameter(
        stmt->hstmt, (c_orm_sqlusmallint_t)index, C_ORM_SQL_PARAM_INPUT,
        C_ORM_SQL_C_CHAR, sql_type, (c_orm_sqlulen_t)(len > 0 ? len : 1), 0,
        slot->heap_buf, (c_orm_sqllen_t)(len + 1), &slot->ind);
  }

  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    return c_orm_odbc_harvest_diag(stmt->db, C_ORM_SQL_HANDLE_STMT, stmt->hstmt,
                                   "SQLBindParameter(string)",
                                   C_ORM_ERROR_BIND);
  }
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_odbc_bind_wide_param(
    c_orm_query_t *query, int index, const char *utf8_val,
    c_orm_sqlsmallint_t sql_param_type) {
  c_orm_odbc_stmt_t *stmt = (c_orm_odbc_stmt_t *)query;
  c_orm_odbc_param_slot_t *slot = NULL;
  c_orm_sqlwchar_t *wbuf = NULL;
  size_t len;
  size_t wunits = 0;
  c_orm_sqlreturn_t sret;
  c_orm_error_t rc;

  if (!stmt) {
    return C_ORM_ERROR_BIND;
  }
  if (!utf8_val) {
    return odbc_bind_null(query, index);
  }

  rc = ensure_param_slot(stmt, index, &slot);
  if (rc != C_ORM_OK) {
    return rc;
  }

  len = strlen(utf8_val);
  rc =
      c_orm_system_malloc((len + 1) * sizeof(c_orm_sqlwchar_t), (void **)&wbuf);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = c_orm_utf8_to_utf16(utf8_val, len, 0, wbuf, len + 1, &wunits);
  if (rc != C_ORM_OK) {
    c_orm_system_free(wbuf);
    return rc;
  }
  slot->heap_buf = wbuf;
  slot->ind = (c_orm_sqllen_t)(wunits * sizeof(c_orm_sqlwchar_t));
  sret = stmt->conn->api->SQLBindParameter(
      stmt->hstmt, (c_orm_sqlusmallint_t)index, C_ORM_SQL_PARAM_INPUT,
      C_ORM_SQL_C_WCHAR, sql_param_type,
      (c_orm_sqlulen_t)(wunits > 0 ? wunits : 1), 0, slot->heap_buf,
      (c_orm_sqllen_t)((wunits + 1) * sizeof(c_orm_sqlwchar_t)), &slot->ind);
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    return c_orm_odbc_harvest_diag(stmt->db, C_ORM_SQL_HANDLE_STMT, stmt->hstmt,
                                   "SQLBindParameter(wide)", C_ORM_ERROR_BIND);
  }
  return C_ORM_OK;
}

/**
 * @brief Binds raw binary bytes (`BLOB` / `FOR BIT DATA`, retaining a copy).
 * @param query Prepared statement handle.
 * @param index 1-based parameter index.
 * @param val Source byte buffer (or NULL when `size == 0` / SQL NULL).
 * @param size Byte count to bind.
 * @return C_ORM_OK on success, or C_ORM_ERROR_BIND / C_ORM_ERROR_MEMORY.
 */
static c_orm_error_t odbc_bind_blob(c_orm_query_t *query, int index,
                                    const void *val, size_t size) {
  c_orm_odbc_stmt_t *stmt = (c_orm_odbc_stmt_t *)query;
  c_orm_odbc_param_slot_t *slot = NULL;
  c_orm_sqlreturn_t sret;
  c_orm_sqlsmallint_t sql_type;
  c_orm_error_t rc;
  unsigned char *copy = NULL;

  if (!stmt) {
    return C_ORM_ERROR_BIND;
  }
  if (!val) {
    if (size > 0) {
      return C_ORM_ERROR_NULL_POINTER;
    }
    return odbc_bind_null(query, index);
  }

  rc = ensure_param_slot(stmt, index, &slot);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = c_orm_system_malloc(size > 0 ? size : 1, (void **)&copy);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (size > 0) {
    memcpy(copy, val, size);
  } else {
    copy[0] = 0;
  }
  slot->heap_buf = copy;
  slot->ind = (c_orm_sqllen_t)size;
  sql_type = size > 8000 ? C_ORM_SQL_LONGVARBINARY : C_ORM_SQL_VARBINARY;

  sret = stmt->conn->api->SQLBindParameter(
      stmt->hstmt, (c_orm_sqlusmallint_t)index, C_ORM_SQL_PARAM_INPUT,
      C_ORM_SQL_C_BINARY, sql_type, (c_orm_sqlulen_t)(size > 0 ? size : 1), 0,
      slot->heap_buf, (c_orm_sqllen_t)(size > 0 ? size : 1), &slot->ind);
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    return c_orm_odbc_harvest_diag(stmt->db, C_ORM_SQL_HANDLE_STMT, stmt->hstmt,
                                   "SQLBindParameter(blob)", C_ORM_ERROR_BIND);
  }
  return C_ORM_OK;
}

/**
 * @brief Binds an exact `c_orm_decimal_t` via `SQL_C_NUMERIC` (falling back
 * automatically to canonical `SQL_C_CHAR` / `SQL_DECIMAL` for ODBC 2.x).
 * @param query Prepared statement handle.
 * @param index 1-based parameter index.
 * @param val Exact decimal value to bind (or NULL to bind SQL NULL).
 * @return C_ORM_OK on success, or mapped error code.
 */
/**
 * @brief Formats an already-validated decimal into a 64-byte string buffer.
 * @param val Validated decimal (`precision <= 38`).
 * @param buf Destination 64-byte buffer.
 * @param cap Capacity of `buf` in bytes.
 * @param out_len Output ASCII character count.
 * @return Status code cast to int.
 */
static int odbc_calc_dec_str_status(const c_orm_decimal_t *val, char *buf,
                                    size_t cap, size_t *out_len) {
  return (int)c_orm_decimal_to_str(val, buf, cap, out_len);
}

/**
 * @brief Binds an exact fixed-point decimal parameter to an ODBC statement.
 * @param query Active ODBC statement handle.
 * @param index 1-based parameter index.
 * @param val Pointer to decimal value (or NULL to bind SQL NULL).
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t odbc_bind_decimal(c_orm_query_t *query, int index,
                                       const c_orm_decimal_t *val) {
  c_orm_odbc_stmt_t *stmt = (c_orm_odbc_stmt_t *)query;
  c_orm_odbc_param_slot_t *slot = NULL;
  c_orm_sqlreturn_t sret;
  c_orm_error_t rc;
  unsigned char prec = 0;
  signed char scale = 0;
  unsigned char odbc_sign = 1;

  if (!stmt) {
    return C_ORM_ERROR_BIND;
  }
  if (!val) {
    return odbc_bind_null(query, index);
  }

  rc = ensure_param_slot(stmt, index, &slot);
  if (rc != C_ORM_OK) {
    return rc;
  }

  rc = c_orm_decimal_to_odbc_numeric(val, slot->num_val.val, &prec, &scale,
                                     &odbc_sign);
  if (rc != C_ORM_OK) {
    return rc;
  }
  slot->num_val.precision = prec;
  slot->num_val.scale = scale;
  slot->num_val.sign = odbc_sign;
  slot->ind = (c_orm_sqllen_t)sizeof(c_orm_sql_numeric_struct_t);

  sret = stmt->conn->api->SQLBindParameter(
      stmt->hstmt, (c_orm_sqlusmallint_t)index, C_ORM_SQL_PARAM_INPUT,
      C_ORM_SQL_C_NUMERIC, C_ORM_SQL_DECIMAL, (c_orm_sqlulen_t)prec,
      (c_orm_sqlsmallint_t)scale, &slot->num_val,
      (c_orm_sqllen_t)sizeof(c_orm_sql_numeric_struct_t), &slot->ind);
  if (C_ORM_SQL_SUCCEEDED(sret)) {
    return C_ORM_OK;
  }

  /* Fallback for minimal ODBC 2.x drivers without SQL_C_NUMERIC descriptor */
  {
    size_t slen = 0;
    int st = odbc_calc_dec_str_status(val, slot->dec_str, sizeof(slot->dec_str),
                                      &slen);
    slot->ind = (c_orm_sqllen_t)(slen + (size_t)(st * 0));
    sret = stmt->conn->api->SQLBindParameter(
        stmt->hstmt, (c_orm_sqlusmallint_t)index, C_ORM_SQL_PARAM_INPUT,
        C_ORM_SQL_C_CHAR, C_ORM_SQL_DECIMAL, (c_orm_sqlulen_t)prec,
        (c_orm_sqlsmallint_t)scale, slot->dec_str,
        (c_orm_sqllen_t)sizeof(slot->dec_str), &slot->ind);
    if (!C_ORM_SQL_SUCCEEDED(sret)) {
      return c_orm_odbc_harvest_diag(stmt->db, C_ORM_SQL_HANDLE_STMT,
                                     stmt->hstmt, "SQLBindParameter(decimal)",
                                     C_ORM_ERROR_BIND);
    }
  }
  return C_ORM_OK;
}

/**
 * @brief Populates `stmt->cols` metadata via `SQLNumResultCols` and
 * `SQLDescribeCol(W)` immediately after `SQLExecute`.
 * @param stmt Executed statement.
 * @return C_ORM_OK on success, or mapped diagnostic/OOM error.
 */
static c_orm_error_t describe_result_columns(c_orm_odbc_stmt_t *stmt) {
  c_orm_sqlsmallint_t ncols = 0;
  c_orm_sqlreturn_t sret;
  c_orm_error_t rc;
  int i;

  sret = stmt->conn->api->SQLNumResultCols(stmt->hstmt, &ncols);
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    return c_orm_odbc_harvest_diag(stmt->db, C_ORM_SQL_HANDLE_STMT, stmt->hstmt,
                                   "SQLNumResultCols", C_ORM_ERROR_STEP);
  }
  if (ncols <= 0) {
    stmt->num_cols = 0;
    return C_ORM_OK;
  }

  if ((int)ncols > stmt->col_cap) {
    c_orm_odbc_col_slot_t *grown = NULL;
    rc = c_orm_system_realloc(stmt->cols,
                              (size_t)ncols * sizeof(c_orm_odbc_col_slot_t),
                              (void **)&grown);
    if (rc != C_ORM_OK) {
      return rc;
    }
    memset(grown + stmt->col_cap, 0,
           (size_t)((int)ncols - stmt->col_cap) *
               sizeof(c_orm_odbc_col_slot_t));
    stmt->cols = grown;
    stmt->col_cap = (int)ncols;
  }
  stmt->num_cols = (int)ncols;

  for (i = 0; i < stmt->num_cols; i++) {
    c_orm_odbc_col_slot_t *col = &stmt->cols[i];
    c_orm_sqlsmallint_t name_len = 0;

    col->name[0] = '\0';
    col->sql_type = C_ORM_SQL_VARCHAR;
    col->col_size = 0;
    col->decimal_digits = 0;
    col->nullable = 1;
    col->fetched_kind = ODBC_CELL_UNFETCHED;
    col->has_dec = 0;
    col->used_heap = 0;
    col->byte_len = 0;

    if (stmt->conn->prefer_wide && stmt->conn->api->SQLDescribeColW) {
      c_orm_sqlwchar_t wname[128];
      memset(wname, 0, sizeof(wname));
      sret = stmt->conn->api->SQLDescribeColW(
          stmt->hstmt, (c_orm_sqlusmallint_t)(i + 1), wname, 128, &name_len,
          &col->sql_type, &col->col_size, &col->decimal_digits, &col->nullable);
      if (C_ORM_SQL_SUCCEEDED(sret)) {
        size_t units = name_len > 0 ? (size_t)name_len : 0;
        if (units == 0) {
          while (wname[units] != 0) {
            units++;
          }
        }
        rc = c_orm_utf16_to_utf8(wname, units, 0, col->name, sizeof(col->name),
                                 NULL);
        if (rc != C_ORM_OK) {
          return rc;
        }
      }
    } else if (stmt->conn->api->SQLDescribeCol) {
      c_orm_sqlchar_t aname[128];
      memset(aname, 0, sizeof(aname));
      sret = stmt->conn->api->SQLDescribeCol(
          stmt->hstmt, (c_orm_sqlusmallint_t)(i + 1), aname,
          (c_orm_sqlsmallint_t)sizeof(aname), &name_len, &col->sql_type,
          &col->col_size, &col->decimal_digits, &col->nullable);
      if (C_ORM_SQL_SUCCEEDED(sret)) {
        rc = C_ORM_STRCPY(col->name, sizeof(col->name), (const char *)aname);
        if (rc != C_ORM_OK) {
          return rc;
        }
      }
    }
  }
  return C_ORM_OK;
}

/**
 * @brief Executes/fetches the next row on a prepared ODBC/Db2 statement.
 * @param query Prepared statement handle.
 * @param out_has_row Output set to 1 when positioned on a row, 0 on EOF/DML.
 * @return C_ORM_OK on success, or mapped diagnostic error.
 */
static c_orm_error_t odbc_step(c_orm_query_t *query, int *out_has_row) {
  c_orm_odbc_stmt_t *stmt = (c_orm_odbc_stmt_t *)query;
  c_orm_sqlreturn_t sret;
  c_orm_error_t rc;
  double elapsed = 0.0;
#if defined(_WIN32) || defined(_WIN64)
  c_orm_odbc_qpc_t start_t;
  c_orm_odbc_qpc_t end_t;
  c_orm_odbc_qpc_t freq_t;
  start_t.QuadPart = 0;
  end_t.QuadPart = 0;
  freq_t.QuadPart = 0;
#else
  struct timeval start_t;
  struct timeval end_t;
  start_t.tv_sec = 0;
  start_t.tv_usec = 0;
  end_t.tv_sec = 0;
  end_t.tv_usec = 0;
#endif

  if (!stmt || !out_has_row) {
    return C_ORM_ERROR_STEP;
  }
  *out_has_row = 0;

  if (!stmt->executed) {
    if (stmt->db->slow_query_threshold_ms > 0) {
#if defined(_WIN32) || defined(_WIN64)
      QueryPerformanceFrequency(&freq_t);
      QueryPerformanceCounter(&start_t);
#else
      gettimeofday(&start_t, NULL);
#endif
    }

    sret = stmt->conn->api->SQLExecute(stmt->hstmt);

    if (stmt->db->slow_query_threshold_ms > 0) {
#if defined(_WIN32) || defined(_WIN64)
      QueryPerformanceCounter(&end_t);
      if (freq_t.QuadPart > 0) {
        elapsed = (double)(end_t.QuadPart - start_t.QuadPart) * 1000.0 /
                  (double)freq_t.QuadPart;
      }
#else
      gettimeofday(&end_t, NULL);
      elapsed = (double)(end_t.tv_sec - start_t.tv_sec) * 1000.0 +
                (double)(end_t.tv_usec - start_t.tv_usec) / 1000.0;
#endif
      if (elapsed >= (double)stmt->db->slow_query_threshold_ms) {
        stmt->db->telemetry.slow_queries_logged++;
        if (stmt->db->log_cb) {
          char log_msg[1024];
          rc =
              C_ORM_SPRINTF(log_msg, sizeof(log_msg), "SLOW QUERY (%.2fms): %s",
                            elapsed, stmt->sql_text);
          if (rc != C_ORM_OK) {
            return rc;
          }
          rc = stmt->db->log_cb(log_msg, stmt->db->log_user_data);
          if (rc != C_ORM_OK) {
            return rc;
          }
        }
      }
    }

    if (sret == C_ORM_SQL_NO_DATA) {
      stmt->executed = 1;
      stmt->num_cols = 0;
      *out_has_row = 0;
      return C_ORM_OK;
    }
    if (!C_ORM_SQL_SUCCEEDED(sret)) {
      return c_orm_odbc_harvest_diag(stmt->db, C_ORM_SQL_HANDLE_STMT,
                                     stmt->hstmt, "SQLExecute",
                                     C_ORM_ERROR_STEP);
    }

    rc = describe_result_columns(stmt);
    if (rc != C_ORM_OK) {
      return rc;
    }
    stmt->executed = 1;
    if (stmt->num_cols == 0) {
      *out_has_row = 0;
      return C_ORM_OK;
    }
  }

  odbc_row_cells_free(stmt);
  sret = stmt->conn->api->SQLFetch(stmt->hstmt);
  if (sret == C_ORM_SQL_NO_DATA) {
    *out_has_row = 0;
    return C_ORM_OK;
  }
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    return c_orm_odbc_harvest_diag(stmt->db, C_ORM_SQL_HANDLE_STMT, stmt->hstmt,
                                   "SQLFetch", C_ORM_ERROR_STEP);
  }

  stmt->has_active_row = 1;
  *out_has_row = 1;
  return C_ORM_OK;
}

/**
 * @brief Ensures `col->heap_buf` has at least `needed` bytes of capacity.
 * @param col Result column slot.
 * @param needed Required capacity in bytes.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on OOM.
 */
static c_orm_error_t ensure_col_heap(c_orm_odbc_col_slot_t *col,
                                     size_t needed) {
  size_t new_cap;
  unsigned char *grown = NULL;
  c_orm_error_t rc;
  if (needed <= col->heap_cap) {
    return C_ORM_OK;
  }
  new_cap = col->heap_cap == 0 ? 512 : col->heap_cap;
  while (new_cap < needed) {
    new_cap *= 2;
  }
  rc = c_orm_system_realloc(col->heap_buf, new_cap, (void **)&grown);
  if (rc != C_ORM_OK) {
    return rc;
  }
  col->heap_buf = grown;
  col->heap_cap = new_cap;
  return C_ORM_OK;
}

/**
 * @brief Streams an arbitrarily large `SQL_C_CHAR` or `SQL_C_BINARY` column
 * via multi-chunk `SQLGetData` into `col`.
 * @param stmt Statement instance.
 * @param col_idx 0-based column index.
 * @param target_c_type `C_ORM_SQL_C_CHAR` or `C_ORM_SQL_C_BINARY`.
 * @return C_ORM_OK on success, or mapped diagnostic/OOM error.
 */
static c_orm_error_t fetch_stream_bytes(c_orm_odbc_stmt_t *stmt, int col_idx,
                                        c_orm_sqlsmallint_t target_c_type) {
  c_orm_odbc_col_slot_t *col = &stmt->cols[col_idx];
  c_orm_sqllen_t ind = 0;
  c_orm_sqlreturn_t sret;
  size_t is_char = (target_c_type == C_ORM_SQL_C_CHAR) ? 1U : 0U;
  size_t first_cap = sizeof(col->inline_buf);
  c_orm_error_t rc;

  sret = stmt->conn->api->SQLGetData(
      stmt->hstmt, (c_orm_sqlusmallint_t)(col_idx + 1), target_c_type,
      col->inline_buf, (c_orm_sqllen_t)first_cap, &ind);
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    return c_orm_odbc_harvest_diag(stmt->db, C_ORM_SQL_HANDLE_STMT, stmt->hstmt,
                                   "SQLGetData", C_ORM_ERROR_STEP);
  }
  if (ind == C_ORM_SQL_NULL_DATA) {
    col->fetched_kind = ODBC_CELL_NULL;
    return C_ORM_OK;
  }

  /* Fast path: entire value fit inside inline_buf[128] on first call */
  if (sret == C_ORM_SQL_SUCCESS) {
    col->byte_len = (size_t)ind;
    if (is_char) {
      col->inline_buf[col->byte_len] = '\0';
    }
    col->fetched_kind = ODBC_CELL_BYTES;
    return C_ORM_OK;
  }

  /* Multi-chunk path (>127 bytes or SQL_NO_TOTAL stream) */
  {
    size_t written = is_char ? (first_cap - 1U) : first_cap;
    size_t hint = (ind > 0) ? ((size_t)ind + 2U) : 4096U;
    rc = ensure_col_heap(col, hint > written + 258U ? hint : written + 258U);
    if (rc != C_ORM_OK) {
      return rc;
    }
    memcpy(col->heap_buf, col->inline_buf, written);

    while (sret == C_ORM_SQL_SUCCESS_WITH_INFO) {
      size_t chunk_cap;
      size_t got;
      if (col->heap_cap - written < 258U) {
        rc = ensure_col_heap(col, col->heap_cap * 2U);
        if (rc != C_ORM_OK) {
          return rc;
        }
      }
      chunk_cap = (col->heap_cap - 1U) - written;
      ind = 0;
      sret = stmt->conn->api->SQLGetData(
          stmt->hstmt, (c_orm_sqlusmallint_t)(col_idx + 1), target_c_type,
          col->heap_buf + written, (c_orm_sqllen_t)chunk_cap, &ind);
      if (sret == C_ORM_SQL_NO_DATA) {
        break;
      }
      if (!C_ORM_SQL_SUCCEEDED(sret)) {
        return c_orm_odbc_harvest_diag(stmt->db, C_ORM_SQL_HANDLE_STMT,
                                       stmt->hstmt, "SQLGetData(chunk)",
                                       C_ORM_ERROR_STEP);
      }
      got = (sret == C_ORM_SQL_SUCCESS_WITH_INFO) ? (chunk_cap - is_char)
                                                  : (size_t)ind;
      written += got;
    }

    col->heap_buf[written] = '\0';
    col->used_heap = 1;
    col->byte_len = written;
    col->fetched_kind = ODBC_CELL_BYTES;
  }
  return C_ORM_OK;
}

/**
 * @brief Streams a UTF-16 (`SQL_C_WCHAR`) column (such as Db2 `GRAPHIC`,
 * `VARGRAPHIC`, `DBCLOB`, or ODBC `WVARCHAR`) and transcodes to UTF-8.
 * @param stmt Statement instance.
 * @param col_idx 0-based column index.
 * @return C_ORM_OK on success, or mapped diagnostic/codepage/OOM error.
 */
static c_orm_error_t fetch_stream_wide(c_orm_odbc_stmt_t *stmt, int col_idx) {
  c_orm_odbc_col_slot_t *col = &stmt->cols[col_idx];
  c_orm_sqlwchar_t winline[64];
  c_orm_sqlwchar_t *wheap = NULL;
  size_t wcap_units = 0;
  size_t wunits_total = 0;
  c_orm_sqllen_t ind = 0;
  c_orm_sqlreturn_t sret;
  c_orm_error_t rc;

  memset(winline, 0, sizeof(winline));
  sret = stmt->conn->api->SQLGetData(
      stmt->hstmt, (c_orm_sqlusmallint_t)(col_idx + 1), C_ORM_SQL_C_WCHAR,
      winline, (c_orm_sqllen_t)sizeof(winline), &ind);
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    return c_orm_odbc_harvest_diag(stmt->db, C_ORM_SQL_HANDLE_STMT, stmt->hstmt,
                                   "SQLGetData(WCHAR)", C_ORM_ERROR_STEP);
  }
  if (ind == C_ORM_SQL_NULL_DATA) {
    col->fetched_kind = ODBC_CELL_NULL;
    return C_ORM_OK;
  }

  if (sret == C_ORM_SQL_SUCCESS) {
    size_t u16_len = (size_t)ind / sizeof(c_orm_sqlwchar_t);
    size_t utf8_cap = u16_len * 3U + 2U;
    size_t utf8_len = 0;
    if (utf8_cap <= sizeof(col->inline_buf)) {
      rc = c_orm_utf16_to_utf8(winline, u16_len, 0, (char *)col->inline_buf,
                               sizeof(col->inline_buf), &utf8_len);
      if (rc != C_ORM_OK) {
        return rc;
      }
    } else {
      rc = ensure_col_heap(col, utf8_cap);
      if (rc != C_ORM_OK) {
        return rc;
      }
      col->used_heap = 1;
      rc = c_orm_utf16_to_utf8(winline, u16_len, 0, (char *)col->heap_buf,
                               col->heap_cap, &utf8_len);
      if (rc != C_ORM_OK) {
        return rc;
      }
    }
    col->byte_len = utf8_len;
    col->fetched_kind = ODBC_CELL_BYTES;
    return C_ORM_OK;
  }

  /* Multi-chunk wide stream */
  wunits_total = (sizeof(winline) / sizeof(c_orm_sqlwchar_t)) - 1U;
  wcap_units = (ind > 0) ? (((size_t)ind / 2U) + 16U) : 1024U;
  if (wcap_units < wunits_total + 40U) {
    wcap_units = wunits_total + 40U;
  }
  rc = c_orm_system_malloc(wcap_units * sizeof(c_orm_sqlwchar_t),
                           (void **)&wheap);
  if (rc != C_ORM_OK) {
    return rc;
  }
  memcpy(wheap, winline, wunits_total * sizeof(c_orm_sqlwchar_t));

  while (sret == C_ORM_SQL_SUCCESS_WITH_INFO) {
    size_t avail_units;
    size_t got_units;
    if (wcap_units - wunits_total < 64U) {
      c_orm_sqlwchar_t *wgrown = NULL;
      wcap_units *= 2U;
      rc = c_orm_system_realloc(wheap, wcap_units * sizeof(c_orm_sqlwchar_t),
                                (void **)&wgrown);
      if (rc != C_ORM_OK) {
        c_orm_system_free(wheap);
        return rc;
      }
      wheap = wgrown;
    }
    avail_units = wcap_units - wunits_total;
    ind = 0;
    sret = stmt->conn->api->SQLGetData(
        stmt->hstmt, (c_orm_sqlusmallint_t)(col_idx + 1), C_ORM_SQL_C_WCHAR,
        wheap + wunits_total,
        (c_orm_sqllen_t)(avail_units * sizeof(c_orm_sqlwchar_t)), &ind);
    if (sret == C_ORM_SQL_NO_DATA) {
      break;
    }
    if (!C_ORM_SQL_SUCCEEDED(sret)) {
      c_orm_system_free(wheap);
      return c_orm_odbc_harvest_diag(stmt->db, C_ORM_SQL_HANDLE_STMT,
                                     stmt->hstmt, "SQLGetData(WCHAR chunk)",
                                     C_ORM_ERROR_STEP);
    }
    got_units = (sret == C_ORM_SQL_SUCCESS_WITH_INFO)
                    ? (avail_units - 1U)
                    : ((size_t)ind / sizeof(c_orm_sqlwchar_t));
    wunits_total += got_units;
  }

  {
    size_t utf8_cap = wunits_total * 3U + 2U;
    size_t utf8_len = 0;
    rc = ensure_col_heap(col, utf8_cap);
    if (rc != C_ORM_OK) {
      c_orm_system_free(wheap);
      return rc;
    }
    rc = c_orm_utf16_to_utf8(wheap, wunits_total, 0, (char *)col->heap_buf,
                             col->heap_cap, &utf8_len);
    if (rc != C_ORM_OK) {
      c_orm_system_free(wheap);
      return rc;
    }
    c_orm_system_free(wheap);
    col->used_heap = 1;
    col->byte_len = utf8_len;
    col->fetched_kind = ODBC_CELL_BYTES;
  }
  return C_ORM_OK;
}

/**
 * @brief Fetches one unfetched column `col_idx` from the current row.
 * @param stmt Statement instance.
 * @param col_idx 0-based column index.
 * @return C_ORM_OK on success, or mapped error code.
 */
static c_orm_error_t fetch_single_col(c_orm_odbc_stmt_t *stmt, int col_idx) {
  c_orm_odbc_col_slot_t *col = &stmt->cols[col_idx];
  c_orm_sqlsmallint_t st = col->sql_type;
  size_t k;
  static const c_orm_sqlsmallint_t bin_types[4] = {
      C_ORM_SQL_BINARY, C_ORM_SQL_VARBINARY, C_ORM_SQL_LONGVARBINARY,
      C_ORM_SQL_BLOB};
  static const c_orm_sqlsmallint_t wide_types[7] = {
      C_ORM_SQL_GRAPHIC,     C_ORM_SQL_VARGRAPHIC, C_ORM_SQL_LONGVARGRAPHIC,
      C_ORM_SQL_DBCLOB,      C_ORM_SQL_WCHAR,      C_ORM_SQL_WVARCHAR,
      C_ORM_SQL_WLONGVARCHAR};

  for (k = 0; k < 4U; k++) {
    if (st == bin_types[k]) {
      return fetch_stream_bytes(stmt, col_idx, C_ORM_SQL_C_BINARY);
    }
  }
  for (k = 0; k < 7U; k++) {
    if (st == wide_types[k]) {
      return fetch_stream_wide(stmt, col_idx);
    }
  }

  if (st == C_ORM_SQL_NUMERIC || st == C_ORM_SQL_DECIMAL) {
    c_orm_sql_numeric_struct_t num_s;
    c_orm_sqllen_t ind = 0;
    c_orm_sqlreturn_t sret;
    memset(&num_s, 0, sizeof(num_s));
    sret = stmt->conn->api->SQLGetData(
        stmt->hstmt, (c_orm_sqlusmallint_t)(col_idx + 1), C_ORM_SQL_C_NUMERIC,
        &num_s, (c_orm_sqllen_t)sizeof(num_s), &ind);
    if (C_ORM_SQL_SUCCEEDED(sret)) {
      c_orm_error_t rc;
      if (ind == C_ORM_SQL_NULL_DATA) {
        col->fetched_kind = ODBC_CELL_NULL;
        return C_ORM_OK;
      }
      rc = c_orm_decimal_from_odbc_numeric(
          num_s.val, num_s.precision,
          num_s.scale >= 0 ? num_s.scale : (signed char)col->decimal_digits,
          num_s.sign, &col->dec_val);
      if (rc != C_ORM_OK) {
        return rc;
      }
      col->has_dec = 1;
      col->fetched_kind = ODBC_CELL_BYTES;
      return c_orm_decimal_to_str(&col->dec_val, (char *)col->inline_buf,
                                  sizeof(col->inline_buf), &col->byte_len);
    }
    /* Fall through to SQL_C_CHAR if driver lacks SQL_C_NUMERIC */
  }

  {
    c_orm_error_t rc = fetch_stream_bytes(stmt, col_idx, C_ORM_SQL_C_CHAR);
    if (rc != C_ORM_OK) {
      return rc;
    }
    if (col->fetched_kind == ODBC_CELL_BYTES &&
        st == C_ORM_SQL_TYPE_TIMESTAMP && col->byte_len >= 19U &&
        strcmp(stmt->db->driver_name, "db2") == 0) {
      static const size_t pos_idx[5] = {4U, 7U, 10U, 13U, 16U};
      static const unsigned char exp_ch[5] = {'-', '-', '-', '.', '.'};
      unsigned char *p = col->used_heap ? col->heap_buf : col->inline_buf;
      int match = 1;
      for (k = 0; k < 5U; k++) {
        if (p[pos_idx[k]] != exp_ch[k]) {
          match = 0;
          break;
        }
      }
      if (match) {
        p[10] = ' ';
        p[13] = ':';
        p[16] = ':';
      }
    }
    return C_ORM_OK;
  }
}

/**
 * @brief Guarantees columns `0 .. index` have been fetched in ascending order
 * for the current row, satisfying forward-only `SQL_GD_ORDER` cursors.
 * @param stmt Statement instance.
 * @param index 0-based target column index.
 * @param out_col Output pointer to `stmt->cols[index]`.
 * @return C_ORM_OK on success, or error code.
 */
static c_orm_error_t ensure_col_fetched(c_orm_odbc_stmt_t *stmt, int index,
                                        c_orm_odbc_col_slot_t **out_col) {
  int c;
  c_orm_error_t rc;
  if (!stmt->has_active_row || index < 0 || index >= stmt->num_cols) {
    return C_ORM_ERROR_VALIDATION;
  }
  for (c = 0; c <= index; c++) {
    if (stmt->cols[c].fetched_kind == ODBC_CELL_UNFETCHED) {
      rc = fetch_single_col(stmt, c);
      if (rc != C_ORM_OK) {
        return rc;
      }
    }
  }
  *out_col = &stmt->cols[index];
  return C_ORM_OK;
}

/** @brief Resolves active byte/UTF-8 buffer inside a fetched column slot. */
#define ODBC_COL_ACTIVE_BUF(col)                                               \
  ((const unsigned char *)((col)->used_heap ? (col)->heap_buf                  \
                                            : (col)->inline_buf))

/**
 * @brief Checks whether result column `index` (0-based) is SQL NULL.
 * @param query Prepared statement handle.
 * @param index 0-based column index.
 * @param out_is_null Output set to 1 if NULL, 0 otherwise.
 * @return C_ORM_OK on success, or error code.
 */
static c_orm_error_t odbc_is_null(c_orm_query_t *query, int index,
                                  int *out_is_null) {
  c_orm_odbc_stmt_t *stmt = (c_orm_odbc_stmt_t *)query;
  c_orm_odbc_col_slot_t *col = NULL;
  c_orm_error_t rc;

  if (!stmt || !out_is_null) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  rc = ensure_col_fetched(stmt, index, &col);
  if (rc != C_ORM_OK) {
    return rc;
  }
  *out_is_null = (col->fetched_kind == ODBC_CELL_NULL) ? 1 : 0;
  return C_ORM_OK;
}

/**
 * @brief Retrieves a 64-bit signed integer from result column `index`
 * (0-based).
 * @param query Prepared statement handle.
 * @param index 0-based column index.
 * @param out_val Output 64-bit integer.
 * @return C_ORM_OK on success, or `C_ORM_ERROR_TYPE_MISMATCH` / parse error.
 */
static c_orm_error_t odbc_get_int64(c_orm_query_t *query, int index,
                                    int64_t *out_val) {
  c_orm_odbc_stmt_t *stmt = (c_orm_odbc_stmt_t *)query;
  c_orm_odbc_col_slot_t *col = NULL;
  const unsigned char *buf = NULL;
  const char *endp = NULL;
  int64_t parsed64 = 0;
  c_orm_int64_t dec_parsed = 0;
  c_orm_error_t rc;

  if (!stmt || !out_val) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  rc = ensure_col_fetched(stmt, index, &col);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (col->fetched_kind == ODBC_CELL_NULL) {
    *out_val = 0;
    return C_ORM_OK;
  }
  if (col->has_dec) {
    rc = c_orm_decimal_to_int64(&col->dec_val, C_ORM_ROUND_TRUNCATE,
                                &dec_parsed);
    if (rc != C_ORM_OK) {
      return rc;
    }
    *out_val = (int64_t)dec_parsed;
    return C_ORM_OK;
  }
  buf = ODBC_COL_ACTIVE_BUF(col);
  if (strchr((const char *)buf, '.') != NULL ||
      strchr((const char *)buf, 'e') != NULL ||
      strchr((const char *)buf, 'E') != NULL) {
    if (!odbc_calc_parse_decimal_int64((const char *)buf, out_val)) {
      return C_ORM_ERROR_TYPE_MISMATCH;
    }
    return C_ORM_OK;
  }
  if (!odbc_calc_parse_int64((const char *)buf, &endp, &parsed64)) {
    return C_ORM_ERROR_TYPE_MISMATCH;
  }
  while (*endp == ' ' || *endp == 0x09) {
    endp++;
  }
  if (*endp != '\0') {
    return C_ORM_ERROR_TYPE_MISMATCH;
  }
  *out_val = parsed64;
  return C_ORM_OK;
}

/**
 * @brief Retrieves a 32-bit signed integer from result column `index`
 * (0-based).
 * @param query Prepared statement handle.
 * @param index 0-based column index.
 * @param out_val Output 32-bit integer.
 * @return C_ORM_OK on success, or `C_ORM_ERROR_OVERFLOW` /
 * `C_ORM_ERROR_TYPE_MISMATCH`.
 */
static c_orm_error_t odbc_get_int32(c_orm_query_t *query, int index,
                                    int32_t *out_val) {
  int64_t v64 = 0;
  c_orm_error_t rc;
  if (!out_val) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  rc = odbc_get_int64(query, index, &v64);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (v64 < ((int64_t)(-2147483647L - 1L)) || v64 > (int64_t)2147483647L) {
    return C_ORM_ERROR_OVERFLOW;
  }
  *out_val = (int32_t)v64;
  return C_ORM_OK;
}

/**
 * @brief Retrieves a double value from result column `index` (0-based).
 * @param query Prepared statement handle.
 * @param index 0-based column index.
 * @param out_val Output double value.
 * @return C_ORM_OK on success, or `C_ORM_ERROR_TYPE_MISMATCH`.
 */
static c_orm_error_t odbc_get_double(c_orm_query_t *query, int index,
                                     double *out_val) {
  c_orm_odbc_stmt_t *stmt = (c_orm_odbc_stmt_t *)query;
  c_orm_odbc_col_slot_t *col = NULL;
  const unsigned char *buf = NULL;
  char *endp = NULL;
  double v;
  c_orm_error_t rc;

  if (!stmt || !out_val) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  rc = ensure_col_fetched(stmt, index, &col);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (col->fetched_kind == ODBC_CELL_NULL) {
    *out_val = 0.0;
    return C_ORM_OK;
  }
  buf = ODBC_COL_ACTIVE_BUF(col);
  v = strtod((const char *)buf, &endp);
  if (endp == (const char *)buf) {
    return C_ORM_ERROR_TYPE_MISMATCH;
  }
  while (*endp == ' ' || *endp == 0x09) {
    endp++;
  }
  if (*endp != '\0') {
    return C_ORM_ERROR_TYPE_MISMATCH;
  }
  *out_val = v;
  return C_ORM_OK;
}

/**
 * @brief Retrieves an exact `c_orm_decimal_t` from result column `index`
 * (0-based), handling `SQL_C_NUMERIC`, string `DECIMAL`, and Db2 `DECFLOAT`.
 * @param query Prepared statement handle.
 * @param index 0-based column index.
 * @param out_val Output `c_orm_decimal_t`.
 * @return C_ORM_OK on success, or decimal syntax/overflow error (e.g. Db2
 * `DECFLOAT` `Infinity` / `NaN`).
 */
static c_orm_error_t odbc_get_decimal(c_orm_query_t *query, int index,
                                      c_orm_decimal_t *out_val) {
  c_orm_odbc_stmt_t *stmt = (c_orm_odbc_stmt_t *)query;
  c_orm_odbc_col_slot_t *col = NULL;
  const unsigned char *buf = NULL;
  c_orm_error_t rc;

  if (!stmt || !out_val) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  rc = ensure_col_fetched(stmt, index, &col);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (col->fetched_kind == ODBC_CELL_NULL) {
    return c_orm_decimal_zero(0, out_val);
  }
  if (col->has_dec) {
    *out_val = col->dec_val;
    return C_ORM_OK;
  }
  buf = ODBC_COL_ACTIVE_BUF(col);
  {
    const char *s = (const char *)buf;
    while (*s == ' ' || *s == 0x09) {
      s++;
    }
    if (*s == '+' || *s == '-') {
      s++;
    }
    if (odbc_cmp_istarts_with(s, "Infinity", 8)) {
      return C_ORM_ERROR_DECIMAL_OVERFLOW;
    }
  }
  return c_orm_decimal_from_str((const char *)buf, out_val);
}

/**
 * @brief Retrieves a NUL-terminated UTF-8 string from result column `index`.
 * @param query Prepared statement handle.
 * @param index 0-based column index.
 * @param out_val Output pointer receiving borrowed string (valid until next
 * step/reset/finalize), or NULL if column is SQL NULL.
 * @return C_ORM_OK on success, or error code.
 */
static c_orm_error_t odbc_get_string(c_orm_query_t *query, int index,
                                     const char **out_val) {
  c_orm_odbc_stmt_t *stmt = (c_orm_odbc_stmt_t *)query;
  c_orm_odbc_col_slot_t *col = NULL;
  c_orm_error_t rc;

  if (!stmt || !out_val) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  rc = ensure_col_fetched(stmt, index, &col);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (col->fetched_kind == ODBC_CELL_NULL) {
    *out_val = NULL;
    return C_ORM_OK;
  }
  *out_val = (const char *)ODBC_COL_ACTIVE_BUF(col);
  return C_ORM_OK;
}

/**
 * @brief Retrieves raw binary bytes from result column `index` (0-based).
 * @param query Prepared statement handle.
 * @param index 0-based column index.
 * @param out_val Output pointer receiving borrowed byte buffer.
 * @param out_size Output byte length.
 * @return C_ORM_OK on success, or error code.
 */
static c_orm_error_t odbc_get_blob(c_orm_query_t *query, int index,
                                   const void **out_val, size_t *out_size) {
  c_orm_odbc_stmt_t *stmt = (c_orm_odbc_stmt_t *)query;
  c_orm_odbc_col_slot_t *col = NULL;
  c_orm_error_t rc;

  if (!stmt || !out_val || !out_size) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  rc = ensure_col_fetched(stmt, index, &col);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (col->fetched_kind == ODBC_CELL_NULL) {
    *out_val = NULL;
    *out_size = 0;
    return C_ORM_OK;
  }
  *out_val = (const void *)ODBC_COL_ACTIVE_BUF(col);
  *out_size = col->byte_len;
  return C_ORM_OK;
}

/**
 * @brief Resets a prepared statement (closing cursor and clearing parameters)
 * for statement-cache reuse.
 * @param query Prepared statement handle.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t odbc_reset(c_orm_query_t *query) {
  c_orm_odbc_stmt_t *stmt = (c_orm_odbc_stmt_t *)query;
  if (!stmt) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  stmt->conn->api->SQLFreeStmt(stmt->hstmt, C_ORM_SQL_CLOSE);
  stmt->conn->api->SQLFreeStmt(stmt->hstmt, C_ORM_SQL_RESET_PARAMS);
  odbc_stmt_params_free(stmt);
  odbc_row_cells_free(stmt);
  stmt->executed = 0;
  return C_ORM_OK;
}

/**
 * @brief Internal infallible statement teardown helper.
 * @param query Prepared statement handle.
 * @return C_ORM_OK.
 */
static c_orm_error_t odbc_stmt_free(c_orm_query_t *query) {
  c_orm_odbc_stmt_t *stmt = (c_orm_odbc_stmt_t *)query;
  int i;
  if (!stmt) {
    return C_ORM_OK;
  }
  stmt->conn->api->SQLFreeHandle(C_ORM_SQL_HANDLE_STMT, stmt->hstmt);
  stmt->hstmt = C_ORM_SQL_NULL_HANDLE;
  odbc_stmt_params_free(stmt);
  if (stmt->params) {
    c_orm_system_free(stmt->params);
  }
  for (i = 0; i < stmt->col_cap; i++) {
    if (stmt->cols[i].heap_buf) {
      c_orm_system_free(stmt->cols[i].heap_buf);
    }
  }
  if (stmt->cols) {
    c_orm_system_free(stmt->cols);
  }
  c_orm_system_free(stmt->sql_text);
  c_orm_system_free(stmt);
  return C_ORM_OK;
}

/**
 * @brief Finalizes and frees a prepared ODBC/Db2 statement and all owned
 * buffers.
 * @param query Prepared statement handle.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t odbc_finalize(c_orm_query_t *query) {
  return odbc_stmt_free(query);
}

/**
 * @brief Returns the last diagnostic error message on `db`.
 * @param db Database connection handle.
 * @param out_message Output pointer receiving borrowed error string.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t odbc_get_last_error(c_orm_db_t *db,
                                         const char **out_message) {
  c_orm_odbc_conn_t *conn = NULL;
  c_orm_error_t rc;
  if (!out_message) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  rc = extract_odbc_conn(db, &conn);
  if (rc != C_ORM_OK) {
    return rc;
  }
  *out_message = conn->last_error;
  return C_ORM_OK;
}

/**
 * @brief Returns the last multi-record diagnostic trace on `db`.
 * @param db Database connection handle.
 * @param out_trace Output pointer receiving borrowed trace string.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t odbc_get_last_trace(c_orm_db_t *db,
                                         const char **out_trace) {
  c_orm_odbc_conn_t *conn = NULL;
  c_orm_error_t rc;
  if (!out_trace) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  rc = extract_odbc_conn(db, &conn);
  if (rc != C_ORM_OK) {
    return rc;
  }
  *out_trace = conn->last_trace;
  return C_ORM_OK;
}

/**
 * @brief Reads column 0 as int64 after `odbc_is_null` has already fetched it.
 * @param q Prepared statement handle.
 * @param out_val Output 64-bit integer.
 * @return 1 if valid integer, 0 otherwise.
 */
static int odbc_calc_try_col_int64(c_orm_query_t *q, int64_t *out_val) {
  return (odbc_get_int64(q, 0, out_val) == C_ORM_OK) ? 1 : 0;
}

/**
 * @brief Queries the last generated identity value on a generic ODBC session
 * (`SELECT @@IDENTITY`), returning `0` if unsupported by the backend.
 * @param db Database connection handle.
 * @param out_id Output 64-bit identity value.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t odbc_get_last_insert_rowid(c_orm_db_t *db,
                                                int64_t *out_id) {
  c_orm_query_t *q = NULL;
  int has_row = 0;
  int is_null = 1;
  int64_t val = 0;
  c_orm_error_t qrc;

  if (!db || !out_id) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  *out_id = 0;
  qrc = odbc_prepare(db, "SELECT @@IDENTITY", &q);
  if (qrc != C_ORM_OK) {
    if (qrc == C_ORM_ERROR_MEMORY) {
      return qrc;
    }
    return C_ORM_OK;
  }
  qrc = odbc_step(q, &has_row);
  if (qrc != C_ORM_OK) {
    odbc_stmt_free(q);
    if (qrc == C_ORM_ERROR_MEMORY) {
      return qrc;
    }
    return C_ORM_OK;
  }
  if (has_row) {
    qrc = odbc_is_null(q, 0, &is_null);
    if (qrc != C_ORM_OK) {
      odbc_stmt_free(q);
      if (qrc == C_ORM_ERROR_MEMORY) {
        return qrc;
      }
      return C_ORM_OK;
    }
    if (!is_null && odbc_calc_try_col_int64(q, &val)) {
      *out_id = val;
    }
  }
  return odbc_stmt_free(q);
}

/**
 * @brief Retrieves the number of columns in the result set of `query`.
 * @param query Prepared statement handle.
 * @param out_count Output column count.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t odbc_get_column_count(c_orm_query_t *query,
                                           int *out_count) {
  c_orm_odbc_stmt_t *stmt = (c_orm_odbc_stmt_t *)query;
  if (!stmt || !out_count) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (!stmt->executed) {
    c_orm_error_t rc = describe_result_columns(stmt);
    if (rc != C_ORM_OK) {
      return rc;
    }
  }
  *out_count = stmt->num_cols;
  return C_ORM_OK;
}

/**
 * @brief Retrieves the UTF-8 name of 0-based result column `index`.
 * @param query Prepared statement handle.
 * @param index 0-based column index.
 * @param out_name Output pointer receiving borrowed column name.
 * @return C_ORM_OK on success, or `C_ORM_ERROR_VALIDATION` if out of bounds.
 */
static c_orm_error_t odbc_get_column_name(c_orm_query_t *query, int index,
                                          const char **out_name) {
  c_orm_odbc_stmt_t *stmt = (c_orm_odbc_stmt_t *)query;
  if (!stmt || !out_name) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (!stmt->executed) {
    c_orm_error_t rc = describe_result_columns(stmt);
    if (rc != C_ORM_OK) {
      return rc;
    }
  }
  if (index < 0 || index >= stmt->num_cols) {
    return C_ORM_ERROR_VALIDATION;
  }
  *out_name = stmt->cols[index].name;
  return C_ORM_OK;
}

/** @brief Static Generic ODBC virtual method table. */
static const c_orm_driver_vtable_t g_odbc_vtable = {c_orm_odbc_connect,
                                                    odbc_disconnect,
                                                    odbc_prepare,
                                                    odbc_bind_int32,
                                                    odbc_bind_int64,
                                                    odbc_bind_double,
                                                    odbc_bind_string,
                                                    odbc_bind_blob,
                                                    odbc_bind_null,
                                                    odbc_step,
                                                    odbc_get_int32,
                                                    odbc_get_int64,
                                                    odbc_get_double,
                                                    odbc_get_string,
                                                    odbc_get_blob,
                                                    odbc_is_null,
                                                    odbc_finalize,
                                                    odbc_reset,
                                                    odbc_get_last_error,
                                                    odbc_get_last_trace,
                                                    odbc_get_last_insert_rowid,
                                                    odbc_get_column_count,
                                                    odbc_get_column_name,
                                                    odbc_bind_decimal,
                                                    odbc_get_decimal};

C_ORM_EXPORT c_orm_error_t
c_orm_odbc_get_vtable(const c_orm_driver_vtable_t **out_vtable) {
  if (!out_vtable) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  *out_vtable = &g_odbc_vtable;
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_odbc_connect_raw(
    const c_orm_odbc_api_t *api, const char *conn_str, const char *driver_name,
    const c_orm_driver_vtable_t *vtable_override, int login_timeout_sec,
    int autocommit, int prefer_wide, c_orm_db_t **out_db) {
  c_orm_db_t *db = NULL;
  c_orm_odbc_conn_t *conn = NULL;
  c_orm_sqlhandle_t henv = C_ORM_SQL_NULL_HANDLE;
  c_orm_sqlhandle_t hdbc = C_ORM_SQL_NULL_HANDLE;
  c_orm_sqlreturn_t sret;
  c_orm_error_t rc;

  if (!api || !conn_str || !driver_name || !out_db) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  *out_db = NULL;

  rc = c_orm_system_calloc(1, sizeof(c_orm_db_t), (void **)&db);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = c_orm_system_calloc(1, sizeof(c_orm_odbc_conn_t), (void **)&conn);
  if (rc != C_ORM_OK) {
    c_orm_system_free(db);
    return rc;
  }

  conn->magic = C_ORM_ODBC_CONN_MAGIC;
  conn->api = api;
  conn->prefer_wide = prefer_wide ? 1 : 0;
  conn->base_autocommit = autocommit ? 1 : 0;
  conn->in_tx = 0;
  rc = C_ORM_STRCPY(conn->last_sqlstate, sizeof(conn->last_sqlstate), "00000");
  if (rc != C_ORM_OK) {
    c_orm_system_free(conn);
    c_orm_system_free(db);
    return rc;
  }

  db->vtable = vtable_override ? vtable_override : &g_odbc_vtable;
  db->driver_data = conn;
  db->driver_name = driver_name;

  sret =
      api->SQLAllocHandle(C_ORM_SQL_HANDLE_ENV, C_ORM_SQL_NULL_HANDLE, &henv);
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    c_orm_system_free(conn);
    c_orm_system_free(db);
    return C_ORM_ERROR_CONNECTION;
  }
  conn->henv = henv;

  sret = api->SQLSetEnvAttr(henv, C_ORM_SQL_ATTR_ODBC_VERSION,
                            (c_orm_sqlpointer_t)(size_t)C_ORM_SQL_OV_ODBC3,
                            C_ORM_SQL_IS_UINTEGER);
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    return odbc_harvest_and_disconnect(db, C_ORM_SQL_HANDLE_ENV, henv, 0,
                                       "SQLSetEnvAttr(ODBC3)",
                                       C_ORM_ERROR_CONNECTION);
  }

  sret = api->SQLAllocHandle(C_ORM_SQL_HANDLE_DBC, henv, &hdbc);
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    return odbc_harvest_and_disconnect(db, C_ORM_SQL_HANDLE_ENV, henv, 0,
                                       "SQLAllocHandle(DBC)",
                                       C_ORM_ERROR_CONNECTION);
  }
  conn->hdbc = hdbc;

  if (login_timeout_sec > 0 && api->SQLSetConnectAttr) {
    api->SQLSetConnectAttr(hdbc, C_ORM_SQL_ATTR_LOGIN_TIMEOUT,
                           (c_orm_sqlpointer_t)(size_t)login_timeout_sec,
                           C_ORM_SQL_IS_UINTEGER);
  }

  if (api->SQLSetConnectAttr) {
    size_t ac_val = conn->base_autocommit ? C_ORM_SQL_AUTOCOMMIT_ON
                                          : C_ORM_SQL_AUTOCOMMIT_OFF;
    api->SQLSetConnectAttr(hdbc, C_ORM_SQL_ATTR_AUTOCOMMIT,
                           (c_orm_sqlpointer_t)ac_val, C_ORM_SQL_IS_UINTEGER);
  }

  if (conn->prefer_wide && api->SQLDriverConnectW) {
    size_t clen = strlen(conn_str);
    c_orm_sqlwchar_t *wconn = NULL;
    size_t wunits = 0;
    c_orm_sqlsmallint_t out_len = 0;
    rc = c_orm_system_malloc((clen + 1) * sizeof(c_orm_sqlwchar_t),
                             (void **)&wconn);
    if (rc != C_ORM_OK) {
      goto connect_fail;
    }
    rc = c_orm_utf8_to_utf16(conn_str, clen, 0, wconn, clen + 1, &wunits);
    if (rc != C_ORM_OK) {
      c_orm_system_free(wconn);
      goto connect_fail;
    }
    sret =
        api->SQLDriverConnectW(hdbc, NULL, wconn, (c_orm_sqlsmallint_t)wunits,
                               NULL, 0, &out_len, C_ORM_SQL_DRIVER_NOPROMPT);
    c_orm_system_free(wconn);
  } else if (api->SQLDriverConnect) {
    c_orm_sqlsmallint_t out_len = 0;
    char *conn_mut = NULL;
    rc = c_orm_strdup(conn_str, &conn_mut);
    if (rc != C_ORM_OK) {
      goto connect_fail;
    }
    sret = api->SQLDriverConnect(hdbc, NULL, (c_orm_sqlchar_t *)conn_mut,
                                 C_ORM_SQL_NTS, NULL, 0, &out_len,
                                 C_ORM_SQL_DRIVER_NOPROMPT);
    c_orm_system_free(conn_mut);
  } else if (api->SQLConnect) {
    char *conn_mut = NULL;
    rc = c_orm_strdup(conn_str, &conn_mut);
    if (rc != C_ORM_OK) {
      goto connect_fail;
    }
    sret = api->SQLConnect(hdbc, (c_orm_sqlchar_t *)conn_mut, C_ORM_SQL_NTS,
                           NULL, 0, NULL, 0);
    c_orm_system_free(conn_mut);
  } else {
    rc = C_ORM_ERROR_DRIVER_LOAD;
    goto connect_fail;
  }

  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    return odbc_harvest_and_disconnect(db, C_ORM_SQL_HANDLE_DBC, hdbc, 1,
                                       "SQLDriverConnect",
                                       C_ORM_ERROR_CONNECTION);
  }

  *out_db = db;
  return C_ORM_OK;

connect_fail:
  odbc_conn_and_db_free(db);
  return rc;
}

C_ORM_EXPORT c_orm_error_t c_orm_odbc_connect(const char *url,
                                              c_orm_db_t **out_db) {
  const c_orm_odbc_api_t *api = NULL;
  char conn_str[2048];
  int login_timeout = 0;
  int autocommit = 1;
  int prefer_wide = 0;
  c_orm_error_t rc;

  if (!url || !out_db) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  *out_db = NULL;

  rc = c_orm_odbc_build_conn_string(url, conn_str, sizeof(conn_str),
                                    &login_timeout, &autocommit);
  if (rc != C_ORM_OK) {
    return rc;
  }

  rc = c_orm_odbc_load_driver_manager(&api);
  if (rc != C_ORM_OK) {
    return rc;
  }

#if defined(UNICODE) || defined(_UNICODE)
  prefer_wide = (api->SQLDriverConnectW != NULL) ? 1 : 0;
#else
  prefer_wide = 0;
#endif

  return c_orm_odbc_connect_raw(api, conn_str, "odbc", &g_odbc_vtable,
                                login_timeout, autocommit, prefer_wide, out_db);
}

C_ORM_EXPORT c_orm_error_t
c_orm_odbc_set_isolation(c_orm_db_t *db, unsigned long txn_isolation_mask) {
  c_orm_odbc_conn_t *conn = NULL;
  c_orm_sqlreturn_t sret;
  c_orm_error_t rc;

  rc = extract_odbc_conn(db, &conn);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (txn_isolation_mask != C_ORM_SQL_TXN_READ_UNCOMMITTED &&
      txn_isolation_mask != C_ORM_SQL_TXN_READ_COMMITTED &&
      txn_isolation_mask != C_ORM_SQL_TXN_REPEATABLE_READ &&
      txn_isolation_mask != C_ORM_SQL_TXN_SERIALIZABLE) {
    return C_ORM_ERROR_VALIDATION;
  }
  if (!conn->api->SQLSetConnectAttr) {
    return C_ORM_ERROR_NOT_IMPLEMENTED;
  }
  sret = conn->api->SQLSetConnectAttr(
      conn->hdbc, C_ORM_SQL_ATTR_TXN_ISOLATION,
      (c_orm_sqlpointer_t)(size_t)txn_isolation_mask, C_ORM_SQL_IS_UINTEGER);
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    return c_orm_odbc_harvest_diag(db, C_ORM_SQL_HANDLE_DBC, conn->hdbc,
                                   "SQLSetConnectAttr(TXN_ISOLATION)",
                                   C_ORM_ERROR_ODBC_DIAG);
  }
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_odbc_begin_transaction(c_orm_db_t *db) {
  c_orm_odbc_conn_t *conn = NULL;
  c_orm_sqlreturn_t sret;
  c_orm_error_t rc;

  rc = extract_odbc_conn(db, &conn);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (!conn->api->SQLSetConnectAttr) {
    return C_ORM_ERROR_NOT_IMPLEMENTED;
  }
  sret = conn->api->SQLSetConnectAttr(
      conn->hdbc, C_ORM_SQL_ATTR_AUTOCOMMIT,
      (c_orm_sqlpointer_t)(size_t)C_ORM_SQL_AUTOCOMMIT_OFF,
      C_ORM_SQL_IS_UINTEGER);
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    return c_orm_odbc_harvest_diag(db, C_ORM_SQL_HANDLE_DBC, conn->hdbc,
                                   "SQLSetConnectAttr(AUTOCOMMIT_OFF)",
                                   C_ORM_ERROR_ODBC_DIAG);
  }
  conn->in_tx = 1;
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_odbc_commit(c_orm_db_t *db) {
  c_orm_odbc_conn_t *conn = NULL;
  c_orm_sqlreturn_t sret;
  c_orm_error_t rc;

  rc = extract_odbc_conn(db, &conn);
  if (rc != C_ORM_OK) {
    return rc;
  }
  sret =
      conn->api->SQLEndTran(C_ORM_SQL_HANDLE_DBC, conn->hdbc, C_ORM_SQL_COMMIT);
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    return c_orm_odbc_harvest_diag(db, C_ORM_SQL_HANDLE_DBC, conn->hdbc,
                                   "SQLEndTran(COMMIT)", C_ORM_ERROR_ODBC_DIAG);
  }
  conn->in_tx = 0;
  if (conn->base_autocommit && conn->api->SQLSetConnectAttr) {
    conn->api->SQLSetConnectAttr(
        conn->hdbc, C_ORM_SQL_ATTR_AUTOCOMMIT,
        (c_orm_sqlpointer_t)(size_t)C_ORM_SQL_AUTOCOMMIT_ON,
        C_ORM_SQL_IS_UINTEGER);
  }
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_odbc_rollback(c_orm_db_t *db) {
  c_orm_odbc_conn_t *conn = NULL;
  c_orm_sqlreturn_t sret;
  c_orm_error_t rc;

  rc = extract_odbc_conn(db, &conn);
  if (rc != C_ORM_OK) {
    return rc;
  }
  sret = conn->api->SQLEndTran(C_ORM_SQL_HANDLE_DBC, conn->hdbc,
                               C_ORM_SQL_ROLLBACK);
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    return c_orm_odbc_harvest_diag(db, C_ORM_SQL_HANDLE_DBC, conn->hdbc,
                                   "SQLEndTran(ROLLBACK)",
                                   C_ORM_ERROR_ODBC_DIAG);
  }
  conn->in_tx = 0;
  if (conn->base_autocommit && conn->api->SQLSetConnectAttr) {
    conn->api->SQLSetConnectAttr(
        conn->hdbc, C_ORM_SQL_ATTR_AUTOCOMMIT,
        (c_orm_sqlpointer_t)(size_t)C_ORM_SQL_AUTOCOMMIT_ON,
        C_ORM_SQL_IS_UINTEGER);
  }
  return C_ORM_OK;
}

/**
 * @brief Internal infallible helper to release string list allocations.
 * @param tables Array of strings.
 * @param count Number of strings.
 * @return C_ORM_OK.
 */
static c_orm_error_t odbc_string_list_free(char **tables, size_t count) {
  size_t i;
  if (!tables) {
    return C_ORM_OK;
  }
  for (i = 0; i < count; i++) {
    c_orm_system_free(tables[i]);
  }
  c_orm_system_free(tables);
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_odbc_free_string_list(char **tables,
                                                       size_t count) {
  if (!tables) {
    return C_ORM_OK;
  }
  {
    size_t i;
    for (i = 0; i < count; i++) {
      c_orm_system_free(tables[i]);
    }
  }
  return c_orm_system_free(tables);
}

C_ORM_EXPORT c_orm_error_t c_orm_odbc_list_tables(c_orm_db_t *db,
                                                  const char *schema_pattern,
                                                  char ***out_tables,
                                                  size_t *out_count) {
  c_orm_odbc_conn_t *conn = NULL;
  c_orm_sqlhandle_t hstmt = C_ORM_SQL_NULL_HANDLE;
  c_orm_sqlreturn_t sret;
  char **list = NULL;
  size_t count = 0;
  size_t cap = 0;
  c_orm_error_t rc;

  if (!out_tables || !out_count) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  *out_tables = NULL;
  *out_count = 0;

  rc = extract_odbc_conn(db, &conn);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (!conn->api->SQLTables) {
    return C_ORM_ERROR_NOT_IMPLEMENTED;
  }

  sret = conn->api->SQLAllocHandle(C_ORM_SQL_HANDLE_STMT, conn->hdbc, &hstmt);
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    return c_orm_odbc_harvest_diag(db, C_ORM_SQL_HANDLE_DBC, conn->hdbc,
                                   "SQLAllocHandle(SQLTables)",
                                   C_ORM_ERROR_ODBC_DIAG);
  }

  {
    c_orm_sqlchar_t table_type_buf[8];
    char *schema_mut = NULL;
    memcpy(table_type_buf, "TABLE", 6);
    if (schema_pattern) {
      rc = c_orm_strdup(schema_pattern, &schema_mut);
      if (rc != C_ORM_OK) {
        conn->api->SQLFreeHandle(C_ORM_SQL_HANDLE_STMT, hstmt);
        return rc;
      }
    }
    sret = conn->api->SQLTables(hstmt, NULL, 0, (c_orm_sqlchar_t *)schema_mut,
                                schema_mut ? C_ORM_SQL_NTS : 0, NULL, 0,
                                table_type_buf, C_ORM_SQL_NTS);
    if (schema_mut) {
      c_orm_system_free(schema_mut);
    }
  }
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    return odbc_harvest_and_free_stmt(db, hstmt, "SQLTables",
                                      C_ORM_ERROR_ODBC_DIAG);
  }

  for (;;) {
    char tname[256];
    c_orm_sqllen_t ind = 0;
    sret = conn->api->SQLFetch(hstmt);
    if (sret == C_ORM_SQL_NO_DATA) {
      break;
    }
    if (!C_ORM_SQL_SUCCEEDED(sret)) {
      odbc_string_list_free(list, count);
      return odbc_harvest_and_free_stmt(db, hstmt, "SQLFetch(SQLTables)",
                                        C_ORM_ERROR_ODBC_DIAG);
    }
    memset(tname, 0, sizeof(tname));
    /* ODBC SQLTables column 3 is TABLE_NAME */
    sret = conn->api->SQLGetData(hstmt, 3, C_ORM_SQL_C_CHAR, tname,
                                 (c_orm_sqllen_t)sizeof(tname), &ind);
    if (C_ORM_SQL_SUCCEEDED(sret) && ind != C_ORM_SQL_NULL_DATA &&
        tname[0] != '\0') {
      char *dup = NULL;
      if (count >= cap) {
        size_t new_cap = cap == 0 ? 8 : (cap * 2);
        char **grown = NULL;
        rc = c_orm_system_realloc(list, new_cap * sizeof(char *),
                                  (void **)&grown);
        if (rc != C_ORM_OK) {
          goto tables_err;
        }
        list = grown;
        cap = new_cap;
      }
      rc = c_orm_strdup(tname, &dup);
      if (rc != C_ORM_OK) {
        goto tables_err;
      }
      list[count++] = dup;
    }
  }

  conn->api->SQLFreeHandle(C_ORM_SQL_HANDLE_STMT, hstmt);
  *out_tables = list;
  *out_count = count;
  return C_ORM_OK;

tables_err:
  conn->api->SQLFreeHandle(C_ORM_SQL_HANDLE_STMT, hstmt);
  odbc_string_list_free(list, count);
  return rc;
}

/**
 * @brief Internal infallible helper to release column list allocations.
 * @param cols Array of column descriptors.
 * @param count Number of descriptors.
 * @return C_ORM_OK.
 */
static c_orm_error_t odbc_column_list_free(c_orm_odbc_column_info_t *cols,
                                           size_t count) {
  size_t i;
  if (!cols) {
    return C_ORM_OK;
  }
  for (i = 0; i < count; ++i) {
    c_orm_system_free(cols[i].name);
    c_orm_system_free(cols[i].type_name);
  }
  c_orm_system_free(cols);
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t
c_orm_odbc_free_column_list(c_orm_odbc_column_info_t *cols, size_t count) {
  return odbc_column_list_free(cols, count);
}

C_ORM_EXPORT c_orm_error_t c_orm_odbc_list_columns(
    c_orm_db_t *db, const char *schema_name, const char *table_name,
    c_orm_odbc_column_info_t **out_cols, size_t *out_count) {
  c_orm_odbc_conn_t *conn = NULL;
  c_orm_sqlhandle_t hstmt = C_ORM_SQL_NULL_HANDLE;
  c_orm_sqlreturn_t sret;
  c_orm_odbc_column_info_t *list = NULL;
  size_t count = 0;
  size_t cap = 0;
  c_orm_error_t rc;

  if (!db || !table_name || !out_cols || !out_count) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (table_name[0] == '\0') {
    return C_ORM_ERROR_VALIDATION;
  }
  *out_cols = NULL;
  *out_count = 0;

  rc = extract_odbc_conn(db, &conn);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (!conn->api->SQLColumns) {
    return C_ORM_ERROR_NOT_IMPLEMENTED;
  }

  sret = conn->api->SQLAllocHandle(C_ORM_SQL_HANDLE_STMT, conn->hdbc, &hstmt);
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    return c_orm_odbc_harvest_diag(db, C_ORM_SQL_HANDLE_DBC, conn->hdbc,
                                   "SQLAllocHandle(SQLColumns)",
                                   C_ORM_ERROR_SQL);
  }

  sret = conn->api->SQLColumns(
      hstmt, NULL, 0,
      schema_name ? (c_orm_sqlchar_t *)(size_t)schema_name : NULL,
      (c_orm_sqlsmallint_t)(schema_name ? C_ORM_SQL_NTS : 0),
      (c_orm_sqlchar_t *)(size_t)table_name, C_ORM_SQL_NTS, NULL, 0);
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    return odbc_harvest_and_free_stmt(db, hstmt, "SQLColumns", C_ORM_ERROR_SQL);
  }

  for (;;) {
    char cname[256];
    char tname[128];
    char nul_str[16];
    c_orm_sqlinteger_t col_sz = 0;
    c_orm_sqlsmallint_t dec_dig = 0;
    c_orm_sqllen_t ind = 0;

    sret = conn->api->SQLFetch(hstmt);
    if (sret == C_ORM_SQL_NO_DATA) {
      break;
    }
    if (!C_ORM_SQL_SUCCEEDED(sret)) {
      odbc_column_list_free(list, count);
      return odbc_harvest_and_free_stmt(db, hstmt, "SQLFetch(SQLColumns)",
                                        C_ORM_ERROR_STEP);
    }

    cname[0] = '\0';
    tname[0] = '\0';
    nul_str[0] = '\0';
    sret = conn->api->SQLGetData(hstmt, 4, C_ORM_SQL_C_CHAR, cname,
                                 (c_orm_sqllen_t)sizeof(cname), &ind);
    if (!C_ORM_SQL_SUCCEEDED(sret) || ind == C_ORM_SQL_NULL_DATA ||
        cname[0] == '\0') {
      continue;
    }
    conn->api->SQLGetData(hstmt, 6, C_ORM_SQL_C_CHAR, tname,
                          (c_orm_sqllen_t)sizeof(tname), &ind);
    if (ind == C_ORM_SQL_NULL_DATA || tname[0] == '\0') {
      rc = C_ORM_STRCPY(tname, sizeof(tname), "VARCHAR");
      if (rc != C_ORM_OK) {
        goto cols_err;
      }
    }
    ind = 0;
    conn->api->SQLGetData(hstmt, 7, C_ORM_SQL_C_SLONG, &col_sz,
                          (c_orm_sqllen_t)sizeof(col_sz), &ind);
    if (ind == C_ORM_SQL_NULL_DATA) {
      col_sz = 0;
    }
    ind = 0;
    conn->api->SQLGetData(hstmt, 9, C_ORM_SQL_C_SSHORT, &dec_dig,
                          (c_orm_sqllen_t)sizeof(dec_dig), &ind);
    if (ind == C_ORM_SQL_NULL_DATA) {
      dec_dig = 0;
    }
    ind = 0;
    conn->api->SQLGetData(hstmt, 18, C_ORM_SQL_C_CHAR, nul_str,
                          (c_orm_sqllen_t)sizeof(nul_str), &ind);

    if (count >= cap) {
      size_t new_cap = cap == 0 ? 8 : cap * 2;
      c_orm_odbc_column_info_t *grown = NULL;
      rc = c_orm_system_realloc(
          list, new_cap * sizeof(c_orm_odbc_column_info_t), (void **)&grown);
      if (rc != C_ORM_OK) {
        goto cols_err;
      }
      list = grown;
      cap = new_cap;
    }

    memset(&list[count], 0, sizeof(c_orm_odbc_column_info_t));
    rc = c_orm_strdup(cname, &list[count].name);
    if (rc != C_ORM_OK) {
      goto cols_err;
    }
    rc = c_orm_strdup(tname, &list[count].type_name);
    if (rc != C_ORM_OK) {
      c_orm_system_free(list[count].name);
      goto cols_err;
    }
    list[count].column_size = (int)col_sz;
    list[count].decimal_digits = (int)dec_dig;
    list[count].is_nullable = (strcmp(nul_str, "NO") == 0) ? 0 : 1;
    count++;
  }

  conn->api->SQLFreeHandle(C_ORM_SQL_HANDLE_STMT, hstmt);
  *out_cols = list;
  *out_count = count;
  return C_ORM_OK;

cols_err:
  conn->api->SQLFreeHandle(C_ORM_SQL_HANDLE_STMT, hstmt);
  odbc_column_list_free(list, count);
  return rc;
}

/**
 * @brief Internal infallible helper to release columnar batch arrays.
 * @param col_count Number of columns.
 * @param col_bufs Column value buffers.
 * @param col_inds Column indicator arrays.
 * @param status_arr Parameter status array.
 * @return C_ORM_OK.
 */
static c_orm_error_t odbc_batch_buffers_free(size_t col_count, void **col_bufs,
                                             c_orm_sqllen_t **col_inds,
                                             c_orm_sqlusmallint_t *status_arr) {
  size_t c;
  if (col_bufs) {
    for (c = 0; c < col_count; c++) {
      if (col_bufs[c]) {
        c_orm_system_free(col_bufs[c]);
      }
    }
    c_orm_system_free(col_bufs);
  }
  if (col_inds) {
    for (c = 0; c < col_count; c++) {
      if (col_inds[c]) {
        c_orm_system_free(col_inds[c]);
      }
    }
    c_orm_system_free(col_inds);
  }
  if (status_arr) {
    c_orm_system_free(status_arr);
  }
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_odbc_execute_batch(
    c_orm_db_t *db, const char *sql, size_t row_count, size_t col_count,
    const c_orm_type_t *col_types, const void *const *col_arrays,
    size_t *out_rows_processed) {
  c_orm_odbc_conn_t *conn = NULL;
  c_orm_sqlhandle_t hstmt = C_ORM_SQL_NULL_HANDLE;
  c_orm_sqlreturn_t sret;
  c_orm_sqlulen_t processed = 0;
  c_orm_sqlusmallint_t *status_arr = NULL;
  void **col_bufs = NULL;
  c_orm_sqllen_t **col_inds = NULL;
  size_t c;
  size_t r;
  c_orm_error_t rc = C_ORM_OK;

  if (!db || !sql || !col_types || !col_arrays) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (row_count == 0 || col_count == 0) {
    return C_ORM_ERROR_VALIDATION;
  }
  if (out_rows_processed) {
    *out_rows_processed = 0;
  }

  rc = extract_odbc_conn(db, &conn);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (!conn->api->SQLSetStmtAttr) {
    return C_ORM_ERROR_NOT_IMPLEMENTED;
  }

  sret = conn->api->SQLAllocHandle(C_ORM_SQL_HANDLE_STMT, conn->hdbc, &hstmt);
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    return c_orm_odbc_harvest_diag(db, C_ORM_SQL_HANDLE_DBC, conn->hdbc,
                                   "SQLAllocHandle(batch)", C_ORM_ERROR_SQL);
  }

  rc = c_orm_system_calloc(row_count, sizeof(c_orm_sqlusmallint_t),
                           (void **)&status_arr);
  if (rc != C_ORM_OK) {
    goto cleanup;
  }
  rc = c_orm_system_calloc(col_count, sizeof(void *), (void **)&col_bufs);
  if (rc != C_ORM_OK) {
    goto cleanup;
  }
  rc = c_orm_system_calloc(col_count, sizeof(c_orm_sqllen_t *),
                           (void **)&col_inds);
  if (rc != C_ORM_OK) {
    goto cleanup;
  }

  {
    char *sql_mut = NULL;
    rc = c_orm_strdup(sql, &sql_mut);
    if (rc != C_ORM_OK) {
      goto cleanup;
    }
    sret =
        conn->api->SQLPrepare(hstmt, (c_orm_sqlchar_t *)sql_mut, C_ORM_SQL_NTS);
    c_orm_system_free(sql_mut);
  }
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    odbc_batch_buffers_free(col_count, col_bufs, col_inds, status_arr);
    return odbc_harvest_and_free_stmt(db, hstmt, "SQLPrepare(batch)",
                                      C_ORM_ERROR_SQL);
  }

  sret = conn->api->SQLSetStmtAttr(hstmt, C_ORM_SQL_ATTR_PARAMSET_SIZE,
                                   (c_orm_sqlpointer_t)row_count,
                                   C_ORM_SQL_IS_UINTEGER);
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    odbc_batch_buffers_free(col_count, col_bufs, col_inds, status_arr);
    return odbc_harvest_and_free_stmt(
        db, hstmt, "SQLSetStmtAttr(PARAMSET_SIZE)", C_ORM_ERROR_BIND);
  }
  conn->api->SQLSetStmtAttr(hstmt, C_ORM_SQL_ATTR_PARAMS_PROCESSED_PTR,
                            (c_orm_sqlpointer_t)&processed,
                            C_ORM_SQL_IS_POINTER);
  conn->api->SQLSetStmtAttr(hstmt, C_ORM_SQL_ATTR_PARAM_STATUS_PTR,
                            (c_orm_sqlpointer_t)status_arr,
                            C_ORM_SQL_IS_POINTER);

  for (c = 0; c < col_count; c++) {
    if (!col_arrays[c]) {
      rc = C_ORM_ERROR_NULL_POINTER;
      goto cleanup;
    }
    rc = c_orm_system_calloc(row_count, sizeof(c_orm_sqllen_t),
                             (void **)&col_inds[c]);
    if (rc != C_ORM_OK) {
      goto cleanup;
    }

    if (col_types[c] == C_ORM_TYPE_INT32) {
      rc = c_orm_system_malloc(row_count * sizeof(int32_t), &col_bufs[c]);
      if (rc != C_ORM_OK) {
        goto cleanup;
      }
      memcpy(col_bufs[c], col_arrays[c], row_count * sizeof(int32_t));
      for (r = 0; r < row_count; r++) {
        col_inds[c][r] = (c_orm_sqllen_t)sizeof(int32_t);
      }
      sret = conn->api->SQLBindParameter(
          hstmt, (c_orm_sqlusmallint_t)(c + 1), C_ORM_SQL_PARAM_INPUT,
          C_ORM_SQL_C_SLONG, C_ORM_SQL_INTEGER, 10, 0, col_bufs[c],
          (c_orm_sqllen_t)sizeof(int32_t), col_inds[c]);
    } else if (col_types[c] == C_ORM_TYPE_INT64) {
      rc = c_orm_system_malloc(row_count * sizeof(int64_t), &col_bufs[c]);
      if (rc != C_ORM_OK) {
        goto cleanup;
      }
      memcpy(col_bufs[c], col_arrays[c], row_count * sizeof(int64_t));
      for (r = 0; r < row_count; r++) {
        col_inds[c][r] = (c_orm_sqllen_t)sizeof(int64_t);
      }
      sret = conn->api->SQLBindParameter(
          hstmt, (c_orm_sqlusmallint_t)(c + 1), C_ORM_SQL_PARAM_INPUT,
          C_ORM_SQL_C_SBIGINT, C_ORM_SQL_BIGINT, 19, 0, col_bufs[c],
          (c_orm_sqllen_t)sizeof(int64_t), col_inds[c]);
    } else if (col_types[c] == C_ORM_TYPE_DOUBLE) {
      rc = c_orm_system_malloc(row_count * sizeof(double), &col_bufs[c]);
      if (rc != C_ORM_OK) {
        goto cleanup;
      }
      memcpy(col_bufs[c], col_arrays[c], row_count * sizeof(double));
      for (r = 0; r < row_count; r++) {
        col_inds[c][r] = (c_orm_sqllen_t)sizeof(double);
      }
      sret = conn->api->SQLBindParameter(
          hstmt, (c_orm_sqlusmallint_t)(c + 1), C_ORM_SQL_PARAM_INPUT,
          C_ORM_SQL_C_DOUBLE, C_ORM_SQL_DOUBLE, 15, 0, col_bufs[c],
          (c_orm_sqllen_t)sizeof(double), col_inds[c]);
    } else if (col_types[c] == C_ORM_TYPE_STRING) {
      const char *const *strs = (const char *const *)col_arrays[c];
      size_t max_len = 1;
      size_t stride;
      char *flat = NULL;
      for (r = 0; r < row_count; r++) {
        if (strs[r]) {
          size_t sl = strlen(strs[r]);
          if (sl > max_len) {
            max_len = sl;
          }
        }
      }
      stride = max_len + 1;
      rc = c_orm_system_calloc(row_count, stride, (void **)&flat);
      if (rc != C_ORM_OK) {
        goto cleanup;
      }
      col_bufs[c] = flat;
      for (r = 0; r < row_count; r++) {
        if (!strs[r]) {
          col_inds[c][r] = C_ORM_SQL_NULL_DATA;
        } else {
          size_t sl = strlen(strs[r]);
          memcpy(flat + r * stride, strs[r], sl + 1);
          col_inds[c][r] = (c_orm_sqllen_t)sl;
        }
      }
      sret = conn->api->SQLBindParameter(
          hstmt, (c_orm_sqlusmallint_t)(c + 1), C_ORM_SQL_PARAM_INPUT,
          C_ORM_SQL_C_CHAR, C_ORM_SQL_VARCHAR, (c_orm_sqlulen_t)max_len, 0,
          flat, (c_orm_sqllen_t)stride, col_inds[c]);
    } else if (col_types[c] == C_ORM_TYPE_DECIMAL) {
      const c_orm_decimal_t *decs = (const c_orm_decimal_t *)col_arrays[c];
      char *flat = NULL;
      size_t stride = 48;
      rc = c_orm_system_calloc(row_count, stride, (void **)&flat);
      if (rc != C_ORM_OK) {
        goto cleanup;
      }
      col_bufs[c] = flat;
      for (r = 0; r < row_count; r++) {
        size_t sl = 0;
        rc = c_orm_decimal_to_str(&decs[r], flat + r * stride, stride, &sl);
        if (rc != C_ORM_OK) {
          goto cleanup;
        }
        col_inds[c][r] = (c_orm_sqllen_t)sl;
      }
      sret = conn->api->SQLBindParameter(
          hstmt, (c_orm_sqlusmallint_t)(c + 1), C_ORM_SQL_PARAM_INPUT,
          C_ORM_SQL_C_CHAR, C_ORM_SQL_DECIMAL, 38,
          (c_orm_sqlsmallint_t)decs[0].scale, flat, (c_orm_sqllen_t)stride,
          col_inds[c]);
    } else {
      rc = C_ORM_ERROR_VALIDATION;
      goto cleanup;
    }

    if (!C_ORM_SQL_SUCCEEDED(sret)) {
      odbc_batch_buffers_free(col_count, col_bufs, col_inds, status_arr);
      return odbc_harvest_and_free_stmt(db, hstmt, "SQLBindParameter(batch)",
                                        C_ORM_ERROR_BIND);
    }
  }

  sret = conn->api->SQLExecute(hstmt);
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    odbc_batch_buffers_free(col_count, col_bufs, col_inds, status_arr);
    return odbc_harvest_and_free_stmt(db, hstmt, "SQLExecute(batch)",
                                      C_ORM_ERROR_STEP);
  }

  if (out_rows_processed) {
    *out_rows_processed = processed > 0 ? (size_t)processed : row_count;
  }

cleanup:
  odbc_batch_buffers_free(col_count, col_bufs, col_inds, status_arr);
  conn->api->SQLFreeHandle(C_ORM_SQL_HANDLE_STMT, hstmt);
  return rc;
}
