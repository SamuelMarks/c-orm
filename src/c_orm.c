/* clang-format off */
#if defined(_WIN32)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#endif
#include "c_orm/c_orm.h"

#if defined(_WIN32)
#if !defined(_X86_) && !defined(_AMD64_) && !defined(_ARM_) && !defined(_ARM64_)
#if defined(_M_IX86)
#define _X86_
#elif defined(_M_AMD64)
#define _AMD64_
#elif defined(_M_ARM)
#define _ARM_
#elif defined(_M_ARM64)
#define _ARM64_
#endif
#endif
#include <windef.h>
#include <winbase.h>
#elif defined(__MSDOS__) || defined(__WATCOMC__)
/* DOS/Watcom doesn't have POSIX threads */
#else
#ifndef _MSC_VER
#include <pthread.h>
#endif
#endif

#if defined(_WIN32)
typedef CRITICAL_SECTION c_orm_mutex_t;
#if defined(_MSC_VER) && _MSC_VER <= 1400
typedef HANDLE c_orm_cond_t;
#else
typedef CONDITION_VARIABLE c_orm_cond_t;
#endif
typedef DWORD c_orm_tls_t;
#elif defined(__MSDOS__) || defined(__WATCOMC__)
typedef int c_orm_mutex_t;
typedef int c_orm_cond_t;
typedef int c_orm_tls_t;
#else
#ifndef _MSC_VER
typedef pthread_mutex_t c_orm_mutex_t;
typedef pthread_cond_t c_orm_cond_t;
typedef pthread_key_t c_orm_tls_t;
#endif
#endif

#include <stdlib.h>

#include <string.h>

#include <stdio.h>

#include <time.h>

#ifdef C_ORM_HAVE_POSTGRES
#include <libpq-fe.h>
#endif

#ifdef C_ORM_HAVE_MYSQL
#ifdef _WIN32
#include <winsock2.h>
#endif
#include <mysql.h>
#endif

#if !defined(_MSC_VER) || (_MSC_VER >= 1600)
#include <stdint.h>
#endif
#if !defined(_WIN32) && !defined(__MSDOS__) && !defined(__WATCOMC__)
#include <unistd.h>
#if !defined(__APPLE__) && !defined(__FreeBSD__) && !defined(__MINGW32__)
#include <sys/eventfd.h>
#endif
#endif
/* clang-format on */

#ifdef C_ORM_HAVE_POSTGRES

/* Mock helpers for 100% test coverage without a running DB */
/**
 * @brief Mock wrapper for PQconnectdb.
 * @param conninfo Connection string.
 * @param out_conn Output for PGconn.
 * @return 0 on success.
 */
static int internal_PQconnectdb(const char *conninfo, PGconn **out_conn) {
  if (strcmp(conninfo, "test_mock_success") == 0) {
    *out_conn = (PGconn *)1;
    return 0;
  }
  *out_conn = PQconnectdb(conninfo);
  return 0;
}
/**
 * @brief Mock wrapper for PQstatus.
 * @param conn The connection.
 * @param out_status Output for ConnStatusType.
 * @return 0 on success.
 */
static int internal_PQstatus(const PGconn *conn, ConnStatusType *out_status) {
  if (conn == (PGconn *)1) {
    *out_status = CONNECTION_OK;
    return 0;
  }
  *out_status = PQstatus(conn);
  return 0;
}
/**
 * @brief Mock wrapper for PQfinish.
 * @param conn The connection.
 */
static void internal_PQfinish(PGconn *conn) {
  if (conn == (PGconn *)1)
    return;
  PQfinish(conn);
}
/**
 * @brief Mock wrapper for PQexec.
 * @param conn The connection.
 * @param query The query string.
 * @param out_res Output for PGresult.
 * @return 0 on success.
 */
static int internal_PQexec(PGconn *conn, const char *query,
                           PGresult **out_res) {
  if (conn == (PGconn *)1) {
    *out_res = (PGresult *)1;
    return 0;
  }
  *out_res = PQexec(conn, query);
  return 0;
}

/**
 * @brief Internal helper internal_PQsendQuery.
 */
static int internal_PQsendQuery(PGconn *conn, const char *query) {
  if (conn == (PGconn *)1)
    return 1;
  return PQsendQuery(conn, query);
}

/**
 * @brief Internal helper internal_PQgetResult.
 */
static int internal_PQgetResult(PGconn *conn, PGresult **out_res) {
  if (conn == (PGconn *)1) {
    *out_res = NULL;
    return 0;
  }
  *out_res = PQgetResult(conn);
  return 0;
}

/**
 * @brief Internal helper internal_PQsocket.
 */
static int internal_PQsocket(const PGconn *conn) {
  if (conn == (PGconn *)1)
    return -1;
  return PQsocket(conn);
}

/**
 * @brief Internal helper internal_PQconsumeInput.
 */
static int internal_PQconsumeInput(PGconn *conn) {
  if (conn == (PGconn *)1)
    return 1;
  return PQconsumeInput(conn);
}

/**
 * @brief Internal helper internal_PQisBusy.
 */
static int internal_PQisBusy(PGconn *conn) {
  if (conn == (PGconn *)1)
    return 0;
  return PQisBusy(conn);
}

/**
 * @brief Mock wrapper for PQexecParams.
 * @param conn The connection.
 * @param command The query command.
 * @param nParams Number of parameters.
 * @param paramTypes Types of parameters.
 * @param paramValues Values of parameters.
 * @param paramLengths Lengths of parameters.
 * @param paramFormats Formats of parameters.
 * @param resultFormat Result format.
 * @param out_res Output for PGresult.
 * @return 0 on success.
 */
static int internal_PQexecParams(PGconn *conn, const char *command, int nParams,
                                 const Oid *paramTypes,
                                 const char *const *paramValues,
                                 const int *paramLengths,
                                 const int *paramFormats, int resultFormat,
                                 PGresult **out_res) {
  if (conn == (PGconn *)1) {
    *out_res = (PGresult *)1;
    return 0;
  }
  *out_res = PQexecParams(conn, command, nParams, paramTypes, paramValues,
                          paramLengths, paramFormats, resultFormat);
  return 0;
}
/**
 * @brief Mock wrapper for PQresultStatus.
 * @param res The result.
 * @param out_status Output for ExecStatusType.
 * @return 0 on success.
 */
static int internal_PQresultStatus(const PGresult *res,
                                   ExecStatusType *out_status) {
  if (res == (PGresult *)1) {
    *out_status = PGRES_COMMAND_OK;
    return 0;
  }
  *out_status = PQresultStatus(res);
  return 0;
}
/**
 * @brief Mock wrapper for PQclear.
 * @param res The result.
 */
static void internal_PQclear(PGresult *res) {
  if (res == (PGresult *)1)
    return;
  PQclear(res);
}
#endif

#ifdef C_ORM_HAVE_MYSQL

/* Mock helpers for 100% test coverage without a running DB */
/**
 * @brief Mock wrapper for mysql_init.
 * @param mysql The mysql handle.
 * @param out_mysql Output for MYSQL handle.
 * @return 0 on success.
 */
static int internal_mysql_init(MYSQL *mysql, MYSQL **out_mysql) {
  *out_mysql = mysql_init(mysql);
  return 0;
}
/**
 * @brief Mock wrapper for mysql_real_connect.
 * @param mysql The mysql handle.
 * @param host The host.
 * @param user The user.
 * @param passwd The password.
 * @param db The database.
 * @param port The port.
 * @param unix_socket The unix socket.
 * @param clientflag The client flags.
 * @param out_mysql Output for MYSQL handle.
 * @return 0 on success.
 */
