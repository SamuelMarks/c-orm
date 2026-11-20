/**
 * @file c_orm_postgres.c
 * @brief PostgreSQL driver implementation for c-orm.
 */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_postgres.h"
#include "c_orm_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef C_ORM_ENABLE_POSTGRESQL
#if !defined(C_ORM_NO_THREADS) && !defined(_WIN32)
#include <pthread.h>
#endif
#ifdef C_ORM_POSTGRESQL_DYNAMIC
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
typedef struct pg_conn PGconn;
typedef struct pg_result PGresult;
typedef unsigned int Oid;
#define InvalidOid ((Oid)0)
#define CONNECTION_OK 0
#define PGRES_COMMAND_OK 1
#define PGRES_TUPLES_OK 2
#define INV_WRITE 0x00020000
#define INV_READ 0x00040000
#else
#include <libpq-fe.h>
#include <libpq/libpq-fs.h>
#endif
#endif
/* clang-format on */

/**
 * @brief Checks if a character is valid inside a PostgreSQL dollar-quote tag.
 * @param ch Character to inspect.
 * @param first Non-zero if testing the first character of the tag.
 * @return C_ORM_OK if valid tag character, or C_ORM_ERROR_VALIDATION otherwise.
 */
static c_orm_error_t is_dollar_tag_char(char ch, int first) {
  if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_' ||
      ((unsigned char)ch >= 0x80)) {
    return C_ORM_OK;
  }
  if (!first && ch >= '0' && ch <= '9') {
    return C_ORM_OK;
  }
  return C_ORM_ERROR_VALIDATION;
}

/**
 * @brief Checks if `p` (starting at `$`) begins a PostgreSQL dollar-quote
 * delimiter (`$$` or `$tag$`) and outputs its total delimiter byte length.
 * @param p Pointer starting at `$`.
 * @param out_delim_len Output delimiter length (>0 when C_ORM_OK is returned).
 * @return C_ORM_OK if a complete delimiter is matched, or
 * C_ORM_ERROR_NOT_FOUND.
 */
static c_orm_error_t match_dollar_delim(const char *p, size_t *out_delim_len) {
  const char *cur;
  *out_delim_len = 0;
  if (p[1] == '$') {
    *out_delim_len = 2;
    return C_ORM_OK;
  }
  if (is_dollar_tag_char(p[1], 1) != C_ORM_OK) {
    return C_ORM_ERROR_NOT_FOUND;
  }
  cur = p + 2;
  while (*cur) {
    if (*cur == '$') {
      *out_delim_len = (size_t)(cur - p + 1);
      return C_ORM_OK;
    }
    if (is_dollar_tag_char(*cur, 0) != C_ORM_OK) {
      return C_ORM_ERROR_NOT_FOUND;
    }
    cur++;
  }
  return C_ORM_ERROR_NOT_FOUND;
}

C_ORM_EXPORT c_orm_error_t c_orm_postgres_rewrite_query(const char *sql,
                                                        int *out_param_count,
                                                        char **out_new_sql) {
  size_t len;
  size_t alloc_len;
  char *new_sql = NULL;
  const char *p;
  char *q;
  int qmark_count = 0;
  int max_dollar_param = 0;
  char num_buf[32];

  LOG_DEBUG("c_orm_postgres_rewrite_query: entry");
  if (!sql || !out_param_count || !out_new_sql) {
    return C_ORM_ERROR_NULL_POINTER;
  }

  *out_param_count = 0;
  *out_new_sql = NULL;
  len = strlen(sql);
  alloc_len = len;

  for (p = sql; *p; p++) {
    if (*p == '?') {
      alloc_len += 11;
    }
  }

  if (c_orm_system_malloc(alloc_len + 1, (void **)&new_sql) != C_ORM_OK) {
    LOG_DEBUG("c_orm_postgres_rewrite_query: OOM");
    return C_ORM_ERROR_MEMORY;
  }

  p = sql;
  q = new_sql;
  while (*p) {
    /* 1. Single-quoted string literal */
    if (*p == '\x27') {
      *q++ = *p++;
      while (*p) {
        *q++ = *p;
        if (*p == '\x27') {
          p++;
          if (*p == '\x27') {
            *q++ = *p++;
            continue;
          }
          break;
        }
        p++;
      }
      continue;
    }

    /* 2. Double-quoted SQL identifier */
    if (*p == '"') {
      *q++ = *p++;
      while (*p) {
        *q++ = *p;
        if (*p == '"') {
          p++;
          if (*p == '"') {
            *q++ = *p++;
            continue;
          }
          break;
        }
        p++;
      }
      continue;
    }

    /* 3. Line comment -- ... */
    if (p[0] == '-' && p[1] == '-') {
      while (*p && *p != '\x0a') {
        *q++ = *p++;
      }
      continue;
    }

    /* 4. Block comment (supports nested PostgreSQL block comments) */
    if (p[0] == '/' && p[1] == '*') {
      int depth = 1;
      *q++ = *p++;
      *q++ = *p++;
      while (*p && depth > 0) {
        if (p[0] == '/' && p[1] == '*') {
          depth++;
          *q++ = *p++;
          *q++ = *p++;
        } else if (p[0] == '*' && p[1] == '/') {
          depth--;
          *q++ = *p++;
          *q++ = *p++;
        } else {
          *q++ = *p++;
        }
      }
      continue;
    }

    /* 5. Dollar-quoted string literal ($$...$$ or $tag$...$tag$) or $N param */
    if (*p == '$') {
      size_t delim_len = 0;
      if (p[1] >= '1' && p[1] <= '9') {
        int param_num = 0;
        *q++ = *p++;
        while (*p >= '0' && *p <= '9') {
          param_num = param_num * 10 + (*p - '0');
          *q++ = *p++;
        }
        if (param_num > max_dollar_param) {
          max_dollar_param = param_num;
        }
        continue;
      }
      if (match_dollar_delim(p, &delim_len) == C_ORM_OK) {
        const char *delim = p;
        memcpy(q, p, delim_len);
        q += delim_len;
        p += delim_len;
        while (*p) {
          if (*p == '$' && strncmp(p, delim, delim_len) == 0) {
            memcpy(q, p, delim_len);
            q += delim_len;
            p += delim_len;
            break;
          }
          *q++ = *p++;
        }
        continue;
      }
    }

    /* 6. Question-mark placeholder or escaped ?? */
    if (*p == '?') {
      if (p[1] == '?') {
        *q++ = '?';
        p += 2;
        continue;
      }
      qmark_count++;
      if (qmark_count > max_dollar_param) {
        max_dollar_param = qmark_count;
      }
      C_ORM_SPRINTF(num_buf, sizeof(num_buf), "$%d", qmark_count);
      {
        size_t nlen = strlen(num_buf);
        memcpy(q, num_buf, nlen);
        q += nlen;
      }
      p++;
      continue;
    }

    *q++ = *p++;
  }
  *q = '\0';

  *out_param_count = max_dollar_param;
  *out_new_sql = new_sql;
  LOG_DEBUG("c_orm_postgres_rewrite_query: exit");
  return C_ORM_OK;
}

