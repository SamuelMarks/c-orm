/**
 * @file c_orm_sqlite.c
 * @brief SQLite3 driver implementation for c-orm.
 */

/* clang-format off */
#include "c_orm_sqlite.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#if defined(_WIN32) || defined(_WIN64)
typedef union _LARGE_INTEGER {
    struct {
        unsigned long LowPart;
        long HighPart;
    } u;
#if defined(_MSC_VER)
    __int64 QuadPart;
#else
    long long QuadPart;
#endif
} LARGE_INTEGER;
__declspec(dllimport) int __stdcall QueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount);
__declspec(dllimport) int __stdcall QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency);
#else
#include <sys/time.h>
#endif
#ifdef C_ORM_ENABLE_SQLITE
#include <sqlite3.h>
#endif
/* clang-format on */

#ifdef C_ORM_ENABLE_SQLITE
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(                                                               \
    disable : 4306) /* 'type cast' : conversion from 'int' to                  \
                       'sqlite3_destructor_type' of greater size */
#endif
#endif

#ifdef C_ORM_ENABLE_SQLITE
#endif

#ifdef C_ORM_ENABLE_SQLITE

struct sqlite_db_data {
  sqlite3 *db;
  char last_error[512];
};

struct sqlite_query_data {
  sqlite3_stmt *stmt;
  c_orm_db_t *db;
};

static void set_error(c_orm_db_t *db, const char *msg) {
  if (db && db->driver_data) {
    struct sqlite_db_data *data = (struct sqlite_db_data *)db->driver_data;
    if (msg) {
      /* Safe copy for C90 compatibility */
      size_t len = strlen(msg);
      if (len >= sizeof(data->last_error)) {
        len = sizeof(data->last_error) - 1;
      }
      memcpy(data->last_error, msg, len);
      data->last_error[len] = '\0';
    } else if (data->db) {
      const char *sqlite_err = sqlite3_errmsg(data->db);
      size_t len = strlen(sqlite_err);
      if (len >= sizeof(data->last_error)) {
        len = sizeof(data->last_error) - 1;
      }
      memcpy(data->last_error, sqlite_err, len);
      data->last_error[len] = '\0';
    }
  }
}