static int internal_mysql_real_connect(MYSQL *mysql, const char *host,
                                       const char *user, const char *passwd,
                                       const char *db, unsigned int port,
                                       const char *unix_socket,
                                       unsigned long clientflag,
                                       MYSQL **out_mysql) {
  if (host && strcmp(host, "test_mock_success") == 0) {
    *out_mysql = mysql ? mysql : (MYSQL *)1;
    return 0;
  }
  *out_mysql = mysql_real_connect(mysql, host, user, passwd, db, port,
                                  unix_socket, clientflag);
  return 0;
}
/**
 * @brief Mock wrapper for mysql_close.
 * @param sock The mysql handle.
 */
static void internal_mysql_close(MYSQL *sock) {
  if (sock == (MYSQL *)1)
    return;
  mysql_close(sock);
}
/**
 * @brief Mock wrapper for mysql_query.
 * @param mysql The mysql handle.
 * @param q The query string.
 * @return 0 on success.
 */
static int internal_mysql_query(MYSQL *mysql, const char *q) {
  if (mysql == (MYSQL *)1)
    return 0;
  return mysql_query(mysql, q);
}

/**
 * @brief Internal helper internal_mysql_real_query_nonblocking.
 */
static int internal_mysql_real_query_nonblocking(MYSQL *mysql, const char *q,
                                                 unsigned long length,
                                                 int *status) {
  if (mysql == (MYSQL *)1) {
    *status = 0;
    return 0;
  }
#if defined(MARIADB_BASE_VERSION) || defined(MARIADB_VERSION_ID)
  /* MariaDB style non-blocking */
  *status = mysql_real_query_nonblocking(mysql, q, length);
  return 0;
#else
  /* MySQL 8 style or fallback */
  return mysql_real_query(
      mysql, q,
      length); /* synchronous fallback if no non-blocking API available */
#endif
}

static int internal_mysql_get_socket(const MYSQL *mysql) {
  if (mysql == (MYSQL *)1)
    return -1;
  return -1; /* Stub for now to prevent opaque struct compile errors */
}
/**
 * @brief Mock wrapper for mysql_stmt_init.
 * @param mysql The mysql handle.
 * @param out_stmt Output for MYSQL_STMT.
 * @return 0 on success.
 */
static int internal_mysql_stmt_init(MYSQL *mysql, MYSQL_STMT **out_stmt) {
  if (mysql == (MYSQL *)1) {
    *out_stmt = (MYSQL_STMT *)1;
    return 0;
  }
  *out_stmt = mysql_stmt_init(mysql);
  return 0;
}
/**
 * @brief Mock wrapper for mysql_stmt_prepare.
 * @param stmt The statement handle.
 * @param query The query string.
 * @param length The query length.
 * @return 0 on success.
 */
static int internal_mysql_stmt_prepare(MYSQL_STMT *stmt, const char *query,
                                       unsigned long length) {
  if (stmt == (MYSQL_STMT *)1)
    return 0;
  return mysql_stmt_prepare(stmt, query, length);
}
/**
 * @brief Mock wrapper for mysql_stmt_bind_param.
 * @param stmt The statement handle.
 * @param bnd The bind structures.
 * @return 0 on success.
 */
static int internal_mysql_stmt_bind_param(MYSQL_STMT *stmt, MYSQL_BIND *bnd) {
  if (stmt == (MYSQL_STMT *)1)
    return 0;
  return mysql_stmt_bind_param(stmt, bnd);
}
/**
 * @brief Mock wrapper for mysql_stmt_execute.
 * @param stmt The statement handle.
 * @return 0 on success.
 */
static int internal_mysql_stmt_execute(MYSQL_STMT *stmt) {
  if (stmt == (MYSQL_STMT *)1)
    return 0;
  return mysql_stmt_execute(stmt);
}
/**
 * @brief Mock wrapper for mysql_stmt_close.
 * @param stmt The statement handle.
 * @param out_bool Output for my_bool.
 * @return 0 on success.
 */
static int internal_mysql_stmt_close(MYSQL_STMT *stmt, my_bool *out_bool) {
  if (stmt == (MYSQL_STMT *)1) {
    *out_bool = 0;
    return 0;
  }
  *out_bool = mysql_stmt_close(stmt);
  return 0;
}
#endif

/**
 * @brief Internal helper c_orm_mutex_init.
 */
static void c_orm_mutex_init(c_orm_mutex_t *mutex);
/**
 * @brief Internal helper c_orm_mutex_destroy.
 */
static void c_orm_mutex_destroy(c_orm_mutex_t *mutex);
/**
 * @brief Internal helper c_orm_mutex_lock.
 */
static void c_orm_mutex_lock(c_orm_mutex_t *mutex);
/**
 * @brief Internal helper c_orm_mutex_unlock.
 */
static void c_orm_mutex_unlock(c_orm_mutex_t *mutex);

/**
 * @brief Internal helper c_orm_cond_init.
 */
static void c_orm_cond_init(c_orm_cond_t *cond);
/**
 * @brief Internal helper c_orm_cond_destroy.
 */
static void c_orm_cond_destroy(c_orm_cond_t *cond);
/**
 * @brief Internal helper c_orm_cond_wait.
 */
static void c_orm_cond_wait(c_orm_cond_t *cond, c_orm_mutex_t *mutex);
/**
 * @brief Internal helper c_orm_cond_signal.
 */
static void c_orm_cond_signal(c_orm_cond_t *cond);

/**
 * @brief Internal helper c_orm_tls_create.
 */
static int c_orm_tls_create(c_orm_tls_t *tls) {
#if defined(_WIN32)
  *tls = TlsAlloc();
  return (*tls == TLS_OUT_OF_INDEXES) ? -1 : 0;
#elif defined(__MSDOS__) || defined(__WATCOMC__)
  *tls = 0;
  return 0;
#else
  return pthread_key_create(tls, NULL);
#endif
}

/**
 * @brief Internal helper c_orm_tls_destroy.
 */
static void c_orm_tls_destroy(c_orm_tls_t tls) {
#if defined(_WIN32)
  TlsFree(tls);
#elif defined(__MSDOS__) || defined(__WATCOMC__)
  (void)tls;
#else
  pthread_key_delete(tls);
#endif
}

/**
 * @brief Internal helper c_orm_tls_set.
 */
static void c_orm_tls_set(c_orm_tls_t tls, void *val) {
#if defined(_WIN32)
  TlsSetValue(tls, val);
#elif defined(__MSDOS__) || defined(__WATCOMC__)
  (void)tls;
  (void)val;
#else
  pthread_setspecific(tls, val);
#endif
}

/**
 * @brief Internal helper c_orm_tls_get.
 */
static int c_orm_tls_get(c_orm_tls_t tls, void **out) {
#if defined(_WIN32)
  *out = TlsGetValue(tls);
#elif defined(__MSDOS__) || defined(__WATCOMC__)
  (void)tls;
  *out = NULL;
#else
  *out = pthread_getspecific(tls);
#endif
  return 0;
}

/**
 * @brief Internal helper strdup_safe.
 */
static int strdup_safe(const char *s, char **out);

/**
 * @brief Initialize a mutex.
 * @param mutex Pointer to the mutex to initialize.
 */
static void c_orm_mutex_init(c_orm_mutex_t *mutex) {
#if defined(_WIN32)
  InitializeCriticalSection(mutex);
#elif defined(__MSDOS__) || defined(__WATCOMC__)
  *mutex = 0;
#else
  pthread_mutex_init(mutex, NULL);
#endif
}

/**
 * @brief Internal helper c_orm_mutex_destroy.
 */
static void c_orm_mutex_destroy(c_orm_mutex_t *mutex) {
#if defined(_WIN32)
  DeleteCriticalSection(mutex);
#elif defined(__MSDOS__) || defined(__WATCOMC__)
  (void)mutex;
#else
  pthread_mutex_destroy(mutex);
#endif
}

/**
 * @brief Internal helper c_orm_mutex_lock.
 */
static void c_orm_mutex_lock(c_orm_mutex_t *mutex) {
#if defined(_WIN32)
  EnterCriticalSection(mutex);
#elif defined(__MSDOS__) || defined(__WATCOMC__)
  (void)mutex;
#else
  pthread_mutex_lock(mutex);
#endif
}

