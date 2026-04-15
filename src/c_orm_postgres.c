/**
 * @file c_orm_postgres.c
 * @brief PostgreSQL driver implementation for c-orm.
 */

/* clang-format off */
#include "c_orm_postgres.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef C_ORM_ENABLE_POSTGRESQL
#include <libpq-fe.h>
#include <libpq/libpq-fs.h>
#endif
/* clang-format on */

#ifdef C_ORM_ENABLE_POSTGRESQL
#endif

#ifdef C_ORM_ENABLE_POSTGRESQL

#if defined(_MSC_VER)
#define INT64_FORMAT "%I64d"
#else
#define INT64_FORMAT "%lld"
#endif

struct postgres_db_data {
  PGconn *conn;
  char last_error[512];
};

struct postgres_query_data {
  c_orm_db_t *db;
  char *stmt_name;
  char *query_string;
  int param_count;

  /* Bind parameters */
  char **param_values;
  int *param_lengths;
  int *param_formats;

  /* Result */
  PGresult *res;
  int current_row;
  int row_count;
};

static int generate_stmt_name(char *buf, size_t size) {
  int rc;
  static int counter = 0;
#if defined(_MSC_VER)
  rc = sprintf_s(buf, size, "c_orm_stmt_%d", ++counter);
#else
#if defined(_MSC_VER)
  sprintf_s(buf, sizeof(buf), "c_orm_stmt_%d", ++counter);
#else
  rc = sprintf(buf, "c_orm_stmt_%d", ++counter);
#endif
#endif
  return rc;
}

/* Replace '?' with '$1', '$2', etc. */
static char *rewrite_query(const char *sql, int *out_param_count) {
  size_t len = strlen(sql);
  size_t new_len = len;
  char *new_sql;
  const char *p;
  char *q;
  int count = 0;
  char num_buf[32];

  for (p = sql; *p; p++) {
    if (*p == '?') {
      count++;
      new_len += 10; /* Extra space for variable numbers */
    }
  }

  new_sql = (char *)malloc(new_len + 1);
  if (!new_sql)
    return NULL;

  p = sql;
  q = new_sql;
  count = 0;
  while (*p) {
    if (*p == '?') {
      int num_len;
      count++;
#if defined(_MSC_VER)
      num_len = sprintf_s(num_buf, sizeof(num_buf), "$%d", count);
#else
#if defined(_MSC_VER)
      sprintf_s(num_buf, sizeof(num_buf), "$%d", count);
#else
      num_len = sprintf(num_buf, "$%d", count);
#endif
#endif
      memcpy(q, num_buf, num_len);
      q += num_len;
      p++;
    } else {
      *q++ = *p++;
    }
  }
  *q = '\0';
  *out_param_count = count;
  return new_sql;
}

static void set_error(c_orm_db_t *db, const char *msg) {
  if (db && db->driver_data) {
    struct postgres_db_data *data = (struct postgres_db_data *)db->driver_data;
    if (msg) {
      size_t len = strlen(msg);
      if (len >= sizeof(data->last_error))
        len = sizeof(data->last_error) - 1;
      memcpy(data->last_error, msg, len);
      data->last_error[len] = '\0';
    } else if (data->conn) {
      const char *pq_err = PQerrorMessage(data->conn);
      size_t len = strlen(pq_err);
      if (len >= sizeof(data->last_error))
        len = sizeof(data->last_error) - 1;
      memcpy(data->last_error, pq_err, len);
      data->last_error[len] = '\0';
    }
  }
}

