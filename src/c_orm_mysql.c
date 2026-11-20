/**
 * @file c_orm_mysql.c
 * @brief MySQL/MariaDB driver implementation for c-orm.
 */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_mysql.h"
#include "c_orm_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef C_ORM_ENABLE_MYSQL
#if !defined(C_ORM_NO_THREADS) && !defined(_WIN32)
#include <pthread.h>
#endif
#ifdef C_ORM_MYSQL_DYNAMIC
#if defined(_WIN32)
__declspec(dllimport) void *__stdcall LoadLibraryA(const char *lpLibFileName);
__declspec(dllimport) void *__stdcall GetProcAddress(void *hModule,
                                                     const char *lpProcName);
#if !defined(C_ORM_NO_THREADS) && defined(_MSC_VER)
#include <intrin.h>
#endif
#else
#include <dlfcn.h>
#endif
typedef struct st_mysql MYSQL;
typedef struct st_mysql_stmt MYSQL_STMT;
typedef struct st_mysql_res MYSQL_RES;
typedef struct st_mysql_field {
  char *name;
} MYSQL_FIELD;
typedef char my_bool;
enum enum_field_types {
  MYSQL_TYPE_DECIMAL,
  MYSQL_TYPE_TINY,
  MYSQL_TYPE_SHORT,
  MYSQL_TYPE_LONG,
  MYSQL_TYPE_FLOAT,
  MYSQL_TYPE_DOUBLE,
  MYSQL_TYPE_NULL,
  MYSQL_TYPE_TIMESTAMP,
  MYSQL_TYPE_LONGLONG,
  MYSQL_TYPE_INT24,
  MYSQL_TYPE_DATE,
  MYSQL_TYPE_TIME,
  MYSQL_TYPE_DATETIME,
  MYSQL_TYPE_YEAR,
  MYSQL_TYPE_NEWDATE,
  MYSQL_TYPE_VARCHAR,
  MYSQL_TYPE_BIT,
  MYSQL_TYPE_NEWDECIMAL = 246,
  MYSQL_TYPE_ENUM = 247,
  MYSQL_TYPE_SET = 248,
  MYSQL_TYPE_TINY_BLOB = 249,
  MYSQL_TYPE_MEDIUM_BLOB = 250,
  MYSQL_TYPE_LONG_BLOB = 251,
  MYSQL_TYPE_BLOB = 252,
  MYSQL_TYPE_VAR_STRING = 253,
  MYSQL_TYPE_STRING = 254,
  MYSQL_TYPE_GEOMETRY = 255
};
typedef struct st_mysql_bind {
  unsigned long *length;
  my_bool *is_null;
  void *buffer;
  my_bool *error;
  unsigned char *row_ptr;
  void (*store_param_func)(void *net, struct st_mysql_bind *param);
  void (*fetch_result)(struct st_mysql_bind *, void *, unsigned char **row);
  void (*skip_result)(struct st_mysql_bind *, void *, unsigned char **row);
  unsigned long buffer_length;
  unsigned long offset;
  unsigned long length_value;
  unsigned int param_number;
  unsigned int pack_length;
  enum enum_field_types buffer_type;
  my_bool is_unsigned;
  my_bool long_data_used;
  my_bool is_null_value;
  void *extension;
} MYSQL_BIND;
#define MYSQL_NO_DATA 100
#define MYSQL_DATA_TRUNCATED 101
#else
#include <mysql.h>
#endif
#endif
/* clang-format on */

