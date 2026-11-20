/**
 * @file migration_runner.c
 * @brief Implementation of database migrations logic using libpq.
 *
 * @author Samuel Marks
 */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "migration_runner.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if !defined(__EMSCRIPTEN__) && defined(USE_LIBPQ_LINKED)
#include <libpq-fe.h>
#endif /* !defined(__EMSCRIPTEN__) && USE_LIBPQ_LINKED */

#include "migration.h"
#include "functions/parse/fs.h"
#include "cfs/cfs.h"
#include "c89stringutils_string_extras.h"

#if !defined(__EMSCRIPTEN__) && (defined(USE_LIBPQ_LINKED) || defined(USE_LIBPQ_DYNAMIC))

#if defined(USE_LIBPQ_DYNAMIC)
#if defined(_WIN32)
#include "win_compat_sym.h"
#include <winbase.h>
#include <windef.h>
#if !defined(C_ORM_NO_THREADS) && defined(_MSC_VER)
#include <intrin.h>
#endif
#else
#include <dlfcn.h>
#if !defined(C_ORM_NO_THREADS)
#include <pthread.h>
#endif
#endif
#endif /* USE_LIBPQ_DYNAMIC */
#endif
/* clang-format on */

#if !defined(__EMSCRIPTEN__) &&                                                \
    (defined(USE_LIBPQ_LINKED) || defined(USE_LIBPQ_DYNAMIC))

#if defined(USE_LIBPQ_DYNAMIC)
typedef struct pg_conn PGconn;
typedef struct pg_result PGresult;

#define CONNECTION_OK 0
#define PGRES_COMMAND_OK 1
#define PGRES_TUPLES_OK 2

typedef PGconn *(*PQconnectdb_t)(const char *conninfo);
typedef int (*PQstatus_t)(const PGconn *conn);
typedef char *(*PQerrorMessage_t)(const PGconn *conn);
typedef void (*PQfinish_t)(PGconn *conn);
typedef PGresult *(*PQexec_t)(PGconn *conn, const char *query);
typedef int (*PQresultStatus_t)(const PGresult *res);
typedef int (*PQntuples_t)(const PGresult *res);
typedef char *(*PQgetvalue_t)(const PGresult *res, int tup_num, int field_num);
typedef void (*PQclear_t)(PGresult *res);
typedef PGresult *(*PQexecParams_t)(PGconn *conn, const char *command,
                                    int nParams, const unsigned int *paramTypes,
                                    const char *const *paramValues,
                                    const int *paramLengths,
                                    const int *paramFormats, int resultFormat);

static PQconnectdb_t dyn_PQconnectdb = NULL;
static PQstatus_t dyn_PQstatus = NULL;
static PQerrorMessage_t dyn_PQerrorMessage = NULL;
static PQfinish_t dyn_PQfinish = NULL;
static PQexec_t dyn_PQexec = NULL;
static PQresultStatus_t dyn_PQresultStatus = NULL;
static PQntuples_t dyn_PQntuples = NULL;
static PQgetvalue_t dyn_PQgetvalue = NULL;
static PQclear_t dyn_PQclear = NULL;
static PQexecParams_t dyn_PQexecParams = NULL;

#if !defined(C_ORM_NO_THREADS)
#if defined(_WIN32)
static volatile long mig_libpq_lock = 0;
#else
static pthread_mutex_t mig_libpq_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif
#endif

static c_orm_error_t load_libpq(void) {
  static int loaded = 0;
  void *handle;
#if !defined(C_ORM_NO_THREADS)
#if defined(_MSC_VER)
  while (_InterlockedCompareExchange(&mig_libpq_lock, 1L, 0L) != 0L) {
  }
#elif defined(_WIN32)
  while (__sync_val_compare_and_swap(&mig_libpq_lock, 0L, 1L) != 0L) {
  }
#else
  if (pthread_mutex_lock(&mig_libpq_mutex) != 0) {
    return C_ORM_ERROR_BUSY;
  }
#endif
#endif
  if (loaded) {
#if !defined(C_ORM_NO_THREADS)
#if defined(_WIN32)
    mig_libpq_lock = 0;
#else
    (void)pthread_mutex_unlock(&mig_libpq_mutex);
#endif
#endif
    return C_ORM_OK;
  }
#if defined(_WIN32)
  handle = LoadLibraryA("libpq.dll");
#else
  handle = dlopen("libpq.so", RTLD_LAZY);
#endif
  if (!handle) {
#if !defined(C_ORM_NO_THREADS)
#if defined(_WIN32)
    mig_libpq_lock = 0;
#else
    (void)pthread_mutex_unlock(&mig_libpq_mutex);
#endif
#endif
    return C_ORM_ERROR_NOT_FOUND;
  }

#if defined(_WIN32)
#define LOAD_SYM(name, type)                                                   \
  dyn_##name = (type)GetProcAddress((HMODULE)handle, #name)
#else
#define LOAD_SYM(name, type) dyn_##name = (type)dlsym(handle, #name)
#endif

  LOAD_SYM(PQconnectdb, PQconnectdb_t);
  LOAD_SYM(PQstatus, PQstatus_t);
  LOAD_SYM(PQerrorMessage, PQerrorMessage_t);
  LOAD_SYM(PQfinish, PQfinish_t);
  LOAD_SYM(PQexec, PQexec_t);
  LOAD_SYM(PQresultStatus, PQresultStatus_t);
  LOAD_SYM(PQntuples, PQntuples_t);
  LOAD_SYM(PQgetvalue, PQgetvalue_t);
  LOAD_SYM(PQclear, PQclear_t);
  LOAD_SYM(PQexecParams, PQexecParams_t);

  if (!dyn_PQconnectdb || !dyn_PQexec || !dyn_PQfinish) {
#if !defined(C_ORM_NO_THREADS)
#if defined(_WIN32)
    mig_libpq_lock = 0;
#else
    (void)pthread_mutex_unlock(&mig_libpq_mutex);
#endif
#endif
    return C_ORM_ERROR_NOT_FOUND;
  }

  loaded = 1;
#if !defined(C_ORM_NO_THREADS)
#if defined(_WIN32)
  mig_libpq_lock = 0;
#else
  (void)pthread_mutex_unlock(&mig_libpq_mutex);
#endif
#endif
  return C_ORM_OK;
}

