/**
 * @file query_projection.c
 * @brief Implementation of CDD_C query projection AST representation
 *
 * @author Samuel Marks
 */

/* clang-format off */
#include "query_projection.h"
#include "c_orm_meta.h"
#include <stdlib.h>
#include <string.h>
/* clang-format on */

/**
 * @brief Initialize a query projection structure.
 * @param proj Pointer to the query projection structure to initialize.
 * @return C_ORM_OK on success, or C_ORM_ERROR_UNKNOWN if proj is NULL.
 */
c_orm_error_t cdd_c_query_projection_init(cdd_c_query_projection_t *proj) {
  if (!proj)
    return C_ORM_ERROR_UNKNOWN;
  proj->fields = NULL;
  proj->n_fields = 0;
  proj->capacity = 0;
  proj->source_table = NULL;
  proj->mapping_meta.target_name = NULL;
  proj->mapping_meta.kind = CDD_C_MAPPING_KIND_SPECIFIC;
  return C_ORM_OK;
}

/**
 * @brief Duplicates a string using c_orm_system_malloc.
 * @param src Source null-terminated string to duplicate.
 * @param dest Output pointer where duplicated string will be stored.
 * @return C_ORM_OK on success, or error code on allocation failure.
 */
static c_orm_error_t duplicate_string_qp(const char *src, char **dest) {
  size_t len;
  if (!src) {
    *dest = NULL;
    return C_ORM_OK;
  }
  len = strlen(src);
  *dest = NULL;
  if (c_orm_system_malloc(len + 1, (void **)dest) != C_ORM_OK || !*dest)
    return C_ORM_ERROR_UNKNOWN;
  memcpy(*dest, src, len + 1);
  return C_ORM_OK;
}

c_orm_error_t
cdd_c_query_projection_add_field(cdd_c_query_projection_t *proj,
                                 const cdd_c_query_projection_field_t *field) {
  cdd_c_query_projection_field_t *new_fields = NULL;
  size_t new_cap;
  if (!proj || !field)
    return C_ORM_ERROR_UNKNOWN;

  if (proj->n_fields >= proj->capacity) {
    new_cap = proj->capacity == 0 ? 4 : proj->capacity * 2;
    if (c_orm_system_realloc(proj->fields,
                             new_cap * sizeof(cdd_c_query_projection_field_t),
                             (void **)&new_fields) != C_ORM_OK ||
        !new_fields)
      return C_ORM_ERROR_UNKNOWN;
    proj->fields = new_fields;
    proj->capacity = new_cap;
  }

  if (duplicate_string_qp(field->name, &proj->fields[proj->n_fields].name) !=
      C_ORM_OK)
    return C_ORM_ERROR_UNKNOWN;
  if (duplicate_string_qp(field->original_name,
                          &proj->fields[proj->n_fields].original_name) !=
      C_ORM_OK) {
    (void)c_orm_system_free(proj->fields[proj->n_fields].name);
    proj->fields[proj->n_fields].name = NULL;
    return C_ORM_ERROR_UNKNOWN;
  }
  proj->fields[proj->n_fields].type = field->type;
  proj->fields[proj->n_fields].is_aggregate = field->is_aggregate;
  proj->fields[proj->n_fields].length = field->length;
  proj->fields[proj->n_fields].is_array = field->is_array;
  proj->fields[proj->n_fields].is_secure = field->is_secure;

  proj->n_fields++;
  return C_ORM_OK;
}

c_orm_error_t cdd_c_query_projection_free(cdd_c_query_projection_t *proj) {
  size_t i;
  c_orm_error_t rc = C_ORM_OK;
  c_orm_error_t frc;
  if (!proj)
    return C_ORM_ERROR_UNKNOWN;

  for (i = 0; i < proj->n_fields; ++i) {
    if (proj->fields[i].name) {
      frc = c_orm_system_free(proj->fields[i].name);
      if (frc != C_ORM_OK && rc == C_ORM_OK)
        rc = frc;
    }
    if (proj->fields[i].original_name) {
      frc = c_orm_system_free(proj->fields[i].original_name);
      if (frc != C_ORM_OK && rc == C_ORM_OK)
        rc = frc;
    }
  }
  if (proj->fields) {
    frc = c_orm_system_free(proj->fields);
    if (frc != C_ORM_OK && rc == C_ORM_OK)
      rc = frc;
  }
  if (proj->source_table) {
    frc = c_orm_system_free(proj->source_table);
    if (frc != C_ORM_OK && rc == C_ORM_OK)
      rc = frc;
  }
  if (proj->mapping_meta.target_name) {
    frc = c_orm_system_free(proj->mapping_meta.target_name);
    if (frc != C_ORM_OK && rc == C_ORM_OK)
      rc = frc;
  }

  proj->fields = NULL;
  proj->n_fields = 0;
  proj->capacity = 0;
  proj->source_table = NULL;
  proj->mapping_meta.target_name = NULL;
  return rc;
}
