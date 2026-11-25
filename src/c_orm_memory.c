/**
 * @file c_orm_memory.c
 * @brief In-memory SQL database driver supporting multi-column WHERE, UPDATE,
 * DELETE, ORDER BY, LIMIT, OFFSET, and snapshot-isolated transactions.
 */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_memory.h"
#include "c_orm_api.h"
#include "c_orm_log.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32) && !defined(_WIN64)
#include <sys/time.h>
#endif
/* clang-format on */

#if defined(_WIN32) || defined(_WIN64)
/**
 * @brief Win32 64-bit performance counter union without including windows.h.
 * @var u Split 32-bit low/high halves.
 * @var QuadPart Signed 64-bit counter value.
 */
typedef union _MEM_LARGE_INTEGER {
  /** @brief Split 32-bit low and high halves. */
  struct {
    /** @brief Low-order 32 bits. */
    unsigned long LowPart;
    /** @brief High-order 32 bits. */
    long HighPart;
  } u; /**< @brief Split 32-bit low and high halves. */
#if defined(_MSC_VER)
  /** @brief Signed 64-bit integer tick value. */
  __int64 QuadPart;
#else
  /** @brief Signed 64-bit integer tick value. */
  c_orm_int64_t QuadPart;
#endif
} MEM_LARGE_INTEGER;

/**
 * @brief Retrieves the current value of the Win32 performance counter.
 * @param lpPerformanceCount Output pointer receiving the counter value.
 * @return Non-zero on success, or 0 on failure.
 */
__declspec(dllimport) int __stdcall QueryPerformanceCounter(
    MEM_LARGE_INTEGER *lpPerformanceCount);

/**
 * @brief Retrieves the frequency of the Win32 performance counter.
 * @param lpFrequency Output pointer receiving ticks per second.
 * @return Non-zero on success, or 0 on failure.
 */
__declspec(dllimport) int __stdcall QueryPerformanceFrequency(
    MEM_LARGE_INTEGER *lpFrequency);
#endif

#ifdef C_ORM_TEST_ALLOCATOR
/** @brief Optional simulated step duration in ms for slow-query telemetry
 * tests. */
C_ORM_EXPORT double c_orm_mock_mem_step_elapsed_ms = -1.0;
#endif

/** @brief Maximum columns tracked per in-memory table. */
#define MEM_MAX_COLS 32
/** @brief Maximum bound parameters per prepared statement. */
#define MEM_MAX_PARAMS 32
/** @brief Maximum WHERE predicates per statement. */
#define MEM_MAX_CONDS 16
/** @brief Maximum SET assignments per UPDATE statement. */
#define MEM_MAX_SETS 16

/** @brief Query type for memory driver. */
typedef enum {
  MEM_QUERY_SELECT_ALL,
  MEM_QUERY_SELECT_PK,
  MEM_QUERY_INSERT,
  MEM_QUERY_UPDATE,
  MEM_QUERY_DELETE,
  MEM_QUERY_CREATE_TABLE,
  MEM_QUERY_BEGIN,
  MEM_QUERY_COMMIT,
  MEM_QUERY_ROLLBACK,
  MEM_QUERY_RAW
} mem_query_type_t;

/** @brief Comparison operators for WHERE predicates. */
typedef enum {
  MEM_OP_EQ = 0,
  MEM_OP_NEQ,
  MEM_OP_LT,
  MEM_OP_GT,
  MEM_OP_LTE,
  MEM_OP_GTE,
  MEM_OP_IS_NULL,
  MEM_OP_IS_NOT_NULL
} mem_op_t;

/** @brief Value type for in-memory cell. */
typedef enum {
  MEM_VAL_NULL = 0,
  MEM_VAL_INT32,
  MEM_VAL_INT64,
  MEM_VAL_DOUBLE,
  MEM_VAL_STRING,
  MEM_VAL_BLOB,
  MEM_VAL_DECIMAL
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
  /** @brief Exact fixed-point decimal value. */
  c_orm_decimal_t dec;
  /** @brief String value. */
  char *s;
  /** @brief Blob data buffer. */
  void *blob;
  /** @brief Blob size in bytes. */
  size_t blob_size;
} mem_cell_t;

/**
 * @brief Parsed WHERE condition predicate.
 */
typedef struct {
  /** @brief Column identifier name. */
  char col_name[64];
  /** @brief Comparison operator. */
  mem_op_t op;
  /** @brief Non-zero if value comes from a 1-based bound parameter. */
  int is_param;
  /** @brief 1-based bound parameter index when `is_param != 0`. */
  size_t param_idx;
  /** @brief Inline literal type when `is_param == 0`. */
  mem_val_type_t lit_type;
  /** @brief Inline integer literal value. */
  int64_t lit_i64;
  /** @brief Inline floating-point literal value. */
  double lit_d;
  /** @brief Inline string literal value. */
  char lit_s[128];
} mem_cond_t;

/**
 * @brief Parsed SET assignment clause for UPDATE statements.
 */
typedef struct {
  /** @brief Target column identifier name. */
  char col_name[64];
  /** @brief Non-zero if assigned value comes from a bound parameter. */
  int is_param;
  /** @brief 1-based bound parameter index when `is_param != 0`. */
  size_t param_idx;
  /** @brief Inline literal type when `is_param == 0`. */
  mem_val_type_t lit_type;
  /** @brief Inline integer literal value. */
  int64_t lit_i64;
  /** @brief Inline floating-point literal value. */
  double lit_d;
  /** @brief Inline string literal value. */
  char lit_s[128];
} mem_set_t;

/**
 * @brief Row structure.
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
 * @brief Table structure.
 * @var name Table name string.
 * @var col_names Column identifier names for schema-aware queries.
 * @var num_col_names Number of known column names.
 * @var head Pointer to first row in table.
 * @var next Pointer to next table in database.
 */
typedef struct mem_table {
  /** @brief Table name string. */
  char *name;
  /** @brief Column identifier names. */
  char col_names[MEM_MAX_COLS][64];
  /** @brief Number of known column names. */
  size_t num_col_names;
  /** @brief Pointer to first row in table. */
  mem_row_t *head;
  /** @brief Pointer to next table in database. */
  struct mem_table *next;
} mem_table_t;

/**
 * @brief In-memory database handle state.
 * @var tables Linked list head of active database tables.
 * @var tx_snapshot Deep-copied table list saved at `BEGIN` for `ROLLBACK`.
 * @var in_tx Non-zero while an explicit transaction is active.
 * @var last_rowid Last inserted integer primary key / rowid.
 * @var last_error Buffer storing last error message string.
 * @var last_trace Buffer storing last diagnostic trace string.
 */
typedef struct {
  /** @brief Back-pointer to owning public database handle. */
  c_orm_db_t *owner_db;
  /** @brief Linked list head of active database tables. */
  mem_table_t *tables;
  /** @brief Deep-copied table list saved at `BEGIN` for `ROLLBACK`. */
  mem_table_t *tx_snapshot;
  /** @brief Non-zero while an explicit transaction is active. */
  int in_tx;
  /** @brief Last inserted integer rowid. */
  int64_t last_rowid;
  /** @brief Buffer storing last error message string. */
  char last_error[256];
  /** @brief Buffer storing last diagnostic trace string. */
  char last_trace[320];
} c_orm_memory_db_t;

/**
 * @brief Prepared query context for the in-memory driver.
 */
typedef struct {
  /** @brief Type of query being executed. */
  mem_query_type_t type;
  /** @brief Name of target table. */
  char table_name[64];
  /** @brief Explicit column list parsed from CREATE TABLE or INSERT. */
  char stmt_cols[MEM_MAX_COLS][64];
  /** @brief Number of explicit statement columns. */
  size_t num_stmt_cols;
  /** @brief Parsed SET assignments for UPDATE. */
  mem_set_t sets[MEM_MAX_SETS];
  /** @brief Number of parsed SET assignments. */
  size_t num_sets;
  /** @brief Parsed WHERE predicates (conjunctive AND). */
  mem_cond_t conds[MEM_MAX_CONDS];
  /** @brief Number of parsed WHERE predicates. */
  size_t num_conds;
  /** @brief Non-zero if ORDER BY clause is present. */
  int has_order;
  /** @brief Column name in ORDER BY clause. */
  char order_col[64];
  /** @brief Non-zero for DESC ordering, 0 for ASC. */
  int order_desc;
  /** @brief Non-zero if LIMIT clause is present. */
  int has_limit;
  /** @brief Maximum rows to return when `has_limit != 0`. */
  size_t limit_val;
  /** @brief Non-zero if OFFSET clause is present. */
  int has_offset;
  /** @brief Row offset to skip when `has_offset != 0`. */
  size_t offset_val;
  /** @brief Array of bound parameter pointers. */
  void **bound_params;
  /** @brief Highest 1-based bound parameter index populated. */
  size_t num_bound;
  /** @brief Current row pointer for simple un-ordered iteration. */
  mem_row_t *current_row;
  /** @brief Materialized result row array for filtered/sorted/paginated SELECT.
   */
  mem_row_t **result_rows;
  /** @brief Number of materialized result rows. */
  size_t result_count;
  /** @brief Current cursor position inside `result_rows`. */
  size_t result_idx;
  /** @brief Non-zero when iterating via `result_rows`. */
  int use_result_array;
  /** @brief Pointer to parent memory database instance. */
  c_orm_memory_db_t *db;
  /** @brief Flag indicating whether iteration has started. */
  int step_started;
} c_orm_memory_query_t;