/**
 * @brief Parses a `mysql://` or `mariadb://` connection URL into structured
 * fields including `unix_socket` and `ssl` query parameters.
 * @param url Connection URL string.
 * @param out_info Output structure populated with parsed parameters.
 * @return C_ORM_OK on success, or C_ORM_ERROR_NULL_POINTER on NULL arguments.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_mysql_parse_url(const char *url, c_orm_mysql_url_info_t *out_info) {
  const char *p;
  const char *qmark;
  size_t path_limit;

  if (!url || !out_info) {
    return C_ORM_ERROR_NULL_POINTER;
  }

  memset(out_info, 0, sizeof(*out_info));
  C_ORM_STRCPY(out_info->host, sizeof(out_info->host), "127.0.0.1");
  C_ORM_STRCPY(out_info->user, sizeof(out_info->user), "root");
  C_ORM_STRCPY(out_info->dbname, sizeof(out_info->dbname), "test");

  p = url;
  if (strncmp(p, "mysql://", 8) == 0) {
    p += 8;
  } else if (strncmp(p, "mariadb://", 10) == 0) {
    p += 10;
  }

  qmark = strchr(p, '?');
  path_limit = qmark ? (size_t)(qmark - p) : strlen(p);

  if (path_limit > 0) {
    const char *end = p + path_limit;
    const char *at = NULL;
    const char *slash = NULL;
    const char *cur;

    for (cur = p; cur < end; cur++) {
      if (*cur == '@' && !at) {
        at = cur;
      }
    }
    if (at) {
      const char *colon = NULL;
      for (cur = p; cur < at; cur++) {
        if (*cur == ':') {
          colon = cur;
          break;
        }
      }
      if (colon) {
        size_t ulen = (size_t)(colon - p);
        size_t plen = (size_t)(at - colon - 1);
        if (ulen >= sizeof(out_info->user))
          ulen = sizeof(out_info->user) - 1;
        if (plen >= sizeof(out_info->password))
          plen = sizeof(out_info->password) - 1;
        memcpy(out_info->user, p, ulen);
        out_info->user[ulen] = '\0';
        memcpy(out_info->password, colon + 1, plen);
        out_info->password[plen] = '\0';
      } else {
        size_t ulen = (size_t)(at - p);
        if (ulen >= sizeof(out_info->user))
          ulen = sizeof(out_info->user) - 1;
        memcpy(out_info->user, p, ulen);
        out_info->user[ulen] = '\0';
      }
      p = at + 1;
    }

    for (cur = p; cur < end; cur++) {
      if (*cur == '/') {
        slash = cur;
        break;
      }
    }
    if (slash) {
      const char *colon = NULL;
      size_t dlen = (size_t)(end - (slash + 1));
      for (cur = p; cur < slash; cur++) {
        if (*cur == ':') {
          colon = cur;
          break;
        }
      }
      if (colon) {
        size_t hlen = (size_t)(colon - p);
        char port_buf[16];
        size_t port_len = (size_t)(slash - colon - 1);
        if (hlen >= sizeof(out_info->host))
          hlen = sizeof(out_info->host) - 1;
        memcpy(out_info->host, p, hlen);
        out_info->host[hlen] = '\0';
        if (port_len > 0 && port_len < sizeof(port_buf)) {
          memcpy(port_buf, colon + 1, port_len);
          port_buf[port_len] = '\0';
          out_info->port = (unsigned int)atoi(port_buf);
        }
      } else if (slash > p) {
        size_t hlen = (size_t)(slash - p);
        if (hlen >= sizeof(out_info->host))
          hlen = sizeof(out_info->host) - 1;
        memcpy(out_info->host, p, hlen);
        out_info->host[hlen] = '\0';
      }
      if (dlen > 0) {
        if (dlen >= sizeof(out_info->dbname))
          dlen = sizeof(out_info->dbname) - 1;
        memcpy(out_info->dbname, slash + 1, dlen);
        out_info->dbname[dlen] = '\0';
      }
    } else if (end > p) {
      const char *colon = NULL;
      for (cur = p; cur < end; cur++) {
        if (*cur == ':') {
          colon = cur;
          break;
        }
      }
      if (colon) {
        size_t hlen = (size_t)(colon - p);
        char port_buf[16];
        size_t port_len = (size_t)(end - colon - 1);
        if (hlen >= sizeof(out_info->host))
          hlen = sizeof(out_info->host) - 1;
        memcpy(out_info->host, p, hlen);
        out_info->host[hlen] = '\0';
        if (port_len > 0 && port_len < sizeof(port_buf)) {
          memcpy(port_buf, colon + 1, port_len);
          port_buf[port_len] = '\0';
          out_info->port = (unsigned int)atoi(port_buf);
        }
      } else {
        size_t hlen = (size_t)(end - p);
        if (hlen >= sizeof(out_info->host))
          hlen = sizeof(out_info->host) - 1;
        memcpy(out_info->host, p, hlen);
        out_info->host[hlen] = '\0';
      }
    }
  }

  /* Parse query string parameters (?unix_socket=...&ssl=1) */
  if (qmark && qmark[1] != '\0') {
    const char *qp = qmark + 1;
    while (*qp) {
      const char *amp = strchr(qp, '&');
      size_t pair_len = amp ? (size_t)(amp - qp) : strlen(qp);
      const char *eq = memchr(qp, '=', pair_len);
      if (eq) {
        size_t klen = (size_t)(eq - qp);
        const char *val = eq + 1;
        size_t vlen = pair_len - klen - 1;
        if ((klen == 11 && strncmp(qp, "unix_socket", 11) == 0) ||
            (klen == 6 && strncmp(qp, "socket", 6) == 0)) {
          if (vlen >= sizeof(out_info->unix_socket))
            vlen = sizeof(out_info->unix_socket) - 1;
          memcpy(out_info->unix_socket, val, vlen);
          out_info->unix_socket[vlen] = '\0';
        } else if ((klen == 3 && strncmp(qp, "ssl", 3) == 0) ||
                   (klen == 8 && strncmp(qp, "ssl-mode", 8) == 0) ||
                   (klen == 7 && strncmp(qp, "sslmode", 7) == 0)) {
          if ((vlen == 1 && val[0] == '1') ||
              (vlen == 4 && strncmp(val, "true", 4) == 0) ||
              (vlen == 4 && strncmp(val, "TRUE", 4) == 0) ||
              (vlen == 8 && strncmp(val, "REQUIRED", 8) == 0) ||
              (vlen == 7 && strncmp(val, "require", 7) == 0)) {
            out_info->use_ssl = 1;
          } else if ((vlen == 1 && val[0] == '0') ||
                     (vlen == 5 && strncmp(val, "false", 5) == 0) ||
                     (vlen == 8 && strncmp(val, "DISABLED", 8) == 0)) {
            out_info->use_ssl = 0;
          }
        }
      }
      if (!amp) {
        break;
      }
      qp = amp + 1;
    }
  }

  return C_ORM_OK;
}

#ifdef C_ORM_ENABLE_MYSQL

#ifdef C_ORM_MYSQL_DYNAMIC
static MYSQL *(*dyn_mysql_init)(MYSQL *mysql);
static my_bool (*dyn_mysql_ssl_set)(MYSQL *mysql, const char *key,
                                    const char *cert, const char *ca,
                                    const char *capath, const char *cipher);
static MYSQL *(*dyn_mysql_real_connect)(MYSQL *mysql, const char *host,
                                        const char *user, const char *passwd,
                                        const char *db, unsigned int port,
                                        const char *unix_socket,
                                        unsigned long clientflag);
static void (*dyn_mysql_close)(MYSQL *sock);
static const char *(*dyn_mysql_error)(MYSQL *mysql);
static unsigned long long (*dyn_mysql_insert_id)(MYSQL *mysql);
static MYSQL_STMT *(*dyn_mysql_stmt_init)(MYSQL *mysql);
static int (*dyn_mysql_stmt_prepare)(MYSQL_STMT *stmt, const char *query,
                                     unsigned long length);
static unsigned long (*dyn_mysql_stmt_param_count)(MYSQL_STMT *stmt);
static my_bool (*dyn_mysql_stmt_bind_param)(MYSQL_STMT *stmt, MYSQL_BIND *bnd);
static int (*dyn_mysql_stmt_execute)(MYSQL_STMT *stmt);
static MYSQL_RES *(*dyn_mysql_stmt_result_metadata)(MYSQL_STMT *stmt);
static unsigned int (*dyn_mysql_num_fields)(MYSQL_RES *res);
static MYSQL_FIELD *(*dyn_mysql_fetch_field_direct)(MYSQL_RES *res,
                                                    unsigned int fieldnr);
static void (*dyn_mysql_free_result)(MYSQL_RES *result);
static my_bool (*dyn_mysql_stmt_bind_result)(MYSQL_STMT *stmt, MYSQL_BIND *bnd);
static int (*dyn_mysql_stmt_store_result)(MYSQL_STMT *stmt);
static int (*dyn_mysql_stmt_fetch)(MYSQL_STMT *stmt);
static int (*dyn_mysql_stmt_fetch_column)(MYSQL_STMT *stmt, MYSQL_BIND *bind,
                                          unsigned int column,
                                          unsigned long offset);
static my_bool (*dyn_mysql_stmt_close)(MYSQL_STMT *stmt);
static my_bool (*dyn_mysql_stmt_free_result)(MYSQL_STMT *stmt);
static const char *(*dyn_mysql_stmt_error)(MYSQL_STMT *stmt);

#if !defined(C_ORM_NO_THREADS)
#if defined(_WIN32)
static volatile long mysql_dyn_lock = 0;
#else
static pthread_mutex_t mysql_dyn_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif
#endif

/**
 * @brief Dynamically loads libmariadb/libmysqlclient symbols at runtime.
 * @return C_ORM_OK on success, C_ORM_ERROR_CONNECTION on failure.
 */
