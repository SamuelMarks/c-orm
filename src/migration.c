/**
 * @file migration.c
 * @brief Implementation of SQL migration file parser.
 *
 * @author Samuel Marks
 */

/* clang-format off */
#include "migration.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "c_str_span.h"
#include "cfs/cfs.h"
#include "c89stringutils_string_extras.h"
#include "functions/parse/fs.h"
#include "c_orm_meta.h"
/* clang-format on */

/**
 * @brief Executes the migration statements init operation.
 */
c_orm_error_t migration_statements_init(struct MigrationStatements *out) {
  if (out) {
    out->up_statement = NULL;
    out->down_statement = NULL;
  }
  return C_ORM_OK;
}

/**
 * @brief Executes the migration statements free operation.
 */
c_orm_error_t migration_statements_free(struct MigrationStatements *out) {
  if (out) {
    if (out->up_statement) {
      C_ORM_FREE(out->up_statement);
      out->up_statement = NULL;
    }
    if (out->down_statement) {
      C_ORM_FREE(out->down_statement);
      out->down_statement = NULL;
    }
  }
  return C_ORM_OK;
}

/**
 * @brief Parses migration file from the given input.
 */
c_orm_error_t parse_migration_file(const char *filepath,
                                   struct MigrationStatements *out) {
  char *file_data;
  size_t file_size;
  c_orm_error_t rc;
  const char *up_marker;
  const char *down_marker;
  const char *up_start;
  const char *down_start;
  size_t up_len;
  size_t down_len;
  char *up_stmt;
  char *down_stmt;

  enum cdd_c_error cdd_rc;

  file_data = NULL;
  file_size = 0;
  up_marker = "-- UP";
  down_marker = "-- DOWN";
  up_start = NULL;
  down_start = NULL;
  up_len = 0;
  down_len = 0;
  up_stmt = NULL;
  down_stmt = NULL;

  if (!filepath || !out) {
    return EINVAL;
  }

  migration_statements_init(out);

  cdd_rc = read_to_file(filepath, "rb", &file_data, &file_size);
  if (cdd_rc != 0) {
    return (c_orm_error_t)cdd_rc;
  }

  if (file_size == 0 || !file_data) {
    if (file_data) {
      C_ORM_FREE(file_data);
    }
    return 0;
  }

  up_start = strstr(file_data, up_marker);
  down_start = strstr(file_data, down_marker);

  if (up_start && down_start) {
    if (up_start < down_start) {
      up_start += strlen(up_marker);
      up_len = (size_t)(down_start - up_start);

      down_start += strlen(down_marker);
      down_len = strlen(down_start);
    } else {
      down_start += strlen(down_marker);
      down_len = (size_t)(up_start - down_start);

      up_start += strlen(up_marker);
      up_len = strlen(up_start);
    }
  } else if (up_start && !down_start) {
    up_start += strlen(up_marker);
    up_len = strlen(up_start);
  } else if (!up_start && down_start) {
    down_start += strlen(down_marker);
    down_len = strlen(down_start);
  } else {
    /* No markers found, assume the whole file is UP */
    up_start = file_data;
    up_len = file_size;
  }

  if (up_start && up_len > 0) {
    up_stmt = (char *)C_ORM_MALLOC(up_len + 1);
    if (!up_stmt) {
      C_ORM_FREE(file_data);
      return ENOMEM;
    }
    memcpy(up_stmt, up_start, up_len);
    up_stmt[up_len] = '\0';
    out->up_statement = up_stmt;
  }

  if (down_start && down_len > 0) {
    down_stmt = (char *)C_ORM_MALLOC(down_len + 1);
    if (!down_stmt) {
      C_ORM_FREE(file_data);
      migration_statements_free(out);
      return ENOMEM;
    }
    memcpy(down_stmt, down_start, down_len);
    down_stmt[down_len] = '\0';
    out->down_statement = down_stmt;
  }

  C_ORM_FREE(file_data);
  return 0;
}
