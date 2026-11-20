/**
 * @file c_orm_copybook.c
 * @brief COBOL Copybook (.cpy) preprocessor, lexer, PIC analyzer, DCLGEN
 * detector, and hierarchical byte-layout AST parser.
 */

/* clang-format off */
#include "c_orm_copybook.h"
#include "c_orm_db.h"
#include "c_orm_log.h"
#include "c_orm_safe_crt.h"
#include "c_orm_sql.h"
#include "c_orm_string_builder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

/**
 * @brief Lexical token classifications for COBOL Copybook parsing.
 */
typedef enum cpy_tok_kind {
  /** @brief Identifier, level number, or COBOL keyword. */
  CPY_TOK_WORD = 0,
  /** @brief Raw `PIC` / `PICTURE` descriptor string. */
  CPY_TOK_PIC_STR = 1,
  /** @brief Quoted character or `X'...'` hex literal. */
  CPY_TOK_STRING = 2,
  /** @brief Statement-terminating separator period (`.`). */
  CPY_TOK_DOT = 3
} cpy_tok_kind_t;

/**
 * @brief Single COBOL Copybook lexical token.
 */
typedef struct cpy_token {
  /** @brief Lexical token category. */
  cpy_tok_kind_t kind;
  /** @brief Null-terminated token text (unquoted for strings, uppercase for
   * words). */
  char *text;
  /** @brief Non-zero when a `CPY_TOK_STRING` originated from `X'...'` hex. */
  int is_hex_lit;
} cpy_token_t;

/**
 * @brief Growable array of COBOL Copybook lexical tokens.
 */
typedef struct cpy_tok_vec {
  /** @brief Heap array of tokens. */
  cpy_token_t *items;
  /** @brief Active token count. */
  size_t count;
  /** @brief Allocated token capacity. */
  size_t cap;
} cpy_tok_vec_t;

/**
 * @brief Temporary entry on the hierarchical group nesting stack.
 */
typedef struct cpy_group_frame {
  /** @brief COBOL level number (`1..49`). */
  int level;
  /** @brief Index into parent's children array (`(size_t)-1` for `01` root). */
  size_t field_idx;
  /** @brief Parent frame index (`(size_t)-1` for `01` root). */
  size_t parent_frame;
} cpy_group_frame_t;

/**
 * @brief Folds an ASCII lowercase letter to uppercase.
 * @param c Input byte.
 * @param out_c Output byte (`A..Z` if `a..z`, else unchanged).
 * @return C_ORM_OK on success.
 */
static c_orm_error_t cpy_char_to_upper(unsigned char c, unsigned char *out_c) {
  *out_c = (c >= 'a' && c <= 'z') ? (unsigned char)(c - ('a' - 'A')) : c;
  return C_ORM_OK;
}

/**
 * @brief Tests whether `c` is an ASCII alphanumeric character.
 * @param c Input byte.
 * @param out_alnum Output flag set to 1 if `[A-Za-z0-9]`, else 0.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t cpy_char_is_alnum(unsigned char c, int *out_alnum) {
  unsigned char u = 0;
  cpy_char_to_upper(c, &u);
  *out_alnum = ((u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9')) ? 1 : 0;
  return C_ORM_OK;
}

/**
 * @brief Case-insensitive comparison treating `-` and `_` as equivalent.
 * @param a First identifier string.
 * @param b Second identifier string.
 * @param out_eq Output flag set to 1 when identifiers match, else 0.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t cpy_ident_eq(const char *a, const char *b, int *out_eq) {
  size_t i = 0;
  *out_eq = 0;
  while (a[i] != '\0' && b[i] != '\0') {
    unsigned char ca = 0;
    unsigned char cb = 0;
    cpy_char_to_upper((unsigned char)a[i], &ca);
    cpy_char_to_upper((unsigned char)b[i], &cb);
    if (ca == '-') {
      ca = '_';
    }
    if (cb == '-') {
      cb = '_';
    }
    if (ca != cb) {
      return C_ORM_OK;
    }
    i++;
  }
  if (a[i] == '\0' && b[i] == '\0') {
    *out_eq = 1;
  }
  return C_ORM_OK;
}

/**
 * @brief Case-insensitive word-boundary prefix match at `pos` within `src`.
 * @param src Source buffer.
 * @param src_len Total length of `src`.
 * @param pos Starting offset in `src`.
 * @param kw Uppercase ASCII keyword to match.
 * @param out_matched Output flag set to 1 if matched at word boundary, else 0.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t cpy_match_kw_at(const char *src, size_t src_len,
                                     size_t pos, const char *kw,
                                     int *out_matched) {
  size_t klen = strlen(kw);
  size_t i;
  *out_matched = 0;
  if (pos + klen > src_len) {
    return C_ORM_OK;
  }
  for (i = 0; i < klen; ++i) {
    unsigned char c = 0;
    cpy_char_to_upper((unsigned char)src[pos + i], &c);
    if (c != (unsigned char)kw[i]) {
      return C_ORM_OK;
    }
  }
  if (pos + klen < src_len) {
    unsigned char next = (unsigned char)src[pos + klen];
    int is_an = 0;
    cpy_char_is_alnum(next, &is_an);
    if (is_an || next == '-' || next == '_') {
      return C_ORM_OK;
    }
  }
  *out_matched = 1;
  return C_ORM_OK;
}

/**
 * @brief Appends a bounded byte slice to a `c_orm_string_builder_t`.
 * @param sb Target string builder.
 * @param data Pointer to characters to append.
 * @param len Number of bytes to append.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on OOM.
 */
static c_orm_error_t cpy_sb_append_n(c_orm_string_builder_t *sb,
                                     const char *data, size_t len) {
  char stack_buf[128];
  char *heap_buf = NULL;
  c_orm_error_t rc;
  if (len == 0) {
    return C_ORM_OK;
  }
  if (len < sizeof(stack_buf)) {
    memcpy(stack_buf, data, len);
    stack_buf[len] = '\0';
    return c_orm_string_builder_append(sb, stack_buf);
  }
  rc = c_orm_system_malloc(len + 1, (void **)&heap_buf);
  if (rc != C_ORM_OK) {
    return rc;
  }
  memcpy(heap_buf, data, len);
  heap_buf[len] = '\0';
  rc = c_orm_string_builder_append(sb, heap_buf);
  (void)c_orm_system_free(heap_buf);
  return rc;
}

/**
 * @brief Normalizes a COBOL identifier into lowercase C89 snake_case, escaping
 * C reserved words and leading digits.
 * @param cobol_name Source COBOL identifier.
 * @param out_c_name Output heap-allocated C identifier string.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on OOM.
 */
static c_orm_error_t cpy_normalize_c_name(const char *cobol_name,
                                          char **out_c_name) {
  size_t len = strlen(cobol_name);
  char *buf = NULL;
  size_t i;
  size_t w = 0;
  c_orm_error_t rc;
  static const char *const c_keywords[] = {
      "auto",     "break",  "case",    "char",   "const",    "continue",
      "default",  "do",     "double",  "else",   "enum",     "extern",
      "float",    "for",    "goto",    "if",     "int",      "long",
      "register", "return", "short",   "signed", "sizeof",   "static",
      "struct",   "switch", "typedef", "union",  "unsigned", "void",
      "volatile", "while",  "bool",    "true",   "false",    NULL};

  rc = c_orm_system_malloc(len + 6, (void **)&buf);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (cobol_name[0] >= '0' && cobol_name[0] <= '9') {
    buf[w++] = 'f';
    buf[w++] = '_';
  }
  for (i = 0; i < len; ++i) {
    unsigned char c = (unsigned char)cobol_name[i];
    unsigned char u = 0;
    int is_an = 0;
    cpy_char_to_upper(c, &u);
    cpy_char_is_alnum(c, &is_an);
    if (u >= 'A' && u <= 'Z') {
      buf[w++] = (char)(u + ('a' - 'A'));
    } else if (is_an) {
      buf[w++] = (char)c;
    } else {
      buf[w++] = '_';
    }
  }
  buf[w] = '\0';
  for (i = 0; c_keywords[i] != NULL; ++i) {
    if (strcmp(buf, c_keywords[i]) == 0) {
      buf[w++] = '_';
      buf[w++] = 'v';
      buf[w] = '\0';
      break;
    }
  }
  *out_c_name = buf;
  return C_ORM_OK;
}

/**
 * @brief Normalizes a COBOL identifier into an uppercase SQL identifier (`-` to
 * `_`).
 * @param cobol_name Source COBOL identifier.
 * @param out_sql_name Output heap-allocated SQL identifier string.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on OOM.
 */
static c_orm_error_t cpy_normalize_sql_name(const char *cobol_name,
                                            char **out_sql_name) {
  size_t len = strlen(cobol_name);
  char *buf = NULL;
  size_t i;
  c_orm_error_t rc;
  rc = c_orm_system_malloc(len + 1, (void **)&buf);
  if (rc != C_ORM_OK) {
    return rc;
  }
  for (i = 0; i < len; ++i) {
    unsigned char u = 0;
    int is_an = 0;
    cpy_char_to_upper((unsigned char)cobol_name[i], &u);
    cpy_char_is_alnum(u, &is_an);
    buf[i] = is_an ? (char)u : '_';
  }
  buf[len] = '\0';
  *out_sql_name = buf;
  return C_ORM_OK;
}

/**
 * @brief Generates a capitalized C89 struct tag name (e.g. `"Acct_rec"`).
 * @param cobol_name Source COBOL `01` record name.
 * @param out_struct_name Output heap-allocated C struct tag name.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on OOM.
 */
static c_orm_error_t cpy_normalize_struct_name(const char *cobol_name,
                                               char **out_struct_name) {
  char *cname = NULL;
  unsigned char u0 = 0;
  c_orm_error_t rc;
  rc = cpy_normalize_c_name(cobol_name, &cname);
  if (rc != C_ORM_OK) {
    return rc;
  }
  cpy_char_to_upper((unsigned char)cname[0], &u0);
  cname[0] = (char)u0;
  *out_struct_name = cname;
  return C_ORM_OK;
}

/**
 * @brief Recursively releases dynamic allocations inside a single
 * `c_orm_cobol_field_t` (without freeing the struct pointer itself).
 * @param field Field node whose owned allocations are freed.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t cpy_field_contents_free(c_orm_cobol_field_t *field) {
  size_t i;
  size_t j;
  (void)c_orm_system_free(field->cobol_name);
  (void)c_orm_system_free(field->c_name);
  (void)c_orm_system_free(field->sql_name);
  (void)c_orm_system_free(field->pic_raw);
  (void)c_orm_system_free(field->default_value);
  (void)c_orm_system_free(field->redefines_name);
  (void)c_orm_system_free(field->renames_from);
  (void)c_orm_system_free(field->renames_thru);
  (void)c_orm_system_free(field->odo_field_name);
  if (field->children) {
    for (i = 0; i < field->num_children; ++i) {
      (void)cpy_field_contents_free(&field->children[i]);
    }
    (void)c_orm_system_free(field->children);
  }
  if (field->conditions) {
    for (i = 0; i < field->num_conditions; ++i) {
      (void)c_orm_system_free(field->conditions[i].cobol_name);
      (void)c_orm_system_free(field->conditions[i].c_name);
      for (j = 0; j < field->conditions[i].num_values; ++j) {
        (void)c_orm_system_free(field->conditions[i].values[j].low_val);
        (void)c_orm_system_free(field->conditions[i].values[j].high_val);
      }
      (void)c_orm_system_free(field->conditions[i].values);
    }
    (void)c_orm_system_free(field->conditions);
  }
  memset(field, 0, sizeof(*field));
  return C_ORM_OK;
}

/**
 * @brief Releases internal allocations of a `c_orm_cobol_record_t` without
 * freeing the outer pointer itself.
 * @param rec Record node to clean up.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t cpy_record_contents_free(c_orm_cobol_record_t *rec) {
  size_t i;
  (void)c_orm_system_free(rec->record_name);
  (void)c_orm_system_free(rec->c_struct_name);
  (void)c_orm_system_free(rec->sql_table_name);
  (void)c_orm_system_free(rec->sql_schema_name);
  if (rec->fields) {
    for (i = 0; i < rec->num_fields; ++i) {
      (void)cpy_field_contents_free(&rec->fields[i]);
    }
    (void)c_orm_system_free(rec->fields);
  }
  if (rec->standalone_fields) {
    for (i = 0; i < rec->num_standalone_fields; ++i) {
      (void)cpy_field_contents_free(&rec->standalone_fields[i]);
    }
    (void)c_orm_system_free(rec->standalone_fields);
  }
  if (rec->secondary_records) {
    for (i = 0; i < rec->num_secondary_records; ++i) {
      (void)cpy_record_contents_free(&rec->secondary_records[i]);
    }
    (void)c_orm_system_free(rec->secondary_records);
  }
  memset(rec, 0, sizeof(*rec));
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t
c_orm_copybook_record_free(c_orm_cobol_record_t *record) {
  if (!record) {
    return C_ORM_OK;
  }
  (void)cpy_record_contents_free(record);
  return c_orm_system_free(record);
}

/**
 * @brief Strips optional COBOL `==...==` pseudo-text delimiters and outer
 * spaces from a `REPLACING` pattern or replacement string.
 * @param raw Raw pattern or replacement text.
 * @param out_ptr Output pointer into `raw` where effective text starts.
 * @param out_len Output byte length of effective text.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t cpy_strip_pseudotext(const char *raw, const char **out_ptr,
                                          size_t *out_len) {
  size_t s = 0;
  size_t e = strlen(raw);
  while (s < e && (raw[s] == ' ' || raw[s] == '\t')) {
    s++;
  }
  while (e > s && (raw[e - 1] == ' ' || raw[e - 1] == '\t')) {
    e--;
  }
  if (e - s >= 4 && strncmp(raw + s, "==", 2) == 0 &&
      strncmp(raw + e - 2, "==", 2) == 0) {
    s += 2;
    e -= 2;
    while (s < e && (raw[s] == ' ' || raw[s] == '\t')) {
      s++;
    }
    while (e > s && (raw[e - 1] == ' ' || raw[e - 1] == '\t')) {
      e--;
    }
  }
  *out_ptr = raw + s;
  *out_len = e - s;
  return C_ORM_OK;
}

/**
 * @brief Applies a single `REPLACING` substring substitution across `src`.
 * @param src Null-terminated source buffer.
 * @param from_ptr Needle pointer.
 * @param from_len Needle byte length.
 * @param to_ptr Replacement pointer.
 * @param to_len Replacement byte length.
 * @param out_res Output heap-allocated string after replacement.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on OOM.
 */
