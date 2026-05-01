/**
 * @file c_orm_codegen.c
 * @brief Implementation of the cdd-c code generation wrapper.
 */

/* clang-format off */
#include "c_orm_codegen.h"
#include "c_orm_log.h"
#include <routes/parse/cli.h>
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
  int rc;
  int argc;
  char *argv[3];
  int res;

  LOG_DEBUG("c_orm_codegen_generate: entry");

  if (!schema_file || !output_dir) {
    LOG_DEBUG("c_orm_codegen_generate: missing schema_file or output_dir");
    rc = C_ORM_ERROR_UNKNOWN;
    LOG_DEBUG("c_orm_codegen_generate: exit");
    return (c_orm_error_t)rc;
  }

  /* We expect a .sql file, so we use sql2c_main.
   * sql2c_main expects argv[0] = schema_file, argv[1] = out_dir.
   */
  argc = 2;
  argv[0] = (char *)schema_file;
  argv[1] = (char *)output_dir;
  argv[2] = NULL;

  res = sql2c_main(argc, argv);
  if (res != 0) {
    LOG_DEBUG("c_orm_codegen_generate: sql2c_main failed");
    rc = C_ORM_ERROR_UNKNOWN;
    LOG_DEBUG("c_orm_codegen_generate: exit");
    return (c_orm_error_t)rc;
  }

  rc = C_ORM_OK;
  LOG_DEBUG("c_orm_codegen_generate: exit");
  return (c_orm_error_t)rc;
}
