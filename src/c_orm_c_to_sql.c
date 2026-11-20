/**
 * @file c_orm_c_to_sql.c
 * @defgroup c_orm_c_to_sql C to SQL Generation
 * @brief Translates C structures and IR schemas into SQL DDL statements.
 */

/* clang-format off */
#include "c_orm_c_to_sql.h"
#include "c_orm_safe_crt.h"
#include "c_orm_string_builder.h"
#include <stdlib.h>
#include <string.h>
/* clang-format on */

/**
 * @brief Maps a C type string to a dialect-specific SQL type string.
 * @param c_type C type string (e.g., "int", "char*", "double").
 * @param dialect Target SQL dialect.
 * @param _out_val Output pointer receiving static SQL type string.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t c_type_to_sql_type(const char *c_type,
                                        c_to_sql_dialect_t dialect,
                                        const char **_out_val) {
  if (!c_type) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if ((dialect == C_TO_SQL_DIALECT_DB2 || dialect == C_TO_SQL_DIALECT_ODBC) &&
      (strcmp(c_type, "int64_t") == 0 || strcmp(c_type, "c_orm_int64_t") == 0 ||
       strcmp(c_type, "C_ORM_TYPE_INT64") == 0 ||
       strcmp(c_type, "bigint") == 0)) {
    *_out_val = "BIGINT";
    return C_ORM_OK;
  }
  if (strcmp(c_type, "integer") == 0 || strcmp(c_type, "int") == 0 ||
      strcmp(c_type, "int32_t") == 0 || strcmp(c_type, "int64_t") == 0 ||
      strcmp(c_type, "c_orm_int64_t") == 0 || strcmp(c_type, "size_t") == 0 ||
      strcmp(c_type, "C_ORM_TYPE_INT32") == 0 ||
      strcmp(c_type, "C_ORM_TYPE_INT64") == 0) {
    *_out_val = (dialect == C_TO_SQL_DIALECT_MYSQL) ? "INT" : "INTEGER";
    return C_ORM_OK;
  }
  if (strncmp(c_type, "DECIMAL(", 8) == 0 ||
      strncmp(c_type, "NUMERIC(", 8) == 0 ||
      strncmp(c_type, "DECFLOAT(", 9) == 0 ||
      strncmp(c_type, "GRAPHIC(", 8) == 0 ||
      strncmp(c_type, "VARGRAPHIC(", 11) == 0 ||
      strncmp(c_type, "CHAR(", 5) == 0 || strncmp(c_type, "VARCHAR(", 8) == 0) {
    *_out_val = c_type;
    return C_ORM_OK;
  }
  if (strcmp(c_type, "c_orm_decimal_t") == 0 ||
      strcmp(c_type, "decimal") == 0 ||
      strcmp(c_type, "C_ORM_TYPE_DECIMAL") == 0) {
    *_out_val = "DECIMAL(18, 2)";
    return C_ORM_OK;
  }
  if (strcmp(c_type, "decfloat") == 0 ||
      strcmp(c_type, "C_ORM_TYPE_DECFLOAT") == 0) {
    *_out_val =
        (dialect == C_TO_SQL_DIALECT_DB2) ? "DECFLOAT(34)" : "DECIMAL(34, 8)";
    return C_ORM_OK;
  }
  if (strcmp(c_type, "char_fixed") == 0 ||
      strcmp(c_type, "C_ORM_TYPE_CHAR_FIXED") == 0) {
    *_out_val = "CHAR(32)";
    return C_ORM_OK;
  }
  if (strcmp(c_type, "graphic") == 0 ||
      strcmp(c_type, "C_ORM_TYPE_GRAPHIC") == 0) {
    *_out_val = (dialect == C_TO_SQL_DIALECT_DB2) ? "GRAPHIC(32)" : "CHAR(32)";
    return C_ORM_OK;
  }
  if (strcmp(c_type, "vargraphic") == 0 ||
      strcmp(c_type, "C_ORM_TYPE_VARGRAPHIC") == 0) {
    *_out_val =
        (dialect == C_TO_SQL_DIALECT_DB2) ? "VARGRAPHIC(255)" : "VARCHAR(255)";
    return C_ORM_OK;
  }
  if (strcmp(c_type, "binary_fixed") == 0 ||
      strcmp(c_type, "C_ORM_TYPE_BINARY_FIXED") == 0) {
    if (dialect == C_TO_SQL_DIALECT_DB2) {
      *_out_val = "CHAR(16) FOR BIT DATA";
    } else if (dialect == C_TO_SQL_DIALECT_ODBC) {
      *_out_val = "BINARY(16)";
    } else {
      *_out_val = "BLOB";
    }
    return C_ORM_OK;
  }
  if (strcmp(c_type, "varbinary") == 0 ||
      strcmp(c_type, "C_ORM_TYPE_VARBINARY") == 0) {
    if (dialect == C_TO_SQL_DIALECT_DB2) {
      *_out_val = "VARCHAR(255) FOR BIT DATA";
    } else if (dialect == C_TO_SQL_DIALECT_ODBC) {
      *_out_val = "VARBINARY(255)";
    } else {
      *_out_val = "BLOB";
    }
    return C_ORM_OK;
  }
  if (strcmp(c_type, "clob") == 0 || strcmp(c_type, "CLOB") == 0) {
    *_out_val = (dialect == C_TO_SQL_DIALECT_DB2) ? "CLOB" : "TEXT";
    return C_ORM_OK;
  }
  if (strcmp(c_type, "date") == 0 || strcmp(c_type, "C_ORM_TYPE_DATE") == 0) {
    *_out_val = "DATE";
    return C_ORM_OK;
  }
  if (strcmp(c_type, "time") == 0 || strcmp(c_type, "C_ORM_TYPE_TIME") == 0) {
    *_out_val = "TIME";
    return C_ORM_OK;
  }
  if (strcmp(c_type, "timestamp") == 0 ||
      strcmp(c_type, "C_ORM_TYPE_TIMESTAMP") == 0) {
    *_out_val = "TIMESTAMP";
    return C_ORM_OK;
  }
  if (strcmp(c_type, "string") == 0 || strcmp(c_type, "char*") == 0 ||
      strcmp(c_type, "const char*") == 0 ||
      strcmp(c_type, "C_ORM_TYPE_STRING") == 0) {
    *_out_val =
        (dialect == C_TO_SQL_DIALECT_MYSQL || dialect == C_TO_SQL_DIALECT_DB2 ||
         dialect == C_TO_SQL_DIALECT_ODBC)
            ? "VARCHAR(255)"
            : "TEXT";
    return C_ORM_OK;
  }
  if (strcmp(c_type, "number") == 0 || strcmp(c_type, "double") == 0 ||
      strcmp(c_type, "float") == 0 || strcmp(c_type, "C_ORM_TYPE_FLOAT") == 0 ||
      strcmp(c_type, "C_ORM_TYPE_DOUBLE") == 0) {
    if (dialect == C_TO_SQL_DIALECT_POSTGRESQL) {
      *_out_val = "DOUBLE PRECISION";
      return C_ORM_OK;
    }
    if (dialect == C_TO_SQL_DIALECT_MYSQL || dialect == C_TO_SQL_DIALECT_DB2 ||
        dialect == C_TO_SQL_DIALECT_ODBC) {
      *_out_val = "DOUBLE";
      return C_ORM_OK;
    }
    *_out_val = "REAL";
    return C_ORM_OK;
  }
  if (strcmp(c_type, "boolean") == 0 || strcmp(c_type, "bool") == 0 ||
      strcmp(c_type, "C_ORM_TYPE_BOOL") == 0) {
    if (dialect == C_TO_SQL_DIALECT_POSTGRESQL) {
      *_out_val = "BOOLEAN";
      return C_ORM_OK;
    }
    if (dialect == C_TO_SQL_DIALECT_MYSQL) {
      *_out_val = "TINYINT(1)";
      return C_ORM_OK;
    }
    if (dialect == C_TO_SQL_DIALECT_DB2) {
      *_out_val = "SMALLINT";
      return C_ORM_OK;
    }
    *_out_val = "INTEGER";
    return C_ORM_OK;
  }
  *_out_val = "BLOB";
  return C_ORM_OK;
}

/**
 * @brief Appends a bounded substring to a string builder.
 * @param sb Target string builder.
 * @param s Source character pointer.
 * @param len Number of bytes from s to append.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on allocation failure.
 */
