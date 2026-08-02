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

static c_orm_error_t duplicate_string_qp(const char *src, char **dest) {
  size_t len;
  if (!src) {
    *dest = NULL;
    return C_ORM_OK;
  }
  len = strlen(src);
  *dest = (char *)C_ORM_MALLOC(len + 1);
  if (!*dest)
    return C_ORM_ERROR_UNKNOWN;
  memcpy(*dest, src, len + 1);
  return C_ORM_OK;
}

c_orm_error_t
cdd_c_query_projection_add_field(cdd_c_query_projection_t *proj,
                                 const cdd_c_query_projection_field_t *field) {
  cdd_c_query_projection_field_t *new_fields;
  size_t new_cap;
  if (!proj || !field)
    return C_ORM_ERROR_UNKNOWN;

  if (proj->n_fields >= proj->capacity) {
    new_cap = proj->capacity == 0 ? 4 : proj->capacity * 2;
    new_fields = (cdd_c_query_projection_field_t *)C_ORM_REALLOC(
        proj->fields, new_cap * sizeof(cdd_c_query_projection_field_t));
    if (!new_fields)
      return C_ORM_ERROR_UNKNOWN;
    proj->fields = new_fields;
    proj->capacity = new_cap;
  }

  if (duplicate_string_qp(field->name, &proj->fields[proj->n_fields].name) != 0)
    return C_ORM_ERROR_UNKNOWN;
  if (duplicate_string_qp(field->original_name,
                          &proj->fields[proj->n_fields].original_name) != 0) {
    C_ORM_FREE(proj->fields[proj->n_fields].name);
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
  if (!proj)
    return C_ORM_ERROR_UNKNOWN;

  for (i = 0; i < proj->n_fields; ++i) {
    if (proj->fields[i].name)
      C_ORM_FREE(proj->fields[i].name);
    if (proj->fields[i].original_name)
      C_ORM_FREE(proj->fields[i].original_name);
  }
  if (proj->fields)
    C_ORM_FREE(proj->fields);
  if (proj->source_table)
    free(proj->source_table);
  if (proj->mapping_meta.target_name)
    free(proj->mapping_meta.target_name);

  proj->fields = NULL;
  proj->n_fields = 0;
  proj->capacity = 0;
  proj->source_table = NULL;
  proj->mapping_meta.target_name = NULL;
  return C_ORM_OK;
}