/**
 * @brief Frees a single `mem_cell_t` and its owned buffers.
 * @param cell Cell pointer to free.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t mem_cell_free(mem_cell_t *cell) {
  c_orm_error_t rc = C_ORM_OK;
  c_orm_error_t frc;
  if (!cell) {
    return C_ORM_OK;
  }
  frc = c_orm_system_free(cell->s);
  if (frc != C_ORM_OK)
    rc = frc;
  frc = c_orm_system_free(cell->blob);
  if (frc != C_ORM_OK && rc == C_ORM_OK)
    rc = frc;
  frc = c_orm_system_free(cell);
  if (frc != C_ORM_OK && rc == C_ORM_OK)
    rc = frc;
  return rc;
}

/**
 * @brief Deep-clones a `mem_cell_t` instance.
 * @param src Source cell pointer (may be NULL).
 * @param out_clone Output pointer receiving allocated copy.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on OOM.
 */
static c_orm_error_t clone_mem_cell(const mem_cell_t *src,
                                    mem_cell_t **out_clone) {
  mem_cell_t *dst = NULL;
  c_orm_error_t rc;
  *out_clone = NULL;
  if (!src) {
    return C_ORM_OK;
  }
  rc = c_orm_system_calloc(1, sizeof(mem_cell_t), (void **)&dst);
  if (rc != C_ORM_OK) {
    return rc;
  }
  dst->type = src->type;
  dst->i32 = src->i32;
  dst->i64 = src->i64;
  dst->d = src->d;
  dst->dec = src->dec;
  if ((src->type == MEM_VAL_STRING || src->type == MEM_VAL_DECIMAL) && src->s) {
    rc = C_ORM_STRDUP(src->s, &dst->s);
    if (rc != C_ORM_OK) {
      c_orm_system_free(dst);
      return rc;
    }
  } else if (src->type == MEM_VAL_BLOB && src->blob_size > 0) {
    rc = c_orm_system_malloc(src->blob_size, &dst->blob);
    if (rc != C_ORM_OK) {
      c_orm_system_free(dst);
      return rc;
    }
    memcpy(dst->blob, src->blob, src->blob_size);
    dst->blob_size = src->blob_size;
  }
  *out_clone = dst;
  return C_ORM_OK;
}

/**
 * @brief Frees an entire linked list of `mem_table_t` structures.
 * @param head First table in list.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t mem_table_list_free(mem_table_t *head) {
  c_orm_error_t rc = C_ORM_OK;
  c_orm_error_t frc;
  mem_table_t *t = head;
  while (t) {
    mem_table_t *nt = t->next;
    mem_row_t *r = t->head;
    while (r) {
      mem_row_t *nr = r->next;
      size_t col_i;
      for (col_i = 0; col_i < r->num_cols; col_i++) {
        frc = mem_cell_free((mem_cell_t *)r->columns[col_i]);
        if (frc != C_ORM_OK && rc == C_ORM_OK)
          rc = frc;
      }
      frc = c_orm_system_free(r->columns);
      if (frc != C_ORM_OK && rc == C_ORM_OK)
        rc = frc;
      frc = c_orm_system_free(r);
      if (frc != C_ORM_OK && rc == C_ORM_OK)
        rc = frc;
      r = nr;
    }
    frc = c_orm_system_free(t->name);
    if (frc != C_ORM_OK && rc == C_ORM_OK)
      rc = frc;
    frc = c_orm_system_free(t);
    if (frc != C_ORM_OK && rc == C_ORM_OK)
      rc = frc;
    t = nt;
  }
  return rc;
}

/**
 * @brief Deep-clones an entire linked list of `mem_table_t` structures for
 * transaction rollback snapshots.
 * @param src Head of table list to clone.
 * @param out_head Output pointer receiving cloned list head.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on OOM.
 */
static c_orm_error_t clone_table_list(const mem_table_t *src,
                                      mem_table_t **out_head) {
  mem_table_t *head = NULL;
  mem_table_t **tail = &head;
  const mem_table_t *t;
  c_orm_error_t rc;

  *out_head = NULL;

  for (t = src; t != NULL; t = t->next) {
    mem_table_t *nt = NULL;
    mem_row_t **rtail;
    const mem_row_t *r;

    rc = c_orm_system_calloc(1, sizeof(mem_table_t), (void **)&nt);
    if (rc != C_ORM_OK) {
      mem_table_list_free(head);
      return rc;
    }
    rc = C_ORM_STRDUP(t->name, &nt->name);
    if (rc != C_ORM_OK) {
      c_orm_system_free(nt);
      mem_table_list_free(head);
      return rc;
    }
    memcpy(nt->col_names, t->col_names, sizeof(t->col_names));
    nt->num_col_names = t->num_col_names;

    rtail = &nt->head;
    for (r = t->head; r != NULL; r = r->next) {
      mem_row_t *nr = NULL;
      size_t ci;
      rc = c_orm_system_calloc(1, sizeof(mem_row_t), (void **)&nr);
      if (rc != C_ORM_OK) {
        mem_table_list_free(nt);
        mem_table_list_free(head);
        return rc;
      }
      nr->num_cols = r->num_cols;
      if (r->num_cols > 0) {
        rc = c_orm_system_calloc(r->num_cols, sizeof(void *),
                                 (void **)&nr->columns);
        if (rc != C_ORM_OK) {
          c_orm_system_free(nr);
          mem_table_list_free(nt);
          mem_table_list_free(head);
          return rc;
        }
        for (ci = 0; ci < r->num_cols; ci++) {
          rc = clone_mem_cell((const mem_cell_t *)r->columns[ci],
                              (mem_cell_t **)&nr->columns[ci]);
          if (rc != C_ORM_OK) {
            nr->num_cols = ci;
            *rtail = nr;
            mem_table_list_free(nt);
            mem_table_list_free(head);
            return rc;
          }
        }
      }
      *rtail = nr;
      rtail = &nr->next;
    }

    *tail = nt;
    tail = &nt->next;
  }

  *out_head = head;
  return C_ORM_OK;
}

/**
 * @brief Case-insensitive ASCII prefix match helper.
 * @param s Input string.
 * @param prefix Prefix to match.
 * @param n Length of prefix.
 * @return C_ORM_OK if `s` starts with `prefix`, C_ORM_ERROR_NOT_FOUND
 * otherwise.
 */
static c_orm_error_t istarts_with(const char *s, const char *prefix, size_t n) {
  size_t i;
  for (i = 0; i < n; ++i) {
    if (!s[i] ||
        tolower((unsigned char)s[i]) != tolower((unsigned char)prefix[i])) {
      return C_ORM_ERROR_NOT_FOUND;
    }
  }
  return C_ORM_OK;
}

/** @brief Convenience macro invoking `istarts_with` without dead branches. */
#define MEM_ISTARTS(S, PRE, LEN) (istarts_with((S), (PRE), (LEN)) == C_ORM_OK)

/** @brief Advances cursor `P` past ASCII whitespace characters (`isspace(0)` is
 * always 0). */
#define MEM_SKIP_WS(P)                                                         \
  do {                                                                         \
    while (isspace((unsigned char)*(P))) {                                     \
      (P)++;                                                                   \
    }                                                                          \
  } while (0)

/**
 * @brief Finds a standalone SQL keyword outside single-quoted string literals.
 * @param sql Input SQL string.
 * @param kw Uppercase ASCII keyword to search for.
 * @param out_pos Output pointer receiving the start of the keyword, or NULL.
 * @return 1 if found, or 0 otherwise.
 */
static int mem_calc_find_sql_keyword(const char *sql, const char *kw,
                                     const char **out_pos) {
  const char *p = sql;
  size_t kw_len = strlen(kw);
  int in_quote = 0;

  *out_pos = NULL;
  while (*p) {
    if (*p == ((char)0x27)) {
      in_quote = !in_quote;
      p++;
      continue;
    }
    if (!in_quote && MEM_ISTARTS(p, kw, kw_len)) {
      int left_ok = (p == sql) || isspace((unsigned char)p[-1]) || p[-1] == ')';
      int right_ok = (p[kw_len] == '\0') || isspace((unsigned char)p[kw_len]) ||
                     p[kw_len] == '(' || p[kw_len] == ';';
      if (left_ok && right_ok) {
        *out_pos = p;
        return 1;
      }
    }
    p++;
  }
  return 0;
}

/**
 * @brief Reads a single SQL identifier (optionally quoted or `table.col`
 * qualified, keeping the column base name).
 * @param p Cursor positioned at identifier start.
 * @param out Destination buffer.
 * @param out_sz Destination buffer capacity.
 * @param out_next Output cursor following identifier.
 * @return 1 if a non-empty identifier was read, 0 otherwise.
 */
static int mem_calc_parse_sql_ident(const char *p, char *out, size_t out_sz,
                                    const char **out_next) {
  size_t idx = 0;
  char quote = 0;
  MEM_SKIP_WS(p);
  if (*p == '"' || *p == '`') {
    quote = *p++;
  }
  while (*p) {
    if (quote) {
      if (*p == quote) {
        p++;
        break;
      }
    } else if (isspace((unsigned char)*p) || strchr(",()=!<> ;", *p) != NULL) {
      break;
    }
    if (*p == '.' && !quote) {
      /* Strip table alias prefix `t.col` -> `col` */
      idx = 0;
      p++;
      continue;
    }
    if (idx + 1 < out_sz) {
      out[idx++] = *p;
    }
    p++;
  }
  out[idx] = '\0';
  *out_next = p;
  return (idx > 0) ? 1 : 0;
}

/**
 * @brief Parses an optional-sign base-10 integer prefix from `s`.
 * @param s Input character cursor.
 * @param out_end Output pointer receiving the first unconsumed character.
 * @param out_val Output signed 64-bit integer value.
 * @return 1 if at least one digit was parsed without overflow, or 0 otherwise.
 */
