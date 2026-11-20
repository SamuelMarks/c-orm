/**
 * @file c_orm_db2.c
 * @brief First-class IBM Db2 (LUW, Db2 for z/OS, Db2 for i) CLI/ODBC driver
 * implementation for c-orm.
 */

/* clang-format off */
#include "c_orm_db2.h"
#include "c_orm_api.h"
#include "c_orm_decimal.h"
#include "c_orm_ebcdic.h"
#include "c_orm_log.h"
#include "c_orm_safe_crt.h"
#include "c_orm_string_builder.h"
#include <stddef.h>
#include <string.h>
/* clang-format on */

static c_orm_driver_vtable_t g_db2_vtable;
static int g_db2_vtable_init = 0;

/**
 * @brief Case-insensitive ASCII prefix match helper.
 * @param s Input string.
 * @param p Prefix string.
 * @param n Prefix length.
 * @param out_eq Output set to 1 if matching, 0 otherwise.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t db2_istarts(const char *s, const char *p, size_t n,
                                 int *out_eq) {
  size_t i;
  for (i = 0; i < n; i++) {
    char a = s[i];
    char b = p[i];
    if (a == '\0') {
      *out_eq = 0;
      return C_ORM_OK;
    }
    if (a >= 'A' && a <= 'Z') {
      a = (char)(a + ('a' - 'A'));
    }
    if (b >= 'A' && b <= 'Z') {
      b = (char)(b + ('a' - 'A'));
    }
    if (a != b) {
      *out_eq = 0;
      return C_ORM_OK;
    }
  }
  *out_eq = 1;
  return C_ORM_OK;
}

/**
 * @brief Case-insensitive exact ASCII match helper.
 * @param a First string.
 * @param b Second string.
 * @param out_eq Output set to 1 if equal, 0 otherwise.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t db2_ieq(const char *a, const char *b, int *out_eq) {
  size_t la = strlen(a);
  size_t lb = strlen(b);
  if (la != lb) {
    *out_eq = 0;
    return C_ORM_OK;
  }
  return db2_istarts(a, b, la, out_eq);
}

/**
 * @brief Appends `KEY=<escaped>;` to a Db2 CLI connection string buffer.
 * @param dst Destination buffer.
 * @param cap Buffer capacity.
 * @param io_pos Current write offset.
 * @param key Keyword name.
 * @param val Unescaped value.
 * @return C_ORM_OK on success, or validation/overflow error.
 */
static c_orm_error_t db2_append_kv(char *dst, size_t cap, size_t *io_pos,
                                   const char *key, const char *val) {
  char esc[512];
  size_t klen = strlen(key);
  size_t vlen = 0;
  c_orm_error_t rc;

  rc = c_orm_odbc_escape_conn_value(val, esc, sizeof(esc), &vlen);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (*io_pos + klen + 1 + vlen + 2 > cap) {
    return C_ORM_ERROR_OVERFLOW;
  }
  memcpy(dst + *io_pos, key, klen);
  *io_pos += klen;
  dst[(*io_pos)++] = '=';
  memcpy(dst + *io_pos, esc, vlen);
  *io_pos += vlen;
  dst[(*io_pos)++] = ';';
  dst[*io_pos] = '\0';
  return C_ORM_OK;
}

/**
 * @brief Parses a Db2 isolation token (`UR`, `CS`, `RS`, `RR`) into enum.
 * @param s String value.
 * @param out_iso Output `c_orm_db2_isolation_t`.
 * @return C_ORM_OK on valid token, or C_ORM_ERROR_VALIDATION.
 */
static c_orm_error_t parse_db2_isolation(const char *s,
                                         c_orm_db2_isolation_t *out_iso) {
  int eq = 0;
  db2_ieq(s, "UR", &eq);
  if (eq) {
    *out_iso = C_ORM_DB2_ISOLATION_UR;
    return C_ORM_OK;
  }
  db2_ieq(s, "CS", &eq);
  if (eq) {
    *out_iso = C_ORM_DB2_ISOLATION_CS;
    return C_ORM_OK;
  }
  db2_ieq(s, "RS", &eq);
  if (eq) {
    *out_iso = C_ORM_DB2_ISOLATION_RS;
    return C_ORM_OK;
  }
  db2_ieq(s, "RR", &eq);
  if (eq) {
    *out_iso = C_ORM_DB2_ISOLATION_RR;
    return C_ORM_OK;
  }
  return C_ORM_ERROR_VALIDATION;
}

/**
 * @brief Parses boolean query parameter (`1`/`true`/`yes`/`on` vs
 * `0`/`false`/`no`/`off`).
 * @param s String value.
 * @param out_flag Output 1 or 0.
 * @return C_ORM_OK on valid boolean, or C_ORM_ERROR_VALIDATION.
 */
static c_orm_error_t db2_parse_bool(const char *s, int *out_flag) {
  static const char *const trues[4] = {"1", "true", "yes", "on"};
  static const char *const falses[4] = {"0", "false", "no", "off"};
  size_t i;
  int eq = 0;
  for (i = 0; i < 4U; ++i) {
    (void)db2_ieq(s, trues[i], &eq);
    if (eq) {
      *out_flag = 1;
      return C_ORM_OK;
    }
    (void)db2_ieq(s, falses[i], &eq);
    if (eq) {
      *out_flag = 0;
      return C_ORM_OK;
    }
  }
  return C_ORM_ERROR_VALIDATION;
}

