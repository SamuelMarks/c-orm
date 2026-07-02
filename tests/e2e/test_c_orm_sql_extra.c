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
static void *mock_malloc_fail(size_t size) {
  if (g_malloc_fail) {
    if (g_malloc_target == g_malloc_count++)
      return NULL;
  }
  return malloc(size);
}

static void *mock_realloc_fail(void *ptr, size_t size) {
  if (g_malloc_fail) {
    if (g_malloc_target == g_malloc_count++)
      return NULL;
  }
  return realloc(ptr, size);
}

static void mock_free(void *p) { free(p); }

enum greatest_test_res test_sql_lexer_oom_impl(void);
enum greatest_test_res test_sql_lexer_oom_impl(void) {
  void *(*old_malloc)(size_t) = c_orm_malloc;
  void *(*old_realloc)(void *, size_t) = c_orm_realloc;
  void (*old_free)(void *) = c_orm_free;
  int i, sql_idx;
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
  az_span span;

  c_orm_set_allocators(mock_malloc_fail, c_orm_realloc, c_orm_free);
  c_orm_set_allocators(c_orm_malloc, mock_realloc_fail, c_orm_free);
  c_orm_set_allocators(c_orm_malloc, c_orm_realloc, mock_free);

  for (sql_idx = 0; sqls[sql_idx] != NULL; sql_idx++) {
    for (i = 0; i < 50; i++) {
      struct sql_token_list_t *list = NULL;
      g_malloc_target = i;
      g_malloc_count = 0;
      g_malloc_fail = 1;

      span = az_span_create_from_str((char *)sqls[sql_idx]);
      sql_lex(span, &list);
      if (list) {
        struct sql_parse_error_t err_info;
        struct sql_table_t *table = NULL;
        sql_parse_table(list, &table, &err_info);
        if (table) {
          sql_table_C_ORM_FREE(table);
          C_ORM_FREE(table);
        }
        sql_token_list_free(list);
      }
      g_malloc_fail = 0;
      if (g_malloc_count <= i)
        break;
    }

    for (i = 0; i < 150; i++) {
      struct sql_table_t *tables = NULL;
      size_t n_tables = 0;
      size_t j;

      g_malloc_target = i;
      g_malloc_count = 0;
      g_malloc_fail = 1;

      parse_sql_ddl(sqls[sql_idx], &tables, &n_tables);
      if (tables) {
        for (j = 0; j < n_tables; ++j) {
          sql_table_C_ORM_FREE(&tables[j]);
        }
        C_ORM_FREE(tables);
      }

      g_malloc_fail = 0;
      if (g_malloc_count <= i)
        break;
    }
  }

  c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);
  c_orm_set_allocators(c_orm_malloc, old_realloc, c_orm_free);
  c_orm_set_allocators(c_orm_malloc, c_orm_realloc, old_free);
  PASS();
}
enum greatest_test_res test_sql_parser_missing_keys(void);
enum greatest_test_res test_sql_parser_missing_keys(void) {
  struct sql_table_t *tables = NULL;
  size_t n_tables = 0;

  /* Missing KEY after PRIMARY at table level */
  parse_sql_ddl("CREATE TABLE t (id INT, PRIMARY);", &tables, &n_tables);
  if (tables) {
    C_ORM_FREE(tables);
    tables = NULL;
    n_tables = 0;
  }

  /* Missing KEY after FOREIGN at table level */
  parse_sql_ddl("CREATE TABLE t (id INT, FOREIGN);", &tables, &n_tables);
  if (tables) {
    C_ORM_FREE(tables);
    tables = NULL;
    n_tables = 0;
  }

  /* Invalid table-level constraint */
  parse_sql_ddl("CREATE TABLE t (id INT, INVALID);", &tables, &n_tables);
  if (tables) {
    C_ORM_FREE(tables);
    tables = NULL;
    n_tables = 0;
  }

  /* Constraint but missing columns in parenthesis */
  parse_sql_ddl("CREATE TABLE t (id INT, PRIMARY KEY ());", &tables, &n_tables);
  if (tables) {
    C_ORM_FREE(tables);
    tables = NULL;
    n_tables = 0;
  }

  /* Constraint missing right paren */
  parse_sql_ddl("CREATE TABLE t (id INT, PRIMARY KEY (id);", &tables,
                &n_tables);
  if (tables) {
    C_ORM_FREE(tables);
    tables = NULL;
    n_tables = 0;
  }

  /* FOREIGN KEY but missing REFERENCES */
  parse_sql_ddl("CREATE TABLE t (id INT, FOREIGN KEY (id));", &tables,
                &n_tables);
  if (tables) {
    C_ORM_FREE(tables);
    tables = NULL;
    n_tables = 0;
  }

  /* FOREIGN KEY REFERENCES but missing table name */
  parse_sql_ddl("CREATE TABLE t (id INT, FOREIGN KEY (id) REFERENCES);",
                &tables, &n_tables);
  if (tables) {
    C_ORM_FREE(tables);
    tables = NULL;
    n_tables = 0;
  }

  /* FOREIGN KEY REFERENCES table name but missing left paren */
  parse_sql_ddl("CREATE TABLE t (id INT, FOREIGN KEY (id) REFERENCES tbl;",
                &tables, &n_tables);
  if (tables) {
    C_ORM_FREE(tables);
    tables = NULL;
    n_tables = 0;
  }

  /* FOREIGN KEY REFERENCES table ( missing col name */
  parse_sql_ddl("CREATE TABLE t (id INT, FOREIGN KEY (id) REFERENCES tbl();",
                &tables, &n_tables);
  if (tables) {
    C_ORM_FREE(tables);
    tables = NULL;
    n_tables = 0;
  }

  /* FOREIGN KEY REFERENCES table ( col missing right paren */
  parse_sql_ddl("CREATE TABLE t (id INT, FOREIGN KEY (id) REFERENCES tbl(col;",
                &tables, &n_tables);
  if (tables) {
    C_ORM_FREE(tables);
    tables = NULL;
    n_tables = 0;
  }

  PASS();
}
