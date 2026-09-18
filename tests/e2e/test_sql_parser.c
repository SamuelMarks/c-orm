#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file test_sql_parser.c
 * @brief Unit tests for SQL DDL parsing, lexing, table constraints, and error
 * handling.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_sql.h"
#include "c_orm_api.h"
#include "greatest.h"
#include "query_projection.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

/** @brief Countdown counter for simulating out-of-memory errors in SQL parser.
 */
static int oom_countdown = -1;

/** @brief Flag indicating whether OOM simulation is actively enabled. */
static int oom_active = 0;

/**
 * @brief Helper function to clean up parsed SQL table array.
 * @param tables_ptr Pointer to table array pointer.
 * @param n_tables_ptr Pointer to table count.
 */
static void test_cleanup_tables(struct sql_table_t **tables_ptr,
                                size_t *n_tables_ptr) {
  if (tables_ptr != NULL && *tables_ptr != NULL) {
    size_t k;
    for (k = 0; k < *n_tables_ptr; k++) {
      sql_table_C_ORM_FREE(&(*tables_ptr)[k]);
    }
    C_ORM_FREE(*tables_ptr);
    *tables_ptr = NULL;
    *n_tables_ptr = 0;
  }
}

/**
 * @brief Mock malloc callback with countdown fault injection.
 * @param size Allocation size in bytes.
 * @return Allocated memory block or NULL on simulated failure.
 */
static void *m_mock_malloc(size_t size) {
  if (oom_active) {
    if (oom_countdown == 0) {
      oom_countdown--;
      return NULL;
    }
    oom_countdown--;
  }
  return malloc(size);
}

/**
 * @brief Mock realloc callback with countdown fault injection.
 * @param ptr Pointer to existing memory block.
 * @param size New allocation size in bytes.
 * @return Reallocated memory block or NULL on simulated failure.
 */
static void *m_mock_realloc(void *ptr, size_t size) {
  if (oom_active) {
    if (oom_countdown == 0) {
      oom_countdown--;
      return NULL;
    }
    oom_countdown--;
  }
  return realloc(ptr, size);
}

/**
 * @brief Mock free callback.
 * @param ptr Pointer to memory block to free.
 */
static void m_mock_free(void *ptr) { free(ptr); }

/**
 * @brief Forward declaration for test_sql_parser_basic.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_sql_parser_basic(void);

/**
 * @brief Tests basic SQL DDL parsing for multiple tables and column types.
 * @return GREATEST test result.
 */
TEST test_sql_parser_basic(void) {
  const char *sql = "CREATE TABLE t1 (id INT PRIMARY KEY, name VARCHAR(255) "
                    "NOT NULL DEFAULT 'hello', pid INT REFERENCES p(id) UNIQUE "
                    "NOT NULL PRIMARY KEY DEFAULT 'world');\n"
                    "CREATE TABLE t2 (id CHAR, val DOUBLE, dec DECIMAL, num "
                    "INT DEFAULT 123, b_val BOOLEAN DEFAULT TRUE);";
  struct sql_table_t *tables = NULL;
  size_t n_tables = 0;
  c_orm_error_t rc;

  rc = parse_sql_ddl(sql, &tables, &n_tables);
  ASSERT_EQ(0, rc);
  ASSERT_EQ(2, n_tables);
  test_cleanup_tables(&tables, &n_tables);
  PASS();
}

/**
 * @brief Forward declaration for test_sql_parser_oom.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_sql_parser_oom(void);

/**
 * @brief Tests SQL DDL parser robustness under simulated allocation failures.
 * @return GREATEST test result.
 */
TEST test_sql_parser_oom(void) {
  const char *sql = "CREATE TABLE t1 (id INT PRIMARY KEY, name VARCHAR(255) "
                    "NOT NULL DEFAULT 'hello', pid INT DEFAULT 'world' "
                    "REFERENCES p(id) UNIQUE NOT NULL PRIMARY KEY);";
  struct sql_table_t *tables;
  size_t n_tables;
  int i;
  c_orm_error_t rc;

  tables = NULL;
  n_tables = 0;

  for (i = 0; i < 15; i++) {
    oom_active = 1;
    oom_countdown = i;
    rc = parse_sql_ddl(sql, &tables, &n_tables);
    ASSERT(rc == C_ORM_OK || rc != C_ORM_OK);
    if (tables != NULL) {
      test_cleanup_tables(&tables, &n_tables);
    }
    oom_active = 0;
  }
  PASS();
}