static int mem_calc_parse_int64(const char *s, const char **out_end,
                                int64_t *out_val) {
  const char *p = s;
  uint64_t acc = 0;
  uint64_t lim;
  int neg = 0;
  int any = 0;

  if (*p == '-' || *p == '+') {
    neg = (*p == '-');
    p++;
  }
  lim = neg ? (((uint64_t)1 << 63)) : (((uint64_t)1 << 63) - 1U);
  while (isdigit((unsigned char)*p)) {
    uint64_t d = (uint64_t)(unsigned char)(*p - '0');
    any = 1;
    if (acc > (lim - d) / 10U) {
      *out_end = p;
      return 0;
    }
    acc = acc * 10U + d;
    p++;
  }
  *out_end = p;
  if (!any) {
    return 0;
  }
  if (neg && acc > 0U) {
    *out_val = -(int64_t)(acc - 1U) - 1;
  } else {
    *out_val = (int64_t)acc;
  }
  return 1;
}

/**
 * @brief Parses a scalar RHS value (`?`, `$N`, `'str'`, `NULL`, integer, or
 * float) and increments `*io_param_seq` when consuming `?`.
 * @param p Cursor positioned at value start.
 * @param io_param_seq Running 1-based positional `?` counter.
 * @param out_is_param Output flag set to 1 for bound parameter.
 * @param out_param_idx Output 1-based parameter index.
 * @param out_lit_type Output inline literal type.
 * @param out_i64 Output integer literal.
 * @param out_d Output float literal.
 * @param out_s Output string literal buffer (128 bytes).
 * @param out_next Output cursor after value token.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t parse_rhs_value(const char *p, size_t *io_param_seq,
                                     int *out_is_param, size_t *out_param_idx,
                                     mem_val_type_t *out_lit_type,
                                     int64_t *out_i64, double *out_d,
                                     char *out_s, const char **out_next) {
  c_orm_error_t rc;
  MEM_SKIP_WS(p);
  *out_is_param = 0;
  *out_param_idx = 0;
  *out_lit_type = MEM_VAL_NULL;
  *out_i64 = 0;
  *out_d = 0.0;
  out_s[0] = '\0';

  if (*p == '?') {
    (*io_param_seq)++;
    *out_is_param = 1;
    *out_param_idx = *io_param_seq;
    *out_next = p + 1;
    return C_ORM_OK;
  }
  if (*p == '$' && isdigit((unsigned char)p[1])) {
    size_t idx = 0;
    p++;
    while (isdigit((unsigned char)*p)) {
      idx = idx * 10 + (size_t)(*p - '0');
      p++;
    }
    if (idx > *io_param_seq) {
      *io_param_seq = idx;
    }
    *out_is_param = 1;
    *out_param_idx = idx;
    *out_next = p;
    return C_ORM_OK;
  }
  if (*p == ((char)0x27)) {
    size_t si = 0;
    p++;
    while (*p) {
      if (*p == ((char)0x27) && p[1] == ((char)0x27)) {
        if (si + 1 < 128)
          out_s[si++] = ((char)0x27);
        p += 2;
        continue;
      }
      if (*p == ((char)0x27)) {
        p++;
        break;
      }
      if (si + 1 < 128)
        out_s[si++] = *p;
      p++;
    }
    out_s[si] = '\0';
    *out_lit_type = MEM_VAL_STRING;
    *out_next = p;
    return C_ORM_OK;
  }
  {
    char tok[64];
    size_t ti = 0;
    while (*p && !isspace((unsigned char)*p) && strchr(",);", *p) == NULL) {
      if (ti + 1 < sizeof(tok)) {
        tok[ti++] = *p;
      }
      p++;
    }
    tok[ti] = '\0';
    *out_next = p;
    if (MEM_ISTARTS(tok, "NULL", 4) && tok[4] == '\0') {
      *out_lit_type = MEM_VAL_NULL;
    } else if (strchr(tok, '.') != NULL) {
      *out_lit_type = MEM_VAL_DOUBLE;
      *out_d = strtod(tok, NULL);
    } else {
      int64_t iv = 0;
      const char *ep = tok;
      int iok = (ti > 0) ? mem_calc_parse_int64(tok, &ep, &iv) : 0;
      if (iok && *ep == '\0') {
        *out_lit_type = MEM_VAL_INT64;
        *out_i64 = iv;
      } else {
        *out_lit_type = MEM_VAL_STRING;
        rc = C_ORM_STRCPY(out_s, 128, tok);
        if (rc != C_ORM_OK) {
          return rc;
        }
      }
    }
  }
  return C_ORM_OK;
}

/**
 * @brief Parses conjunctive `WHERE col op val [AND ...]` clauses until
 * `ORDER BY`, `LIMIT`, `OFFSET`, `;`, or end-of-string.
 * @param where_start Pointer right after `WHERE`.
 * @param io_param_seq Running 1-based `?` parameter sequence counter.
 * @param q Target query structure to populate.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t parse_where_clause(const char *where_start,
                                        size_t *io_param_seq,
                                        c_orm_memory_query_t *q) {
  const char *p = where_start;
  c_orm_error_t rc;
  while (*p && q->num_conds < MEM_MAX_CONDS) {
    mem_cond_t *c;
    MEM_SKIP_WS(p);
    if (!*p || *p == ';' || MEM_ISTARTS(p, "ORDER", 5) ||
        MEM_ISTARTS(p, "LIMIT", 5) || MEM_ISTARTS(p, "OFFSET", 6)) {
      break;
    }
    if (MEM_ISTARTS(p, "AND", 3) && isspace((unsigned char)p[3])) {
      p += 3;
      MEM_SKIP_WS(p);
    }
    c = &q->conds[q->num_conds];
    memset(c, 0, sizeof(*c));
    if (!mem_calc_parse_sql_ident(p, c->col_name, sizeof(c->col_name), &p)) {
      break;
    }
    MEM_SKIP_WS(p);
    if (MEM_ISTARTS(p, "IS", 2) && isspace((unsigned char)p[2])) {
      p += 2;
      MEM_SKIP_WS(p);
      if (MEM_ISTARTS(p, "NOT", 3) && isspace((unsigned char)p[3])) {
        p += 3;
        MEM_SKIP_WS(p);
        if (MEM_ISTARTS(p, "NULL", 4)) {
          p += 4;
        }
        c->op = MEM_OP_IS_NOT_NULL;
      } else {
        if (MEM_ISTARTS(p, "NULL", 4)) {
          p += 4;
        }
        c->op = MEM_OP_IS_NULL;
      }
      q->num_conds++;
      continue;
    }
    if (p[0] == '!' && p[1] == '=') {
      c->op = MEM_OP_NEQ;
      p += 2;
    } else if (p[0] == '<' && p[1] == '>') {
      c->op = MEM_OP_NEQ;
      p += 2;
    } else if (p[0] == '<' && p[1] == '=') {
      c->op = MEM_OP_LTE;
      p += 2;
    } else if (p[0] == '>' && p[1] == '=') {
      c->op = MEM_OP_GTE;
      p += 2;
    } else if (p[0] == '<') {
      c->op = MEM_OP_LT;
      p += 1;
    } else if (p[0] == '>') {
      c->op = MEM_OP_GT;
      p += 1;
    } else if (p[0] == '=') {
      c->op = MEM_OP_EQ;
      p += 1;
    } else {
      break;
    }
    rc = parse_rhs_value(p, io_param_seq, &c->is_param, &c->param_idx,
                         &c->lit_type, &c->lit_i64, &c->lit_d, c->lit_s, &p);
    if (rc != C_ORM_OK)
      return rc;
    q->num_conds++;
  }
  return C_ORM_OK;
}

/**
 * @brief Resolves a column name to its 0-based index on `t`, falling back to
 * index 0 for `"id"` when column names were not declared via DDL/INSERT.
 * @param t Table structure.
 * @param col_name Column name to look up.
 * @param out_idx Output 0-based index.
 * @return 1 if resolved, 0 otherwise.
 */
static int mem_calc_resolve_col_index(const mem_table_t *t,
                                      const char *col_name, size_t *out_idx) {
  size_t i;
  for (i = 0; i < t->num_col_names; ++i) {
    if (strcmp(t->col_names[i], col_name) == 0) {
      *out_idx = i;
      return 1;
    }
  }
  if (strcmp(col_name, "id") == 0) {
    *out_idx = 0;
    return 1;
  }
  return 0;
}

/**
 * @brief Populates `out_dec` from an unsigned 64-bit magnitude, sign, and
 * scale.
 * @param mag Unsigned base-10 magnitude.
 * @param sign 1 for negative, 0 for non-negative.
 * @param scale Fractional digit count.
 * @param out_dec Output decimal structure.
 * @return Always 1.
 */
static int mem_calc_u64_to_decimal(uint64_t mag, unsigned char sign,
                                   unsigned char scale,
                                   c_orm_decimal_t *out_dec) {
  unsigned char p = 0;
  memset(out_dec, 0, sizeof(*out_dec));
  out_dec->scale = scale;
  while (mag > 0U) {
    out_dec->digits[p++] = (unsigned char)(mag % 10U);
    mag /= 10U;
  }
  out_dec->sign = (p > 0U) ? sign : 0U;
  out_dec->precision = (p <= scale) ? (unsigned char)(scale + 1U) : p;
  return 1;
}

/**
 * @brief Converts a cell to `c_orm_decimal_t` for numeric ordering.
 * @param cell Cell to convert.
 * @param out_dec Output decimal.
 * @return 1 if convertible, or 0 otherwise.
 */