#ifdef C_ORM_ENABLE_POSTGRESQL

#ifdef C_ORM_POSTGRESQL_DYNAMIC
static PGconn *(*dyn_PQconnectdb)(const char *conninfo);
static int (*dyn_PQstatus)(const PGconn *conn);
static char *(*dyn_PQerrorMessage)(const PGconn *conn);
static void (*dyn_PQfinish)(PGconn *conn);
static PGresult *(*dyn_PQprepare)(PGconn *conn, const char *stmtName,
                                  const char *query, int nParams,
                                  const Oid *paramTypes);
static PGresult *(*dyn_PQexecPrepared)(PGconn *conn, const char *stmtName,
                                       int nParams,
                                       const char *const *paramValues,
                                       const int *paramLengths,
                                       const int *paramFormats,
                                       int resultFormat);
static PGresult *(*dyn_PQexec)(PGconn *conn, const char *query);
static int (*dyn_PQresultStatus)(const PGresult *res);
static void (*dyn_PQclear)(PGresult *res);
static int (*dyn_PQntuples)(const PGresult *res);
static int (*dyn_PQnfields)(const PGresult *res);
static char *(*dyn_PQfname)(const PGresult *res, int column_number);
static char *(*dyn_PQgetvalue)(const PGresult *res, int tup_num, int field_num);
static int (*dyn_PQgetlength)(const PGresult *res, int tup_num, int field_num);
static int (*dyn_PQgetisnull)(const PGresult *res, int tup_num, int field_num);
static Oid (*dyn_lo_creat)(PGconn *conn, int mode);
static int (*dyn_lo_open)(PGconn *conn, Oid lobjId, int mode);
static int (*dyn_lo_read)(PGconn *conn, int fd, char *buf, size_t len);
static int (*dyn_lo_write)(PGconn *conn, int fd, const char *buf, size_t len);
static int (*dyn_lo_close)(PGconn *conn, int fd);

#if !defined(C_ORM_NO_THREADS)
#if defined(_WIN32)
static volatile long pg_dyn_lock = 0;
#else
static pthread_mutex_t pg_dyn_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif
#endif

/**
 * @brief Dynamically loads libpq symbols at runtime with thread-safe locking.
 * @return C_ORM_OK on success, C_ORM_ERROR_CONNECTION on failure.
 */
static c_orm_error_t c_orm_load_libpq(void) {
  static int loaded = 0;
  void *h;
#if !defined(C_ORM_NO_THREADS)
#if defined(_MSC_VER)
  while (_InterlockedCompareExchange(&pg_dyn_lock, 1L, 0L) != 0L) {
  }
#elif defined(_WIN32)
  while (__sync_val_compare_and_swap(&pg_dyn_lock, 0L, 1L) != 0L) {
  }
#else
  if (pthread_mutex_lock(&pg_dyn_mutex) != 0) {
    return C_ORM_ERROR_BUSY;
  }
#endif
#endif
  if (loaded) {
#if !defined(C_ORM_NO_THREADS)
#if defined(_WIN32)
    pg_dyn_lock = 0;
#else
    (void)pthread_mutex_unlock(&pg_dyn_mutex);
#endif
#endif
    return C_ORM_OK;
  }
#if defined(_WIN32)
  h = LoadLibraryA("libpq.dll");
#elif defined(__APPLE__)
  h = dlopen("libpq.dylib", RTLD_LAZY);
  if (!h)
    h = dlopen("libpq.5.dylib", RTLD_LAZY);
#else
  h = dlopen("libpq.so", RTLD_LAZY);
  if (!h)
    h = dlopen("libpq.so.5", RTLD_LAZY);
#endif
  if (!h) {
#if !defined(C_ORM_NO_THREADS)
#if defined(_WIN32)
    pg_dyn_lock = 0;
#else
    (void)pthread_mutex_unlock(&pg_dyn_mutex);
#endif
#endif
    return C_ORM_ERROR_CONNECTION;
  }
#if defined(_WIN32)
#define PG_SYM(name, type) *(void **)(&dyn_##name) = GetProcAddress(h, #name)
#else
#define PG_SYM(name, type) *(void **)(&dyn_##name) = dlsym(h, #name)
#endif
  PG_SYM(PQconnectdb, void *);
  PG_SYM(PQstatus, void *);
  PG_SYM(PQerrorMessage, void *);
  PG_SYM(PQfinish, void *);
  PG_SYM(PQprepare, void *);
  PG_SYM(PQexecPrepared, void *);
  PG_SYM(PQexec, void *);
  PG_SYM(PQresultStatus, void *);
  PG_SYM(PQclear, void *);
  PG_SYM(PQntuples, void *);
  PG_SYM(PQnfields, void *);
  PG_SYM(PQfname, void *);
  PG_SYM(PQgetvalue, void *);
  PG_SYM(PQgetlength, void *);
  PG_SYM(PQgetisnull, void *);
  PG_SYM(lo_creat, void *);
  PG_SYM(lo_open, void *);
  PG_SYM(lo_read, void *);
  PG_SYM(lo_write, void *);
  PG_SYM(lo_close, void *);
#undef PG_SYM
  if (!dyn_PQconnectdb || !dyn_PQexec || !dyn_PQfinish || !dyn_PQnfields ||
      !dyn_PQfname) {
#if !defined(C_ORM_NO_THREADS)
#if defined(_WIN32)
    pg_dyn_lock = 0;
#else
    (void)pthread_mutex_unlock(&pg_dyn_mutex);
#endif
#endif
    return C_ORM_ERROR_CONNECTION;
  }
  loaded = 1;
#if !defined(C_ORM_NO_THREADS)
#if defined(_WIN32)
  pg_dyn_lock = 0;
#else
  (void)pthread_mutex_unlock(&pg_dyn_mutex);
#endif
#endif
  return C_ORM_OK;
}
#define PQconnectdb dyn_PQconnectdb
#define PQstatus dyn_PQstatus
#define PQerrorMessage dyn_PQerrorMessage
#define PQfinish dyn_PQfinish
#define PQprepare dyn_PQprepare
#define PQexecPrepared dyn_PQexecPrepared
#define PQexec dyn_PQexec
#define PQresultStatus dyn_PQresultStatus
#define PQclear dyn_PQclear
#define PQntuples dyn_PQntuples
#define PQnfields dyn_PQnfields
#define PQfname dyn_PQfname
#define PQgetvalue dyn_PQgetvalue
#define PQgetlength dyn_PQgetlength
#define PQgetisnull dyn_PQgetisnull
#define lo_creat dyn_lo_creat
#define lo_open dyn_lo_open
#define lo_read dyn_lo_read
#define lo_write dyn_lo_write
#define lo_close dyn_lo_close
#endif /* C_ORM_POSTGRESQL_DYNAMIC */

