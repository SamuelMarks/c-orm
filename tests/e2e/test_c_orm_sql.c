/**
 * @file test_c_orm_sql.c
 * @brief Unit tests for SQL DDL lexer and parser.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_sql.h"
#include "c_orm_api.h"
#include "query_projection.h"
#define GREATEST_USE_LONGJMP 0
#include <greatest.h>
#include "test_assert_zero_branch.h"
#include <stdlib.h>
#include <string.h>
/* clang-format on */

static int sql_oom_active = 0;
static int sql_oom_countdown = -1;
static int sql_realloc_oom_active = 0;
static int sql_realloc_oom_countdown = -1;

/**
 * @brief Executes sql_mock_malloc.
 * @param size Parameter size.
 * @param out_ptr Output pointer receiving allocated memory.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on OOM.
 */
static c_orm_error_t sql_mock_malloc(size_t size, void **out_ptr) {
  if (sql_oom_active) {
    if (sql_oom_countdown == 0) {
      sql_oom_countdown--;
      *out_ptr = NULL;
      return C_ORM_ERROR_MEMORY;
    }
    sql_oom_countdown--;
  }
  return c_orm_default_malloc(size, out_ptr);
}

/**
 * @brief Mock realloc callback for OOM testing.
 * @param ptr Existing pointer.
 * @param size Allocation size in bytes.
 * @param out_ptr Output pointer receiving reallocated memory.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on OOM.
 */
static c_orm_error_t sql_mock_realloc(void *ptr, size_t size, void **out_ptr) {
  if (sql_realloc_oom_active) {
    if (sql_realloc_oom_countdown == 0) {
      sql_realloc_oom_countdown--;
      *out_ptr = NULL;
      return C_ORM_ERROR_MEMORY;
    }
    sql_realloc_oom_countdown--;
  }
  return c_orm_default_realloc(ptr, size, out_ptr);
}

/**
 * @brief Mock free callback.
 * @param ptr Pointer to free.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t sql_mock_free(void *ptr) {
  c_orm_default_free(ptr);
  return C_ORM_OK;
}

/**
 * @brief Test basic SQL lexer tokenization.
 * @return GREATEST test result.
 */
TEST test_sql_lexer_basic(void) {
  const char *sql =
      "CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(255));";
  az_span span;
  struct sql_token_list_t *list;
  c_orm_error_t rc;

  span = az_span_create_from_str(sql);
  list = NULL;
  rc = sql_lex(span, &list);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT(list != NULL);
  ASSERT(list->size > 0);

  /* CREATE */
  ASSERT_EQ(SQL_TOKEN_KEYWORD, list->tokens[0].kind);
  ASSERT_EQ(6, list->tokens[0].length);
  /* <space> */
  ASSERT_EQ(SQL_TOKEN_WHITESPACE, list->tokens[1].kind);
  /* TABLE */
  ASSERT_EQ(SQL_TOKEN_KEYWORD, list->tokens[2].kind);
  /* <space> */
  ASSERT_EQ(SQL_TOKEN_WHITESPACE, list->tokens[3].kind);
  /* users */
  ASSERT_EQ(SQL_TOKEN_IDENTIFIER, list->tokens[4].kind);

  sql_token_list_free(list);
  PASS();
}

/**
 * @brief Test SQL lexer data types tokenization.
 * @return GREATEST test result.
 */
TEST test_sql_lexer_types(void) {
  const char *sql = "id BIGINT, is_active BOOLEAN DEFAULT true";
  az_span span;
  struct sql_token_list_t *list;
  c_orm_error_t rc;

  span = az_span_create_from_str(sql);
  list = NULL;
  rc = sql_lex(span, &list);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT(list != NULL);

  sql_token_list_free(list);
  PASS();
}

/**
 * @brief Test basic SQL table parsing.
 * @return GREATEST test result.
 */
TEST test_sql_parser_basic(void) {
  const char *sql =
      "CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(255) NOT NULL, "
      "role_id BIGINT REFERENCES roles(id), is_active BOOLEAN DEFAULT true);";
  az_span span;
  struct sql_token_list_t *list;
  struct sql_table_t *table;
  struct sql_parse_error_t err_info;
  c_orm_error_t rc;

  span = az_span_create_from_str(sql);
  list = NULL;
  table = NULL;
  rc = sql_lex(span, &list);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = sql_parse_table(list, &table, &err_info);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT(table != NULL);
  ASSERT_STR_EQ("users", table->name);
  ASSERT_EQ(4, table->n_columns);

  sql_table_C_ORM_FREE(table);
  c_orm_system_free(table);
  sql_token_list_free(list);
  PASS();
}

/**
 * @brief Test SQL lexer with strings and unknown tokens.
 * @return GREATEST test result.
 */
TEST test_sql_lexer_strings_unknown(void) {
  const char *sql = "DEFAULT 'some_string' ^ ~";
  az_span span;
  struct sql_token_list_t *list;
  c_orm_error_t rc;
  int has_str;
  int has_unknown;
  size_t i;

  has_str = 0;
  has_unknown = 0;
  list = NULL;

  span = az_span_create_from_str(sql);
  rc = sql_lex(span, &list);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT(list != NULL);

  for (i = 0; i < list->size; i++) {
    if (list->tokens[i].kind == SQL_TOKEN_STRING) {
      has_str = 1;
    }
    if (list->tokens[i].kind == SQL_TOKEN_UNKNOWN) {
      has_unknown = 1;
    }
  }

  ASSERT_EQ(1, has_str);
  ASSERT_EQ(1, has_unknown);

  sql_token_list_free(list);

  /* unclosed string */
  sql = "'unclosed";
  span = az_span_create_from_str(sql);
  rc = sql_lex(span, &list);
  ASSERT_EQ(C_ORM_OK, rc);
  sql_token_list_free(list);

  PASS();
}

/**
 * @brief Test SQL parser foreign keys and defaults.
 * @return GREATEST test result.
 */
TEST test_sql_parser_foreign_keys_defaults(void) {
  const char *sql = "CREATE TABLE t1 (id INT PRIMARY KEY, "
                    "ref_id INT REFERENCES other_table(id), "
                    "status VARCHAR(255) DEFAULT 'active');";
  const char *many_sql =
      "CREATE TABLE t01 (id INT); CREATE TABLE t02 (id INT); "
      "CREATE TABLE t03 (id INT); CREATE TABLE t04 (id INT); "
      "CREATE TABLE t05 (id INT); CREATE TABLE t06 (id INT); "
      "CREATE TABLE t07 (id INT); CREATE TABLE t08 (id INT); "
      "CREATE TABLE t09 (id INT); CREATE TABLE t10 (id INT); "
      "CREATE TABLE t11 (id INT); CREATE TABLE t12 (id INT); "
      "CREATE TABLE t13 (id INT); CREATE TABLE t14 (id INT); "
      "CREATE TABLE t15 (id INT);";
  struct sql_table_t *tables;
  size_t n_tables;
  size_t i;
  c_orm_error_t rc;

  tables = NULL;
  n_tables = 0;
  rc = parse_sql_ddl(sql, &tables, &n_tables);
  ASSERT_EQ(C_ORM_OK, rc);

  for (i = 0; i < n_tables; ++i) {
    sql_table_free(&tables[i]);
  }
  c_orm_system_free(tables);
  tables = NULL;

  /* Verify >10 tables dynamically grows out_tables without heap overflow */
  n_tables = 0;
  rc = parse_sql_ddl(many_sql, &tables, &n_tables);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ((size_t)15, n_tables);
  ASSERT_STR_EQ("t15", tables[14].name);
  for (i = 0; i < n_tables; ++i) {
    sql_table_free(&tables[i]);
  }
  c_orm_system_free(tables);
  tables = NULL;

  PASS();
}

