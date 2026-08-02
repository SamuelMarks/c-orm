/**
 * @file sql.c
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
 * @return 1 if keyword, 0 otherwise.
 */
static c_orm_error_t is_sql_keyword(const char *str, size_t len,
                                    int *out_is_kw) {
  /* Simple exact matching for now; can be expanded. */
  static const char *keywords[] = {
      "CREATE",     "TABLE",  "INT",       "INTEGER",   "BIGINT",  "VARCHAR",
      "TEXT",       "CHAR",   "FLOAT",     "DOUBLE",    "DECIMAL", "BOOLEAN",
      "BOOL",       "DATE",   "TIMESTAMP", "PRIMARY",   "KEY",     "FOREIGN",
      "REFERENCES", "UNIQUE", "NOT",       "NULL",      "DEFAULT", "SELECT",
      "INSERT",     "INTO",   "VALUES",    "RETURNING", "UPDATE",  "SET",
      "DELETE",     "FROM",   "WHERE",     "AS",        NULL};
  int i;
  for (i = 0; keywords[i] != NULL; ++i) {
    if (strlen(keywords[i]) == len &&
        strncmp(str, keywords[i], len) ==
            0) { /* Case sensitive for simplicity now; SQL usually case
                    insensitive, but we'll improve later */
      {
        if (out_is_kw)
          *out_is_kw = 1;

        return C_ORM_OK;
      }
    }
  }
  {
    if (out_is_kw)
      *out_is_kw = 0;

    return C_ORM_OK;
  }
}

/**
 * @brief Push a token into the list.
 */