static c_orm_error_t sb_append_n(c_orm_string_builder_t *sb, const char *s,
                                 size_t len) {
  char *tmp = NULL;
  c_orm_error_t rc;

  rc = c_orm_system_malloc(len + 1, (void **)&tmp);
  if (rc != C_ORM_OK) {
    return C_ORM_ERROR_MEMORY;
  }
  memcpy(tmp, s, len);
  tmp[len] = '\0';
  rc = c_orm_string_builder_append(sb, tmp);
  (void)c_orm_system_free(tmp);
  return rc;
}

/**
 * @brief Detaches a heap-allocated copy of a string builder's buffer and frees
 * the builder.
 * @param sb String builder to finalize and free.
 * @param out_str Output pointer receiving duplicated string.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t sb_finish_into(c_orm_string_builder_t *sb,
                                    char **out_str) {
  const char *raw = NULL;
  c_orm_error_t rc;

  rc = c_orm_string_builder_get(sb, &raw);
  if (rc != C_ORM_OK) {
    (void)c_orm_string_builder_free(sb);
    return rc;
  }
  rc = C_ORM_STRDUP(raw, out_str);
  (void)c_orm_string_builder_free(sb);
  return rc;
}

/**
 * @brief Generates a CREATE TABLE SQL statement from C struct field metadata.
 * @param fp Destination file stream to write the SQL DDL statement to.
 * @param table_name Name of the SQL table to create.
 * @param sf Struct field collection describing table columns and constraints.
 * @param dialect Target SQL dialect for column types and identity syntax.
 * @return C_ORM_OK on success, or an error code on invalid arguments/types.
 */
C_ORM_EXPORT c_orm_error_t write_struct_to_sql_create_table(
    FILE *fp, const char *table_name, const struct StructFields *sf,
    c_to_sql_dialect_t dialect) {
  const char *sql_type = NULL;
  size_t i;

  if (!fp || !table_name || !sf) {
    return C_ORM_ERROR_NULL_POINTER;
  }

  fprintf(fp, "CREATE TABLE %s (\n", table_name);

  for (i = 0; i < sf->size; ++i) {
    const struct StructField *f = &sf->fields[i];
    int is_pk = 0;
    int is_unique = 0;
    int is_notnull = f->required;

    (void)c_type_to_sql_type(f->type, dialect, &sql_type);

    if (strcmp(f->name, "id") == 0 || strcmp(f->name, "ID") == 0 ||
        strcmp(f->name, "Id") == 0) {
      is_pk = 1;
    }

    if (f->description[0] != '\0') {
      if (strstr(f->description, "@primary_key") ||
          strstr(f->description, "@pk")) {
        is_pk = 1;
      }
      if (strstr(f->description, "@unique")) {
        is_unique = 1;
      }
      if (strstr(f->description, "@notnull") ||
          strstr(f->description, "@required")) {
        is_notnull = 1;
      }
    }

    fprintf(fp, "    %s %s", f->name, sql_type);

    if (is_pk) {
      if (dialect == C_TO_SQL_DIALECT_DB2 &&
          (strcmp(sql_type, "INTEGER") == 0 ||
           strcmp(sql_type, "BIGINT") == 0)) {
        fprintf(fp, " GENERATED BY DEFAULT AS IDENTITY NOT NULL PRIMARY KEY");
        is_notnull = 0;
      } else {
        fprintf(fp, " PRIMARY KEY");
        if (dialect == C_TO_SQL_DIALECT_SQLITE &&
            strcmp(sql_type, "INTEGER") == 0) {
          fprintf(fp, " AUTOINCREMENT");
        }
      }
    }
    if (is_unique) {
      fprintf(fp, " UNIQUE");
    }
    if (is_notnull) {
      fprintf(fp, " NOT NULL");
    }

    /* Check foreign key heuristic: ends with _id */
    {
      size_t len = strlen(f->name);
      if (len > 3 && strcmp(f->name + len - 3, "_id") == 0) {
        fprintf(fp, " REFERENCES %.*s(id)", (int)(len - 3), f->name);
      }
    }

    if (i + 1 < sf->size) {
      fprintf(fp, ",\n");
    } else {
      fprintf(fp, "\n");
    }
  }

  fprintf(fp, ");\n");
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t cdd_c_meta_to_sql_create_table(
    const cdd_c_meta_t *meta, c_to_sql_dialect_t dialect, char **out_sql) {
  c_orm_string_builder_t *sb = NULL;
  size_t i;
  c_orm_error_t rc;

  if (!meta || !out_sql || !meta->name) {
    return C_ORM_ERROR_NULL_POINTER;
  }

  *out_sql = NULL;
  rc = c_orm_string_builder_init(&sb);
  if (rc != C_ORM_OK) {
    return rc;
  }

  rc = c_orm_string_builder_append(sb, "CREATE TABLE ");
  if (rc == C_ORM_OK)
    rc = c_orm_string_builder_append(sb, meta->name);
  if (rc == C_ORM_OK)
    rc = c_orm_string_builder_append(sb, " (\n");
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }

  for (i = 0; i < meta->num_props; ++i) {
    const cdd_c_prop_meta_t *p = &meta->props[i];
    const char *sql_type = NULL;
    int is_pk = (strcmp(p->name, "id") == 0);

    rc = c_type_to_sql_type(p->type, dialect, &sql_type);
    if (rc == C_ORM_OK)
      rc = c_orm_string_builder_append(sb, "    ");
    if (rc == C_ORM_OK)
      rc = c_orm_string_builder_append(sb, p->name);
    if (rc == C_ORM_OK)
      rc = c_orm_string_builder_append(sb, " ");
    if (rc == C_ORM_OK)
      rc = c_orm_string_builder_append(sb, sql_type);

    if (rc == C_ORM_OK && is_pk) {
      if (dialect == C_TO_SQL_DIALECT_DB2 &&
          (strcmp(sql_type, "INTEGER") == 0 ||
           strcmp(sql_type, "BIGINT") == 0)) {
        rc = c_orm_string_builder_append(
            sb, " GENERATED BY DEFAULT AS IDENTITY NOT NULL PRIMARY KEY");
      } else {
        rc = c_orm_string_builder_append(sb, " PRIMARY KEY");
        if (rc == C_ORM_OK && dialect == C_TO_SQL_DIALECT_SQLITE &&
            strcmp(sql_type, "INTEGER") == 0) {
          rc = c_orm_string_builder_append(sb, " AUTOINCREMENT");
        }
      }
    }

    if (rc == C_ORM_OK) {
      size_t len = strlen(p->name);
      if (len > 3 && strcmp(p->name + len - 3, "_id") == 0) {
        rc = c_orm_string_builder_append(sb, " REFERENCES ");
        if (rc == C_ORM_OK)
          rc = sb_append_n(sb, p->name, len - 3);
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(sb, "(id)");
      }
    }

    if (rc == C_ORM_OK) {
      rc = c_orm_string_builder_append(sb, (i + 1 < meta->num_props) ? ",\n"
                                                                     : "\n");
    }

    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }
  }

  rc = c_orm_string_builder_append(sb, ");\n");
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }

  return sb_finish_into(sb, out_sql);
}

