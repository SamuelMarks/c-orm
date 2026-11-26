/**
 * @file c_orm_mysql.c
 * @brief MySQL/MariaDB driver implementation for c-orm.
 */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_mysql.h"
#include "c_orm_api.h"
#include "c_orm_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef C_ORM_ENABLE_MYSQL
#if !defined(_WIN32) && !defined(_WIN64)
#include <sys/time.h>
#endif
#if !defined(C_ORM_NO_THREADS) && !defined(_WIN32)
#include <pthread.h>
#endif
#ifdef C_ORM_MYSQL_DYNAMIC
#if defined(_WIN32)
#if !defined(C_ORM_NO_THREADS) && defined(_MSC_VER)
#include <intrin.h>
#endif
#else
#include <dlfcn.h>
#endif
#else
#include <mysql.h>
#endif
#endif
/* clang-format on */

#ifdef C_ORM_ENABLE_MYSQL
#if defined(_WIN32) || defined(_WIN64)
/**
 * @brief Win32 64-bit performance counter union without including windows.h.
 * @var u Split 32-bit low/high halves.
 * @var QuadPart Signed 64-bit tick count.
 */
typedef union _MY_LARGE_INTEGER {
  /** @brief Split 32-bit low and high halves. */
  struct {
    /** @brief Low-order 32 bits. */
    unsigned long LowPart;
    /** @brief High-order 32 bits. */
    long HighPart;
  } u; /**< @brief Split 32-bit low and high halves. */
#if defined(_MSC_VER)
  /** @brief Signed 64-bit integer tick value. */
  __int64 QuadPart;
#else
  /** @brief Signed 64-bit integer tick value. */
  c_orm_int64_t QuadPart;
#endif
} MY_LARGE_INTEGER;

/**
 * @brief Retrieves the current value of the Win32 performance counter.
 * @param lpPerformanceCount Output pointer receiving counter ticks.
 * @return Non-zero on success, or 0 on failure.
 */
__declspec(dllimport) int __stdcall QueryPerformanceCounter(
    MY_LARGE_INTEGER *lpPerformanceCount);

/**
 * @brief Retrieves the frequency of the Win32 performance counter.
 * @param lpFrequency Output pointer receiving ticks per second.
 * @return Non-zero on success, or 0 on failure.
 */
__declspec(dllimport) int __stdcall QueryPerformanceFrequency(
    MY_LARGE_INTEGER *lpFrequency);
#endif

#ifdef C_ORM_MYSQL_DYNAMIC
#if defined(_WIN32)
/**
 * @brief Loads the specified Win32 dynamic-link library into the process.
 * @param lpLibFileName Null-terminated library file name.
 * @return Module handle on success, or NULL on failure.
 */
__declspec(dllimport) void *__stdcall LoadLibraryA(const char *lpLibFileName);

/**
 * @brief Retrieves the address of an exported function from a Win32 DLL.
 * @param hModule Handle to the loaded DLL module.
 * @param lpProcName Null-terminated exported symbol name.
 * @return Symbol address on success, or NULL on failure.
 */
__declspec(dllimport) void *__stdcall GetProcAddress(void *hModule,
                                                     const char *lpProcName);
#endif
typedef struct st_mysql MYSQL;
typedef struct st_mysql_stmt MYSQL_STMT;
typedef struct st_mysql_res MYSQL_RES;

/**
 * @brief Minimal MySQL/MariaDB result column metadata descriptor.
 */
