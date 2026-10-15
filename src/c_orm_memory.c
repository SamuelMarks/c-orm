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

/** @brief Value type for in-memory cell */
typedef enum {
  MEM_VAL_NULL = 0,
  MEM_VAL_INT32,
  MEM_VAL_INT64,
  MEM_VAL_DOUBLE,
  MEM_VAL_STRING,
  MEM_VAL_BLOB
} mem_val_type_t;

/**
 * @brief In-memory cell value structure.
 */
typedef struct {
  /** @brief Active cell value type. */
  mem_val_type_t type;
  /** @brief 32-bit integer value. */
  int32_t i32;
  /** @brief 64-bit integer value. */
  int64_t i64;
  /** @brief Double precision value. */
  double d;
  /** @brief String value. */
  char *s;
  /** @brief Blob data buffer. */
  void *blob;
  /** @brief Blob size in bytes. */
  size_t blob_size;
} mem_cell_t;

/**
 * @brief Row structure
 * @var columns Array of column value pointers.
 * @var num_cols Number of columns in this row.
 * @var next Pointer to next row in table.
 */
typedef struct mem_row {
  /** @brief Array of column value pointers. */
  void **columns;
  /** @brief Number of columns in this row. */
  size_t num_cols;
  /** @brief Pointer to next row in table. */
  struct mem_row *next;
} mem_row_t;

/**
 * @brief Table structure
 * @var name Table name string.
 * @var head Pointer to first row in table.
 * @var next Pointer to next table in database.
 */
typedef struct mem_table {
  /** @brief Table name string. */
  char *name;
  /** @brief Pointer to first row in table. */
  mem_row_t *head;
  /** @brief Pointer to next table in database. */
  struct mem_table *next;
} mem_table_t;

/**
 * @brief DB structure
 * @var tables Linked list head of database tables.
 * @var last_error Buffer storing last error message string.
 */
typedef struct {
  /** @brief Linked list head of database tables. */
  mem_table_t *tables;
  /** @brief Buffer storing last error message string. */
  char last_error[256];
} c_orm_memory_db_t;

/**
 * @brief Query context
 * @var type Type of query being executed.
 * @var table_name Name of target table.
 * @var bound_params Array of bound parameter pointers.
 * @var num_bound Number of bound parameters.
 * @var current_row Current row pointer for iteration.
 * @var db Pointer to parent memory database instance.
 * @var step_started Flag indicating whether iteration has started.
 */
typedef struct {
  /** @brief Type of query being executed. */
  mem_query_type_t type;
  /** @brief Name of target table. */
  char table_name[64];
  /** @brief Array of bound parameter pointers. */
  void **bound_params;
  /** @brief Number of bound parameters. */
  size_t num_bound;
  /** @brief Current row pointer for iteration. */
  mem_row_t *current_row; /* For iteration */
  /** @brief Pointer to parent memory database instance. */
  c_orm_memory_db_t *db;
  /** @brief Flag indicating whether iteration has started. */
  int step_started;
} c_orm_memory_query_t;

/**
 * @brief Frees a single mem_cell_t and its owned buffers.
 * @param cell Cell pointer to free.
 */
static void free_mem_cell(mem_cell_t *cell) {
  if (!cell) {
    return;
  }
  C_ORM_FREE(cell->s);
  C_ORM_FREE(cell->blob);
  C_ORM_FREE(cell);
}

/**
 * @brief Helper to retrieve a cell from the current row.
 * @param query Query handle.
 * @param index 0-based column index.
 * @param out_cell Output pointer receiving the cell.
 * @return C_ORM_OK if found, C_ORM_ERROR_NOT_FOUND otherwise.
 */
static c_orm_error_t get_current_cell(c_orm_query_t *query, int index,
                                      mem_cell_t **out_cell) {
  c_orm_memory_query_t *q = (c_orm_memory_query_t *)query;
  if (!q || !q->current_row || (size_t)index >= q->current_row->num_cols ||
      !q->current_row->columns[index]) {
    return C_ORM_ERROR_NOT_FOUND;
  }
  *out_cell = (mem_cell_t *)q->current_row->columns[index];
  return C_ORM_OK;
}

