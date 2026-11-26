/**
 * @file c_orm_sql_to_c.c
 * @brief Emits C structures and array containers from SQL DDL AST.
 */

/* clang-format off */
#include "c_orm_sql_to_c.h"
#include <errno.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

/**
 * @brief Checks if `type` is a string-backed SQL column type.
 * @param type SQL data type enum value.
 * @return 1 if string type, 0 otherwise.
 */
static int s2c_calc_is_string_type(enum SqlDataType type) {
  unsigned long mask = (1UL << (unsigned int)SQL_TYPE_VARCHAR) |
                       (1UL << (unsigned int)SQL_TYPE_TEXT) |
                       (1UL << (unsigned int)SQL_TYPE_CHAR) |
                       (1UL << (unsigned int)SQL_TYPE_GRAPHIC) |
                       (1UL << (unsigned int)SQL_TYPE_VARGRAPHIC) |
                       (1UL << (unsigned int)SQL_TYPE_CLOB) |
                       (1UL << (unsigned int)SQL_TYPE_DBCLOB) |
                       (1UL << (unsigned int)SQL_TYPE_DATE) |
                       (1UL << (unsigned int)SQL_TYPE_TIME) |
                       (1UL << (unsigned int)SQL_TYPE_TIMESTAMP);
  if ((unsigned int)type >= 32U) {
    return 0;
  }
  return ((1UL << (unsigned int)type) & mask) != 0UL;
}

/**
 * @brief Checks if `type` is a binary/BLOB SQL column type.
 * @param type SQL data type enum value.
 * @return 1 if binary/BLOB type, 0 otherwise.
 */
static int s2c_calc_is_blob_type(enum SqlDataType type) {
  unsigned long mask = (1UL << (unsigned int)SQL_TYPE_BLOB) |
                       (1UL << (unsigned int)SQL_TYPE_BINARY) |
                       (1UL << (unsigned int)SQL_TYPE_VARBINARY);
  if ((unsigned int)type >= 32U) {
    return 0;
  }
  return ((1UL << (unsigned int)type) & mask) != 0UL;
}

#define S2C_IS_STRING_TYPE(t) s2c_calc_is_string_type(t)
#define S2C_IS_BLOB_TYPE(t) s2c_calc_is_blob_type(t)

/**
 * @brief Computes whether an SQL column is part of an inline or table-level
 * PRIMARY KEY constraint.
 * @param table Owning SQL table AST node.
 * @param col Column definition pointer.
 * @return 1 if primary key, 0 otherwise.
 */
static int s2c_calc_col_pk(const struct sql_table_t *table,
                           const struct sql_column_t *col) {
  size_t i;
  size_t k;
  for (i = 0; i < col->n_constraints; ++i) {
    if (col->constraints[i].type == SQL_CONSTRAINT_PRIMARY_KEY) {
      return 1;
    }
  }
  if (col->name) {
    for (i = 0; i < table->n_table_constraints; ++i) {
      const struct sql_constraint_t *tc = &table->table_constraints[i];
      if (tc->type == SQL_CONSTRAINT_PRIMARY_KEY) {
        if (tc->column_name && strcmp(tc->column_name, col->name) == 0) {
          return 1;
        }
        for (k = 0; k < tc->n_columns; ++k) {
          if (tc->columns[k] && strcmp(tc->columns[k], col->name) == 0) {
            return 1;
          }
        }
      }
    }
  }
  return 0;
}

/**
 * @brief Computes whether an SQL column is nullable (neither NOT NULL nor
 * PRIMARY KEY).
 * @param table Owning SQL table AST node.
 * @param col Column definition pointer.
 * @return 1 if nullable, 0 otherwise.
 */
static int s2c_calc_col_nullable(const struct sql_table_t *table,
                                 const struct sql_column_t *col) {
  size_t i;
  if (s2c_calc_col_pk(table, col)) {
    return 0;
  }
  for (i = 0; i < col->n_constraints; ++i) {
    if (col->constraints[i].type == SQL_CONSTRAINT_NOT_NULL) {
      return 0;
    }
  }
  return 1;
}

/**
 * @brief Resolves inline or single-column table-level FOREIGN KEY target
 * metadata for a column.
 * @param table Owning SQL table AST node.
 * @param col Column definition pointer.
 * @param out_ref_table Output target table name (or NULL if not an FK).
 * @param out_ref_col Output target column name (or NULL if unspecified).
 * @return C_ORM_OK on completion.
 */
static c_orm_error_t s2c_resolve_col_fk(const struct sql_table_t *table,
                                        const struct sql_column_t *col,
                                        const char **out_ref_table,
                                        const char **out_ref_col) {
  size_t i;
  *out_ref_table = NULL;
  *out_ref_col = NULL;
  for (i = 0; i < col->n_constraints; ++i) {
    if (col->constraints[i].type == SQL_CONSTRAINT_FOREIGN_KEY &&
        col->constraints[i].reference_table) {
      *out_ref_table = col->constraints[i].reference_table;
      *out_ref_col = col->constraints[i].reference_column;
      return C_ORM_OK;
    }
  }
  if (col->name) {
    for (i = 0; i < table->n_table_constraints; ++i) {
      const struct sql_constraint_t *tc = &table->table_constraints[i];
      if (tc->type == SQL_CONSTRAINT_FOREIGN_KEY && tc->reference_table) {
        const char *local_col = tc->column_name;
        if (!local_col && tc->n_columns > 0 && tc->columns) {
          local_col = tc->columns[0];
        }
        if (local_col && strcmp(local_col, col->name) == 0) {
          *out_ref_table = tc->reference_table;
          *out_ref_col = tc->reference_column;
          return C_ORM_OK;
        }
      }
    }
  }
  return C_ORM_OK;
}

/** @brief Static C type string lookup table indexed by s2c_calc_c_type_idx. */
static const char *const k_s2c_c_types[] = {
    NULL,     "int32_t",         "int64_t", "char *",      "float",
    "double", "c_orm_decimal_t", "bool",    "c_orm_blob_t"};

/**
 * @brief Computes the lookup index into `k_s2c_c_types` for an SQL data type.
 * @param type SQL data type enum value.
 * @return Index into `k_s2c_c_types` (0 for unmapped/unknown).
 */
static int s2c_calc_c_type_idx(enum SqlDataType type) {
  switch (type) {
  case SQL_TYPE_SMALLINT:
  case SQL_TYPE_INT:
    return 1;
  case SQL_TYPE_BIGINT:
    return 2;
  case SQL_TYPE_VARCHAR:
  case SQL_TYPE_TEXT:
  case SQL_TYPE_CHAR:
  case SQL_TYPE_GRAPHIC:
  case SQL_TYPE_VARGRAPHIC:
  case SQL_TYPE_CLOB:
  case SQL_TYPE_DBCLOB:
  case SQL_TYPE_DATE:
  case SQL_TYPE_TIME:
  case SQL_TYPE_TIMESTAMP:
    return 3;
  case SQL_TYPE_FLOAT:
    return 4;
  case SQL_TYPE_DOUBLE:
    return 5;
  case SQL_TYPE_DECIMAL:
  case SQL_TYPE_NUMERIC:
  case SQL_TYPE_DECFLOAT:
    return 6;
  case SQL_TYPE_BOOLEAN:
    return 7;
  case SQL_TYPE_BLOB:
  case SQL_TYPE_BINARY:
  case SQL_TYPE_VARBINARY:
    return 8;
  default:
    return 0;
  }
}

c_orm_error_t sql_type_to_c_type(enum SqlDataType type, char **_out_val) {
  if (!_out_val) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  *_out_val = (char *)(size_t)k_s2c_c_types[s2c_calc_c_type_idx(type)];
  return C_ORM_OK;
}

c_orm_error_t sql_type_is_string(enum SqlDataType type, int *out_is_string) {
  if (!out_is_string) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  *out_is_string = S2C_IS_STRING_TYPE(type);
  return C_ORM_OK;
}

/**
 * @brief Emits SQL query macros and ORM table metadata.
 * @param fp Target file pointer.
 * @param table SQL table AST node.
 * @param struct_name Generated struct name string.
 * @return C_ORM_OK on completion.
 */
static c_orm_error_t s2c_emit_queries_and_meta(FILE *fp,
                                               const struct sql_table_t *table,
                                               const char *struct_name);