static c_orm_error_t c_orm_load_libmysql(void) {
  static int loaded = 0;
  void *h;
#if !defined(C_ORM_NO_THREADS)
#if defined(_MSC_VER)
  while (_InterlockedCompareExchange(&mysql_dyn_lock, 1L, 0L) != 0L) {
  }
#elif defined(_WIN32)
  while (__sync_val_compare_and_swap(&mysql_dyn_lock, 0L, 1L) != 0L) {
  }
#else
  if (pthread_mutex_lock(&mysql_dyn_mutex) != 0) {
    return C_ORM_ERROR_BUSY;
  }
#endif
#endif
  if (loaded) {
#if !defined(C_ORM_NO_THREADS)
#if defined(_WIN32)
    mysql_dyn_lock = 0;
#else
    (void)pthread_mutex_unlock(&mysql_dyn_mutex);
#endif
#endif
    return C_ORM_OK;
  }
#if defined(_WIN32)
  h = LoadLibraryA("libmariadb.dll");
  if (!h)
    h = LoadLibraryA("libmysql.dll");
#elif defined(__APPLE__)
  h = dlopen("libmariadb.dylib", RTLD_LAZY);
  if (!h)
    h = dlopen("libmysqlclient.dylib", RTLD_LAZY);
#else
  h = dlopen("libmariadb.so", RTLD_LAZY);
  if (!h)
    h = dlopen("libmysqlclient.so", RTLD_LAZY);
#endif
  if (!h) {
#if !defined(C_ORM_NO_THREADS)
#if defined(_WIN32)
    mysql_dyn_lock = 0;
#else
    (void)pthread_mutex_unlock(&mysql_dyn_mutex);
#endif
#endif
    return C_ORM_ERROR_CONNECTION;
  }
#if defined(_WIN32)
#define MY_SYM(name) *(void **)(&dyn_##name) = GetProcAddress(h, #name)
#else
#define MY_SYM(name) *(void **)(&dyn_##name) = dlsym(h, #name)
#endif
  MY_SYM(mysql_init);
  MY_SYM(mysql_ssl_set);
  MY_SYM(mysql_real_connect);
  MY_SYM(mysql_close);
  MY_SYM(mysql_error);
  MY_SYM(mysql_insert_id);
  MY_SYM(mysql_stmt_init);
  MY_SYM(mysql_stmt_prepare);
  MY_SYM(mysql_stmt_param_count);
  MY_SYM(mysql_stmt_bind_param);
  MY_SYM(mysql_stmt_execute);
  MY_SYM(mysql_stmt_result_metadata);
  MY_SYM(mysql_num_fields);
  MY_SYM(mysql_fetch_field_direct);
  MY_SYM(mysql_free_result);
  MY_SYM(mysql_stmt_bind_result);
  MY_SYM(mysql_stmt_store_result);
  MY_SYM(mysql_stmt_fetch);
  MY_SYM(mysql_stmt_fetch_column);
  MY_SYM(mysql_stmt_close);
  MY_SYM(mysql_stmt_free_result);
  MY_SYM(mysql_stmt_error);
#undef MY_SYM
  if (!dyn_mysql_init || !dyn_mysql_real_connect || !dyn_mysql_close) {
#if !defined(C_ORM_NO_THREADS)
#if defined(_WIN32)
    mysql_dyn_lock = 0;
#else
    (void)pthread_mutex_unlock(&mysql_dyn_mutex);
#endif
#endif
    return C_ORM_ERROR_CONNECTION;
  }
  loaded = 1;
#if !defined(C_ORM_NO_THREADS)
#if defined(_WIN32)
  mysql_dyn_lock = 0;
#else
  (void)pthread_mutex_unlock(&mysql_dyn_mutex);
#endif
#endif
  return C_ORM_OK;
}
#define mysql_init dyn_mysql_init
#define mysql_ssl_set dyn_mysql_ssl_set
#define mysql_real_connect dyn_mysql_real_connect
#define mysql_close dyn_mysql_close
#define mysql_error dyn_mysql_error
#define mysql_insert_id dyn_mysql_insert_id
#define mysql_stmt_init dyn_mysql_stmt_init
#define mysql_stmt_prepare dyn_mysql_stmt_prepare
#define mysql_stmt_param_count dyn_mysql_stmt_param_count
#define mysql_stmt_bind_param dyn_mysql_stmt_bind_param
#define mysql_stmt_execute dyn_mysql_stmt_execute
#define mysql_stmt_result_metadata dyn_mysql_stmt_result_metadata
#define mysql_num_fields dyn_mysql_num_fields
#define mysql_fetch_field_direct dyn_mysql_fetch_field_direct
#define mysql_free_result dyn_mysql_free_result
#define mysql_stmt_bind_result dyn_mysql_stmt_bind_result
#define mysql_stmt_store_result dyn_mysql_stmt_store_result
#define mysql_stmt_fetch dyn_mysql_stmt_fetch
#define mysql_stmt_fetch_column dyn_mysql_stmt_fetch_column
#define mysql_stmt_close dyn_mysql_stmt_close
#define mysql_stmt_free_result dyn_mysql_stmt_free_result
#define mysql_stmt_error dyn_mysql_stmt_error
#endif /* C_ORM_MYSQL_DYNAMIC */

/** @brief Internal data for MySQL connection. */
struct mysql_db_data {
  /** @brief Underlying MYSQL handle. */
  MYSQL *conn;
  /** @brief Last error message buffer. */
  char last_error[512];
};

/** @brief Internal data for MySQL query. */
struct mysql_query_data {
  /** @brief Owning database handle. */
  c_orm_db_t *db;
  /** @brief Prepared MYSQL_STMT pointer. */
  MYSQL_STMT *stmt;
  /** @brief Bound parameter count. */
  int param_count;
  /** @brief Bound MYSQL_BIND parameter array. */
  MYSQL_BIND *bind_params;
  /** @brief Bound parameter NULL indicator array. */
  my_bool *param_is_null;
  /** @brief Result column count. */
  int result_col_count;
  /** @brief Result MYSQL_BIND array. */
  MYSQL_BIND *result_binds;
  /** @brief Result column NULL flags. */
  my_bool *result_is_null;
  /** @brief Result column byte lengths. */
  unsigned long *result_length;
  /** @brief Result column heap buffers. */
  char **result_buffers;
  /** @brief Result column names copied from metadata. */
  char **result_col_names;
  /** @brief Non-zero once statement result has been executed and bound. */
  int has_result;
};

/** @brief Internal MySQL statement handle wrapper. */
typedef struct mysql_stmt_handle {
  /** @brief Pointer to MySQL query state. */
  struct mysql_query_data *data;
} mysql_stmt_t;