/** @brief Postgres DB data */
struct postgres_db_data {
  /** @brief libpq connection pointer. */
  PGconn *conn;
  /** @brief Connection-scoped prepared statement sequence counter. */
  unsigned int stmt_counter;
  /** @brief Last formatted error string. */
  char last_error[512];
};

/** @brief Postgres query data */
struct postgres_query_data {
  /** @brief Owning database handle. */
  c_orm_db_t *db;
  /** @brief Server-side prepared statement identifier. */
  char *stmt_name;
  /** @brief Rewritten SQL string. */
  char *query_string;
  /** @brief Parameter count. */
  int param_count;
  /** @brief Bound parameter string/binary buffers. */
  char **param_values;
  /** @brief Bound parameter byte lengths. */
  int *param_lengths;
  /** @brief Bound parameter formats (0=text, 1=binary). */
  int *param_formats;
  /** @brief Active libpq result set. */
  PGresult *res;
  /** @brief Current tuple index. */
  int current_row;
  /** @brief Total tuple count in `res`. */
  int row_count;
};

/** @brief Internal PostgreSQL statement handle wrapper. */
typedef struct postgres_stmt_handle {
  /** @brief Pointer to PostgreSQL query state. */
  struct postgres_query_data *data;
} postgres_stmt_t;

/**
 * @brief Generate connection-scoped unique statement name.
 * @param db_data PostgreSQL connection data.
 * @param buf Output buffer.
 * @param size Buffer capacity.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t generate_stmt_name(struct postgres_db_data *db_data,
                                        char *buf, size_t size) {
  c_orm_error_t rc;
  LOG_DEBUG("generate_stmt_name: entry");
  db_data->stmt_counter++;
  rc = C_ORM_SPRINTF(buf, size, "c_orm_stmt_%u", db_data->stmt_counter);
  LOG_DEBUG("generate_stmt_name: exit");
  return rc;
}

/**
 * @brief Set last error message on the database handle.
 * @param db Database handle.
 * @param msg Optional explicit error string.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t set_error(c_orm_db_t *db, const char *msg) {
  LOG_DEBUG("set_error: entry");
  if (db && db->driver_data) {
    struct postgres_db_data *data = (struct postgres_db_data *)db->driver_data;
    if (msg) {
      size_t len = strlen(msg);
      if (len >= sizeof(data->last_error))
        len = sizeof(data->last_error) - 1;
      memcpy(data->last_error, msg, len);
      data->last_error[len] = '\0';
    } else if (data->conn) {
      const char *pg_err = PQerrorMessage(data->conn);
      size_t len = strlen(pg_err);
      if (len >= sizeof(data->last_error))
        len = sizeof(data->last_error) - 1;
      memcpy(data->last_error, pg_err, len);
      data->last_error[len] = '\0';
    }
  }
  LOG_DEBUG("set_error: exit");
  return C_ORM_OK;
}

/** @brief Postgres connect */
static c_orm_error_t postgres_connect(const char *url, c_orm_db_t **out_db) {
  c_orm_error_t rc;
  c_orm_db_t *db;
  struct postgres_db_data *data;

  LOG_DEBUG("postgres_connect: entry");

  if (!url || !out_db) {
    LOG_DEBUG("postgres_connect: invalid args");
    return C_ORM_ERROR_MEMORY;
  }

#ifdef C_ORM_POSTGRESQL_DYNAMIC
  rc = c_orm_load_libpq();
  if (rc != C_ORM_OK) {
    return rc;
  }
#endif

  db = NULL;
  if (c_orm_system_calloc(1, sizeof(c_orm_db_t), (void **)&db) != C_ORM_OK ||
      !db) {
    LOG_DEBUG("postgres_connect: OOM db");
    return C_ORM_ERROR_MEMORY;
  }

  data = NULL;
  if (c_orm_system_calloc(1, sizeof(struct postgres_db_data), (void **)&data) !=
          C_ORM_OK ||
      !data) {
    LOG_DEBUG("postgres_connect: OOM data");
    (void)c_orm_system_free(db);
    return C_ORM_ERROR_MEMORY;
  }

  data->conn = PQconnectdb(url);
  if (PQstatus(data->conn) != CONNECTION_OK) {
    LOG_DEBUG("postgres_connect: connection failed");
    rc = set_error(db, PQerrorMessage(data->conn));
    (void)rc;
    PQfinish(data->conn);
    (void)c_orm_system_free(data);
    (void)c_orm_system_free(db);
    return C_ORM_ERROR_CONNECTION;
  }

  if (c_orm_postgres_get_vtable(&db->vtable) != C_ORM_OK) {
    LOG_DEBUG("postgres_connect: vtable failed");
    PQfinish(data->conn);
    (void)c_orm_system_free(data);
    (void)c_orm_system_free(db);
    return C_ORM_ERROR_UNKNOWN;
  }
  db->driver_data = data;
  db->driver_name = "postgres";
  *out_db = db;

  LOG_DEBUG("postgres_connect: exit");
  return C_ORM_OK;
}