C_ORM_EXPORT c_orm_error_t cdd_c_meta_diff(const cdd_c_meta_t *old_schema,
                                           const cdd_c_meta_t *new_schema,
                                           cdd_c_meta_diff_t *out_diff) {
  size_t i;
  size_t j;
  c_orm_error_t rc;

  if (!old_schema || !new_schema || !out_diff) {
    return C_ORM_ERROR_NULL_POINTER;
  }

  memset(out_diff, 0, sizeof(*out_diff));

  /* Find added and altered */
  for (i = 0; i < new_schema->num_props; ++i) {
    const cdd_c_prop_meta_t *np = &new_schema->props[i];
    int found = 0;
    for (j = 0; j < old_schema->num_props; ++j) {
      const cdd_c_prop_meta_t *op = &old_schema->props[j];
      if (strcmp(np->name, op->name) == 0) {
        found = 1;
        if (strcmp(np->type, op->type) != 0) {
          cdd_c_prop_meta_t *tmp = NULL;
          rc = c_orm_system_realloc(out_diff->altered_props,
                                    (out_diff->num_altered + 1) *
                                        sizeof(cdd_c_prop_meta_t),
                                    (void **)&tmp);
          if (rc != C_ORM_OK) {
            (void)cdd_c_meta_diff_free(out_diff);
            return C_ORM_ERROR_MEMORY;
          }
          out_diff->altered_props = tmp;
          out_diff->altered_props[out_diff->num_altered++] = *np;
        }
        break;
      }
    }
    if (!found) {
      cdd_c_prop_meta_t *tmp = NULL;
      rc = c_orm_system_realloc(
          out_diff->added_props,
          (out_diff->num_added + 1) * sizeof(cdd_c_prop_meta_t), (void **)&tmp);
      if (rc != C_ORM_OK) {
        (void)cdd_c_meta_diff_free(out_diff);
        return C_ORM_ERROR_MEMORY;
      }
      out_diff->added_props = tmp;
      out_diff->added_props[out_diff->num_added++] = *np;
    }
  }

  /* Find dropped */
  for (j = 0; j < old_schema->num_props; ++j) {
    const cdd_c_prop_meta_t *op = &old_schema->props[j];
    int found = 0;
    for (i = 0; i < new_schema->num_props; ++i) {
      if (strcmp(op->name, new_schema->props[i].name) == 0) {
        found = 1;
        break;
      }
    }
    if (!found) {
      cdd_c_prop_meta_t *tmp = NULL;
      rc = c_orm_system_realloc(out_diff->dropped_props,
                                (out_diff->num_dropped + 1) *
                                    sizeof(cdd_c_prop_meta_t),
                                (void **)&tmp);
      if (rc != C_ORM_OK) {
        (void)cdd_c_meta_diff_free(out_diff);
        return C_ORM_ERROR_MEMORY;
      }
      out_diff->dropped_props = tmp;
      out_diff->dropped_props[out_diff->num_dropped++] = *op;
    }
  }

  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t cdd_c_meta_diff_to_sql(const char *table_name,
                                                  const cdd_c_meta_diff_t *diff,
                                                  c_to_sql_dialect_t dialect,
                                                  char **up_sql,
                                                  char **down_sql) {
  c_orm_string_builder_t *up_sb = NULL;
  c_orm_string_builder_t *down_sb = NULL;
  size_t i;
  c_orm_error_t rc;

  if (!table_name || !diff || !up_sql || !down_sql) {
    return C_ORM_ERROR_NULL_POINTER;
  }

  *up_sql = NULL;
  *down_sql = NULL;

  rc = c_orm_string_builder_init(&up_sb);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = c_orm_string_builder_init(&down_sb);
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(up_sb);
    return rc;
  }

  /* Added columns -> UP: ADD COLUMN, DOWN: DROP COLUMN */
  for (i = 0; i < diff->num_added; ++i) {
    const cdd_c_prop_meta_t *p = &diff->added_props[i];
    const char *sql_type = NULL;

    rc = c_type_to_sql_type(p->type, dialect, &sql_type);
    if (rc == C_ORM_OK) {
      if (dialect == C_TO_SQL_DIALECT_DB2) {
        rc = c_orm_string_builder_append(up_sb, "ALTER TABLE ");
        if (rc == C_ORM_OK)
          rc = c_orm_quote_qualified_identifier(up_sb, table_name, '"');
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, " ADD COLUMN ");
        if (rc == C_ORM_OK)
          rc = c_orm_quote_identifier(up_sb, p->name, '"');
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, " ");
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, sql_type);
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, ";\n");

        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(down_sb, "ALTER TABLE ");
        if (rc == C_ORM_OK)
          rc = c_orm_quote_qualified_identifier(down_sb, table_name, '"');
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(down_sb, " DROP COLUMN ");
        if (rc == C_ORM_OK)
          rc = c_orm_quote_identifier(down_sb, p->name, '"');
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(
              down_sb, ";\n-- CALL SYSPROC.ADMIN_CMD('REORG TABLE ');\n");
      } else {
        rc = c_orm_string_builder_append(up_sb, "ALTER TABLE ");
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, table_name);
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, " ADD COLUMN ");
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, p->name);
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, " ");
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, sql_type);
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, ";\n");

        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(down_sb, "ALTER TABLE ");
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(down_sb, table_name);
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(down_sb, " DROP COLUMN ");
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(down_sb, p->name);
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(down_sb, ";\n");
      }
    }

    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(up_sb);
      c_orm_string_builder_free(down_sb);
      return rc;
    }
  }

  /* Dropped columns -> UP: DROP COLUMN, DOWN: comment warning */
  for (i = 0; i < diff->num_dropped; ++i) {
    const cdd_c_prop_meta_t *p = &diff->dropped_props[i];

    if (dialect == C_TO_SQL_DIALECT_DB2) {
      rc = c_orm_string_builder_append(up_sb, "ALTER TABLE ");
      if (rc == C_ORM_OK)
        rc = c_orm_quote_qualified_identifier(up_sb, table_name, '"');
      if (rc == C_ORM_OK)
        rc = c_orm_string_builder_append(up_sb, " DROP COLUMN ");
      if (rc == C_ORM_OK)
        rc = c_orm_quote_identifier(up_sb, p->name, '"');
      if (rc == C_ORM_OK)
        rc = c_orm_string_builder_append(
            up_sb, ";\n-- CALL SYSPROC.ADMIN_CMD('REORG TABLE \"");
      if (rc == C_ORM_OK)
        rc = c_orm_string_builder_append(up_sb, table_name);
      if (rc == C_ORM_OK)
        rc = c_orm_string_builder_append(up_sb, "\"');\n");
    } else {
      rc = c_orm_string_builder_append(up_sb, "ALTER TABLE ");
      if (rc == C_ORM_OK)
        rc = c_orm_string_builder_append(up_sb, table_name);
      if (rc == C_ORM_OK)
        rc = c_orm_string_builder_append(up_sb, " DROP COLUMN ");
      if (rc == C_ORM_OK)
        rc = c_orm_string_builder_append(up_sb, p->name);
      if (rc == C_ORM_OK)
        rc = c_orm_string_builder_append(up_sb, ";\n");
    }

    if (rc == C_ORM_OK)
      rc = c_orm_string_builder_append(down_sb, "-- WARNING: Dropped column ");
    if (rc == C_ORM_OK)
      rc = c_orm_string_builder_append(down_sb, p->name);
    if (rc == C_ORM_OK)
      rc = c_orm_string_builder_append(down_sb, " cannot be restored safely\n");

    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(up_sb);
      c_orm_string_builder_free(down_sb);
      return rc;
    }
  }

  /* Altered columns */
  for (i = 0; i < diff->num_altered; ++i) {
    const cdd_c_prop_meta_t *p = &diff->altered_props[i];
    const char *sql_type = NULL;

    rc = c_type_to_sql_type(p->type, dialect, &sql_type);
    if (rc == C_ORM_OK) {
      if (dialect == C_TO_SQL_DIALECT_DB2) {
        rc = c_orm_string_builder_append(up_sb, "ALTER TABLE ");
        if (rc == C_ORM_OK)
          rc = c_orm_quote_qualified_identifier(up_sb, table_name, '"');
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, " ALTER COLUMN ");
        if (rc == C_ORM_OK)
          rc = c_orm_quote_identifier(up_sb, p->name, '"');
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, " SET DATA TYPE ");
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, sql_type);
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(
              up_sb, ";\n-- CALL SYSPROC.ADMIN_CMD('REORG TABLE \"");
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, table_name);
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, "\"');\n");
      } else if (dialect == C_TO_SQL_DIALECT_ODBC) {
        rc = c_orm_string_builder_append(up_sb, "ALTER TABLE ");
        if (rc == C_ORM_OK)
          rc = c_orm_quote_qualified_identifier(up_sb, table_name, '"');
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, " ALTER COLUMN ");
        if (rc == C_ORM_OK)
          rc = c_orm_quote_identifier(up_sb, p->name, '"');
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, " ");
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, sql_type);
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, ";\n");
      } else if (dialect == C_TO_SQL_DIALECT_POSTGRESQL) {
        rc = c_orm_string_builder_append(up_sb, "ALTER TABLE ");
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, table_name);
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, " ALTER COLUMN ");
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, p->name);
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, " TYPE ");
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, sql_type);
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, ";\n");
      } else if (dialect == C_TO_SQL_DIALECT_MYSQL) {
        rc = c_orm_string_builder_append(up_sb, "ALTER TABLE ");
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, table_name);
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, " MODIFY COLUMN ");
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, p->name);
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, " ");
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, sql_type);
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, ";\n");
      } else {
        rc = c_orm_string_builder_append(
            up_sb, "-- SQLite requires table recreation to alter column ");
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, p->name);
        if (rc == C_ORM_OK)
          rc = c_orm_string_builder_append(up_sb, "\n");
      }
    }

    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(up_sb);
      c_orm_string_builder_free(down_sb);
      return rc;
    }
  }

  rc = sb_finish_into(up_sb, up_sql);
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(down_sb);
    return rc;
  }
  rc = sb_finish_into(down_sb, down_sql);
  if (rc != C_ORM_OK) {
    (void)c_orm_system_free(*up_sql);
    *up_sql = NULL;
    return rc;
  }
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t cdd_c_meta_diff_free(cdd_c_meta_diff_t *diff) {
  if (!diff) {
    return C_ORM_OK;
  }
  if (diff->added_props) {
    (void)c_orm_system_free(diff->added_props);
  }
  if (diff->dropped_props) {
    (void)c_orm_system_free(diff->dropped_props);
  }
  if (diff->altered_props) {
    (void)c_orm_system_free(diff->altered_props);
  }
  memset(diff, 0, sizeof(*diff));
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t cdd_c_get_schema_inspection_query(
    c_to_sql_dialect_t dialect, const char *table_name, char **out_query) {
  c_orm_string_builder_t *sb = NULL;
  c_orm_error_t rc;

  if (!table_name || !out_query) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (dialect != C_TO_SQL_DIALECT_SQLITE &&
      dialect != C_TO_SQL_DIALECT_POSTGRESQL &&
      dialect != C_TO_SQL_DIALECT_MYSQL && dialect != C_TO_SQL_DIALECT_DB2 &&
      dialect != C_TO_SQL_DIALECT_ODBC) {
    return C_ORM_ERROR_VALIDATION;
  }

  *out_query = NULL;
  rc = c_orm_string_builder_init(&sb);
  if (rc != C_ORM_OK) {
    return rc;
  }

  if (dialect == C_TO_SQL_DIALECT_SQLITE) {
    rc = c_orm_string_builder_append(sb, "PRAGMA table_info(");
    if (rc == C_ORM_OK)
      rc = c_orm_string_builder_append(sb, table_name);
    if (rc == C_ORM_OK)
      rc = c_orm_string_builder_append(sb, ");");
  } else if (dialect == C_TO_SQL_DIALECT_POSTGRESQL) {
    rc = c_orm_string_builder_append(
        sb, "SELECT column_name, data_type, is_nullable FROM "
            "information_schema.columns WHERE table_name = '");
    if (rc == C_ORM_OK)
      rc = c_orm_string_builder_append(sb, table_name);
    if (rc == C_ORM_OK)
      rc = c_orm_string_builder_append(sb, "';");
  } else if (dialect == C_TO_SQL_DIALECT_DB2) {
    rc = c_orm_string_builder_append(
        sb,
        "SELECT COLNAME, TYPENAME, LENGTH, SCALE, NULLS FROM SYSCAT.COLUMNS "
        "WHERE TABNAME = ");
    if (rc == C_ORM_OK)
      rc = c_orm_append_string_literal(sb, table_name);
    if (rc == C_ORM_OK)
      rc = c_orm_string_builder_append(sb, " ORDER BY COLNO ASC;");
  } else if (dialect == C_TO_SQL_DIALECT_ODBC) {
    rc = c_orm_string_builder_append(
        sb, "SELECT COLUMN_NAME, DATA_TYPE, CHARACTER_MAXIMUM_LENGTH, "
            "NUMERIC_SCALE, IS_NULLABLE FROM INFORMATION_SCHEMA.COLUMNS WHERE "
            "TABLE_NAME = ");
    if (rc == C_ORM_OK)
      rc = c_orm_append_string_literal(sb, table_name);
    if (rc == C_ORM_OK)
      rc = c_orm_string_builder_append(sb, " ORDER BY ORDINAL_POSITION ASC;");
  } else {
    rc = c_orm_string_builder_append(sb, "SHOW COLUMNS FROM ");
    if (rc == C_ORM_OK)
      rc = c_orm_string_builder_append(sb, table_name);
    if (rc == C_ORM_OK)
      rc = c_orm_string_builder_append(sb, ";");
  }

  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }

  return sb_finish_into(sb, out_query);
}