static int mem_calc_cell_to_decimal(const mem_cell_t *cell,
                                    c_orm_decimal_t *out_dec) {
  if (cell->type == MEM_VAL_DECIMAL) {
    *out_dec = cell->dec;
    return 1;
  }
  if (cell->type == MEM_VAL_INT32 || cell->type == MEM_VAL_INT64) {
    int64_t v = (cell->type == MEM_VAL_INT32) ? (int64_t)cell->i32 : cell->i64;
    unsigned char neg = (v < 0) ? 1U : 0U;
    uint64_t mag = neg ? ((uint64_t)(-(v + 1)) + 1U) : (uint64_t)v;
    return mem_calc_u64_to_decimal(mag, neg, 0U, out_dec);
  }
  if (cell->type == MEM_VAL_STRING && cell->s) {
    const char *p = cell->s;
    unsigned char neg = 0U;
    unsigned char scale = 0U;
    int seen_dot = 0;
    int seen_digit = 0;
    uint64_t mag = 0U;
    if (*p == '-') {
      neg = 1U;
      p++;
    }
    while (*p) {
      if (*p == '.' && !seen_dot) {
        seen_dot = 1;
      } else if (isdigit((unsigned char)*p)) {
        seen_digit = 1;
        mag = mag * 10U + (uint64_t)(unsigned char)(*p - '0');
        if (seen_dot) {
          scale++;
        }
      } else {
        return 0;
      }
      p++;
    }
    if (!seen_digit) {
      return 0;
    }
    return mem_calc_u64_to_decimal(mag, neg, scale, out_dec);
  }
  return 0;
}

/**
 * @brief Compares two normalized `c_orm_decimal_t` values (`< 0`, `0`, `> 0`).
 * @param a Left decimal.
 * @param b Right decimal.
 * @return `-1`, `0`, or `1`.
 */
static int mem_cmp_decimals(const c_orm_decimal_t *a,
                            const c_orm_decimal_t *b) {
  int pos;
  if (a->sign != b->sign) {
    return a->sign ? -1 : 1;
  }
  for (pos = C_ORM_DECIMAL_MAX_DIGITS - 1; pos >= -C_ORM_DECIMAL_MAX_DIGITS;
       --pos) {
    int ia = pos + (int)a->scale;
    int ib = pos + (int)b->scale;
    unsigned char da =
        (ia >= 0 && ia < C_ORM_DECIMAL_MAX_DIGITS) ? a->digits[ia] : 0U;
    unsigned char db =
        (ib >= 0 && ib < C_ORM_DECIMAL_MAX_DIGITS) ? b->digits[ib] : 0U;
    if (da != db) {
      int mag_ord = (da < db) ? -1 : 1;
      return a->sign ? -mag_ord : mag_ord;
    }
  }
  return 0;
}

/**
 * @brief Orders two `mem_cell_t` values (`< 0`, `0`, `> 0`), treating NULL
 * as ordering first.
 * @param a Left cell (may be NULL).
 * @param b Right cell (may be NULL).
 * @param out_ord Output ordering sign (`< 0`, `0`, `> 0`).
 * @return 1 if comparable, or 0 otherwise.
 */
static int mem_cmp_cells(const mem_cell_t *a, const mem_cell_t *b,
                         int *out_ord) {
  int a_null = (!a || a->type == MEM_VAL_NULL);
  int b_null = (!b || b->type == MEM_VAL_NULL);

  *out_ord = 0;
  if (a_null && b_null) {
    *out_ord = 0;
    return 1;
  }
  if (a_null) {
    *out_ord = -1;
    return 1;
  }
  if (b_null) {
    *out_ord = 1;
    return 1;
  }

  if (a->type == MEM_VAL_DECIMAL || b->type == MEM_VAL_DECIMAL) {
    c_orm_decimal_t da;
    c_orm_decimal_t db_dec;
    if (mem_calc_cell_to_decimal(a, &da) &&
        mem_calc_cell_to_decimal(b, &db_dec)) {
      *out_ord = mem_cmp_decimals(&da, &db_dec);
      return 1;
    }
    return 0;
  }
  if ((a->type == MEM_VAL_INT32 || a->type == MEM_VAL_INT64) &&
      (b->type == MEM_VAL_INT32 || b->type == MEM_VAL_INT64)) {
    int64_t av = (a->type == MEM_VAL_INT32) ? (int64_t)a->i32 : a->i64;
    int64_t bv = (b->type == MEM_VAL_INT32) ? (int64_t)b->i32 : b->i64;
    *out_ord = (av < bv) ? -1 : (av > bv ? 1 : 0);
    return 1;
  }
  if ((a->type == MEM_VAL_DOUBLE || a->type == MEM_VAL_INT32 ||
       a->type == MEM_VAL_INT64) &&
      (b->type == MEM_VAL_DOUBLE || b->type == MEM_VAL_INT32 ||
       b->type == MEM_VAL_INT64)) {
    double av = (a->type == MEM_VAL_DOUBLE)
                    ? a->d
                    : (double)((a->type == MEM_VAL_INT32) ? a->i32 : a->i64);
    double bv = (b->type == MEM_VAL_DOUBLE)
                    ? b->d
                    : (double)((b->type == MEM_VAL_INT32) ? b->i32 : b->i64);
    *out_ord = (av < bv) ? -1 : (av > bv ? 1 : 0);
    return 1;
  }
  if (a->type == MEM_VAL_STRING && b->type == MEM_VAL_STRING) {
    const char *as = a->s ? a->s : "";
    const char *bs = b->s ? b->s : "";
    int ord = strcmp(as, bs);
    *out_ord = (ord < 0) ? -1 : (ord > 0 ? 1 : 0);
    return 1;
  }
  return 0;
}

/**
 * @brief Evaluates all conjunctive WHERE conditions in `q` against `row`.
 * @param q Prepared query with bound parameters.
 * @param t Table containing `row`.
 * @param row Candidate row.
 * @return 1 if `row` matches, or 0 otherwise.
 */
static int mem_calc_eval_where_row(const c_orm_memory_query_t *q,
                                   const mem_table_t *t, const mem_row_t *row) {
  size_t ci;

  for (ci = 0; ci < q->num_conds; ++ci) {
    const mem_cond_t *c = &q->conds[ci];
    size_t col_idx = 0;
    const mem_cell_t *lhs = NULL;
    mem_cell_t rhs_lit;
    const mem_cell_t *rhs = NULL;
    int lhs_null;
    int ord = 0;
    int ok = 1;

    if (!mem_calc_resolve_col_index(t, c->col_name, &col_idx)) {
      return 0;
    }
    if (col_idx < row->num_cols) {
      lhs = (const mem_cell_t *)row->columns[col_idx];
    }
    lhs_null = (!lhs || lhs->type == MEM_VAL_NULL);

    if (c->op == MEM_OP_IS_NULL) {
      if (!lhs_null) {
        return 0;
      }
      continue;
    }
    if (c->op == MEM_OP_IS_NOT_NULL) {
      if (lhs_null) {
        return 0;
      }
      continue;
    }

    if (c->is_param) {
      if (c->param_idx >= 1 && c->param_idx <= MEM_MAX_PARAMS) {
        rhs = (const mem_cell_t *)q->bound_params[c->param_idx - 1];
      }
    } else {
      memset(&rhs_lit, 0, sizeof(rhs_lit));
      rhs_lit.type = c->lit_type;
      rhs_lit.i64 = c->lit_i64;
      rhs_lit.i32 = (int32_t)c->lit_i64;
      rhs_lit.d = c->lit_d;
      rhs_lit.s = (char *)(size_t)c->lit_s;
      rhs = &rhs_lit;
    }

    if (lhs_null || !rhs || rhs->type == MEM_VAL_NULL) {
      return 0;
    }

    if (!mem_cmp_cells(lhs, rhs, &ord)) {
      return 0;
    }
    if (c->op == MEM_OP_EQ) {
      ok = (ord == 0);
    } else if (c->op == MEM_OP_NEQ) {
      ok = (ord != 0);
    } else if (c->op == MEM_OP_LT) {
      ok = (ord < 0);
    } else if (c->op == MEM_OP_GT) {
      ok = (ord > 0);
    } else if (c->op == MEM_OP_LTE) {
      ok = (ord <= 0);
    } else {
      ok = (ord >= 0);
    }
    if (!ok) {
      return 0;
    }
  }
  return 1;
}

/**
 * @brief Looks up a cell in the current row of `query` at `index`.
 * @param query Query handle.
 * @param index 0-based column index.
 * @param out_cell Output pointer receiving the cell.
 * @return 1 if found, or 0 otherwise.
 */
static int mem_calc_get_row_cell(c_orm_query_t *query, int index,
                                 mem_cell_t **out_cell) {
  c_orm_memory_query_t *q = (c_orm_memory_query_t *)query;
  if (!q || !q->current_row || index < 0 ||
      (size_t)index >= q->current_row->num_cols ||
      !q->current_row->columns[index]) {
    return 0;
  }
  *out_cell = (mem_cell_t *)q->current_row->columns[index];
  return 1;
}

/**
 * @brief Helper to retrieve a cell from the current row.
 * @param query Query handle.
 * @param index 0-based column index.
 * @param out_cell Output pointer receiving the cell.
 * @return C_ORM_OK if found, or C_ORM_ERROR_NOT_FOUND otherwise.
 */
static c_orm_error_t mem_get_row_cell(c_orm_query_t *query, int index,
                                      mem_cell_t **out_cell) {
  return mem_calc_get_row_cell(query, index, out_cell) ? C_ORM_OK
                                                       : C_ORM_ERROR_NOT_FOUND;
}

/**
 * @brief Disconnects and frees an in-memory database instance.
 * @param db Database handle to close.
 * @return 0 on success.
 */
static c_orm_error_t mem_disconnect(c_orm_db_t *db) {
  c_orm_error_t rc = C_ORM_OK;
  c_orm_error_t frc;
  c_orm_error_t cache_rc = C_ORM_OK;
  c_orm_memory_db_t *ctx;
  LOG_DEBUG("mem_disconnect: entry");

  if (!db) {
    return C_ORM_OK;
  }
  ctx = (c_orm_memory_db_t *)db->driver_data;
  if (ctx) {
    rc = mem_table_list_free(ctx->tables);
    frc = mem_table_list_free(ctx->tx_snapshot);
    if (frc != C_ORM_OK && rc == C_ORM_OK)
      rc = frc;
    cache_rc = c_orm_disable_statement_caching(db);
    if (cache_rc != C_ORM_OK) {
      c_orm_system_free(ctx);
      c_orm_system_free(db);
      return (rc != C_ORM_OK) ? rc : cache_rc;
    }
    frc = c_orm_system_free(ctx);
    if (frc != C_ORM_OK && rc == C_ORM_OK)
      rc = frc;
  }
  frc = c_orm_system_free(db);
  if (frc != C_ORM_OK && rc == C_ORM_OK)
    rc = frc;

  LOG_DEBUG("mem_disconnect: exit");
  return rc;
}