static c_orm_error_t cpy_replace_all(const char *src, const char *from_ptr,
                                     size_t from_len, const char *to_ptr,
                                     size_t to_len, char **out_res) {
  c_orm_string_builder_t *sb = NULL;
  const char *s_view = NULL;
  size_t src_len = strlen(src);
  size_t pos = 0;
  c_orm_error_t rc;

  if (from_len == 0) {
    return c_orm_strdup(src, out_res);
  }
  rc = c_orm_string_builder_init(&sb);
  if (rc != C_ORM_OK) {
    return rc;
  }
  while (pos < src_len) {
    if (pos + from_len <= src_len &&
        memcmp(src + pos, from_ptr, from_len) == 0) {
      rc = cpy_sb_append_n(sb, to_ptr, to_len);
      if (rc != C_ORM_OK) {
        (void)c_orm_string_builder_free(sb);
        return rc;
      }
      pos += from_len;
    } else {
      size_t next = pos + 1;
      while (next < src_len && !(next + from_len <= src_len &&
                                 memcmp(src + next, from_ptr, from_len) == 0)) {
        next++;
      }
      rc = cpy_sb_append_n(sb, src + pos, next - pos);
      if (rc != C_ORM_OK) {
        (void)c_orm_string_builder_free(sb);
        return rc;
      }
      pos = next;
    }
  }
  c_orm_string_builder_get(sb, &s_view);
  rc = c_orm_strdup(s_view, out_res);
  (void)c_orm_string_builder_free(sb);
  return rc;
}

/**
 * @brief Detects whether `cpy_source` uses fixed 80-column format or free
 * format when `C_ORM_COBOL_FORMAT_AUTO` is requested.
 * @param cpy_source Raw copybook source string.
 * @param out_fmt Output resolved format (`FIXED` or `FREE`).
 * @return C_ORM_OK on success.
 */
static c_orm_error_t cpy_detect_format(const char *cpy_source,
                                       c_orm_cobol_format_t *out_fmt) {
  size_t pos = 0;
  size_t len = strlen(cpy_source);
  *out_fmt = C_ORM_COBOL_FORMAT_FREE;

  while (pos < len) {
    size_t line_end = pos;
    size_t line_len;
    size_t first_non_sp;
    while (line_end < len && cpy_source[line_end] != '\n') {
      line_end++;
    }
    line_len = line_end - pos;
    if (line_len > 0 && cpy_source[pos + line_len - 1] == '\r') {
      line_len--;
    }
    first_non_sp = 0;
    while (first_non_sp < line_len &&
           (cpy_source[pos + first_non_sp] == ' ' ||
            cpy_source[pos + first_non_sp] == '\t')) {
      first_non_sp++;
    }
    if (first_non_sp < line_len) {
      int is_dir = 0;
      cpy_match_kw_at(cpy_source + pos, line_len, first_non_sp, ">>SOURCE",
                      &is_dir);
      if (!is_dir) {
        if (line_len >= 7) {
          size_t k;
          int all_digits = 1;
          int all_spaces = 1;
          char ind = cpy_source[pos + 6];
          for (k = 0; k < 6; ++k) {
            char ch = cpy_source[pos + k];
            if (!(ch >= '0' && ch <= '9')) {
              all_digits = 0;
            }
            if (ch != ' ') {
              all_spaces = 0;
            }
          }
          if (all_digits && strchr(" */-Dd", (unsigned char)ind) != NULL) {
            *out_fmt = C_ORM_COBOL_FORMAT_FIXED;
            return C_ORM_OK;
          }
          if (all_spaces && strchr("*/-", (unsigned char)ind) != NULL) {
            *out_fmt = C_ORM_COBOL_FORMAT_FIXED;
            return C_ORM_OK;
          }
        }
        *out_fmt = C_ORM_COBOL_FORMAT_FREE;
        return C_ORM_OK;
      }
    }
    pos = (line_end < len) ? (line_end + 1) : len;
  }
  return C_ORM_OK;
}

/**
 * @brief Strips inline `*>` comments outside quoted literals within a line.
 * @param line Pointer to line content.
 * @param line_len Length of `line`.
 * @param out_effective_len Output length excluding `*>` comment and trailing
 * spaces.
 * @param io_open_quote Input/output active quote char (`'\0'`, `'\''`, or
 * `'"'`).
 * @return C_ORM_OK on success.
 */
static c_orm_error_t cpy_strip_inline_comment(const char *line, size_t line_len,
                                              size_t *out_effective_len,
                                              char *io_open_quote) {
  size_t i = 0;
  char q = *io_open_quote;
  size_t eff = line_len;
  while (i < line_len) {
    char c = line[i];
    if (q != '\0') {
      if (c == q) {
        if (i + 1 < line_len && line[i + 1] == q) {
          i += 2;
          continue;
        }
        q = '\0';
      }
      i++;
    } else {
      if (c == '\'' || c == '"') {
        q = c;
        i++;
      } else if (c == '*' && i + 1 < line_len && line[i + 1] == '>') {
        eff = i;
        break;
      } else {
        i++;
      }
    }
  }
  if (q == '\0') {
    while (eff > 0 && (line[eff - 1] == ' ' || line[eff - 1] == '\t')) {
      eff--;
    }
  }
  *io_open_quote = q;
  *out_effective_len = eff;
  return C_ORM_OK;
}

/**
 * @brief Normalizes fixed/free COBOL lines (handling cols 1-6, col 7 `*`/`/`/
 * `D`/`-` continuation, cols 73+, `*>` comments, `>>SOURCE FORMAT` directives,
 * inline `REPLACING ==from== BY ==to==`, and caller `replacements`) into a
 * clean character stream.
 * @param cpy_source Raw copybook source string.
 * @param format Requested format (`AUTO`, `FIXED`, or `FREE`).
 * @param replacements Optional caller-supplied `REPLACING` pairs.
 * @param num_replacements Count of caller-supplied `REPLACING` pairs.
 * @param out_clean Output heap-allocated normalized source string.
 * @return C_ORM_OK on success, C_ORM_ERROR_COPYBOOK_SYNTAX on invalid indicator
 * or directive, or C_ORM_ERROR_MEMORY on OOM.
 */
static c_orm_error_t
cpy_normalize_source(const char *cpy_source, c_orm_cobol_format_t format,
                     const c_orm_cobol_replace_pair_t *replacements,
                     size_t num_replacements, char **out_clean) {
  c_orm_cobol_format_t active_fmt = format;
  c_orm_string_builder_t *sb = NULL;
  const char *sb_str = NULL;
  char *stage = NULL;
  size_t len = strlen(cpy_source);
  size_t pos = 0;
  char open_quote = '\0';
  size_t i;
  c_orm_error_t rc;

  if (active_fmt == C_ORM_COBOL_FORMAT_AUTO) {
    cpy_detect_format(cpy_source, &active_fmt);
  }
  rc = c_orm_string_builder_init(&sb);
  if (rc != C_ORM_OK) {
    return rc;
  }

  while (pos < len) {
    size_t line_end = pos;
    size_t line_len;
    size_t trim_s;
    int is_src_dir = 0;

    while (line_end < len && cpy_source[line_end] != '\n') {
      line_end++;
    }
    line_len = line_end - pos;
    if (line_len > 0 && cpy_source[pos + line_len - 1] == '\r') {
      line_len--;
    }

    trim_s = 0;
    while (trim_s < line_len &&
           (cpy_source[pos + trim_s] == ' ' ||
            cpy_source[pos + trim_s] == '\t' ||
            (active_fmt == C_ORM_COBOL_FORMAT_FIXED && trim_s < 6 &&
             cpy_source[pos + trim_s] >= '0' &&
             cpy_source[pos + trim_s] <= '9'))) {
      trim_s++;
    }
    if (trim_s < line_len) {
      cpy_match_kw_at(cpy_source + pos, line_len, trim_s, ">>SOURCE",
                      &is_src_dir);
    }
    if (is_src_dir) {
      size_t scan = trim_s;
      int switched = 0;
      while (scan < line_len) {
        int m_free = 0;
        int m_fixed = 0;
        cpy_match_kw_at(cpy_source + pos, line_len, scan, "FREE", &m_free);
        cpy_match_kw_at(cpy_source + pos, line_len, scan, "FIXED", &m_fixed);
        if (m_free) {
          active_fmt = C_ORM_COBOL_FORMAT_FREE;
          switched = 1;
          break;
        }
        if (m_fixed) {
          active_fmt = C_ORM_COBOL_FORMAT_FIXED;
          switched = 1;
          break;
        }
        scan++;
      }
      if (!switched) {
        (void)c_orm_string_builder_free(sb);
        return C_ORM_ERROR_COPYBOOK_SYNTAX;
      }
      pos = (line_end < len) ? (line_end + 1) : len;
      continue;
    }

    if (active_fmt == C_ORM_COBOL_FORMAT_FIXED) {
      size_t area_end;
      char ind;
      const char *area_ptr;
      size_t area_len;
      size_t eff_len = 0;

      if (line_len < 7) {
        pos = (line_end < len) ? (line_end + 1) : len;
        continue;
      }
      ind = cpy_source[pos + 6];
      if (ind == '*' || ind == '/' || ind == 'D' || ind == 'd') {
        pos = (line_end < len) ? (line_end + 1) : len;
        continue;
      }
      area_end = (line_len > 72) ? 72 : line_len;
      area_ptr = cpy_source + pos + 7;
      area_len = area_end - 7;

      if (ind == '-') {
        size_t cs = 0;
        while (cs < area_len && area_ptr[cs] == ' ') {
          cs++;
        }
        if (open_quote != '\0' && cs < area_len && area_ptr[cs] == open_quote) {
          cs++;
        }
        cpy_strip_inline_comment(area_ptr + cs, area_len - cs, &eff_len,
                                 &open_quote);
        rc = cpy_sb_append_n(sb, area_ptr + cs, eff_len);
        if (rc != C_ORM_OK) {
          (void)c_orm_string_builder_free(sb);
          return rc;
        }
      } else if (ind == ' ') {
        char prev_q = open_quote;
        cpy_strip_inline_comment(area_ptr, area_len, &eff_len, &open_quote);
        if (prev_q == '\0' && eff_len > 0) {
          rc = c_orm_string_builder_append(sb, " ");
          if (rc != C_ORM_OK) {
            (void)c_orm_string_builder_free(sb);
            return rc;
          }
        }
        rc = cpy_sb_append_n(sb, area_ptr, eff_len);
        if (rc != C_ORM_OK) {
          (void)c_orm_string_builder_free(sb);
          return rc;
        }
      } else {
        (void)c_orm_string_builder_free(sb);
        return C_ORM_ERROR_COPYBOOK_SYNTAX;
      }
    } else {
      size_t eff_len = 0;
      cpy_strip_inline_comment(cpy_source + pos, line_len, &eff_len,
                               &open_quote);
      if (eff_len > 0) {
        rc = c_orm_string_builder_append(sb, " ");
        if (rc == C_ORM_OK) {
          rc = cpy_sb_append_n(sb, cpy_source + pos, eff_len);
        }
        if (rc != C_ORM_OK) {
          (void)c_orm_string_builder_free(sb);
          return rc;
        }
      }
    }
    pos = (line_end < len) ? (line_end + 1) : len;
  }

  if (open_quote != '\0') {
    (void)c_orm_string_builder_free(sb);
    return C_ORM_ERROR_COPYBOOK_SYNTAX;
  }

  c_orm_string_builder_get(sb, &sb_str);
  rc = c_orm_strdup(sb_str, &stage);
  (void)c_orm_string_builder_free(sb);
  if (rc != C_ORM_OK) {
    return rc;
  }

  /* Process optional leading inline REPLACING ==from== BY ==to== [.] */
  {
    size_t sp = 0;
    size_t slen = strlen(stage);
    int has_rep = 0;
    while (sp < slen && stage[sp] == ' ') {
      sp++;
    }
    cpy_match_kw_at(stage, slen, sp, "REPLACING", &has_rep);
    if (has_rep) {
      sp += 9;
      for (;;) {
        size_t f_start;
        size_t f_end;
        size_t t_start;
        size_t t_end;
        int has_by = 0;
        while (sp < slen && stage[sp] == ' ') {
          sp++;
        }
        if (sp < slen && stage[sp] == '.') {
          sp++;
          break;
        }
        if (sp + 4 > slen || strncmp(stage + sp, "==", 2) != 0) {
          break;
        }
        sp += 2;
        f_start = sp;
        while (sp + 1 < slen && strncmp(stage + sp, "==", 2) != 0) {
          sp++;
        }
        if (sp + 1 >= slen) {
          (void)c_orm_system_free(stage);
          return C_ORM_ERROR_COPYBOOK_SYNTAX;
        }
        f_end = sp;
        sp += 2;
        while (sp < slen && stage[sp] == ' ') {
          sp++;
        }
        cpy_match_kw_at(stage, slen, sp, "BY", &has_by);
        if (!has_by) {
          (void)c_orm_system_free(stage);
          return C_ORM_ERROR_COPYBOOK_SYNTAX;
        }
        sp += 2;
        while (sp < slen && stage[sp] == ' ') {
          sp++;
        }
        if (sp + 2 > slen || strncmp(stage + sp, "==", 2) != 0) {
          (void)c_orm_system_free(stage);
          return C_ORM_ERROR_COPYBOOK_SYNTAX;
        }
        sp += 2;
        t_start = sp;
        while (sp + 1 < slen && strncmp(stage + sp, "==", 2) != 0) {
          sp++;
        }
        if (sp + 1 >= slen) {
          (void)c_orm_system_free(stage);
          return C_ORM_ERROR_COPYBOOK_SYNTAX;
        }
        t_end = sp;
        sp += 2;
        {
          char *replaced = NULL;
          char *from_copy = NULL;
          char *to_copy = NULL;
          rc = c_orm_system_malloc((f_end - f_start) + 1, (void **)&from_copy);
          if (rc != C_ORM_OK) {
            (void)c_orm_system_free(stage);
            return rc;
          }
          rc = c_orm_system_malloc((t_end - t_start) + 1, (void **)&to_copy);
          if (rc != C_ORM_OK) {
            (void)c_orm_system_free(from_copy);
            (void)c_orm_system_free(stage);
            return rc;
          }
          memcpy(from_copy, stage + f_start, f_end - f_start);
          from_copy[f_end - f_start] = '\0';
          memcpy(to_copy, stage + t_start, t_end - t_start);
          to_copy[t_end - t_start] = '\0';
          rc = cpy_replace_all(stage + sp, from_copy, f_end - f_start, to_copy,
                               t_end - t_start, &replaced);
          (void)c_orm_system_free(from_copy);
          (void)c_orm_system_free(to_copy);
          (void)c_orm_system_free(stage);
          if (rc != C_ORM_OK) {
            return rc;
          }
          stage = replaced;
          slen = strlen(stage);
          sp = 0;
        }
      }
      if (sp > 0) {
        memmove(stage, stage + sp, slen - sp + 1);
      }
    }
  }

  /* Apply caller-supplied REPLACING pairs */
  for (i = 0; i < num_replacements; ++i) {
    const char *fp = NULL;
    size_t fl = 0;
    const char *tp = NULL;
    size_t tl = 0;
    char *next_stage = NULL;
    if (!replacements || !replacements[i].from_text ||
        !replacements[i].to_text) {
      (void)c_orm_system_free(stage);
      return C_ORM_ERROR_NULL_POINTER;
    }
    cpy_strip_pseudotext(replacements[i].from_text, &fp, &fl);
    cpy_strip_pseudotext(replacements[i].to_text, &tp, &tl);
    rc = cpy_replace_all(stage, fp, fl, tp, tl, &next_stage);
    (void)c_orm_system_free(stage);
    if (rc != C_ORM_OK) {
      return rc;
    }
    stage = next_stage;
  }

  *out_clean = stage;
  return C_ORM_OK;
}

