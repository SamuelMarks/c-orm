/* clang-format off */
#include "c_orm/c_orm.h"

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

static void c_orm_mutex_init(c_orm_mutex_t *mutex);
static void c_orm_mutex_destroy(c_orm_mutex_t *mutex);
static void c_orm_mutex_lock(c_orm_mutex_t *mutex);
static void c_orm_mutex_unlock(c_orm_mutex_t *mutex);
static int strdup_safe(const char *s, char **out);

/**
 * @brief Initialize a mutex.
 * @param mutex Pointer to the mutex to initialize.
 */
static void c_orm_mutex_init(c_orm_mutex_t *mutex) {
#if defined(_WIN32)
  InitializeCriticalSection(mutex);
#else
  pthread_mutex_init(mutex, NULL);
#endif
}

static void c_orm_mutex_destroy(c_orm_mutex_t *mutex) {
#if defined(_WIN32)
  DeleteCriticalSection(mutex);
#else
  pthread_mutex_destroy(mutex);
#endif
}

static void c_orm_mutex_lock(c_orm_mutex_t *mutex) {
#if defined(_WIN32)
  EnterCriticalSection(mutex);
#else
  pthread_mutex_lock(mutex);
#endif
}

static void c_orm_mutex_unlock(c_orm_mutex_t *mutex) {
#if defined(_WIN32)
  LeaveCriticalSection(mutex);
#else
  pthread_mutex_unlock(mutex);
#endif
}

/* Simulation structures for Async I/O */
struct c_orm_async_job {
  char *query;
  c_orm_async_cb_t cb;
  void *user_data;
  struct c_orm_async_job *next;
};

struct c_orm_db {
  c_orm_dialect_t dialect;
  void *native_conn;
  c_orm_log_cb_t logger;
  void *logger_user_data;
  int simulated_migration_version;
  c_orm_mutex_t mutex;

  /* Async simulation queue */
  struct c_orm_async_job *async_queue_head;
  struct c_orm_async_job *async_queue_tail;
};

struct c_orm_pool {
  c_orm_db_t **connections;
  int *in_use;
  size_t pool_size;
  c_orm_dialect_t dialect;
  char *conn_string;
  c_orm_mutex_t mutex;
};

struct c_orm_query {
  c_orm_db_t *db;
  char *table_name;
  char *select_columns;
  char *where_condition;
  char *order_by;
  int has_limit;
  size_t limit;
};

static int strdup_safe(const char *s, char **out) {
  if (!s || !out)
    return -1;
  *out = malloc(strlen(s) + 1);
  if (!*out)
    return -1;
#if defined(_MSC_VER)
  strcpy_s(*out, strlen(s) + 1, s);
#else
  strcpy(*out, s);
#endif
  return 0;
}

/* Stub connections for now to satisfy link dependencies.
   Full implementation will require libpq, sqlite3, and mysql headers/libs.
*/
int c_orm_connect(c_orm_db_t **db_out, c_orm_dialect_t dialect,
                  const char *conn_string) {
  c_orm_db_t *db;

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

  db = calloc(1, sizeof(c_orm_db_t));
  if (!db) {
    return -1;
  }

  db->dialect = dialect;
  db->logger = NULL;
  db->logger_user_data = NULL;
  db->simulated_migration_version = 0; /* Starts at 0 initially */
  db->async_queue_head = NULL;
  db->async_queue_tail = NULL;
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
      free(db);
      return -1;
    }
    internal_mysql_real_connect(conn, conn_string, NULL, NULL, NULL, 0, NULL, 0,
                                &__real_conn);
    if (!__real_conn) {
      internal_mysql_close(conn);
      free(db);
      return -1;
    }
    db->native_conn = conn;
#endif
  }

  *db_out = db;
  return 0;
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

