/**
 * @file c_orm_sql.c
 * @brief Parses SQL DDL into an AST.
 */

/* clang-format off */
#include "c_orm_sql.h"
#include "c_orm_meta.h"
#include "query_projection.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

/**
 * @brief Helper to check if a string is a SQL keyword.
 * @param str The string to check.
 * @param len The length of the string.
 * @param out_is_kw Pointer to receive 1 if keyword, 0 otherwise.
 * @return 0 on success.
 */
static c_orm_error_t is_sql_keyword(const char *str, size_t len,
                                    int *out_is_kw) {
  /* Simple exact matching for now; can be expanded. */
  static const char *keywords[] = {
      "CREATE",    "TABLE",      "SMALLINT",  "INT",        "INTEGER",
      "BIGINT",    "VARCHAR",    "TEXT",      "CHAR",       "CHARACTER",
      "GRAPHIC",   "VARGRAPHIC", "CLOB",      "DBCLOB",     "BINARY",
      "VARBINARY", "FLOAT",      "REAL",      "DOUBLE",     "DECIMAL",
      "DEC",       "NUMERIC",    "DECFLOAT",  "BOOLEAN",    "BOOL",
      "DATE",      "TIME",       "TIMESTAMP", "BLOB",       "BYTEA",
      "PRIMARY",   "KEY",        "FOREIGN",   "REFERENCES", "UNIQUE",
      "NOT",       "NULL",       "DEFAULT",   "SELECT",     "DISTINCT",
      "INSERT",    "INTO",       "VALUES",    "RETURNING",  "UPDATE",
      "SET",       "DELETE",     "FROM",      "WHERE",      "AS",
      NULL};
  int i;
  for (i = 0; keywords[i] != NULL; ++i) {
    if (strlen(keywords[i]) == len && strncmp(str, keywords[i], len) == 0) {
      *out_is_kw = 1;
      return C_ORM_OK;
    }
  }
  *out_is_kw = 0;
  return C_ORM_OK;
}

/**
 * @brief Push a token into the list.
 * @param list Token list container.
 * @param kind Token classification.
 * @param start Pointer to start of token in input.
 * @param length Length of token string.
 * @return 0 on success, non-zero on error.
 */
static c_orm_error_t push_token(struct sql_token_list_t *list,
                                enum SqlTokenKind kind, const char *start,
                                size_t length) {
  if (list->size >= list->capacity) {
    size_t new_cap = list->capacity == 0 ? 16 : list->capacity * 2;
    struct sql_token_t *new_tokens = NULL;
    if (c_orm_system_realloc(list->tokens, new_cap * sizeof(struct sql_token_t),
                             (void **)&new_tokens) != C_ORM_OK) {
      return C_ORM_ERROR_MEMORY;
    }
    list->tokens = new_tokens;
    list->capacity = new_cap;
  }
  list->tokens[list->size].kind = kind;
  list->tokens[list->size].start = start;
  list->tokens[list->size].length = length;
  list->size++;
  return C_ORM_OK;
}

/**
 * @brief Tokenizes SQL source code into a token list.
 * @param source Azure SDK string span containing SQL text.
 * @param out_list Pointer to receive allocated token list.
 * @return 0 on success, non-zero on error.
 */
c_orm_error_t sql_lex(az_span source, struct sql_token_list_t **out_list) {
  struct sql_token_list_t *list = NULL;
  const char *curr;
  const char *end;
  c_orm_error_t err;

  if (!source._internal.ptr || !out_list) {
    return C_ORM_ERROR_VALIDATION;
  }

  if (c_orm_system_calloc(1, sizeof(struct sql_token_list_t), (void **)&list) !=
      C_ORM_OK) {
    return C_ORM_ERROR_MEMORY;
  }

  curr = (const char *)az_span_ptr(source);
  end = curr + az_span_size(source);

  while (curr < end) {
    if (isspace((unsigned char)*curr)) {
      const char *start = curr;
      while (curr < end && isspace((unsigned char)*curr)) {
        curr++;
      }
      err =
          push_token(list, SQL_TOKEN_WHITESPACE, start, (size_t)(curr - start));
      if (err)
        goto fail;
      continue;
    }

    if (isalpha((unsigned char)*curr) || *curr == '_') {
      const char *start = curr;
      while (curr < end && (isalnum((unsigned char)*curr) || *curr == '_')) {
        curr++;
      }
      {
        size_t len = (size_t)(curr - start);
        enum SqlTokenKind kind = SQL_TOKEN_IDENTIFIER;
        {
          int is_kw = 0;
          is_sql_keyword(start, len, &is_kw);
          if (is_kw) {
            /* Note: In a real implementation we'd do case-insensitive
             * comparison
             */
            kind = SQL_TOKEN_KEYWORD;
          }
        }
        err = push_token(list, kind, start, len);
        if (err)
          goto fail;
      }
      continue;
    }

    if (isdigit((unsigned char)*curr)) {
      const char *start = curr;
      while (curr < end && isdigit((unsigned char)*curr)) {
        curr++;
      }
      err = push_token(list, SQL_TOKEN_NUMBER, start, (size_t)(curr - start));
      if (err)
        goto fail;
      continue;
    }

    if (*curr == '"') {
      const char *start;
      size_t qlen;
      curr++;
      start = curr;
      while (curr < end) {
        if (*curr == '"') {
          if ((curr + 1) < end && curr[1] == '"') {
            curr += 2;
            continue;
          }
          break;
        }
        curr++;
      }
      qlen = (size_t)(curr - start);
      if (curr < end) {
        curr++;
      }
      err = push_token(list, SQL_TOKEN_IDENTIFIER, start, qlen);
      if (err)
        goto fail;
      continue;
    }

    if (*curr == '\'') {
      const char *start = curr;
      curr++;
      while (curr < end) {
        if (*curr == '\'') {
          if ((curr + 1) < end && curr[1] == '\'') {
            curr += 2;
            continue;
          }
          break;
        }
        curr++;
      }
      if (curr < end) {
        curr++; /* Skip closing quote */
      }
      err = push_token(list, SQL_TOKEN_STRING, start, (size_t)(curr - start));
      if (err)
        goto fail;
      continue;
    }

    switch (*curr) {
    case '(':
      err = push_token(list, SQL_TOKEN_LPAREN, curr, 1);
      curr++;
      break;
    case ')':
      err = push_token(list, SQL_TOKEN_RPAREN, curr, 1);
      curr++;
      break;
    case ',':
      err = push_token(list, SQL_TOKEN_COMMA, curr, 1);
      curr++;
      break;
    case ';':
      err = push_token(list, SQL_TOKEN_SEMICOLON, curr, 1);
      curr++;
      break;
    default:
      err = push_token(list, SQL_TOKEN_UNKNOWN, curr, 1);
      curr++;
      break;
    }
    if (err)
      goto fail;
  }

  err = push_token(list, SQL_TOKEN_EOF, curr, 0);
  if (err)
    goto fail;

  *out_list = list;
  return C_ORM_OK;

fail:
  sql_token_list_free(list);
  return C_ORM_ERROR_MEMORY;
}

c_orm_error_t sql_token_list_free(struct sql_token_list_t *list) {
  if (list) {
    if (list->tokens) {
      (void)c_orm_system_free(list->tokens);
    }
    (void)c_orm_system_free(list);
  }
  return C_ORM_OK;
}

/**
 * @brief Frees allocated members within a sql_constraint_t structure.
 * @param c Constraint structure to clean up.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t sql_constraint_free_internals(struct sql_constraint_t *c) {
  if (c->reference_table)
    (void)c_orm_system_free(c->reference_table);
  if (c->reference_column)
    (void)c_orm_system_free(c->reference_column);
  if (c->default_value)
    (void)c_orm_system_free(c->default_value);
  if (c->columns) {
    size_t k;
    for (k = 0; k < c->n_columns; ++k) {
      if (c->columns[k])
        (void)c_orm_system_free(c->columns[k]);
    }
    (void)c_orm_system_free(c->columns);
  }
  c->reference_table = NULL;
  c->reference_column = NULL;
  c->default_value = NULL;
  c->columns = NULL;
  c->n_columns = 0;
  return C_ORM_OK;
}

#undef sql_table_C_ORM_FREE
C_ORM_EXPORT c_orm_error_t sql_table_free(struct sql_table_t *table) {
  if (table) {
    size_t i;
    for (i = 0; i < table->n_columns; ++i) {
      struct sql_column_t *col = &table->columns[i];
      if (col->name)
        (void)c_orm_system_free(col->name);
      if (col->constraints) {
        size_t j;
        for (j = 0; j < col->n_constraints; ++j) {
          sql_constraint_free_internals(&col->constraints[j]);
        }
        (void)c_orm_system_free(col->constraints);
      }
    }
    if (table->columns)
      (void)c_orm_system_free(table->columns);
    if (table->name)
      (void)c_orm_system_free(table->name);

    for (i = 0; i < table->n_table_constraints; ++i) {
      sql_constraint_free_internals(&table->table_constraints[i]);
    }
    if (table->table_constraints)
      (void)c_orm_system_free(table->table_constraints);
  }
  return C_ORM_OK;
}

/**
 * @brief Releases partially populated column and table AST structures on parse
 * error.
 * @param col Partially populated column structure (may be NULL).
 * @param table Partially populated table structure (may be NULL).
 * @return C_ORM_OK on completion.
 */