/**
 * @brief Sets the last error message on the database context.
 * @param db The database connection.
 * @param msg The error message to set.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t set_error(c_orm_db_t *db, const char *msg) {
  if (db && db->driver_data) {
    struct mysql_db_data *data = (struct mysql_db_data *)db->driver_data;
    if (msg) {
      size_t len = strlen(msg);
      if (len >= sizeof(data->last_error)) {
        len = sizeof(data->last_error) - 1;
      }
      memcpy(data->last_error, msg, len);
      data->last_error[len] = '\0';
    } else if (data->conn) {
      const char *my_err = mysql_error(data->conn);
      size_t len = strlen(my_err);
      if (len >= sizeof(data->last_error)) {
        len = sizeof(data->last_error) - 1;
      }
      memcpy(data->last_error, my_err, len);
      data->last_error[len] = '\0';
    }
  }
  return C_ORM_OK;
}

/**
 * @brief Connects to a MySQL database.
 * @param url The connection URL.
 * @param out_db Pointer to store the created database connection.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t mysql_drv_connect(const char *url, c_orm_db_t **out_db) {
  c_orm_error_t rc;
  c_orm_db_t *db;
  struct mysql_db_data *data;
  c_orm_mysql_url_info_t info;

  if (!url || !out_db) {
    return C_ORM_ERROR_MEMORY;
  }

  rc = c_orm_mysql_parse_url(url, &info);
  if (rc != C_ORM_OK) {
    return rc;
  }

#ifdef C_ORM_MYSQL_DYNAMIC
  rc = c_orm_load_libmysql();
  if (rc != C_ORM_OK) {
    return rc;
  }
#endif

  db = NULL;
  if (c_orm_system_calloc(1, sizeof(c_orm_db_t), (void **)&db) != C_ORM_OK ||
      !db) {
    return C_ORM_ERROR_MEMORY;
  }

  data = NULL;
  if (c_orm_system_calloc(1, sizeof(struct mysql_db_data), (void **)&data) !=
          C_ORM_OK ||
      !data) {
    (void)c_orm_system_free(db);
    return C_ORM_ERROR_MEMORY;
  }

  data->conn = mysql_init(NULL);
  if (!data->conn) {
    (void)c_orm_system_free(data);
    (void)c_orm_system_free(db);
    return C_ORM_ERROR_CONNECTION;
  }

  if (info.use_ssl) {
#ifdef C_ORM_MYSQL_DYNAMIC
    if (dyn_mysql_ssl_set) {
      (void)mysql_ssl_set(data->conn, NULL, NULL, NULL, NULL, NULL);
    }
#else
    (void)mysql_ssl_set(data->conn, NULL, NULL, NULL, NULL, NULL);
#endif
  }

  if (!mysql_real_connect(data->conn, info.host, info.user, info.password,
                          info.dbname, info.port,
                          info.unix_socket[0] ? info.unix_socket : NULL, 0)) {
    mysql_close(data->conn);
    (void)c_orm_system_free(data);
    (void)c_orm_system_free(db);
    return C_ORM_ERROR_CONNECTION;
  }

  if (c_orm_mysql_get_vtable(&db->vtable) != C_ORM_OK) {
    mysql_close(data->conn);
    (void)c_orm_system_free(data);
    (void)c_orm_system_free(db);
    return C_ORM_ERROR_UNKNOWN;
  }

  db->driver_data = data;
  db->driver_name = "mysql";
  *out_db = db;
  return C_ORM_OK;
}

/**
 * @brief Disconnects from a MySQL database.
 * @param db The database connection.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t mysql_drv_disconnect(c_orm_db_t *db) {
  struct mysql_db_data *data;

  if (!db) {
    return C_ORM_OK;
  }

  data = (struct mysql_db_data *)db->driver_data;
  if (data) {
    if (data->conn) {
      mysql_close(data->conn);
    }
    (void)c_orm_system_free(data);
  }
  return c_orm_system_free(db);
}

/**
 * @brief Prepares a SQL query.
 * @param db The database connection.
 * @param sql The SQL string.
 * @param out_query Pointer to store the prepared query.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t mysql_drv_prepare(c_orm_db_t *db, const char *sql,
                                       c_orm_query_t **out_query) {
  c_orm_error_t rc;
  struct mysql_db_data *db_data;
  struct mysql_query_data *q_data;
  mysql_stmt_t *stmt_handle;

  if (!db || !sql || !out_query) {
    return C_ORM_ERROR_MEMORY;
  }
  db_data = (struct mysql_db_data *)db->driver_data;

  if (db->log_cb) {
    rc = db->log_cb(sql, db->log_user_data);
    if (rc != C_ORM_OK) {
      return rc;
    }
  }

  stmt_handle = NULL;
  if (c_orm_system_calloc(1, sizeof(mysql_stmt_t), (void **)&stmt_handle) !=
          C_ORM_OK ||
      !stmt_handle) {
    return C_ORM_ERROR_MEMORY;
  }

  q_data = NULL;
  if (c_orm_system_calloc(1, sizeof(struct mysql_query_data),
                          (void **)&q_data) != C_ORM_OK ||
      !q_data) {
    (void)c_orm_system_free(stmt_handle);
    return C_ORM_ERROR_MEMORY;
  }

  q_data->stmt = mysql_stmt_init(db_data->conn);
  if (!q_data->stmt) {
    if (set_error(db, mysql_error(db_data->conn)) != C_ORM_OK) {
      /* recorded best-effort */
    }
    (void)c_orm_system_free(q_data);
    (void)c_orm_system_free(stmt_handle);
    return C_ORM_ERROR_SQL;
  }

  if (mysql_stmt_prepare(q_data->stmt, sql, (unsigned long)strlen(sql))) {
    if (set_error(db, mysql_stmt_error(q_data->stmt)) != C_ORM_OK) {
      /* recorded best-effort */
    }
    mysql_stmt_close(q_data->stmt);
    (void)c_orm_system_free(q_data);
    (void)c_orm_system_free(stmt_handle);
    return C_ORM_ERROR_SQL;
  }

  q_data->db = db;
  q_data->param_count = (int)mysql_stmt_param_count(q_data->stmt);
  if (q_data->param_count > 0) {
    if (c_orm_system_calloc((size_t)q_data->param_count, sizeof(MYSQL_BIND),
                            (void **)&q_data->bind_params) != C_ORM_OK ||
        c_orm_system_calloc((size_t)q_data->param_count, sizeof(my_bool),
                            (void **)&q_data->param_is_null) != C_ORM_OK ||
        !q_data->bind_params || !q_data->param_is_null) {
      if (q_data->bind_params)
        (void)c_orm_system_free(q_data->bind_params);
      if (q_data->param_is_null)
        (void)c_orm_system_free(q_data->param_is_null);
      mysql_stmt_close(q_data->stmt);
      (void)c_orm_system_free(q_data);
      (void)c_orm_system_free(stmt_handle);
      return C_ORM_ERROR_MEMORY;
    }
  }

  stmt_handle->data = q_data;
  *out_query = (c_orm_query_t *)stmt_handle;
  return C_ORM_OK;
}

