#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file test_c_orm_sql_extra.c
 * @brief Extra unit tests for SQL parser OOM and edge cases.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_sql.h"
#include <greatest.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

static int g_malloc_fail = 0;
static int g_malloc_count = 0;
static int g_malloc_target = -1;

/**
 * @brief Mock malloc callback with countdown target failure.
 * @param size Size in bytes.
 * @return Allocated pointer or NULL.
 */
static void *mock_malloc_fail(size_t size) {
  if (g_malloc_fail) {
    if (g_malloc_target == g_malloc_count++) {
      return NULL;
    }
  }
  return malloc(size);
}

/**
 * @brief Mock realloc callback with countdown target failure.
 * @param ptr Existing pointer.
 * @param size Size in bytes.
 * @return Reallocated pointer or NULL.
 */
static void *mock_realloc_fail(void *ptr, size_t size) {
  if (g_malloc_fail) {
    if (g_malloc_target == g_malloc_count++) {
      return NULL;
    }
  }
  return realloc(ptr, size);
}

/**
 * @brief Mock free callback.
 * @param p Pointer to free.
 */
static void mock_free(void *p) { free(p); }

/**
 * @brief Forward declaration for OOM lexer test implementation.
 * @return GREATEST test result.
 */
enum greatest_test_res test_sql_lexer_oom_impl(void);

/**
 * @brief Implementation of SQL lexer OOM and stress test.
 * @return GREATEST test result.
 */
enum greatest_test_res test_sql_lexer_oom_impl(void) {
  void *(*old_malloc)(size_t);
  void *(*old_realloc)(void *, size_t);
  void (*old_free)(void *);
  int i;
  int sql_idx;
  az_span span;
  struct sql_token_list_t *list;
  struct sql_table_t *table;
  struct sql_table_t *tables;
  struct sql_parse_error_t err_info;
  size_t n_tables;
  size_t j;
  c_orm_error_t rc;

  const char *sqls[] = {
      "CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(255) NOT NULL, "
      "role_id BIGINT REFERENCES roles(id), is_active BOOLEAN DEFAULT true);",
      "CREATE TABLE t (id INT, PRIMARY KEY (id), FOREIGN KEY (id) REFERENCES "
      "tbl(id));",
      "CREATE TABLE t (id INT, FOREIGN KEY (id, id2) REFERENCES tbl(id1, "
      "id2));",
      "CREATE TABLE",
      "CREATE TABLE t (id INT, UNIQUE",
      "CREATE TABLE t (id INT, PRIMARY KEY (id",
      "CREATE TABLE t (id INT, FOREIGN KEY (id) REFERENCES",
      "CREATE TABLE t (id INT, FOREIGN KEY (id) REFERENCES tbl",
      "CREATE TABLE t (id INT, FOREIGN KEY (id) REFERENCES tbl(",
      "CREATE TABLE t (id INT, FOREIGN KEY (id) REFERENCES tbl(id",
      "CREATE TABLE t (id INT, FOREIGN KEY (id) REFERENCES tbl(id)",
      "CREATE TABLE t (id INT, INVALID",
      "CREATE TABLE t (id INT, FOREIGN KEY (id))",
      NULL};

  old_malloc = c_orm_malloc;
  old_realloc = c_orm_realloc;
  old_free = c_orm_free;

  c_orm_set_allocators(mock_malloc_fail, mock_realloc_fail, mock_free);

  for (sql_idx = 0; sqls[sql_idx] != NULL; sql_idx++) {
    for (i = 0; i < 50; i++) {
      list = NULL;
      table = NULL;
      g_malloc_target = i;
      g_malloc_count = 0;
      g_malloc_fail = 1;

      span = az_span_create_from_str((char *)sqls[sql_idx]);
      rc = sql_lex(span, &list);
      if (rc == C_ORM_OK && list != NULL) {
        rc = sql_parse_table(list, &table, &err_info);
        if (rc == C_ORM_OK && table != NULL) {
          sql_table_C_ORM_FREE(table);
          C_ORM_FREE(table);
          table = NULL;
        }
        sql_token_list_free(list);
        list = NULL;
      }
      g_malloc_fail = 0;
      if (g_malloc_count <= i) {
        break;
      }
    }

    for (i = 0; i < 30; i++) {
      tables = NULL;
      n_tables = 0;

      g_malloc_target = i;
      g_malloc_count = 0;
      g_malloc_fail = 1;

      rc = parse_sql_ddl(sqls[sql_idx], &tables, &n_tables);
      if (rc == C_ORM_OK && tables != NULL) {
        for (j = 0; j < n_tables; ++j) {
          sql_table_C_ORM_FREE(&tables[j]);
        }
        C_ORM_FREE(tables);
        tables = NULL;
      }
      g_malloc_fail = 0;
      if (g_malloc_count <= i) {
        break;
      }
    }
  }