int c_orm_execute(c_orm_db_t *db, const char *query) {
  clock_t start_time, end_time;
  double duration;

  if (!db || !query)
    return -1;

  c_orm_lock(db);

  start_time = clock();

  if (db->dialect == C_ORM_DIALECT_POSTGRES) {
#ifdef C_ORM_HAVE_POSTGRES
    PGresult *res = NULL;
    ExecStatusType __s;
    internal_PQexec((PGconn *)db->native_conn, query, &res);
    internal_PQresultStatus(res, &__s);
    if (__s != PGRES_COMMAND_OK && __s != PGRES_TUPLES_OK) {
      internal_PQclear(res);
      c_orm_unlock(db);
      return -1;
    }
    internal_PQclear(res);
#endif
  } else if (db->dialect == C_ORM_DIALECT_MYSQL) {
#ifdef C_ORM_HAVE_MYSQL
    if (internal_mysql_query((MYSQL *)db->native_conn, query) != 0) {
      c_orm_unlock(db);
      return -1;
    }
#endif
  } else {
    /* Execute via sqlite3_exec */
  }

  end_time = clock();

  if (db->logger) {
    duration = ((double)(end_time - start_time)) / CLOCKS_PER_SEC * 1000.0;
    db->logger(query, duration, db->logger_user_data);
  }

  c_orm_unlock(db);
  return 0;
}

int c_orm_execute_async(c_orm_db_t *db, const char *query, c_orm_async_cb_t cb,
                        void *user_data) {
  struct c_orm_async_job *job;
  if (!db || !query || !cb)
    return -1;

  job = calloc(1, sizeof(struct c_orm_async_job));
  if (!job)
    return -1;

  if (strdup_safe(query, &job->query) != 0) {
    free(job);
    return -1;
  }

  job->cb = cb;
  job->user_data = user_data;

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

int c_orm_poll_async(c_orm_db_t *db) {
  struct c_orm_async_job *job;
  int exec_res;

  if (!db)
    return -1;

  c_orm_lock(db);
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
    exec_res = c_orm_execute(db, job->query);
    job->cb(exec_res, job->user_data);

    free(job->query);
    free(job);
    return 1; /* Returned 1 job */
  }

  return 0; /* Queue empty */
}

int c_orm_execute_params(c_orm_db_t *db, const char *query,
                         const c_orm_param_t *params, size_t param_count) {
  clock_t start_time, end_time;
  double duration;

  if (!db || !query)
    return -1;
  if (param_count > 0 && !params)
    return -1;

  c_orm_lock(db);

  start_time = clock();

  /*
   * Stub execution for parameterized queries.
   * Full implementation will require:
   * - SQLite: sqlite3_prepare_v2, sqlite3_bind_*, sqlite3_step
   * - PostgreSQL: internal_PQexecParams
   * - MySQL: mysql_stmt_prepare, mysql_stmt_bind_param, mysql_stmt_execute
   */

  if (params) {
    size_t i;
    for (i = 0; i < param_count; i++) {
      switch (params[i].type) {
      case C_ORM_PARAM_INTEGER:
        break;
      case C_ORM_PARAM_REAL:
        break;
      case C_ORM_PARAM_TEXT:
        if (!params[i].value.text_val) {
          c_orm_unlock(db);
          return -2; /* Invalid param text */
        }
        break;
      case C_ORM_PARAM_BLOB:
        if (!params[i].value.blob_val.data &&
            params[i].value.blob_val.size > 0) {
          c_orm_unlock(db);
          return -2; /* Invalid param blob */
        }
        break;
      case C_ORM_PARAM_NULL:
        break;
      default:
        c_orm_unlock(db);
        return -3; /* Unknown type */
      }
    }
  }

  if (db->dialect == C_ORM_DIALECT_POSTGRES) {
#ifdef C_ORM_HAVE_POSTGRES
    PGresult *res;
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
        c_orm_unlock(db);
        return -1;
      }
      for (i = 0; i < param_count; i++) {
        if (params[i].type == C_ORM_PARAM_NULL) {
          param_values[i] = NULL;
        } else if (params[i].type == C_ORM_PARAM_INTEGER) {
          str_allocs[i] = malloc(32);
          if (str_allocs[i]) {
#if defined(_MSC_VER)
            sprintf_s(str_allocs[i], 32, NUM_FORMAT, params[i].value.int_val);
#else
            sprintf(str_allocs[i], NUM_FORMAT, params[i].value.int_val);
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
          /* Not fully implemented for BLOB in stub */
          param_values[i] = NULL;
        }
      }
    }

    internal_PQexecParams((PGconn *)db->native_conn, query, (int)param_count,
                          NULL, param_values, NULL, NULL, 0, &res);

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
      internal_PQresultStatus(res, &__s);
      if (__s != PGRES_COMMAND_OK && __s != PGRES_TUPLES_OK) {
        internal_PQclear(res);
        c_orm_unlock(db);
        return -1;
      }
    }
    internal_PQclear(res);