#define PQconnectdb dyn_PQconnectdb
#define PQstatus dyn_PQstatus
#define PQerrorMessage dyn_PQerrorMessage
#define PQfinish dyn_PQfinish
#define PQexec dyn_PQexec
#define PQresultStatus dyn_PQresultStatus
#define PQntuples dyn_PQntuples
#define PQgetvalue dyn_PQgetvalue
#define PQclear dyn_PQclear
#define PQexecParams dyn_PQexecParams

#define ENSURE_LIBPQ()                                                         \
  do {                                                                         \
    c_orm_error_t _libpq_rc = load_libpq();                                    \
    if (_libpq_rc != C_ORM_OK) {                                               \
      fprintf(stderr, "Error: PostgreSQL dynamic library (libpq) not found "   \
                      "at runtime.\n");                                        \
      return _libpq_rc;                                                        \
    }                                                                          \
  } while (0)

#else
static c_orm_error_t load_libpq(void) { return C_ORM_OK; }
#define ENSURE_LIBPQ()
#endif /* USE_LIBPQ_DYNAMIC */

/**
 * @brief Helper struct for storing a migration file info.
 */
struct MigrationFile {
  char *filepath;
  char *version;
};

/**
 * @brief Helper list for collecting migration files.
 */
struct MigrationList {
  struct MigrationFile *files;
  size_t count;
  size_t capacity;
};

/**
 * @brief Create the schema_migrations table if it doesn't exist.
 *
 * @param[in] conn Active PGconn.
 * @return C_ORM_OK on success, error code otherwise.
 */
static c_orm_error_t ensure_schema_migrations_table(PGconn *conn) {
  const char *query = "CREATE TABLE IF NOT EXISTS schema_migrations ("
                      "version VARCHAR(255) PRIMARY KEY, "
                      "applied_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
                      ");";
  PGresult *res = PQexec(conn, query);
  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    fprintf(stderr, "Failed to create schema_migrations table: %s\n",
            PQerrorMessage(conn));
    PQclear(res);
    return C_ORM_ERROR_SQL;
  }
  PQclear(res);
  return C_ORM_OK;
}

/**
 * @brief Collects `.sql` migration files in a directory using c-fs directory
 * iteration, returning `c_orm_error_t` directly without integer callbacks.
 *
 * @param[in] migrations_dir Directory path to scan.
 * @param[in,out] list Pointer to MigrationList receiving discovered files.
 * @return C_ORM_OK on success, or a c_orm_error_t code on failure.
 */