C_ORM_EXPORT c_orm_error_t cdd_c_get_zos_schema_inspection_query(
    const char *table_name, char **out_query) {
  c_orm_string_builder_t *sb = NULL;
  c_orm_error_t rc;

  if (!table_name || !out_query) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  *out_query = NULL;
  rc = c_orm_string_builder_init(&sb);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = c_orm_string_builder_append(
      sb, "SELECT NAME, COLTYPE, LENGTH, SCALE, NULLS FROM SYSIBM.SYSCOLUMNS "
          "WHERE TBNAME = ");
  if (rc == C_ORM_OK) {
    rc = c_orm_append_string_literal(sb, table_name);
  }
  if (rc == C_ORM_OK) {
    rc = c_orm_string_builder_append(sb, " ORDER BY COLNO ASC;");
  }
  if (rc != C_ORM_OK) {
    (void)c_orm_string_builder_free(sb);
    return rc;
  }
  return sb_finish_into(sb, out_query);
}

C_ORM_EXPORT c_orm_error_t c_orm_table_meta_to_sql_create_table(
    const c_orm_table_meta_t *meta, c_to_sql_dialect_t dialect,
    char **out_sql) {
  c_orm_string_builder_t *sb = NULL;
  size_t i;
  c_orm_error_t rc;
  int quote_db2;

  if (!meta || !meta->name || !out_sql) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (meta->num_columns == 0 || !meta->columns) {
    return C_ORM_ERROR_VALIDATION;
  }

  *out_sql = NULL;
  rc = c_orm_string_builder_init(&sb);
  if (rc != C_ORM_OK) {
    return rc;
  }

  quote_db2 =
      (dialect == C_TO_SQL_DIALECT_DB2 || dialect == C_TO_SQL_DIALECT_ODBC);

  rc = c_orm_string_builder_append(sb, "CREATE TABLE ");
  if (rc == C_ORM_OK) {
    rc = quote_db2 ? c_orm_quote_qualified_identifier(sb, meta->name, '"')
                   : c_orm_string_builder_append(sb, meta->name);
  }
  if (rc == C_ORM_OK) {
    rc = c_orm_string_builder_append(sb, " (\n");
  }
  if (rc != C_ORM_OK) {
    (void)c_orm_string_builder_free(sb);
    return rc;
  }

  for (i = 0; i < meta->num_columns; ++i) {
    const c_orm_column_meta_t *col = &meta->columns[i];
    char type_buf[64];
    const char *type_str = "VARCHAR(255)";
    size_t flen = col->fixed_length > 0 ? col->fixed_length : 32;

    switch (col->type) {
    case C_ORM_TYPE_INT32:
      type_str = (dialect == C_TO_SQL_DIALECT_MYSQL) ? "INT" : "INTEGER";
      break;
    case C_ORM_TYPE_INT64:
      type_str = (dialect == C_TO_SQL_DIALECT_SQLITE) ? "INTEGER" : "BIGINT";
      break;
    case C_ORM_TYPE_BOOL:
      type_str = (dialect == C_TO_SQL_DIALECT_DB2)
                     ? "SMALLINT"
                     : ((dialect == C_TO_SQL_DIALECT_POSTGRESQL) ? "BOOLEAN"
                                                                 : "INTEGER");
      break;
    case C_ORM_TYPE_FLOAT:
      type_str = "REAL";
      break;
    case C_ORM_TYPE_DOUBLE:
      type_str = (dialect == C_TO_SQL_DIALECT_POSTGRESQL) ? "DOUBLE PRECISION"
                                                          : "DOUBLE";
      break;
    case C_ORM_TYPE_DECIMAL: {
      unsigned int p = col->precision > 0 ? (unsigned int)col->precision : 18U;
      unsigned int s = (unsigned int)col->scale;
      rc = C_ORM_SPRINTF(type_buf, sizeof(type_buf), "DECIMAL(%u, %u)", p, s);
      if (rc != C_ORM_OK) {
        (void)c_orm_string_builder_free(sb);
        return rc;
      }
      type_str = type_buf;
      break;
    }
    case C_ORM_TYPE_DECFLOAT: {
      unsigned int p = (col->precision == 16U || col->precision == 34U)
                           ? (unsigned int)col->precision
                           : 34U;
      if (dialect == C_TO_SQL_DIALECT_DB2) {
        rc = C_ORM_SPRINTF(type_buf, sizeof(type_buf), "DECFLOAT(%u)", p);
        if (rc != C_ORM_OK) {
          (void)c_orm_string_builder_free(sb);
          return rc;
        }
        type_str = type_buf;
      } else {
        type_str = "DECIMAL(34, 8)";
      }
      break;
    }
    case C_ORM_TYPE_CHAR_FIXED:
      rc = C_ORM_SPRINTF(type_buf, sizeof(type_buf),
                         "CHAR(" C_ORM_FMT_SIZE_T ")", C_ORM_CAST_SIZE_T(flen));
      if (rc != C_ORM_OK) {
        (void)c_orm_string_builder_free(sb);
        return rc;
      }
      type_str = type_buf;
      break;
    case C_ORM_TYPE_GRAPHIC:
      rc = C_ORM_SPRINTF(type_buf, sizeof(type_buf),
                         (dialect == C_TO_SQL_DIALECT_DB2)
                             ? "GRAPHIC(" C_ORM_FMT_SIZE_T ")"
                             : "CHAR(" C_ORM_FMT_SIZE_T ")",
                         C_ORM_CAST_SIZE_T(flen));
      if (rc != C_ORM_OK) {
        (void)c_orm_string_builder_free(sb);
        return rc;
      }
      type_str = type_buf;
      break;
    case C_ORM_TYPE_VARGRAPHIC:
      flen = col->fixed_length > 0 ? col->fixed_length : 255;
      rc = C_ORM_SPRINTF(type_buf, sizeof(type_buf),
                         (dialect == C_TO_SQL_DIALECT_DB2)
                             ? "VARGRAPHIC(" C_ORM_FMT_SIZE_T ")"
                             : "VARCHAR(" C_ORM_FMT_SIZE_T ")",
                         C_ORM_CAST_SIZE_T(flen));
      if (rc != C_ORM_OK) {
        (void)c_orm_string_builder_free(sb);
        return rc;
      }
      type_str = type_buf;
      break;
    case C_ORM_TYPE_BINARY_FIXED:
      flen = col->fixed_length > 0 ? col->fixed_length : 16;
      if (dialect == C_TO_SQL_DIALECT_DB2) {
        rc = C_ORM_SPRINTF(type_buf, sizeof(type_buf),
                           "CHAR(" C_ORM_FMT_SIZE_T ") FOR BIT DATA",
                           C_ORM_CAST_SIZE_T(flen));
      } else if (dialect == C_TO_SQL_DIALECT_ODBC) {
        rc = C_ORM_SPRINTF(type_buf, sizeof(type_buf),
                           "BINARY(" C_ORM_FMT_SIZE_T ")",
                           C_ORM_CAST_SIZE_T(flen));
      } else {
        rc = C_ORM_STRCPY(type_buf, sizeof(type_buf), "BLOB");
      }
      if (rc != C_ORM_OK) {
        (void)c_orm_string_builder_free(sb);
        return rc;
      }
      type_str = type_buf;
      break;
    case C_ORM_TYPE_VARBINARY:
      flen = col->fixed_length > 0 ? col->fixed_length : 255;
      if (dialect == C_TO_SQL_DIALECT_DB2) {
        rc = C_ORM_SPRINTF(type_buf, sizeof(type_buf),
                           "VARCHAR(" C_ORM_FMT_SIZE_T ") FOR BIT DATA",
                           C_ORM_CAST_SIZE_T(flen));
      } else if (dialect == C_TO_SQL_DIALECT_ODBC) {
        rc = C_ORM_SPRINTF(type_buf, sizeof(type_buf),
                           "VARBINARY(" C_ORM_FMT_SIZE_T ")",
                           C_ORM_CAST_SIZE_T(flen));
      } else {
        rc = C_ORM_STRCPY(type_buf, sizeof(type_buf), "BLOB");
      }
      if (rc != C_ORM_OK) {
        (void)c_orm_string_builder_free(sb);
        return rc;
      }
      type_str = type_buf;
      break;
    case C_ORM_TYPE_BLOB:
      type_str = "BLOB";
      break;
    case C_ORM_TYPE_DATE:
      type_str = "DATE";
      break;
    case C_ORM_TYPE_TIME:
      type_str = "TIME";
      break;
    case C_ORM_TYPE_TIMESTAMP:
      type_str = "TIMESTAMP";
      break;
    default:
      if (col->fixed_length > 0) {
        rc = C_ORM_SPRINTF(type_buf, sizeof(type_buf),
                           "VARCHAR(" C_ORM_FMT_SIZE_T ")",
                           C_ORM_CAST_SIZE_T(col->fixed_length));
        if (rc != C_ORM_OK) {
          (void)c_orm_string_builder_free(sb);
          return rc;
        }
        type_str = type_buf;
      } else {
        type_str = (dialect == C_TO_SQL_DIALECT_SQLITE ||
                    dialect == C_TO_SQL_DIALECT_POSTGRESQL)
                       ? "TEXT"
                       : "VARCHAR(255)";
      }
      break;
    }

    if ((rc = c_orm_string_builder_append(sb, "    ")) != C_ORM_OK) {
      (void)c_orm_string_builder_free(sb);
      return rc;
    }
    rc = quote_db2 ? c_orm_quote_identifier(sb, col->name, '"')
                   : c_orm_string_builder_append(sb, col->name);
    if (rc != C_ORM_OK ||
        (rc = c_orm_string_builder_append(sb, " ")) != C_ORM_OK ||
        (rc = c_orm_string_builder_append(sb, type_str)) != C_ORM_OK) {
      (void)c_orm_string_builder_free(sb);
      return rc;
    }

    if (col->is_pk) {
      if (dialect == C_TO_SQL_DIALECT_DB2 &&
          (col->type == C_ORM_TYPE_INT32 || col->type == C_ORM_TYPE_INT64)) {
        rc = c_orm_string_builder_append(
            sb, " GENERATED BY DEFAULT AS IDENTITY NOT NULL PRIMARY KEY");
      } else {
        rc = c_orm_string_builder_append(sb, " NOT NULL PRIMARY KEY");
      }
      if (rc != C_ORM_OK) {
        (void)c_orm_string_builder_free(sb);
        return rc;
      }
    } else if (!col->is_nullable) {
      rc = c_orm_string_builder_append(sb, " NOT NULL");
      if (rc != C_ORM_OK) {
        (void)c_orm_string_builder_free(sb);
        return rc;
      }
    }

    if ((rc = c_orm_string_builder_append(
             sb, (i + 1 < meta->num_columns) ? ",\n" : "\n")) != C_ORM_OK) {
      (void)c_orm_string_builder_free(sb);
      return rc;
    }
  }

  if ((rc = c_orm_string_builder_append(sb, ");\n")) != C_ORM_OK) {
    (void)c_orm_string_builder_free(sb);
    return rc;
  }

  return sb_finish_into(sb, out_sql);
}