c_orm_error_t sql_to_c_header_emit_body(FILE *fp,
                                        const struct sql_table_t *table) {
  char *table_name_upper = NULL;
  char *struct_name = NULL;
  size_t name_len;
  size_t i;
  c_orm_error_t rc;

  if (!fp || !table) {
    return C_ORM_ERROR_MEMORY;
  }
  if (!table->name) {
    return C_ORM_ERROR_VALIDATION;
  }

  name_len = strlen(table->name);
  rc = c_orm_system_malloc(name_len + 1, (void **)&table_name_upper);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = c_orm_system_malloc(name_len + 1, (void **)&struct_name);
  if (rc != C_ORM_OK) {
    c_orm_system_free(table_name_upper);
    return rc;
  }

  for (i = 0; i < name_len; ++i) {
    table_name_upper[i] = (char)toupper((unsigned char)table->name[i]);
    struct_name[i] = (i == 0) ? (char)toupper((unsigned char)table->name[0])
                              : table->name[i];
  }
  table_name_upper[name_len] = '\0';
  struct_name[name_len] = '\0';

  fprintf(fp, "#ifndef C_ORM_MODEL_%s_H\n", table_name_upper);
  fprintf(fp, "#define C_ORM_MODEL_%s_H\n\n", table_name_upper);

  /* Emit row struct */
  fprintf(fp, "/**\n");
  fprintf(fp, " * @brief Represents a single row of the %s table.\n",
          table->name);
  fprintf(fp, " */\n");
  fprintf(fp, "struct %s {\n", struct_name);

  for (i = 0; i < table->n_columns; ++i) {
    const struct sql_column_t *col = &table->columns[i];
    char *c_type =
        (char *)(size_t)k_s2c_c_types[s2c_calc_c_type_idx(col->type)];
    int nullable = s2c_calc_col_nullable(table, col);
    int is_str = S2C_IS_STRING_TYPE(col->type);
    if (!c_type) {
      c_type = (char *)(size_t) "void *";
    }

    if (nullable && !is_str) {
      /* Use pointer for nullable primitive types */
      fprintf(fp, "  /** @brief Column %s of table %s (nullable). */\n",
              col->name, table->name);
      fprintf(fp, "  %s *%s; /**< Nullable */\n", c_type, col->name);
    } else {
      fprintf(fp, "  /** @brief Column %s of table %s. */\n", col->name,
              table->name);
      fprintf(fp, "  %s %s;\n", c_type, col->name);
    }
  }
  fprintf(fp, "};\n\n");

  /* Emit array struct */
  fprintf(fp, "/**\n");
  fprintf(fp, " * @brief A collection of %s rows.\n", struct_name);
  fprintf(fp, " */\n");
  fprintf(fp, "struct %s_Array {\n", struct_name);
  fprintf(fp, "  /** @brief Pointer to allocated %s row array. */\n",
          struct_name);
  fprintf(fp, "  struct %s *data;\n", struct_name);
  fprintf(fp, "  /** @brief Number of populated elements. */\n");
  fprintf(fp, "  size_t length;\n");
  fprintf(fp, "  /** @brief Allocated element capacity. */\n");
  fprintf(fp, "  size_t capacity;\n");
  fprintf(fp, "};\n\n");

  /* Function declarations */
  fprintf(fp,
          "/**\n * @brief Initialize a %s_Array.\n * @param arr Array to "
          "init.\n * @param initial_capacity Initial capacity.\n * @return "
          "C_ORM_OK on success.\n */\n",
          struct_name);
  fprintf(fp,
          "c_orm_error_t %s_Array_init(struct %s_Array *arr, size_t "
          "initial_capacity);\n\n",
          struct_name, struct_name);

  fprintf(fp,
          "/**\n * @brief Free resources inside a single %s struct.\n * @param "
          "item Pointer to struct.\n * @return C_ORM_OK on success.\n */\n",
          struct_name);
  fprintf(fp, "c_orm_error_t %s_free(struct %s *item);\n\n", struct_name,
          struct_name);

  fprintf(fp,
          "/**\n * @brief Free resources of a %s_Array.\n * @param arr Pointer "
          "to array.\n * @return C_ORM_OK on success.\n */\n",
          struct_name);
  fprintf(fp, "c_orm_error_t %s_Array_free(struct %s_Array *arr);\n\n",
          struct_name, struct_name);

  fprintf(fp,
          "/**\n * @brief Deep copy a %s row.\n * @param src Source struct.\n "
          "* @param dest Destination struct.\n * @return C_ORM_OK on "
          "success.\n */\n",
          struct_name);
  fprintf(
      fp,
      "c_orm_error_t %s_deepcopy(const struct %s *src, struct %s *dest);\n\n",
      struct_name, struct_name, struct_name);

  fprintf(fp,
          "/**\n * @brief Deep copy a %s_Array.\n * @param src Source array.\n "
          "* @param dest Destination array.\n * @return C_ORM_OK on "
          "success.\n */\n",
          struct_name);
  fprintf(fp,
          "c_orm_error_t %s_Array_deepcopy(const struct %s_Array *src, struct "
          "%s_Array "
          "*dest);\n\n",
          struct_name, struct_name, struct_name);

  fprintf(fp, "extern const c_orm_table_meta_t %s_meta;\n\n", struct_name);
  fprintf(fp, "#endif /* C_ORM_MODEL_%s_H */\n\n", table_name_upper);

  c_orm_system_free(struct_name);
  return c_orm_system_free(table_name_upper);
}

c_orm_error_t sql_to_c_header_emit(FILE *fp, const struct sql_table_t *table) {
  c_orm_error_t rc;
  if (!fp || !table) {
    return C_ORM_ERROR_MEMORY;
  }
  if (!table->name) {
    return C_ORM_ERROR_VALIDATION;
  }

  fprintf(fp, "/**\n");
  fprintf(fp, " * @file %s.h\n", table->name);
  fprintf(fp, " * @brief Auto-generated C structs for SQL table %s.\n",
          table->name);
  fprintf(fp, " */\n\n");

  fprintf(fp, "/* clang-format "
              "off */\n");
  fprintf(fp, "#include \"c_orm_meta.h\"\n");
  fprintf(fp, "#include \"c_orm_decimal.h\"\n");
  fprintf(fp, "#include <stddef.h>\n");
  fprintf(fp, "/* clang-format "
              "on */\n\n");

  fprintf(fp, "#ifdef __cplusplus\n");
  fprintf(fp, "extern \"C\" {\n");
  fprintf(fp, "#endif /* __cplusplus */\n\n");

  rc = sql_to_c_header_emit_body(fp, table);
  if (rc != C_ORM_OK) {
    return rc;
  }

  fprintf(fp, "#ifdef __cplusplus\n");
  fprintf(fp, "}\n");
  fprintf(fp, "#endif /* __cplusplus */\n");

  return C_ORM_OK;
}