/**
 * @brief Extracts any embedded `EXEC SQL DECLARE [<schema>.]<table> TABLE (...)
 * END-EXEC` block into a parsed `sql_table_t` and strips `EXEC SQL ...
 * END-EXEC[.]` blocks from the COBOL stream.
 * @param src Normalized copybook source string.
 * @param out_cobol_only Output heap-allocated COBOL-only source string.
 * @param out_schema Optional output heap-allocated SQL schema name (or NULL).
 * @param out_sql_tbl Output pointer receiving parsed `sql_table_t` (caller
 * frees via `sql_table_free` + `c_orm_system_free`), or NULL if none.
 * @return C_ORM_OK on success, C_ORM_ERROR_COPYBOOK_SYNTAX on unclosed EXEC
 * SQL, or C_ORM_ERROR_MEMORY on OOM.
 */
static c_orm_error_t cpy_extract_exec_sql(const char *src,
                                          char **out_cobol_only,
                                          char **out_schema,
                                          struct sql_table_t **out_sql_tbl) {
  c_orm_string_builder_t *sb = NULL;
  const char *sb_str = NULL;
  size_t len = strlen(src);
  size_t pos = 0;
  char quote = '\0';
  c_orm_error_t rc;

  *out_cobol_only = NULL;
  *out_schema = NULL;
  *out_sql_tbl = NULL;

  rc = c_orm_string_builder_init(&sb);
  if (rc != C_ORM_OK) {
    return rc;
  }

  while (pos < len) {
    char c = src[pos];
    if (quote != '\0') {
      if (c == quote) {
        if (pos + 1 < len && src[pos + 1] == quote) {
          rc = cpy_sb_append_n(sb, src + pos, 2);
          if (rc != C_ORM_OK) {
            (void)c_orm_string_builder_free(sb);
            return rc;
          }
          pos += 2;
          continue;
        }
        quote = '\0';
      }
      rc = cpy_sb_append_n(sb, src + pos, 1);
      if (rc != C_ORM_OK) {
        (void)c_orm_string_builder_free(sb);
        return rc;
      }
      pos++;
      continue;
    }
    if (c == '\'' || c == '"') {
      quote = c;
      rc = cpy_sb_append_n(sb, src + pos, 1);
      if (rc != C_ORM_OK) {
        (void)c_orm_string_builder_free(sb);
        return rc;
      }
      pos++;
      continue;
    }

    {
      int m_exec = 0;
      cpy_match_kw_at(src, len, pos, "EXEC", &m_exec);
      if (m_exec) {
        size_t scan = pos + 4;
        int m_sql = 0;
        while (scan < len && src[scan] == ' ') {
          scan++;
        }
        cpy_match_kw_at(src, len, scan, "SQL", &m_sql);
        if (m_sql) {
          size_t body_start = scan + 3;
          size_t body_end = body_start;
          int found_end = 0;
          while (body_end < len) {
            int m_end = 0;
            cpy_match_kw_at(src, len, body_end, "END-EXEC", &m_end);
            if (m_end) {
              found_end = 1;
              break;
            }
            body_end++;
          }
          if (!found_end) {
            (void)c_orm_string_builder_free(sb);
            (void)c_orm_system_free(*out_schema);
            *out_schema = NULL;
            if (*out_sql_tbl) {
              (void)sql_table_free(*out_sql_tbl);
              (void)c_orm_system_free(*out_sql_tbl);
              *out_sql_tbl = NULL;
            }
            return C_ORM_ERROR_COPYBOOK_SYNTAX;
          }

          /* Inspect body for DECLARE [schema.]table TABLE (...) */
          if (!*out_sql_tbl) {
            size_t bp = body_start;
            int m_decl = 0;
            while (src[bp] == ' ') {
              bp++;
            }
            cpy_match_kw_at(src, body_end, bp, "DECLARE", &m_decl);
            if (m_decl) {
              size_t q_start;
              size_t q_end;
              bp += 7;
              while (src[bp] == ' ') {
                bp++;
              }
              q_start = bp;
              while (bp < body_end && src[bp] != ' ' && src[bp] != '(') {
                bp++;
              }
              q_end = bp;
              while (src[bp] == ' ') {
                bp++;
              }
              {
                int m_tbl = 0;
                cpy_match_kw_at(src, body_end, bp, "TABLE", &m_tbl);
                if (m_tbl) {
                  bp += 5;
                  while (src[bp] == ' ') {
                    bp++;
                  }
                  if (src[bp] == '(') {
                    const char *dot = NULL;
                    size_t qi;
                    const char *t_ptr = src + q_start;
                    size_t t_len = q_end - q_start;
                    c_orm_string_builder_t *ddl_sb = NULL;
                    const char *ddl_str = NULL;
                    struct sql_table_t *parsed_tbls = NULL;
                    size_t n_tbls = 0;

                    for (qi = q_start; qi < q_end; ++qi) {
                      if (src[qi] == '.') {
                        dot = src + qi;
                        break;
                      }
                    }
                    if (dot) {
                      size_t sl = (size_t)(dot - (src + q_start));
                      size_t so = 0;
                      if (sl >= 2 && t_ptr[0] == '"' && t_ptr[sl - 1] == '"') {
                        so = 1;
                        sl -= 2;
                      }
                      rc = c_orm_system_malloc(sl + 1, (void **)out_schema);
                      if (rc != C_ORM_OK) {
                        (void)c_orm_string_builder_free(sb);
                        return rc;
                      }
                      memcpy(*out_schema, t_ptr + so, sl);
                      (*out_schema)[sl] = '\0';
                      t_ptr = dot + 1;
                      t_len = (size_t)((src + q_end) - t_ptr);
                    }
                    if (t_len >= 2 && t_ptr[0] == '"' &&
                        t_ptr[t_len - 1] == '"') {
                      t_ptr++;
                      t_len -= 2;
                    }

                    rc = c_orm_string_builder_init(&ddl_sb);
                    if (rc != C_ORM_OK) {
                      (void)c_orm_system_free(*out_schema);
                      *out_schema = NULL;
                      (void)c_orm_string_builder_free(sb);
                      return rc;
                    }
                    rc = c_orm_string_builder_append(ddl_sb, "CREATE TABLE ");
                    if (rc == C_ORM_OK) {
                      rc = cpy_sb_append_n(ddl_sb, t_ptr, t_len);
                    }
                    if (rc == C_ORM_OK) {
                      rc = c_orm_string_builder_append(ddl_sb, " ");
                    }
                    if (rc == C_ORM_OK) {
                      rc = cpy_sb_append_n(ddl_sb, src + bp, body_end - bp);
                    }
                    if (rc == C_ORM_OK) {
                      rc = c_orm_string_builder_append(ddl_sb, ";");
                    }
                    if (rc == C_ORM_OK) {
                      rc = c_orm_string_builder_get(ddl_sb, &ddl_str);
                    }
                    if (rc == C_ORM_OK) {
                      if (parse_sql_ddl(ddl_str, &parsed_tbls, &n_tbls) ==
                              C_ORM_OK &&
                          n_tbls > 0) {
                        *out_sql_tbl = parsed_tbls;
                      }
                    }
                    (void)c_orm_string_builder_free(ddl_sb);
                    if (rc != C_ORM_OK) {
                      (void)c_orm_system_free(*out_schema);
                      *out_schema = NULL;
                      (void)c_orm_string_builder_free(sb);
                      return rc;
                    }
                  }
                }
              }
            }
          }

          pos = body_end + 8;
          while (pos < len && src[pos] == ' ') {
            pos++;
          }
          if (pos < len && src[pos] == '.') {
            pos++;
          }
          continue;
        }
      }
    }

    rc = cpy_sb_append_n(sb, src + pos, 1);
    if (rc != C_ORM_OK) {
      (void)c_orm_system_free(*out_schema);
      *out_schema = NULL;
      if (*out_sql_tbl) {
        (void)sql_table_free(*out_sql_tbl);
        (void)c_orm_system_free(*out_sql_tbl);
        *out_sql_tbl = NULL;
      }
      (void)c_orm_string_builder_free(sb);
      return rc;
    }
    pos++;
  }

  c_orm_string_builder_get(sb, &sb_str);
  rc = c_orm_strdup(sb_str, out_cobol_only);
  (void)c_orm_string_builder_free(sb);
  if (rc != C_ORM_OK) {
    (void)c_orm_system_free(*out_schema);
    *out_schema = NULL;
    if (*out_sql_tbl) {
      (void)sql_table_free(*out_sql_tbl);
      (void)c_orm_system_free(*out_sql_tbl);
      *out_sql_tbl = NULL;
    }
  }
  return rc;
}

/**
 * @brief Frees all tokens inside a `cpy_tok_vec_t`.
 * @param vec Token vector to release.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t cpy_tok_vec_free(cpy_tok_vec_t *vec) {
  size_t i;
  if (!vec->items) {
    return C_ORM_OK;
  }
  for (i = 0; i < vec->count; ++i) {
    (void)c_orm_system_free(vec->items[i].text);
  }
  (void)c_orm_system_free(vec->items);
  vec->items = NULL;
  vec->count = 0;
  vec->cap = 0;
  return C_ORM_OK;
}

/**
 * @brief Appends a lexical token to `vec`.
 * @param vec Destination token vector.
 * @param kind Lexical token kind.
 * @param raw Raw character slice.
 * @param raw_len Byte length of `raw`.
 * @param uppercase Non-zero to fold ASCII lowercase letters to uppercase.
 * @param is_hex Non-zero when token came from `X'...'` hex literal.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on OOM.
 */
static c_orm_error_t cpy_tok_vec_push(cpy_tok_vec_t *vec, cpy_tok_kind_t kind,
                                      const char *raw, size_t raw_len,
                                      int uppercase, int is_hex) {
  char *copy = NULL;
  size_t i;
  c_orm_error_t rc;
  if (vec->count == vec->cap) {
    size_t new_cap = (vec->cap == 0) ? 32 : (vec->cap * 2);
    void *new_items = NULL;
    rc = c_orm_system_realloc(vec->items, new_cap * sizeof(cpy_token_t),
                              &new_items);
    if (rc != C_ORM_OK) {
      return rc;
    }
    vec->items = (cpy_token_t *)new_items;
    vec->cap = new_cap;
  }
  rc = c_orm_system_malloc(raw_len + 1, (void **)&copy);
  if (rc != C_ORM_OK) {
    return rc;
  }
  for (i = 0; i < raw_len; ++i) {
    unsigned char c = (unsigned char)raw[i];
    if (uppercase) {
      cpy_char_to_upper(c, &c);
    }
    copy[i] = (char)c;
  }
  copy[raw_len] = '\0';
  vec->items[vec->count].kind = kind;
  vec->items[vec->count].text = copy;
  vec->items[vec->count].is_hex_lit = is_hex;
  vec->count++;
  return C_ORM_OK;
}

/**
 * @brief Converts a single hexadecimal ASCII character to `0..15`, or `-1` if
 * not a valid hex digit.
 * @param hc Input character.
 * @param out_nib Output nybble (`0..15` or `-1`).
 * @return C_ORM_OK on success.
 */
