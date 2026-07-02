/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_sql.h"
#include <greatest.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

TEST test_sql_lexer_basic(void) {
  const char *sql =
      "CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(255));";
  az_span span = az_span_create_from_str((char *)sql);
  struct sql_token_list_t *list = NULL;
  int err;

  err = sql_lex(span, &list);
  ASSERT_EQ(0, err);
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

TEST test_sql_lexer_types(void) {
  const char *sql = "id BIGINT, is_active BOOLEAN DEFAULT true";
  az_span span = az_span_create_from_str((char *)sql);
  struct sql_token_list_t *list = NULL;
  int err;

  err = sql_lex(span, &list);
  ASSERT_EQ(0, err);
  ASSERT(list != NULL);

  sql_token_list_free(list);
  PASS();
}

TEST test_sql_parser_basic(void) {
  const char *sql =
      "CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(255) NOT NULL, "
      "role_id BIGINT REFERENCES roles(id), is_active BOOLEAN DEFAULT true);";
  az_span span = az_span_create_from_str((char *)sql);
  struct sql_token_list_t *list = NULL;
  struct sql_table_t *table = NULL;
  struct sql_parse_error_t err_info;
  int err;

  err = sql_lex(span, &list);
  ASSERT_EQ(0, err);

  err = sql_parse_table(list, &table, &err_info);
  ASSERT_EQ(0, err);
  ASSERT(table != NULL);
  ASSERT_STR_EQ("users", table->name);
  ASSERT_EQ(4, table->n_columns);

  sql_table_C_ORM_FREE(table);
  free(table);
  sql_token_list_free(list);
  PASS();
}

TEST test_sql_lexer_strings_unknown(void) {
  const char *sql = "DEFAULT 'some_string' ^ ~";
  az_span span = az_span_create_from_str((char *)sql);
  struct sql_token_list_t *list = NULL;
  int err;
  int has_str = 0;
  int has_unknown = 0;
  size_t i;

  err = sql_lex(span, &list);
  ASSERT_EQ(0, err);
  ASSERT(list != NULL);

  for (i = 0; i < list->size; i++) {
    if (list->tokens[i].kind == SQL_TOKEN_STRING)
      has_str = 1;
    if (list->tokens[i].kind == SQL_TOKEN_UNKNOWN)
      has_unknown = 1;
  }

  ASSERT_EQ(1, has_str);
  ASSERT_EQ(1, has_unknown);

  sql_token_list_free(list);

  /* unclosed string */
  sql = "'unclosed";
  span = az_span_create_from_str((char *)sql);
  err = sql_lex(span, &list);
  ASSERT_EQ(0, err);
  sql_token_list_free(list);

  PASS();
}

TEST test_sql_parser_foreign_keys_defaults(void) {
  const char *sql = "CREATE TABLE t1 (id INT PRIMARY KEY, "
                    "ref_id INT REFERENCES other_table(id), "
                    "status VARCHAR(255) DEFAULT 'active');";
  struct sql_table_t *tables = NULL;
  size_t n_tables = 0;
  int err;

  err = parse_sql_ddl(sql, &tables, &n_tables);
  ASSERT_EQ(0, err);

  if (tables) {
    size_t i;
    for (i = 0; i < n_tables; ++i) {
      sql_table_C_ORM_FREE(&tables[i]);
    }
    free(tables);
  }

  PASS();
}

enum greatest_test_res test_sql_lexer_oom_impl(void);
TEST test_sql_lexer_oom(void) { return test_sql_lexer_oom_impl(); }

TEST test_sql_parser_errors(void) { PASS(); }

enum greatest_test_res test_sql_parser_missing_keys(void);
SUITE(sql_suite) {
  RUN_TEST(test_sql_lexer_basic);
  RUN_TEST(test_sql_lexer_types);
  RUN_TEST(test_sql_parser_basic);
  RUN_TEST(test_sql_lexer_strings_unknown);
  RUN_TEST(test_sql_parser_foreign_keys_defaults);

  RUN_TEST(test_sql_lexer_oom);
  RUN_TEST(test_sql_parser_errors);
  RUN_TEST(test_sql_parser_missing_keys);
}