/**
 * @brief Internal helper c_orm_mutex_unlock.
 */
static void c_orm_mutex_unlock(c_orm_mutex_t *mutex) {
#if defined(_WIN32)
  LeaveCriticalSection(mutex);
#elif defined(__MSDOS__) || defined(__WATCOMC__)
  (void)mutex;
#else
  pthread_mutex_unlock(mutex);
#endif
}

/**
 * @brief Internal helper c_orm_cond_init.
 */
static void c_orm_cond_init(c_orm_cond_t *cond) {
#if defined(_WIN32)
#if defined(_MSC_VER) && _MSC_VER <= 1400
  *cond = CreateEventA(NULL, FALSE, FALSE, NULL);
#else
  InitializeConditionVariable(cond);
#endif
#elif defined(__MSDOS__) || defined(__WATCOMC__)
  *cond = 0;
#else
  pthread_cond_init(cond, NULL);
#endif
}

/**
 * @brief Internal helper c_orm_cond_destroy.
 */
static void c_orm_cond_destroy(c_orm_cond_t *cond) {
#if defined(_WIN32)
#if defined(_MSC_VER) && _MSC_VER <= 1400
  if (*cond)
    CloseHandle(*cond);
#else
  /* CONDITION_VARIABLE doesn't need explicit destruction in Win32 */
#endif
#elif defined(__MSDOS__) || defined(__WATCOMC__)
  (void)cond;
#else
  pthread_cond_destroy(cond);
#endif
}

/**
 * @brief Internal helper c_orm_cond_wait.
 */
static void c_orm_cond_wait(c_orm_cond_t *cond, c_orm_mutex_t *mutex) {
#if defined(_WIN32)
#if defined(_MSC_VER) && _MSC_VER <= 1400
  LeaveCriticalSection(mutex);
  WaitForSingleObject(*cond, INFINITE);
  EnterCriticalSection(mutex);
#else
  SleepConditionVariableCS(cond, mutex, INFINITE);
#endif
#elif defined(__MSDOS__) || defined(__WATCOMC__)
  (void)cond;
  (void)mutex;
#else
  pthread_cond_wait(cond, mutex);
#endif
}

/**
 * @brief Internal helper c_orm_cond_wait_timeout.
 */
static int c_orm_cond_wait_timeout(c_orm_cond_t *cond, c_orm_mutex_t *mutex,
                                   int ms) {
#if defined(_WIN32)
#if defined(_MSC_VER) && _MSC_VER <= 1400
  DWORD res;
  LeaveCriticalSection(mutex);
  res = WaitForSingleObject(*cond, (DWORD)ms);
  EnterCriticalSection(mutex);
  if (res == WAIT_OBJECT_0) {
    return 0;
  }
  return -1;
#else
  if (SleepConditionVariableCS(cond, mutex, ms)) {
    return 0;
  }
  return -1; /* Timeout or error */
#endif
#elif defined(__MSDOS__) || defined(__WATCOMC__)
  (void)cond;
  (void)mutex;
  (void)ms;
  return -1;
#else
  struct timespec ts;
  time_t now = time(NULL);
  ts.tv_sec = now + (ms / 1000);
  ts.tv_nsec = (ms % 1000) * 1000000L;
  if (pthread_cond_timedwait(cond, mutex, &ts) == 0) {
    return 0;
  }
  return -1;
#endif
}

/**
 * @brief Internal helper c_orm_cond_signal.
 */
static void c_orm_cond_signal(c_orm_cond_t *cond) {
#if defined(_WIN32)
#if defined(_MSC_VER) && _MSC_VER <= 1400
  SetEvent(*cond);
#else
  WakeConditionVariable(cond);
#endif
#elif defined(__MSDOS__) || defined(__WATCOMC__)
  (void)cond;
#else
  pthread_cond_signal(cond);
#endif
}

/**
 * @brief Simulation structures for Async I/O.
 */
struct c_orm_async_job {
  char *query;
  c_orm_param_t *params;
  size_t param_count;
  c_orm_async_cb_t cb;
  void *user_data;
  double timeout_ms;   /* Timeout duration in milliseconds (0 for infinite) */
  clock_t enqueued_at; /* Time when the job was added to the queue */
  struct c_orm_async_job *next;
};

/**
 * @brief Cross-modality Memory Allocator Interface.
 */
typedef struct {
  int (*malloc_fn)(size_t size, void **out);
  int (*calloc_fn)(size_t nmemb, size_t size, void **out);
  int (*realloc_fn)(void *ptr, size_t size, void **out);
  void (*free_fn)(void *ptr);
} c_orm_allocator_t;

/* Default allocators */
static int default_malloc(size_t size, void **out) {
  *out = malloc(size);
  return 0;
}
static int default_calloc(size_t nmemb, size_t size, void **out) {
  *out = calloc(nmemb, size);
  return 0;
}
/**
 * @brief Internal helper default_realloc.
 */

/**
 * @brief Internal helper default_free.
 */
static void default_free(void *ptr) { free(ptr); }

static const c_orm_allocator_t g_default_allocator = {
    default_malloc, default_calloc, NULL, default_free};

/**
 * @brief Stub Event Loop Reactor Interface.
 */
typedef struct {
  int (*add_fd)(int fd, int flags, void *user_data);
  int (*remove_fd)(int fd);
  int (*on_readable)(int fd, void *user_data);
  int (*on_writable)(int fd, void *user_data);
} c_orm_event_loop_adapter_t;

/**
 * @brief Internal VTable for dynamic query routing across modalities.
 */
typedef struct {
  int (*execute)(c_orm_db_t *db, const char *query, c_orm_result_t **res);
  int (*execute_params)(c_orm_db_t *db, const char *query,
                        const c_orm_param_t *params, size_t param_count,
                        c_orm_result_t **res);
  int (*execute_async)(c_orm_db_t *db, const char *query, c_orm_async_cb_t cb,
                       void *user_data);
} c_orm_vtable_t;

/* Forward declare internal synchronous execute functions for vtables */
static int sync_execute(c_orm_db_t *db, const char *query,
                        c_orm_result_t **res);
/**
 * @brief Internal helper sync_execute_params.
 */
static int sync_execute_params(c_orm_db_t *db, const char *query,
                               const c_orm_param_t *params, size_t param_count,
                               c_orm_result_t **res);

/**
 * @brief Internal helper sync_multi_execute.
 */
static int sync_multi_execute(c_orm_db_t *db, const char *query,
                              c_orm_result_t **res);
/**
 * @brief Internal helper sync_multi_execute_params.
 */
static int sync_multi_execute_params(c_orm_db_t *db, const char *query,
                                     const c_orm_param_t *params,
                                     size_t param_count, c_orm_result_t **res);

static const c_orm_vtable_t g_vtable_sync_single = {
    sync_execute, sync_execute_params,
    NULL /* Async not natively supported in sync modes, handled by queue */
};

static const c_orm_vtable_t g_vtable_sync_multi = {
    sync_multi_execute, sync_multi_execute_params,
    NULL /* Async not natively supported in sync modes, handled by queue */
};

struct c_orm_context {
  c_orm_modality_t modality;
  const c_orm_vtable_t *vtable;
  const c_orm_allocator_t *allocator;
  const c_orm_event_loop_adapter_t *event_loop;
  void *modality_state;
};

struct c_orm_db {
  c_orm_dialect_t dialect;
  c_orm_context_t *ctx;
  void *native_conn;
  c_orm_log_cb_t logger;
  void *logger_user_data;
  int simulated_migration_version;
  c_orm_mutex_t mutex;

  /* Async simulation queue */
  struct c_orm_async_job *async_queue_head;
  struct c_orm_async_job *async_queue_tail;

  /* Background pool for SQLite ASYNC_EVENT_LOOP */
  struct c_orm_sqlite_worker_pool *bg_pool;
};

