/**
 * @file c_orm_sql_to_c.h
 * @brief Emits C structures and array containers from SQL DDL AST.
 */

#ifndef C_CDD_SQL_TO_C_H
#define C_CDD_SQL_TO_C_H

/* clang-format off */
#include "c_orm_db.h"
#include "c_orm_sql.h"
#include "cdd_c_orm_meta.h"
#include "query_projection.h"
#include "c_orm_safe_crt.h"
#include "abstract_struct.h"
#include <stdio.h>
/* clang-format on */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @brief Map a SQL data type to a C type string.
 *
 * @param type The SQL data type.
 * @param _out_val Output pointer receiving the C type string literal.
 * @return C_ORM_OK on success, or C_ORM_ERROR_VALIDATION for unknown type.
 */
C_ORM_EXPORT c_orm_error_t sql_type_to_c_type(enum SqlDataType type,
                                              char **_out_val);

/**
 * @brief Checks if a given SQL type is passed/stored as a pointer string by
 * default.
 * @param type The SQL data type.
 * @param out_is_string Output pointer set to 1 if string/pointer, 0 if
 * primitive.
 * @return C_ORM_OK on success, or C_ORM_ERROR_NULL_POINTER if out_is_string is
 * NULL.
 */
C_ORM_EXPORT c_orm_error_t sql_type_is_string(enum SqlDataType type,
                                              int *out_is_string);

/**
 * @brief Map a SQL data type to a c_orm_type_t enum string.
 *
 * @param type The SQL data type.
 * @param[out] out_val Output pointer receiving the c_orm_type_t constant string
 * (e.g., "C_ORM_TYPE_INT32").
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY if out_val is NULL.
 */
C_ORM_EXPORT c_orm_error_t sql_type_to_c_orm_type(enum SqlDataType type,
                                                  const char **out_val);

/**
 * @brief Generate a standalone C header file content for a single SQL table,
 * including header guards, clang-format include guards, and extern "C" guards.
 *
 * @param fp File pointer to write the output to.
 * @param table The SQL table AST node.
 * @return C_ORM_OK on success, or error code on failure.
 */
C_ORM_EXPORT c_orm_error_t
sql_to_c_header_emit(FILE *fp, const struct sql_table_t *table);

/**
 * @brief Generate the C struct, array, helper prototypes, and metadata
 * declaration body for a single SQL table (for embedding inside a multi-table
 * header that already emitted its top-level includes and extern "C" block).
 *
 * @param fp File pointer to write the output to.
 * @param table The SQL table AST node.
 * @return C_ORM_OK on success, or error code on failure.
 */
C_ORM_EXPORT c_orm_error_t
sql_to_c_header_emit_body(FILE *fp, const struct sql_table_t *table);

/**
 * @brief Generate the C source file content containing helpers for a given SQL
 * table.
 *
 * @param fp File pointer to write the output to.
 * @param table The SQL table AST node.
 * @param header_name The name of the header file to include (e.g. "users.h").
 * @return 0 on success, non-zero on failure.
 */
C_ORM_EXPORT c_orm_error_t sql_to_c_source_emit(FILE *fp,
                                                const struct sql_table_t *table,
                                                const char *header_name);

/**
 * @brief Emit C struct definition for a query projection.
 *
 * @param[in,out] fp File pointer to write to.
 * @param[in] proj Query projection definition.
 * @param[in] struct_name Name of the struct to emit.
 * @param[out] out_hash Pointer to receive the schema hash.
 * @return C_ORM_OK on success, or an error code on failure.
 */
C_ORM_EXPORT c_orm_error_t sql_to_c_projection_struct_emit(
    FILE *fp, const cdd_c_query_projection_t *proj, const char *struct_name,
    c_orm_uint64_t *out_hash);

/**
 * @brief Emit free function for a query projection struct.
 *
 * @param[in,out] fp File pointer to write to.
 * @param[in] proj Query projection definition.
 * @param[in] struct_name Name of the struct.
 * @return C_ORM_OK on success, or an error code on failure.
 */
C_ORM_EXPORT c_orm_error_t sql_to_c_projection_free_emit(
    FILE *fp, const cdd_c_query_projection_t *proj, const char *struct_name);

/**
 * @brief Emit metadata table definition for a query projection struct.
 *
 * @param[in,out] fp File pointer to write to.
 * @param[in] proj Query projection definition.
 * @param[in] struct_name Name of the struct.
 * @return C_ORM_OK on success, or an error code on failure.
 */
C_ORM_EXPORT c_orm_error_t sql_to_c_projection_meta_emit(
    FILE *fp, const cdd_c_query_projection_t *proj, const char *struct_name);