static c_orm_error_t cpy_hex_nibble(char hc, int *out_nib) {
  unsigned char u = 0;
  cpy_char_to_upper((unsigned char)hc, &u);
  if (u >= '0' && u <= '9') {
    *out_nib = (int)(u - '0');
  } else if (u >= 'A' && u <= 'F') {
    *out_nib = (int)(u - 'A') + 10;
  } else {
    *out_nib = -1;
  }
  return C_ORM_OK;
}

/**
 * @brief Tokenizes normalized COBOL Copybook text, handling `PIC` / `PICTURE`
 * character-strings, quoted/hex literals, signed decimal numbers, and separator
 * periods.
 * @param src Normalized COBOL text without `EXEC SQL`.
 * @param out_vec Output populated token vector.
 * @return C_ORM_OK on success, C_ORM_ERROR_COPYBOOK_SYNTAX on malformed
 * literal, or C_ORM_ERROR_MEMORY on OOM.
 */
static c_orm_error_t cpy_lex_source(const char *src, cpy_tok_vec_t *out_vec) {
  size_t len = strlen(src);
  size_t pos = 0;
  int expect_pic = 0;
  c_orm_error_t rc;

  memset(out_vec, 0, sizeof(*out_vec));

  while (pos < len) {
    char c = src[pos];
    if (strchr(" \t,;", (unsigned char)c) != NULL) {
      pos++;
      continue;
    }

    if (expect_pic) {
      int m_is = 0;
      cpy_match_kw_at(src, len, pos, "IS", &m_is);
      if (m_is) {
        pos += 2;
        continue;
      }
      {
        size_t p_start = pos;
        size_t p_end;
        int trailing_dot = 0;
        while (pos < len && strchr(" \t;", (unsigned char)src[pos]) == NULL) {
          pos++;
        }
        p_end = pos;
        if (src[p_end - 1] == ',') {
          p_end--;
        } else if (src[p_end - 1] == '.') {
          p_end--;
          trailing_dot = 1;
        }
        if (p_end == p_start) {
          (void)cpy_tok_vec_free(out_vec);
          return C_ORM_ERROR_COPYBOOK_SYNTAX;
        }
        rc = cpy_tok_vec_push(out_vec, CPY_TOK_PIC_STR, src + p_start,
                              p_end - p_start, 1, 0);
        if (rc != C_ORM_OK) {
          (void)cpy_tok_vec_free(out_vec);
          return rc;
        }
        if (trailing_dot) {
          rc = cpy_tok_vec_push(out_vec, CPY_TOK_DOT, ".", 1, 0, 0);
          if (rc != C_ORM_OK) {
            (void)cpy_tok_vec_free(out_vec);
            return rc;
          }
        }
        expect_pic = 0;
        continue;
      }
    }

    if (c == '.' &&
        !(pos + 1 < len && src[pos + 1] >= '0' && src[pos + 1] <= '9')) {
      rc = cpy_tok_vec_push(out_vec, CPY_TOK_DOT, ".", 1, 0, 0);
      if (rc != C_ORM_OK) {
        (void)cpy_tok_vec_free(out_vec);
        return rc;
      }
      pos++;
      continue;
    }

    /* Hex literal X'...' or character literal '...' / "..." */
    if ((c == 'X' || c == 'x') && pos + 1 < len &&
        (src[pos + 1] == '\'' || src[pos + 1] == '"')) {
      char q = src[pos + 1];
      size_t h_start = pos + 2;
      size_t h_end;
      size_t hex_len;
      char *decoded = NULL;
      size_t k;
      pos += 2;
      while (src[pos] != q) {
        pos++;
      }
      h_end = pos;
      pos++;
      hex_len = h_end - h_start;
      if (hex_len == 0 || (hex_len % 2) != 0) {
        (void)cpy_tok_vec_free(out_vec);
        return C_ORM_ERROR_COPYBOOK_SYNTAX;
      }
      rc = c_orm_system_malloc((hex_len / 2) + 1, (void **)&decoded);
      if (rc != C_ORM_OK) {
        (void)cpy_tok_vec_free(out_vec);
        return rc;
      }
      for (k = 0; k < hex_len; k += 2) {
        int hi_v = -1;
        int lo_v = -1;
        cpy_hex_nibble(src[h_start + k], &hi_v);
        cpy_hex_nibble(src[h_start + k + 1], &lo_v);
        if (hi_v < 0 || lo_v < 0) {
          (void)c_orm_system_free(decoded);
          (void)cpy_tok_vec_free(out_vec);
          return C_ORM_ERROR_COPYBOOK_SYNTAX;
        }
        decoded[k / 2] = (char)(((unsigned int)hi_v << 4) | (unsigned int)lo_v);
      }
      decoded[hex_len / 2] = '\0';
      rc =
          cpy_tok_vec_push(out_vec, CPY_TOK_STRING, decoded, hex_len / 2, 0, 1);
      (void)c_orm_system_free(decoded);
      if (rc != C_ORM_OK) {
        (void)cpy_tok_vec_free(out_vec);
        return rc;
      }
      continue;
    }

    if (c == '\'' || c == '"') {
      char q = c;
      c_orm_string_builder_t *qsb = NULL;
      const char *qstr = NULL;
      size_t qlen = 0;
      pos++;
      rc = c_orm_string_builder_init(&qsb);
      if (rc != C_ORM_OK) {
        (void)cpy_tok_vec_free(out_vec);
        return rc;
      }
      for (;;) {
        if (src[pos] == q) {
          if (src[pos + 1] == q) {
            rc = cpy_sb_append_n(qsb, &q, 1);
            if (rc != C_ORM_OK) {
              (void)c_orm_string_builder_free(qsb);
              (void)cpy_tok_vec_free(out_vec);
              return rc;
            }
            pos += 2;
            continue;
          }
          pos++;
          break;
        }
        rc = cpy_sb_append_n(qsb, src + pos, 1);
        if (rc != C_ORM_OK) {
          (void)c_orm_string_builder_free(qsb);
          (void)cpy_tok_vec_free(out_vec);
          return rc;
        }
        pos++;
      }
      c_orm_string_builder_get(qsb, &qstr);
      c_orm_string_builder_len(qsb, &qlen);
      rc = cpy_tok_vec_push(out_vec, CPY_TOK_STRING, qstr, qlen, 0, 0);
      (void)c_orm_string_builder_free(qsb);
      if (rc != C_ORM_OK) {
        (void)cpy_tok_vec_free(out_vec);
        return rc;
      }
      continue;
    }

    if (c == '(' || c == ')') {
      pos++;
      continue;
    }

    /* Word or signed numeric literal */
    {
      size_t w_start = pos;
      size_t w_end;
      int trailing_dot = 0;
      while (pos < len &&
             strchr(" \t,;()'\"", (unsigned char)src[pos]) == NULL) {
        if (src[pos] == '.' &&
            !(pos + 1 < len && src[pos + 1] >= '0' && src[pos + 1] <= '9')) {
          trailing_dot = 1;
          break;
        }
        pos++;
      }
      w_end = pos;
      if (trailing_dot) {
        pos++;
      }
      {
        const char *word_text;
        rc = cpy_tok_vec_push(out_vec, CPY_TOK_WORD, src + w_start,
                              w_end - w_start, 1, 0);
        if (rc != C_ORM_OK) {
          (void)cpy_tok_vec_free(out_vec);
          return rc;
        }
        word_text = out_vec->items[out_vec->count - 1].text;
        if (!trailing_dot && (strcmp(word_text, "PIC") == 0 ||
                              strcmp(word_text, "PICTURE") == 0)) {
          expect_pic = 1;
        }
      }
      if (trailing_dot) {
        rc = cpy_tok_vec_push(out_vec, CPY_TOK_DOT, ".", 1, 0, 0);
        if (rc != C_ORM_OK) {
          (void)cpy_tok_vec_free(out_vec);
          return rc;
        }
      }
    }
  }

  if (expect_pic) {
    (void)cpy_tok_vec_free(out_vec);
    return C_ORM_ERROR_COPYBOOK_SYNTAX;
  }
  return C_ORM_OK;
}

/**
 * @brief Analyzes an uppercase COBOL `PIC` character-string and populates
 * numeric precision/scale, display length, sign flag, and `pic_category` on
 * `field`.
 * @param pic Uppercase `PIC` descriptor string.
 * @param field Target field node to populate.
 * @return C_ORM_OK on success, or C_ORM_ERROR_COPYBOOK_SYNTAX on invalid `PIC`.
 */
static c_orm_error_t cpy_analyze_pic(const char *pic,
                                     c_orm_cobol_field_t *field) {
  size_t len = strlen(pic);
  size_t i = 0;
  size_t int_d = 0;
  size_t frac_d = 0;
  long scale_p = 0;
  size_t disp_len = 0;
  int seen_v = 0;
  int seen_9_after_v = 0;
  int has_x = 0;
  int has_a = 0;
  int has_g = 0;
  int has_non_g = 0;
  int has_edit = 0;

  while (i < len) {
    char sym = pic[i];
    size_t rep = 1;
    size_t sym_width = 1;
    int is_cr_db = 0;

    if (i + 1 < len && ((sym == 'C' && pic[i + 1] == 'R') ||
                        (sym == 'D' && pic[i + 1] == 'B'))) {
      is_cr_db = 1;
      sym_width = 2;
      i += 2;
    } else {
      i++;
    }

    if (i < len && pic[i] == '(') {
      size_t r_val = 0;
      size_t digits_seen = 0;
      if (is_cr_db || sym == 'S' || sym == 'V') {
        return C_ORM_ERROR_COPYBOOK_SYNTAX;
      }
      i++;
      while (i < len && pic[i] >= '0' && pic[i] <= '9') {
        r_val = r_val * 10 + (size_t)(pic[i] - '0');
        digits_seen++;
        if (r_val > 65535) {
          return C_ORM_ERROR_COPYBOOK_SYNTAX;
        }
        i++;
      }
      if (digits_seen == 0 || r_val == 0 || i >= len || pic[i] != ')') {
        return C_ORM_ERROR_COPYBOOK_SYNTAX;
      }
      i++;
      rep = r_val;
    }

    if (is_cr_db) {
      has_edit = 1;
      has_non_g = 1;
      disp_len += sym_width;
    } else if (sym == 'S') {
      if (i > 1) {
        return C_ORM_ERROR_COPYBOOK_SYNTAX;
      }
      field->is_signed = 1;
      has_non_g = 1;
    } else if (sym == 'V') {
      if (seen_v || has_x || has_a || has_g) {
        return C_ORM_ERROR_COPYBOOK_SYNTAX;
      }
      seen_v = 1;
      has_non_g = 1;
    } else if (sym == '9') {
      if (seen_v) {
        frac_d += rep;
        seen_9_after_v = 1;
      } else {
        int_d += rep;
      }
      has_non_g = 1;
      disp_len += rep;
    } else if (sym == 'P') {
      if (seen_v && !seen_9_after_v) {
        scale_p -= (long)rep;
      } else {
        scale_p += (long)rep;
      }
      has_non_g = 1;
    } else if (sym == 'X') {
      has_x = 1;
      has_non_g = 1;
      disp_len += rep;
    } else if (sym == 'A') {
      has_a = 1;
      has_non_g = 1;
      disp_len += rep;
    } else if (sym == 'G' || sym == 'N') {
      has_g = 1;
      disp_len += rep;
    } else if (strchr("Z*$,.+-B0/", (unsigned char)sym) != NULL) {
      has_edit = 1;
      has_non_g = 1;
      disp_len += rep;
    } else {
      return C_ORM_ERROR_COPYBOOK_SYNTAX;
    }
  }

  field->display_length = disp_len;
  field->scale_p = (signed char)scale_p;

  if (has_g) {
    if (has_non_g) {
      return C_ORM_ERROR_COPYBOOK_SYNTAX;
    }
    field->pic_category = C_ORM_COBOL_PIC_NATIONAL;
    return C_ORM_OK;
  }
  if (has_edit) {
    if (field->is_signed) {
      return C_ORM_ERROR_COPYBOOK_SYNTAX;
    }
    field->pic_category = (has_x || has_a) ? C_ORM_COBOL_PIC_ALPHANUMERIC_EDITED
                                           : C_ORM_COBOL_PIC_NUMERIC_EDITED;
    return C_ORM_OK;
  }
  if (has_x || (has_a && (int_d + frac_d) > 0)) {
    if (field->is_signed || seen_v) {
      return C_ORM_ERROR_COPYBOOK_SYNTAX;
    }
    field->pic_category = C_ORM_COBOL_PIC_ALPHANUMERIC;
    return C_ORM_OK;
  }
  if (has_a) {
    if (field->is_signed || seen_v) {
      return C_ORM_ERROR_COPYBOOK_SYNTAX;
    }
    field->pic_category = C_ORM_COBOL_PIC_ALPHABETIC;
    return C_ORM_OK;
  }
  if ((int_d + frac_d) > 0) {
    if ((int_d + frac_d) > C_ORM_DECIMAL_MAX_DIGITS) {
      return C_ORM_ERROR_COPYBOOK_SYNTAX;
    }
    field->int_digits = (unsigned char)int_d;
    field->frac_digits = (unsigned char)frac_d;
    field->total_digits = (unsigned char)(int_d + frac_d);
    field->pic_category = C_ORM_COBOL_PIC_NUMERIC;
    return C_ORM_OK;
  }
  return C_ORM_ERROR_COPYBOOK_SYNTAX;
}

/**
 * @brief Parses an unsigned positive integer token (`1..65535`).
 * @param text Token text.
 * @param out_val Output parsed value.
 * @return C_ORM_OK on success, or C_ORM_ERROR_COPYBOOK_SYNTAX if not a valid
 * integer.
 */