c_orm_error_t sql_to_c_source_emit(FILE *fp, const struct sql_table_t *table,
                                   const char *header_name) {
  char *struct_name = NULL;
  size_t name_len;
  size_t i;
  c_orm_error_t rc;
  if (!fp || !table) {
    return C_ORM_ERROR_MEMORY;
  }
  if (!table->name) {
    return C_ORM_ERROR_VALIDATION;
  }
  if (header_name && header_name[0] != '\0') {
    fprintf(fp, "/* Source implementation for %s (%s) */\n", table->name,
            header_name);
  }

  name_len = strlen(table->name);
  rc = c_orm_system_malloc(name_len + 1, (void **)&struct_name);
  if (rc != C_ORM_OK) {
    return rc;
  }

  for (i = 0; i < name_len; ++i) {
    struct_name[i] = (i == 0) ? (char)toupper((unsigned char)table->name[0])
                              : table->name[i];
  }
  struct_name[name_len] = '\0';

  fprintf(fp, "\n\n");

  /* Array init */
  fprintf(fp,
          "/**\n * @brief Initialize a %s_Array.\n * @param arr Array to "
          "init.\n * @param initial_capacity Initial capacity.\n * @return "
          "C_ORM_OK on success or C_ORM_ERROR_MEMORY.\n */\n",
          struct_name);
  fprintf(fp,
          "c_orm_error_t %s_Array_init(struct %s_Array *arr, size_t "
          "initial_capacity) {\n",
          struct_name, struct_name);
  fprintf(fp, "  c_orm_error_t rc;\n");
  fprintf(fp, "  if (!arr) return C_ORM_ERROR_MEMORY;\n");
  fprintf(fp, "  arr->length = 0;\n");
  fprintf(fp, "  arr->capacity = initial_capacity;\n");
  fprintf(fp, "  if (initial_capacity > 0) {\n");
  fprintf(fp,
          "    rc = c_orm_system_calloc(initial_capacity, sizeof(struct %s), "
          "(void **)&arr->data);\n    if (rc != C_ORM_OK) {\n      return "
          "rc;\n    }\n",
          struct_name);
  fprintf(fp, "  } else {\n");
  fprintf(fp, "    arr->data = NULL;\n");
  fprintf(fp, "  }\n");
  fprintf(fp, "  return C_ORM_OK;\n");
  fprintf(fp, "}\n\n");

  /* Free */
  fprintf(fp,
          "/**\n * @brief Free resources inside a %s row.\n * @param item "
          "Item to free.\n * @return C_ORM_OK on success.\n */\n",
          struct_name);
  fprintf(fp, "c_orm_error_t %s_free(struct %s *item) {\n", struct_name,
          struct_name);
  fprintf(fp, "  if (!item) return C_ORM_OK;\n");
  for (i = 0; i < table->n_columns; ++i) {
    const struct sql_column_t *col = &table->columns[i];
    int is_str = S2C_IS_STRING_TYPE(col->type);
    int is_blob = S2C_IS_BLOB_TYPE(col->type);
    int nullable = s2c_calc_col_nullable(table, col);

    if (is_str) {
      fprintf(fp,
              "  if (item->%s) {\n    c_orm_system_free(item->%s);\n    "
              "item->%s = NULL;\n  }\n",
              col->name, col->name, col->name);
    } else if (nullable) {
      fprintf(fp,
              "  if (item->%s) {\n    c_orm_system_free(item->%s);\n    "
              "item->%s = NULL;\n  }\n",
              col->name, col->name, col->name);
    } else if (is_blob) {
      fprintf(fp,
              "  if (item->%s.data) {\n    c_orm_system_free(item->%s.data);\n"
              "    item->%s.data = NULL;\n    item->%s.size = 0;\n  }\n",
              col->name, col->name, col->name, col->name);
    }
  }
  fprintf(fp, "  return C_ORM_OK;\n");
  fprintf(fp, "}\n\n");

  /* Array Free */
  fprintf(fp,
          "/**\n * @brief Free a %s_Array and all its elements.\n * @param arr "
          "Array to free.\n * @return C_ORM_OK on success.\n */\n",
          struct_name);
  fprintf(fp, "c_orm_error_t %s_Array_free(struct %s_Array *arr) {\n",
          struct_name, struct_name);
  fprintf(fp, "  size_t i;\n");
  fprintf(fp, "  if (!arr) return C_ORM_OK;\n");
  fprintf(fp, "  if (arr->data) {\n");
  fprintf(fp, "    for (i = 0; i < arr->length; ++i) {\n");
  fprintf(fp, "      %s_free(&arr->data[i]);\n", struct_name);
  fprintf(fp, "    }\n");
  fprintf(fp, "    c_orm_system_free(arr->data);\n");
  fprintf(fp, "    arr->data = NULL;\n");
  fprintf(fp, "  }\n");
  fprintf(fp, "  arr->length = 0;\n");
  fprintf(fp, "  arr->capacity = 0;\n");
  fprintf(fp, "  return C_ORM_OK;\n");
  fprintf(fp, "}\n\n");

  /* Deepcopy */
  fprintf(fp,
          "/**\n * @brief Deep copy a %s row.\n * @param src Source item.\n * "
          "@param dest Destination item.\n * @return C_ORM_OK on success or "
          "C_ORM_ERROR_MEMORY.\n */\n",
          struct_name);
  fprintf(
      fp,
      "c_orm_error_t %s_deepcopy(const struct %s *src, struct %s *dest) {\n",
      struct_name, struct_name, struct_name);
  {
    int needs_rc = 0;
    for (i = 0; i < table->n_columns; ++i) {
      int col_str = S2C_IS_STRING_TYPE(table->columns[i].type);
      int col_null = s2c_calc_col_nullable(table, &table->columns[i]);
      if (col_str || col_null) {
        needs_rc = 1;
        break;
      }
    }
    if (needs_rc) {
      fprintf(fp, "  c_orm_error_t rc;\n");
    }
  }
  fprintf(fp, "  if (!src || !dest) return C_ORM_ERROR_MEMORY;\n");
  fprintf(fp, "  memset(dest, 0, sizeof(*dest));\n");
  for (i = 0; i < table->n_columns; ++i) {
    const struct sql_column_t *col = &table->columns[i];
    char *c_type =
        (char *)(size_t)k_s2c_c_types[s2c_calc_c_type_idx(col->type)];
    int is_str = S2C_IS_STRING_TYPE(col->type);
    int nullable = s2c_calc_col_nullable(table, col);
    if (!c_type) {
      c_type = (char *)(size_t) "void *";
    }

    if (is_str) {
      fprintf(fp, "  if (src->%s) {\n", col->name);
      fprintf(fp,
              "    rc = c_orm_strdup(src->%s, &dest->%s);\n    if (rc != "
              "C_ORM_OK) {\n      %s_free(dest);\n      return rc;\n    }\n",
              col->name, col->name, struct_name);
      fprintf(fp, "  } else {\n    dest->%s = NULL;\n  }\n", col->name);
    } else if (nullable) {
      fprintf(fp, "  if (src->%s) {\n", col->name);
      fprintf(fp,
              "    rc = c_orm_system_malloc(sizeof(%s), (void **)&dest->%s);\n"
              "    if (rc != C_ORM_OK) {\n      %s_free(dest);\n      return "
              "rc;\n    }\n",
              c_type, col->name, struct_name);
      fprintf(fp, "    *dest->%s = *src->%s;\n", col->name, col->name);
      fprintf(fp, "  } else {\n    dest->%s = NULL;\n  }\n", col->name);
    } else {
      fprintf(fp, "  dest->%s = src->%s;\n", col->name, col->name);
    }
  }
  fprintf(fp, "  return C_ORM_OK;\n");
  fprintf(fp, "}\n\n");

  /* Array Deepcopy */
  fprintf(fp,
          "/**\n * @brief Deep copy a %s_Array.\n * @param src Source array.\n "
          "* @param dest Destination array.\n * @return C_ORM_OK on success or "
          "C_ORM_ERROR_MEMORY.\n */\n",
          struct_name);
  fprintf(fp,
          "c_orm_error_t %s_Array_deepcopy(const struct %s_Array *src, struct "
          "%s_Array "
          "*dest) {\n",
          struct_name, struct_name, struct_name);
  fprintf(fp, "  size_t i;\n");
  fprintf(fp, "  c_orm_error_t err;\n");
  fprintf(fp, "  if (!src || !dest) return C_ORM_ERROR_MEMORY;\n");
  fprintf(fp, "  err = %s_Array_init(dest, src->capacity);\n", struct_name);
  fprintf(fp, "  if (err != C_ORM_OK) return err;\n");
  fprintf(fp, "  for (i = 0; i < src->length; ++i) {\n");
  fprintf(fp, "    err = %s_deepcopy(&src->data[i], &dest->data[i]);\n",
          struct_name);
  fprintf(fp, "    if (err != C_ORM_OK) {\n");
  fprintf(fp, "      dest->length = i;\n");
  fprintf(fp, "      %s_Array_free(dest);\n", struct_name);
  fprintf(fp, "      return err;\n");
  fprintf(fp, "    }\n");
  fprintf(fp, "  }\n");
  fprintf(fp, "  dest->length = src->length;\n");
  fprintf(fp, "  return C_ORM_OK;\n");
  fprintf(fp, "}\n\n");

  rc = s2c_emit_queries_and_meta(fp, table, struct_name);
  return (c_orm_error_t)((int)rc | (int)c_orm_system_free(struct_name));
}

/** @brief Static C ORM type constant lookup table indexed by
 * s2c_calc_c_orm_type_idx. */
static const char *const k_s2c_orm_types[] = {
    "C_ORM_TYPE_UNKNOWN",   "C_ORM_TYPE_INT32",   "C_ORM_TYPE_INT64",
    "C_ORM_TYPE_STRING",    "C_ORM_TYPE_GRAPHIC", "C_ORM_TYPE_VARGRAPHIC",
    "C_ORM_TYPE_DATE",      "C_ORM_TYPE_TIME",    "C_ORM_TYPE_TIMESTAMP",
    "C_ORM_TYPE_FLOAT",     "C_ORM_TYPE_DOUBLE",  "C_ORM_TYPE_DECIMAL",
    "C_ORM_TYPE_DECFLOAT",  "C_ORM_TYPE_BOOL",    "C_ORM_TYPE_BINARY_FIXED",
    "C_ORM_TYPE_VARBINARY", "C_ORM_TYPE_BLOB"};

/**
 * @brief Computes the lookup index into `k_s2c_orm_types` for an SQL data type.
 * @param type SQL data type enum value.
 * @return Index into `k_s2c_orm_types`.
 */