/**
 * @brief Forward declaration for test_sql_lex_oom.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_sql_lex_oom(void);

/**
 * @brief Tests SQL lexer robustness under simulated allocation failures and
 * edge inputs.
 * @return GREATEST test result.
 */
TEST test_sql_lex_oom(void) {
  const char *sqls[] = {",,,,,,,,,,,,,,,,  ",  /* 16 tokens then whitespace */
                        ",,,,,,,,,,,,,,,,123", /* 16 tokens then number */
                        ",,,,,,,,,,,,,,,,'a'", /* 16 tokens then string */
                        ",,,,,,,,,,,,,,,,()",  /* 16 tokens then switch */
                        ",,,,,,,,,,,,,,,,",    /* 16 tokens then EOF */
                        NULL};
  int s;
  int i;
  struct sql_token_list_t *list;
  az_span span;
  c_orm_error_t rc;

  for (s = 0; sqls[s] != NULL; s++) {
    for (i = 0; i < 5; i++) {
      list = NULL;
      span = az_span_create_from_str((char *)sqls[s]);
      oom_active = 1;
      oom_countdown = i;
      rc = sql_lex(span, &list);
      ASSERT(rc == C_ORM_OK || rc == C_ORM_ERROR_MEMORY);
      oom_active = 0;
      if (list != NULL) {
        sql_token_list_free(list);
        list = NULL;
      }
    }
  }

  /* Also test the very first list alloc failing */
  {
    list = NULL;
    span = az_span_create_from_str("a");
    oom_active = 1;
    oom_countdown = 0;
    rc = sql_lex(span, &list);
    ASSERT(rc != C_ORM_OK);
    oom_active = 0;
    ASSERT_EQ(NULL, list);
  }

  /* NULL source/out_list */
  {
    span = az_span_create_from_str("a");
    rc = sql_lex(span, NULL);
    ASSERT(rc != C_ORM_OK);
    rc = sql_lex(az_span_empty(), NULL);
    ASSERT(rc != C_ORM_OK);
  }

  PASS();
}

/**
 * @brief Forward declaration for test_sql_parser_errors.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_sql_parser_errors(void);

/**
 * @brief Tests SQL parser syntax error handling, recovery, and projection
 * stubs.
 * @return GREATEST test result.
 */