static c_orm_error_t cpy_parse_uint_tok(const char *text, size_t *out_val) {
  size_t i = 0;
  size_t v = 0;
  while (text[i] != '\0') {
    if (text[i] < '0' || text[i] > '9') {
      return C_ORM_ERROR_COPYBOOK_SYNTAX;
    }
    v = v * 10 + (size_t)(text[i] - '0');
    if (v > 1000000UL) {
      return C_ORM_ERROR_COPYBOOK_SYNTAX;
    }
    i++;
  }
  *out_val = v;
  return C_ORM_OK;
}

/**
 * @brief Classifies a COBOL `VALUE` token into `c_orm_cobol_value_kind_t` and
 * canonical string form.
 * @param tok Lexical token representing the literal.
 * @param out_kind Output value classification.
 * @param out_str Output heap-allocated value string.
 * @return C_ORM_OK on success, C_ORM_ERROR_COPYBOOK_SYNTAX on invalid literal,
 * or C_ORM_ERROR_MEMORY on OOM.
 */
static c_orm_error_t cpy_parse_value_token(const cpy_token_t *tok,
                                           c_orm_cobol_value_kind_t *out_kind,
                                           char **out_str) {
  c_orm_decimal_t tmp_dec;
  if (tok->kind == CPY_TOK_STRING) {
    *out_kind = C_ORM_COBOL_VALUE_STRING;
    return c_orm_strdup(tok->text, out_str);
  }
  if (tok->kind != CPY_TOK_WORD) {
    return C_ORM_ERROR_COPYBOOK_SYNTAX;
  }
  if (strcmp(tok->text, "ZERO") == 0 || strcmp(tok->text, "ZEROS") == 0 ||
      strcmp(tok->text, "ZEROES") == 0) {
    *out_kind = C_ORM_COBOL_VALUE_ZEROS;
    return c_orm_strdup("0", out_str);
  }
  if (strcmp(tok->text, "SPACE") == 0 || strcmp(tok->text, "SPACES") == 0) {
    *out_kind = C_ORM_COBOL_VALUE_SPACES;
    return c_orm_strdup(" ", out_str);
  }
  if (strcmp(tok->text, "LOW-VALUE") == 0 ||
      strcmp(tok->text, "LOW-VALUES") == 0) {
    *out_kind = C_ORM_COBOL_VALUE_LOW_VALUES;
    return c_orm_strdup("", out_str);
  }
  if (strcmp(tok->text, "HIGH-VALUE") == 0 ||
      strcmp(tok->text, "HIGH-VALUES") == 0) {
    *out_kind = C_ORM_COBOL_VALUE_HIGH_VALUES;
    return c_orm_strdup("\xFF", out_str);
  }
  if (c_orm_decimal_from_str(tok->text, &tmp_dec) == C_ORM_OK) {
    *out_kind = C_ORM_COBOL_VALUE_NUMERIC;
    return c_orm_strdup(tok->text, out_str);
  }
  return C_ORM_ERROR_COPYBOOK_SYNTAX;
}

/**
 * @brief Checks if `vec->items[pos]` is any `CPY_TOK_WORD`.
 * @param vec Token stream.
 * @param pos Token index.
 * @param out_yes Output set to 1 if `pos < count` and kind is `CPY_TOK_WORD`.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t cpy_tok_is_any_word(const cpy_tok_vec_t *vec, size_t pos,
                                         int *out_yes) {
  *out_yes = (pos < vec->count && vec->items[pos].kind == CPY_TOK_WORD) ? 1 : 0;
  return C_ORM_OK;
}

/**
 * @brief Checks if `vec->items[pos]` is a `CPY_TOK_WORD` equal to `kw`.
 * @param vec Token stream.
 * @param pos Token index.
 * @param kw Uppercase keyword to compare.
 * @param out_yes Output set to 1 on match, else 0.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t cpy_tok_is_word(const cpy_tok_vec_t *vec, size_t pos,
                                     const char *kw, int *out_yes) {
  int any = 0;
  cpy_tok_is_any_word(vec, pos, &any);
  *out_yes = (any && strcmp(vec->items[pos].text, kw) == 0) ? 1 : 0;
  return C_ORM_OK;
}

/**
 * @brief Checks if `vec->items[pos]` is a `CPY_TOK_WORD` equal to `kw1` or
 * `kw2`.
 * @param vec Token stream.
 * @param pos Token index.
 * @param kw1 First uppercase keyword.
 * @param kw2 Second uppercase keyword.
 * @param out_yes Output set to 1 on match, else 0.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t cpy_tok_is_word2(const cpy_tok_vec_t *vec, size_t pos,
                                      const char *kw1, const char *kw2,
                                      int *out_yes) {
  int m1 = 0;
  int m2 = 0;
  cpy_tok_is_word(vec, pos, kw1, &m1);
  cpy_tok_is_word(vec, pos, kw2, &m2);
  *out_yes = (m1 || m2) ? 1 : 0;
  return C_ORM_OK;
}

/**
 * @brief Appends a parsed level-`88` condition item onto `target_field`.
 * @param vec Token vector.
 * @param io_pos Input/output token cursor positioned right after `"88"`.
 * @param target_field Parent COBOL field receiving the condition.
 * @return C_ORM_OK on success, C_ORM_ERROR_COPYBOOK_SYNTAX on malformed `88`,
 * or C_ORM_ERROR_MEMORY on OOM.
 */
static c_orm_error_t cpy_parse_condition_88(const cpy_tok_vec_t *vec,
                                            size_t *io_pos,
                                            c_orm_cobol_field_t *target_field) {
  size_t pos = *io_pos;
  c_orm_cobol_condition_t cond;
  void *new_arr = NULL;
  int m_kw = 0;
  c_orm_error_t rc;

  cpy_tok_is_any_word(vec, pos, &m_kw);
  if (!target_field || !m_kw) {
    return C_ORM_ERROR_COPYBOOK_SYNTAX;
  }
  memset(&cond, 0, sizeof(cond));
  rc = c_orm_strdup(vec->items[pos].text, &cond.cobol_name);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = cpy_normalize_c_name(cond.cobol_name, &cond.c_name);
  if (rc != C_ORM_OK) {
    (void)c_orm_system_free(cond.cobol_name);
    return rc;
  }
  pos++;

  cpy_tok_is_word2(vec, pos, "VALUE", "VALUES", &m_kw);
  if (m_kw) {
    pos++;
    cpy_tok_is_word2(vec, pos, "IS", "ARE", &m_kw);
    if (m_kw) {
      pos++;
    }
  } else {
    (void)c_orm_system_free(cond.cobol_name);
    (void)c_orm_system_free(cond.c_name);
    return C_ORM_ERROR_COPYBOOK_SYNTAX;
  }

  while (pos < vec->count && vec->items[pos].kind != CPY_TOK_DOT) {
    c_orm_cobol_value_kind_t lkind = C_ORM_COBOL_VALUE_NONE;
    char *lval = NULL;
    char *hval = NULL;
    int is_range = 0;
    void *new_vals = NULL;

    cpy_tok_is_word(vec, pos, "ALL", &m_kw);
    if (m_kw) {
      pos++;
      if (pos >= vec->count) {
        rc = C_ORM_ERROR_COPYBOOK_SYNTAX;
        goto fail;
      }
    }
    rc = cpy_parse_value_token(&vec->items[pos], &lkind, &lval);
    if (rc != C_ORM_OK) {
      goto fail;
    }
    pos++;

    cpy_tok_is_word2(vec, pos, "THRU", "THROUGH", &m_kw);
    if (m_kw) {
      c_orm_cobol_value_kind_t hkind = C_ORM_COBOL_VALUE_NONE;
      pos++;
      if (pos >= vec->count) {
        (void)c_orm_system_free(lval);
        rc = C_ORM_ERROR_COPYBOOK_SYNTAX;
        goto fail;
      }
      rc = cpy_parse_value_token(&vec->items[pos], &hkind, &hval);
      if (rc != C_ORM_OK) {
        (void)c_orm_system_free(lval);
        goto fail;
      }
      is_range = 1;
      pos++;
    }

    rc = c_orm_system_realloc(
        cond.values, (cond.num_values + 1) * sizeof(c_orm_cobol_value_range_t),
        &new_vals);
    if (rc != C_ORM_OK) {
      (void)c_orm_system_free(lval);
      (void)c_orm_system_free(hval);
      goto fail;
    }
    cond.values = (c_orm_cobol_value_range_t *)new_vals;
    cond.values[cond.num_values].is_range = is_range;
    cond.values[cond.num_values].is_numeric =
        (lkind == C_ORM_COBOL_VALUE_NUMERIC || lkind == C_ORM_COBOL_VALUE_ZEROS)
            ? 1
            : 0;
    cond.values[cond.num_values].low_val = lval;
    cond.values[cond.num_values].high_val = hval;
    cond.num_values++;
  }

  if (cond.num_values == 0 || pos >= vec->count) {
    rc = C_ORM_ERROR_COPYBOOK_SYNTAX;
    goto fail;
  }
  pos++;

  rc = c_orm_system_realloc(target_field->conditions,
                            (target_field->num_conditions + 1) *
                                sizeof(c_orm_cobol_condition_t),
                            &new_arr);
  if (rc != C_ORM_OK) {
    goto fail;
  }
  target_field->conditions = (c_orm_cobol_condition_t *)new_arr;
  target_field->conditions[target_field->num_conditions] = cond;
  target_field->num_conditions++;
  *io_pos = pos;
  return C_ORM_OK;

fail:
  (void)c_orm_system_free(cond.cobol_name);
  (void)c_orm_system_free(cond.c_name);
  if (cond.values) {
    size_t vi;
    for (vi = 0; vi < cond.num_values; ++vi) {
      (void)c_orm_system_free(cond.values[vi].low_val);
      (void)c_orm_system_free(cond.values[vi].high_val);
    }
    (void)c_orm_system_free(cond.values);
  }
  return rc;
}

/**
 * @brief Checks if `word` is a COBOL clause keyword indicating an unnamed
 * implicit `FILLER` item.
 * @param word Uppercase token string.
 * @param out_yes Output set to 1 if `word` starts a clause, else 0.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t cpy_is_clause_starter(const char *word, int *out_yes) {
  static const char *const starters[] = {"PIC",       "PICTURE", "USAGE",
                                         "COMP",      "COMP-3",  "VALUE",
                                         "REDEFINES", "OCCURS",  NULL};
  size_t i;
  *out_yes = 0;
  for (i = 0; starters[i] != NULL; ++i) {
    if (strcmp(word, starters[i]) == 0) {
      *out_yes = 1;
      return C_ORM_OK;
    }
  }
  return C_ORM_OK;
}

/**
 * @brief Parses a single COBOL data description entry (`01..49`, `66`, or `77`)
 * up to and including its terminating `.` token.
 * @param vec Lexical token stream.
 * @param io_pos Input/output token index (positioned at level token on entry).
 * @param level Parsed numeric level (`1..49`, `66`, or `77`).
 * @param filler_seq Monotonic counter used to synthesize unique C names for
 * `FILLER` fields.
 * @param out_field Output populated `c_orm_cobol_field_t`.
 * @param out_has_explicit_usage Output flag set to 1 if an explicit `USAGE`
 * clause was parsed on this item.
 * @return C_ORM_OK on success, C_ORM_ERROR_COPYBOOK_SYNTAX on invalid syntax,
 * or C_ORM_ERROR_MEMORY on OOM.
 */