static int s2c_calc_c_orm_type_idx(enum SqlDataType type) {
  switch (type) {
  case SQL_TYPE_SMALLINT:
  case SQL_TYPE_INT:
    return 1;
  case SQL_TYPE_BIGINT:
    return 2;
  case SQL_TYPE_VARCHAR:
  case SQL_TYPE_TEXT:
  case SQL_TYPE_CHAR:
  case SQL_TYPE_CLOB:
    return 3;
  case SQL_TYPE_GRAPHIC:
    return 4;
  case SQL_TYPE_VARGRAPHIC:
  case SQL_TYPE_DBCLOB:
    return 5;
  case SQL_TYPE_DATE:
    return 6;
  case SQL_TYPE_TIME:
    return 7;
  case SQL_TYPE_TIMESTAMP:
    return 8;
  case SQL_TYPE_FLOAT:
    return 9;
  case SQL_TYPE_DOUBLE:
    return 10;
  case SQL_TYPE_DECIMAL:
  case SQL_TYPE_NUMERIC:
    return 11;
  case SQL_TYPE_DECFLOAT:
    return 12;
  case SQL_TYPE_BOOLEAN:
    return 13;
  case SQL_TYPE_BINARY:
    return 14;
  case SQL_TYPE_VARBINARY:
    return 15;
  case SQL_TYPE_BLOB:
    return 16;
  default:
    return 0;
  }
}

/**
 * @brief Converts a SQL data type enum to its corresponding C ORM string
 * representation.
 * @param type SQL data type enum value.
 * @param out_val Pointer receiving C ORM type string representation.
 * @return C_ORM_OK on success or error code.
 */
c_orm_error_t sql_type_to_c_orm_type(enum SqlDataType type,
                                     const char **out_val) {
  if (!out_val)
    return C_ORM_ERROR_MEMORY;
  *out_val = k_s2c_orm_types[s2c_calc_c_orm_type_idx(type)];
  return C_ORM_OK;
}

/**
 * @brief Emit c-orm metadata structs into the C source file.
 *
 * @param fp File pointer to write to.
 * @param table The SQL table AST node.
 * @param struct_name The generated struct name.
 * @return C_ORM_OK on completion.
 */
static c_orm_error_t s2c_emit_metadata(FILE *fp,
                                       const struct sql_table_t *table,
                                       const char *struct_name) {
  size_t i;
  int is_pk;
  int is_null;
  const char *orm_type_str;
  c_orm_error_t rc = C_ORM_OK;

  /* Emit column names array */
  fprintf(fp, "/** @brief Column names for %s. */\n", struct_name);
  fprintf(fp, "const char* %s_col_names[] = {\n", struct_name);
  for (i = 0; i < table->n_columns; ++i) {
    fprintf(fp, "  \"%s\"%s\n", table->columns[i].name,
            i == table->n_columns - 1 ? "" : ",");
  }
  fprintf(fp, "};\n\n");

  /* Emit column types array */
  fprintf(fp, "/** @brief Column types for %s. */\n", struct_name);
  fprintf(fp, "const c_orm_type_t %s_col_types[] = {\n", struct_name);
  for (i = 0; i < table->n_columns; ++i) {
    orm_type_str =
        k_s2c_orm_types[s2c_calc_c_orm_type_idx(table->columns[i].type)];
    fprintf(fp, "  %s%s\n", orm_type_str, i == table->n_columns - 1 ? "" : ",");
  }
  fprintf(fp, "};\n\n");

  /* Emit column offsets array */
  fprintf(fp, "/** @brief Column struct byte offsets for %s. */\n",
          struct_name);
  fprintf(fp, "const size_t %s_col_offsets[] = {\n", struct_name);
  for (i = 0; i < table->n_columns; ++i) {
    fprintf(fp, "  offsetof(struct %s, %s)%s\n", struct_name,
            table->columns[i].name, i == table->n_columns - 1 ? "" : ",");
  }
  fprintf(fp, "};\n\n");

  /* Emit PK flags */
  fprintf(fp, "/** @brief Primary key flags for %s columns. */\n", struct_name);
  fprintf(fp, "const bool %s_col_is_pk[] = {\n", struct_name);
  for (i = 0; i < table->n_columns; ++i) {
    is_pk = s2c_calc_col_pk(table, &table->columns[i]);
    fprintf(fp, "  %s%s\n", is_pk ? "true" : "false",
            i == table->n_columns - 1 ? "" : ",");
  }
  fprintf(fp, "};\n\n");

  /* Emit Nullable flags */
  fprintf(fp, "/** @brief Nullability flags for %s columns. */\n", struct_name);
  fprintf(fp, "const bool %s_col_is_nullable[] = {\n", struct_name);
  for (i = 0; i < table->n_columns; ++i) {
    is_null = s2c_calc_col_nullable(table, &table->columns[i]);
    fprintf(fp, "  %s%s\n", is_null ? "true" : "false",
            i == table->n_columns - 1 ? "" : ",");
  }
  fprintf(fp, "};\n\n");

  /* Emit relationships array */
  fprintf(fp, "/** @brief Foreign key target descriptors for %s columns. */\n",
          struct_name);
  fprintf(fp, "const char* %s_col_fk_targets[] = {\n", struct_name);
  for (i = 0; i < table->n_columns; ++i) {
    const char *ref_tbl = NULL;
    const char *ref_col = NULL;
    rc = s2c_resolve_col_fk(table, &table->columns[i], &ref_tbl, &ref_col);
    if (!ref_tbl) {
      fprintf(fp, "  NULL%s\n", i == table->n_columns - 1 ? "" : ",");
    } else if (ref_col) {
      fprintf(fp, "  \"%s.%s\"%s\n", ref_tbl, ref_col,
              i == table->n_columns - 1 ? "" : ",");
    } else {
      fprintf(fp, "  \"%s\"%s\n", ref_tbl,
              i == table->n_columns - 1 ? "" : ",");
    }
  }
  fprintf(fp, "};\n\n");

  /* Emit struct sizes */
  fprintf(fp, "/** @brief Size in bytes of struct %s. */\n", struct_name);
  fprintf(fp, "const size_t %s_struct_size = sizeof(struct %s);\n\n",
          struct_name, struct_name);

  /* Emit fully compiled metadata struct */
  fprintf(fp, "/** @brief Column reflection metadata array for %s. */\n",
          struct_name);
  fprintf(fp, "const c_orm_column_meta_t %s_meta_columns[] = {\n", struct_name);
  for (i = 0; i < table->n_columns; ++i) {
    unsigned int col_prec = 0;
    unsigned int col_scale = 0;
    unsigned long col_fixed = 0UL;
    unsigned int col_ccsid = (unsigned int)table->columns[i].ccsid;
    const char *ref_tbl = NULL;
    const char *ref_col = NULL;

    is_pk = s2c_calc_col_pk(table, &table->columns[i]);
    is_null = s2c_calc_col_nullable(table, &table->columns[i]);
    rc = s2c_resolve_col_fk(table, &table->columns[i], &ref_tbl, &ref_col);
    orm_type_str =
        k_s2c_orm_types[s2c_calc_c_orm_type_idx(table->columns[i].type)];

    if ((table->columns[i].type == SQL_TYPE_DECIMAL ||
         table->columns[i].type == SQL_TYPE_NUMERIC ||
         table->columns[i].type == SQL_TYPE_DECFLOAT) &&
        table->columns[i].length > 0) {
      col_prec = (unsigned int)table->columns[i].length;
    }
    if ((table->columns[i].type == SQL_TYPE_DECIMAL ||
         table->columns[i].type == SQL_TYPE_NUMERIC) &&
        table->columns[i].scale >= 0) {
      col_scale = (unsigned int)table->columns[i].scale;
    }
    if ((table->columns[i].type == SQL_TYPE_CHAR ||
         table->columns[i].type == SQL_TYPE_GRAPHIC ||
         table->columns[i].type == SQL_TYPE_BINARY) &&
        table->columns[i].length > 0) {
      col_fixed = (unsigned long)table->columns[i].length;
    }

    fprintf(fp, "  { \"%s\", %s, offsetof(struct %s, %s), %s, %s, ",
            table->columns[i].name, orm_type_str, struct_name,
            table->columns[i].name, is_pk ? "true" : "false",
            is_null ? "true" : "false");
    if (!ref_tbl) {
      fprintf(fp, "NULL");
    } else if (ref_col) {
      fprintf(fp, "\"%s.%s\"", ref_tbl, ref_col);
    } else {
      fprintf(fp, "\"%s\"", ref_tbl);
    }
    fprintf(fp, ", false, false, %u, %u, %lu, %u }%s\n", col_prec, col_scale,
            col_fixed, col_ccsid, i == table->n_columns - 1 ? "" : ",");
  }
  fprintf(fp, "};\n\n");

  fprintf(fp, "/** @brief Table reflection metadata descriptor for %s. */\n",
          struct_name);
  fprintf(fp, "const c_orm_table_meta_t %s_meta = {\n", struct_name);
  fprintf(fp, "  \"%s\",\n", table->name);
  fprintf(fp, "  %s_meta_columns,\n", struct_name);
  fprintf(fp, "  %lu,\n", (unsigned long)table->n_columns);
  fprintf(fp, "  sizeof(struct %s),\n", struct_name);
  fprintf(fp, "  %s_query_select_all,\n", struct_name);
  fprintf(fp, "  %s_query_select_by_pk,\n", struct_name);
  fprintf(fp, "  %s_query_insert,\n", struct_name);
  fprintf(fp, "  %s_query_update,\n", struct_name);
  fprintf(fp, "  %s_query_delete_by_pk,\n", struct_name);
  fprintf(fp, "  NULL,\n");
  fprintf(fp, "  false, false, 0, 0, { 0 }, NULL, 0\n");
  fprintf(fp, "};\n\n");

  return rc;
}