static c_orm_error_t sql_column_cleanup(struct sql_column_t *col,
                                        struct sql_table_t *table) {
  if (col) {
    size_t c;
    if (col->constraints) {
      for (c = 0; c < col->n_constraints; ++c) {
        sql_constraint_free_internals(&col->constraints[c]);
      }
      (void)c_orm_system_free(col->constraints);
      col->constraints = NULL;
    }
    (void)c_orm_system_free(col->name);
    col->name = NULL;
  }
  (void)sql_table_free(table);
  (void)c_orm_system_free(table);
  return C_ORM_OK;
}

/**
 * @brief SqlParserState struct
 * @var list Pointer to token list being parsed.
 * @var cursor Current token index.
 * @var out_error Optional output error information.
 */
struct SqlParserState {
  /** @brief Pointer to token list being parsed. */
  const struct sql_token_list_t *list;
  /** @brief Current token index. */
  size_t cursor;
  /** @brief Optional output error information. */
  struct sql_parse_error_t *out_error;
};

static int g_parser_fail_countdown = -1;
C_ORM_EXPORT c_orm_error_t c_orm_parser_set_fail(int count) {
  g_parser_fail_countdown = count;
  return C_ORM_OK;
}

/**
 * @brief Peeks at the current non-whitespace token without advancing.
 * @param state Parser state.
 * @param _out_val Pointer to receive token pointer.
 * @return 0 on success, non-zero on error.
 */
static c_orm_error_t sql_parser_peek(struct SqlParserState *state,
                                     struct sql_token_t **_out_val) {
  size_t c;
  if (g_parser_fail_countdown == 0) {
    g_parser_fail_countdown--;
    return C_ORM_ERROR_UNKNOWN;
  }
  if (g_parser_fail_countdown > 0)
    g_parser_fail_countdown--;
  c = state->cursor;
  while (state->list->tokens[c].kind == SQL_TOKEN_WHITESPACE) {
    c++;
  }
  *_out_val = &state->list->tokens[c];
  return C_ORM_OK;
}

/**
 * @brief Consumes the current non-whitespace token and advances cursor.
 * @param state Parser state.
 * @return 0 on success, non-zero on error.
 */
static c_orm_error_t sql_parser_consume(struct SqlParserState *state) {
  if (g_parser_fail_countdown == 0) {
    g_parser_fail_countdown--;
    return C_ORM_ERROR_UNKNOWN;
  }
  if (g_parser_fail_countdown > 0)
    g_parser_fail_countdown--;
  while (state->list->tokens[state->cursor].kind == SQL_TOKEN_WHITESPACE) {
    state->cursor++;
  }
  state->cursor++;
  return C_ORM_OK;
}

/**
 * @brief Checks if current token matches keyword and consumes it if true.
 * @param state Parser state.
 * @param kw Keyword string to match against.
 * @param out_match Pointer to receive 1 if matched, 0 otherwise.
 * @return 0 on success, non-zero on error.
 */
static c_orm_error_t sql_parser_match_keyword(struct SqlParserState *state,
                                              const char *kw, int *out_match) {
  c_orm_error_t rc;
  struct sql_token_t *_ast_sql_parser_peek_0 = NULL;
  const struct sql_token_t *tok;
  rc = sql_parser_peek(state, &_ast_sql_parser_peek_0);
  if (rc != C_ORM_OK)
    return rc;
  tok = _ast_sql_parser_peek_0;
  if (tok->kind == SQL_TOKEN_KEYWORD) {
    if (tok->length == strlen(kw) &&
        strncmp(tok->start, kw, tok->length) == 0) {
      rc = sql_parser_consume(state);
      if (rc != C_ORM_OK)
        return rc;
      *out_match = 1;
      return C_ORM_OK;
    }
  }
  *out_match = 0;
  return C_ORM_OK;
}

/**
 * @brief Checks if current token matches kind and consumes it if true.
 * @param state Parser state.
 * @param kind Token kind to match against.
 * @param out_tok Optional pointer to receive matched token.
 * @param out_match Pointer to receive 1 if matched, 0 otherwise.
 * @return 0 on success, non-zero on error.
 */
static c_orm_error_t sql_parser_match_kind(struct SqlParserState *state,
                                           enum SqlTokenKind kind,
                                           const struct sql_token_t **out_tok,
                                           int *out_match) {
  c_orm_error_t rc;
  struct sql_token_t *_ast_sql_parser_peek_1 = NULL;
  const struct sql_token_t *tok;
  rc = sql_parser_peek(state, &_ast_sql_parser_peek_1);
  if (rc != C_ORM_OK)
    return rc;
  tok = _ast_sql_parser_peek_1;
  if (tok->kind == kind) {
    if (out_tok) {
      *out_tok = tok;
    }
    rc = sql_parser_consume(state);
    if (rc != C_ORM_OK)
      return rc;
    *out_match = 1;
    return C_ORM_OK;
  }
  *out_match = 0;
  return C_ORM_OK;
}

/**
 * @brief Records a parser error message and captures current token.
 * @param state Parser state.
 * @param msg Error message string.
 * @return Error code.
 */
static c_orm_error_t sql_parser_set_error(struct SqlParserState *state,
                                          const char *msg) {
  c_orm_error_t rc;
  struct sql_token_t *_ast_sql_parser_peek_2 = NULL;
  if (state->out_error) {
    state->out_error->message = msg;
    rc = sql_parser_peek(state, &_ast_sql_parser_peek_2);
    if (rc != C_ORM_OK)
      return rc;
    state->out_error->token = _ast_sql_parser_peek_2;
  }
  return C_ORM_ERROR_SQL;
}

/**
 * @brief Checks if current token matches a word (keyword or identifier,
 * case-insensitively) and consumes it if matched.
 * @param state Parser state.
 * @param word Target word string.
 * @param out_match Pointer to receive 1 if matched, 0 otherwise.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t sql_parser_match_word(struct SqlParserState *state,
                                           const char *word, int *out_match) {
  c_orm_error_t rc;
  struct sql_token_t *peek_tok = NULL;
  size_t wlen;
  size_t i;

  rc = sql_parser_peek(state, &peek_tok);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (peek_tok->kind == SQL_TOKEN_KEYWORD ||
      peek_tok->kind == SQL_TOKEN_IDENTIFIER) {
    wlen = strlen(word);
    if (peek_tok->length == wlen) {
      int eq = 1;
      for (i = 0; i < wlen; ++i) {
        if (toupper((unsigned char)peek_tok->start[i]) !=
            toupper((unsigned char)word[i])) {
          eq = 0;
          break;
        }
      }
      if (eq) {
        rc = sql_parser_consume(state);
        if (rc != C_ORM_OK) {
          return rc;
        }
        *out_match = 1;
        return C_ORM_OK;
      }
    }
  }
  *out_match = 0;
  return C_ORM_OK;
}

/**
 * @brief Parses optional `(len)` or `(precision, scale)` parenthesized numeric
 * type arguments.
 * @param state Parser state.
 * @param allow_scale Non-zero to accept a second comma-separated integer.
 * @param out_length Pointer to receive first integer (`-1` if absent).
 * @param out_scale Pointer to receive second integer (`-1` if absent).
 * @return C_ORM_OK on success, or parse error code on malformed syntax.
 */
static c_orm_error_t sql_parse_type_params(struct SqlParserState *state,
                                           int allow_scale, int *out_length,
                                           int *out_scale) {
  c_orm_error_t rc;
  int match_lp = 0;
  int match_num = 0;
  int match_comma = 0;
  int match_rp = 0;
  const struct sql_token_t *num_tok = NULL;
  size_t i;
  int val;

  rc = sql_parser_match_kind(state, SQL_TOKEN_LPAREN, NULL, &match_lp);
  if (rc != C_ORM_OK || !match_lp) {
    return rc;
  }

  rc = sql_parser_match_kind(state, SQL_TOKEN_NUMBER, &num_tok, &match_num);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (match_num) {
    val = 0;
    for (i = 0; i < num_tok->length; ++i) {
      val = val * 10 + (num_tok->start[i] - '0');
    }
    *out_length = val;
  }

  if (allow_scale) {
    rc = sql_parser_match_kind(state, SQL_TOKEN_COMMA, NULL, &match_comma);
    if (rc != C_ORM_OK) {
      return rc;
    }
    if (match_comma) {
      num_tok = NULL;
      rc = sql_parser_match_kind(state, SQL_TOKEN_NUMBER, &num_tok, &match_num);
      if (rc != C_ORM_OK) {
        return rc;
      }
      if (!match_num) {
        return sql_parser_set_error(state, "Expected scale after ','");
      }
      val = 0;
      for (i = 0; i < num_tok->length; ++i) {
        val = val * 10 + (num_tok->start[i] - '0');
      }
      *out_scale = val;
    }
  }

  rc = sql_parser_match_kind(state, SQL_TOKEN_RPAREN, NULL, &match_rp);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (!match_rp) {
    return sql_parser_set_error(state, "Expected ) after length");
  }
  return C_ORM_OK;
}