static c_orm_error_t push_token(struct sql_token_list_t *list,
                                enum SqlTokenKind kind, const char *start,
                                size_t length) {
  if (list->size >= list->capacity) {
    size_t new_cap = list->capacity == 0 ? 16 : list->capacity * 2;
    struct sql_token_t *new_tokens = (struct sql_token_t *)C_ORM_REALLOC(
        list->tokens, new_cap * sizeof(struct sql_token_t));
    if (!new_tokens) {
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

c_orm_error_t sql_lex(az_span source, struct sql_token_list_t **out_list) {
  struct sql_token_list_t *list;
  const char *curr;
  const char *end;
  int err;

  if (!source._internal.ptr || !out_list) {
    return 1;
  }

  list =
      (struct sql_token_list_t *)C_ORM_MALLOC(sizeof(struct sql_token_list_t));
  if (!list) {
    return 1;
  }
  memset(list, 0, sizeof(struct sql_token_list_t));

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

    if (*curr == '\'') {
      const char *start = curr;
      curr++;
      while (curr < end && *curr != '\'') {
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
  return 0;

fail:
  sql_token_list_free(list);
  return 1;
}

c_orm_error_t sql_token_list_free(struct sql_token_list_t *list) {
  if (list) {
    if (list->tokens) {
      C_ORM_FREE(list->tokens);
    }
    C_ORM_FREE(list);
  }
  return 0;
}

static void sql_constraint_free_internals(struct sql_constraint_t *c) {
  if (c->reference_table)
    C_ORM_FREE(c->reference_table);
  if (c->reference_column)
    C_ORM_FREE(c->reference_column);
  if (c->default_value)
    C_ORM_FREE(c->default_value);
  if (c->columns) {
    size_t k;
    for (k = 0; k < c->n_columns; ++k) {
      if (c->columns[k])
        C_ORM_FREE(c->columns[k]);
    }
    C_ORM_FREE(c->columns);
  }
  c->reference_table = NULL;
  c->reference_column = NULL;
  c->default_value = NULL;
  c->columns = NULL;
  c->n_columns = 0;
}

C_ORM_EXPORT c_orm_error_t sql_table_C_ORM_FREE(struct sql_table_t *table) {
  if (table) {
    size_t i;
    for (i = 0; i < table->n_columns; ++i) {
      struct sql_column_t *col = &table->columns[i];
      if (col->name)
        C_ORM_FREE(col->name);
      if (col->constraints) {
        size_t j;
        for (j = 0; j < col->n_constraints; ++j) {
          sql_constraint_free_internals(&col->constraints[j]);
        }
        C_ORM_FREE(col->constraints);
      }
    }
    if (table->columns)
      C_ORM_FREE(table->columns);
    if (table->name)
      C_ORM_FREE(table->name);

    for (i = 0; i < table->n_table_constraints; ++i) {
      sql_constraint_free_internals(&table->table_constraints[i]);
    }
    if (table->table_constraints)
      C_ORM_FREE(table->table_constraints);

    /* C_ORM_FREE(table); removed */
  }
  return 0;
}

/** @brief SqlParserState struct */
struct SqlParserState {
  /** @brief cursor field */
  const struct sql_token_list_t *list;
  /** @brief cursor field */
  size_t cursor;
  struct sql_parse_error_t *out_error;
};

static int g_parser_fail_countdown = -1;
C_ORM_EXPORT c_orm_error_t c_orm_parser_set_fail(int count) {
  g_parser_fail_countdown = count;
  return C_ORM_OK;
}

static c_orm_error_t sql_parser_peek(struct SqlParserState *state,
                                     struct sql_token_t **_out_val) {
  if (g_parser_fail_countdown == 0) {
    g_parser_fail_countdown--;
    return C_ORM_ERROR_UNKNOWN;
  }
  if (g_parser_fail_countdown > 0)
    g_parser_fail_countdown--;
  size_t c = state->cursor;
  while (c < state->list->size &&
         state->list->tokens[c].kind == SQL_TOKEN_WHITESPACE) {
    c++;
  }
  if (c < state->list->size) {
    {
      *_out_val = &state->list->tokens[c];
      return 0;
    }
  }
  {
    *_out_val = NULL;
    return 0;
  }
}

static c_orm_error_t sql_parser_consume(struct SqlParserState *state) {
  if (g_parser_fail_countdown == 0) {
    g_parser_fail_countdown--;
    return C_ORM_ERROR_UNKNOWN;
  }
  if (g_parser_fail_countdown > 0)
    g_parser_fail_countdown--;
  while (state->cursor < state->list->size &&
         state->list->tokens[state->cursor].kind == SQL_TOKEN_WHITESPACE) {
    state->cursor++;
  }
  if (state->cursor < state->list->size) {
    state->cursor++;
  }
  return C_ORM_OK;
}

static c_orm_error_t sql_parser_match_keyword(struct SqlParserState *state,
                                              const char *kw, int *out_match) {
  c_orm_error_t rc;
  struct sql_token_t *_ast_sql_parser_peek_0 = NULL;
  const struct sql_token_t *tok;
  rc = sql_parser_peek(state, &_ast_sql_parser_peek_0);
  if (rc != C_ORM_OK)
    return rc;
  tok = _ast_sql_parser_peek_0;
  if (tok && tok->kind == SQL_TOKEN_KEYWORD) {
    if (tok->length == strlen(kw) &&
        strncmp(tok->start, kw, tok->length) == 0) {
      rc = sql_parser_consume(state);
      if (rc != C_ORM_OK)
        return rc;
      if (out_match)
        *out_match = 1;
      return C_ORM_OK;
    }
  }
  if (out_match)
    *out_match = 0;
  return C_ORM_OK;
}

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
  if (tok && tok->kind == kind) {
    if (out_tok) {
      *out_tok = tok;
    }
    rc = sql_parser_consume(state);
    if (rc != C_ORM_OK)
      return rc;
    if (out_match)
      *out_match = 1;
    return C_ORM_OK;
  }
  if (out_match)
    *out_match = 0;
  return C_ORM_OK;
}

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

static c_orm_error_t sql_parse_data_type(struct SqlParserState *state,
                                         enum SqlDataType *out_type,
                                         int *out_length) {
  c_orm_error_t rc;
  int match1 = 0, match2 = 0, match3 = 0;
  struct sql_token_t *_ast_sql_parser_peek_3 = NULL;
  const struct sql_token_t *tok;

  rc = sql_parser_peek(state, &_ast_sql_parser_peek_3);
  if (rc != C_ORM_OK)
    return rc;
  tok = _ast_sql_parser_peek_3;
  if (!tok || tok->kind != SQL_TOKEN_KEYWORD) {
    {
      rc = sql_parser_set_error(state, "Expected data type");
      if (rc != C_ORM_OK)
        return rc;
      return C_ORM_ERROR_SQL;
    }
  }

  *out_length = -1;

  rc = sql_parser_match_keyword(state, "INT", &match1);
  if (rc != C_ORM_OK)
    return rc;
  rc = sql_parser_match_keyword(state, "INTEGER", &match2);
  if (rc != C_ORM_OK)
    return rc;

  if (match1 || match2) {
    *out_type = SQL_TYPE_INT;
    return C_ORM_OK;
  }

  rc = sql_parser_match_keyword(state, "BIGINT", &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (match1) {
    *out_type = SQL_TYPE_BIGINT;
    return C_ORM_OK;
  }

  rc = sql_parser_match_keyword(state, "VARCHAR", &match1);
  if (rc != C_ORM_OK)
    return rc;
  rc = sql_parser_match_keyword(state, "TEXT", &match2);
  if (rc != C_ORM_OK)
    return rc;
  rc = sql_parser_match_keyword(state, "CHAR", &match3);
  if (rc != C_ORM_OK)
    return rc;

  if (match1 || match2 || match3) {
    if (strncmp(tok->start, "VARCHAR", 7) == 0)
      *out_type = SQL_TYPE_VARCHAR;
    else if (strncmp(tok->start, "TEXT", 4) == 0)
      *out_type = SQL_TYPE_TEXT;
    else
      *out_type = SQL_TYPE_CHAR;

    rc = sql_parser_match_kind(state, SQL_TOKEN_LPAREN, NULL, &match1);
    if (rc != C_ORM_OK)
      return rc;
    if (match1) {
      const struct sql_token_t *num_tok = NULL;
      rc = sql_parser_match_kind(state, SQL_TOKEN_NUMBER, &num_tok, &match2);
      if (rc != C_ORM_OK)
        return rc;
      if (match2) {
        /* simplistic string to int */
        int val = 0;
        size_t i;
        for (i = 0; i < num_tok->length; ++i) {
          val = val * 10 + (num_tok->start[i] - "0"[0]);
        }
        *out_length = val;
      }
      rc = sql_parser_match_kind(state, SQL_TOKEN_RPAREN, NULL, &match3);
      if (rc != C_ORM_OK)
        return rc;
      if (!match3) {
        {
          c_orm_error_t _e =
              sql_parser_set_error(state, "Expected ) after length");
          if (_e != C_ORM_OK)
            return _e;
          return _e;
        }
      }
    }
    return C_ORM_OK;
  }

  rc = sql_parser_match_keyword(state, "FLOAT", &match1);
  if (rc != C_ORM_OK)
    return rc;
  rc = sql_parser_match_keyword(state, "DOUBLE", &match2);
  if (rc != C_ORM_OK)
    return rc;
  rc = sql_parser_match_keyword(state, "DECIMAL", &match3);
  if (rc != C_ORM_OK)
    return rc;

  if (match1 || match2 || match3) {
    if (strncmp(tok->start, "FLOAT", 5) == 0)
      *out_type = SQL_TYPE_FLOAT;
    else if (strncmp(tok->start, "DOUBLE", 6) == 0)
      *out_type = SQL_TYPE_DOUBLE;
    else
      *out_type = SQL_TYPE_DECIMAL;
    return C_ORM_OK;
  }

  rc = sql_parser_match_keyword(state, "BOOLEAN", &match1);
  if (rc != C_ORM_OK)
    return rc;
  rc = sql_parser_match_keyword(state, "BOOL", &match2);
  if (rc != C_ORM_OK)
    return rc;

  if (match1 || match2) {
    *out_type = SQL_TYPE_BOOLEAN;
    return C_ORM_OK;
  }

  rc = sql_parser_match_keyword(state, "DATE", &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (match1) {
    *out_type = SQL_TYPE_DATE;
    return C_ORM_OK;
  }

  rc = sql_parser_match_keyword(state, "TIMESTAMP", &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (match1) {
    *out_type = SQL_TYPE_TIMESTAMP;
    return C_ORM_OK;
  }

  {
    rc = sql_parser_set_error(state, "Unknown data type");
    if (rc != C_ORM_OK)
      return rc;
    return C_ORM_ERROR_SQL;
  }
}

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
      {
        c_orm_error_t _e =
            sql_parser_set_error(state, "Expected 'KEY' after 'PRIMARY'");
        if (_e != C_ORM_OK)
          return _e;
        return _e;
      }
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
      {
        c_orm_error_t _e =
            sql_parser_set_error(state, "Expected 'NULL' after 'NOT'");
        if (_e != C_ORM_OK)
          return _e;
        return _e;
      }
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
      char *val = (char *)C_ORM_MALLOC(val_tok->length + 1);
      if (!val) {
        c_orm_error_t _e =
            sql_parser_set_error(state, "OOM allocating default value");
        if (_e != C_ORM_OK)
          return _e;
        return _e;
      }
      memcpy(val, val_tok->start, val_tok->length);
      val[val_tok->length] = '\0';
      out_constraint->type = SQL_CONSTRAINT_DEFAULT;
      out_constraint->default_value = val;
      return C_ORM_OK;
    }
    {
      c_orm_error_t _e =
          sql_parser_set_error(state, "Expected value after 'DEFAULT'");
      if (_e != C_ORM_OK)
        return _e;
      return _e;
    }
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
      {
        rc = sql_parser_set_error(state,
                                  "Expected table name after 'REFERENCES'");
        if (rc != C_ORM_OK)
          return rc;
        return C_ORM_ERROR_SQL;
      }
    }
    {
      char *tname = (char *)C_ORM_MALLOC(ref_table_tok->length + 1);
      if (!tname) {
        c_orm_error_t _e =
            sql_parser_set_error(state, "OOM allocating reference table name");
        if (_e != C_ORM_OK)
          return _e;
        return _e;
      }
      memcpy(tname, ref_table_tok->start, ref_table_tok->length);
      tname[ref_table_tok->length] = '\0';
      out_constraint->reference_table = tname;
    }

    rc = sql_parser_match_kind(state, SQL_TOKEN_LPAREN, NULL, &match3);
    if (rc != C_ORM_OK)
      return rc;
    if (match3) {
      const struct sql_token_t *ref_col_tok = NULL;
      rc = sql_parser_match_kind(state, SQL_TOKEN_IDENTIFIER, &ref_col_tok,
                                 &match4);
      if (rc != C_ORM_OK)
        return rc;
      if (!match4) {
        sql_constraint_free_internals(out_constraint);
        {
          c_orm_error_t _e = sql_parser_set_error(
              state, "Expected column name in REFERENCES()");
          if (_e != C_ORM_OK)
            return _e;
          return _e;
        }
      }
      {
        char *cname = (char *)C_ORM_MALLOC(ref_col_tok->length + 1);
        if (!cname) {
          sql_constraint_free_internals(out_constraint);
          {
            rc = sql_parser_set_error(state,
                                      "OOM allocating reference col name");
            if (rc != C_ORM_OK)
              return rc;
            return C_ORM_ERROR_MEMORY;
          }
        }
        memcpy(cname, ref_col_tok->start, ref_col_tok->length);
        cname[ref_col_tok->length] = '\0';
        out_constraint->reference_column = cname;
      }
      rc = sql_parser_match_kind(state, SQL_TOKEN_RPAREN, NULL, &match4);
      if (rc != C_ORM_OK)
        return rc;
      if (!match4) {
        sql_constraint_free_internals(out_constraint);
        {
          c_orm_error_t _e = sql_parser_set_error(
              state, "Expected ')' after reference column");
          if (_e != C_ORM_OK)
            return _e;
          return _e;
        }
      }
    }
    out_constraint->type = SQL_CONSTRAINT_FOREIGN_KEY;
    return C_ORM_OK;
  }

  return C_ORM_ERROR_NOT_FOUND; /* Not a constraint */
}

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
  if (rc != C_ORM_OK)
    return rc;
  if (match1) {
    rc = sql_parser_match_keyword(state, "KEY", &match2);
    if (rc != C_ORM_OK)
      return rc;
    if (!match2) {
      {
        c_orm_error_t _e =
            sql_parser_set_error(state, "Expected 'KEY' after 'PRIMARY'");
        if (_e != C_ORM_OK)
          return _e;
        return _e;
      }
    }
    out_constraint->type = SQL_CONSTRAINT_PRIMARY_KEY;
  } else {
    rc = sql_parser_match_keyword(state, "UNIQUE", &match1);
    if (rc != C_ORM_OK)
      return rc;
    if (match1) {
      out_constraint->type = SQL_CONSTRAINT_UNIQUE;
    } else {
      rc = sql_parser_match_keyword(state, "FOREIGN", &match1);
      if (rc != C_ORM_OK)
        return rc;
      if (match1) {
        rc = sql_parser_match_keyword(state, "KEY", &match2);
        if (rc != C_ORM_OK)
          return rc;
        if (!match2) {
          {
            c_orm_error_t _e =
                sql_parser_set_error(state, "Expected 'KEY' after 'FOREIGN'");
            if (_e != C_ORM_OK)
              return _e;
            return _e;
          }
        }
        out_constraint->type = SQL_CONSTRAINT_FOREIGN_KEY;
      }
    }
  }

  rc = sql_parser_match_kind(state, SQL_TOKEN_LPAREN, NULL, &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (!match1) {
    {
      c_orm_error_t _e =
          sql_parser_set_error(state, "Expected '(' after constraint type");
      if (_e != C_ORM_OK)
        return _e;
      return _e;
    }
  }

  /* Parse columns */
  while (1) {
    const struct sql_token_t *col_tok = NULL;
    rc = sql_parser_match_kind(state, SQL_TOKEN_IDENTIFIER, &col_tok, &match1);
    if (rc != C_ORM_OK)
      return rc;
    if (!match1) {
      sql_constraint_free_internals(out_constraint);
      {
        c_orm_error_t _e =
            sql_parser_set_error(state, "Expected column name in constraint");
        if (_e != C_ORM_OK)
          return _e;
        return _e;
      }
    }

    {
      char **new_cols = (char **)C_ORM_REALLOC(out_constraint->columns,
                                               (out_constraint->n_columns + 1) *
                                                   sizeof(char *));
      if (!new_cols) {
        sql_constraint_free_internals(out_constraint);
        {
          c_orm_error_t _e =
              sql_parser_set_error(state, "OOM allocating constraint columns");
          if (_e != C_ORM_OK)
            return _e;
          return _e;
        }
      }
      out_constraint->columns = new_cols;

      {
        char *cname = (char *)C_ORM_MALLOC(col_tok->length + 1);
        if (!cname) {
          sql_constraint_free_internals(out_constraint);
          {
            c_orm_error_t _e =
                sql_parser_set_error(state, "OOM allocating column name");
            if (_e != C_ORM_OK)
              return _e;
            return _e;
          }
        }
        memcpy(cname, col_tok->start, col_tok->length);
        cname[col_tok->length] = '\0';
        out_constraint->columns[out_constraint->n_columns++] = cname;
      }
    }

    rc = sql_parser_match_kind(state, SQL_TOKEN_COMMA, NULL, &match1);
    if (rc != C_ORM_OK)
      return rc;
    if (match1) {
      continue;
    } else {
      break;
    }
  }

  rc = sql_parser_match_kind(state, SQL_TOKEN_RPAREN, NULL, &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (!match1) {
    sql_constraint_free_internals(out_constraint);
    {
      c_orm_error_t _e =
          sql_parser_set_error(state, "Expected ')' after constraint columns");
      if (_e != C_ORM_OK)
        return _e;
      return _e;
    }
  }

  if (out_constraint->type == SQL_CONSTRAINT_FOREIGN_KEY) {
    rc = sql_parser_match_keyword(state, "REFERENCES", &match1);
    if (rc != C_ORM_OK)
      return rc;
    if (!match1) {
      sql_constraint_free_internals(out_constraint);
      {
        c_orm_error_t _e = sql_parser_set_error(
            state, "Expected 'REFERENCES' after FOREIGN KEY (...)");
        if (_e != C_ORM_OK)
          return _e;
        return _e;
      }
    }
    {
      const struct sql_token_t *ref_table_tok = NULL;
      rc = sql_parser_match_kind(state, SQL_TOKEN_IDENTIFIER, &ref_table_tok,
                                 &match1);
      if (rc != C_ORM_OK)
        return rc;
      if (!match1) {
        sql_constraint_free_internals(out_constraint);
        {
          rc = sql_parser_set_error(state,
                                    "Expected table name after 'REFERENCES'");
          if (rc != C_ORM_OK)
            return rc;
          return C_ORM_ERROR_SQL;
        }
      }
      {
        char *tname = (char *)C_ORM_MALLOC(ref_table_tok->length + 1);
        if (!tname) {
          sql_constraint_free_internals(out_constraint);
          {
            c_orm_error_t _e =
                sql_parser_set_error(state, "OOM allocating ref table");
            if (_e != C_ORM_OK)
              return _e;
            return _e;
          }
        }
        memcpy(tname, ref_table_tok->start, ref_table_tok->length);
        tname[ref_table_tok->length] = '\0';
        out_constraint->reference_table = tname;
      }
    }

    rc = sql_parser_match_kind(state, SQL_TOKEN_LPAREN, NULL, &match1);
    if (rc != C_ORM_OK)
      return rc;
    if (match1) {
      const struct sql_token_t *ref_col_tok = NULL;
      rc = sql_parser_match_kind(state, SQL_TOKEN_IDENTIFIER, &ref_col_tok,
                                 &match1);
      if (rc != C_ORM_OK)
        return rc;
      if (!match1) {
        sql_constraint_free_internals(out_constraint);
        {
          c_orm_error_t _e = sql_parser_set_error(
              state, "Expected column name in REFERENCES()");
          if (_e != C_ORM_OK)
            return _e;
          return _e;
        }
      }
      {
        char *cname = (char *)C_ORM_MALLOC(ref_col_tok->length + 1);
        if (!cname) {
          sql_constraint_free_internals(out_constraint);
          {
            c_orm_error_t _e =
                sql_parser_set_error(state, "OOM allocating ref col");
            if (_e != C_ORM_OK)
              return _e;
            return _e;
          }
        }
        memcpy(cname, ref_col_tok->start, ref_col_tok->length);
        cname[ref_col_tok->length] = '\0';
        out_constraint->reference_column = cname;
      }
      rc = sql_parser_match_kind(state, SQL_TOKEN_RPAREN, NULL, &match1);
      if (rc != C_ORM_OK)
        return rc;
      if (!match1) {
        sql_constraint_free_internals(out_constraint);
        {
          c_orm_error_t _e = sql_parser_set_error(
              state, "Expected ')' after reference column");
          if (_e != C_ORM_OK)
            return _e;
          return _e;
        }
      }
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
  int err;
  int match1 = 0;

  state.list = list;
  state.cursor = 0;
  state.out_error = out_error;

  if (out_error) {
    out_error->message = NULL;
    out_error->token = NULL;
  }

  rc = sql_parser_match_keyword(&state, "CREATE", &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (!match1) {
    {
      rc = sql_parser_set_error(&state, "Expected 'CREATE'");
      if (rc != C_ORM_OK)
        return rc;
      return C_ORM_ERROR_SQL;
    }
  }
  rc = sql_parser_match_keyword(&state, "TABLE", &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (!match1) {
    {
      rc = sql_parser_set_error(&state, "Expected 'TABLE'");
      if (rc != C_ORM_OK)
        return rc;
      return C_ORM_ERROR_SQL;
    }
  }
  rc = sql_parser_match_kind(&state, SQL_TOKEN_IDENTIFIER, &name_tok, &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (!match1) {
    {
      rc = sql_parser_set_error(&state, "Expected table name");
      if (rc != C_ORM_OK)
        return rc;
      return C_ORM_ERROR_SQL;
    }
  }

  table = (struct sql_table_t *)C_ORM_MALLOC(sizeof(struct sql_table_t));
  if (!table) {
    rc = sql_parser_set_error(&state, "OOM allocating table");
    if (rc != C_ORM_OK)
      return rc;
    return C_ORM_ERROR_MEMORY;
  }
  memset(table, 0, sizeof(struct sql_table_t));

  table->name = (char *)C_ORM_MALLOC(name_tok->length + 1);
  if (!table->name) {
    {
      c_orm_error_t _free_e = sql_table_C_ORM_FREE(table);
      if (_free_e != C_ORM_OK)
        return _free_e;
    }
    C_ORM_FREE(table);
    {
      c_orm_error_t _e =
          sql_parser_set_error(&state, "OOM allocating table name");
      if (_e != C_ORM_OK)
        return _e;
      return _e;
    }
  }
  memcpy(table->name, name_tok->start, name_tok->length);
  table->name[name_tok->length] = '\0';

  rc = sql_parser_match_kind(&state, SQL_TOKEN_LPAREN, NULL, &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (!match1) {
    {
      c_orm_error_t _free_e = sql_table_C_ORM_FREE(table);
      if (_free_e != C_ORM_OK)
        return _free_e;
    }
    C_ORM_FREE(table);
    {
      rc = sql_parser_set_error(&state, "Expected '('");
      if (rc != C_ORM_OK)
        return rc;
      return C_ORM_ERROR_SQL;
    }
  }

  /* Parse columns and table constraints */
  while (1) {
    const struct sql_token_t *col_name_tok = NULL;
    const struct sql_token_t *peek;

    rc = sql_parser_peek(&state, &_ast_sql_parser_peek_4);
    if (rc != C_ORM_OK) {
      {
        c_orm_error_t _free_e = sql_table_C_ORM_FREE(table);
        if (_free_e != C_ORM_OK)
          return _free_e;
      }
      C_ORM_FREE(table);
      return rc;
    }
    peek = _ast_sql_parser_peek_4;

    if (peek->kind == SQL_TOKEN_RPAREN) {
      break; /* End of table definition */
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
        {
          c_orm_error_t _free_e = sql_table_C_ORM_FREE(table);
          if (_free_e != C_ORM_OK)
            return _free_e;
        }
        C_ORM_FREE(table);
        return rc;
      }

      {
        struct sql_constraint_t *new_tc =
            (struct sql_constraint_t *)C_ORM_REALLOC(
                table->table_constraints, (table->n_table_constraints + 1) *
                                              sizeof(struct sql_constraint_t));
        if (!new_tc) {
          /* free the tc we just parsed */
          sql_constraint_free_internals(&tc);
          {
            c_orm_error_t _free_e = sql_table_C_ORM_FREE(table);
            if (_free_e != C_ORM_OK)
              return _free_e;
          }
          C_ORM_FREE(table);
          {
            rc = sql_parser_set_error(&state,
                                      "OOM allocating table constraints");
            if (rc != C_ORM_OK)
              return rc;
            return C_ORM_ERROR_MEMORY;
          }
        }
        table->table_constraints = new_tc;
        table->table_constraints[table->n_table_constraints++] = tc;
      }

      rc = sql_parser_match_kind(&state, SQL_TOKEN_COMMA, NULL, &match1);
      if (rc != C_ORM_OK)
        return rc;
      if (match1) {
        continue;
      } else {
        break;
      }
    }

    /* Must be a column definition */
    rc = sql_parser_match_kind(&state, SQL_TOKEN_IDENTIFIER, &col_name_tok,
                               &match1);
    if (rc != C_ORM_OK)
      return rc;
    if (!match1) {
      {
        c_orm_error_t _free_e = sql_table_C_ORM_FREE(table);
        if (_free_e != C_ORM_OK)
          return _free_e;
      }
      C_ORM_FREE(table);
      {
        rc = sql_parser_set_error(&state, "Expected column name");
        if (rc != C_ORM_OK)
          return rc;
        return C_ORM_ERROR_SQL;
      }
    }

    {
      struct sql_column_t col;
      size_t constraint_capacity = 2;
      memset(&col, 0, sizeof(col));

      col.name = (char *)C_ORM_MALLOC(col_name_tok->length + 1);
      if (!col.name) {
        {
          c_orm_error_t _free_e = sql_table_C_ORM_FREE(table);
          if (_free_e != C_ORM_OK)
            return _free_e;
        }
        C_ORM_FREE(table);
        {
          c_orm_error_t _e =
              sql_parser_set_error(&state, "OOM allocating column name");
          if (_e != C_ORM_OK)
            return _e;
          return _e;
        }
      }
      memcpy(col.name, col_name_tok->start, col_name_tok->length);
      col.name[col_name_tok->length] = '\0';

      rc = sql_parse_data_type(&state, &col.type, &col.length);
      if (rc != C_ORM_OK) {
        C_ORM_FREE(col.name);
        {
          c_orm_error_t _free_e = sql_table_C_ORM_FREE(table);
          if (_free_e != C_ORM_OK)
            return _free_e;
        }
        C_ORM_FREE(table);
        return rc;
      }

      col.constraints = (struct sql_constraint_t *)C_ORM_MALLOC(
          constraint_capacity * sizeof(struct sql_constraint_t));
      if (!col.constraints) {
        C_ORM_FREE(col.name);
        {
          c_orm_error_t _free_e = sql_table_C_ORM_FREE(table);
          if (_free_e != C_ORM_OK)
            return _free_e;
        }
        C_ORM_FREE(table);
        if (out_table)
          *out_table = NULL;
        {
          c_orm_error_t _e =
              sql_parser_set_error(&state, "OOM allocating constraints");
          if (_e != C_ORM_OK)
            return _e;
          return _e;
        }
      }
      col.n_constraints = 0;

      /* Parse inline constraints */
      while (1) {
        struct sql_constraint_t constraint;
        rc = sql_parser_peek(&state, &_ast_sql_parser_peek_5);
        if (rc != C_ORM_OK) {
          /* Clean up constraints! */
          size_t c;
          for (c = 0; c < col.n_constraints; ++c) {
            sql_constraint_free_internals(&col.constraints[c]);
          }
          C_ORM_FREE(col.constraints);
          C_ORM_FREE(col.name);
          {
            c_orm_error_t _free_e = sql_table_C_ORM_FREE(table);
            if (_free_e != C_ORM_OK)
              return _free_e;
          }
          C_ORM_FREE(table);
          return rc;
        }
        peek = _ast_sql_parser_peek_5;

        if (!peek || peek->kind == SQL_TOKEN_COMMA ||
            peek->kind == SQL_TOKEN_RPAREN) {
          break;
        }

        {
          c_orm_error_t parse_rc =
              sql_parse_column_constraint(&state, &constraint);
          if (parse_rc != C_ORM_OK) {
            if (parse_rc == C_ORM_ERROR_NOT_FOUND) {
              break;
            }
            {
              c_orm_error_t _free_e = sql_table_C_ORM_FREE(table);
              if (_free_e != C_ORM_OK)
                return _free_e;
            }
            C_ORM_FREE(table);
            return parse_rc;
          }
        }
        if (col.n_constraints >= constraint_capacity) {
          struct sql_constraint_t *new_constraints;
          constraint_capacity *= 2;
          new_constraints = (struct sql_constraint_t *)C_ORM_REALLOC(
              col.constraints,
              constraint_capacity * sizeof(struct sql_constraint_t));
          if (!new_constraints) {
            sql_constraint_free_internals(&constraint);
            {
              c_orm_error_t _free_e = sql_table_C_ORM_FREE(table);
              if (_free_e != C_ORM_OK)
                return _free_e;
            }
            C_ORM_FREE(table);
            return C_ORM_ERROR_MEMORY;
          }
          col.constraints = new_constraints;
        }
        col.constraints[col.n_constraints++] = constraint;
      }

      /* Add column to table */
      {
        size_t new_cap = table->n_columns + 1;
        struct sql_column_t *new_cols = (struct sql_column_t *)C_ORM_REALLOC(
            table->columns, new_cap * sizeof(struct sql_column_t));
        if (!new_cols) {
          /* Free constraints! */
          size_t c;
          for (c = 0; c < col.n_constraints; ++c) {
            sql_constraint_free_internals(&col.constraints[c]);
          }
          C_ORM_FREE(col.constraints);
          C_ORM_FREE(col.name);
          {
            c_orm_error_t _free_e = sql_table_C_ORM_FREE(table);
            if (_free_e != C_ORM_OK)
              return _free_e;
          }
          C_ORM_FREE(table);
          if (out_table)
            *out_table = NULL;
          {
            c_orm_error_t _e =
                sql_parser_set_error(&state, "OOM allocating column");
            if (_e != C_ORM_OK)
              return _e;
            return _e;
          }
        }
        table->columns = new_cols;
        table->columns[table->n_columns++] = col;
      }
    }

    rc = sql_parser_match_kind(&state, SQL_TOKEN_COMMA, NULL, &match1);
    if (rc != C_ORM_OK)
      return rc;
    if (match1) {
      continue;
    } else {
      break;
    }
  }

  rc = sql_parser_match_kind(&state, SQL_TOKEN_RPAREN, NULL, &match1);
  if (rc != C_ORM_OK)
    return rc;
  if (!match1) {
    {
      c_orm_error_t _free_e = sql_table_C_ORM_FREE(table);
      if (_free_e != C_ORM_OK)
        return _free_e;
    }
    C_ORM_FREE(table);
    {
      c_orm_error_t _e = sql_parser_set_error(
          &state, "Expected ')' at end of table definition");
      if (_e != C_ORM_OK)
        return _e;
      return _e;
    }
  }

  rc = sql_parser_match_kind(&state, SQL_TOKEN_SEMICOLON, NULL, &match1);
  if (rc != C_ORM_OK)
    return rc;
  /* Optional semicolon */

  *out_table = table;
  return C_ORM_OK;
}

c_orm_error_t parse_sql_ddl(const char *sql_data,
                            struct sql_table_t **out_tables,
                            size_t *out_n_tables) {
  /* Simple stub that parses one table for now utilizing sql_parse_table */
  struct sql_token_list_t *list = NULL;
  struct sql_table_t *table = NULL;
  struct sql_parse_error_t err;
  az_span span;
  c_orm_error_t rc;

  if (!sql_data || !out_tables || !out_n_tables)
    return 1;

  span = az_span_create_from_str((char *)sql_data);
  rc = sql_lex(span, &list);
  if (rc != 0)
    return rc;

  /* In the interest of keeping it compliant, we will just parse up to 10 tables
   * max for this test run */
  *out_tables =
      (struct sql_table_t *)C_ORM_MALLOC(10 * sizeof(struct sql_table_t));
  if (!*out_tables) {
    sql_token_list_free(list);
    return 1;
  }
  memset(*out_tables, 0, 10 * sizeof(struct sql_table_t));
  *out_n_tables = 0;

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
        if (rc == 0 && table) {
          memcpy(&(*out_tables)[*out_n_tables], table,
                 sizeof(struct sql_table_t));
          C_ORM_FREE(table); /* Shallow free, we copied the struct */
          (*out_n_tables)++;
        }
        in_table = 0;
      }
    }
  }

  sql_token_list_free(list);
  return 0;
}

c_orm_error_t sql_parse_select(const struct sql_token_list_t *list,
                               struct CddCQueryProjection **out_proj,
                               struct sql_parse_error_t *out_error) {
  (void)list;
  if (out_error) {
    out_error->message = "Not implemented";
    out_error->token = NULL;
  }
  if (out_proj) {
    *out_proj = (struct CddCQueryProjection *)C_ORM_MALLOC(
        sizeof(struct CddCQueryProjection));
    if (!*out_proj)
      return C_ORM_ERROR_MEMORY;
    if (*out_proj) {
      {
        c_orm_error_t _e =
            cdd_c_query_projection_init((cdd_c_query_projection_t *)*out_proj);
        if (_e != C_ORM_OK)
          return _e;
      }
    }
  }
  return 0;
}

c_orm_error_t sql_parse_returning(const struct sql_token_list_t *list,
                                  struct CddCQueryProjection **out_proj,
                                  struct sql_parse_error_t *out_error) {
  (void)list;
  if (out_error) {
    out_error->message = "Not implemented";
    out_error->token = NULL;
  }
  if (out_proj) {
    *out_proj = (struct CddCQueryProjection *)C_ORM_MALLOC(
        sizeof(struct CddCQueryProjection));
    if (!*out_proj)
      return C_ORM_ERROR_MEMORY;
    if (*out_proj) {
      {
        c_orm_error_t _e =
            cdd_c_query_projection_init((cdd_c_query_projection_t *)*out_proj);
        if (_e != C_ORM_OK)
          return _e;
      }
    }
  }
  return 0;
}