/**
 * @brief Emits SQL query macros and ORM table metadata.
 * @param fp Target file pointer.
 * @param table SQL table metadata.
 * @param struct_name Generated struct name string.
 * @return C_ORM_OK on completion.
 */
static c_orm_error_t s2c_emit_queries_and_meta(FILE *fp,
                                               const struct sql_table_t *table,
                                               const char *struct_name) {
  size_t i;
  int pk_count = 0;
  const char *pk_name = NULL;

  for (i = 0; i < table->n_columns; ++i) {
    if (s2c_calc_col_pk(table, &table->columns[i])) {
      pk_count++;
      pk_name = table->columns[i].name;
    }
  }

  fprintf(fp, "/** @brief SQL SELECT all query for %s. */\n", struct_name);
  fprintf(fp, "#define %s_query_select_all \"SELECT * FROM %s\"\n\n",
          struct_name, table->name);

  if (pk_count == 1) {
    fprintf(fp, "/** @brief SQL SELECT by primary key query for %s. */\n",
            struct_name);
    fprintf(fp,
            "#define %s_query_select_by_pk \"SELECT * FROM %s WHERE %s = "
            "?\"\n\n",
            struct_name, table->name, pk_name);
    fprintf(fp, "/** @brief SQL DELETE by primary key query for %s. */\n",
            struct_name);
    fprintf(fp,
            "#define %s_query_delete_by_pk \"DELETE FROM %s WHERE %s = "
            "?\"\n\n",
            struct_name, table->name, pk_name);
  } else {
    fprintf(fp, "/** @brief SQL SELECT by primary key query for %s. */\n",
            struct_name);
    fprintf(fp, "#define %s_query_select_by_pk NULL\n\n", struct_name);
    fprintf(fp, "/** @brief SQL DELETE by primary key query for %s. */\n",
            struct_name);
    fprintf(fp, "#define %s_query_delete_by_pk NULL\n\n", struct_name);
  }

  fprintf(fp, "/** @brief SQL INSERT query for %s. */\n", struct_name);
  fprintf(fp, "#define %s_query_insert \"INSERT INTO %s (", struct_name,
          table->name);
  for (i = 0; i < table->n_columns; ++i) {
    fprintf(fp, "%s%s", table->columns[i].name,
            i == table->n_columns - 1 ? "" : ", ");
  }
  fprintf(fp, ") VALUES (");
  for (i = 0; i < table->n_columns; ++i) {
    fprintf(fp, "?%s", i == table->n_columns - 1 ? "" : ", ");
  }
  fprintf(fp, ")\"\n\n");

  fprintf(fp, "/** @brief SQL UPDATE query for %s. */\n", struct_name);
  fprintf(fp, "#define %s_query_update \"UPDATE %s SET ", struct_name,
          table->name);
  for (i = 0; i < table->n_columns; ++i) {
    fprintf(fp, "%s = ?%s", table->columns[i].name,
            i == table->n_columns - 1 ? "" : ", ");
  }
  if (pk_count == 1) {
    fprintf(fp, " WHERE %s = ?\"\n\n", pk_name);
  } else {
    fprintf(fp, "\"\n\n");
  }

  return s2c_emit_metadata(fp, table, struct_name);
}
/* To be appended to sql_to_c.c */

/**
 * @brief Calculates 64-bit FNV-1a hash step mixing a NUL-terminated string.
 * @param str NUL-terminated string to mix.
 * @param h Current 64-bit FNV-1a hash state.
 * @return Updated 64-bit FNV-1a hash state.
 */
static c_orm_uint64_t fnv1a_calc_str(const char *str, c_orm_uint64_t h) {
  const c_orm_uint64_t prime = (((c_orm_uint64_t)0x100) << 32) | 0x000001B3;
  while (*str) {
    h ^= (unsigned char)(*str++);
    h *= prime;
  }
  h ^= (unsigned char)0xFFU;
  h *= prime;
  return h;
}

/**
 * @brief Calculates 64-bit FNV-1a hash step mixing a 32-bit unsigned integer.
 * @param val Unsigned integer value to mix.
 * @param h Current 64-bit FNV-1a hash state.
 * @return Updated 64-bit FNV-1a hash state.
 */
static c_orm_uint64_t fnv1a_calc_u32(unsigned int val, c_orm_uint64_t h) {
  const c_orm_uint64_t prime = (((c_orm_uint64_t)0x100) << 32) | 0x000001B3;
  unsigned int b;
  for (b = 0; b < 4U; ++b) {
    h ^= (unsigned char)((val >> (b * 8U)) & 0xFFU);
    h *= prime;
  }
  return h;
}

/**
 * @brief Emits field cleanup statements inside a generated projection `_free`
 * function.
 * @param fp Output file stream.
 * @param indent Indentation prefix string.
 * @param access_prefix Member access prefix (e.g. `"obj->"`).
 * @param field Projection field descriptor.
 * @return C_ORM_OK on completion.
 */
static c_orm_error_t
s2c_emit_proj_field_free(FILE *fp, const char *indent,
                         const char *access_prefix,
                         const cdd_c_query_projection_field_t *field) {
  int is_str = S2C_IS_STRING_TYPE(field->type);
  int is_blob = S2C_IS_BLOB_TYPE(field->type);

  if (field->is_array) {
    fprintf(fp, "%sif (%s%s) { c_orm_system_free(%s%s); %s%s = NULL; }\n",
            indent, access_prefix, field->name, access_prefix, field->name,
            access_prefix, field->name);
  } else if (is_str && field->length == 0) {
    if (field->is_secure) {
      fprintf(fp,
              "%sif (%s%s) { memset(%s%s, 0, strlen(%s%s)); "
              "c_orm_system_free(%s%s); %s%s = NULL; }\n",
              indent, access_prefix, field->name, access_prefix, field->name,
              access_prefix, field->name, access_prefix, field->name,
              access_prefix, field->name);
    } else {
      fprintf(fp, "%sif (%s%s) { c_orm_system_free(%s%s); %s%s = NULL; }\n",
              indent, access_prefix, field->name, access_prefix, field->name,
              access_prefix, field->name);
    }
  } else if (is_str && field->is_secure) {
    fprintf(fp, "%smemset(%s%s, 0, %u);\n", indent, access_prefix, field->name,
            (unsigned int)field->length);
  } else if (is_blob) {
    fprintf(fp,
            "%sif (%s%s.data) { c_orm_system_free(%s%s.data); %s%s.data = "
            "NULL; %s%s.size = 0; }\n",
            indent, access_prefix, field->name, access_prefix, field->name,
            access_prefix, field->name, access_prefix, field->name);
  }
  return C_ORM_OK;
}

/**
 * @brief sql_to_c_projection_struct_emit
 * @param fp fp
 * @param proj proj
 * @param struct_name struct_name
 * @param out_hash out_hash
 */