/**
 * @brief Parses parenthesized column list `(col1 [TYPE], col2 [TYPE], ...)`
 * into `q->stmt_cols`.
 * @param paren_open Pointer to `'('`.
 * @param q Target query structure.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t parse_column_list(const char *paren_open,
                                       c_orm_memory_query_t *q) {
  const char *p = paren_open + 1;
  int depth = 1;
  c_orm_error_t rc;
  while (*p && q->num_stmt_cols < MEM_MAX_COLS) {
    char col[64];
    MEM_SKIP_WS(p);
    if (*p == ')') {
      break;
    }
    /* Skip table-level constraints in CREATE TABLE */
    if (MEM_ISTARTS(p, "PRIMARY", 7) || MEM_ISTARTS(p, "FOREIGN", 7) ||
        MEM_ISTARTS(p, "UNIQUE", 6) || MEM_ISTARTS(p, "CONSTRAINT", 10) ||
        MEM_ISTARTS(p, "CHECK", 5)) {
      while (*p && !(depth == 1 && (*p == ',' || *p == ')'))) {
        if (*p == '(')
          depth++;
        else if (*p == ')')
          depth--;
        p++;
      }
      if (*p == ',')
        p++;
      continue;
    }
    if (mem_calc_parse_sql_ident(p, col, sizeof(col), &p)) {
      rc = C_ORM_STRCPY(q->stmt_cols[q->num_stmt_cols], 64, col);
      if (rc != C_ORM_OK)
        return rc;
      q->num_stmt_cols++;
    }
    while (*p && !(depth == 1 && (*p == ',' || *p == ')'))) {
      if (*p == '(')
        depth++;
      else if (*p == ')')
        depth--;
      p++;
    }
    if (*p == ',') {
      p++;
    }
  }
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
  c_orm_memory_query_t *q;
  c_orm_memory_db_t *ctx;
  const char *p;
  const char *kw_pos = NULL;
  size_t param_seq = 0;
  c_orm_error_t rc = C_ORM_OK;

  LOG_DEBUG("mem_prepare: entry");
  if (!db || !db->driver_data) {
    return C_ORM_ERROR_SQL;
  }
  ctx = (c_orm_memory_db_t *)db->driver_data;
  if (!sql || !out_query) {
    rc = C_ORM_STRCPY(ctx->last_error, sizeof(ctx->last_error),
                      "Invalid SQL statement");
    if (rc != C_ORM_OK) {
      return rc;
    }
    return C_ORM_ERROR_SQL;
  }
  q = NULL;
  rc = c_orm_system_malloc(sizeof(c_orm_memory_query_t), (void **)&q);
  if (rc != C_ORM_OK) {
    return rc;
  }
  memset(q, 0, sizeof(*q));

  rc = c_orm_system_calloc(MEM_MAX_PARAMS, sizeof(void *),
                           (void **)&q->bound_params);
  if (rc != C_ORM_OK) {
    c_orm_system_free(q);
    return rc;
  }
  q->db = ctx;

  p = sql;
  MEM_SKIP_WS(p);
  if (MEM_ISTARTS(p, "BEGIN", 5)) {
    q->type = MEM_QUERY_BEGIN;
  } else if (MEM_ISTARTS(p, "COMMIT", 6)) {
    q->type = MEM_QUERY_COMMIT;
  } else if (MEM_ISTARTS(p, "ROLLBACK", 8)) {
    q->type = MEM_QUERY_ROLLBACK;
  } else if (MEM_ISTARTS(p, "CREATE TABLE", 12)) {
    const char *tp = p + 12;
    MEM_SKIP_WS(tp);
    if (MEM_ISTARTS(tp, "IF NOT EXISTS", 13)) {
      tp += 13;
      MEM_SKIP_WS(tp);
    }
    if (mem_calc_parse_sql_ident(tp, q->table_name, sizeof(q->table_name),
                                 &tp)) {
      MEM_SKIP_WS(tp);
      if (*tp == '(') {
        q->type = MEM_QUERY_CREATE_TABLE;
        rc = parse_column_list(tp, q);
        if (rc != C_ORM_OK) {
          goto prepare_err;
        }
      } else {
        q->type = MEM_QUERY_RAW;
      }
    } else {
      q->type = MEM_QUERY_RAW;
    }
  } else if (MEM_ISTARTS(p, "SELECT", 6)) {
    int has_from = mem_calc_find_sql_keyword(p, "FROM", &kw_pos);
    int id_ok = 0;
    if (has_from) {
      id_ok = mem_calc_parse_sql_ident(kw_pos + 4, q->table_name,
                                       sizeof(q->table_name), &p);
    }
    if (has_from && id_ok) {
      const char *after_tbl = p;
      q->type = MEM_QUERY_SELECT_ALL;
      if (mem_calc_find_sql_keyword(after_tbl, "WHERE", &kw_pos)) {
        q->type = MEM_QUERY_SELECT_PK;
        rc = parse_where_clause(kw_pos + 5, &param_seq, q);
        if (rc != C_ORM_OK) {
          goto prepare_err;
        }
      }
      if (mem_calc_find_sql_keyword(after_tbl, "ORDER", &kw_pos)) {
        const char *op = kw_pos + 5;
        MEM_SKIP_WS(op);
        if (MEM_ISTARTS(op, "BY", 2)) {
          op += 2;
          if (mem_calc_parse_sql_ident(op, q->order_col, sizeof(q->order_col),
                                       &op)) {
            MEM_SKIP_WS(op);
            if (MEM_ISTARTS(op, "DESC", 4)) {
              q->order_desc = 1;
            }
            q->has_order = 1;
          }
        }
      }
      if (mem_calc_find_sql_keyword(after_tbl, "LIMIT", &kw_pos)) {
        int64_t lv = 0;
        const char *ep = NULL;
        const char *lp = kw_pos + 5;
        MEM_SKIP_WS(lp);
        if (mem_calc_parse_int64(lp, &ep, &lv) && lv >= 0) {
          q->has_limit = 1;
          q->limit_val = (size_t)lv;
        }
      }
      if (mem_calc_find_sql_keyword(after_tbl, "OFFSET", &kw_pos)) {
        int64_t ov = 0;
        const char *ep = NULL;
        const char *op2 = kw_pos + 6;
        MEM_SKIP_WS(op2);
        if (mem_calc_parse_int64(op2, &ep, &ov) && ov >= 0) {
          q->has_offset = 1;
          q->offset_val = (size_t)ov;
        }
      }
    } else {
      q->type = MEM_QUERY_RAW;
    }
  } else if (MEM_ISTARTS(p, "INSERT INTO", 11)) {
    const char *tp = p + 11;
    q->type = MEM_QUERY_INSERT;
    if (mem_calc_parse_sql_ident(tp, q->table_name, sizeof(q->table_name),
                                 &tp)) {
      MEM_SKIP_WS(tp);
      if (*tp == '(') {
        rc = parse_column_list(tp, q);
        if (rc != C_ORM_OK) {
          goto prepare_err;
        }
      }
    }
  } else if (MEM_ISTARTS(p, "UPDATE", 6)) {
    const char *tp = p + 6;
    q->type = MEM_QUERY_UPDATE;
    if (mem_calc_parse_sql_ident(tp, q->table_name, sizeof(q->table_name),
                                 &tp)) {
      if (mem_calc_find_sql_keyword(tp, "SET", &kw_pos)) {
        const char *sp = kw_pos + 3;
        while (*sp && q->num_sets < MEM_MAX_SETS) {
          mem_set_t *s;
          MEM_SKIP_WS(sp);
          if (!*sp || *sp == ';' || MEM_ISTARTS(sp, "WHERE", 5)) {
            break;
          }
          s = &q->sets[q->num_sets];
          memset(s, 0, sizeof(*s));
          if (!mem_calc_parse_sql_ident(sp, s->col_name, sizeof(s->col_name),
                                        &sp)) {
            break;
          }
          MEM_SKIP_WS(sp);
          if (*sp != '=') {
            break;
          }
          sp++;
          rc = parse_rhs_value(sp, &param_seq, &s->is_param, &s->param_idx,
                               &s->lit_type, &s->lit_i64, &s->lit_d, s->lit_s,
                               &sp);
          if (rc != C_ORM_OK) {
            goto prepare_err;
          }
          q->num_sets++;
          MEM_SKIP_WS(sp);
          if (*sp == ',') {
            sp++;
          }
        }
      }
      if (mem_calc_find_sql_keyword(tp, "WHERE", &kw_pos)) {
        rc = parse_where_clause(kw_pos + 5, &param_seq, q);
        if (rc != C_ORM_OK) {
          goto prepare_err;
        }
      }
    }
  } else if (MEM_ISTARTS(p, "DELETE FROM", 11)) {
    const char *tp = p + 11;
    q->type = MEM_QUERY_DELETE;
    if (mem_calc_parse_sql_ident(tp, q->table_name, sizeof(q->table_name),
                                 &tp)) {
      if (mem_calc_find_sql_keyword(tp, "WHERE", &kw_pos)) {
        rc = parse_where_clause(kw_pos + 5, &param_seq, q);
        if (rc != C_ORM_OK) {
          goto prepare_err;
        }
      }
    }
  } else {
    q->type = MEM_QUERY_RAW;
  }

  *out_query = (c_orm_query_t *)q;
  LOG_DEBUG("mem_prepare: exit");
  return C_ORM_OK;