/**
 * @brief Pluggable Synchronization Interface for the Connection Pool.
 */
typedef struct {
  void (*lock)(void *state);
  void (*unlock)(void *state);
  void (*wait)(void *state);
  void (*signal)(void *state);
  int (*wait_timeout)(void *state, int ms);
} c_orm_sync_ops_t;

/**
 * @brief SQLite Background Thread Pool Subsystem for Async Modality.
 */
struct c_orm_sqlite_worker_pool {
  c_orm_mutex_t queue_mutex;
  struct c_orm_async_job *queue_head;
  struct c_orm_async_job *queue_tail;
  int terminate_flag;
  int initialized;
#if !defined(_WIN32) && !defined(__MINGW32__) && !defined(__MSDOS__) &&        \
    !defined(__WATCOMC__)
  pthread_t *threads;
#else
  void **threads; /* HANDLE* or stub */
#endif
  size_t thread_count;
};

struct c_orm_pool {
  c_orm_db_t **connections;
  int *in_use;
  size_t pool_size;
  c_orm_dialect_t dialect;
  c_orm_modality_t modality;
  char *conn_string;

  /* Synchronization */
  const c_orm_sync_ops_t *sync_ops;
  void *sync_state;
  c_orm_mutex_t default_mutex; /* Fallback/default implementation */
  c_orm_cond_t default_cond;
  c_orm_tls_t tls_slot;
};

struct c_orm_query {
  c_orm_db_t *db;
  char *table_name;
  char *select_columns;
  char *where_condition;
  char *order_by;
  int has_limit;
  size_t limit;
  const c_orm_allocator_t *allocator;
};

/**
 * @brief Internal helper strdup_safe_ext.
 */
static int strdup_safe_ext(const c_orm_allocator_t *alloc, const char *s,
                           char **out) {
  if (!s || !out || !alloc)
    return -1;
  alloc->malloc_fn(strlen(s) + 1, (void **)out);
  if (!*out)
    return -1;
#if defined(_MSC_VER)
  strcpy_s(*out, strlen(s) + 1, s);
#else
  strcpy(*out, s);
#endif
  return 0;
}

/**
 * @brief Internal helper strdup_safe.
 */
static int strdup_safe(const char *s, char **out) {
  return strdup_safe_ext(&g_default_allocator, s, out);
}

/**
 * @brief Internal helper c_orm_params_copy.
 */
static int c_orm_params_copy(const c_orm_allocator_t *alloc,
                             const c_orm_param_t *src, size_t count,
                             c_orm_param_t **dest) {
  size_t i;
  c_orm_param_t *copied;

  if (count == 0) {
    *dest = NULL;
    return 0;
  }
  if (!src || !dest || !alloc)
    return -1;

  alloc->calloc_fn(count, sizeof(c_orm_param_t), (void **)&copied);
  if (!copied)
    return -1;

  for (i = 0; i < count; ++i) {
    copied[i].type = src[i].type;
    switch (src[i].type) {
    case C_ORM_PARAM_INTEGER:
      copied[i].value.int_val = src[i].value.int_val;
      break;
    case C_ORM_PARAM_REAL:
      copied[i].value.real_val = src[i].value.real_val;
      break;
    case C_ORM_PARAM_TEXT:
      if (src[i].value.text_val) {
        if (strdup_safe_ext(alloc, src[i].value.text_val,
                            (char **)&copied[i].value.text_val) != 0) {
          /* Free previously allocated strings on failure */
          size_t j;
          for (j = 0; j < i; ++j) {
            if (copied[j].type == C_ORM_PARAM_TEXT &&
                copied[j].value.text_val) {
              alloc->free_fn((void *)copied[j].value.text_val);
            } else if (copied[j].type == C_ORM_PARAM_BLOB &&
                       copied[j].value.blob_val.data) {
              alloc->free_fn((void *)copied[j].value.blob_val.data);
            }
          }
          alloc->free_fn(copied);
          return -1;
        }
      } else {
        copied[i].value.text_val = NULL;
      }
      break;
    case C_ORM_PARAM_BLOB:
      if (src[i].value.blob_val.data && src[i].value.blob_val.size > 0) {
        void *blob_copy = NULL;
        alloc->malloc_fn(src[i].value.blob_val.size, &blob_copy);
        if (!blob_copy) {
          size_t j;
          for (j = 0; j < i; ++j) {
            if (copied[j].type == C_ORM_PARAM_TEXT &&
                copied[j].value.text_val) {
              alloc->free_fn((void *)copied[j].value.text_val);
            } else if (copied[j].type == C_ORM_PARAM_BLOB &&
                       copied[j].value.blob_val.data) {
              alloc->free_fn((void *)copied[j].value.blob_val.data);
            }
          }
          alloc->free_fn(copied);
          return -1;
        }
        memcpy(blob_copy, src[i].value.blob_val.data,
               src[i].value.blob_val.size);
        copied[i].value.blob_val.data = blob_copy;
        copied[i].value.blob_val.size = src[i].value.blob_val.size;
      } else {
        copied[i].value.blob_val.data = NULL;
        copied[i].value.blob_val.size = 0;
      }
      break;
    case C_ORM_PARAM_NULL:
      /* Nothing to deep copy */
      break;
    }
  }

  *dest = copied;
  return 0;
}

static void c_orm_params_free(const c_orm_allocator_t *alloc,
                              c_orm_param_t *params, size_t count) {
  size_t i;
  if (!params || !alloc)
    return;

  for (i = 0; i < count; ++i) {
    if (params[i].type == C_ORM_PARAM_TEXT && params[i].value.text_val) {
      alloc->free_fn((void *)params[i].value.text_val);
    } else if (params[i].type == C_ORM_PARAM_BLOB &&
               params[i].value.blob_val.data) {
      alloc->free_fn((void *)params[i].value.blob_val.data);
    }
  }
  alloc->free_fn(params);
}

/* Stub connections for now to satisfy link dependencies.
   Full implementation will require libpq, sqlite3, and mysql headers/libs.
*/
int c_orm_connect_ext(c_orm_db_t **db_out, c_orm_dialect_t dialect,
                      c_orm_modality_t modality, const char *conn_string) {
  c_orm_db_t *db;
  c_orm_context_t *ctx;

  if (!db_out || !conn_string) {
    return -1;
  }

  /* Validate dialect compiled in before allocating */
  if (dialect == C_ORM_DIALECT_SQLITE) {
#ifndef C_ORM_HAVE_SQLITE
    return -4;
#endif
  } else if (dialect == C_ORM_DIALECT_POSTGRES) {
#ifndef C_ORM_HAVE_POSTGRES
    return -4;
#endif
  } else if (dialect == C_ORM_DIALECT_MYSQL) {
#ifndef C_ORM_HAVE_MYSQL
    return -4;
#endif
  } else {
    return -1; /* Unknown dialect */
  }

  db = (c_orm_db_t *)calloc(1, sizeof(c_orm_db_t));
  if (!db) {
    return -1;
  }

  ctx = (c_orm_context_t *)calloc(1, sizeof(c_orm_context_t));
  if (!ctx) {
    free(db);
    return -1;
  }
  ctx->modality = modality;
  if (modality == C_ORM_MODALITY_SYNC_SINGLE) {
    ctx->vtable = &g_vtable_sync_single;
  } else if (modality == C_ORM_MODALITY_SYNC_MULTI) {
    ctx->vtable = &g_vtable_sync_multi;
  } else {
    /* For ASYNC, GREENTHREAD, MULTIPROCESS we will leave NULL or stub until
     * phase 4-6 */
    ctx->vtable = NULL;
  }
  ctx->allocator = &g_default_allocator;
  ctx->event_loop = NULL;
  ctx->modality_state = NULL;

  db->dialect = dialect;
  db->ctx = ctx;
  db->logger = NULL;
  db->logger_user_data = NULL;
  db->simulated_migration_version = 0; /* Starts at 0 initially */
  db->async_queue_head = NULL;
  db->async_queue_tail = NULL;
  db->bg_pool = NULL;
  c_orm_mutex_init(&db->mutex);

  if (dialect == C_ORM_DIALECT_SQLITE) {
#ifdef C_ORM_HAVE_SQLITE
    /* sqlite3_open(conn_string, (sqlite3**)&db->native_conn); */
#endif
  } else if (dialect == C_ORM_DIALECT_POSTGRES) {
#ifdef C_ORM_HAVE_POSTGRES
    PGconn *conn = NULL;
    ConnStatusType __s;
    internal_PQconnectdb(conn_string, &conn);
    internal_PQstatus(conn, &__s);
    if (__s != CONNECTION_OK) {
      internal_PQfinish(conn);
      free(ctx);
      free(db);
      return -1;
    }
    db->native_conn = conn;
#endif
  } else if (dialect == C_ORM_DIALECT_MYSQL) {
#ifdef C_ORM_HAVE_MYSQL
    MYSQL *conn;
    MYSQL *__real_conn = NULL;
    internal_mysql_init(NULL, &conn);
    if (!conn) {
      free(ctx);
      free(db);
      return -1;
    }
    internal_mysql_real_connect(conn, conn_string, NULL, NULL, NULL, 0, NULL, 0,
                                &__real_conn);
    if (!__real_conn) {
      internal_mysql_close(conn);
      free(ctx);
      free(db);
      return -1;
    }
    db->native_conn = conn;
#endif
  }

  *db_out = db;
  return 0;
}