C_ORM_EXPORT c_orm_error_t cdd_c_emit_create_index(const char *table_name,
                                                   const char *index_name,
                                                   const char *column_name,
                                                   int is_unique,
                                                   char **out_sql) {
  c_orm_string_builder_t *sb = NULL;
  c_orm_error_t rc;

  if (!table_name || !index_name || !column_name || !out_sql) {
    return C_ORM_ERROR_NULL_POINTER;
  }

  *out_sql = NULL;
  rc = c_orm_string_builder_init(&sb);
  if (rc != C_ORM_OK) {
    return rc;
  }

  rc = c_orm_string_builder_append(sb, is_unique ? "CREATE UNIQUE INDEX "
                                                 : "CREATE INDEX ");
  if (rc == C_ORM_OK)
    rc = c_orm_string_builder_append(sb, index_name);
  if (rc == C_ORM_OK)
    rc = c_orm_string_builder_append(sb, " ON ");
  if (rc == C_ORM_OK)
    rc = c_orm_string_builder_append(sb, table_name);
  if (rc == C_ORM_OK)
    rc = c_orm_string_builder_append(sb, " (");
  if (rc == C_ORM_OK)
    rc = c_orm_string_builder_append(sb, column_name);
  if (rc == C_ORM_OK)
    rc = c_orm_string_builder_append(sb, ");");

  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }
  return sb_finish_into(sb, out_sql);
}