c_orm_error_t
sql_to_c_projection_struct_emit(FILE *fp, const cdd_c_query_projection_t *proj,
                                const char *struct_name,
                                c_orm_uint64_t *out_hash) {

  size_t i;

  if (!fp || !proj || !struct_name)
    return C_ORM_ERROR_MEMORY;

  if (out_hash) {
    unsigned long hi, lo;
    c_orm_uint64_t hash = (((c_orm_uint64_t)0xCBF29CE4) << 32) | 0x84222325;
    hash = fnv1a_calc_str(struct_name, hash);
    if (proj->source_table) {
      hash = fnv1a_calc_str(proj->source_table, hash);
    }
    hash = fnv1a_calc_u32((unsigned int)proj->n_fields, hash);
    for (i = 0; i < proj->n_fields; ++i) {
      if (proj->fields[i].name) {
        hash = fnv1a_calc_str(proj->fields[i].name, hash);
      }
      hash = fnv1a_calc_u32((unsigned int)proj->fields[i].type, hash);
      hash = fnv1a_calc_u32((unsigned int)proj->fields[i].length, hash);
      hash = fnv1a_calc_u32((unsigned int)proj->fields[i].is_array, hash);
    }
    *out_hash = hash;

    hi = (unsigned long)(hash >> 32);
    lo = (unsigned long)(hash & 0xFFFFFFFFUL);
    fprintf(fp, "\n/* Auto-generated Route Hash ID Tag: %lx%08lx */\n", hi, lo);
  }

  fprintf(fp, "/**\n");

  fprintf(fp, " * @brief Specific generated struct for a query projection.\n");

  if (proj->source_table) {

    fprintf(fp, " * Target table: %s\n", proj->source_table);
  }

  fprintf(fp, " */\n");

  fprintf(fp, "typedef struct %s {\n", struct_name);

  for (i = 0; i < proj->n_fields; ++i) {

    const cdd_c_query_projection_field_t *field = &proj->fields[i];
    char *c_type =
        (char *)(size_t)k_s2c_c_types[s2c_calc_c_type_idx(field->type)];

    if (!c_type) {

      fprintf(fp, "    /* Unknown SQL type: %d */\n", field->type);

      fprintf(fp, "    void *%s;\n", field->name);

    } else {

      if (field->is_array) {

        fprintf(fp, "    %s *%s;\n", c_type, field->name);

        fprintf(fp, "    size_t %s_count;\n", field->name);

      } else {
        int is_str = S2C_IS_STRING_TYPE(field->type);

        if (is_str) {

          if (field->length > 0) {

            fprintf(fp, "    char %s[%u];\n", field->name,
                    (unsigned int)field->length);

          } else {

            fprintf(fp, "    %s %s;\n", c_type, field->name);
          }

        } else {

          fprintf(fp, "    %s %s;\n", c_type, field->name);
        }
      }
    }
  }

  fprintf(fp, "} %s;\n\n", struct_name);

  /* Generate deep free decl */

  fprintf(fp, "extern C_ORM_EXPORT c_orm_error_t %s_free(%s *obj);\n\n",
          struct_name, struct_name);

  return C_ORM_OK;
}

/**
 * @brief sql_to_c_projection_free_emit
 * @param fp fp
 * @param proj proj
 * @param struct_name struct_name
 */
c_orm_error_t
sql_to_c_projection_free_emit(FILE *fp, const cdd_c_query_projection_t *proj,
                              const char *struct_name) {

  size_t i;
  c_orm_error_t rc = C_ORM_OK;

  if (!fp || !proj || !struct_name)
    return C_ORM_ERROR_MEMORY;

  fprintf(fp, "c_orm_error_t %s_free(%s *obj) {\n", struct_name, struct_name);

  fprintf(fp, "    if (!obj) return C_ORM_OK;\n");

  for (i = 0; i < proj->n_fields; ++i) {
    rc = s2c_emit_proj_field_free(fp, "    ", "obj->", &proj->fields[i]);
  }

  fprintf(fp, "    return C_ORM_OK;\n");
  fprintf(fp, "}\n\n");

  return rc;
}

/**
 * @brief sql_to_c_projection_meta_emit
 * @param fp fp
 * @param proj proj
 * @param struct_name struct_name
 */
c_orm_error_t
sql_to_c_projection_meta_emit(FILE *fp, const cdd_c_query_projection_t *proj,
                              const char *struct_name) {

  size_t i;
  const char *orm_type_str = NULL;

  if (!fp || !proj || !struct_name)
    return C_ORM_ERROR_MEMORY;

  fprintf(fp, "/* Metadata for %s */\n", struct_name);

  fprintf(fp, "static const c_orm_prop_meta_t %s_props[] = {\n", struct_name);

  for (i = 0; i < proj->n_fields; ++i) {

    const cdd_c_query_projection_field_t *field = &proj->fields[i];
    orm_type_str = k_s2c_orm_types[s2c_calc_c_orm_type_idx(field->type)];

    fprintf(fp, "    {\n");

    fprintf(fp, "        \"%s\",\n", field->name);

    fprintf(fp, "        %s,\n", orm_type_str);

    fprintf(fp, "        offsetof(%s, %s),\n", struct_name, field->name);

    fprintf(fp, "        %s,\n", field->is_array ? "1" : "0");

    fprintf(fp, "        %u,\n", (unsigned int)field->length);

    fprintf(fp, "        %d /* is_secure */\n", field->is_secure ? 1 : 0);
    fprintf(fp, "    }");
    if (i < proj->n_fields - 1) {
      fprintf(fp, ",");
    }

    fprintf(fp, "\n");
  }

  fprintf(fp, "};\n\n");

  fprintf(fp, "const c_orm_meta_t %s_meta = {\n", struct_name);

  fprintf(fp, "    \"%s\",\n", struct_name);

  fprintf(fp, "    sizeof(%s),\n", struct_name);

  fprintf(fp, "    %u,\n", (unsigned int)proj->n_fields);

  fprintf(fp, "    %s_props,\n", struct_name);

  fprintf(fp, "    NULL\n");

  fprintf(fp, "};\n\n");

  return C_ORM_OK;
}

/**
 * @brief sql_to_c_projection_hydrate_emit
 * @param fp fp
 * @param proj proj
 * @param struct_name struct_name
 */
c_orm_error_t
sql_to_c_projection_hydrate_emit(FILE *fp, const cdd_c_query_projection_t *proj,
                                 const char *struct_name) {

  size_t i;

  if (!fp || !proj || !struct_name)
    return C_ORM_ERROR_MEMORY;

  fprintf(fp,
          "c_orm_error_t %s_hydrate(%s *out_struct, const "
          "cdd_c_abstract_struct_t *row) {\n",
          struct_name, struct_name);

  fprintf(fp, "    cdd_c_variant_t *val;\n");

  fprintf(fp, "    if (!out_struct || !row) return C_ORM_ERROR_MEMORY;\n");

  for (i = 0; i < proj->n_fields; ++i) {

    const cdd_c_query_projection_field_t *field = &proj->fields[i];

    fprintf(fp,
            "    if (cdd_c_abstract_get(row, \"%s\", &val) == C_ORM_OK) {\n",
            field->name);

    switch (field->type) {

    case SQL_TYPE_SMALLINT:
    case SQL_TYPE_INT:
    case SQL_TYPE_BIGINT:
    case SQL_TYPE_BOOLEAN:

      fprintf(fp, "        if (val->type == CDD_C_VARIANT_TYPE_INT) {\n");

      fprintf(fp, "            out_struct->%s = val->value.i_val;\n",
              field->name);

      fprintf(fp, "        }\n");

      break;

    case SQL_TYPE_FLOAT:
    case SQL_TYPE_DOUBLE:

      fprintf(fp, "        if (val->type == CDD_C_VARIANT_TYPE_FLOAT) {\n");

      fprintf(fp, "            out_struct->%s = val->value.f_val;\n",
              field->name);

      fprintf(fp, "        }\n");

      break;

    case SQL_TYPE_VARCHAR:
    case SQL_TYPE_TEXT:
    case SQL_TYPE_CHAR:
    case SQL_TYPE_CLOB:
    case SQL_TYPE_GRAPHIC:
    case SQL_TYPE_VARGRAPHIC:
    case SQL_TYPE_DBCLOB:
    case SQL_TYPE_DATE:
    case SQL_TYPE_TIME:
    case SQL_TYPE_TIMESTAMP:

      fprintf(fp, "        if (val->type == CDD_C_VARIANT_TYPE_STRING) {\n");

      if (field->length > 0) {

        fprintf(fp,
                "            if (C_ORM_STRNCPY(out_struct->%s, %u, "
                "val->value.s_val, %u - 1) != C_ORM_OK) return "
                "C_ORM_ERROR_OVERFLOW;\n",
                field->name, (unsigned int)field->length,
                (unsigned int)field->length);

        fprintf(fp, "            out_struct->%s[%u - 1] = '\\0';\n",
                field->name, (unsigned int)field->length);

      } else {

        fprintf(fp,
                "            if (C_ORM_STRDUP(val->value.s_val, "
                "&out_struct->%s) != C_ORM_OK) return C_ORM_ERROR_MEMORY;\n",
                field->name);
      }

      fprintf(fp, "        }\n");

      break;

    case SQL_TYPE_DECIMAL:
    case SQL_TYPE_NUMERIC:
    case SQL_TYPE_DECFLOAT:

      fprintf(fp, "        if (val->type == CDD_C_VARIANT_TYPE_DECIMAL) {\n");
      fprintf(fp, "            out_struct->%s = val->value.d_val;\n",
              field->name);
      fprintf(fp, "        }\n");
      break;

    case SQL_TYPE_BLOB:
    case SQL_TYPE_BINARY:
    case SQL_TYPE_VARBINARY:

      fprintf(fp, "        if (val->type == CDD_C_VARIANT_TYPE_BLOB) {\n");
      fprintf(fp, "            if (val->value.b_val.size > 0 && "
                  "val->value.b_val.data) {\n");
      fprintf(fp,
              "                if (c_orm_system_malloc(val->value.b_val.size, "
              "&out_struct->%s.data) != C_ORM_OK) return C_ORM_ERROR_MEMORY;\n",
              field->name);
      fprintf(fp,
              "                memcpy(out_struct->%s.data, "
              "val->value.b_val.data, val->value.b_val.size);\n",
              field->name);
      fprintf(fp,
              "                out_struct->%s.size = val->value.b_val.size;\n",
              field->name);
      fprintf(fp, "            } else {\n");
      fprintf(fp, "                out_struct->%s.data = NULL;\n", field->name);
      fprintf(fp, "                out_struct->%s.size = 0;\n", field->name);
      fprintf(fp, "            }\n");
      fprintf(fp, "        }\n");
      break;

    default:
      break;
    }

    fprintf(fp, "    }\n");
  }

  fprintf(fp, "    return C_ORM_OK;\n");

  fprintf(fp, "}\n\n");

  return C_ORM_OK;
}

