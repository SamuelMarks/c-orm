#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file cdd_c_ir.c
 * @brief Implementation of the top-level Intermediate Representation for CDD-C
 *
 * @author Samuel Marks
 */

/* clang-format off */
#include "cdd_c_ir.h"
#include <stdlib.h>
#include <string.h>
#include "c_orm_meta.h"
/* clang-format on */

c_orm_error_t cdd_c_ir_init(cdd_c_ir_t *ir) {
  if (!ir)
    return C_ORM_ERROR_UNKNOWN;
  ir->tables = NULL;
  ir->n_tables = 0;
  ir->capacity_tables = 0;

  ir->projections = NULL;
  ir->n_projections = 0;
  ir->capacity_projections = 0;
  return C_ORM_OK;
}

c_orm_error_t cdd_c_ir_add_table(cdd_c_ir_t *ir,
                                 const struct sql_table_t *table) {
  struct sql_table_t *new_tables;
  size_t new_cap;
  if (!ir || !table)
    return C_ORM_ERROR_UNKNOWN;

  if (ir->n_tables >= ir->capacity_tables) {
    new_cap = ir->capacity_tables == 0 ? 4 : ir->capacity_tables * 2;
    new_tables = (struct sql_table_t *)C_ORM_REALLOC(
        ir->tables, new_cap * sizeof(struct sql_table_t));
    if (!new_tables)
      return C_ORM_ERROR_UNKNOWN;
    ir->tables = new_tables;
    ir->capacity_tables = new_cap;
  }

  ir->tables[ir->n_tables] =
      *table; /* Shallow copy: assumes ownership is transferred */
  ir->n_tables++;
  return C_ORM_OK;
}

static c_orm_error_t duplicate_projection(cdd_c_query_projection_t *dest,
                                          const cdd_c_query_projection_t *src) {
  size_t i;
  c_orm_error_t rc;
  /* !dest and !src are checked implicitly or by callers */
  cdd_c_query_projection_init(dest);
  for (i = 0; i < src->n_fields; ++i) {
    rc = cdd_c_query_projection_add_field(dest, &src->fields[i]);
    if (rc != C_ORM_OK) {
      cdd_c_query_projection_free(dest);
      return rc;
    }
  }
  if (src->source_table) {
    dest->source_table = (char *)C_ORM_MALLOC(strlen(src->source_table) + 1);
    if (!dest->source_table) {
      cdd_c_query_projection_free(dest);
      return C_ORM_ERROR_UNKNOWN;
    }
    memcpy(dest->source_table, src->source_table,
           strlen(src->source_table) + 1);
  }
  dest->mapping_meta.kind = src->mapping_meta.kind;
  if (src->mapping_meta.target_name) {
    dest->mapping_meta.target_name =
        (char *)C_ORM_MALLOC(strlen(src->mapping_meta.target_name) + 1);
    if (!dest->mapping_meta.target_name) {
      cdd_c_query_projection_free(dest);
      return C_ORM_ERROR_UNKNOWN;
    }
    memcpy(dest->mapping_meta.target_name, src->mapping_meta.target_name,
           strlen(src->mapping_meta.target_name) + 1);
  } else {
    dest->mapping_meta.target_name = NULL;
  }
  return C_ORM_OK;
}

c_orm_error_t cdd_c_ir_add_projection(cdd_c_ir_t *ir,
                                      const cdd_c_query_projection_t *proj) {
  cdd_c_query_projection_t *new_projs;
  size_t new_cap;
  c_orm_error_t rc;
  if (!ir || !proj)
    return C_ORM_ERROR_UNKNOWN;

  if (ir->n_projections >= ir->capacity_projections) {
    new_cap = ir->capacity_projections == 0 ? 4 : ir->capacity_projections * 2;
    new_projs = (cdd_c_query_projection_t *)C_ORM_REALLOC(
        ir->projections, new_cap * sizeof(cdd_c_query_projection_t));
    if (!new_projs)
      return C_ORM_ERROR_UNKNOWN;
    ir->projections = new_projs;
    ir->capacity_projections = new_cap;
  }

  rc = duplicate_projection(&ir->projections[ir->n_projections], proj);
  if (rc != C_ORM_OK)
    return rc;

  ir->n_projections++;
  return C_ORM_OK;
}