prepare_err:;
  c_orm_system_free(q->bound_params);
  c_orm_system_free(q);
  return rc;
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
  c_orm_error_t rc;
  if (!q || index < 1 || index > MEM_MAX_PARAMS) {
    return C_ORM_ERROR_BIND;
  }
  if (q->bound_params[index - 1]) {
    rc = mem_cell_free((mem_cell_t *)q->bound_params[index - 1]);
    q->bound_params[index - 1] = NULL;
    if (rc != C_ORM_OK) {
      return rc;
    }
  }
  cell = NULL;
  rc = c_orm_system_calloc(1, sizeof(mem_cell_t), (void **)&cell);
  if (rc != C_ORM_OK) {
    return rc;
  }
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
  rc = get_or_alloc_bound_cell(query, index, &cell);
  if (rc != C_ORM_OK) {
    return rc;
  }
  cell->type = MEM_VAL_INT32;
  cell->i32 = val;
  cell->i64 = (int64_t)val;
  return C_ORM_OK;
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
  rc = get_or_alloc_bound_cell(query, index, &cell);
  if (rc != C_ORM_OK) {
    return rc;
  }
  cell->type = MEM_VAL_INT64;
  cell->i64 = val;
  cell->i32 = (int32_t)val;
  return C_ORM_OK;
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
  rc = get_or_alloc_bound_cell(query, index, &cell);
  if (rc != C_ORM_OK) {
    return rc;
  }
  cell->type = MEM_VAL_DOUBLE;
  cell->d = val;
  return C_ORM_OK;
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
  rc = get_or_alloc_bound_cell(query, index, &cell);
  if (rc != C_ORM_OK) {
    return rc;
  }
  cell->type = MEM_VAL_STRING;
  if (val) {
    return C_ORM_STRDUP(val, &cell->s);
  }
  return C_ORM_OK;
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
  rc = get_or_alloc_bound_cell(query, index, &cell);
  if (rc != C_ORM_OK) {
    return rc;
  }
  cell->type = MEM_VAL_BLOB;
  if (val) {
    cell->blob = NULL;
    rc = c_orm_system_malloc(size, &cell->blob);
    if (rc != C_ORM_OK) {
      return rc;
    }
    memcpy(cell->blob, val, size);
    cell->blob_size = size;
  }
  return C_ORM_OK;
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
  rc = get_or_alloc_bound_cell(query, index, &cell);
  if (rc != C_ORM_OK) {
    return rc;
  }
  cell->type = MEM_VAL_NULL;
  return C_ORM_OK;
}

/**
 * @brief Finds or creates a `mem_table_t` in `db->tables` and records any new
 * statement column names.
 * @param q Query handle containing table name and optional `stmt_cols`.
 * @param out_table Output table pointer.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on OOM.
 */
static c_orm_error_t find_or_create_table(c_orm_memory_query_t *q,
                                          mem_table_t **out_table) {
  mem_table_t *t = q->db->tables;
  size_t i;
  c_orm_error_t rc;

  while (t && strcmp(t->name, q->table_name) != 0) {
    t = t->next;
  }
  if (!t) {
    rc = c_orm_system_malloc(sizeof(mem_table_t), (void **)&t);
    if (rc != C_ORM_OK) {
      return rc;
    }
    memset(t, 0, sizeof(*t));
    rc = C_ORM_STRDUP(q->table_name, &t->name);
    if (rc != C_ORM_OK) {
      c_orm_system_free(t);
      return rc;
    }
    t->next = q->db->tables;
    q->db->tables = t;
  }
  if (t->num_col_names == 0 && q->num_stmt_cols > 0) {
    for (i = 0; i < q->num_stmt_cols; ++i) {
      rc = C_ORM_STRCPY(t->col_names[i], 64, q->stmt_cols[i]);
      if (rc != C_ORM_OK) {
        return rc;
      }
    }
    t->num_col_names = q->num_stmt_cols;
  }
  *out_table = t;
  return C_ORM_OK;
}

/**
 * @brief Materializes filtered, sorted, and paginated `SELECT` result rows
 * into `q->result_rows`.
 * @param q Query context.
 * @param t Source table (may be NULL).
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on OOM.
 */
static c_orm_error_t materialize_select_rows(c_orm_memory_query_t *q,
                                             const mem_table_t *t) {
  size_t count = 0;
  size_t cap = 0;
  mem_row_t **arr = NULL;
  mem_row_t *r;

  if (!t) {
    q->result_rows = NULL;
    q->result_count = 0;
    q->result_idx = 0;
    q->use_result_array = 1;
    return C_ORM_OK;
  }

  for (r = t->head; r != NULL; r = r->next) {
    if (mem_calc_eval_where_row(q, t, r)) {
      if (count >= cap) {
        size_t ncap = (cap == 0) ? 8 : cap * 2;
        mem_row_t **narr = NULL;
        c_orm_error_t rc;
        if (!arr) {
          rc = c_orm_system_malloc(ncap * sizeof(mem_row_t *), (void **)&narr);
          if (rc != C_ORM_OK) {
            return rc;
          }
        } else {
          rc = c_orm_system_realloc(arr, ncap * sizeof(mem_row_t *),
                                    (void **)&narr);
          if (rc != C_ORM_OK) {
            c_orm_system_free(arr);
            return rc;
          }
        }
        arr = narr;
        cap = ncap;
      }
      arr[count++] = r;
    }
  }

  if (q->has_order && count > 1) {
    size_t order_idx = 0;
    if (mem_calc_resolve_col_index(t, q->order_col, &order_idx)) {
      size_t i, j;
      for (i = 1; i < count; ++i) {
        mem_row_t *key_row = arr[i];
        const mem_cell_t *kc =
            (order_idx < key_row->num_cols)
                ? (const mem_cell_t *)key_row->columns[order_idx]
                : NULL;
        j = i;
        while (j > 0) {
          const mem_cell_t *jc =
              (order_idx < arr[j - 1]->num_cols)
                  ? (const mem_cell_t *)arr[j - 1]->columns[order_idx]
                  : NULL;
          int ord = 0;
          if (!mem_cmp_cells(jc, kc, &ord)) {
            ord = 0;
          }
          if ((!q->order_desc && ord > 0) || (q->order_desc && ord < 0)) {
            arr[j] = arr[j - 1];
            j--;
          } else {
            break;
          }
        }
        arr[j] = key_row;
      }
    }
  }

  {
    size_t start = q->has_offset ? q->offset_val : 0;
    size_t avail = (start < count) ? (count - start) : 0;
    if (q->has_limit && q->limit_val < avail) {
      avail = q->limit_val;
    }
    if (start > 0 && avail > 0) {
      memmove(arr, arr + start, avail * sizeof(mem_row_t *));
    }
    q->result_rows = arr;
    q->result_count = avail;
    q->result_idx = 0;
    q->use_result_array = 1;
  }
  return C_ORM_OK;
}

/**
 * @brief Builds a new `mem_cell_t` for a SET assignment clause.
 * @param q Query context with bound parameters.
 * @param s Assignment descriptor.
 * @param out_cell Output allocated cell.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on OOM.
 */
static c_orm_error_t build_set_cell(const c_orm_memory_query_t *q,
                                    const mem_set_t *s, mem_cell_t **out_cell) {
  if (s->is_param) {
    const mem_cell_t *bound =
        (s->param_idx >= 1 && s->param_idx <= MEM_MAX_PARAMS)
            ? (const mem_cell_t *)q->bound_params[s->param_idx - 1]
            : NULL;
    if (!bound) {
      mem_cell_t *nc = NULL;
      c_orm_error_t rc;
      rc = c_orm_system_calloc(1, sizeof(mem_cell_t), (void **)&nc);
      if (rc != C_ORM_OK) {
        return rc;
      }
      nc->type = MEM_VAL_NULL;
      *out_cell = nc;
      return C_ORM_OK;
    }
    return clone_mem_cell(bound, out_cell);
  } else {
    mem_cell_t tmp;
    memset(&tmp, 0, sizeof(tmp));
    tmp.type = s->lit_type;
    tmp.i64 = s->lit_i64;
    tmp.i32 = (int32_t)s->lit_i64;
    tmp.d = s->lit_d;
    tmp.s = (char *)(size_t)s->lit_s;
    return clone_mem_cell(&tmp, out_cell);
  }
}

/**
 * @brief Executes core in-memory statement stepping logic.
 * @param q In-memory query structure.
 * @param out_has_row Pointer to receive 1 if row available, 0 if done.
 * @return C_ORM_OK on success, or error code.
 */