/** @brief Binds a NULL value to a query parameter. */
static c_orm_error_t mysql_drv_bind_null(c_orm_query_t *query, int index) {
  mysql_stmt_t *stmt = (mysql_stmt_t *)query;
  int i;
  MYSQL_BIND *b;

  if (!stmt || !stmt->data || index < 1 || index > stmt->data->param_count) {
    return C_ORM_ERROR_BIND;
  }

  i = index - 1;
  b = &stmt->data->bind_params[i];
  b->buffer_type = MYSQL_TYPE_NULL;
  b->is_null = &stmt->data->param_is_null[i];
  *b->is_null = 1;
  return C_ORM_OK;
}

/** @brief Binds a 32-bit integer to a query parameter. */
static c_orm_error_t mysql_drv_bind_int32(c_orm_query_t *query, int index,
                                          int32_t val) {
  mysql_stmt_t *stmt = (mysql_stmt_t *)query;
  int i;
  MYSQL_BIND *b;

  if (!stmt || !stmt->data || index < 1 || index > stmt->data->param_count) {
    return C_ORM_ERROR_BIND;
  }

  i = index - 1;
  b = &stmt->data->bind_params[i];
  b->buffer_type = MYSQL_TYPE_LONG;
  if (!b->buffer) {
    if (c_orm_system_malloc(sizeof(int32_t), &b->buffer) != C_ORM_OK ||
        !b->buffer) {
      return C_ORM_ERROR_MEMORY;
    }
  }
  *(int32_t *)b->buffer = val;
  b->is_null = &stmt->data->param_is_null[i];
  *b->is_null = 0;
  return C_ORM_OK;
}

/** @brief Binds a 64-bit integer to a query parameter. */
static c_orm_error_t mysql_drv_bind_int64(c_orm_query_t *query, int index,
                                          int64_t val) {
  mysql_stmt_t *stmt = (mysql_stmt_t *)query;
  int i;
  MYSQL_BIND *b;

  if (!stmt || !stmt->data || index < 1 || index > stmt->data->param_count) {
    return C_ORM_ERROR_BIND;
  }

  i = index - 1;
  b = &stmt->data->bind_params[i];
  b->buffer_type = MYSQL_TYPE_LONGLONG;
  if (!b->buffer) {
    if (c_orm_system_malloc(sizeof(int64_t), &b->buffer) != C_ORM_OK ||
        !b->buffer) {
      return C_ORM_ERROR_MEMORY;
    }
  }
  *(int64_t *)b->buffer = val;
  b->is_null = &stmt->data->param_is_null[i];
  *b->is_null = 0;
  return C_ORM_OK;
}

/** @brief Binds a double to a query parameter. */
static c_orm_error_t mysql_drv_bind_double(c_orm_query_t *query, int index,
                                           double val) {
  mysql_stmt_t *stmt = (mysql_stmt_t *)query;
  int i;
  MYSQL_BIND *b;

  if (!stmt || !stmt->data || index < 1 || index > stmt->data->param_count) {
    return C_ORM_ERROR_BIND;
  }

  i = index - 1;
  b = &stmt->data->bind_params[i];
  b->buffer_type = MYSQL_TYPE_DOUBLE;
  if (!b->buffer) {
    if (c_orm_system_malloc(sizeof(double), &b->buffer) != C_ORM_OK ||
        !b->buffer) {
      return C_ORM_ERROR_MEMORY;
    }
  }
  *(double *)b->buffer = val;
  b->is_null = &stmt->data->param_is_null[i];
  *b->is_null = 0;
  return C_ORM_OK;
}

/** @brief Binds a string to a query parameter (routes NULL to bind_null). */
static c_orm_error_t mysql_drv_bind_string(c_orm_query_t *query, int index,
                                           const char *val) {
  mysql_stmt_t *stmt = (mysql_stmt_t *)query;
  int i;
  size_t len;
  MYSQL_BIND *b;

  if (!stmt || !stmt->data || index < 1 || index > stmt->data->param_count) {
    return C_ORM_ERROR_BIND;
  }
  if (!val) {
    return mysql_drv_bind_null(query, index);
  }

  i = index - 1;
  len = strlen(val);
  b = &stmt->data->bind_params[i];

  b->buffer_type = MYSQL_TYPE_STRING;
  if (b->buffer) {
    (void)c_orm_system_free(b->buffer);
    b->buffer = NULL;
  }

  if (c_orm_system_malloc(len + 1, &b->buffer) != C_ORM_OK || !b->buffer) {
    return C_ORM_ERROR_MEMORY;
  }

  C_ORM_STRCPY((char *)b->buffer, len + 1, val);
  b->buffer_length = (unsigned long)len;
  b->is_null = &stmt->data->param_is_null[i];
  *b->is_null = 0;
  return C_ORM_OK;
}

/** @brief Binds a BLOB to a query parameter. */
static c_orm_error_t mysql_drv_bind_blob(c_orm_query_t *query, int index,
                                         const void *val, size_t size) {
  mysql_stmt_t *stmt = (mysql_stmt_t *)query;
  int i;
  MYSQL_BIND *b;

  if (!stmt || !stmt->data || index < 1 || index > stmt->data->param_count) {
    return C_ORM_ERROR_BIND;
  }
  if (!val && size == 0) {
    return mysql_drv_bind_null(query, index);
  }

  i = index - 1;
  b = &stmt->data->bind_params[i];

  b->buffer_type = MYSQL_TYPE_BLOB;
  if (b->buffer) {
    (void)c_orm_system_free(b->buffer);
    b->buffer = NULL;
  }

  if (c_orm_system_malloc(size > 0 ? size : 1, &b->buffer) != C_ORM_OK ||
      !b->buffer) {
    return C_ORM_ERROR_MEMORY;
  }

  if (size > 0 && val) {
    memcpy(b->buffer, val, size);
  }
  b->buffer_length = (unsigned long)size;
  b->is_null = &stmt->data->param_is_null[i];
  *b->is_null = 0;
  return C_ORM_OK;
}

