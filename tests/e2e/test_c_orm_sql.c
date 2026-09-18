#if defined(__clang__) || defined(__GNUC__)
#endif
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
#include "query_projection.h"
#include <greatest.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

static int sql_oom_active = 0;
static int sql_oom_countdown = -1;
static int sql_realloc_oom_active = 0;
static int sql_realloc_oom_countdown = -1;

/**
 * @brief Mock malloc callback for OOM testing.
 * @param size Allocation size in bytes.
 * @return Pointer to allocated memory, or NULL on OOM.
 */
static void *sql_mock_malloc(size_t size) {
  if (sql_oom_active) {
    if (sql_oom_countdown == 0) {
      sql_oom_countdown--;
      return NULL;
    }
    sql_oom_countdown--;
  }
  return malloc(size);
}

/**
 * @brief Mock realloc callback for OOM testing.
 * @param ptr Existing pointer.
 * @param size Allocation size in bytes.
 * @return Pointer to reallocated memory, or NULL on OOM.
 */
static void *sql_mock_realloc(void *ptr, size_t size) {
  if (sql_realloc_oom_active) {
    if (sql_realloc_oom_countdown == 0) {
      sql_realloc_oom_countdown--;
      return NULL;
    }
    sql_realloc_oom_countdown--;
  }
  return realloc(ptr, size);
}

/**
 * @brief Mock free callback.
 * @param ptr Pointer to free.
 */
static void sql_mock_free(void *ptr) { free(ptr); }

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

  span = az_span_create_from_str((char *)sql);
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

  span = az_span_create_from_str((char *)sql);
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

  span = az_span_create_from_str((char *)sql);
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
  C_ORM_FREE(table);
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

  span = az_span_create_from_str((char *)sql);
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
  span = az_span_create_from_str((char *)sql);
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
  struct sql_table_t *tables;
  size_t n_tables;
  size_t i;
  c_orm_error_t rc;

  tables = NULL;
  n_tables = 0;
  rc = parse_sql_ddl(sql, &tables, &n_tables);
  ASSERT_EQ(C_ORM_OK, rc);

  if (tables != NULL) {
    for (i = 0; i < n_tables; ++i) {
      sql_table_C_ORM_FREE(&tables[i]);
    }
    C_ORM_FREE(tables);
  }

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
      "CREATE TABLE t (id INT, CHECK (id > 0));",
      NULL};
  size_t i;
  az_span span;
  struct sql_token_list_t *list;
  struct sql_table_t *table;
  struct sql_parse_error_t err_info;
  c_orm_error_t rc;

  for (i = 0; err_sqls[i] != NULL; ++i) {
    span = az_span_create_from_str((char *)err_sqls[i]);
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
      span = az_span_create_from_str((char *)sqls[s]);
      list = NULL;
      table = NULL;

      rc = sql_lex(span, &list);
      ASSERT_EQ(C_ORM_OK, rc);

      c_orm_parser_set_fail(i);
      rc = sql_parse_table(list, &table, &err_info);
      c_orm_parser_set_fail(-1);

      if (rc == C_ORM_OK && table != NULL) {
        sql_table_C_ORM_FREE(table);
        C_ORM_FREE(table);
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

  span = az_span_create_from_str((char *)sql);
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
  C_ORM_FREE(table);
  table = NULL;

  /* Realloc failure during capacity expansion */
  for (i = 0; i < 5; ++i) {
    sql_realloc_oom_active = 1;
    sql_realloc_oom_countdown = i;
    rc = sql_parse_table(list, &table, &err_info);
    if (rc == C_ORM_OK && table != NULL) {
      sql_table_C_ORM_FREE(table);
      C_ORM_FREE(table);
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
  C_ORM_FREE(proj);
  proj = NULL;

  rc = sql_parse_returning(NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = sql_parse_returning(NULL, &proj, &err_info);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT(proj != NULL);
  cdd_c_query_projection_free((cdd_c_query_projection_t *)proj);
  C_ORM_FREE(proj);
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
  if (rc == C_ORM_OK && tbl != NULL) {
    sql_table_C_ORM_FREE(tbl);
    C_ORM_FREE(tbl);
  }
  sql_token_list_free(empty_list);
  empty_list = NULL;

  /* sql_token_list_free branches */
  sql_token_list_free(NULL);

  empty_list =
      (struct sql_token_list_t *)malloc(sizeof(struct sql_token_list_t));
  if (empty_list != NULL) {
    memset(empty_list, 0, sizeof(*empty_list));
    sql_token_list_free(empty_list);
  }

  /* sql_table_C_ORM_FREE branches (NULL table, and NULL column
   * name/constraints) */
  sql_table_C_ORM_FREE(NULL);

  memset(&empty_table, 0, sizeof(empty_table));
  dummy_cols = (struct sql_column_t *)malloc(2 * sizeof(struct sql_column_t));
  if (dummy_cols != NULL) {
    memset(dummy_cols, 0, 2 * sizeof(struct sql_column_t));
    dummy_cols[0].constraints =
        (struct sql_constraint_t *)malloc(sizeof(struct sql_constraint_t));
    if (dummy_cols[0].constraints != NULL) {
      memset(dummy_cols[0].constraints, 0, sizeof(struct sql_constraint_t));
      dummy_cols[0].constraints[0].columns = (char **)malloc(sizeof(char *));
      if (dummy_cols[0].constraints[0].columns != NULL) {
        dummy_cols[0].constraints[0].columns[0] = NULL;
        dummy_cols[0].constraints[0].n_columns = 1;
      }
      dummy_cols[0].n_constraints = 1;
    }
    dummy_cols[1].constraints = NULL;
    dummy_cols[1].n_constraints = 0;
    empty_table.columns = dummy_cols;
    empty_table.n_columns = 2;
    sql_table_C_ORM_FREE(&empty_table);
    empty_table.columns = NULL;
    empty_table.n_columns = 0;
  }

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
  if (tables != NULL) {
    C_ORM_FREE(tables);
    tables = NULL;
  }

  /* parse_sql_ddl with table failing parse */
  rc = parse_sql_ddl("CREATE TABLE t (invalid_syntax);", &tables, &n_tables);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ(0, n_tables);
  if (tables != NULL) {
    C_ORM_FREE(tables);
    tables = NULL;
  }

  /* parse_sql_ddl with multiple tables */
  rc = parse_sql_ddl("CREATE TABLE t1 (id INT); CREATE TABLE t2 (name TEXT);",
                     &tables, &n_tables);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ(2, n_tables);
  if (tables != NULL) {
    for (i = 0; i < n_tables; ++i) {
      sql_table_C_ORM_FREE(&tables[i]);
    }
    C_ORM_FREE(tables);
    tables = NULL;
  }

  /* parse_sql_ddl OOM allocating out_tables */
  sql_oom_active = 1;
  sql_oom_countdown = 1;
  tmp_p = sql_mock_malloc(10);
  ASSERT(tmp_p != NULL);
  free(tmp_p);
  tmp_p = sql_mock_malloc(10);
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
 * @brief SQL test suite runner.
 */
SUITE(sql_suite) {
  void *(*old_malloc)(size_t);
  void *(*old_realloc)(void *, size_t);
  void (*old_free)(void *);

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

  c_orm_set_allocators(old_malloc, old_realloc, old_free);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#if defined(__clang__) || defined(__GNUC__)
#endif