static c_orm_error_t postgres_connect(const char *url, c_orm_db_t **out_db) {
  int rc;

  c_orm_db_t *db;
  struct postgres_db_data *data;

  if (!url || !out_db) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  db = (c_orm_db_t *)calloc(1, sizeof(c_orm_db_t));
  if (!db) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  data = (struct postgres_db_data *)calloc(1, sizeof(struct postgres_db_data));
  if (!data) {
    free(db);
    {
      rc = C_ORM_ERROR_MEMORY;
      return (c_orm_error_t)rc;
    }
  }

  data->conn = PQconnectdb(url);
  if (PQstatus(data->conn) != CONNECTION_OK) {
    set_error(db, PQerrorMessage(data->conn));
    PQfinish(data->conn);
    free(data);
    free(db);
    {
      rc = C_ORM_ERROR_CONNECTION;
      return (c_orm_error_t)rc;
    }
  }

  if (c_orm_postgres_get_vtable(&db->vtable) != 0) {
    PQfinish(data->conn);
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

static c_orm_error_t postgres_disconnect(c_orm_db_t *db) {
  int rc;

  struct postgres_db_data *data;
  if (!db) {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }

  data = (struct postgres_db_data *)db->driver_data;
  if (data) {
    if (data->conn) {
      PQfinish(data->conn);
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
  struct postgres_query_data *data;
};

static c_orm_error_t postgres_prepare(c_orm_db_t *db, const char *sql,
                                      c_orm_query_t **out_query) {
  int rc;

  struct postgres_db_data *db_data;
  struct postgres_query_data *q_data;
  c_orm_query_t *query;
  char *new_sql;
  int param_count = 0;
  char stmt_name[64];
  PGresult *res;

  if (!db || !sql || !out_query) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  db_data = (struct postgres_db_data *)db->driver_data;

  if (db->log_cb) {
    db->log_cb(sql, db->log_user_data);
  }

  new_sql = rewrite_query(sql, &param_count);
  if (!new_sql) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  query = (c_orm_query_t *)calloc(1, sizeof(c_orm_query_t));
  if (!query) {
    free(new_sql);
    {
      rc = C_ORM_ERROR_MEMORY;
      return (c_orm_error_t)rc;
    }
  }

  q_data = (struct postgres_query_data *)calloc(
      1, sizeof(struct postgres_query_data));
  if (!q_data) {
    free(query);
    free(new_sql);
    {
      rc = C_ORM_ERROR_MEMORY;
      return (c_orm_error_t)rc;
    }
  }

  generate_stmt_name(stmt_name, sizeof(stmt_name));

  res = PQprepare(db_data->conn, stmt_name, new_sql, param_count, NULL);
  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    set_error(db, PQerrorMessage(db_data->conn));
    PQclear(res);
    free(q_data);
    free(query);
    free(new_sql);
    {
      rc = C_ORM_ERROR_SQL;
      return (c_orm_error_t)rc;
    }
  }
  PQclear(res);

  q_data->db = db;
  q_data->query_string = new_sql;

  q_data->stmt_name = (char *)malloc(strlen(stmt_name) + 1);
#if defined(_MSC_VER)
  strcpy_s(q_data->stmt_name, strlen(stmt_name) + 1, stmt_name);
#else
  strcpy(q_data->stmt_name, stmt_name);
#endif

  q_data->param_count = param_count;
  if (param_count > 0) {
    q_data->param_values = (char **)calloc(param_count, sizeof(char *));
    q_data->param_lengths = (int *)calloc(param_count, sizeof(int));
    q_data->param_formats = (int *)calloc(param_count, sizeof(int));
  }

  q_data->res = NULL;
  q_data->current_row = 0;
  q_data->row_count = 0;

  query->data = q_data;
  *out_query = query;

  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static void free_param(struct postgres_query_data *q_data, int index) {
  if (q_data->param_values[index - 1]) {
    free(q_data->param_values[index - 1]);
    q_data->param_values[index - 1] = NULL;
  }
}

static char *orm_strdup(const char *s) {
  size_t len = strlen(s);
  char *dup = (char *)malloc(len + 1);
  if (dup) {
#if defined(_MSC_VER)
    strcpy_s(dup, len + 1, s);
#else
    strcpy(dup, s);
#endif
  }
  return dup;
}

static c_orm_error_t postgres_bind_int32(c_orm_query_t *query, int index,
                                         int32_t val) {
  int rc;

  char buf[32];
  if (!query || !query->data || index < 1 || index > query->data->param_count) {
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }
  free_param(query->data, index);
#if defined(_MSC_VER)
  sprintf_s(buf, sizeof(buf), "%d", (int)val);
#else
#if defined(_MSC_VER)
  sprintf_s(buf, sizeof(buf), "%d", (int)val);
#else
  sprintf(buf, "%d", (int)val);
#endif
#endif
  query->data->param_values[index - 1] = orm_strdup(buf);
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static c_orm_error_t postgres_bind_int64(c_orm_query_t *query, int index,
                                         int64_t val) {
  int rc;

  char buf[64];
  if (!query || !query->data || index < 1 || index > query->data->param_count) {
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }
  free_param(query->data, index);
#if defined(_MSC_VER)
  sprintf_s(buf, sizeof(buf), INT64_FORMAT, (long long)val);
#else
#if defined(_MSC_VER)
  sprintf_s(buf, sizeof(buf), INT64_FORMAT, (long long)val);
#else
  sprintf(buf, INT64_FORMAT, (long long)val);
#endif
#endif
  query->data->param_values[index - 1] = orm_strdup(buf);
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static c_orm_error_t postgres_bind_double(c_orm_query_t *query, int index,
                                          double val) {
  int rc;

  char buf[64];
  if (!query || !query->data || index < 1 || index > query->data->param_count) {
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }
  free_param(query->data, index);
#if defined(_MSC_VER)
  sprintf_s(buf, sizeof(buf), "%.17g", val);
#else
#if defined(_MSC_VER)
  sprintf_s(buf, sizeof(buf), "%.17g", val);
#else
  sprintf(buf, "%.17g", val);
#endif
#endif
  query->data->param_values[index - 1] = orm_strdup(buf);
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static c_orm_error_t postgres_bind_string(c_orm_query_t *query, int index,
                                          const char *val) {
  int rc;

  if (!query || !query->data || index < 1 || index > query->data->param_count) {
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }
  free_param(query->data, index);
  query->data->param_values[index - 1] = orm_strdup(val);
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static c_orm_error_t postgres_bind_blob(c_orm_query_t *query, int index,
                                        const void *val, size_t size) {
  int rc;

  if (!query || !query->data || index < 1 || index > query->data->param_count) {
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }
  free_param(query->data, index);
  query->data->param_values[index - 1] = malloc(size);
  if (query->data->param_values[index - 1]) {
    memcpy(query->data->param_values[index - 1], val, size);
  }
  query->data->param_lengths[index - 1] = (int)size;
  query->data->param_formats[index - 1] = 1; /* binary format */
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static c_orm_error_t postgres_bind_null(c_orm_query_t *query, int index) {
  int rc;

  if (!query || !query->data || index < 1 || index > query->data->param_count) {
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }
  free_param(query->data, index);
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static c_orm_error_t postgres_step(c_orm_query_t *query, int *out_has_row) {
  int rc;

  struct postgres_query_data *q_data;
  struct postgres_db_data *db_data;

  if (!query || !query->data || !out_has_row) {
    rc = C_ORM_ERROR_STEP;
    return (c_orm_error_t)rc;
  }
  q_data = query->data;
  db_data = (struct postgres_db_data *)q_data->db->driver_data;

  if (q_data->res == NULL) {
    q_data->res =
        PQexecPrepared(db_data->conn, q_data->stmt_name, q_data->param_count,
                       (const char *const *)q_data->param_values,
                       q_data->param_lengths, q_data->param_formats, 0);

    if (PQresultStatus(q_data->res) != PGRES_TUPLES_OK &&
        PQresultStatus(q_data->res) != PGRES_COMMAND_OK) {
      set_error(q_data->db, PQerrorMessage(db_data->conn));
      PQclear(q_data->res);
      q_data->res = NULL;
      {
        rc = C_ORM_ERROR_STEP;
        return (c_orm_error_t)rc;
      }
    }

    q_data->row_count = PQntuples(q_data->res);
    q_data->current_row = 0;
  } else {
    q_data->current_row++;
  }

  if (q_data->current_row < q_data->row_count) {
    *out_has_row = 1;
    {
      rc = C_ORM_OK;
      return (c_orm_error_t)rc;
    }
  } else {
    *out_has_row = 0;
    {
      rc = C_ORM_OK;
      return (c_orm_error_t)rc;
    }
  }
}

static c_orm_error_t postgres_get_int32(c_orm_query_t *query, int index,
                                        int32_t *out_val) {
  int rc;

  const char *val;
  if (!query || !query->data || !query->data->res || !out_val) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  val = PQgetvalue(query->data->res, query->data->current_row, index);
  *out_val = (int32_t)atoi(val);
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static c_orm_error_t postgres_get_int64(c_orm_query_t *query, int index,
                                        int64_t *out_val) {
  int rc;

  const char *val;
  if (!query || !query->data || !query->data->res || !out_val) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  val = PQgetvalue(query->data->res, query->data->current_row, index);
#if defined(_MSC_VER)
  *out_val = (int64_t)_atoi64(val);
#else
  *out_val = (int64_t)atoll(val);
#endif
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static c_orm_error_t postgres_get_double(c_orm_query_t *query, int index,
                                         double *out_val) {
  int rc;

  const char *val;
  if (!query || !query->data || !query->data->res || !out_val) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  val = PQgetvalue(query->data->res, query->data->current_row, index);
  *out_val = atof(val);
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static c_orm_error_t postgres_get_string(c_orm_query_t *query, int index,
                                         const char **out_val) {
  int rc;

  if (!query || !query->data || !query->data->res || !out_val) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  *out_val = PQgetvalue(query->data->res, query->data->current_row, index);
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static c_orm_error_t postgres_get_blob(c_orm_query_t *query, int index,
                                       const void **out_val, size_t *out_size) {
  int rc;

  if (!query || !query->data || !query->data->res || !out_val || !out_size) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  *out_val = PQgetvalue(query->data->res, query->data->current_row, index);
  *out_size = PQgetlength(query->data->res, query->data->current_row, index);
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static c_orm_error_t postgres_is_null(c_orm_query_t *query, int index,
                                      int *out_is_null) {
  int rc;

  if (!query || !query->data || !query->data->res || !out_is_null) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  *out_is_null =
      PQgetisnull(query->data->res, query->data->current_row, index) ? 1 : 0;
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static c_orm_error_t postgres_finalize(c_orm_query_t *query) {
  int rc;

  int i;
  if (!query) {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
  if (query->data) {
    if (query->data->res) {
      PQclear(query->data->res);
    }
    if (query->data->param_count > 0) {
      for (i = 0; i < query->data->param_count; i++) {
        free_param(query->data, i + 1);
      }
      free(query->data->param_values);
      free(query->data->param_lengths);
      free(query->data->param_formats);
    }
    free(query->data->stmt_name);
    free(query->data->query_string);
    free(query->data);
  }
  free(query);
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static c_orm_error_t postgres_reset(c_orm_query_t *query) {
  int rc;

  int i;
  if (!query || !query->data) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  if (query->data->res) {
    PQclear(query->data->res);
    query->data->res = NULL;
  }
  for (i = 0; i < query->data->param_count; i++) {
    free_param(query->data, i + 1);
  }
  query->data->current_row = 0;
  query->data->row_count = 0;
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

static int postgres_get_last_error(c_orm_db_t *db, const char **out_message) {
  int rc;
  struct postgres_db_data *data;
  if (!out_message) {
    rc = 1;
    return rc;
  }
  if (!db || !db->driver_data) {
    *out_message = "Invalid DB object";
    rc = 1;
    return rc;
  }
  data = (struct postgres_db_data *)db->driver_data;
  *out_message = data->last_error;
  rc = 0;
  return rc;
}

static int postgres_get_last_trace(c_orm_db_t *db, const char **out_trace) {
  int rc;
  /* Step 265 / 266 */
  if (out_trace) {
    *out_trace =
        "Postgres Driver Stack Trace: Requires external cdd-c AST diagnostic "
        "parsing locally integrated inside driver compilation units natively. "
        "Returning Last error mapping fallback.";
  }
  (void)db;
  rc = 0;
  return rc;
}

static c_orm_error_t postgres_get_last_insert_rowid(c_orm_db_t *db,
                                                    int64_t *out_id) {
  int rc;

  /* Postgres doesn't have a single last_insert_rowid function, usually requires
   * RETURNING clause. Stub implementation for now. */
  (void)db;
  if (out_id)
    *out_id = 0;
  {
    rc = C_ORM_ERROR_NOT_IMPLEMENTED;
    return (c_orm_error_t)rc;
  }
}

static const c_orm_driver_vtable_t postgres_vtable = {
    postgres_connect,
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
    postgres_get_last_insert_rowid};

C_ORM_EXPORT int
c_orm_postgres_get_vtable(const c_orm_driver_vtable_t **out_vtable) {
  int rc;
  if (!out_vtable) {
    rc = 1;
    return rc;
  }
  *out_vtable = &postgres_vtable;
  rc = 0;
  return rc;
}

C_ORM_EXPORT c_orm_error_t c_orm_postgres_connect(const char *url,
                                                  c_orm_db_t **out_db) {
  int rc;

  {
    rc = postgres_connect(url, out_db);
    return (c_orm_error_t)rc;
  }
}

C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_create(c_orm_db_t *db,
                                                    unsigned int *out_oid) {
  int rc;

  struct postgres_db_data *data;
  Oid oid;
  if (!db || !db->driver_data || !out_oid) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  data = (struct postgres_db_data *)db->driver_data;
  oid = lo_creat(data->conn, INV_READ | INV_WRITE);
  if (oid == InvalidOid) {
    rc = C_ORM_ERROR_UNKNOWN;
    return (c_orm_error_t)rc;
  }
  *out_oid = (unsigned int)oid;
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_open(c_orm_db_t *db,
                                                  unsigned int oid, int mode,
                                                  void **out_fd) {
  int rc;

  struct postgres_db_data *data;
  int fd;
  if (!db || !db->driver_data || !out_fd) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  data = (struct postgres_db_data *)db->driver_data;
  fd = lo_open(data->conn, (Oid)oid, mode);
  if (fd < 0) {
    rc = C_ORM_ERROR_UNKNOWN;
    return (c_orm_error_t)rc;
  }
  /* Cast int to void* for abstraction */
  *out_fd = (void *)(intptr_t)fd;
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_read(c_orm_db_t *db, void *fd,
                                                  void *buffer, size_t len,
                                                  size_t *out_read) {
  int rc;

  struct postgres_db_data *data;
  int bytes_read;
  if (!db || !db->driver_data || !buffer || !out_read) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  data = (struct postgres_db_data *)db->driver_data;
  bytes_read = lo_read(data->conn, (int)(intptr_t)fd, (char *)buffer, len);
  if (bytes_read < 0) {
    rc = C_ORM_ERROR_UNKNOWN;
    return (c_orm_error_t)rc;
  }
  *out_read = (size_t)bytes_read;
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_write(c_orm_db_t *db, void *fd,
                                                   const void *buffer,
                                                   size_t len,
                                                   size_t *out_written) {
  int rc;

  struct postgres_db_data *data;
  int bytes_written;
  if (!db || !db->driver_data || !buffer || !out_written) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  data = (struct postgres_db_data *)db->driver_data;
  bytes_written =
      lo_write(data->conn, (int)(intptr_t)fd, (const char *)buffer, len);
  if (bytes_written < 0) {
    rc = C_ORM_ERROR_UNKNOWN;
    return (c_orm_error_t)rc;
  }
  *out_written = (size_t)bytes_written;
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_close(c_orm_db_t *db, void *fd) {
  struct postgres_db_data *data;
  int rc;
  if (!db || !db->driver_data) {
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  data = (struct postgres_db_data *)db->driver_data;
  rc = lo_close(data->conn, (int)(intptr_t)fd);
  if (rc < 0) {
    rc = C_ORM_ERROR_UNKNOWN;
    return (c_orm_error_t)rc;
  }
  {
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

#else

/* Stub out if PostgreSQL is not enabled */
C_ORM_EXPORT int
c_orm_postgres_get_vtable(const c_orm_driver_vtable_t **out_vtable) {
  int rc;
  if (out_vtable) {
    *out_vtable = NULL;
  }
  rc = 1;
  return rc;
}

C_ORM_EXPORT c_orm_error_t c_orm_postgres_connect(const char *url,
                                                  c_orm_db_t **out_db) {
  int rc;

  (void)url;
  (void)out_db;
  {
    rc = C_ORM_ERROR_NOT_IMPLEMENTED;
    return (c_orm_error_t)rc;
  }
}

C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_create(c_orm_db_t *db,
                                                    unsigned int *out_oid) {
  int rc;

  (void)db;
  (void)out_oid;
  {
    rc = C_ORM_ERROR_NOT_IMPLEMENTED;
    return (c_orm_error_t)rc;
  }
}
C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_open(c_orm_db_t *db,
                                                  unsigned int oid, int mode,
                                                  void **out_fd) {
  int rc;

  (void)db;
  (void)oid;
  (void)mode;
  (void)out_fd;
  {
    rc = C_ORM_ERROR_NOT_IMPLEMENTED;
    return (c_orm_error_t)rc;
  }
}
C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_read(c_orm_db_t *db, void *fd,
                                                  void *buffer, size_t len,
                                                  size_t *out_read) {
  int rc;

  (void)db;
  (void)fd;
  (void)buffer;
  (void)len;
  (void)out_read;
  {
    rc = C_ORM_ERROR_NOT_IMPLEMENTED;
    return (c_orm_error_t)rc;
  }
}
C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_write(c_orm_db_t *db, void *fd,
                                                   const void *buffer,
                                                   size_t len,
                                                   size_t *out_written) {
  int rc;

  (void)db;
  (void)fd;
  (void)buffer;
  (void)len;
  (void)out_written;
  {
    rc = C_ORM_ERROR_NOT_IMPLEMENTED;
    return (c_orm_error_t)rc;
  }
}
C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_close(c_orm_db_t *db, void *fd) {
  int rc;

  (void)db;
  (void)fd;
  {
    rc = C_ORM_ERROR_NOT_IMPLEMENTED;
    return (c_orm_error_t)rc;
  }
}

#endif