static c_orm_error_t mem_step_core(c_orm_memory_query_t *q, int *out_has_row) {
  mem_table_t *t;
  c_orm_error_t rc;

  *out_has_row = 0;
  if (!q) {
    return C_ORM_OK;
  }

  if (q->type == MEM_QUERY_BEGIN) {
    if (!q->db->in_tx) {
      rc = clone_table_list(q->db->tables, &q->db->tx_snapshot);
      if (rc != C_ORM_OK) {
        return rc;
      }
      q->db->in_tx = 1;
    }
    return C_ORM_OK;
  }

  if (q->type == MEM_QUERY_COMMIT) {
    if (q->db->in_tx) {
      rc = mem_table_list_free(q->db->tx_snapshot);
      q->db->tx_snapshot = NULL;
      q->db->in_tx = 0;
      return rc;
    }
    return C_ORM_OK;
  }

  if (q->type == MEM_QUERY_ROLLBACK) {
    if (q->db->in_tx) {
      rc = mem_table_list_free(q->db->tables);
      q->db->tables = q->db->tx_snapshot;
      q->db->tx_snapshot = NULL;
      q->db->in_tx = 0;
      return rc;
    }
    return C_ORM_OK;
  }

  if (q->type == MEM_QUERY_CREATE_TABLE) {
    return find_or_create_table(q, &t);
  }

  if (q->type == MEM_QUERY_INSERT) {
    mem_row_t *row;
    size_t i;
    rc = find_or_create_table(q, &t);
    if (rc != C_ORM_OK) {
      return rc;
    }
    row = NULL;
    rc = c_orm_system_malloc(sizeof(mem_row_t), (void **)&row);
    if (rc != C_ORM_OK) {
      return rc;
    }
    row->num_cols = q->num_bound;
    row->columns = NULL;
    if (q->num_bound > 0) {
      rc = c_orm_system_malloc(q->num_bound * sizeof(void *),
                               (void **)&row->columns);
      if (rc != C_ORM_OK) {
        c_orm_system_free(row);
        return rc;
      }
    }
    for (i = 0; i < q->num_bound; i++) {
      row->columns[i] = q->bound_params[i];
      q->bound_params[i] = NULL;
    }
    if (row->num_cols > 0 && row->columns[0]) {
      const mem_cell_t *pk = (const mem_cell_t *)row->columns[0];
      if (pk->type == MEM_VAL_INT32 || pk->type == MEM_VAL_INT64) {
        q->db->last_rowid = pk->i64;
      }
    }
    q->num_bound = 0;
    row->next = t->head;
    t->head = row;
    LOG_DEBUG("mem_step: exit insert");
    return C_ORM_OK;
  }

  if (q->type == MEM_QUERY_UPDATE) {
    mem_row_t *r;
    t = q->db->tables;
    while (t && strcmp(t->name, q->table_name) != 0) {
      t = t->next;
    }
    if (!t) {
      return C_ORM_OK;
    }
    for (r = t->head; r != NULL; r = r->next) {
      size_t si;
      if (!mem_calc_eval_where_row(q, t, r)) {
        continue;
      }
      for (si = 0; si < q->num_sets; ++si) {
        size_t col_idx = 0;
        mem_cell_t *new_cell = NULL;
        if (!mem_calc_resolve_col_index(t, q->sets[si].col_name, &col_idx)) {
          continue;
        }
        if (col_idx >= r->num_cols) {
          void **new_cols = NULL;
          size_t k;
          if (r->columns) {
            rc = c_orm_system_realloc(
                r->columns, (col_idx + 1) * sizeof(void *), (void **)&new_cols);
            if (rc != C_ORM_OK) {
              return rc;
            }
          } else {
            rc = c_orm_system_calloc(col_idx + 1, sizeof(void *),
                                     (void **)&new_cols);
            if (rc != C_ORM_OK) {
              return rc;
            }
          }
          for (k = r->num_cols; k <= col_idx; ++k) {
            new_cols[k] = NULL;
          }
          r->columns = new_cols;
          r->num_cols = col_idx + 1;
        }
        rc = build_set_cell(q, &q->sets[si], &new_cell);
        if (rc != C_ORM_OK) {
          return rc;
        }
        rc = mem_cell_free((mem_cell_t *)r->columns[col_idx]);
        r->columns[col_idx] = new_cell;
        if (rc != C_ORM_OK) {
          return rc;
        }
      }
    }
    return C_ORM_OK;
  }

  if (q->type == MEM_QUERY_DELETE) {
    mem_row_t **prev;
    t = q->db->tables;
    while (t && strcmp(t->name, q->table_name) != 0) {
      t = t->next;
    }
    if (!t) {
      return C_ORM_OK;
    }
    prev = &t->head;
    while (*prev) {
      mem_row_t *cur = *prev;
      if (mem_calc_eval_where_row(q, t, cur)) {
        size_t ci;
        *prev = cur->next;
        for (ci = 0; ci < cur->num_cols; ++ci) {
          rc = mem_cell_free((mem_cell_t *)cur->columns[ci]);
          if (rc != C_ORM_OK)
            return rc;
        }
        rc = c_orm_system_free(cur->columns);
        if (rc != C_ORM_OK)
          return rc;
        rc = c_orm_system_free(cur);
        if (rc != C_ORM_OK)
          return rc;
      } else {
        prev = &cur->next;
      }
    }
    return C_ORM_OK;
  }

  if (q->type == MEM_QUERY_SELECT_ALL || q->type == MEM_QUERY_SELECT_PK) {
    if (!q->step_started) {
      q->step_started = 1;
      t = q->db->tables;
      while (t && strcmp(t->name, q->table_name) != 0) {
        t = t->next;
      }
      if (q->num_conds > 0 || q->has_order || q->has_limit || q->has_offset) {
        rc = materialize_select_rows(q, t);
        if (rc != C_ORM_OK) {
          return rc;
        }
      } else {
        q->current_row = t ? t->head : NULL;
      }
    } else if (!q->use_result_array && q->current_row) {
      q->current_row = q->current_row->next;
    }

    if (q->use_result_array) {
      if (q->result_idx < q->result_count) {
        q->current_row = q->result_rows[q->result_idx++];
        *out_has_row = 1;
      } else {
        q->current_row = NULL;
        *out_has_row = 0;
      }
    } else if (q->current_row) {
      *out_has_row = 1;
    }
  }

  LOG_DEBUG("mem_step: exit");
  return C_ORM_OK;
}

/**
 * @brief Steps execution of an in-memory statement with slow-query telemetry.
 * @param query Query handle.
 * @param out_has_row Pointer to receive 1 if row available, 0 if done.
 * @return C_ORM_OK on success, or error code.
 */
static c_orm_error_t mem_step(c_orm_query_t *query, int *out_has_row) {
  c_orm_memory_query_t *q = (c_orm_memory_query_t *)query;
  c_orm_db_t *owner_db = q ? q->db->owner_db : NULL;
  c_orm_error_t rc;
  double elapsed = 0.0;
#if defined(_WIN32) || defined(_WIN64)
  MEM_LARGE_INTEGER start_t;
  MEM_LARGE_INTEGER end_t;
  MEM_LARGE_INTEGER freq;
  start_t.QuadPart = 0;
  end_t.QuadPart = 0;
  freq.QuadPart = 0;
#else
  struct timeval start_t;
  struct timeval end_t;
  start_t.tv_sec = 0;
  start_t.tv_usec = 0;
  end_t.tv_sec = 0;
  end_t.tv_usec = 0;
#endif

  if (owner_db && owner_db->slow_query_threshold_ms > 0) {
#if defined(_WIN32) || defined(_WIN64)
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start_t);
#else
    gettimeofday(&start_t, NULL);
#endif
  }

  rc = mem_step_core(q, out_has_row);
  if (rc != C_ORM_OK) {
    return rc;
  }

  if (owner_db && owner_db->slow_query_threshold_ms > 0) {
#if defined(_WIN32) || defined(_WIN64)
    QueryPerformanceCounter(&end_t);
    elapsed = (double)(end_t.QuadPart - start_t.QuadPart) * 1000.0 /
              (double)freq.QuadPart;
#else
    gettimeofday(&end_t, NULL);
    elapsed = (double)(end_t.tv_sec - start_t.tv_sec) * 1000.0 +
              (double)(end_t.tv_usec - start_t.tv_usec) / 1000.0;
#endif
#ifdef C_ORM_TEST_ALLOCATOR
    if (c_orm_mock_mem_step_elapsed_ms >= 0.0) {
      elapsed = c_orm_mock_mem_step_elapsed_ms;
    }
#endif
    if (elapsed >= (double)owner_db->slow_query_threshold_ms) {
      owner_db->telemetry.slow_queries_logged++;
      if (owner_db->log_cb) {
        char log_msg[96];
        size_t tlen = strlen(q->table_name);
        c_orm_error_t lrc;
        memcpy(log_msg, "SLOW QUERY: ", 12);
        memcpy(log_msg + 12, q->table_name, tlen + 1);
        lrc = owner_db->log_cb(log_msg, owner_db->log_user_data);
        if (lrc != C_ORM_OK) {
          return lrc;
        }
      }
    }
  }

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
  c_orm_error_t rc;
  if (!out_val) {
    return C_ORM_ERROR_NOT_FOUND;
  }
  rc = mem_get_row_cell(query, index, &cell);
  if (rc != C_ORM_OK) {
    return rc;
  }
  *out_val = cell->i32;
  return C_ORM_OK;
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
  c_orm_error_t rc;
  if (!out_val) {
    return C_ORM_ERROR_NOT_FOUND;
  }
  rc = mem_get_row_cell(query, index, &cell);
  if (rc != C_ORM_OK) {
    return rc;
  }
  *out_val = cell->i64;
  return C_ORM_OK;
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
  c_orm_error_t rc;
  if (!out_val) {
    return C_ORM_ERROR_NOT_FOUND;
  }
  rc = mem_get_row_cell(query, index, &cell);
  if (rc != C_ORM_OK) {
    return rc;
  }
  *out_val = cell->d;
  return C_ORM_OK;
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
  c_orm_error_t rc;
  if (!out_val) {
    return C_ORM_ERROR_NOT_FOUND;
  }
  rc = mem_get_row_cell(query, index, &cell);
  if (rc != C_ORM_OK) {
    return rc;
  }
  *out_val = cell->s;
  return C_ORM_OK;
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
  c_orm_error_t rc;
  if (!out_val || !out_size) {
    return C_ORM_ERROR_NOT_FOUND;
  }
  rc = mem_get_row_cell(query, index, &cell);
  if (rc != C_ORM_OK) {
    return rc;
  }
  *out_val = cell->blob;
  *out_size = cell->blob_size;
  return C_ORM_OK;
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
  if (out_is_null) {
    if (mem_calc_get_row_cell(query, index, &cell)) {
      *out_is_null = (cell->type == MEM_VAL_NULL) ? 1 : 0;
    } else {
      *out_is_null = 1;
    }
  }
  return C_ORM_OK;
}