/** @brief Postgres disconnect */
static c_orm_error_t postgres_disconnect(c_orm_db_t *db) {
  c_orm_error_t rc;
  struct postgres_db_data *data;

  LOG_DEBUG("postgres_disconnect: entry");
  if (!db) {
    LOG_DEBUG("postgres_disconnect: no db");
    return C_ORM_OK;
  }

  data = (struct postgres_db_data *)db->driver_data;
  if (data) {
    if (data->conn) {
      PQfinish(data->conn);
    }
    (void)c_orm_system_free(data);
  }
  rc = c_orm_system_free(db);

  LOG_DEBUG("postgres_disconnect: exit");
  return rc;
}

/** @brief Postgres prepare */
static c_orm_error_t postgres_prepare(c_orm_db_t *db, const char *sql,
                                      c_orm_query_t **out_query) {
  c_orm_error_t rc;
  struct postgres_db_data *db_data;
  struct postgres_query_data *q_data;
  postgres_stmt_t *stmt;
  char *new_sql = NULL;
  int param_count = 0;
  char stmt_name[64];
  PGresult *res;

  LOG_DEBUG("postgres_prepare: entry");

  if (!db || !sql || !out_query) {
    LOG_DEBUG("postgres_prepare: invalid args");
    return C_ORM_ERROR_MEMORY;
  }
  db_data = (struct postgres_db_data *)db->driver_data;

  if (db->log_cb) {
    rc = db->log_cb(sql, db->log_user_data);
    if (rc != C_ORM_OK) {
      return rc;
    }
  }

  rc = c_orm_postgres_rewrite_query(sql, &param_count, &new_sql);
  if (rc != C_ORM_OK || !new_sql) {
    LOG_DEBUG("postgres_prepare: rewrite OOM");
    return (rc != C_ORM_OK) ? rc : C_ORM_ERROR_MEMORY;
  }

  stmt = NULL;
  if (c_orm_system_calloc(1, sizeof(postgres_stmt_t), (void **)&stmt) !=
          C_ORM_OK ||
      !stmt) {
    LOG_DEBUG("postgres_prepare: OOM stmt");
    (void)c_orm_system_free(new_sql);
    return C_ORM_ERROR_MEMORY;
  }

  q_data = NULL;
  if (c_orm_system_calloc(1, sizeof(struct postgres_query_data),
                          (void **)&q_data) != C_ORM_OK ||
      !q_data) {
    LOG_DEBUG("postgres_prepare: OOM q_data");
    (void)c_orm_system_free(stmt);
    (void)c_orm_system_free(new_sql);
    return C_ORM_ERROR_MEMORY;
  }

  rc = generate_stmt_name(db_data, stmt_name, sizeof(stmt_name));
  if (rc != C_ORM_OK) {
    (void)c_orm_system_free(q_data);
    (void)c_orm_system_free(stmt);
    (void)c_orm_system_free(new_sql);
    return rc;
  }

  res = PQprepare(db_data->conn, stmt_name, new_sql, param_count, NULL);
  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    LOG_DEBUG("postgres_prepare: prepare failed");
    if (set_error(db, PQerrorMessage(db_data->conn)) != C_ORM_OK) {
      /* error recorded best-effort */
    }
    PQclear(res);
    (void)c_orm_system_free(q_data);
    (void)c_orm_system_free(stmt);
    (void)c_orm_system_free(new_sql);
    return C_ORM_ERROR_SQL;
  }
  PQclear(res);

  q_data->db = db;
  q_data->query_string = new_sql;

  q_data->stmt_name = NULL;
  if (c_orm_system_malloc(strlen(stmt_name) + 1, (void **)&q_data->stmt_name) !=
          C_ORM_OK ||
      !q_data->stmt_name) {
    char dealloc_sql[128];
    PGresult *dres;
    C_ORM_SPRINTF(dealloc_sql, sizeof(dealloc_sql), "DEALLOCATE %s", stmt_name);
    dres = PQexec(db_data->conn, dealloc_sql);
    if (dres) {
      PQclear(dres);
    }
    (void)c_orm_system_free(q_data);
    (void)c_orm_system_free(stmt);
    (void)c_orm_system_free(new_sql);
    return C_ORM_ERROR_MEMORY;
  }
  C_ORM_STRCPY(q_data->stmt_name, strlen(stmt_name) + 1, stmt_name);

  q_data->param_count = param_count;
  if (param_count > 0) {
    if (c_orm_system_calloc((size_t)param_count, sizeof(char *),
                            (void **)&q_data->param_values) != C_ORM_OK ||
        c_orm_system_calloc((size_t)param_count, sizeof(int),
                            (void **)&q_data->param_lengths) != C_ORM_OK ||
        c_orm_system_calloc((size_t)param_count, sizeof(int),
                            (void **)&q_data->param_formats) != C_ORM_OK ||
        !q_data->param_values || !q_data->param_lengths ||
        !q_data->param_formats) {
      char dealloc_sql[128];
      PGresult *dres;
      C_ORM_SPRINTF(dealloc_sql, sizeof(dealloc_sql), "DEALLOCATE %s",
                    stmt_name);
      dres = PQexec(db_data->conn, dealloc_sql);
      if (dres) {
        PQclear(dres);
      }
      if (q_data->param_values)
        (void)c_orm_system_free(q_data->param_values);
      if (q_data->param_lengths)
        (void)c_orm_system_free(q_data->param_lengths);
      if (q_data->param_formats)
        (void)c_orm_system_free(q_data->param_formats);
      (void)c_orm_system_free(q_data->stmt_name);
      (void)c_orm_system_free(q_data);
      (void)c_orm_system_free(stmt);
      (void)c_orm_system_free(new_sql);
      return C_ORM_ERROR_MEMORY;
    }
  }

  q_data->res = NULL;
  q_data->current_row = 0;
  q_data->row_count = 0;

  stmt->data = q_data;
  *out_query = (c_orm_query_t *)stmt;

  LOG_DEBUG("postgres_prepare: exit");
  return C_ORM_OK;
}