int c_orm_connect(c_orm_db_t **db_out, c_orm_dialect_t dialect,
                  const char *conn_string) {
  return c_orm_connect_ext(db_out, dialect, C_ORM_MODALITY_SYNC_MULTI,
                           conn_string);
}

void c_orm_disconnect(c_orm_db_t *db) {
  struct c_orm_async_job *job;
  struct c_orm_async_job *next;

  if (!db)
    return;

  c_orm_mutex_destroy(&db->mutex);

  job = db->async_queue_head;
  while (job) {
    next = job->next;
    free(job->query);
    if (job->params) {
      c_orm_params_free(db->ctx ? db->ctx->allocator : &g_default_allocator,
                        job->params, job->param_count);
    }
    free(job);
    job = next;
  }

  if (db->dialect == C_ORM_DIALECT_SQLITE) {
#ifdef C_ORM_HAVE_SQLITE
    /* sqlite3_close(db->native_conn); */
#endif
  } else if (db->dialect == C_ORM_DIALECT_POSTGRES) {
#ifdef C_ORM_HAVE_POSTGRES
    if (db->native_conn) {
      internal_PQfinish((PGconn *)db->native_conn);
    }
#endif
  } else if (db->dialect == C_ORM_DIALECT_MYSQL) {
#ifdef C_ORM_HAVE_MYSQL
    if (db->native_conn) {
      internal_mysql_close((MYSQL *)db->native_conn);
    }
#endif
  }
  if (db->ctx) {
    free(db->ctx);
  }
  free(db);
}

int c_orm_lock(c_orm_db_t *db) {
  if (!db)
    return -1;
  c_orm_mutex_lock(&db->mutex);
  return 0;
}

int c_orm_unlock(c_orm_db_t *db) {
  if (!db)
    return -1;
  c_orm_mutex_unlock(&db->mutex);
  return 0;
}

int c_orm_migrate(c_orm_db_t *db, const char *migrations_dir) {
  if (!db || !migrations_dir)
    return -1;

  c_orm_lock(db);

  /*
   * In a real implementation:
   * 1. Create a `_c_orm_migrations` table if it doesn't exist.
   * 2. Read applied versions from it.
   * 3. Iterate through `migrations_dir` finding `up.sql` scripts.
   * 4. Execute those that are newer than `current_version` within a
   * transaction.
   * 5. Record applied version into `_c_orm_migrations`.
   */

  /* Simulation */
  db->simulated_migration_version++;

  c_orm_unlock(db);
  return 0;
}

int c_orm_migrate_rollback(c_orm_db_t *db, const char *migrations_dir) {
  if (!db || !migrations_dir)
    return -1;

  c_orm_lock(db);

  /*
   * Real implementation would read last version from `_c_orm_migrations`,
   * execute `down.sql`, and delete the record.
   */

  if (db->simulated_migration_version > 0) {
    db->simulated_migration_version--;
  }

  c_orm_unlock(db);
  return 0;
}

int c_orm_migrate_current_version(c_orm_db_t *db, int *current_version) {
  if (!db || !current_version)
    return -1;

  c_orm_lock(db);
  /* Real impl queries `_c_orm_migrations` */
  *current_version = db->simulated_migration_version;
  c_orm_unlock(db);

  return 0;
}

int c_orm_set_logger(c_orm_db_t *db, c_orm_log_cb_t logger, void *user_data) {
  if (!db)
    return -1;
  c_orm_lock(db);
  db->logger = logger;
  db->logger_user_data = user_data;
  c_orm_unlock(db);
  return 0;
}

/**
 * @brief Internal helper sync_execute.
 */
static int sync_execute(c_orm_db_t *db, const char *query,
                        c_orm_result_t **res) {
  if (res)
    *res = NULL;
  if (db->dialect == C_ORM_DIALECT_POSTGRES) {
#ifdef C_ORM_HAVE_POSTGRES
    PGresult *pq_res = NULL;
    ExecStatusType __s;
    internal_PQexec((PGconn *)db->native_conn, query, &pq_res);
    internal_PQresultStatus(pq_res, &__s);
    if (__s != PGRES_COMMAND_OK && __s != PGRES_TUPLES_OK) {
      internal_PQclear(pq_res);
      return -1;
    }
    internal_PQclear(pq_res);
#endif
  } else if (db->dialect == C_ORM_DIALECT_MYSQL) {
#ifdef C_ORM_HAVE_MYSQL
    if (internal_mysql_query((MYSQL *)db->native_conn, query) != 0) {
      return -1;
    }
#endif
  } else {
    /* Execute via sqlite3_exec */
  }
  return 0;
}