static c_orm_error_t cpy_parse_field_entry(const cpy_tok_vec_t *vec,
                                           size_t *io_pos, int level,
                                           size_t *filler_seq,
                                           c_orm_cobol_field_t *out_field,
                                           int *out_has_explicit_usage) {
  size_t pos = *io_pos + 1;
  int has_explicit_usage = 0;
  int is_starter = 0;
  c_orm_error_t rc;

  memset(out_field, 0, sizeof(*out_field));
  out_field->level = level;
  out_field->usage = C_ORM_COBOL_USAGE_DISPLAY;
  *out_has_explicit_usage = 0;

  if (pos >= vec->count) {
    return C_ORM_ERROR_COPYBOOK_SYNTAX;
  }

  /* Field name or implicit/explicit FILLER */
  if (vec->items[pos].kind == CPY_TOK_WORD) {
    cpy_is_clause_starter(vec->items[pos].text, &is_starter);
  }
  if (vec->items[pos].kind == CPY_TOK_WORD && !is_starter) {
    if (strcmp(vec->items[pos].text, "FILLER") == 0) {
      out_field->is_filler = 1;
    }
    rc = c_orm_strdup(vec->items[pos].text, &out_field->cobol_name);
    if (rc != C_ORM_OK) {
      return rc;
    }
    pos++;
  } else {
    if (level == 1 || level == 66 || level == 77) {
      return C_ORM_ERROR_COPYBOOK_SYNTAX;
    }
    out_field->is_filler = 1;
    rc = c_orm_strdup("FILLER", &out_field->cobol_name);
    if (rc != C_ORM_OK) {
      return rc;
    }
  }

  if (out_field->is_filler) {
    char fbuf[48];
    (*filler_seq)++;
    rc = C_ORM_SPRINTF(fbuf, sizeof(fbuf), "filler_" C_ORM_FMT_SIZE_T,
                       C_ORM_CAST_SIZE_T(*filler_seq));
    if (rc == C_ORM_OK) {
      rc = c_orm_strdup(fbuf, &out_field->c_name);
    }
    if (rc == C_ORM_OK) {
      rc = cpy_normalize_sql_name(fbuf, &out_field->sql_name);
    }
  } else {
    rc = cpy_normalize_c_name(out_field->cobol_name, &out_field->c_name);
    if (rc == C_ORM_OK) {
      rc = cpy_normalize_sql_name(out_field->cobol_name, &out_field->sql_name);
    }
  }
  if (rc != C_ORM_OK) {
    (void)cpy_field_contents_free(out_field);
    return rc;
  }

  /* Level 66 RENAMES <from> [THRU|THROUGH <thru>]. */
  if (level == 66) {
    int m_kw = 0;
    cpy_tok_is_word(vec, pos, "RENAMES", &m_kw);
    if (!m_kw) {
      (void)cpy_field_contents_free(out_field);
      return C_ORM_ERROR_COPYBOOK_SYNTAX;
    }
    pos++;
    cpy_tok_is_any_word(vec, pos, &m_kw);
    if (!m_kw) {
      (void)cpy_field_contents_free(out_field);
      return C_ORM_ERROR_COPYBOOK_SYNTAX;
    }
    rc = c_orm_strdup(vec->items[pos].text, &out_field->renames_from);
    if (rc != C_ORM_OK) {
      (void)cpy_field_contents_free(out_field);
      return rc;
    }
    pos++;
    cpy_tok_is_word2(vec, pos, "THRU", "THROUGH", &m_kw);
    if (m_kw) {
      pos++;
      cpy_tok_is_any_word(vec, pos, &m_kw);
      if (!m_kw) {
        (void)cpy_field_contents_free(out_field);
        return C_ORM_ERROR_COPYBOOK_SYNTAX;
      }
      rc = c_orm_strdup(vec->items[pos].text, &out_field->renames_thru);
      if (rc != C_ORM_OK) {
        (void)cpy_field_contents_free(out_field);
        return rc;
      }
      pos++;
    }
    if (pos >= vec->count || vec->items[pos].kind != CPY_TOK_DOT) {
      (void)cpy_field_contents_free(out_field);
      return C_ORM_ERROR_COPYBOOK_SYNTAX;
    }
    *io_pos = pos + 1;
    return C_ORM_OK;
  }

  /* Parse clauses until terminating CPY_TOK_DOT */
  while (pos < vec->count && vec->items[pos].kind != CPY_TOK_DOT) {
    const cpy_token_t *t = &vec->items[pos];
    int m_kw = 0;
    if (t->kind != CPY_TOK_WORD) {
      (void)cpy_field_contents_free(out_field);
      return C_ORM_ERROR_COPYBOOK_SYNTAX;
    }

    if (strcmp(t->text, "IS") == 0) {
      pos++;
      continue;
    }
    if (strcmp(t->text, "REDEFINES") == 0) {
      pos++;
      cpy_tok_is_any_word(vec, pos, &m_kw);
      if (!m_kw || out_field->redefines_name != NULL) {
        (void)cpy_field_contents_free(out_field);
        return C_ORM_ERROR_COPYBOOK_SYNTAX;
      }
      rc = c_orm_strdup(vec->items[pos].text, &out_field->redefines_name);
      if (rc != C_ORM_OK) {
        (void)cpy_field_contents_free(out_field);
        return rc;
      }
      pos++;
      continue;
    }
    if (strcmp(t->text, "PIC") == 0 || strcmp(t->text, "PICTURE") == 0) {
      pos++;
      if (vec->items[pos].kind != CPY_TOK_PIC_STR ||
          out_field->pic_raw != NULL) {
        (void)cpy_field_contents_free(out_field);
        return C_ORM_ERROR_COPYBOOK_SYNTAX;
      }
      rc = c_orm_strdup(vec->items[pos].text, &out_field->pic_raw);
      if (rc != C_ORM_OK) {
        (void)cpy_field_contents_free(out_field);
        return rc;
      }
      rc = cpy_analyze_pic(out_field->pic_raw, out_field);
      if (rc != C_ORM_OK) {
        (void)cpy_field_contents_free(out_field);
        return rc;
      }
      if (out_field->pic_category == C_ORM_COBOL_PIC_NATIONAL &&
          !has_explicit_usage) {
        out_field->usage = C_ORM_COBOL_USAGE_DISPLAY_1;
      }
      pos++;
      continue;
    }
    if (strcmp(t->text, "USAGE") == 0) {
      pos++;
      cpy_tok_is_word(vec, pos, "IS", &m_kw);
      if (m_kw) {
        pos++;
      }
      continue;
    }
    if (strcmp(t->text, "DISPLAY") == 0) {
      out_field->usage = C_ORM_COBOL_USAGE_DISPLAY;
      has_explicit_usage = 1;
      pos++;
      continue;
    }
    if (strcmp(t->text, "DISPLAY-1") == 0 || strcmp(t->text, "NATIONAL") == 0) {
      out_field->usage = C_ORM_COBOL_USAGE_DISPLAY_1;
      has_explicit_usage = 1;
      pos++;
      continue;
    }
    if (strcmp(t->text, "COMP") == 0 || strcmp(t->text, "COMPUTATIONAL") == 0 ||
        strcmp(t->text, "COMP-4") == 0 ||
        strcmp(t->text, "COMPUTATIONAL-4") == 0 ||
        strcmp(t->text, "BINARY") == 0) {
      out_field->usage = C_ORM_COBOL_USAGE_COMP;
      has_explicit_usage = 1;
      pos++;
      continue;
    }
    if (strcmp(t->text, "COMP-5") == 0 ||
        strcmp(t->text, "COMPUTATIONAL-5") == 0) {
      out_field->usage = C_ORM_COBOL_USAGE_COMP_5;
      has_explicit_usage = 1;
      pos++;
      continue;
    }
    if (strcmp(t->text, "COMP-1") == 0 ||
        strcmp(t->text, "COMPUTATIONAL-1") == 0) {
      out_field->usage = C_ORM_COBOL_USAGE_COMP_1;
      has_explicit_usage = 1;
      pos++;
      continue;
    }
    if (strcmp(t->text, "COMP-2") == 0 ||
        strcmp(t->text, "COMPUTATIONAL-2") == 0) {
      out_field->usage = C_ORM_COBOL_USAGE_COMP_2;
      has_explicit_usage = 1;
      pos++;
      continue;
    }
    if (strcmp(t->text, "COMP-3") == 0 ||
        strcmp(t->text, "COMPUTATIONAL-3") == 0 ||
        strcmp(t->text, "PACKED-DECIMAL") == 0) {
      out_field->usage = C_ORM_COBOL_USAGE_COMP_3;
      has_explicit_usage = 1;
      pos++;
      continue;
    }
    if (strcmp(t->text, "SIGN") == 0) {
      pos++;
      cpy_tok_is_word(vec, pos, "IS", &m_kw);
      if (m_kw) {
        pos++;
      }
      cpy_tok_is_any_word(vec, pos, &m_kw);
      if (!m_kw) {
        (void)cpy_field_contents_free(out_field);
        return C_ORM_ERROR_COPYBOOK_SYNTAX;
      }
      if (strcmp(vec->items[pos].text, "LEADING") == 0) {
        out_field->sign_leading = 1;
        pos++;
      } else if (strcmp(vec->items[pos].text, "TRAILING") == 0) {
        out_field->sign_leading = 0;
        pos++;
      } else {
        (void)cpy_field_contents_free(out_field);
        return C_ORM_ERROR_COPYBOOK_SYNTAX;
      }
      cpy_tok_is_word(vec, pos, "SEPARATE", &m_kw);
      if (m_kw) {
        out_field->sign_separate = 1;
        pos++;
        cpy_tok_is_word(vec, pos, "CHARACTER", &m_kw);
        if (m_kw) {
          pos++;
        }
      }
      continue;
    }
    if (strcmp(t->text, "SYNC") == 0 || strcmp(t->text, "SYNCHRONIZED") == 0) {
      out_field->is_synchronized = 1;
      pos++;
      cpy_tok_is_word2(vec, pos, "LEFT", "RIGHT", &m_kw);
      if (m_kw) {
        pos++;
      }
      continue;
    }
    if (strcmp(t->text, "JUST") == 0 || strcmp(t->text, "JUSTIFIED") == 0) {
      out_field->is_justified_right = 1;
      pos++;
      cpy_tok_is_word(vec, pos, "RIGHT", &m_kw);
      if (m_kw) {
        pos++;
      }
      continue;
    }
    if (strcmp(t->text, "BLANK") == 0) {
      int z1 = 0;
      int z2 = 0;
      pos++;
      cpy_tok_is_word(vec, pos, "WHEN", &m_kw);
      if (m_kw) {
        pos++;
      }
      cpy_tok_is_word2(vec, pos, "ZERO", "ZEROS", &z1);
      cpy_tok_is_word(vec, pos, "ZEROES", &z2);
      if (!(z1 || z2)) {
        (void)cpy_field_contents_free(out_field);
        return C_ORM_ERROR_COPYBOOK_SYNTAX;
      }
      out_field->is_blank_when_zero = 1;
      pos++;
      continue;
    }
    if (strcmp(t->text, "VALUE") == 0 || strcmp(t->text, "VALUES") == 0) {
      pos++;
      cpy_tok_is_word2(vec, pos, "IS", "ARE", &m_kw);
      if (m_kw) {
        pos++;
      }
      cpy_tok_is_word(vec, pos, "ALL", &m_kw);
      if (m_kw) {
        pos++;
      }
      if (pos >= vec->count || out_field->default_value != NULL) {
        (void)cpy_field_contents_free(out_field);
        return C_ORM_ERROR_COPYBOOK_SYNTAX;
      }
      rc = cpy_parse_value_token(&vec->items[pos], &out_field->value_kind,
                                 &out_field->default_value);
      if (rc != C_ORM_OK) {
        (void)cpy_field_contents_free(out_field);
        return rc;
      }
      pos++;
      continue;
    }
    if (strcmp(t->text, "OCCURS") == 0) {
      size_t first_n = 0;
      pos++;
      cpy_tok_is_any_word(vec, pos, &m_kw);
      if (!m_kw) {
        (void)cpy_field_contents_free(out_field);
        return C_ORM_ERROR_COPYBOOK_SYNTAX;
      }
      rc = cpy_parse_uint_tok(vec->items[pos].text, &first_n);
      if (rc != C_ORM_OK) {
        (void)cpy_field_contents_free(out_field);
        return rc;
      }
      pos++;
      cpy_tok_is_word(vec, pos, "TO", &m_kw);
      if (m_kw) {
        size_t max_n = 0;
        pos++;
        cpy_tok_is_any_word(vec, pos, &m_kw);
        if (!m_kw) {
          (void)cpy_field_contents_free(out_field);
          return C_ORM_ERROR_COPYBOOK_SYNTAX;
        }
        rc = cpy_parse_uint_tok(vec->items[pos].text, &max_n);
        if (rc != C_ORM_OK || max_n == 0 || first_n > max_n) {
          (void)cpy_field_contents_free(out_field);
          return C_ORM_ERROR_COPYBOOK_SYNTAX;
        }
        out_field->min_occurs = first_n;
        out_field->max_occurs = max_n;
        pos++;
      } else {
        if (first_n == 0) {
          (void)cpy_field_contents_free(out_field);
          return C_ORM_ERROR_COPYBOOK_SYNTAX;
        }
        out_field->min_occurs = first_n;
        out_field->max_occurs = first_n;
      }
      out_field->has_occurs = 1;
      cpy_tok_is_word(vec, pos, "TIMES", &m_kw);
      if (m_kw) {
        pos++;
      }
      cpy_tok_is_word(vec, pos, "DEPENDING", &m_kw);
      if (m_kw) {
        pos++;
        cpy_tok_is_word(vec, pos, "ON", &m_kw);
        if (m_kw) {
          pos++;
        }
        cpy_tok_is_any_word(vec, pos, &m_kw);
        if (!m_kw) {
          (void)cpy_field_contents_free(out_field);
          return C_ORM_ERROR_COPYBOOK_SYNTAX;
        }
        rc = c_orm_strdup(vec->items[pos].text, &out_field->odo_field_name);
        if (rc != C_ORM_OK) {
          (void)cpy_field_contents_free(out_field);
          return rc;
        }
        pos++;
      }
      /* Skip optional ASCENDING/DESCENDING KEY and INDEXED BY subclauses */
      for (;;) {
        int m_asc = 0;
        int m_idx = 0;
        cpy_tok_is_word2(vec, pos, "ASCENDING", "DESCENDING", &m_asc);
        cpy_tok_is_word(vec, pos, "INDEXED", &m_idx);
        if (m_idx) {
          pos++;
          cpy_tok_is_word(vec, pos, "BY", &m_kw);
          if (m_kw) {
            pos++;
          }
          cpy_tok_is_any_word(vec, pos, &m_kw);
          if (m_kw) {
            pos++;
          }
        } else if (m_asc) {
          pos++;
          cpy_tok_is_word(vec, pos, "KEY", &m_kw);
          if (m_kw) {
            pos++;
          }
          cpy_tok_is_word(vec, pos, "IS", &m_kw);
          if (m_kw) {
            pos++;
          }
          cpy_tok_is_any_word(vec, pos, &m_kw);
          if (m_kw) {
            pos++;
          }
        } else {
          break;
        }
      }
      continue;
    }

    (void)cpy_field_contents_free(out_field);
    return C_ORM_ERROR_COPYBOOK_SYNTAX;
  }

  if (pos >= vec->count) {
    (void)cpy_field_contents_free(out_field);
    return C_ORM_ERROR_COPYBOOK_SYNTAX;
  }
  *out_has_explicit_usage = has_explicit_usage;
  *io_pos = pos + 1;
  return C_ORM_OK;
}

/**
 * @brief Resolves a `cpy_group_frame_t` on the nesting stack into a live
 * children array pointer and child count pointer.
 * @param rec Current `01` record.
 * @param stack Group frame stack.
 * @param frame_idx Index in `stack`.
 * @param out_arr Output pointer to children array pointer.
 * @param out_cnt Output pointer to children count.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t cpy_resolve_frame(c_orm_cobol_record_t *rec,
                                       const cpy_group_frame_t *stack,
                                       size_t frame_idx,
                                       c_orm_cobol_field_t ***out_arr,
                                       size_t **out_cnt) {
  size_t path[50];
  size_t depth = 0;
  size_t cur = frame_idx;
  c_orm_cobol_field_t **arr = &rec->fields;
  size_t *cnt = &rec->num_fields;
  c_orm_cobol_field_t *self_node = NULL;
  size_t k;

  while (stack[cur].parent_frame != (size_t)-1) {
    path[depth++] = stack[cur].field_idx;
    cur = stack[cur].parent_frame;
  }
  for (k = depth; k > 0; --k) {
    self_node = &(*arr)[path[k - 1]];
    arr = &self_node->children;
    cnt = &self_node->num_children;
  }
  *out_arr = arr;
  *out_cnt = cnt;
  return C_ORM_OK;
}

/**
 * @brief Recursively validates elementary vs group `PIC` rules, propagates
 * group `USAGE`, and detects IBM Db2 `DCLGEN` `49`-level `VARCHAR`/`VARGRAPHIC`
 * group pairs.
 * @param fields Array of sibling fields.
 * @param count Number of sibling fields.
 * @param inherited_usage Group `USAGE` inherited from enclosing group.
 * @return C_ORM_OK on success, or C_ORM_ERROR_COPYBOOK_SYNTAX on invalid item.
 */
