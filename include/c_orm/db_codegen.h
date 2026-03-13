/**
 * @file db_codegen.h
 * @brief Emitters for C ORM and DB Schemas.
 */

#ifndef C_CDD_DB_CODEGEN_H
#define C_CDD_DB_CODEGEN_H

#ifdef __cplusplus
extern "C" {
#endif

/* clang-format off */
#include <stdio.h>

#include <c_orm/database.h>
/* clang-format on */

/**
 * @brief Generate a C header file containing the structs for the
 * DatabaseSchema.
 * @param schema The database schema.
 * @param out The output file stream.
 * @param header_guard The include guard name.
 * @return 0 on success, non-zero error code otherwise.
 */
int db_codegen_struct_header(const struct DatabaseSchema *schema, FILE *out,
                             const char *header_guard);

/**
 * @brief Generate raw SQL CREATE TABLE scripts.
 * @param schema The database schema.
 * @param out The output file stream.
 * @param dialect The database dialect ("sqlite", "postgres", or "mysql").
 * @return 0 on success, non-zero error code otherwise.
 */
int db_codegen_sql(const struct DatabaseSchema *schema, FILE *out,
                   const char *dialect);

/**
 * @brief Generate C CRUD boilerplate header.
 * @param schema The database schema.
 * @param out The output file stream.
 * @param header_guard The include guard name.
 * @param struct_header The header file name containing the structs to include.
 * @return 0 on success, non-zero error code otherwise.
 */
int db_codegen_crud_h(const struct DatabaseSchema *schema, FILE *out,
                      const char *header_guard, const char *struct_header);

/**
 * @brief Generate C CRUD boilerplate source file.
 * @param schema The database schema.
 * @param out The output file stream.
 * @param header_name The header file name to include.
 * @param dialect The database dialect ("sqlite", "postgres", or "mysql").
 * @return 0 on success, non-zero error code otherwise.
 */
int db_codegen_crud_c(const struct DatabaseSchema *schema, FILE *out,
                      const char *header_name, const char *dialect);

/**
 * @brief Parse a SQL CREATE TABLE script and populate a DatabaseSchema struct.
 * @param sql The raw SQL string to parse.
 * @param out_schema Pointer to the DatabaseSchema to populate.
 * @return 0 on success, non-zero error code otherwise.
 */
int db_codegen_parse_sql(const char *sql, struct DatabaseSchema *out_schema);

#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif /* C_CDD_DB_CODEGEN_H */