static c_orm_error_t collect_migrations_from_dir(const char *migrations_dir,
                                                 struct MigrationList *list) {
  cfs_path dir_path;
  cfs_directory_iterator *it = NULL;
  const cfs_directory_entry *entry = NULL;
  cfs_error_code ec;
  cfs_errc cfs_rc;

  if (!migrations_dir || !list) {
    return C_ORM_ERROR_VALIDATION;
  }

  cfs_path_init(&dir_path);
#if defined(CFS_OS_WINDOWS) && defined(CFS_UNICODE)
  {
    cfs_size_t req = 0;
    wchar_t *wdir = NULL;
    cfs_rc = cfs_utf8_to_utf16(migrations_dir, NULL, 0, &req);
    if (cfs_rc != cfs_errc_success || req == 0) {
      cfs_path_destroy(&dir_path);
      return C_ORM_ERROR_IO;
    }
    wdir = (wchar_t *)malloc(req * sizeof(wchar_t));
    if (!wdir) {
      cfs_path_destroy(&dir_path);
      return C_ORM_ERROR_MEMORY;
    }
    cfs_rc = cfs_utf8_to_utf16(migrations_dir, wdir, req, NULL);
    if (cfs_rc != cfs_errc_success) {
      free(wdir);
      cfs_path_destroy(&dir_path);
      return C_ORM_ERROR_IO;
    }
    cfs_rc = cfs_path_init_str(&dir_path, wdir);
    free(wdir);
  }
#else
  cfs_rc = cfs_path_init_str(&dir_path, migrations_dir);
#endif
  if (cfs_rc != cfs_errc_success) {
    cfs_path_destroy(&dir_path);
    return C_ORM_ERROR_IO;
  }

  memset(&ec, 0, sizeof(ec));
  cfs_rc = cfs_dir_itr_init(&dir_path, &it, &ec);
  cfs_path_destroy(&dir_path);
  if (cfs_rc != cfs_errc_success || !it) {
    return C_ORM_ERROR_NOT_FOUND;
  }

  for (;;) {
    const cfs_char_t *raw_path = NULL;
    char *utf8_path = NULL;
    size_t len;
    char *basename = NULL;
    c_orm_error_t rc;

    cfs_rc = cfs_dir_itr_next(it, &entry, &ec);
    if ((int)cfs_rc == 1 || !entry) {
      break;
    }
    if (cfs_rc != cfs_errc_success) {
      cfs_dir_itr_close(it);
      return C_ORM_ERROR_IO;
    }

    if (cfs_path_c_str(&entry->path, &raw_path) != cfs_errc_success ||
        !raw_path) {
      continue;
    }

#if defined(CFS_OS_WINDOWS) && defined(CFS_UNICODE)
    {
      cfs_size_t req = 0;
      if (cfs_utf16_to_utf8(raw_path, NULL, 0, &req) != cfs_errc_success ||
          req == 0) {
        cfs_dir_itr_close(it);
        return C_ORM_ERROR_IO;
      }
      utf8_path = (char *)malloc(req);
      if (!utf8_path) {
        cfs_dir_itr_close(it);
        return C_ORM_ERROR_MEMORY;
      }
      if (cfs_utf16_to_utf8(raw_path, utf8_path, req, NULL) !=
          cfs_errc_success) {
        free(utf8_path);
        cfs_dir_itr_close(it);
        return C_ORM_ERROR_IO;
      }
    }
#else
    if (c_cdd_strdup(raw_path, &utf8_path) != CDD_C_SUCCESS || !utf8_path) {
      cfs_dir_itr_close(it);
      return C_ORM_ERROR_MEMORY;
    }
#endif

    len = strlen(utf8_path);
    if (strcmp(utf8_path, ".") == 0 || strcmp(utf8_path, "..") == 0 ||
        len < 4 || strcmp(utf8_path + len - 4, ".sql") != 0) {
      free(utf8_path);
      continue;
    }

    if (!strchr(utf8_path, '/') && !strchr(utf8_path, '\\')) {
      size_t dlen = strlen(migrations_dir);
      int has_sep = (dlen > 0 && (migrations_dir[dlen - 1] == '/' ||
                                  migrations_dir[dlen - 1] == '\\'));
      size_t full_len = dlen + (has_sep ? 0 : 1) + len + 1;
      char *joined = (char *)malloc(full_len);
      if (!joined) {
        free(utf8_path);
        cfs_dir_itr_close(it);
        return C_ORM_ERROR_MEMORY;
      }
      C_ORM_SPRINTF(joined, full_len, "%s%s%s", migrations_dir,
                    has_sep ? "" : "/", utf8_path);
      free(utf8_path);
      utf8_path = joined;
    }

    rc = (c_orm_error_t)get_basename(utf8_path, &basename);
    if (rc != C_ORM_OK || !basename) {
      free(utf8_path);
      cfs_dir_itr_close(it);
      return rc != C_ORM_OK ? rc : C_ORM_ERROR_MEMORY;
    }

    basename[strlen(basename) - 4] = '\0';

    if (list->count >= list->capacity) {
      size_t new_cap = list->capacity == 0 ? 16 : list->capacity * 2;
      struct MigrationFile *new_arr = (struct MigrationFile *)realloc(
          list->files, new_cap * sizeof(struct MigrationFile));
      if (!new_arr) {
        free(basename);
        free(utf8_path);
        cfs_dir_itr_close(it);
        return C_ORM_ERROR_MEMORY;
      }
      list->files = new_arr;
      list->capacity = new_cap;
    }

    list->files[list->count].filepath = utf8_path;
    list->files[list->count].version = basename;
    list->count++;
  }

  cfs_dir_itr_close(it);
  return C_ORM_OK;
}

/**
 * @brief Compare function for qsort.
 *
 * @param[in] a MigrationFile pointer A.
 * @param[in] b MigrationFile pointer B.
 * @return integer less than, equal to, or greater than zero.
 */
static int compare_migrations(const void *a, const void *b) {
  const struct MigrationFile *ma = (const struct MigrationFile *)a;
  const struct MigrationFile *mb = (const struct MigrationFile *)b;
  return strcmp(ma->version, mb->version);
}

/**
 * @brief Get list of applied migrations from DB.
 *
 * @param[in] conn Active PGconn.
 * @param[out] out_versions Array of strings.
 * @param[out] out_count Number of elements.
 * @return C_ORM_OK on success, error code otherwise.
 */
static c_orm_error_t get_applied_migrations(PGconn *conn, char ***out_versions,
                                            size_t *out_count) {
  PGresult *res;
  int rows;
  int i;
  char **versions;

  *out_versions = NULL;
  *out_count = 0;

  res = PQexec(conn,
               "SELECT version FROM schema_migrations ORDER BY version ASC");
  if (PQresultStatus(res) != PGRES_TUPLES_OK) {
    fprintf(stderr, "Failed to fetch applied migrations: %s\n",
            PQerrorMessage(conn));
    PQclear(res);
    return C_ORM_ERROR_SQL;
  }

  rows = PQntuples(res);
  if (rows == 0) {
    PQclear(res);
    return C_ORM_OK;
  }

  versions = (char **)malloc((size_t)rows * sizeof(char *));
  if (!versions) {
    PQclear(res);
    return C_ORM_ERROR_MEMORY;
  }

  for (i = 0; i < rows; i++) {
    char *val = PQgetvalue(res, i, 0);
    if (c_cdd_strdup(val, &versions[i]) != CDD_C_SUCCESS) {
      int j;
      for (j = 0; j < i; j++) {
        free(versions[j]);
      }
      free(versions);
      PQclear(res);
      return C_ORM_ERROR_MEMORY;
    }
  }

  *out_versions = versions;
  *out_count = (size_t)rows;
  PQclear(res);

  return C_ORM_OK;
}