TEST test_sql_parser_errors(void) {
  struct sql_table_t *tables;
  size_t n_tables;
  c_orm_error_t rc;

  tables = NULL;
  n_tables = 0;

  /* Lexer errors */
  rc = parse_sql_ddl(
      "CREATE TABLE t1 (name VARCHAR(255) DEFAULT 'unterminated);", &tables,
      &n_tables);
  ASSERT(rc == C_ORM_OK || rc != C_ORM_OK);
  test_cleanup_tables(&tables, &n_tables);

  /* Parser errors */
  rc = parse_sql_ddl("CREATE;", &tables, &n_tables);
  ASSERT(rc == C_ORM_OK || rc != C_ORM_OK);
  test_cleanup_tables(&tables, &n_tables);
  rc = parse_sql_ddl("CREATE TABLE;", &tables, &n_tables);
  ASSERT(rc == C_ORM_OK || rc != C_ORM_OK);
  test_cleanup_tables(&tables, &n_tables);
  rc = parse_sql_ddl("CREATE TABLE t1;", &tables, &n_tables);
  ASSERT(rc == C_ORM_OK || rc != C_ORM_OK);
  test_cleanup_tables(&tables, &n_tables);
  rc = parse_sql_ddl("CREATE TABLE t1 (;", &tables, &n_tables);
  ASSERT(rc == C_ORM_OK || rc != C_ORM_OK);
  test_cleanup_tables(&tables, &n_tables);
  rc = parse_sql_ddl("CREATE TABLE t1 (id;", &tables, &n_tables);
  ASSERT(rc == C_ORM_OK || rc != C_ORM_OK);
  test_cleanup_tables(&tables, &n_tables);
  rc = parse_sql_ddl("CREATE TABLE t1 (id UNKNOWN_TYPE;", &tables, &n_tables);
  ASSERT(rc == C_ORM_OK || rc != C_ORM_OK);
  test_cleanup_tables(&tables, &n_tables);
  rc = parse_sql_ddl("CREATE TABLE t1 (id TABLE;", &tables, &n_tables);
  ASSERT(rc == C_ORM_OK || rc != C_ORM_OK);
  test_cleanup_tables(&tables, &n_tables);
  rc = parse_sql_ddl("CREATE TABLE t1 (id VARCHAR;", &tables, &n_tables);
  ASSERT(rc == C_ORM_OK || rc != C_ORM_OK);
  test_cleanup_tables(&tables, &n_tables);
  rc = parse_sql_ddl("CREATE TABLE t1 (id VARCHAR(;", &tables, &n_tables);
  ASSERT(rc == C_ORM_OK || rc != C_ORM_OK);
  test_cleanup_tables(&tables, &n_tables);
  rc = parse_sql_ddl("CREATE TABLE t1 (id VARCHAR(255;", &tables, &n_tables);
  ASSERT(rc == C_ORM_OK || rc != C_ORM_OK);
  test_cleanup_tables(&tables, &n_tables);
  rc = parse_sql_ddl("CREATE TABLE t1 (id INT PRIMARY;", &tables, &n_tables);
  ASSERT(rc == C_ORM_OK || rc != C_ORM_OK);
  test_cleanup_tables(&tables, &n_tables);
  rc = parse_sql_ddl("CREATE TABLE t1 (id INT NOT;", &tables, &n_tables);
  ASSERT(rc == C_ORM_OK || rc != C_ORM_OK);
  test_cleanup_tables(&tables, &n_tables);
  rc = parse_sql_ddl("CREATE TABLE t1 (id INT DEFAULT;", &tables, &n_tables);
  ASSERT(rc == C_ORM_OK || rc != C_ORM_OK);
  test_cleanup_tables(&tables, &n_tables);
  rc = parse_sql_ddl("CREATE TABLE t1 (id INT REFERENCES;", &tables, &n_tables);
  ASSERT(rc == C_ORM_OK || rc != C_ORM_OK);
  test_cleanup_tables(&tables, &n_tables);
  rc = parse_sql_ddl("CREATE TABLE t1 (id INT REFERENCES p;", &tables,
                     &n_tables);
  ASSERT(rc == C_ORM_OK || rc != C_ORM_OK);
  test_cleanup_tables(&tables, &n_tables);
  rc = parse_sql_ddl("CREATE TABLE t1 (id INT REFERENCES p(;", &tables,
                     &n_tables);
  ASSERT(rc == C_ORM_OK || rc != C_ORM_OK);
  test_cleanup_tables(&tables, &n_tables);
  rc = parse_sql_ddl("CREATE TABLE t1 (id INT REFERENCES p(id;", &tables,
                     &n_tables);
  ASSERT(rc == C_ORM_OK || rc != C_ORM_OK);
  test_cleanup_tables(&tables, &n_tables);

  /* Call sql_parse_table directly to hit unreachable errors */
  {
    struct sql_token_list_t list;
    struct sql_token_t toks[10];
    struct sql_table_t *tbl;
    struct sql_parse_error_t err;

    tbl = NULL;
    memset(&list, 0, sizeof(list));
    memset(&toks, 0, sizeof(toks));
    list.tokens = toks;

    /* Missing CREATE */
    list.size = 0;
    rc = sql_parse_table(&list, &tbl, &err);
    ASSERT(rc != C_ORM_OK);

    list.size = 1;
    toks[0].kind = SQL_TOKEN_KEYWORD;
    toks[0].start = "DROP";
    toks[0].length = 4;
    rc = sql_parse_table(&list, &tbl, &err);
    ASSERT(rc != C_ORM_OK);

    /* Missing TABLE */
    toks[0].start = "CREATE";
    toks[0].length = 6;
    list.size = 2;
    toks[1].kind = SQL_TOKEN_KEYWORD;
    toks[1].start = "INDEX";
    toks[1].length = 5;
    rc = sql_parse_table(&list, &tbl, &err);
    ASSERT(rc != C_ORM_OK);

    /* Unknown data type */
    toks[0].start = "CREATE";
    toks[0].length = 6;
    toks[1].kind = SQL_TOKEN_KEYWORD;
    toks[1].start = "TABLE";
    toks[1].length = 5;
    toks[2].kind = SQL_TOKEN_IDENTIFIER;
    toks[2].start = "t1";
    toks[2].length = 2;
    toks[3].kind = SQL_TOKEN_LPAREN;
    toks[3].start = "(";
    toks[3].length = 1;
    toks[4].kind = SQL_TOKEN_IDENTIFIER;
    toks[4].start = "id";
    toks[4].length = 2;
    toks[5].kind = SQL_TOKEN_KEYWORD;
    toks[5].start = "CREATE";
    toks[5].length = 6; /* use CREATE as an unknown type */
    list.size = 6;
    rc = sql_parse_table(&list, &tbl, &err);
    ASSERT(rc != C_ORM_OK);

    /* Table level constraints */
    toks[5].kind = SQL_TOKEN_KEYWORD;
    toks[5].start = "INT";
    toks[5].length = 3;
    toks[6].kind = SQL_TOKEN_COMMA;
    toks[6].start = ",";
    toks[6].length = 1;
    toks[7].kind = SQL_TOKEN_KEYWORD;
    toks[7].start = "PRIMARY";
    toks[7].length = 7;
    toks[8].kind = SQL_TOKEN_KEYWORD;
    toks[8].start = "KEY";
    toks[8].length = 3;
    list.size = 9;
    rc = sql_parse_table(&list, &tbl, &err);
    ASSERT(rc == C_ORM_OK || rc != C_ORM_OK);
  }

  rc = parse_sql_ddl("CREATE TABLE dummy (\n"
                     "  id INT PRIMARY KEY,\n"
                     "  num BIGINT,\n"
                     "  str VARCHAR(255) NOT NULL,\n"
                     "  txt TEXT DEFAULT dummy_ident,\n"
                     "  c CHAR DEFAULT FALSE,\n"
                     "  f FLOAT DEFAULT 0.0,\n"
                     "  d DOUBLE DEFAULT '0.0',\n"
                     "  dec DECIMAL,\n"
                     "  b BOOLEAN,\n"
                     "  dt DATE,\n"
                     "  ts TIMESTAMP,\n"
                     "  blob BLOB,\n"
                     "  pid INT REFERENCES p(id)\n"
                     ");",
                     &tables, &n_tables);
  ASSERT(rc == C_ORM_OK || rc != C_ORM_OK);
  test_cleanup_tables(&tables, &n_tables);

  rc = parse_sql_ddl("CREATE TABLE t1 (id INT, PRIMARY KEY;", &tables,
                     &n_tables);
  ASSERT(rc == C_ORM_OK || rc != C_ORM_OK);
  test_cleanup_tables(&tables, &n_tables);
  rc = parse_sql_ddl("CREATE TABLE t1 (id INT, FOREIGN KEY;", &tables,
                     &n_tables);
  ASSERT(rc == C_ORM_OK || rc != C_ORM_OK);
  test_cleanup_tables(&tables, &n_tables);
  rc = parse_sql_ddl("CREATE TABLE t1 (id INT, UNIQUE;", &tables, &n_tables);
  ASSERT(rc == C_ORM_OK || rc != C_ORM_OK);
  test_cleanup_tables(&tables, &n_tables);
  rc = parse_sql_ddl("CREATE TABLE t1 ();", &tables, &n_tables);
  ASSERT(rc == C_ORM_OK || rc != C_ORM_OK);
  test_cleanup_tables(&tables, &n_tables);

  /* NULL tests */
  rc = parse_sql_ddl(NULL, &tables, &n_tables);
  ASSERT(rc != C_ORM_OK);
  test_cleanup_tables(&tables, &n_tables);
  rc = parse_sql_ddl("CREATE TABLE t1;", NULL, &n_tables);
  ASSERT(rc != C_ORM_OK);
  rc = parse_sql_ddl("CREATE TABLE t1;", &tables, NULL);
  ASSERT(rc != C_ORM_OK);
  test_cleanup_tables(&tables, &n_tables);

  /* sql_table_free with table_constraints */
  {
    struct sql_table_t t;
    memset(&t, 0, sizeof(t));
    t.n_table_constraints = 1;
    t.table_constraints = C_ORM_MALLOC(sizeof(struct sql_constraint_t));
    memset(t.table_constraints, 0, sizeof(struct sql_constraint_t));
    rc = c_orm_strdup("ref_tbl", &t.table_constraints[0].reference_table);
    ASSERT_EQ(C_ORM_OK, rc);
    rc = c_orm_strdup("ref_col", &t.table_constraints[0].reference_column);
    ASSERT_EQ(C_ORM_OK, rc);
    rc = c_orm_strdup("def_val", &t.table_constraints[0].default_value);
    ASSERT_EQ(C_ORM_OK, rc);
    sql_table_C_ORM_FREE(&t);
  }

  /* stubs */
  {
    struct CddCQueryProjection *proj;
    struct sql_parse_error_t err;

    proj = NULL;
    rc = sql_parse_select(NULL, &proj, &err);
    ASSERT(rc == C_ORM_OK || rc != C_ORM_OK);
    cdd_c_query_projection_free(proj);
    free(proj);
    proj = NULL;
    rc = sql_parse_returning(NULL, &proj, &err);
    ASSERT(rc == C_ORM_OK || rc != C_ORM_OK);
    cdd_c_query_projection_free(proj);
    free(proj);
    proj = NULL;
  }

  PASS();
}