C_ORM_EXPORT c_orm_error_t c_orm_db2_build_conn_string(
    const char *url, int is_direct_cli, char *out_conn_str, size_t buf_size,
    c_orm_db2_url_opts_t *out_opts) {
  const char *body;
  int has_scheme = 0;
  int is_raw_kv = 0;
  size_t pos = 0;
  c_orm_db2_url_opts_t opts;
  c_orm_error_t rc;

  if (!url || !out_conn_str) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (buf_size < 2 || url[0] == '\0') {
    return buf_size < 2 ? C_ORM_ERROR_OVERFLOW : C_ORM_ERROR_VALIDATION;
  }

  memset(&opts, 0, sizeof(opts));
  opts.autocommit = 1;
  out_conn_str[0] = '\0';

  (void)db2_istarts(url, "db2://", 6, &has_scheme);
  body = has_scheme ? (url + 6) : url;
  if (body[0] == '\0') {
    return C_ORM_ERROR_VALIDATION;
  }

  (void)db2_istarts(body, "DATABASE=", 9, &is_raw_kv);
  if (!is_raw_kv) {
    (void)db2_istarts(body, "DSN=", 4, &is_raw_kv);
  }
  if (!is_raw_kv) {
    (void)db2_istarts(body, "DRIVER=", 7, &is_raw_kv);
  }
  if (is_raw_kv || (!has_scheme && strchr(body, '=') != NULL)) {
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
    if (out_opts) {
      *out_opts = opts;
    }
    return C_ORM_OK;
  }

  if (!has_scheme) {
    return C_ORM_ERROR_VALIDATION;
  }

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
    char driver_override[256];
    char extra_kv[1024];
    size_t extra_pos = 0;
    int has_user = 0;
    int has_pass = 0;
    int has_port = 0;
    int has_db = 0;
    int use_ssl = 0;

    user_dec[0] = '\0';
    pass_dec[0] = '\0';
    host_dec[0] = '\0';
    port_dec[0] = '\0';
    db_dec[0] = '\0';
    driver_override[0] = '\0';
    extra_kv[0] = '\0';

    for (i = 0; i < auth_path_len; i++) {
      if (body[i] == '/') {
        slash = body + i;
        break;
      }
    }
    {
      size_t auth_len = slash ? (size_t)(slash - body) : auth_path_len;
      for (i = 0; i < auth_len; i++) {
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
        host_port_len = auth_len - (size_t)((at + 1) - body);
      } else {
        host_start = body;
        host_port_len = auth_len;
      }
    }

    if (host_port_len > 0) {
      const char *port_colon = NULL;
      for (i = 0; i < host_port_len; i++) {
        if (host_start[i] == ':') {
          port_colon = host_start + i;
          break;
        }
      }
      if (port_colon) {
        size_t plen = host_port_len - (size_t)((port_colon + 1) - host_start);
        int64_t pnum = 0;
        const char *endp = NULL;
        rc = c_orm_odbc_percent_decode(host_start,
                                       (size_t)(port_colon - host_start), 0,
                                       host_dec, sizeof(host_dec), NULL);
        if (rc != C_ORM_OK) {
          return rc;
        }
        if (plen == 0) {
          return C_ORM_ERROR_VALIDATION;
        }
        rc = c_orm_odbc_percent_decode(port_colon + 1, plen, 0, port_dec,
                                       sizeof(port_dec), NULL);
        if (rc != C_ORM_OK) {
          return rc;
        }
        if (c_orm_strtoint64(port_dec, &endp, 10, &pnum) != C_ORM_OK ||
            *endp != '\0' || pnum < 1 || pnum > 65535) {
          return C_ORM_ERROR_VALIDATION;
        }
        has_port = 1;
      } else {
        rc = c_orm_odbc_percent_decode(host_start, host_port_len, 0, host_dec,
                                       sizeof(host_dec), NULL);
        if (rc != C_ORM_OK) {
          return rc;
        }
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

    if (qmark && qmark[1] != '\0') {
      const char *cur = qmark + 1;
      while (*cur != '\0') {
        const char *amp = strchr(cur, '&');
        size_t pair_len = amp ? (size_t)(amp - cur) : strlen(cur);
        const char *eqp = NULL;
        char qkey[128];
        char qval[512];
        int m1 = 0;
        int m2 = 0;

        for (i = 0; i < pair_len; i++) {
          if (cur[i] == '=') {
            eqp = cur + i;
            break;
          }
        }
        if (!eqp || eqp == cur) {
          return C_ORM_ERROR_VALIDATION;
        }
        rc = c_orm_odbc_percent_decode(cur, (size_t)(eqp - cur), 1, qkey,
                                       sizeof(qkey), NULL);
        if (rc != C_ORM_OK) {
          return rc;
        }
        rc = c_orm_odbc_percent_decode(eqp + 1,
                                       pair_len - (size_t)((eqp + 1) - cur), 1,
                                       qval, sizeof(qval), NULL);
        if (rc != C_ORM_OK) {
          return rc;
        }

        db2_ieq(qkey, "schema", &m1);
        db2_ieq(qkey, "currentschema", &m2);
        if (m1 || m2) {
          rc = C_ORM_STRCPY(opts.schema, sizeof(opts.schema), qval);
          if (rc != C_ORM_OK) {
            return rc;
          }
          rc = db2_append_kv(extra_kv, sizeof(extra_kv), &extra_pos,
                             "CurrentSchema", qval);
          if (rc != C_ORM_OK) {
            return rc;
          }
        } else {
          db2_ieq(qkey, "isolation", &m1);
          if (m1) {
            rc = parse_db2_isolation(qval, &opts.isolation);
            if (rc != C_ORM_OK) {
              return rc;
            }
            opts.has_isolation = 1;
          } else {
            db2_ieq(qkey, "ssl", &m1);
            if (m1) {
              rc = db2_parse_bool(qval, &use_ssl);
              if (rc != C_ORM_OK) {
                return rc;
              }
            } else {
              db2_ieq(qkey, "userid", &m1);
              db2_ieq(qkey, "client_userid", &m2);
              if (m1 || m2) {
                rc = C_ORM_STRCPY(opts.info_userid, sizeof(opts.info_userid),
                                  qval);
                if (rc != C_ORM_OK) {
                  return rc;
                }
              } else {
                db2_ieq(qkey, "wrkstn", &m1);
                db2_ieq(qkey, "wrkstnname", &m2);
                if (m1 || m2) {
                  rc = C_ORM_STRCPY(opts.info_wrkstn, sizeof(opts.info_wrkstn),
                                    qval);
                  if (rc != C_ORM_OK) {
                    return rc;
                  }
                } else {
                  db2_ieq(qkey, "applname", &m1);
                  if (m1) {
                    rc = C_ORM_STRCPY(opts.info_applname,
                                      sizeof(opts.info_applname), qval);
                    if (rc != C_ORM_OK) {
                      return rc;
                    }
                  } else {
                    db2_ieq(qkey, "acctstr", &m1);
                    if (m1) {
                      rc = C_ORM_STRCPY(opts.info_acctstr,
                                        sizeof(opts.info_acctstr), qval);
                      if (rc != C_ORM_OK) {
                        return rc;
                      }
                    } else {
                      db2_ieq(qkey, "uid", &m1);
                      if (m1) {
                        rc = C_ORM_STRCPY(user_dec, sizeof(user_dec), qval);
                        if (rc != C_ORM_OK) {
                          return rc;
                        }
                        has_user = 1;
                      } else {
                        db2_ieq(qkey, "pwd", &m1);
                        if (m1) {
                          rc = C_ORM_STRCPY(pass_dec, sizeof(pass_dec), qval);
                          if (rc != C_ORM_OK) {
                            return rc;
                          }
                          has_pass = 1;
                        } else {
                          db2_ieq(qkey, "timeout", &m1);
                          db2_ieq(qkey, "login_timeout", &m2);
                          if (m1 || m2) {
                            int64_t tv = 0;
                            const char *endp = NULL;
                            if (c_orm_strtoint64(qval, &endp, 10, &tv) !=
                                    C_ORM_OK ||
                                *endp != '\0' || tv < 0 || tv > 86400) {
                              return C_ORM_ERROR_VALIDATION;
                            }
                            opts.login_timeout = (int)tv;
                          } else {
                            db2_ieq(qkey, "autocommit", &m1);
                            if (m1) {
                              rc = db2_parse_bool(qval, &opts.autocommit);
                              if (rc != C_ORM_OK) {
                                return rc;
                              }
                            } else {
                              db2_ieq(qkey, "driver", &m1);
                              if (m1) {
                                rc =
                                    C_ORM_STRCPY(driver_override,
                                                 sizeof(driver_override), qval);
                                if (rc != C_ORM_OK) {
                                  return rc;
                                }
                              } else {
                                rc = db2_append_kv(extra_kv, sizeof(extra_kv),
                                                   &extra_pos, qkey, qval);
                                if (rc != C_ORM_OK) {
                                  return rc;
                                }
                              }
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }

        if (!amp) {
          break;
        }
        cur = amp + 1;
      }
    }

    if (has_db) {
      /* TCP/IP connection: db2://user:pass@host[:port]/SAMPLE */
      if (!is_direct_cli || driver_override[0] != '\0') {
        const char *drv = driver_override[0] != '\0' ? driver_override
                                                     : "{IBM DB2 ODBC DRIVER}";
        rc = db2_append_kv(out_conn_str, buf_size, &pos, "DRIVER", drv);
        if (rc != C_ORM_OK) {
          return rc;
        }
      }
      rc = db2_append_kv(out_conn_str, buf_size, &pos, "DATABASE", db_dec);
      if (rc != C_ORM_OK) {
        return rc;
      }
      if (host_dec[0] != '\0') {
        rc = db2_append_kv(out_conn_str, buf_size, &pos, "HOSTNAME", host_dec);
        if (rc != C_ORM_OK) {
          return rc;
        }
      }
      rc = db2_append_kv(out_conn_str, buf_size, &pos, "PORT",
                         has_port ? port_dec : "50000");
      if (rc != C_ORM_OK) {
        return rc;
      }
      rc = db2_append_kv(out_conn_str, buf_size, &pos, "PROTOCOL", "TCPIP");
      if (rc != C_ORM_OK) {
        return rc;
      }
    } else if (host_dec[0] != '\0' && !has_port) {
      /* Cataloged DSN / z/OS subsystem alias: db2://DB2Z_SUBSYS?uid=u&pwd=p */
      rc = db2_append_kv(out_conn_str, buf_size, &pos, "DSN", host_dec);
      if (rc != C_ORM_OK) {
        return rc;
      }
    } else {
      return C_ORM_ERROR_VALIDATION;
    }

    if (has_user && user_dec[0] != '\0') {
      rc = db2_append_kv(out_conn_str, buf_size, &pos, "UID", user_dec);
      if (rc != C_ORM_OK) {
        return rc;
      }
    }
    if (has_pass) {
      rc = db2_append_kv(out_conn_str, buf_size, &pos, "PWD", pass_dec);
      if (rc != C_ORM_OK) {
        return rc;
      }
    }
    if (use_ssl) {
      rc = db2_append_kv(out_conn_str, buf_size, &pos, "Security", "SSL");
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
  }

  if (out_opts) {
    *out_opts = opts;
  }
  return C_ORM_OK;
}

/**
 * @brief Executes a parameterless DDL/DCL command directly on `db`.
 * @param db Active Db2 connection.
 * @param sql SQL statement text.
 * @return C_ORM_OK on success, or mapped diagnostic error.
 */
static c_orm_error_t db2_exec_simple(c_orm_db_t *db, const char *sql) {
  c_orm_query_t *q = NULL;
  int has_row = 0;
  c_orm_error_t rc;

  rc = db->vtable->prepare(db, sql, &q);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = db->vtable->step(q, &has_row);
  db->vtable->finalize(q);
  return rc;
}

/**
 * @brief Sets a single Db2 CLI string connection attribute (`SQL_ATTR_INFO_*`).
 * @param db Active Db2 connection.
 * @param api Dispatch table.
 * @param hdbc Connection handle.
 * @param attr Attribute identifier.
 * @param val String value (skipped when NULL or empty).
 * @return C_ORM_OK on success, or mapped diagnostic error.
 */
static c_orm_error_t db2_set_str_attr(c_orm_db_t *db,
                                      const c_orm_odbc_api_t *api,
                                      c_orm_sqlhdbc_t hdbc,
                                      c_orm_sqlinteger_t attr,
                                      const char *val) {
  c_orm_sqlreturn_t sret;
  char mut_buf[512];
  c_orm_error_t rc;
  if (!val || val[0] == '\0') {
    return C_ORM_OK;
  }
  if (!api->SQLSetConnectAttr) {
    return C_ORM_ERROR_NOT_IMPLEMENTED;
  }
  rc = C_ORM_STRCPY(mut_buf, sizeof(mut_buf), val);
  if (rc != C_ORM_OK) {
    return rc;
  }
  sret = api->SQLSetConnectAttr(hdbc, attr, mut_buf, C_ORM_SQL_NTS);
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    return c_orm_odbc_harvest_diag(db, C_ORM_SQL_HANDLE_DBC, hdbc,
                                   "SQLSetConnectAttr(WLM)",
                                   C_ORM_ERROR_ODBC_DIAG);
  }
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_db2_set_client_info(c_orm_db_t *db,
                                                     const char *userid,
                                                     const char *wrkstn,
                                                     const char *applname,
                                                     const char *acctstr) {
  const c_orm_odbc_api_t *api = NULL;
  c_orm_sqlhdbc_t hdbc = C_ORM_SQL_NULL_HANDLE;
  c_orm_error_t rc;

  rc = c_orm_odbc_get_handles(db, &api, NULL, &hdbc);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = db2_set_str_attr(db, api, hdbc, C_ORM_SQL_ATTR_INFO_USERID, userid);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = db2_set_str_attr(db, api, hdbc, C_ORM_SQL_ATTR_INFO_WRKSTNNAME, wrkstn);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = db2_set_str_attr(db, api, hdbc, C_ORM_SQL_ATTR_INFO_APPLNAME, applname);
  if (rc != C_ORM_OK) {
    return rc;
  }
  return db2_set_str_attr(db, api, hdbc, C_ORM_SQL_ATTR_INFO_ACCTSTR, acctstr);
}

C_ORM_EXPORT c_orm_error_t c_orm_db2_set_current_schema(c_orm_db_t *db,
                                                        const char *schema) {
  const c_orm_odbc_api_t *api = NULL;
  c_orm_sqlhdbc_t hdbc = C_ORM_SQL_NULL_HANDLE;
  c_orm_string_builder_t *sb = NULL;
  const char *sql = NULL;
  c_orm_error_t rc;

  if (!db || !schema) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  rc = c_orm_odbc_get_handles(db, &api, NULL, &hdbc);
  if (rc != C_ORM_OK) {
    return rc;
  }

  rc = c_orm_string_builder_init(&sb);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = c_orm_string_builder_append(sb, "SET CURRENT SCHEMA = ");
  if (rc == C_ORM_OK) {
    rc = c_orm_quote_identifier(sb, schema, '"');
  }
  if (rc != C_ORM_OK) {
    (void)c_orm_string_builder_free(sb);
    return rc;
  }

  if (api->SQLSetConnectAttr) {
    char schema_mut[256];
    if (C_ORM_STRCPY(schema_mut, sizeof(schema_mut), schema) == C_ORM_OK &&
        C_ORM_SQL_SUCCEEDED(api->SQLSetConnectAttr(
            hdbc, C_ORM_SQL_ATTR_CURRENT_SCHEMA, schema_mut, C_ORM_SQL_NTS))) {
      (void)c_orm_string_builder_free(sb);
      return C_ORM_OK;
    }
  }

  rc = c_orm_string_builder_get(sb, &sql);
  if (rc == C_ORM_OK) {
    rc = db2_exec_simple(db, sql);
  }
  (void)c_orm_string_builder_free(sb);
  return rc;
}

C_ORM_EXPORT c_orm_error_t
c_orm_db2_set_isolation(c_orm_db_t *db, c_orm_db2_isolation_t level) {
  unsigned long mask;
  switch (level) {
  case C_ORM_DB2_ISOLATION_UR:
    mask = C_ORM_SQL_TXN_READ_UNCOMMITTED;
    break;
  case C_ORM_DB2_ISOLATION_CS:
    mask = C_ORM_SQL_TXN_READ_COMMITTED;
    break;
  case C_ORM_DB2_ISOLATION_RS:
    mask = C_ORM_SQL_TXN_REPEATABLE_READ;
    break;
  case C_ORM_DB2_ISOLATION_RR:
    mask = C_ORM_SQL_TXN_SERIALIZABLE;
    break;
  default:
    return C_ORM_ERROR_VALIDATION;
  }
  return c_orm_odbc_set_isolation(db, mask);
}

/**
 * @brief Queries `SELECT IDENTITY_VAL_LOCAL() FROM SYSIBM.SYSDUMMY1` and
 * converts Db2's `DECIMAL(31,0)` result into `int64_t`.
 * @param db Active Db2 connection.
 * @param out_id Output generated identity key (`0` if NULL).
 * @return C_ORM_OK on success, or mapped SQL/overflow error.
 */
static c_orm_error_t db2_get_last_insert_rowid(c_orm_db_t *db,
                                               int64_t *out_id) {
  c_orm_query_t *q = NULL;
  int has_row = 0;
  int is_null = 1;
  c_orm_decimal_t dec;
  c_orm_int64_t val = 0;
  c_orm_error_t rc;

  if (!db || !out_id) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  *out_id = 0;

  rc = db->vtable->prepare(
      db, "SELECT IDENTITY_VAL_LOCAL() FROM SYSIBM.SYSDUMMY1", &q);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = db->vtable->step(q, &has_row);
  if (rc == C_ORM_OK && has_row) {
    rc = db->vtable->is_null(q, 0, &is_null);
    if (rc == C_ORM_OK && !is_null) {
      rc = db->vtable->get_decimal(q, 0, &dec);
      if (rc == C_ORM_OK) {
        rc = c_orm_decimal_to_int64(&dec, C_ORM_ROUND_TRUNCATE, &val);
        if (rc == C_ORM_OK) {
          *out_id = (int64_t)val;
        }
      }
    }
  }
  db->vtable->finalize(q);
  return rc;
}

C_ORM_EXPORT c_orm_error_t
c_orm_db2_get_vtable(const c_orm_driver_vtable_t **out_vtable) {
  const c_orm_driver_vtable_t *base = NULL;

  if (!out_vtable) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (!g_db2_vtable_init) {
    c_orm_odbc_get_vtable(&base);
    g_db2_vtable = *base;
    g_db2_vtable.connect = c_orm_db2_connect;
    g_db2_vtable.get_last_insert_rowid = db2_get_last_insert_rowid;
    g_db2_vtable_init = 1;
  }
  *out_vtable = &g_db2_vtable;
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_db2_connect(const char *url,
                                             c_orm_db_t **out_db) {
  const c_orm_odbc_api_t *api = NULL;
  const c_orm_driver_vtable_t *vt = NULL;
  int is_direct_cli = 0;
  int prefer_wide = 0;
  char conn_str[2048];
  c_orm_db2_url_opts_t opts;
  c_orm_db_t *db = NULL;
  c_orm_error_t rc;

  if (!url || !out_db) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  *out_db = NULL;

  rc = c_orm_db2_load_cli(&api, &is_direct_cli);
  if (rc != C_ORM_OK) {
    return rc;
  }

  rc = c_orm_db2_build_conn_string(url, is_direct_cli, conn_str,
                                   sizeof(conn_str), &opts);
  if (rc != C_ORM_OK) {
    return rc;
  }

  c_orm_db2_get_vtable(&vt);

#if defined(UNICODE) || defined(_UNICODE)
  prefer_wide = (api->SQLDriverConnectW != NULL) ? 1 : 0;
#else
  prefer_wide = 0;
#endif

  rc = c_orm_odbc_connect_raw(api, conn_str, "db2", vt, opts.login_timeout,
                              opts.autocommit, prefer_wide, &db);
  if (rc != C_ORM_OK) {
    return rc;
  }

  if (opts.info_userid[0] != '\0' || opts.info_wrkstn[0] != '\0' ||
      opts.info_applname[0] != '\0' || opts.info_acctstr[0] != '\0') {
    rc = c_orm_db2_set_client_info(db, opts.info_userid, opts.info_wrkstn,
                                   opts.info_applname, opts.info_acctstr);
    if (rc != C_ORM_OK) {
      db->vtable->disconnect(db);
      return rc;
    }
  }

  if (opts.schema[0] != '\0') {
    rc = c_orm_db2_set_current_schema(db, opts.schema);
    if (rc != C_ORM_OK) {
      db->vtable->disconnect(db);
      return rc;
    }
  }

  if (opts.has_isolation) {
    rc = c_orm_db2_set_isolation(db, opts.isolation);
    if (rc != C_ORM_OK) {
      db->vtable->disconnect(db);
      return rc;
    }
  }

  *out_db = db;
  return C_ORM_OK;
}

/**
 * @brief Helper building and executing a quoted Db2 savepoint command.
 * @param db Active Db2 connection.
 * @param prefix Leading SQL keyword clause (`"SAVEPOINT "` etc.).
 * @param sp_name Savepoint identifier.
 * @param suffix Trailing SQL clause (`" ON ROLLBACK RETAIN CURSORS"` or `""`).
 * @return C_ORM_OK on success, or validation/SQL error.
 */
static c_orm_error_t db2_exec_savepoint_cmd(c_orm_db_t *db, const char *prefix,
                                            const char *sp_name,
                                            const char *suffix) {
  c_orm_string_builder_t *sb = NULL;
  const char *sql = NULL;
  c_orm_error_t rc;

  if (!db || !sp_name) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  rc = c_orm_string_builder_init(&sb);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = c_orm_string_builder_append(sb, prefix);
  if (rc == C_ORM_OK) {
    rc = c_orm_quote_identifier(sb, sp_name, '"');
  }
  if (rc == C_ORM_OK && suffix[0] != '\0') {
    rc = c_orm_string_builder_append(sb, suffix);
  }
  if (rc == C_ORM_OK) {
    rc = c_orm_string_builder_get(sb, &sql);
  }
  if (rc == C_ORM_OK) {
    rc = db2_exec_simple(db, sql);
  }
  (void)c_orm_string_builder_free(sb);
  return rc;
}

C_ORM_EXPORT c_orm_error_t
c_orm_db2_savepoint_create(c_orm_db_t *db, const char *savepoint_name) {
  return db2_exec_savepoint_cmd(db, "SAVEPOINT ", savepoint_name,
                                " ON ROLLBACK RETAIN CURSORS");
}

C_ORM_EXPORT c_orm_error_t
c_orm_db2_savepoint_rollback(c_orm_db_t *db, const char *savepoint_name) {
  return db2_exec_savepoint_cmd(db, "ROLLBACK TO SAVEPOINT ", savepoint_name,
                                "");
}

C_ORM_EXPORT c_orm_error_t
c_orm_db2_savepoint_release(c_orm_db_t *db, const char *savepoint_name) {
  return db2_exec_savepoint_cmd(db, "RELEASE SAVEPOINT ", savepoint_name, "");
}

/**
 * @brief Validates a Db2 LOB locator C type and maps it to target C & SQL
 * types.
 * @param lob_c_type Input locator C type.
 * @param out_norm_locator Normalized CLI locator C type.
 * @param out_target_c Target data C type (`SQL_C_BINARY` / `CHAR` / `WCHAR`).
 * @param out_sql_type Corresponding SQL LOB type (`SQL_BLOB` / `CLOB` /
 * `DBCLOB`).
 * @return C_ORM_OK on valid locator type, or C_ORM_ERROR_VALIDATION.
 */
static c_orm_error_t resolve_lob_locator_types(
    short lob_c_type, c_orm_sqlsmallint_t *out_norm_locator,
    c_orm_sqlsmallint_t *out_target_c, c_orm_sqlsmallint_t *out_sql_type) {
  if (lob_c_type == C_ORM_SQL_C_BLOB_LOCATOR) {
    *out_norm_locator = C_ORM_SQL_C_BLOB_LOCATOR;
    *out_target_c = C_ORM_SQL_C_BINARY;
    *out_sql_type = C_ORM_SQL_BLOB;
    return C_ORM_OK;
  }
  if (lob_c_type == C_ORM_SQL_C_CLOB_LOCATOR) {
    *out_norm_locator = C_ORM_SQL_C_CLOB_LOCATOR;
    *out_target_c = C_ORM_SQL_C_CHAR;
    *out_sql_type = C_ORM_SQL_CLOB;
    return C_ORM_OK;
  }
  if (lob_c_type == C_ORM_SQL_C_DBCLOB_LOCATOR ||
      lob_c_type == C_ORM_SQL_C_DBCLOB_LOCATOR_CLI) {
    *out_norm_locator = lob_c_type;
    *out_target_c = C_ORM_SQL_C_WCHAR;
    *out_sql_type = C_ORM_SQL_DBCLOB;
    return C_ORM_OK;
  }
  return C_ORM_ERROR_VALIDATION;
}

C_ORM_EXPORT c_orm_error_t c_orm_db2_lob_get_length(c_orm_db_t *db,
                                                    short lob_c_type,
                                                    unsigned int locator,
                                                    c_orm_int64_t *out_length) {
  const c_orm_odbc_api_t *api = NULL;
  c_orm_sqlhdbc_t hdbc = C_ORM_SQL_NULL_HANDLE;
  c_orm_sqlhandle_t hstmt = C_ORM_SQL_NULL_HANDLE;
  c_orm_sqlsmallint_t norm_loc = 0;
  c_orm_sqlsmallint_t target_c = 0;
  c_orm_sqlsmallint_t sql_t = 0;
  c_orm_sqlinteger_t slen = 0;
  c_orm_sqlinteger_t ind = 0;
  c_orm_sqlreturn_t sret;
  c_orm_error_t rc;

  if (!db || !out_length) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  *out_length = 0;

  rc = resolve_lob_locator_types(lob_c_type, &norm_loc, &target_c, &sql_t);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = c_orm_odbc_get_handles(db, &api, NULL, &hdbc);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (!api->SQLGetLength) {
    return C_ORM_ERROR_NOT_IMPLEMENTED;
  }

  sret = api->SQLAllocHandle(C_ORM_SQL_HANDLE_STMT, hdbc, &hstmt);
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    return c_orm_odbc_harvest_diag(db, C_ORM_SQL_HANDLE_DBC, hdbc,
                                   "SQLAllocHandle(SQLGetLength)",
                                   C_ORM_ERROR_ODBC_DIAG);
  }
  sret = api->SQLGetLength(hstmt, norm_loc, (c_orm_sqlinteger_t)locator, &slen,
                           &ind);
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    rc = c_orm_odbc_harvest_diag(db, C_ORM_SQL_HANDLE_STMT, hstmt,
                                 "SQLGetLength", C_ORM_ERROR_ODBC_DIAG);
    (void)api->SQLFreeHandle(C_ORM_SQL_HANDLE_STMT, hstmt);
    return rc;
  }
  (void)api->SQLFreeHandle(C_ORM_SQL_HANDLE_STMT, hstmt);
  *out_length = (ind == C_ORM_SQL_NULL_DATA || slen < 0) ? 0 : slen;
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_db2_lob_read(c_orm_db_t *db, short lob_c_type,
                                              unsigned int locator,
                                              c_orm_int64_t offset_1based,
                                              void *out_buf, size_t buf_len,
                                              size_t *out_read) {
  const c_orm_odbc_api_t *api = NULL;
  c_orm_sqlhdbc_t hdbc = C_ORM_SQL_NULL_HANDLE;
  c_orm_sqlhandle_t hstmt = C_ORM_SQL_NULL_HANDLE;
  c_orm_sqlsmallint_t norm_loc = 0;
  c_orm_sqlsmallint_t target_c = 0;
  c_orm_sqlsmallint_t sql_t = 0;
  c_orm_sqlinteger_t got_len = 0;
  c_orm_sqlinteger_t ind = 0;
  c_orm_sqluinteger_t for_units;
  c_orm_sqlreturn_t sret;
  c_orm_error_t rc;

  if (!db || !out_buf || !out_read) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  *out_read = 0;
  if (offset_1based < 1 || buf_len == 0) {
    return C_ORM_ERROR_VALIDATION;
  }

  rc = resolve_lob_locator_types(lob_c_type, &norm_loc, &target_c, &sql_t);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = c_orm_odbc_get_handles(db, &api, NULL, &hdbc);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (!api->SQLGetSubString) {
    return C_ORM_ERROR_NOT_IMPLEMENTED;
  }

  if (target_c == C_ORM_SQL_C_BINARY) {
    for_units = (c_orm_sqluinteger_t)buf_len;
  } else if (target_c == C_ORM_SQL_C_CHAR) {
    if (buf_len < 2) {
      return C_ORM_ERROR_OVERFLOW;
    }
    for_units = (c_orm_sqluinteger_t)(buf_len - 1U);
  } else {
    if (buf_len < 4) {
      return C_ORM_ERROR_OVERFLOW;
    }
    for_units = (c_orm_sqluinteger_t)((buf_len / 2U) - 1U);
  }

  sret = api->SQLAllocHandle(C_ORM_SQL_HANDLE_STMT, hdbc, &hstmt);
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    return c_orm_odbc_harvest_diag(db, C_ORM_SQL_HANDLE_DBC, hdbc,
                                   "SQLAllocHandle(SQLGetSubString)",
                                   C_ORM_ERROR_ODBC_DIAG);
  }

  sret = api->SQLGetSubString(hstmt, norm_loc, (c_orm_sqlinteger_t)locator,
                              (c_orm_sqluinteger_t)offset_1based, for_units,
                              target_c, out_buf, (c_orm_sqlinteger_t)buf_len,
                              &got_len, &ind);
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    rc = c_orm_odbc_harvest_diag(db, C_ORM_SQL_HANDLE_STMT, hstmt,
                                 "SQLGetSubString", C_ORM_ERROR_ODBC_DIAG);
    (void)api->SQLFreeHandle(C_ORM_SQL_HANDLE_STMT, hstmt);
    return rc;
  }
  (void)api->SQLFreeHandle(C_ORM_SQL_HANDLE_STMT, hstmt);

  if (ind != C_ORM_SQL_NULL_DATA && got_len > 0) {
    *out_read = (size_t)got_len;
  }
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_db2_lob_free_locator(c_orm_db_t *db,
                                                      short lob_c_type,
                                                      unsigned int locator) {
  const c_orm_odbc_api_t *api = NULL;
  c_orm_sqlhdbc_t hdbc = C_ORM_SQL_NULL_HANDLE;
  c_orm_sqlhandle_t hstmt = C_ORM_SQL_NULL_HANDLE;
  c_orm_sqlsmallint_t norm_loc = 0;
  c_orm_sqlsmallint_t target_c = 0;
  c_orm_sqlsmallint_t sql_t = 0;
  c_orm_sqlinteger_t loc_val = (c_orm_sqlinteger_t)locator;
  c_orm_sqllen_t ind = (c_orm_sqllen_t)sizeof(c_orm_sqlinteger_t);
  c_orm_sqlreturn_t sret;
  c_orm_error_t rc;
  c_orm_sqlchar_t free_sql[20];

  if (!db) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  memcpy(free_sql, "FREE LOCATOR ?", 15);
  rc = resolve_lob_locator_types(lob_c_type, &norm_loc, &target_c, &sql_t);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = c_orm_odbc_get_handles(db, &api, NULL, &hdbc);
  if (rc != C_ORM_OK) {
    return rc;
  }

  sret = api->SQLAllocHandle(C_ORM_SQL_HANDLE_STMT, hdbc, &hstmt);
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    return c_orm_odbc_harvest_diag(db, C_ORM_SQL_HANDLE_DBC, hdbc,
                                   "SQLAllocHandle(FREE LOCATOR)",
                                   C_ORM_ERROR_ODBC_DIAG);
  }

  sret =
      api->SQLBindParameter(hstmt, 1, C_ORM_SQL_PARAM_INPUT, norm_loc, sql_t, 4,
                            0, &loc_val, (c_orm_sqllen_t)sizeof(loc_val), &ind);
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    rc = c_orm_odbc_harvest_diag(db, C_ORM_SQL_HANDLE_STMT, hstmt,
                                 "SQLBindParameter(FREE LOCATOR)",
                                 C_ORM_ERROR_BIND);
    (void)api->SQLFreeHandle(C_ORM_SQL_HANDLE_STMT, hstmt);
    return rc;
  }

  if (api->SQLExecDirect) {
    sret = api->SQLExecDirect(hstmt, free_sql, C_ORM_SQL_NTS);
  } else {
    sret = api->SQLPrepare(hstmt, free_sql, C_ORM_SQL_NTS);
    if (C_ORM_SQL_SUCCEEDED(sret)) {
      sret = api->SQLExecute(hstmt);
    }
  }
  if (!C_ORM_SQL_SUCCEEDED(sret)) {
    rc = c_orm_odbc_harvest_diag(db, C_ORM_SQL_HANDLE_STMT, hstmt,
                                 "FREE LOCATOR", C_ORM_ERROR_ODBC_DIAG);
    (void)api->SQLFreeHandle(C_ORM_SQL_HANDLE_STMT, hstmt);
    return rc;
  }

  (void)api->SQLFreeHandle(C_ORM_SQL_HANDLE_STMT, hstmt);
  return C_ORM_OK;
}

/**
 * @brief Checks if `ch` is an ASCII decimal digit `'0'..'9'`.
 * @param ch Input character.
 * @param out_ok Output set to 1 if digit, 0 otherwise.
 * @return C_ORM_OK on completion.
 */
static c_orm_error_t is_dec_digit(char ch, int *out_ok) {
  *out_ok = (ch >= '0' && ch <= '9') ? 1 : 0;
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_db2_normalize_timestamp(const char *db2_ts,
                                                         char *out_iso_ts,
                                                         size_t buf_size) {
  size_t len;
  size_t i;
  static const size_t digit_idx[14] = {0, 1,  2,  3,  5,  6,  8,
                                       9, 11, 12, 14, 15, 17, 18};

  if (!db2_ts || !out_iso_ts) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  while (*db2_ts == ' ' || *db2_ts == 0x09) {
    db2_ts++;
  }
  len = strlen(db2_ts);
  while (len > 0 && (db2_ts[len - 1] == ' ' || db2_ts[len - 1] == 0x09)) {
    len--;
  }
  if (len < 19 || len > 32) {
    return C_ORM_ERROR_VALIDATION;
  }
  if (len + 1 > buf_size) {
    return C_ORM_ERROR_OVERFLOW;
  }

  for (i = 0; i < 14; i++) {
    int ok = 0;
    (void)is_dec_digit(db2_ts[digit_idx[i]], &ok);
    if (!ok) {
      return C_ORM_ERROR_VALIDATION;
    }
  }
  if (db2_ts[4] != '-' || db2_ts[7] != '-') {
    return C_ORM_ERROR_VALIDATION;
  }
  if (!(db2_ts[10] == '-' || db2_ts[10] == ' ' || db2_ts[10] == 'T')) {
    return C_ORM_ERROR_VALIDATION;
  }
  if (!((db2_ts[13] == '.' && db2_ts[16] == '.') ||
        (db2_ts[13] == ':' && db2_ts[16] == ':'))) {
    return C_ORM_ERROR_VALIDATION;
  }
  if (len > 19) {
    if (db2_ts[19] != '.' || len == 20) {
      return C_ORM_ERROR_VALIDATION;
    }
    for (i = 20; i < len; i++) {
      int ok = 0;
      (void)is_dec_digit(db2_ts[i], &ok);
      if (!ok) {
        return C_ORM_ERROR_VALIDATION;
      }
    }
  }

  memcpy(out_iso_ts, db2_ts, len);
  out_iso_ts[10] = ' ';
  out_iso_ts[13] = ':';
  out_iso_ts[16] = ':';
  out_iso_ts[len] = '\0';
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_db2_bind_graphic(c_orm_query_t *query,
                                                  int index,
                                                  const char *utf8_val) {
  return c_orm_odbc_bind_wide_param(query, index, utf8_val,
                                    C_ORM_SQL_VARGRAPHIC);
}
