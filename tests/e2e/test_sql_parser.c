#if defined(__clang__) || defined(__GNUC__)
#endif
/* clang-format off */
#include "test_utils.h"
#include "c_orm_sql.h"
#include "c_orm_api.h"
#include "greatest.h"
#include "query_projection.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

static int oom_countdown = -1;
static int oom_active = 0;

static void test_cleanup_tables(struct sql_table_t **tables_ptr,
                                size_t *n_tables_ptr) {
  if (tables_ptr && *tables_ptr) {
    size_t k;
    for (k = 0; k < *n_tables_ptr; k++) {
      sql_table_C_ORM_FREE(&(*tables_ptr)[k]);
    }
    C_ORM_FREE(*tables_ptr);
    *tables_ptr = NULL;
    *n_tables_ptr = 0;
  }
}

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
static void m_mock_free(void *ptr) { free(ptr); }

TEST test_sql_parser_basic(void) {
  const char *sql = "CREATE TABLE t1 (id INT PRIMARY KEY, name VARCHAR(255) "
                    "NOT NULL DEFAULT 'hello', pid INT REFERENCES p(id) UNIQUE "
                    "NOT NULL PRIMARY KEY DEFAULT 'world');\n"
                    "CREATE TABLE t2 (id CHAR, val DOUBLE, dec DECIMAL, num "
                    "INT DEFAULT 123, b_val BOOLEAN DEFAULT TRUE);";
  struct sql_table_t *tables = NULL;
  size_t n_tables = 0;
  int rc;

  rc = parse_sql_ddl(sql, &tables, &n_tables);
  ASSERT_EQ(0, rc);
  ASSERT_EQ(2, n_tables);
  test_cleanup_tables(&tables, &n_tables);
  PASS();
}

TEST test_sql_parser_oom(void) {
  const char *sql = "CREATE TABLE t1 (id INT PRIMARY KEY, name VARCHAR(255) "
                    "NOT NULL DEFAULT 'hello', pid INT DEFAULT 'world' "
                    "REFERENCES p(id) UNIQUE NOT NULL PRIMARY KEY);";
  struct sql_table_t *tables = NULL;
  size_t n_tables = 0;
  int i;
  for (i = 0; i < 2; i++) {
    oom_active = 1;
    oom_countdown = i;
    {
      c_orm_error_t _err = parse_sql_ddl(sql, &tables, &n_tables);
      /* Expected to fail or partially succeed depending on OOM state */
      (void)_err;
    }
    if (tables) {
      test_cleanup_tables(&tables, &n_tables);
    }
    oom_active = 0;
  }
  PASS();
}

TEST test_sql_lex_oom(void) {
  const char *sqls[] = {",,,,,,,,,,,,,,,,  ",  /* 16 tokens then whitespace */
                        ",,,,,,,,,,,,,,,,123", /* 16 tokens then number */
                        ",,,,,,,,,,,,,,,,'a'", /* 16 tokens then string */
                        ",,,,,,,,,,,,,,,,()",  /* 16 tokens then switch */
                        ",,,,,,,,,,,,,,,,",    /* 16 tokens then EOF */
                        NULL};
  int s, i;
  for (s = 0; sqls[s]; s++) {
    for (i = 0; i < 5; i++) {
      struct sql_token_list_t *list = NULL;
      az_span span = az_span_create_from_str((char *)test_strdup(sqls[s]));
      oom_active = 1;
      oom_countdown = i;
      sql_lex(span, &list);
      oom_active = 0;
      if (list) {
        sql_token_list_free(list);
      }
    }
  }

  /* Also test the very first list alloc failing */
  {
    struct sql_token_list_t *list = NULL;
    az_span span = az_span_create_from_str((char *)test_strdup("a"));
    oom_active = 1;
    oom_countdown = 0;
    sql_lex(span, &list);
    oom_active = 0;
    ASSERT_EQ(NULL, list);
  }

  /* NULL source/out_list */
  {
    az_span span = az_span_create_from_str((char *)test_strdup("a"));
    sql_lex(span, NULL);
    sql_lex(az_span_empty(), NULL);
  }

  PASS();
}