/**
 * @brief sql_to_c_projection_dehydrate_emit
 * @param fp fp
 * @param proj proj
 * @param struct_name struct_name
 */
c_orm_error_t sql_to_c_projection_dehydrate_emit(
    FILE *fp, const cdd_c_query_projection_t *proj, const char *struct_name) {

  size_t i;

  if (!fp || !proj || !struct_name)
    return C_ORM_ERROR_MEMORY;

  fprintf(
      fp,
      "c_orm_error_t %s_dehydrate(const %s *in_struct, cdd_c_abstract_struct_t "
      "*out_row) {\n",
      struct_name, struct_name);

  fprintf(fp, "    cdd_c_variant_t val;\n");

  fprintf(fp, "    if (!in_struct || !out_row) return C_ORM_ERROR_MEMORY;\n");

  fprintf(fp, "    if (cdd_c_abstract_struct_init(out_row) != C_ORM_OK) return "
              "C_ORM_ERROR_MEMORY;\n");

  for (i = 0; i < proj->n_fields; ++i) {

    const cdd_c_query_projection_field_t *field = &proj->fields[i];

    switch (field->type) {

    case SQL_TYPE_SMALLINT:
    case SQL_TYPE_INT:
    case SQL_TYPE_BIGINT:
    case SQL_TYPE_BOOLEAN:

      fprintf(fp, "    val.type = CDD_C_VARIANT_TYPE_INT;\n");

      fprintf(fp, "    val.value.i_val = in_struct->%s;\n", field->name);

      break;

    case SQL_TYPE_FLOAT:
    case SQL_TYPE_DOUBLE:

      fprintf(fp, "    val.type = CDD_C_VARIANT_TYPE_FLOAT;\n");

      fprintf(fp, "    val.value.f_val = in_struct->%s;\n", field->name);

      break;

    case SQL_TYPE_VARCHAR:
    case SQL_TYPE_TEXT:
    case SQL_TYPE_CHAR:
    case SQL_TYPE_CLOB:
    case SQL_TYPE_GRAPHIC:
    case SQL_TYPE_VARGRAPHIC:
    case SQL_TYPE_DBCLOB:
    case SQL_TYPE_DATE:
    case SQL_TYPE_TIME:
    case SQL_TYPE_TIMESTAMP:

      fprintf(fp, "    val.type = CDD_C_VARIANT_TYPE_STRING;\n");

      if (field->length > 0) {

        fprintf(fp, "    val.value.s_val = (char*)in_struct->%s;\n",
                field->name);

      } else {

        fprintf(fp, "    val.value.s_val = in_struct->%s;\n", field->name);
      }

      break;

    case SQL_TYPE_DECIMAL:
    case SQL_TYPE_NUMERIC:
    case SQL_TYPE_DECFLOAT:

      fprintf(fp, "    val.type = CDD_C_VARIANT_TYPE_DECIMAL;\n");
      fprintf(fp, "    val.value.d_val = in_struct->%s;\n", field->name);
      break;

    case SQL_TYPE_BLOB:
    case SQL_TYPE_BINARY:
    case SQL_TYPE_VARBINARY:

      fprintf(fp, "    val.type = CDD_C_VARIANT_TYPE_BLOB;\n");
      fprintf(
          fp,
          "    val.value.b_val.data = (unsigned char *)in_struct->%s.data;\n",
          field->name);
      fprintf(fp, "    val.value.b_val.size = in_struct->%s.size;\n",
              field->name);
      break;

    default:

      continue;
    }

    fprintf(
        fp,
        "    if (cdd_c_abstract_set(out_row, \"%s\", &val) != C_ORM_OK) {\n",
        field->name);

    fprintf(fp, "        cdd_c_abstract_struct_free(out_row);\n");

    fprintf(fp, "        return C_ORM_ERROR_MEMORY;\n");

    fprintf(fp, "    }\n");
  }

  fprintf(fp, "    return C_ORM_OK;\n");

  fprintf(fp, "}\n\n");

  return C_ORM_OK;
}
/* To be appended to sql_to_c.c */

/**
 * @brief sql_to_c_projection_nested_struct_emit
 * @param fp fp
 * @param proj proj
 * @param struct_name struct_name
 */
c_orm_error_t sql_to_c_projection_nested_struct_emit(
    FILE *fp, const cdd_c_query_projection_t *proj, const char *struct_name) {

  if (!fp || !proj || !struct_name)
    return C_ORM_ERROR_MEMORY;

  /* A nested 1-to-1 struct uses the exact same codegen logic as a top level
     projection,

     just rendered within the context of an outer struct */

  return sql_to_c_projection_struct_emit(fp, proj, struct_name, NULL);
}

/**
 * @brief sql_to_c_projection_nested_array_emit
 * @param fp fp
 * @param proj proj
 * @param struct_name struct_name
 * @param array_name array_name
 */
c_orm_error_t sql_to_c_projection_nested_array_emit(
    FILE *fp, const cdd_c_query_projection_t *proj, const char *struct_name,
    const char *array_name) {

  c_orm_error_t err;

  if (!array_name)
    return C_ORM_ERROR_MEMORY;

  /* First emit the base struct */

  err = sql_to_c_projection_struct_emit(fp, proj, struct_name, NULL);
  if (err != C_ORM_OK)
    return err;

  /* Then emit the array wrapper struct used for 1-to-Many nested responses */

  fprintf(fp, "/**\n");

  fprintf(fp, " * @brief 1-to-Many array container for %s.\n", struct_name);

  fprintf(fp, " */\n");

  fprintf(fp, "typedef struct %s {\n", array_name);

  fprintf(fp, "    %s *items;\n", struct_name);

  fprintf(fp, "    size_t count;\n");

  fprintf(fp, "    size_t capacity;\n");

  fprintf(fp, "} %s;\n\n", array_name);

  /* Array Deep Free handler */

  fprintf(fp, "extern C_ORM_EXPORT c_orm_error_t %s_free(%s *arr);\n\n",
          array_name, array_name);

  fprintf(fp, "c_orm_error_t %s_free(%s *arr) {\n", array_name, array_name);

  fprintf(fp, "    size_t i;\n");

  fprintf(fp, "    if (!arr || !arr->items) return C_ORM_OK;\n");

  fprintf(fp, "    for (i = 0; i < arr->count; ++i) {\n");

  fprintf(fp, "        %s_free(&arr->items[i]);\n", struct_name);

  fprintf(fp, "    }\n");

  fprintf(fp, "    c_orm_system_free(arr->items);\n");

  fprintf(fp, "    arr->items = NULL;\n");

  fprintf(fp, "    arr->count = 0;\n");

  fprintf(fp, "    arr->capacity = 0;\n");

  fprintf(fp, "    return C_ORM_OK;\n");

  fprintf(fp, "}\n\n");

  return C_ORM_OK;
}

/* To be appended to sql_to_c.c */

/**
 * @brief sql_to_c_projection_dirty_bitmask_emit
 * @param fp fp
 * @param proj proj
 * @param struct_name struct_name
 */