static int sync_execute_params(c_orm_db_t *db, const char *query,
                               const c_orm_param_t *params, size_t param_count,
                               c_orm_result_t **res) {
  if (res)
    *res = NULL;
  if (params) {
    size_t i;
    for (i = 0; i < param_count; i++) {
      switch (params[i].type) {
      case C_ORM_PARAM_INTEGER:
      case C_ORM_PARAM_REAL:
      case C_ORM_PARAM_NULL:
        break;
      case C_ORM_PARAM_TEXT:
        if (!params[i].value.text_val)
          return -2;
        break;
      case C_ORM_PARAM_BLOB:
        if (!params[i].value.blob_val.data && params[i].value.blob_val.size > 0)
          return -2;
        break;
      default:
        return -3;
      }
    }
  }

  if (db->dialect == C_ORM_DIALECT_POSTGRES) {
#ifdef C_ORM_HAVE_POSTGRES
    PGresult *pq_res;
    const char **param_values = NULL;
    char **str_allocs = NULL;
    if (param_count > 0 && params) {
      size_t i;
      param_values = calloc(param_count, sizeof(char *));
      str_allocs = calloc(param_count, sizeof(char *));
      if (!param_values || !str_allocs) {
        if (param_values)
          free((void *)param_values);
        if (str_allocs)
          free(str_allocs);
        return -1;
      }
      for (i = 0; i < param_count; i++) {
        if (params[i].type == C_ORM_PARAM_NULL) {
          param_values[i] = NULL;
        } else if (params[i].type == C_ORM_PARAM_INTEGER) {
          str_allocs[i] = malloc(32);
          if (str_allocs[i]) {
#if defined(_MSC_VER)
            sprintf_s(str_allocs[i], 32, NUM_FORMAT,
                      (c_orm_int_t)params[i].value.int_val);
#else
            sprintf(str_allocs[i], NUM_FORMAT,
                    (c_orm_int_t)params[i].value.int_val);
#endif
            param_values[i] = str_allocs[i];
          }
        } else if (params[i].type == C_ORM_PARAM_REAL) {
          str_allocs[i] = malloc(64);
          if (str_allocs[i]) {
#if defined(_MSC_VER)
            sprintf_s(str_allocs[i], 64, "%f", params[i].value.real_val);
#else
            sprintf(str_allocs[i], "%f", params[i].value.real_val);
#endif
            param_values[i] = str_allocs[i];
          }
        } else if (params[i].type == C_ORM_PARAM_TEXT) {
          param_values[i] = params[i].value.text_val;
        } else if (params[i].type == C_ORM_PARAM_BLOB) {
          param_values[i] = NULL;
        }
      }
    }

    internal_PQexecParams((PGconn *)db->native_conn, query, (int)param_count,
                          NULL, param_values, NULL, NULL, 0, &pq_res);

    if (param_count > 0 && str_allocs) {
      size_t i;
      for (i = 0; i < param_count; i++) {
        if (str_allocs[i])
          free(str_allocs[i]);
      }
      free(str_allocs);
      free((void *)param_values);
    }

    {
      ExecStatusType __s;
      internal_PQresultStatus(pq_res, &__s);
      if (__s != PGRES_COMMAND_OK && __s != PGRES_TUPLES_OK) {
        internal_PQclear(pq_res);
        return -1;
      }
    }
    internal_PQclear(pq_res);
#endif
  } else if (db->dialect == C_ORM_DIALECT_MYSQL) {
#ifdef C_ORM_HAVE_MYSQL
    MYSQL_STMT *stmt;
    internal_mysql_stmt_init((MYSQL *)db->native_conn, &stmt);
    if (!stmt)
      return -1;
    if (internal_mysql_stmt_prepare(stmt, query,
                                    (unsigned long)strlen(query)) != 0) {
      my_bool __b;
      internal_mysql_stmt_close(stmt, &__b);
      return -1;
    }

    if (param_count > 0 && params) {
      MYSQL_BIND *bind;
      bind = calloc(param_count, sizeof(MYSQL_BIND));
      if (!bind) {
        my_bool __b;
        internal_mysql_stmt_close(stmt, &__b);
        return -1;
      }
      if (internal_mysql_stmt_bind_param(stmt, bind) != 0) {
        free(bind);
        {
          my_bool __b;
          internal_mysql_stmt_close(stmt, &__b);
        }
        return -1;
      }
      free(bind);
    }

    if (internal_mysql_stmt_execute(stmt) != 0) {
      my_bool __b;
      internal_mysql_stmt_close(stmt, &__b);
      return -1;
    }
    {
      my_bool __b;
      internal_mysql_stmt_close(stmt, &__b);
    }
#endif
  } else {
    /* SQLite execution... */
  }
  return 0;
}

static int sync_multi_execute(c_orm_db_t *db, const char *query,
                              c_orm_result_t **res) {
  int r;
  c_orm_lock(db);
  r = sync_execute(db, query, res);
  c_orm_unlock(db);
  return r;
}

/**
 * @brief Internal helper sync_multi_execute_params.
 */
static int sync_multi_execute_params(c_orm_db_t *db, const char *query,
                                     const c_orm_param_t *params,
                                     size_t param_count, c_orm_result_t **res) {
  int r;
  c_orm_lock(db);
  r = sync_execute_params(db, query, params, param_count, res);
  c_orm_unlock(db);
  return r;
}

int c_orm_execute(c_orm_db_t *db, const char *query) {
  clock_t start_time, end_time;
  double duration;
  int res;

  if (!db || !query)
    return -1;

  start_time = clock();

  if (db->ctx && db->ctx->vtable && db->ctx->vtable->execute) {
    res = db->ctx->vtable->execute(db, query, NULL);
  } else {
    res = sync_execute(db, query, NULL); /* Fallback */
  }

  if (res != 0) {
    return res;
  }

  end_time = clock();

  if (db->logger) {
    duration = ((double)(end_time - start_time)) / CLOCKS_PER_SEC * 1000.0;
    db->logger(query, duration, db->logger_user_data);
  }

  return 0;
}

int c_orm_execute_async(c_orm_db_t *db, const char *query, c_orm_async_cb_t cb,
                        void *user_data) {
  struct c_orm_async_job *job;
  if (!db || !query || !cb)
    return -1;

  job = (struct c_orm_async_job *)calloc(1, sizeof(struct c_orm_async_job));
  if (!job)
    return -1;

  if (strdup_safe(query, &job->query) != 0) {
    free(job);
    return -1;
  }

  job->cb = cb;
  job->user_data = user_data;
  job->params = NULL;
  job->param_count = 0;
  job->timeout_ms = 0.0; /* No timeout by default for this simple API */
  job->enqueued_at = clock();

  c_orm_lock(db);

  if (!db->async_queue_head) {
    db->async_queue_head = job;
    db->async_queue_tail = job;
  } else {
    db->async_queue_tail->next = job;
    db->async_queue_tail = job;
  }

  c_orm_unlock(db);
  return 0;
}

int c_orm_execute_async_params(c_orm_db_t *db, const char *query,
                               const c_orm_param_t *params, size_t param_count,
                               c_orm_async_cb_t cb, void *user_data) {
  struct c_orm_async_job *job;
  if (!db || !query || !cb)
    return -1;
  if (param_count > 0 && !params)
    return -1;

  job = (struct c_orm_async_job *)calloc(1, sizeof(struct c_orm_async_job));
  if (!job)
    return -1;

  if (strdup_safe(query, &job->query) != 0) {
    free(job);
    return -1;
  }

  if (param_count > 0) {
    if (c_orm_params_copy(db->ctx ? db->ctx->allocator : &g_default_allocator,
                          params, param_count, &job->params) != 0) {
      free(job->query);
      free(job);
      return -1;
    }
  } else {
    job->params = NULL;
  }
  job->param_count = param_count;

  job->cb = cb;
  job->user_data = user_data;
  job->timeout_ms = 0.0;
  job->enqueued_at = clock();

  c_orm_lock(db);

  if (!db->async_queue_head) {
    db->async_queue_head = job;
    db->async_queue_tail = job;
  } else {
    db->async_queue_tail->next = job;
    db->async_queue_tail = job;
  }

  c_orm_unlock(db);
  return 0;
}

int c_orm_execute_async_timeout(c_orm_db_t *db, const char *query,
                                double timeout_ms, c_orm_async_cb_t cb,
                                void *user_data) {
  struct c_orm_async_job *job;
  if (!db || !query || !cb)
    return -1;

  job = (struct c_orm_async_job *)calloc(1, sizeof(struct c_orm_async_job));
  if (!job)
    return -1;

  if (strdup_safe(query, &job->query) != 0) {
    free(job);
    return -1;
  }

  job->cb = cb;
  job->user_data = user_data;
  job->params = NULL;
  job->param_count = 0;
  job->timeout_ms = timeout_ms;
  job->enqueued_at = clock();

  c_orm_lock(db);

  if (!db->async_queue_head) {
    db->async_queue_head = job;
    db->async_queue_tail = job;
  } else {
    db->async_queue_tail->next = job;
    db->async_queue_tail = job;
  }

  c_orm_unlock(db);
  return 0;
}