/**
 * @brief Test SQL parser error cases on invalid DDL inputs.
 * @return GREATEST test result.
 */
TEST test_sql_parser_errors(void) {
  const char *err_sqls[] = {
      "SELECT 1;",
      "CREATE INDEX idx ON t (id);",
      "CREATE TABLE ();",
      "CREATE TABLE t;",
      "CREATE TABLE t (,);",
      "CREATE TABLE t (id);",
      "CREATE TABLE t (id VARCHAR(255);",
      "CREATE TABLE t (id UNKNOWN_DATA_TYPE);",
      "CREATE TABLE t (id INT PRIMARY);",
      "CREATE TABLE t (id INT NOT);",
      "CREATE TABLE t (id INT DEFAULT);",
      "CREATE TABLE t (id INT REFERENCES);",
      "CREATE TABLE t (id INT REFERENCES ());",
      "CREATE TABLE t (id INT REFERENCES p(id;",
      "CREATE TABLE t (id INT, PRIMARY);",
      "CREATE TABLE t (id INT, FOREIGN);",
      "CREATE TABLE t (id INT, PRIMARY KEY);",
      "CREATE TABLE t (id INT, PRIMARY KEY ());",
      "CREATE TABLE t (id INT, PRIMARY KEY (id;",
      "CREATE TABLE t (id INT, FOREIGN KEY (id));",
      "CREATE TABLE t (id INT, FOREIGN KEY (id) REFERENCES);",
      "CREATE TABLE t (id INT, FOREIGN KEY (id) REFERENCES p());",
      "CREATE TABLE t (id INT, FOREIGN KEY (id) REFERENCES p(id;",
      "CREATE TABLE t (id INT;",
      "CREATE TABLE t (id",
      "CREATE TABLE t (id INT",
      "CREATE TABLE t (id INT, CHECK);",
      "CREATE TABLE t (id INT, CHECK (id > 0);",
      NULL};
  size_t i;
  az_span span;
  struct sql_token_list_t *list;
  struct sql_table_t *table;
  struct sql_parse_error_t err_info;
  c_orm_error_t rc;

  for (i = 0; err_sqls[i] != NULL; ++i) {
    span = az_span_create_from_str(err_sqls[i]);
    list = NULL;
    table = NULL;

    rc = sql_lex(span, &list);
    ASSERT_EQ(C_ORM_OK, rc);
    ASSERT(list != NULL);

    rc = sql_parse_table(list, &table, &err_info);
    ASSERT(rc != C_ORM_OK);
    ASSERT_EQ(NULL, table);

    /* Also test with out_error == NULL */
    rc = sql_parse_table(list, &table, NULL);
    ASSERT(rc != C_ORM_OK);
    ASSERT_EQ(NULL, table);

    sql_token_list_free(list);
  }

  /* Validation checks on sql_parse_table */
  rc = sql_parse_table(NULL, &table, &err_info);
  ASSERT(rc != C_ORM_OK);

  rc = sql_parse_table(NULL, NULL, NULL);
  ASSERT(rc != C_ORM_OK);

  PASS();
}

/**
 * @brief Test parser fault injection step failures.
 * @return GREATEST test result.
 */
TEST test_sql_parser_step_failures(void) {
  const char *sqls[] = {
      "CREATE TABLE t (id INT PRIMARY KEY, name VARCHAR(255) NOT NULL, "
      "b BOOL, d DATE, ts TIMESTAMP DEFAULT CURRENT_TIMESTAMP, num INT DEFAULT "
      "42, "
      "f FLOAT, c CHAR(10), p INT, "
      "CONSTRAINT fk FOREIGN KEY (p) REFERENCES parent(id) ON DELETE CASCADE, "
      "PRIMARY KEY (id, p), UNIQUE (name));",
      "CREATE TABLE t (id INT, v VARCHAR, n NUMERIC(10, 2), d DOUBLE "
      "PRECISION, t TIME, u UUID, j JSON, jb JSONB);",
      "CREATE TABLE t (id INT AUTO_INCREMENT, b BIGSERIAL, s SERIAL);",
      "CREATE TABLE t (id INT DEFAULT NULL);", NULL};
  size_t s;
  int i;
  az_span span;
  struct sql_token_list_t *list;
  struct sql_table_t *table;
  struct sql_parse_error_t err_info;
  c_orm_error_t rc;

  for (s = 0; sqls[s] != NULL; ++s) {
    for (i = 0; i < 300; ++i) {
      span = az_span_create_from_str(sqls[s]);
      list = NULL;
      table = NULL;

      rc = sql_lex(span, &list);
      ASSERT_EQ(C_ORM_OK, rc);

      c_orm_parser_set_fail(i);
      rc = sql_parse_table(list, &table, &err_info);
      c_orm_parser_set_fail(-1);

      if (rc == C_ORM_OK) {
        sql_table_C_ORM_FREE(table);
        ASSERT_EQ(C_ORM_OK, c_orm_system_free(table));
        table = NULL;
      }
      sql_token_list_free(list);
    }
  }

  PASS();
}

/**
 * @brief Test constraints capacity expansion and realloc failure.
 * @return GREATEST test result.
 */
TEST test_sql_parser_constraints_capacity(void) {
  const char *sql =
      "CREATE TABLE t (c INT PRIMARY KEY NOT NULL UNIQUE DEFAULT '1');";
  az_span span;
  struct sql_token_list_t *list;
  struct sql_table_t *table;
  struct sql_parse_error_t err_info;
  c_orm_error_t rc;
  int i;

  span = az_span_create_from_str(sql);
  list = NULL;
  table = NULL;

  c_orm_set_allocators(sql_mock_malloc, sql_mock_realloc, sql_mock_free);

  rc = sql_lex(span, &list);
  ASSERT_EQ(C_ORM_OK, rc);

  /* Normal capacity expansion (initial cap is 2, 4 constraints forces
   * expansion) */
  rc = sql_parse_table(list, &table, &err_info);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT(table != NULL);
  sql_table_C_ORM_FREE(table);
  c_orm_system_free(table);
  table = NULL;

  /* Realloc failure during capacity expansion */
  for (i = 0; i < 5; ++i) {
    sql_realloc_oom_active = 1;
    sql_realloc_oom_countdown = i;
    rc = sql_parse_table(list, &table, &err_info);
    if (rc == C_ORM_OK) {
      sql_table_C_ORM_FREE(table);
      c_orm_system_free(table);
      table = NULL;
    }
    sql_realloc_oom_active = 0;
    sql_realloc_oom_countdown = -1;
  }

  sql_token_list_free(list);
  PASS();
}