/**
 * @brief Connects to in-memory database instance.
 * @param url Connection string URL (ignored for memory driver).
 * @param out_db Pointer to receive allocated database handle.
 * @return 0 on success, non-zero on error.
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
 * @brief Disconnects and frees an in-memory database instance.
 * @param db Database handle to close.
 * @return 0 on success.
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
      size_t col_i;
      for (col_i = 0; col_i < r->num_cols; col_i++) {
        free_mem_cell((mem_cell_t *)r->columns[col_i]);
      }
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
 * @brief Parses table name from SQL string following a prefix.
 * @param sql Input SQL statement string.
 * @param prefix Keyword prefix preceding table name.
 * @param out Buffer to receive parsed table name string.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t parse_table_name(const char *sql, const char *prefix,
                                      char *out) {
  const char *p;
  LOG_DEBUG("parse_table_name: entry");
  p = strstr(sql, prefix);
  p += strlen(prefix);
  while (*p == ' ') {
    p++;
  }
  while (*p && *p != ' ' && *p != '(') {
    *out++ = *p++;
  }
  *out = '\0';
  LOG_DEBUG("parse_table_name: exit");
  return C_ORM_OK;
}

/**
 * @brief Prepares an in-memory query statement.
 * @param db Database handle.
 * @param sql SQL statement string.
 * @param out_query Pointer to receive allocated query handle.
 * @return 0 on success, non-zero on error.
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
  q->step_started = 0;
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
 * @brief Helper to allocate or replace a bound cell at a 1-based parameter
 * index.
 * @param query Query handle.
 * @param index 1-based parameter index.
 * @param out_cell Output pointer receiving the allocated cell.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t get_or_alloc_bound_cell(c_orm_query_t *query, int index,
                                             mem_cell_t **out_cell) {
  c_orm_memory_query_t *q = (c_orm_memory_query_t *)query;
  mem_cell_t *cell;
  if (!q || index < 1 || index > 32) {
    return C_ORM_ERROR_BIND;
  }
  if (q->bound_params[index - 1]) {
    free_mem_cell((mem_cell_t *)q->bound_params[index - 1]);
    q->bound_params[index - 1] = NULL;
  }
  cell = (mem_cell_t *)C_ORM_MALLOC(sizeof(mem_cell_t));
  if (!cell) {
    return C_ORM_ERROR_MEMORY;
  }
  memset(cell, 0, sizeof(mem_cell_t));
  q->bound_params[index - 1] = cell;
  if ((size_t)index > q->num_bound) {
    q->num_bound = (size_t)index;
  }
  *out_cell = cell;
  return C_ORM_OK;
}

/**
 * @brief Binds a 32-bit integer parameter.
 * @param query Query handle.
 * @param index 1-based parameter index.
 * @param val Value to bind.
 * @return 0 on success.
 */
static c_orm_error_t mem_bind_int32(c_orm_query_t *query, int index,
                                    int32_t val) {
  mem_cell_t *cell = NULL;
  c_orm_error_t rc;
  LOG_DEBUG("mem_bind_int32: entry");
  rc = get_or_alloc_bound_cell(query, index, &cell);
  if (rc == C_ORM_OK) {
    cell->type = MEM_VAL_INT32;
    cell->i32 = val;
    cell->i64 = (int64_t)val;
  }
  LOG_DEBUG("mem_bind_int32: exit");
  return rc;
}

/**
 * @brief Binds a 64-bit integer parameter.
 * @param query Query handle.
 * @param index 1-based parameter index.
 * @param val Value to bind.
 * @return 0 on success.
 */
static c_orm_error_t mem_bind_int64(c_orm_query_t *query, int index,
                                    int64_t val) {
  mem_cell_t *cell = NULL;
  c_orm_error_t rc;
  LOG_DEBUG("mem_bind_int64: entry");
  rc = get_or_alloc_bound_cell(query, index, &cell);
  if (rc == C_ORM_OK) {
    cell->type = MEM_VAL_INT64;
    cell->i64 = val;
    cell->i32 = (int32_t)val;
  }
  LOG_DEBUG("mem_bind_int64: exit");
  return rc;
}