/**
 * @brief Free a bound parameter at index.
 * @param q_data Postgres query data.
 * @param index 1-based parameter index.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t free_param(struct postgres_query_data *q_data, int index) {
  c_orm_error_t rc = C_ORM_OK;
  if (q_data->param_values[index - 1]) {
    rc = c_orm_system_free(q_data->param_values[index - 1]);
    q_data->param_values[index - 1] = NULL;
  }
  q_data->param_lengths[index - 1] = 0;
  q_data->param_formats[index - 1] = 0;
  return rc;
}

/** @brief Bind int32 */
static c_orm_error_t postgres_bind_int32(c_orm_query_t *query, int index,
                                         int32_t val) {
  postgres_stmt_t *stmt = (postgres_stmt_t *)query;
  c_orm_error_t rc;
  char buf[32];

  if (!stmt || !stmt->data || index < 1 || index > stmt->data->param_count) {
    return C_ORM_ERROR_BIND;
  }
  rc = free_param(stmt->data, index);
  if (rc != C_ORM_OK) {
    return rc;
  }
  C_ORM_SPRINTF(buf, sizeof(buf), "%d", (int)val);
  return c_orm_strdup(buf, &stmt->data->param_values[index - 1]);
}

/** @brief Bind int64 */
static c_orm_error_t postgres_bind_int64(c_orm_query_t *query, int index,
                                         int64_t val) {
  postgres_stmt_t *stmt = (postgres_stmt_t *)query;
  c_orm_error_t rc;
  char buf[64];

  if (!stmt || !stmt->data || index < 1 || index > stmt->data->param_count) {
    return C_ORM_ERROR_BIND;
  }
  rc = free_param(stmt->data, index);
  if (rc != C_ORM_OK) {
    return rc;
  }
  C_ORM_SPRINTF(buf, sizeof(buf), C_ORM_INT64_FORMAT, (c_orm_int64_t)val);
  return c_orm_strdup(buf, &stmt->data->param_values[index - 1]);
}

/** @brief Bind double */
static c_orm_error_t postgres_bind_double(c_orm_query_t *query, int index,
                                          double val) {
  postgres_stmt_t *stmt = (postgres_stmt_t *)query;
  c_orm_error_t rc;
  char buf[64];

  if (!stmt || !stmt->data || index < 1 || index > stmt->data->param_count) {
    return C_ORM_ERROR_BIND;
  }
  rc = free_param(stmt->data, index);
  if (rc != C_ORM_OK) {
    return rc;
  }
  C_ORM_SPRINTF(buf, sizeof(buf), "%.17g", val);
  return c_orm_strdup(buf, &stmt->data->param_values[index - 1]);
}

/** @brief Bind string */
static c_orm_error_t postgres_bind_string(c_orm_query_t *query, int index,
                                          const char *val) {
  postgres_stmt_t *stmt = (postgres_stmt_t *)query;
  c_orm_error_t rc;

  if (!stmt || !stmt->data || index < 1 || index > stmt->data->param_count) {
    return C_ORM_ERROR_BIND;
  }
  rc = free_param(stmt->data, index);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (!val) {
    return C_ORM_OK;
  }
  return c_orm_strdup(val, &stmt->data->param_values[index - 1]);
}

/** @brief Bind blob */
static c_orm_error_t postgres_bind_blob(c_orm_query_t *query, int index,
                                        const void *val, size_t size) {
  postgres_stmt_t *stmt = (postgres_stmt_t *)query;
  c_orm_error_t rc;

  if (!stmt || !stmt->data || index < 1 || index > stmt->data->param_count) {
    return C_ORM_ERROR_BIND;
  }
  rc = free_param(stmt->data, index);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (!val && size == 0) {
    return C_ORM_OK;
  }
  if (c_orm_system_malloc(size > 0 ? size : 1,
                          (void **)&stmt->data->param_values[index - 1]) !=
          C_ORM_OK ||
      !stmt->data->param_values[index - 1]) {
    return C_ORM_ERROR_MEMORY;
  }
  if (size > 0 && val) {
    memcpy(stmt->data->param_values[index - 1], val, size);
  }
  stmt->data->param_lengths[index - 1] = (int)size;
  stmt->data->param_formats[index - 1] = 1;
  return C_ORM_OK;
}

/** @brief Bind null */
static c_orm_error_t postgres_bind_null(c_orm_query_t *query, int index) {
  postgres_stmt_t *stmt = (postgres_stmt_t *)query;

  if (!stmt || !stmt->data || index < 1 || index > stmt->data->param_count) {
    return C_ORM_ERROR_BIND;
  }
  return free_param(stmt->data, index);
}

/** @brief Step query */
static c_orm_error_t postgres_step(c_orm_query_t *query, int *out_has_row) {
  postgres_stmt_t *stmt = (postgres_stmt_t *)query;
  struct postgres_query_data *q_data;
  struct postgres_db_data *db_data;

  if (!stmt || !stmt->data || !out_has_row) {
    return C_ORM_ERROR_STEP;
  }
  q_data = stmt->data;
  db_data = (struct postgres_db_data *)q_data->db->driver_data;

  if (q_data->res == NULL) {
    q_data->res =
        PQexecPrepared(db_data->conn, q_data->stmt_name, q_data->param_count,
                       (const char *const *)q_data->param_values,
                       q_data->param_lengths, q_data->param_formats, 0);

    if (PQresultStatus(q_data->res) != PGRES_TUPLES_OK &&
        PQresultStatus(q_data->res) != PGRES_COMMAND_OK) {
      if (set_error(q_data->db, PQerrorMessage(db_data->conn)) != C_ORM_OK) {
        /* recorded best-effort */
      }
      PQclear(q_data->res);
      q_data->res = NULL;
      return C_ORM_ERROR_STEP;
    }

    q_data->row_count = PQntuples(q_data->res);
    q_data->current_row = 0;
  } else {
    q_data->current_row++;
  }

  *out_has_row = (q_data->current_row < q_data->row_count) ? 1 : 0;
  return C_ORM_OK;
}

/** @brief Get int32 */
static c_orm_error_t postgres_get_int32(c_orm_query_t *query, int index,
                                        int32_t *out_val) {
  postgres_stmt_t *stmt = (postgres_stmt_t *)query;
  const char *val;

  if (!stmt || !stmt->data || !stmt->data->res || !out_val) {
    return C_ORM_ERROR_MEMORY;
  }
  if (index < 0 || index >= PQnfields(stmt->data->res) ||
      stmt->data->current_row < 0 ||
      stmt->data->current_row >= stmt->data->row_count) {
    return C_ORM_ERROR_VALIDATION;
  }
  if (PQgetisnull(stmt->data->res, stmt->data->current_row, index)) {
    *out_val = 0;
    return C_ORM_OK;
  }
  val = PQgetvalue(stmt->data->res, stmt->data->current_row, index);
  *out_val = val ? (int32_t)atoi(val) : 0;
  return C_ORM_OK;
}

