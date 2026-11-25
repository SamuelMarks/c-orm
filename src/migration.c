/**
 * @file migration.c
 * @brief Implementation of SQL migration file parser.
 *
 * @author Samuel Marks
 */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "migration.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c_orm_meta.h"
/* clang-format on */

#ifdef C_ORM_TEST_ALLOCATOR
/** @brief Mock hook to simulate migration_statements_init failure. */
C_ORM_EXPORT int c_orm_mock_migration_statements_init_fail = 0;
#endif /* C_ORM_TEST_ALLOCATOR */

/**
 * @brief Executes the migration statements init operation.
 * @param out The structure to zero.
 * @return C_ORM_OK on success, or error code on failure.
 */
c_orm_error_t migration_statements_init(struct MigrationStatements *out) {
#ifdef C_ORM_TEST_ALLOCATOR
  if (c_orm_mock_migration_statements_init_fail)
    return C_ORM_ERROR_UNKNOWN;
#endif
  if (out) {

    out->up_statement = NULL;
    out->down_statement = NULL;
  }
  return C_ORM_OK;
}

/**
 * @brief Executes the migration statements free operation.
 * @param out The structure to clean.
 * @return C_ORM_OK on success.
 */
c_orm_error_t migration_statements_free(struct MigrationStatements *out) {
  c_orm_error_t rc = C_ORM_OK;
  c_orm_error_t frc;
  if (out) {
    if (out->up_statement) {
      frc = c_orm_system_free(out->up_statement);
      if (frc != C_ORM_OK)
        rc = frc;
      out->up_statement = NULL;
    }
    if (out->down_statement) {
      frc = c_orm_system_free(out->down_statement);
      if (frc != C_ORM_OK && rc == C_ORM_OK)
        rc = frc;
      out->down_statement = NULL;
    }
  }
  return rc;
}

/**
 * @brief Reads a migration file into a c-orm heap buffer.
 * @param filepath Path to the migration `.sql` file.
 * @param out_data Output NUL-terminated buffer (`NULL` when empty).
 * @param out_size Output byte length (`0` when empty).
 * @return C_ORM_OK on success, C_ORM_ERROR_NOT_FOUND on open failure, or
 * C_ORM_ERROR_MEMORY on allocation failure.
 */
static c_orm_error_t migration_read_file_text(const char *filepath,
                                              char **out_data,
                                              size_t *out_size) {
  FILE *fp = NULL;
  long flen;
  size_t nread;
  char *buf = NULL;
  c_orm_error_t rc;

  rc = C_ORM_FOPEN(&fp, filepath, "rb");
  if (rc != C_ORM_OK) {
    return C_ORM_ERROR_NOT_FOUND;
  }
  fseek(fp, 0, SEEK_END);
  flen = ftell(fp);
  rewind(fp);
  if (flen <= 0) {
    fclose(fp);
    *out_data = NULL;
    *out_size = 0;
    return C_ORM_OK;
  }
  rc = c_orm_system_malloc((size_t)flen + 1, (void **)&buf);
  if (rc != C_ORM_OK) {
    fclose(fp);
    return rc;
  }
  nread = fread(buf, 1, (size_t)flen, fp);
  fclose(fp);
  buf[nread] = '\0';
  *out_data = buf;
  *out_size = nread;
  return C_ORM_OK;
}

/**
 * @brief Parses migration file from the given input.
 * @param filepath The path to the .sql migration file.
 * @param out The populated migration statements structure.
 * @return C_ORM_OK on success, or error code on failure.
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
    return C_ORM_ERROR_UNKNOWN;
  }

  rc = migration_statements_init(out);
  if (rc != C_ORM_OK) {
    return rc;
  }

  rc = migration_read_file_text(filepath, &file_data, &file_size);
  if (rc != C_ORM_OK) {
    return rc;
  }

  if (file_size == 0) {
    return C_ORM_OK;
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
  } else if (up_start) {
    up_start += strlen(up_marker);
    up_len = strlen(up_start);
  } else if (down_start) {
    down_start += strlen(down_marker);
    down_len = strlen(down_start);
  } else {
    /* No markers found, assume the whole file is UP */
    up_start = file_data;
    up_len = file_size;
  }

  if (up_start && up_len > 0) {
    rc = c_orm_system_malloc(up_len + 1, (void **)&up_stmt);
    if (rc != C_ORM_OK) {
      c_orm_system_free(file_data);
      return rc;
    }
    memcpy(up_stmt, up_start, up_len);
    up_stmt[up_len] = '\0';
    out->up_statement = up_stmt;
  }

  if (down_start && down_len > 0) {
    rc = c_orm_system_malloc(down_len + 1, (void **)&down_stmt);
    if (rc != C_ORM_OK) {
      c_orm_system_free(file_data);
      migration_statements_free(out);
      return rc;
    }
    memcpy(down_stmt, down_start, down_len);
    down_stmt[down_len] = '\0';
    out->down_statement = down_stmt;
  }

  return c_orm_system_free(file_data);
}