/**
 * @brief Binds a double floating-point parameter.
 * @param query Query handle.
 * @param index 1-based parameter index.
 * @param val Value to bind.
 * @return 0 on success.
 */
static c_orm_error_t mem_bind_double(c_orm_query_t *query, int index,
                                     double val) {
  mem_cell_t *cell = NULL;
  c_orm_error_t rc;
  LOG_DEBUG("mem_bind_double: entry");
  rc = get_or_alloc_bound_cell(query, index, &cell);
  if (rc == C_ORM_OK) {
    cell->type = MEM_VAL_DOUBLE;
    cell->d = val;
  }
  LOG_DEBUG("mem_bind_double: exit");
  return rc;
}

/**
 * @brief Binds a string parameter.
 * @param query Query handle.
 * @param index 1-based parameter index.
 * @param val Value string to bind.
 * @return 0 on success.
 */
static c_orm_error_t mem_bind_string(c_orm_query_t *query, int index,
                                     const char *val) {
  mem_cell_t *cell = NULL;
  c_orm_error_t rc;
  LOG_DEBUG("mem_bind_string: entry");
  rc = get_or_alloc_bound_cell(query, index, &cell);
  if (rc == C_ORM_OK) {
    cell->type = MEM_VAL_STRING;
    if (val) {
      rc = C_ORM_STRDUP(val, &cell->s);
    }
  }
  LOG_DEBUG("mem_bind_string: exit");
  return rc;
}

/**
 * @brief Binds a binary blob parameter.
 * @param query Query handle.
 * @param index 1-based parameter index.
 * @param val Pointer to blob buffer.
 * @param size Size in bytes.
 * @return 0 on success.
 */
static c_orm_error_t mem_bind_blob(c_orm_query_t *query, int index,
                                   const void *val, size_t size) {
  mem_cell_t *cell = NULL;
  c_orm_error_t rc;
  LOG_DEBUG("mem_bind_blob: entry");
  rc = get_or_alloc_bound_cell(query, index, &cell);
  if (rc == C_ORM_OK) {
    cell->type = MEM_VAL_BLOB;
    if (val) {
      cell->blob = C_ORM_MALLOC(size);
      if (!cell->blob) {
        return C_ORM_ERROR_MEMORY;
      }
      memcpy(cell->blob, val, size);
      cell->blob_size = size;
    }
  }
  LOG_DEBUG("mem_bind_blob: exit");
  return rc;
}

/**
 * @brief Binds a NULL parameter.
 * @param query Query handle.
 * @param index 1-based parameter index.
 * @return 0 on success.
 */
static c_orm_error_t mem_bind_null(c_orm_query_t *query, int index) {
  mem_cell_t *cell = NULL;
  c_orm_error_t rc;
  LOG_DEBUG("mem_bind_null: entry");
  rc = get_or_alloc_bound_cell(query, index, &cell);
  if (rc == C_ORM_OK) {
    cell->type = MEM_VAL_NULL;
  }
  LOG_DEBUG("mem_bind_null: exit");
  return rc;
}

/**
 * @brief Steps execution of an in-memory statement.
 * @param query Query handle.
 * @param out_has_row Pointer to receive 1 if row available, 0 if done.
 * @return 0 on success.
 */