/**
 * @brief Emit hydration function for a query projection struct.
 *
 * @param[in,out] fp File pointer to write to.
 * @param[in] proj Query projection definition.
 * @param[in] struct_name Name of the struct.
 * @return C_ORM_OK on success, or an error code on failure.
 */
C_ORM_EXPORT c_orm_error_t sql_to_c_projection_hydrate_emit(
    FILE *fp, const cdd_c_query_projection_t *proj, const char *struct_name);
/**
 * @brief Emit dehydration parameter-binding function for a query projection
 * struct.
 *
 * @param[in,out] fp File pointer to write to.
 * @param[in] proj Query projection definition.
 * @param[in] struct_name Name of the struct.
 * @return C_ORM_OK on success, or an error code on failure.
 */
C_ORM_EXPORT c_orm_error_t sql_to_c_projection_dehydrate_emit(
    FILE *fp, const cdd_c_query_projection_t *proj, const char *struct_name);
/**
 * @brief Emit nested sub-struct definition for joined query projections.
 *
 * @param[in,out] fp File pointer to write to.
 * @param[in] proj Query projection definition.
 * @param[in] struct_name Name of the struct.
 * @return C_ORM_OK on success, or an error code on failure.
 */
C_ORM_EXPORT c_orm_error_t sql_to_c_projection_nested_struct_emit(
    FILE *fp, const cdd_c_query_projection_t *proj, const char *struct_name);
/**
 * @brief Emit nested array container definition for one-to-many query
 * projections.
 *
 * @param[in,out] fp File pointer to write to.
 * @param[in] proj Query projection definition.
 * @param[in] struct_name Element struct name.
 * @param[in] array_name Generated array container struct name.
 * @return C_ORM_OK on success, or an error code on failure.
 */
C_ORM_EXPORT c_orm_error_t sql_to_c_projection_nested_array_emit(
    FILE *fp, const cdd_c_query_projection_t *proj, const char *struct_name,
    const char *array_name);
/**
 * @brief Emit dirty-tracking bitmask helpers for partial updates on a query
 * projection struct.
 *
 * @param[in,out] fp File pointer to write to.
 * @param[in] proj Query projection definition.
 * @param[in] struct_name Name of the struct.
 * @return C_ORM_OK on success, or an error code on failure.
 */
C_ORM_EXPORT c_orm_error_t sql_to_c_projection_dirty_bitmask_emit(
    FILE *fp, const cdd_c_query_projection_t *proj, const char *struct_name);
/**
 * @brief Emit union struct definition for query projections.
 *
 * @param[in,out] fp File pointer to write to.
 * @param[in] projs Array of query projections.
 * @param[in] n_projs Number of query projections.
 * @param[in] struct_name Name of the union struct to emit.
 * @return C_ORM_OK on success, or an error code on failure.
 */
C_ORM_EXPORT c_orm_error_t sql_to_c_projection_union_struct_emit(
    FILE *fp, const cdd_c_query_projection_t *projs, size_t n_projs,
    const char *struct_name);

/**
 * @brief Emit deep-free function implementation for a SQL UNION projection
 * struct.
 *
 * @param[in,out] fp File pointer to write to.
 * @param[in] projs Array of branch query projections.
 * @param[in] n_projs Number of branch query projections.
 * @param[in] struct_name Name of the union struct.
 * @return C_ORM_OK on success, or an error code on failure.
 */
C_ORM_EXPORT c_orm_error_t sql_to_c_projection_union_free_emit(
    FILE *fp, const cdd_c_query_projection_t *projs, size_t n_projs,
    const char *struct_name);

/**
 * @brief Emit polymorphic struct combining explicit projection fields with a
 * dynamic abstract property dictionary.
 *
 * @param[in,out] fp File pointer to write to.
 * @param[in] proj Query projection definition.
 * @param[in] struct_name Name of the polymorphic struct to emit.
 * @return C_ORM_OK on success, or an error code on failure.
 */
C_ORM_EXPORT c_orm_error_t sql_to_c_projection_polymorphic_struct_emit(
    FILE *fp, const cdd_c_query_projection_t *proj, const char *struct_name);

/**
 * @brief Emit deep-free function implementation for a polymorphic projection
 * struct (freeing both explicit fields and `extra_properties`).
 *
 * @param[in,out] fp File pointer to write to.
 * @param[in] proj Query projection definition.
 * @param[in] struct_name Name of the polymorphic struct.
 * @return C_ORM_OK on success, or an error code on failure.
 */
C_ORM_EXPORT c_orm_error_t sql_to_c_projection_polymorphic_free_emit(
    FILE *fp, const cdd_c_query_projection_t *proj, const char *struct_name);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* C_CDD_SQL_TO_C_H */