/** @brief Initializes result binds and column names for a query. */
static c_orm_error_t init_result_binds(struct mysql_query_data *q_data) {
  MYSQL_RES *meta;
  int i;

  meta = mysql_stmt_result_metadata(q_data->stmt);
  if (!meta) {
    q_data->result_col_count = 0;
    return C_ORM_OK;
  }

  q_data->result_col_count = (int)mysql_num_fields(meta);

  if (q_data->result_col_count > 0) {
    if (c_orm_system_calloc((size_t)q_data->result_col_count,
                            sizeof(MYSQL_BIND),
                            (void **)&q_data->result_binds) != C_ORM_OK ||
        c_orm_system_calloc((size_t)q_data->result_col_count, sizeof(my_bool),
                            (void **)&q_data->result_is_null) != C_ORM_OK ||
        c_orm_system_calloc((size_t)q_data->result_col_count,
                            sizeof(unsigned long),
                            (void **)&q_data->result_length) != C_ORM_OK ||
        c_orm_system_calloc((size_t)q_data->result_col_count, sizeof(char *),
                            (void **)&q_data->result_buffers) != C_ORM_OK ||
        c_orm_system_calloc((size_t)q_data->result_col_count, sizeof(char *),
                            (void **)&q_data->result_col_names) != C_ORM_OK ||
        !q_data->result_binds || !q_data->result_is_null ||
        !q_data->result_length || !q_data->result_buffers ||
        !q_data->result_col_names) {
      mysql_free_result(meta);
      return C_ORM_ERROR_MEMORY;
    }

    for (i = 0; i < q_data->result_col_count; i++) {
      MYSQL_FIELD *fld = NULL;
#ifdef C_ORM_MYSQL_DYNAMIC
      if (dyn_mysql_fetch_field_direct) {
        fld = mysql_fetch_field_direct(meta, (unsigned int)i);
      }
#else
      fld = mysql_fetch_field_direct(meta, (unsigned int)i);
#endif
      if (fld && fld->name) {
        (void)c_orm_strdup(fld->name, &q_data->result_col_names[i]);
      }
      q_data->result_buffers[i] = NULL;
      q_data->result_binds[i].buffer_type = MYSQL_TYPE_STRING;
      q_data->result_binds[i].buffer = NULL;
      q_data->result_binds[i].buffer_length = 0;
      q_data->result_binds[i].is_null = &q_data->result_is_null[i];
      q_data->result_binds[i].length = &q_data->result_length[i];
    }

    mysql_free_result(meta);

    if (mysql_stmt_bind_result(q_data->stmt, q_data->result_binds)) {
      return C_ORM_ERROR_STEP;
    }
    return C_ORM_OK;
  }

  mysql_free_result(meta);
  return C_ORM_OK;
}

/** @brief Steps execution of a query. */
static c_orm_error_t mysql_drv_step(c_orm_query_t *query, int *out_has_row) {
  mysql_stmt_t *stmt = (mysql_stmt_t *)query;
  struct mysql_query_data *q_data;

  if (!stmt || !stmt->data || !out_has_row) {
    return C_ORM_ERROR_STEP;
  }
  q_data = stmt->data;

  if (!q_data->has_result) {
    if (q_data->param_count > 0) {
      if (mysql_stmt_bind_param(q_data->stmt, q_data->bind_params)) {
        if (set_error(q_data->db, mysql_stmt_error(q_data->stmt)) != C_ORM_OK) {
          /* recorded best-effort */
        }
        return C_ORM_ERROR_STEP;
      }
    }

    if (mysql_stmt_execute(q_data->stmt)) {
      if (set_error(q_data->db, mysql_stmt_error(q_data->stmt)) != C_ORM_OK) {
        /* recorded best-effort */
      }
      return C_ORM_ERROR_STEP;
    }

    if (init_result_binds(q_data) != C_ORM_OK) {
      if (set_error(q_data->db, mysql_stmt_error(q_data->stmt)) != C_ORM_OK) {
        /* recorded best-effort */
      }
      return C_ORM_ERROR_STEP;
    }

    if (q_data->result_col_count > 0) {
      if (mysql_stmt_store_result(q_data->stmt)) {
        if (set_error(q_data->db, mysql_stmt_error(q_data->stmt)) != C_ORM_OK) {
          /* recorded best-effort */
        }
        return C_ORM_ERROR_STEP;
      }
    }
    q_data->has_result = 1;
  }

  if (q_data->result_col_count == 0) {
    *out_has_row = 0;
    return C_ORM_OK;
  }

  {
    int fetch_rc = mysql_stmt_fetch(q_data->stmt);
    if (fetch_rc == 0 || fetch_rc == MYSQL_DATA_TRUNCATED) {
      int col_i;
      for (col_i = 0; col_i < q_data->result_col_count; col_i++) {
        if (q_data->result_buffers[col_i]) {
          (void)c_orm_system_free(q_data->result_buffers[col_i]);
          q_data->result_buffers[col_i] = NULL;
        }
        if (!q_data->result_is_null[col_i]) {
          unsigned long len = q_data->result_length[col_i];
          if (c_orm_system_malloc(len + 1,
                                  (void **)&q_data->result_buffers[col_i]) !=
                  C_ORM_OK ||
              !q_data->result_buffers[col_i]) {
            return C_ORM_ERROR_MEMORY;
          }
          q_data->result_binds[col_i].buffer = q_data->result_buffers[col_i];
          q_data->result_binds[col_i].buffer_length = len + 1;
          if (mysql_stmt_fetch_column(q_data->stmt,
                                      &q_data->result_binds[col_i],
                                      (unsigned int)col_i, 0) != 0) {
            if (set_error(q_data->db, mysql_stmt_error(q_data->stmt)) !=
                C_ORM_OK) {
              /* recorded best-effort */
            }
            return C_ORM_ERROR_STEP;
          }
          q_data->result_buffers[col_i][len] = '\0';
          q_data->result_binds[col_i].buffer = NULL;
          q_data->result_binds[col_i].buffer_length = 0;
        }
      }
      *out_has_row = 1;
      return C_ORM_OK;
    } else if (fetch_rc == MYSQL_NO_DATA) {
      *out_has_row = 0;
      return C_ORM_OK;
    }
  }

  if (set_error(q_data->db, mysql_stmt_error(q_data->stmt)) != C_ORM_OK) {
    /* recorded best-effort */
  }
  return C_ORM_ERROR_STEP;
}

/** @brief Gets a 32-bit integer from a query result. */
static c_orm_error_t mysql_drv_get_int32(c_orm_query_t *query, int index,
                                         int32_t *out_val) {
  mysql_stmt_t *stmt = (mysql_stmt_t *)query;
  if (!stmt || !stmt->data || !stmt->data->has_result || !out_val) {
    return C_ORM_ERROR_MEMORY;
  }
  if (index < 0 || index >= stmt->data->result_col_count) {
    return C_ORM_ERROR_VALIDATION;
  }
  if (stmt->data->result_is_null[index] || !stmt->data->result_buffers ||
      !stmt->data->result_buffers[index]) {
    *out_val = 0;
  } else {
    *out_val = (int32_t)atoi(stmt->data->result_buffers[index]);
  }
  return C_ORM_OK;
}