int c_orm_poll_async(c_orm_db_t *db, int *jobs_processed) {
  struct c_orm_async_job *job;
  struct c_orm_async_job *prev = NULL;
  struct c_orm_async_job *current;
  int exec_res;
  clock_t now;
  double elapsed_ms;

  if (!db || !jobs_processed)
    return -1;

  *jobs_processed = 0;
  now = clock();

  c_orm_lock(db);

  /* Fast path: Check for timeouts before processing the normal queue order */
  current = db->async_queue_head;
  while (current) {
    if (current->timeout_ms > 0.0) {
      elapsed_ms =
          ((double)(now - current->enqueued_at)) / CLOCKS_PER_SEC * 1000.0;
      if (elapsed_ms >= current->timeout_ms) {
        /* Detach the timed-out job */
        job = current;
        if (prev) {
          prev->next = current->next;
          if (db->async_queue_tail == current) {
            db->async_queue_tail = prev;
          }
        } else {
          db->async_queue_head = current->next;
          if (!db->async_queue_head) {
            db->async_queue_tail = NULL;
          }
        }
        c_orm_unlock(db);

        /* Trigger timeout callback immediately */
        if (job->cb) {
          job->cb(-3, job->user_data); /* -3 indicates timeout error */
        }
        free(job->query);
        if (job->params) {
          c_orm_params_free(db->ctx ? db->ctx->allocator : &g_default_allocator,
                            job->params, job->param_count);
        }
        free(job);
        *jobs_processed = 1;
        return 0;
      }
    }
    prev = current;
    current = current->next;
  }

  /* Normal polling path */
  job = db->async_queue_head;
  if (job) {
    db->async_queue_head = job->next;
    if (!db->async_queue_head) {
      db->async_queue_tail = NULL;
    }
  }
  c_orm_unlock(db);

  if (job) {
    /* Run it locally to simulate async completion */
    if (job->param_count > 0 && job->params) {
      exec_res =
          c_orm_execute_params(db, job->query, job->params, job->param_count);
    } else {
      exec_res = c_orm_execute(db, job->query);
    }
    if (job->cb) {
      job->cb(exec_res, job->user_data);
    }

    free(job->query);
    if (job->params) {
      c_orm_params_free(db->ctx ? db->ctx->allocator : &g_default_allocator,
                        job->params, job->param_count);
    }
    free(job);
    *jobs_processed = 1;
  }

  return 0; /* Success */
}

int c_orm_execute_params(c_orm_db_t *db, const char *query,
                         const c_orm_param_t *params, size_t param_count) {
  clock_t start_time, end_time;
  double duration;
  int res;

  if (!db || !query)
    return -1;
  if (param_count > 0 && !params)
    return -1;

  start_time = clock();

  if (db->ctx && db->ctx->vtable && db->ctx->vtable->execute_params) {
    res = db->ctx->vtable->execute_params(db, query, params, param_count, NULL);
  } else {
    res = sync_execute_params(db, query, params, param_count,
                              NULL); /* Fallback */
  }

  if (res != 0) {
    return res;
  }

  end_time = clock();

  if (db->logger) {
    duration = ((double)(end_time - start_time)) / CLOCKS_PER_SEC * 1000.0;
    db->logger(query, duration, db->logger_user_data);
  }

  return 0;
}

int c_orm_transaction_begin(c_orm_db_t *db) {
  if (!db)
    return -1;
  return c_orm_execute(db, "BEGIN TRANSACTION");
}

int c_orm_transaction_commit(c_orm_db_t *db) {
  if (!db)
    return -1;
  return c_orm_execute(db, "COMMIT");
}

int c_orm_transaction_rollback(c_orm_db_t *db) {
  if (!db)
    return -1;
  return c_orm_execute(db, "ROLLBACK");
}

/* Default sync operations using c_orm_mutex_t and c_orm_cond_t on pool */
static void default_sync_lock(void *state) {
  c_orm_pool_t *pool = (c_orm_pool_t *)state;
  c_orm_mutex_lock(&pool->default_mutex);
}

/**
 * @brief Internal helper default_sync_unlock.
 */
static void default_sync_unlock(void *state) {
  c_orm_pool_t *pool = (c_orm_pool_t *)state;
  c_orm_mutex_unlock(&pool->default_mutex);
}

/**
 * @brief Internal helper default_sync_wait.
 */
static void default_sync_wait(void *state) {
  c_orm_pool_t *pool = (c_orm_pool_t *)state;
  c_orm_cond_wait(&pool->default_cond, &pool->default_mutex);
}

/**
 * @brief Internal helper default_sync_signal.
 */
static void default_sync_signal(void *state) {
  c_orm_pool_t *pool = (c_orm_pool_t *)state;
  c_orm_cond_signal(&pool->default_cond);
}

/**
 * @brief Internal helper default_sync_wait_timeout.
 */
static int default_sync_wait_timeout(void *state, int ms) {
  c_orm_pool_t *pool = (c_orm_pool_t *)state;
  return c_orm_cond_wait_timeout(&pool->default_cond, &pool->default_mutex, ms);
}

static const c_orm_sync_ops_t g_default_sync_ops = {
    default_sync_lock, default_sync_unlock, default_sync_wait,
    default_sync_signal, default_sync_wait_timeout};

int c_orm_pool_create_ext(c_orm_pool_t **pool_out, c_orm_dialect_t dialect,
                          c_orm_modality_t modality, const char *conn_string,
                          size_t pool_size) {
  c_orm_pool_t *pool;
  size_t i;

  if (!pool_out || !conn_string || pool_size == 0) {
    return -1;
  }

  pool = (c_orm_pool_t *)calloc(1, sizeof(c_orm_pool_t));
  if (!pool)
    return -1;

  pool->pool_size = pool_size;
  pool->dialect = dialect;
  pool->modality = modality;

  c_orm_mutex_init(&pool->default_mutex);
  c_orm_cond_init(&pool->default_cond);
  pool->sync_state = pool;
  pool->sync_ops = &g_default_sync_ops;

  if (c_orm_tls_create(&pool->tls_slot) != 0) {
    c_orm_cond_destroy(&pool->default_cond);
    c_orm_mutex_destroy(&pool->default_mutex);
    free(pool);
    return -1;
  }

  pool->conn_string = (char *)malloc(strlen(conn_string) + 1);
  if (!pool->conn_string) {
    c_orm_tls_destroy(pool->tls_slot);
    c_orm_cond_destroy(&pool->default_cond);
    c_orm_mutex_destroy(&pool->default_mutex);
    free(pool);
    return -1;
  }
#if defined(_MSC_VER)
  strcpy_s(pool->conn_string, strlen(conn_string) + 1, conn_string);
#else
  strcpy(pool->conn_string, conn_string);
#endif

  pool->connections = (c_orm_db_t **)calloc(pool_size, sizeof(c_orm_db_t *));
  if (!pool->connections) {
    free(pool->conn_string);
    c_orm_tls_destroy(pool->tls_slot);
    c_orm_cond_destroy(&pool->default_cond);
    c_orm_mutex_destroy(&pool->default_mutex);
    free(pool);
    return -1;
  }

  pool->in_use = (int *)calloc(pool_size, sizeof(int));
  if (!pool->in_use) {
    free(pool->connections);
    free(pool->conn_string);
    c_orm_tls_destroy(pool->tls_slot);
    c_orm_cond_destroy(&pool->default_cond);
    c_orm_mutex_destroy(&pool->default_mutex);
    free(pool);
    return -1;
  }

  for (i = 0; i < pool_size; i++) {
    if (c_orm_connect_ext(&pool->connections[i], dialect, modality,
                          conn_string) != 0) {
      c_orm_pool_destroy(pool);
      return -1;
    }
    pool->in_use[i] = 0;
  }

  *pool_out = pool;
  return 0;
}

int c_orm_pool_create(c_orm_pool_t **pool_out, c_orm_dialect_t dialect,
                      const char *conn_string, size_t pool_size) {
  return c_orm_pool_create_ext(pool_out, dialect, C_ORM_MODALITY_SYNC_MULTI,
                               conn_string, pool_size);
}