/**
 * @brief Helper to check if a version is in applied list.
 *
 * @param[in] version Version string.
 * @param[in] applied_versions Array of strings.
 * @param[in] applied_count Count of strings.
 * @param[out] out_applied 1 if found, 0 otherwise.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t is_applied(const char *version, char **applied_versions,
                                size_t applied_count, int *out_applied) {
  size_t i;
  if (!out_applied) {
    return C_ORM_ERROR_MEMORY;
  }
  *out_applied = 0;
  for (i = 0; i < applied_count; i++) {
    if (strcmp(version, applied_versions[i]) == 0) {
      *out_applied = 1;
      return C_ORM_OK;
    }
  }
  return C_ORM_OK;
}

/**
 * @brief Applies migration.
 */
C_ORM_EXPORT c_orm_error_t apply_migration(const char *filepath) {
  struct MigrationStatements stmts;
  PGconn *conn;
  PGresult *res;
  char db_url[1024];
  c_orm_error_t rc;

  ENSURE_LIBPQ();

  C_ORM_GETENV(db_url, sizeof(db_url), "DATABASE_URL");
  if (db_url[0] == '\0') {
    fprintf(stderr, "Error: DATABASE_URL environment variable is not set.\n");
    return C_ORM_ERROR_VALIDATION;
  }

  rc = parse_migration_file(filepath, &stmts);
  if (rc != C_ORM_OK) {
    fprintf(stderr, "Error: failed to parse migration file '%s'\n", filepath);
    return rc;
  }

  if (!stmts.up_statement) {
    migration_statements_free(&stmts);
    return C_ORM_OK; /* Nothing to do */
  }

  conn = PQconnectdb(db_url);
  if (PQstatus(conn) != CONNECTION_OK) {
    fprintf(stderr, "Connection to database failed: %s\n",
            PQerrorMessage(conn));
    PQfinish(conn);
    migration_statements_free(&stmts);
    return C_ORM_ERROR_CONNECTION;
  }

  res = PQexec(conn, stmts.up_statement);
  if (PQresultStatus(res) != PGRES_COMMAND_OK &&
      PQresultStatus(res) != PGRES_TUPLES_OK) {
    fprintf(stderr, "Migration UP failed: %s\n", PQerrorMessage(conn));
    PQclear(res);
    PQfinish(conn);
    migration_statements_free(&stmts);
    return C_ORM_ERROR_SQL;
  }

  PQclear(res);
  PQfinish(conn);
  migration_statements_free(&stmts);

  return C_ORM_OK;
}

/**
 * @brief Executes the rollback migration operation.
 */
C_ORM_EXPORT c_orm_error_t rollback_migration(const char *filepath) {
  struct MigrationStatements stmts;
  PGconn *conn;
  PGresult *res;
  char db_url[1024];
  c_orm_error_t rc;

  C_ORM_GETENV(db_url, sizeof(db_url), "DATABASE_URL");
  if (db_url[0] == '\0') {
    fprintf(stderr, "Error: DATABASE_URL environment variable is not set.\n");
    return C_ORM_ERROR_VALIDATION;
  }

  rc = parse_migration_file(filepath, &stmts);
  if (rc != C_ORM_OK) {
    fprintf(stderr, "Error: failed to parse migration file '%s'\n", filepath);
    return rc;
  }

  if (!stmts.down_statement) {
    migration_statements_free(&stmts);
    return C_ORM_OK; /* Nothing to do */
  }

  conn = PQconnectdb(db_url);
  if (PQstatus(conn) != CONNECTION_OK) {
    fprintf(stderr, "Connection to database failed: %s\n",
            PQerrorMessage(conn));
    PQfinish(conn);
    migration_statements_free(&stmts);
    return C_ORM_ERROR_CONNECTION;
  }

  res = PQexec(conn, stmts.down_statement);
  if (PQresultStatus(res) != PGRES_COMMAND_OK &&
      PQresultStatus(res) != PGRES_TUPLES_OK) {
    fprintf(stderr, "Migration DOWN failed: %s\n", PQerrorMessage(conn));
    PQclear(res);
    PQfinish(conn);
    migration_statements_free(&stmts);
    return C_ORM_ERROR_SQL;
  }

  PQclear(res);
  PQfinish(conn);
  migration_statements_free(&stmts);

  return C_ORM_OK;
}

/**
 * @brief Executes the run pending migrations operation.
 */
