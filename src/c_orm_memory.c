

/* clang-format off */
#include "c_orm_memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

typedef enum {
  MEM_QUERY_SELECT_ALL,
  MEM_QUERY_SELECT_PK,
  MEM_QUERY_INSERT,
  MEM_QUERY_UPDATE,
  MEM_QUERY_DELETE,
  MEM_QUERY_RAW
} mem_query_type_t;

typedef struct mem_row {
  void **columns;
  size_t num_cols;
  struct mem_row *next;
} mem_row_t;

typedef struct mem_table {
  char *name;
  mem_row_t *head;
  struct mem_table *next;
} mem_table_t;

typedef struct {
  mem_table_t *tables;
  char last_error[256];
} c_orm_memory_db_t;

typedef struct {
  mem_query_type_t type;
  char table_name[64];
  void **bound_params;
  size_t num_bound;
  mem_row_t *current_row; /* For iteration */
  c_orm_memory_db_t *db;
} c_orm_memory_query_t;

static c_orm_error_t mem_connect(const char *url, c_orm_db_t **out_db) {
  c_orm_memory_db_t *ctx;
  c_orm_db_t *db;
  const c_orm_driver_vtable_t *vt;

  (void)url; /* in-memory ignores url */

  if (!out_db)
    return C_ORM_ERROR_MEMORY;

  ctx = (c_orm_memory_db_t *)malloc(sizeof(c_orm_memory_db_t));
  if (!ctx)
    return C_ORM_ERROR_MEMORY;
  ctx->tables = NULL;
  ctx->last_error[0] = '\0';

  db = (c_orm_db_t *)malloc(sizeof(c_orm_db_t));
  if (!db) {
    free(ctx);
    return C_ORM_ERROR_MEMORY;
  }

  c_orm_memory_get_vtable(&vt);
  db->vtable = vt;
  db->driver_data = ctx;
  db->log_cb = NULL;
  db->log_user_data = NULL;
  db->expire_cb = NULL;
  db->expire_user_data = NULL;

  *out_db = db;
  return C_ORM_OK;
}

static c_orm_error_t mem_disconnect(c_orm_db_t *db) {
  if (db && db->driver_data) {
    c_orm_memory_db_t *ctx = (c_orm_memory_db_t *)db->driver_data;
    mem_table_t *t = ctx->tables;
    while (t) {
      mem_table_t *nt = t->next;
      mem_row_t *r = t->head;
      while (r) {
        mem_row_t *nr = r->next;
        if (r->columns)
          free(r->columns);
        free(r);
        r = nr;
      }
      free(t->name);
      free(t);
      t = nt;
    }
    free(ctx);
    free(db);
  }
  return C_ORM_OK;
}

static void parse_table_name(const char *sql, const char *prefix, char *out) {
  const char *p = strstr(sql, prefix);
  if (p) {
    p += strlen(prefix);
    while (*p == ' ')
      p++;
    while (*p && *p != ' ' && *p != '(') {
      *out++ = *p++;
    }
  }
  *out = '\0';
}

static c_orm_error_t mem_prepare(c_orm_db_t *db, const char *sql,
                                 c_orm_query_t **out_query) {
  c_orm_memory_query_t *q;
  c_orm_memory_db_t *ctx;
  if (!db || !sql || !out_query)
    return C_ORM_ERROR_MEMORY;

  ctx = (c_orm_memory_db_t *)db->driver_data;
  q = (c_orm_memory_query_t *)malloc(sizeof(c_orm_memory_query_t));
  if (!q)
    return C_ORM_ERROR_MEMORY;

  q->bound_params = (void **)calloc(
      32, sizeof(void *)); /* Max 32 cols for simple memory driver */
  q->num_bound = 0;
  q->current_row = NULL;
  q->db = ctx;
  q->table_name[0] = '\0';

  if (strncmp(sql, "SELECT * FROM ", 14) == 0) {
    if (strstr(sql, "WHERE")) {
      q->type = MEM_QUERY_SELECT_PK;
    } else {
      q->type = MEM_QUERY_SELECT_ALL;
    }
    parse_table_name(sql, "FROM ", q->table_name);
  } else if (strncmp(sql, "INSERT INTO ", 12) == 0) {
    q->type = MEM_QUERY_INSERT;
    parse_table_name(sql, "INTO ", q->table_name);
  } else if (strncmp(sql, "UPDATE ", 7) == 0) {
    q->type = MEM_QUERY_UPDATE;
    parse_table_name(sql, "UPDATE ", q->table_name);
  } else if (strncmp(sql, "DELETE FROM ", 12) == 0) {
    q->type = MEM_QUERY_DELETE;
    parse_table_name(sql, "FROM ", q->table_name);
  } else {
    q->type = MEM_QUERY_RAW;
  }

  *out_query = (c_orm_query_t *)q;
  return C_ORM_OK;
}