/**
 * @brief Parses a SQL column data type and optional length/scale.
 * @param state Parser state.
 * @param out_type Pointer to receive parsed SQL data type.
 * @param out_length Pointer to receive length/precision (-1 if unspecified).
 * @param out_scale Pointer to receive scale (-1 if unspecified).
 * @return 0 on success, non-zero on error.
 */
static c_orm_error_t sql_parse_data_type_ex(struct SqlParserState *state,
                                            enum SqlDataType *out_type,
                                            int *out_length, int *out_scale) {
  c_orm_error_t rc;
  int match1 = 0, match2 = 0, match3 = 0;
  struct sql_token_t *_ast_sql_parser_peek_3 = NULL;
  const struct sql_token_t *tok;

  rc = sql_parser_peek(state, &_ast_sql_parser_peek_3);
  if (rc != C_ORM_OK)
    return rc;
  tok = _ast_sql_parser_peek_3;
  if (tok->kind != SQL_TOKEN_KEYWORD && tok->kind != SQL_TOKEN_IDENTIFIER) {
    return sql_parser_set_error(state, "Expected data type");
  }

  *out_length = -1;
  *out_scale = -1;

  rc = sql_parser_match_word(state, "SMALLINT", &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (match1) {
    *out_type = SQL_TYPE_SMALLINT;
    return C_ORM_OK;
  }

  rc = sql_parser_match_word(state, "INT", &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (!match1) {
    rc = sql_parser_match_word(state, "INTEGER", &match2);
    if (rc != C_ORM_OK)
      return rc;
  }
  if (match1 || match2) {
    *out_type = SQL_TYPE_INT;
    return C_ORM_OK;
  }

  rc = sql_parser_match_word(state, "BIGINT", &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (match1) {
    *out_type = SQL_TYPE_BIGINT;
    return C_ORM_OK;
  }

  rc = sql_parser_match_word(state, "VARCHAR", &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (match1) {
    *out_type = SQL_TYPE_VARCHAR;
    return sql_parse_type_params(state, 0, out_length, out_scale);
  }

  rc = sql_parser_match_word(state, "TEXT", &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (match1) {
    *out_type = SQL_TYPE_TEXT;
    return sql_parse_type_params(state, 0, out_length, out_scale);
  }

  rc = sql_parser_match_word(state, "CHAR", &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (!match1) {
    rc = sql_parser_match_word(state, "CHARACTER", &match2);
    if (rc != C_ORM_OK)
      return rc;
  }
  if (match1 || match2) {
    int match_var = 0;
    rc = sql_parser_match_word(state, "VARYING", &match_var);
    if (rc != C_ORM_OK)
      return rc;
    *out_type = match_var ? SQL_TYPE_VARCHAR : SQL_TYPE_CHAR;
    return sql_parse_type_params(state, 0, out_length, out_scale);
  }

  rc = sql_parser_match_word(state, "GRAPHIC", &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (match1) {
    *out_type = SQL_TYPE_GRAPHIC;
    return sql_parse_type_params(state, 0, out_length, out_scale);
  }

  rc = sql_parser_match_word(state, "VARGRAPHIC", &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (match1) {
    *out_type = SQL_TYPE_VARGRAPHIC;
    return sql_parse_type_params(state, 0, out_length, out_scale);
  }

  rc = sql_parser_match_word(state, "CLOB", &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (match1) {
    *out_type = SQL_TYPE_CLOB;
    return sql_parse_type_params(state, 0, out_length, out_scale);
  }

  rc = sql_parser_match_word(state, "DBCLOB", &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (match1) {
    *out_type = SQL_TYPE_DBCLOB;
    return sql_parse_type_params(state, 0, out_length, out_scale);
  }

  rc = sql_parser_match_word(state, "BINARY", &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (match1) {
    *out_type = SQL_TYPE_BINARY;
    return sql_parse_type_params(state, 0, out_length, out_scale);
  }

  rc = sql_parser_match_word(state, "VARBINARY", &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (match1) {
    *out_type = SQL_TYPE_VARBINARY;
    return sql_parse_type_params(state, 0, out_length, out_scale);
  }

  rc = sql_parser_match_word(state, "DECIMAL", &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (!match1) {
    rc = sql_parser_match_word(state, "DEC", &match2);
    if (rc != C_ORM_OK)
      return rc;
  }
  if (match1 || match2) {
    *out_type = SQL_TYPE_DECIMAL;
    return sql_parse_type_params(state, 1, out_length, out_scale);
  }

  rc = sql_parser_match_word(state, "NUMERIC", &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (match1) {
    *out_type = SQL_TYPE_NUMERIC;
    return sql_parse_type_params(state, 1, out_length, out_scale);
  }

  rc = sql_parser_match_word(state, "DECFLOAT", &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (match1) {
    *out_type = SQL_TYPE_DECFLOAT;
    return sql_parse_type_params(state, 0, out_length, out_scale);
  }

  rc = sql_parser_match_word(state, "FLOAT", &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (!match1) {
    rc = sql_parser_match_word(state, "REAL", &match2);
    if (rc != C_ORM_OK)
      return rc;
  }
  if (match1 || match2) {
    *out_type = SQL_TYPE_FLOAT;
    return sql_parse_type_params(state, 0, out_length, out_scale);
  }

  rc = sql_parser_match_word(state, "DOUBLE", &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (match1) {
    int match_prec = 0;
    rc = sql_parser_match_word(state, "PRECISION", &match_prec);
    if (rc != C_ORM_OK)
      return rc;
    *out_type = SQL_TYPE_DOUBLE;
    return C_ORM_OK;
  }

  rc = sql_parser_match_word(state, "BOOLEAN", &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (!match1) {
    rc = sql_parser_match_word(state, "BOOL", &match2);
    if (rc != C_ORM_OK)
      return rc;
  }
  if (match1 || match2) {
    *out_type = SQL_TYPE_BOOLEAN;
    return C_ORM_OK;
  }

  rc = sql_parser_match_word(state, "DATE", &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (match1) {
    *out_type = SQL_TYPE_DATE;
    return C_ORM_OK;
  }

  rc = sql_parser_match_word(state, "TIME", &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (match1) {
    *out_type = SQL_TYPE_TIME;
    return C_ORM_OK;
  }

  rc = sql_parser_match_word(state, "TIMESTAMP", &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (match1) {
    *out_type = SQL_TYPE_TIMESTAMP;
    return sql_parse_type_params(state, 0, out_length, out_scale);
  }

  rc = sql_parser_match_word(state, "BLOB", &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (!match1) {
    rc = sql_parser_match_word(state, "BYTEA", &match3);
    if (rc != C_ORM_OK)
      return rc;
  }
  if (match1 || match3) {
    *out_type = SQL_TYPE_BLOB;
    return sql_parse_type_params(state, 0, out_length, out_scale);
  }

  return sql_parser_set_error(state, "Unknown data type");
}

/**
 * @brief Parses an inline SQL column constraint.
 * @param state Parser state.
 * @param out_constraint Pointer to receive parsed constraint.
 * @return 0 on success, non-zero on error.
 */
static c_orm_error_t
sql_parse_column_constraint(struct SqlParserState *state,
                            struct sql_constraint_t *out_constraint) {
  c_orm_error_t rc;
  int match1 = 0, match2 = 0, match3 = 0, match4 = 0;

  out_constraint->type = SQL_CONSTRAINT_NONE;
  out_constraint->reference_table = NULL;
  out_constraint->reference_column = NULL;
  out_constraint->default_value = NULL;
  out_constraint->columns = NULL;
  out_constraint->n_columns = 0;

  rc = sql_parser_match_keyword(state, "PRIMARY", &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (match1) {
    rc = sql_parser_match_keyword(state, "KEY", &match2);
    if (rc != C_ORM_OK)
      return rc;
    if (!match2) {
      return sql_parser_set_error(state, "Expected 'KEY' after 'PRIMARY'");
    }
    out_constraint->type = SQL_CONSTRAINT_PRIMARY_KEY;
    return C_ORM_OK;
  }

  rc = sql_parser_match_keyword(state, "NOT", &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (match1) {
    rc = sql_parser_match_keyword(state, "NULL", &match2);
    if (rc != C_ORM_OK)
      return rc;
    if (!match2) {
      return sql_parser_set_error(state, "Expected 'NULL' after 'NOT'");
    }
    out_constraint->type = SQL_CONSTRAINT_NOT_NULL;
    return C_ORM_OK;
  }

  rc = sql_parser_match_keyword(state, "UNIQUE", &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (match1) {
    out_constraint->type = SQL_CONSTRAINT_UNIQUE;
    return C_ORM_OK;
  }

  rc = sql_parser_match_keyword(state, "DEFAULT", &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (match1) {
    const struct sql_token_t *val_tok = NULL;

    rc = sql_parser_match_kind(state, SQL_TOKEN_STRING, &val_tok, &match1);
    if (rc != C_ORM_OK)
      return rc;
    if (!match1) {
      rc = sql_parser_match_kind(state, SQL_TOKEN_NUMBER, &val_tok, &match2);
      if (rc != C_ORM_OK)
        return rc;
    }
    if (!match1 && !match2) {
      rc =
          sql_parser_match_kind(state, SQL_TOKEN_IDENTIFIER, &val_tok, &match3);
      if (rc != C_ORM_OK)
        return rc;
    }
    if (!match1 && !match2 && !match3) {
      rc = sql_parser_match_kind(state, SQL_TOKEN_KEYWORD, &val_tok, &match4);
      if (rc != C_ORM_OK)
        return rc;
    }

    if (match1 || match2 || match3 || match4) {
      char *val = NULL;
      if (c_orm_system_malloc(val_tok->length + 1, (void **)&val) != C_ORM_OK) {
        return sql_parser_set_error(state, "OOM allocating default value");
      }
      memcpy(val, val_tok->start, val_tok->length);
      val[val_tok->length] = '\0';
      out_constraint->type = SQL_CONSTRAINT_DEFAULT;
      out_constraint->default_value = val;
      return C_ORM_OK;
    }
    return sql_parser_set_error(state, "Expected value after 'DEFAULT'");
  }

  rc = sql_parser_match_keyword(state, "REFERENCES", &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (match1) {
    const struct sql_token_t *ref_table_tok = NULL;
    rc = sql_parser_match_kind(state, SQL_TOKEN_IDENTIFIER, &ref_table_tok,
                               &match2);
    if (rc != C_ORM_OK)
      return rc;
    if (!match2) {
      return sql_parser_set_error(state,
                                  "Expected table name after 'REFERENCES'");
    }
    {
      char *tname = NULL;
      if (c_orm_system_malloc(ref_table_tok->length + 1, (void **)&tname) !=
          C_ORM_OK) {
        return sql_parser_set_error(state,
                                    "OOM allocating reference table name");
      }
      memcpy(tname, ref_table_tok->start, ref_table_tok->length);
      tname[ref_table_tok->length] = '\0';
      out_constraint->reference_table = tname;
    }

    rc = sql_parser_match_kind(state, SQL_TOKEN_LPAREN, NULL, &match3);
    if (rc != C_ORM_OK)
      goto fail_col_constraint;
    if (match3) {
      const struct sql_token_t *ref_col_tok = NULL;
      rc = sql_parser_match_kind(state, SQL_TOKEN_IDENTIFIER, &ref_col_tok,
                                 &match4);
      if (rc != C_ORM_OK)
        goto fail_col_constraint;
      if (!match4) {
        rc =
            sql_parser_set_error(state, "Expected column name in REFERENCES()");
        goto fail_col_constraint;
      }
      {
        char *cname = NULL;
        if (c_orm_system_malloc(ref_col_tok->length + 1, (void **)&cname) !=
            C_ORM_OK) {
          rc = sql_parser_set_error(state, "OOM allocating reference col name");
          goto fail_col_constraint;
        }
        memcpy(cname, ref_col_tok->start, ref_col_tok->length);
        cname[ref_col_tok->length] = '\0';
        out_constraint->reference_column = cname;
      }
      rc = sql_parser_match_kind(state, SQL_TOKEN_RPAREN, NULL, &match4);
      if (rc != C_ORM_OK)
        goto fail_col_constraint;
      if (!match4) {
        rc = sql_parser_set_error(state, "Expected ')' after reference column");
        goto fail_col_constraint;
      }
    }
    out_constraint->type = SQL_CONSTRAINT_FOREIGN_KEY;
    return C_ORM_OK;
  }

  return C_ORM_ERROR_NOT_FOUND; /* Not a constraint */

fail_col_constraint:
  sql_constraint_free_internals(out_constraint);
  return rc;
}

/**
 * @brief Parses a table-level SQL constraint.
 * @param state Parser state.
 * @param out_constraint Pointer to receive parsed constraint.
 * @return 0 on success, non-zero on error.
 */
static c_orm_error_t
sql_parse_table_constraint(struct SqlParserState *state,
                           struct sql_constraint_t *out_constraint) {
  c_orm_error_t rc;
  int match1 = 0, match2 = 0;

  out_constraint->type = SQL_CONSTRAINT_NONE;
  out_constraint->reference_table = NULL;
  out_constraint->reference_column = NULL;
  out_constraint->default_value = NULL;
  out_constraint->columns = NULL;
  out_constraint->n_columns = 0;

  rc = sql_parser_match_keyword(state, "PRIMARY", &match1);
  if (rc != C_ORM_OK) {
    goto fail_tbl_constraint;
  }
  if (match1) {
    rc = sql_parser_match_keyword(state, "KEY", &match2);
    if (rc != C_ORM_OK) {
      goto fail_tbl_constraint;
    }
    if (!match2) {
      rc = sql_parser_set_error(state, "Expected 'KEY' after 'PRIMARY'");
      goto fail_tbl_constraint;
    }
    out_constraint->type = SQL_CONSTRAINT_PRIMARY_KEY;
  } else {
    rc = sql_parser_match_keyword(state, "UNIQUE", &match1);
    if (rc != C_ORM_OK) {
      goto fail_tbl_constraint;
    }
    if (match1) {
      out_constraint->type = SQL_CONSTRAINT_UNIQUE;
    } else {
      rc = sql_parser_match_keyword(state, "FOREIGN", &match1);
      if (rc != C_ORM_OK) {
        goto fail_tbl_constraint;
      }
      rc = sql_parser_match_keyword(state, "KEY", &match2);
      if (rc != C_ORM_OK) {
        goto fail_tbl_constraint;
      }
      if (!match2) {
        rc = sql_parser_set_error(state, "Expected 'KEY' after 'FOREIGN'");
        goto fail_tbl_constraint;
      }
      out_constraint->type = SQL_CONSTRAINT_FOREIGN_KEY;
    }
  }

  rc = sql_parser_match_kind(state, SQL_TOKEN_LPAREN, NULL, &match1);
  if (rc != C_ORM_OK) {
    goto fail_tbl_constraint;
  }
  if (!match1) {
    rc = sql_parser_set_error(state, "Expected '(' after constraint type");
    goto fail_tbl_constraint;
  }

  /* Parse columns */
  while (1) {
    const struct sql_token_t *col_tok = NULL;
    rc = sql_parser_match_kind(state, SQL_TOKEN_IDENTIFIER, &col_tok, &match1);
    if (rc != C_ORM_OK) {
      goto fail_tbl_constraint;
    }
    if (!match1) {
      rc = sql_parser_set_error(state, "Expected column name in constraint");
      goto fail_tbl_constraint;
    }

    {
      char **new_cols = NULL;
      if (c_orm_system_realloc(out_constraint->columns,
                               (out_constraint->n_columns + 1) * sizeof(char *),
                               (void **)&new_cols) != C_ORM_OK) {
        rc = sql_parser_set_error(state, "OOM allocating constraint columns");
        goto fail_tbl_constraint;
      }
      out_constraint->columns = new_cols;

      {
        char *cname = NULL;
        if (c_orm_system_malloc(col_tok->length + 1, (void **)&cname) !=
            C_ORM_OK) {
          rc = sql_parser_set_error(state, "OOM allocating column name");
          goto fail_tbl_constraint;
        }
        memcpy(cname, col_tok->start, col_tok->length);
        cname[col_tok->length] = '\0';
        out_constraint->columns[out_constraint->n_columns++] = cname;
      }
    }

    rc = sql_parser_match_kind(state, SQL_TOKEN_COMMA, NULL, &match1);
    if (rc != C_ORM_OK) {
      goto fail_tbl_constraint;
    }
    if (match1) {
      continue;
    } else {
      break;
    }
  }

  rc = sql_parser_match_kind(state, SQL_TOKEN_RPAREN, NULL, &match1);
  if (rc != C_ORM_OK) {
    goto fail_tbl_constraint;
  }
  if (!match1) {
    rc = sql_parser_set_error(state, "Expected ')' after constraint columns");
    goto fail_tbl_constraint;
  }

  if (out_constraint->type == SQL_CONSTRAINT_FOREIGN_KEY) {
    rc = sql_parser_match_keyword(state, "REFERENCES", &match1);
    if (rc != C_ORM_OK) {
      goto fail_tbl_constraint;
    }
    if (!match1) {
      rc = sql_parser_set_error(
          state, "Expected 'REFERENCES' after FOREIGN KEY (...)");
      goto fail_tbl_constraint;
    }
    {
      const struct sql_token_t *ref_table_tok = NULL;
      rc = sql_parser_match_kind(state, SQL_TOKEN_IDENTIFIER, &ref_table_tok,
                                 &match1);
      if (rc != C_ORM_OK) {
        goto fail_tbl_constraint;
      }
      if (!match1) {
        rc = sql_parser_set_error(state,
                                  "Expected table name after 'REFERENCES'");
        goto fail_tbl_constraint;
      }
      {
        char *tname = NULL;
        if (c_orm_system_malloc(ref_table_tok->length + 1, (void **)&tname) !=
            C_ORM_OK) {
          rc = sql_parser_set_error(state, "OOM allocating ref table");
          goto fail_tbl_constraint;
        }
        memcpy(tname, ref_table_tok->start, ref_table_tok->length);
        tname[ref_table_tok->length] = '\0';
        out_constraint->reference_table = tname;
      }
    }

    rc = sql_parser_match_kind(state, SQL_TOKEN_LPAREN, NULL, &match1);
    if (rc != C_ORM_OK) {
      goto fail_tbl_constraint;
    }
    if (match1) {
      const struct sql_token_t *ref_col_tok = NULL;
      rc = sql_parser_match_kind(state, SQL_TOKEN_IDENTIFIER, &ref_col_tok,
                                 &match1);
      if (rc != C_ORM_OK) {
        goto fail_tbl_constraint;
      }
      if (!match1) {
        rc =
            sql_parser_set_error(state, "Expected column name in REFERENCES()");
        goto fail_tbl_constraint;
      }
      {
        char *cname = NULL;
        if (c_orm_system_malloc(ref_col_tok->length + 1, (void **)&cname) !=
            C_ORM_OK) {
          rc = sql_parser_set_error(state, "OOM allocating ref col");
          goto fail_tbl_constraint;
        }
        memcpy(cname, ref_col_tok->start, ref_col_tok->length);
        cname[ref_col_tok->length] = '\0';
        out_constraint->reference_column = cname;
      }
      rc = sql_parser_match_kind(state, SQL_TOKEN_RPAREN, NULL, &match1);
      if (rc != C_ORM_OK) {
        goto fail_tbl_constraint;
      }
      if (!match1) {
        rc = sql_parser_set_error(state, "Expected ')' after reference column");
        goto fail_tbl_constraint;
      }
    }
  }

  return C_ORM_OK;

fail_tbl_constraint:
  sql_constraint_free_internals(out_constraint);
  return rc;
}

/**
 * @brief Consumes a parenthesized `CHECK (...)` predicate expression with
 * balanced parentheses.
 * @param state Parser state positioned immediately after the `CHECK` token.
 * @return C_ORM_OK on success, or parse error code if parentheses are missing
 * or unbalanced.
 */
static c_orm_error_t sql_skip_check_clause(struct SqlParserState *state) {
  c_orm_error_t rc;
  int match_lp = 0;
  int depth = 1;

  rc = sql_parser_match_kind(state, SQL_TOKEN_LPAREN, NULL, &match_lp);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (!match_lp) {
    return sql_parser_set_error(state, "Expected '(' after CHECK");
  }
  while (depth > 0) {
    struct sql_token_t *pt = NULL;
    rc = sql_parser_peek(state, &pt);
    if (rc != C_ORM_OK) {
      return rc;
    }
    if (pt->kind == SQL_TOKEN_EOF) {
      return sql_parser_set_error(state, "Unclosed '(' in CHECK constraint");
    }
    if (pt->kind == SQL_TOKEN_LPAREN) {
      depth++;
    } else if (pt->kind == SQL_TOKEN_RPAREN) {
      depth--;
    }
    rc = sql_parser_consume(state);
    if (rc != C_ORM_OK) {
      return rc;
    }
  }
  return C_ORM_OK;
}

c_orm_error_t sql_parse_table(const struct sql_token_list_t *list,
                              struct sql_table_t **out_table,
                              struct sql_parse_error_t *out_error) {
  struct sql_token_t *_ast_sql_parser_peek_4;
  struct sql_token_t *_ast_sql_parser_peek_5;
  struct SqlParserState state;
  struct sql_table_t *table = NULL;
  const struct sql_token_t *name_tok = NULL;
  c_orm_error_t rc;
  int match1 = 0;

  if (!list || !out_table)
    return C_ORM_ERROR_VALIDATION;
  *out_table = NULL;

  state.list = list;
  state.cursor = 0;
  state.out_error = out_error;

  if (out_error) {
    out_error->message = NULL;
    out_error->token = NULL;
  }

  rc = sql_parser_match_keyword(&state, "CREATE", &match1);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (!match1) {
    return sql_parser_set_error(&state, "Expected 'CREATE'");
  }
  rc = sql_parser_match_keyword(&state, "TABLE", &match1);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (!match1) {
    return sql_parser_set_error(&state, "Expected 'TABLE'");
  }
  rc = sql_parser_match_kind(&state, SQL_TOKEN_IDENTIFIER, &name_tok, &match1);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (!match1) {
    return sql_parser_set_error(&state, "Expected table name");
  }
  {
    struct sql_token_t *dot_peek = NULL;
    rc = sql_parser_peek(&state, &dot_peek);
    if (rc != C_ORM_OK) {
      return rc;
    }
    if (dot_peek->kind == SQL_TOKEN_UNKNOWN && dot_peek->start[0] == '.') {
      const struct sql_token_t *sub_name_tok = NULL;
      int match_sub = 0;
      rc = sql_parser_consume(&state);
      if (rc != C_ORM_OK) {
        return rc;
      }
      rc = sql_parser_match_kind(&state, SQL_TOKEN_IDENTIFIER, &sub_name_tok,
                                 &match_sub);
      if (rc != C_ORM_OK) {
        return rc;
      }
      if (!match_sub) {
        return sql_parser_set_error(&state, "Expected table name after '.'");
      }
      name_tok = sub_name_tok;
    }
  }

  table = NULL;
  if (c_orm_system_calloc(1, sizeof(struct sql_table_t), (void **)&table) !=
      C_ORM_OK) {
    return sql_parser_set_error(&state, "OOM allocating table");
  }

  if (c_orm_system_malloc(name_tok->length + 1, (void **)&table->name) !=
      C_ORM_OK) {
    sql_column_cleanup(NULL, table);
    return sql_parser_set_error(&state, "OOM allocating table name");
  }
  memcpy(table->name, name_tok->start, name_tok->length);
  table->name[name_tok->length] = '\0';

  rc = sql_parser_match_kind(&state, SQL_TOKEN_LPAREN, NULL, &match1);
  if (rc != C_ORM_OK) {
    sql_column_cleanup(NULL, table);
    return rc;
  }
  if (!match1) {
    sql_column_cleanup(NULL, table);
    return sql_parser_set_error(&state, "Expected '('");
  }

  /* Parse columns and table constraints */
  while (1) {
    const struct sql_token_t *col_name_tok = NULL;
    const struct sql_token_t *peek;

    rc = sql_parser_peek(&state, &_ast_sql_parser_peek_4);
    if (rc != C_ORM_OK) {
      sql_column_cleanup(NULL, table);
      return rc;
    }
    peek = _ast_sql_parser_peek_4;

    if (peek->kind == SQL_TOKEN_RPAREN) {
      break; /* End of table definition */
    }

    /* Optional table-level `CONSTRAINT <name>` prefix or `CHECK (...)` */
    {
      int m_cns = 0;
      int m_chk = 0;
      rc = sql_parser_match_word(&state, "CONSTRAINT", &m_cns);
      if (rc != C_ORM_OK) {
        sql_column_cleanup(NULL, table);
        return rc;
      }
      if (m_cns) {
        struct sql_token_t *cn_tok = NULL;
        rc = sql_parser_peek(&state, &cn_tok);
        if (rc != C_ORM_OK) {
          sql_column_cleanup(NULL, table);
          return rc;
        }
        if (cn_tok->kind != SQL_TOKEN_IDENTIFIER &&
            cn_tok->kind != SQL_TOKEN_KEYWORD) {
          sql_column_cleanup(NULL, table);
          return sql_parser_set_error(&state, "Expected constraint name");
        }
        rc = sql_parser_consume(&state);
        if (rc != C_ORM_OK) {
          sql_column_cleanup(NULL, table);
          return rc;
        }
      }
      rc = sql_parser_match_word(&state, "CHECK", &m_chk);
      if (rc != C_ORM_OK) {
        sql_column_cleanup(NULL, table);
        return rc;
      }
      if (m_chk) {
        rc = sql_skip_check_clause(&state);
        if (rc != C_ORM_OK) {
          sql_column_cleanup(NULL, table);
          return rc;
        }
        rc = sql_parser_match_kind(&state, SQL_TOKEN_COMMA, NULL, &match1);
        if (rc != C_ORM_OK) {
          sql_column_cleanup(NULL, table);
          return rc;
        }
        if (match1) {
          continue;
        }
        break;
      }
      if (m_cns) {
        rc = sql_parser_peek(&state, &_ast_sql_parser_peek_4);
        if (rc != C_ORM_OK) {
          sql_column_cleanup(NULL, table);
          return rc;
        }
        peek = _ast_sql_parser_peek_4;
      }
    }

    /* Is it a table-level constraint? (e.g., PRIMARY KEY (id), FOREIGN KEY ...)
     */
    if (peek->kind == SQL_TOKEN_KEYWORD &&
        ((peek->length == 7 && strncmp(peek->start, "PRIMARY", 7) == 0) ||
         (peek->length == 7 && strncmp(peek->start, "FOREIGN", 7) == 0) ||
         (peek->length == 6 && strncmp(peek->start, "UNIQUE", 6) == 0))) {

      struct sql_constraint_t tc;
      rc = sql_parse_table_constraint(&state, &tc);
      if (rc != C_ORM_OK) {
        sql_column_cleanup(NULL, table);
        return rc;
      }

      {
        struct sql_constraint_t *new_tc = NULL;
        if (c_orm_system_realloc(table->table_constraints,
                                 (table->n_table_constraints + 1) *
                                     sizeof(struct sql_constraint_t),
                                 (void **)&new_tc) != C_ORM_OK) {
          sql_constraint_free_internals(&tc);
          sql_column_cleanup(NULL, table);
          return sql_parser_set_error(&state,
                                      "OOM allocating table constraints");
        }
        table->table_constraints = new_tc;
        table->table_constraints[table->n_table_constraints++] = tc;
      }

      rc = sql_parser_match_kind(&state, SQL_TOKEN_COMMA, NULL, &match1);
      if (rc != C_ORM_OK) {
        sql_column_cleanup(NULL, table);
        return rc;
      }
      if (match1) {
        continue;
      } else {
        break;
      }
    }

    /* Must be a column definition */
    rc = sql_parser_match_kind(&state, SQL_TOKEN_IDENTIFIER, &col_name_tok,
                               &match1);
    if (rc != C_ORM_OK) {
      sql_column_cleanup(NULL, table);
      return rc;
    }
    if (!match1) {
      sql_column_cleanup(NULL, table);
      return sql_parser_set_error(&state, "Expected column name");
    }

    {
      struct sql_column_t col;
      size_t constraint_capacity = 2;
      memset(&col, 0, sizeof(col));
      col.length = -1;
      col.scale = -1;

      if (c_orm_system_malloc(col_name_tok->length + 1, (void **)&col.name) !=
          C_ORM_OK) {
        sql_column_cleanup(NULL, table);
        return sql_parser_set_error(&state, "OOM allocating column name");
      }
      memcpy(col.name, col_name_tok->start, col_name_tok->length);
      col.name[col_name_tok->length] = '\0';

      rc = sql_parse_data_type_ex(&state, &col.type, &col.length, &col.scale);
      if (rc != C_ORM_OK) {
        sql_column_cleanup(&col, table);
        return rc;
      }

      if (c_orm_system_malloc(constraint_capacity *
                                  sizeof(struct sql_constraint_t),
                              (void **)&col.constraints) != C_ORM_OK) {
        sql_column_cleanup(&col, table);
        *out_table = NULL;
        return sql_parser_set_error(&state, "OOM allocating constraints");
      }
      col.n_constraints = 0;

      /* Parse inline constraints and Db2 column clauses */
      while (1) {
        struct sql_constraint_t constraint;
        int mod_match = 0;
        rc = sql_parser_peek(&state, &_ast_sql_parser_peek_5);
        if (rc != C_ORM_OK) {
          sql_column_cleanup(&col, table);
          return rc;
        }
        peek = _ast_sql_parser_peek_5;

        if (peek->kind == SQL_TOKEN_COMMA || peek->kind == SQL_TOKEN_RPAREN ||
            peek->kind == SQL_TOKEN_EOF) {
          break;
        }

        /* Optional inline `CONSTRAINT <name>` prefix and `CHECK (...)` */
        rc = sql_parser_match_word(&state, "CONSTRAINT", &mod_match);
        if (rc != C_ORM_OK) {
          sql_column_cleanup(&col, table);
          return rc;
        }
        if (mod_match) {
          struct sql_token_t *cn_tok = NULL;
          rc = sql_parser_peek(&state, &cn_tok);
          if (rc != C_ORM_OK) {
            sql_column_cleanup(&col, table);
            return rc;
          }
          if (cn_tok->kind != SQL_TOKEN_IDENTIFIER &&
              cn_tok->kind != SQL_TOKEN_KEYWORD) {
            rc = sql_parser_set_error(&state, "Expected constraint name");
            sql_column_cleanup(&col, table);
            return rc;
          }
          rc = sql_parser_consume(&state);
          if (rc != C_ORM_OK) {
            sql_column_cleanup(&col, table);
            return rc;
          }
          continue;
        }

        rc = sql_parser_match_word(&state, "CHECK", &mod_match);
        if (rc != C_ORM_OK) {
          sql_column_cleanup(&col, table);
          return rc;
        }
        if (mod_match) {
          rc = sql_skip_check_clause(&state);
          if (rc != C_ORM_OK) {
            sql_column_cleanup(&col, table);
            return rc;
          }
          continue;
        }

        /* Db2 GENERATED ALWAYS / BY DEFAULT AS IDENTITY (...) */
        rc = sql_parser_match_word(&state, "GENERATED", &mod_match);
        if (rc != C_ORM_OK) {
          sql_column_cleanup(&col, table);
          return rc;
        }
        if (mod_match) {
          int m_alw = 0, m_by = 0, m_def = 0, m_as = 0, m_id = 0, m_lp = 0;
          rc = sql_parser_match_word(&state, "ALWAYS", &m_alw);
          if (rc == C_ORM_OK && !m_alw) {
            rc = sql_parser_match_word(&state, "BY", &m_by);
            if (rc == C_ORM_OK && m_by) {
              rc = sql_parser_match_word(&state, "DEFAULT", &m_def);
            }
          }
          if (rc == C_ORM_OK) {
            rc = sql_parser_match_word(&state, "AS", &m_as);
          }
          if (rc == C_ORM_OK) {
            rc = sql_parser_match_word(&state, "IDENTITY", &m_id);
          }
          if (rc != C_ORM_OK) {
            sql_column_cleanup(&col, table);
            return rc;
          }
          if (!m_id || (!m_alw && !m_def) || !m_as) {
            rc = sql_parser_set_error(&state, "Malformed GENERATED clause");
            sql_column_cleanup(&col, table);
            return rc;
          }
          col.is_identity = 1;
          rc = sql_parser_match_kind(&state, SQL_TOKEN_LPAREN, NULL, &m_lp);
          if (rc != C_ORM_OK) {
            sql_column_cleanup(&col, table);
            return rc;
          }
          if (m_lp) {
            int depth = 1;
            while (depth > 0) {
              struct sql_token_t *pt = NULL;
              rc = sql_parser_peek(&state, &pt);
              if (rc != C_ORM_OK) {
                sql_column_cleanup(&col, table);
                return rc;
              }
              if (pt->kind == SQL_TOKEN_EOF) {
                break;
              }
              if (pt->kind == SQL_TOKEN_LPAREN) {
                depth++;
              } else if (pt->kind == SQL_TOKEN_RPAREN) {
                depth--;
              }
              rc = sql_parser_consume(&state);
              if (rc != C_ORM_OK) {
                sql_column_cleanup(&col, table);
                return rc;
              }
            }
          }
          continue;
        }

        /* Db2 FOR BIT DATA / FOR SBCS DATA / FOR MIXED DATA */
        rc = sql_parser_match_word(&state, "FOR", &mod_match);
        if (rc != C_ORM_OK) {
          sql_column_cleanup(&col, table);
          return rc;
        }
        if (mod_match) {
          int m_bit = 0, m_sbcs = 0, m_mix = 0, m_data = 0;
          rc = sql_parser_match_word(&state, "BIT", &m_bit);
          if (rc == C_ORM_OK && !m_bit) {
            rc = sql_parser_match_word(&state, "SBCS", &m_sbcs);
          }
          if (rc == C_ORM_OK && !m_bit && !m_sbcs) {
            rc = sql_parser_match_word(&state, "MIXED", &m_mix);
          }
          if (rc == C_ORM_OK) {
            rc = sql_parser_match_word(&state, "DATA", &m_data);
          }
          if (rc != C_ORM_OK) {
            sql_column_cleanup(&col, table);
            return rc;
          }
          if ((!m_bit && !m_sbcs && !m_mix) || !m_data) {
            rc = sql_parser_set_error(&state, "Expected BIT/SBCS/MIXED DATA");
            sql_column_cleanup(&col, table);
            return rc;
          }
          if (m_bit) {
            col.is_for_bit_data = 1;
            col.ccsid = 65535U;
            if (col.type == SQL_TYPE_CHAR) {
              col.type = SQL_TYPE_BINARY;
            } else if (col.type == SQL_TYPE_VARCHAR) {
              col.type = SQL_TYPE_VARBINARY;
            }
          } else {
            col.is_for_bit_data = 0;
          }
          continue;
        }

        /* Db2 CCSID EBCDIC / UNICODE / ASCII / <number> */
        rc = sql_parser_match_word(&state, "CCSID", &mod_match);
        if (rc != C_ORM_OK) {
          sql_column_cleanup(&col, table);
          return rc;
        }
        if (mod_match) {
          int m_eb = 0, m_un = 0, m_as = 0, m_num = 0;
          const struct sql_token_t *ntok = NULL;
          rc = sql_parser_match_word(&state, "EBCDIC", &m_eb);
          if (rc != C_ORM_OK) {
            sql_column_cleanup(&col, table);
            return rc;
          }
          if (m_eb) {
            col.ccsid = 37U;
            continue;
          }
          rc = sql_parser_match_word(&state, "UNICODE", &m_un);
          if (rc != C_ORM_OK) {
            sql_column_cleanup(&col, table);
            return rc;
          }
          if (m_un) {
            col.ccsid = 1208U;
            continue;
          }
          rc = sql_parser_match_word(&state, "ASCII", &m_as);
          if (rc != C_ORM_OK) {
            sql_column_cleanup(&col, table);
            return rc;
          }
          if (m_as) {
            col.ccsid = 367U;
            continue;
          }
          rc = sql_parser_match_kind(&state, SQL_TOKEN_NUMBER, &ntok, &m_num);
          if (rc != C_ORM_OK) {
            sql_column_cleanup(&col, table);
            return rc;
          }
          if (m_num) {
            size_t ni;
            unsigned int cv = 0;
            for (ni = 0; ni < ntok->length; ++ni) {
              cv = cv * 10U + (unsigned int)(ntok->start[ni] - '0');
            }
            col.ccsid = (unsigned short)cv;
            continue;
          }
          rc = sql_parser_set_error(&state, "Invalid CCSID specification");
          sql_column_cleanup(&col, table);
          return rc;
        }

        /* Db2 WITH DEFAULT [<expr>] */
        rc = sql_parser_match_word(&state, "WITH", &mod_match);
        if (rc != C_ORM_OK) {
          sql_column_cleanup(&col, table);
          return rc;
        }
        if (mod_match) {
          int m_def = 0;
          int m_cur = 0;
          const char *def_lit = "DEFAULT";
          size_t def_len = 7;
          char *dval = NULL;

          rc = sql_parser_match_word(&state, "DEFAULT", &m_def);
          if (rc != C_ORM_OK) {
            sql_column_cleanup(&col, table);
            return rc;
          }
          if (!m_def) {
            rc = sql_parser_set_error(&state, "Expected DEFAULT after WITH");
            sql_column_cleanup(&col, table);
            return rc;
          }
          rc = sql_parser_match_word(&state, "CURRENT", &m_cur);
          if (rc != C_ORM_OK) {
            sql_column_cleanup(&col, table);
            return rc;
          }
          if (m_cur) {
            int m_ts = 0, m_dt = 0, m_tm = 0;
            rc = sql_parser_match_word(&state, "TIMESTAMP", &m_ts);
            if (rc == C_ORM_OK && !m_ts) {
              rc = sql_parser_match_word(&state, "DATE", &m_dt);
            }
            if (rc == C_ORM_OK && !m_ts && !m_dt) {
              rc = sql_parser_match_word(&state, "TIME", &m_tm);
            }
            if (rc != C_ORM_OK) {
              sql_column_cleanup(&col, table);
              return rc;
            }
            if (m_ts) {
              def_lit = "CURRENT TIMESTAMP";
              def_len = 17;
            } else if (m_dt) {
              def_lit = "CURRENT DATE";
              def_len = 12;
            } else if (m_tm) {
              def_lit = "CURRENT TIME";
              def_len = 12;
            } else {
              def_lit = "CURRENT";
              def_len = 7;
            }
          } else {
            struct sql_token_t *dtok = NULL;
            rc = sql_parser_peek(&state, &dtok);
            if (rc != C_ORM_OK) {
              sql_column_cleanup(&col, table);
              return rc;
            }
            if (dtok->kind == SQL_TOKEN_NUMBER ||
                dtok->kind == SQL_TOKEN_STRING) {
              def_lit = dtok->start;
              def_len = dtok->length;
              rc = sql_parser_consume(&state);
              if (rc != C_ORM_OK) {
                sql_column_cleanup(&col, table);
                return rc;
              }
            } else if (dtok->kind == SQL_TOKEN_KEYWORD && dtok->length == 4 &&
                       strncmp(dtok->start, "NULL", 4) == 0) {
              def_lit = "NULL";
              def_len = 4;
              rc = sql_parser_consume(&state);
              if (rc != C_ORM_OK) {
                sql_column_cleanup(&col, table);
                return rc;
              }
            }
          }

          memset(&constraint, 0, sizeof(constraint));
          constraint.type = SQL_CONSTRAINT_DEFAULT;
          if (c_orm_system_malloc(def_len + 1, (void **)&dval) != C_ORM_OK) {
            sql_column_cleanup(&col, table);
            return C_ORM_ERROR_MEMORY;
          }
          memcpy(dval, def_lit, def_len);
          dval[def_len] = '\0';
          constraint.default_value = dval;

          if (col.n_constraints >= constraint_capacity) {
            struct sql_constraint_t *new_constraints = NULL;
            constraint_capacity *= 2;
            if (c_orm_system_realloc(col.constraints,
                                     constraint_capacity *
                                         sizeof(struct sql_constraint_t),
                                     (void **)&new_constraints) != C_ORM_OK) {
              sql_constraint_free_internals(&constraint);
              sql_column_cleanup(&col, table);
              return C_ORM_ERROR_MEMORY;
            }
            col.constraints = new_constraints;
          }
          col.constraints[col.n_constraints++] = constraint;
          continue;
        }

        {
          c_orm_error_t parse_rc =
              sql_parse_column_constraint(&state, &constraint);
          if (parse_rc != C_ORM_OK) {
            if (parse_rc == C_ORM_ERROR_NOT_FOUND) {
              break;
            }
            sql_column_cleanup(&col, table);
            return parse_rc;
          }
        }
        if (col.n_constraints >= constraint_capacity) {
          struct sql_constraint_t *new_constraints = NULL;
          constraint_capacity *= 2;
          if (c_orm_system_realloc(col.constraints,
                                   constraint_capacity *
                                       sizeof(struct sql_constraint_t),
                                   (void **)&new_constraints) != C_ORM_OK) {
            sql_constraint_free_internals(&constraint);
            sql_column_cleanup(&col, table);
            return C_ORM_ERROR_MEMORY;
          }
          col.constraints = new_constraints;
        }
        col.constraints[col.n_constraints++] = constraint;
      }

      /* Add column to table */
      {
        size_t new_cap = table->n_columns + 1;
        struct sql_column_t *new_cols = NULL;
        if (c_orm_system_realloc(table->columns,
                                 new_cap * sizeof(struct sql_column_t),
                                 (void **)&new_cols) != C_ORM_OK) {
          sql_column_cleanup(&col, table);
          *out_table = NULL;
          return sql_parser_set_error(&state, "OOM allocating column");
        }
        table->columns = new_cols;
        table->columns[table->n_columns++] = col;
      }
    }

    rc = sql_parser_match_kind(&state, SQL_TOKEN_COMMA, NULL, &match1);
    if (rc != C_ORM_OK) {
      sql_column_cleanup(NULL, table);
      return rc;
    }
    if (match1) {
      continue;
    } else {
      break;
    }
  }

  rc = sql_parser_match_kind(&state, SQL_TOKEN_RPAREN, NULL, &match1);
  if (rc != C_ORM_OK) {
    sql_column_cleanup(NULL, table);
    return rc;
  }
  if (!match1) {
    sql_column_cleanup(NULL, table);
    return sql_parser_set_error(&state,
                                "Expected ')' at end of table definition");
  }

  rc = sql_parser_match_kind(&state, SQL_TOKEN_SEMICOLON, NULL, &match1);
  if (rc != C_ORM_OK) {
    sql_column_cleanup(NULL, table);
    return rc;
  }
  /* Optional semicolon */

  *out_table = table;
  return C_ORM_OK;
}

c_orm_error_t parse_sql_ddl(const char *sql_data,
                            struct sql_table_t **out_tables,
                            size_t *out_n_tables) {
  struct sql_token_list_t *list = NULL;
  struct sql_table_t *table = NULL;
  struct sql_parse_error_t err;
  size_t capacity = 8;
  az_span span;
  c_orm_error_t rc;

  if (!sql_data || !out_tables || !out_n_tables)
    return C_ORM_ERROR_VALIDATION;

  *out_tables = NULL;
  *out_n_tables = 0;

  span = az_span_create_from_str(sql_data);
  rc = sql_lex(span, &list);
  if (rc != C_ORM_OK)
    return rc;

  if (c_orm_system_calloc(capacity, sizeof(struct sql_table_t),
                          (void **)out_tables) != C_ORM_OK) {
    (void)sql_token_list_free(list);
    return C_ORM_ERROR_MEMORY;
  }

  {
    struct sql_token_list_t sublist;
    size_t i;
    size_t start_idx = 0;
    int in_table = 0;

    for (i = 0; i < list->size; ++i) {
      if (list->tokens[i].kind == SQL_TOKEN_KEYWORD) {
        if (list->tokens[i].length == 6 &&
            strncmp(list->tokens[i].start, "CREATE", 6) == 0) {
          start_idx = i;
          in_table = 1;
        }
      }

      if (in_table && list->tokens[i].kind == SQL_TOKEN_SEMICOLON) {
        /* Parse this slice */
        sublist.tokens = &list->tokens[start_idx];
        sublist.size = i - start_idx + 1;
        sublist.capacity = sublist.size;

        table = NULL;
        rc = sql_parse_table(&sublist, &table, &err);
        if (rc == C_ORM_OK) {
          if (*out_n_tables >= capacity) {
            size_t new_cap = capacity * 2;
            struct sql_table_t *grown = NULL;
            if (c_orm_system_realloc(*out_tables,
                                     new_cap * sizeof(struct sql_table_t),
                                     (void **)&grown) != C_ORM_OK) {
              size_t t;
              sql_column_cleanup(NULL, table);
              for (t = 0; t < *out_n_tables; ++t) {
                (void)sql_table_free(&(*out_tables)[t]);
              }
              (void)c_orm_system_free(*out_tables);
              *out_tables = NULL;
              *out_n_tables = 0;
              (void)sql_token_list_free(list);
              return C_ORM_ERROR_MEMORY;
            }
            memset(grown + capacity, 0,
                   (new_cap - capacity) * sizeof(struct sql_table_t));
            *out_tables = grown;
            capacity = new_cap;
          }
          memcpy(&(*out_tables)[*out_n_tables], table,
                 sizeof(struct sql_table_t));
          (void)c_orm_system_free(
              table); /* Shallow free, we copied the struct */
          (*out_n_tables)++;
        }
        in_table = 0;
      }
    }
  }

  return sql_token_list_free(list);
}

/**
 * @brief Advances index past any whitespace or comment tokens in `list`.
 * @param list Token list.
 * @param idx Starting token index.
 * @param out_idx Output index of next non-trivia token (or `list->size`).
 * @return C_ORM_OK on success.
 */
static c_orm_error_t sql_skip_trivia_idx(const struct sql_token_list_t *list,
                                         size_t idx, size_t *out_idx) {
  while (idx < list->size && list->tokens[idx].kind == SQL_TOKEN_WHITESPACE) {
    idx++;
  }
  *out_idx = idx;
  return C_ORM_OK;
}

/**
 * @brief Parses projection items starting after SELECT or RETURNING keyword.
 * @param list Token list.
 * @param start_idx Token index immediately after SELECT or RETURNING.
 * @param proj Initialized projection output structure.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on allocation failure.
 */
static c_orm_error_t
sql_parse_projection_items(const struct sql_token_list_t *list,
                           size_t start_idx, cdd_c_query_projection_t *proj) {
  size_t idx = start_idx;
  while (idx < list->size) {
    const struct sql_token_t *tok;
    cdd_c_query_projection_field_t field;
    char orig_buf[128];
    char alias_buf[128];
    size_t copy_len;
    size_t next_idx = 0;

    sql_skip_trivia_idx(list, idx, &idx);
    if (idx >= list->size) {
      break;
    }
    tok = &list->tokens[idx];

    if (tok->kind == SQL_TOKEN_EOF || tok->kind == SQL_TOKEN_SEMICOLON) {
      break;
    }
    if (tok->kind == SQL_TOKEN_KEYWORD && tok->length == 4 &&
        strncmp(tok->start, "FROM", 4) == 0) {
      break;
    }
    if (tok->kind == SQL_TOKEN_COMMA) {
      idx++;
      continue;
    }
    if (tok->kind == SQL_TOKEN_KEYWORD && tok->length == 8 &&
        strncmp(tok->start, "DISTINCT", 8) == 0) {
      idx++;
      continue;
    }

    memset(&field, 0, sizeof(field));
    field.type = SQL_TYPE_UNKNOWN;
    copy_len =
        tok->length < sizeof(orig_buf) - 1 ? tok->length : sizeof(orig_buf) - 1;
    memcpy(orig_buf, tok->start, copy_len);
    orig_buf[copy_len] = '\0';
    idx++;

    sql_skip_trivia_idx(list, idx, &idx);

    /* Handle table.col qualification */
    if (idx < list->size && list->tokens[idx].length == 1 &&
        list->tokens[idx].start[0] == '.') {
      sql_skip_trivia_idx(list, idx + 1, &next_idx);
      if (next_idx < list->size &&
          list->tokens[next_idx].kind != SQL_TOKEN_EOF) {
        const struct sql_token_t *col_tok = &list->tokens[next_idx];
        copy_len = col_tok->length < sizeof(orig_buf) - 1
                       ? col_tok->length
                       : sizeof(orig_buf) - 1;
        memcpy(orig_buf, col_tok->start, copy_len);
        orig_buf[copy_len] = '\0';
        idx = next_idx + 1;
      }
    }

    sql_skip_trivia_idx(list, idx, &idx);

    /* Handle aggregate / function call FUNC(...) */
    if (idx < list->size && list->tokens[idx].kind == SQL_TOKEN_LPAREN) {
      int depth = 1;
      field.is_aggregate = 1;
      idx++;
      while (idx < list->size && depth > 0) {
        if (list->tokens[idx].kind == SQL_TOKEN_LPAREN)
          depth++;
        else if (list->tokens[idx].kind == SQL_TOKEN_RPAREN)
          depth--;
        idx++;
      }
    }

    memcpy(alias_buf, orig_buf, strlen(orig_buf) + 1);

    sql_skip_trivia_idx(list, idx, &idx);

    /* Optional AS <alias> */
    if (idx < list->size && list->tokens[idx].length == 2 &&
        strncmp(list->tokens[idx].start, "AS", 2) == 0) {
      sql_skip_trivia_idx(list, idx + 1, &next_idx);
      if (next_idx < list->size &&
          list->tokens[next_idx].kind != SQL_TOKEN_EOF) {
        const struct sql_token_t *al_tok = &list->tokens[next_idx];
        copy_len = al_tok->length < sizeof(alias_buf) - 1
                       ? al_tok->length
                       : sizeof(alias_buf) - 1;
        memcpy(alias_buf, al_tok->start, copy_len);
        alias_buf[copy_len] = '\0';
        idx = next_idx + 1;
      }
    }

    field.original_name = orig_buf;
    field.name = alias_buf;
    if (cdd_c_query_projection_add_field(proj, &field) != C_ORM_OK) {
      return C_ORM_ERROR_MEMORY;
    }
  }
  return C_ORM_OK;
}

c_orm_error_t sql_parse_select(const struct sql_token_list_t *list,
                               struct CddCQueryProjection **out_proj,
                               struct sql_parse_error_t *out_error) {
  c_orm_error_t rc;
  size_t i;

  if (out_error) {
    out_error->message = NULL;
    out_error->token = NULL;
  }
  if (!out_proj) {
    return C_ORM_OK;
  }

  *out_proj = NULL;
  if (c_orm_system_malloc(sizeof(struct CddCQueryProjection),
                          (void **)out_proj) != C_ORM_OK) {
    return C_ORM_ERROR_MEMORY;
  }
  cdd_c_query_projection_init((cdd_c_query_projection_t *)*out_proj);

  if (list && list->tokens) {
    for (i = 0; i < list->size; ++i) {
      if (list->tokens[i].kind == SQL_TOKEN_KEYWORD &&
          list->tokens[i].length == 6 &&
          strncmp(list->tokens[i].start, "SELECT", 6) == 0) {
        rc = sql_parse_projection_items(list, i + 1,
                                        (cdd_c_query_projection_t *)*out_proj);
        if (rc != C_ORM_OK) {
          (void)cdd_c_query_projection_free(
              (cdd_c_query_projection_t *)*out_proj);
          (void)c_orm_system_free(*out_proj);
          *out_proj = NULL;
          return rc;
        }
        break;
      }
    }
  }

  return C_ORM_OK;
}

c_orm_error_t sql_parse_returning(const struct sql_token_list_t *list,
                                  struct CddCQueryProjection **out_proj,
                                  struct sql_parse_error_t *out_error) {
  c_orm_error_t rc;
  size_t i;

  if (out_error) {
    out_error->message = NULL;
    out_error->token = NULL;
  }
  if (!out_proj) {
    return C_ORM_OK;
  }

  *out_proj = NULL;
  if (c_orm_system_malloc(sizeof(struct CddCQueryProjection),
                          (void **)out_proj) != C_ORM_OK) {
    return C_ORM_ERROR_MEMORY;
  }
  cdd_c_query_projection_init((cdd_c_query_projection_t *)*out_proj);

  if (list && list->tokens) {
    for (i = 0; i < list->size; ++i) {
      if (list->tokens[i].kind == SQL_TOKEN_KEYWORD &&
          list->tokens[i].length == 9 &&
          strncmp(list->tokens[i].start, "RETURNING", 9) == 0) {
        rc = sql_parse_projection_items(list, i + 1,
                                        (cdd_c_query_projection_t *)*out_proj);
        if (rc != C_ORM_OK) {
          (void)cdd_c_query_projection_free(
              (cdd_c_query_projection_t *)*out_proj);
          (void)c_orm_system_free(*out_proj);
          *out_proj = NULL;
          return rc;
        }
        break;
      }
    }
  }

  return C_ORM_OK;
}