  c_orm_set_allocators(old_malloc, old_realloc, old_free);
  PASS();
}

/**
 * @brief Forward declaration for missing keys parser test.
 * @return GREATEST test result.
 */
enum greatest_test_res test_sql_parser_missing_keys(void);

/**
 * @brief Test SQL parser handling of syntax errors involving missing keywords.
 * @return GREATEST test result.
 */
enum greatest_test_res test_sql_parser_missing_keys(void) {
  struct sql_table_t *tables;
  size_t n_tables;
  c_orm_error_t rc;

  tables = NULL;
  n_tables = 0;

  /* Missing KEY after PRIMARY at table level */
  rc = parse_sql_ddl("CREATE TABLE t (id INT, PRIMARY);", &tables, &n_tables);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ(0, n_tables);
  if (tables != NULL) {
    C_ORM_FREE(tables);
    tables = NULL;
    n_tables = 0;
  }

  /* Missing KEY after FOREIGN at table level */
  rc = parse_sql_ddl("CREATE TABLE t (id INT, FOREIGN);", &tables, &n_tables);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ(0, n_tables);
  if (tables != NULL) {
    C_ORM_FREE(tables);
    tables = NULL;
    n_tables = 0;
  }

  /* Invalid table-level constraint */
  rc = parse_sql_ddl("CREATE TABLE t (id INT, INVALID);", &tables, &n_tables);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ(0, n_tables);
  if (tables != NULL) {
    C_ORM_FREE(tables);
    tables = NULL;
    n_tables = 0;
  }

  /* Constraint but missing columns in parenthesis */
  rc = parse_sql_ddl("CREATE TABLE t (id INT, PRIMARY KEY ());", &tables,
                     &n_tables);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ(0, n_tables);
  if (tables != NULL) {
    C_ORM_FREE(tables);
    tables = NULL;
    n_tables = 0;
  }

  /* Constraint missing right paren */
  rc = parse_sql_ddl("CREATE TABLE t (id INT, PRIMARY KEY (id);", &tables,
                     &n_tables);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ(0, n_tables);
  if (tables != NULL) {
    C_ORM_FREE(tables);
    tables = NULL;
    n_tables = 0;
  }

  /* FOREIGN KEY but missing REFERENCES */
  rc = parse_sql_ddl("CREATE TABLE t (id INT, FOREIGN KEY (id));", &tables,
                     &n_tables);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ(0, n_tables);
  if (tables != NULL) {
    C_ORM_FREE(tables);
    tables = NULL;
    n_tables = 0;
  }

  /* FOREIGN KEY REFERENCES but missing table name */
  rc = parse_sql_ddl("CREATE TABLE t (id INT, FOREIGN KEY (id) REFERENCES);",
                     &tables, &n_tables);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ(0, n_tables);
  if (tables != NULL) {
    C_ORM_FREE(tables);
    tables = NULL;
    n_tables = 0;
  }

  /* FOREIGN KEY REFERENCES table name but missing left paren */
  rc = parse_sql_ddl("CREATE TABLE t (id INT, FOREIGN KEY (id) REFERENCES tbl;",
                     &tables, &n_tables);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ(0, n_tables);
  if (tables != NULL) {
    C_ORM_FREE(tables);
    tables = NULL;
    n_tables = 0;
  }

  /* FOREIGN KEY REFERENCES table ( missing col name */
  rc = parse_sql_ddl(
      "CREATE TABLE t (id INT, FOREIGN KEY (id) REFERENCES tbl();", &tables,
      &n_tables);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ(0, n_tables);
  if (tables != NULL) {
    C_ORM_FREE(tables);
    tables = NULL;
    n_tables = 0;
  }

  /* FOREIGN KEY REFERENCES table ( col missing right paren */
  rc = parse_sql_ddl(
      "CREATE TABLE t (id INT, FOREIGN KEY (id) REFERENCES tbl(col;", &tables,
      &n_tables);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ(0, n_tables);
  if (tables != NULL) {
    C_ORM_FREE(tables);
    tables = NULL;
    n_tables = 0;
  }

  PASS();
}

/**
 * @brief Forward declaration for exhaustive OOM parser test.
 * @return GREATEST test result.
 */
enum greatest_test_res test_sql_parser_exhaustive_oom_impl(void);

/**
 * @brief Test exhaustive OOM and parser fault injection.
 * @return GREATEST test result.
 */