static c_orm_error_t mem_bind_int32(c_orm_query_t *query, int index,
                                    int32_t val) {
  (void)query;
  (void)index;
  (void)val;
  /* Not fully implementing memory state filtering as it's an ephemeral stub */
  return C_ORM_OK;
}
static c_orm_error_t mem_bind_int64(c_orm_query_t *query, int index,
                                    int64_t val) {
  (void)query;
  (void)index;
  (void)val;
  return C_ORM_OK;
}
static c_orm_error_t mem_bind_double(c_orm_query_t *query, int index,
                                     double val) {
  (void)query;
  (void)index;
  (void)val;
  return C_ORM_OK;
}
static c_orm_error_t mem_bind_string(c_orm_query_t *query, int index,
                                     const char *val) {
  (void)query;
  (void)index;
  (void)val;
  return C_ORM_OK;
}
static c_orm_error_t mem_bind_blob(c_orm_query_t *query, int index,
                                   const void *val, size_t size) {
  (void)query;
  (void)index;
  (void)val;
  (void)size;
  return C_ORM_OK;
}
static c_orm_error_t mem_bind_null(c_orm_query_t *query, int index) {
  (void)query;
  (void)index;
  return C_ORM_OK;
}

static c_orm_error_t mem_step(c_orm_query_t *query, int *out_has_row) {
  if (!query || !out_has_row)
    return C_ORM_ERROR_MEMORY;
  *out_has_row = 0;
  return C_ORM_OK;
}

static c_orm_error_t mem_get_int32(c_orm_query_t *query, int index,
                                   int32_t *out_val) {
  (void)query;
  (void)index;
  (void)out_val;
  return C_ORM_ERROR_NOT_FOUND;
}
static c_orm_error_t mem_get_int64(c_orm_query_t *query, int index,
                                   int64_t *out_val) {
  (void)query;
  (void)index;
  (void)out_val;
  return C_ORM_ERROR_NOT_FOUND;
}
static c_orm_error_t mem_get_double(c_orm_query_t *query, int index,
                                    double *out_val) {
  (void)query;
  (void)index;
  (void)out_val;
  return C_ORM_ERROR_NOT_FOUND;
}
static c_orm_error_t mem_get_string(c_orm_query_t *query, int index,
                                    const char **out_val) {
  (void)query;
  (void)index;
  (void)out_val;
  return C_ORM_ERROR_NOT_FOUND;
}
static c_orm_error_t mem_get_blob(c_orm_query_t *query, int index,
                                  const void **out_val, size_t *out_size) {
  (void)query;
  (void)index;
  (void)out_val;
  (void)out_size;
  return C_ORM_ERROR_NOT_FOUND;
}
static c_orm_error_t mem_is_null(c_orm_query_t *query, int index,
                                 int *out_is_null) {
  (void)query;
  (void)index;
  if (out_is_null)
    *out_is_null = 1;
  return C_ORM_OK;
}

static c_orm_error_t mem_finalize(c_orm_query_t *query) {
  c_orm_memory_query_t *q = (c_orm_memory_query_t *)query;
  if (q) {
    if (q->bound_params)
      free(q->bound_params);
    free(q);
  }
  return C_ORM_OK;
}
static c_orm_error_t mem_reset(c_orm_query_t *query) {
  (void)query;
  return C_ORM_OK;
}
static int mem_get_last_error(c_orm_db_t *db, const char **out_message) {
  c_orm_memory_db_t *ctx = (c_orm_memory_db_t *)db->driver_data;
  if (out_message)
    *out_message = ctx->last_error;
  return 1;
}

static c_orm_error_t mem_get_last_insert_rowid(c_orm_db_t *db,
                                               int64_t *out_id) {
  (void)db;
  if (out_id)
    *out_id = 0;
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}

static c_orm_error_t mem_get_column_count(c_orm_query_t *query,
                                          int *out_count) {
  (void)query;
  if (out_count)
    *out_count = 0;
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}

static c_orm_error_t mem_get_column_name(c_orm_query_t *query, int index,
                                         const char **out_name) {
  (void)query;
  (void)index;
  if (out_name)
    *out_name = NULL;
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}

static const c_orm_driver_vtable_t memory_vtable = {mem_connect,
                                                    mem_disconnect,
                                                    mem_prepare,
                                                    mem_bind_int32,
                                                    mem_bind_int64,
                                                    mem_bind_double,
                                                    mem_bind_string,
                                                    mem_bind_blob,
                                                    mem_bind_null,
                                                    mem_step,
                                                    mem_get_int32,
                                                    mem_get_int64,
                                                    mem_get_double,
                                                    mem_get_string,
                                                    mem_get_blob,
                                                    mem_is_null,
                                                    mem_finalize,
                                                    mem_reset,
                                                    mem_get_last_error,
                                                    NULL,
                                                    mem_get_last_insert_rowid,
                                                    mem_get_column_count,
                                                    mem_get_column_name};

C_ORM_EXPORT int
c_orm_memory_get_vtable(const c_orm_driver_vtable_t **out_vtable) {
  if (out_vtable) {
    *out_vtable = &memory_vtable;
    return 0;
  }
  return 1;
}