typedef struct st_mysql_field {
  /** @brief Null-terminated result column identifier. */
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

/**
 * @brief ABI-compatible MySQL/MariaDB parameter and result bind structure.
 */
typedef struct st_mysql_bind {
  /** @brief Pointer to actual byte length output/input variable. */
  unsigned long *length;
  /** @brief Pointer to SQL NULL indicator flag. */
  my_bool *is_null;
  /** @brief Data buffer pointer for bound parameter or result column. */
  void *buffer;
  /** @brief Pointer to truncation/conversion error flag. */
  my_bool *error;
  /** @brief Internal client-library current row byte pointer. */
  unsigned char *row_ptr;
  /** @brief Internal client-library parameter serialization hook. */
  void (*store_param_func)(void *net, struct st_mysql_bind *param);
  /** @brief Internal client-library column fetch conversion hook. */
  void (*fetch_result)(struct st_mysql_bind *, void *, unsigned char **row);
  /** @brief Internal client-library column skip hook. */
  void (*skip_result)(struct st_mysql_bind *, void *, unsigned char **row);
  /** @brief Byte capacity of buffer. */
  unsigned long buffer_length;
  /** @brief Byte offset for incremental column fetching. */
  unsigned long offset;
  /** @brief Internal embedded length value storage. */
  unsigned long length_value;
  /** @brief Internal parameter ordinal index. */
  unsigned int param_number;
  /** @brief Internal wire packet pack length. */
  unsigned int pack_length;
  /** @brief MySQL field/buffer wire type enum. */
  enum enum_field_types buffer_type;
  /** @brief Non-zero when integer buffer is unsigned. */
  my_bool is_unsigned;
  /** @brief Non-zero when parameter data was streamed via send_long_data. */
  my_bool long_data_used;
  /** @brief Internal embedded NULL flag value storage. */
  my_bool is_null_value;
  /** @brief Reserved extension pointer for connector internals. */
  void *extension;
} MYSQL_BIND;
#define MYSQL_NO_DATA 100
#define MYSQL_DATA_TRUNCATED 101
#endif
#endif

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
  c_orm_error_t rc;

  if (!url || !out_info) {
    return C_ORM_ERROR_NULL_POINTER;
  }

  memset(out_info, 0, sizeof(*out_info));
  rc = C_ORM_STRCPY(out_info->host, sizeof(out_info->host), "127.0.0.1");
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = C_ORM_STRCPY(out_info->user, sizeof(out_info->user), "root");
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = C_ORM_STRCPY(out_info->dbname, sizeof(out_info->dbname), "test");
  if (rc != C_ORM_OK) {
    return rc;
  }

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
static c_orm_uint64_t (*dyn_mysql_insert_id)(MYSQL *mysql);
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
    pthread_mutex_unlock(&mysql_dyn_mutex);
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
    pthread_mutex_unlock(&mysql_dyn_mutex);
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
    pthread_mutex_unlock(&mysql_dyn_mutex);
#endif
#endif
    return C_ORM_ERROR_CONNECTION;
  }
  loaded = 1;
#if !defined(C_ORM_NO_THREADS)
#if defined(_WIN32)
  mysql_dyn_lock = 0;