C_ORM_EXPORT c_orm_error_t cdd_c_emit_drop_index(const char *index_name,
                                                 char **out_sql) {
  c_orm_string_builder_t *sb = NULL;
  c_orm_error_t rc;

  if (!index_name || !out_sql) {
    return C_ORM_ERROR_NULL_POINTER;
  }

  *out_sql = NULL;
  rc = c_orm_string_builder_init(&sb);
  if (rc != C_ORM_OK) {
    return rc;
  }

  rc = c_orm_string_builder_append(sb, "DROP INDEX ");
  if (rc == C_ORM_OK)
    rc = c_orm_string_builder_append(sb, index_name);
  if (rc == C_ORM_OK)
    rc = c_orm_string_builder_append(sb, ";");

  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }
  return sb_finish_into(sb, out_sql);
}

C_ORM_EXPORT c_orm_error_t
cdd_c_meta_topological_sort(const cdd_c_meta_t **schemas, size_t num_schemas,
                            const cdd_c_meta_t **out_schemas) {
  int *in_degree = NULL;
  size_t *queue = NULL;
  size_t head = 0;
  size_t tail = 0;
  size_t out_idx = 0;
  size_t i;
  size_t j;
  size_t k;
  c_orm_error_t rc;

  if (!schemas || !out_schemas || num_schemas == 0) {
    return C_ORM_ERROR_NULL_POINTER;
  }

  rc = c_orm_system_calloc(num_schemas, sizeof(int), (void **)&in_degree);
  if (rc != C_ORM_OK) {
    return C_ORM_ERROR_MEMORY;
  }

  rc = c_orm_system_calloc(num_schemas, sizeof(size_t), (void **)&queue);
  if (rc != C_ORM_OK) {
    (void)c_orm_system_free(in_degree);
    return C_ORM_ERROR_MEMORY;
  }

  /* Calculate in-degrees without fixed-size stack buffers */
  for (i = 0; i < num_schemas; i++) {
    const cdd_c_meta_t *s = schemas[i];
    for (j = 0; j < s->num_props; j++) {
      const char *col = s->props[j].name;
      size_t len = strlen(col);
      if (len > 3 && strcmp(col + len - 3, "_id") == 0) {
        size_t prefix_len = len - 3;
        for (k = 0; k < num_schemas; k++) {
          if (strncmp(schemas[k]->name, col, prefix_len) == 0 &&
              schemas[k]->name[prefix_len] == '\0') {
            in_degree[i]++;
            break;
          }
        }
      }
    }
  }

  /* Enqueue 0 in-degree nodes */
  for (i = 0; i < num_schemas; i++) {
    if (in_degree[i] == 0) {
      queue[tail++] = i;
    }
  }

  /* Kahn's algorithm */
  while (head < tail) {
    size_t u = queue[head++];
    size_t u_len = strlen(schemas[u]->name);
    out_schemas[out_idx++] = schemas[u];

    for (i = 0; i < num_schemas; i++) {
      const cdd_c_meta_t *s = schemas[i];
      for (j = 0; j < s->num_props; j++) {
        const char *col = s->props[j].name;
        size_t len = strlen(col);
        if (len == u_len + 3 && strcmp(col + len - 3, "_id") == 0 &&
            strncmp(col, schemas[u]->name, u_len) == 0) {
          in_degree[i]--;
          if (in_degree[i] == 0) {
            queue[tail++] = i;
          }
          break;
        }
      }
    }
  }

  (void)c_orm_system_free(in_degree);
  (void)c_orm_system_free(queue);

  if (out_idx != num_schemas) {
    return C_ORM_ERROR_CYCLE;
  }

  return C_ORM_OK;
}