/**
 * @brief Finalizes and frees a query handle.
 * @param query Query handle to destroy.
 * @return 0 on success.
 */
static c_orm_error_t mem_finalize(c_orm_query_t *query) {
  c_orm_memory_query_t *q;
  c_orm_error_t rc = C_ORM_OK;
  c_orm_error_t frc;
  size_t i;
  if (!query) {
    return C_ORM_OK;
  }
  q = (c_orm_memory_query_t *)query;
  for (i = 0; i < MEM_MAX_PARAMS; i++) {
    if (q->bound_params[i]) {
      frc = mem_cell_free((mem_cell_t *)q->bound_params[i]);
      if (frc != C_ORM_OK && rc == C_ORM_OK)
        rc = frc;
    }
  }
  frc = c_orm_system_free(q->result_rows);
  if (frc != C_ORM_OK && rc == C_ORM_OK)
    rc = frc;
  frc = c_orm_system_free(q->bound_params);
  if (frc != C_ORM_OK && rc == C_ORM_OK)
    rc = frc;
  frc = c_orm_system_free(q);
  if (frc != C_ORM_OK && rc == C_ORM_OK)
    rc = frc;
  return rc;
}

/**
 * @brief Resets a prepared statement for re-execution.
 * @param query Query handle to reset.
 * @return 0 on success.
 */
static c_orm_error_t mem_reset(c_orm_query_t *query) {
  c_orm_memory_query_t *q = (c_orm_memory_query_t *)query;
  if (q) {
    c_orm_system_free(q->result_rows);
    q->result_rows = NULL;
    q->result_count = 0;
    q->result_idx = 0;
    q->use_result_array = 0;
    q->current_row = NULL;
    q->step_started = 0;
  }
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
  c_orm_memory_db_t *ctx;
  ctx = (c_orm_memory_db_t *)db->driver_data;
  *out_message = ctx->last_error;
  return C_ORM_ERROR_UNKNOWN;
}

/**
 * @brief Retrieves a formatted diagnostic trace string for the in-memory
 * driver.
 * @param db Database handle.
 * @param out_trace Pointer to receive trace string pointer.
 * @return C_ORM_OK on success, or C_ORM_ERROR_NULL_POINTER on invalid
 * arguments.
 */
static c_orm_error_t mem_get_last_trace(c_orm_db_t *db,
                                        const char **out_trace) {
  c_orm_memory_db_t *ctx;
  c_orm_error_t rc;
  if (!db || !db->driver_data || !out_trace) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  ctx = (c_orm_memory_db_t *)db->driver_data;
  memcpy(ctx->last_trace, "[MemoryDriver] ", 15);
  rc = C_ORM_STRCPY(ctx->last_trace + 15, sizeof(ctx->last_trace) - 15,
                    ctx->last_error[0] ? ctx->last_error : "OK");
  if (rc != C_ORM_OK) {
    return rc;
  }
  *out_trace = ctx->last_trace;
  return C_ORM_OK;
}

/**
 * @brief Retrieves the last inserted rowid.
 * @param db Database handle.
 * @param out_id Pointer to receive row ID.
 * @return Error code.
 */
static c_orm_error_t mem_get_last_insert_rowid(c_orm_db_t *db,
                                               int64_t *out_id) {
  c_orm_memory_db_t *ctx;
  if (!db || !out_id) {
    if (out_id) {
      *out_id = 0;
    }
    return C_ORM_ERROR_NOT_IMPLEMENTED;
  }
  ctx = (c_orm_memory_db_t *)db->driver_data;
  if (ctx && ctx->last_rowid > 0) {
    *out_id = ctx->last_rowid;
    return C_ORM_OK;
  }
  *out_id = 0;
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}

/**
 * @brief Retrieves the number of columns in the result set.
 * @param query Query handle.
 * @param out_count Pointer to receive column count.
 * @return Error code.
 */
static c_orm_error_t mem_get_column_count(c_orm_query_t *query,
                                          int *out_count) {
  c_orm_memory_query_t *q = (c_orm_memory_query_t *)query;
  mem_table_t *t;
  if (out_count) {
    *out_count = 0;
  }
  if (!q || !out_count ||
      (q->type != MEM_QUERY_SELECT_ALL && q->type != MEM_QUERY_SELECT_PK)) {
    return C_ORM_ERROR_NOT_IMPLEMENTED;
  }
  t = q->db->tables;
  while (t && strcmp(t->name, q->table_name) != 0) {
    t = t->next;
  }
  if (t && t->num_col_names > 0) {
    *out_count = (int)t->num_col_names;
    return C_ORM_OK;
  }
  return C_ORM_ERROR_NOT_IMPLEMENTED;
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
  c_orm_memory_query_t *q = (c_orm_memory_query_t *)query;
  mem_table_t *t;
  if (out_name) {
    *out_name = NULL;
  }
  if (!q || !out_name || index < 0 ||
      (q->type != MEM_QUERY_SELECT_ALL && q->type != MEM_QUERY_SELECT_PK)) {
    return C_ORM_ERROR_NOT_IMPLEMENTED;
  }
  t = q->db->tables;
  while (t && strcmp(t->name, q->table_name) != 0) {
    t = t->next;
  }
  if (t && (size_t)index < t->num_col_names) {
    *out_name = t->col_names[index];
    return C_ORM_OK;
  }
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}

/**
 * @brief Binds an exact fixed-point decimal parameter in the memory driver.
 * @param query Query handle.
 * @param index 1-based parameter index.
 * @param val Decimal value pointer.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t mem_bind_decimal(c_orm_query_t *query, int index,
                                      const c_orm_decimal_t *val) {
  mem_cell_t *cell = NULL;
  char sbuf[64];
  c_orm_error_t rc;
  if (!val) {
    return mem_bind_null(query, index);
  }
  rc = c_orm_decimal_to_str(val, sbuf, sizeof(sbuf), NULL);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = get_or_alloc_bound_cell(query, index, &cell);
  if (rc != C_ORM_OK) {
    return rc;
  }
  cell->type = MEM_VAL_DECIMAL;
  cell->dec = *val;
  return C_ORM_STRDUP(sbuf, &cell->s);
}

/**
 * @brief Retrieves an exact fixed-point decimal column value from the memory
 * driver.
 * @param query Query handle.
 * @param index 0-based column index.
 * @param out_val Output pointer receiving decimal value.
 * @return C_ORM_OK on success, or an error code.
 */
static c_orm_error_t mem_get_decimal(c_orm_query_t *query, int index,
                                     c_orm_decimal_t *out_val) {
  mem_cell_t *cell = NULL;
  c_orm_error_t rc;
  if (!out_val) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  rc = mem_get_row_cell(query, index, &cell);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (cell->type == MEM_VAL_DECIMAL) {
    *out_val = cell->dec;
    return C_ORM_OK;
  }
  if (cell->type == MEM_VAL_INT32) {
    return c_orm_decimal_from_int64((c_orm_int64_t)cell->i32, out_val);
  }
  if (cell->type == MEM_VAL_INT64) {
    return c_orm_decimal_from_int64((c_orm_int64_t)cell->i64, out_val);
  }
  if (cell->type == MEM_VAL_STRING && cell->s) {
    return c_orm_decimal_from_str(cell->s, out_val);
  }
  return c_orm_decimal_zero(0, out_val);
}

/** @brief Driver virtual table for in-memory database. */
static const c_orm_driver_vtable_t memory_vtable = {c_orm_memory_connect,
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
                                                    mem_get_last_trace,
                                                    mem_get_last_insert_rowid,
                                                    mem_get_column_count,
                                                    mem_get_column_name,
                                                    mem_bind_decimal,
                                                    mem_get_decimal};

/**
 * @brief Retrieves the driver virtual table for the memory driver.
 * @param out_vtable Pointer to receive driver vtable pointer.
 * @return 0 on success, non-zero on error.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_memory_get_vtable(const c_orm_driver_vtable_t **out_vtable) {
  if (out_vtable) {
    *out_vtable = &memory_vtable;
    return C_ORM_OK;
  }
  return C_ORM_ERROR_UNKNOWN;
}

/**
 * @brief Connects to an in-memory database instance.
 * @param url Connection string URL (e.g. `memory://`, `:memory:`, or NULL).
 * @param out_db Pointer to receive allocated database handle.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_memory_connect(const char *url,
                                                c_orm_db_t **out_db) {
  c_orm_memory_db_t *ctx = NULL;
  c_orm_db_t *db = NULL;
  c_orm_error_t rc;
  c_orm_error_t frc;

  LOG_DEBUG("c_orm_memory_connect: entry");

  if (!out_db) {
    LOG_DEBUG("c_orm_memory_connect: validation error");
    return C_ORM_ERROR_MEMORY;
  }

  rc = c_orm_system_malloc(sizeof(c_orm_memory_db_t), (void **)&ctx);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_memory_connect: OOM");
    return rc;
  }
  memset(ctx, 0, sizeof(*ctx));
  if (url && url[0] != '\0') {
    rc = C_ORM_STRCPY(ctx->last_trace, sizeof(ctx->last_trace),
                      "[MemoryDriver] OK");
    if (rc != C_ORM_OK) {
      frc = c_orm_system_free(ctx);
      return (frc != C_ORM_OK) ? frc : rc;
    }
  }

  rc = c_orm_system_malloc(sizeof(c_orm_db_t), (void **)&db);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_memory_connect: OOM");
    frc = c_orm_system_free(ctx);
    return (frc != C_ORM_OK) ? frc : rc;
  }
  memset(db, 0, sizeof(*db));

  ctx->owner_db = db;
  db->vtable = &memory_vtable;
  db->driver_data = ctx;
  db->driver_name = "memory";

  *out_db = db;
  LOG_DEBUG("c_orm_memory_connect: exit");
  return C_ORM_OK;
}