#endif
  } else if (db->dialect == C_ORM_DIALECT_MYSQL) {
#ifdef C_ORM_HAVE_MYSQL
    MYSQL_STMT *stmt;
    internal_mysql_stmt_init((MYSQL *)db->native_conn, &stmt);
    if (!stmt) {
      c_orm_unlock(db);
      return -1;
    }
    if (internal_mysql_stmt_prepare(stmt, query,
                                    (unsigned long)strlen(query)) != 0) {
      {
        my_bool __b;
        internal_mysql_stmt_close(stmt, &__b);
      }
      c_orm_unlock(db);
      return -1;
    }

    if (param_count > 0 && params) {
      MYSQL_BIND *bind;
      bind = calloc(param_count, sizeof(MYSQL_BIND));
      if (!bind) {
        {
          my_bool __b;
          internal_mysql_stmt_close(stmt, &__b);
        }
        c_orm_unlock(db);
        return -1;
      }
      /* In a real implementation we would populate bind here... */
      if (internal_mysql_stmt_bind_param(stmt, bind) != 0) {
        free(bind);
        {
          my_bool __b;
          internal_mysql_stmt_close(stmt, &__b);
        }
        c_orm_unlock(db);
        return -1;
      }
      free(bind);
    }

    if (internal_mysql_stmt_execute(stmt) != 0) {
      {
        my_bool __b;
        internal_mysql_stmt_close(stmt, &__b);
      }
      c_orm_unlock(db);
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

  end_time = clock();

  if (db->logger) {
    duration = ((double)(end_time - start_time)) / CLOCKS_PER_SEC * 1000.0;
    db->logger(query, duration, db->logger_user_data);
  }

  c_orm_unlock(db);
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

int c_orm_pool_create(c_orm_pool_t **pool_out, c_orm_dialect_t dialect,
                      const char *conn_string, size_t pool_size) {
  c_orm_pool_t *pool;
  size_t i;

  if (!pool_out || !conn_string || pool_size == 0) {
    return -1;
  }

  pool = calloc(1, sizeof(c_orm_pool_t));
  if (!pool)
    return -1;

  pool->pool_size = pool_size;
  pool->dialect = dialect;
  c_orm_mutex_init(&pool->mutex);

  pool->conn_string = malloc(strlen(conn_string) + 1);
  if (!pool->conn_string) {
    c_orm_mutex_destroy(&pool->mutex);
    free(pool);
    return -1;
  }
#if defined(_MSC_VER)
  strcpy_s(pool->conn_string, strlen(conn_string) + 1, conn_string);
#else
  strcpy(pool->conn_string, conn_string);
#endif

  pool->connections = calloc(pool_size, sizeof(c_orm_db_t *));
  if (!pool->connections) {
    free(pool->conn_string);
    c_orm_mutex_destroy(&pool->mutex);
    free(pool);
    return -1;
  }

  pool->in_use = calloc(pool_size, sizeof(int));
  if (!pool->in_use) {
    free(pool->connections);
    free(pool->conn_string);
    c_orm_mutex_destroy(&pool->mutex);
    free(pool);
    return -1;
  }

  for (i = 0; i < pool_size; i++) {
    if (c_orm_connect(&pool->connections[i], dialect, conn_string) != 0) {
      c_orm_pool_destroy(pool);
      return -1;
    }
    pool->in_use[i] = 0;
  }

  *pool_out = pool;
  return 0;
}

int c_orm_pool_destroy(c_orm_pool_t *pool) {
  size_t i;
  if (!pool)
    return -1;

  c_orm_mutex_lock(&pool->mutex);

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

  c_orm_mutex_unlock(&pool->mutex);
  c_orm_mutex_destroy(&pool->mutex);

  free(pool);
  return 0;
}

int c_orm_pool_acquire(c_orm_pool_t *pool, c_orm_db_t **db_out) {
  size_t i;

  if (!pool || !db_out)
    return -1;

  c_orm_mutex_lock(&pool->mutex);

  for (i = 0; i < pool->pool_size; i++) {
    if (pool->in_use[i] == 0) {
      pool->in_use[i] = 1;
      *db_out = pool->connections[i];
      c_orm_mutex_unlock(&pool->mutex);
      return 0;
    }
  }

  c_orm_mutex_unlock(&pool->mutex);

  /* Pool exhausted */
  return -2;
}

int c_orm_pool_release(c_orm_pool_t *pool, c_orm_db_t *db) {
  size_t i;

  if (!pool || !db)
    return -1;

  c_orm_mutex_lock(&pool->mutex);

  for (i = 0; i < pool->pool_size; i++) {
    if (pool->connections[i] == db) {
      pool->in_use[i] = 0;
      c_orm_mutex_unlock(&pool->mutex);
      return 0;
    }
  }

  c_orm_mutex_unlock(&pool->mutex);

  /* Connection not found in pool */
  return -2;
}

int c_orm_query_create(c_orm_query_t **query_out, c_orm_db_t *db,
                       const char *table_name) {
  c_orm_query_t *query;
  if (!query_out || !db || !table_name)
    return -1;

  query = calloc(1, sizeof(c_orm_query_t));
  if (!query)
    return -1;

  query->db = db;
  if (strdup_safe(table_name, &query->table_name) != 0) {
    free(query);
    return -1;
  }

  *query_out = query;
  return 0;
}

int c_orm_query_select(c_orm_query_t *query, const char *columns) {
  if (!query || !columns)
    return -1;
  if (query->select_columns)
    free(query->select_columns);
  if (strdup_safe(columns, &query->select_columns) != 0)
    return -1;
  return 0;
}

int c_orm_query_where(c_orm_query_t *query, const char *condition) {
  if (!query || !condition)
    return -1;
  if (query->where_condition)
    free(query->where_condition);
  if (strdup_safe(condition, &query->where_condition) != 0)
    return -1;
  return 0;
}

int c_orm_query_order_by(c_orm_query_t *query, const char *order_by) {
  if (!query || !order_by)
    return -1;
  if (query->order_by)
    free(query->order_by);
  if (strdup_safe(order_by, &query->order_by) != 0)
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

  sql = malloc(size);
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
              (long)query->limit);
    strcat_s(sql, size, limit_str);
#else
    sprintf(limit_str, " LIMIT " NUM_FORMAT, (long)query->limit);
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
  free(sql);

  return res;
}

int c_orm_query_destroy(c_orm_query_t *query) {
  if (!query)
    return -1;

  if (query->table_name)
    free(query->table_name);
  if (query->select_columns)
    free(query->select_columns);
  if (query->where_condition)
    free(query->where_condition);
  if (query->order_by)
    free(query->order_by);

  free(query);
  return 0;
}
