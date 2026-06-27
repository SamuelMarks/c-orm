/**
 * @file c_orm_codegen.c
 * @brief Implementation of the c_orm code generation.
 */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_codegen.h"
#include "c_orm_log.h"
#include "c_orm_sql.h"
#include "c_orm_sql_to_c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

/**
 * @brief Generates C code from a SQL schema file.
 * @param schema_file The path to the SQL schema file.
 * @param output_dir The directory to write the generated code to.
 * @return C_ORM_OK on success, or an error code.
 */
c_orm_error_t c_orm_codegen_generate(const char *schema_file,
                                     const char *output_dir) {
  c_orm_error_t rc;
  char *sql_data = NULL;
  long sql_size;
  struct sql_table_t *tables = NULL;
  size_t n_tables = 0;
  size_t i, j;
  char *h_path = NULL;
  char *c_path = NULL;
  FILE *fp = NULL;

  LOG_DEBUG("c_orm_codegen_generate: entry");

  if (!schema_file || !output_dir) {
    LOG_DEBUG("c_orm_codegen_generate: missing schema_file or output_dir");
    rc = C_ORM_ERROR_UNKNOWN;
    goto cleanup;
  }

#if defined(_MSC_VER)
  fopen_s(&fp, schema_file, "rb");
#else
  fp = fopen(schema_file, "rb");
#endif
  if (!fp) {
    LOG_DEBUG("c_orm_codegen_generate: failed to open schema_file");
    rc = C_ORM_ERROR_UNKNOWN;
    goto cleanup;
  }

  fseek(fp, 0, SEEK_END);
  sql_size = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  if (sql_size > 0) {
    sql_data = (char *)C_ORM_MALLOC((size_t)sql_size + 1);
    if (!sql_data) {
      LOG_DEBUG("c_orm_codegen_generate: OOM");
      fclose(fp);
      rc = C_ORM_ERROR_MEMORY;
      goto cleanup;
    }
    if (fread(sql_data, 1, (size_t)sql_size, fp) != (size_t)sql_size) {
      LOG_DEBUG("c_orm_codegen_generate: fread failed");
      C_ORM_FREE(sql_data);
      sql_data = NULL;
      fclose(fp);
      rc = C_ORM_ERROR_UNKNOWN;
      goto cleanup;
    }
    sql_data[sql_size] = '\0';
  }
  fclose(fp);
  fp = NULL;

  if (sql_data) {
    if (parse_sql_ddl(sql_data, &tables, &n_tables) != 0) {
      printf("FAILED TO PARSE SQL\n");

      LOG_DEBUG("c_orm_codegen_generate: parse_sql_ddl failed");
      rc = C_ORM_ERROR_UNKNOWN;
      goto cleanup;
    }
  }

  LOG_DEBUG("NUM TABLES GENERATED: %d\n", (int)n_tables);
  h_path = (char *)C_ORM_MALLOC(strlen(output_dir) + 32);
  c_path = (char *)C_ORM_MALLOC(strlen(output_dir) + 32);
  if (!h_path || !c_path) {
    LOG_DEBUG("c_orm_codegen_generate: OOM for paths");
    rc = C_ORM_ERROR_MEMORY;
    goto cleanup;
  }

  C_ORM_SPRINTF(h_path, strlen(output_dir) + 32, "%s/Models.h", output_dir);
  C_ORM_SPRINTF(c_path, strlen(output_dir) + 32, "%s/Models.c", output_dir);

#if defined(_MSC_VER)
  fopen_s(&fp, h_path, "wb");
#else
  fp = fopen(h_path, "wb");
#endif
  if (fp) {
    fprintf(fp, "#ifndef MODELS_H\n#define MODELS_H\n\n");
    fprintf(fp, "#include \"c_orm_meta.h\"\n\n");
    for (i = 0; i < n_tables; ++i) {
      sql_to_c_header_emit(fp, &tables[i]);
    }
    fprintf(fp, "#endif\n");
    fclose(fp);
    fp = NULL;
  } else {
    LOG_DEBUG("c_orm_codegen_generate: failed to write header");
    rc = C_ORM_ERROR_UNKNOWN;
    goto cleanup;
  }

#if defined(_MSC_VER)
  fopen_s(&fp, c_path, "wb");
#else
  fp = fopen(c_path, "wb");
#endif
  if (fp) {
    for (i = 0; i < n_tables; ++i) {
      sql_to_c_source_emit(fp, &tables[i], "Models.h");
    }
    fclose(fp);
    fp = NULL;
  } else {
    LOG_DEBUG("c_orm_codegen_generate: failed to write source");
    rc = C_ORM_ERROR_UNKNOWN;
    goto cleanup;
  }

  rc = C_ORM_OK;

cleanup:
  if (sql_data) {
    C_ORM_FREE(sql_data);
  }
  if (h_path) {
    C_ORM_FREE(h_path);
  }
  if (c_path) {
    C_ORM_FREE(c_path);
  }
  if (tables) {
    for (i = 0; i < n_tables; ++i) {
      for (j = 0; j < tables[i].n_columns; ++j) {
        if (tables[i].columns[j].name) {
          C_ORM_FREE(tables[i].columns[j].name);
        }
        if (tables[i].columns[j].constraints) {
          C_ORM_FREE(tables[i].columns[j].constraints);
        }
      }
      if (tables[i].columns) {
        C_ORM_FREE(tables[i].columns);
      }
      if (tables[i].name) {
        C_ORM_FREE(tables[i].name);
      }
    }
    C_ORM_FREE(tables);
  }

  LOG_DEBUG("c_orm_codegen_generate: exit");
  return (c_orm_error_t)rc;
}
