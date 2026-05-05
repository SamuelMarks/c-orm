/**
 * @file c_orm_postgres.c
 * @brief PostgreSQL driver implementation for c-orm.
 */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_postgres.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "c_orm_log.h"
#ifdef C_ORM_ENABLE_POSTGRESQL
#include <libpq-fe.h>
#include <libpq/libpq-fs.h>
#endif
/* clang-format on */

#ifdef C_ORM_ENABLE_POSTGRESQL

#if defined(_MSC_VER)
#define INT64_FORMAT "%I64d"
#else
#define INT64_FORMAT "%lld"
#endif

/** @brief Postgres DB data */
struct postgres_db_data {
  PGconn *conn;
  char last_error[512];
};

/** @brief Postgres query data */
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

/** @brief Generate unique statement name */
static int generate_stmt_name(char *buf, size_t size) {
  int rc;
  static int counter = 0;
  LOG_DEBUG("generate_stmt_name: entry");
  rc = C_ORM_SPRINTF(buf, size, "c_orm_stmt_%d", ++counter);
  LOG_DEBUG("generate_stmt_name: exit");
  return rc;
}

/** @brief Rewrite query to postgres format */
static char *rewrite_query(const char *sql, int *out_param_count) {
  size_t len;
  size_t new_len;
  char *new_sql;
  const char *p;
  char *q;
  int count = 0;
  char num_buf[32];

  LOG_DEBUG("rewrite_query: entry");

  len = strlen(sql);
  new_len = len;

  for (p = sql; *p; p++) {
    if (*p == '?') {
      count++;
      new_len += 10; /* Extra space for variable numbers */
    }
  }

  new_sql = (char *)C_ORM_MALLOC(new_len + 1);
  if (!new_sql) {
    LOG_DEBUG("rewrite_query: OOM");
    return NULL;
  }

  p = sql;
  q = new_sql;
  count = 0;
  while (*p) {
    if (*p == '?') {
      int num_len;
      count++;
      num_len = C_ORM_SPRINTF(num_buf, sizeof(num_buf), "$%d", count);
      memcpy(q, num_buf, num_len);
      q += num_len;
      p++;
    } else {
      *q++ = *p++;
    }
  }
  *q = '\0';
  *out_param_count = count;
  LOG_DEBUG("rewrite_query: exit");
  return new_sql;
}

/** @brief Set error message */
static void set_error(c_orm_db_t *db, const char *msg) {
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
      const char *pq_err = PQerrorMessage(data->conn);
      size_t len = strlen(pq_err);
      if (len >= sizeof(data->last_error))
        len = sizeof(data->last_error) - 1;
      memcpy(data->last_error, pq_err, len);
      data->last_error[len] = '\0';
    }
  }
  LOG_DEBUG("set_error: exit");
}