/** @brief Get int64 */
static c_orm_error_t postgres_get_int64(c_orm_query_t *query, int index,
                                        int64_t *out_val) {
  postgres_stmt_t *stmt = (postgres_stmt_t *)query;
  const char *val;

  if (!stmt || !stmt->data || !stmt->data->res || !out_val) {
    return C_ORM_ERROR_MEMORY;
  }
  if (index < 0 || index >= PQnfields(stmt->data->res) ||
      stmt->data->current_row < 0 ||
      stmt->data->current_row >= stmt->data->row_count) {
    return C_ORM_ERROR_VALIDATION;
  }
  *out_val = 0;
  if (PQgetisnull(stmt->data->res, stmt->data->current_row, index)) {
    return C_ORM_OK;
  }
  val = PQgetvalue(stmt->data->res, stmt->data->current_row, index);
  if (val && c_orm_strtoint64(val, NULL, 10, out_val) != C_ORM_OK) {
    *out_val = 0;
  }
  return C_ORM_OK;
}

/** @brief Get double */
static c_orm_error_t postgres_get_double(c_orm_query_t *query, int index,
                                         double *out_val) {
  postgres_stmt_t *stmt = (postgres_stmt_t *)query;
  const char *val;

  if (!stmt || !stmt->data || !stmt->data->res || !out_val) {
    return C_ORM_ERROR_MEMORY;
  }
  if (index < 0 || index >= PQnfields(stmt->data->res) ||
      stmt->data->current_row < 0 ||
      stmt->data->current_row >= stmt->data->row_count) {
    return C_ORM_ERROR_VALIDATION;
  }
  if (PQgetisnull(stmt->data->res, stmt->data->current_row, index)) {
    *out_val = 0.0;
    return C_ORM_OK;
  }
  val = PQgetvalue(stmt->data->res, stmt->data->current_row, index);
  *out_val = val ? strtod(val, NULL) : 0.0;
  return C_ORM_OK;
}

/** @brief Get string */
static c_orm_error_t postgres_get_string(c_orm_query_t *query, int index,
                                         const char **out_val) {
  postgres_stmt_t *stmt = (postgres_stmt_t *)query;

  if (!stmt || !stmt->data || !stmt->data->res || !out_val) {
    return C_ORM_ERROR_MEMORY;
  }
  if (index < 0 || index >= PQnfields(stmt->data->res) ||
      stmt->data->current_row < 0 ||
      stmt->data->current_row >= stmt->data->row_count) {
    return C_ORM_ERROR_VALIDATION;
  }
  if (PQgetisnull(stmt->data->res, stmt->data->current_row, index)) {
    *out_val = NULL;
    return C_ORM_OK;
  }
  *out_val = PQgetvalue(stmt->data->res, stmt->data->current_row, index);
  return C_ORM_OK;
}

/** @brief Get blob */
static c_orm_error_t postgres_get_blob(c_orm_query_t *query, int index,
                                       const void **out_val, size_t *out_size) {
  postgres_stmt_t *stmt = (postgres_stmt_t *)query;

  if (!stmt || !stmt->data || !stmt->data->res || !out_val || !out_size) {
    return C_ORM_ERROR_MEMORY;
  }
  if (index < 0 || index >= PQnfields(stmt->data->res) ||
      stmt->data->current_row < 0 ||
      stmt->data->current_row >= stmt->data->row_count) {
    return C_ORM_ERROR_VALIDATION;
  }
  if (PQgetisnull(stmt->data->res, stmt->data->current_row, index)) {
    *out_val = NULL;
    *out_size = 0;
    return C_ORM_OK;
  }
  *out_val = PQgetvalue(stmt->data->res, stmt->data->current_row, index);
  *out_size =
      (size_t)PQgetlength(stmt->data->res, stmt->data->current_row, index);
  return C_ORM_OK;
}

/** @brief Is null */
static c_orm_error_t postgres_is_null(c_orm_query_t *query, int index,
                                      int *out_is_null) {
  postgres_stmt_t *stmt = (postgres_stmt_t *)query;

  if (!stmt || !stmt->data || !stmt->data->res || !out_is_null) {
    return C_ORM_ERROR_MEMORY;
  }
  if (index < 0 || index >= PQnfields(stmt->data->res) ||
      stmt->data->current_row < 0 ||
      stmt->data->current_row >= stmt->data->row_count) {
    return C_ORM_ERROR_VALIDATION;
  }
  *out_is_null =
      PQgetisnull(stmt->data->res, stmt->data->current_row, index) ? 1 : 0;
  return C_ORM_OK;
}

/** @brief Finalize and DEALLOCATE server-side statement */
static c_orm_error_t postgres_finalize(c_orm_query_t *query) {
  postgres_stmt_t *stmt = (postgres_stmt_t *)query;
  int i;

  if (!stmt) {
    return C_ORM_OK;
  }
  if (stmt->data) {
    if (stmt->data->res) {
      PQclear(stmt->data->res);
    }
    if (stmt->data->db && stmt->data->db->driver_data &&
        stmt->data->stmt_name) {
      struct postgres_db_data *db_data =
          (struct postgres_db_data *)stmt->data->db->driver_data;
      if (db_data->conn) {
        char dealloc_sql[128];
        PGresult *dres;
        C_ORM_SPRINTF(dealloc_sql, sizeof(dealloc_sql), "DEALLOCATE %s",
                      stmt->data->stmt_name);
        dres = PQexec(db_data->conn, dealloc_sql);
        if (dres) {
          PQclear(dres);
        }
      }
    }
    if (stmt->data->param_count > 0) {
      for (i = 0; i < stmt->data->param_count; i++) {
        if (free_param(stmt->data, i + 1) != C_ORM_OK) {
          /* best effort parameter cleanup */
        }
      }
      (void)c_orm_system_free(stmt->data->param_values);
      (void)c_orm_system_free(stmt->data->param_lengths);
      (void)c_orm_system_free(stmt->data->param_formats);
    }
    (void)c_orm_system_free(stmt->data->stmt_name);
    (void)c_orm_system_free(stmt->data->query_string);
    (void)c_orm_system_free(stmt->data);
  }
  return c_orm_system_free(stmt);
}