/** @brief Gets a 64-bit integer from a query result. */
static c_orm_error_t mysql_drv_get_int64(c_orm_query_t *query, int index,
                                         int64_t *out_val) {
  mysql_stmt_t *stmt = (mysql_stmt_t *)query;
  if (!stmt || !stmt->data || !stmt->data->has_result || !out_val) {
    return C_ORM_ERROR_MEMORY;
  }
  if (index < 0 || index >= stmt->data->result_col_count) {
    return C_ORM_ERROR_VALIDATION;
  }
  *out_val = 0;
  if (!stmt->data->result_is_null[index] && stmt->data->result_buffers &&
      stmt->data->result_buffers[index]) {
    if (c_orm_strtoint64(stmt->data->result_buffers[index], NULL, 10,
                         out_val) != C_ORM_OK) {
      *out_val = 0;
    }
  }
  return C_ORM_OK;
}

/** @brief Gets a double from a query result. */
static c_orm_error_t mysql_drv_get_double(c_orm_query_t *query, int index,
                                          double *out_val) {
  mysql_stmt_t *stmt = (mysql_stmt_t *)query;
  if (!stmt || !stmt->data || !stmt->data->has_result || !out_val) {
    return C_ORM_ERROR_MEMORY;
  }
  if (index < 0 || index >= stmt->data->result_col_count) {
    return C_ORM_ERROR_VALIDATION;
  }
  if (stmt->data->result_is_null[index] || !stmt->data->result_buffers ||
      !stmt->data->result_buffers[index]) {
    *out_val = 0.0;
  } else {
    *out_val = strtod(stmt->data->result_buffers[index], NULL);
  }
  return C_ORM_OK;
}

/** @brief Gets a string from a query result. */
static c_orm_error_t mysql_drv_get_string(c_orm_query_t *query, int index,
                                          const char **out_val) {
  mysql_stmt_t *stmt = (mysql_stmt_t *)query;
  if (!stmt || !stmt->data || !stmt->data->has_result || !out_val) {
    return C_ORM_ERROR_MEMORY;
  }
  if (index < 0 || index >= stmt->data->result_col_count) {
    return C_ORM_ERROR_VALIDATION;
  }
  *out_val = stmt->data->result_is_null[index]
                 ? NULL
                 : stmt->data->result_buffers[index];
  return C_ORM_OK;
}

/** @brief Gets a BLOB from a query result. */
static c_orm_error_t mysql_drv_get_blob(c_orm_query_t *query, int index,
                                        const void **out_val,
                                        size_t *out_size) {
  mysql_stmt_t *stmt = (mysql_stmt_t *)query;
  if (!stmt || !stmt->data || !stmt->data->has_result || !out_val ||
      !out_size) {
    return C_ORM_ERROR_MEMORY;
  }
  if (index < 0 || index >= stmt->data->result_col_count) {
    return C_ORM_ERROR_VALIDATION;
  }
  if (stmt->data->result_is_null[index]) {
    *out_val = NULL;
    *out_size = 0;
  } else {
    *out_val = stmt->data->result_buffers[index];
    *out_size = (size_t)stmt->data->result_length[index];
  }
  return C_ORM_OK;
}

/** @brief Checks if a column in a query result is NULL. */
static c_orm_error_t mysql_drv_is_null(c_orm_query_t *query, int index,
                                       int *out_is_null) {
  mysql_stmt_t *stmt = (mysql_stmt_t *)query;
  if (!stmt || !stmt->data || !stmt->data->has_result || !out_is_null) {
    return C_ORM_ERROR_MEMORY;
  }
  if (index < 0 || index >= stmt->data->result_col_count) {
    return C_ORM_ERROR_VALIDATION;
  }
  *out_is_null = stmt->data->result_is_null[index] ? 1 : 0;
  return C_ORM_OK;
}

/** @brief Finalizes a query and releases its resources. */
static c_orm_error_t mysql_drv_finalize(c_orm_query_t *query) {
  mysql_stmt_t *stmt = (mysql_stmt_t *)query;
  int i;

  if (!stmt) {
    return C_ORM_OK;
  }

  if (stmt->data) {
    if (stmt->data->stmt) {
      mysql_stmt_close(stmt->data->stmt);
    }
    if (stmt->data->param_count > 0) {
      for (i = 0; i < stmt->data->param_count; i++) {
        if (stmt->data->bind_params[i].buffer) {
          (void)c_orm_system_free(stmt->data->bind_params[i].buffer);
        }
      }
      (void)c_orm_system_free(stmt->data->bind_params);
      (void)c_orm_system_free(stmt->data->param_is_null);
    }
    if (stmt->data->result_col_count > 0) {
      for (i = 0; i < stmt->data->result_col_count; i++) {
        if (stmt->data->result_buffers) {
          (void)c_orm_system_free(stmt->data->result_buffers[i]);
        }
        if (stmt->data->result_col_names) {
          (void)c_orm_system_free(stmt->data->result_col_names[i]);
        }
      }
      (void)c_orm_system_free(stmt->data->result_buffers);
      (void)c_orm_system_free(stmt->data->result_col_names);
      (void)c_orm_system_free(stmt->data->result_binds);
      (void)c_orm_system_free(stmt->data->result_is_null);
      (void)c_orm_system_free(stmt->data->result_length);
    }
    (void)c_orm_system_free(stmt->data);
  }
  return c_orm_system_free(stmt);
}

/** @brief Resets a query to be executed again. */
static c_orm_error_t mysql_drv_reset(c_orm_query_t *query) {
  mysql_stmt_t *stmt = (mysql_stmt_t *)query;
  int i;

  if (!stmt || !stmt->data) {
    return C_ORM_ERROR_MEMORY;
  }

  if (stmt->data->has_result) {
    mysql_stmt_free_result(stmt->data->stmt);
    if (stmt->data->result_col_count > 0) {
      for (i = 0; i < stmt->data->result_col_count; i++) {
        if (stmt->data->result_buffers) {
          (void)c_orm_system_free(stmt->data->result_buffers[i]);
        }
        if (stmt->data->result_col_names) {
          (void)c_orm_system_free(stmt->data->result_col_names[i]);
        }
      }
      (void)c_orm_system_free(stmt->data->result_buffers);
      (void)c_orm_system_free(stmt->data->result_col_names);
      (void)c_orm_system_free(stmt->data->result_binds);
      (void)c_orm_system_free(stmt->data->result_is_null);
      (void)c_orm_system_free(stmt->data->result_length);
      stmt->data->result_buffers = NULL;
      stmt->data->result_col_names = NULL;
      stmt->data->result_binds = NULL;
      stmt->data->result_is_null = NULL;
      stmt->data->result_length = NULL;
      stmt->data->result_col_count = 0;
    }
    stmt->data->has_result = 0;
  }
  return C_ORM_OK;
}

