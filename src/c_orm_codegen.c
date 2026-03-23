/**
 * @file c_orm_codegen.c
 * @brief Implementation of the cdd-c code generation wrapper.
 */

/* clang-format off */
#include "c_orm_codegen.h"
#include <routes/parse/cli.h>
#include <string.h>
/* clang-format on */

c_orm_error_t c_orm_codegen_generate(const char *schema_file,
                                     const char *output_dir) {
  int argc;
  char *argv[3];
  int res;

  if (!schema_file || !output_dir) {
    return C_ORM_ERROR_UNKNOWN; /* Or a more specific error if added */
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
    return C_ORM_ERROR_UNKNOWN;
  }

  return C_ORM_OK;
}