/** @brief Reset */
static c_orm_error_t postgres_reset(c_orm_query_t *query) {
  postgres_stmt_t *stmt = (postgres_stmt_t *)query;
  int i;

  if (!stmt || !stmt->data) {
    return C_ORM_ERROR_MEMORY;
  }
  if (stmt->data->res) {
    PQclear(stmt->data->res);
    stmt->data->res = NULL;
  }
  for (i = 0; i < stmt->data->param_count; i++) {
    if (free_param(stmt->data, i + 1) != C_ORM_OK) {
      return C_ORM_ERROR_MEMORY;
    }
  }
  stmt->data->current_row = 0;
  stmt->data->row_count = 0;
  return C_ORM_OK;
}

/** @brief Get last error */
static c_orm_error_t postgres_get_last_error(c_orm_db_t *db,
                                             const char **out_message) {
  struct postgres_db_data *data;

  if (!out_message) {
    return C_ORM_ERROR_UNKNOWN;
  }
  if (!db || !db->driver_data) {
    *out_message = "Invalid DB object";
    return C_ORM_ERROR_UNKNOWN;
  }
  data = (struct postgres_db_data *)db->driver_data;
  *out_message = data->last_error;
  return C_ORM_OK;
}

/** @brief Get last trace */
static c_orm_error_t postgres_get_last_trace(c_orm_db_t *db,
                                             const char **out_trace) {
  (void)db;
  if (out_trace) {
    *out_trace =
        "Postgres Driver Stack Trace: Requires external cdd-c AST diagnostic "
        "parsing locally integrated inside driver compilation units natively. "
        "Returning Last error mapping fallback.";
  }
  return C_ORM_OK;
}

/** @brief Get last insert rowid */
static c_orm_error_t postgres_get_last_insert_rowid(c_orm_db_t *db,
                                                    int64_t *out_id) {
  struct postgres_db_data *data;
  PGresult *res;

  if (!db || !db->driver_data || !out_id) {
    return C_ORM_ERROR_MEMORY;
  }
  data = (struct postgres_db_data *)db->driver_data;
  res = PQexec(data->conn, "SELECT LASTVAL()");
  if (!res || PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) < 1) {
    if (res) {
      PQclear(res);
    }
    if (set_error(db, NULL) != C_ORM_OK) {
      /* recorded best-effort */
    }
    return C_ORM_ERROR_SQL;
  }
  *out_id = 0;
  if (PQgetvalue(res, 0, 0) &&
      c_orm_strtoint64(PQgetvalue(res, 0, 0), NULL, 10, out_id) != C_ORM_OK) {
    *out_id = 0;
  }
  PQclear(res);
  return C_ORM_OK;
}

/** @brief Get result column count via PQnfields. */
static c_orm_error_t postgres_get_column_count(c_orm_query_t *query,
                                               int *out_count) {
  postgres_stmt_t *stmt = (postgres_stmt_t *)query;
  if (!stmt || !stmt->data || !stmt->data->res || !out_count) {
    return C_ORM_ERROR_MEMORY;
  }
  *out_count = PQnfields(stmt->data->res);
  return C_ORM_OK;
}

/** @brief Get result column name via PQfname. */
static c_orm_error_t postgres_get_column_name(c_orm_query_t *query, int index,
                                              const char **out_name) {
  postgres_stmt_t *stmt = (postgres_stmt_t *)query;
  if (!stmt || !stmt->data || !stmt->data->res || !out_name) {
    return C_ORM_ERROR_MEMORY;
  }
  if (index < 0 || index >= PQnfields(stmt->data->res)) {
    return C_ORM_ERROR_VALIDATION;
  }
  *out_name = PQfname(stmt->data->res, index);
  return C_ORM_OK;
}

/**
 * @brief Binds an exact fixed-point decimal to a PostgreSQL NUMERIC parameter.
 * @param query Prepared PostgreSQL statement handle.
 * @param index 1-based parameter index.
 * @param val Decimal value pointer.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t postgres_bind_decimal(c_orm_query_t *query, int index,
                                           const c_orm_decimal_t *val) {
  char buf[64];
  c_orm_error_t rc;
  if (!val) {
    return postgres_bind_null(query, index);
  }
  rc = c_orm_decimal_to_str(val, buf, sizeof(buf), NULL);
  if (rc != C_ORM_OK) {
    return rc;
  }
  return postgres_bind_string(query, index, buf);
}

/**
 * @brief Retrieves an exact fixed-point decimal from a PostgreSQL result
 * column.
 * @param query Prepared PostgreSQL statement handle.
 * @param index 0-based column index.
 * @param out_val Output pointer receiving parsed decimal.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t postgres_get_decimal(c_orm_query_t *query, int index,
                                          c_orm_decimal_t *out_val) {
  const char *s = NULL;
  c_orm_error_t rc;
  if (!out_val) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  rc = postgres_get_string(query, index, &s);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (!s) {
    return c_orm_decimal_zero(0, out_val);
  }
  return c_orm_decimal_from_str(s, out_val);
}

/** @brief Postgres vtable */
static c_orm_driver_vtable_t postgres_vtable = {postgres_connect,
                                                postgres_disconnect,
                                                postgres_prepare,
                                                postgres_bind_int32,
                                                postgres_bind_int64,
                                                postgres_bind_double,
                                                postgres_bind_string,
                                                postgres_bind_blob,
                                                postgres_bind_null,
                                                postgres_step,
                                                postgres_get_int32,
                                                postgres_get_int64,
                                                postgres_get_double,
                                                postgres_get_string,
                                                postgres_get_blob,
                                                postgres_is_null,
                                                postgres_finalize,
                                                postgres_reset,
                                                postgres_get_last_error,
                                                postgres_get_last_trace,
                                                postgres_get_last_insert_rowid,
                                                postgres_get_column_count,
                                                postgres_get_column_name,
                                                postgres_bind_decimal,
                                                postgres_get_decimal};

/** @brief Get Postgres vtable */
C_ORM_EXPORT c_orm_error_t
c_orm_postgres_get_vtable(const c_orm_driver_vtable_t **out_vtable) {
  if (!out_vtable) {
    return C_ORM_ERROR_UNKNOWN;
  }
  *out_vtable = &postgres_vtable;
  return C_ORM_OK;
}

/** @brief Postgres connect exported */
C_ORM_EXPORT c_orm_error_t c_orm_postgres_connect(const char *url,
                                                  c_orm_db_t **out_db) {
  return postgres_connect(url, out_db);
}