/** @brief Postgres connect */
static c_orm_error_t postgres_connect(const char *url, c_orm_db_t **out_db) {
  int rc;
  c_orm_db_t *db;
  struct postgres_db_data *data;

  LOG_DEBUG("postgres_connect: entry");

  if (!url || !out_db) {
    LOG_DEBUG("postgres_connect: invalid args");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  db = (c_orm_db_t *)calloc(1, sizeof(c_orm_db_t));
  if (!db) {
    LOG_DEBUG("postgres_connect: OOM db");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  data = (struct postgres_db_data *)calloc(1, sizeof(struct postgres_db_data));
  if (!data) {
    LOG_DEBUG("postgres_connect: OOM data");
    C_ORM_FREE(db);
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  data->conn = PQconnectdb(url);
  if (PQstatus(data->conn) != CONNECTION_OK) {
    LOG_DEBUG("postgres_connect: connection failed");
    set_error(db, PQerrorMessage(data->conn));
    PQfinish(data->conn);
    C_ORM_FREE(data);
    C_ORM_FREE(db);
    rc = C_ORM_ERROR_CONNECTION;
    return (c_orm_error_t)rc;
  }

  if (c_orm_postgres_get_vtable(&db->vtable) != 0) {
    LOG_DEBUG("postgres_connect: vtable failed");
    PQfinish(data->conn);
    C_ORM_FREE(data);
    C_ORM_FREE(db);
    rc = C_ORM_ERROR_UNKNOWN;
    return (c_orm_error_t)rc;
  }
  db->driver_data = data;
  *out_db = db;

  LOG_DEBUG("postgres_connect: exit");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/** @brief Postgres disconnect */
static c_orm_error_t postgres_disconnect(c_orm_db_t *db) {
  int rc;
  struct postgres_db_data *data;

  LOG_DEBUG("postgres_disconnect: entry");
  if (!db) {
    LOG_DEBUG("postgres_disconnect: no db");
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }

  data = (struct postgres_db_data *)db->driver_data;
  if (data) {
    if (data->conn) {
      PQfinish(data->conn);
    }
    C_ORM_FREE(data);
  }
  C_ORM_FREE(db);

  LOG_DEBUG("postgres_disconnect: exit");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/** @brief c_orm_query */
struct c_orm_query {
  struct postgres_query_data *data;
};

/** @brief Postgres prepare */
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

  LOG_DEBUG("postgres_prepare: entry");

  if (!db || !sql || !out_query) {
    LOG_DEBUG("postgres_prepare: invalid args");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  db_data = (struct postgres_db_data *)db->driver_data;

  if (db->log_cb) {
    db->log_cb(sql, db->log_user_data);
  }

  new_sql = rewrite_query(sql, &param_count);
  if (!new_sql) {
    LOG_DEBUG("postgres_prepare: rewrite OOM");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  query = (c_orm_query_t *)calloc(1, sizeof(c_orm_query_t));
  if (!query) {
    LOG_DEBUG("postgres_prepare: OOM query");
    C_ORM_FREE(new_sql);
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  q_data = (struct postgres_query_data *)calloc(
      1, sizeof(struct postgres_query_data));
  if (!q_data) {
    LOG_DEBUG("postgres_prepare: OOM q_data");
    C_ORM_FREE(query);
    C_ORM_FREE(new_sql);
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  generate_stmt_name(stmt_name, sizeof(stmt_name));

  res = PQprepare(db_data->conn, stmt_name, new_sql, param_count, NULL);
  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    LOG_DEBUG("postgres_prepare: prepare failed");
    set_error(db, PQerrorMessage(db_data->conn));
    PQclear(res);
    C_ORM_FREE(q_data);
    C_ORM_FREE(query);
    C_ORM_FREE(new_sql);
    rc = C_ORM_ERROR_SQL;
    return (c_orm_error_t)rc;
  }
  PQclear(res);

  q_data->db = db;
  q_data->query_string = new_sql;

  q_data->stmt_name = (char *)C_ORM_MALLOC(strlen(stmt_name) + 1);
  if (!q_data->stmt_name) {
    LOG_DEBUG("postgres_prepare: OOM stmt_name");
    C_ORM_FREE(q_data);
    C_ORM_FREE(query);
    C_ORM_FREE(new_sql);
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  C_ORM_STRCPY(q_data->stmt_name, strlen(stmt_name) + 1, stmt_name);

  q_data->param_count = param_count;
  if (param_count > 0) {
    q_data->param_values = (char **)calloc(param_count, sizeof(char *));
    q_data->param_lengths = (int *)calloc(param_count, sizeof(int));
    q_data->param_formats = (int *)calloc(param_count, sizeof(int));
    if (!q_data->param_values || !q_data->param_lengths ||
        !q_data->param_formats) {
      LOG_DEBUG("postgres_prepare: OOM params");
      if (q_data->param_values)
        C_ORM_FREE(q_data->param_values);
      if (q_data->param_lengths)
        C_ORM_FREE(q_data->param_lengths);
      if (q_data->param_formats)
        C_ORM_FREE(q_data->param_formats);
      C_ORM_FREE(q_data->stmt_name);
      C_ORM_FREE(q_data);
      C_ORM_FREE(query);
      C_ORM_FREE(new_sql);
      rc = C_ORM_ERROR_MEMORY;
      return (c_orm_error_t)rc;
    }
  }

  q_data->res = NULL;
  q_data->current_row = 0;
  q_data->row_count = 0;

  query->data = q_data;
  *out_query = query;

  LOG_DEBUG("postgres_prepare: exit");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/** @brief Free a parameter */
static void free_param(struct postgres_query_data *q_data, int index) {
  LOG_DEBUG("free_param: entry");
  if (q_data->param_values[index - 1]) {
    C_ORM_FREE(q_data->param_values[index - 1]);
    q_data->param_values[index - 1] = NULL;
  }
  LOG_DEBUG("free_param: exit");
}

/** @brief Strdup for ORM */
static char *orm_strdup(const char *s) {
  size_t len;
  char *dup;
  LOG_DEBUG("orm_strdup: entry");
  len = strlen(s);
  dup = (char *)C_ORM_MALLOC(len + 1);
  if (dup) {
    C_ORM_STRCPY(dup, len + 1, s);
  }
  LOG_DEBUG("orm_strdup: exit");
  return dup;
}

/** @brief Bind int32 */
static c_orm_error_t postgres_bind_int32(c_orm_query_t *query, int index,
                                         int32_t val) {
  int rc;
  char buf[32];

  LOG_DEBUG("postgres_bind_int32: entry");
  if (!query || !query->data || index < 1 || index > query->data->param_count) {
    LOG_DEBUG("postgres_bind_int32: invalid args");
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }
  free_param(query->data, index);
  C_ORM_SPRINTF(buf, sizeof(buf), "%d", (int)val);
  query->data->param_values[index - 1] = orm_strdup(buf);
  if (!query->data->param_values[index - 1]) {
    LOG_DEBUG("postgres_bind_int32: OOM");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  LOG_DEBUG("postgres_bind_int32: exit");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/** @brief Bind int64 */
static c_orm_error_t postgres_bind_int64(c_orm_query_t *query, int index,
                                         int64_t val) {
  int rc;
  char buf[64];

  LOG_DEBUG("postgres_bind_int64: entry");
  if (!query || !query->data || index < 1 || index > query->data->param_count) {
    LOG_DEBUG("postgres_bind_int64: invalid args");
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }
  free_param(query->data, index);
  C_ORM_SPRINTF(buf, sizeof(buf), INT64_FORMAT, (long long)val);
  query->data->param_values[index - 1] = orm_strdup(buf);
  if (!query->data->param_values[index - 1]) {
    LOG_DEBUG("postgres_bind_int64: OOM");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  LOG_DEBUG("postgres_bind_int64: exit");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/** @brief Bind double */
static c_orm_error_t postgres_bind_double(c_orm_query_t *query, int index,
                                          double val) {
  int rc;
  char buf[64];

  LOG_DEBUG("postgres_bind_double: entry");
  if (!query || !query->data || index < 1 || index > query->data->param_count) {
    LOG_DEBUG("postgres_bind_double: invalid args");
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }
  free_param(query->data, index);
  C_ORM_SPRINTF(buf, sizeof(buf), "%.17g", val);
  query->data->param_values[index - 1] = orm_strdup(buf);
  if (!query->data->param_values[index - 1]) {
    LOG_DEBUG("postgres_bind_double: OOM");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  LOG_DEBUG("postgres_bind_double: exit");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/** @brief Bind string */
static c_orm_error_t postgres_bind_string(c_orm_query_t *query, int index,
                                          const char *val) {
  int rc;

  LOG_DEBUG("postgres_bind_string: entry");
  if (!query || !query->data || index < 1 || index > query->data->param_count) {
    LOG_DEBUG("postgres_bind_string: invalid args");
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }
  free_param(query->data, index);
  query->data->param_values[index - 1] = orm_strdup(val);
  if (!query->data->param_values[index - 1]) {
    LOG_DEBUG("postgres_bind_string: OOM");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  LOG_DEBUG("postgres_bind_string: exit");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/** @brief Bind blob */
static c_orm_error_t postgres_bind_blob(c_orm_query_t *query, int index,
                                        const void *val, size_t size) {
  int rc;

  LOG_DEBUG("postgres_bind_blob: entry");
  if (!query || !query->data || index < 1 || index > query->data->param_count) {
    LOG_DEBUG("postgres_bind_blob: invalid args");
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }
  free_param(query->data, index);
  query->data->param_values[index - 1] = (char *)C_ORM_MALLOC(size);
  if (query->data->param_values[index - 1]) {
    memcpy(query->data->param_values[index - 1], val, size);
  } else {
    LOG_DEBUG("postgres_bind_blob: OOM");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  query->data->param_lengths[index - 1] = (int)size;
  query->data->param_formats[index - 1] = 1; /* binary format */

  LOG_DEBUG("postgres_bind_blob: exit");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/** @brief Bind null */
static c_orm_error_t postgres_bind_null(c_orm_query_t *query, int index) {
  int rc;

  LOG_DEBUG("postgres_bind_null: entry");
  if (!query || !query->data || index < 1 || index > query->data->param_count) {
    LOG_DEBUG("postgres_bind_null: invalid args");
    rc = C_ORM_ERROR_BIND;
    return (c_orm_error_t)rc;
  }
  free_param(query->data, index);

  LOG_DEBUG("postgres_bind_null: exit");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/** @brief Step query */
static c_orm_error_t postgres_step(c_orm_query_t *query, int *out_has_row) {
  int rc;
  struct postgres_query_data *q_data;
  struct postgres_db_data *db_data;

  LOG_DEBUG("postgres_step: entry");

  if (!query || !query->data || !out_has_row) {
    LOG_DEBUG("postgres_step: invalid args");
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
      LOG_DEBUG("postgres_step: exec failed");
      set_error(q_data->db, PQerrorMessage(db_data->conn));
      PQclear(q_data->res);
      q_data->res = NULL;
      rc = C_ORM_ERROR_STEP;
      return (c_orm_error_t)rc;
    }

    q_data->row_count = PQntuples(q_data->res);
    q_data->current_row = 0;
  } else {
    q_data->current_row++;
  }

  if (q_data->current_row < q_data->row_count) {
    *out_has_row = 1;
    LOG_DEBUG("postgres_step: has row");
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  } else {
    *out_has_row = 0;
    LOG_DEBUG("postgres_step: no row");
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }
}

/** @brief Get int32 */
static c_orm_error_t postgres_get_int32(c_orm_query_t *query, int index,
                                        int32_t *out_val) {
  int rc;
  const char *val;

  LOG_DEBUG("postgres_get_int32: entry");
  if (!query || !query->data || !query->data->res || !out_val) {
    LOG_DEBUG("postgres_get_int32: invalid args");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  val = PQgetvalue(query->data->res, query->data->current_row, index);
  *out_val = (int32_t)atoi(val);

  LOG_DEBUG("postgres_get_int32: exit");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/** @brief Get int64 */
static c_orm_error_t postgres_get_int64(c_orm_query_t *query, int index,
                                        int64_t *out_val) {
  int rc;
  const char *val;

  LOG_DEBUG("postgres_get_int64: entry");
  if (!query || !query->data || !query->data->res || !out_val) {
    LOG_DEBUG("postgres_get_int64: invalid args");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  val = PQgetvalue(query->data->res, query->data->current_row, index);
#if defined(_MSC_VER)
  *out_val = (int64_t)_atoi64(val);
#else
  *out_val = (int64_t)atoll(val);
#endif

  LOG_DEBUG("postgres_get_int64: exit");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/** @brief Get double */
static c_orm_error_t postgres_get_double(c_orm_query_t *query, int index,
                                         double *out_val) {
  int rc;
  const char *val;

  LOG_DEBUG("postgres_get_double: entry");
  if (!query || !query->data || !query->data->res || !out_val) {
    LOG_DEBUG("postgres_get_double: invalid args");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  val = PQgetvalue(query->data->res, query->data->current_row, index);
  *out_val = atof(val);

  LOG_DEBUG("postgres_get_double: exit");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/** @brief Get string */
static c_orm_error_t postgres_get_string(c_orm_query_t *query, int index,
                                         const char **out_val) {
  int rc;

  LOG_DEBUG("postgres_get_string: entry");
  if (!query || !query->data || !query->data->res || !out_val) {
    LOG_DEBUG("postgres_get_string: invalid args");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  *out_val = PQgetvalue(query->data->res, query->data->current_row, index);

  LOG_DEBUG("postgres_get_string: exit");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/** @brief Get blob */
static c_orm_error_t postgres_get_blob(c_orm_query_t *query, int index,
                                       const void **out_val, size_t *out_size) {
  int rc;

  LOG_DEBUG("postgres_get_blob: entry");
  if (!query || !query->data || !query->data->res || !out_val || !out_size) {
    LOG_DEBUG("postgres_get_blob: invalid args");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  *out_val = PQgetvalue(query->data->res, query->data->current_row, index);
  *out_size = PQgetlength(query->data->res, query->data->current_row, index);

  LOG_DEBUG("postgres_get_blob: exit");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/** @brief Is null */
static c_orm_error_t postgres_is_null(c_orm_query_t *query, int index,
                                      int *out_is_null) {
  int rc;

  LOG_DEBUG("postgres_is_null: entry");
  if (!query || !query->data || !query->data->res || !out_is_null) {
    LOG_DEBUG("postgres_is_null: invalid args");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  *out_is_null =
      PQgetisnull(query->data->res, query->data->current_row, index) ? 1 : 0;

  LOG_DEBUG("postgres_is_null: exit");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/** @brief Finalize */
static c_orm_error_t postgres_finalize(c_orm_query_t *query) {
  int rc;
  int i;

  LOG_DEBUG("postgres_finalize: entry");
  if (!query) {
    LOG_DEBUG("postgres_finalize: no query");
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
      C_ORM_FREE(query->data->param_values);
      C_ORM_FREE(query->data->param_lengths);
      C_ORM_FREE(query->data->param_formats);
    }
    C_ORM_FREE(query->data->stmt_name);
    C_ORM_FREE(query->data->query_string);
    C_ORM_FREE(query->data);
  }
  C_ORM_FREE(query);

  LOG_DEBUG("postgres_finalize: exit");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/** @brief Reset */
static c_orm_error_t postgres_reset(c_orm_query_t *query) {
  int rc;
  int i;

  LOG_DEBUG("postgres_reset: entry");
  if (!query || !query->data) {
    LOG_DEBUG("postgres_reset: invalid args");
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

  LOG_DEBUG("postgres_reset: exit");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/** @brief Get last error */
static int postgres_get_last_error(c_orm_db_t *db, const char **out_message) {
  int rc;
  struct postgres_db_data *data;

  LOG_DEBUG("postgres_get_last_error: entry");
  if (!out_message) {
    LOG_DEBUG("postgres_get_last_error: invalid args");
    rc = 1;
    return rc;
  }
  if (!db || !db->driver_data) {
    *out_message = "Invalid DB object";
    LOG_DEBUG("postgres_get_last_error: no driver_data");
    rc = 1;
    return rc;
  }
  data = (struct postgres_db_data *)db->driver_data;
  *out_message = data->last_error;

  LOG_DEBUG("postgres_get_last_error: exit");
  rc = 0;
  return rc;
}

/** @brief Get last trace */
static int postgres_get_last_trace(c_orm_db_t *db, const char **out_trace) {
  int rc;

  LOG_DEBUG("postgres_get_last_trace: entry");
  if (out_trace) {
    *out_trace =
        "Postgres Driver Stack Trace: Requires external cdd-c AST diagnostic "
        "parsing locally integrated inside driver compilation units natively. "
        "Returning Last error mapping fallback.";
  }
  (void)db;

  LOG_DEBUG("postgres_get_last_trace: exit");
  rc = 0;
  return rc;
}

/** @brief Get last insert rowid */
static c_orm_error_t postgres_get_last_insert_rowid(c_orm_db_t *db,
                                                    int64_t *out_id) {
  int rc;

  LOG_DEBUG("postgres_get_last_insert_rowid: entry");
  (void)db;
  if (out_id) {
    *out_id = 0;
  }

  LOG_DEBUG("postgres_get_last_insert_rowid: exit");
  rc = C_ORM_ERROR_NOT_IMPLEMENTED;
  return (c_orm_error_t)rc;
}

/** @brief Postgres vtable */
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

/** @brief Get Postgres vtable */
C_ORM_EXPORT int
c_orm_postgres_get_vtable(const c_orm_driver_vtable_t **out_vtable) {
  int rc;

  LOG_DEBUG("c_orm_postgres_get_vtable: entry");
  if (!out_vtable) {
    LOG_DEBUG("c_orm_postgres_get_vtable: invalid args");
    rc = 1;
    return rc;
  }
  *out_vtable = &postgres_vtable;

  LOG_DEBUG("c_orm_postgres_get_vtable: exit");
  rc = 0;
  return rc;
}

/** @brief Postgres connect exported */
C_ORM_EXPORT c_orm_error_t c_orm_postgres_connect(const char *url,
                                                  c_orm_db_t **out_db) {
  int rc;
  LOG_DEBUG("c_orm_postgres_connect: entry");
  rc = postgres_connect(url, out_db);
  LOG_DEBUG("c_orm_postgres_connect: exit");
  return (c_orm_error_t)rc;
}

/** @brief Postgres large object create */
C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_create(c_orm_db_t *db,
                                                    unsigned int *out_oid) {
  int rc;
  struct postgres_db_data *data;
  Oid oid;

  LOG_DEBUG("c_orm_postgres_lo_create: entry");
  if (!db || !db->driver_data || !out_oid) {
    LOG_DEBUG("c_orm_postgres_lo_create: invalid args");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  data = (struct postgres_db_data *)db->driver_data;
  oid = lo_creat(data->conn, INV_READ | INV_WRITE);
  if (oid == InvalidOid) {
    LOG_DEBUG("c_orm_postgres_lo_create: lo_creat failed");
    rc = C_ORM_ERROR_UNKNOWN;
    return (c_orm_error_t)rc;
  }
  *out_oid = (unsigned int)oid;

  LOG_DEBUG("c_orm_postgres_lo_create: exit");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/** @brief Postgres large object open */
C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_open(c_orm_db_t *db,
                                                  unsigned int oid, int mode,
                                                  void **out_fd) {
  int rc;
  struct postgres_db_data *data;
  int fd;

  LOG_DEBUG("c_orm_postgres_lo_open: entry");
  if (!db || !db->driver_data || !out_fd) {
    LOG_DEBUG("c_orm_postgres_lo_open: invalid args");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  data = (struct postgres_db_data *)db->driver_data;
  fd = lo_open(data->conn, (Oid)oid, mode);
  if (fd < 0) {
    LOG_DEBUG("c_orm_postgres_lo_open: lo_open failed");
    rc = C_ORM_ERROR_UNKNOWN;
    return (c_orm_error_t)rc;
  }
  /* Cast int to void* for abstraction */
  *out_fd = (void *)(intptr_t)fd;

  LOG_DEBUG("c_orm_postgres_lo_open: exit");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/** @brief Postgres large object read */
C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_read(c_orm_db_t *db, void *fd,
                                                  void *buffer, size_t len,
                                                  size_t *out_read) {
  int rc;
  struct postgres_db_data *data;
  int bytes_read;

  LOG_DEBUG("c_orm_postgres_lo_read: entry");
  if (!db || !db->driver_data || !buffer || !out_read) {
    LOG_DEBUG("c_orm_postgres_lo_read: invalid args");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  data = (struct postgres_db_data *)db->driver_data;
  bytes_read = lo_read(data->conn, (int)(intptr_t)fd, (char *)buffer, len);
  if (bytes_read < 0) {
    LOG_DEBUG("c_orm_postgres_lo_read: lo_read failed");
    rc = C_ORM_ERROR_UNKNOWN;
    return (c_orm_error_t)rc;
  }
  *out_read = (size_t)bytes_read;

  LOG_DEBUG("c_orm_postgres_lo_read: exit");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/** @brief Postgres large object write */
C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_write(c_orm_db_t *db, void *fd,
                                                   const void *buffer,
                                                   size_t len,
                                                   size_t *out_written) {
  int rc;
  struct postgres_db_data *data;
  int bytes_written;

  LOG_DEBUG("c_orm_postgres_lo_write: entry");
  if (!db || !db->driver_data || !buffer || !out_written) {
    LOG_DEBUG("c_orm_postgres_lo_write: invalid args");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  data = (struct postgres_db_data *)db->driver_data;
  bytes_written =
      lo_write(data->conn, (int)(intptr_t)fd, (const char *)buffer, len);
  if (bytes_written < 0) {
    LOG_DEBUG("c_orm_postgres_lo_write: lo_write failed");
    rc = C_ORM_ERROR_UNKNOWN;
    return (c_orm_error_t)rc;
  }
  *out_written = (size_t)bytes_written;

  LOG_DEBUG("c_orm_postgres_lo_write: exit");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

/** @brief Postgres large object close */
C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_close(c_orm_db_t *db, void *fd) {
  int rc;
  struct postgres_db_data *data;

  LOG_DEBUG("c_orm_postgres_lo_close: entry");
  if (!db || !db->driver_data) {
    LOG_DEBUG("c_orm_postgres_lo_close: invalid args");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  data = (struct postgres_db_data *)db->driver_data;
  rc = lo_close(data->conn, (int)(intptr_t)fd);
  if (rc < 0) {
    LOG_DEBUG("c_orm_postgres_lo_close: lo_close failed");
    rc = C_ORM_ERROR_UNKNOWN;
    return (c_orm_error_t)rc;
  }

  LOG_DEBUG("c_orm_postgres_lo_close: exit");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
}

#else

/** @brief Get Postgres vtable stub */
C_ORM_EXPORT int
c_orm_postgres_get_vtable(const c_orm_driver_vtable_t **out_vtable) {
  int rc;
  LOG_DEBUG("c_orm_postgres_get_vtable: entry");
  if (out_vtable) {
    *out_vtable = NULL;
  }
  LOG_DEBUG("c_orm_postgres_get_vtable: exit");
  rc = 1;
  return rc;
}

/** @brief Postgres connect stub */
C_ORM_EXPORT c_orm_error_t c_orm_postgres_connect(const char *url,
                                                  c_orm_db_t **out_db) {
  int rc;
  LOG_DEBUG("c_orm_postgres_connect: entry");
  (void)url;
  (void)out_db;
  LOG_DEBUG("c_orm_postgres_connect: exit");
  rc = C_ORM_ERROR_NOT_IMPLEMENTED;
  return (c_orm_error_t)rc;
}

/** @brief Postgres large object create stub */
C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_create(c_orm_db_t *db,
                                                    unsigned int *out_oid) {
  int rc;
  LOG_DEBUG("c_orm_postgres_lo_create: entry");
  (void)db;
  (void)out_oid;
  LOG_DEBUG("c_orm_postgres_lo_create: exit");
  rc = C_ORM_ERROR_NOT_IMPLEMENTED;
  return (c_orm_error_t)rc;
}

/** @brief Postgres large object open stub */
C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_open(c_orm_db_t *db,
                                                  unsigned int oid, int mode,
                                                  void **out_fd) {
  int rc;
  LOG_DEBUG("c_orm_postgres_lo_open: entry");
  (void)db;
  (void)oid;
  (void)mode;
  (void)out_fd;
  LOG_DEBUG("c_orm_postgres_lo_open: exit");
  rc = C_ORM_ERROR_NOT_IMPLEMENTED;
  return (c_orm_error_t)rc;
}

/** @brief Postgres large object read stub */
C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_read(c_orm_db_t *db, void *fd,
                                                  void *buffer, size_t len,
                                                  size_t *out_read) {
  int rc;
  LOG_DEBUG("c_orm_postgres_lo_read: entry");
  (void)db;
  (void)fd;
  (void)buffer;
  (void)len;
  (void)out_read;
  LOG_DEBUG("c_orm_postgres_lo_read: exit");
  rc = C_ORM_ERROR_NOT_IMPLEMENTED;
  return (c_orm_error_t)rc;
}

/** @brief Postgres large object write stub */
C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_write(c_orm_db_t *db, void *fd,
                                                   const void *buffer,
                                                   size_t len,
                                                   size_t *out_written) {
  int rc;
  LOG_DEBUG("c_orm_postgres_lo_write: entry");
  (void)db;
  (void)fd;
  (void)buffer;
  (void)len;
  (void)out_written;
  LOG_DEBUG("c_orm_postgres_lo_write: exit");
  rc = C_ORM_ERROR_NOT_IMPLEMENTED;
  return (c_orm_error_t)rc;
}

/** @brief Postgres large object close stub */
C_ORM_EXPORT c_orm_error_t c_orm_postgres_lo_close(c_orm_db_t *db, void *fd) {
  int rc;
  LOG_DEBUG("c_orm_postgres_lo_close: entry");
  (void)db;
  (void)fd;
  LOG_DEBUG("c_orm_postgres_lo_close: exit");
  rc = C_ORM_ERROR_NOT_IMPLEMENTED;
  return (c_orm_error_t)rc;
}

#endif