/**
 * @brief Test projection and returning clauses with OOM handling.
 * @return GREATEST test result.
 */
TEST test_sql_parser_projection_and_returning(void) {
  struct CddCQueryProjection *proj;
  struct sql_parse_error_t err_info;
  c_orm_error_t rc;

  proj = NULL;
  c_orm_set_allocators(sql_mock_malloc, sql_mock_realloc, sql_mock_free);

  /* Normal calls */
  rc = sql_parse_select(NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = sql_parse_select(NULL, &proj, &err_info);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT(proj != NULL);
  cdd_c_query_projection_free((cdd_c_query_projection_t *)proj);
  c_orm_system_free(proj);
  proj = NULL;

  rc = sql_parse_returning(NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = sql_parse_returning(NULL, &proj, &err_info);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT(proj != NULL);
  cdd_c_query_projection_free((cdd_c_query_projection_t *)proj);
  c_orm_system_free(proj);
  proj = NULL;

  /* OOM calls */
  sql_oom_active = 1;
  sql_oom_countdown = 0;
  rc = sql_parse_select(NULL, &proj, &err_info);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  ASSERT_EQ(NULL, proj);

  sql_oom_countdown = 0;
  rc = sql_parse_returning(NULL, &proj, &err_info);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  ASSERT_EQ(NULL, proj);

  sql_oom_active = 0;
  sql_oom_countdown = -1;

  /* Real SELECT and RETURNING projection AST parsing + BLOB/BYTEA DDL */
  {
    struct sql_token_list_t *sel_list = NULL;
    struct sql_token_list_t *ret_list = NULL;
    struct sql_table_t *blob_tables = NULL;
    size_t n_blob_tables = 0;
    cdd_c_query_projection_t *cproj = NULL;

    ASSERT_EQ(C_ORM_OK,
              sql_lex(az_span_create_from_str(
                          "SELECT DISTINCT u.id, COUNT(u.id) AS total FROM u;"),
                      &sel_list));
    ASSERT_EQ(C_ORM_OK, sql_parse_select(sel_list, &proj, &err_info));
    ASSERT(err_info.message == NULL);
    cproj = (cdd_c_query_projection_t *)proj;
    ASSERT_EQ(2U, (unsigned int)cproj->n_fields);
    ASSERT_STR_EQ("id", cproj->fields[0].original_name);
    ASSERT_STR_EQ("id", cproj->fields[0].name);
    ASSERT_EQ(0, cproj->fields[0].is_aggregate);
    ASSERT_STR_EQ("COUNT", cproj->fields[1].original_name);
    ASSERT_STR_EQ("total", cproj->fields[1].name);
    ASSERT_EQ(1, cproj->fields[1].is_aggregate);
    cdd_c_query_projection_free(cproj);
    c_orm_system_free(proj);
    proj = NULL;
    sql_token_list_free(sel_list);

    ASSERT_EQ(C_ORM_OK,
              sql_lex(az_span_create_from_str(
                          "INSERT INTO u (id) VALUES (1) RETURNING id AS uid;"),
                      &ret_list));
    ASSERT_EQ(C_ORM_OK, sql_parse_returning(ret_list, &proj, &err_info));
    cproj = (cdd_c_query_projection_t *)proj;
    ASSERT_EQ(1U, (unsigned int)cproj->n_fields);
    ASSERT_STR_EQ("id", cproj->fields[0].original_name);
    ASSERT_STR_EQ("uid", cproj->fields[0].name);
    cdd_c_query_projection_free(cproj);
    c_orm_system_free(proj);
    proj = NULL;
    sql_token_list_free(ret_list);

    ASSERT_EQ(C_ORM_OK, parse_sql_ddl("CREATE TABLE bin_t (a BLOB, b BYTEA);",
                                      &blob_tables, &n_blob_tables));
    ASSERT_EQ(1U, (unsigned int)n_blob_tables);
    ASSERT_EQ(SQL_TYPE_BLOB, blob_tables[0].columns[0].type);
    ASSERT_EQ(SQL_TYPE_BLOB, blob_tables[0].columns[1].type);
    sql_table_free(&blob_tables[0]);
    c_orm_system_free(blob_tables);
  }

  PASS();
}

/**
 * @brief Test SQL lexer branch coverage and cleanup routines.
 * @return GREATEST test result.
 */
TEST test_sql_lexer_and_cleanup_branches(void) {
  az_span span;
  struct sql_token_list_t *empty_list;
  struct sql_table_t empty_table;
  struct sql_table_t *tables;
  struct sql_table_t *tbl;
  struct sql_column_t *dummy_cols;
  void *tmp_p;
  size_t n_tables;
  size_t i;
  c_orm_error_t rc;

  empty_list = NULL;
  tables = NULL;
  n_tables = 0;

  /* sql_lex validation: out_list is NULL */
  span = az_span_create_from_str("SELECT 1;");
  rc = sql_lex(span, NULL);
  ASSERT(rc != C_ORM_OK);

  /* sql_lex with underscore identifier and trailing whitespace */
  span = az_span_create_from_str("CREATE TABLE _tbl (_col INT);    ");
  rc = sql_lex(span, &empty_list);
  ASSERT_EQ(C_ORM_OK, rc);
  tbl = NULL;
  rc = sql_parse_table(empty_list, &tbl, NULL);
  ASSERT_EQ(C_ORM_OK, rc);
  sql_table_C_ORM_FREE(tbl);
  c_orm_system_free(tbl);
  tbl = NULL;
  sql_token_list_free(empty_list);
  empty_list = NULL;

  /* sql_token_list_free branches */
  sql_token_list_free(NULL);

  ASSERT_EQ(C_ORM_OK, c_orm_default_malloc(sizeof(struct sql_token_list_t),
                                           (void **)&empty_list));
  memset(empty_list, 0, sizeof(*empty_list));
  sql_token_list_free(empty_list);
  empty_list = NULL;

  /* sql_table_C_ORM_FREE branches (NULL table, and NULL column
   * name/constraints) */
  sql_table_C_ORM_FREE(NULL);

  memset(&empty_table, 0, sizeof(empty_table));
  ASSERT_EQ(C_ORM_OK, c_orm_default_malloc(2 * sizeof(struct sql_column_t),
                                           (void **)&dummy_cols));
  memset(dummy_cols, 0, 2 * sizeof(struct sql_column_t));
  ASSERT_EQ(C_ORM_OK,
            c_orm_default_malloc(sizeof(struct sql_constraint_t),
                                 (void **)&dummy_cols[0].constraints));
  memset(dummy_cols[0].constraints, 0, sizeof(struct sql_constraint_t));
  ASSERT_EQ(C_ORM_OK, c_orm_default_malloc(
                          sizeof(char *),
                          (void **)&dummy_cols[0].constraints[0].columns));
  dummy_cols[0].constraints[0].columns[0] = NULL;
  dummy_cols[0].constraints[0].n_columns = 1;
  dummy_cols[0].n_constraints = 1;
  dummy_cols[1].constraints = NULL;
  dummy_cols[1].n_constraints = 0;
  empty_table.columns = dummy_cols;
  empty_table.n_columns = 2;
  sql_table_C_ORM_FREE(&empty_table);
  empty_table.columns = NULL;
  empty_table.n_columns = 0;

  /* parse_sql_ddl validation */
  rc = parse_sql_ddl(NULL, &tables, &n_tables);
  ASSERT(rc != C_ORM_OK);
  rc = parse_sql_ddl("CREATE TABLE t (id INT);", NULL, &n_tables);
  ASSERT(rc != C_ORM_OK);
  rc = parse_sql_ddl("CREATE TABLE t (id INT);", &tables, NULL);
  ASSERT(rc != C_ORM_OK);

  /* parse_sql_ddl with invalid SQL */
  rc = parse_sql_ddl("NOT A VALID DDL STATEMENT;", &tables, &n_tables);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ(0, n_tables);
  c_orm_system_free(tables);
  tables = NULL;

  /* parse_sql_ddl with table failing parse */
  rc = parse_sql_ddl("CREATE TABLE t (invalid_syntax);", &tables, &n_tables);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ(0, n_tables);
  c_orm_system_free(tables);
  tables = NULL;

  /* parse_sql_ddl with multiple tables */
  rc = parse_sql_ddl("CREATE TABLE t1 (id INT); CREATE TABLE t2 (name TEXT);",
                     &tables, &n_tables);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ(2, n_tables);
  for (i = 0; i < n_tables; ++i) {
    sql_table_C_ORM_FREE(&tables[i]);
  }
  c_orm_system_free(tables);
  tables = NULL;

  /* parse_sql_ddl OOM allocating out_tables */
  sql_oom_active = 1;
  sql_oom_countdown = 1;
  ASSERT_EQ(C_ORM_OK, sql_mock_malloc(10, &tmp_p));
  ASSERT(tmp_p != NULL);
  ASSERT_EQ(C_ORM_OK, c_orm_default_free(tmp_p));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, sql_mock_malloc(10, &tmp_p));
  ASSERT_EQ(NULL, tmp_p);

  sql_oom_countdown = 0;
  rc = parse_sql_ddl("CREATE TABLE t (id INT);", &tables, &n_tables);
  ASSERT(rc != C_ORM_OK);
  sql_oom_active = 0;
  sql_oom_countdown = -1;

  PASS();
}

/**
 * @brief Forward declaration for OOM lexer test implementation.
 * @return GREATEST test result.
 */
enum greatest_test_res test_sql_lexer_oom_impl(void);

/**
 * @brief Test SQL lexer OOM error handling.
 * @return GREATEST test result.
 */
TEST test_sql_lexer_oom(void) { return test_sql_lexer_oom_impl(); }

/**
 * @brief Forward declaration for missing keys parser test.
 * @return GREATEST test result.
 */
enum greatest_test_res test_sql_parser_missing_keys(void);

/**
 * @brief Forward declaration for exhaustive OOM parser test.
 * @return GREATEST test result.
 */
enum greatest_test_res test_sql_parser_exhaustive_oom_impl(void);

/**
 * @brief Test SQL parser exhaustive OOM error handling.
 * @return GREATEST test result.
 */
TEST test_sql_parser_exhaustive_oom(void) {
  return test_sql_parser_exhaustive_oom_impl();
}

/**
 * @brief Test exhaustive Db2 DDL clauses, quoted identifiers, types, CHECK
 * constraints, and projection edge cases.
 * @return GREATEST test result.
 */
TEST test_sql_parser_db2_and_full_branches(void) {
  az_span span;
  struct sql_token_list_t *list = NULL;
  struct sql_table_t *table = NULL;
  struct sql_table_t *tables = NULL;
  struct sql_parse_error_t err_info;
  size_t n_tables = 0;
  size_t i;
  int step;
  c_orm_error_t rc;

  /* 1. Quoted identifiers with doubled "" and unclosed ", plus doubled '' */
  span = az_span_create_from_str(
      "CREATE TABLE \"my\"\"schema\".\"my\"\"tbl\" (\"col\"\"1\" VARCHAR(10) "
      "DEFAULT 'it''s');");
  ASSERT_EQ(C_ORM_OK, sql_lex(span, &list));
  ASSERT_EQ(C_ORM_OK, sql_parse_table(list, &table, &err_info));
  ASSERT(table != NULL);
  sql_table_free(table);
  c_orm_system_free(table);
  table = NULL;
  sql_token_list_free(list);
  list = NULL;

  /* Unclosed double-quoted identifier at EOF & closing quote right at EOF */
  span = az_span_create_from_str("\"unclosed_ident");
  ASSERT_EQ(C_ORM_OK, sql_lex(span, &list));
  sql_token_list_free(list);
  list = NULL;

  span = az_span_create_from_str("\"endquote\"");
  ASSERT_EQ(C_ORM_OK, sql_lex(span, &list));
  sql_token_list_free(list);
  list = NULL;

  /* push_token realloc OOM on double-quoted identifier */
  sql_realloc_oom_active = 1;
  sql_realloc_oom_countdown = 0;
  span = az_span_create_from_str("\"ident_oom\"");
  ASSERT_EQ(C_ORM_ERROR_MEMORY, sql_lex(span, &list));
  sql_realloc_oom_active = 0;
  sql_realloc_oom_countdown = -1;

  /* NULL span ptr check in sql_lex */
  span = az_span_empty();
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, sql_lex(span, &list));

  /* 2. All SQL types, Db2 identity, FOR BIT/SBCS/MIXED DATA, CCSID, WITH
   * DEFAULT, inline/table CONSTRAINT & CHECK */
  {
    char db2_sql[1400];
    const char *part1 =
        "CREATE TABLE prod.accounts (\n"
        "  id SMALLINT GENERATED ALWAYS AS IDENTITY (START WITH 1 (INNER)),\n"
        "  seq_id BIGINT GENERATED BY DEFAULT AS IDENTITY,\n"
        "  amt DEC(15, 2) CONSTRAINT KEY NOT NULL CHECK ((amt >= 0)),\n"
        "  fk_id DEC(10) CONSTRAINT nn_fk REFERENCES parent(id),\n"
        "  df DECFLOAT(34) WITH DEFAULT 0,\n"
        "  ch CHARACTER(8) FOR BIT DATA WITH DEFAULT 'X',\n"
        "  vc CHARACTER VARYING(32) FOR BIT DATA WITH DEFAULT NULL,\n";
    const char *part2 =
        "  raw_int INT FOR BIT DATA WITH DEFAULT UNIQUE,\n"
        "  sb VARCHAR(20) FOR SBCS DATA CCSID EBCDIC,\n"
        "  mx VARCHAR(20) FOR MIXED DATA CCSID UNICODE,\n"
        "  asc_col CHAR(10) CCSID ASCII,\n"
        "  num_ccsid CHAR(10) CCSID 1047,\n"
        "  gr GRAPHIC(10), vg VARGRAPHIC(), cl CLOB(1024), dbcl DBCLOB(512),\n"
        "  bin BINARY(16), vbin VARBINARY(32), dbl DOUBLE,\n";
    const char *part3 =
        "  tm TIME WITH DEFAULT CURRENT TIME,\n"
        "  dt DATE WITH DEFAULT CURRENT DATE,\n"
        "  ts TIMESTAMP WITH DEFAULT CURRENT TIMESTAMP,\n"
        "  cur_only VARCHAR(16) WITH DEFAULT CURRENT,\n"
        "  multi_def INT NOT NULL UNIQUE WITH DEFAULT 1,\n"
        "  CONSTRAINT chk_tbl CHECK ((id > 0) AND (seq_id > 0)),\n"
        "  CONSTRAINT uq_seq UNIQUE (seq_id),\n"
        "  CHECK (amt < 1000000)\n"
        ")";
    ASSERT_EQ(C_ORM_OK, C_ORM_STRCPY(db2_sql, sizeof(db2_sql), part1));
    ASSERT_EQ(C_ORM_OK, C_ORM_STRCAT(db2_sql, sizeof(db2_sql), part2));
    ASSERT_EQ(C_ORM_OK, C_ORM_STRCAT(db2_sql, sizeof(db2_sql), part3));
    span = az_span_create_from_str(db2_sql);
    ASSERT_EQ(C_ORM_OK, sql_lex(span, &list));
    ASSERT_EQ(C_ORM_OK, sql_parse_table(list, &table, &err_info));
    ASSERT(table != NULL);
    ASSERT_STR_EQ("accounts", table->name);
    ASSERT_EQ(24U, (unsigned int)table->n_columns);
    ASSERT_EQ(SQL_TYPE_SMALLINT, table->columns[0].type);
    ASSERT_EQ(1, table->columns[0].is_identity);
    ASSERT_EQ(1, table->columns[1].is_identity);
    ASSERT_EQ(SQL_TYPE_DECIMAL, table->columns[2].type);
    ASSERT_EQ(15, table->columns[2].length);
    ASSERT_EQ(2, table->columns[2].scale);
    ASSERT_EQ(SQL_TYPE_DECFLOAT, table->columns[4].type);
    ASSERT_EQ(SQL_TYPE_BINARY, table->columns[5].type);
    ASSERT_EQ(65535U, (unsigned int)table->columns[5].ccsid);
    ASSERT_EQ(SQL_TYPE_VARBINARY, table->columns[6].type);
    ASSERT_EQ(37U, (unsigned int)table->columns[8].ccsid);
    ASSERT_EQ(1208U, (unsigned int)table->columns[9].ccsid);
    ASSERT_EQ(367U, (unsigned int)table->columns[10].ccsid);
    ASSERT_EQ(1047U, (unsigned int)table->columns[11].ccsid);
    ASSERT_EQ(SQL_TYPE_GRAPHIC, table->columns[12].type);
    ASSERT_EQ(SQL_TYPE_VARGRAPHIC, table->columns[13].type);
    ASSERT_EQ(SQL_TYPE_CLOB, table->columns[14].type);
    ASSERT_EQ(SQL_TYPE_DBCLOB, table->columns[15].type);
    ASSERT_EQ(SQL_TYPE_BINARY, table->columns[16].type);
    ASSERT_EQ(SQL_TYPE_VARBINARY, table->columns[17].type);
    ASSERT_EQ(SQL_TYPE_DOUBLE, table->columns[18].type);
    ASSERT_EQ(SQL_TYPE_TIME, table->columns[19].type);
    sql_table_free(table);
    c_orm_system_free(table);
    table = NULL;

    /* Parser step-failure sweep over the entire Db2 DDL */
    for (step = 0;; ++step) {
      c_orm_parser_set_fail(step);
      rc = sql_parse_table(list, &table, &err_info);
      c_orm_parser_set_fail(-1);
      if (rc == C_ORM_OK) {
        sql_table_free(table);
        c_orm_system_free(table);
        table = NULL;
        break;
      }
      ASSERT(step < 1500);
    }

    /* Malloc & Realloc OOM sweep over the entire Db2 DDL */
    for (step = 0;; ++step) {
      sql_oom_active = 1;
      sql_oom_countdown = step;
      rc = sql_parse_table(list, &table, &err_info);
      sql_oom_active = 0;
      sql_oom_countdown = -1;
      if (rc == C_ORM_OK) {
        sql_table_free(table);
        c_orm_system_free(table);
        table = NULL;
        break;
      }
      ASSERT(step < 200);
    }
    for (step = 0;; ++step) {
      sql_realloc_oom_active = 1;
      sql_realloc_oom_countdown = step;
      rc = sql_parse_table(list, &table, &err_info);
      sql_realloc_oom_active = 0;
      sql_realloc_oom_countdown = -1;
      if (rc == C_ORM_OK) {
        sql_table_free(table);
        c_orm_system_free(table);
        table = NULL;
        break;
      }
      ASSERT(step < 200);
    }

    sql_token_list_free(list);
    list = NULL;
  }

  /* 3. Syntax error branches in new Db2 / SQL clauses */
  {
    const char *bad_sqls[] = {
        "CREATE TABLE s.123 (id INT);",
        "CREATE TABLE t @ (id INT);",
        "CREATE TABLE t (a VARCHAR(abc));",
        "CREATE TABLE t (a DECIMAL(10, abc));",
        "CREATE TABLE t (a DECIMAL(10, 2);",
        "CREATE TABLE t (a 123);",
        "CREATE TABLE t (CONSTRAINT 123 PRIMARY KEY (a));",
        "CREATE TABLE t (CONSTRAINT PRIMARY KEY (a));",
        "CREATE TABLE t (a INT CONSTRAINT 123);",
        "CREATE TABLE t (a INT CHECK 123);",
        "CREATE TABLE t (a INT CHECK (a > 0);",
        "CREATE TABLE t (a INT CHECK (a > 0",
        "CREATE TABLE t (a INT GENERATED FOOBAR);",
        "CREATE TABLE t (a INT GENERATED BY FOOBAR);",
        "CREATE TABLE t (a INT GENERATED ALWAYS FOOBAR);",
        "CREATE TABLE t (a INT GENERATED ALWAYS IDENTITY);",
        "CREATE TABLE t (a INT GENERATED ALWAYS AS FOOBAR);",
        "CREATE TABLE t (a INT GENERATED ALWAYS AS IDENTITY (START",
        "CREATE TABLE t (a CHAR(4) FOR FOOBAR DATA);",
        "CREATE TABLE t (a CHAR(4) FOR BIT FOOBAR);",
        "CREATE TABLE t (a CHAR(4) CCSID @);",
        "CREATE TABLE t (a INT WITH FOOBAR);",
        "CREATE TABLE t (a INT WITH DEFAULT);",
        "CREATE TABLE t (a INT WITH DEFAULT INTO);",
        "CREATE TABLE t (a INT REFERENCES p);",
        "CREATE TABLE t (a INT, FOREIGN KEY (a) REFERENCES p);",
        "CREATE TABLE t (a INT) EXTRA",
        NULL};
    for (i = 0; bad_sqls[i] != NULL; ++i) {
      span = az_span_create_from_str(bad_sqls[i]);
      ASSERT_EQ(C_ORM_OK, sql_lex(span, &list));
      rc = sql_parse_table(list, &table, &err_info);
      if (rc == C_ORM_OK) {
        sql_table_free(table);
        ASSERT_EQ(C_ORM_OK, c_orm_system_free(table));
        table = NULL;
      }
      sql_token_list_free(list);
      list = NULL;
    }
  }

  /* 4. parse_sql_ddl realloc OOM when growing >8 tables and sql_lex OOM */
  {
    const char *nine_sql =
        "CREATE TABLE t1 (id INT); CREATE TABLE t2 (id INT); "
        "CREATE TABLE t3 (id INT); CREATE TABLE t4 (id INT); "
        "CREATE TABLE t5 (id INT); CREATE TABLE t6 (id INT); "
        "CREATE TABLE t7 (id INT); CREATE TABLE t8 (id INT); "
        "CREATE TABLE t9 (id INT);";
    int saw_oom = 0;
    for (step = 0; step < 25; ++step) {
      sql_realloc_oom_active = 1;
      sql_realloc_oom_countdown = step;
      tables = NULL;
      n_tables = 0;
      rc = parse_sql_ddl(nine_sql, &tables, &n_tables);
      sql_realloc_oom_active = 0;
      sql_realloc_oom_countdown = -1;
      if (rc == C_ORM_ERROR_MEMORY) {
        saw_oom = 1;
      } else {
        ASSERT_EQ(C_ORM_OK, rc);
        for (i = 0; i < n_tables; ++i) {
          sql_table_free(&tables[i]);
        }
        c_orm_system_free(tables);
      }
    }
    ASSERT_EQ(1, saw_oom);
  }

  /* 5. sql_parse_select & sql_parse_returning edge cases + OOM */
  {
    struct sql_token_list_t empty_tl;
    struct CddCQueryProjection *proj = NULL;
    char long_q[450];

    memset(&empty_tl, 0, sizeof(empty_tl));
    ASSERT_EQ(C_ORM_OK, sql_parse_select(&empty_tl, &proj, NULL));
    cdd_c_query_projection_free((cdd_c_query_projection_t *)proj);
    c_orm_system_free(proj);
    proj = NULL;

    ASSERT_EQ(C_ORM_OK, sql_parse_returning(&empty_tl, &proj, NULL));
    cdd_c_query_projection_free((cdd_c_query_projection_t *)proj);
    c_orm_system_free(proj);
    proj = NULL;

    /* >128 char unqualified & qualified identifier & alias */
    memset(long_q, 'a', sizeof(long_q));
    memcpy(long_q, "SELECT ", 7);
    memcpy(long_q + 145, ", tbl.", 6);
    memcpy(long_q + 290, " AS ", 4);
    long_q[430] = '\0';
    ASSERT_EQ(C_ORM_OK, sql_lex(az_span_create_from_str(long_q), &list));
    ASSERT_EQ(C_ORM_OK, sql_parse_select(list, &proj, &err_info));
    cdd_c_query_projection_free((cdd_c_query_projection_t *)proj);
    c_orm_system_free(proj);
    proj = NULL;

    /* OOM on add_field in sql_parse_select */
    for (step = 0;; ++step) {
      sql_oom_active = 1;
      sql_oom_countdown = step;
      rc = sql_parse_select(list, &proj, &err_info);
      sql_oom_active = 0;
      sql_oom_countdown = -1;
      if (rc == C_ORM_OK) {
        cdd_c_query_projection_free((cdd_c_query_projection_t *)proj);
        c_orm_system_free(proj);
        proj = NULL;
        break;
      }
      ASSERT(step < 50);
    }
    for (step = 0;; ++step) {
      sql_realloc_oom_active = 1;
      sql_realloc_oom_countdown = step;
      rc = sql_parse_select(list, &proj, &err_info);
      sql_realloc_oom_active = 0;
      sql_realloc_oom_countdown = -1;
      if (rc == C_ORM_OK) {
        cdd_c_query_projection_free((cdd_c_query_projection_t *)proj);
        c_orm_system_free(proj);
        proj = NULL;
        break;
      }
      ASSERT(step < 50);
    }
    sql_token_list_free(list);
    list = NULL;

    /* Nested parens, 4-char/8-char/9-char non-FROM/DISTINCT/RETURNING keywords,
     * unclosed aggregate paren, trailing dot, and OOM in sql_parse_returning */
    ASSERT_EQ(C_ORM_OK,
              sql_lex(az_span_create_from_str(
                          "UPDATE u SET a = CHARACTER RETURNING NULL, "
                          "SMALLINT, COALESCE(MAX(a), 0) AS m, t."),
                      &list));
    for (step = 0;; ++step) {
      sql_oom_active = 1;
      sql_oom_countdown = step;
      rc = sql_parse_returning(list, &proj, &err_info);
      sql_oom_active = 0;
      sql_oom_countdown = -1;
      if (rc == C_ORM_OK) {
        cdd_c_query_projection_free((cdd_c_query_projection_t *)proj);
        c_orm_system_free(proj);
        proj = NULL;
        break;
      }
      ASSERT(step < 50);
    }
    sql_token_list_free(list);
    list = NULL;

    /* Unclosed aggregate paren + trailing dot with EOF stripped */
    ASSERT_EQ(C_ORM_OK,
              sql_lex(az_span_create_from_str("SELECT COUNT(a"), &list));
    list->size--;
    ASSERT_EQ(C_ORM_OK, sql_parse_select(list, &proj, NULL));
    cdd_c_query_projection_free((cdd_c_query_projection_t *)proj);
    c_orm_system_free(proj);
    proj = NULL;
    sql_token_list_free(list);
    list = NULL;

    ASSERT_EQ(C_ORM_OK, sql_lex(az_span_create_from_str("SELECT tbl."), &list));
    list->size--;
    ASSERT_EQ(C_ORM_OK, sql_parse_select(list, &proj, NULL));
    cdd_c_query_projection_free((cdd_c_query_projection_t *)proj);
    c_orm_system_free(proj);
    proj = NULL;
    sql_token_list_free(list);
    list = NULL;

    /* Trailing whitespace after SELECT with EOF stripped -> idx >= list->size
     */
    ASSERT_EQ(C_ORM_OK, sql_lex(az_span_create_from_str("SELECT   "), &list));
    list->size--;
    ASSERT_EQ(C_ORM_OK, sql_parse_select(list, &proj, NULL));
    cdd_c_query_projection_free((cdd_c_query_projection_t *)proj);
    c_orm_system_free(proj);
    proj = NULL;
    sql_token_list_free(list);
    list = NULL;

    /* Trailing AS at EOF & 2-char non-AS token & truncated token list */
    ASSERT_EQ(C_ORM_OK,
              sql_lex(az_span_create_from_str("SELECT col xx FROM u;"), &list));
    ASSERT_EQ(C_ORM_OK, sql_parse_select(list, &proj, NULL));
    cdd_c_query_projection_free((cdd_c_query_projection_t *)proj);
    c_orm_system_free(proj);
    proj = NULL;
    sql_token_list_free(list);
    list = NULL;

    ASSERT_EQ(C_ORM_OK,
              sql_lex(az_span_create_from_str("SELECT col AS "), &list));
    ASSERT_EQ(C_ORM_OK, sql_parse_select(list, &proj, NULL));
    cdd_c_query_projection_free((cdd_c_query_projection_t *)proj);
    c_orm_system_free(proj);
    proj = NULL;
    list->size--;
    ASSERT_EQ(C_ORM_OK, sql_parse_select(list, &proj, NULL));
    cdd_c_query_projection_free((cdd_c_query_projection_t *)proj);
    c_orm_system_free(proj);
    proj = NULL;
    sql_token_list_free(list);
    list = NULL;

    /* No SELECT / RETURNING keyword present in non-empty token list */
    ASSERT_EQ(C_ORM_OK,
              sql_lex(az_span_create_from_str("DELETE FROM u;"), &list));
    ASSERT_EQ(C_ORM_OK, sql_parse_select(list, &proj, NULL));
    cdd_c_query_projection_free((cdd_c_query_projection_t *)proj);
    c_orm_system_free(proj);
    proj = NULL;
    ASSERT_EQ(C_ORM_OK, sql_parse_returning(list, &proj, NULL));
    cdd_c_query_projection_free((cdd_c_query_projection_t *)proj);
    c_orm_system_free(proj);
    proj = NULL;
    sql_token_list_free(list);
    list = NULL;
  }

  PASS();
}