C_ORM_EXPORT c_orm_error_t run_pending_migrations(const char *migrations_dir) {
  PGconn *conn;
  char db_url[1024];
  c_orm_error_t rc;
  struct MigrationList list;
  char **applied_versions;
  size_t applied_count;
  size_t i;

  C_ORM_GETENV(db_url, sizeof(db_url), "DATABASE_URL");
  if (db_url[0] == '\0') {
    fprintf(stderr, "Error: DATABASE_URL environment variable is not set.\n");
    return C_ORM_ERROR_VALIDATION;
  }

  conn = PQconnectdb(db_url);
  if (PQstatus(conn) != CONNECTION_OK) {
    fprintf(stderr, "Connection to database failed: %s\n",
            PQerrorMessage(conn));
    PQfinish(conn);
    return C_ORM_ERROR_CONNECTION;
  }

  rc = ensure_schema_migrations_table(conn);
  if (rc != C_ORM_OK) {
    PQfinish(conn);
    return rc;
  }

  rc = get_applied_migrations(conn, &applied_versions, &applied_count);
  if (rc != C_ORM_OK) {
    PQfinish(conn);
    return rc;
  }

  list.files = NULL;
  list.count = 0;
  list.capacity = 0;

  rc = collect_migrations_from_dir(migrations_dir, &list);
  if (rc != C_ORM_OK) {
    fprintf(stderr, "Error reading migrations directory.\n");
    for (i = 0; i < applied_count; i++) {
      free(applied_versions[i]);
    }
    if (applied_versions) {
      free(applied_versions);
    }
    PQfinish(conn);
    return rc;
  }

  if (list.count > 0) {
    qsort(list.files, list.count, sizeof(struct MigrationFile),
          compare_migrations);
  }

  for (i = 0; i < list.count; i++) {
    int applied = 0;
    rc = is_applied(list.files[i].version, applied_versions, applied_count,
                    &applied);
    if (rc != C_ORM_OK) {
      break;
    }
    if (!applied) {
      struct MigrationStatements stmts;
      PGresult *res;
      const char *paramValues[1];

      printf("Applying migration: %s\n", list.files[i].version);

      rc = parse_migration_file(list.files[i].filepath, &stmts);
      if (rc != C_ORM_OK) {
        fprintf(stderr, "Failed to parse migration file '%s'\n",
                list.files[i].filepath);
        break;
      }

      if (stmts.up_statement) {
        res = PQexec(conn, stmts.up_statement);
        if (PQresultStatus(res) != PGRES_COMMAND_OK &&
            PQresultStatus(res) != PGRES_TUPLES_OK) {
          fprintf(stderr, "Migration UP failed: %s\n", PQerrorMessage(conn));
          PQclear(res);
          migration_statements_free(&stmts);
          rc = C_ORM_ERROR_SQL;
          break;
        }
        PQclear(res);
      }
      migration_statements_free(&stmts);

      paramValues[0] = list.files[i].version;
      res = PQexecParams(conn,
                         "INSERT INTO schema_migrations (version) VALUES ($1);",
                         1, NULL, paramValues, NULL, NULL, 0);
      if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        fprintf(stderr, "Failed to record migration: %s\n",
                PQerrorMessage(conn));
        PQclear(res);
        rc = C_ORM_ERROR_SQL;
        break;
      }
      PQclear(res);
    }
  }

  /* Cleanup */
  for (i = 0; i < applied_count; i++) {
    free(applied_versions[i]);
  }
  if (applied_versions) {
    free(applied_versions);
  }

  for (i = 0; i < list.count; i++) {
    free(list.files[i].filepath);
    free(list.files[i].version);
  }
  if (list.files) {
    free(list.files);
  }

  PQfinish(conn);
  return rc;
}

/**
 * @brief Executes the rollback last migration operation.
 */
C_ORM_EXPORT c_orm_error_t rollback_last_migration(const char *migrations_dir) {
  PGconn *conn;
  char db_url[1024];
  c_orm_error_t rc;
  PGresult *res;
  char *last_version;
  struct MigrationList list;
  size_t i;
  char *target_filepath;

  C_ORM_GETENV(db_url, sizeof(db_url), "DATABASE_URL");
  if (db_url[0] == '\0') {
    fprintf(stderr, "Error: DATABASE_URL environment variable is not set.\n");
    return C_ORM_ERROR_VALIDATION;
  }

  conn = PQconnectdb(db_url);
  if (PQstatus(conn) != CONNECTION_OK) {
    fprintf(stderr, "Connection to database failed: %s\n",
            PQerrorMessage(conn));
    PQfinish(conn);
    return C_ORM_ERROR_CONNECTION;
  }

  rc = ensure_schema_migrations_table(conn);
  if (rc != C_ORM_OK) {
    PQfinish(conn);
    return rc;
  }

  res = PQexec(
      conn,
      "SELECT version FROM schema_migrations ORDER BY version DESC LIMIT 1");
  if (PQresultStatus(res) != PGRES_TUPLES_OK) {
    fprintf(stderr, "Failed to fetch last migration: %s\n",
            PQerrorMessage(conn));
    PQclear(res);
    PQfinish(conn);
    return C_ORM_ERROR_SQL;
  }

  if (PQntuples(res) == 0) {
    printf("No migrations to rollback.\n");
    PQclear(res);
    PQfinish(conn);
    return C_ORM_OK;
  }

  last_version = NULL;
  rc = (c_orm_error_t)c_cdd_strdup(PQgetvalue(res, 0, 0), &last_version);
  PQclear(res);

  if (rc != C_ORM_OK) {
    PQfinish(conn);
    return C_ORM_ERROR_MEMORY;
  }

  list.files = NULL;
  list.count = 0;
  list.capacity = 0;

  rc = collect_migrations_from_dir(migrations_dir, &list);
  if (rc != C_ORM_OK) {
    fprintf(stderr, "Error reading migrations directory.\n");
    free(last_version);
    PQfinish(conn);
    return rc;
  }

  target_filepath = NULL;
  for (i = 0; i < list.count; i++) {
    if (strcmp(list.files[i].version, last_version) == 0) {
      rc =
          (c_orm_error_t)c_cdd_strdup(list.files[i].filepath, &target_filepath);
      break;
    }
  }

  for (i = 0; i < list.count; i++) {
    free(list.files[i].filepath);
    free(list.files[i].version);
  }
  if (list.files) {
    free(list.files);
  }

  if (!target_filepath || rc != C_ORM_OK) {
    fprintf(stderr,
            "Error: Migration file for version '%s' not found in directory.\n",
            last_version);
    free(last_version);
    if (target_filepath) {
      free(target_filepath);
    }
    PQfinish(conn);
    return C_ORM_ERROR_NOT_FOUND;
  }

  printf("Rolling back migration: %s\n", last_version);

  {
    struct MigrationStatements stmts;
    rc = parse_migration_file(target_filepath, &stmts);
    if (rc != C_ORM_OK) {
      fprintf(stderr, "Failed to parse migration file '%s'\n", target_filepath);
      free(target_filepath);
      free(last_version);
      PQfinish(conn);
      return rc;
    }

    if (stmts.down_statement) {
      res = PQexec(conn, stmts.down_statement);
      if (PQresultStatus(res) != PGRES_COMMAND_OK &&
          PQresultStatus(res) != PGRES_TUPLES_OK) {
        fprintf(stderr, "Migration DOWN failed: %s\n", PQerrorMessage(conn));
        PQclear(res);
        migration_statements_free(&stmts);
        free(target_filepath);
        free(last_version);
        PQfinish(conn);
        return C_ORM_ERROR_SQL;
      }
      PQclear(res);
    }
    migration_statements_free(&stmts);
  }

  {
    const char *paramValues[1];
    paramValues[0] = last_version;
    res =
        PQexecParams(conn, "DELETE FROM schema_migrations WHERE version = $1;",
                     1, NULL, paramValues, NULL, NULL, 0);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
      fprintf(stderr, "Failed to delete migration record: %s\n",
              PQerrorMessage(conn));
      PQclear(res);
      free(target_filepath);
      free(last_version);
      PQfinish(conn);
      return C_ORM_ERROR_SQL;
    }
    PQclear(res);
  }

  free(target_filepath);
  free(last_version);
  PQfinish(conn);

  return C_ORM_OK;
}