static c_orm_error_t cpy_finalize_tree(c_orm_cobol_field_t *fields,
                                       size_t count,
                                       c_orm_cobol_usage_t inherited_usage) {
  size_t i;
  c_orm_error_t rc;
  for (i = 0; i < count; ++i) {
    c_orm_cobol_field_t *f = &fields[i];
    if (f->level == 66) {
      continue;
    }
    if (f->usage == C_ORM_COBOL_USAGE_DISPLAY &&
        f->pic_category != C_ORM_COBOL_PIC_NATIONAL &&
        inherited_usage != C_ORM_COBOL_USAGE_DISPLAY) {
      f->usage = inherited_usage;
    }
    if (f->num_children > 0) {
      if (f->pic_raw != NULL) {
        return C_ORM_ERROR_COPYBOOK_SYNTAX;
      }
      rc = cpy_finalize_tree(f->children, f->num_children, f->usage);
      if (rc != C_ORM_OK) {
        return rc;
      }
      /* Detect IBM Db2 DCLGEN 49-level VARCHAR / VARGRAPHIC pair */
      if (f->num_children == 2 && f->children[0].level == 49 &&
          f->children[1].level == 49 &&
          f->children[0].pic_category == C_ORM_COBOL_PIC_NUMERIC &&
          f->children[0].frac_digits == 0 && f->children[0].total_digits <= 4 &&
          (f->children[0].usage == C_ORM_COBOL_USAGE_COMP ||
           f->children[0].usage == C_ORM_COBOL_USAGE_COMP_5) &&
          (f->children[1].pic_category == C_ORM_COBOL_PIC_ALPHANUMERIC ||
           f->children[1].pic_category == C_ORM_COBOL_PIC_NATIONAL) &&
          f->children[1].display_length > 0) {
        f->is_dclgen_varchar = 1;
        f->is_dclgen_vargraphic =
            (f->children[1].pic_category == C_ORM_COBOL_PIC_NATIONAL) ? 1 : 0;
        f->dclgen_max_chars = f->children[1].display_length;
      }
    } else {
      if (f->pic_raw == NULL && f->usage != C_ORM_COBOL_USAGE_COMP_1 &&
          f->usage != C_ORM_COBOL_USAGE_COMP_2) {
        return C_ORM_ERROR_COPYBOOK_SYNTAX;
      }
    }
  }
  return C_ORM_OK;
}

/**
 * @brief Searches a field array recursively for a matching COBOL, C, or SQL
 * identifier.
 * @param fields Field array to search.
 * @param count Number of elements in `fields`.
 * @param name Identifier to find.
 * @param out_found Output matched field pointer, or NULL.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t
cpy_search_fields_rec(const c_orm_cobol_field_t *fields, size_t count,
                      const char *name, const c_orm_cobol_field_t **out_found) {
  size_t i;
  for (i = 0; i < count; ++i) {
    int eq1 = 0;
    int eq2 = 0;
    cpy_ident_eq(fields[i].cobol_name, name, &eq1);
    cpy_ident_eq(fields[i].c_name, name, &eq2);
    if (eq1 || eq2) {
      *out_found = &fields[i];
      return C_ORM_OK;
    }
    if (fields[i].num_children > 0) {
      cpy_search_fields_rec(fields[i].children, fields[i].num_children, name,
                            out_found);
      if (*out_found) {
        return C_ORM_OK;
      }
    }
  }
  return C_ORM_OK;
}

/**
 * @brief Searches a mutable field array recursively for a matching COBOL, C, or
 * SQL identifier without casting away `const`.
 * @param fields Mutable field array to search.
 * @param count Number of elements in `fields`.
 * @param name Identifier to find.
 * @param out_found Output matched mutable field pointer, or NULL.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t
cpy_search_fields_mut_rec(c_orm_cobol_field_t *fields, size_t count,
                          const char *name, c_orm_cobol_field_t **out_found) {
  size_t i;
  for (i = 0; i < count; ++i) {
    int eq1 = 0;
    int eq2 = 0;
    cpy_ident_eq(fields[i].cobol_name, name, &eq1);
    cpy_ident_eq(fields[i].c_name, name, &eq2);
    if (eq1 || eq2) {
      *out_found = &fields[i];
      return C_ORM_OK;
    }
    if (fields[i].num_children > 0) {
      cpy_search_fields_mut_rec(fields[i].children, fields[i].num_children,
                                name, out_found);
      if (*out_found) {
        return C_ORM_OK;
      }
    }
  }
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t
c_orm_copybook_find_field(const c_orm_cobol_record_t *rec, const char *name,
                          const c_orm_cobol_field_t **out_field) {
  const c_orm_cobol_field_t *found = NULL;
  if (!rec || !name || !out_field) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  *out_field = NULL;
  cpy_search_fields_rec(rec->fields, rec->num_fields, name, &found);
  if (!found && rec->standalone_fields) {
    cpy_search_fields_rec(rec->standalone_fields, rec->num_standalone_fields,
                          name, &found);
  }
  if (!found) {
    return C_ORM_ERROR_NOT_FOUND;
  }
  *out_field = found;
  return C_ORM_OK;
}

/**
 * @brief Computes wire byte length of a single occurrence of an elementary
 * field.
 * @param f Elementary COBOL field.
 * @param out_bytes Output single-element wire size in bytes.
 * @return C_ORM_OK on success, or C_ORM_ERROR_COPYBOOK_SYNTAX if invalid.
 */
static c_orm_error_t cpy_elementary_byte_size(const c_orm_cobol_field_t *f,
                                              size_t *out_bytes) {
  switch (f->usage) {
  case C_ORM_COBOL_USAGE_COMP_1:
    *out_bytes = 4;
    return C_ORM_OK;
  case C_ORM_COBOL_USAGE_COMP_2:
    *out_bytes = 8;
    return C_ORM_OK;
  case C_ORM_COBOL_USAGE_COMP:
  case C_ORM_COBOL_USAGE_COMP_5:
    if (f->pic_category != C_ORM_COBOL_PIC_NUMERIC || f->total_digits > 18) {
      return C_ORM_ERROR_COPYBOOK_SYNTAX;
    }
    c_orm_cobol_comp_byte_len(f->total_digits, out_bytes);
    return C_ORM_OK;
  case C_ORM_COBOL_USAGE_COMP_3:
    if (f->pic_category != C_ORM_COBOL_PIC_NUMERIC) {
      return C_ORM_ERROR_COPYBOOK_SYNTAX;
    }
    c_orm_decimal_comp3_byte_len(f->total_digits, out_bytes);
    return C_ORM_OK;
  case C_ORM_COBOL_USAGE_DISPLAY_1:
    *out_bytes = f->display_length * 2;
    return C_ORM_OK;
  default:
    if (f->pic_category == C_ORM_COBOL_PIC_NUMERIC) {
      *out_bytes = (size_t)f->total_digits +
                   ((f->is_signed && f->sign_separate) ? 1U : 0U);
      return C_ORM_OK;
    }
    if (f->pic_category == C_ORM_COBOL_PIC_NATIONAL) {
      *out_bytes = f->display_length * 2;
      return C_ORM_OK;
    }
    *out_bytes = f->display_length;
    return C_ORM_OK;
  }
}

/**
 * @brief Computes recursive byte offsets, single-element byte widths, total
 * `OCCURS` byte widths, `REDEFINES` sibling targets, and `66 RENAMES` spans.
 * @param rec Enclosing `01` record (used for `ODO` and `RENAMES` lookup).
 * @param siblings Array of sibling fields at current group level.
 * @param count Number of fields in `siblings`.
 * @param base_offset Starting 0-based record offset of this group.
 * @param out_group_size Output total byte span of this sibling list.
 * @return C_ORM_OK on success, or C_ORM_ERROR_COPYBOOK_SYNTAX on unresolved
 * `REDEFINES`, `RENAMES`, or `OCCURS DEPENDING ON` target.
 */
static c_orm_error_t cpy_compute_layout(const c_orm_cobol_record_t *rec,
                                        c_orm_cobol_field_t *siblings,
                                        size_t count, size_t base_offset,
                                        size_t *out_group_size) {
  size_t cur_rel = 0;
  size_t i;
  c_orm_error_t rc;

  for (i = 0; i < count; ++i) {
    c_orm_cobol_field_t *f = &siblings[i];
    size_t start_offset = base_offset + cur_rel;

    if (f->odo_field_name) {
      const c_orm_cobol_field_t *odo_target = NULL;
      if (c_orm_copybook_find_field(rec, f->odo_field_name, &odo_target) !=
              C_ORM_OK ||
          odo_target->pic_category != C_ORM_COBOL_PIC_NUMERIC) {
        return C_ORM_ERROR_COPYBOOK_SYNTAX;
      }
    }

    if (f->level == 66) {
      const c_orm_cobol_field_t *from_f = NULL;
      const c_orm_cobol_field_t *thru_f = NULL;
      if (c_orm_copybook_find_field(rec, f->renames_from, &from_f) !=
          C_ORM_OK) {
        return C_ORM_ERROR_COPYBOOK_SYNTAX;
      }
      f->byte_offset = from_f->byte_offset;
      if (f->renames_thru) {
        if (c_orm_copybook_find_field(rec, f->renames_thru, &thru_f) !=
                C_ORM_OK ||
            thru_f->byte_offset < from_f->byte_offset) {
          return C_ORM_ERROR_COPYBOOK_SYNTAX;
        }
        f->element_byte_size = (thru_f->byte_offset + thru_f->total_byte_size) -
                               from_f->byte_offset;
      } else {
        f->element_byte_size = from_f->total_byte_size;
      }
      f->total_byte_size = f->element_byte_size;
      continue;
    }

    if (f->redefines_name) {
      size_t j;
      c_orm_cobol_field_t *target = NULL;
      for (j = 0; j < i; ++j) {
        if (strcmp(siblings[j].cobol_name, f->redefines_name) == 0 &&
            !siblings[j].is_filler) {
          target = &siblings[j];
          break;
        }
      }
      if (!target) {
        return C_ORM_ERROR_COPYBOOK_SYNTAX;
      }
      f->redefines_target = target;
      start_offset = target->byte_offset;
    } else if (f->is_synchronized && f->num_children == 0) {
      size_t elem_sz = 0;
      size_t align = 1;
      rc = cpy_elementary_byte_size(f, &elem_sz);
      if (rc != C_ORM_OK) {
        return rc;
      }
      if (f->usage == C_ORM_COBOL_USAGE_COMP ||
          f->usage == C_ORM_COBOL_USAGE_COMP_5 ||
          f->usage == C_ORM_COBOL_USAGE_COMP_1 ||
          f->usage == C_ORM_COBOL_USAGE_COMP_2) {
        align = (elem_sz >= 4) ? 4 : 2;
      }
      if (align > 1 && (start_offset % align) != 0) {
        size_t pad = align - (start_offset % align);
        cur_rel += pad;
        start_offset += pad;
      }
    }

    f->byte_offset = start_offset;
    if (f->num_children > 0) {
      size_t grp_sz = 0;
      rc = cpy_compute_layout(rec, f->children, f->num_children, start_offset,
                              &grp_sz);
      if (rc != C_ORM_OK) {
        return rc;
      }
      f->element_byte_size = grp_sz;
    } else {
      rc = cpy_elementary_byte_size(f, &f->element_byte_size);
      if (rc != C_ORM_OK) {
        return rc;
      }
    }

    f->total_byte_size =
        f->element_byte_size * (f->has_occurs ? f->max_occurs : 1U);
    if (f->redefines_target) {
      size_t redef_end = (f->byte_offset - base_offset) + f->total_byte_size;
      if (redef_end > cur_rel) {
        cur_rel = redef_end;
      }
    } else {
      cur_rel += f->total_byte_size;
    }
  }

  *out_group_size = cur_rel;
  return C_ORM_OK;
}

