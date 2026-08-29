#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file c_orm_codegen.h
 * @brief Code generation capabilities wrapping cdd-c.
 */

#ifndef C_ORM_CODEGEN_H
#define C_ORM_CODEGEN_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_db.h"
#include <stddef.h>
/* clang-format on */

/**
 * @brief Generate C structs and CRUD boilerplate directly from a SQL schema or
 * JSON schema using cdd-c.
 *
 * @param schema_file Path to the schema file (e.g. schema.sql or schema.json).
 * @param output_dir Path to the directory where the generated source should be
 * placed.
 * @return C_ORM_OK on success, C_ORM_ERROR_UNKNOWN on failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_codegen_generate(const char *schema_file,
                                                  const char *output_dir);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* C_ORM_CODEGEN_H */

#if defined(__clang__) || defined(__GNUC__)
#endif