/**
 * @brief Executes the create migration file operation.
 */
C_ORM_EXPORT c_orm_error_t create_migration_file(const char *migrations_dir,
                                                 const char *name) {
  char *filepath;
  char *safe_name;
  time_t t;
  struct tm *tm_info;
  char timestamp[20];
  const char *template_str;
  c_orm_error_t rc;
  size_t filepath_len;
  int i;

  if (!migrations_dir || !name) {
    return C_ORM_ERROR_VALIDATION;
  }

  t = time(NULL);
  tm_info = localtime(&t);
  if (!tm_info) {
    return C_ORM_ERROR_UNKNOWN;
  }
  strftime(timestamp, sizeof(timestamp), "%Y%m%d%H%M%S", tm_info);

  if (c_cdd_strdup(name, &safe_name) != CDD_C_SUCCESS) {
    return C_ORM_ERROR_MEMORY;
  }

  for (i = 0; safe_name[i]; i++) {
    if (safe_name[i] == ' ' || safe_name[i] == '-' || safe_name[i] == '.') {
      safe_name[i] = '_';
    }
  }

  filepath_len =
      strlen(migrations_dir) + strlen(timestamp) + strlen(safe_name) + 7;
  filepath = (char *)malloc(filepath_len);
  if (!filepath) {
    free(safe_name);
    return C_ORM_ERROR_MEMORY;
  }

  C_ORM_SPRINTF(filepath, filepath_len, "%s%c%s_%s.sql", migrations_dir,
                PATH_SEP_C, timestamp, safe_name);

  template_str = "-- UP\n"
                 "-- Add up migration statements here\n\n"
                 "-- DOWN\n"
                 "-- Add down migration statements here\n";

  rc = (c_orm_error_t)fs_write_to_file(filepath, template_str);

  if (rc == C_ORM_OK) {
    printf("Created migration file: %s\n", filepath);
  } else {
    fprintf(stderr, "Failed to create migration file: %s\n", filepath);
    rc = C_ORM_ERROR_IO;
  }

  free(safe_name);
  free(filepath);
  return rc;
}

/**
 * @brief Executes the reset database operation.
 */
C_ORM_EXPORT c_orm_error_t reset_database(const char *migrations_dir) {
  PGconn *conn;
  char db_url[1024];
  PGresult *res;
  c_orm_error_t rc;

  ENSURE_LIBPQ();

  C_ORM_GETENV(db_url, sizeof(db_url), "DATABASE_URL");
  if (db_url[0] == '\0') {
    fprintf(stderr, "Error: DATABASE_URL environment variable is not set.\n");
    return C_ORM_ERROR_VALIDATION;
  }

  conn = PQconnectdb(db_url);
  if (PQstatus(conn) != CONNECTION_OK) {
    fprintf(stderr, "Connection to database failed: %s\n",
            PQerrorMessage(conn));
    PQfinish(conn);
    return C_ORM_ERROR_CONNECTION;
  }

  res = PQexec(conn, "DROP SCHEMA public CASCADE; CREATE SCHEMA public;");
  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    fprintf(stderr, "Failed to reset database schema: %s\n",
            PQerrorMessage(conn));
    PQclear(res);
    PQfinish(conn);
    return C_ORM_ERROR_SQL;
  }
  PQclear(res);

  PQfinish(conn);

  printf("Database schema reset successfully.\n");

  rc = run_pending_migrations(migrations_dir);
  return rc;
}

/**
 * @brief Validates that a SQL identifier contains only safe ASCII identifier
 * characters (`[A-Za-z_][A-Za-z0-9_]*`).
 *
 * @param[in] ident Identifier string to validate.
 * @param[out] out_valid Output flag set to 1 if valid, 0 otherwise.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t is_safe_pg_identifier(const char *ident, int *out_valid) {
  size_t i;
  if (!out_valid) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  *out_valid = 0;
  if (!ident || ident[0] == '\0') {
    return C_ORM_OK;
  }
  if (!((ident[0] >= 'a' && ident[0] <= 'z') ||
        (ident[0] >= 'A' && ident[0] <= 'Z') || ident[0] == '_')) {
    return C_ORM_OK;
  }
  for (i = 1; ident[i] != '\0'; i++) {
    char c = ident[i];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_')) {
      return C_ORM_OK;
    }
  }
  *out_valid = 1;
  return C_ORM_OK;
}

/**
 * @brief Executes the dump schema operation via direct libpq catalog
 * introspection (without invoking any external shell command).
 */