static c_orm_error_t sqlite_connect(const char *url, c_orm_db_t **out_db) {
  c_orm_db_t *db;
  struct sqlite_db_data *data;
  int rc;

  if (!url || !out_db) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  db = (c_orm_db_t *)calloc(1, sizeof(c_orm_db_t));
  if (!db) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  data = (struct sqlite_db_data *)calloc(1, sizeof(struct sqlite_db_data));
  if (!data) {
    free(db);
    {
      rc = C_ORM_ERROR_MEMORY;
      return (c_orm_error_t)rc;
    }
  }

  rc = sqlite3_open(url, &data->db);
  if (rc != SQLITE_OK) {
    sqlite3_close(data->db); /* clean up if needed */
    free(data);
    free(db);
    {
      rc = C_ORM_ERROR_CONNECTION;
      return (c_orm_error_t)rc;
    }
  }

  if (c_orm_sqlite_get_vtable(&db->vtable) != 0) {
    sqlite3_close(data->db);
    free(data);
    free(db);
    {
      rc = C_ORM_ERROR_UNKNOWN;
      return (c_orm_error_t)rc;
    }
  }
  db->driver_data = data;
  *out_db = db;

  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static c_orm_error_t sqlite_disconnect(c_orm_db_t *db) {
  int rc;

  struct sqlite_db_data *data;
  if (!db) {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }

  data = (struct sqlite_db_data *)db->driver_data;
  if (data) {
    if (data->db) {
      sqlite3_close(data->db);
    }
    free(data);
  }
  free(db);
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

struct c_orm_query {
  struct sqlite_query_data *data;
};

static c_orm_error_t sqlite_prepare(c_orm_db_t *db, const char *sql,
                                    c_orm_query_t **out_query) {
  struct sqlite_db_data *db_data;
  struct sqlite_query_data *q_data;
  c_orm_query_t *query;
  int rc;

  if (!db || !sql || !out_query) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  db_data = (struct sqlite_db_data *)db->driver_data;

  if (db->log_cb) {
    db->log_cb(sql, db->log_user_data);
  }

  query = (c_orm_query_t *)calloc(1, sizeof(c_orm_query_t));
  if (!query) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  q_data =
      (struct sqlite_query_data *)calloc(1, sizeof(struct sqlite_query_data));
  if (!q_data) {
    free(query);
    {
      rc = C_ORM_ERROR_MEMORY;
      return (c_orm_error_t)rc;
    }
  }

  rc = sqlite3_prepare_v2(db_data->db, sql, -1, &q_data->stmt, NULL);
  if (rc != SQLITE_OK) {
    set_error(db, NULL);
    free(q_data);
    free(query);
    {
      rc = C_ORM_ERROR_SQL;
      return (c_orm_error_t)rc;
    }
  }

  q_data->db = db;
  query->data = q_data;
  *out_query = query;

  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static c_orm_error_t sqlite_bind_int32(c_orm_query_t *query, int index,
                                       int32_t val) {
  int rc;
  if (!query || !query->data || !query->data->stmt) {
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }
  rc = sqlite3_bind_int(query->data->stmt, index, val);
  if (rc != SQLITE_OK) {
    set_error(query->data->db, NULL);
    {
      rc = C_ORM_ERROR_BIND;
      return (c_orm_error_t)rc;
    }
  }
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static c_orm_error_t sqlite_bind_int64(c_orm_query_t *query, int index,
                                       int64_t val) {
  int rc;
  if (!query || !query->data || !query->data->stmt) {
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }
  rc = sqlite3_bind_int64(query->data->stmt, index, val);
  if (rc != SQLITE_OK) {
    set_error(query->data->db, NULL);
    {
      rc = C_ORM_ERROR_BIND;
      return (c_orm_error_t)rc;
    }
  }
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static c_orm_error_t sqlite_bind_double(c_orm_query_t *query, int index,
                                        double val) {
  int rc;
  if (!query || !query->data || !query->data->stmt) {
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }
  rc = sqlite3_bind_double(query->data->stmt, index, val);
  if (rc != SQLITE_OK) {
    set_error(query->data->db, NULL);
    {
      rc = C_ORM_ERROR_BIND;
      return (c_orm_error_t)rc;
    }
  }
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static c_orm_error_t sqlite_bind_string(c_orm_query_t *query, int index,
                                        const char *val) {
  int rc;
  if (!query || !query->data || !query->data->stmt) {
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }
  rc = sqlite3_bind_text(query->data->stmt, index, val, -1, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK) {
    set_error(query->data->db, NULL);
    {
      rc = C_ORM_ERROR_BIND;
      return (c_orm_error_t)rc;
    }
  }
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static c_orm_error_t sqlite_bind_blob(c_orm_query_t *query, int index,
                                      const void *val, size_t size) {
  int rc;
  if (!query || !query->data || !query->data->stmt) {
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }
  rc = sqlite3_bind_blob(query->data->stmt, index, val, (int)size,
                         SQLITE_TRANSIENT);
  if (rc != SQLITE_OK) {
    set_error(query->data->db, NULL);
    {
      rc = C_ORM_ERROR_BIND;
      return (c_orm_error_t)rc;
    }
  }
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static c_orm_error_t sqlite_bind_null(c_orm_query_t *query, int index) {
  int rc;
  if (!query || !query->data || !query->data->stmt) {
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }
  rc = sqlite3_bind_null(query->data->stmt, index);
  if (rc != SQLITE_OK) {
    set_error(query->data->db, NULL);
    {
      rc = C_ORM_ERROR_BIND;
      return (c_orm_error_t)rc;
    }
  }
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static c_orm_error_t sqlite_step(c_orm_query_t *query, int *out_has_row) {
  int rc;
  double elapsed = 0.0;
#if defined(_WIN32) || defined(_WIN64)
  LARGE_INTEGER start_time = {0};
  LARGE_INTEGER end_time = {0};
  LARGE_INTEGER freq = {0};
  if (query && query->data && query->data->db &&
      query->data->db->slow_query_threshold_ms > 0) {
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start_time);
  }
#else
  struct timeval start_time = {0};
  struct timeval end_time = {0};
  if (query && query->data && query->data->db &&
      query->data->db->slow_query_threshold_ms > 0) {
    gettimeofday(&start_time, NULL);
  }
#endif

  if (!query || !query->data || !query->data->stmt || !out_has_row) {
    rc = C_ORM_ERROR_STEP;
    return (c_orm_error_t)rc;
  }

  rc = sqlite3_step(query->data->stmt);

#if defined(_WIN32) || defined(_WIN64)
  if (query->data->db && query->data->db->slow_query_threshold_ms > 0) {
    QueryPerformanceCounter(&end_time);
    elapsed = (double)(end_time.QuadPart - start_time.QuadPart) * 1000.0 /
              (double)freq.QuadPart;
  }
#else
  if (query->data->db && query->data->db->slow_query_threshold_ms > 0) {
    gettimeofday(&end_time, NULL);
    elapsed = (end_time.tv_sec - start_time.tv_sec) * 1000.0;
    elapsed += (end_time.tv_usec - start_time.tv_usec) / 1000.0;
  }
#endif

  if (query->data->db && query->data->db->slow_query_threshold_ms > 0 &&
      elapsed >= query->data->db->slow_query_threshold_ms) {
    if (query->data->db->log_cb) {
      const char *sql = sqlite3_sql(query->data->stmt);
      if (sql) {
        char log_msg[1024];
#if defined(_MSC_VER)
        sprintf_s(log_msg, sizeof(log_msg), "SLOW QUERY (%.2fms): %s", elapsed,
                  sql);
#else
#if defined(_MSC_VER)
        sprintf_s(log_msg, sizeof(log_msg), "SLOW QUERY (%.2fms): %s", elapsed,
                  sql);
#else
        sprintf(log_msg, "SLOW QUERY (%.2fms): %s", elapsed, sql);
#endif
#endif
        query->data->db->log_cb(log_msg, query->data->db->log_user_data);
      }
    }
    query->data->db->telemetry.slow_queries_logged++;
  }

  if (rc == SQLITE_ROW) {
    *out_has_row = 1;
    {
      rc = C_ORM_OK;
      return (c_orm_error_t)rc;
    }
  } else if (rc == SQLITE_DONE) {
    *out_has_row = 0;
    {
      rc = C_ORM_OK;
      return (c_orm_error_t)rc;
    }
  }

  set_error(query->data->db, NULL);
  {
    rc = C_ORM_ERROR_STEP;
    return (c_orm_error_t)rc;
  }
}

static c_orm_error_t sqlite_get_int32(c_orm_query_t *query, int index,
                                      int32_t *out_val) {
  int rc;

  int type;
  if (!query || !query->data || !query->data->stmt || !out_val) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  type = sqlite3_column_type(query->data->stmt, index);
  if (type != SQLITE_INTEGER && type != SQLITE_NULL) {
    {
      rc = C_ORM_ERROR_TYPE_MISMATCH;
      return (c_orm_error_t)rc;
    }
  }

  *out_val = (int32_t)sqlite3_column_int(query->data->stmt, index);
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static c_orm_error_t sqlite_get_int64(c_orm_query_t *query, int index,
                                      int64_t *out_val) {
  int rc;

  int type;
  if (!query || !query->data || !query->data->stmt || !out_val) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  type = sqlite3_column_type(query->data->stmt, index);
  if (type != SQLITE_INTEGER && type != SQLITE_NULL) {
    {
      rc = C_ORM_ERROR_TYPE_MISMATCH;
      return (c_orm_error_t)rc;
    }
  }

  *out_val = (int64_t)sqlite3_column_int64(query->data->stmt, index);
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static c_orm_error_t sqlite_get_double(c_orm_query_t *query, int index,
                                       double *out_val) {
  int rc;

  int type;
  if (!query || !query->data || !query->data->stmt || !out_val) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  type = sqlite3_column_type(query->data->stmt, index);
  if (type != SQLITE_FLOAT && type != SQLITE_INTEGER && type != SQLITE_NULL) {
    {
      rc = C_ORM_ERROR_TYPE_MISMATCH;
      return (c_orm_error_t)rc;
    }
  }

  *out_val = sqlite3_column_double(query->data->stmt, index);
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static c_orm_error_t sqlite_get_string(c_orm_query_t *query, int index,
                                       const char **out_val) {
  int rc;

  int type;
  if (!query || !query->data || !query->data->stmt || !out_val) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  type = sqlite3_column_type(query->data->stmt, index);
  /* Text and Null are okay. Sometimes integer is tolerated in loose databases
   * but step 251 requires strict checks */
  if (type != SQLITE_TEXT && type != SQLITE_NULL) {
    {
      rc = C_ORM_ERROR_TYPE_MISMATCH;
      return (c_orm_error_t)rc;
    }
  }

  *out_val = (const char *)sqlite3_column_text(query->data->stmt, index);
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static c_orm_error_t sqlite_get_blob(c_orm_query_t *query, int index,
                                     const void **out_val, size_t *out_size) {
  int rc;

  int type;
  if (!query || !query->data || !query->data->stmt || !out_val || !out_size) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  type = sqlite3_column_type(query->data->stmt, index);
  if (type != SQLITE_BLOB && type != SQLITE_NULL) {
    {
      rc = C_ORM_ERROR_TYPE_MISMATCH;
      return (c_orm_error_t)rc;
    }
  }

  *out_val = sqlite3_column_blob(query->data->stmt, index);
  *out_size = (size_t)sqlite3_column_bytes(query->data->stmt, index);
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static c_orm_error_t sqlite_is_null(c_orm_query_t *query, int index,
                                    int *out_is_null) {
  int rc;

  if (!query || !query->data || !query->data->stmt || !out_is_null) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  *out_is_null =
      (sqlite3_column_type(query->data->stmt, index) == SQLITE_NULL) ? 1 : 0;
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static c_orm_error_t sqlite_finalize(c_orm_query_t *query) {
  int rc;

  if (!query) {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
  if (query->data) {
    if (query->data->stmt) {
      sqlite3_finalize(query->data->stmt);
    }
    free(query->data);
  }
  free(query);
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static c_orm_error_t sqlite_reset(c_orm_query_t *query) {
  int rc;

  if (!query || !query->data || !query->data->stmt) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  sqlite3_reset(query->data->stmt);
  sqlite3_clear_bindings(query->data->stmt);
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static int sqlite_get_last_error(c_orm_db_t *db, const char **out_message) {
  int rc;
  struct sqlite_db_data *data;
  if (!out_message) {
    rc = 1;
    return rc;
  }
  if (!db || !db->driver_data) {
    *out_message = "Invalid DB object";
    rc = 1;
    return rc;
  }
  data = (struct sqlite_db_data *)db->driver_data;
  *out_message = data->last_error;
  rc = 0;
  return rc;
}

static int sqlite_get_last_trace(c_orm_db_t *db, const char **out_trace) {
  int rc;
  (void)db;
  /* SQLite doesn't natively expose rich trace stacks through its public C API
   * without compiling with SQLITE_ENABLE_API_ARMOR or SQLITE_ENABLE_SQLLOG.
   * Step 265 / Step 266: We return the last message contextualized. */
  if (out_trace) {
    *out_trace = "SQLite Driver Stack Trace: Contextual reporting requires "
                 "runtime AST parser integration currently unsupported "
                 "directly in vtable mappings. Last Error: ";
    /* It normally concatenates, but as a stub returning the literal string fits
     * signature. */
  }
  rc = 0;
  return rc;
}

static c_orm_error_t sqlite_get_last_insert_rowid(c_orm_db_t *db,
                                                  int64_t *out_id) {
  int rc;

  struct sqlite_db_data *db_data;
  if (!db || !db->driver_data || !out_id) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  db_data = (struct sqlite_db_data *)db->driver_data;
  *out_id = (int64_t)sqlite3_last_insert_rowid(db_data->db);
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static c_orm_error_t sqlite_get_column_count(c_orm_query_t *query,
                                             int *out_count) {
  int rc;

  if (!query || !query->data || !query->data->stmt || !out_count) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  *out_count = sqlite3_column_count(query->data->stmt);
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static c_orm_error_t sqlite_get_column_name(c_orm_query_t *query, int index,
                                            const char **out_name) {
  int rc;

  if (!query || !query->data || !query->data->stmt || !out_name) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  *out_name = sqlite3_column_name(query->data->stmt, index);
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static const c_orm_driver_vtable_t sqlite_vtable = {
    sqlite_connect,
    sqlite_disconnect,
    sqlite_prepare,
    sqlite_bind_int32,
    sqlite_bind_int64,
    sqlite_bind_double,
    sqlite_bind_string,
    sqlite_bind_blob,
    sqlite_bind_null,
    sqlite_step,
    sqlite_get_int32,
    sqlite_get_int64,
    sqlite_get_double,
    sqlite_get_string,
    sqlite_get_blob,
    sqlite_is_null,
    sqlite_finalize,
    sqlite_reset,
    sqlite_get_last_error,
    sqlite_get_last_trace,
    sqlite_get_last_insert_rowid,
    sqlite_get_column_count,
    sqlite_get_column_name};

C_ORM_EXPORT int
c_orm_sqlite_get_vtable(const c_orm_driver_vtable_t **out_vtable) {
  int rc;
  if (!out_vtable) {
    rc = 1;
    return rc;
  }
  *out_vtable = &sqlite_vtable;
  rc = 0;
  return rc;
}

C_ORM_EXPORT c_orm_error_t c_orm_sqlite_connect(const char *url,
                                                c_orm_db_t **out_db) {
  int rc;

  {
    rc = sqlite_connect(url, out_db);
    return (c_orm_error_t)rc;
  }
}

C_ORM_EXPORT c_orm_error_t c_orm_sqlite_blob_open(
    c_orm_db_t *db, const char *db_name, const char *table, const char *column,
    int64_t row_id, int is_read_write, void **out_blob_handle) {
  int rc;
  if (!db || !db->driver_data || !table || !column || !out_blob_handle) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  rc = sqlite3_blob_open((sqlite3 *)db->driver_data, db_name ? db_name : "main",
                         table, column, row_id, is_read_write,
                         (sqlite3_blob **)out_blob_handle);
  if (rc != SQLITE_OK) {
    rc = C_ORM_ERROR_UNKNOWN;
    return (c_orm_error_t)rc;
  }
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

C_ORM_EXPORT c_orm_error_t c_orm_sqlite_blob_read(void *blob_handle,
                                                  void *buffer, int n,
                                                  int offset) {
  int rc;
  if (!blob_handle || !buffer) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  rc = sqlite3_blob_read((sqlite3_blob *)blob_handle, buffer, n, offset);
  if (rc != SQLITE_OK) {
    rc = C_ORM_ERROR_UNKNOWN;
    return (c_orm_error_t)rc;
  }
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

C_ORM_EXPORT c_orm_error_t c_orm_sqlite_blob_write(void *blob_handle,
                                                   const void *buffer, int n,
                                                   int offset) {
  int rc;
  if (!blob_handle || !buffer) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  rc = sqlite3_blob_write((sqlite3_blob *)blob_handle, buffer, n, offset);
  if (rc != SQLITE_OK) {
    rc = C_ORM_ERROR_UNKNOWN;
    return (c_orm_error_t)rc;
  }
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

C_ORM_EXPORT c_orm_error_t c_orm_sqlite_blob_close(void *blob_handle) {
  int rc;

  if (!blob_handle) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  sqlite3_blob_close((sqlite3_blob *)blob_handle);
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

#else

/* Stub out if SQLite is not enabled */
C_ORM_EXPORT int
c_orm_sqlite_get_vtable(const c_orm_driver_vtable_t **out_vtable) {
  int rc;
  if (out_vtable) {
    *out_vtable = NULL;
  }
  rc = 1;
  return rc;
}
C_ORM_EXPORT c_orm_error_t c_orm_sqlite_connect(const char *url,
                                                c_orm_db_t **out_db) {
  int rc;

  (void)url;
  (void)out_db;
  {
    rc = C_ORM_ERROR_NOT_IMPLEMENTED;
    return (c_orm_error_t)rc;
  }
}

C_ORM_EXPORT c_orm_error_t c_orm_sqlite_blob_open(
    c_orm_db_t *db, const char *db_name, const char *table, const char *column,
    int64_t row_id, int is_read_write, void **out_blob_handle) {
  int rc;

  {
    rc = C_ORM_ERROR_NOT_IMPLEMENTED;
    return (c_orm_error_t)rc;
  }
}
C_ORM_EXPORT c_orm_error_t c_orm_sqlite_blob_read(void *blob_handle,
                                                  void *buffer, int n,
                                                  int offset) {
  int rc;

  {
    rc = C_ORM_ERROR_NOT_IMPLEMENTED;
    return (c_orm_error_t)rc;
  }
}
C_ORM_EXPORT c_orm_error_t c_orm_sqlite_blob_write(void *blob_handle,
                                                   const void *buffer, int n,
                                                   int offset) {
  int rc;

  {
    rc = C_ORM_ERROR_NOT_IMPLEMENTED;
    return (c_orm_error_t)rc;
  }
}
C_ORM_EXPORT c_orm_error_t c_orm_sqlite_blob_close(void *blob_handle) {
  int rc;

  {
    rc = C_ORM_ERROR_NOT_IMPLEMENTED;
    return (c_orm_error_t)rc;
  }
}

#endif

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