/**
 * @brief Tests case-insensitive SQL keywords, line/block comments, backtick and
 * bracket quoted identifiers, and table-level FK local column resolution.
 * @return GREATEST test result.
 */
static enum greatest_test_res
test_sql_case_comments_quotes_and_fk_column(void) {
  static char c_mut_s0[] = "author_fk";
  static char c_mut_s1[] = "syn";
  static char c_mut_s2[] = "authors";
  static char c_mut_s3[] = "aid";
  static char c_mut_s4[] = "orgs";
  static char c_mut_s5[] = "oid";
  static char c_mut_s6[] = "teams";
  static char c_mut_s7[] = "tid";

  const char *ddl =
      "-- leading comment\n"
      "/* block * comment */ create table `posts` (\n"
      "  `id` int, [user]]id] int default null, `slug` varchar(64),\n"
      "  primary key (`id`),\n"
      "  unique (`slug`),\n"
      "  foreign key ([user]]id]) references `users`(`id`)\n"
      "); -- trailing eof comment";
  struct sql_table_t *tables = NULL;
  size_t n_tables = 0;
  c_orm_relation_meta_t *rels = NULL;
  size_t n_rels = 0;
  struct sql_token_list_t *list = NULL;
  struct CddCQueryProjection *proj = NULL;
  struct sql_parse_error_t perr;
  az_span sp;
  int cd;

  ASSERT_EQ(C_ORM_OK, parse_sql_ddl(ddl, &tables, &n_tables));
  ASSERT_EQ(1U, n_tables);
  ASSERT_STR_EQ("posts", tables[0].name);
  ASSERT_EQ(3U, tables[0].n_table_constraints);
  ASSERT_STR_EQ("user]]id", tables[0].table_constraints[2].column_name);

  ASSERT_EQ(C_ORM_OK, c_orm_build_relation_meta(&tables[0], &rels, &n_rels));
  ASSERT_EQ(1U, n_rels);
  ASSERT_STR_EQ("user]]id", rels[0].field_name);
  ASSERT_STR_EQ("user]]id", rels[0].local_key);
  ASSERT_STR_EQ("users", rels[0].target_table);
  ASSERT_STR_EQ("id", rels[0].foreign_key);
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(rels));

  /* Synthetic table constraint branches in c_orm_build_relation_meta */
  {
    struct sql_table_t syn_tbl;
    struct sql_constraint_t syn_tc[3];
    char *col_arr[1];
    col_arr[0] = c_mut_s0;
    memset(&syn_tbl, 0, sizeof(syn_tbl));
    memset(syn_tc, 0, sizeof(syn_tc));
    syn_tbl.name = c_mut_s1;
    syn_tbl.table_constraints = syn_tc;
    syn_tbl.n_table_constraints = 3;

    /* 1: column_name NULL, columns[0] populated */
    syn_tc[0].type = SQL_CONSTRAINT_FOREIGN_KEY;
    syn_tc[0].columns = col_arr;
    syn_tc[0].n_columns = 1;
    syn_tc[0].reference_table = c_mut_s2;
    syn_tc[0].reference_column = c_mut_s3;

    /* 2: column_name NULL, n_columns > 0 but columns NULL -> fallback */
    syn_tc[1].type = SQL_CONSTRAINT_FOREIGN_KEY;
    syn_tc[1].columns = NULL;
    syn_tc[1].n_columns = 1;
    syn_tc[1].reference_table = c_mut_s4;
    syn_tc[1].reference_column = c_mut_s5;

    /* 3: column_name NULL, n_columns == 0 -> fallback */
    syn_tc[2].type = SQL_CONSTRAINT_FOREIGN_KEY;
    syn_tc[2].reference_table = c_mut_s6;
    syn_tc[2].reference_column = c_mut_s7;

    rels = NULL;
    n_rels = 0;
    ASSERT_EQ(C_ORM_OK, c_orm_build_relation_meta(&syn_tbl, &rels, &n_rels));
    ASSERT_EQ(3U, n_rels);
    ASSERT_STR_EQ("author_fk", rels[0].local_key);
    ASSERT_STR_EQ("oid", rels[1].local_key);
    ASSERT_STR_EQ("tid", rels[2].local_key);
    ASSERT_EQ(C_ORM_OK, c_orm_system_free(rels));
  }

  ASSERT_EQ(C_ORM_OK, sql_table_free(&tables[0]));
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(tables));

  /* Case-insensitive select distinct ... as ... from & returning + comments */
  sp = az_span_create_from_str(
      "select distinct `a``b` as [c] from `t` /* unclosed comment *");
  ASSERT_EQ(C_ORM_OK, sql_lex(sp, &list));
  ASSERT_EQ(C_ORM_OK, sql_parse_select(list, &proj, &perr));
  ASSERT_EQ(1U, ((cdd_c_query_projection_t *)proj)->n_fields);
  ASSERT_STR_EQ("c", ((cdd_c_query_projection_t *)proj)->fields[0].name);
  cdd_c_query_projection_free((cdd_c_query_projection_t *)proj);
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(proj));
  ASSERT_EQ(C_ORM_OK, sql_token_list_free(list));

  sp = az_span_create_from_str("insert into t values (1) returning id -");
  ASSERT_EQ(C_ORM_OK, sql_lex(sp, &list));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, sql_parse_table(list, NULL, NULL));
  proj = NULL;
  ASSERT_EQ(C_ORM_OK, sql_parse_returning(list, &proj, &perr));
  cdd_c_query_projection_free((cdd_c_query_projection_t *)proj);
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(proj));
  ASSERT_EQ(C_ORM_OK, sql_token_list_free(list));

  sp = az_span_create_from_str("select 1 /");
  ASSERT_EQ(C_ORM_OK, sql_lex(sp, &list));
  ASSERT_EQ(C_ORM_OK, sql_token_list_free(list));

  sp = az_span_create_from_str("select 1 - / `unclosed");
  ASSERT_EQ(C_ORM_OK, sql_lex(sp, &list));
  ASSERT_EQ(C_ORM_OK, sql_token_list_free(list));

  /* Non-matching 4/6/8/9-len keywords in select/returning/ddl */
  sp = az_span_create_from_str(
      "select into, time, date, decfloat, references from t;");
  ASSERT_EQ(C_ORM_OK, sql_lex(sp, &list));
  proj = NULL;
  ASSERT_EQ(C_ORM_OK, sql_parse_select(list, &proj, &perr));
  cdd_c_query_projection_free((cdd_c_query_projection_t *)proj);
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(proj));
  proj = NULL;
  ASSERT_EQ(C_ORM_OK, sql_parse_returning(list, &proj, &perr));
  cdd_c_query_projection_free((cdd_c_query_projection_t *)proj);
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(proj));
  ASSERT_EQ(C_ORM_OK, sql_token_list_free(list));

  tables = NULL;
  n_tables = 0;
  ASSERT_EQ(
      C_ORM_OK,
      parse_sql_ddl(
          "select 1; delete from t; create table x (id int default time);",
          &tables, &n_tables));
  for (cd = 0; cd < (int)n_tables; ++cd) {
    ASSERT_EQ(C_ORM_OK, sql_table_free(&tables[cd]));
  }
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(tables));

  /* OOM during comment token push (initial realloc at token #0) */
  sql_realloc_oom_active = 1;
  sql_realloc_oom_countdown = 0;
  list = NULL;
  sp = az_span_create_from_str("--line");
  ASSERT_EQ(C_ORM_ERROR_MEMORY, sql_lex(sp, &list));

  sql_realloc_oom_countdown = 0;
  list = NULL;
  sp = az_span_create_from_str("/*blk*/");
  ASSERT_EQ(C_ORM_ERROR_MEMORY, sql_lex(sp, &list));
  sql_realloc_oom_active = 0;
  sql_realloc_oom_countdown = -1;

  /* Semicolon & unknown token push_token OOM + sql_column_partial_free with
   * col->n_constraints > 0 */
  sql_realloc_oom_active = 1;
  sql_realloc_oom_countdown = 0;
  list = NULL;
  sp = az_span_create_from_str(";");
  ASSERT_EQ(C_ORM_ERROR_MEMORY, sql_lex(sp, &list));
  sql_realloc_oom_active = 0;
  sql_realloc_oom_countdown = -1;

  sql_realloc_oom_active = 1;
  sql_realloc_oom_countdown = 0;
  list = NULL;
  sp = az_span_create_from_str("~");
  ASSERT_EQ(C_ORM_ERROR_MEMORY, sql_lex(sp, &list));
  sql_realloc_oom_active = 0;
  sql_realloc_oom_countdown = -1;

  {
    struct sql_table_t *t_bad = NULL;
    list = NULL;
    sp = az_span_create_from_str(
        "CREATE TABLE t (id INT NOT NULL CONSTRAINT 123);");
    ASSERT_EQ(C_ORM_OK, sql_lex(sp, &list));
    ASSERT_NEQ(C_ORM_OK, sql_parse_table(list, &t_bad, &perr));
    sql_token_list_free(list);
    list = NULL;
  }

  PASS();
}