static c_orm_error_t mem_step(c_orm_query_t *query, int *out_has_row) {
  c_orm_memory_query_t *q = (c_orm_memory_query_t *)query;
  mem_table_t *t;
  LOG_DEBUG("mem_step: entry");

  *out_has_row = 0;
  if (!q) {
    return C_ORM_OK;
  }

  if (q->type == MEM_QUERY_INSERT) {
    mem_row_t *row;
    size_t i;
    t = q->db->tables;
    while (t && strcmp(t->name, q->table_name) != 0) {
      t = t->next;
    }
    if (!t) {
      t = (mem_table_t *)C_ORM_MALLOC(sizeof(mem_table_t));
      if (!t) {
        return C_ORM_ERROR_MEMORY;
      }
      if (C_ORM_STRDUP(q->table_name, &t->name) != C_ORM_OK) {
        C_ORM_FREE(t);
        return C_ORM_ERROR_MEMORY;
      }
      t->head = NULL;
      t->next = q->db->tables;
      q->db->tables = t;
    }
    row = (mem_row_t *)C_ORM_MALLOC(sizeof(mem_row_t));
    if (!row) {
      return C_ORM_ERROR_MEMORY;
    }
    row->num_cols = q->num_bound;
    row->columns = q->num_bound > 0
                       ? (void **)C_ORM_MALLOC(q->num_bound * sizeof(void *))
                       : NULL;
    if (q->num_bound > 0 && !row->columns) {
      C_ORM_FREE(row);
      return C_ORM_ERROR_MEMORY;
    }
    for (i = 0; i < q->num_bound; i++) {
      row->columns[i] = q->bound_params[i];
      q->bound_params[i] = NULL;
    }
    q->num_bound = 0;
    row->next = t->head;
    t->head = row;
    LOG_DEBUG("mem_step: exit insert");
    return C_ORM_OK;
  }

  if (q->type == MEM_QUERY_SELECT_ALL || q->type == MEM_QUERY_SELECT_PK) {
    if (!q->step_started) {
      q->step_started = 1;
      t = q->db->tables;
      while (t && strcmp(t->name, q->table_name) != 0) {
        t = t->next;
      }
      q->current_row = t ? t->head : NULL;
    } else if (q->current_row) {
      q->current_row = q->current_row->next;
    }
    if (q->current_row) {
      *out_has_row = 1;
    }
  }

  LOG_DEBUG("mem_step: exit");
  return C_ORM_OK;
}

/**
 * @brief Retrieves a 32-bit integer column value.
 * @param query Query handle.
 * @param index 0-based column index.
 * @param out_val Pointer to receive integer value.
 * @return Error code.
 */
static c_orm_error_t mem_get_int32(c_orm_query_t *query, int index,
                                   int32_t *out_val) {
  mem_cell_t *cell = NULL;
  LOG_DEBUG("mem_get_int32: entry");
  if (out_val && get_current_cell(query, index, &cell) == C_ORM_OK) {
    *out_val = cell->i32;
    LOG_DEBUG("mem_get_int32: exit");
    return C_ORM_OK;
  }
  LOG_DEBUG("mem_get_int32: exit not found");
  return C_ORM_ERROR_NOT_FOUND;
}

/**
 * @brief Retrieves a 64-bit integer column value.
 * @param query Query handle.
 * @param index 0-based column index.
 * @param out_val Pointer to receive 64-bit integer value.
 * @return Error code.
 */
static c_orm_error_t mem_get_int64(c_orm_query_t *query, int index,
                                   int64_t *out_val) {
  mem_cell_t *cell = NULL;
  LOG_DEBUG("mem_get_int64: entry");
  if (out_val && get_current_cell(query, index, &cell) == C_ORM_OK) {
    *out_val = cell->i64;
    LOG_DEBUG("mem_get_int64: exit");
    return C_ORM_OK;
  }
  LOG_DEBUG("mem_get_int64: exit not found");
  return C_ORM_ERROR_NOT_FOUND;
}

/**
 * @brief Retrieves a double floating-point column value.
 * @param query Query handle.
 * @param index 0-based column index.
 * @param out_val Pointer to receive double value.
 * @return Error code.
 */
static c_orm_error_t mem_get_double(c_orm_query_t *query, int index,
                                    double *out_val) {
  mem_cell_t *cell = NULL;
  LOG_DEBUG("mem_get_double: entry");
  if (out_val && get_current_cell(query, index, &cell) == C_ORM_OK) {
    *out_val = cell->d;
    LOG_DEBUG("mem_get_double: exit");
    return C_ORM_OK;
  }
  LOG_DEBUG("mem_get_double: exit not found");
  return C_ORM_ERROR_NOT_FOUND;
}

/**
 * @brief Retrieves a string column value.
 * @param query Query handle.
 * @param index 0-based column index.
 * @param out_val Pointer to receive string pointer.
 * @return Error code.
 */