c_orm_error_t cdd_c_ir_free(cdd_c_ir_t *ir) {
  size_t i;
  if (!ir)
    return C_ORM_ERROR_UNKNOWN;

  for (i = 0; i < ir->n_tables; ++i) {
    sql_table_C_ORM_FREE(&ir->tables[i]);
  }
  if (ir->tables)
    C_ORM_FREE(ir->tables);

  for (i = 0; i < ir->n_projections; ++i) {
    cdd_c_query_projection_free(&ir->projections[i]);
  }
  if (ir->projections)
    C_ORM_FREE(ir->projections);

  ir->tables = NULL;
  ir->n_tables = 0;
  ir->capacity_tables = 0;

  ir->projections = NULL;
  ir->n_projections = 0;
  ir->capacity_projections = 0;
  return C_ORM_OK;
}

c_orm_error_t parse_sql_into_ir(const char *sql_data, cdd_c_ir_t *out_ir) {
  struct sql_token_list_t *list = NULL;
  struct sql_table_t *table = NULL;
  cdd_c_query_projection_t *proj = NULL;
  struct sql_parse_error_t err;
  az_span span;
  c_orm_error_t rc;
  size_t i;
  size_t start_idx = 0;
  int in_table = 0;
  int in_select = 0;

  if (!sql_data || !out_ir)
    return C_ORM_ERROR_UNKNOWN;

  span = az_span_create_from_str((char *)sql_data);
  rc = sql_lex(span, &list);
  if (rc != C_ORM_OK)
    return rc;

  for (i = 0; i < list->size; ++i) {
    if (!in_table && !in_select && list->tokens[i].kind == SQL_TOKEN_KEYWORD) {
      if (list->tokens[i].length == 6 &&
          strncmp(list->tokens[i].start, "CREATE", 6) == 0) {
        start_idx = i;
        in_table = 1;
      } else if (list->tokens[i].length == 6 &&
                 strncmp(list->tokens[i].start, "SELECT", 6) == 0) {
        start_idx = i;
        in_select = 1;
      }
    }

    if ((in_table || in_select) &&
        list->tokens[i].kind == SQL_TOKEN_SEMICOLON) {
      struct sql_token_list_t sublist;
      sublist.tokens = &list->tokens[start_idx];
      sublist.size = i - start_idx + 1;
      sublist.capacity = sublist.size;

      if (in_table) {
        table = NULL;
        rc = sql_parse_table(&sublist, &table, &err);
        if (rc != C_ORM_OK) {
          sql_token_list_free(list);
          return rc;
        }
        if (table) {
          rc = cdd_c_ir_add_table(out_ir, table);
          if (rc != C_ORM_OK) {
            sql_table_C_ORM_FREE(table);
            C_ORM_FREE(table);
            sql_token_list_free(list);
            return rc;
          }
          C_ORM_FREE(table);
        }
        in_table = 0;
      } else if (in_select) {
        proj = NULL;
        rc = sql_parse_select(&sublist, &proj, &err);
        if (rc != C_ORM_OK) {
          sql_token_list_free(list);
          return rc;
        }
        if (proj) {
          rc = cdd_c_ir_add_projection(out_ir, proj);
          if (rc != C_ORM_OK) {
            C_ORM_FREE(proj);
            sql_token_list_free(list);
            return rc;
          }
          /* cdd_c_query_projection_free(proj); */
          C_ORM_FREE(proj);
        }
        in_select = 0;
      }
    }
  }

  /* Process RETURNING if present, could be attached to INSERT/UPDATE/DELETE */
  /* For now, parse them as separate projections if standalone RETURNING keyword
   * appears */
  {
    proj = NULL;
    {
      c_orm_error_t tmp = sql_parse_returning(list, &proj, &err);
      if (tmp != C_ORM_OK) {
        sql_token_list_free(list);
        return tmp;
      }
    }
    if (proj) {
      rc = cdd_c_ir_add_projection(out_ir, proj);
      if (rc != C_ORM_OK) {
        C_ORM_FREE(proj);
        sql_token_list_free(list);
        return rc;
      }
      /* cdd_c_query_projection_free(proj); */
      C_ORM_FREE(proj);
    }
  }

  sql_token_list_free(list);
  return C_ORM_OK;
}

#if defined(__clang__) || defined(__GNUC__)
#endif
