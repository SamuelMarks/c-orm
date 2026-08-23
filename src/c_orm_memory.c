#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file c_orm_memory.c
 * @brief Memory driver implementation for testing and simple in-memory
 * operations.
 */

/* clang-format off */
#include "c_orm_memory.h"
#include "c_orm_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

/** @brief Query type for memory */
typedef enum {
  MEM_QUERY_SELECT_ALL,
  MEM_QUERY_SELECT_PK,
  MEM_QUERY_INSERT,
  MEM_QUERY_UPDATE,
  MEM_QUERY_DELETE,
  MEM_QUERY_RAW
} mem_query_type_t;

/** @brief Row structure */
typedef struct mem_row {
  void **columns;
  size_t num_cols;
  struct mem_row *next;
} mem_row_t;

/** @brief Table structure */
typedef struct mem_table {
  char *name;
  mem_row_t *head;
  struct mem_table *next;
} mem_table_t;

/** @brief DB structure */
typedef struct {
  mem_table_t *tables;
  char last_error[256];
} c_orm_memory_db_t;

/** @brief Query context */
typedef struct {
  mem_query_type_t type;
  char table_name[64];
  void **bound_params;
  size_t num_bound;
  mem_row_t *current_row; /* For iteration */
  c_orm_memory_db_t *db;
} c_orm_memory_query_t;

/**
 * @brief Connect
 */