#else
  pthread_mutex_unlock(&mysql_dyn_mutex);
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
  /** @brief Last formatted diagnostic trace buffer. */
  char last_trace[600];
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
C_ORM_EXPORT c_orm_error_t c_orm_mysql_connect(const char *url,
                                               c_orm_db_t **out_db) {
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
  rc = c_orm_system_calloc(1, sizeof(c_orm_db_t), (void **)&db);
  if (rc != C_ORM_OK) {
    return rc;
  }

  data = NULL;
  rc = c_orm_system_calloc(1, sizeof(struct mysql_db_data), (void **)&data);
  if (rc != C_ORM_OK) {
    c_orm_system_free(db);
    return rc;
  }

  data->conn = mysql_init(NULL);
  if (!data->conn) {
    c_orm_system_free(data);
    c_orm_system_free(db);
    return C_ORM_ERROR_CONNECTION;
  }

  if (info.use_ssl) {
#ifdef C_ORM_MYSQL_DYNAMIC
    if (dyn_mysql_ssl_set) {
      mysql_ssl_set(data->conn, NULL, NULL, NULL, NULL, NULL);
    }
#else
    mysql_ssl_set(data->conn, NULL, NULL, NULL, NULL, NULL);
#endif
  }

  if (!mysql_real_connect(data->conn, info.host, info.user, info.password,
                          info.dbname, info.port,
                          info.unix_socket[0] ? info.unix_socket : NULL, 0)) {
    mysql_close(data->conn);
    c_orm_system_free(data);
    c_orm_system_free(db);
    return C_ORM_ERROR_CONNECTION;
  }

  rc = c_orm_mysql_get_vtable(&db->vtable);
  if (rc != C_ORM_OK) {
    mysql_close(data->conn);
    c_orm_system_free(data);
    c_orm_system_free(db);
    return rc;
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
  c_orm_error_t rc;
  c_orm_error_t frc;

  if (!db) {
    return C_ORM_OK;
  }

  rc = c_orm_disable_statement_caching(db);
  if (rc != C_ORM_OK) {
    data = (struct mysql_db_data *)db->driver_data;
    if (data) {
      if (data->conn) {
        mysql_close(data->conn);
      }
      c_orm_system_free(data);
    }
    c_orm_system_free(db);
    return rc;
  }

  data = (struct mysql_db_data *)db->driver_data;
  if (data) {
    if (data->conn) {
      mysql_close(data->conn);
    }
    frc = c_orm_system_free(data);
    if (frc != C_ORM_OK) {
      c_orm_system_free(db);
      return frc;
    }
  }
  frc = c_orm_system_free(db);
  if (frc != C_ORM_OK) {
    return frc;
  }
  return C_ORM_OK;
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
  rc = c_orm_system_calloc(1, sizeof(mysql_stmt_t), (void **)&stmt_handle);
  if (rc != C_ORM_OK) {
    return rc;
  }

  q_data = NULL;
  rc =
      c_orm_system_calloc(1, sizeof(struct mysql_query_data), (void **)&q_data);
  if (rc != C_ORM_OK) {
    c_orm_system_free(stmt_handle);
    return rc;
  }

  q_data->stmt = mysql_stmt_init(db_data->conn);
  if (!q_data->stmt) {
    c_orm_error_t err_rc = set_error(db, mysql_error(db_data->conn));
    if (err_rc != C_ORM_OK) {
      c_orm_system_free(q_data);
      c_orm_system_free(stmt_handle);
      return err_rc;
    }
    c_orm_system_free(q_data);
    c_orm_system_free(stmt_handle);
    return C_ORM_ERROR_SQL;
  }

  if (mysql_stmt_prepare(q_data->stmt, sql, (unsigned long)strlen(sql))) {
    c_orm_error_t err_rc = set_error(db, mysql_stmt_error(q_data->stmt));
    if (err_rc != C_ORM_OK) {
      mysql_stmt_close(q_data->stmt);
      c_orm_system_free(q_data);
      c_orm_system_free(stmt_handle);
      return err_rc;
    }
    mysql_stmt_close(q_data->stmt);
    c_orm_system_free(q_data);
    c_orm_system_free(stmt_handle);
    return C_ORM_ERROR_SQL;
  }

  q_data->db = db;
  q_data->param_count = (int)mysql_stmt_param_count(q_data->stmt);
  if (q_data->param_count > 0) {
    rc = c_orm_system_calloc((size_t)q_data->param_count, sizeof(MYSQL_BIND),
                             (void **)&q_data->bind_params);
    if (rc != C_ORM_OK) {
      goto mysql_prep_oom;
    }
    rc = c_orm_system_calloc((size_t)q_data->param_count, sizeof(my_bool),
                             (void **)&q_data->param_is_null);
    if (rc != C_ORM_OK) {
    mysql_prep_oom:
      if (q_data->bind_params)
        c_orm_system_free(q_data->bind_params);
      if (q_data->param_is_null)
        c_orm_system_free(q_data->param_is_null);
      mysql_stmt_close(q_data->stmt);
      c_orm_system_free(q_data);
      c_orm_system_free(stmt_handle);
      return rc;
    }
  }

  stmt_handle->data = q_data;
  *out_query = (c_orm_query_t *)stmt_handle;
  return C_ORM_OK;
}

/**
 * @brief Binds an SQL NULL value to a MySQL prepared statement parameter.
 * @param query Prepared MySQL statement handle.
 * @param index 1-based parameter ordinal.
 * @return C_ORM_OK on success, or C_ORM_ERROR_BIND on invalid index/handle.
 */
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

/**
 * @brief Binds a 32-bit signed integer to a MySQL prepared statement parameter.
 * @param query Prepared MySQL statement handle.
 * @param index 1-based parameter ordinal.
 * @param val 32-bit integer value to bind.
 * @return C_ORM_OK on success, or an error code on failure.
 */
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
    c_orm_error_t rc = c_orm_system_malloc(sizeof(int32_t), &b->buffer);
    if (rc != C_ORM_OK) {
      return rc;
    }
  }
  *(int32_t *)b->buffer = val;
  b->is_null = &stmt->data->param_is_null[i];
  *b->is_null = 0;
  return C_ORM_OK;
}