static c_orm_error_t mem_get_string(c_orm_query_t *query, int index,
                                    const char **out_val) {
  mem_cell_t *cell = NULL;
  LOG_DEBUG("mem_get_string: entry");
  if (out_val && get_current_cell(query, index, &cell) == C_ORM_OK) {
    *out_val = cell->s;
    LOG_DEBUG("mem_get_string: exit");
    return C_ORM_OK;
  }
  LOG_DEBUG("mem_get_string: exit not found");
  return C_ORM_ERROR_NOT_FOUND;
}

/**
 * @brief Retrieves a binary blob column value.
 * @param query Query handle.
 * @param index 0-based column index.
 * @param out_val Pointer to receive blob buffer pointer.
 * @param out_size Pointer to receive blob size in bytes.
 * @return Error code.
 */
static c_orm_error_t mem_get_blob(c_orm_query_t *query, int index,
                                  const void **out_val, size_t *out_size) {
  mem_cell_t *cell = NULL;
  LOG_DEBUG("mem_get_blob: entry");
  if (out_val && out_size &&
      get_current_cell(query, index, &cell) == C_ORM_OK) {
    *out_val = cell->blob;
    *out_size = cell->blob_size;
    LOG_DEBUG("mem_get_blob: exit");
    return C_ORM_OK;
  }
  LOG_DEBUG("mem_get_blob: exit not found");
  return C_ORM_ERROR_NOT_FOUND;
}

/**
 * @brief Checks if a column value is NULL.
 * @param query Query handle.
 * @param index 0-based column index.
 * @param out_is_null Pointer to receive 1 if NULL, 0 otherwise.
 * @return 0 on success.
 */
static c_orm_error_t mem_is_null(c_orm_query_t *query, int index,
                                 int *out_is_null) {
  mem_cell_t *cell = NULL;
  LOG_DEBUG("mem_is_null: entry");
  if (out_is_null) {
    if (get_current_cell(query, index, &cell) == C_ORM_OK) {
      *out_is_null = (cell->type == MEM_VAL_NULL) ? 1 : 0;
    } else {
      *out_is_null = 1;
    }
  }
  LOG_DEBUG("mem_is_null: exit");
  return C_ORM_OK;
}

/**
 * @brief Finalizes and frees a query handle.
 * @param query Query handle to destroy.
 * @return 0 on success.
 */
static c_orm_error_t mem_finalize(c_orm_query_t *query) {
  c_orm_memory_query_t *q;
  size_t i;
  LOG_DEBUG("mem_finalize: entry");

  q = (c_orm_memory_query_t *)query;
  for (i = 0; i < 32; i++) {
    if (q->bound_params[i]) {
      free_mem_cell((mem_cell_t *)q->bound_params[i]);
    }
  }
  C_ORM_FREE(q->bound_params);
  C_ORM_FREE(q);
  LOG_DEBUG("mem_finalize: exit");
  return C_ORM_OK;
}

/**
 * @brief Resets a prepared statement.
 * @param query Query handle to reset.
 * @return 0 on success.
 */
static c_orm_error_t mem_reset(c_orm_query_t *query) {
  c_orm_memory_query_t *q = (c_orm_memory_query_t *)query;
  LOG_DEBUG("mem_reset: entry");
  if (q) {
    q->current_row = NULL;
    q->step_started = 0;
  }
  LOG_DEBUG("mem_reset: exit");
  return C_ORM_OK;
}

/**
 * @brief Retrieves the last error message string.
 * @param db Database handle.
 * @param out_message Pointer to receive error message pointer.
 * @return Error code.
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
 * @brief Retrieves the last inserted rowid.
 * @param db Database handle.
 * @param out_id Pointer to receive row ID.
 * @return Error code.
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
 * @brief Retrieves the number of columns in the result set.
 * @param query Query handle.
 * @param out_count Pointer to receive column count.
 * @return Error code.
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
 * @brief Retrieves the name of a result column.
 * @param query Query handle.
 * @param index 0-based column index.
 * @param out_name Pointer to receive column name string.
 * @return Error code.
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
 * @brief Retrieves the driver virtual table for the memory driver.
 * @param out_vtable Pointer to receive driver vtable pointer.
 * @return 0 on success, non-zero on error.
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