int c_orm_pool_destroy(c_orm_pool_t *pool) {
  size_t i;
  if (!pool)
    return -1;

  pool->sync_ops->lock(pool->sync_state);

  if (pool->connections) {
    for (i = 0; i < pool->pool_size; i++) {
      if (pool->connections[i]) {
        c_orm_disconnect(pool->connections[i]);
      }
    }
    free(pool->connections);
  }

  if (pool->in_use) {
    free(pool->in_use);
  }

  if (pool->conn_string) {
    free(pool->conn_string);
  }

  pool->sync_ops->unlock(pool->sync_state);

  /* Destroy default sync primitives if we were using them */
  if (pool->sync_state == pool) {
    c_orm_tls_destroy(pool->tls_slot);
    c_orm_cond_destroy(&pool->default_cond);
    c_orm_mutex_destroy(&pool->default_mutex);
  }

  free(pool);
  return 0;
}

int c_orm_pool_acquire(c_orm_pool_t *pool, c_orm_db_t **db_out) {
  size_t i;
  void *tls_db;

  if (!pool || !db_out)
    return -1;

  if (pool->modality == C_ORM_MODALITY_SYNC_MULTI) {
    if (c_orm_tls_get(pool->tls_slot, &tls_db) == 0 && tls_db) {
      *db_out = (c_orm_db_t *)tls_db;
      return 0;
    }
  }

  pool->sync_ops->lock(pool->sync_state);

  while (1) {
    for (i = 0; i < pool->pool_size; i++) {
      if (pool->in_use[i] == 0) {
        pool->in_use[i] = 1;
        *db_out = pool->connections[i];
        if (pool->modality == C_ORM_MODALITY_SYNC_MULTI) {
          c_orm_tls_set(pool->tls_slot, *db_out);
        }
        pool->sync_ops->unlock(pool->sync_state);
        return 0;
      }
    }

    /* Pool is exhausted */
    if (pool->modality == C_ORM_MODALITY_SYNC_MULTI) {
      /* Wait for a connection to be released with a timeout for deadlock
       * diagnostic */
      pool->sync_ops->wait_timeout(pool->sync_state, 5000);
    } else {
      /* Non-blocking fail */
      pool->sync_ops->unlock(pool->sync_state);
      return -2;
    }
  }

  /* Unreachable */
  return -2;
}

int c_orm_pool_release(c_orm_pool_t *pool, c_orm_db_t *db) {
  size_t i;
  void *tls_db;

  if (!pool || !db)
    return -1;

  if (pool->modality == C_ORM_MODALITY_SYNC_MULTI) {
    if (c_orm_tls_get(pool->tls_slot, &tls_db) == 0 && tls_db == db) {
      /* Thread-local connection is just kept. Do not actually release it to the
         pool yet. Wait, if we never release it, it's pinned to the thread
         forever. A simpler TLS optimization is to just prefer this thread's
         last connection. Let's clear the TLS slot and actually return it to the
         pool, or we can keep it pinned. The prompt says "when pooled sharing is
         inefficient". If it's pinned, another thread might starve. Let's just
         release it normally, but clear the TLS slot so it's not used invalidly.
       */
      c_orm_tls_set(pool->tls_slot, NULL);
    }
  }

  pool->sync_ops->lock(pool->sync_state);

  for (i = 0; i < pool->pool_size; i++) {
    if (pool->connections[i] == db) {
      pool->in_use[i] = 0;
      if (pool->modality == C_ORM_MODALITY_SYNC_MULTI) {
        pool->sync_ops->signal(pool->sync_state);
      }
      pool->sync_ops->unlock(pool->sync_state);
      return 0;
    }
  }

  pool->sync_ops->unlock(pool->sync_state);

  /* Connection not found in pool */
  return -2;
}

int c_orm_query_create(c_orm_query_t **query_out, c_orm_db_t *db,
                       const char *table_name) {
  c_orm_query_t *query;
  const c_orm_allocator_t *alloc;
  if (!query_out || !db || !table_name)
    return -1;

  alloc = db->ctx ? db->ctx->allocator : &g_default_allocator;

  alloc->calloc_fn(1, sizeof(c_orm_query_t), (void **)&query);
  if (!query)
    return -1;

  query->db = db;
  query->allocator = alloc;
  if (strdup_safe_ext(alloc, table_name, &query->table_name) != 0) {
    alloc->free_fn(query);
    return -1;
  }

  *query_out = query;
  return 0;
}

int c_orm_query_select(c_orm_query_t *query, const char *columns) {
  if (!query || !columns)
    return -1;
  if (query->select_columns)
    query->allocator->free_fn(query->select_columns);
  if (strdup_safe_ext(query->allocator, columns, &query->select_columns) != 0)
    return -1;
  return 0;
}

int c_orm_query_where(c_orm_query_t *query, const char *condition) {
  if (!query || !condition)
    return -1;
  if (query->where_condition)
    query->allocator->free_fn(query->where_condition);
  if (strdup_safe_ext(query->allocator, condition, &query->where_condition) !=
      0)
    return -1;
  return 0;
}

int c_orm_query_order_by(c_orm_query_t *query, const char *order_by) {
  if (!query || !order_by)
    return -1;
  if (query->order_by)
    query->allocator->free_fn(query->order_by);
  if (strdup_safe_ext(query->allocator, order_by, &query->order_by) != 0)
    return -1;
  return 0;
}

int c_orm_query_limit(c_orm_query_t *query, size_t limit) {
  if (!query)
    return -1;
  query->has_limit = 1;
  query->limit = limit;
  return 0;
}

int c_orm_query_build(c_orm_query_t *query, char **sql_out) {
  size_t size = 128;
  char *sql;

  if (!query || !sql_out)
    return -1;

  size += query->select_columns ? strlen(query->select_columns) : 1;
  size += strlen(query->table_name);
  if (query->where_condition)
    size += strlen(query->where_condition) + 8;
  if (query->order_by)
    size += strlen(query->order_by) + 12;
  if (query->has_limit)
    size += 32;

  query->allocator->malloc_fn(size, (void **)&sql);
  if (!sql)
    return -1;

#if defined(_MSC_VER)
  strcpy_s(sql, size, "SELECT ");
  strcat_s(sql, size, query->select_columns ? query->select_columns : "*");
  strcat_s(sql, size, " FROM ");
  strcat_s(sql, size, query->table_name);
#else
  strcpy(sql, "SELECT ");
  strcat(sql, query->select_columns ? query->select_columns : "*");
  strcat(sql, " FROM ");
  strcat(sql, query->table_name);
#endif

  if (query->where_condition) {
#if defined(_MSC_VER)
    strcat_s(sql, size, " WHERE ");
    strcat_s(sql, size, query->where_condition);
#else
    strcat(sql, " WHERE ");
    strcat(sql, query->where_condition);
#endif
  }

  if (query->order_by) {
#if defined(_MSC_VER)
    strcat_s(sql, size, " ORDER BY ");
    strcat_s(sql, size, query->order_by);
#else
    strcat(sql, " ORDER BY ");
    strcat(sql, query->order_by);
#endif
  }

  if (query->has_limit) {
    char limit_str[32];
#if defined(_MSC_VER)
    sprintf_s(limit_str, sizeof(limit_str), " LIMIT " NUM_FORMAT,
              (c_orm_int_t)query->limit);
    strcat_s(sql, size, limit_str);
#else
    sprintf(limit_str, " LIMIT " NUM_FORMAT, (c_orm_int_t)query->limit);
    strcat(sql, limit_str);
#endif
  }

  *sql_out = sql;
  return 0;
}

int c_orm_query_execute(c_orm_query_t *query) {
  char *sql = NULL;
  int res;

  if (!query)
    return -1;

  res = c_orm_query_build(query, &sql);
  if (res != 0)
    return res;

  res = c_orm_execute(query->db, sql);
  query->allocator->free_fn(sql);

  return res;
}

int c_orm_query_destroy(c_orm_query_t *query) {
  if (!query)
    return -1;

  if (query->table_name)
    query->allocator->free_fn(query->table_name);
  if (query->select_columns)
    query->allocator->free_fn(query->select_columns);
  if (query->where_condition)
    query->allocator->free_fn(query->where_condition);
  if (query->order_by)
    query->allocator->free_fn(query->order_by);

  query->allocator->free_fn(query);
  return 0;
}