/**
 * @brief Binds a 64-bit signed integer to a MySQL prepared statement parameter.
 * @param query Prepared MySQL statement handle.
 * @param index 1-based parameter ordinal.
 * @param val 64-bit integer value to bind.
 * @return C_ORM_OK on success, or an error code on failure.
 */
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
    c_orm_error_t rc = c_orm_system_malloc(sizeof(int64_t), &b->buffer);
    if (rc != C_ORM_OK) {
      return rc;
    }
  }
  *(int64_t *)b->buffer = val;
  b->is_null = &stmt->data->param_is_null[i];
  *b->is_null = 0;
  return C_ORM_OK;
}

/**
 * @brief Binds a double-precision float to a MySQL prepared statement
 * parameter.
 * @param query Prepared MySQL statement handle.
 * @param index 1-based parameter ordinal.
 * @param val Double-precision value to bind.
 * @return C_ORM_OK on success, or an error code on failure.
 */
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
    c_orm_error_t rc = c_orm_system_malloc(sizeof(double), &b->buffer);
    if (rc != C_ORM_OK) {
      return rc;
    }
  }
  *(double *)b->buffer = val;
  b->is_null = &stmt->data->param_is_null[i];
  *b->is_null = 0;
  return C_ORM_OK;
}

/**
 * @brief Binds a UTF-8 string to a MySQL parameter (routes NULL to bind_null).
 * @param query Prepared MySQL statement handle.
 * @param index 1-based parameter ordinal.
 * @param val Null-terminated string to bind, or NULL for SQL NULL.
 * @return C_ORM_OK on success, or an error code on failure.
 */
static c_orm_error_t mysql_drv_bind_string(c_orm_query_t *query, int index,
                                           const char *val) {
  mysql_stmt_t *stmt = (mysql_stmt_t *)query;
  int i;
  size_t len;
  MYSQL_BIND *b;
  c_orm_error_t rc;

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
    rc = c_orm_system_free(b->buffer);
    b->buffer = NULL;
    if (rc != C_ORM_OK) {
      return rc;
    }
  }

  rc = c_orm_system_malloc(len + 1, &b->buffer);
  if (rc != C_ORM_OK) {
    return rc;
  }

  rc = C_ORM_STRCPY((char *)b->buffer, len + 1, val);
  if (rc != C_ORM_OK) {
    return rc;
  }
  b->buffer_length = (unsigned long)len;
  b->is_null = &stmt->data->param_is_null[i];
  *b->is_null = 0;
  return C_ORM_OK;
}

/**
 * @brief Binds a binary BLOB buffer to a MySQL prepared statement parameter.
 * @param query Prepared MySQL statement handle.
 * @param index 1-based parameter ordinal.
 * @param val Pointer to raw byte buffer, or NULL when size is 0.
 * @param size Byte length of val.
 * @return C_ORM_OK on success, or an error code on failure.
 */
static c_orm_error_t mysql_drv_bind_blob(c_orm_query_t *query, int index,
                                         const void *val, size_t size) {
  mysql_stmt_t *stmt = (mysql_stmt_t *)query;
  int i;
  MYSQL_BIND *b;
  c_orm_error_t rc;

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
    rc = c_orm_system_free(b->buffer);
    b->buffer = NULL;
    if (rc != C_ORM_OK) {
      return rc;
    }
  }

  rc = c_orm_system_malloc(size > 0 ? size : 1, &b->buffer);
  if (rc != C_ORM_OK) {
    return rc;
  }

  if (size > 0 && val) {
    memcpy(b->buffer, val, size);
  }
  b->buffer_length = (unsigned long)size;
  b->is_null = &stmt->data->param_is_null[i];
  *b->is_null = 0;
  return C_ORM_OK;
}

/**
 * @brief Allocates and binds result buffers and column metadata for a query.
 * @param q_data Active MySQL query execution state.
 * @return C_ORM_OK on success, or an allocation/bind error code.
 */