enum greatest_test_res test_sql_parser_exhaustive_oom_impl(void) {
  void *(*old_malloc)(size_t);
  void *(*old_realloc)(void *, size_t);
  void (*old_free)(void *);
  int i;
  int sql_idx;
  az_span span;
  struct sql_token_list_t *list;
  struct sql_table_t *ast;
  struct sql_parse_error_t err_info;
  c_orm_error_t rc;

  const char *sqls[] = {
      "CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(255) NOT NULL, "
      "role_id BIGINT REFERENCES roles(id), is_active BOOLEAN DEFAULT true);",
      "CREATE TABLE t (id INT, PRIMARY KEY (id), FOREIGN KEY (id) REFERENCES "
      "x(id) ON DELETE CASCADE);",
      "CREATE TABLE t2 (id INT UNIQUE, val FLOAT DEFAULT 3.14);",
      "CREATE TABLE a (id UUID PRIMARY KEY, raw TEXT);",
      "CREATE TABLE bad (id INT PRIMARY KEY, FOREIGN KEY(fk) REFERENCES "
      "foo(id), UNIQUE(fk));",
      /* Truncated / malformed queries */
      "CREATE TABLE", "CREATE TABLE x (", "CREATE TABLE x (id",
      "CREATE TABLE x (id INT", "CREATE TABLE x (id INT,",
      "CREATE TABLE x (id INT PRIMARY KEY",
      "CREATE TABLE x (id INT PRIMARY KEY,",
      "CREATE TABLE x (id INT PRIMARY KEY, FOREIGN KEY",
      "CREATE TABLE x (id INT PRIMARY KEY, FOREIGN KEY(",
      "CREATE TABLE x (id INT PRIMARY KEY, FOREIGN KEY(a",
      "CREATE TABLE x (id INT PRIMARY KEY, FOREIGN KEY(a)",
      "CREATE TABLE x (id INT PRIMARY KEY, FOREIGN KEY(a) REFERENCES",
      "CREATE TABLE x (id INT PRIMARY KEY, FOREIGN KEY(a) REFERENCES t",
      "CREATE TABLE x (id INT PRIMARY KEY, FOREIGN KEY(a) REFERENCES t(",
      "CREATE TABLE x (id INT PRIMARY KEY, FOREIGN KEY(a) REFERENCES t(b",
      "CREATE TABLE x (id INT PRIMARY KEY, FOREIGN KEY(a) REFERENCES t(b)",
      "CREATE TABLE x (id INT PRIMARY KEY, FOREIGN KEY(a) REFERENCES t(b) ON",
      "CREATE TABLE x (id INT PRIMARY KEY, FOREIGN KEY(a) REFERENCES t(b) ON "
      "DELETE",
      "CREATE TABLE x (id INT PRIMARY KEY, FOREIGN KEY(a) REFERENCES t(b) ON "
      "DELETE CASCADE"};

  old_malloc = c_orm_malloc;
  old_realloc = c_orm_realloc;
  old_free = c_orm_free;

  c_orm_set_allocators(mock_malloc_fail, mock_realloc_fail, mock_free);

  for (sql_idx = 0; sql_idx < (int)(sizeof(sqls) / sizeof(sqls[0]));
       sql_idx++) {
    for (i = 0; i < 75; i++) {
      list = NULL;
      ast = NULL;
      span = az_span_create_from_str((char *)sqls[sql_idx]);

      g_malloc_fail = 0;
      rc = sql_lex(span, &list);
      if (rc == C_ORM_OK && list != NULL) {
        g_malloc_target = i;
        g_malloc_count = 0;
        g_malloc_fail = 1;

        rc = sql_parse_table(list, &ast, &err_info);
        if (rc == C_ORM_OK && ast != NULL) {
          sql_table_C_ORM_FREE(ast);
          C_ORM_FREE(ast);
          ast = NULL;
        }
        sql_token_list_free(list);
        list = NULL;
      }

      rc = sql_lex(span, &list);
      if (rc == C_ORM_OK && list != NULL) {
        c_orm_parser_set_fail(i);

        rc = sql_parse_table(list, &ast, &err_info);
        if (rc == C_ORM_OK && ast != NULL) {
          c_orm_parser_set_fail(-1);
          sql_table_C_ORM_FREE(ast);
          C_ORM_FREE(ast);
          ast = NULL;
        }
        c_orm_parser_set_fail(-1);
        sql_token_list_free(list);
        list = NULL;
      }
    }
  }

  g_malloc_fail = 0;
  c_orm_set_allocators(old_malloc, old_realloc, old_free);
  PASS();
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#if defined(__clang__) || defined(__GNUC__)
#endif