/**
 * @brief Maps a `c_orm_type_t` column type to its canonical `cdd_c_prop_meta_t`
 * type string.
 *
 * @param col_type Source `c_orm_type_t` enum.
 * @param out_str Output pointer receiving static string literal.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t map_col_type_to_cdd_str(c_orm_type_t col_type,
                                             const char **out_str) {
  switch (col_type) {
  case C_ORM_TYPE_INT32:
  case C_ORM_TYPE_BOOL:
    *out_str = "C_ORM_TYPE_INT32";
    break;
  case C_ORM_TYPE_INT64:
    *out_str = "C_ORM_TYPE_INT64";
    break;
  case C_ORM_TYPE_FLOAT:
    *out_str = "C_ORM_TYPE_FLOAT";
    break;
  case C_ORM_TYPE_DOUBLE:
    *out_str = "C_ORM_TYPE_DOUBLE";
    break;
  case C_ORM_TYPE_DECIMAL:
    *out_str = "C_ORM_TYPE_DECIMAL";
    break;
  case C_ORM_TYPE_DECFLOAT:
    *out_str = "C_ORM_TYPE_DECFLOAT";
    break;
  case C_ORM_TYPE_CHAR_FIXED:
    *out_str = "C_ORM_TYPE_CHAR_FIXED";
    break;
  case C_ORM_TYPE_GRAPHIC:
    *out_str = "C_ORM_TYPE_GRAPHIC";
    break;
  case C_ORM_TYPE_VARGRAPHIC:
    *out_str = "C_ORM_TYPE_VARGRAPHIC";
    break;
  case C_ORM_TYPE_BINARY_FIXED:
    *out_str = "C_ORM_TYPE_BINARY_FIXED";
    break;
  case C_ORM_TYPE_VARBINARY:
    *out_str = "C_ORM_TYPE_VARBINARY";
    break;
  case C_ORM_TYPE_BLOB:
    *out_str = "C_ORM_TYPE_BLOB";
    break;
  case C_ORM_TYPE_DATE:
    *out_str = "C_ORM_TYPE_DATE";
    break;
  case C_ORM_TYPE_TIME:
    *out_str = "C_ORM_TYPE_TIME";
    break;
  case C_ORM_TYPE_TIMESTAMP:
    *out_str = "C_ORM_TYPE_TIMESTAMP";
    break;
  default:
    *out_str = "C_ORM_TYPE_STRING";
    break;
  }
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_table_meta_to_cdd_meta(
    const c_orm_table_meta_t *table_meta, cdd_c_meta_t **out_cdd_meta) {
  cdd_c_meta_t *cm = NULL;
  cdd_c_prop_meta_t *props = NULL;
  size_t i;
  c_orm_error_t rc;

  if (!table_meta || !out_cdd_meta) {
    return C_ORM_ERROR_VALIDATION;
  }

  *out_cdd_meta = NULL;
  rc = c_orm_system_calloc(1, sizeof(cdd_c_meta_t), (void **)&cm);
  if (rc != C_ORM_OK) {
    return C_ORM_ERROR_MEMORY;
  }

  cm->name = table_meta->name;
  cm->size = table_meta->struct_size;
  cm->num_props = table_meta->num_columns;

  if (table_meta->num_columns > 0) {
    rc = c_orm_system_calloc(table_meta->num_columns, sizeof(cdd_c_prop_meta_t),
                             (void **)&props);
    if (rc != C_ORM_OK) {
      (void)c_orm_system_free(cm);
      return C_ORM_ERROR_MEMORY;
    }
    for (i = 0; i < table_meta->num_columns; i++) {
      props[i].name = table_meta->columns[i].name;
      (void)map_col_type_to_cdd_str(table_meta->columns[i].type,
                                    &props[i].type);
      props[i].offset = table_meta->columns[i].offset;
      props[i].is_secure = table_meta->columns[i].is_secure ? 1 : 0;
    }
    cm->props = props;
  }

  *out_cdd_meta = cm;
  return C_ORM_OK;
}

/**
 * @brief Maps a `cdd_c_prop_meta_t` type string to `c_orm_type_t`.
 *
 * @param type_str Source type string.
 * @param out_type Output `c_orm_type_t`.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t map_cdd_str_to_col_type(const char *type_str,
                                             c_orm_type_t *out_type) {
  if (!type_str) {
    *out_type = C_ORM_TYPE_STRING;
    return C_ORM_OK;
  }
  if (strcmp(type_str, "C_ORM_TYPE_INT32") == 0 ||
      strcmp(type_str, "int") == 0 || strcmp(type_str, "int32_t") == 0) {
    *out_type = C_ORM_TYPE_INT32;
  } else if (strcmp(type_str, "C_ORM_TYPE_INT64") == 0 ||
             strcmp(type_str, "int64_t") == 0 ||
             strcmp(type_str, "bigint") == 0) {
    *out_type = C_ORM_TYPE_INT64;
  } else if (strcmp(type_str, "C_ORM_TYPE_FLOAT") == 0 ||
             strcmp(type_str, "float") == 0) {
    *out_type = C_ORM_TYPE_FLOAT;
  } else if (strcmp(type_str, "C_ORM_TYPE_DOUBLE") == 0 ||
             strcmp(type_str, "double") == 0) {
    *out_type = C_ORM_TYPE_DOUBLE;
  } else if (strcmp(type_str, "C_ORM_TYPE_BOOL") == 0 ||
             strcmp(type_str, "bool") == 0) {
    *out_type = C_ORM_TYPE_BOOL;
  } else if (strcmp(type_str, "C_ORM_TYPE_BLOB") == 0 ||
             strcmp(type_str, "blob") == 0) {
    *out_type = C_ORM_TYPE_BLOB;
  } else if (strcmp(type_str, "C_ORM_TYPE_DECIMAL") == 0 ||
             strcmp(type_str, "decimal") == 0 ||
             strcmp(type_str, "c_orm_decimal_t") == 0) {
    *out_type = C_ORM_TYPE_DECIMAL;
  } else if (strcmp(type_str, "C_ORM_TYPE_DECFLOAT") == 0 ||
             strcmp(type_str, "decfloat") == 0) {
    *out_type = C_ORM_TYPE_DECFLOAT;
  } else if (strcmp(type_str, "C_ORM_TYPE_CHAR_FIXED") == 0 ||
             strcmp(type_str, "char_fixed") == 0) {
    *out_type = C_ORM_TYPE_CHAR_FIXED;
  } else if (strcmp(type_str, "C_ORM_TYPE_GRAPHIC") == 0 ||
             strcmp(type_str, "graphic") == 0) {
    *out_type = C_ORM_TYPE_GRAPHIC;
  } else if (strcmp(type_str, "C_ORM_TYPE_VARGRAPHIC") == 0 ||
             strcmp(type_str, "vargraphic") == 0) {
    *out_type = C_ORM_TYPE_VARGRAPHIC;
  } else if (strcmp(type_str, "C_ORM_TYPE_BINARY_FIXED") == 0 ||
             strcmp(type_str, "binary_fixed") == 0) {
    *out_type = C_ORM_TYPE_BINARY_FIXED;
  } else if (strcmp(type_str, "C_ORM_TYPE_VARBINARY") == 0 ||
             strcmp(type_str, "varbinary") == 0) {
    *out_type = C_ORM_TYPE_VARBINARY;
  } else if (strcmp(type_str, "C_ORM_TYPE_DATE") == 0 ||
             strcmp(type_str, "date") == 0) {
    *out_type = C_ORM_TYPE_DATE;
  } else if (strcmp(type_str, "C_ORM_TYPE_TIME") == 0 ||
             strcmp(type_str, "time") == 0) {
    *out_type = C_ORM_TYPE_TIME;
  } else if (strcmp(type_str, "C_ORM_TYPE_TIMESTAMP") == 0 ||
             strcmp(type_str, "timestamp") == 0) {
    *out_type = C_ORM_TYPE_TIMESTAMP;
  } else {
    *out_type = C_ORM_TYPE_STRING;
  }
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_free_cdd_meta(cdd_c_meta_t *cdd_meta) {
  if (!cdd_meta) {
    return C_ORM_OK;
  }
  if (cdd_meta->props) {
    (void)c_orm_system_free((void *)(size_t)cdd_meta->props);
  }
  (void)c_orm_system_free(cdd_meta);
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_cdd_meta_to_table_meta(
    const cdd_c_meta_t *cdd_meta, c_orm_table_meta_t **out_table_meta) {
  c_orm_table_meta_t *tm = NULL;
  c_orm_column_meta_t *cols = NULL;
  size_t i;
  c_orm_error_t rc;

  if (!cdd_meta || !out_table_meta) {
    return C_ORM_ERROR_VALIDATION;
  }

  *out_table_meta = NULL;
  rc = c_orm_system_calloc(1, sizeof(c_orm_table_meta_t), (void **)&tm);
  if (rc != C_ORM_OK) {
    return C_ORM_ERROR_MEMORY;
  }

  tm->name = cdd_meta->name;
  tm->struct_size = cdd_meta->size;
  tm->num_columns = cdd_meta->num_props;

  if (cdd_meta->num_props > 0) {
    rc = c_orm_system_calloc(cdd_meta->num_props, sizeof(c_orm_column_meta_t),
                             (void **)&cols);
    if (rc != C_ORM_OK) {
      (void)c_orm_system_free(tm);
      return C_ORM_ERROR_MEMORY;
    }
    for (i = 0; i < cdd_meta->num_props; ++i) {
      cols[i].name = cdd_meta->props[i].name;
      (void)map_cdd_str_to_col_type(cdd_meta->props[i].type, &cols[i].type);
      cols[i].offset = cdd_meta->props[i].offset;
      cols[i].is_pk = (cdd_meta->props[i].name &&
                       strcmp(cdd_meta->props[i].name, "id") == 0)
                          ? true
                          : false;
      cols[i].is_nullable = !cols[i].is_pk;
      cols[i].is_secure = cdd_meta->props[i].is_secure ? true : false;
    }
    tm->columns = cols;
  }

  *out_table_meta = tm;
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t
c_orm_free_table_meta(c_orm_table_meta_t *table_meta) {
  if (!table_meta) {
    return C_ORM_OK;
  }
  if (table_meta->columns) {
    (void)c_orm_system_free((void *)(size_t)table_meta->columns);
  }
  (void)c_orm_system_free(table_meta);
  return C_ORM_OK;
}
