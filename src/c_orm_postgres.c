/**
 * @file c_orm_postgres.c
 * @brief PostgreSQL driver implementation for c-orm.
 */

/* clang-format off */
#include "c_orm_postgres.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifdef C_ORM_ENABLE_POSTGRESQL
#include <libpq-fe.h>
#endif
/* clang-format on */

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
  static int counter = 0;
#if defined(_MSC_VER)
  return sprintf_s(buf, size, "c_orm_stmt_%d", ++counter);
#else
  return sprintf(buf, "c_orm_stmt_%d", ++counter);
#endif
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
      num_len = sprintf(num_buf, "$%d", count);
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
  c_orm_db_t *db;
  struct postgres_db_data *data;

  if (!url || !out_db)
    return C_ORM_ERROR_MEMORY;

  db = (c_orm_db_t *)calloc(1, sizeof(c_orm_db_t));
  if (!db)
    return C_ORM_ERROR_MEMORY;

  data = (struct postgres_db_data *)calloc(1, sizeof(struct postgres_db_data));
  if (!data) {
    free(db);
    return C_ORM_ERROR_MEMORY;
  }

  data->conn = PQconnectdb(url);
  if (PQstatus(data->conn) != CONNECTION_OK) {
    set_error(db, PQerrorMessage(data->conn));
    PQfinish(data->conn);
    free(data);
    free(db);
    return C_ORM_ERROR_CONNECTION;
  }

  if (c_orm_postgres_get_vtable(&db->vtable) != 0) {
    PQfinish(data->conn);
    free(data);
    free(db);
    return C_ORM_ERROR_UNKNOWN;
  }
  db->driver_data = data;
  *out_db = db;

  return C_ORM_OK;
}

static c_orm_error_t postgres_disconnect(c_orm_db_t *db) {
  struct postgres_db_data *data;
  if (!db)
    return C_ORM_OK;

  data = (struct postgres_db_data *)db->driver_data;
  if (data) {
    if (data->conn) {
      PQfinish(data->conn);
    }
    free(data);
  }
  free(db);
  return C_ORM_OK;
}

struct c_orm_query {
  struct postgres_query_data *data;
};

