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
    if (fread(sql_data, 1, (size_t)sql_size, fp) != (size_t)sql_size &&
        ferror(fp)) {
      LOG_DEBUG("c_orm_codegen_generate: read error");
      C_ORM_FREE(sql_data);
      fclose(fp);
      rc = C_ORM_ERROR_UNKNOWN;
      goto cleanup;
    }
    sql_data[sql_size] = '\0';
  }
  fclose(fp);
  fp = NULL;

  if (sql_data) {
    parse_sql_ddl(sql_data, &tables, &n_tables);
  }

  printf("NUM TABLES GENERATED: %d\n", (int)n_tables);
  h_path = (char *)C_ORM_MALLOC(strlen(output_dir) + 32);
  c_path = (char *)C_ORM_MALLOC(strlen(output_dir) + 32);

  C_ORM_SPRINTF(h_path, strlen(output_dir) + 32, "%s/Models.h", output_dir);
  C_ORM_SPRINTF(c_path, strlen(output_dir) + 32, "%s/Models.c", output_dir);

#if defined(_MSC_VER)
  fopen_s(&fp, h_path, "wb");
#else
  fp = fopen(h_path, "wb");
#endif
  if (fp) {
    fprintf(fp, "#ifndef MODELS_H\n#define MODELS_H\n\n");
    fprintf(fp, "/* clang-format off */\n");
    fprintf(fp, "#include \"c_orm_meta.h\"\n");
    fprintf(fp, "#if defined(_MSC_VER)\n"
                "# if _MSC_VER < 1600\n"
                "typedef signed __int8 int8_t;\n"
                "typedef unsigned __int8 uint8_t;\n"
                "typedef signed __int16 int16_t;\n"
                "typedef unsigned __int16 uint16_t;\n"
                "typedef signed __int32 int32_t;\n"
                "typedef unsigned __int32 uint32_t;\n"
                "typedef signed __int64 int64_t;\n"
                "typedef unsigned __int64 uint64_t;\n"
                "# else\n"
                "#  include <stdint.h>\n"
                "# endif\n"
                "#  ifndef __cplusplus\n"
                "#   ifndef _STDBOOL_H\n"
                "#    define _STDBOOL_H\n"
                "typedef unsigned char bool;\n"
                "#    define true 1\n"
                "#    define false 0\n"
                "#   endif\n"
                "#  endif\n"
                "#else\n"
                "# include <stdint.h>\n"
                "# ifndef __cplusplus\n"
                "#  ifndef _STDBOOL_H\n"
                "#   define _STDBOOL_H\n"
                "typedef unsigned char bool;\n"
                "#   define true 1\n"
                "#   define false 0\n"
                "#  endif\n"
                "# endif\n"
                "#endif\n"
                "#include <stddef.h>\n"
                "/* clang-format on */\n\n");

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
    fprintf(fp, "/* clang-format off */\n");
    fprintf(fp, "#include \"Models.h\"\n");
    fprintf(fp, "#include <errno.h>\n");
    fprintf(fp, "#include <stdlib.h>\n");
    fprintf(fp, "#include <string.h>\n");
    fprintf(fp, "/* clang-format on */\n\n");
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