c_orm_error_t sql_to_c_projection_dirty_bitmask_emit(
    FILE *fp, const cdd_c_query_projection_t *proj, const char *struct_name) {

  if (!fp || !proj || !struct_name)
    return C_ORM_ERROR_MEMORY;

  fprintf(fp, "/**\n");

  fprintf(fp, " * @brief Bitmask struct tracking dirtied fields for %s.\n",
          struct_name);

  fprintf(fp, " */\n");

  fprintf(fp, "typedef struct %s_mask {\n", struct_name);

  if (proj->n_fields == 0) {

    fprintf(fp, "    int _empty_mask;\n");

  } else if (proj->n_fields <= 8) {

    fprintf(fp, "    unsigned char mask;\n");

  } else if (proj->n_fields <= 16) {

    fprintf(fp, "    unsigned short mask;\n");

  } else if (proj->n_fields <= 32) {

    fprintf(fp, "    unsigned int mask;\n");

  } else if (proj->n_fields <= 64) {

    fprintf(fp, "    c_orm_uint64_t mask;\n");

  } else {

    size_t arr_size = (proj->n_fields / 64) + 1;

    fprintf(fp, "    c_orm_uint64_t mask[%u];\n", (unsigned int)arr_size);
  }

  fprintf(fp, "} %s_mask;\n\n", struct_name);

  return C_ORM_OK;
}

/**
 * @brief sql_to_c_projection_union_struct_emit
 * @param fp fp
 * @param projs projs
 * @param n_projs n_projs
 * @param struct_name struct_name
 */
c_orm_error_t
sql_to_c_projection_union_struct_emit(FILE *fp,
                                      const cdd_c_query_projection_t *projs,
                                      size_t n_projs, const char *struct_name) {

  size_t i, j;

  if (!fp || !projs || !struct_name || n_projs == 0)
    return C_ORM_ERROR_MEMORY;

  fprintf(fp, "/**\n");

  fprintf(fp,
          " * @brief Variant enum and struct for a SQL UNION projection.\n");

  fprintf(fp, " */\n");

  fprintf(fp, "typedef enum %s_type {\n", struct_name);

  fprintf(fp, "    %s_TYPE_UNKNOWN = 0,\n", struct_name);

  for (i = 0; i < n_projs; ++i) {

    fprintf(fp, "    %s_TYPE_BRANCH_%u", struct_name, (unsigned int)i);

    if (i < n_projs - 1)
      fprintf(fp, ",");

    fprintf(fp, "\n");
  }

  fprintf(fp, "} %s_type;\n\n", struct_name);

  fprintf(fp, "typedef struct %s {\n", struct_name);

  fprintf(fp, "    %s_type type;\n", struct_name);

  fprintf(fp, "    union {\n");

  for (i = 0; i < n_projs; ++i) {

    fprintf(fp, "        struct {\n");

    for (j = 0; j < projs[i].n_fields; ++j) {

      const cdd_c_query_projection_field_t *field = &projs[i].fields[j];
      char *c_type =
          (char *)(size_t)k_s2c_c_types[s2c_calc_c_type_idx(field->type)];

      if (c_type) {
        int is_str = S2C_IS_STRING_TYPE(field->type);

        if (field->is_array) {

          fprintf(fp, "            %s *%s;\n", c_type, field->name);

          fprintf(fp, "            size_t %s_count;\n", field->name);

        } else if (is_str) {

          if (field->length > 0) {

            fprintf(fp, "            char %s[%u];\n", field->name,
                    (unsigned int)field->length);

          } else {

            fprintf(fp, "            %s %s;\n", c_type, field->name);
          }

        } else {

          fprintf(fp, "            %s %s;\n", c_type, field->name);
        }
      }
    }

    fprintf(fp, "        } branch_%u;\n", (unsigned int)i);
  }

  fprintf(fp, "    } data;\n");

  fprintf(fp, "} %s;\n\n", struct_name);

  fprintf(fp, "extern C_ORM_EXPORT c_orm_error_t %s_free(%s *obj);\n\n",
          struct_name, struct_name);

  return C_ORM_OK;
}

c_orm_error_t
sql_to_c_projection_union_free_emit(FILE *fp,
                                    const cdd_c_query_projection_t *projs,
                                    size_t n_projs, const char *struct_name) {
  size_t i;
  size_t j;
  char prefix[64];
  c_orm_error_t rc = C_ORM_OK;

  if (!fp || !projs || !struct_name || n_projs == 0) {
    return C_ORM_ERROR_MEMORY;
  }

  fprintf(fp, "c_orm_error_t %s_free(%s *obj) {\n", struct_name, struct_name);
  fprintf(fp, "    if (!obj) return C_ORM_OK;\n");
  fprintf(fp, "    switch (obj->type) {\n");
  for (i = 0; i < n_projs; ++i) {
    unsigned int idx = (unsigned int)i;
    char rev[16];
    size_t rlen = 0;
    size_t pos = 17;
    fprintf(fp, "    case %s_TYPE_BRANCH_%u:\n", struct_name, idx);
    memcpy(prefix, "obj->data.branch_", 17);
    do {
      rev[rlen++] = (char)('0' + (idx % 10U));
      idx /= 10U;
    } while (idx > 0U);
    while (rlen > 0U) {
      prefix[pos++] = rev[--rlen];
    }
    prefix[pos++] = '.';
    prefix[pos] = '\0';
    for (j = 0; j < projs[i].n_fields; ++j) {
      rc =
          s2c_emit_proj_field_free(fp, "        ", prefix, &projs[i].fields[j]);
    }
    fprintf(fp, "        break;\n");
  }
  fprintf(fp, "    default:\n        break;\n    }\n");
  fprintf(fp, "    obj->type = %s_TYPE_UNKNOWN;\n", struct_name);
  fprintf(fp, "    return C_ORM_OK;\n}\n\n");
  return rc;
}

/**
 * @brief sql_to_c_projection_polymorphic_struct_emit
 * @param fp fp
 * @param proj proj
 * @param struct_name struct_name
 */
c_orm_error_t sql_to_c_projection_polymorphic_struct_emit(
    FILE *fp, const cdd_c_query_projection_t *proj, const char *struct_name) {

  if (!fp || !proj || !struct_name)
    return C_ORM_ERROR_MEMORY;

  fprintf(fp, "/**\n");

  fprintf(fp, " * @brief Polymorphic struct capable of mapping explicit fields "
              "and dynamic rows.\n");

  fprintf(fp, " */\n");

  fprintf(fp, "typedef struct %s {\n", struct_name);

  fprintf(fp, "    /* Abstract mapping properties dynamically resolved */\n");

  fprintf(fp, "    cdd_c_abstract_struct_t abstract_properties;\n");

  fprintf(fp, "    \n");

  fprintf(fp, "    /* Specific mapping properties generated strictly */\n");

  {
    size_t i;

    for (i = 0; i < proj->n_fields; ++i) {

      const cdd_c_query_projection_field_t *field = &proj->fields[i];
      char *c_type =
          (char *)(size_t)k_s2c_c_types[s2c_calc_c_type_idx(field->type)];

      if (c_type) {
        int is_str = S2C_IS_STRING_TYPE(field->type);

        if (field->is_array) {

          fprintf(fp, "    %s *%s;\n", c_type, field->name);

          fprintf(fp, "    size_t %s_count;\n", field->name);

        } else if (is_str) {

          if (field->length > 0) {

            fprintf(fp, "    char %s[%u];\n", field->name,
                    (unsigned int)field->length);

          } else {

            fprintf(fp, "    %s %s;\n", c_type, field->name);
          }

        } else {

          fprintf(fp, "    %s %s;\n", c_type, field->name);
        }
      }
    }
  }

  fprintf(fp, "} %s;\n\n", struct_name);

  fprintf(fp, "extern C_ORM_EXPORT c_orm_error_t %s_free(%s *obj);\n\n",
          struct_name, struct_name);

  return C_ORM_OK;
}

c_orm_error_t sql_to_c_projection_polymorphic_free_emit(
    FILE *fp, const cdd_c_query_projection_t *proj, const char *struct_name) {
  size_t i;
  c_orm_error_t rc = C_ORM_OK;

  if (!fp || !proj || !struct_name) {
    return C_ORM_ERROR_MEMORY;
  }

  fprintf(fp, "c_orm_error_t %s_free(%s *obj) {\n", struct_name, struct_name);
  fprintf(fp, "    if (!obj) return C_ORM_OK;\n");
  fprintf(fp, "    cdd_c_abstract_struct_free(&obj->abstract_properties);\n");
  for (i = 0; i < proj->n_fields; ++i) {
    rc = s2c_emit_proj_field_free(fp, "    ", "obj->", &proj->fields[i]);
  }
  fprintf(fp, "    return C_ORM_OK;\n}\n\n");
  return rc;
}