static c_orm_error_t postgres_prepare(c_orm_db_t *db, const char *sql,
                                      c_orm_query_t **out_query) {
  struct postgres_db_data *db_data;
  struct postgres_query_data *q_data;
  c_orm_query_t *query;
  char *new_sql;
  int param_count = 0;
  char stmt_name[64];
  PGresult *res;

  if (!db || !sql || !out_query)
    return C_ORM_ERROR_MEMORY;
  db_data = (struct postgres_db_data *)db->driver_data;

  if (db->log_cb) {
    db->log_cb(sql, db->log_user_data);
  }

  new_sql = rewrite_query(sql, &param_count);
  if (!new_sql)
    return C_ORM_ERROR_MEMORY;

  query = (c_orm_query_t *)calloc(1, sizeof(c_orm_query_t));
  if (!query) {
    free(new_sql);
    return C_ORM_ERROR_MEMORY;
  }

  q_data = (struct postgres_query_data *)calloc(
      1, sizeof(struct postgres_query_data));
  if (!q_data) {
    free(query);
    free(new_sql);
    return C_ORM_ERROR_MEMORY;
  }

  generate_stmt_name(stmt_name, sizeof(stmt_name));

  res = PQprepare(db_data->conn, stmt_name, new_sql, param_count, NULL);
  if (PQresultStatus(res) != PGRES_COMMAND_OK) {
    set_error(db, PQerrorMessage(db_data->conn));
    PQclear(res);
    free(q_data);
    free(query);
    free(new_sql);
    return C_ORM_ERROR_SQL;
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

  return C_ORM_OK;
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
  char buf[32];
  if (!query || !query->data || index < 1 || index > query->data->param_count)
    return C_ORM_ERROR_BIND;
  free_param(query->data, index);
#if defined(_MSC_VER)
  sprintf_s(buf, sizeof(buf), "%d", (int)val);
#else
  sprintf(buf, "%d", (int)val);
#endif
  query->data->param_values[index - 1] = orm_strdup(buf);
  return C_ORM_OK;
}

static c_orm_error_t postgres_bind_int64(c_orm_query_t *query, int index,
                                         int64_t val) {
  char buf[64];
  if (!query || !query->data || index < 1 || index > query->data->param_count)
    return C_ORM_ERROR_BIND;
  free_param(query->data, index);
#if defined(_MSC_VER)
  sprintf_s(buf, sizeof(buf), "%I64d", (long long)val);
#else
  sprintf(buf, "%lld", (long long)val);
#endif
  query->data->param_values[index - 1] = orm_strdup(buf);
  return C_ORM_OK;
}

static c_orm_error_t postgres_bind_double(c_orm_query_t *query, int index,
                                          double val) {
  char buf[64];
  if (!query || !query->data || index < 1 || index > query->data->param_count)
    return C_ORM_ERROR_BIND;
  free_param(query->data, index);
#if defined(_MSC_VER)
  sprintf_s(buf, sizeof(buf), "%.17g", val);
#else
  sprintf(buf, "%.17g", val);
#endif
  query->data->param_values[index - 1] = orm_strdup(buf);
  return C_ORM_OK;
}

static c_orm_error_t postgres_bind_string(c_orm_query_t *query, int index,
                                          const char *val) {
  if (!query || !query->data || index < 1 || index > query->data->param_count)
    return C_ORM_ERROR_BIND;
  free_param(query->data, index);
  query->data->param_values[index - 1] = orm_strdup(val);
  return C_ORM_OK;
}

static c_orm_error_t postgres_bind_blob(c_orm_query_t *query, int index,
                                        const void *val, size_t size) {
  if (!query || !query->data || index < 1 || index > query->data->param_count)
    return C_ORM_ERROR_BIND;
  free_param(query->data, index);
  query->data->param_values[index - 1] = malloc(size);
  if (query->data->param_values[index - 1]) {
    memcpy(query->data->param_values[index - 1], val, size);
  }
  query->data->param_lengths[index - 1] = (int)size;
  query->data->param_formats[index - 1] = 1; /* binary format */
  return C_ORM_OK;
}

static c_orm_error_t postgres_bind_null(c_orm_query_t *query, int index) {
  if (!query || !query->data || index < 1 || index > query->data->param_count)
    return C_ORM_ERROR_BIND;
  free_param(query->data, index);
  return C_ORM_OK;
}

static c_orm_error_t postgres_step(c_orm_query_t *query, int *out_has_row) {
  struct postgres_query_data *q_data;
  struct postgres_db_data *db_data;

  if (!query || !query->data || !out_has_row)
    return C_ORM_ERROR_STEP;
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
      return C_ORM_ERROR_STEP;
    }

    q_data->row_count = PQntuples(q_data->res);
    q_data->current_row = 0;
  } else {
    q_data->current_row++;
  }

  if (q_data->current_row < q_data->row_count) {
    *out_has_row = 1;
    return C_ORM_OK;
  } else {
    *out_has_row = 0;
    return C_ORM_OK;
  }
}

static c_orm_error_t postgres_get_int32(c_orm_query_t *query, int index,
                                        int32_t *out_val) {
  const char *val;
  if (!query || !query->data || !query->data->res || !out_val)
    return C_ORM_ERROR_MEMORY;
  val = PQgetvalue(query->data->res, query->data->current_row, index);
  *out_val = (int32_t)atoi(val);
  return C_ORM_OK;
}

static c_orm_error_t postgres_get_int64(c_orm_query_t *query, int index,
                                        int64_t *out_val) {
  const char *val;
  if (!query || !query->data || !query->data->res || !out_val)
    return C_ORM_ERROR_MEMORY;
  val = PQgetvalue(query->data->res, query->data->current_row, index);
#if defined(_MSC_VER)
  *out_val = (int64_t)_atoi64(val);
#else
  *out_val = (int64_t)atoll(val);
#endif
  return C_ORM_OK;
}