TEST test_sql_parser_errors(void) {
  struct sql_table_t *tables = NULL;
  size_t n_tables = 0;

  /* Lexer errors */
  parse_sql_ddl("CREATE TABLE t1 (name VARCHAR(255) DEFAULT 'unterminated);",
                &tables, &n_tables);
  test_cleanup_tables(&tables, &n_tables);

  /* Parser errors */
  parse_sql_ddl("CREATE;", &tables, &n_tables);
  test_cleanup_tables(&tables, &n_tables);
  parse_sql_ddl("CREATE TABLE;", &tables, &n_tables);
  test_cleanup_tables(&tables, &n_tables);
  parse_sql_ddl("CREATE TABLE t1;", &tables, &n_tables);
  test_cleanup_tables(&tables, &n_tables);
  parse_sql_ddl("CREATE TABLE t1 (;", &tables, &n_tables);
  test_cleanup_tables(&tables, &n_tables);
  parse_sql_ddl("CREATE TABLE t1 (id;", &tables, &n_tables);
  test_cleanup_tables(&tables, &n_tables);
  parse_sql_ddl("CREATE TABLE t1 (id UNKNOWN_TYPE;", &tables, &n_tables);
  test_cleanup_tables(&tables, &n_tables);
  parse_sql_ddl("CREATE TABLE t1 (id TABLE;", &tables, &n_tables);
  test_cleanup_tables(&tables, &n_tables);
  parse_sql_ddl("CREATE TABLE t1 (id VARCHAR;", &tables, &n_tables);
  test_cleanup_tables(&tables, &n_tables);
  parse_sql_ddl("CREATE TABLE t1 (id VARCHAR(;", &tables, &n_tables);
  test_cleanup_tables(&tables, &n_tables);
  parse_sql_ddl("CREATE TABLE t1 (id VARCHAR(255;", &tables, &n_tables);
  test_cleanup_tables(&tables, &n_tables);
  parse_sql_ddl("CREATE TABLE t1 (id INT PRIMARY;", &tables, &n_tables);
  test_cleanup_tables(&tables, &n_tables);
  parse_sql_ddl("CREATE TABLE t1 (id INT NOT;", &tables, &n_tables);
  test_cleanup_tables(&tables, &n_tables);
  parse_sql_ddl("CREATE TABLE t1 (id INT DEFAULT;", &tables, &n_tables);
  test_cleanup_tables(&tables, &n_tables);
  parse_sql_ddl("CREATE TABLE t1 (id INT REFERENCES;", &tables, &n_tables);
  test_cleanup_tables(&tables, &n_tables);
  parse_sql_ddl("CREATE TABLE t1 (id INT REFERENCES p;", &tables, &n_tables);
  test_cleanup_tables(&tables, &n_tables);
  parse_sql_ddl("CREATE TABLE t1 (id INT REFERENCES p(;", &tables, &n_tables);
  test_cleanup_tables(&tables, &n_tables);
  parse_sql_ddl("CREATE TABLE t1 (id INT REFERENCES p(id;", &tables, &n_tables);
  test_cleanup_tables(&tables, &n_tables);

  /* Call sql_parse_table directly to hit unreachable errors */
  {
    struct sql_token_list_t list;
    struct sql_token_t toks[10];
    struct sql_table_t *tbl = NULL;
    struct sql_parse_error_t err;
    memset(&list, 0, sizeof(list));
    memset(&toks, 0, sizeof(toks));
    list.tokens = toks;

    /* Missing CREATE */
    list.size = 0;
    sql_parse_table(&list, &tbl, &err);

    list.size = 1;
    toks[0].kind = SQL_TOKEN_KEYWORD;
    toks[0].start = "DROP";
    toks[0].length = 4;
    sql_parse_table(&list, &tbl, &err);

    /* Missing TABLE */
    toks[0].start = "CREATE";
    toks[0].length = 6;
    list.size = 2;
    toks[1].kind = SQL_TOKEN_KEYWORD;
    toks[1].start = "INDEX";
    toks[1].length = 5;
    sql_parse_table(&list, &tbl, &err);

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
    sql_parse_table(&list, &tbl, &err);

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
    sql_parse_table(&list, &tbl, &err);
  }

  parse_sql_ddl("CREATE TABLE dummy (\n"
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
  test_cleanup_tables(&tables, &n_tables);
  parse_sql_ddl("CREATE TABLE t1 (id INT, PRIMARY KEY;", &tables, &n_tables);
  test_cleanup_tables(&tables, &n_tables);
  parse_sql_ddl("CREATE TABLE t1 (id INT, FOREIGN KEY;", &tables, &n_tables);
  test_cleanup_tables(&tables, &n_tables);
  parse_sql_ddl("CREATE TABLE t1 (id INT, UNIQUE;", &tables, &n_tables);
  test_cleanup_tables(&tables, &n_tables);
  parse_sql_ddl("CREATE TABLE t1 ();", &tables, &n_tables);
  test_cleanup_tables(&tables, &n_tables);

  /* NULL tests */
  parse_sql_ddl(NULL, &tables, &n_tables);
  test_cleanup_tables(&tables, &n_tables);
  parse_sql_ddl("CREATE TABLE t1;", NULL, &n_tables);
  parse_sql_ddl("CREATE TABLE t1;", &tables, NULL);
  test_cleanup_tables(&tables, &n_tables);

  /* sql_table_free with table_constraints */
  {
    struct sql_table_t t;
    memset(&t, 0, sizeof(t));
    t.n_table_constraints = 1;
    t.table_constraints = C_ORM_MALLOC(sizeof(struct sql_constraint_t));
    memset(t.table_constraints, 0, sizeof(struct sql_constraint_t));
    c_orm_strdup("ref_tbl", &t.table_constraints[0].reference_table);
    c_orm_strdup("ref_col", &t.table_constraints[0].reference_column);
    c_orm_strdup("def_val", &t.table_constraints[0].default_value);
    sql_table_C_ORM_FREE(&t);
  }

  /* stubs */
  {
    struct CddCQueryProjection *proj = NULL;
    struct sql_parse_error_t err;
    sql_parse_select(NULL, &proj, &err);
    cdd_c_query_projection_free(proj);
    free(proj);
    proj = NULL;
    sql_parse_returning(NULL, &proj, &err);
    cdd_c_query_projection_free(proj);
    free(proj);
    proj = NULL;
  }

  PASS();
}

TEST test_sql_parser_table_constraints(void) {
  const char *sql = "CREATE TABLE multi_pk ("
                    "  id INT, "
                    "  tenant_id INT, "
                    "  ref_id INT, "
                    "  PRIMARY KEY (id, tenant_id), "
                    "  FOREIGN KEY (ref_id) REFERENCES other_table(id), "
                    "  UNIQUE (id) "
                    ");";
  struct sql_table_t *tables = NULL;
  size_t n_tables = 0;
  int rc;

  rc = parse_sql_ddl(sql, &tables, &n_tables);
  ASSERT_EQ(0, rc);
  ASSERT_EQ(1, n_tables);
  if (tables) {
    struct sql_table_t *tbl = &tables[0];
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

SUITE(sql_parser_suite) {
  void *(*old_malloc)(size_t) = c_orm_malloc;
  void *(*old_realloc)(void *, size_t) = c_orm_realloc;
  void (*old_free)(void *) = c_orm_free;

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

#if defined(__clang__) || defined(__GNUC__)
#endif