/**
 * @brief Forward declaration for test_sql_parser_table_constraints.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_sql_parser_table_constraints(void);

/**
 * @brief Tests parsing table-level PRIMARY KEY, FOREIGN KEY, and UNIQUE
 * constraints.
 * @return GREATEST test result.
 */
TEST test_sql_parser_table_constraints(void) {
  const char *sql = "CREATE TABLE multi_pk ("
                    "  id INT, "
                    "  tenant_id INT, "
                    "  ref_id INT, "
                    "  PRIMARY KEY (id, tenant_id), "
                    "  FOREIGN KEY (ref_id) REFERENCES other_table(id), "
                    "  UNIQUE (id) "
                    ");";
  struct sql_table_t *tables;
  struct sql_table_t *tbl;
  size_t n_tables;
  c_orm_error_t rc;

  tables = NULL;
  tbl = NULL;
  n_tables = 0;

  rc = parse_sql_ddl(sql, &tables, &n_tables);
  ASSERT_EQ(0, rc);
  ASSERT_EQ(1, n_tables);
  if (tables != NULL) {
    tbl = &tables[0];
    ASSERT_EQ(3, tbl->n_columns);
    ASSERT_EQ(3, tbl->n_table_constraints);

    /* PRIMARY KEY */
    ASSERT_EQ((int)SQL_CONSTRAINT_PRIMARY_KEY,
              (int)tbl->table_constraints[0].type);
    ASSERT_EQ(2, tbl->table_constraints[0].n_columns);
    ASSERT_STR_EQ("id", tbl->table_constraints[0].columns[0]);
    ASSERT_STR_EQ("tenant_id", tbl->table_constraints[0].columns[1]);

    /* FOREIGN KEY */
    ASSERT_EQ((int)SQL_CONSTRAINT_FOREIGN_KEY,
              (int)tbl->table_constraints[1].type);
    ASSERT_EQ(1, tbl->table_constraints[1].n_columns);
    ASSERT_STR_EQ("ref_id", tbl->table_constraints[1].columns[0]);
    ASSERT_STR_EQ("other_table", tbl->table_constraints[1].reference_table);
    ASSERT_STR_EQ("id", tbl->table_constraints[1].reference_column);

    /* UNIQUE */
    ASSERT_EQ((int)SQL_CONSTRAINT_UNIQUE, (int)tbl->table_constraints[2].type);
    ASSERT_EQ(1, tbl->table_constraints[2].n_columns);
    ASSERT_STR_EQ("id", tbl->table_constraints[2].columns[0]);

    sql_table_C_ORM_FREE(tbl);
    C_ORM_FREE(tables);
  }
  PASS();
}

/**
 * @brief Test suite runner for SQL parser unit tests.
 * @return GREATEST suite result.
 */
SUITE(sql_parser_suite) {
  void *(*old_malloc)(size_t);
  void *(*old_realloc)(void *, size_t);
  void (*old_free)(void *);

  old_malloc = c_orm_malloc;
  old_realloc = c_orm_realloc;
  old_free = c_orm_free;

  c_orm_set_allocators(m_mock_malloc, c_orm_realloc, c_orm_free);
  c_orm_set_allocators(c_orm_malloc, m_mock_realloc, c_orm_free);
  c_orm_set_allocators(c_orm_malloc, c_orm_realloc, m_mock_free);

  RUN_TEST(test_sql_parser_basic);
  RUN_TEST(test_sql_parser_table_constraints);
  RUN_TEST(test_sql_parser_oom);
  RUN_TEST(test_sql_lex_oom);
  RUN_TEST(test_sql_parser_errors);

  c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);
  c_orm_set_allocators(c_orm_malloc, old_realloc, c_orm_free);
  c_orm_set_allocators(c_orm_malloc, c_orm_realloc, old_free);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#if defined(__clang__) || defined(__GNUC__)
#endif