/** @brief Retrieves the last error message from the database. */
static c_orm_error_t mysql_drv_get_last_error(c_orm_db_t *db,
                                              const char **out_message) {
  struct mysql_db_data *data;

  if (!out_message) {
    return C_ORM_ERROR_UNKNOWN;
  }
  if (!db || !db->driver_data) {
    *out_message = "Invalid DB object";
    return C_ORM_ERROR_UNKNOWN;
  }

  data = (struct mysql_db_data *)db->driver_data;
  *out_message = data->last_error;
  return C_ORM_OK;
}

/** @brief Retrieves the last stack trace from the database. */
static c_orm_error_t mysql_drv_get_last_trace(c_orm_db_t *db,
                                              const char **out_trace) {
  (void)db;
  if (out_trace) {
    *out_trace = "MySQL stack traces require extended trace plugins lacking in "
                 "cdd-c struct wrappers. Database trace missing natively.";
  }
  return C_ORM_OK;
}

/** @brief Retrieves the last inserted row ID. */
static c_orm_error_t mysql_drv_get_last_insert_rowid(c_orm_db_t *db,
                                                     int64_t *out_id) {
  struct mysql_db_data *data;

  if (!db || !db->driver_data || !out_id) {
    return C_ORM_ERROR_MEMORY;
  }

  data = (struct mysql_db_data *)db->driver_data;
  *out_id = (int64_t)mysql_insert_id(data->conn);
  return C_ORM_OK;
}

/** @brief Gets result column count for a prepared MySQL statement. */
static c_orm_error_t mysql_drv_get_column_count(c_orm_query_t *query,
                                                int *out_count) {
  mysql_stmt_t *stmt = (mysql_stmt_t *)query;
  if (!stmt || !stmt->data || !out_count) {
    return C_ORM_ERROR_MEMORY;
  }
  if (!stmt->data->has_result && stmt->data->stmt) {
    MYSQL_RES *meta = mysql_stmt_result_metadata(stmt->data->stmt);
    if (!meta) {
      *out_count = 0;
      return C_ORM_OK;
    }
    *out_count = (int)mysql_num_fields(meta);
    mysql_free_result(meta);
    return C_ORM_OK;
  }
  *out_count = stmt->data->result_col_count;
  return C_ORM_OK;
}

/** @brief Gets result column name for a prepared MySQL statement. */
static c_orm_error_t mysql_drv_get_column_name(c_orm_query_t *query, int index,
                                               const char **out_name) {
  mysql_stmt_t *stmt = (mysql_stmt_t *)query;
  if (!stmt || !stmt->data || !out_name) {
    return C_ORM_ERROR_MEMORY;
  }
  if (index < 0 || index >= stmt->data->result_col_count ||
      !stmt->data->result_col_names) {
    return C_ORM_ERROR_VALIDATION;
  }
  *out_name = stmt->data->result_col_names[index];
  return C_ORM_OK;
}

/**
 * @brief Binds an exact fixed-point decimal to a MySQL prepared statement.
 * @param query Prepared MySQL statement handle.
 * @param index 1-based parameter index.
 * @param val Decimal value pointer.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t mysql_drv_bind_decimal(c_orm_query_t *query, int index,
                                            const c_orm_decimal_t *val) {
  char buf[64];
  c_orm_error_t rc;
  if (!val) {
    return mysql_drv_bind_null(query, index);
  }
  rc = c_orm_decimal_to_str(val, buf, sizeof(buf), NULL);
  if (rc != C_ORM_OK) {
    return rc;
  }
  return mysql_drv_bind_string(query, index, buf);
}

/**
 * @brief Retrieves an exact fixed-point decimal from a MySQL result column.
 * @param query Prepared MySQL statement handle.
 * @param index 0-based column index.
 * @param out_val Output pointer receiving parsed decimal.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t mysql_drv_get_decimal(c_orm_query_t *query, int index,
                                           c_orm_decimal_t *out_val) {
  const char *s = NULL;
  c_orm_error_t rc;
  if (!out_val) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  rc = mysql_drv_get_string(query, index, &s);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (!s) {
    return c_orm_decimal_zero(0, out_val);
  }
  return c_orm_decimal_from_str(s, out_val);
}

/** @brief The MySQL driver vtable. */
static c_orm_driver_vtable_t mysql_vtable = {mysql_drv_connect,
                                             mysql_drv_disconnect,
                                             mysql_drv_prepare,
                                             mysql_drv_bind_int32,
                                             mysql_drv_bind_int64,
                                             mysql_drv_bind_double,
                                             mysql_drv_bind_string,
                                             mysql_drv_bind_blob,
                                             mysql_drv_bind_null,
                                             mysql_drv_step,
                                             mysql_drv_get_int32,
                                             mysql_drv_get_int64,
                                             mysql_drv_get_double,
                                             mysql_drv_get_string,
                                             mysql_drv_get_blob,
                                             mysql_drv_is_null,
                                             mysql_drv_finalize,
                                             mysql_drv_reset,
                                             mysql_drv_get_last_error,
                                             mysql_drv_get_last_trace,
                                             mysql_drv_get_last_insert_rowid,
                                             mysql_drv_get_column_count,
                                             mysql_drv_get_column_name,
                                             mysql_drv_bind_decimal,
                                             mysql_drv_get_decimal};

/**
 * @brief Gets the MySQL driver vtable.
 * @param out_vtable Pointer to store the vtable.
 * @return C_ORM_OK on success, non-zero otherwise.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_mysql_get_vtable(const c_orm_driver_vtable_t **out_vtable) {
  if (!out_vtable) {
    return C_ORM_ERROR_UNKNOWN;
  }
  *out_vtable = &mysql_vtable;
  return C_ORM_OK;
}

/**
 * @brief Connects to a MySQL database using a URL.
 * @param url The connection URL.
 * @param out_db Pointer to store the created database connection.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_mysql_connect(const char *url,
                                               c_orm_db_t **out_db) {
  return mysql_drv_connect(url, out_db);
}

#else

/**
 * @brief Gets the MySQL driver vtable (stub).
 * @param out_vtable Pointer to store the vtable.
 * @return C_ORM_ERROR_NOT_IMPLEMENTED when MySQL is disabled.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_mysql_get_vtable(const c_orm_driver_vtable_t **out_vtable) {
  if (out_vtable) {
    *out_vtable = NULL;
  }
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}

/**
 * @brief Connects to a MySQL database (stub).
 * @param url The connection URL.
 * @param out_db Pointer to store the connection.
 * @return C_ORM_ERROR_NOT_IMPLEMENTED when MySQL is disabled.
 */
C_ORM_EXPORT c_orm_error_t c_orm_mysql_connect(const char *url,
                                               c_orm_db_t **out_db) {
  (void)url;
  (void)out_db;
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}

#endif