static c_orm_error_t mem_connect(const char *url, c_orm_db_t **out_db) {
  c_orm_error_t rc;
  c_orm_memory_db_t *ctx;
  c_orm_db_t *db;
  const c_orm_driver_vtable_t *vt;

  LOG_DEBUG("mem_connect: entry");

  (void)url; /* in-memory ignores url */

  if (!out_db) {
    LOG_DEBUG("mem_connect: validation error");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("mem_connect: exit");
    return (c_orm_error_t)rc;
  }

  ctx = (c_orm_memory_db_t *)C_ORM_MALLOC(sizeof(c_orm_memory_db_t));
  if (!ctx) {
    LOG_DEBUG("mem_connect: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("mem_connect: exit");
    return (c_orm_error_t)rc;
  }
  ctx->tables = NULL;
  ctx->last_error[0] = '\0';

  db = (c_orm_db_t *)C_ORM_MALLOC(sizeof(c_orm_db_t));
  if (!db) {
    LOG_DEBUG("mem_connect: OOM");
    C_ORM_FREE(ctx);
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("mem_connect: exit");
    return (c_orm_error_t)rc;
  }

  c_orm_memory_get_vtable(&vt);
  db->vtable = vt;
  db->driver_data = ctx;
  db->driver_name = "memory";
  db->log_cb = NULL;
  db->log_user_data = NULL;
  db->expire_cb = NULL;
  db->expire_user_data = NULL;

  *out_db = db;
  rc = C_ORM_OK;
  LOG_DEBUG("mem_connect: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Disconnect
 */
static c_orm_error_t mem_disconnect(c_orm_db_t *db) {
  c_orm_error_t rc;
  c_orm_memory_db_t *ctx;
  mem_table_t *t;
  LOG_DEBUG("mem_disconnect: entry");

  ctx = (c_orm_memory_db_t *)db->driver_data;
  t = ctx->tables;
  while (t) {
    mem_table_t *nt = t->next;
    mem_row_t *r = t->head;
    while (r) {
      mem_row_t *nr = r->next;
      C_ORM_FREE(r->columns);
      C_ORM_FREE(r);
      r = nr;
    }
    C_ORM_FREE(t->name);
    C_ORM_FREE(t);
    t = nt;
  }
  C_ORM_FREE(ctx);
  C_ORM_FREE(db);

  rc = C_ORM_OK;
  LOG_DEBUG("mem_disconnect: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Parse table name
 */
static void parse_table_name(const char *sql, const char *prefix, char *out) {
  const char *p;
  LOG_DEBUG("parse_table_name: entry");
  p = strstr(sql, prefix);
  if (p) {
    p += strlen(prefix);
    while (*p == ' ') {
      p++;
    }
    while (*p && *p != ' ' && *p != '(') {
      *out++ = *p++;
    }
  }
  *out = '\0';
  LOG_DEBUG("parse_table_name: exit");
}

/**
 * @brief Prepare
 */
static c_orm_error_t mem_prepare(c_orm_db_t *db, const char *sql,
                                 c_orm_query_t **out_query) {
  c_orm_error_t rc;
  c_orm_memory_query_t *q;
  c_orm_memory_db_t *ctx;

  LOG_DEBUG("mem_prepare: entry");

  ctx = (c_orm_memory_db_t *)db->driver_data;
  q = (c_orm_memory_query_t *)C_ORM_MALLOC(sizeof(c_orm_memory_query_t));
  if (!q) {
    LOG_DEBUG("mem_prepare: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("mem_prepare: exit");
    return (c_orm_error_t)rc;
  }

  q->bound_params = (void **)C_ORM_MALLOC(32 * sizeof(void *));
  if (!q->bound_params) {
    LOG_DEBUG("mem_prepare: OOM");
    C_ORM_FREE(q);
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("mem_prepare: exit");
    return (c_orm_error_t)rc;
  }
  memset(q->bound_params, 0, 32 * sizeof(void *));

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
  rc = C_ORM_OK;
  LOG_DEBUG("mem_prepare: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Bind int32
 */
static c_orm_error_t mem_bind_int32(c_orm_query_t *query, int index,
                                    int32_t val) {
  c_orm_error_t rc;
  LOG_DEBUG("mem_bind_int32: entry");
  (void)query;
  (void)index;
  (void)val;
  rc = C_ORM_OK;
  LOG_DEBUG("mem_bind_int32: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Bind int64
 */
static c_orm_error_t mem_bind_int64(c_orm_query_t *query, int index,
                                    int64_t val) {
  c_orm_error_t rc;
  LOG_DEBUG("mem_bind_int64: entry");
  (void)query;
  (void)index;
  (void)val;
  rc = C_ORM_OK;
  LOG_DEBUG("mem_bind_int64: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Bind double
 */
static c_orm_error_t mem_bind_double(c_orm_query_t *query, int index,
                                     double val) {
  c_orm_error_t rc;
  LOG_DEBUG("mem_bind_double: entry");
  (void)query;
  (void)index;
  (void)val;
  rc = C_ORM_OK;
  LOG_DEBUG("mem_bind_double: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Bind string
 */
static c_orm_error_t mem_bind_string(c_orm_query_t *query, int index,
                                     const char *val) {
  c_orm_error_t rc;
  LOG_DEBUG("mem_bind_string: entry");
  (void)query;
  (void)index;
  (void)val;
  rc = C_ORM_OK;
  LOG_DEBUG("mem_bind_string: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Bind blob
 */
static c_orm_error_t mem_bind_blob(c_orm_query_t *query, int index,
                                   const void *val, size_t size) {
  c_orm_error_t rc;
  LOG_DEBUG("mem_bind_blob: entry");
  (void)query;
  (void)index;
  (void)val;
  (void)size;
  rc = C_ORM_OK;
  LOG_DEBUG("mem_bind_blob: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Bind null
 */
static c_orm_error_t mem_bind_null(c_orm_query_t *query, int index) {
  c_orm_error_t rc;
  LOG_DEBUG("mem_bind_null: entry");
  (void)query;
  (void)index;
  rc = C_ORM_OK;
  LOG_DEBUG("mem_bind_null: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Step
 */
static c_orm_error_t mem_step(c_orm_query_t *query, int *out_has_row) {
  c_orm_error_t rc;
  (void)query;
  LOG_DEBUG("mem_step: entry");

  *out_has_row = 0;
  rc = C_ORM_OK;
  LOG_DEBUG("mem_step: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Get int32
 */
static c_orm_error_t mem_get_int32(c_orm_query_t *query, int index,
                                   int32_t *out_val) {
  c_orm_error_t rc;
  LOG_DEBUG("mem_get_int32: entry");
  (void)query;
  (void)index;
  (void)out_val;
  rc = C_ORM_ERROR_NOT_FOUND;
  LOG_DEBUG("mem_get_int32: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Get int64
 */
static c_orm_error_t mem_get_int64(c_orm_query_t *query, int index,
                                   int64_t *out_val) {
  c_orm_error_t rc;
  LOG_DEBUG("mem_get_int64: entry");
  (void)query;
  (void)index;
  (void)out_val;
  rc = C_ORM_ERROR_NOT_FOUND;
  LOG_DEBUG("mem_get_int64: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Get double
 */
static c_orm_error_t mem_get_double(c_orm_query_t *query, int index,
                                    double *out_val) {
  c_orm_error_t rc;
  LOG_DEBUG("mem_get_double: entry");
  (void)query;
  (void)index;
  (void)out_val;
  rc = C_ORM_ERROR_NOT_FOUND;
  LOG_DEBUG("mem_get_double: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Get string
 */
static c_orm_error_t mem_get_string(c_orm_query_t *query, int index,
                                    const char **out_val) {
  c_orm_error_t rc;
  LOG_DEBUG("mem_get_string: entry");
  (void)query;
  (void)index;
  (void)out_val;
  rc = C_ORM_ERROR_NOT_FOUND;
  LOG_DEBUG("mem_get_string: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Get blob
 */
static c_orm_error_t mem_get_blob(c_orm_query_t *query, int index,
                                  const void **out_val, size_t *out_size) {
  c_orm_error_t rc;
  LOG_DEBUG("mem_get_blob: entry");
  (void)query;
  (void)index;
  (void)out_val;
  (void)out_size;
  rc = C_ORM_ERROR_NOT_FOUND;
  LOG_DEBUG("mem_get_blob: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Is null
 */
static c_orm_error_t mem_is_null(c_orm_query_t *query, int index,
                                 int *out_is_null) {
  c_orm_error_t rc;
  LOG_DEBUG("mem_is_null: entry");
  (void)query;
  (void)index;
  if (out_is_null) {
    *out_is_null = 1;
  }
  rc = C_ORM_OK;
  LOG_DEBUG("mem_is_null: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Finalize
 */
static c_orm_error_t mem_finalize(c_orm_query_t *query) {
  c_orm_error_t rc;
  c_orm_memory_query_t *q;
  LOG_DEBUG("mem_finalize: entry");

  q = (c_orm_memory_query_t *)query;
  C_ORM_FREE(q->bound_params);
  C_ORM_FREE(q);
  rc = C_ORM_OK;
  LOG_DEBUG("mem_finalize: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Reset
 */
static c_orm_error_t mem_reset(c_orm_query_t *query) {
  c_orm_error_t rc;
  LOG_DEBUG("mem_reset: entry");
  (void)query;
  rc = C_ORM_OK;
  LOG_DEBUG("mem_reset: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Get last error
 */
static c_orm_error_t mem_get_last_error(c_orm_db_t *db,
                                        const char **out_message) {
  c_orm_error_t rc;
  c_orm_memory_db_t *ctx;
  LOG_DEBUG("mem_get_last_error: entry");
  ctx = (c_orm_memory_db_t *)db->driver_data;
  *out_message = ctx->last_error;
  rc = C_ORM_ERROR_UNKNOWN;
  LOG_DEBUG("mem_get_last_error: exit");
  return rc;
}

/**
 * @brief Get last insert rowid
 */
static c_orm_error_t mem_get_last_insert_rowid(c_orm_db_t *db,
                                               int64_t *out_id) {
  c_orm_error_t rc;
  LOG_DEBUG("mem_get_last_insert_rowid: entry");
  (void)db;
  if (out_id) {
    *out_id = 0;
  }
  rc = C_ORM_ERROR_NOT_IMPLEMENTED;
  LOG_DEBUG("mem_get_last_insert_rowid: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Get column count
 */
static c_orm_error_t mem_get_column_count(c_orm_query_t *query,
                                          int *out_count) {
  c_orm_error_t rc;
  LOG_DEBUG("mem_get_column_count: entry");
  (void)query;
  if (out_count) {
    *out_count = 0;
  }
  rc = C_ORM_ERROR_NOT_IMPLEMENTED;
  LOG_DEBUG("mem_get_column_count: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Get column name
 */
static c_orm_error_t mem_get_column_name(c_orm_query_t *query, int index,
                                         const char **out_name) {
  c_orm_error_t rc;
  LOG_DEBUG("mem_get_column_name: entry");
  (void)query;
  (void)index;
  if (out_name) {
    *out_name = NULL;
  }
  rc = C_ORM_ERROR_NOT_IMPLEMENTED;
  LOG_DEBUG("mem_get_column_name: exit");
  return (c_orm_error_t)rc;
}

/** @brief VTable */
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

/**
 * @brief Get vtable
 */
C_ORM_EXPORT c_orm_error_t
c_orm_memory_get_vtable(const c_orm_driver_vtable_t **out_vtable) {
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_memory_get_vtable: entry");
  if (out_vtable) {
    *out_vtable = &memory_vtable;
    rc = C_ORM_OK;
    LOG_DEBUG("c_orm_memory_get_vtable: exit");
    return rc;
  }
  rc = C_ORM_ERROR_UNKNOWN;
  LOG_DEBUG("c_orm_memory_get_vtable: exit");
  return rc;
}

C_ORM_EXPORT c_orm_error_t c_orm_memory_connect(const char *url,
                                                c_orm_db_t **out_db) {
  return mem_connect(url, out_db);
}

#if defined(__clang__) || defined(__GNUC__)
#endif