static c_orm_error_t postgres_get_double(c_orm_query_t *query, int index,
                                         double *out_val) {
  const char *val;
  if (!query || !query->data || !query->data->res || !out_val)
    return C_ORM_ERROR_MEMORY;
  val = PQgetvalue(query->data->res, query->data->current_row, index);
  *out_val = atof(val);
  return C_ORM_OK;
}

static c_orm_error_t postgres_get_string(c_orm_query_t *query, int index,
                                         const char **out_val) {
  if (!query || !query->data || !query->data->res || !out_val)
    return C_ORM_ERROR_MEMORY;
  *out_val = PQgetvalue(query->data->res, query->data->current_row, index);
  return C_ORM_OK;
}

static c_orm_error_t postgres_get_blob(c_orm_query_t *query, int index,
                                       const void **out_val, size_t *out_size) {
  if (!query || !query->data || !query->data->res || !out_val || !out_size)
    return C_ORM_ERROR_MEMORY;
  *out_val = PQgetvalue(query->data->res, query->data->current_row, index);
  *out_size = PQgetlength(query->data->res, query->data->current_row, index);
  return C_ORM_OK;
}

static c_orm_error_t postgres_is_null(c_orm_query_t *query, int index,
                                      int *out_is_null) {
  if (!query || !query->data || !query->data->res || !out_is_null)
    return C_ORM_ERROR_MEMORY;
  *out_is_null =
      PQgetisnull(query->data->res, query->data->current_row, index) ? 1 : 0;
  return C_ORM_OK;
}

static c_orm_error_t postgres_finalize(c_orm_query_t *query) {
  int i;
  if (!query)
    return C_ORM_OK;
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
  return C_ORM_OK;
}

static c_orm_error_t postgres_reset(c_orm_query_t *query) {
  int i;
  if (!query || !query->data)
    return C_ORM_ERROR_MEMORY;
  if (query->data->res) {
    PQclear(query->data->res);
    query->data->res = NULL;
  }
  for (i = 0; i < query->data->param_count; i++) {
    free_param(query->data, i + 1);
  }
  query->data->current_row = 0;
  query->data->row_count = 0;
  return C_ORM_OK;
}

static int postgres_get_last_error(c_orm_db_t *db, const char **out_message) {
  struct postgres_db_data *data;
  if (!out_message)
    return 1;
  if (!db || !db->driver_data) {
    *out_message = "Invalid DB object";
    return 1;
  }
  data = (struct postgres_db_data *)db->driver_data;
  *out_message = data->last_error;
  return 0;
}

static const c_orm_driver_vtable_t postgres_vtable = {
    postgres_connect,       postgres_disconnect, postgres_prepare,
    postgres_bind_int32,    postgres_bind_int64, postgres_bind_double,
    postgres_bind_string,   postgres_bind_blob,  postgres_bind_null,
    postgres_step,          postgres_get_int32,  postgres_get_int64,
    postgres_get_double,    postgres_get_string, postgres_get_blob,
    postgres_is_null,       postgres_finalize,   postgres_reset,
    postgres_get_last_error};

C_ORM_EXPORT int
c_orm_postgres_get_vtable(const c_orm_driver_vtable_t **out_vtable) {
  if (!out_vtable)
    return 1;
  *out_vtable = &postgres_vtable;
  return 0;
}

C_ORM_EXPORT c_orm_error_t c_orm_postgres_connect(const char *url,
                                                  c_orm_db_t **out_db) {
  return postgres_connect(url, out_db);
}

#else

/* Stub out if PostgreSQL is not enabled */
C_ORM_EXPORT int
c_orm_postgres_get_vtable(const c_orm_driver_vtable_t **out_vtable) {
  if (out_vtable)
    *out_vtable = NULL;
  return 1;
}

C_ORM_EXPORT c_orm_error_t c_orm_postgres_connect(const char *url,
                                                  c_orm_db_t **out_db) {
  (void)url;
  (void)out_db;
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}

#endif