/**
 * @brief Applies column constraints (`NOT NULL`, `PRIMARY KEY`, `FOR BIT DATA`)
 * from an embedded `EXEC SQL DECLARE` table onto matching fields in `rec`.
 * @param rec Target COBOL record AST.
 * @param sql_tbl Parsed SQL `DECLARE ... TABLE` metadata.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t cpy_bind_sql_metadata(c_orm_cobol_record_t *rec,
                                           const struct sql_table_t *sql_tbl) {
  size_t i;
  for (i = 0; i < sql_tbl->n_columns; ++i) {
    const struct sql_column_t *sc = &sql_tbl->columns[i];
    c_orm_cobol_field_t *target = NULL;
    size_t k;
    int is_nn = 0;
    int is_pk = 0;

    cpy_search_fields_mut_rec(rec->fields, rec->num_fields, sc->name, &target);
    if (!target) {
      /* Also match prefixed DCLGEN host fields such as ACCT-ID <-> ID */
      size_t fi;
      for (fi = 0; fi < rec->num_fields; ++fi) {
        size_t slen = strlen(sc->name);
        size_t flen = strlen(rec->fields[fi].sql_name);
        if (flen > slen && rec->fields[fi].sql_name[flen - slen - 1] == '_') {
          int tail_eq = 0;
          cpy_ident_eq(rec->fields[fi].sql_name + (flen - slen), sc->name,
                       &tail_eq);
          if (tail_eq) {
            target = &rec->fields[fi];
            break;
          }
        }
      }
    }
    if (!target) {
      continue;
    }
    for (k = 0; k < sc->n_constraints; ++k) {
      if (sc->constraints[k].type == SQL_CONSTRAINT_NOT_NULL) {
        is_nn = 1;
      } else if (sc->constraints[k].type == SQL_CONSTRAINT_PRIMARY_KEY) {
        is_pk = 1;
        is_nn = 1;
      }
    }
    /* Check table-level PRIMARY KEY (col) */
    for (k = 0; k < sql_tbl->n_table_constraints; ++k) {
      if (sql_tbl->table_constraints[k].type == SQL_CONSTRAINT_PRIMARY_KEY) {
        size_t ci;
        for (ci = 0; ci < sql_tbl->table_constraints[k].n_columns; ++ci) {
          int eq = 0;
          cpy_ident_eq(sql_tbl->table_constraints[k].columns[ci], sc->name,
                       &eq);
          if (eq) {
            is_pk = 1;
            is_nn = 1;
          }
        }
      }
    }
    target->has_sql_decl = 1;
    target->sql_not_null = is_nn;
    target->sql_is_pk = is_pk;
    target->sql_for_bit_data = sc->is_for_bit_data;
  }
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_copybook_parse_string(
    const char *cpy_source, c_orm_cobol_format_t format,
    const c_orm_cobol_replace_pair_t *replacements, size_t num_replacements,
    c_orm_cobol_record_t **out_record) {
  char *norm = NULL;
  char *cobol_only = NULL;
  char *sql_schema = NULL;
  struct sql_table_t *sql_tbl = NULL;
  cpy_tok_vec_t tokens;
  c_orm_cobol_record_t *primary = NULL;
  c_orm_cobol_record_t *cur_rec = NULL;
  cpy_group_frame_t stack[52];
  size_t stack_len = 0;
  c_orm_cobol_field_t *last_field_for_88 = NULL;
  size_t pos = 0;
  size_t filler_seq = 0;
  c_orm_error_t rc;

  if (!cpy_source || !out_record) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  *out_record = NULL;
  memset(&tokens, 0, sizeof(tokens));

  rc = cpy_normalize_source(cpy_source, format, replacements, num_replacements,
                            &norm);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = cpy_extract_exec_sql(norm, &cobol_only, &sql_schema, &sql_tbl);
  (void)c_orm_system_free(norm);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = cpy_lex_source(cobol_only, &tokens);
  (void)c_orm_system_free(cobol_only);
  if (rc != C_ORM_OK) {
    goto cleanup;
  }
  if (tokens.count == 0) {
    rc = C_ORM_ERROR_COPYBOOK_SYNTAX;
    goto cleanup;
  }

  while (pos < tokens.count) {
    size_t lvl_u = 0;
    int level;
    c_orm_cobol_field_t parsed_f;
    int explicit_u = 0;

    if (tokens.items[pos].kind == CPY_TOK_DOT) {
      pos++;
      continue;
    }
    if (tokens.items[pos].kind != CPY_TOK_WORD ||
        cpy_parse_uint_tok(tokens.items[pos].text, &lvl_u) != C_ORM_OK) {
      rc = C_ORM_ERROR_COPYBOOK_SYNTAX;
      goto cleanup;
    }
    level = (int)lvl_u;

    if (level == 88) {
      pos++;
      rc = cpy_parse_condition_88(&tokens, &pos, last_field_for_88);
      if (rc != C_ORM_OK) {
        goto cleanup;
      }
      continue;
    }

    if (!((level >= 1 && level <= 49) || level == 66 || level == 77)) {
      rc = C_ORM_ERROR_COPYBOOK_SYNTAX;
      goto cleanup;
    }

    rc = cpy_parse_field_entry(&tokens, &pos, level, &filler_seq, &parsed_f,
                               &explicit_u);
    (void)explicit_u;
    if (rc != C_ORM_OK) {
      goto cleanup;
    }

    if (level == 77) {
      void *new_sf = NULL;
      if (!primary) {
        rc = c_orm_system_calloc(1, sizeof(c_orm_cobol_record_t),
                                 (void **)&primary);
        if (rc != C_ORM_OK) {
          (void)cpy_field_contents_free(&parsed_f);
          goto cleanup;
        }
      }
      rc = c_orm_system_realloc(primary->standalone_fields,
                                (primary->num_standalone_fields + 1) *
                                    sizeof(c_orm_cobol_field_t),
                                &new_sf);
      if (rc != C_ORM_OK) {
        (void)cpy_field_contents_free(&parsed_f);
        goto cleanup;
      }
      primary->standalone_fields = (c_orm_cobol_field_t *)new_sf;
      primary->standalone_fields[primary->num_standalone_fields] = parsed_f;
      last_field_for_88 =
          &primary->standalone_fields[primary->num_standalone_fields];
      primary->num_standalone_fields++;
      continue;
    }

    if (level == 1) {
      if (!primary || !primary->record_name) {
        if (!primary) {
          rc = c_orm_system_calloc(1, sizeof(c_orm_cobol_record_t),
                                   (void **)&primary);
          if (rc != C_ORM_OK) {
            (void)cpy_field_contents_free(&parsed_f);
            goto cleanup;
          }
        }
        cur_rec = primary;
      } else {
        void *new_sec = NULL;
        rc = c_orm_system_realloc(primary->secondary_records,
                                  (primary->num_secondary_records + 1) *
                                      sizeof(c_orm_cobol_record_t),
                                  &new_sec);
        if (rc != C_ORM_OK) {
          (void)cpy_field_contents_free(&parsed_f);
          goto cleanup;
        }
        primary->secondary_records = (c_orm_cobol_record_t *)new_sec;
        cur_rec = &primary->secondary_records[primary->num_secondary_records];
        memset(cur_rec, 0, sizeof(*cur_rec));
        primary->num_secondary_records++;
      }

      rc = c_orm_strdup(parsed_f.cobol_name, &cur_rec->record_name);
      if (rc == C_ORM_OK) {
        rc = cpy_normalize_struct_name(parsed_f.cobol_name,
                                       &cur_rec->c_struct_name);
      }
      if (rc == C_ORM_OK) {
        if (cur_rec == primary && sql_tbl) {
          rc = cpy_normalize_sql_name(sql_tbl->name, &cur_rec->sql_table_name);
        } else {
          rc = cpy_normalize_sql_name(parsed_f.cobol_name,
                                      &cur_rec->sql_table_name);
        }
      }
      if (rc != C_ORM_OK) {
        (void)cpy_field_contents_free(&parsed_f);
        goto cleanup;
      }

      /* If the 01 item itself is elementary (has PIC), store it as single
       * child; otherwise push 01 group root on stack */
      stack_len = 0;
      stack[0].level = 1;
      stack[0].field_idx = (size_t)-1;
      stack[0].parent_frame = (size_t)-1;
      stack_len = 1;

      if (parsed_f.pic_raw != NULL ||
          parsed_f.usage == C_ORM_COBOL_USAGE_COMP_1 ||
          parsed_f.usage == C_ORM_COBOL_USAGE_COMP_2) {
        rc = c_orm_system_malloc(sizeof(c_orm_cobol_field_t),
                                 (void **)&cur_rec->fields);
        if (rc != C_ORM_OK) {
          (void)cpy_field_contents_free(&parsed_f);
          goto cleanup;
        }
        cur_rec->fields[0] = parsed_f;
        cur_rec->num_fields = 1;
        last_field_for_88 = &cur_rec->fields[0];
      } else {
        (void)cpy_field_contents_free(&parsed_f);
        last_field_for_88 = NULL;
      }
      continue;
    }

    /* Levels 02..49 and 66 require an open 01 record */
    if (!cur_rec) {
      (void)cpy_field_contents_free(&parsed_f);
      rc = C_ORM_ERROR_COPYBOOK_SYNTAX;
      goto cleanup;
    }

    if (level == 66) {
      stack_len = 1;
    } else {
      while (stack_len > 1 && stack[stack_len - 1].level >= level) {
        stack_len--;
      }
    }

    {
      c_orm_cobol_field_t **arr_ptr = NULL;
      size_t *cnt_ptr = NULL;
      void *new_children = NULL;
      size_t inserted_idx;

      cpy_resolve_frame(cur_rec, stack, stack_len - 1, &arr_ptr, &cnt_ptr);
      rc = c_orm_system_realloc(*arr_ptr,
                                (*cnt_ptr + 1) * sizeof(c_orm_cobol_field_t),
                                &new_children);
      if (rc != C_ORM_OK) {
        (void)cpy_field_contents_free(&parsed_f);
        goto cleanup;
      }
      *arr_ptr = (c_orm_cobol_field_t *)new_children;
      inserted_idx = *cnt_ptr;
      (*arr_ptr)[inserted_idx] = parsed_f;
      (*cnt_ptr)++;
      last_field_for_88 = &(*arr_ptr)[inserted_idx];

      if (level != 66 && parsed_f.usage != C_ORM_COBOL_USAGE_COMP_1 &&
          parsed_f.usage != C_ORM_COBOL_USAGE_COMP_2) {
        stack[stack_len].level = level;
        stack[stack_len].field_idx = inserted_idx;
        stack[stack_len].parent_frame = stack_len - 1;
        stack_len++;
      }
    }
  }

  if (!primary) {
    rc = C_ORM_ERROR_COPYBOOK_SYNTAX;
    goto cleanup;
  }

  /* Synthesize record names if copybook had only 77-level items */
  if (!primary->record_name) {
    rc = c_orm_strdup("STANDALONE-REC", &primary->record_name);
    if (rc == C_ORM_OK) {
      rc = c_orm_strdup("Standalone_rec", &primary->c_struct_name);
    }
    if (rc == C_ORM_OK) {
      rc = c_orm_strdup("STANDALONE_REC", &primary->sql_table_name);
    }
    if (rc != C_ORM_OK) {
      goto cleanup;
    }
  }

  if (primary->num_fields == 0 && primary->num_standalone_fields == 0) {
    rc = C_ORM_ERROR_COPYBOOK_SYNTAX;
    goto cleanup;
  }

  rc = cpy_finalize_tree(primary->fields, primary->num_fields,
                         C_ORM_COBOL_USAGE_DISPLAY);
  if (rc != C_ORM_OK) {
    goto cleanup;
  }
  if (primary->num_standalone_fields > 0) {
    size_t dummy_sz = 0;
    rc = cpy_finalize_tree(primary->standalone_fields,
                           primary->num_standalone_fields,
                           C_ORM_COBOL_USAGE_DISPLAY);
    if (rc == C_ORM_OK) {
      rc = cpy_compute_layout(primary, primary->standalone_fields,
                              primary->num_standalone_fields, 0, &dummy_sz);
    }
    if (rc != C_ORM_OK) {
      goto cleanup;
    }
  }

  rc = cpy_compute_layout(primary, primary->fields, primary->num_fields, 0,
                          &primary->total_byte_size);
  if (rc != C_ORM_OK) {
    goto cleanup;
  }

  {
    size_t si;
    for (si = 0; si < primary->num_secondary_records; ++si) {
      c_orm_cobol_record_t *srec = &primary->secondary_records[si];
      rc = cpy_finalize_tree(srec->fields, srec->num_fields,
                             C_ORM_COBOL_USAGE_DISPLAY);
      if (rc == C_ORM_OK) {
        rc = cpy_compute_layout(srec, srec->fields, srec->num_fields, 0,
                                &srec->total_byte_size);
      }
      if (rc != C_ORM_OK) {
        goto cleanup;
      }
    }
  }

  if (sql_tbl) {
    primary->has_exec_sql_declare = 1;
    primary->sql_schema_name = sql_schema;
    sql_schema = NULL;
    cpy_bind_sql_metadata(primary, sql_tbl);
  }

  (void)cpy_tok_vec_free(&tokens);
  if (sql_tbl) {
    (void)sql_table_free(sql_tbl);
    (void)c_orm_system_free(sql_tbl);
  }
  *out_record = primary;
  return C_ORM_OK;

cleanup:
  (void)cpy_tok_vec_free(&tokens);
  (void)c_orm_system_free(sql_schema);
  if (sql_tbl) {
    (void)sql_table_free(sql_tbl);
    (void)c_orm_system_free(sql_tbl);
  }
  (void)c_orm_copybook_record_free(primary);
  return rc;
}

C_ORM_EXPORT c_orm_error_t c_orm_copybook_parse_file(
    const char *file_path, c_orm_cobol_format_t format,
    const c_orm_cobol_replace_pair_t *replacements, size_t num_replacements,
    c_orm_cobol_record_t **out_record) {
  FILE *fp = NULL;
  c_orm_string_builder_t *sb = NULL;
  const char *src_str = NULL;
  char chunk[512];
  size_t nread;
  c_orm_error_t rc;

  if (!file_path || !out_record) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  *out_record = NULL;

  rc = C_ORM_FOPEN(&fp, file_path, "rb");
  if (rc != C_ORM_OK) {
    return C_ORM_ERROR_IO;
  }
  rc = c_orm_string_builder_init(&sb);
  if (rc != C_ORM_OK) {
    fclose(fp);
    return rc;
  }
  while ((nread = fread(chunk, 1, sizeof(chunk), fp)) > 0) {
    rc = cpy_sb_append_n(sb, chunk, nread);
    if (rc != C_ORM_OK) {
      fclose(fp);
      (void)c_orm_string_builder_free(sb);
      return rc;
    }
  }
  fclose(fp);
  c_orm_string_builder_get(sb, &src_str);
  rc = c_orm_copybook_parse_string(src_str, format, replacements,
                                   num_replacements, out_record);
  (void)c_orm_string_builder_free(sb);
  return rc;
}