static c_orm_error_t init_result_binds(struct mysql_query_data *q_data) {
  MYSQL_RES *meta;
  int i;
  c_orm_error_t rc;

  meta = mysql_stmt_result_metadata(q_data->stmt);
  if (!meta) {
    q_data->result_col_count = 0;
    return C_ORM_OK;
  }

  q_data->result_col_count = (int)mysql_num_fields(meta);

  if (q_data->result_col_count > 0) {
    rc =
        c_orm_system_calloc((size_t)q_data->result_col_count,
                            sizeof(MYSQL_BIND), (void **)&q_data->result_binds);
    if (rc != C_ORM_OK) {
      mysql_free_result(meta);
      return rc;
    }
    rc = c_orm_system_calloc((size_t)q_data->result_col_count, sizeof(my_bool),
                             (void **)&q_data->result_is_null);
    if (rc != C_ORM_OK) {
      mysql_free_result(meta);
      return rc;
    }
    rc = c_orm_system_calloc((size_t)q_data->result_col_count,
                             sizeof(unsigned long),
                             (void **)&q_data->result_length);
    if (rc != C_ORM_OK) {
      mysql_free_result(meta);
      return rc;
    }
    rc = c_orm_system_calloc((size_t)q_data->result_col_count, sizeof(char *),
                             (void **)&q_data->result_buffers);
    if (rc != C_ORM_OK) {
      mysql_free_result(meta);
      return rc;
    }
    rc = c_orm_system_calloc((size_t)q_data->result_col_count, sizeof(char *),
                             (void **)&q_data->result_col_names);
    if (rc != C_ORM_OK) {
      mysql_free_result(meta);
      return rc;
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
        c_orm_error_t drc =
            c_orm_strdup(fld->name, &q_data->result_col_names[i]);
        if (drc != C_ORM_OK) {
          mysql_free_result(meta);
          return drc;
        }
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

/**
 * @brief Executes or fetches the next row of a prepared MySQL statement.
 * @param query Prepared MySQL statement handle.
 * @param out_has_row Output flag set to 1 if a row was fetched, or 0 if done.
 * @return C_ORM_OK on success, or C_ORM_ERROR_STEP on execution/fetch failure.
 */
static c_orm_error_t mysql_drv_step(c_orm_query_t *query, int *out_has_row) {
  mysql_stmt_t *stmt = (mysql_stmt_t *)query;
  struct mysql_query_data *q_data;
  double elapsed = 0.0;
#if defined(_WIN32) || defined(_WIN64)
  MY_LARGE_INTEGER start_t;
  MY_LARGE_INTEGER end_t;
  MY_LARGE_INTEGER freq;
  start_t.QuadPart = 0;
  end_t.QuadPart = 0;
  freq.QuadPart = 0;
#else
  struct timeval start_t;
  struct timeval end_t;
  start_t.tv_sec = 0;
  start_t.tv_usec = 0;
  end_t.tv_sec = 0;
  end_t.tv_usec = 0;
#endif

  if (!stmt || !stmt->data || !out_has_row) {
    return C_ORM_ERROR_STEP;
  }
  q_data = stmt->data;

  if (!q_data->has_result) {
    if (q_data->db->slow_query_threshold_ms > 0) {
#if defined(_WIN32) || defined(_WIN64)
      QueryPerformanceFrequency(&freq);
      QueryPerformanceCounter(&start_t);
#else
      gettimeofday(&start_t, NULL);
#endif
    }

    if (q_data->param_count > 0) {
      if (mysql_stmt_bind_param(q_data->stmt, q_data->bind_params)) {
        c_orm_error_t err_rc =
            set_error(q_data->db, mysql_stmt_error(q_data->stmt));
        if (err_rc != C_ORM_OK) {
          return err_rc;
        }
        return C_ORM_ERROR_STEP;
      }
    }

    if (mysql_stmt_execute(q_data->stmt)) {
      c_orm_error_t err_rc =
          set_error(q_data->db, mysql_stmt_error(q_data->stmt));
      if (err_rc != C_ORM_OK) {
        return err_rc;
      }
      return C_ORM_ERROR_STEP;
    }

    if (q_data->db->slow_query_threshold_ms > 0) {
#if defined(_WIN32) || defined(_WIN64)
      QueryPerformanceCounter(&end_t);
      elapsed = (double)(end_t.QuadPart - start_t.QuadPart) * 1000.0 /
                (double)freq.QuadPart;
#else
      gettimeofday(&end_t, NULL);
      elapsed = (double)(end_t.tv_sec - start_t.tv_sec) * 1000.0 +
                (double)(end_t.tv_usec - start_t.tv_usec) / 1000.0;
#endif
      if (elapsed >= (double)q_data->db->slow_query_threshold_ms) {
        q_data->db->telemetry.slow_queries_logged++;
        if (q_data->db->log_cb) {
          char log_msg[512];
          c_orm_error_t lrc =
              C_ORM_SPRINTF(log_msg, sizeof(log_msg),
                            "SLOW QUERY (%.2fms): mysql_stmt", elapsed);
          if (lrc != C_ORM_OK) {
            return lrc;
          }
          lrc = q_data->db->log_cb(log_msg, q_data->db->log_user_data);
          if (lrc != C_ORM_OK) {
            return lrc;
          }
        }
      }
    }

    {
      c_orm_error_t irc = init_result_binds(q_data);
      if (irc != C_ORM_OK) {
        c_orm_error_t err_rc =
            set_error(q_data->db, mysql_stmt_error(q_data->stmt));
        if (err_rc != C_ORM_OK) {
          return err_rc;
        }
        return irc;
      }
    }

    if (q_data->result_col_count > 0) {
      if (mysql_stmt_store_result(q_data->stmt)) {
        c_orm_error_t err_rc =
            set_error(q_data->db, mysql_stmt_error(q_data->stmt));
        if (err_rc != C_ORM_OK) {
          return err_rc;
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
          c_orm_error_t frc = c_orm_system_free(q_data->result_buffers[col_i]);
          q_data->result_buffers[col_i] = NULL;
          if (frc != C_ORM_OK) {
            return frc;
          }
        }
        if (!q_data->result_is_null[col_i]) {
          unsigned long len = q_data->result_length[col_i];
          c_orm_error_t mrc = c_orm_system_malloc(
              len + 1, (void **)&q_data->result_buffers[col_i]);
          if (mrc != C_ORM_OK) {
            return mrc;
          }
          q_data->result_binds[col_i].buffer = q_data->result_buffers[col_i];
          q_data->result_binds[col_i].buffer_length = len + 1;
          if (mysql_stmt_fetch_column(q_data->stmt,
                                      &q_data->result_binds[col_i],
                                      (unsigned int)col_i, 0) != 0) {
            c_orm_error_t err_rc =
                set_error(q_data->db, mysql_stmt_error(q_data->stmt));
            if (err_rc != C_ORM_OK) {
              return err_rc;
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

  {
    c_orm_error_t err_rc =
        set_error(q_data->db, mysql_stmt_error(q_data->stmt));
    if (err_rc != C_ORM_OK) {
      return err_rc;
    }
  }
  return C_ORM_ERROR_STEP;
}

/**
 * @brief Retrieves a 32-bit signed integer from the current MySQL result row.
 * @param query Prepared MySQL statement handle.
 * @param index 0-based result column ordinal.
 * @param out_val Output pointer receiving the 32-bit integer value.
 * @return C_ORM_OK on success, or an error code on invalid state/column.
 */
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

/**
 * @brief Retrieves a 64-bit signed integer from the current MySQL result row.
 * @param query Prepared MySQL statement handle.
 * @param index 0-based result column ordinal.
 * @param out_val Output pointer receiving the 64-bit integer value.
 * @return C_ORM_OK on success, or an error code on invalid state/column.
 */
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
    c_orm_error_t prc =
        c_orm_strtoint64(stmt->data->result_buffers[index], NULL, 10, out_val);
    if (prc != C_ORM_OK) {
      *out_val = 0;
      return prc;
    }
  }
  return C_ORM_OK;
}

/**
 * @brief Retrieves a double-precision float from the current MySQL result row.
 * @param query Prepared MySQL statement handle.
 * @param index 0-based result column ordinal.
 * @param out_val Output pointer receiving the double value.
 * @return C_ORM_OK on success, or an error code on invalid state/column.
 */
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

/**
 * @brief Retrieves a UTF-8 string pointer from the current MySQL result row.
 * @param query Prepared MySQL statement handle.
 * @param index 0-based result column ordinal.
 * @param out_val Output pointer receiving the string pointer (or NULL).
 * @return C_ORM_OK on success, or an error code on invalid state/column.
 */
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

/**
 * @brief Retrieves a binary BLOB slice from the current MySQL result row.
 * @param query Prepared MySQL statement handle.
 * @param index 0-based result column ordinal.
 * @param out_val Output pointer receiving the raw buffer pointer (or NULL).
 * @param out_size Output pointer receiving the byte length.
 * @return C_ORM_OK on success, or an error code on invalid state/column.
 */
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

/**
 * @brief Checks whether a column in the current MySQL result row is SQL NULL.
 * @param query Prepared MySQL statement handle.
 * @param index 0-based result column ordinal.
 * @param out_is_null Output flag set to 1 if NULL, or 0 otherwise.
 * @return C_ORM_OK on success, or an error code on invalid state/column.
 */
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

/**
 * @brief Finalizes a MySQL prepared statement and frees its buffers.
 * @param query Prepared MySQL statement handle to release.
 * @return C_ORM_OK on success, or the first encountered allocator error.
 */
static c_orm_error_t mysql_drv_finalize(c_orm_query_t *query) {
  mysql_stmt_t *stmt = (mysql_stmt_t *)query;
  c_orm_error_t rc = C_ORM_OK;
  c_orm_error_t frc;
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
          frc = c_orm_system_free(stmt->data->bind_params[i].buffer);
          if (frc != C_ORM_OK && rc == C_ORM_OK) {
            rc = frc;
          }
        }
      }
      frc = c_orm_system_free(stmt->data->bind_params);
      if (frc != C_ORM_OK && rc == C_ORM_OK) {
        rc = frc;
      }
      frc = c_orm_system_free(stmt->data->param_is_null);
      if (frc != C_ORM_OK && rc == C_ORM_OK) {
        rc = frc;
      }
    }
    if (stmt->data->result_col_count > 0) {
      for (i = 0; i < stmt->data->result_col_count; i++) {
        if (stmt->data->result_buffers) {
          frc = c_orm_system_free(stmt->data->result_buffers[i]);
          if (frc != C_ORM_OK && rc == C_ORM_OK) {
            rc = frc;
          }
        }
        if (stmt->data->result_col_names) {
          frc = c_orm_system_free(stmt->data->result_col_names[i]);
          if (frc != C_ORM_OK && rc == C_ORM_OK) {
            rc = frc;
          }
        }
      }
      frc = c_orm_system_free(stmt->data->result_buffers);
      if (frc != C_ORM_OK && rc == C_ORM_OK) {
        rc = frc;
      }
      frc = c_orm_system_free(stmt->data->result_col_names);
      if (frc != C_ORM_OK && rc == C_ORM_OK) {
        rc = frc;
      }
      frc = c_orm_system_free(stmt->data->result_binds);
      if (frc != C_ORM_OK && rc == C_ORM_OK) {
        rc = frc;
      }
      frc = c_orm_system_free(stmt->data->result_is_null);
      if (frc != C_ORM_OK && rc == C_ORM_OK) {
        rc = frc;
      }
      frc = c_orm_system_free(stmt->data->result_length);
      if (frc != C_ORM_OK && rc == C_ORM_OK) {
        rc = frc;
      }
    }
    frc = c_orm_system_free(stmt->data);
    if (frc != C_ORM_OK && rc == C_ORM_OK) {
      rc = frc;
    }
  }
  frc = c_orm_system_free(stmt);
  if (frc != C_ORM_OK && rc == C_ORM_OK) {
    rc = frc;
  }
  return rc;
}

/**
 * @brief Resets a prepared MySQL statement so it can be executed again.
 * @param query Prepared MySQL statement handle.
 * @return C_ORM_OK on success, or an allocator error code.
 */
static c_orm_error_t mysql_drv_reset(c_orm_query_t *query) {
  mysql_stmt_t *stmt = (mysql_stmt_t *)query;
  c_orm_error_t rc = C_ORM_OK;
  c_orm_error_t frc;
  int i;

  if (!stmt || !stmt->data) {
    return C_ORM_ERROR_MEMORY;
  }

  if (stmt->data->has_result) {
    mysql_stmt_free_result(stmt->data->stmt);
    if (stmt->data->result_col_count > 0) {
      for (i = 0; i < stmt->data->result_col_count; i++) {
        if (stmt->data->result_buffers) {
          frc = c_orm_system_free(stmt->data->result_buffers[i]);
          if (frc != C_ORM_OK && rc == C_ORM_OK) {
            rc = frc;
          }
        }
        if (stmt->data->result_col_names) {
          frc = c_orm_system_free(stmt->data->result_col_names[i]);
          if (frc != C_ORM_OK && rc == C_ORM_OK) {
            rc = frc;
          }
        }
      }
      frc = c_orm_system_free(stmt->data->result_buffers);
      if (frc != C_ORM_OK && rc == C_ORM_OK) {
        rc = frc;
      }
      frc = c_orm_system_free(stmt->data->result_col_names);
      if (frc != C_ORM_OK && rc == C_ORM_OK) {
        rc = frc;
      }
      frc = c_orm_system_free(stmt->data->result_binds);
      if (frc != C_ORM_OK && rc == C_ORM_OK) {
        rc = frc;
      }
      frc = c_orm_system_free(stmt->data->result_is_null);
      if (frc != C_ORM_OK && rc == C_ORM_OK) {
        rc = frc;
      }
      frc = c_orm_system_free(stmt->data->result_length);
      if (frc != C_ORM_OK && rc == C_ORM_OK) {
        rc = frc;
      }
      stmt->data->result_buffers = NULL;
      stmt->data->result_col_names = NULL;
      stmt->data->result_binds = NULL;
      stmt->data->result_is_null = NULL;
      stmt->data->result_length = NULL;
      stmt->data->result_col_count = 0;
    }
    stmt->data->has_result = 0;
  }
  return rc;
}

/**
 * @brief Retrieves the latest MySQL driver error message.
 * @param db Active MySQL database handle.
 * @param out_message Output pointer receiving the null-terminated error string.
 * @return C_ORM_OK on success, or C_ORM_ERROR_UNKNOWN on invalid handle.
 */
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

/**
 * @brief Retrieves a formatted MySQL diagnostic trace string.
 * @param db Active MySQL database handle.
 * @param out_trace Output pointer receiving the diagnostic trace string.
 * @return C_ORM_OK on success, or a formatting error code.
 */
static c_orm_error_t mysql_drv_get_last_trace(c_orm_db_t *db,
                                              const char **out_trace) {
  if (out_trace) {
    if (db && db->driver_data) {
      struct mysql_db_data *data = (struct mysql_db_data *)db->driver_data;
      c_orm_error_t fmt_rc = C_ORM_SPRINTF(
          data->last_trace, sizeof(data->last_trace), "[MySQL] %s",
          data->last_error[0] ? data->last_error : "OK");
      if (fmt_rc != C_ORM_OK) {
        return fmt_rc;
      }
      *out_trace = data->last_trace;
    } else {
      *out_trace = "[MySQL] Invalid DB context";
    }
  }
  return C_ORM_OK;
}

/**
 * @brief Retrieves the last auto-incremented row ID via mysql_insert_id.
 * @param db Active MySQL database handle.
 * @param out_id Output pointer receiving the 64-bit inserted row identifier.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on invalid arguments.
 */
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

/**
 * @brief Queries the result column count for a prepared MySQL statement.
 * @param query Prepared MySQL statement handle.
 * @param out_count Output pointer receiving the column count.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on invalid arguments.
 */
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

/**
 * @brief Queries a result column name for a prepared MySQL statement.
 * @param query Prepared MySQL statement handle.
 * @param index 0-based result column ordinal.
 * @param out_name Output pointer receiving the column name string.
 * @return C_ORM_OK on success, or an error code on invalid state/index.
 */
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
static c_orm_driver_vtable_t mysql_vtable = {c_orm_mysql_connect,
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
  if (out_db) {
    *out_db = NULL;
  }
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}

#endif