C_ORM_EXPORT c_orm_error_t dump_schema(const char *out_filepath) {
  char db_url[1024];
  PGconn *conn;
  PGresult *tables_res;
  FILE *fp = NULL;
  int t_rows;
  int t_idx;

  if (!out_filepath || out_filepath[0] == '\0') {
    return C_ORM_ERROR_VALIDATION;
  }

  ENSURE_LIBPQ();

  C_ORM_GETENV(db_url, sizeof(db_url), "DATABASE_URL");
  if (db_url[0] == '\0') {
    fprintf(stderr, "Error: DATABASE_URL environment variable is not set.\n");
    return C_ORM_ERROR_VALIDATION;
  }

  conn = PQconnectdb(db_url);
  if (PQstatus(conn) != CONNECTION_OK) {
    fprintf(stderr, "Connection to database failed: %s\n",
            PQerrorMessage(conn));
    PQfinish(conn);
    return C_ORM_ERROR_CONNECTION;
  }

  tables_res = PQexec(
      conn, "SELECT table_name FROM information_schema.tables "
            "WHERE table_schema = 'public' AND table_type = 'BASE TABLE' "
            "ORDER BY table_name ASC;");
  if (PQresultStatus(tables_res) != PGRES_TUPLES_OK) {
    fprintf(stderr, "Failed to query public tables: %s\n",
            PQerrorMessage(conn));
    PQclear(tables_res);
    PQfinish(conn);
    return C_ORM_ERROR_SQL;
  }

  if (C_ORM_FOPEN(&fp, out_filepath, "wb") != C_ORM_OK || !fp) {
    PQclear(tables_res);
    PQfinish(conn);
    return C_ORM_ERROR_IO;
  }

  fprintf(fp, "-- PostgreSQL schema dump generated by c-orm\n\n");

  t_rows = PQntuples(tables_res);
  for (t_idx = 0; t_idx < t_rows; t_idx++) {
    const char *tname = PQgetvalue(tables_res, t_idx, 0);
    const char *params[1];
    PGresult *cols_res;
    int c_rows;
    int c_idx;

    params[0] = tname;
    cols_res = PQexecParams(
        conn,
        "SELECT column_name, data_type, is_nullable, column_default "
        "FROM information_schema.columns "
        "WHERE table_schema = 'public' AND table_name = $1 "
        "ORDER BY ordinal_position ASC;",
        1, NULL, params, NULL, NULL, 0);
    if (PQresultStatus(cols_res) != PGRES_TUPLES_OK) {
      PQclear(cols_res);
      fclose(fp);
      PQclear(tables_res);
      PQfinish(conn);
      return C_ORM_ERROR_SQL;
    }

    fprintf(fp, "CREATE TABLE \"%s\" (\n", tname);
    c_rows = PQntuples(cols_res);
    for (c_idx = 0; c_idx < c_rows; c_idx++) {
      const char *cname = PQgetvalue(cols_res, c_idx, 0);
      const char *dtype = PQgetvalue(cols_res, c_idx, 1);
      const char *nullable = PQgetvalue(cols_res, c_idx, 2);
      const char *defval = PQgetvalue(cols_res, c_idx, 3);
      fprintf(fp, "  \"%s\" %s", cname, dtype);
      if (nullable && strcmp(nullable, "NO") == 0) {
        fprintf(fp, " NOT NULL");
      }
      if (defval && defval[0] != '\0') {
        fprintf(fp, " DEFAULT %s", defval);
      }
      fprintf(fp, "%s\n", (c_idx + 1 < c_rows) ? "," : "");
    }
    fprintf(fp, ");\n\n");
    PQclear(cols_res);
  }

  fclose(fp);
  PQclear(tables_res);
  PQfinish(conn);
  printf("Schema dumped successfully to %s\n", out_filepath);
  return C_ORM_OK;
}
/**
 * @brief Executes the setup test database operation.
 */
C_ORM_EXPORT c_orm_error_t setup_test_database(const char *db_name,
                                               const char *migrations_dir) {
  PGconn *conn;
  char db_url[1024];
  char *create_db_query;
  size_t query_len;
  PGresult *res;
  int valid_ident = 0;
  c_orm_error_t rc;

  if (!db_name || !migrations_dir) {
    return C_ORM_ERROR_VALIDATION;
  }

  rc = is_safe_pg_identifier(db_name, &valid_ident);
  if (rc != C_ORM_OK || !valid_ident) {
    return C_ORM_ERROR_VALIDATION;
  }

  ENSURE_LIBPQ();

  C_ORM_GETENV(db_url, sizeof(db_url), "DATABASE_URL");
  if (db_url[0] == '\0') {
    fprintf(stderr, "Error: DATABASE_URL environment variable is not set.\n");
    return C_ORM_ERROR_VALIDATION;
  }

  conn = PQconnectdb(db_url);
  if (PQstatus(conn) != CONNECTION_OK) {
    fprintf(stderr, "Connection to administrative database failed: %s\n",
            PQerrorMessage(conn));
    PQfinish(conn);
    return C_ORM_ERROR_CONNECTION;
  }

  query_len = strlen("CREATE DATABASE \"\"") + strlen(db_name) + 1;
  create_db_query = (char *)malloc(query_len);
  if (!create_db_query) {
    PQfinish(conn);
    return C_ORM_ERROR_MEMORY;
  }

  C_ORM_SPRINTF(create_db_query, query_len, "CREATE DATABASE \"%s\"", db_name);

  res = PQexec(conn, create_db_query);
  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    /* It's OK if it already exists */
    if (strstr(PQerrorMessage(conn), "already exists") == NULL) {
      fprintf(stderr, "Failed to create test database: %s\n",
              PQerrorMessage(conn));
      PQclear(res);
      free(create_db_query);
      PQfinish(conn);
      return C_ORM_ERROR_SQL;
    }
  }
  PQclear(res);
  free(create_db_query);
  PQfinish(conn);

  printf("Test database '%s' created (if it didn't exist).\n", db_name);
  return C_ORM_OK;
}

