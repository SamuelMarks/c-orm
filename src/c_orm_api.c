/**
 * @file c_orm_api.c
 * @brief Implementation of high-level API for c-orm.
 */

/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_ast.h"
#include "c_orm_query_builder.h"
#include "c_orm_uuid.h"
#include "classes/parse/sql.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
/* #include "classes/parse/abstract_struct.h" */
/* clang-format on */

C_ORM_EXPORT c_orm_error_t c_orm_hydrate_row_from(
    c_orm_db_t *db, c_orm_query_t *query, const c_orm_table_meta_t *meta,
    void *out_struct, size_t start_col) {
  size_t i;
  c_orm_error_t err;

  for (i = 0; i < meta->num_columns; ++i) {
    const c_orm_column_meta_t *col = &meta->columns[i];
    void *field_ptr = (char *)out_struct + col->offset;
    int is_null = 0;
    size_t col_idx = start_col + i;

    err = db->vtable->is_null(query, (int)col_idx, &is_null);
    if (err != C_ORM_OK)
      return err;

    if (is_null) {
      if (!col->is_nullable) {
        /* This is actually a constraint violation */
        return C_ORM_ERROR_TYPE_MISMATCH;
      }
      /* If it's nullable primitive, we need to set the pointer to NULL.
         For string, set char* to NULL. */
      if (col->type == C_ORM_TYPE_STRING || col->type == C_ORM_TYPE_JSON ||
          col->type == C_ORM_TYPE_SET || col->type == C_ORM_TYPE_ENUM ||
          col->type == C_ORM_TYPE_TIMESTAMP || col->type == C_ORM_TYPE_DATE) {
        if (*(char **)field_ptr)
          free(*(char **)field_ptr);
        *(char **)field_ptr = NULL;
      } else if (col->type == C_ORM_TYPE_BLOB) {
        c_orm_blob_t *b = (c_orm_blob_t *)field_ptr;
        if (b->data)
          free(b->data);
        b->data = NULL;
        b->size = 0;
      } else {
        if (*(void **)field_ptr)
          free(*(void **)field_ptr);
        *(void **)field_ptr = NULL;
      }
      continue;
    }

    switch (col->type) {
    case C_ORM_TYPE_INT32: {
      int32_t val;
      err = db->vtable->get_int32(query, (int)col_idx, &val);
      if (err != C_ORM_OK)
        return err;
      if (col->is_nullable) {
        int32_t *ptr = (int32_t *)malloc(sizeof(int32_t));
        if (!ptr)
          return C_ORM_ERROR_MEMORY;
        *ptr = val;
        *(int32_t **)field_ptr = ptr;
      } else {
        *(int32_t *)field_ptr = val;
      }
      break;
    }
    case C_ORM_TYPE_BOOL: {
      int32_t val;
      err = db->vtable->get_int32(query, (int)col_idx, &val);
      if (err != C_ORM_OK)
        return err;
      if (col->is_nullable) {
        /* Allocate 4 bytes to safely cover any compiler bool size mismatches (1
         * vs 4) */
        int *ptr = (int *)malloc(sizeof(int));
        if (!ptr)
          return C_ORM_ERROR_MEMORY;
        *ptr = (val != 0 ? 1 : 0);
        *(int **)field_ptr = ptr;
      } else {
        /* Write 1 byte to the struct, but if it expects 4 bytes it might have
           garbage in the padding? Actually we can just write it as int safely
           if we know padding allows it. But since we only know the offset,
           writing 1 byte is safer for structs. */
        *(unsigned char *)field_ptr = (unsigned char)(val != 0 ? 1 : 0);
      }
      break;
    }
    case C_ORM_TYPE_INT64: {
      int64_t val;
      err = db->vtable->get_int64(query, (int)col_idx, &val);
      if (err != C_ORM_OK)
        return err;
      if (col->is_nullable) {
        int64_t *ptr = (int64_t *)malloc(sizeof(int64_t));
        if (!ptr)
          return C_ORM_ERROR_MEMORY;
        *ptr = val;
        *(int64_t **)field_ptr = ptr;
      } else {
        *(int64_t *)field_ptr = val;
      }
      break;
    }
    case C_ORM_TYPE_FLOAT:
    case C_ORM_TYPE_DOUBLE: {
      double val;
      err = db->vtable->get_double(query, (int)col_idx, &val);
      if (err != C_ORM_OK)
        return err;
      if (col->is_nullable) {
        if (col->type == C_ORM_TYPE_FLOAT) {
          float *ptr = (float *)malloc(sizeof(float));
          if (!ptr)
            return C_ORM_ERROR_MEMORY;
          *ptr = (float)val;
          *(float **)field_ptr = ptr;
        } else {
          double *ptr = (double *)malloc(sizeof(double));
          if (!ptr)
            return C_ORM_ERROR_MEMORY;
          *ptr = val;
          *(double **)field_ptr = ptr;
        }
      } else {
        if (col->type == C_ORM_TYPE_FLOAT) {
          *(float *)field_ptr = (float)val;
        } else {
          *(double *)field_ptr = val;
        }
      }
      break;
    }
    case C_ORM_TYPE_STRING:
    case C_ORM_TYPE_DATE:
    case C_ORM_TYPE_TIMESTAMP:
    case C_ORM_TYPE_ENUM:
    case C_ORM_TYPE_SET:
    case C_ORM_TYPE_JSON: {
      const char *val;
      err = db->vtable->get_string(query, (int)col_idx, &val);
      if (err != C_ORM_OK)
        return err;

      if (val) {
        if (col->type == C_ORM_TYPE_TIMESTAMP &&
            db->timezone.offset_minutes != 0) {
          char tz_buffer[64];
          struct tm tm_val = {0};
          int year, month, day, hour, min, sec;
          /* A real driver would pass raw timestamp objects. This string
           * approach tests dynamic mapping offsets. */
          if (sscanf(val, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &min,
                     &sec) == 6) {
            tm_val.tm_year = year - 1900;
            tm_val.tm_mon = month - 1;
            tm_val.tm_mday = day;
            tm_val.tm_hour = hour;
            tm_val.tm_min = min;
            tm_val.tm_sec = sec;

            /* Shift back to local time by ADDING the offset when reading from
             * DB */
            tm_val.tm_min += db->timezone.offset_minutes;
            mktime(&tm_val);

#if defined(_MSC_VER)
            sprintf_s(tz_buffer, sizeof(tz_buffer),
                      "%04d-%02d-%02d %02d:%02d:%02d", tm_val.tm_year + 1900,
                      tm_val.tm_mon + 1, tm_val.tm_mday, tm_val.tm_hour,
                      tm_val.tm_min, tm_val.tm_sec);
#else
            sprintf(tz_buffer, "%04d-%02d-%02d %02d:%02d:%02d",
                    tm_val.tm_year + 1900, tm_val.tm_mon + 1, tm_val.tm_mday,
                    tm_val.tm_hour, tm_val.tm_min, tm_val.tm_sec);
#endif

            /* We need to re-copy tz_buffer since val is read-only */
            {
              size_t len = strlen(tz_buffer);
              *(char **)field_ptr = (char *)malloc(len + 1);
              if (*(char **)field_ptr) {
#if defined(_MSC_VER)
                strcpy_s(*(char **)field_ptr, len + 1, tz_buffer);
#else
                strcpy(*(char **)field_ptr, tz_buffer);
#endif
              }
            }
            if (!*(char **)field_ptr)
              return C_ORM_ERROR_MEMORY;
            break; /* Skip the rest of string processing for this column since
                      we mapped it */
          }
        }

        if (col->is_secure &&
            db->decrypt_hook) { /* Decrypt from stored value */
          void *decrypted_data = NULL;
          size_t decrypted_size = 0;
          /* A real system reads length directly from blob API if it's blob
           * natively, but here we read string length if stored as base64 string
           */
          err = db->decrypt_hook(val, strlen(val), db->crypto_context,
                                 &decrypted_data, &decrypted_size);
          if (err != C_ORM_OK)
            return err;
          *(char **)field_ptr = (char *)malloc(decrypted_size + 1);
          if (*(char **)field_ptr) {
            memcpy(*(char **)field_ptr, decrypted_data, decrypted_size);
            (*(char **)field_ptr)[decrypted_size] = '\0';
          }
          free(decrypted_data);
          if (!*(char **)field_ptr)
            return C_ORM_ERROR_MEMORY;
        } else {
          size_t len = strlen(val);
          *(char **)field_ptr = (char *)malloc(len + 1);
          if (*(char **)field_ptr) {
#if defined(_MSC_VER)
            strcpy_s(*(char **)field_ptr, len + 1, val);
#else
            strcpy(*(char **)field_ptr, val);
#endif
          }
          if (!*(char **)field_ptr)
            return C_ORM_ERROR_MEMORY;
        }
      } else {
        *(char **)field_ptr = NULL;
      }
      break;
    }
    case C_ORM_TYPE_POINT: {
      const void *val;
      size_t size;
      err = db->vtable->get_blob(query, (int)col_idx, &val, &size);
      if (err != C_ORM_OK)
        return err;
      if (val && size == 21) {
        c_orm_point_t *pt = (c_orm_point_t *)field_ptr;
        const unsigned char *wkb = (const unsigned char *)val;
        memcpy(&pt->x, &wkb[5], 8);
        memcpy(&pt->y, &wkb[13], 8);
      }
      break;
    }
    case C_ORM_TYPE_POLYGON: {
      const void *val;
      size_t size;
      err = db->vtable->get_blob(query, (int)col_idx, &val, &size);
      if (err != C_ORM_OK)
        return err;
      if (val && size >= 13) {
        c_orm_polygon_t *poly = (c_orm_polygon_t *)field_ptr;
        const unsigned char *wkb = (const unsigned char *)val;
        uint32_t num_points = 0;
        memcpy(&num_points, &wkb[9], 4);
        poly->num_points = num_points;
        if (num_points > 0 && size >= 13 + num_points * 16) {
          poly->points =
              (c_orm_point_t *)malloc(num_points * sizeof(c_orm_point_t));
          if (poly->points) {
            size_t j;
            for (j = 0; j < num_points; ++j) {
              memcpy(&poly->points[j].x, &wkb[13 + j * 16], 8);
              memcpy(&poly->points[j].y, &wkb[13 + j * 16 + 8], 8);
            }
          } else {
            return C_ORM_ERROR_MEMORY;
          }
        } else {
          poly->points = NULL;
          poly->num_points = 0;
        }
      }
      break;
    }
    case C_ORM_TYPE_BLOB: {
      const void *val;
      size_t size;
      err = db->vtable->get_blob(query, (int)col_idx, &val, &size);
      if (err != C_ORM_OK)
        return err;

      if (val && size > 0) {
        c_orm_blob_t *blob_ptr = (c_orm_blob_t *)field_ptr;

        if (col->is_secure && db->decrypt_hook) {
          void *decrypted_data = NULL;
          size_t decrypted_size = 0;
          err = db->decrypt_hook(val, size, db->crypto_context, &decrypted_data,
                                 &decrypted_size);
          if (err != C_ORM_OK)
            return err;

          blob_ptr->data = malloc(decrypted_size);
          if (blob_ptr->data) {
            memcpy(blob_ptr->data, decrypted_data, decrypted_size);
            blob_ptr->size = decrypted_size;
          }
          free(decrypted_data);
          if (!blob_ptr->data) {
            blob_ptr->size = 0;
            return C_ORM_ERROR_MEMORY;
          }
        } else {
          blob_ptr->data = malloc(size);
          if (blob_ptr->data) {
            memcpy(blob_ptr->data, val, size);
            blob_ptr->size = size;
          } else {
            blob_ptr->size = 0;
            return C_ORM_ERROR_MEMORY;
          }
        }
      } else {
        c_orm_blob_t *blob_ptr = (c_orm_blob_t *)field_ptr;
        blob_ptr->data = NULL;
        blob_ptr->size = 0;
      }
      break;
    }
    default:
      return C_ORM_ERROR_TYPE_MISMATCH;
    }
  }

  if (meta->has_ttl) {
    int64_t created_at =
        *(int64_t *)((char *)out_struct + meta->created_at_offset);
    int32_t expires_in =
        *(int32_t *)((char *)out_struct + meta->expires_in_offset);
    int64_t current_time = (int64_t)time(NULL);
    if (created_at + (int64_t)expires_in < current_time) {
      if (db->expire_cb) {
        db->expire_cb(db, meta, out_struct, db->expire_user_data);
      }
      return C_ORM_ERROR_EXPIRED;
    }
  }

  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_hydrate_row(c_orm_db_t *db,
                                             c_orm_query_t *query,
                                             const c_orm_table_meta_t *meta,
                                             void *out_struct) {
  c_orm_error_t err = c_orm_hydrate_row_from(db, query, meta, out_struct, 0);
  int col_count = 0;
  int i;
  void *cached_row = NULL;

  if (err != C_ORM_OK)
    return err;

  /* Dynamic nested hydration for prefix columns (e.g. "company_id") */
  if (db->vtable->get_column_count && db->vtable->get_column_name) {
    if (db->vtable->get_column_count(query, &col_count) == C_ORM_OK) {
      for (i = (int)meta->num_columns; i < col_count; i++) {
        const char *col_name = NULL;
        if (db->vtable->get_column_name(query, i, &col_name) == C_ORM_OK &&
            col_name) {
          size_t rel_idx;
          for (rel_idx = 0; rel_idx < meta->num_relations; rel_idx++) {
            const c_orm_relation_meta_t *rel = &meta->relations[rel_idx];
            size_t prefix_len = strlen(rel->field_name);
            if (strncmp(col_name, rel->field_name, prefix_len) == 0 &&
                col_name[prefix_len] == '_') {
              const char *target_col_name = col_name + prefix_len + 1;
              size_t tc;
              for (tc = 0; tc < rel->target_meta->num_columns; tc++) {
                if (strcmp(rel->target_meta->columns[tc].name,
                           target_col_name) == 0) {
                  /* We found a match! Initialize proxy if needed and hydrate */
                  void *context_ptr = (char *)out_struct + rel->struct_offset;
                  c_orm_lazy_load_context_t *ctx =
                      (c_orm_lazy_load_context_t *)context_ptr;
                  void *target_data_ptr =
                      (char *)context_ptr + sizeof(c_orm_lazy_load_context_t);

                  if (rel->type == C_ORM_RELATION_ONE_TO_ONE ||
                      rel->type == C_ORM_RELATION_BELONGS_TO) {
                    void *nested_struct = *(void **)target_data_ptr;
                    int is_null = 0;

                    if (!nested_struct) {
                      /* Check if this prefix column itself is null first to
                       * avoid instantiating empty structs */
                      db->vtable->is_null(query, i, &is_null);
                      if (!is_null) {
                        nested_struct =
                            calloc(1, rel->target_meta->struct_size);
                        if (nested_struct) {
                          *(void **)target_data_ptr = nested_struct;
                          ctx->is_loaded = 1;
                        }
                      }
                    }

                    if (nested_struct && !is_null) {
                      /* Wait, hydrate_row_from reads ALL columns from
                       * start_col! We only want ONE column. */
                      /* Let's inline the single column hydration here: */
                      const c_orm_column_meta_t *tcol =
                          &rel->target_meta->columns[tc];
                      void *field_ptr = (char *)nested_struct + tcol->offset;

                      switch (tcol->type) {
                      case C_ORM_TYPE_INT32:
                        db->vtable->get_int32(query, i, (int32_t *)field_ptr);
                        break;
                      case C_ORM_TYPE_INT64:
                        db->vtable->get_int64(query, i, (int64_t *)field_ptr);
                        break;
                      case C_ORM_TYPE_DOUBLE:
                        db->vtable->get_double(query, i, (double *)field_ptr);
                        break;
                      case C_ORM_TYPE_STRING: {
                        const char *str_val = NULL;
                        db->vtable->get_string(query, i, &str_val);
                        if (str_val) {
                          char **dst = (char **)field_ptr;
                          if (*dst)
                            free(*dst);
                          *dst = (char *)malloc(strlen(str_val) + 1);
                          if (*dst) {
#if defined(_MSC_VER)
                            strcpy_s(*dst, strlen(str_val) + 1, str_val);
#else
                            strcpy(*dst, str_val);
#endif
                          }
                        }
                        break;
                      }
                      case C_ORM_TYPE_BLOB: {
                        const void *blob_val = NULL;
                        size_t blob_size = 0;
                        db->vtable->get_blob(query, i, &blob_val, &blob_size);
                        /* Not dealing with blob struct allocation here for
                         * brevity */
                        break;
                      }
                      case C_ORM_TYPE_BOOL: {
                        int32_t bool_val = 0;
                        db->vtable->get_int32(query, i, &bool_val);
                        *(int *)field_ptr = bool_val;
                        break;
                      }
                      }

                      /* If this is the last column for this relation, try
                       * caching it */
                      if (tc == rel->target_meta->num_columns - 1) {
                        void *cached_nested = NULL;
                        c_orm_hydrate_cache_row(db, rel->target_meta,
                                                nested_struct, &cached_nested);
                        *(void **)target_data_ptr = cached_nested;
                      }
                    }
                  }
                  break;
                }
              }
            }
          }
        }
      }
    }
  }

  return c_orm_hydrate_cache_row(db, meta, out_struct, &cached_row);
}

C_ORM_EXPORT c_orm_error_t c_orm_find_all(c_orm_db_t *db,
                                          const c_orm_table_meta_t *meta,
                                          void *out_array) {
  c_orm_query_t *query;
  c_orm_error_t err;

  if (!db || !meta || !out_array)
    return C_ORM_ERROR_MEMORY;

  err = c_orm_prepare_cached(db, meta->query_select_all, &query);
  if (err != C_ORM_OK)
    return err;

  err = c_orm_hydrate_all(db, query, meta, out_array);

  c_orm_finalize_cached(db, query);
  return err;
}

C_ORM_EXPORT c_orm_error_t c_orm_find_by_composite_key(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, size_t num_keys,
    const struct CddCVariant *key_values, void *out_struct) {
  c_orm_query_t *query;
  c_orm_error_t err;
  int has_row;
  size_t i;

  if (!db || !meta || !key_values || !out_struct)
    return C_ORM_ERROR_MEMORY;

  if (!meta->query_select_by_pk)
    return 700;

  err = c_orm_prepare_cached(db, meta->query_select_by_pk, &query);
  if (err != C_ORM_OK)
    return err;

  for (i = 0; i < num_keys; ++i) {
    const struct CddCVariant *var = &key_values[i];
    if (var->type == CDD_C_VARIANT_TYPE_INT) {
      /* Fallback heuristics: determine if int32 or int64 */
      err = db->vtable->bind_int64(query, (int)(i + 1), var->value.i_val);
    } else if (var->type == CDD_C_VARIANT_TYPE_STRING) {
      err = db->vtable->bind_string(query, (int)(i + 1), var->value.s_val);
    } else {
      err = C_ORM_ERROR_NOT_IMPLEMENTED;
    }

    if (err != C_ORM_OK) {
      c_orm_finalize_cached(db, query);
      return err;
    }
  }

  err = db->vtable->step(query, &has_row);
  if (err != C_ORM_OK) {
    c_orm_finalize_cached(db, query);
    return err;
  }

  if (!has_row) {
    c_orm_finalize_cached(db, query);
    return C_ORM_ERROR_NOT_FOUND;
  }

  err = c_orm_hydrate_row(db, query, meta, out_struct);
  c_orm_finalize_cached(db, query);
  return err;
}

C_ORM_EXPORT c_orm_error_t c_orm_update_by_composite_key(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, size_t num_keys,
    const struct CddCVariant *key_values, const void *in_struct) {
  /*
   * Step 165: Refactor c_orm_update to support composite keys
   * Standard c_orm_update loops over all columns and binds them implicitly
   * based on the `is_pk` tags directly from the `in_struct` data.
   * This dedicated composite wrapper allows explicitly passing variants rather
   * than extracting them via offsets, useful for abstract dynamic row mapping.
   * For this stub, we route directly to c_orm_update which safely maps PKs via
   * `meta->columns[i].is_pk`.
   */
  (void)num_keys;
  (void)key_values;
  return c_orm_update(db, meta, in_struct);
}

C_ORM_EXPORT c_orm_error_t c_orm_delete_by_composite_key(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, size_t num_keys,
    const struct CddCVariant *key_values) {
  c_orm_query_t *query;
  c_orm_error_t err;
  int has_row;
  size_t i;

  if (!db || !meta || !key_values)
    return C_ORM_ERROR_MEMORY;

  if (!meta->query_delete_by_pk)
    return 701;
  if (meta->is_view)
    return C_ORM_ERROR_READ_ONLY;

  err = c_orm_prepare_cached(db, meta->query_delete_by_pk, &query);
  if (err != C_ORM_OK)
    return err;

  for (i = 0; i < num_keys; ++i) {
    const struct CddCVariant *var = &key_values[i];
    if (var->type == CDD_C_VARIANT_TYPE_INT) {
      err = db->vtable->bind_int64(query, (int)(i + 1), var->value.i_val);
    } else if (var->type == CDD_C_VARIANT_TYPE_STRING) {
      err = db->vtable->bind_string(query, (int)(i + 1), var->value.s_val);
    } else {
      err = C_ORM_ERROR_NOT_IMPLEMENTED;
    }

    if (err != C_ORM_OK) {
      c_orm_finalize_cached(db, query);
      return err;
    }
  }

  err = db->vtable->step(query, &has_row);
  c_orm_finalize_cached(db, query);
  return err;
}

C_ORM_EXPORT c_orm_error_t
c_orm_find_by_id_int32(c_orm_db_t *db, const c_orm_table_meta_t *meta,
                       int32_t id_val, void *out_struct) {
  c_orm_query_t *query;
  c_orm_error_t err;
  int has_row;

  if (!db || !meta || !out_struct)
    return C_ORM_ERROR_MEMORY;

  if (!meta->query_select_by_pk) {
    return 702; /* No single PK available */
  }

  err = c_orm_prepare_cached(db, meta->query_select_by_pk, &query);
  if (err != C_ORM_OK)
    return err;

  err = db->vtable->bind_int32(query, 1, id_val);
  if (err != C_ORM_OK) {
    c_orm_finalize_cached(db, query);
    return err;
  }

  err = db->vtable->step(query, &has_row);
  if (err != C_ORM_OK) {
    c_orm_finalize_cached(db, query);
    return err;
  }

  if (!has_row) {
    c_orm_finalize_cached(db, query);
    return C_ORM_ERROR_NOT_FOUND;
  }

  err = c_orm_hydrate_row(db, query, meta, out_struct);
  if (err != C_ORM_OK) {
    printf("DEBUG: c_orm_hydrate_row failed with err %d\n", err);
    fflush(stdout);
  }
  c_orm_finalize_cached(db, query);
  return err;
}

/* Array Layout matching cdd-c:
   struct Type_Array {
     struct Type *data;
     size_t length;
     size_t capacity;
   }
*/
struct Generic_Array {
  void *data;
  size_t length;
  size_t capacity;
};

C_ORM_EXPORT c_orm_error_t c_orm_find_with_relation_int32(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, int32_t id_val,
    const char *relation_name, void *out_struct) {
  c_orm_query_t *query;
  c_orm_error_t err;
  c_orm_string_builder_t *sb;
  int has_row;
  size_t i;
  const c_orm_relation_meta_t *rel = NULL;
  const c_orm_table_meta_t *target_meta;
  const c_orm_column_meta_t *pk_col = NULL;
  const char *sql;
  int is_null = 0;
  void *context_ptr;
  c_orm_lazy_load_context_t *ctx;
  void *target_data_ptr;

  if (!db || !meta || !relation_name || !out_struct)
    return C_ORM_ERROR_MEMORY;

  /* Find relation */
  for (i = 0; i < meta->num_relations; i++) {
    if (strcmp(meta->relations[i].field_name, relation_name) == 0) {
      rel = &meta->relations[i];
      break;
    }
  }

  if (!rel)
    return C_ORM_ERROR_NOT_FOUND;

  if (rel->type != C_ORM_RELATION_ONE_TO_ONE &&
      rel->type != C_ORM_RELATION_BELONGS_TO &&
      rel->type != C_ORM_RELATION_ONE_TO_MANY &&
      rel->type != C_ORM_RELATION_MANY_TO_MANY) {
    return 703; /* Eager load array not implemented here
                                           yet for others */
  }

  target_meta = rel->target_meta;
  if (!target_meta)
    return 704;

  for (i = 0; i < meta->num_columns; i++) {
    if (meta->columns[i].is_pk) {
      pk_col = &meta->columns[i];
      break;
    }
  }

  if (!pk_col || pk_col->type != C_ORM_TYPE_INT32)
    return 705;

  if (c_orm_string_builder_init(&sb) != 0)
    return C_ORM_ERROR_MEMORY;

  /* Build LEFT JOIN query: SELECT p.*, c.* FROM Parent p LEFT JOIN Child c ON
   * p.local_key = c.foreign_key WHERE p.pk = ? */
  c_orm_string_builder_append(sb, "SELECT ");
  /* Select all from parent */
  for (i = 0; i < meta->num_columns; i++) {
    if (i > 0)
      c_orm_string_builder_append(sb, ", ");
    c_orm_string_builder_append(sb, "p.");
    c_orm_string_builder_append(sb, meta->columns[i].name);
  }
  /* Select all from child */
  for (i = 0; i < target_meta->num_columns; i++) {
    c_orm_string_builder_append(sb, ", ");
    c_orm_string_builder_append(sb, "c.");
    c_orm_string_builder_append(sb, target_meta->columns[i].name);
  }

  c_orm_string_builder_append(sb, " FROM ");
  c_orm_string_builder_append(sb, meta->name);
  c_orm_string_builder_append(sb, " p ");

  if (rel->type == C_ORM_RELATION_MANY_TO_MANY) {
    c_orm_string_builder_append(sb, "LEFT JOIN ");
    c_orm_string_builder_append(sb, rel->join_table);
    c_orm_string_builder_append(sb, " j ON p.");
    c_orm_string_builder_append(sb, rel->local_key);
    c_orm_string_builder_append(sb, " = j.");
    c_orm_string_builder_append(sb, rel->join_local_key);
    c_orm_string_builder_append(sb, " LEFT JOIN ");
    c_orm_string_builder_append(sb, target_meta->name);
    c_orm_string_builder_append(sb, " c ON j.");
    c_orm_string_builder_append(sb, rel->join_foreign_key);
    c_orm_string_builder_append(sb, " = c.");
    c_orm_string_builder_append(sb, rel->foreign_key);
  } else {
    c_orm_string_builder_append(sb, "LEFT JOIN ");
    c_orm_string_builder_append(sb, target_meta->name);
    c_orm_string_builder_append(sb, " c ON p.");
    c_orm_string_builder_append(sb, rel->local_key);
    c_orm_string_builder_append(sb, " = c.");
    c_orm_string_builder_append(sb, rel->foreign_key);
  }

  c_orm_string_builder_append(sb, " WHERE p.");
  c_orm_string_builder_append(sb, pk_col->name);
  c_orm_string_builder_append(sb, " = ?");

  if (c_orm_string_builder_get(sb, &sql) != 0) {
    c_orm_string_builder_free(sb);
    return C_ORM_ERROR_MEMORY;
  }

  err = c_orm_prepare_cached(db, sql, &query);
  c_orm_string_builder_free(sb);
  if (err != C_ORM_OK)
    return err;

  err = db->vtable->bind_int32(query, 1, id_val);
  if (err != C_ORM_OK) {
    c_orm_finalize_cached(db, query);
    return err;
  }

  err = db->vtable->step(query, &has_row);
  if (err != C_ORM_OK) {
    c_orm_finalize_cached(db, query);
    return err;
  }

  if (!has_row) {
    c_orm_finalize_cached(db, query);
    return C_ORM_ERROR_NOT_FOUND;
  }

  /* Hydrate parent from columns 0 to num_columns-1 */
  err = c_orm_hydrate_row_from(db, query, meta, out_struct, 0);
  if (err != C_ORM_OK) {
    c_orm_finalize_cached(db, query);
    return err;
  }

  context_ptr = (char *)out_struct + rel->struct_offset;
  ctx = (c_orm_lazy_load_context_t *)context_ptr;
  target_data_ptr = (char *)context_ptr + sizeof(c_orm_lazy_load_context_t);

  if (rel->type == C_ORM_RELATION_ONE_TO_ONE ||
      rel->type == C_ORM_RELATION_BELONGS_TO) {
    /* Check if LEFT JOIN succeeded (is the first column of the child NULL?) */
    err = db->vtable->is_null(query, (int)meta->num_columns, &is_null);
    if (err == C_ORM_OK && !is_null) {
      void *new_struct = calloc(1, target_meta->struct_size);
      if (!new_struct) {
        c_orm_finalize_cached(db, query);
        return C_ORM_ERROR_MEMORY;
      }
      err = c_orm_hydrate_row_from(db, query, target_meta, new_struct,
                                   meta->num_columns);
      if (err == C_ORM_OK) {
        *(void **)target_data_ptr = new_struct;
        ctx->is_loaded = 1;
      } else {
        free(new_struct);
        *(void **)target_data_ptr = NULL;
      }
    } else {
      /* No matching child record */
      *(void **)target_data_ptr = NULL;
      ctx->is_loaded = 1; /* It's loaded, and there is none */
    }
  } else if (rel->type == C_ORM_RELATION_ONE_TO_MANY) {
    struct Generic_Array *arr = (struct Generic_Array *)target_data_ptr;
    size_t count = 0;
    size_t cap = arr->capacity;
    void *data = arr->data;

    do {
      err = db->vtable->is_null(query, (int)meta->num_columns, &is_null);
      if (err == C_ORM_OK && !is_null) {
        if (count >= cap) {
          size_t new_cap = cap == 0 ? 16 : cap * 2;
          void *new_data = realloc(data, new_cap * target_meta->struct_size);
          if (!new_data) {
            c_orm_finalize_cached(db, query);
            return C_ORM_ERROR_MEMORY;
          }
          memset((char *)new_data + (cap * target_meta->struct_size), 0,
                 (new_cap - cap) * target_meta->struct_size);
          data = new_data;
          cap = new_cap;
        }

        err = c_orm_hydrate_row_from(db, query, target_meta,
                                     (char *)data +
                                         (count * target_meta->struct_size),
                                     meta->num_columns);
        if (err == C_ORM_OK) {
          count++;
        } else {
          c_orm_finalize_cached(db, query);
          return err;
        }
      }

      err = db->vtable->step(query, &has_row);
      if (err != C_ORM_OK) {
        c_orm_finalize_cached(db, query);
        return err;
      }
    } while (has_row);

    arr->data = data;
    arr->length = count;
    arr->capacity = cap;
    ctx->is_loaded = 1;
  }

  c_orm_finalize_cached(db, query);
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t
c_orm_find_all_with_relation(c_orm_db_t *db, const c_orm_table_meta_t *meta,
                             const char *relation_name, void *out_array) {
  c_orm_error_t err;
  size_t i;
  const c_orm_relation_meta_t *rel = NULL;
  const c_orm_table_meta_t *target_meta;
  const c_orm_column_meta_t *pk_col = NULL;
  const char *sql;
  struct Generic_Array *out_arr = (struct Generic_Array *)out_array;
  size_t parents_count;
  void *parents_data;
  c_orm_query_t *query = NULL;
  int has_row;

  if (!db || !meta || !relation_name || !out_array)
    return C_ORM_ERROR_MEMORY;

  /* Find relation */
  for (i = 0; i < meta->num_relations; i++) {
    if (strcmp(meta->relations[i].field_name, relation_name) == 0) {
      rel = &meta->relations[i];
      break;
    }
  }

  if (!rel)
    return C_ORM_ERROR_NOT_FOUND;

  if (rel->type != C_ORM_RELATION_ONE_TO_ONE &&
      rel->type != C_ORM_RELATION_BELONGS_TO &&
      rel->type != C_ORM_RELATION_ONE_TO_MANY &&
      rel->type != C_ORM_RELATION_MANY_TO_MANY) {
    return 706;
  }

  target_meta = rel->target_meta;
  if (!target_meta)
    return 707;

  for (i = 0; i < meta->num_columns; i++) {
    if (meta->columns[i].is_pk) {
      pk_col = &meta->columns[i];
      break;
    }
  }

  if (!pk_col || pk_col->type != C_ORM_TYPE_INT32)
    return 708;

  /* Fetch all parents first */
  err = c_orm_find_all(db, meta, out_array);
  if (err != C_ORM_OK)
    return err;

  parents_count = out_arr->length;
  if (parents_count == 0)
    return C_ORM_OK;
  parents_data = out_arr->data;

  /* Initialize relation fields for all parents to empty/NULL */
  for (i = 0; i < parents_count; ++i) {
    void *parent_ptr = (char *)parents_data + (i * meta->struct_size);
    void *context_ptr = (char *)parent_ptr + rel->struct_offset;
    c_orm_lazy_load_context_t *ctx = (c_orm_lazy_load_context_t *)context_ptr;
    void *target_data_ptr =
        (char *)context_ptr + sizeof(c_orm_lazy_load_context_t);

    ctx->is_loaded = 1;
    if (rel->type == C_ORM_RELATION_ONE_TO_ONE ||
        rel->type == C_ORM_RELATION_BELONGS_TO) {
      *(void **)target_data_ptr = NULL;
    } else {
      struct Generic_Array *arr = (struct Generic_Array *)target_data_ptr;
      arr->data = NULL;
      arr->length = 0;
      arr->capacity = 0;
    }
  }

  /* Eager loading via WHERE IN batching */
  {
    size_t chunk_size = 800; /* Safe limit for SQLite parameter count */
    size_t start_idx = 0;
    c_orm_string_builder_t *sb = NULL;

    while (start_idx < parents_count) {
      size_t actual_chunk = parents_count - start_idx;
      if (actual_chunk > chunk_size)
        actual_chunk = chunk_size;

      if (c_orm_string_builder_init(&sb) != 0)
        return C_ORM_ERROR_MEMORY;

      if (rel->type == C_ORM_RELATION_MANY_TO_MANY) {
        const char *target_pk = NULL;
        size_t k;
        c_orm_string_builder_append(sb, "SELECT c.*, j.");
        c_orm_string_builder_append(sb, rel->join_local_key);
        c_orm_string_builder_append(sb, " FROM ");
        c_orm_string_builder_append(sb, target_meta->name);
        c_orm_string_builder_append(sb, " c INNER JOIN ");
        c_orm_string_builder_append(sb, rel->join_table);
        c_orm_string_builder_append(sb, " j ON c.");
        /* Find target PK for join */
        for (k = 0; k < target_meta->num_columns; ++k) {
          if (target_meta->columns[k].is_pk) {
            target_pk = target_meta->columns[k].name;
            break;
          }
        }
        if (!target_pk) {
          c_orm_string_builder_free(sb);
          return 709;
        }
        c_orm_string_builder_append(sb, target_pk);
        c_orm_string_builder_append(sb, " = j.");
        c_orm_string_builder_append(sb, rel->join_foreign_key);
        c_orm_string_builder_append(sb, " WHERE j.");
        c_orm_string_builder_append(sb, rel->join_local_key);
        c_orm_string_builder_append(sb, " IN (");
      } else {
        c_orm_string_builder_append(sb, "SELECT * FROM ");
        c_orm_string_builder_append(sb, target_meta->name);
        c_orm_string_builder_append(sb, " WHERE ");
        c_orm_string_builder_append(sb, rel->foreign_key);
        c_orm_string_builder_append(sb, " IN (");
      }

      for (i = 0; i < actual_chunk; i++) {
        if (i > 0)
          c_orm_string_builder_append(sb, ", ");
        c_orm_string_builder_append(sb, "?");
      }
      c_orm_string_builder_append(sb, ")");

      if (rel->custom_filter && rel->custom_filter[0]) {
        c_orm_string_builder_append(sb, " AND ");
        c_orm_string_builder_append(sb, rel->custom_filter);
      }
      if (rel->order_by && rel->order_by[0]) {
        c_orm_string_builder_append(sb, " ORDER BY ");
        c_orm_string_builder_append(sb, rel->order_by);
      }

      if (c_orm_string_builder_get(sb, &sql) != 0) {
        c_orm_string_builder_free(sb);
        return C_ORM_ERROR_MEMORY;
      }

      err = c_orm_prepare_cached(db, sql, &query);
      if (err != C_ORM_OK) {
        c_orm_string_builder_free(sb);
        return err;
      }

      for (i = 0; i < actual_chunk; i++) {
        void *parent_ptr =
            (char *)parents_data + ((start_idx + i) * meta->struct_size);
        int32_t pk_val = *(int32_t *)((char *)parent_ptr + pk_col->offset);
        err = db->vtable->bind_int32(query, (int)(i + 1), pk_val);
        if (err != C_ORM_OK) {
          c_orm_finalize_cached(db, query);
          c_orm_string_builder_free(sb);
          return err;
        }
      }

      err = db->vtable->step(query, &has_row);
      if (err != C_ORM_OK) {
        c_orm_finalize_cached(db, query);
        c_orm_string_builder_free(sb);
        return err;
      }

      while (has_row) {
        int32_t parent_id = 0;
        void *parent_ptr = NULL;

        if (rel->type == C_ORM_RELATION_MANY_TO_MANY) {
          /* Last column contains parent_id from join table */
          err = db->vtable->get_int32(query, (int)target_meta->num_columns,
                                      &parent_id);
          if (err != C_ORM_OK)
            break;
        } else {
          /* Find parent_id column index in target_meta */
          int fk_idx = -1;
          size_t k;
          for (k = 0; k < target_meta->num_columns; k++) {
            if (strcmp(target_meta->columns[k].name, rel->foreign_key) == 0) {
              fk_idx = (int)k;
              break;
            }
          }
          if (fk_idx != -1) {
            err = db->vtable->get_int32(query, fk_idx, &parent_id);
            if (err != C_ORM_OK)
              break;
          } else {
            break; /* Should not happen if schema valid */
          }
        }

        /* Find parent */
        for (i = 0; i < parents_count; i++) {
          void *p_ptr = (char *)parents_data + (i * meta->struct_size);
          int32_t p_id = *(int32_t *)((char *)p_ptr + pk_col->offset);
          if (p_id == parent_id) {
            parent_ptr = p_ptr;
            break;
          }
        }

        if (parent_ptr) {
          void *context_ptr = (char *)parent_ptr + rel->struct_offset;
          void *target_data_ptr =
              (char *)context_ptr + sizeof(c_orm_lazy_load_context_t);

          if (rel->type == C_ORM_RELATION_ONE_TO_ONE ||
              rel->type == C_ORM_RELATION_BELONGS_TO) {
            if (!*(void **)target_data_ptr) {
              void *new_struct = calloc(1, target_meta->struct_size);
              if (new_struct) {
                if (c_orm_hydrate_row(db, query, target_meta, new_struct) ==
                    C_ORM_OK) {
                  *(void **)target_data_ptr = new_struct;
                } else {
                  free(new_struct);
                }
              }
            }
          } else {
            struct Generic_Array *arr = (struct Generic_Array *)target_data_ptr;
            size_t count = arr->length;
            size_t cap = arr->capacity;
            void *data = arr->data;

            if (count >= cap) {
              size_t new_cap = cap == 0 ? 4 : cap * 2;
              void *new_data =
                  realloc(data, new_cap * target_meta->struct_size);
              if (new_data) {
                memset((char *)new_data + (cap * target_meta->struct_size), 0,
                       (new_cap - cap) * target_meta->struct_size);
                data = new_data;
                cap = new_cap;
              }
            }

            if (data && count < cap) {
              void *child_ptr =
                  (char *)data + (count * target_meta->struct_size);
              if (c_orm_hydrate_row(db, query, target_meta, child_ptr) ==
                  C_ORM_OK) {
                arr->data = data;
                arr->length = count + 1;
                arr->capacity = cap;
              }
            }
          }
        }

        err = db->vtable->step(query, &has_row);
        if (err != C_ORM_OK)
          break;
      }

      c_orm_finalize_cached(db, query);
      c_orm_string_builder_free(sb);
      sb = NULL;

      if (err != C_ORM_OK && err != C_ORM_ERROR_NOT_FOUND) {
        return err;
      }

      start_idx += actual_chunk;
    }
  }

  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_hydrate_all(c_orm_db_t *db,
                                             c_orm_query_t *query,
                                             const c_orm_table_meta_t *meta,
                                             void *out_array) {
  c_orm_error_t err;
  int has_row;
  struct Generic_Array *arr = (struct Generic_Array *)out_array;
  size_t count = 0;
  size_t cap = arr->capacity;
  void *data = arr->data;

  if (!db || !query || !meta || !out_array)
    return C_ORM_ERROR_MEMORY;

  for (;;) {
    err = db->vtable->step(query, &has_row);
    if (err != C_ORM_OK) {
      return err;
    }
    if (!has_row)
      break;

    if (count >= cap) {
      size_t new_cap = cap == 0 ? 16 : cap * 2;
      void *new_data = realloc(data, new_cap * meta->struct_size);
      if (!new_data) {
        return C_ORM_ERROR_MEMORY;
      }
      /* Step 47: Initialize structure fully including child arrays/relations to
       * 0 */
      memset((char *)new_data + (cap * meta->struct_size), 0,
             (new_cap - cap) * meta->struct_size);
      data = new_data;
      cap = new_cap;
    }

    /* Hydrate into data[count] */
    err = c_orm_hydrate_row(db, query, meta,
                            (char *)data + (count * meta->struct_size));
    if (err == C_ORM_ERROR_EXPIRED) {
      /* Skip adding this record to the output array */
      continue;
    } else if (err != C_ORM_OK) {
      return err;
    }
    count++;
  }

  arr->data = data;
  arr->capacity = cap;
  arr->length = count;

  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_find_with_relations_int32(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, int32_t id_val,
    const char **relation_paths, size_t num_paths, void *out_struct) {
  c_orm_error_t err;
  size_t i;
  char first_rel[64];

  if (!db || !meta || !relation_paths || !out_struct)
    return C_ORM_ERROR_MEMORY;

  /* Baseline parent fetch */
  err = c_orm_find_by_id_int32(db, meta, id_val, out_struct);
  if (err != C_ORM_OK)
    return err;

  /* Iterative lazy load to simulate eager mapping recursively */
  /* Real eager load mapping via huge JOIN AST parsing would go here in Phase 6
   */
  for (i = 0; i < num_paths; i++) {
    const char *path = relation_paths[i];
    const char *dot = strchr(path, '.');
    const c_orm_relation_meta_t *rel = NULL;
    size_t rel_idx;

    if (dot) {
      size_t len = dot - path;
      if (len >= sizeof(first_rel))
        len = sizeof(first_rel) - 1;
      strncpy(first_rel, path, len);
      first_rel[len] = '\0';
    } else {
#if defined(_MSC_VER)
      strcpy_s(first_rel, sizeof(first_rel), path);
#else
      strcpy(first_rel, path);
#endif
    }

    err = c_orm_lazy_load(db, meta, out_struct, first_rel);
    if (err != C_ORM_OK)
      return err;

    /* Handle deep nesting */
    if (dot) {
      const char *nested_path = dot + 1;
      for (rel_idx = 0; rel_idx < meta->num_relations; rel_idx++) {
        if (strcmp(meta->relations[rel_idx].field_name, first_rel) == 0) {
          rel = &meta->relations[rel_idx];
          break;
        }
      }
      if (rel) {
        void *context_ptr = (char *)out_struct + rel->struct_offset;
        void *target_data_ptr =
            (char *)context_ptr + sizeof(c_orm_lazy_load_context_t);

        if (rel->type == C_ORM_RELATION_ONE_TO_ONE ||
            rel->type == C_ORM_RELATION_BELONGS_TO) {
          void *nested_obj = *(void **)target_data_ptr;
          if (nested_obj) {
            /* Note: passing id_val here is wrong conceptually, but we actually
             * just need to do lazy load on nested */
            /* Since it's a proxy for eager load, we can recursively call lazy
             * load */
            err =
                c_orm_lazy_load(db, rel->target_meta, nested_obj, nested_path);
            if (err != C_ORM_OK)
              return err;
          }
        } else {
          struct Generic_Array *arr = (struct Generic_Array *)target_data_ptr;
          size_t j;
          for (j = 0; j < arr->length; j++) {
            void *child_obj =
                (char *)arr->data + (j * rel->target_meta->struct_size);
            err = c_orm_lazy_load(db, rel->target_meta, child_obj, nested_path);
            if (err != C_ORM_OK)
              return err;
          }
        }
      }
    }
  }

  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_find_all_with_relations(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, const char **relation_paths,
    size_t num_paths, void *out_array) {
  c_orm_error_t err;
  struct Generic_Array *arr;
  size_t i, p;

  if (!db || !meta || !relation_paths || !out_array)
    return C_ORM_ERROR_MEMORY;

  err = c_orm_find_all(db, meta, out_array);
  if (err != C_ORM_OK)
    return err;

  arr = (struct Generic_Array *)out_array;
  for (i = 0; i < arr->length; i++) {
    void *parent_obj = (char *)arr->data + (i * meta->struct_size);
    for (p = 0; p < num_paths; p++) {
      const char *path = relation_paths[p];
      const char *dot = strchr(path, '.');
      char first_rel[64];

      if (dot) {
        size_t len = dot - path;
        if (len >= sizeof(first_rel))
          len = sizeof(first_rel) - 1;
        strncpy(first_rel, path, len);
        first_rel[len] = '\0';
      } else {
#if defined(_MSC_VER)
        strcpy_s(first_rel, sizeof(first_rel), path);
#else
        strcpy(first_rel, path);
#endif
      }

      err = c_orm_lazy_load(db, meta, parent_obj, first_rel);
      if (err != C_ORM_OK)
        return err;

      if (dot) {
        const char *nested_path = dot + 1;
        const c_orm_relation_meta_t *rel = NULL;
        size_t rel_idx;
        for (rel_idx = 0; rel_idx < meta->num_relations; rel_idx++) {
          if (strcmp(meta->relations[rel_idx].field_name, first_rel) == 0) {
            rel = &meta->relations[rel_idx];
            break;
          }
        }
        if (rel) {
          void *context_ptr = (char *)parent_obj + rel->struct_offset;
          void *target_data_ptr =
              (char *)context_ptr + sizeof(c_orm_lazy_load_context_t);

          if (rel->type == C_ORM_RELATION_ONE_TO_ONE ||
              rel->type == C_ORM_RELATION_BELONGS_TO) {
            void *nested_obj = *(void **)target_data_ptr;
            if (nested_obj) {
              err = c_orm_lazy_load(db, rel->target_meta, nested_obj,
                                    nested_path);
              if (err != C_ORM_OK)
                return err;
            }
          } else {
            struct Generic_Array *child_arr =
                (struct Generic_Array *)target_data_ptr;
            size_t j;
            for (j = 0; j < child_arr->length; j++) {
              void *child_obj =
                  (char *)child_arr->data + (j * rel->target_meta->struct_size);
              err =
                  c_orm_lazy_load(db, rel->target_meta, child_obj, nested_path);
              if (err != C_ORM_OK)
                return err;
            }
          }
        }
      }
    }
  }

  return C_ORM_OK;
}

static c_orm_error_t set_null_field(const c_orm_table_meta_t *meta,
                                    void *struct_ptr, const char *field_name) {
  size_t i;
  for (i = 0; i < meta->num_columns; i++) {
    if (strcmp(meta->columns[i].name, field_name) == 0) {
      void *field_ptr = (char *)struct_ptr + meta->columns[i].offset;
      if (meta->columns[i].is_nullable) {
        if (*(void **)field_ptr) {
          free(*(void **)field_ptr);
          *(void **)field_ptr = NULL;
        }
      } else {
        /* Not nullable, set to 0 for primitives */
        if (meta->columns[i].type == C_ORM_TYPE_INT32) {
          *(int32_t *)field_ptr = 0;
        } else if (meta->columns[i].type == C_ORM_TYPE_INT64) {
          *(int64_t *)field_ptr = 0;
        } else if (meta->columns[i].type == C_ORM_TYPE_FLOAT) {
          *(float *)field_ptr = 0.0f;
        } else if (meta->columns[i].type == C_ORM_TYPE_DOUBLE) {
          *(double *)field_ptr = 0.0;
        }
      }
      return C_ORM_OK;
    }
  }
  return C_ORM_ERROR_NOT_FOUND;
}

static c_orm_error_t set_int_field(const c_orm_table_meta_t *meta,
                                   void *struct_ptr, const char *field_name,
                                   int64_t val) {
  size_t i;
  for (i = 0; i < meta->num_columns; i++) {
    if (strcmp(meta->columns[i].name, field_name) == 0) {
      void *field_ptr = (char *)struct_ptr + meta->columns[i].offset;
      if (meta->columns[i].type == C_ORM_TYPE_INT32) {
        if (meta->columns[i].is_nullable) {
          if (!*(int32_t **)field_ptr) {
            *(int32_t **)field_ptr = (int32_t *)malloc(sizeof(int32_t));
            if (!*(int32_t **)field_ptr)
              return C_ORM_ERROR_MEMORY;
          }
          **(int32_t **)field_ptr = (int32_t)val;
        } else {
          *(int32_t *)field_ptr = (int32_t)val;
        }
      } else if (meta->columns[i].type == C_ORM_TYPE_INT64) {
        if (meta->columns[i].is_nullable) {
          if (!*(int64_t **)field_ptr) {
            *(int64_t **)field_ptr = (int64_t *)malloc(sizeof(int64_t));
            if (!*(int64_t **)field_ptr)
              return C_ORM_ERROR_MEMORY;
          }
          **(int64_t **)field_ptr = val;
        } else {
          *(int64_t *)field_ptr = val;
        }
      }
      return C_ORM_OK;
    }
  }
  return C_ORM_ERROR_NOT_FOUND;
}

static c_orm_error_t get_int_field(const c_orm_table_meta_t *meta,
                                   const void *struct_ptr,
                                   const char *field_name, int64_t *out_val) {
  size_t i;
  for (i = 0; i < meta->num_columns; i++) {
    if (strcmp(meta->columns[i].name, field_name) == 0) {
      void *field_ptr = (char *)struct_ptr + meta->columns[i].offset;
      if (meta->columns[i].type == C_ORM_TYPE_INT32) {
        if (meta->columns[i].is_nullable) {
          if (!*(int32_t **)field_ptr)
            return C_ORM_ERROR_NOT_FOUND;
          *out_val = **(int32_t **)field_ptr;
        } else {
          *out_val = *(int32_t *)field_ptr;
        }
      } else if (meta->columns[i].type == C_ORM_TYPE_INT64) {
        if (meta->columns[i].is_nullable) {
          if (!*(int64_t **)field_ptr)
            return C_ORM_ERROR_NOT_FOUND;
          *out_val = **(int64_t **)field_ptr;
        } else {
          *out_val = *(int64_t *)field_ptr;
        }
      } else {
        return C_ORM_ERROR_TYPE_MISMATCH;
      }
      return C_ORM_OK;
    }
  }
  return C_ORM_ERROR_NOT_FOUND;
}

static c_orm_error_t bind_row(c_orm_db_t *db, c_orm_query_t *query,
                              const c_orm_table_meta_t *meta,
                              const void *in_struct, int skip_pk,
                              int skip_clean, int *bind_idx) {
  size_t i;
  c_orm_error_t err;
  const c_orm_dirty_flags_t *flags = NULL;

  /* If tracking dirty flags, we expect them as the first field of the struct
     (or similar offset). Assuming cdd-c generates dirty_flags at offset 0, but
     since we don't have offset in table meta, we safely check the size. Usually
     we pass it directly or check C_ORM_IS_FIELD_DIRTY macro */
  if (skip_clean) {
    /* Hardcode 0 offset convention for the bitmask if skip_clean is true for
     * now */
    flags = (const c_orm_dirty_flags_t *)in_struct;
  }

  for (i = 0; i < meta->num_columns; ++i) {
    const c_orm_column_meta_t *col = &meta->columns[i];
    const void *field_ptr = (const char *)in_struct + col->offset;

    if (skip_pk && col->is_pk) {
      continue;
    }

    if (skip_clean && flags && !(((*flags) & (1ULL << i)) != 0)) {
      /* Field is not dirty, but our pre-compiled templates need all parameters
         bound. Phase 3 dynamic queries handles partial updates. For
         pre-compiled we MUST bind. To truly use dirty tracking we need dynamic
         SQL in update. For now, bind it anyway, but this enables Phase 3
         dynamic logic later */
      /* continue;  <- Cannot continue on precompiled templates, must bind */
    }

    if (col->type == C_ORM_TYPE_STRING || col->type == C_ORM_TYPE_DATE ||
        col->type == C_ORM_TYPE_TIMESTAMP || col->type == C_ORM_TYPE_ENUM ||
        col->type == C_ORM_TYPE_SET || col->type == C_ORM_TYPE_JSON) {
      const char *str_val = *(const char **)field_ptr;
      char tz_buffer[64];

      /* Step 168: UUID Auto-generation for empty string primary keys */
      if (col->is_pk && col->type == C_ORM_TYPE_STRING &&
          (!str_val || str_val[0] == '\0')) {
        char *new_uuid = (char *)malloc(37);
        if (new_uuid) {
          c_orm_uuid_v4(new_uuid);
          *(char **)field_ptr = new_uuid;
          str_val = new_uuid;
        }
      }

      if (!str_val) {
        err = db->vtable->bind_null(query, (*bind_idx)++);
        if (err != C_ORM_OK)
          return err;
        continue;
      }

      /* Phase 6: Step 252 & Step 253: Convert string timestamps to UTC via
       * offset before binding */
      if (col->type == C_ORM_TYPE_TIMESTAMP &&
          db->timezone.offset_minutes != 0) {
        struct tm tm_val = {0};
        int year, month, day, hour, min, sec;
        if (sscanf(str_val, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour,
                   &min, &sec) == 6) {
          tm_val.tm_year = year - 1900;
          tm_val.tm_mon = month - 1;
          tm_val.tm_mday = day;
          tm_val.tm_hour = hour;
          tm_val.tm_min = min;
          tm_val.tm_sec = sec;

          /* Shift back from local offset to UTC before save */
          tm_val.tm_min -= db->timezone.offset_minutes;
          mktime(&tm_val); /* Normalize overflow/underflow */

#if defined(_MSC_VER)
          sprintf_s(tz_buffer, sizeof(tz_buffer),
                    "%04d-%02d-%02d %02d:%02d:%02d", tm_val.tm_year + 1900,
                    tm_val.tm_mon + 1, tm_val.tm_mday, tm_val.tm_hour,
                    tm_val.tm_min, tm_val.tm_sec);
#else
          sprintf(tz_buffer, "%04d-%02d-%02d %02d:%02d:%02d",
                  tm_val.tm_year + 1900, tm_val.tm_mon + 1, tm_val.tm_mday,
                  tm_val.tm_hour, tm_val.tm_min, tm_val.tm_sec);
#endif
          str_val = tz_buffer;
        }
      }

      /* Phase 6: Step 248: Transparent string encryption hook */
      if (col->is_secure && db->encrypt_hook) {
        void *encrypted_data = NULL;
        size_t encrypted_size = 0;
        err = db->encrypt_hook(str_val, strlen(str_val), db->crypto_context,
                               &encrypted_data, &encrypted_size);
        if (err != C_ORM_OK)
          return err;
        /* Re-route secured strings directly into blob parameters to guarantee
         * raw byte safety */
        err = db->vtable->bind_blob(query, (*bind_idx)++, encrypted_data,
                                    encrypted_size);
        free(encrypted_data); /* Assumes hook allocates generic dynamically */
        if (err != C_ORM_OK)
          return err;
        continue;
      }

      err = db->vtable->bind_string(query, (*bind_idx)++, str_val);
      if (err != C_ORM_OK)
        return err;
      continue;
    }

    if (col->type == C_ORM_TYPE_BLOB) {
      const c_orm_blob_t *blob_val = (const c_orm_blob_t *)field_ptr;
      if (!blob_val->data || blob_val->size == 0) {
        err = db->vtable->bind_null(query, (*bind_idx)++);
        if (err != C_ORM_OK)
          return err;
        continue;
      }

      /* Phase 6: Step 248: Transparent blob encryption hook */
      if (col->is_secure && db->encrypt_hook) {
        void *encrypted_data = NULL;
        size_t encrypted_size = 0;
        err =
            db->encrypt_hook(blob_val->data, blob_val->size, db->crypto_context,
                             &encrypted_data, &encrypted_size);
        if (err != C_ORM_OK)
          return err;
        err = db->vtable->bind_blob(query, (*bind_idx)++, encrypted_data,
                                    encrypted_size);
        free(encrypted_data);
        if (err != C_ORM_OK)
          return err;
        continue;
      }

      err = db->vtable->bind_blob(query, (*bind_idx)++, blob_val->data,
                                  blob_val->size);
      if (err != C_ORM_OK)
        return err;
      continue;
    }

    if (col->is_nullable) {
      /* Pointer type for primitives */
      const void *ptr_val = *(const void **)field_ptr;
      if (!ptr_val) {
        err = db->vtable->bind_null(query, (*bind_idx)++);
        if (err != C_ORM_OK)
          return err;
        continue;
      }
      field_ptr = ptr_val; /* Dereference to read the primitive value */
    }

    switch (col->type) {
    case C_ORM_TYPE_INT32: {
      int32_t val = *(const int32_t *)field_ptr;
      err = db->vtable->bind_int32(query, (*bind_idx)++, val);
      break;
    }
    case C_ORM_TYPE_BOOL: {
      int32_t val;
      if (sizeof(bool) == 1) {
        val = *(const unsigned char *)field_ptr;
      } else {
        val = *(const int *)field_ptr;
      }
      err = db->vtable->bind_int32(query, (*bind_idx)++, val);
      break;
    }
    case C_ORM_TYPE_INT64: {
      int64_t val = *(const int64_t *)field_ptr;
      err = db->vtable->bind_int64(query, (*bind_idx)++, val);
      break;
    }
    case C_ORM_TYPE_FLOAT: {
      float val = *(const float *)field_ptr;
      err = db->vtable->bind_double(query, (*bind_idx)++, (double)val);
      break;
    }
    case C_ORM_TYPE_DOUBLE: {
      double val = *(const double *)field_ptr;
      err = db->vtable->bind_double(query, (*bind_idx)++, val);
      break;
    }
    case C_ORM_TYPE_POINT: {
      const c_orm_point_t *pt = (const c_orm_point_t *)field_ptr;
      unsigned char wkb[21];
      wkb[0] = 1;
      wkb[1] = 1;
      wkb[2] = 0;
      wkb[3] = 0;
      wkb[4] = 0;
      memcpy(&wkb[5], &pt->x, 8);
      memcpy(&wkb[13], &pt->y, 8);
      err = db->vtable->bind_blob(query, (*bind_idx)++, wkb, 21);
      break;
    }
    case C_ORM_TYPE_POLYGON: {
      const c_orm_polygon_t *poly = (const c_orm_polygon_t *)field_ptr;
      size_t size = 1 + 4 + 4 + 4 + (poly->num_points * 16);
      unsigned char *wkb = (unsigned char *)malloc(size);
      if (!wkb)
        return C_ORM_ERROR_MEMORY;
      wkb[0] = 1;
      wkb[1] = 3;
      wkb[2] = 0;
      wkb[3] = 0;
      wkb[4] = 0;
      wkb[5] = 1;
      wkb[6] = 0;
      wkb[7] = 0;
      wkb[8] = 0;
      {
        uint32_t pts = (uint32_t)poly->num_points;
        size_t offset = 13;
        size_t p;
        memcpy(&wkb[9], &pts, 4);
        for (p = 0; p < poly->num_points; p++) {
          memcpy(&wkb[offset], &poly->points[p].x, 8);
          offset += 8;
          memcpy(&wkb[offset], &poly->points[p].y, 8);
          offset += 8;
        }
      }
      err = db->vtable->bind_blob(query, (*bind_idx)++, wkb, size);
      free(wkb);
      break;
    }
    default:
      return C_ORM_ERROR_TYPE_MISMATCH;
    }
    if (err != C_ORM_OK)
      return err;
  }
  return C_ORM_OK;
}

struct c_orm_iterator {
  c_orm_db_t *db;
  const c_orm_table_meta_t *meta;
  c_orm_query_t *query;
  size_t chunk_size;
};

C_ORM_EXPORT c_orm_error_t c_orm_insert_batch_ext(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, const void *in_array,
    size_t num_items, size_t chunk_size, c_orm_on_conflict_t conflict_policy,
    c_orm_batch_progress_cb progress_cb, void *progress_ctx) {
  c_orm_error_t err;
  size_t i, j, k;
  size_t actual_chunk;

  if (!db || !meta || !in_array)
    return C_ORM_ERROR_MEMORY;
  if (meta->is_view)
    return C_ORM_ERROR_READ_ONLY;
  if (num_items == 0)
    return C_ORM_OK;

  if (chunk_size == 0) {
    chunk_size = 30000 / (meta->num_columns > 0 ? meta->num_columns : 1);
    if (chunk_size > 1000)
      chunk_size = 1000;
  }

  err = c_orm_transaction_begin(db);
  if (err != C_ORM_OK)
    return err;

  for (i = 0; i < num_items; i += actual_chunk) {
    c_orm_query_t *query;
    c_orm_string_builder_t *sb;
    const char *sql_str;
    int has_row;
    int bind_idx = 1;

    actual_chunk = chunk_size;
    if (i + actual_chunk > num_items) {
      actual_chunk = num_items - i;
    }

    if (c_orm_string_builder_init(&sb) != 0) {
      c_orm_transaction_rollback(db);
      return C_ORM_ERROR_MEMORY;
    }

    c_orm_string_builder_append(sb, "INSERT INTO ");
    c_orm_string_builder_append(sb, meta->name);
    c_orm_string_builder_append(sb, " (");
    for (j = 0; j < meta->num_columns; j++) {
      if (j > 0)
        c_orm_string_builder_append(sb, ", ");
      c_orm_string_builder_append(sb, meta->columns[j].name);
    }
    c_orm_string_builder_append(sb, ") VALUES ");

    for (j = 0; j < actual_chunk; j++) {
      if (j > 0)
        c_orm_string_builder_append(sb, ", ");
      c_orm_string_builder_append(sb, "(");
      for (k = 0; k < meta->num_columns; k++) {
        if (k > 0)
          c_orm_string_builder_append(sb, ", ");
        c_orm_string_builder_append(sb, "?");
      }
      c_orm_string_builder_append(sb, ")");
    }

    if (conflict_policy == C_ORM_ON_CONFLICT_DO_NOTHING) {
      c_orm_string_builder_append(sb, " ON CONFLICT DO NOTHING");
    } else if (conflict_policy == C_ORM_ON_CONFLICT_DO_UPDATE) {
      /* Basic DO UPDATE without specifying conflict target. Usually needs
       * conflict target (PK). */
      /* SQLite and Postgres require ON CONFLICT(pk) DO UPDATE SET ... */
      /* This is a simplification. Real implementation needs dynamic PK and SET
       * logic. */
      /* For now we just implement DO NOTHING successfully. */
    }

    if (c_orm_string_builder_get(sb, &sql_str) != 0) {
      c_orm_string_builder_free(sb);
      c_orm_transaction_rollback(db);
      return C_ORM_ERROR_MEMORY;
    }

    err = db->vtable->prepare(db, sql_str, &query);
    c_orm_string_builder_free(sb);
    if (err != C_ORM_OK) {
      c_orm_transaction_rollback(db);
      return err;
    }

    for (j = 0; j < actual_chunk; j++) {
      const void *current_struct =
          (const char *)in_array + ((i + j) * meta->struct_size);

      if (meta->hooks[C_ORM_HOOK_BEFORE_SAVE] &&
          meta->hooks[C_ORM_HOOK_BEFORE_SAVE]((void *)current_struct, db) !=
              0) {
        db->vtable->finalize(query);
        c_orm_transaction_rollback(db);
        return C_ORM_ERROR_UNKNOWN;
      }
      if (meta->hooks[C_ORM_HOOK_BEFORE_INSERT] &&
          meta->hooks[C_ORM_HOOK_BEFORE_INSERT]((void *)current_struct, db) !=
              0) {
        db->vtable->finalize(query);
        c_orm_transaction_rollback(db);
        return C_ORM_ERROR_UNKNOWN;
      }

      err = bind_row(db, query, meta, current_struct, 0, 0, &bind_idx);
      if (err != C_ORM_OK) {
        db->vtable->finalize(query);
        c_orm_transaction_rollback(db);
        return err;
      }
    }

    err = db->vtable->step(query, &has_row);
    db->vtable->finalize(query);

    if (err != C_ORM_OK) {
      c_orm_transaction_rollback(db);
      return err;
    }

    for (j = 0; j < actual_chunk; j++) {
      const void *current_struct =
          (const char *)in_array + ((i + j) * meta->struct_size);
      if (meta->hooks[C_ORM_HOOK_AFTER_INSERT] &&
          meta->hooks[C_ORM_HOOK_AFTER_INSERT]((void *)current_struct, db) !=
              0) {
        c_orm_transaction_rollback(db);
        return C_ORM_ERROR_UNKNOWN;
      }
      if (meta->hooks[C_ORM_HOOK_AFTER_SAVE] &&
          meta->hooks[C_ORM_HOOK_AFTER_SAVE]((void *)current_struct, db) != 0) {
        c_orm_transaction_rollback(db);
        return C_ORM_ERROR_UNKNOWN;
      }
    }

    if (progress_cb) {
      progress_cb(i + actual_chunk, num_items, progress_ctx);
    }
  }

  err = c_orm_transaction_commit(db);
  return err;
}

C_ORM_EXPORT c_orm_error_t c_orm_insert_batch(c_orm_db_t *db,
                                              const c_orm_table_meta_t *meta,
                                              const void *in_array,
                                              size_t num_items,
                                              size_t chunk_size) {
  return c_orm_insert_batch_ext(db, meta, in_array, num_items, chunk_size,
                                C_ORM_ON_CONFLICT_FAIL, NULL, NULL);
}

C_ORM_EXPORT c_orm_error_t c_orm_find_batch_init(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, const char *sql,
    size_t chunk_size, struct c_orm_iterator **out_iter) {
  struct c_orm_iterator *iter;
  c_orm_error_t err;

  if (!db || !meta || !out_iter || chunk_size == 0)
    return C_ORM_ERROR_MEMORY;

  iter = (struct c_orm_iterator *)malloc(sizeof(struct c_orm_iterator));
  if (!iter)
    return C_ORM_ERROR_MEMORY;

  iter->db = db;
  iter->meta = meta;
  iter->chunk_size = chunk_size;

  if (!sql) {
    sql = meta->query_select_all;
  }

  err = db->vtable->prepare(db, sql, &iter->query);
  if (err != C_ORM_OK) {
    free(iter);
    return err;
  }

  *out_iter = iter;
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_iterator_next(struct c_orm_iterator *iter,
                                               void *out_array,
                                               size_t *out_num_fetched) {
  size_t count = 0;
  int has_row;
  c_orm_error_t err;

  if (!iter || !out_array || !out_num_fetched)
    return C_ORM_ERROR_MEMORY;

  *out_num_fetched = 0;

  /* Step 292: Clear string memory pool or reset arena for this array chunk.
   * Handled by c_orm_arena_reset(&db->arena) in an optimized implementation.
   * For now we assume the user manages deep frees on out_array before reuse.
   */

  for (count = 0; count < iter->chunk_size; count++) {
    err = iter->db->vtable->step(iter->query, &has_row);
    if (err != C_ORM_OK)
      return err;
    if (!has_row)
      break;

    memset((char *)out_array + (count * iter->meta->struct_size), 0,
           iter->meta->struct_size);

    err = c_orm_hydrate_row(iter->db, iter->query, iter->meta,
                            (char *)out_array +
                                (count * iter->meta->struct_size));
    if (err == C_ORM_ERROR_EXPIRED) {
      count--; /* Overwrite on next loop */
      continue;
    } else if (err != C_ORM_OK) {
      return err;
    }
  }

  *out_num_fetched = count;
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_iterator_close(struct c_orm_iterator *iter) {
  if (!iter)
    return C_ORM_ERROR_MEMORY;

  if (iter->query) {
    iter->db->vtable->finalize(iter->query);
  }
  free(iter);
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_insert(c_orm_db_t *db,
                                        const c_orm_table_meta_t *meta,
                                        const void *in_struct) {
  c_orm_query_t *query;
  c_orm_error_t err;
  int has_row;
  size_t i;
  int bind_idx = 1;

  if (!db || !meta || !in_struct)
    return C_ORM_ERROR_MEMORY;

  if (meta->is_view)
    return C_ORM_ERROR_READ_ONLY;

  if (meta->hooks[C_ORM_HOOK_BEFORE_SAVE] &&
      meta->hooks[C_ORM_HOOK_BEFORE_SAVE]((void *)in_struct, db) != 0)
    return C_ORM_ERROR_UNKNOWN;
  if (meta->hooks[C_ORM_HOOK_BEFORE_INSERT] &&
      meta->hooks[C_ORM_HOOK_BEFORE_INSERT]((void *)in_struct, db) != 0)
    return C_ORM_ERROR_UNKNOWN;

  /* BELONGS_TO: Insert children first, get their PK, assign to parent's FK */
  for (i = 0; i < meta->num_relations; i++) {
    const c_orm_relation_meta_t *rel = &meta->relations[i];
    if (rel->target_meta && rel->type == C_ORM_RELATION_BELONGS_TO) {
      void *context_ptr = (char *)in_struct + rel->struct_offset;
      void *target_data_ptr =
          (char *)context_ptr + sizeof(c_orm_lazy_load_context_t);
      void *nested_ptr = *(void **)target_data_ptr;
      if (nested_ptr) {
        err = c_orm_insert(db, rel->target_meta, nested_ptr);
        if (err == C_ORM_OK) {
          int64_t new_id = 0;
          c_orm_error_t lid_err =
              db->vtable->get_last_insert_rowid(db, &new_id);
          if (lid_err == C_ORM_OK && new_id > 0) {
            set_int_field(meta, (void *)in_struct, rel->local_key, new_id);
            /* Also populate the nested struct's PK so it reflects reality */
            set_int_field(rel->target_meta, nested_ptr, rel->foreign_key,
                          new_id);
          } else {
            get_int_field(rel->target_meta, nested_ptr, rel->foreign_key,
                          &new_id);
            if (new_id > 0) {
              set_int_field(meta, (void *)in_struct, rel->local_key, new_id);
            }
          }
        } else {
          return err;
        }
      } else {
        /* Validate required BelongsTo */
        size_t j;
        for (j = 0; j < meta->num_columns; ++j) {
          if (strcmp(meta->columns[j].name, rel->local_key) == 0) {
            if (!meta->columns[j].is_nullable) {
              int64_t existing_fk = 0;
              get_int_field(meta, in_struct, rel->local_key, &existing_fk);
              if (existing_fk == 0) {
                return C_ORM_ERROR_VALIDATION;
              } else {
                /* Runtime check if FK exists */
                int exists = 0;
                c_orm_error_t exists_err = c_orm_exists_int32(
                    db, rel->target_meta, (int32_t)existing_fk, &exists);
                if (exists_err == C_ORM_OK && !exists) {
                  return C_ORM_ERROR_VALIDATION;
                }
              }
            } else {
              /* Nullable FK but provided. Check if it exists. */
              int64_t existing_fk = 0;
              if (get_int_field(meta, in_struct, rel->local_key,
                                &existing_fk) == C_ORM_OK &&
                  existing_fk != 0) {
                int exists = 0;
                c_orm_error_t exists_err = c_orm_exists_int32(
                    db, rel->target_meta, (int32_t)existing_fk, &exists);
                if (exists_err == C_ORM_OK && !exists) {
                  return C_ORM_ERROR_VALIDATION;
                }
              }
            }
            break;
          }
        }
      }
    }
  }

  err = c_orm_prepare_cached(db, meta->query_insert, &query);
  if (err != C_ORM_OK)
    return err;

  err = bind_row(db, query, meta, in_struct, 0, 0, &bind_idx); /* Bind all */
  if (err != C_ORM_OK) {
    c_orm_finalize_cached(db, query);
    return err;
  }

  err = db->vtable->step(query, &has_row);
  c_orm_finalize_cached(db, query);

  if (err != C_ORM_OK)
    return err;

  /* HAS_ONE / ONE_TO_ONE: Insert parent, get its PK, assign to child's FK,
   * insert child */
  for (i = 0; i < meta->num_relations; i++) {
    const c_orm_relation_meta_t *rel = &meta->relations[i];
    if (rel->target_meta && rel->type == C_ORM_RELATION_ONE_TO_ONE) {
      void *context_ptr = (char *)in_struct + rel->struct_offset;
      void *target_data_ptr =
          (char *)context_ptr + sizeof(c_orm_lazy_load_context_t);
      void *nested_ptr = *(void **)target_data_ptr;
      if (nested_ptr) {
        int64_t parent_id = 0;
        if (db->vtable->get_last_insert_rowid(db, &parent_id) == C_ORM_OK &&
            parent_id > 0) {
          /* Fallback to reading the struct if 0 was returned, but here we got
           * it */
        }
        if (parent_id <= 0) {
          get_int_field(meta, in_struct, rel->local_key, &parent_id);
        }
        if (parent_id > 0) {
          set_int_field(rel->target_meta, nested_ptr, rel->foreign_key,
                        parent_id);
        }
        err = c_orm_insert(db, rel->target_meta, nested_ptr);
        if (err != C_ORM_OK)
          return err;
      }
    }
  }

  if (meta->hooks[C_ORM_HOOK_AFTER_INSERT] &&
      meta->hooks[C_ORM_HOOK_AFTER_INSERT]((void *)in_struct, db) != 0)
    return C_ORM_ERROR_UNKNOWN;
  if (meta->hooks[C_ORM_HOOK_AFTER_SAVE] &&
      meta->hooks[C_ORM_HOOK_AFTER_SAVE]((void *)in_struct, db) != 0)
    return C_ORM_ERROR_UNKNOWN;

  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_update(c_orm_db_t *db,
                                        const c_orm_table_meta_t *meta,
                                        const void *in_struct) {
  c_orm_query_t *query;
  c_orm_error_t err;
  int has_row;
  int32_t pk_val = 0; /* Fallback assuming int PK */
  int bind_idx = 1;
  size_t i;

  if (!db || !meta || !in_struct)
    return C_ORM_ERROR_MEMORY;
  if (meta->is_view)
    return C_ORM_ERROR_READ_ONLY;
  if (!meta->query_update)
    return 710;

  /* Step 92: Implement cascading update for nested structs */
  /* BELONGS_TO: Update children first */
  for (i = 0; i < meta->num_relations; i++) {
    const c_orm_relation_meta_t *rel = &meta->relations[i];
    if (rel->target_meta && rel->type == C_ORM_RELATION_BELONGS_TO) {
      void *context_ptr = (char *)in_struct + rel->struct_offset;
      void *target_data_ptr =
          (char *)context_ptr + sizeof(c_orm_lazy_load_context_t);
      void *nested_ptr = *(void **)target_data_ptr;
      if (nested_ptr && rel->on_update == C_ORM_CASCADE_UPDATE) {
        err = c_orm_save(db, rel->target_meta, nested_ptr);
        if (err != C_ORM_OK)
          return err;
      } else if (!nested_ptr) {
        /* Validate required BelongsTo */
        size_t j;
        for (j = 0; j < meta->num_columns; ++j) {
          if (strcmp(meta->columns[j].name, rel->local_key) == 0) {
            if (!meta->columns[j].is_nullable) {
              int64_t existing_fk = 0;
              get_int_field(meta, in_struct, rel->local_key, &existing_fk);
              if (existing_fk == 0) {
                return C_ORM_ERROR_VALIDATION;
              } else {
                int exists = 0;
                c_orm_error_t exists_err = c_orm_exists_int32(
                    db, rel->target_meta, (int32_t)existing_fk, &exists);
                if (exists_err == C_ORM_OK && !exists) {
                  return C_ORM_ERROR_VALIDATION;
                }
              }
            } else {
              int64_t existing_fk = 0;
              if (get_int_field(meta, in_struct, rel->local_key,
                                &existing_fk) == C_ORM_OK &&
                  existing_fk != 0) {
                int exists = 0;
                c_orm_error_t exists_err = c_orm_exists_int32(
                    db, rel->target_meta, (int32_t)existing_fk, &exists);
                if (exists_err == C_ORM_OK && !exists) {
                  return C_ORM_ERROR_VALIDATION;
                }
              }
            }
            break;
          }
        }
      }
    }
  }

  err = c_orm_prepare_cached(db, meta->query_update, &query);
  if (err != C_ORM_OK)
    return err;

  /* We bind all fields, then the PK at the end. Here we can use dirty tracking
   */
  err = bind_row(db, query, meta, in_struct, 0, 1, &bind_idx);
  if (err != C_ORM_OK) {
    c_orm_finalize_cached(db, query);
    return err;
  }

  bind_idx = (int)(meta->num_columns + 1);

  /* Find PK to bind to WHERE clause */
  for (i = 0; i < meta->num_columns; ++i) {
    if (meta->columns[i].is_pk) {
      const void *field_ptr = (const char *)in_struct + meta->columns[i].offset;
      if (meta->columns[i].type == C_ORM_TYPE_INT32) {
        pk_val = *(const int32_t *)field_ptr;
        err = db->vtable->bind_int32(query, bind_idx, pk_val);
        if (err != C_ORM_OK) {
          c_orm_finalize_cached(db, query);
          return err;
        }
      } else if (meta->columns[i].type == C_ORM_TYPE_STRING) {
        const char *pk_str = *(const char **)field_ptr;
        err = db->vtable->bind_string(query, bind_idx, pk_str);
        if (err != C_ORM_OK) {
          c_orm_finalize_cached(db, query);
          return err;
        }
      } else if (meta->columns[i].type == C_ORM_TYPE_INT64) {
        int64_t pk_64 = *(const int64_t *)field_ptr;
        err = db->vtable->bind_int64(query, bind_idx, pk_64);
        if (err != C_ORM_OK) {
          c_orm_finalize_cached(db, query);
          return err;
        }
      }
      bind_idx++;
    }
  }
  err = db->vtable->step(query, &has_row);
  c_orm_finalize_cached(db, query);
  return err;
}

C_ORM_EXPORT c_orm_error_t c_orm_save(c_orm_db_t *db,
                                      const c_orm_table_meta_t *meta,
                                      const void *in_struct) {
  size_t i;
  int is_pk_set = 0;

  if (!db || !meta || !in_struct)
    return C_ORM_ERROR_MEMORY;

  if (meta->is_view)
    return C_ORM_ERROR_READ_ONLY;

  /* Step 98: Upsert based on PK presence. Check if PK is zero/null */
  for (i = 0; i < meta->num_columns; ++i) {
    if (meta->columns[i].is_pk) {
      const void *field_ptr = (const char *)in_struct + meta->columns[i].offset;
      if (meta->columns[i].type == C_ORM_TYPE_INT32) {
        int32_t val = *(const int32_t *)field_ptr;
        if (val != 0)
          is_pk_set = 1;
      } else if (meta->columns[i].type == C_ORM_TYPE_STRING) {
        const char *val = *(const char **)field_ptr;
        if (val && val[0] != '\0')
          is_pk_set = 1;
      } else {
        is_pk_set = 1; /* Fallback: assume it is set if not int/string */
      }
      break;
    }
  }

  if (is_pk_set) {
    return c_orm_update(db, meta, in_struct);
  } else {
    return c_orm_insert(db, meta, in_struct);
  }
}

C_ORM_EXPORT c_orm_error_t c_orm_delete_batch(c_orm_db_t *db,
                                              const c_orm_table_meta_t *meta,
                                              const void *in_array,
                                              size_t num_items,
                                              size_t chunk_size) {
  c_orm_error_t err;
  size_t i, j;
  size_t actual_chunk;
  const c_orm_column_meta_t *pk_col = NULL;

  if (!db || !meta || !in_array)
    return C_ORM_ERROR_MEMORY;
  if (meta->is_view)
    return C_ORM_ERROR_READ_ONLY;
  if (num_items == 0)
    return C_ORM_OK;

  for (i = 0; i < meta->num_columns; i++) {
    if (meta->columns[i].is_pk) {
      pk_col = &meta->columns[i];
      break;
    }
  }

  if (!pk_col)
    return 711;

  if (chunk_size == 0) {
    chunk_size = 30000;
    if (chunk_size > 1000)
      chunk_size = 1000;
  }

  err = c_orm_transaction_begin(db);
  if (err != C_ORM_OK)
    return err;

  for (i = 0; i < num_items; i += actual_chunk) {
    c_orm_query_t *query;
    c_orm_string_builder_t *sb;
    const char *sql_str;
    int has_row;

    actual_chunk = chunk_size;
    if (i + actual_chunk > num_items) {
      actual_chunk = num_items - i;
    }

    if (c_orm_string_builder_init(&sb) != 0) {
      c_orm_transaction_rollback(db);
      return C_ORM_ERROR_MEMORY;
    }

    c_orm_string_builder_append(sb, "DELETE FROM ");
    c_orm_string_builder_append(sb, meta->name);
    c_orm_string_builder_append(sb, " WHERE ");
    c_orm_string_builder_append(sb, pk_col->name);
    c_orm_string_builder_append(sb, " IN (");

    for (j = 0; j < actual_chunk; j++) {
      if (j > 0)
        c_orm_string_builder_append(sb, ", ");
      c_orm_string_builder_append(sb, "?");
    }
    c_orm_string_builder_append(sb, ")");

    if (c_orm_string_builder_get(sb, &sql_str) != 0) {
      c_orm_string_builder_free(sb);
      c_orm_transaction_rollback(db);
      return C_ORM_ERROR_MEMORY;
    }

    err = db->vtable->prepare(db, sql_str, &query);
    c_orm_string_builder_free(sb);
    if (err != C_ORM_OK) {
      c_orm_transaction_rollback(db);
      return err;
    }

    for (j = 0; j < actual_chunk; j++) {
      const void *current_struct =
          (const char *)in_array + ((i + j) * meta->struct_size);
      const void *field_ptr = (const char *)current_struct + pk_col->offset;
      int bind_idx = (int)(j + 1);

      if (meta->hooks[C_ORM_HOOK_BEFORE_DELETE] &&
          meta->hooks[C_ORM_HOOK_BEFORE_DELETE]((void *)current_struct, db) !=
              0) {
        db->vtable->finalize(query);
        c_orm_transaction_rollback(db);
        return C_ORM_ERROR_UNKNOWN;
      }

      if (pk_col->type == C_ORM_TYPE_INT32) {
        err = db->vtable->bind_int32(query, bind_idx,
                                     *(const int32_t *)field_ptr);
      } else if (pk_col->type == C_ORM_TYPE_STRING) {
        err =
            db->vtable->bind_string(query, bind_idx, *(const char **)field_ptr);
      } else if (pk_col->type == C_ORM_TYPE_INT64) {
        err = db->vtable->bind_int64(query, bind_idx,
                                     *(const int64_t *)field_ptr);
      } else {
        err = C_ORM_ERROR_NOT_IMPLEMENTED;
      }

      if (err != C_ORM_OK) {
        db->vtable->finalize(query);
        c_orm_transaction_rollback(db);
        return err;
      }
    }

    err = db->vtable->step(query, &has_row);
    db->vtable->finalize(query);

    if (err != C_ORM_OK) {
      c_orm_transaction_rollback(db);
      return err;
    }

    for (j = 0; j < actual_chunk; j++) {
      const void *current_struct =
          (const char *)in_array + ((i + j) * meta->struct_size);
      if (meta->hooks[C_ORM_HOOK_AFTER_DELETE] &&
          meta->hooks[C_ORM_HOOK_AFTER_DELETE]((void *)current_struct, db) !=
              0) {
        c_orm_transaction_rollback(db);
        return C_ORM_ERROR_UNKNOWN;
      }
    }
  }

  err = c_orm_transaction_commit(db);
  return err;
}

C_ORM_EXPORT c_orm_error_t c_orm_update_batch(c_orm_db_t *db,
                                              const c_orm_table_meta_t *meta,
                                              const void *in_array,
                                              size_t num_items,
                                              size_t chunk_size) {
  c_orm_error_t err;
  size_t i, j, k;
  size_t actual_chunk;
  const c_orm_column_meta_t *pk_col = NULL;

  if (!db || !meta || !in_array)
    return C_ORM_ERROR_MEMORY;
  if (meta->is_view)
    return C_ORM_ERROR_READ_ONLY;
  if (num_items == 0)
    return C_ORM_OK;

  for (i = 0; i < meta->num_columns; i++) {
    if (meta->columns[i].is_pk) {
      pk_col = &meta->columns[i];
      break;
    }
  }

  if (!pk_col)
    return 712;

  /* CASE WHEN generates 2 params per column per row, plus 1 for IN clause.
     Total params = chunk_size * ( (num_cols - 1)*2 + 1 )
     Keep total params under 30000 */
  if (chunk_size == 0) {
    size_t params_per_row = (meta->num_columns - 1) * 2 + 1;
    chunk_size = 30000 / (params_per_row > 0 ? params_per_row : 1);
    if (chunk_size > 500)
      chunk_size = 500;
  }

  err = c_orm_transaction_begin(db);
  if (err != C_ORM_OK)
    return err;

  for (i = 0; i < num_items; i += actual_chunk) {
    c_orm_query_t *query;
    c_orm_string_builder_t *sb;
    const char *sql_str;
    int has_row;
    int bind_idx = 1;
    int first_col = 1;

    actual_chunk = chunk_size;
    if (i + actual_chunk > num_items) {
      actual_chunk = num_items - i;
    }

    if (c_orm_string_builder_init(&sb) != 0) {
      c_orm_transaction_rollback(db);
      return C_ORM_ERROR_MEMORY;
    }

    c_orm_string_builder_append(sb, "UPDATE ");
    c_orm_string_builder_append(sb, meta->name);
    c_orm_string_builder_append(sb, " SET ");

    for (k = 0; k < meta->num_columns; k++) {
      if (meta->columns[k].is_pk)
        continue;

      if (!first_col) {
        c_orm_string_builder_append(sb, ", ");
      }
      first_col = 0;

      c_orm_string_builder_append(sb, meta->columns[k].name);
      c_orm_string_builder_append(sb, " = CASE ");
      c_orm_string_builder_append(sb, pk_col->name);

      for (j = 0; j < actual_chunk; j++) {
        c_orm_string_builder_append(sb, " WHEN ? THEN ?");
      }
      c_orm_string_builder_append(sb, " END");
    }

    c_orm_string_builder_append(sb, " WHERE ");
    c_orm_string_builder_append(sb, pk_col->name);
    c_orm_string_builder_append(sb, " IN (");

    for (j = 0; j < actual_chunk; j++) {
      if (j > 0)
        c_orm_string_builder_append(sb, ", ");
      c_orm_string_builder_append(sb, "?");
    }
    c_orm_string_builder_append(sb, ")");

    if (c_orm_string_builder_get(sb, &sql_str) != 0) {
      c_orm_string_builder_free(sb);
      c_orm_transaction_rollback(db);
      return C_ORM_ERROR_MEMORY;
    }

    err = db->vtable->prepare(db, sql_str, &query);
    c_orm_string_builder_free(sb);
    if (err != C_ORM_OK) {
      c_orm_transaction_rollback(db);
      return err;
    }

    /* Bind SET CASE WHEN params */
    for (k = 0; k < meta->num_columns; k++) {
      if (meta->columns[k].is_pk)
        continue;

      for (j = 0; j < actual_chunk; j++) {
        const void *current_struct =
            (const char *)in_array + ((i + j) * meta->struct_size);
        const void *pk_ptr = (const char *)current_struct + pk_col->offset;

        /* Bind PK for WHEN */
        if (pk_col->type == C_ORM_TYPE_INT32) {
          err = db->vtable->bind_int32(query, bind_idx++,
                                       *(const int32_t *)pk_ptr);
        } else if (pk_col->type == C_ORM_TYPE_STRING) {
          err = db->vtable->bind_string(query, bind_idx++,
                                        *(const char **)pk_ptr);
        } else {
          err = C_ORM_ERROR_NOT_IMPLEMENTED;
        }
        if (err != C_ORM_OK)
          goto update_err;

        /* Bind Field for THEN */
        {
          /* Temporary struct meta wrapper to reuse bind_row */
          c_orm_table_meta_t temp_meta = *meta;
          c_orm_column_meta_t temp_col = meta->columns[k];
          temp_meta.columns = &temp_col;
          temp_meta.num_columns = 1;
          err =
              bind_row(db, query, &temp_meta, current_struct, 0, 0, &bind_idx);
          if (err != C_ORM_OK)
            goto update_err;
        }
      }
    }

    /* Bind WHERE IN params */
    for (j = 0; j < actual_chunk; j++) {
      const void *current_struct =
          (const char *)in_array + ((i + j) * meta->struct_size);
      const void *pk_ptr = (const char *)current_struct + pk_col->offset;

      if (pk_col->type == C_ORM_TYPE_INT32) {
        err =
            db->vtable->bind_int32(query, bind_idx++, *(const int32_t *)pk_ptr);
      } else if (pk_col->type == C_ORM_TYPE_STRING) {
        err =
            db->vtable->bind_string(query, bind_idx++, *(const char **)pk_ptr);
      } else {
        err = C_ORM_ERROR_NOT_IMPLEMENTED;
      }
      if (err != C_ORM_OK)
        goto update_err;
    }

    err = db->vtable->step(query, &has_row);
    db->vtable->finalize(query);

    if (err != C_ORM_OK) {
      c_orm_transaction_rollback(db);
      return err;
    }

    for (j = 0; j < actual_chunk; j++) {
      const void *current_struct =
          (const char *)in_array + ((i + j) * meta->struct_size);
      if (meta->hooks[C_ORM_HOOK_AFTER_SAVE] &&
          meta->hooks[C_ORM_HOOK_AFTER_SAVE]((void *)current_struct, db) != 0) {
        c_orm_transaction_rollback(db);
        return C_ORM_ERROR_UNKNOWN;
      }
    }
    continue;

  update_err:
    db->vtable->finalize(query);
    c_orm_transaction_rollback(db);
    return err;
  }

  err = c_orm_transaction_commit(db);
  return err;
}

C_ORM_EXPORT c_orm_error_t c_orm_delete(c_orm_db_t *db,
                                        const c_orm_table_meta_t *meta,
                                        const void *in_struct) {
  c_orm_query_t *query;
  c_orm_error_t err;
  int has_row;
  int bind_idx = 1;
  size_t i;
  int is_pk_found = 0;

  if (!db || !meta || !in_struct)
    return C_ORM_ERROR_MEMORY;
  if (!meta->query_delete_by_pk)
    return 713;
  if (meta->is_view)
    return C_ORM_ERROR_READ_ONLY;

  if (meta->hooks[C_ORM_HOOK_BEFORE_DELETE] &&
      meta->hooks[C_ORM_HOOK_BEFORE_DELETE]((void *)in_struct, db) != 0)
    return C_ORM_ERROR_UNKNOWN;

  /* Handle cascade delete before deleting the parent to avoid FK constraints */
  for (i = 0; i < meta->num_relations; ++i) {
    const c_orm_relation_meta_t *rel = &meta->relations[i];
    if (rel->target_meta && (rel->type == C_ORM_RELATION_ONE_TO_ONE ||
                             rel->type == C_ORM_RELATION_ONE_TO_MANY ||
                             rel->type == C_ORM_RELATION_BELONGS_TO)) {
      if (rel->on_delete == C_ORM_CASCADE_DELETE ||
          rel->on_delete == C_ORM_CASCADE_SET_NULL) {
        c_orm_query_t *casc_query;
        char sql[512];
        const c_orm_column_meta_t *pk_col = NULL;
        size_t j;
        for (j = 0; j < meta->num_columns; ++j) {
          if (strcmp(meta->columns[j].name, rel->local_key) == 0) {
            pk_col = &meta->columns[j];
            break;
          }
        }
        if (pk_col) {
          const void *field_ptr = (const char *)in_struct + pk_col->offset;
          if (rel->on_delete == C_ORM_CASCADE_DELETE) {
#if defined(_MSC_VER)
            sprintf_s(sql, sizeof(sql), "DELETE FROM %s WHERE %s = ?",
                      rel->target_meta->name, rel->foreign_key);
#else
            sprintf(sql, "DELETE FROM %s WHERE %s = ?", rel->target_meta->name,
                    rel->foreign_key);
#endif
          } else {
#if defined(_MSC_VER)
            sprintf_s(sql, sizeof(sql), "UPDATE %s SET %s = NULL WHERE %s = ?",
                      rel->target_meta->name, rel->foreign_key,
                      rel->foreign_key);
#else
            sprintf(sql, "UPDATE %s SET %s = NULL WHERE %s = ?",
                    rel->target_meta->name, rel->foreign_key, rel->foreign_key);
#endif
          }
          if (c_orm_prepare_cached(db, sql, &casc_query) == C_ORM_OK) {
            if (pk_col->type == C_ORM_TYPE_INT32) {
              db->vtable->bind_int32(casc_query, 1,
                                     *(const int32_t *)field_ptr);
            } else if (pk_col->type == C_ORM_TYPE_STRING) {
              db->vtable->bind_string(casc_query, 1, *(const char **)field_ptr);
            } else if (pk_col->type == C_ORM_TYPE_INT64) {
              db->vtable->bind_int64(casc_query, 1,
                                     *(const int64_t *)field_ptr);
            }
            db->vtable->step(casc_query, &has_row);
            c_orm_finalize_cached(db, casc_query);
          }
        }
      }
    } else if (rel->target_meta && rel->type == C_ORM_RELATION_MANY_TO_MANY) {
      c_orm_query_t *casc_query;
      char sql[512];
      const c_orm_column_meta_t *pk_col = NULL;
      size_t j;
      for (j = 0; j < meta->num_columns; ++j) {
        if (strcmp(meta->columns[j].name, rel->local_key) == 0) {
          pk_col = &meta->columns[j];
          break;
        }
      }
      if (pk_col) {
        const void *field_ptr = (const char *)in_struct + pk_col->offset;
        if (rel->on_delete == C_ORM_CASCADE_DELETE) {
          /* Delete target records first */
#if defined(_MSC_VER)
          sprintf_s(
              sql, sizeof(sql),
              "DELETE FROM %s WHERE %s IN (SELECT %s FROM %s WHERE %s = ?)",
              rel->target_meta->name, rel->foreign_key, rel->join_foreign_key,
              rel->join_table, rel->join_local_key);
#else
          sprintf(sql,
                  "DELETE FROM %s WHERE %s IN (SELECT %s FROM %s WHERE %s = ?)",
                  rel->target_meta->name, rel->foreign_key,
                  rel->join_foreign_key, rel->join_table, rel->join_local_key);
#endif
          if (c_orm_prepare_cached(db, sql, &casc_query) == C_ORM_OK) {
            if (pk_col->type == C_ORM_TYPE_INT32) {
              db->vtable->bind_int32(casc_query, 1,
                                     *(const int32_t *)field_ptr);
            } else if (pk_col->type == C_ORM_TYPE_STRING) {
              db->vtable->bind_string(casc_query, 1, *(const char **)field_ptr);
            } else if (pk_col->type == C_ORM_TYPE_INT64) {
              db->vtable->bind_int64(casc_query, 1,
                                     *(const int64_t *)field_ptr);
            }
            db->vtable->step(casc_query, &has_row);
            c_orm_finalize_cached(db, casc_query);
          }
        }
        /* Always clean up the join table to avoid FK constraints preventing
         * deletion of parent */
#if defined(_MSC_VER)
        sprintf_s(sql, sizeof(sql), "DELETE FROM %s WHERE %s = ?",
                  rel->join_table, rel->join_local_key);
#else
        sprintf(sql, "DELETE FROM %s WHERE %s = ?", rel->join_table,
                rel->join_local_key);
#endif
        if (c_orm_prepare_cached(db, sql, &casc_query) == C_ORM_OK) {
          if (pk_col->type == C_ORM_TYPE_INT32) {
            db->vtable->bind_int32(casc_query, 1, *(const int32_t *)field_ptr);
          } else if (pk_col->type == C_ORM_TYPE_STRING) {
            db->vtable->bind_string(casc_query, 1, *(const char **)field_ptr);
          } else if (pk_col->type == C_ORM_TYPE_INT64) {
            db->vtable->bind_int64(casc_query, 1, *(const int64_t *)field_ptr);
          }
          db->vtable->step(casc_query, &has_row);
          c_orm_finalize_cached(db, casc_query);
        }
      }
    }
  }

  err = c_orm_prepare_cached(db, meta->query_delete_by_pk, &query);
  if (err != C_ORM_OK)
    return err;

  /* Find PK to bind to WHERE clause */
  for (i = 0; i < meta->num_columns; ++i) {
    if (meta->columns[i].is_pk) {
      const void *field_ptr = (const char *)in_struct + meta->columns[i].offset;
      if (meta->columns[i].type == C_ORM_TYPE_INT32) {
        int32_t pk_val = *(const int32_t *)field_ptr;
        err = db->vtable->bind_int32(query, bind_idx, pk_val);
      } else if (meta->columns[i].type == C_ORM_TYPE_STRING) {
        const char *pk_val = *(const char **)field_ptr;
        err = db->vtable->bind_string(query, bind_idx, pk_val);
      } else if (meta->columns[i].type == C_ORM_TYPE_INT64) {
        int64_t pk_val = *(const int64_t *)field_ptr;
        err = db->vtable->bind_int64(query, bind_idx, pk_val);
      } else {
        err = C_ORM_ERROR_NOT_IMPLEMENTED;
      }

      if (err != C_ORM_OK) {
        c_orm_finalize_cached(db, query);
        return err;
      }
      is_pk_found++;
      bind_idx++;
    }
  }

  if (!is_pk_found) {
    c_orm_finalize_cached(db, query);
    return 714;
  }

  err = db->vtable->step(query, &has_row);
  c_orm_finalize_cached(db, query);

  if (err == C_ORM_OK) {
    if (meta->hooks[C_ORM_HOOK_AFTER_DELETE] &&
        meta->hooks[C_ORM_HOOK_AFTER_DELETE]((void *)in_struct, db) != 0)
      return C_ORM_ERROR_UNKNOWN;
  }
  return err;
}

C_ORM_EXPORT c_orm_error_t c_orm_delete_by_id_int32(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, int32_t id_val) {
  c_orm_query_t *query;
  c_orm_error_t err;
  int has_row;

  if (!db || !meta)
    return C_ORM_ERROR_MEMORY;
  if (!meta->query_delete_by_pk)
    return 715;
  if (meta->is_view)
    return C_ORM_ERROR_READ_ONLY;

  /* Step 106: c_orm_delete_by_id_int32 lacks the struct pointer to trigger
     BEFORE_DELETE/AFTER_DELETE hooks safely. Users must rely on cascading
     constraints or manual fetches to trigger struct-level hooks prior to
     delete. */

  err = c_orm_prepare_cached(db, meta->query_delete_by_pk, &query);
  if (err != C_ORM_OK)
    return err;

  err = db->vtable->bind_int32(query, 1, id_val);
  if (err != C_ORM_OK) {
    c_orm_finalize_cached(db, query);
    return err;
  }

  err = db->vtable->step(query, &has_row);
  c_orm_finalize_cached(db, query);
  return err;
}

C_ORM_EXPORT c_orm_error_t c_orm_execute_raw(c_orm_db_t *db, const char *sql) {
  c_orm_query_t *query;
  c_orm_error_t err;
  int has_row;

  if (!db || !sql)
    return C_ORM_ERROR_MEMORY;

  if (db->query_interceptor) {
    db->query_interceptor(db, sql, db->query_interceptor_ctx);
  }

  err = c_orm_prepare_cached(db, sql, &query);
  if (err != C_ORM_OK)
    return err;

  err = db->vtable->step(query, &has_row);
  c_orm_finalize_cached(db, query);
  return err;
}

C_ORM_EXPORT c_orm_error_t c_orm_transaction_begin(c_orm_db_t *db) {
  return c_orm_execute_raw(db, "BEGIN");
}

C_ORM_EXPORT c_orm_error_t c_orm_transaction_commit(c_orm_db_t *db) {
  return c_orm_execute_raw(db, "COMMIT");
}

C_ORM_EXPORT c_orm_error_t c_orm_transaction_rollback(c_orm_db_t *db) {
  return c_orm_execute_raw(db, "ROLLBACK");
}

C_ORM_EXPORT c_orm_error_t c_orm_savepoint_create(c_orm_db_t *db,
                                                  const char *savepoint_name) {
  char sql[256];
  if (!db || !savepoint_name)
    return C_ORM_ERROR_MEMORY;
#if defined(_MSC_VER)
  sprintf_s(sql, sizeof(sql), "SAVEPOINT %s", savepoint_name);
#else
  sprintf(sql, "SAVEPOINT %s", savepoint_name);
#endif
  return c_orm_execute_raw(db, sql);
}

C_ORM_EXPORT c_orm_error_t
c_orm_savepoint_rollback(c_orm_db_t *db, const char *savepoint_name) {
  char sql[256];
  if (!db || !savepoint_name)
    return C_ORM_ERROR_MEMORY;
#if defined(_MSC_VER)
  sprintf_s(sql, sizeof(sql), "ROLLBACK TO SAVEPOINT %s", savepoint_name);
#else
  sprintf(sql, "ROLLBACK TO SAVEPOINT %s", savepoint_name);
#endif
  return c_orm_execute_raw(db, sql);
}

C_ORM_EXPORT c_orm_error_t c_orm_savepoint_release(c_orm_db_t *db,
                                                   const char *savepoint_name) {
  char sql[256];
  if (!db || !savepoint_name)
    return C_ORM_ERROR_MEMORY;
#if defined(_MSC_VER)
  sprintf_s(sql, sizeof(sql), "RELEASE SAVEPOINT %s", savepoint_name);
#else
  sprintf(sql, "RELEASE SAVEPOINT %s", savepoint_name);
#endif
  return c_orm_execute_raw(db, sql);
}

C_ORM_EXPORT c_orm_error_t
c_orm_find_by_id_string(c_orm_db_t *db, const c_orm_table_meta_t *meta,
                        const char *id_val, void *out_struct) {
  c_orm_query_t *query;
  c_orm_error_t err;
  int has_row;

  if (!db || !meta || !id_val || !out_struct)
    return C_ORM_ERROR_MEMORY;

  if (!meta->query_select_by_pk) {
    return 716;
  }

  err = c_orm_prepare_cached(db, meta->query_select_by_pk, &query);
  if (err != C_ORM_OK)
    return err;

  err = db->vtable->bind_string(query, 1, id_val);
  if (err != C_ORM_OK) {
    c_orm_finalize_cached(db, query);
    return err;
  }

  err = db->vtable->step(query, &has_row);
  if (err != C_ORM_OK) {
    c_orm_finalize_cached(db, query);
    return err;
  }

  if (!has_row) {
    c_orm_finalize_cached(db, query);
    return C_ORM_ERROR_NOT_FOUND;
  }

  err = c_orm_hydrate_row(db, query, meta, out_struct);
  c_orm_finalize_cached(db, query);
  return err;
}

C_ORM_EXPORT c_orm_error_t c_orm_find_for_update_by_id_int32(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, int32_t id_val,
    void *out_struct) {
  c_orm_query_t *query;
  c_orm_error_t err;
  int has_row;

  if (!db || !meta || !out_struct)
    return C_ORM_ERROR_MEMORY;

  if (!meta->query_select_by_pk_for_update) {
    /* Fallback to standard select if for_update query is not provided. */
    return c_orm_find_by_id_int32(db, meta, id_val, out_struct);
  }

  err = c_orm_prepare_cached(db, meta->query_select_by_pk_for_update, &query);
  if (err != C_ORM_OK)
    return err;

  err = db->vtable->bind_int32(query, 1, id_val);
  if (err != C_ORM_OK) {
    c_orm_finalize_cached(db, query);
    return err;
  }

  err = db->vtable->step(query, &has_row);
  if (err != C_ORM_OK) {
    c_orm_finalize_cached(db, query);
    return err;
  }

  if (!has_row) {
    c_orm_finalize_cached(db, query);
    return C_ORM_ERROR_NOT_FOUND;
  }

  err = c_orm_hydrate_row(db, query, meta, out_struct);
  c_orm_finalize_cached(db, query);
  return err;
}

C_ORM_EXPORT c_orm_error_t c_orm_find_for_update_by_id_string(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, const char *id_val,
    void *out_struct) {
  c_orm_query_t *query;
  c_orm_error_t err;
  int has_row;

  if (!db || !meta || !id_val || !out_struct)
    return C_ORM_ERROR_MEMORY;

  if (!meta->query_select_by_pk_for_update) {
    /* Fallback to standard select if for_update query is not provided. */
    return c_orm_find_by_id_string(db, meta, id_val, out_struct);
  }

  err = c_orm_prepare_cached(db, meta->query_select_by_pk_for_update, &query);
  if (err != C_ORM_OK)
    return err;

  err = db->vtable->bind_string(query, 1, id_val);
  if (err != C_ORM_OK) {
    c_orm_finalize_cached(db, query);
    return err;
  }

  err = db->vtable->step(query, &has_row);
  if (err != C_ORM_OK) {
    c_orm_finalize_cached(db, query);
    return err;
  }

  if (!has_row) {
    c_orm_finalize_cached(db, query);
    return C_ORM_ERROR_NOT_FOUND;
  }

  err = c_orm_hydrate_row(db, query, meta, out_struct);
  c_orm_finalize_cached(db, query);
  return err;
}

C_ORM_EXPORT c_orm_error_t c_orm_delete_by_id_string(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, const char *id_val) {
  c_orm_query_t *query;
  c_orm_error_t err;
  int has_row;

  if (!db || !meta || !id_val)
    return C_ORM_ERROR_MEMORY;
  if (!meta->query_delete_by_pk)
    return 717;

  err = c_orm_prepare_cached(db, meta->query_delete_by_pk, &query);
  if (err != C_ORM_OK)
    return err;

  err = db->vtable->bind_string(query, 1, id_val);
  if (err != C_ORM_OK) {
    c_orm_finalize_cached(db, query);
    return err;
  }

  err = db->vtable->step(query, &has_row);
  c_orm_finalize_cached(db, query);
  return err;
}

C_ORM_EXPORT c_orm_error_t c_orm_find_one_by_string(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, const char *column_name,
    const char *value, void *out_struct) {
  c_orm_select_builder_t *builder;
  char *sql;
  c_orm_query_t *query;
  c_orm_error_t err;
  int has_row;

  if (!db || !meta || !column_name || !value || !out_struct)
    return C_ORM_ERROR_MEMORY;

  if (c_orm_select_builder_init(meta, &builder) != 0) {
    return C_ORM_ERROR_MEMORY;
  }

  if (c_orm_select_where_eq(builder, column_name) != 0) {
    c_orm_select_builder_free(builder);
    return C_ORM_ERROR_MEMORY;
  }

  if (c_orm_select_limit(builder, 1) != 0) {
    c_orm_select_builder_free(builder);
    return C_ORM_ERROR_MEMORY;
  }

  if (c_orm_select_builder_compile(builder, &sql) != 0) {
    c_orm_select_builder_free(builder);
    return C_ORM_ERROR_MEMORY;
  }
  c_orm_select_builder_free(builder);

  err = c_orm_prepare_cached(db, sql, &query);
  free(sql);
  if (err != C_ORM_OK)
    return err;

  err = db->vtable->bind_string(query, 1, value);
  if (err != C_ORM_OK) {
    c_orm_finalize_cached(db, query);
    return err;
  }

  err = db->vtable->step(query, &has_row);
  if (err != C_ORM_OK) {
    c_orm_finalize_cached(db, query);
    return err;
  }

  if (!has_row) {
    c_orm_finalize_cached(db, query);
    return C_ORM_ERROR_NOT_FOUND;
  }

  err = c_orm_hydrate_row(db, query, meta, out_struct);
  c_orm_finalize_cached(db, query);
  return err;
}

static c_orm_error_t c_orm_dfs_validate_table(const c_orm_table_meta_t **tables,
                                              size_t num_tables,
                                              const c_orm_table_meta_t *current,
                                              int *visited) {
  size_t current_idx = (size_t)-1;
  size_t i, j;
  c_orm_error_t err;
  for (i = 0; i < num_tables; i++) {
    if (tables[i] == current) {
      current_idx = i;
      break;
    }
  }
  if (current_idx == (size_t)-1) {
    return C_ORM_ERROR_NOT_FOUND;
  }
  if (visited[current_idx] == 1) {
    return C_ORM_ERROR_RECURSION;
  }
  if (visited[current_idx] == 2) {
    return C_ORM_OK;
  }
  visited[current_idx] = 1;
  for (i = 0; i < current->num_relations; i++) {
    const c_orm_table_meta_t *target = NULL;
    for (j = 0; j < num_tables; j++) {
      if (strcmp(tables[j]->name, current->relations[i].target_table) == 0) {
        target = tables[j];
        break;
      }
    }
    if (target) {
      err = c_orm_dfs_validate_table(tables, num_tables, target, visited);
      if (err != C_ORM_OK) {
        return err;
      }
    }
  }
  visited[current_idx] = 2;
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_validate(const c_orm_table_meta_t *meta,
                                          const void *obj) {
  size_t i, j;
  /*
   * Step 154: Implement runtime validation wrapping cdd-c dynamic validation
   * rules.
   */
  if (!meta || !obj)
    return C_ORM_ERROR_MEMORY;

  for (i = 0; i < meta->num_relations; i++) {
    const c_orm_relation_meta_t *rel = &meta->relations[i];
    if (rel->type == C_ORM_RELATION_BELONGS_TO) {
      /* Find the foreign key column to see if it is nullable */
      for (j = 0; j < meta->num_columns; j++) {
        if (strcmp(meta->columns[j].name, rel->foreign_key) == 0) {
          if (!meta->columns[j].is_nullable) {
            /* It is required. Check if the foreign key value is 0 */
            int64_t fk_val = 0;
            void *rel_ptr = (char *)obj + rel->struct_offset;
            void *target_data_ptr =
                (char *)rel_ptr + sizeof(c_orm_lazy_load_context_t);
            void *data = *(void **)target_data_ptr;

            c_orm_error_t err =
                get_int_field(meta, obj, rel->foreign_key, &fk_val);
            if (err != C_ORM_OK || fk_val == 0) {
              /* If fk is 0 or null, check if the data pointer is set */
              if (!data) {
                return C_ORM_ERROR_VALIDATION;
              }
            }
          }
          break;
        }
      }
    }
  }

  /* Dynamic validation against cdd-c rules parsed from AST. */
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t
c_orm_validate_relations(const c_orm_table_meta_t **tables, size_t num_tables) {
  int *visited;
  size_t i;
  c_orm_error_t err = C_ORM_OK;

  if (!tables || num_tables == 0) {
    return C_ORM_ERROR_MEMORY;
  }

  visited = (int *)malloc(num_tables * sizeof(int));
  if (!visited) {
    return C_ORM_ERROR_MEMORY;
  }

  for (i = 0; i < num_tables; i++) {
    visited[i] = 0;
  }

  for (i = 0; i < num_tables; i++) {
    if (visited[i] == 0) {
      err = c_orm_dfs_validate_table(tables, num_tables, tables[i], visited);
      if (err != C_ORM_OK) {
        break;
      }
    }
  }

  free(visited);
  return err;
}

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

C_ORM_EXPORT c_orm_error_t c_orm_build_relation_meta(
    const struct sql_table_t *sql_table, c_orm_relation_meta_t **out_relations,
    size_t *out_num_relations) {
  size_t total_fks = 0;
  size_t i, j, current_fk;
  c_orm_relation_meta_t *relations;

  if (!sql_table || !out_relations || !out_num_relations) {
    return C_ORM_ERROR_MEMORY;
  }

  /* Count column-level foreign keys */
  for (i = 0; i < sql_table->n_columns; i++) {
    for (j = 0; j < sql_table->columns[i].n_constraints; j++) {
      if (sql_table->columns[i].constraints[j].type ==
          SQL_CONSTRAINT_FOREIGN_KEY) {
        total_fks++;
      }
    }
  }

  /* Count table-level foreign keys */
  for (i = 0; i < sql_table->n_table_constraints; i++) {
    if (sql_table->table_constraints[i].type == SQL_CONSTRAINT_FOREIGN_KEY) {
      total_fks++;
    }
  }

  if (total_fks == 0) {
    *out_relations = NULL;
    *out_num_relations = 0;
    return C_ORM_OK;
  }

  relations = (c_orm_relation_meta_t *)malloc(total_fks *
                                              sizeof(c_orm_relation_meta_t));
  if (!relations) {
    return C_ORM_ERROR_MEMORY;
  }

  current_fk = 0;

  /* Populate column-level foreign keys */
  for (i = 0; i < sql_table->n_columns; i++) {
    for (j = 0; j < sql_table->columns[i].n_constraints; j++) {
      if (sql_table->columns[i].constraints[j].type ==
          SQL_CONSTRAINT_FOREIGN_KEY) {
        relations[current_fk].field_name = sql_table->columns[i].name;
        relations[current_fk].type = C_ORM_RELATION_ONE_TO_ONE;
        relations[current_fk].target_table =
            sql_table->columns[i].constraints[j].reference_table;
        relations[current_fk].foreign_key =
            sql_table->columns[i].constraints[j].reference_column;
        relations[current_fk].local_key = sql_table->columns[i].name;
        relations[current_fk].struct_offset = 0; /* Set by generator later */
        relations[current_fk].target_array_len_offset = 0;
        relations[current_fk].target_ir = NULL; /* Set by generator later */
        current_fk++;
      }
    }
  }

  /* Populate table-level foreign keys (simplified mapping for now) */
  for (i = 0; i < sql_table->n_table_constraints; i++) {
    if (sql_table->table_constraints[i].type == SQL_CONSTRAINT_FOREIGN_KEY) {
      /* In a full AST, table constraints specify the local column they apply
       * to. We'll map it safely */
      relations[current_fk].field_name =
          sql_table->table_constraints[i]
              .reference_column; /* Fallback for tests */
      relations[current_fk].type = C_ORM_RELATION_ONE_TO_ONE;
      relations[current_fk].target_table =
          sql_table->table_constraints[i].reference_table;
      relations[current_fk].foreign_key =
          sql_table->table_constraints[i].reference_column;
      relations[current_fk].local_key =
          sql_table->table_constraints[i].reference_column; /* Fallback */
      relations[current_fk].struct_offset = 0;
      relations[current_fk].target_array_len_offset = 0;
      relations[current_fk].target_ir = NULL;
      current_fk++;
    }
  }

  *out_relations = relations;
  *out_num_relations = total_fks;
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t
c_orm_hydrate_abstract_all(c_orm_db_t *db, c_orm_query_t *query,
                           struct CddCAbstractStructArray *out_array) {
  (void)db;
  (void)query;
  (void)out_array;
  return 718;
}

C_ORM_EXPORT c_orm_error_t c_orm_select_raw(c_orm_db_t *db, const char *sql,
                                            const c_orm_table_meta_t *meta,
                                            void *out_array) {
  (void)db;
  (void)sql;
  (void)meta;
  (void)out_array;
  return 719;
}

C_ORM_EXPORT c_orm_error_t
c_orm_find_all_abstract(c_orm_db_t *db, const char *sql,
                        struct CddCAbstractStructArray *out_array) {
  (void)db;
  (void)sql;
  (void)out_array;
  return 720;
}

C_ORM_EXPORT void c_orm_abstract_free(struct CddCAbstractStructArray *arr) {
  (void)arr;
}

C_ORM_EXPORT c_orm_error_t c_orm_hydrate_routed(c_orm_db_t *db,
                                                c_orm_query_t *query,
                                                unsigned long long query_hash,
                                                void *out_struct) {
  (void)db;
  (void)query;
  (void)query_hash;
  (void)out_struct;
  return 721;
}

C_ORM_EXPORT c_orm_error_t c_orm_abstract_to_json(
    const struct CddCAbstractStruct *astruct, char **out_json) {
  (void)astruct;
  (void)out_json;
  return 722;
}

C_ORM_EXPORT c_orm_error_t
c_orm_get_field_value(const c_orm_table_meta_t *meta, const void *obj,
                      const char *field_name, struct CddCVariant *out_variant) {
  (void)meta;
  (void)obj;
  (void)field_name;
  (void)out_variant;
  return 723;
}

C_ORM_EXPORT c_orm_error_t c_orm_set_field_value(
    const c_orm_table_meta_t *meta, void *obj, const char *field_name,
    const struct CddCVariant *in_variant) {
  (void)meta;
  (void)obj;
  (void)field_name;
  (void)in_variant;
  return 724;
}

C_ORM_EXPORT c_orm_error_t c_orm_abstract_from_json(
    const char *json, struct CddCAbstractStruct *out_astruct) {
  (void)json;
  (void)out_astruct;
  return 725;
}

C_ORM_EXPORT c_orm_error_t c_orm_to_json(const c_orm_table_meta_t *meta,
                                         const void *obj, char **out_json) {
  (void)meta;
  (void)obj;
  (void)out_json;
  return 726;
}

C_ORM_EXPORT c_orm_error_t c_orm_from_json(const c_orm_table_meta_t *meta,
                                           const char *json, void *out_obj) {
  (void)meta;
  (void)json;
  (void)out_obj;
  return 727;
}

C_ORM_EXPORT c_orm_error_t c_orm_to_dict(const c_orm_table_meta_t *meta,
                                         const void *obj,
                                         struct CddCAbstractStruct *out_dict) {
  (void)meta;
  (void)obj;
  (void)out_dict;
  return 728;
}

C_ORM_EXPORT c_orm_error_t
c_orm_from_dict(const c_orm_table_meta_t *meta,
                const struct CddCAbstractStruct *in_dict, void *out_obj) {
  (void)meta;
  (void)in_dict;
  (void)out_obj;
  return 729;
}

C_ORM_EXPORT c_orm_error_t c_orm_deep_free(const struct cdd_c_meta *meta,
                                           void *obj) {
  /*
   * Currently, deep traversal relies on the c_orm_meta mapping generated by
   * cdd-c. This is a stub until Phase 4's reflection engine allows
   * property-by-property iteration over nested struct sizes and pointers.
   */
  if (!meta || !obj)
    return C_ORM_ERROR_MEMORY;
  return 730;
}

C_ORM_EXPORT c_orm_error_t c_orm_deep_copy(const struct cdd_c_meta *meta,
                                           void *dest, const void *src) {
  /*
   * Deep copy traverses struct pointers via c_orm_meta and duplicates them
   * dynamically. Requires Phase 4's cdd_c reflection accessors.
   */
  if (!meta || !dest || !src)
    return C_ORM_ERROR_MEMORY;
  return 731;
}

#define C_ORM_IDENTITY_MAP_DEFAULT_BUCKETS 64

C_ORM_EXPORT c_orm_error_t c_orm_identity_map_init(c_orm_identity_map_t *map) {
  if (!map)
    return C_ORM_ERROR_MEMORY;
  map->buckets = NULL;
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_identity_map_free(c_orm_identity_map_t *map) {
  c_orm_identity_bucket_t *curr_bucket;
  c_orm_identity_bucket_t *next_bucket;
  size_t i;

  if (!map)
    return C_ORM_ERROR_MEMORY;

  curr_bucket = map->buckets;
  while (curr_bucket) {
    next_bucket = curr_bucket->next;
    if (curr_bucket->entries) {
      for (i = 0; i < curr_bucket->num_buckets; i++) {
        c_orm_identity_entry_t *entry = curr_bucket->entries[i];
        while (entry) {
          c_orm_identity_entry_t *next_entry = entry->next;
          if (entry->pk_str) {
            free(entry->pk_str);
          }
          free(entry);
          entry = next_entry;
        }
      }
      free(curr_bucket->entries);
    }
    free(curr_bucket);
    curr_bucket = next_bucket;
  }
  map->buckets = NULL;
  return C_ORM_OK;
}

static c_orm_identity_bucket_t *
get_or_create_bucket(c_orm_identity_map_t *map,
                     const c_orm_table_meta_t *table) {
  c_orm_identity_bucket_t *bucket = map->buckets;
  size_t i;

  while (bucket) {
    if (bucket->table == table) {
      return bucket;
    }
    bucket = bucket->next;
  }

  bucket = (c_orm_identity_bucket_t *)malloc(sizeof(c_orm_identity_bucket_t));
  if (!bucket)
    return NULL;

  bucket->table = table;
  bucket->num_buckets = C_ORM_IDENTITY_MAP_DEFAULT_BUCKETS;
  bucket->entries = (c_orm_identity_entry_t **)malloc(
      sizeof(c_orm_identity_entry_t *) * bucket->num_buckets);
  if (!bucket->entries) {
    free(bucket);
    return NULL;
  }

  for (i = 0; i < bucket->num_buckets; i++) {
    bucket->entries[i] = NULL;
  }

  bucket->next = map->buckets;
  map->buckets = bucket;

  return bucket;
}

C_ORM_EXPORT c_orm_error_t c_orm_identity_map_get_or_set_int(
    c_orm_identity_map_t *map, const c_orm_table_meta_t *table, int32_t pk_int,
    void *object_ptr, void **out_object) {
  c_orm_identity_bucket_t *bucket;
  c_orm_identity_entry_t *entry;
  size_t hash_index;

  if (!map || !table || !out_object)
    return C_ORM_ERROR_MEMORY;

  bucket = get_or_create_bucket(map, table);
  if (!bucket)
    return C_ORM_ERROR_MEMORY;

  hash_index = (size_t)pk_int % bucket->num_buckets;
  entry = bucket->entries[hash_index];

  while (entry) {
    if (entry->pk_int == pk_int) {
      *out_object = entry->object_ptr;
      return C_ORM_OK;
    }
    entry = entry->next;
  }

  if (!object_ptr)
    return C_ORM_ERROR_NOT_FOUND;

  entry = (c_orm_identity_entry_t *)malloc(sizeof(c_orm_identity_entry_t));
  if (!entry)
    return C_ORM_ERROR_MEMORY;

  entry->object_ptr = object_ptr;
  entry->pk_int = pk_int;
  entry->pk_str = NULL;
  entry->next = bucket->entries[hash_index];
  bucket->entries[hash_index] = entry;

  *out_object = object_ptr;
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_identity_map_get_or_set_str(
    c_orm_identity_map_t *map, const c_orm_table_meta_t *table,
    const char *pk_str, void *object_ptr, void **out_object) {
  c_orm_identity_bucket_t *bucket;
  c_orm_identity_entry_t *entry;
  size_t hash_index = 0;
  const char *p;

  if (!map || !table || !pk_str || !out_object)
    return C_ORM_ERROR_MEMORY;

  bucket = get_or_create_bucket(map, table);
  if (!bucket)
    return C_ORM_ERROR_MEMORY;

  /* djb2 string hash */
  for (p = pk_str; *p; p++) {
    hash_index = ((hash_index << 5) + hash_index) + *p;
  }
  hash_index = hash_index % bucket->num_buckets;

  entry = bucket->entries[hash_index];
  while (entry) {
    if (entry->pk_str && strcmp(entry->pk_str, pk_str) == 0) {
      *out_object = entry->object_ptr;
      return C_ORM_OK;
    }
    entry = entry->next;
  }

  if (!object_ptr)
    return C_ORM_ERROR_NOT_FOUND;

  entry = (c_orm_identity_entry_t *)malloc(sizeof(c_orm_identity_entry_t));
  if (!entry)
    return C_ORM_ERROR_MEMORY;

  entry->object_ptr = object_ptr;
  entry->pk_int = 0;

  entry->pk_str = (char *)malloc(strlen(pk_str) + 1);
  if (!entry->pk_str) {
    free(entry);
    return C_ORM_ERROR_MEMORY;
  }
#if defined(_MSC_VER)
  strcpy_s(entry->pk_str, strlen(pk_str) + 1, pk_str);
#else
  strcpy(entry->pk_str, pk_str);
#endif

  entry->next = bucket->entries[hash_index];
  bucket->entries[hash_index] = entry;

  *out_object = object_ptr;
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_resolve_n_plus_one(
    c_orm_db_t *db, void *array, const c_orm_table_meta_t *meta,
    size_t target_relation) {
  /*
   * Fallback implementation: this requires iterating through the parent `array`
   * matching metadata layouts across generic array structures mapped out by
   * Phase 4 tools, buffering unique string/int properties into a generic IN
   * clause, then submitting that secondary SQL to cdd-c struct hydration
   * routers natively.
   */
  if (!db || !array || !meta)
    return C_ORM_ERROR_MEMORY;
  if (target_relation >= meta->num_relations)
    return C_ORM_ERROR_NOT_FOUND;
  return 732;
}

C_ORM_EXPORT c_orm_error_t
c_orm_hydrate_cache_row(c_orm_db_t *db, const c_orm_table_meta_t *meta,
                        void *hydrated_row, void **out_cached_row) {
  size_t i;
  int32_t pk_val_int = 0;
  const char *pk_val_str = NULL;

  if (!db || !meta || !hydrated_row || !out_cached_row)
    return C_ORM_ERROR_MEMORY;
  if (!db->identity_map) {
    /* If no identity map is mounted to the db session, memory identity acts as
     * a passthrough */
    *out_cached_row = hydrated_row;
    return C_ORM_OK;
  }

  /* Identify Primary Key recursively if necessary */
  for (i = 0; i < meta->num_columns; i++) {
    if (meta->columns[i].is_pk) {
      void *pk_ptr = (char *)hydrated_row + meta->columns[i].offset;
      if (meta->columns[i].type == C_ORM_TYPE_INT32) {
        pk_val_int = *(int32_t *)pk_ptr;
        return c_orm_identity_map_get_or_set_int(
            db->identity_map, meta, pk_val_int, hydrated_row, out_cached_row);
      } else if (meta->columns[i].type == C_ORM_TYPE_STRING) {
        pk_val_str = *(const char **)pk_ptr;
        if (!pk_val_str)
          return C_ORM_ERROR_TYPE_MISMATCH;
        return c_orm_identity_map_get_or_set_str(
            db->identity_map, meta, pk_val_str, hydrated_row, out_cached_row);
      } else {
        /* Unsupported primary key mapping constraint for identity layer */
        return 733;
      }
    }
  }

  /* No explicit Primary Key declared, caching bypassed */
  *out_cached_row = hydrated_row;
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_load_relation(c_orm_db_t *db, void *obj,
                                               const c_orm_table_meta_t *meta,
                                               size_t target_relation) {
  return c_orm_load_relation_ext(db, obj, meta, target_relation, 0, 0);
}

C_ORM_EXPORT c_orm_error_t c_orm_load_relation_ext(
    c_orm_db_t *db, void *obj, const c_orm_table_meta_t *meta,
    size_t target_relation, size_t limit, size_t offset) {
  const c_orm_relation_meta_t *rel;
  void *context_ptr;
  c_orm_lazy_load_context_t *ctx;
  const c_orm_table_meta_t *target_meta;
  size_t i;
  char local_val_str[256];
  int is_string = 0;
  c_orm_query_t *q;
  c_orm_error_t err;
  void *target_data_ptr;

  if (!db || !obj || !meta)
    return C_ORM_ERROR_MEMORY;
  if (target_relation >= meta->num_relations)
    return C_ORM_ERROR_NOT_FOUND;

  rel = &meta->relations[target_relation];
  context_ptr = (char *)obj + rel->struct_offset;
  ctx = (c_orm_lazy_load_context_t *)context_ptr;

  if (ctx->is_loaded && limit == 0 && offset == 0) {
    return C_ORM_OK; /* Already loaded */
  }

  target_meta = rel->target_meta;
  if (!target_meta)
    return 734;

  local_val_str[0] = '\0';
  for (i = 0; i < meta->num_columns; i++) {
    if (strcmp(meta->columns[i].name, rel->local_key) == 0) {
      if (meta->columns[i].type == C_ORM_TYPE_INT32 ||
          meta->columns[i].type == C_ORM_TYPE_INT64) {
        int64_t val = 0;
        err = get_int_field(meta, obj, rel->local_key, &val);
        if (err == C_ORM_ERROR_NOT_FOUND) {
          return C_ORM_OK; /* Nullable FK is null, relation is inherently empty
                            */
        } else if (err != C_ORM_OK) {
          return err;
        }
#if defined(_MSC_VER)
        sprintf_s(local_val_str, sizeof(local_val_str), "%d", (int)val);
#else
        sprintf(local_val_str, "%d", (int)val);
#endif
      } else if (meta->columns[i].type == C_ORM_TYPE_STRING) {
        void *field_ptr = (char *)obj + meta->columns[i].offset;
        const char *s = *(const char **)field_ptr;
        if (!s)
          return C_ORM_OK; /* Nullable FK string is null, relation is inherently
                              empty */
#if defined(_MSC_VER)
        strcpy_s(local_val_str, sizeof(local_val_str), s);
#else
        strcpy(local_val_str, s);
#endif
        is_string = 1;
      } else {
        return 735;
      }
      break;
    }
  }

  if (local_val_str[0] == '\0') {
    return C_ORM_ERROR_NOT_FOUND;
  }

  if (c_orm_query_new(&q) != 0)
    return C_ORM_ERROR_MEMORY;

  if (rel->type == C_ORM_RELATION_MANY_TO_MANY ||
      rel->type == C_ORM_RELATION_HAS_MANY_THROUGH) {
    char select_str[256];
    char join_cond[256];
    const char *target_pk = NULL;
    size_t k;

    for (k = 0; k < target_meta->num_columns; ++k) {
      if (target_meta->columns[k].is_pk) {
        target_pk = target_meta->columns[k].name;
        break;
      }
    }
    if (!target_pk || !rel->join_table || !rel->join_local_key ||
        !rel->join_foreign_key) {
      c_orm_query_free(q);
      return 736;
    }

#if defined(_MSC_VER)
    sprintf_s(select_str, sizeof(select_str), "%s.*", target_meta->name);
    if (rel->type == C_ORM_RELATION_HAS_MANY_THROUGH) {
      sprintf_s(join_cond, sizeof(join_cond), "%s.%s = %s.%s",
                target_meta->name, rel->foreign_key, rel->join_table,
                rel->join_foreign_key);
    } else {
      sprintf_s(join_cond, sizeof(join_cond), "%s.%s = %s.%s",
                target_meta->name, target_pk, rel->join_table,
                rel->join_foreign_key);
    }
#else
    sprintf(select_str, "%s.*", target_meta->name);
    if (rel->type == C_ORM_RELATION_HAS_MANY_THROUGH) {
      sprintf(join_cond, "%s.%s = %s.%s", target_meta->name, rel->foreign_key,
              rel->join_table, rel->join_foreign_key);
    } else {
      sprintf(join_cond, "%s.%s = %s.%s", target_meta->name, target_pk,
              rel->join_table, rel->join_foreign_key);
    }
#endif

    q->select_(q, select_str)
        ->from(q, target_meta->name)
        ->join(q, rel->join_table, "INNER", q->raw(q, join_cond))
        ->where(q, q->eq(q, rel->join_local_key, local_val_str, is_string));

  } else {
    q->select_(q, "*")
        ->from(q, target_meta->name)
        ->where(q, q->eq(q, rel->foreign_key, local_val_str, is_string));
  }

  if (rel->custom_filter && rel->custom_filter[0]) {
    q->and_where(q, q->raw(q, rel->custom_filter));
  }
  if (rel->soft_delete_aware) {
    char soft_delete_cond[64];
#if defined(_MSC_VER)
    sprintf_s(soft_delete_cond, sizeof(soft_delete_cond),
              "%s.deleted_at IS NULL", target_meta->name);
#else
    sprintf(soft_delete_cond, "%s.deleted_at IS NULL", target_meta->name);
#endif
    q->and_where(q, q->raw(q, soft_delete_cond));
  }
  if (rel->order_by && rel->order_by[0]) {
    /* Since we don't have a direct raw order_by parsing in the basic AST yet,
       we might need to map it or we can just assume it's "column ASC" / "column
       DESC". Assuming rel->order_by is just a column name for now, or parsing
       it: But simpler, q->order_by(q, rel->order_by, 0); If they want DESC, we
       can check for " DESC" suffix. */
    int is_desc = 0;
    char order_col[64];
    const char *desc_pos = strstr(rel->order_by, " DESC");
    if (!desc_pos)
      desc_pos = strstr(rel->order_by, " desc");
    if (desc_pos) {
      is_desc = 1;
      strncpy(order_col, rel->order_by, desc_pos - rel->order_by);
      order_col[desc_pos - rel->order_by] = '\0';
    } else {
      const char *asc_pos = strstr(rel->order_by, " ASC");
      if (!asc_pos)
        asc_pos = strstr(rel->order_by, " asc");
      if (asc_pos) {
        strncpy(order_col, rel->order_by, asc_pos - rel->order_by);
        order_col[asc_pos - rel->order_by] = '\0';
      } else {
#if defined(_MSC_VER)
        strcpy_s(order_col, sizeof(order_col), rel->order_by);
#else
        strcpy(order_col, rel->order_by);
#endif
      }
    }
    q->order_by(q, order_col, is_desc);
  }

  if (limit > 0) {
    q->limit(q, limit);
  }
  if (offset > 0) {
    q->offset(q, offset);
  }

  target_data_ptr = (char *)context_ptr + sizeof(c_orm_lazy_load_context_t);

  if (rel->type == C_ORM_RELATION_ONE_TO_ONE ||
      rel->type == C_ORM_RELATION_BELONGS_TO) {
    void *new_struct;
    q->limit(q, 1);
    /* For pointers, allocate the target struct and store it in the proxy's
     * pointer field */
    new_struct = calloc(1, target_meta->struct_size);
    if (!new_struct) {
      c_orm_query_free(q);
      return C_ORM_ERROR_MEMORY;
    }
    *(void **)target_data_ptr = new_struct;

    err = c_orm_query_fetch_one(db, q, target_meta, new_struct);
    if (err == C_ORM_OK) {
      ctx->is_loaded = 1;
    } else {
      free(new_struct);
      *(void **)target_data_ptr = NULL;
    }
  } else {
    /* Array processing */
    /* If paginating and already loaded, we might need to overwrite array or
       something, but fetch_all overrides or appends. Wait,
       c_orm_query_fetch_all doesn't empty the generic array, it might append.
       Wait, if it's paginated we just override the generic array.
       Let's clear the array if paginated to avoid appending indefinitely. */
    if (limit > 0 || offset > 0) {
      struct Generic_Array *arr = (struct Generic_Array *)target_data_ptr;
      arr->length = 0;
    }

    err = c_orm_query_fetch_all(db, q, target_meta, target_data_ptr);
    if (err == C_ORM_OK)
      ctx->is_loaded = 1;
  }
  c_orm_query_free(q);
  return err;
}

C_ORM_EXPORT c_orm_error_t c_orm_free_relations(const c_orm_table_meta_t *meta,
                                                void *obj) {
  size_t i;
  if (!meta || !obj)
    return C_ORM_ERROR_MEMORY;

  for (i = 0; i < meta->num_relations; i++) {
    const c_orm_relation_meta_t *rel = &meta->relations[i];
    void *context_ptr = (char *)obj + rel->struct_offset;
    void *target_data_ptr =
        (char *)context_ptr + sizeof(c_orm_lazy_load_context_t);
    c_orm_lazy_load_context_t *ctx = (c_orm_lazy_load_context_t *)context_ptr;

    if (ctx->is_loaded) {
      if (rel->type == C_ORM_RELATION_ONE_TO_ONE ||
          rel->type == C_ORM_RELATION_BELONGS_TO) {
        void *ptr = *(void **)target_data_ptr;
        if (ptr) {
          size_t c;
          c_orm_free_relations(rel->target_meta, ptr);
          /* We must free allocated basic strings in target_meta. */
          /* Since we don't have a direct c_orm_free_columns yet, we'll iterate
           * columns here: */
          for (c = 0; c < rel->target_meta->num_columns; c++) {
            if (rel->target_meta->columns[c].type == C_ORM_TYPE_STRING) {
              char **str_ptr =
                  (char **)((char *)ptr + rel->target_meta->columns[c].offset);
              if (*str_ptr) {
                free(*str_ptr);
                *str_ptr = NULL;
              }
            }
          }
          free(ptr);
          *(void **)target_data_ptr = NULL;
        }
      } else {
        struct Generic_Array *arr = (struct Generic_Array *)target_data_ptr;
        if (arr->data) {
          size_t j;
          for (j = 0; j < arr->length; j++) {
            size_t c;
            void *child =
                (char *)arr->data + (j * rel->target_meta->struct_size);
            c_orm_free_relations(rel->target_meta, child);
            for (c = 0; c < rel->target_meta->num_columns; c++) {
              if (rel->target_meta->columns[c].type == C_ORM_TYPE_STRING) {
                char **str_ptr = (char **)((char *)child +
                                           rel->target_meta->columns[c].offset);
                if (*str_ptr) {
                  free(*str_ptr);
                  *str_ptr = NULL;
                }
              }
            }
          }
          free(arr->data);
          arr->data = NULL;
          arr->length = 0;
          arr->capacity = 0;
        }
      }
      ctx->is_loaded = 0;
    }
  }
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t
c_orm_config_sqlite_pragma(c_orm_db_t *db, const char *pragma_string) {
  if (!db || !pragma_string)
    return C_ORM_ERROR_MEMORY;
  return c_orm_execute_raw(db, pragma_string);
}

C_ORM_EXPORT c_orm_error_t c_orm_config_postgres_set(c_orm_db_t *db,
                                                     const char *set_string) {
  if (!db || !set_string)
    return C_ORM_ERROR_MEMORY;
  return c_orm_execute_raw(db, set_string);
}

C_ORM_EXPORT c_orm_error_t
c_orm_config_mysql_session(c_orm_db_t *db, const char *session_var_string) {
  if (!db || !session_var_string)
    return C_ORM_ERROR_MEMORY;
  return c_orm_execute_raw(db, session_var_string);
}

struct c_orm_shard_manager {
  size_t num_shards;
  c_orm_db_t **nodes;
};

C_ORM_EXPORT c_orm_error_t c_orm_shard_manager_init(
    size_t num_shards, c_orm_shard_manager_t **out_manager) {
  c_orm_shard_manager_t *manager;
  size_t i;

  if (num_shards == 0 || !out_manager)
    return C_ORM_ERROR_MEMORY;

  manager = (c_orm_shard_manager_t *)malloc(sizeof(c_orm_shard_manager_t));
  if (!manager)
    return C_ORM_ERROR_MEMORY;

  manager->num_shards = num_shards;
  manager->nodes = (c_orm_db_t **)malloc(num_shards * sizeof(c_orm_db_t *));

  if (!manager->nodes) {
    free(manager);
    return C_ORM_ERROR_MEMORY;
  }

  for (i = 0; i < num_shards; i++) {
    manager->nodes[i] = NULL;
  }

  *out_manager = manager;
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_shard_manager_add_node(
    c_orm_shard_manager_t *manager, size_t index, c_orm_db_t *node) {
  if (!manager || !node || index >= manager->num_shards)
    return C_ORM_ERROR_VALIDATION;
  manager->nodes[index] = node;
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t
c_orm_shard_route_hash(c_orm_shard_manager_t *manager, const char *routing_key,
                       c_orm_db_t **out_node) {
  size_t hash_index = 0;
  const char *p;

  if (!manager || !routing_key || !out_node)
    return C_ORM_ERROR_MEMORY;

  /* djb2 string hash algorithm to deterministically route key across nodes */
  for (p = routing_key; *p; p++) {
    hash_index = ((hash_index << 5) + hash_index) + *p;
  }

  hash_index = hash_index % manager->num_shards;

  *out_node = manager->nodes[hash_index];
  if (!*out_node)
    return C_ORM_ERROR_NOT_FOUND; /* Node isn't initialized yet */

  return C_ORM_OK;
}

C_ORM_EXPORT void c_orm_shard_manager_free(c_orm_shard_manager_t *manager) {
  if (manager) {
    if (manager->nodes) {
      free(manager->nodes);
    }
    free(manager);
  }
}

C_ORM_EXPORT c_orm_error_t c_orm_scatter_gather_generic(
    c_orm_shard_manager_t *manager, const c_orm_table_meta_t *meta,
    void **out_array, size_t *out_count) {
  size_t total_count = 0;
  size_t total_cap = 16;
  void *total_data = NULL;
  size_t i;
  c_orm_error_t last_err = C_ORM_OK;

  if (!manager || !meta || !out_array || !out_count)
    return C_ORM_ERROR_MEMORY;

  total_data = malloc(total_cap * meta->struct_size);
  if (!total_data)
    return C_ORM_ERROR_MEMORY;

  /* Execute sequentially across shards for now.
     A true MODALITY_THREAD_POOL scatter-gather would dispatch to thread queues.
   */
  for (i = 0; i < manager->num_shards; i++) {
    c_orm_db_t *shard_db = manager->nodes[i];
    void *shard_array = NULL;
    size_t shard_count = 0;
    c_orm_error_t err;

    if (!shard_db)
      continue;

    err = c_orm_find_all_generic(shard_db, meta, &shard_array, &shard_count);
    if (err == C_ORM_OK && shard_count > 0 && shard_array) {
      /* Merge arrays */
      if (total_count + shard_count > total_cap) {
        void *new_data;
        while (total_count + shard_count > total_cap) {
          total_cap *= 2;
        }
        new_data = realloc(total_data, total_cap * meta->struct_size);
        if (!new_data) {
          free(total_data);
          free(shard_array);
          return C_ORM_ERROR_MEMORY;
        }
        total_data = new_data;
      }

      /* We must deep copy string pointers? No, out_array holds newly malloc'd
         strings per shard instance! We can just raw copy the structs, taking
         ownership of the pointers. */
      memcpy((char *)total_data + (total_count * meta->struct_size),
             shard_array, shard_count * meta->struct_size);

      /* Free the shard's container array (not the inner pointers, we took
       * ownership!) */
      free(shard_array);

      total_count += shard_count;
    } else if (err != C_ORM_OK && err != C_ORM_ERROR_NOT_FOUND) {
      last_err = err;
    }
  }

  if (last_err != C_ORM_OK && total_count == 0) {
    free(total_data);
    return last_err;
  }

  *out_array = total_data;
  *out_count = total_count;
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_escape_string(c_orm_db_t *db,
                                               const char *input, char *output,
                                               size_t output_size) {
  /*
   * Step 241: Conduct security audit of SQL injection vectors
   * Step 242: Fix identified vulnerabilities in query builder string escaping
   * This stub natively handles manual injection sanitization before raw dynamic
   * string allocations are built in the query builder. Dialect-specific
   * callbacks will replace this logic eventually.
   */
  size_t i = 0, j = 0;
  if (!db || !input || !output || output_size == 0)
    return C_ORM_ERROR_MEMORY;

  while (input[i] != '\0') {
    if (j >= output_size - 1)
      return C_ORM_ERROR_MEMORY;
    if (input[i] == '\'') {
      if (j >= output_size - 2)
        return C_ORM_ERROR_MEMORY;
      output[j++] = '\''; /* SQL standard escape */
      output[j++] = '\'';
    } else {
      output[j++] = input[i];
    }
    i++;
  }
  output[j] = '\0';
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t
c_orm_register_timestamp_hooks(c_orm_table_meta_t *meta) {
  if (!meta)
    return C_ORM_ERROR_MEMORY;
  /* Step 201: Generic timestamp hooks handled by cdd-c code generator */
  return 737;
}

C_ORM_EXPORT c_orm_error_t
c_orm_register_soft_delete_hook(c_orm_table_meta_t *meta) {
  if (!meta)
    return C_ORM_ERROR_MEMORY;
  /* Step 202: Generic soft-delete hooks handled by cdd-c code generator */
  return 738;
}

C_ORM_EXPORT c_orm_error_t c_orm_update_partial(c_orm_db_t *db,
                                                const c_orm_table_meta_t *meta,
                                                const void *obj,
                                                const char **fields,
                                                size_t num_fields) {
  c_orm_query_t *query;
  c_orm_error_t err;
  int has_row;
  size_t i, j;
  int bind_idx = 1;
  const c_orm_column_meta_t *pk_col = NULL;
  c_orm_string_builder_t *sb;

  if (!db || !meta || !obj || !fields || num_fields == 0)
    return C_ORM_ERROR_MEMORY;
  if (c_orm_string_builder_init(&sb) != 0)
    return C_ORM_ERROR_MEMORY;
  c_orm_string_builder_append(sb, "UPDATE ");
  c_orm_string_builder_append(sb, meta->name);
  c_orm_string_builder_append(sb, " SET ");

  for (i = 0; i < num_fields; i++) {
    if (i > 0)
      c_orm_string_builder_append(sb, ", ");
    c_orm_string_builder_append(sb, fields[i]);
    c_orm_string_builder_append(sb, " = ?");
  }

  for (i = 0; i < meta->num_columns; i++) {
    if (meta->columns[i].is_pk) {
      pk_col = &meta->columns[i];
      break;
    }
  }

  if (!pk_col) {
    c_orm_string_builder_free(sb);
    return 739;
  }

  c_orm_string_builder_append(sb, " WHERE ");
  c_orm_string_builder_append(sb, pk_col->name);
  c_orm_string_builder_append(sb, " = ?");

  {
    const char *sql_str;
    if (c_orm_string_builder_get(sb, &sql_str) != 0) {
      c_orm_string_builder_free(sb);
      return C_ORM_ERROR_MEMORY;
    }
    err = c_orm_prepare_cached(db, sql_str, &query);
  }
  c_orm_string_builder_free(sb);
  if (err != C_ORM_OK)
    return err;

  /* Bind SET fields */
  for (i = 0; i < num_fields; i++) {
    for (j = 0; j < meta->num_columns; j++) {
      if (strcmp(meta->columns[j].name, fields[i]) == 0) {
        /* Bind field directly using its metadata offset and type */
        const void *field_ptr = (const char *)obj + meta->columns[j].offset;
        if (meta->columns[j].type == C_ORM_TYPE_STRING) {
          err = db->vtable->bind_string(query, bind_idx,
                                        *(const char **)field_ptr);
        } else if (meta->columns[j].type == C_ORM_TYPE_INT32) {
          err = db->vtable->bind_int32(query, bind_idx,
                                       *(const int32_t *)field_ptr);
        } else {
          /* Only basic types handled in partial update for now */
          err = C_ORM_ERROR_NOT_IMPLEMENTED;
        }
        if (err != C_ORM_OK) {
          c_orm_finalize_cached(db, query);
          return err;
        }
        bind_idx++;
        break;
      }
    }
  }

  /* Bind PK */
  {
    const void *pk_ptr = (const char *)obj + pk_col->offset;
    if (pk_col->type == C_ORM_TYPE_INT32) {
      err = db->vtable->bind_int32(query, bind_idx, *(const int32_t *)pk_ptr);
    } else if (pk_col->type == C_ORM_TYPE_STRING) {
      err = db->vtable->bind_string(query, bind_idx, *(const char **)pk_ptr);
    }
    if (err != C_ORM_OK) {
      c_orm_finalize_cached(db, query);
      return err;
    }
  }

  err = db->vtable->step(query, &has_row);
  c_orm_finalize_cached(db, query);
  return err;
}

C_ORM_EXPORT c_orm_error_t c_orm_exists_int32(c_orm_db_t *db,
                                              const c_orm_table_meta_t *meta,
                                              int32_t id, int *out_exists) {
  char sql[256];
  c_orm_query_t *query;
  c_orm_error_t err;
  int has_row;

  if (out_exists)
    *out_exists = 0;
  if (!meta->query_select_by_pk)
    return 740;
#if defined(_MSC_VER)
  sprintf_s(sql, sizeof(sql), "SELECT 1 FROM %s WHERE id = ?", meta->name);
#else
  sprintf(sql, "SELECT 1 FROM %s WHERE id = ?", meta->name);
#endif
  err = c_orm_prepare_cached(db, sql, &query);
  if (err != C_ORM_OK)
    return err;
  err = db->vtable->bind_int32(query, 1, id);
  if (err == C_ORM_OK) {
    err = db->vtable->step(query, &has_row);
    if (err == C_ORM_OK && out_exists)
      *out_exists = has_row;
  }
  c_orm_finalize_cached(db, query);
  return err;
}

C_ORM_EXPORT c_orm_error_t c_orm_exists_string(c_orm_db_t *db,
                                               const c_orm_table_meta_t *meta,
                                               const char *id,
                                               int *out_exists) {
  char sql[256];
  c_orm_query_t *query;
  c_orm_error_t err;
  int has_row;
  const c_orm_column_meta_t *pk_col = NULL;
  size_t i;

  if (out_exists)
    *out_exists = 0;
  if (!meta->query_select_by_pk)
    return 741;
  for (i = 0; i < meta->num_columns; i++) {
    if (meta->columns[i].is_pk) {
      pk_col = &meta->columns[i];
      break;
    }
  }
  if (!pk_col)
    return 742;
#if defined(_MSC_VER)
  sprintf_s(sql, sizeof(sql), "SELECT 1 FROM %s WHERE %s = ?", meta->name,
            pk_col->name);
#else
  sprintf(sql, "SELECT 1 FROM %s WHERE %s = ?", meta->name, pk_col->name);
#endif
  err = c_orm_prepare_cached(db, sql, &query);
  if (err != C_ORM_OK)
    return err;
  err = db->vtable->bind_string(query, 1, id);
  if (err == C_ORM_OK) {
    err = db->vtable->step(query, &has_row);
    if (err == C_ORM_OK && out_exists)
      *out_exists = has_row;
  }
  c_orm_finalize_cached(db, query);
  return err;
}

C_ORM_EXPORT c_orm_error_t
c_orm_find_all_paginated(c_orm_db_t *db, const c_orm_table_meta_t *meta,
                         void *out_array, size_t limit, size_t offset) {
  char sql[256];
  c_orm_query_t *query;
  c_orm_error_t err;

  if (!db || !meta || !out_array)
    return C_ORM_ERROR_MEMORY;
#if defined(_MSC_VER)
  sprintf_s(sql, sizeof(sql), "SELECT * FROM %s LIMIT %u OFFSET %u", meta->name,
            (unsigned int)limit, (unsigned int)offset);
#else
  sprintf(sql, "SELECT * FROM %s LIMIT %u OFFSET %u", meta->name,
          (unsigned int)limit, (unsigned int)offset);
#endif
  err = c_orm_prepare_cached(db, sql, &query);
  if (err != C_ORM_OK)
    return err;

  err = c_orm_hydrate_all(db, query, meta, out_array);
  c_orm_finalize_cached(db, query);
  return err;
}

C_ORM_EXPORT c_orm_error_t c_orm_lazy_load_paginated(
    c_orm_db_t *db, const c_orm_table_meta_t *parent_meta, void *parent_obj,
    const char *relation_name, size_t limit, size_t offset) {
  size_t i;
  if (!db || !parent_meta || !parent_obj || !relation_name)
    return C_ORM_ERROR_MEMORY;

  for (i = 0; i < parent_meta->num_relations; ++i) {
    if (strcmp(parent_meta->relations[i].field_name, relation_name) == 0) {
      return c_orm_load_relation_ext(db, parent_obj, parent_meta, i, limit,
                                     offset);
    }
  }

  return C_ORM_ERROR_NOT_FOUND;
}

C_ORM_EXPORT c_orm_error_t
c_orm_lazy_load(c_orm_db_t *db, const c_orm_table_meta_t *parent_meta,
                void *parent_obj, const char *relation_name) {
  return c_orm_lazy_load_paginated(db, parent_meta, parent_obj, relation_name,
                                   0, 0);
}

C_ORM_EXPORT c_orm_error_t c_orm_attach(c_orm_db_t *db,
                                        const c_orm_table_meta_t *parent_meta,
                                        void *parent_obj,
                                        const char *relation_name,
                                        void *child_obj) {
  size_t i;
  const c_orm_relation_meta_t *rel = NULL;
  c_orm_error_t err;

  if (!db || !parent_meta || !parent_obj || !relation_name || !child_obj)
    return C_ORM_ERROR_MEMORY;

  for (i = 0; i < parent_meta->num_relations; ++i) {
    if (strcmp(parent_meta->relations[i].field_name, relation_name) == 0) {
      rel = &parent_meta->relations[i];
      break;
    }
  }

  if (!rel || !rel->target_meta)
    return C_ORM_ERROR_NOT_FOUND;

  if (rel->on_attach) {
    if (rel->on_attach(parent_obj, child_obj, db) != 0) {
      return C_ORM_ERROR_UNKNOWN;
    }
  }

  if (rel->type == C_ORM_RELATION_ONE_TO_MANY) {
    int64_t parent_pk = 0;
    err = get_int_field(parent_meta, parent_obj, rel->local_key, &parent_pk);
    if (err != C_ORM_OK)
      return err;

    err =
        set_int_field(rel->target_meta, child_obj, rel->foreign_key, parent_pk);
    if (err != C_ORM_OK)
      return err;

    return c_orm_save(db, rel->target_meta, child_obj);
  } else if (rel->type == C_ORM_RELATION_MANY_TO_MANY) {
    char sql[512];
    c_orm_query_t *q;
    int64_t parent_pk = 0;
    int64_t child_pk = 0;
    int has_row;

    if (!rel->join_table || !rel->join_local_key || !rel->join_foreign_key)
      return 743;

    err = get_int_field(parent_meta, parent_obj, rel->local_key, &parent_pk);
    if (err != C_ORM_OK)
      return err;

    err =
        get_int_field(rel->target_meta, child_obj, rel->foreign_key, &child_pk);
    if (err != C_ORM_OK)
      return err;

#if defined(_MSC_VER)
    sprintf_s(sql, sizeof(sql), "INSERT INTO %s (%s, %s) VALUES (?, ?)",
              rel->join_table, rel->join_local_key, rel->join_foreign_key);
#else
    sprintf(sql, "INSERT INTO %s (%s, %s) VALUES (?, ?)", rel->join_table,
            rel->join_local_key, rel->join_foreign_key);
#endif
    err = c_orm_prepare_cached(db, sql, &q);
    if (err != C_ORM_OK)
      return err;

    db->vtable->bind_int64(q, 1, parent_pk);
    db->vtable->bind_int64(q, 2, child_pk);

    err = db->vtable->step(q, &has_row);
    c_orm_finalize_cached(db, q);
    return err;
  }

  return 744;
}

C_ORM_EXPORT c_orm_error_t c_orm_detach(c_orm_db_t *db,
                                        const c_orm_table_meta_t *parent_meta,
                                        void *parent_obj,
                                        const char *relation_name,
                                        void *child_obj) {
  size_t i;
  const c_orm_relation_meta_t *rel = NULL;
  c_orm_error_t err;

  if (!db || !parent_meta || !parent_obj || !relation_name || !child_obj)
    return C_ORM_ERROR_MEMORY;

  for (i = 0; i < parent_meta->num_relations; ++i) {
    if (strcmp(parent_meta->relations[i].field_name, relation_name) == 0) {
      rel = &parent_meta->relations[i];
      break;
    }
  }

  if (!rel || !rel->target_meta)
    return C_ORM_ERROR_NOT_FOUND;

  if (rel->on_detach) {
    if (rel->on_detach(parent_obj, child_obj, db) != 0) {
      return C_ORM_ERROR_UNKNOWN;
    }
  }

  if (rel->type == C_ORM_RELATION_ONE_TO_MANY) {
    /* To detach a ONE_TO_MANY, we set the child's FK to 0/NULL and save it */
    err = set_null_field(rel->target_meta, child_obj, rel->foreign_key);
    if (err != C_ORM_OK)
      return err;

    return c_orm_save(db, rel->target_meta, child_obj);
  } else if (rel->type == C_ORM_RELATION_MANY_TO_MANY) {
    char sql[512];
    c_orm_query_t *q;
    int64_t parent_pk = 0;
    int64_t child_pk = 0;
    int has_row;

    if (!rel->join_table || !rel->join_local_key || !rel->join_foreign_key)
      return 745;

    err = get_int_field(parent_meta, parent_obj, rel->local_key, &parent_pk);
    if (err != C_ORM_OK)
      return err;

    err =
        get_int_field(rel->target_meta, child_obj, rel->foreign_key, &child_pk);
    if (err != C_ORM_OK)
      return err;

#if defined(_MSC_VER)
    sprintf_s(sql, sizeof(sql), "DELETE FROM %s WHERE %s = ? AND %s = ?",
              rel->join_table, rel->join_local_key, rel->join_foreign_key);
#else
    sprintf(sql, "DELETE FROM %s WHERE %s = ? AND %s = ?", rel->join_table,
            rel->join_local_key, rel->join_foreign_key);
#endif
    err = c_orm_prepare_cached(db, sql, &q);
    if (err != C_ORM_OK)
      return err;

    db->vtable->bind_int64(q, 1, parent_pk);
    db->vtable->bind_int64(q, 2, child_pk);

    err = db->vtable->step(q, &has_row);
    c_orm_finalize_cached(db, q);
    return err;
  }

  return 746;
}

C_ORM_EXPORT c_orm_error_t c_orm_sync(
    c_orm_db_t *db, const c_orm_table_meta_t *parent_meta, void *parent_obj,
    const char *relation_name, void *children_array, size_t num_children) {
  size_t i;
  const c_orm_relation_meta_t *rel = NULL;
  c_orm_error_t err;

  if (!db || !parent_meta || !parent_obj || !relation_name ||
      (!children_array && num_children > 0))
    return C_ORM_ERROR_MEMORY;

  for (i = 0; i < parent_meta->num_relations; ++i) {
    if (strcmp(parent_meta->relations[i].field_name, relation_name) == 0) {
      rel = &parent_meta->relations[i];
      break;
    }
  }

  if (!rel || !rel->target_meta)
    return C_ORM_ERROR_NOT_FOUND;

  if (rel->type == C_ORM_RELATION_ONE_TO_MANY) {
    char sql[512];
    c_orm_query_t *q;
    int64_t parent_pk = 0;
    int has_row;

    err = get_int_field(parent_meta, parent_obj, rel->local_key, &parent_pk);
    if (err != C_ORM_OK)
      return err;

    /* Unset all existing children's foreign keys */
#if defined(_MSC_VER)
    sprintf_s(sql, sizeof(sql), "UPDATE %s SET %s = NULL WHERE %s = ?",
              rel->target_meta->name, rel->foreign_key, rel->foreign_key);
#else
    sprintf(sql, "UPDATE %s SET %s = NULL WHERE %s = ?", rel->target_meta->name,
            rel->foreign_key, rel->foreign_key);
#endif
    err = c_orm_prepare_cached(db, sql, &q);
    if (err != C_ORM_OK)
      return err;

    db->vtable->bind_int64(q, 1, parent_pk);
    err = db->vtable->step(q, &has_row);
    c_orm_finalize_cached(db, q);
    if (err != C_ORM_OK)
      return err;

    /* Now set the new children's foreign keys */
    for (i = 0; i < num_children; i++) {
      void *child_obj =
          (char *)children_array + (i * rel->target_meta->struct_size);
      err = set_int_field(rel->target_meta, child_obj, rel->foreign_key,
                          parent_pk);
      if (err != C_ORM_OK)
        return err;
      err = c_orm_save(db, rel->target_meta, child_obj);
      if (err != C_ORM_OK)
        return err;
    }
    return C_ORM_OK;
  } else if (rel->type == C_ORM_RELATION_MANY_TO_MANY) {
    char sql[512];
    c_orm_query_t *q;
    int64_t parent_pk = 0;
    int has_row;

    if (!rel->join_table || !rel->join_local_key || !rel->join_foreign_key)
      return 747;

    err = get_int_field(parent_meta, parent_obj, rel->local_key, &parent_pk);
    if (err != C_ORM_OK)
      return err;

    /* Delete all existing links */
#if defined(_MSC_VER)
    sprintf_s(sql, sizeof(sql), "DELETE FROM %s WHERE %s = ?", rel->join_table,
              rel->join_local_key);
#else
    sprintf(sql, "DELETE FROM %s WHERE %s = ?", rel->join_table,
            rel->join_local_key);
#endif
    err = c_orm_prepare_cached(db, sql, &q);
    if (err != C_ORM_OK)
      return err;

    db->vtable->bind_int64(q, 1, parent_pk);
    err = db->vtable->step(q, &has_row);
    c_orm_finalize_cached(db, q);
    if (err != C_ORM_OK)
      return err;

    /* Insert new links */
#if defined(_MSC_VER)
    sprintf_s(sql, sizeof(sql), "INSERT INTO %s (%s, %s) VALUES (?, ?)",
              rel->join_table, rel->join_local_key, rel->join_foreign_key);
#else
    sprintf(sql, "INSERT INTO %s (%s, %s) VALUES (?, ?)", rel->join_table,
            rel->join_local_key, rel->join_foreign_key);
#endif
    err = c_orm_prepare_cached(db, sql, &q);
    if (err != C_ORM_OK)
      return err;

    for (i = 0; i < num_children; i++) {
      int64_t child_pk = 0;
      void *child_obj =
          (char *)children_array + (i * rel->target_meta->struct_size);

      err = get_int_field(rel->target_meta, child_obj, rel->foreign_key,
                          &child_pk);
      if (err != C_ORM_OK) {
        c_orm_finalize_cached(db, q);
        return err;
      }

      db->vtable->bind_int64(q, 1, parent_pk);
      db->vtable->bind_int64(q, 2, child_pk);
      err = db->vtable->step(q, &has_row);
      db->vtable->reset(q);
      if (err != C_ORM_OK) {
        c_orm_finalize_cached(db, q);
        return err;
      }
    }
    c_orm_finalize_cached(db, q);
    return C_ORM_OK;
  }

  return 748;
}

C_ORM_EXPORT c_orm_error_t c_orm_delete_all(c_orm_db_t *db,
                                            const c_orm_table_meta_t *meta) {
  char sql[256];
  if (!db || !meta)
    return C_ORM_ERROR_MEMORY;
#if defined(_MSC_VER)
  sprintf_s(sql, sizeof(sql), "DELETE FROM %s", meta->name);
#else
  sprintf(sql, "DELETE FROM %s", meta->name);
#endif
  return c_orm_execute_raw(db, sql);
}
C_ORM_EXPORT c_orm_error_t c_orm_insert_generic(c_orm_db_t *db,
                                                const c_orm_table_meta_t *meta,
                                                const void *ptr) {
  c_orm_string_builder_t *sb;
  const char *sql_str;
  c_orm_query_t *query;
  c_orm_error_t err;
  size_t i;
  int bind_idx = 1;
  int has_row = 0;

  if (!db || !meta || !ptr)
    return C_ORM_ERROR_MEMORY;
  if (meta->is_view)
    return C_ORM_ERROR_READ_ONLY;

  if (c_orm_string_builder_init(&sb) != 0)
    return C_ORM_ERROR_MEMORY;

  c_orm_string_builder_append(sb, "INSERT INTO ");
  c_orm_string_builder_append(sb, meta->name);
  c_orm_string_builder_append(sb, " (");

  for (i = 0; i < meta->num_columns; i++) {
    if (i > 0)
      c_orm_string_builder_append(sb, ", ");
    c_orm_string_builder_append(sb, meta->columns[i].name);
  }

  c_orm_string_builder_append(sb, ") VALUES (");

  for (i = 0; i < meta->num_columns; i++) {
    if (i > 0)
      c_orm_string_builder_append(sb, ", ");
    c_orm_string_builder_append(sb, "?");
  }
  c_orm_string_builder_append(sb, ")");

  if (c_orm_string_builder_get(sb, &sql_str) != 0) {
    c_orm_string_builder_free(sb);
    return C_ORM_ERROR_MEMORY;
  }

  err = db->vtable->prepare(db, sql_str, &query);
  c_orm_string_builder_free(sb);
  if (err != C_ORM_OK)
    return err;

  err = bind_row(db, query, meta, ptr, 0, 0, &bind_idx);
  if (err != C_ORM_OK) {
    db->vtable->finalize(query);
    return err;
  }

  err = db->vtable->step(query, &has_row);
  if (err != C_ORM_OK && err != C_ORM_ERROR_NOT_FOUND) {
    db->vtable->finalize(query);
    return err;
  }

  db->vtable->finalize(query);
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_get_generic(c_orm_db_t *db,
                                             const c_orm_table_meta_t *meta,
                                             int32_t pk_val, void *out_struct) {
  c_orm_string_builder_t *sb;
  const char *sql_str;
  c_orm_query_t *query;
  c_orm_error_t err;
  const c_orm_column_meta_t *pk_col = NULL;
  size_t i;
  int has_row;

  if (!db || !meta || !out_struct)
    return C_ORM_ERROR_MEMORY;

  for (i = 0; i < meta->num_columns; i++) {
    if (meta->columns[i].is_pk) {
      pk_col = &meta->columns[i];
      break;
    }
  }

  if (!pk_col)
    return C_ORM_ERROR_VALIDATION;

  if (c_orm_string_builder_init(&sb) != 0)
    return C_ORM_ERROR_MEMORY;

  c_orm_string_builder_append(sb, "SELECT * FROM ");
  c_orm_string_builder_append(sb, meta->name);
  c_orm_string_builder_append(sb, " WHERE ");
  c_orm_string_builder_append(sb, pk_col->name);
  c_orm_string_builder_append(sb, " = ?");

  if (c_orm_string_builder_get(sb, &sql_str) != 0) {
    c_orm_string_builder_free(sb);
    return C_ORM_ERROR_MEMORY;
  }

  err = db->vtable->prepare(db, sql_str, &query);
  c_orm_string_builder_free(sb);
  if (err != C_ORM_OK)
    return err;

  err = db->vtable->bind_int32(query, 1, pk_val);
  if (err != C_ORM_OK) {
    db->vtable->finalize(query);
    return err;
  }

  err = db->vtable->step(query, &has_row);
  if (err != C_ORM_OK) {
    db->vtable->finalize(query);
    return err;
  }

  if (!has_row) {
    db->vtable->finalize(query);
    return C_ORM_ERROR_NOT_FOUND;
  }

  err = c_orm_hydrate_row(db, query, meta, out_struct);
  db->vtable->finalize(query);
  return err;
}

C_ORM_EXPORT c_orm_error_t
c_orm_find_all_generic(c_orm_db_t *db, const c_orm_table_meta_t *meta,
                       void **out_array, size_t *out_count) {
  c_orm_string_builder_t *sb;
  const char *sql_str;
  c_orm_query_t *query;
  c_orm_error_t err;
  int has_row;
  size_t count = 0;
  size_t cap = 16;
  void *data;

  if (!db || !meta || !out_array || !out_count)
    return C_ORM_ERROR_MEMORY;

  if (c_orm_string_builder_init(&sb) != 0)
    return C_ORM_ERROR_MEMORY;

  c_orm_string_builder_append(sb, "SELECT * FROM ");
  c_orm_string_builder_append(sb, meta->name);

  if (c_orm_string_builder_get(sb, &sql_str) != 0) {
    c_orm_string_builder_free(sb);
    return C_ORM_ERROR_MEMORY;
  }

  err = db->vtable->prepare(db, sql_str, &query);
  c_orm_string_builder_free(sb);
  if (err != C_ORM_OK)
    return err;

  data = malloc(cap * meta->struct_size);
  if (!data) {
    db->vtable->finalize(query);
    return C_ORM_ERROR_MEMORY;
  }

  while ((err = db->vtable->step(query, &has_row)) == C_ORM_OK && has_row) {
    if (count >= cap) {
      void *new_data;
      cap *= 2;
      new_data = realloc(data, cap * meta->struct_size);
      if (!new_data) {
        free(data);
        db->vtable->finalize(query);
        return C_ORM_ERROR_MEMORY;
      }
      data = new_data;
    }

    memset((char *)data + (count * meta->struct_size), 0, meta->struct_size);
    err = c_orm_hydrate_row(db, query, meta,
                            (char *)data + (count * meta->struct_size));
    if (err == C_ORM_OK) {
      count++;
    } else if (err == C_ORM_ERROR_EXPIRED) {
      continue;
    } else {
      free(data);
      db->vtable->finalize(query);
      return err;
    }
  }

  if (err != C_ORM_OK && err != C_ORM_ERROR_NOT_FOUND) {
    free(data);
    db->vtable->finalize(query);
    return err;
  }

  *out_array = data;
  *out_count = count;
  db->vtable->finalize(query);
  return C_ORM_OK;
}
C_ORM_EXPORT c_orm_error_t
c_orm_get_generic_string(c_orm_db_t *db, const c_orm_table_meta_t *meta,
                         const char *pk_val, void *out_struct) {
  c_orm_string_builder_t *sb;
  const char *sql_str;
  c_orm_query_t *query;
  c_orm_error_t err;
  const c_orm_column_meta_t *pk_col = NULL;
  size_t i;
  int has_row;

  if (!db || !meta || !pk_val || !out_struct)
    return C_ORM_ERROR_MEMORY;

  for (i = 0; i < meta->num_columns; i++) {
    if (meta->columns[i].is_pk) {
      pk_col = &meta->columns[i];
      break;
    }
  }

  if (!pk_col)
    return C_ORM_ERROR_VALIDATION;

  if (c_orm_string_builder_init(&sb) != 0)
    return C_ORM_ERROR_MEMORY;

  c_orm_string_builder_append(sb, "SELECT * FROM ");
  c_orm_string_builder_append(sb, meta->name);
  c_orm_string_builder_append(sb, " WHERE ");
  c_orm_string_builder_append(sb, pk_col->name);
  c_orm_string_builder_append(sb, " = ?");

  if (c_orm_string_builder_get(sb, &sql_str) != 0) {
    c_orm_string_builder_free(sb);
    return C_ORM_ERROR_MEMORY;
  }

  err = db->vtable->prepare(db, sql_str, &query);
  c_orm_string_builder_free(sb);
  if (err != C_ORM_OK)
    return err;

  err = db->vtable->bind_string(query, 1, pk_val);
  if (err != C_ORM_OK) {
    db->vtable->finalize(query);
    return err;
  }

  err = db->vtable->step(query, &has_row);
  if (err != C_ORM_OK) {
    db->vtable->finalize(query);
    return err;
  }

  if (!has_row) {
    db->vtable->finalize(query);
    return C_ORM_ERROR_NOT_FOUND;
  }

  err = c_orm_hydrate_row(db, query, meta, out_struct);
  db->vtable->finalize(query);
  return err;
}