/** @brief Postgres large object create */
C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_create(c_orm_db_t *db,
                                                    unsigned int *out_oid) {
  struct postgres_db_data *data;
  Oid oid;

  if (!db || !db->driver_data || !out_oid) {
    return C_ORM_ERROR_MEMORY;
  }
  data = (struct postgres_db_data *)db->driver_data;
  oid = lo_creat(data->conn, INV_READ | INV_WRITE);
  if (oid == InvalidOid) {
    return C_ORM_ERROR_UNKNOWN;
  }
  *out_oid = (unsigned int)oid;
  return C_ORM_OK;
}

/** @brief Postgres large object open */
C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_open(c_orm_db_t *db,
                                                  unsigned int oid, int mode,
                                                  void **out_fd) {
  struct postgres_db_data *data;
  int fd;

  if (!db || !db->driver_data || !out_fd) {
    return C_ORM_ERROR_MEMORY;
  }
  data = (struct postgres_db_data *)db->driver_data;
  fd = lo_open(data->conn, (Oid)oid, mode);
  if (fd < 0) {
    return C_ORM_ERROR_UNKNOWN;
  }
  *out_fd = (void *)(intptr_t)fd;
  return C_ORM_OK;
}

/** @brief Postgres large object read */
C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_read(c_orm_db_t *db, void *fd,
                                                  void *buffer, size_t len,
                                                  size_t *out_read) {
  struct postgres_db_data *data;
  int bytes_read;

  if (!db || !db->driver_data || !buffer || !out_read) {
    return C_ORM_ERROR_MEMORY;
  }
  data = (struct postgres_db_data *)db->driver_data;
  bytes_read = lo_read(data->conn, (int)(intptr_t)fd, (char *)buffer, len);
  if (bytes_read < 0) {
    return C_ORM_ERROR_UNKNOWN;
  }
  *out_read = (size_t)bytes_read;
  return C_ORM_OK;
}

/** @brief Postgres large object write */
C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_write(c_orm_db_t *db, void *fd,
                                                   const void *buffer,
                                                   size_t len,
                                                   size_t *out_written) {
  struct postgres_db_data *data;
  int bytes_written;

  if (!db || !db->driver_data || !buffer || !out_written) {
    return C_ORM_ERROR_MEMORY;
  }
  data = (struct postgres_db_data *)db->driver_data;
  bytes_written =
      lo_write(data->conn, (int)(intptr_t)fd, (const char *)buffer, len);
  if (bytes_written < 0) {
    return C_ORM_ERROR_UNKNOWN;
  }
  *out_written = (size_t)bytes_written;
  return C_ORM_OK;
}

/** @brief Postgres large object close */
C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_close(c_orm_db_t *db, void *fd) {
  struct postgres_db_data *data;
  int close_rc;

  if (!db || !db->driver_data) {
    return C_ORM_ERROR_MEMORY;
  }
  data = (struct postgres_db_data *)db->driver_data;
  close_rc = lo_close(data->conn, (int)(intptr_t)fd);
  if (close_rc < 0) {
    return C_ORM_ERROR_UNKNOWN;
  }
  return C_ORM_OK;
}

#else

/**
 * @brief Get Postgres vtable stub.
 * @param out_vtable Pointer receiving the driver vtable.
 * @return C_ORM_ERROR_NOT_IMPLEMENTED when Postgres is disabled.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_postgres_get_vtable(const c_orm_driver_vtable_t **out_vtable) {
  if (out_vtable) {
    *out_vtable = NULL;
  }
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}

/**
 * @brief Postgres connect stub.
 * @param url Connection string URL.
 * @param out_db Pointer receiving database handle.
 * @return C_ORM_ERROR_NOT_IMPLEMENTED when Postgres is disabled.
 */
C_ORM_EXPORT c_orm_error_t c_orm_postgres_connect(const char *url,
                                                  c_orm_db_t **out_db) {
  (void)url;
  (void)out_db;
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}

/**
 * @brief Postgres large object create stub.
 * @param db Database handle.
 * @param out_oid Pointer receiving object identifier.
 * @return C_ORM_ERROR_NOT_IMPLEMENTED when Postgres is disabled.
 */
C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_create(c_orm_db_t *db,
                                                    unsigned int *out_oid) {
  (void)db;
  (void)out_oid;
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}

/**
 * @brief Postgres large object open stub.
 * @param db Database handle.
 * @param oid Object identifier.
 * @param mode Open mode flags.
 * @param out_fd Pointer receiving descriptor pointer.
 * @return C_ORM_ERROR_NOT_IMPLEMENTED when Postgres is disabled.
 */
C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_open(c_orm_db_t *db,
                                                  unsigned int oid, int mode,
                                                  void **out_fd) {
  (void)db;
  (void)oid;
  (void)mode;
  (void)out_fd;
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}

/**
 * @brief Postgres large object read stub.
 * @param db Database handle.
 * @param fd Large object descriptor.
 * @param buffer Output buffer.
 * @param len Buffer length.
 * @param out_read Pointer receiving bytes read.
 * @return C_ORM_ERROR_NOT_IMPLEMENTED when Postgres is disabled.
 */
C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_read(c_orm_db_t *db, void *fd,
                                                  void *buffer, size_t len,
                                                  size_t *out_read) {
  (void)db;
  (void)fd;
  (void)buffer;
  (void)len;
  (void)out_read;
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}

/**
 * @brief Postgres large object write stub.
 * @param db Database handle.
 * @param fd Large object descriptor.
 * @param buffer Input data buffer.
 * @param len Buffer length.
 * @param out_written Pointer receiving bytes written.
 * @return C_ORM_ERROR_NOT_IMPLEMENTED when Postgres is disabled.
 */
C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_write(c_orm_db_t *db, void *fd,
                                                   const void *buffer,
                                                   size_t len,
                                                   size_t *out_written) {
  (void)db;
  (void)fd;
  (void)buffer;
  (void)len;
  (void)out_written;
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}

/**
 * @brief Postgres large object close stub.
 * @param db Database handle.
 * @param fd Large object descriptor.
 * @return C_ORM_ERROR_NOT_IMPLEMENTED when Postgres is disabled.
 */
C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_close(c_orm_db_t *db, void *fd) {
  (void)db;
  (void)fd;
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}

#endif