/**
 * @brief SQL test suite runner.
 * @param sql_suite Suite runner function name.
 */
SUITE(sql_suite) {
  static int recursed = 0;
  c_orm_malloc_fn_t old_malloc;
  c_orm_realloc_fn_t old_realloc;
  c_orm_free_fn_t old_free;

  old_malloc = c_orm_malloc;
  old_realloc = c_orm_realloc;
  old_free = c_orm_free;

  c_orm_set_allocators(sql_mock_malloc, sql_mock_realloc, sql_mock_free);

  RUN_TEST(test_sql_lexer_basic);
  RUN_TEST(test_sql_lexer_types);
  RUN_TEST(test_sql_parser_basic);
  RUN_TEST(test_sql_lexer_strings_unknown);
  RUN_TEST(test_sql_parser_foreign_keys_defaults);

  RUN_TEST(test_sql_lexer_oom);
  RUN_TEST(test_sql_parser_errors);
  RUN_TEST(test_sql_parser_step_failures);
  RUN_TEST(test_sql_parser_constraints_capacity);
  RUN_TEST(test_sql_parser_projection_and_returning);
  RUN_TEST(test_sql_lexer_and_cleanup_branches);
  RUN_TEST(test_sql_parser_missing_keys);
  RUN_TEST(test_sql_parser_exhaustive_oom);
  RUN_TEST(test_sql_parser_db2_and_full_branches);
  RUN_TEST(test_sql_case_comments_quotes_and_fk_column);

  c_orm_set_allocators(old_malloc, old_realloc, old_free);

  if (!recursed) {
    recursed = 1;
    greatest_set_test_filter("never_match_filter");
    sql_suite();
    greatest_set_test_filter(NULL);
    recursed = 0;
  }
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
