#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file test_sql.h
 * @brief Tests for the SQL DDL Lexer.
 */

#ifndef C_CDD_TEST_SQL_H
#define C_CDD_TEST_SQL_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_sql.h"
#include <greatest.h>
/* clang-format on */

TEST test_sql_lexer_basic(void) {
  const char *sql =
      "CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(255));";
  az_span span = az_span_create_from_str((char *)sql);
  struct sql_token_list_t *list = NULL;
  c_orm_error_t err;

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
  c_orm_error_t err;

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
  c_orm_error_t err;

  err = sql_lex(span, &list);
  ASSERT_EQ(0, err);

  err = sql_parse_table(list, &table, &err_info);
  ASSERT_EQ(0, err);
  ASSERT(table != NULL);
  ASSERT_STR_EQ("users", table->name);
  ASSERT_EQ(4, table->n_columns);

  /* id INT PRIMARY KEY */
  ASSERT_STR_EQ("id", table->columns[0].name);
  ASSERT_EQ(SQL_TYPE_INT, table->columns[0].type);
  ASSERT_EQ(1, table->columns[0].n_constraints);
  ASSERT_EQ(SQL_CONSTRAINT_PRIMARY_KEY, table->columns[0].constraints[0].type);

  /* name VARCHAR(255) NOT NULL */
  ASSERT_STR_EQ("name", table->columns[1].name);
  ASSERT_EQ(SQL_TYPE_VARCHAR, table->columns[1].type);
  ASSERT_EQ(255, table->columns[1].length);
  ASSERT_EQ(1, table->columns[1].n_constraints);
  ASSERT_EQ(SQL_CONSTRAINT_NOT_NULL, table->columns[1].constraints[0].type);

  /* role_id BIGINT REFERENCES roles(id) */
  ASSERT_STR_EQ("role_id", table->columns[2].name);
  ASSERT_EQ(SQL_TYPE_BIGINT, table->columns[2].type);
  ASSERT_EQ(1, table->columns[2].n_constraints);
  ASSERT_EQ(SQL_CONSTRAINT_FOREIGN_KEY, table->columns[2].constraints[0].type);
  ASSERT_STR_EQ("roles", table->columns[2].constraints[0].reference_table);
  ASSERT_STR_EQ("id", table->columns[2].constraints[0].reference_column);

  /* is_active BOOLEAN DEFAULT true */
  ASSERT_STR_EQ("is_active", table->columns[3].name);
  ASSERT_EQ(SQL_TYPE_BOOLEAN, table->columns[3].type);
  ASSERT_EQ(1, table->columns[3].n_constraints);
  ASSERT_EQ(SQL_CONSTRAINT_DEFAULT, table->columns[3].constraints[0].type);
  ASSERT_STR_EQ("true", table->columns[3].constraints[0].default_value);

  sql_table_C_ORM_FREE(table);
  sql_token_list_free(list);
  PASS();
}

TEST test_sql_lexer_strings_unknown(void) {
  const char *sql = "DEFAULT 'some_string' ^ ~";
  az_span span = az_span_create_from_str((char *)sql);
  struct sql_token_list_t *list = NULL;
  c_orm_error_t err;
  int has_str = 0;
  int has_unknown = 0;
  size_t i;

  err = sql_lex(span, &list);
  ASSERT_EQ(0, err);
  ASSERT(list != NULL);

  /* DEFAULT */
  /* space */
  /* 'some_string' -> SQL_TOKEN_STRING */
  /* space */
  /* ^ -> SQL_TOKEN_UNKNOWN */
  /* space */
  /* ~ -> SQL_TOKEN_UNKNOWN */

  for (i = 0; i < list->size; i++) {
    if (list->tokens[i].kind == SQL_TOKEN_STRING)
      has_str = 1;
    if (list->tokens[i].kind == SQL_TOKEN_UNKNOWN)
      has_unknown = 1;
  }

  ASSERT_EQ(1, has_str);
  ASSERT_EQ(1, has_unknown);

  sql_token_list_free(list);

  /* also unclosed string */
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
  const char *sql_err = "CREATE TABLE t2 (";
  struct sql_table_t *tables = NULL;
  size_t n_tables = 0;
  struct sql_table_t *tables_err = NULL;
  size_t n_tables_err = 0;
  struct sql_token_list_t *list = NULL;
  struct sql_token_list_t *list_err = NULL;
  size_t i;
  c_orm_error_t err;

  err = sql_lex(az_span_create_from_str((char *)sql), &list);
  ASSERT_EQ(0, err);

  err = parse_sql_ddl(sql, &tables, &n_tables);
  ASSERT_EQ(0, err);

  /* Add a test for parser error (e.g. invalid syntax) */
  err = sql_lex(az_span_create_from_str((char *)sql_err), &list_err);
  ASSERT_EQ(0, err);

  err = parse_sql_ddl(sql_err, &tables_err, &n_tables_err);
  ASSERT_EQ(0, n_tables_err);

  if (tables) {
    for (i = 0; i < n_tables; ++i) {
      sql_table_C_ORM_FREE(&tables[i]);
    }
    free(tables);
  }

  free(tables_err);
  sql_token_list_free(list);
  sql_token_list_free(list_err);

  PASS();
}

SUITE(sql_suite) {
  RUN_TEST(test_sql_lexer_basic);
  RUN_TEST(test_sql_lexer_types);
  RUN_TEST(test_sql_parser_basic);
  RUN_TEST(test_sql_lexer_strings_unknown);
  RUN_TEST(test_sql_parser_foreign_keys_defaults);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* C_CDD_TEST_SQL_H */

#if defined(__clang__) || defined(__GNUC__)
#endif