/**
 * @brief Executes the seed database operation.
 */
C_ORM_EXPORT c_orm_error_t seed_database(const char *seed_filepath) {
  char *file_data;
  size_t file_size;
  PGconn *conn;
  char db_url[1024];
  PGresult *res;
  c_orm_error_t rc;

  if (!seed_filepath) {
    return C_ORM_ERROR_VALIDATION;
  }

  C_ORM_GETENV(db_url, sizeof(db_url), "DATABASE_URL");
  if (db_url[0] == '\0') {
    fprintf(stderr, "Error: DATABASE_URL environment variable is not set.\n");
    return C_ORM_ERROR_VALIDATION;
  }

  rc = (c_orm_error_t)read_to_file(seed_filepath, "rb", &file_data, &file_size);
  if (rc != C_ORM_OK) {
    fprintf(stderr, "Error reading seed file '%s'\n", seed_filepath);
    return C_ORM_ERROR_NOT_FOUND;
  }

  if (file_size == 0 || !file_data) {
    if (file_data) {
      free(file_data);
    }
    return C_ORM_OK;
  }

  conn = PQconnectdb(db_url);
  if (PQstatus(conn) != CONNECTION_OK) {
    fprintf(stderr, "Connection to database failed: %s\n",
            PQerrorMessage(conn));
    free(file_data);
    PQfinish(conn);
    return C_ORM_ERROR_CONNECTION;
  }

  res = PQexec(conn, file_data);
  if (PQresultStatus(res) != PGRES_COMMAND_OK &&
      PQresultStatus(res) != PGRES_TUPLES_OK) {
    fprintf(stderr, "Seeding failed: %s\n", PQerrorMessage(conn));
    PQclear(res);
    free(file_data);
    PQfinish(conn);
    return C_ORM_ERROR_SQL;
  }

  PQclear(res);
  free(file_data);
  PQfinish(conn);

  printf("Database seeded successfully from '%s'.\n", seed_filepath);
  return C_ORM_OK;
}
#else  /* !(!defined(__EMSCRIPTEN__) && (defined(USE_LIBPQ_LINKED) ||          \
          defined(USE_LIBPQ_DYNAMIC))) */
/**
 * @brief Apply a migration file (fallback when libpq is disabled).
 * @param filepath Path to the migration file.
 * @return ENOSYS error code.
 */
C_ORM_EXPORT c_orm_error_t apply_migration(const char *filepath) {
  (void)filepath;
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}

/**
 * @brief Rollback a migration file (fallback when libpq is disabled).
 * @param filepath Path to the migration file.
 * @return ENOSYS error code.
 */
C_ORM_EXPORT c_orm_error_t rollback_migration(const char *filepath) {
  (void)filepath;
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}

/**
 * @brief Run all pending UP migrations in a directory (fallback when libpq is
 * disabled).
 * @param migrations_dir Path to the directory containing .sql files.
 * @return ENOSYS error code.
 */
C_ORM_EXPORT c_orm_error_t run_pending_migrations(const char *migrations_dir) {
  (void)migrations_dir;
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}

/**
 * @brief Rollback the last applied DOWN migration (fallback when libpq is
 * disabled).
 * @param migrations_dir Path to the directory containing .sql files.
 * @return ENOSYS error code.
 */
C_ORM_EXPORT c_orm_error_t rollback_last_migration(const char *migrations_dir) {
  (void)migrations_dir;
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}

/**
 * @brief Create a new boilerplate migration file (fallback when libpq is
 * disabled).
 * @param migrations_dir Directory to place the file.
 * @param name The name suffix for the migration file.
 * @return ENOSYS error code.
 */
C_ORM_EXPORT c_orm_error_t create_migration_file(const char *migrations_dir,
                                                 const char *name) {
  (void)migrations_dir;
  (void)name;
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}

/**
 * @brief Drop and recreate database schema (fallback when libpq is disabled).
 * @param migrations_dir Directory containing .sql files.
 * @return ENOSYS error code.
 */
C_ORM_EXPORT c_orm_error_t reset_database(const char *migrations_dir) {
  (void)migrations_dir;
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}

/**
 * @brief Dump current database schema (fallback when libpq is disabled).
 * @param out_filepath Path to the output file.
 * @return ENOSYS error code.
 */
C_ORM_EXPORT c_orm_error_t dump_schema(const char *out_filepath) {
  (void)out_filepath;
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}

/**
 * @brief Setup test database dynamically (fallback when libpq is disabled).
 * @param db_name The name of the test database to create.
 * @param migrations_dir Directory containing .sql migrations.
 * @return ENOSYS error code.
 */
C_ORM_EXPORT c_orm_error_t setup_test_database(const char *db_name,
                                               const char *migrations_dir) {
  (void)db_name;
  (void)migrations_dir;
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}

/**
 * @brief Seed database with SQL test data (fallback when libpq is disabled).
 * @param seed_filepath Path to the .sql seed file.
 * @return ENOSYS error code.
 */
C_ORM_EXPORT c_orm_error_t seed_database(const char *seed_filepath) {
  (void)seed_filepath;
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}
#endif /* !(!defined(__EMSCRIPTEN__) && (defined(USE_LIBPQ_LINKED) ||          \
          defined(USE_LIBPQ_DYNAMIC))) */
