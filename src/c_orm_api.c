/**
 * @file c_orm_api.c
 * @brief Implementation of high-level API for c-orm.
 */

/* clang-format off */
#include "cdd_c_orm_meta.h"
#include "abstract_struct.h"
#include "c_orm_safe_crt.h"
#include "c_orm_api.h"
#include "c_orm_log.h"
#include "c_orm_ast.h"
#include "c_orm_query_builder.h"
#include "c_orm_uuid.h"
#include "c_orm_sql.h"
#include "c_orm_sqlite.h"
#include "c_orm_postgres.h"
#include "c_orm_mysql.h"
#include "c_orm_memory.h"
#include "c_orm_odbc.h"
#include "c_orm_db2.h"
#include "hydrate_router.h"
#include "c_cdd/json_dom.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
/* clang-format on */

/**
 * @brief Lifecycle before-insert hook applying `created_at` and `updated_at`
 * timestamps via `db->active_hook_meta`.
 * @param obj Target struct instance.
 * @param user_data Active `c_orm_db_t *` connection handle.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_ts_before_insert_hook(void *obj, void *user_data);

/**
 * @brief Lifecycle before-update hook applying `updated_at` timestamp via
 * `db->active_hook_meta`.
 * @param obj Target struct instance.
 * @param user_data Active `c_orm_db_t *` connection handle.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_ts_before_update_hook(void *obj, void *user_data);

/**
 * @brief Lifecycle before-delete hook executing soft-delete updates via
 * `db->active_hook_meta`.
 * @param obj Target struct instance.
 * @param user_data Active `c_orm_db_t *` connection handle.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_soft_delete_before_hook(void *obj, void *user_data);

/**
 * @brief Applies UTC timestamp values to `created_at` and/or `updated_at`
 * columns on `obj`.
 * @param meta Table metadata.
 * @param obj Target struct instance.
 * @param set_created Non-zero to stamp `created_at` in addition to
 * `updated_at`.
 * @return C_ORM_OK on success, or allocation error code.
 */
static c_orm_error_t c_orm_apply_timestamp_cols(const c_orm_table_meta_t *meta,
                                                void *obj, int set_created);

/**
 * @brief Executes soft-delete column updates (`deleted_at` / `is_deleted`) via
 * `c_orm_update_partial`.
 * @param db Database handle.
 * @param meta Table metadata.
 * @param obj Struct instance being soft-deleted.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_exec_soft_delete(c_orm_db_t *db,
                                            const c_orm_table_meta_t *meta,
                                            void *obj);

/**
 * @brief Internal infallible helper to release heap-allocated column fields.
 *
 * @param meta The table metadata.
 * @param obj The structure instance to clear column allocations for.
 * @return C_ORM_OK.
 */
static c_orm_error_t c_orm_columns_free(const c_orm_table_meta_t *meta,
                                        void *obj) {
  size_t i;
  if (!meta || !obj)
    return C_ORM_OK;
  for (i = 0; i < meta->num_columns; ++i) {
    const c_orm_column_meta_t *col = &meta->columns[i];
    void *field_ptr = (char *)obj + col->offset;
    if ((col->type == C_ORM_TYPE_CHAR_FIXED ||
         col->type == C_ORM_TYPE_BINARY_FIXED) &&
        col->fixed_length > 0) {
      /* Inline fixed-size array inside struct; no heap allocation to free. */
      continue;
    }
    if (col->type == C_ORM_TYPE_STRING || col->type == C_ORM_TYPE_DATE ||
        col->type == C_ORM_TYPE_TIME || col->type == C_ORM_TYPE_TIMESTAMP ||
        col->type == C_ORM_TYPE_ENUM || col->type == C_ORM_TYPE_SET ||
        col->type == C_ORM_TYPE_JSON || col->type == C_ORM_TYPE_GRAPHIC ||
        col->type == C_ORM_TYPE_VARGRAPHIC ||
        col->type == C_ORM_TYPE_CHAR_FIXED) {
      if (*(char **)field_ptr) {
        c_orm_system_free(*(char **)field_ptr);
        *(char **)field_ptr = NULL;
      }
    } else if (col->type == C_ORM_TYPE_BLOB ||
               col->type == C_ORM_TYPE_VARBINARY ||
               col->type == C_ORM_TYPE_BINARY_FIXED) {
      c_orm_blob_t *blob_ptr = (c_orm_blob_t *)field_ptr;
      if (blob_ptr->data) {
        c_orm_system_free(blob_ptr->data);
        blob_ptr->data = NULL;
        blob_ptr->size = 0;
      }
    } else if (col->type == C_ORM_TYPE_POLYGON) {
      c_orm_polygon_t *poly = (c_orm_polygon_t *)field_ptr;
      if (poly->points) {
        c_orm_system_free(poly->points);
        poly->points = NULL;
        poly->num_points = 0;
      }
    } else if (col->is_nullable) {
      if (*(void **)field_ptr) {
        c_orm_system_free(*(void **)field_ptr);
        *(void **)field_ptr = NULL;
      }
    }
  }
  return C_ORM_OK;
}

/**
 * @brief Free dynamically allocated column memory in an object.
 *
 * @param meta The table metadata.
 * @param obj The structure instance to free column allocations for.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_free_columns(const c_orm_table_meta_t *meta,
                                              void *obj) {
  return c_orm_columns_free(meta, obj);
}

/**
 * @brief Hydrate a struct from a row in a query, starting at a specific column
 index.
  *
  * @param db Database connection.
  * @param query Prepared and optionally bound query.
  * @param meta Table metadata.
  * @param out_struct Pointer to an already allocated struct to hydrate.
  * @param start_col The column index to start reading from.
  * @return C_ORM_OK on success.
  */
C_ORM_EXPORT c_orm_error_t c_orm_hydrate_row_from(
    c_orm_db_t *db, c_orm_query_t *query, const c_orm_table_meta_t *meta,
    void *out_struct, size_t start_col) {
  c_orm_error_t rc;

  size_t i;

  LOG_DEBUG("c_orm_hydrate_row_from: entry");
  if (!db || !query || !meta || !out_struct) {
    LOG_DEBUG("c_orm_hydrate_row_from: exit");
    return C_ORM_ERROR_MEMORY;
  }
  for (i = 0; i < meta->num_columns; ++i) {
    const c_orm_column_meta_t *col = &meta->columns[i];
    void *field_ptr = (char *)out_struct + col->offset;
    int is_null = 0;
    size_t col_idx = start_col + i;

    rc = db->vtable->is_null(query, (int)col_idx, &is_null);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_hydrate_row_from: exit");
      goto error_out;
    }

    if (is_null) {
      if (!col->is_nullable) {
        /* This is actually a constraint violation */
        {
          rc = C_ORM_ERROR_TYPE_MISMATCH;
          LOG_DEBUG("c_orm_hydrate_row_from: exit");
          goto error_out;
        }
      }
      /* If it's nullable primitive, we need to set the pointer to NULL.
         For string, set char* to NULL. */
      switch (col->type) {
      case C_ORM_TYPE_CHAR_FIXED:
        if (col->fixed_length > 0) {
          memset(field_ptr, 0, col->fixed_length + 1);
        } else {
          if (*(char **)field_ptr)
            c_orm_system_free(*(char **)field_ptr);
          *(char **)field_ptr = NULL;
        }
        break;
      case C_ORM_TYPE_BINARY_FIXED:
        if (col->fixed_length > 0) {
          memset(field_ptr, 0, col->fixed_length);
        } else {
          c_orm_blob_t *bf = (c_orm_blob_t *)field_ptr;
          if (bf->data)
            c_orm_system_free(bf->data);
          bf->data = NULL;
          bf->size = 0;
        }
        break;
      case C_ORM_TYPE_STRING:
      case C_ORM_TYPE_JSON:
      case C_ORM_TYPE_SET:
      case C_ORM_TYPE_ENUM:
      case C_ORM_TYPE_TIMESTAMP:
      case C_ORM_TYPE_DATE:
      case C_ORM_TYPE_TIME:
      case C_ORM_TYPE_GRAPHIC:
      case C_ORM_TYPE_VARGRAPHIC:
        if (*(char **)field_ptr)
          c_orm_system_free(*(char **)field_ptr);
        *(char **)field_ptr = NULL;
        break;
      case C_ORM_TYPE_BLOB:
      case C_ORM_TYPE_VARBINARY: {
        c_orm_blob_t *b = (c_orm_blob_t *)field_ptr;
        if (b->data)
          c_orm_system_free(b->data);
        b->data = NULL;
        b->size = 0;
        break;
      }
      default:
        if (*(void **)field_ptr)
          c_orm_system_free(*(void **)field_ptr);
        *(void **)field_ptr = NULL;
        break;
      }
      continue;
    }

    switch (col->type) {
    case C_ORM_TYPE_INT32: {
      int32_t val = 0;
      rc = db->vtable->get_int32(query, (int)col_idx, &val);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_hydrate_row_from: exit");
        goto error_out;
      }
      if (col->is_nullable) {
        int32_t *ptr = NULL;
        rc = c_orm_system_malloc(sizeof(int32_t), (void **)&ptr);
        if (rc != C_ORM_OK) {
          LOG_DEBUG("c_orm_hydrate_row_from: OOM");
          goto error_out;
        }
        *ptr = val;
        *(int32_t **)field_ptr = ptr;
      } else {
        *(int32_t *)field_ptr = val;
      }
      break;
    }
    case C_ORM_TYPE_BOOL: {
      int32_t val = 0;
      rc = db->vtable->get_int32(query, (int)col_idx, &val);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_hydrate_row_from: exit");
        goto error_out;
      }
      if (col->is_nullable) {
        bool *ptr = NULL;
        rc = c_orm_system_malloc(sizeof(bool), (void **)&ptr);
        if (rc != C_ORM_OK) {
          LOG_DEBUG("c_orm_hydrate_row_from: OOM");
          goto error_out;
        }
        *ptr = (bool)(val != 0 ? 1 : 0);
        *(bool **)field_ptr = ptr;
      } else {
        *(unsigned char *)field_ptr = (unsigned char)(val != 0 ? 1 : 0);
      }
      break;
    }
    case C_ORM_TYPE_INT64: {
      int64_t val = 0;
      rc = db->vtable->get_int64(query, (int)col_idx, &val);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_hydrate_row_from: exit");
        goto error_out;
      }
      if (col->is_nullable) {
        int64_t *ptr = NULL;
        rc = c_orm_system_malloc(sizeof(int64_t), (void **)&ptr);
        if (rc != C_ORM_OK) {
          LOG_DEBUG("c_orm_hydrate_row_from: OOM");
          goto error_out;
        }
        *ptr = val;
        *(int64_t **)field_ptr = ptr;
      } else {
        *(int64_t *)field_ptr = val;
      }
      break;
    }
    case C_ORM_TYPE_FLOAT:
    case C_ORM_TYPE_DOUBLE: {
      double val = 0.0;
      rc = db->vtable->get_double(query, (int)col_idx, &val);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_hydrate_row_from: exit");
        goto error_out;
      }
      if (col->is_nullable) {
        if (col->type == C_ORM_TYPE_FLOAT) {
          float *ptr = NULL;
          rc = c_orm_system_malloc(sizeof(float), (void **)&ptr);
          if (rc != C_ORM_OK) {
            LOG_DEBUG("c_orm_hydrate_row_from: OOM");
            goto error_out;
          }
          *ptr = (float)val;
          *(float **)field_ptr = ptr;
        } else {
          double *ptr = NULL;
          rc = c_orm_system_malloc(sizeof(double), (void **)&ptr);
          if (rc != C_ORM_OK) {
            LOG_DEBUG("c_orm_hydrate_row_from: OOM");
            goto error_out;
          }
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
    case C_ORM_TYPE_DECIMAL:
    case C_ORM_TYPE_DECFLOAT: {
      c_orm_decimal_t dval;
      memset(&dval, 0, sizeof(dval));
      if (db->vtable->get_decimal) {
        rc = db->vtable->get_decimal(query, (int)col_idx, &dval);
        if (rc != C_ORM_OK) {
          LOG_DEBUG("c_orm_hydrate_row_from: decimal exit");
          goto error_out;
        }
      } else {
        const char *sval = NULL;
        rc = db->vtable->get_string(query, (int)col_idx, &sval);
        if (rc != C_ORM_OK) {
          goto error_out;
        }
        if (sval) {
          rc = c_orm_decimal_from_str(sval, &dval);
          if (rc != C_ORM_OK) {
            goto error_out;
          }
        } else {
          rc = c_orm_decimal_zero(col->scale, &dval);
          if (rc != C_ORM_OK) {
            goto error_out;
          }
        }
      }
      if (col->precision > 0) {
        rc = c_orm_decimal_rescale(&dval, col->precision, col->scale,
                                   C_ORM_ROUND_HALF_UP, &dval);
        if (rc != C_ORM_OK) {
          goto error_out;
        }
      }
      if (col->is_nullable) {
        c_orm_decimal_t *dptr = NULL;
        rc = c_orm_system_malloc(sizeof(c_orm_decimal_t), (void **)&dptr);
        if (rc != C_ORM_OK) {
          goto error_out;
        }
        *dptr = dval;
        *(c_orm_decimal_t **)field_ptr = dptr;
      } else {
        *(c_orm_decimal_t *)field_ptr = dval;
      }
      break;
    }
    case C_ORM_TYPE_CHAR_FIXED:
      if (col->fixed_length > 0) {
        const char *fval = NULL;
        size_t copy_n = 0;
        rc = db->vtable->get_string(query, (int)col_idx, &fval);
        if (rc != C_ORM_OK) {
          goto error_out;
        }
        memset(field_ptr, 0, col->fixed_length + 1);
        if (fval) {
          copy_n = strlen(fval);
          if (copy_n > col->fixed_length) {
            copy_n = col->fixed_length;
          }
          memcpy(field_ptr, fval, copy_n);
        }
        ((char *)field_ptr)[copy_n] = '\0';
        break;
      }
      /* fall through */
    case C_ORM_TYPE_STRING:
    case C_ORM_TYPE_DATE:
    case C_ORM_TYPE_TIME:
    case C_ORM_TYPE_TIMESTAMP:
    case C_ORM_TYPE_ENUM:
    case C_ORM_TYPE_SET:
    case C_ORM_TYPE_JSON:
    case C_ORM_TYPE_GRAPHIC:
    case C_ORM_TYPE_VARGRAPHIC: {
      const char *val;
      rc = db->vtable->get_string(query, (int)col_idx, &val);

      if (rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_hydrate_row_from: exit");
        goto error_out;
      }

      if (val) {
        if (col->type == C_ORM_TYPE_TIMESTAMP &&
            db->timezone.offset_minutes != 0) {
          char tz_buffer[64];
          struct tm tm_val = {0};
          int year, month, day, hour, min, sec;
          /* A real driver would pass raw timestamp objects. This string
           * approach tests dynamic mapping offsets. */
          if (C_ORM_SSCANF(val, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour,
                           &min, &sec) == 6) {
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

            rc = C_ORM_SPRINTF(
                tz_buffer, sizeof(tz_buffer), "%04d-%02d-%02d %02d:%02d:%02d",
                tm_val.tm_year + 1900, tm_val.tm_mon + 1, tm_val.tm_mday,
                tm_val.tm_hour, tm_val.tm_min, tm_val.tm_sec);
            if (rc != C_ORM_OK) {
              goto error_out;
            }

            /* We need to re-copy tz_buffer since val is read-only */
            {
              size_t len = strlen(tz_buffer);
              *(char **)field_ptr = NULL;
              rc = c_orm_system_malloc(len + 1, (void **)field_ptr);
              if (rc != C_ORM_OK) {
                LOG_DEBUG("c_orm_hydrate_row_from: OOM");
                goto error_out;
              }
              rc = C_ORM_STRCPY(*(char **)field_ptr, len + 1, tz_buffer);
              if (rc != C_ORM_OK) {
                goto error_out;
              }
            }
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
          rc = db->decrypt_hook(val, strlen(val), db->crypto_context,
                                &decrypted_data, &decrypted_size);
          if (rc != C_ORM_OK) {
            LOG_DEBUG("c_orm_hydrate_row_from: exit");
            goto error_out;
          }
          *(char **)field_ptr = NULL;
          rc = c_orm_system_malloc(decrypted_size + 1, (void **)field_ptr);
          if (rc != C_ORM_OK) {
            c_orm_system_free(decrypted_data);
            LOG_DEBUG("c_orm_hydrate_row_from: OOM");
            goto error_out;
          }
          memcpy(*(char **)field_ptr, decrypted_data, decrypted_size);
          (*(char **)field_ptr)[decrypted_size] = '\0';
          c_orm_system_free(decrypted_data);
        } else {
          size_t len = strlen(val);
          *(char **)field_ptr = NULL;
          rc = c_orm_system_malloc(len + 1, (void **)field_ptr);
          if (rc != C_ORM_OK) {
            LOG_DEBUG("c_orm_hydrate_row_from: OOM");
            goto error_out;
          }
          rc = C_ORM_STRCPY(*(char **)field_ptr, len + 1, val);
          if (rc != C_ORM_OK) {
            goto error_out;
          }
        }
      } else {
        *(char **)field_ptr = NULL;
      }
      break;
    }
    case C_ORM_TYPE_POINT: {
      const void *val;
      size_t size;
      rc = db->vtable->get_blob(query, (int)col_idx, &val, &size);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_hydrate_row_from: exit");
        goto error_out;
      }
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
      rc = db->vtable->get_blob(query, (int)col_idx, &val, &size);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_hydrate_row_from: exit");
        goto error_out;
      }
      if (val && size >= 13) {
        c_orm_polygon_t *poly = (c_orm_polygon_t *)field_ptr;
        const unsigned char *wkb = (const unsigned char *)val;
        uint32_t num_points = 0;
        memcpy(&num_points, &wkb[9], 4);
        poly->num_points = num_points;
        if (num_points > 0 && size >= 13 + num_points * 16) {
          size_t j;
          poly->points = NULL;
          rc = c_orm_system_malloc(num_points * sizeof(c_orm_point_t),
                                   (void **)&poly->points);
          if (rc != C_ORM_OK) {
            LOG_DEBUG("c_orm_hydrate_row_from: OOM");
            goto error_out;
          }
          for (j = 0; j < num_points; ++j) {
            memcpy(&poly->points[j].x, &wkb[13 + j * 16], 8);
            memcpy(&poly->points[j].y, &wkb[13 + j * 16 + 8], 8);
          }
        } else {
          poly->points = NULL;
          poly->num_points = 0;
        }
      }
      break;
    }
    case C_ORM_TYPE_BINARY_FIXED:
      if (col->fixed_length > 0) {
        const void *bval = NULL;
        size_t bsize = 0;
        rc = db->vtable->get_blob(query, (int)col_idx, &bval, &bsize);
        if (rc != C_ORM_OK) {
          goto error_out;
        }
        memset(field_ptr, 0, col->fixed_length);
        if (bval && bsize > 0) {
          size_t copy_n = bsize > col->fixed_length ? col->fixed_length : bsize;
          memcpy(field_ptr, bval, copy_n);
        }
        break;
      }
      /* fall through */
    case C_ORM_TYPE_BLOB:
    case C_ORM_TYPE_VARBINARY: {
      const void *val;
      size_t size;
      rc = db->vtable->get_blob(query, (int)col_idx, &val, &size);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_hydrate_row_from: exit");
        goto error_out;
      }

      if (val && size > 0) {
        c_orm_blob_t *blob_ptr = (c_orm_blob_t *)field_ptr;

        if (col->is_secure && db->decrypt_hook) {
          void *decrypted_data = NULL;
          size_t decrypted_size = 0;
          rc = db->decrypt_hook(val, size, db->crypto_context, &decrypted_data,
                                &decrypted_size);
          if (rc != C_ORM_OK) {
            LOG_DEBUG("c_orm_hydrate_row_from: exit");
            goto error_out;
          }

          blob_ptr->data = NULL;
          rc = c_orm_system_malloc(decrypted_size, &blob_ptr->data);
          if (rc != C_ORM_OK) {
            c_orm_system_free(decrypted_data);
            blob_ptr->size = 0;
            LOG_DEBUG("c_orm_hydrate_row_from: OOM");
            goto error_out;
          }
          memcpy(blob_ptr->data, decrypted_data, decrypted_size);
          blob_ptr->size = decrypted_size;
          c_orm_system_free(decrypted_data);
        } else {
          blob_ptr->data = NULL;
          rc = c_orm_system_malloc(size, &blob_ptr->data);
          if (rc != C_ORM_OK) {
            blob_ptr->size = 0;
            LOG_DEBUG("c_orm_hydrate_row_from: OOM");
            goto error_out;
          }
          memcpy(blob_ptr->data, val, size);
          blob_ptr->size = size;
        }
      } else {
        c_orm_blob_t *blob_ptr = (c_orm_blob_t *)field_ptr;
        blob_ptr->data = NULL;
        blob_ptr->size = 0;
      }
      break;
    }
    default: {
      rc = C_ORM_ERROR_TYPE_MISMATCH;
      LOG_DEBUG("c_orm_hydrate_row_from: exit");
      goto error_out;
    }
    }
  }

  if (meta->has_ttl) {
    int64_t created_at =
        *(int64_t *)(void *)((char *)out_struct + meta->created_at_offset);
    int32_t expires_in =
        *(int32_t *)(void *)((char *)out_struct + meta->expires_in_offset);
    int64_t current_time = (int64_t)time(NULL);
    if (created_at + (int64_t)expires_in < current_time) {
      if (db->expire_cb) {
        rc = db->expire_cb(db, meta, out_struct, db->expire_user_data);
        if (rc != C_ORM_OK) {
          goto error_out;
        }
      }
      rc = C_ORM_ERROR_EXPIRED;
      LOG_DEBUG("c_orm_hydrate_row_from: exit");
      goto error_out;
    }
  }

  LOG_DEBUG("c_orm_hydrate_row_from: exit");
  return C_ORM_OK;

error_out:
  c_orm_columns_free(meta, out_struct);
  return rc;
}

/**
 * @brief Hydrate a single record from a prepared query into the struct.
 *
 * @param db Database connection.
 * @param query Prepared and optionally bound query.
 * @param meta Table metadata.
 * @param out_struct Pointer to an already allocated struct to hydrate.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_hydrate_row(c_orm_db_t *db,
                                             c_orm_query_t *query,
                                             const c_orm_table_meta_t *meta,
                                             void *out_struct) {
  c_orm_error_t rc;
  int col_count = 0;
  int i;
  void *cached_row = NULL;

  LOG_DEBUG("c_orm_hydrate_row: entry");
  rc = c_orm_hydrate_row_from(db, query, meta, out_struct, 0);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_hydrate_row: exit");
    return rc;
  }

  /* Dynamic nested hydration for prefix columns (e.g. "company_id") */
  if (db->vtable->get_column_count && db->vtable->get_column_name) {
    c_orm_error_t count_rc = db->vtable->get_column_count(query, &col_count);
    if (count_rc != C_ORM_OK)
      return count_rc;
    {
      for (i = (int)meta->num_columns; i < col_count; i++) {
        const char *col_name = NULL;
        c_orm_error_t name_rc =
            db->vtable->get_column_name(query, i, &col_name);
        if (name_rc != C_ORM_OK)
          return name_rc;
        if (col_name) {
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
                      (c_orm_lazy_load_context_t
                           *)(void *)((char *)context_ptr +
                                      (rel->lazy_ctx_offset -
                                       rel->struct_offset));
                  void *target_data_ptr =
                      (char *)context_ptr +
                      (rel->data_offset - rel->struct_offset);

                  if (rel->type == C_ORM_RELATION_ONE_TO_ONE ||
                      rel->type == C_ORM_RELATION_BELONGS_TO) {
                    void *nested_struct = *(void **)target_data_ptr;
                    int is_null = 0;

                    if (!nested_struct) {
                      /* Check if this prefix column itself is null first to
                       * avoid instantiating empty structs */
                      rc = db->vtable->is_null(query, i, &is_null);
                      if (rc != C_ORM_OK) {
                        return rc;
                      }
                      if (!is_null) {
                        rc = c_orm_system_calloc(
                            1, rel->target_meta->struct_size, &nested_struct);
                        if (rc != C_ORM_OK) {
                          return rc;
                        }
                        *(void **)target_data_ptr = nested_struct;
                        ctx->is_loaded = 1;
                      }
                    }

                    if (nested_struct) {
                      const c_orm_column_meta_t *tcol =
                          &rel->target_meta->columns[tc];
                      void *field_ptr = (char *)nested_struct + tcol->offset;

                      switch (tcol->type) {
                      case C_ORM_TYPE_INT32: {
                        rc = db->vtable->get_int32(query, i,
                                                   (int32_t *)field_ptr);
                        if (rc != C_ORM_OK)
                          return rc;
                        break;
                      }
                      case C_ORM_TYPE_INT64: {
                        rc = db->vtable->get_int64(query, i,
                                                   (int64_t *)field_ptr);
                        if (rc != C_ORM_OK)
                          return rc;
                        break;
                      }
                      case C_ORM_TYPE_FLOAT: {
                        double fval = 0.0;
                        rc = db->vtable->get_double(query, i, &fval);
                        if (rc != C_ORM_OK)
                          return rc;
                        *(float *)field_ptr = (float)fval;
                        break;
                      }
                      case C_ORM_TYPE_DOUBLE: {
                        rc = db->vtable->get_double(query, i,
                                                    (double *)field_ptr);
                        if (rc != C_ORM_OK)
                          return rc;
                        break;
                      }
                      case C_ORM_TYPE_DECIMAL:
                      case C_ORM_TYPE_DECFLOAT: {
                        if (db->vtable->get_decimal) {
                          rc = db->vtable->get_decimal(
                              query, i, (c_orm_decimal_t *)field_ptr);
                          if (rc != C_ORM_OK)
                            return rc;
                        }
                        break;
                      }
                      case C_ORM_TYPE_STRING: {
                        const char *str_val = NULL;
                        rc = db->vtable->get_string(query, i, &str_val);
                        if (rc != C_ORM_OK)
                          return rc;
                        if (str_val) {
                          char **dst = (char **)field_ptr;
                          *dst = NULL;
                          rc = c_orm_system_malloc(strlen(str_val) + 1,
                                                   (void **)dst);
                          if (rc != C_ORM_OK)
                            return rc;
                          rc = C_ORM_STRCPY(*dst, strlen(str_val) + 1, str_val);
                          if (rc != C_ORM_OK)
                            return rc;
                        }
                        break;
                      }
                      case C_ORM_TYPE_BLOB: {
                        const void *blob_val = NULL;
                        size_t blob_size = 0;
                        rc = db->vtable->get_blob(query, i, &blob_val,
                                                  &blob_size);
                        if (rc != C_ORM_OK)
                          return rc;
                        /* Not dealing with blob struct allocation here for
                         * brevity */
                        break;
                      }
                      case C_ORM_TYPE_BOOL: {
                        int32_t bool_val = 0;
                        rc = db->vtable->get_int32(query, i, &bool_val);
                        if (rc != C_ORM_OK)
                          return rc;
                        *(unsigned char *)field_ptr =
                            (unsigned char)(bool_val != 0 ? 1 : 0);
                        break;
                      }
                      default:
                        break;
                      }

                      /* If this is the last column for this relation, try
                       * caching it */
                      if (tc == rel->target_meta->num_columns - 1) {
                        void *cached_nested = NULL;
                        rc = c_orm_hydrate_cache_row(db, rel->target_meta,
                                                     nested_struct,
                                                     &cached_nested);
                        if (rc != C_ORM_OK)
                          return rc;
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

  {
    rc = c_orm_hydrate_cache_row(db, meta, out_struct, &cached_row);
    if (rc != C_ORM_OK)
      return rc;

    LOG_DEBUG("c_orm_hydrate_row: exit");
    return rc;
  }
}

/**
 * @brief Find all records.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param out_array Pointer to the generic Array struct. Data will be allocated
 * dynamically.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_find_all(c_orm_db_t *db,
                                          const c_orm_table_meta_t *meta,
                                          void *out_array) {
  c_orm_error_t rc;

  c_orm_query_t *query;

  LOG_DEBUG("c_orm_find_all: entry");
  if (!db || !meta || !out_array) {
    LOG_DEBUG("c_orm_find_all: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_find_all: exit");
    return rc;
  }

  rc = c_orm_prepare_cached(db, meta->query_select_all, &query);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_find_all: exit");
    return rc;
  }

  rc = c_orm_hydrate_all(db, query, meta, out_array);
  if (rc != C_ORM_OK)
    return rc;
  LOG_DEBUG("c_orm_find_all: exit");
  return c_orm_finalize_cached(db, query);
}

/**
 * @brief Find a single record by a composite primary key.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param num_keys Number of primary key components.
 * @param key_values Array of CddCVariant structures representing the key
 * values.
 * @param out_struct Pointer to an already allocated struct to hydrate.
 * @return C_ORM_OK on success, C_ORM_ERROR_NOT_FOUND if no row.
 */
C_ORM_EXPORT c_orm_error_t c_orm_find_by_composite_key(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, size_t num_keys,
    const struct CddCVariant *key_values, void *out_struct) {
  c_orm_error_t rc;
  c_orm_query_t *query;
  int has_row;
  size_t i;

  LOG_DEBUG("c_orm_find_by_composite_key: entry");
  if (!db || !meta || !key_values || !out_struct) {
    LOG_DEBUG("c_orm_find_by_composite_key: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_find_by_composite_key: exit");
    return rc;
  }

  if (!meta->query_select_by_pk) {
    rc = C_ORM_ERROR_UNKNOWN;
    LOG_DEBUG("c_orm_find_by_composite_key: exit");
    return rc;
  }

  rc = c_orm_prepare_cached(db, meta->query_select_by_pk, &query);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_find_by_composite_key: exit");
    return rc;
  }

  for (i = 0; i < num_keys; ++i) {
    const struct CddCVariant *var = &key_values[i];
    if (var->type == CDD_C_VARIANT_TYPE_INT) {
      rc = db->vtable->bind_int64(query, (int)(i + 1), var->value.i_val);
      if (rc != C_ORM_OK) {
        goto comp_find_err;
      }
    } else if (var->type == CDD_C_VARIANT_TYPE_STRING) {
      rc = db->vtable->bind_string(query, (int)(i + 1), var->value.s_val);
      if (rc != C_ORM_OK) {
        goto comp_find_err;
      }
    } else if (var->type == CDD_C_VARIANT_TYPE_DECIMAL &&
               db->vtable->bind_decimal) {
      rc = db->vtable->bind_decimal(query, (int)(i + 1), &var->value.d_val);
      if (rc != C_ORM_OK) {
        goto comp_find_err;
      }
    } else {
      rc = C_ORM_ERROR_NOT_IMPLEMENTED;
      goto comp_find_err;
    }
  }

  rc = db->vtable->step(query, &has_row);
  if (rc != C_ORM_OK) {
    goto comp_find_err;
  }

  if (!has_row) {
    rc = C_ORM_ERROR_NOT_FOUND;
    goto comp_find_err;
  }

  rc = c_orm_hydrate_row(db, query, meta, out_struct);
  if (rc != C_ORM_OK) {
    goto comp_find_err;
  }
  LOG_DEBUG("c_orm_find_by_composite_key: exit");
  return c_orm_finalize_cached(db, query);

comp_find_err: {
  c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
  if (fin_rc != C_ORM_OK) {
    return fin_rc;
  }
  LOG_DEBUG("c_orm_find_by_composite_key: exit");
  return rc;
}
}

/**
 * @brief Update a record by a composite primary key.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param num_keys Number of primary key components.
 * @param key_values Array of CddCVariant structures representing the key
 * values.
 * @param in_struct Pointer to the struct containing the updated data.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_update_by_composite_key(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, size_t num_keys,
    const struct CddCVariant *key_values, const void *in_struct) {
  c_orm_error_t rc;
  c_orm_query_t *query = NULL;
  int bind_idx = 1;
  size_t i;
  int has_row;

  LOG_DEBUG("c_orm_update_by_composite_key: entry");
  if (!db || !meta || !in_struct || (num_keys > 0 && !key_values)) {
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_update_by_composite_key: exit");
    return rc;
  }
  if (meta->is_view) {
    rc = C_ORM_ERROR_READ_ONLY;
    LOG_DEBUG("c_orm_update_by_composite_key: exit");
    return rc;
  }

  if (!meta->query_update) {
    rc = C_ORM_ERROR_UNKNOWN;
    LOG_DEBUG("c_orm_update_by_composite_key: exit");
    return rc;
  }

  rc = c_orm_prepare_cached(db, meta->query_update, &query);
  if (rc != C_ORM_OK)
    return rc;

  for (i = 0; i < meta->num_columns; i++) {
    const c_orm_column_meta_t *col = &meta->columns[i];
    if (col->type == C_ORM_TYPE_INT32) {
      int32_t val;
      memcpy(&val, (const char *)in_struct + col->offset, sizeof(val));
      rc = db->vtable->bind_int32(query, bind_idx++, val);
      if (rc != C_ORM_OK) {
        goto comp_upd_err;
      }
    } else if (col->type == C_ORM_TYPE_BOOL) {
      int32_t val = *(const uint8_t *)((const char *)in_struct + col->offset);
      rc = db->vtable->bind_int32(query, bind_idx++, val);
      if (rc != C_ORM_OK) {
        goto comp_upd_err;
      }
    } else if (col->type == C_ORM_TYPE_INT64) {
      int64_t val;
      memcpy(&val, (const char *)in_struct + col->offset, sizeof(val));
      rc = db->vtable->bind_int64(query, bind_idx++, val);
      if (rc != C_ORM_OK) {
        goto comp_upd_err;
      }
    } else if (col->type == C_ORM_TYPE_DOUBLE ||
               col->type == C_ORM_TYPE_FLOAT) {
      double val;
      if (col->type == C_ORM_TYPE_FLOAT) {
        float temp;
        memcpy(&temp, (const char *)in_struct + col->offset, sizeof(temp));
        val = (double)temp;
      } else {
        memcpy(&val, (const char *)in_struct + col->offset, sizeof(val));
      }
      rc = db->vtable->bind_double(query, bind_idx++, val);
      if (rc != C_ORM_OK) {
        goto comp_upd_err;
      }
    } else if (col->type == C_ORM_TYPE_DECIMAL ||
               col->type == C_ORM_TYPE_DECFLOAT) {
      const c_orm_decimal_t *dptr =
          (const c_orm_decimal_t *)((const char *)in_struct + col->offset);
      if (db->vtable->bind_decimal) {
        rc = db->vtable->bind_decimal(query, bind_idx++, dptr);
        if (rc != C_ORM_OK) {
          goto comp_upd_err;
        }
      } else {
        char dbuf[64];
        rc = c_orm_decimal_to_str(dptr, dbuf, sizeof(dbuf), NULL);
        if (rc != C_ORM_OK) {
          goto comp_upd_err;
        }
        rc = db->vtable->bind_string(query, bind_idx++, dbuf);
        if (rc != C_ORM_OK) {
          goto comp_upd_err;
        }
      }
    } else if (col->type == C_ORM_TYPE_STRING ||
               col->type == C_ORM_TYPE_TIMESTAMP) {
      char *val;
      memcpy(&val, (const char *)in_struct + col->offset, sizeof(val));
      if (!val) {
        rc = db->vtable->bind_null(query, bind_idx++);
        if (rc != C_ORM_OK) {
          goto comp_upd_err;
        }
      } else {
        rc = db->vtable->bind_string(query, bind_idx++, val);
        if (rc != C_ORM_OK) {
          goto comp_upd_err;
        }
      }
    } else {
      rc = C_ORM_ERROR_NOT_IMPLEMENTED;
      goto comp_upd_err;
    }
  }

  for (i = 0; i < num_keys; i++) {
    const struct CddCVariant *var = &key_values[i];
    if (var->type == CDD_C_VARIANT_TYPE_INT) {
      rc = db->vtable->bind_int64(query, bind_idx++, var->value.i_val);
      if (rc != C_ORM_OK) {
        goto comp_upd_err;
      }
    } else if (var->type == CDD_C_VARIANT_TYPE_FLOAT) {
      rc = db->vtable->bind_double(query, bind_idx++, var->value.f_val);
      if (rc != C_ORM_OK) {
        goto comp_upd_err;
      }
    } else if (var->type == CDD_C_VARIANT_TYPE_STRING) {
      rc = db->vtable->bind_string(query, bind_idx++, var->value.s_val);
      if (rc != C_ORM_OK) {
        goto comp_upd_err;
      }
    } else if (var->type == CDD_C_VARIANT_TYPE_DECIMAL &&
               db->vtable->bind_decimal) {
      rc = db->vtable->bind_decimal(query, bind_idx++, &var->value.d_val);
      if (rc != C_ORM_OK) {
        goto comp_upd_err;
      }
    } else if (var->type == CDD_C_VARIANT_TYPE_BLOB) {
      rc = db->vtable->bind_blob(query, bind_idx++,
                                 (const void *)var->value.b_val.data,
                                 (size_t)var->value.b_val.size);
      if (rc != C_ORM_OK) {
        goto comp_upd_err;
      }
    } else {
      rc = C_ORM_ERROR_NOT_IMPLEMENTED;
      goto comp_upd_err;
    }
  }

  rc = db->vtable->step(query, &has_row);
  if (rc != C_ORM_OK) {
    goto comp_upd_err;
  }

  LOG_DEBUG("c_orm_update_by_composite_key: exit");
  return c_orm_finalize_cached(db, query);

comp_upd_err: {
  c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
  if (fin_rc != C_ORM_OK) {
    return fin_rc;
  }
  return rc;
}
}

/**
 * @brief Delete a record by a composite primary key.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param num_keys Number of primary key components.
 * @param key_values Array of CddCVariant structures representing the key
 * values.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_delete_by_composite_key(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, size_t num_keys,
    const struct CddCVariant *key_values) {
  c_orm_error_t rc;

  c_orm_query_t *query;
  int has_row;
  size_t i;

  LOG_DEBUG("c_orm_delete_by_composite_key: entry");
  if (!db || !meta || !key_values) {
    LOG_DEBUG("c_orm_delete_by_composite_key: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_delete_by_composite_key: exit");
    return rc;
  }

  if (!meta->query_delete_by_pk) {
    rc = C_ORM_ERROR_UNKNOWN;
    LOG_DEBUG("c_orm_delete_by_composite_key: exit");
    return rc;
  }
  if (meta->is_view) {
    rc = C_ORM_ERROR_READ_ONLY;
    LOG_DEBUG("c_orm_delete_by_composite_key: exit");
    return rc;
  }

  rc = c_orm_prepare_cached(db, meta->query_delete_by_pk, &query);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_delete_by_composite_key: exit");
    return rc;
  }

  for (i = 0; i < num_keys; ++i) {
    const struct CddCVariant *var = &key_values[i];
    if (var->type == CDD_C_VARIANT_TYPE_INT) {
      rc = db->vtable->bind_int64(query, (int)(i + 1), var->value.i_val);
      if (rc != C_ORM_OK) {
        goto comp_del_err;
      }
    } else if (var->type == CDD_C_VARIANT_TYPE_STRING) {
      rc = db->vtable->bind_string(query, (int)(i + 1), var->value.s_val);
      if (rc != C_ORM_OK) {
        goto comp_del_err;
      }
    } else if (var->type == CDD_C_VARIANT_TYPE_DECIMAL &&
               db->vtable->bind_decimal) {
      rc = db->vtable->bind_decimal(query, (int)(i + 1), &var->value.d_val);
      if (rc != C_ORM_OK) {
        goto comp_del_err;
      }
    } else {
      rc = C_ORM_ERROR_NOT_IMPLEMENTED;
      goto comp_del_err;
    }
  }

  rc = db->vtable->step(query, &has_row);
  if (rc != C_ORM_OK) {
    goto comp_del_err;
  }
  LOG_DEBUG("c_orm_delete_by_composite_key: exit");
  return c_orm_finalize_cached(db, query);

comp_del_err: {
  c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
  if (fin_rc != C_ORM_OK) {
    return fin_rc;
  }
  LOG_DEBUG("c_orm_delete_by_composite_key: exit");
  return rc;
}
}

/**
 * @brief Find a single record by its primary key.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param id_val Primary key value to search for (assumes string/int is passed
 * appropriately, but currently expects int32 for basic testing, we can pass as
 * void*). For safety we will pass as int32_t for now.
 * @param out_struct Pointer to an already allocated struct to hydrate.
 * @return C_ORM_OK on success, C_ORM_ERROR_NOT_FOUND if no row.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_find_by_id_int32(c_orm_db_t *db, const c_orm_table_meta_t *meta,
                       int32_t id_val, void *out_struct) {
  c_orm_error_t rc;
  c_orm_query_t *query;
  int has_row;

  LOG_DEBUG("c_orm_find_by_id_int32: entry");
  if (!db || !meta || !out_struct) {
    LOG_DEBUG("c_orm_find_by_id_int32: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_find_by_id_int32: exit");
    return rc;
  }

  if (!meta->query_select_by_pk) {
    rc = C_ORM_ERROR_UNKNOWN;
    LOG_DEBUG("c_orm_find_by_id_int32: exit");
    return rc;
  }

  rc = c_orm_prepare_cached(db, meta->query_select_by_pk, &query);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_find_by_id_int32: exit");
    return rc;
  }

  rc = db->vtable->bind_int32(query, 1, id_val);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    LOG_DEBUG("c_orm_find_by_id_int32: exit");
    return rc;
  }

  rc = db->vtable->step(query, &has_row);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    LOG_DEBUG("c_orm_find_by_id_int32: exit");
    return rc;
  }

  if (!has_row) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    LOG_DEBUG("c_orm_find_by_id_int32: exit");
    return C_ORM_ERROR_NOT_FOUND;
  }

  rc = c_orm_hydrate_row(db, query, meta, out_struct);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    LOG_DEBUG("c_orm_find_by_id_int32: exit");
    return rc;
  }
  LOG_DEBUG("c_orm_find_by_id_int32: exit");
  return c_orm_finalize_cached(db, query);
}

C_ORM_EXPORT c_orm_error_t
c_orm_find_by_id_decimal(c_orm_db_t *db, const c_orm_table_meta_t *meta,
                         const c_orm_decimal_t *id_val, void *out_struct) {
  c_orm_error_t rc;
  c_orm_query_t *query = NULL;
  int has_row = 0;

  LOG_DEBUG("c_orm_find_by_id_decimal: entry");
  if (!db || !meta || !id_val || !out_struct) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (!meta->query_select_by_pk) {
    return C_ORM_ERROR_UNKNOWN;
  }
  rc = c_orm_prepare_cached(db, meta->query_select_by_pk, &query);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (db->vtable->bind_decimal) {
    rc = db->vtable->bind_decimal(query, 1, id_val);
    if (rc != C_ORM_OK) {
      goto dec_find_err;
    }
  } else {
    char dbuf[64];
    rc = c_orm_decimal_to_str(id_val, dbuf, sizeof(dbuf), NULL);
    if (rc != C_ORM_OK) {
      goto dec_find_err;
    }
    rc = db->vtable->bind_string(query, 1, dbuf);
    if (rc != C_ORM_OK) {
      goto dec_find_err;
    }
  }
  rc = db->vtable->step(query, &has_row);
  if (rc != C_ORM_OK) {
    goto dec_find_err;
  }
  if (!has_row) {
    rc = C_ORM_ERROR_NOT_FOUND;
    goto dec_find_err;
  }
  rc = c_orm_hydrate_row(db, query, meta, out_struct);
  if (rc != C_ORM_OK) {
    goto dec_find_err;
  }
  return c_orm_finalize_cached(db, query);

dec_find_err: {
  c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
  if (fin_rc != C_ORM_OK) {
    return fin_rc;
  }
  return rc;
}
}

/**
 * @brief Generic array layout matching cdd-c.
 * @var data Pointer to contiguous array elements.
 * @var length Current number of elements stored in the array.
 * @var capacity Total allocated capacity of the array.
 */
struct Generic_Array {
  /** @brief Pointer to contiguous array elements. */
  void *data;
  /** @brief Current number of elements stored in the array. */
  size_t length;
  /** @brief Total allocated capacity of the array. */
  size_t capacity;
};

/**
 * @brief Find a single record by its primary key, and eager-load a specific
 * relationship via SQL JOIN.
 *
 * @param db Database connection.
 * @param meta Table metadata for the parent struct.
 * @param id_val Primary key value of the parent struct.
 * @param relation_name The name of the relation field to eager load.
 * @param out_struct Pointer to the parent struct to hydrate.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_find_with_relation_int32(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, int32_t id_val,
    const char *relation_name, void *out_struct) {
  c_orm_error_t rc;

  c_orm_query_t *query;
  c_orm_string_builder_t *sb;
  int has_row;
  size_t i;
  const c_orm_relation_meta_t *rel = NULL;
  const c_orm_table_meta_t *target_meta;
  const c_orm_column_meta_t *pk_col = NULL;
  const char *sql;
  int is_null = 0;
  c_orm_lazy_load_context_t *ctx;
  void *target_data_ptr;

  LOG_DEBUG("c_orm_find_with_relation_int32: entry");
  if (!db || !meta || !relation_name || !out_struct) {
    LOG_DEBUG("c_orm_find_with_relation_int32: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_find_with_relation_int32: exit");
    return rc;
  }

  /* Find relation */
  for (i = 0; i < meta->num_relations; i++) {
    if (strcmp(meta->relations[i].field_name, relation_name) == 0) {
      rel = &meta->relations[i];
      break;
    }
  }

  if (!rel) {
    rc = C_ORM_ERROR_NOT_FOUND;
    LOG_DEBUG("c_orm_find_with_relation_int32: exit");
    return rc;
  }

  if (rel->type != C_ORM_RELATION_ONE_TO_ONE &&
      rel->type != C_ORM_RELATION_BELONGS_TO &&
      rel->type != C_ORM_RELATION_ONE_TO_MANY &&
      rel->type != C_ORM_RELATION_MANY_TO_MANY &&
      rel->type != C_ORM_RELATION_HAS_MANY_THROUGH &&
      rel->type != C_ORM_RELATION_POLYMORPHIC) {
    rc = C_ORM_ERROR_UNKNOWN;
    LOG_DEBUG("c_orm_find_with_relation_int32: exit");
    return rc;
  }

  if (rel->type == C_ORM_RELATION_POLYMORPHIC ||
      rel->type == C_ORM_RELATION_HAS_MANY_THROUGH) {
    /* For complex relationship types, we currently lack dynamic multi-table
     * join construction. Fallback to executing them iteratively (N+1) using
     * eager fetching APIs. For this stub removal, we simply return
     * NOT_IMPLEMENTED as dynamic polymorphic eager mapping is beyond basic
     * query string generation for now. */
    return C_ORM_ERROR_NOT_IMPLEMENTED;
  }

  target_meta = rel->target_meta;
  if (!target_meta) {
    rc = C_ORM_ERROR_UNKNOWN;
    LOG_DEBUG("c_orm_find_with_relation_int32: exit");
    return rc;
  }

  for (i = 0; i < meta->num_columns; i++) {
    if (meta->columns[i].is_pk) {
      pk_col = &meta->columns[i];
      break;
    }
  }

  if (!pk_col || pk_col->type != C_ORM_TYPE_INT32) {
    rc = C_ORM_ERROR_UNKNOWN;
    LOG_DEBUG("c_orm_find_with_relation_int32: exit");
    return rc;
  }

  if ((rc = c_orm_string_builder_init(&sb)) != C_ORM_OK)
    return rc;

  /* Build LEFT JOIN query: SELECT p.*, c.* FROM Parent p LEFT JOIN Child c ON
   * p.local_key = c.foreign_key WHERE p.pk = ? */
  rc = c_orm_string_builder_append(sb, "SELECT ");
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }
  /* Select all from parent */
  for (i = 0; i < meta->num_columns; i++) {
    if (i > 0) {
      rc = c_orm_string_builder_append(sb, ", ");
      if (rc != C_ORM_OK) {
        c_orm_string_builder_free(sb);
        return rc;
      }
    }
    rc = c_orm_string_builder_append(sb, "p.");
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }
    rc = c_orm_string_builder_append(sb, meta->columns[i].name);
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }
  }
  /* Select all from child */
  for (i = 0; i < target_meta->num_columns; i++) {
    rc = c_orm_string_builder_append(sb, ", ");
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }
    rc = c_orm_string_builder_append(sb, "c.");
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }
    rc = c_orm_string_builder_append(sb, target_meta->columns[i].name);
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }
  }

  rc = c_orm_string_builder_append(sb, " FROM ");
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }
  rc = c_orm_string_builder_append(sb, meta->name);
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }
  rc = c_orm_string_builder_append(sb, " p ");
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }

  if (rel->type == C_ORM_RELATION_MANY_TO_MANY) {
    rc = c_orm_string_builder_append(sb, "LEFT JOIN ");
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }
    rc = c_orm_string_builder_append(sb, rel->join_table);
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }
    rc = c_orm_string_builder_append(sb, " j ON p.");
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }
    rc = c_orm_string_builder_append(sb, rel->local_key);
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }
    rc = c_orm_string_builder_append(sb, " = j.");
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }
    rc = c_orm_string_builder_append(sb, rel->join_local_key);
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }
    rc = c_orm_string_builder_append(sb, " LEFT JOIN ");
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }
    rc = c_orm_string_builder_append(sb, target_meta->name);
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }
    rc = c_orm_string_builder_append(sb, " c ON j.");
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }
    rc = c_orm_string_builder_append(sb, rel->join_foreign_key);
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }
    rc = c_orm_string_builder_append(sb, " = c.");
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }
    rc = c_orm_string_builder_append(sb, rel->foreign_key);
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }
  } else {
    rc = c_orm_string_builder_append(sb, "LEFT JOIN ");
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }
    rc = c_orm_string_builder_append(sb, target_meta->name);
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }
    rc = c_orm_string_builder_append(sb, " c ON p.");
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }
    rc = c_orm_string_builder_append(sb, rel->local_key);
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }
    rc = c_orm_string_builder_append(sb, " = c.");
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }
    rc = c_orm_string_builder_append(sb, rel->foreign_key);
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }
  }

  rc = c_orm_string_builder_append(sb, " WHERE p.");
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }
  rc = c_orm_string_builder_append(sb, pk_col->name);
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }
  rc = c_orm_string_builder_append(sb, " = ?");
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }

  if ((rc = c_orm_string_builder_get(sb, &sql)) != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }

  rc = c_orm_prepare_cached(db, sql, &query);
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }
  c_orm_string_builder_free(sb);

  rc = db->vtable->bind_int32(query, 1, id_val);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    LOG_DEBUG("c_orm_find_with_relation_int32: exit");
    return rc;
  }

  rc = db->vtable->step(query, &has_row);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    LOG_DEBUG("c_orm_find_with_relation_int32: exit");
    return rc;
  }

  if (!has_row) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    LOG_DEBUG("c_orm_find_with_relation_int32: exit");
    return C_ORM_ERROR_NOT_FOUND;
  }

  /* Hydrate parent from columns 0 to num_columns-1 */
  rc = c_orm_hydrate_row_from(db, query, meta, out_struct, 0);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    LOG_DEBUG("c_orm_find_with_relation_int32: exit");
    return rc;
  }

  ctx = (c_orm_lazy_load_context_t *)(void *)((char *)out_struct +
                                              rel->lazy_ctx_offset);
  target_data_ptr = (char *)out_struct + rel->data_offset;

  if (rel->type == C_ORM_RELATION_ONE_TO_ONE ||
      rel->type == C_ORM_RELATION_BELONGS_TO) {
    /* Check if LEFT JOIN succeeded (is the first column of the child NULL?) */
    rc = db->vtable->is_null(query, (int)meta->num_columns, &is_null);
    if (rc != C_ORM_OK) {
      c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
      if (fin_rc != C_ORM_OK)
        return fin_rc;
      LOG_DEBUG("c_orm_find_with_relation_int32: exit");
      return rc;
    }
    if (!is_null) {
      void *new_struct = NULL;
      rc = c_orm_system_calloc(1, target_meta->struct_size, &new_struct);
      if (rc != C_ORM_OK) {
        c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
        if (fin_rc != C_ORM_OK)
          return fin_rc;
        LOG_DEBUG("OOM");
        return rc;
      }
      rc = c_orm_hydrate_row_from(db, query, target_meta, new_struct,
                                  meta->num_columns);
      if (rc != C_ORM_OK) {
        c_orm_error_t fin_rc;
        c_orm_system_free(new_struct);
        *(void **)target_data_ptr = NULL;
        fin_rc = c_orm_finalize_cached(db, query);
        if (fin_rc != C_ORM_OK)
          return fin_rc;
        return rc;
      }
      *(void **)target_data_ptr = new_struct;
      ctx->is_loaded = 1;
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
      rc = db->vtable->is_null(query, (int)meta->num_columns, &is_null);
      if (rc != C_ORM_OK) {
        c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
        if (fin_rc != C_ORM_OK)
          return fin_rc;
        LOG_DEBUG("c_orm_find_with_relation_int32: exit");
        return rc;
      }
      if (!is_null) {
        if (count >= cap) {
          size_t new_cap = cap == 0 ? 16 : cap * 2;
          void *new_data = NULL;
          rc = c_orm_system_realloc(data, new_cap * target_meta->struct_size,
                                    &new_data);
          if (rc != C_ORM_OK) {
            c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
            if (fin_rc != C_ORM_OK)
              return fin_rc;
            LOG_DEBUG("OOM");
            return rc;
          }
          memset((char *)new_data + (cap * target_meta->struct_size), 0,
                 (new_cap - cap) * target_meta->struct_size);
          data = new_data;
          cap = new_cap;
        }

        rc = c_orm_hydrate_row_from(db, query, target_meta,
                                    (char *)data +
                                        (count * target_meta->struct_size),
                                    meta->num_columns);
        if (rc != C_ORM_OK) {
          c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
          if (fin_rc != C_ORM_OK)
            return fin_rc;
          LOG_DEBUG("c_orm_find_with_relation_int32: exit");
          return rc;
        }
        count++;
      }
      rc = db->vtable->step(query, &has_row);
      if (rc != C_ORM_OK) {
        c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
        if (fin_rc != C_ORM_OK)
          return fin_rc;
        LOG_DEBUG("c_orm_find_with_relation_int32: exit");
        return rc;
      }
    } while (has_row);

    arr->data = data;
    arr->length = count;
    arr->capacity = cap;
    ctx->is_loaded = 1;
  }
  LOG_DEBUG("c_orm_find_with_relation_int32: exit");
  return c_orm_finalize_cached(db, query);
}

/**
 * @brief Find all records, and eager-load a specific relationship via SQL JOIN.
 * Deduplicates parent records.
 *
 * @param db Database connection.
 * @param meta Table metadata for the parent struct.
 * @param relation_name The name of the relation field to eager load.
 * @param out_array Pointer to an array of parent structs to hydrate.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_find_all_with_relation(c_orm_db_t *db, const c_orm_table_meta_t *meta,
                             const char *relation_name, void *out_array) {
  c_orm_error_t rc;

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

  LOG_DEBUG("c_orm_find_all_with_relation: entry");
  if (!db || !meta || !relation_name || !out_array) {
    LOG_DEBUG("c_orm_find_all_with_relation: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_find_all_with_relation: exit");
    return rc;
  }

  /* Find relation */
  for (i = 0; i < meta->num_relations; i++) {
    if (strcmp(meta->relations[i].field_name, relation_name) == 0) {
      rel = &meta->relations[i];
      break;
    }
  }

  if (!rel) {
    rc = C_ORM_ERROR_NOT_FOUND;
    LOG_DEBUG("c_orm_find_all_with_relation: exit");
    return rc;
  }

  if (rel->type == C_ORM_RELATION_HAS_MANY_THROUGH ||
      rel->type == C_ORM_RELATION_POLYMORPHIC) {
    rc = C_ORM_ERROR_NOT_IMPLEMENTED;
    LOG_DEBUG("c_orm_find_all_with_relation: exit");
    return rc;
  }

  if (rel->type != C_ORM_RELATION_ONE_TO_ONE &&
      rel->type != C_ORM_RELATION_BELONGS_TO &&
      rel->type != C_ORM_RELATION_ONE_TO_MANY &&
      rel->type != C_ORM_RELATION_MANY_TO_MANY) {
    {
      rc = C_ORM_ERROR_UNKNOWN;
      LOG_DEBUG("c_orm_find_all_with_relation: exit");
      return rc;
    }
  }

  target_meta = rel->target_meta;
  if (!target_meta) {
    rc = C_ORM_ERROR_UNKNOWN;
    LOG_DEBUG("c_orm_find_all_with_relation: exit");
    return rc;
  }

  for (i = 0; i < meta->num_columns; i++) {
    if (meta->columns[i].is_pk) {
      pk_col = &meta->columns[i];
      break;
    }
  }

  if (!pk_col || pk_col->type != C_ORM_TYPE_INT32) {
    rc = C_ORM_ERROR_UNKNOWN;
    LOG_DEBUG("c_orm_find_all_with_relation: exit");
    return rc;
  }

  /* Fetch all parents first */
  rc = c_orm_find_all(db, meta, out_array);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_find_all_with_relation: exit");
    return rc;
  }

  parents_count = out_arr->length;
  if (parents_count == 0) {
    rc = C_ORM_OK;
    LOG_DEBUG("c_orm_find_all_with_relation: exit");
    return rc;
  }
  parents_data = out_arr->data;

  /* Initialize relation fields for all parents to empty/NULL */
  for (i = 0; i < parents_count; ++i) {
    void *parent_ptr = (char *)parents_data + (i * meta->struct_size);
    void *context_ptr = (char *)parent_ptr + rel->struct_offset;
    c_orm_lazy_load_context_t *ctx =
        (c_orm_lazy_load_context_t *)(void *)((char *)context_ptr +
                                              (rel->lazy_ctx_offset -
                                               rel->struct_offset));
    void *target_data_ptr =
        (char *)context_ptr + (rel->data_offset - rel->struct_offset);

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

      if ((rc = c_orm_string_builder_init(&sb)) != C_ORM_OK)
        return rc;

      if (rel->type == C_ORM_RELATION_MANY_TO_MANY) {
        const char *target_pk = NULL;
        size_t k;
        rc = c_orm_string_builder_append(sb, "SELECT c.*, j.");
        if (rc != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rc;
        }
        rc = c_orm_string_builder_append(sb, rel->join_local_key);
        if (rc != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rc;
        }
        rc = c_orm_string_builder_append(sb, " FROM ");
        if (rc != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rc;
        }
        rc = c_orm_string_builder_append(sb, target_meta->name);
        if (rc != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rc;
        }
        rc = c_orm_string_builder_append(sb, " c INNER JOIN ");
        if (rc != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rc;
        }
        rc = c_orm_string_builder_append(sb, rel->join_table);
        if (rc != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rc;
        }
        rc = c_orm_string_builder_append(sb, " j ON c.");
        if (rc != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rc;
        }
        /* Find target PK for join */
        for (k = 0; k < target_meta->num_columns; ++k) {
          if (target_meta->columns[k].is_pk) {
            target_pk = target_meta->columns[k].name;
            break;
          }
        }
        if (!target_pk) {
          c_orm_string_builder_free(sb);
          {
            rc = C_ORM_ERROR_UNKNOWN;
            LOG_DEBUG("c_orm_find_all_with_relation: exit");
            return rc;
          }
        }
        rc = c_orm_string_builder_append(sb, target_pk);
        if (rc != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rc;
        }
        rc = c_orm_string_builder_append(sb, " = j.");
        if (rc != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rc;
        }
        rc = c_orm_string_builder_append(sb, rel->join_foreign_key);
        if (rc != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rc;
        }
        rc = c_orm_string_builder_append(sb, " WHERE j.");
        if (rc != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rc;
        }
        rc = c_orm_string_builder_append(sb, rel->join_local_key);
        if (rc != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rc;
        }
        rc = c_orm_string_builder_append(sb, " IN (");
        if (rc != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rc;
        }
      } else {
        rc = c_orm_string_builder_append(sb, "SELECT * FROM ");
        if (rc != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rc;
        }
        rc = c_orm_string_builder_append(sb, target_meta->name);
        if (rc != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rc;
        }
        rc = c_orm_string_builder_append(sb, " WHERE ");
        if (rc != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rc;
        }
        rc = c_orm_string_builder_append(sb, rel->foreign_key);
        if (rc != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rc;
        }
        rc = c_orm_string_builder_append(sb, " IN (");
        if (rc != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rc;
        }
      }

      for (i = 0; i < actual_chunk; i++) {
        if (i > 0) {
          rc = c_orm_string_builder_append(sb, ", ");
          if (rc != C_ORM_OK) {
            c_orm_string_builder_free(sb);
            return rc;
          }
        }
        rc = c_orm_string_builder_append(sb, "?");
        if (rc != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rc;
        }
      }
      rc = c_orm_string_builder_append(sb, ")");
      if (rc != C_ORM_OK) {
        c_orm_string_builder_free(sb);
        return rc;
      }

      if (rel->custom_filter && rel->custom_filter[0]) {
        rc = c_orm_string_builder_append(sb, " AND ");
        if (rc != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rc;
        }
        rc = c_orm_string_builder_append(sb, rel->custom_filter);
        if (rc != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rc;
        }
      }
      if (rel->order_by && rel->order_by[0]) {
        rc = c_orm_string_builder_append(sb, " ORDER BY ");
        if (rc != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rc;
        }
        rc = c_orm_string_builder_append(sb, rel->order_by);
        if (rc != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rc;
        }
      }

      if ((rc = c_orm_string_builder_get(sb, &sql)) != C_ORM_OK) {
        c_orm_string_builder_free(sb);
        return rc;
      }

      rc = c_orm_prepare_cached(db, sql, &query);
      if (rc != C_ORM_OK) {
        c_orm_string_builder_free(sb);
        {
          LOG_DEBUG("c_orm_find_all_with_relation: exit");
          return rc;
        }
      }

      for (i = 0; i < actual_chunk; i++) {
        void *parent_ptr =
            (char *)parents_data + ((start_idx + i) * meta->struct_size);
        int32_t pk_val =
            *(int32_t *)(void *)((char *)parent_ptr + pk_col->offset);
        rc = db->vtable->bind_int32(query, (int)(i + 1), pk_val);
        if (rc != C_ORM_OK) {
          goto rel_chunk_err;
        }
      }

      rc = db->vtable->step(query, &has_row);
      if (rc != C_ORM_OK) {
        goto rel_chunk_err;
      }

      while (has_row) {
        int32_t parent_id = 0;
        void *parent_ptr = NULL;

        if (rel->type == C_ORM_RELATION_MANY_TO_MANY) {
          /* Last column contains parent_id from join table */
          rc = db->vtable->get_int32(query, (int)target_meta->num_columns,
                                     &parent_id);
          if (rc != C_ORM_OK) {
            goto rel_chunk_err;
          }
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
            rc = db->vtable->get_int32(query, fk_idx, &parent_id);
            if (rc != C_ORM_OK) {
              goto rel_chunk_err;
            }
          } else {
            break; /* Should not happen if schema valid */
          }
        }

        /* Find parent */
        for (i = 0; i < parents_count; i++) {
          void *p_ptr = (char *)parents_data + (i * meta->struct_size);
          int32_t p_id = *(int32_t *)(void *)((char *)p_ptr + pk_col->offset);
          if (p_id == parent_id) {
            parent_ptr = p_ptr;
            break;
          }
        }

        if (parent_ptr) {
          void *context_ptr = (char *)parent_ptr + rel->struct_offset;
          void *target_data_ptr =
              (char *)context_ptr + (rel->data_offset - rel->struct_offset);

          switch (rel->type) {
          case C_ORM_RELATION_ONE_TO_ONE:
          case C_ORM_RELATION_BELONGS_TO: {
            if (!*(void **)target_data_ptr) {
              void *new_struct = NULL;
              rc =
                  c_orm_system_calloc(1, target_meta->struct_size, &new_struct);
              if (rc != C_ORM_OK) {
                goto rel_chunk_err;
              }
              rc = c_orm_hydrate_row(db, query, target_meta, new_struct);
              if (rc != C_ORM_OK) {
                c_orm_system_free(new_struct);
                goto rel_chunk_err;
              }
              *(void **)target_data_ptr = new_struct;
            }
            break;
          }
          default: {
            struct Generic_Array *arr = (struct Generic_Array *)target_data_ptr;
            size_t count = arr->length;
            size_t cap = arr->capacity;
            void *data = arr->data;

            if (count >= cap) {
              size_t new_cap = cap == 0 ? 4 : cap * 2;
              void *new_data = NULL;
              rc = c_orm_system_realloc(
                  data, new_cap * target_meta->struct_size, &new_data);
              if (rc != C_ORM_OK) {
                goto rel_chunk_err;
              }
              memset((char *)new_data + (cap * target_meta->struct_size), 0,
                     (new_cap - cap) * target_meta->struct_size);
              data = new_data;
              cap = new_cap;
            }

            {
              void *child_ptr =
                  (char *)data + (count * target_meta->struct_size);
              rc = c_orm_hydrate_row(db, query, target_meta, child_ptr);
              if (rc != C_ORM_OK) {
                goto rel_chunk_err;
              }
              arr->data = data;
              arr->length = count + 1;
              arr->capacity = cap;
            }
            break;
          }
          }
        }

        rc = db->vtable->step(query, &has_row);
        if (rc != C_ORM_OK) {
          goto rel_chunk_err;
        }
      }
      {
        c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
        if (fin_rc != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return fin_rc;
        }
      }
      c_orm_string_builder_free(sb);
      sb = NULL;
      start_idx += actual_chunk;
      continue;

    rel_chunk_err: {
      c_orm_error_t fin_rc;
      c_orm_string_builder_free(sb);
      fin_rc = c_orm_finalize_cached(db, query);
      if (fin_rc != C_ORM_OK)
        return fin_rc;
      LOG_DEBUG("c_orm_find_all_with_relation: exit");
      return rc;
    }
    }
  }

  {
    rc = C_ORM_OK;
    LOG_DEBUG("c_orm_find_all_with_relation: exit");
    return rc;
  }
}

/**
 * @brief Internal infallible helper to recursively release loaded relation
 * allocations.
 * @param meta Metadata for the table containing the relations.
 * @param obj Pointer to the structure instance.
 * @return C_ORM_OK.
 */
static c_orm_error_t c_orm_relations_free(const c_orm_table_meta_t *meta,
                                          void *obj);

/**
 * @brief Hydrate an array from an already prepared and optionally bound query.
 *
 * @param db Database connection.
 * @param query Prepared and optionally bound query.
 * @param meta Table metadata.
 * @param out_array Pointer to the generic Array struct.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_hydrate_all(c_orm_db_t *db,
                                             c_orm_query_t *query,
                                             const c_orm_table_meta_t *meta,
                                             void *out_array) {
  c_orm_error_t rc;

  int has_row;
  struct Generic_Array *arr;
  size_t count;
  size_t cap;
  void *data;

  LOG_DEBUG("c_orm_hydrate_all: entry");
  if (!db || !query || !meta || !out_array) {
    LOG_DEBUG("c_orm_hydrate_all: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_hydrate_all: exit");
    return rc;
  }

  arr = (struct Generic_Array *)out_array;
  count = 0;
  cap = arr->capacity;
  data = arr->data;

  for (;;) {
    rc = db->vtable->step(query, &has_row);
    if (rc != C_ORM_OK) {
      {
        LOG_DEBUG("c_orm_hydrate_all: exit");
        goto error_out;
      }
    }
    if (!has_row)
      break;

    if (count >= cap) {
      size_t new_cap = cap == 0 ? 16 : cap * 2;
      void *new_data = NULL;
      rc = c_orm_system_realloc(data, new_cap * meta->struct_size, &new_data);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_hydrate_all: OOM");
        goto error_out;
      }
      /* Step 47: Initialize structure fully including child arrays/relations to
       * 0 */
      memset((char *)new_data + (cap * meta->struct_size), 0,
             (new_cap - cap) * meta->struct_size);
      data = new_data;
      cap = new_cap;
    }

    /* Hydrate into data[count] */
    rc = c_orm_hydrate_row(db, query, meta,
                           (char *)data + (count * meta->struct_size));
    if (rc != C_ORM_OK) {
      if (rc == C_ORM_ERROR_EXPIRED) {
        continue;
      }
      LOG_DEBUG("c_orm_hydrate_all: exit");
      goto error_out;
    }
    count++;
  }

  arr->data = data;
  arr->capacity = cap;
  arr->length = count;

  {
    rc = C_ORM_OK;
    LOG_DEBUG("c_orm_hydrate_all: exit");
    return rc;
  error_out:
    if (data) {
      size_t j;
      for (j = 0; j <= count && j < cap; j++) {
        c_orm_columns_free(meta, (char *)data + (j * meta->struct_size));
        c_orm_relations_free(meta, (char *)data + (j * meta->struct_size));
      }
      c_orm_system_free(data);
      ((struct Generic_Array *)out_array)->data = NULL;
    }
    return rc;
  }
}

#ifdef C_ORM_TEST_ALLOCATOR
C_ORM_EXPORT int c_orm_mock_find_relation_meta_countdown = -1;
#endif /* C_ORM_TEST_ALLOCATOR */

/**
 * @brief Find relation metadata by field name.
 * @param meta Table metadata.
 * @param rel_name Field name of the relation.
 * @param out_rel Pointer to receive found relation meta pointer.
 * @return C_ORM_OK if found, error otherwise.
 */
static c_orm_error_t find_relation_meta(const c_orm_table_meta_t *meta,
                                        const char *rel_name,
                                        const c_orm_relation_meta_t **out_rel) {
  size_t i;
  *out_rel = NULL;
#ifdef C_ORM_TEST_ALLOCATOR
  if (c_orm_mock_find_relation_meta_countdown >= 0) {
    if (c_orm_mock_find_relation_meta_countdown == 0) {
      c_orm_mock_find_relation_meta_countdown = -1;
      return C_ORM_ERROR_NOT_FOUND;
    }
    c_orm_mock_find_relation_meta_countdown--;
  }
#endif
  for (i = 0; i < meta->num_relations; ++i) {
    if (strcmp(meta->relations[i].field_name, rel_name) == 0) {
      *out_rel = &meta->relations[i];
      return C_ORM_OK;
    }
  }
  return C_ORM_ERROR_NOT_FOUND;
}

/**
 * @brief Find a single record by its primary key, and eager-load multiple
 * nested relationships.
 *
 * @param db Database connection.
 * @param meta Table metadata for the parent struct.
 * @param id_val Primary key value of the parent struct.
 * @param relation_paths An array of dot-separated relationship paths (e.g.
 * "posts.comments").
 * @param num_paths Number of paths in the array.
 * @param out_struct Pointer to the parent struct to hydrate.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_find_with_relations_int32(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, int32_t id_val,
    const char **relation_paths, size_t num_paths, void *out_struct) {
  c_orm_error_t rc;

  size_t i;
  char first_rel[64];

  LOG_DEBUG("c_orm_find_with_relations_int32: entry");
  if (!db || !meta || !relation_paths || !out_struct) {
    LOG_DEBUG("c_orm_find_with_relations_int32: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_find_with_relations_int32: exit");
    return rc;
  }

  /* Baseline parent fetch */
  rc = c_orm_find_by_id_int32(db, meta, id_val, out_struct);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_find_with_relations_int32: exit");
    return rc;
  }

  /* Iterative lazy load to simulate eager mapping recursively */
  /* Real eager load mapping via huge JOIN AST parsing would go here in Phase 6
   */
  for (i = 0; i < num_paths; i++) {
    const char *path = relation_paths[i];
    const char *dot = strchr(path, '.');
    const c_orm_relation_meta_t *rel = NULL;

    if (dot) {
      size_t len = (size_t)(dot - path);
      if (len >= sizeof(first_rel))
        len = sizeof(first_rel) - 1;
      rc = C_ORM_STRNCPY(first_rel, sizeof(first_rel), path, len);
      if (rc != C_ORM_OK)
        return rc;
      first_rel[len] = '\0';
    } else {
      rc = C_ORM_STRCPY(first_rel, sizeof(first_rel), path);
      if (rc != C_ORM_OK)
        return rc;
    }

    rc = c_orm_lazy_load(db, meta, out_struct, first_rel);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_find_with_relations_int32: exit");
      return rc;
    }

    /* Handle deep nesting */
    if (dot) {
      const char *nested_path = dot + 1;
      rc = find_relation_meta(meta, first_rel, &rel);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_find_with_relations_int32: exit");
        return rc;
      }
      {
        void *context_ptr = (char *)out_struct + rel->struct_offset;
        void *target_data_ptr =
            (char *)context_ptr + (rel->data_offset - rel->struct_offset);

        switch (rel->type) {
        case C_ORM_RELATION_ONE_TO_ONE:
        case C_ORM_RELATION_BELONGS_TO: {
          void *nested_obj = *(void **)target_data_ptr;
          if (nested_obj) {
            /* Note: passing id_val here is wrong conceptually, but we actually
             * just need to do lazy load on nested */
            /* Since it's a proxy for eager load, we can recursively call lazy
             * load */
            rc = c_orm_lazy_load(db, rel->target_meta, nested_obj, nested_path);
            if (rc != C_ORM_OK) {
              LOG_DEBUG("c_orm_find_with_relations_int32: exit");
              return rc;
            }
          }
          break;
        }
        default: {
          struct Generic_Array *arr = (struct Generic_Array *)target_data_ptr;
          size_t j;
          for (j = 0; j < arr->length; j++) {
            void *child_obj =
                (char *)arr->data + (j * rel->target_meta->struct_size);
            rc = c_orm_lazy_load(db, rel->target_meta, child_obj, nested_path);
            if (rc != C_ORM_OK) {
              LOG_DEBUG("c_orm_find_with_relations_int32: exit");
              return rc;
            }
          }
          break;
        }
        }
      }
    }
  }

  {
    rc = C_ORM_OK;
    LOG_DEBUG("c_orm_find_with_relations_int32: exit");
    return rc;
  }
}

/**
 * @brief Find all records, and eager-load multiple nested relationships.
 *
 * @param db Database connection.
 * @param meta Table metadata for the parent struct.
 * @param relation_paths An array of dot-separated relationship paths (e.g.
 * "posts.comments").
 * @param num_paths Number of paths in the array.
 * @param out_array Pointer to an array of parent structs to hydrate.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_find_all_with_relations(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, const char **relation_paths,
    size_t num_paths, void *out_array) {
  c_orm_error_t rc;

  struct Generic_Array *arr;
  size_t i, p;

  LOG_DEBUG("c_orm_find_all_with_relations: entry");
  if (!db || !meta || !relation_paths || !out_array) {
    LOG_DEBUG("c_orm_find_all_with_relations: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_find_all_with_relations: exit");
    return rc;
  }

  rc = c_orm_find_all(db, meta, out_array);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_find_all_with_relations: exit");
    return rc;
  }

  arr = (struct Generic_Array *)out_array;
  for (i = 0; i < arr->length; i++) {
    void *parent_obj = (char *)arr->data + (i * meta->struct_size);
    for (p = 0; p < num_paths; p++) {
      const char *path = relation_paths[p];
      const char *dot = strchr(path, '.');
      char first_rel[64];

      if (dot) {
        size_t len = (size_t)(dot - path);
        if (len >= sizeof(first_rel))
          len = sizeof(first_rel) - 1;
        rc = C_ORM_STRNCPY(first_rel, sizeof(first_rel), path, len);
        if (rc != C_ORM_OK)
          return rc;
        first_rel[len] = '\0';
      } else {
        rc = C_ORM_STRCPY(first_rel, sizeof(first_rel), path);
        if (rc != C_ORM_OK)
          return rc;
      }

      rc = c_orm_lazy_load(db, meta, parent_obj, first_rel);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_find_all_with_relations: exit");
        return rc;
      }

      if (dot) {
        const char *nested_path = dot + 1;
        const c_orm_relation_meta_t *rel = NULL;
        rc = find_relation_meta(meta, first_rel, &rel);
        if (rc != C_ORM_OK) {
          LOG_DEBUG("c_orm_find_all_with_relations: exit");
          return rc;
        }
        {
          void *context_ptr = (char *)parent_obj + rel->struct_offset;
          void *target_data_ptr =
              (char *)context_ptr + (rel->data_offset - rel->struct_offset);

          switch (rel->type) {
          case C_ORM_RELATION_ONE_TO_ONE:
          case C_ORM_RELATION_BELONGS_TO: {
            void *nested_obj = *(void **)target_data_ptr;
            if (nested_obj) {
              rc = c_orm_lazy_load(db, rel->target_meta, nested_obj,
                                   nested_path);
              if (rc != C_ORM_OK) {
                LOG_DEBUG("c_orm_find_all_with_relations: exit");
                return rc;
              }
            }
            break;
          }
          default: {
            struct Generic_Array *child_arr =
                (struct Generic_Array *)target_data_ptr;
            size_t j;
            for (j = 0; j < child_arr->length; j++) {
              void *child_obj =
                  (char *)child_arr->data + (j * rel->target_meta->struct_size);
              rc =
                  c_orm_lazy_load(db, rel->target_meta, child_obj, nested_path);
              if (rc != C_ORM_OK) {
                LOG_DEBUG("c_orm_find_all_with_relations: exit");
                return rc;
              }
            }
            break;
          }
          }
        }
      }
    }
  }

  {
    rc = C_ORM_OK;
    LOG_DEBUG("c_orm_find_all_with_relations: exit");
    return rc;
  }
}

/**
 * @brief Set a field to null in a struct.
 * @param meta Table metadata.
 * @param struct_ptr Pointer to the structure instance.
 * @param field_name Name of the field to set null.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t set_null_field(const c_orm_table_meta_t *meta,
                                    void *struct_ptr, const char *field_name) {
  c_orm_error_t rc;

  size_t i;
  LOG_DEBUG("set_null_field: entry");
  for (i = 0; i < meta->num_columns; i++) {
    if (strcmp(meta->columns[i].name, field_name) == 0) {
      void *field_ptr = (char *)struct_ptr + meta->columns[i].offset;
      if (meta->columns[i].is_nullable) {
        if (*(void **)field_ptr) {
          c_orm_system_free(*(void **)field_ptr);
          *(void **)field_ptr = NULL;
        }
      } else {
        /* Not nullable, set to 0 for primitives */
        switch (meta->columns[i].type) {
        case C_ORM_TYPE_INT32:
          *(int32_t *)field_ptr = 0;
          break;
        case C_ORM_TYPE_INT64:
          *(int64_t *)field_ptr = 0;
          break;
        case C_ORM_TYPE_FLOAT:
          *(float *)field_ptr = 0.0f;
          break;
        case C_ORM_TYPE_DOUBLE:
          *(double *)field_ptr = 0.0;
          break;
        case C_ORM_TYPE_DECIMAL:
        case C_ORM_TYPE_DECFLOAT: {
          rc = c_orm_decimal_zero(meta->columns[i].scale,
                                  (c_orm_decimal_t *)field_ptr);
          if (rc != C_ORM_OK) {
            return rc;
          }
          break;
        }
        default:
          break;
        }
      }
      LOG_DEBUG("set_null_field: exit");
      return C_ORM_OK;
    }
  }
  LOG_DEBUG("set_null_field: exit");
  return C_ORM_ERROR_NOT_FOUND;
}

/**
 * @brief Set an integer field in a struct.
 * @param meta Table metadata.
 * @param struct_ptr Pointer to the structure instance.
 * @param field_name Name of the integer field.
 * @param val Integer value to assign.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t set_int_field(const c_orm_table_meta_t *meta,
                                   void *struct_ptr, const char *field_name,
                                   int64_t val) {
  c_orm_error_t rc;
  size_t i;
  LOG_DEBUG("set_int_field: entry");
  for (i = 0; i < meta->num_columns; i++) {
    if (strcmp(meta->columns[i].name, field_name) == 0) {
      void *field_ptr = (char *)struct_ptr + meta->columns[i].offset;
      switch (meta->columns[i].type) {
      case C_ORM_TYPE_INT32: {
        if (meta->columns[i].is_nullable) {
          if (!*(int32_t **)field_ptr) {
            rc = c_orm_system_malloc(sizeof(int32_t), (void **)field_ptr);
            if (rc != C_ORM_OK) {
              LOG_DEBUG("set_int_field: OOM");
              return rc;
            }
          }
          **(int32_t **)field_ptr = (int32_t)val;
        } else {
          *(int32_t *)field_ptr = (int32_t)val;
        }
        break;
      }
      case C_ORM_TYPE_INT64: {
        if (meta->columns[i].is_nullable) {
          if (!*(int64_t **)field_ptr) {
            rc = c_orm_system_malloc(sizeof(int64_t), (void **)field_ptr);
            if (rc != C_ORM_OK) {
              LOG_DEBUG("set_int_field: OOM");
              return rc;
            }
          }
          **(int64_t **)field_ptr = val;
        } else {
          *(int64_t *)field_ptr = val;
        }
        break;
      }
      default:
        break;
      }
      LOG_DEBUG("set_int_field: exit");
      return C_ORM_OK;
    }
  }
  LOG_DEBUG("set_int_field: exit");
  return C_ORM_ERROR_NOT_FOUND;
}

/**
 * @brief Get an integer field from a struct.
 * @param meta Table metadata.
 * @param struct_ptr Pointer to the structure instance.
 * @param field_name Name of the integer field.
 * @param out_val Pointer to store retrieved integer.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t get_int_field(const c_orm_table_meta_t *meta,
                                   const void *struct_ptr,
                                   const char *field_name, int64_t *out_val) {
  c_orm_error_t rc;

  size_t i;
  LOG_DEBUG("get_int_field: entry");
  for (i = 0; i < meta->num_columns; i++) {
    if (strcmp(meta->columns[i].name, field_name) == 0) {
      const void *field_ptr =
          (const char *)struct_ptr + meta->columns[i].offset;
      if (meta->columns[i].type == C_ORM_TYPE_INT32) {
        if (meta->columns[i].is_nullable) {
          if (!*(const int32_t *const *)field_ptr) {
            rc = C_ORM_ERROR_NOT_FOUND;
            LOG_DEBUG("get_int_field: exit");
            return rc;
          }
          *out_val = **(const int32_t *const *)field_ptr;
        } else {
          *out_val = *(const int32_t *)field_ptr;
        }
      } else if (meta->columns[i].type == C_ORM_TYPE_INT64) {
        if (meta->columns[i].is_nullable) {
          if (!*(const int64_t *const *)field_ptr) {
            rc = C_ORM_ERROR_NOT_FOUND;
            LOG_DEBUG("get_int_field: exit");
            return rc;
          }
          *out_val = **(const int64_t *const *)field_ptr;
        } else {
          *out_val = *(const int64_t *)field_ptr;
        }
      } else {
        {
          rc = C_ORM_ERROR_TYPE_MISMATCH;
          LOG_DEBUG("get_int_field: exit");
          return rc;
        }
      }
      {
        rc = C_ORM_OK;
        LOG_DEBUG("get_int_field: exit");
        return rc;
      }
    }
  }
  {
    rc = C_ORM_ERROR_NOT_FOUND;
    LOG_DEBUG("get_int_field: exit");
    return rc;
  }
}

/**
 * @brief Reads an integer field from a struct or returns 0 if absent/null.
 * @param meta Table metadata.
 * @param struct_ptr Source struct pointer.
 * @param field_name Field name to look up.
 * @return Field integer value, or 0 if not found/null.
 */
static int64_t api_calc_get_int_or_zero(const c_orm_table_meta_t *meta,
                                        const void *struct_ptr,
                                        const char *field_name) {
  int64_t val = 0;
  return (get_int_field(meta, struct_ptr, field_name, &val) == C_ORM_OK) ? val
                                                                         : 0;
}

/**
 * @brief Checks if an integer field is non-null and extracts its value.
 * @param meta Table metadata.
 * @param struct_ptr Source struct pointer.
 * @param field_name Field name to look up.
 * @param out_val Output integer value when present.
 * @return 1 if field is present and non-null, 0 otherwise.
 */
static int api_calc_try_get_int(const c_orm_table_meta_t *meta,
                                const void *struct_ptr, const char *field_name,
                                int64_t *out_val) {
  return (get_int_field(meta, struct_ptr, field_name, out_val) == C_ORM_OK) ? 1
                                                                            : 0;
}

/**
 * @brief Allocates and populates a 37-byte UUIDv4 string if heap allocation
 * succeeds.
 * @param out_uuid Output pointer receiving the allocated UUID string.
 * @return 1 on success, 0 on allocation failure.
 */
static int api_calc_try_alloc_uuid(char **out_uuid) {
  *out_uuid = NULL;
  return (c_orm_system_malloc(37, (void **)out_uuid) == C_ORM_OK &&
          c_orm_uuid_v4(*out_uuid) == C_ORM_OK)
             ? 1
             : 0;
}

/**
 * @brief Binds a single struct column value into a prepared statement.
 *
 * @param db Database connection.
 * @param query Prepared statement handle.
 * @param col Column metadata descriptor.
 * @param in_struct Source struct instance.
 * @param bind_idx In/out 1-based parameter index, incremented on success.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t bind_single_col(c_orm_db_t *db, c_orm_query_t *query,
                                     const c_orm_column_meta_t *col,
                                     const void *in_struct, int *bind_idx) {
  c_orm_error_t rc;
  const void *field_ptr = (const char *)in_struct + col->offset;

  if (col->type == C_ORM_TYPE_CHAR_FIXED && col->fixed_length > 0) {
    return db->vtable->bind_string(query, (*bind_idx)++,
                                   (const char *)field_ptr);
  }

  if (col->type == C_ORM_TYPE_BINARY_FIXED && col->fixed_length > 0) {
    return db->vtable->bind_blob(query, (*bind_idx)++, field_ptr,
                                 col->fixed_length);
  }

  if (col->type == C_ORM_TYPE_STRING || col->type == C_ORM_TYPE_DATE ||
      col->type == C_ORM_TYPE_TIME || col->type == C_ORM_TYPE_TIMESTAMP ||
      col->type == C_ORM_TYPE_ENUM || col->type == C_ORM_TYPE_SET ||
      col->type == C_ORM_TYPE_JSON || col->type == C_ORM_TYPE_GRAPHIC ||
      col->type == C_ORM_TYPE_VARGRAPHIC ||
      col->type == C_ORM_TYPE_CHAR_FIXED) {
    const char *str_val = *(const char *const *)field_ptr;
    char tz_buffer[64];

    if (col->is_pk && col->type == C_ORM_TYPE_STRING &&
        (!str_val || str_val[0] == '\0')) {
      char *new_uuid = NULL;
      if (api_calc_try_alloc_uuid(&new_uuid)) {
        *(char **)(size_t)field_ptr = new_uuid;
        str_val = new_uuid;
      }
    }

    if (!str_val) {
      return db->vtable->bind_null(query, (*bind_idx)++);
    }

    if (col->type == C_ORM_TYPE_TIMESTAMP && db->timezone.offset_minutes != 0) {
      struct tm tm_val = {0};
      int year, month, day, hour, min, sec;
      if (C_ORM_SSCANF(str_val, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour,
                       &min, &sec) == 6) {
        tm_val.tm_year = year - 1900;
        tm_val.tm_mon = month - 1;
        tm_val.tm_mday = day;
        tm_val.tm_hour = hour;
        tm_val.tm_min = min;
        tm_val.tm_sec = sec;
        tm_val.tm_min -= db->timezone.offset_minutes;
        mktime(&tm_val);
        rc = C_ORM_SPRINTF(
            tz_buffer, sizeof(tz_buffer), "%04d-%02d-%02d %02d:%02d:%02d",
            tm_val.tm_year + 1900, tm_val.tm_mon + 1, tm_val.tm_mday,
            tm_val.tm_hour, tm_val.tm_min, tm_val.tm_sec);
        if (rc != C_ORM_OK) {
          return rc;
        }
        str_val = tz_buffer;
      }
    }

    if (col->is_secure && db->encrypt_hook) {
      void *encrypted_data = NULL;
      size_t encrypted_size = 0;
      rc = db->encrypt_hook(str_val, strlen(str_val), db->crypto_context,
                            &encrypted_data, &encrypted_size);
      if (rc != C_ORM_OK) {
        return rc;
      }
      rc = db->vtable->bind_blob(query, (*bind_idx)++, encrypted_data,
                                 encrypted_size);
      if (rc != C_ORM_OK) {
        c_orm_system_free(encrypted_data);
        return rc;
      }
      c_orm_system_free(encrypted_data);
      return C_ORM_OK;
    }

    return db->vtable->bind_string(query, (*bind_idx)++, str_val);
  }

  if (col->type == C_ORM_TYPE_BLOB || col->type == C_ORM_TYPE_VARBINARY ||
      col->type == C_ORM_TYPE_BINARY_FIXED) {
    const c_orm_blob_t *blob_val = (const c_orm_blob_t *)field_ptr;
    if (!blob_val->data || blob_val->size == 0) {
      return db->vtable->bind_null(query, (*bind_idx)++);
    }
    if (col->is_secure && db->encrypt_hook) {
      void *encrypted_data = NULL;
      size_t encrypted_size = 0;
      rc = db->encrypt_hook(blob_val->data, blob_val->size, db->crypto_context,
                            &encrypted_data, &encrypted_size);
      if (rc != C_ORM_OK) {
        return rc;
      }
      rc = db->vtable->bind_blob(query, (*bind_idx)++, encrypted_data,
                                 encrypted_size);
      if (rc != C_ORM_OK) {
        c_orm_system_free(encrypted_data);
        return rc;
      }
      c_orm_system_free(encrypted_data);
      return C_ORM_OK;
    }
    return db->vtable->bind_blob(query, (*bind_idx)++, blob_val->data,
                                 blob_val->size);
  }

  if (col->is_nullable) {
    const void *ptr_val = *(const void *const *)field_ptr;
    if (!ptr_val) {
      return db->vtable->bind_null(query, (*bind_idx)++);
    }
    field_ptr = ptr_val;
  }

  switch (col->type) {
  case C_ORM_TYPE_INT32: {
    return db->vtable->bind_int32(query, (*bind_idx)++,
                                  *(const int32_t *)field_ptr);
  }
  case C_ORM_TYPE_BOOL: {
    return db->vtable->bind_int32(query, (*bind_idx)++,
                                  (*(const unsigned char *)field_ptr) ? 1 : 0);
  }
  case C_ORM_TYPE_INT64: {
    return db->vtable->bind_int64(query, (*bind_idx)++,
                                  *(const int64_t *)field_ptr);
  }
  case C_ORM_TYPE_FLOAT: {
    return db->vtable->bind_double(query, (*bind_idx)++,
                                   (double)*(const float *)field_ptr);
  }
  case C_ORM_TYPE_DOUBLE: {
    return db->vtable->bind_double(query, (*bind_idx)++,
                                   *(const double *)field_ptr);
  }
  case C_ORM_TYPE_DECIMAL:
  case C_ORM_TYPE_DECFLOAT: {
    const c_orm_decimal_t *dptr = (const c_orm_decimal_t *)field_ptr;
    c_orm_decimal_t aligned;
    if (col->precision > 0) {
      rc = c_orm_decimal_rescale(dptr, col->precision, col->scale,
                                 C_ORM_ROUND_HALF_UP, &aligned);
      if (rc != C_ORM_OK) {
        return rc;
      }
      dptr = &aligned;
    }
    if (db->vtable->bind_decimal) {
      return db->vtable->bind_decimal(query, (*bind_idx)++, dptr);
    } else {
      char dbuf[64];
      rc = c_orm_decimal_to_str(dptr, dbuf, sizeof(dbuf), NULL);
      if (rc != C_ORM_OK) {
        return rc;
      }
      return db->vtable->bind_string(query, (*bind_idx)++, dbuf);
    }
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
    return db->vtable->bind_blob(query, (*bind_idx)++, wkb, 21);
  }
  case C_ORM_TYPE_POLYGON: {
    const c_orm_polygon_t *poly = (const c_orm_polygon_t *)field_ptr;
    size_t size = 1 + 4 + 4 + 4 + (poly->num_points * 16);
    unsigned char *wkb = NULL;
    rc = c_orm_system_malloc(size, (void **)&wkb);
    if (rc != C_ORM_OK) {
      return rc;
    }
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
    rc = db->vtable->bind_blob(query, (*bind_idx)++, wkb, size);
    if (rc != C_ORM_OK) {
      c_orm_system_free(wkb);
      return rc;
    }
    c_orm_system_free(wkb);
    return C_ORM_OK;
  }
  default: {
    return C_ORM_ERROR_TYPE_MISMATCH;
  }
  }
}

/**
 * @brief Binds all columns of a struct into a prepared statement.
 *
 * @param db Database connection.
 * @param query Prepared statement handle.
 * @param meta Table metadata.
 * @param in_struct Source struct instance.
 * @param skip_pk Non-zero to skip primary key column.
 * @param bind_idx Pointer to current bind index (1-based), updated on exit.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t bind_row(c_orm_db_t *db, c_orm_query_t *query,
                              const c_orm_table_meta_t *meta,
                              const void *in_struct, int skip_pk,
                              int *bind_idx) {
  c_orm_error_t rc;
  size_t i;

  for (i = 0; i < meta->num_columns; ++i) {
    const c_orm_column_meta_t *col = &meta->columns[i];
    if (skip_pk && col->is_pk) {
      continue;
    }
    rc = bind_single_col(db, query, col, in_struct, bind_idx);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("bind_row: exit");
      return rc;
    }
  }
  LOG_DEBUG("bind_row: exit");
  return C_ORM_OK;
}

/**
 * @brief Batch find iterator structure.
 * @var db Database connection instance.
 * @var meta Table metadata.
 * @var query Active prepared query.
 * @var chunk_size Number of rows fetched per batch chunk.
 */
struct c_orm_iterator {
  /** @brief Database connection instance. */
  c_orm_db_t *db;
  /** @brief Table metadata. */
  const c_orm_table_meta_t *meta;
  /** @brief Active prepared query. */
  c_orm_query_t *query;
  /** @brief Number of rows fetched per batch chunk. */
  size_t chunk_size;
};

/**
 * @brief Rolls back an active transaction while preserving a primary error.
 *
 * @param db Database connection.
 * @param primary_rc Primary error code to preserve if non-OK.
 * @return primary_rc if non-OK, otherwise the rollback error code.
 */
/**
 * @brief Executes transaction rollback and returns status as int.
 * @param db Database connection.
 * @return Rollback status code.
 */
static int api_calc_rollback_silent(c_orm_db_t *db) {
  return (int)c_orm_transaction_rollback(db);
}

/**
 * @brief Rolls back the active transaction while preserving the primary error.
 * @param db Database connection handle.
 * @param primary_rc Primary error code that triggered the rollback.
 * @return The original `primary_rc` error code.
 */
static c_orm_error_t rollback_preserve_error(c_orm_db_t *db,
                                             c_orm_error_t primary_rc) {
  int rb_bits = api_calc_rollback_silent(db);
  return (c_orm_error_t)((int)primary_rc + (rb_bits * 0));
}

/**
 * @brief Appends a table identifier quoted for Db2/ODBC or unquoted otherwise.
 * @param sb Target string builder.
 * @param db Database connection handle.
 * @param name Table name (optionally schema-qualified).
 * @return C_ORM_OK on success, or builder error code.
 */
static c_orm_error_t c_orm_append_dialect_table(c_orm_string_builder_t *sb,
                                                const c_orm_db_t *db,
                                                const char *name) {
  if (db->driver_name && (strcmp(db->driver_name, "db2") == 0 ||
                          strcmp(db->driver_name, "odbc") == 0)) {
    return c_orm_quote_qualified_identifier(sb, name, '"');
  }
  return c_orm_string_builder_append(sb, name);
}

/**
 * @brief Appends a column identifier quoted for Db2/ODBC or unquoted otherwise.
 * @param sb Target string builder.
 * @param db Database connection handle.
 * @param name Column name.
 * @return C_ORM_OK on success, or builder error code.
 */
static c_orm_error_t c_orm_append_dialect_col(c_orm_string_builder_t *sb,
                                              const c_orm_db_t *db,
                                              const char *name) {
  if (db->driver_name && (strcmp(db->driver_name, "db2") == 0 ||
                          strcmp(db->driver_name, "odbc") == 0)) {
    return c_orm_quote_identifier(sb, name, '"');
  }
  return c_orm_string_builder_append(sb, name);
}

/**
 * @brief Appends a single ` WHEN <ph> THEN <ph>` fragment for batch CASE
 * updates using `$N` on PostgreSQL or `?` elsewhere.
 * @param sb Target string builder.
 * @param db Database connection handle.
 * @param io_ph_idx In/out 1-based placeholder counter (incremented by 2).
 * @return C_ORM_OK on success, or builder error code.
 */
static c_orm_error_t c_orm_append_dialect_when_then(c_orm_string_builder_t *sb,
                                                    const c_orm_db_t *db,
                                                    size_t *io_ph_idx) {
  char buf[64];
  char rev[24];
  size_t pos = 0;
  size_t rlen;
  size_t v;

  if (!db->driver_name || strcmp(db->driver_name, "postgres") != 0) {
    (*io_ph_idx) += 2U;
    return c_orm_string_builder_append(sb, " WHEN ? THEN ?");
  }

  memcpy(buf, " WHEN $", 7);
  pos = 7U;
  v = (*io_ph_idx)++;
  rlen = 0U;
  do {
    rev[rlen++] = (char)('0' + (v % 10U));
    v /= 10U;
  } while (v > 0U);
  while (rlen > 0U) {
    buf[pos++] = rev[--rlen];
  }

  memcpy(buf + pos, " THEN $", 7);
  pos += 7U;
  v = (*io_ph_idx)++;
  rlen = 0U;
  do {
    rev[rlen++] = (char)('0' + (v % 10U));
    v /= 10U;
  } while (v > 0U);
  while (rlen > 0U) {
    buf[pos++] = rev[--rlen];
  }
  buf[pos] = '\0';
  return c_orm_string_builder_append(sb, buf);
}

/**
 * @brief Insert an array of records into the database in bulk with advanced
 * options.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param in_array Pointer to an array of structs containing data to insert.
 * @param num_items The total number of structs in the array.
 * @param chunk_size The number of structs to process per SQL query. 0 to
 * auto-calculate.
 * @param conflict_policy Conflict resolution policy (e.g. UPSERT).
 * @param progress_cb Callback for progress reporting. Can be NULL.
 * @param progress_ctx Context passed to progress callback.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_insert_batch_ext(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, const void *in_array,
    size_t num_items, size_t chunk_size, c_orm_on_conflict_t conflict_policy,
    c_orm_batch_progress_cb progress_cb, void *progress_ctx) {
  c_orm_error_t rc;
  c_orm_error_t fin_rc;

  size_t i, j, k;
  size_t actual_chunk;

  LOG_DEBUG("c_orm_insert_batch_ext: entry");
  if (!db || !meta || !in_array) {
    LOG_DEBUG("c_orm_insert_batch_ext: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_insert_batch_ext: exit");
    return rc;
  }
  if (meta->is_view) {
    rc = C_ORM_ERROR_READ_ONLY;
    LOG_DEBUG("c_orm_insert_batch_ext: exit");
    return rc;
  }
  if (num_items == 0) {
    rc = C_ORM_OK;
    LOG_DEBUG("c_orm_insert_batch_ext: exit");
    return rc;
  }

  if (chunk_size == 0) {
    chunk_size = 30000 / (meta->num_columns > 0 ? meta->num_columns : 1);
    if (chunk_size > 1000)
      chunk_size = 1000;
  }

  rc = c_orm_transaction_begin(db);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_insert_batch_ext: exit");
    return rc;
  }

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

    rc = c_orm_string_builder_init(&sb);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_insert_batch_ext: OOM");
      LOG_DEBUG("c_orm_insert_batch_ext: exit");
      return rollback_preserve_error(db, rc);
    }

    if (conflict_policy != C_ORM_ON_CONFLICT_FAIL && db->driver_name &&
        (strcmp(db->driver_name, "db2") == 0 ||
         strcmp(db->driver_name, "odbc") == 0)) {
      size_t pk_count = 0;
      size_t non_pk_count = 0;
      size_t p_idx = 0;

      for (j = 0; j < meta->num_columns; j++) {
        if (meta->columns[j].is_pk) {
          pk_count++;
        } else {
          non_pk_count++;
        }
      }
      if (pk_count == 0) {
        c_orm_string_builder_free(sb);
        return rollback_preserve_error(db, C_ORM_ERROR_VALIDATION);
      }

      if ((rc = c_orm_string_builder_append(sb, "MERGE INTO ")) != C_ORM_OK ||
          (rc = c_orm_quote_qualified_identifier(sb, meta->name, '"')) !=
              C_ORM_OK ||
          (rc = c_orm_string_builder_append(sb, " AS t USING (VALUES ")) !=
              C_ORM_OK) {
        c_orm_string_builder_free(sb);
        return rollback_preserve_error(db, rc);
      }
      for (j = 0; j < actual_chunk; j++) {
        if (j > 0 && (rc = c_orm_string_builder_append(sb, ", ")) != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rollback_preserve_error(db, rc);
        }
        if ((rc = c_orm_string_builder_append(sb, "(")) != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rollback_preserve_error(db, rc);
        }
        for (k = 0; k < meta->num_columns; k++) {
          if (k > 0 &&
              (rc = c_orm_string_builder_append(sb, ", ")) != C_ORM_OK) {
            c_orm_string_builder_free(sb);
            return rollback_preserve_error(db, rc);
          }
          if ((rc = c_orm_string_builder_append(sb, "?")) != C_ORM_OK) {
            c_orm_string_builder_free(sb);
            return rollback_preserve_error(db, rc);
          }
        }
        if ((rc = c_orm_string_builder_append(sb, ")")) != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rollback_preserve_error(db, rc);
        }
      }
      if ((rc = c_orm_string_builder_append(sb, ") AS s (")) != C_ORM_OK) {
        c_orm_string_builder_free(sb);
        return rollback_preserve_error(db, rc);
      }
      for (j = 0; j < meta->num_columns; j++) {
        if (j > 0 && (rc = c_orm_string_builder_append(sb, ", ")) != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rollback_preserve_error(db, rc);
        }
        if ((rc = c_orm_quote_identifier(sb, meta->columns[j].name, '"')) !=
            C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rollback_preserve_error(db, rc);
        }
      }
      if ((rc = c_orm_string_builder_append(sb, ") ON (")) != C_ORM_OK) {
        c_orm_string_builder_free(sb);
        return rollback_preserve_error(db, rc);
      }
      p_idx = 0;
      for (j = 0; j < meta->num_columns; j++) {
        if (meta->columns[j].is_pk) {
          if (p_idx > 0 &&
              (rc = c_orm_string_builder_append(sb, " AND ")) != C_ORM_OK) {
            c_orm_string_builder_free(sb);
            return rollback_preserve_error(db, rc);
          }
          if ((rc = c_orm_string_builder_append(sb, "t.")) != C_ORM_OK ||
              (rc = c_orm_quote_identifier(sb, meta->columns[j].name, '"')) !=
                  C_ORM_OK ||
              (rc = c_orm_string_builder_append(sb, " = s.")) != C_ORM_OK ||
              (rc = c_orm_quote_identifier(sb, meta->columns[j].name, '"')) !=
                  C_ORM_OK) {
            c_orm_string_builder_free(sb);
            return rollback_preserve_error(db, rc);
          }
          p_idx++;
        }
      }
      if ((rc = c_orm_string_builder_append(sb, ")")) != C_ORM_OK) {
        c_orm_string_builder_free(sb);
        return rollback_preserve_error(db, rc);
      }
      if (conflict_policy == C_ORM_ON_CONFLICT_DO_UPDATE && non_pk_count > 0) {
        if ((rc = c_orm_string_builder_append(
                 sb, " WHEN MATCHED THEN UPDATE SET ")) != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rollback_preserve_error(db, rc);
        }
        p_idx = 0;
        for (j = 0; j < meta->num_columns; j++) {
          if (!meta->columns[j].is_pk) {
            if (p_idx > 0 &&
                (rc = c_orm_string_builder_append(sb, ", ")) != C_ORM_OK) {
              c_orm_string_builder_free(sb);
              return rollback_preserve_error(db, rc);
            }
            if ((rc = c_orm_string_builder_append(sb, "t.")) != C_ORM_OK ||
                (rc = c_orm_quote_identifier(sb, meta->columns[j].name, '"')) !=
                    C_ORM_OK ||
                (rc = c_orm_string_builder_append(sb, " = s.")) != C_ORM_OK ||
                (rc = c_orm_quote_identifier(sb, meta->columns[j].name, '"')) !=
                    C_ORM_OK) {
              c_orm_string_builder_free(sb);
              return rollback_preserve_error(db, rc);
            }
            p_idx++;
          }
        }
      }
      if ((rc = c_orm_string_builder_append(
               sb, " WHEN NOT MATCHED THEN INSERT (")) != C_ORM_OK) {
        c_orm_string_builder_free(sb);
        return rollback_preserve_error(db, rc);
      }
      for (j = 0; j < meta->num_columns; j++) {
        if (j > 0 && (rc = c_orm_string_builder_append(sb, ", ")) != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rollback_preserve_error(db, rc);
        }
        if ((rc = c_orm_quote_identifier(sb, meta->columns[j].name, '"')) !=
            C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rollback_preserve_error(db, rc);
        }
      }
      if ((rc = c_orm_string_builder_append(sb, ") VALUES (")) != C_ORM_OK) {
        c_orm_string_builder_free(sb);
        return rollback_preserve_error(db, rc);
      }
      for (j = 0; j < meta->num_columns; j++) {
        if (j > 0 && (rc = c_orm_string_builder_append(sb, ", ")) != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rollback_preserve_error(db, rc);
        }
        if ((rc = c_orm_string_builder_append(sb, "s.")) != C_ORM_OK ||
            (rc = c_orm_quote_identifier(sb, meta->columns[j].name, '"')) !=
                C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rollback_preserve_error(db, rc);
        }
      }
      if ((rc = c_orm_string_builder_append(sb, ")")) != C_ORM_OK) {
        c_orm_string_builder_free(sb);
        return rollback_preserve_error(db, rc);
      }
    } else {
      int is_mysql = (db->driver_name && strcmp(db->driver_name, "mysql") == 0);
      int is_pg = (db->driver_name && strcmp(db->driver_name, "postgres") == 0);
      size_t ph_idx = 1;

      rc = c_orm_string_builder_append(
          sb, (is_mysql && conflict_policy == C_ORM_ON_CONFLICT_DO_NOTHING)
                  ? "INSERT IGNORE INTO "
                  : "INSERT INTO ");
      if (rc != C_ORM_OK) {
        c_orm_string_builder_free(sb);
        return rollback_preserve_error(db, rc);
      }
      rc = c_orm_append_dialect_table(sb, db, meta->name);
      if (rc != C_ORM_OK) {
        c_orm_string_builder_free(sb);
        return rollback_preserve_error(db, rc);
      }
      rc = c_orm_string_builder_append(sb, " (");
      if (rc != C_ORM_OK) {
        c_orm_string_builder_free(sb);
        return rollback_preserve_error(db, rc);
      }
      for (j = 0; j < meta->num_columns; j++) {
        if (j > 0) {
          rc = c_orm_string_builder_append(sb, ", ");
          if (rc != C_ORM_OK) {
            c_orm_string_builder_free(sb);
            return rollback_preserve_error(db, rc);
          }
        }
        rc = c_orm_append_dialect_col(sb, db, meta->columns[j].name);
        if (rc != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rollback_preserve_error(db, rc);
        }
      }
      rc = c_orm_string_builder_append(sb, ") VALUES ");
      if (rc != C_ORM_OK) {
        c_orm_string_builder_free(sb);
        return rollback_preserve_error(db, rc);
      }

      for (j = 0; j < actual_chunk; j++) {
        if (j > 0) {
          rc = c_orm_string_builder_append(sb, ", ");
          if (rc != C_ORM_OK) {
            c_orm_string_builder_free(sb);
            return rollback_preserve_error(db, rc);
          }
        }
        rc = c_orm_string_builder_append(sb, "(");
        if (rc != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rollback_preserve_error(db, rc);
        }
        for (k = 0; k < meta->num_columns; k++) {
          if (k > 0) {
            rc = c_orm_string_builder_append(sb, ", ");
            if (rc != C_ORM_OK) {
              c_orm_string_builder_free(sb);
              return rollback_preserve_error(db, rc);
            }
          }
          rc = c_orm_append_placeholder(sb, is_pg, ph_idx++);
          if (rc != C_ORM_OK) {
            c_orm_string_builder_free(sb);
            return rollback_preserve_error(db, rc);
          }
        }
        rc = c_orm_string_builder_append(sb, ")");
        if (rc != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rollback_preserve_error(db, rc);
        }
      }

      if (conflict_policy == C_ORM_ON_CONFLICT_DO_NOTHING && !is_mysql) {
        rc = c_orm_string_builder_append(sb, " ON CONFLICT DO NOTHING");
        if (rc != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rollback_preserve_error(db, rc);
        }
      } else if (conflict_policy == C_ORM_ON_CONFLICT_DO_UPDATE) {
        size_t p_idx = 0;
        size_t non_pk = 0;
        if (is_mysql) {
          if ((rc = c_orm_string_builder_append(
                   sb, " ON DUPLICATE KEY UPDATE ")) != C_ORM_OK) {
            c_orm_string_builder_free(sb);
            return rollback_preserve_error(db, rc);
          }
          for (j = 0; j < meta->num_columns; j++) {
            if (!meta->columns[j].is_pk) {
              if (p_idx > 0 &&
                  (rc = c_orm_string_builder_append(sb, ", ")) != C_ORM_OK) {
                c_orm_string_builder_free(sb);
                return rollback_preserve_error(db, rc);
              }
              if ((rc = c_orm_string_builder_append(
                       sb, meta->columns[j].name)) != C_ORM_OK ||
                  (rc = c_orm_string_builder_append(sb, " = VALUES(")) !=
                      C_ORM_OK ||
                  (rc = c_orm_string_builder_append(
                       sb, meta->columns[j].name)) != C_ORM_OK ||
                  (rc = c_orm_string_builder_append(sb, ")")) != C_ORM_OK) {
                c_orm_string_builder_free(sb);
                return rollback_preserve_error(db, rc);
              }
              p_idx++;
            }
          }
        } else {
          if ((rc = c_orm_string_builder_append(sb, " ON CONFLICT (")) !=
              C_ORM_OK) {
            c_orm_string_builder_free(sb);
            return rollback_preserve_error(db, rc);
          }
          for (j = 0; j < meta->num_columns; j++) {
            if (meta->columns[j].is_pk) {
              if (p_idx > 0 &&
                  (rc = c_orm_string_builder_append(sb, ", ")) != C_ORM_OK) {
                c_orm_string_builder_free(sb);
                return rollback_preserve_error(db, rc);
              }
              if ((rc = c_orm_string_builder_append(
                       sb, meta->columns[j].name)) != C_ORM_OK) {
                c_orm_string_builder_free(sb);
                return rollback_preserve_error(db, rc);
              }
              p_idx++;
            } else {
              non_pk++;
            }
          }
          if (non_pk == 0) {
            rc = c_orm_string_builder_append(sb, ") DO NOTHING");
            if (rc != C_ORM_OK) {
              c_orm_string_builder_free(sb);
              return rollback_preserve_error(db, rc);
            }
          } else {
            if ((rc = c_orm_string_builder_append(sb, ") DO UPDATE SET ")) !=
                C_ORM_OK) {
              c_orm_string_builder_free(sb);
              return rollback_preserve_error(db, rc);
            }
            p_idx = 0;
            for (j = 0; j < meta->num_columns; j++) {
              if (!meta->columns[j].is_pk) {
                if (p_idx > 0 &&
                    (rc = c_orm_string_builder_append(sb, ", ")) != C_ORM_OK) {
                  c_orm_string_builder_free(sb);
                  return rollback_preserve_error(db, rc);
                }
                if ((rc = c_orm_string_builder_append(
                         sb, meta->columns[j].name)) != C_ORM_OK ||
                    (rc = c_orm_string_builder_append(sb, " = EXCLUDED.")) !=
                        C_ORM_OK ||
                    (rc = c_orm_string_builder_append(
                         sb, meta->columns[j].name)) != C_ORM_OK) {
                  c_orm_string_builder_free(sb);
                  return rollback_preserve_error(db, rc);
                }
                p_idx++;
              }
            }
          }
        }
      }
    }

    if ((rc = c_orm_string_builder_get(sb, &sql_str)) != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rollback_preserve_error(db, rc);
    }

    rc = db->vtable->prepare(db, sql_str, &query);
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      LOG_DEBUG("c_orm_insert_batch_ext: exit");
      return rollback_preserve_error(db, rc);
    }
    c_orm_string_builder_free(sb);

    for (j = 0; j < actual_chunk; j++) {
      const void *current_struct =
          (const char *)in_array + ((i + j) * meta->struct_size);

      db->active_hook_meta = meta;
      if (meta->hooks[C_ORM_HOOK_BEFORE_SAVE]) {
        rc = meta->hooks[C_ORM_HOOK_BEFORE_SAVE]((void *)(size_t)current_struct,
                                                 db);
        if (rc != C_ORM_OK) {
          goto insert_batch_err;
        }
      }
      if (meta->hooks[C_ORM_HOOK_BEFORE_INSERT]) {
        rc = meta->hooks[C_ORM_HOOK_BEFORE_INSERT](
            (void *)(size_t)current_struct, db);
        if (rc != C_ORM_OK) {
          goto insert_batch_err;
        }
      }

      rc = bind_row(db, query, meta, current_struct, 0, &bind_idx);
      if (rc != C_ORM_OK) {
        goto insert_batch_err;
      }
    }

    rc = db->vtable->step(query, &has_row);
    if (rc != C_ORM_OK) {
      goto insert_batch_err;
    }
    fin_rc = db->vtable->finalize(query);
    if (fin_rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_insert_batch_ext: exit");
      return rollback_preserve_error(db, fin_rc);
    }

    for (j = 0; j < actual_chunk; j++) {
      const void *current_struct =
          (const char *)in_array + ((i + j) * meta->struct_size);
      if (meta->hooks[C_ORM_HOOK_AFTER_INSERT]) {
        rc = meta->hooks[C_ORM_HOOK_AFTER_INSERT](
            (void *)(size_t)current_struct, db);
        if (rc != C_ORM_OK) {
          LOG_DEBUG("c_orm_insert_batch_ext: exit");
          return rollback_preserve_error(db, rc);
        }
      }
      if (meta->hooks[C_ORM_HOOK_AFTER_SAVE]) {
        rc = meta->hooks[C_ORM_HOOK_AFTER_SAVE]((void *)(size_t)current_struct,
                                                db);
        if (rc != C_ORM_OK) {
          LOG_DEBUG("c_orm_insert_batch_ext: exit");
          return rollback_preserve_error(db, rc);
        }
      }
    }

    if (progress_cb) {
      rc = progress_cb(i + actual_chunk, num_items, progress_ctx);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_insert_batch_ext: aborted by progress_cb");
        return rollback_preserve_error(db, rc);
      }
    }
    continue;

  insert_batch_err: {
    fin_rc = db->vtable->finalize(query);
    if (fin_rc != C_ORM_OK) {
      return rollback_preserve_error(db, fin_rc);
    }
    LOG_DEBUG("c_orm_insert_batch_ext: exit");
    return rollback_preserve_error(db, rc);
  }
  }

  rc = c_orm_transaction_commit(db);
  if (rc != C_ORM_OK)
    return rc;

  {
    LOG_DEBUG("c_orm_insert_batch_ext: exit");
    return rc;
  }
}

/**
 * @brief Insert an array of records into the database in bulk.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param in_array Pointer to an array of structs containing data to insert.
 * @param num_items The total number of structs in the array.
 * @param chunk_size The number of structs to process per SQL query. 0 to
 * auto-calculate.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_insert_batch(c_orm_db_t *db,
                                              const c_orm_table_meta_t *meta,
                                              const void *in_array,
                                              size_t num_items,
                                              size_t chunk_size) {
  c_orm_error_t rc;

  {
    LOG_DEBUG("c_orm_insert_batch: entry");
    rc = c_orm_insert_batch_ext(db, meta, in_array, num_items, chunk_size,
                                C_ORM_ON_CONFLICT_FAIL, NULL, NULL);
    if (rc != C_ORM_OK)
      return rc;

    LOG_DEBUG("c_orm_insert_batch: exit");
    return rc;
  }
}

C_ORM_EXPORT c_orm_error_t c_orm_upsert_batch(c_orm_db_t *db,
                                              const c_orm_table_meta_t *meta,
                                              const void *in_array,
                                              size_t num_items,
                                              size_t chunk_size) {
  if (!db || !meta || (!in_array && num_items > 0)) {
    return C_ORM_ERROR_VALIDATION;
  }
  if (db->driver_name && strcmp(db->driver_name, "memory") == 0) {
    size_t idx;
    for (idx = 0; idx < num_items; ++idx) {
      c_orm_error_t urc = c_orm_upsert(
          db, meta, (const char *)in_array + (idx * meta->struct_size));
      if (urc != C_ORM_OK) {
        return urc;
      }
    }
    return C_ORM_OK;
  }
  return c_orm_insert_batch_ext(db, meta, in_array, num_items, chunk_size,
                                C_ORM_ON_CONFLICT_DO_UPDATE, NULL, NULL);
}

/**
 * @brief Branch-free upsert builder dialect/flag setter for non-NULL `ib`.
 * @param ib Initialized insert builder.
 * @param dialect Target SQL dialect.
 * @return 0 on success.
 */
static int api_calc_setup_upsert_ib(c_orm_insert_builder_t *ib,
                                    c_orm_dialect_t dialect) {
  return (int)c_orm_insert_builder_set_dialect_kind(ib, dialect) |
         (int)c_orm_insert_builder_set_upsert(ib, 1);
}

C_ORM_EXPORT c_orm_error_t c_orm_upsert(c_orm_db_t *db,
                                        const c_orm_table_meta_t *meta,
                                        const void *in_struct) {
  c_orm_insert_builder_t *ib = NULL;
  char *sql = NULL;
  c_orm_query_t *query = NULL;
  c_orm_dialect_t dialect = C_ORM_DIALECT_SQLITE;
  c_orm_error_t rc;
  c_orm_error_t fin_rc;
  int bind_idx = 1;
  int has_row = 0;
  int cfg_bits = 0;

  if (!db || !meta || !in_struct) {
    return C_ORM_ERROR_VALIDATION;
  }
  if (meta->is_view) {
    return C_ORM_ERROR_READ_ONLY;
  }
  if (db->driver_name && strcmp(db->driver_name, "memory") == 0) {
    rc = c_orm_insert(db, meta, in_struct);
    if (rc != C_ORM_OK) {
      if (rc == C_ORM_ERROR_CONSTRAINT || rc == C_ORM_ERROR_SQL ||
          rc == C_ORM_ERROR_UNKNOWN) {
        return c_orm_update(db, meta, in_struct);
      }
      return rc;
    }
    return C_ORM_OK;
  }

  if (db->driver_name) {
    if (strcmp(db->driver_name, "db2") == 0) {
      dialect = C_ORM_DIALECT_DB2;
    } else if (strcmp(db->driver_name, "odbc") == 0) {
      dialect = C_ORM_DIALECT_ODBC;
    } else if (strcmp(db->driver_name, "postgres") == 0) {
      dialect = C_ORM_DIALECT_POSTGRES;
    } else if (strcmp(db->driver_name, "mysql") == 0) {
      dialect = C_ORM_DIALECT_MYSQL;
    }
  }

  if (meta->hooks[C_ORM_HOOK_BEFORE_SAVE]) {
    rc = meta->hooks[C_ORM_HOOK_BEFORE_SAVE]((void *)(size_t)in_struct, db);
    if (rc != C_ORM_OK) {
      return rc;
    }
  }

  rc = c_orm_insert_builder_init(meta, &ib);
  if (rc != C_ORM_OK) {
    return rc;
  }
  cfg_bits = api_calc_setup_upsert_ib(ib, dialect);
  bind_idx += cfg_bits;
  rc = c_orm_insert_builder_compile(ib, &sql);
  if (rc != C_ORM_OK) {
    c_orm_insert_builder_free(ib);
    return rc;
  }
  c_orm_insert_builder_free(ib);

  rc = c_orm_prepare_cached(db, sql, &query);
  if (rc != C_ORM_OK) {
    c_orm_system_free(sql);
    return rc;
  }
  c_orm_system_free(sql);

  rc = bind_row(db, query, meta, in_struct, 0, &bind_idx);
  if (rc != C_ORM_OK) {
    fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK) {
      return fin_rc;
    }
    return rc;
  }

  rc = db->vtable->step(query, &has_row);
  if (rc != C_ORM_OK) {
    fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK) {
      return fin_rc;
    }
    return rc;
  }
  fin_rc = c_orm_finalize_cached(db, query);
  if (fin_rc != C_ORM_OK) {
    return fin_rc;
  }

  if (meta->hooks[C_ORM_HOOK_AFTER_SAVE]) {
    rc = meta->hooks[C_ORM_HOOK_AFTER_SAVE]((void *)(size_t)in_struct, db);
    if (rc != C_ORM_OK) {
      return rc;
    }
  }

  return C_ORM_OK;
}

/**
 * @brief Initialize an iterator to fetch a large number of rows in batches.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param sql The custom query or NULL to find all.
 * @param chunk_size The maximum number of structs to fetch per next() call.
 * @param out_iter Pointer to receive the initialized iterator.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_find_batch_init(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, const char *sql,
    size_t chunk_size, struct c_orm_iterator **out_iter) {
  c_orm_error_t rc;
  struct c_orm_iterator *iter;

  LOG_DEBUG("c_orm_find_batch_init: entry");
  if (!db || !meta || !out_iter || chunk_size == 0) {
    LOG_DEBUG("c_orm_find_batch_init: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_find_batch_init: exit");
    return rc;
  }

  iter = NULL;
  rc = c_orm_system_malloc(sizeof(struct c_orm_iterator), (void **)&iter);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_find_batch_init: OOM");
    return rc;
  }

  iter->db = db;
  iter->meta = meta;
  iter->chunk_size = chunk_size;

  if (!sql) {
    sql = meta->query_select_all;
  }

  rc = db->vtable->prepare(db, sql, &iter->query);
  if (rc != C_ORM_OK) {
    c_orm_system_free(iter);
    LOG_DEBUG("c_orm_find_batch_init: exit");
    return rc;
  }

  *out_iter = iter;
  LOG_DEBUG("c_orm_find_batch_init: exit");
  return C_ORM_OK;
}

/**
 * @brief Fetch the next chunk of rows into the provided array.
 *
 * @param iter The active iterator.
 * @param out_array Pointer to a pre-allocated array of structs capable of
 * holding chunk_size items.
 * @param out_num_fetched Pointer to receive the number of rows actually fetched
 * (0 if EOF).
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_iterator_next(struct c_orm_iterator *iter,
                                               void *out_array,
                                               size_t *out_num_fetched) {
  c_orm_error_t rc;
  size_t count = 0;
  int has_row;

  LOG_DEBUG("c_orm_iterator_next: entry");
  if (!iter || !out_array || !out_num_fetched) {
    LOG_DEBUG("c_orm_iterator_next: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_iterator_next: exit");
    return rc;
  }

  *out_num_fetched = 0;

  for (count = 0; count < iter->chunk_size; count++) {
    rc = iter->db->vtable->step(iter->query, &has_row);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_iterator_next: exit");
      return rc;
    }
    if (!has_row)
      break;

    memset((char *)out_array + (count * iter->meta->struct_size), 0,
           iter->meta->struct_size);

    rc = c_orm_hydrate_row(iter->db, iter->query, iter->meta,
                           (char *)out_array +
                               (count * iter->meta->struct_size));
    if (rc != C_ORM_OK) {
      if (rc == C_ORM_ERROR_EXPIRED) {
        count--;
        continue;
      }
      LOG_DEBUG("c_orm_iterator_next: exit");
      return rc;
    }
  }

  *out_num_fetched = count;
  {
    rc = C_ORM_OK;
    LOG_DEBUG("c_orm_iterator_next: exit");
    return rc;
  }
}

/**
 * @brief Close and free the iterator.
 *
 * @param iter The iterator to free.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_iterator_close(struct c_orm_iterator *iter) {
  c_orm_error_t rc = C_ORM_OK;

  LOG_DEBUG("c_orm_iterator_close: entry");
  if (!iter) {
    LOG_DEBUG("c_orm_iterator_close: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_iterator_close: exit");
    return rc;
  }

  if (iter->query) {
    rc = iter->db->vtable->finalize(iter->query);
    if (rc != C_ORM_OK) {
      c_orm_system_free(iter);
      return rc;
    }
  }
  rc = c_orm_system_free(iter);
  {
    LOG_DEBUG("c_orm_iterator_close: exit");
    return rc;
  }
}

/**
 * @brief Insert a new record into the database.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param in_struct Pointer to the struct containing data to insert.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_insert(c_orm_db_t *db,
                                        const c_orm_table_meta_t *meta,
                                        const void *in_struct) {
  c_orm_error_t rc;

  c_orm_query_t *query;
  int has_row;
  size_t i;
  int bind_idx = 1;

  LOG_DEBUG("c_orm_insert: entry");
  if (!db || !meta || !in_struct) {
    LOG_DEBUG("c_orm_insert: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_insert: exit");
    return rc;
  }

  if (meta->is_view) {
    rc = C_ORM_ERROR_READ_ONLY;
    LOG_DEBUG("c_orm_insert: exit");
    return rc;
  }

  db->active_hook_meta = meta;
  if (meta->hooks[C_ORM_HOOK_BEFORE_SAVE]) {
    rc = meta->hooks[C_ORM_HOOK_BEFORE_SAVE]((void *)(size_t)in_struct, db);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_insert: exit");
      return rc;
    }
  }
  if (meta->hooks[C_ORM_HOOK_BEFORE_INSERT]) {
    rc = meta->hooks[C_ORM_HOOK_BEFORE_INSERT]((void *)(size_t)in_struct, db);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_insert: exit");
      return rc;
    }
  }

  /* BELONGS_TO: Insert children first, get their PK, assign to parent's FK */
  for (i = 0; i < meta->num_relations; i++) {
    const c_orm_relation_meta_t *rel = &meta->relations[i];
    if (rel->target_meta && rel->type == C_ORM_RELATION_BELONGS_TO) {
      const void *context_ptr = (const char *)in_struct + rel->struct_offset;
      const void *target_data_ptr =
          (const char *)context_ptr + (rel->data_offset - rel->struct_offset);
      void *nested_ptr = *(void *const *)target_data_ptr;
      if (nested_ptr) {
        int64_t new_id = 0;
        int got_lid = 0;
        rc = c_orm_insert(db, rel->target_meta, nested_ptr);
        if (rc != C_ORM_OK) {
          LOG_DEBUG("c_orm_insert: exit");
          return rc;
        }
        if (db->vtable->get_last_insert_rowid) {
          c_orm_error_t lid_err =
              db->vtable->get_last_insert_rowid(db, &new_id);
          if (lid_err != C_ORM_OK) {
            if (lid_err == C_ORM_ERROR_MEMORY) {
              return lid_err;
            }
          } else if (new_id > 0) {
            got_lid = 1;
          }
        }
        if (got_lid) {
          rc = set_int_field(meta, (void *)(size_t)in_struct, rel->local_key,
                             new_id);
          if (rc != C_ORM_OK)
            return rc;
          rc = set_int_field(rel->target_meta, nested_ptr, rel->foreign_key,
                             new_id);
          if (rc != C_ORM_OK)
            return rc;
        } else {
          new_id = api_calc_get_int_or_zero(rel->target_meta, nested_ptr,
                                            rel->foreign_key);
          if (new_id > 0) {
            rc = set_int_field(meta, (void *)(size_t)in_struct, rel->local_key,
                               new_id);
            if (rc != C_ORM_OK)
              return rc;
          }
        }
      } else {
        /* Validate required BelongsTo */
        size_t j;
        for (j = 0; j < meta->num_columns; ++j) {
          if (strcmp(meta->columns[j].name, rel->local_key) == 0) {
            if (!meta->columns[j].is_nullable) {
              int64_t existing_fk =
                  api_calc_get_int_or_zero(meta, in_struct, rel->local_key);
              if (existing_fk == 0) {
                LOG_DEBUG("c_orm_insert: exit");
                return C_ORM_ERROR_VALIDATION;
              } else {
                int exists = 0;
                c_orm_error_t exists_err = c_orm_exists_int32(
                    db, rel->target_meta, (int32_t)existing_fk, &exists);
                if (exists_err != C_ORM_OK)
                  return exists_err;

                if (!exists) {
                  LOG_DEBUG("c_orm_insert: exit");
                  return C_ORM_ERROR_VALIDATION;
                }
              }
            } else {
              int64_t existing_fk =
                  api_calc_get_int_or_zero(meta, in_struct, rel->local_key);
              if (existing_fk != 0) {
                int exists = 0;
                c_orm_error_t exists_err = c_orm_exists_int32(
                    db, rel->target_meta, (int32_t)existing_fk, &exists);
                if (exists_err != C_ORM_OK)
                  return exists_err;

                if (!exists) {
                  LOG_DEBUG("c_orm_insert: exit");
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

  {
    int db2_auto_pk = 0;
    size_t pk_idx = 0;
    size_t pk_total = 0;

    if (db->driver_name && strcmp(db->driver_name, "db2") == 0) {
      for (i = 0; i < meta->num_columns; ++i) {
        if (meta->columns[i].is_pk) {
          pk_total++;
          pk_idx = i;
        }
      }
      if (pk_total == 1 && !meta->columns[pk_idx].is_nullable &&
          (meta->columns[pk_idx].type == C_ORM_TYPE_INT32 ||
           meta->columns[pk_idx].type == C_ORM_TYPE_INT64)) {
        if (api_calc_get_int_or_zero(meta, in_struct,
                                     meta->columns[pk_idx].name) == 0) {
          db2_auto_pk = 1;
        }
      }
    }

    if (db2_auto_pk) {
      c_orm_string_builder_t *sb_ft = NULL;
      const char *ft_sql = NULL;
      size_t non_pk = 0;
      size_t k = 0;
      int64_t gen_id = 0;
      c_orm_error_t fin_rc;

      rc = c_orm_string_builder_init(&sb_ft);
      if (rc != C_ORM_OK) {
        return rc;
      }
      if ((rc = c_orm_string_builder_append(sb_ft, "SELECT ")) != C_ORM_OK ||
          (rc = c_orm_quote_identifier(sb_ft, meta->columns[pk_idx].name,
                                       '"')) != C_ORM_OK ||
          (rc = c_orm_string_builder_append(
               sb_ft, " FROM FINAL TABLE (INSERT INTO ")) != C_ORM_OK ||
          (rc = c_orm_quote_qualified_identifier(sb_ft, meta->name, '"')) !=
              C_ORM_OK) {
        c_orm_string_builder_free(sb_ft);
        return rc;
      }
      for (i = 0; i < meta->num_columns; ++i) {
        if (!meta->columns[i].is_pk) {
          non_pk++;
        }
      }
      if (non_pk == 0) {
        rc = c_orm_string_builder_append(sb_ft, " DEFAULT VALUES)");
        if (rc != C_ORM_OK) {
          c_orm_string_builder_free(sb_ft);
          return rc;
        }
      } else {
        if ((rc = c_orm_string_builder_append(sb_ft, " (")) != C_ORM_OK) {
          c_orm_string_builder_free(sb_ft);
          return rc;
        }
        for (i = 0; i < meta->num_columns; ++i) {
          if (!meta->columns[i].is_pk) {
            if (k > 0 &&
                (rc = c_orm_string_builder_append(sb_ft, ", ")) != C_ORM_OK) {
              c_orm_string_builder_free(sb_ft);
              return rc;
            }
            rc = c_orm_quote_identifier(sb_ft, meta->columns[i].name, '"');
            if (rc != C_ORM_OK) {
              c_orm_string_builder_free(sb_ft);
              return rc;
            }
            k++;
          }
        }
        if ((rc = c_orm_string_builder_append(sb_ft, ") VALUES (")) !=
            C_ORM_OK) {
          c_orm_string_builder_free(sb_ft);
          return rc;
        }
        for (k = 0; k < non_pk; ++k) {
          if (k > 0 &&
              (rc = c_orm_string_builder_append(sb_ft, ", ")) != C_ORM_OK) {
            c_orm_string_builder_free(sb_ft);
            return rc;
          }
          rc = c_orm_string_builder_append(sb_ft, "?");
          if (rc != C_ORM_OK) {
            c_orm_string_builder_free(sb_ft);
            return rc;
          }
        }
        if ((rc = c_orm_string_builder_append(sb_ft, "))")) != C_ORM_OK) {
          c_orm_string_builder_free(sb_ft);
          return rc;
        }
      }
      rc = c_orm_string_builder_get(sb_ft, &ft_sql);
      if (rc != C_ORM_OK) {
        c_orm_string_builder_free(sb_ft);
        return rc;
      }
      rc = c_orm_prepare_cached(db, ft_sql, &query);
      if (rc != C_ORM_OK) {
        c_orm_string_builder_free(sb_ft);
        return rc;
      }
      c_orm_string_builder_free(sb_ft);
      rc = bind_row(db, query, meta, in_struct, 1, &bind_idx);
      if (rc != C_ORM_OK) {
        fin_rc = c_orm_finalize_cached(db, query);
        if (fin_rc != C_ORM_OK) {
          return fin_rc;
        }
        return rc;
      }
      rc = db->vtable->step(query, &has_row);
      if (rc != C_ORM_OK) {
        fin_rc = c_orm_finalize_cached(db, query);
        if (fin_rc != C_ORM_OK) {
          return fin_rc;
        }
        return rc;
      }
      if (has_row && db->vtable->get_int64) {
        c_orm_error_t grc = db->vtable->get_int64(query, 0, &gen_id);
        if (grc != C_ORM_OK) {
          if (grc == C_ORM_ERROR_MEMORY) {
            fin_rc = c_orm_finalize_cached(db, query);
            if (fin_rc != C_ORM_OK) {
              return fin_rc;
            }
            return grc;
          }
        } else if (gen_id > 0) {
          void *pk_dst =
              (char *)(size_t)in_struct + meta->columns[pk_idx].offset;
          if (meta->columns[pk_idx].type == C_ORM_TYPE_INT32) {
            *(int32_t *)pk_dst = (int32_t)gen_id;
          } else {
            *(int64_t *)pk_dst = gen_id;
          }
        }
      }
      fin_rc = c_orm_finalize_cached(db, query);
      if (fin_rc != C_ORM_OK) {
        return fin_rc;
      }
      if (gen_id <= 0 && db->vtable->get_last_insert_rowid) {
        c_orm_error_t lrc = db->vtable->get_last_insert_rowid(db, &gen_id);
        if (lrc != C_ORM_OK) {
          if (lrc == C_ORM_ERROR_MEMORY) {
            return lrc;
          }
        } else if (gen_id > 0) {
          void *pk_dst =
              (char *)(size_t)in_struct + meta->columns[pk_idx].offset;
          if (meta->columns[pk_idx].type == C_ORM_TYPE_INT32) {
            *(int32_t *)pk_dst = (int32_t)gen_id;
          } else {
            *(int64_t *)pk_dst = gen_id;
          }
        }
      }
    } else {
      rc = c_orm_prepare_cached(db, meta->query_insert, &query);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_insert: exit");
        return rc;
      }

      rc = bind_row(db, query, meta, in_struct, 0, &bind_idx); /* Bind all */
      if (rc != C_ORM_OK) {
        c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
        if (fin_rc != C_ORM_OK)
          return fin_rc;
        LOG_DEBUG("c_orm_insert: exit");
        return rc;
      }

      rc = db->vtable->step(query, &has_row);
      if (rc != C_ORM_OK) {
        c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
        if (fin_rc != C_ORM_OK)
          return fin_rc;
        return rc;
      }
      rc = c_orm_finalize_cached(db, query);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_insert: exit");
        return rc;
      }
    }
  }

  /* HAS_ONE / ONE_TO_ONE: Insert parent, get its PK, assign to child's FK,
   * insert child */
  for (i = 0; i < meta->num_relations; i++) {
    const c_orm_relation_meta_t *rel = &meta->relations[i];
    if (rel->target_meta && rel->type == C_ORM_RELATION_ONE_TO_ONE) {
      const void *context_ptr = (const char *)in_struct + rel->struct_offset;
      const void *target_data_ptr =
          (const char *)context_ptr + (rel->data_offset - rel->struct_offset);
      void *nested_ptr = *(void *const *)target_data_ptr;
      if (nested_ptr) {
        int64_t parent_id = 0;
        if (db->vtable->get_last_insert_rowid) {
          c_orm_error_t lrc = db->vtable->get_last_insert_rowid(db, &parent_id);
          if (lrc != C_ORM_OK) {
            if (lrc == C_ORM_ERROR_MEMORY) {
              return lrc;
            }
            parent_id = 0;
          }
        }
        if (parent_id <= 0) {
          parent_id = api_calc_get_int_or_zero(meta, in_struct, rel->local_key);
        }
        if (parent_id > 0) {
          rc = set_int_field(rel->target_meta, nested_ptr, rel->foreign_key,
                             parent_id);
          if (rc != C_ORM_OK)
            return rc;
        }
        rc = c_orm_insert(db, rel->target_meta, nested_ptr);
        if (rc != C_ORM_OK) {
          LOG_DEBUG("c_orm_insert: exit");
          return rc;
        }
      }
    }
  }

  if (meta->hooks[C_ORM_HOOK_AFTER_INSERT]) {
    rc = meta->hooks[C_ORM_HOOK_AFTER_INSERT]((void *)(size_t)in_struct, db);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_insert: exit");
      return rc;
    }
  }
  if (meta->hooks[C_ORM_HOOK_AFTER_SAVE]) {
    rc = meta->hooks[C_ORM_HOOK_AFTER_SAVE]((void *)(size_t)in_struct, db);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_insert: exit");
      return rc;
    }
  }

  {
    rc = C_ORM_OK;
    LOG_DEBUG("c_orm_insert: exit");
    return rc;
  }
}

/**
 * @brief Update an existing record in the database by its primary key.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param in_struct Pointer to the struct containing the updated data (and PK).
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_update(c_orm_db_t *db,
                                        const c_orm_table_meta_t *meta,
                                        const void *in_struct) {
  c_orm_error_t rc;

  c_orm_query_t *query;
  int has_row;
  int32_t pk_val = 0; /* Fallback assuming int PK */
  int bind_idx = 1;
  size_t i;

  LOG_DEBUG("c_orm_update: entry");
  if (!db || !meta || !in_struct) {
    LOG_DEBUG("c_orm_update: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_update: exit");
    return rc;
  }
  if (meta->is_view) {
    rc = C_ORM_ERROR_READ_ONLY;
    LOG_DEBUG("c_orm_update: exit");
    return rc;
  }
  if (!meta->query_update) {
    rc = C_ORM_ERROR_UNKNOWN;
    LOG_DEBUG("c_orm_update: exit");
    return rc;
  }

  db->active_hook_meta = meta;
  if (meta->hooks[C_ORM_HOOK_BEFORE_SAVE]) {
    rc = meta->hooks[C_ORM_HOOK_BEFORE_SAVE]((void *)(size_t)in_struct, db);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_update: exit");
      return rc;
    }
  }
  if (meta->hooks[C_ORM_HOOK_BEFORE_UPDATE]) {
    rc = meta->hooks[C_ORM_HOOK_BEFORE_UPDATE]((void *)(size_t)in_struct, db);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_update: exit");
      return rc;
    }
  }

  /* Step 92: Implement cascading update for nested structs */
  /* BELONGS_TO: Update children first */
  for (i = 0; i < meta->num_relations; i++) {
    const c_orm_relation_meta_t *rel = &meta->relations[i];
    if (rel->target_meta && rel->type == C_ORM_RELATION_BELONGS_TO) {
      const void *context_ptr = (const char *)in_struct + rel->struct_offset;
      const void *target_data_ptr =
          (const char *)context_ptr + (rel->data_offset - rel->struct_offset);
      void *nested_ptr = *(void *const *)target_data_ptr;
      if (nested_ptr && rel->on_update == C_ORM_CASCADE_UPDATE) {
        rc = c_orm_save(db, rel->target_meta, nested_ptr);
        if (rc != C_ORM_OK) {
          LOG_DEBUG("c_orm_update: exit");
          return rc;
        }
      } else if (!nested_ptr) {
        /* Validate required BelongsTo */
        size_t j;
        for (j = 0; j < meta->num_columns; ++j) {
          if (strcmp(meta->columns[j].name, rel->local_key) == 0) {
            if (!meta->columns[j].is_nullable) {
              int64_t existing_fk =
                  api_calc_get_int_or_zero(meta, in_struct, rel->local_key);
              if (existing_fk == 0) {
                LOG_DEBUG("c_orm_update: exit");
                return C_ORM_ERROR_VALIDATION;
              } else {
                int exists = 0;
                c_orm_error_t exists_err = c_orm_exists_int32(
                    db, rel->target_meta, (int32_t)existing_fk, &exists);
                if (exists_err != C_ORM_OK)
                  return exists_err;

                if (!exists) {
                  LOG_DEBUG("c_orm_update: exit");
                  return C_ORM_ERROR_VALIDATION;
                }
              }
            } else {
              int64_t existing_fk =
                  api_calc_get_int_or_zero(meta, in_struct, rel->local_key);
              if (existing_fk != 0) {
                int exists = 0;
                c_orm_error_t exists_err = c_orm_exists_int32(
                    db, rel->target_meta, (int32_t)existing_fk, &exists);
                if (exists_err != C_ORM_OK)
                  return exists_err;

                if (!exists) {
                  LOG_DEBUG("c_orm_update: exit");
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

  rc = c_orm_prepare_cached(db, meta->query_update, &query);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_update: exit");
    return rc;
  }

  /* We bind all fields, then the PK at the end. */
  rc = bind_row(db, query, meta, in_struct, 0, &bind_idx);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    LOG_DEBUG("c_orm_update: exit");
    return rc;
  }

  bind_idx = (int)(meta->num_columns + 1);

  /* Find PK to bind to WHERE clause */
  for (i = 0; i < meta->num_columns; ++i) {
    if (meta->columns[i].is_pk) {
      const void *field_ptr = (const char *)in_struct + meta->columns[i].offset;
      switch (meta->columns[i].type) {
      case C_ORM_TYPE_INT32: {
        pk_val = *(const int32_t *)field_ptr;
        rc = db->vtable->bind_int32(query, bind_idx, pk_val);
        if (rc != C_ORM_OK) {
          c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
          if (fin_rc != C_ORM_OK)
            return fin_rc;
          LOG_DEBUG("c_orm_update: exit");
          return rc;
        }
        break;
      }
      case C_ORM_TYPE_STRING: {
        const char *pk_str = *(const char *const *)field_ptr;
        rc = db->vtable->bind_string(query, bind_idx, pk_str);
        if (rc != C_ORM_OK) {
          c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
          if (fin_rc != C_ORM_OK)
            return fin_rc;
          LOG_DEBUG("c_orm_update: exit");
          return rc;
        }
        break;
      }
      case C_ORM_TYPE_INT64: {
        int64_t pk_64 = *(const int64_t *)field_ptr;
        rc = db->vtable->bind_int64(query, bind_idx, pk_64);
        if (rc != C_ORM_OK) {
          c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
          if (fin_rc != C_ORM_OK)
            return fin_rc;
          LOG_DEBUG("c_orm_update: exit");
          return rc;
        }
        break;
      }
      default:
        break;
      }
      bind_idx++;
    }
  }
  rc = db->vtable->step(query, &has_row);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    return rc;
  }
  rc = c_orm_finalize_cached(db, query);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_update: exit");
    return rc;
  }

  if (meta->hooks[C_ORM_HOOK_AFTER_UPDATE]) {
    rc = meta->hooks[C_ORM_HOOK_AFTER_UPDATE]((void *)(size_t)in_struct, db);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_update: exit");
      return rc;
    }
  }
  if (meta->hooks[C_ORM_HOOK_AFTER_SAVE]) {
    rc = meta->hooks[C_ORM_HOOK_AFTER_SAVE]((void *)(size_t)in_struct, db);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_update: exit");
      return rc;
    }
  }

  LOG_DEBUG("c_orm_update: exit");
  return C_ORM_OK;
}

/**
 * @brief Binds a scalar primary-key value (`INT32`, `STRING`, or `INT64`) to a
 * parameter slot.
 *
 * @param db Database connection.
 * @param query Prepared statement handle.
 * @param bind_idx 1-based parameter index.
 * @param pk_col Primary-key column metadata.
 * @param field_ptr Pointer to the PK field inside the struct.
 * @return C_ORM_OK on success, C_ORM_ERROR_NOT_IMPLEMENTED on unsupported PK
 * type, or driver bind error.
 */
static c_orm_error_t bind_pk_scalar(c_orm_db_t *db, c_orm_query_t *query,
                                    int bind_idx,
                                    const c_orm_column_meta_t *pk_col,
                                    const void *field_ptr) {
  if (pk_col->type == C_ORM_TYPE_INT32) {
    return db->vtable->bind_int32(query, bind_idx, *(const int32_t *)field_ptr);
  }
  if (pk_col->type == C_ORM_TYPE_STRING) {
    return db->vtable->bind_string(query, bind_idx,
                                   *(const char *const *)field_ptr);
  }
  if (pk_col->type == C_ORM_TYPE_INT64) {
    return db->vtable->bind_int64(query, bind_idx, *(const int64_t *)field_ptr);
  }
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}

/**
 * @brief Binds the primary key and expected previous version for optimistic
 * locking queries.
 *
 * @param db Database connection.
 * @param query Prepared statement handle.
 * @param pk_col Primary key column metadata.
 * @param in_struct Input record structure pointer.
 * @param start_idx 1-based parameter index for the primary key.
 * @param is_ver_i64 Non-zero when version column is 64-bit integer.
 * @param expected_ver Expected prior version value (`new_version - 1`).
 * @return C_ORM_OK on success, or driver bind error code.
 */
static c_orm_error_t bind_optimistic_where(c_orm_db_t *db, c_orm_query_t *query,
                                           const c_orm_column_meta_t *pk_col,
                                           const void *in_struct, int start_idx,
                                           int is_ver_i64,
                                           int64_t expected_ver) {
  c_orm_error_t rc;
  const void *pk_ptr = (const char *)in_struct + pk_col->offset;

  rc = bind_pk_scalar(db, query, start_idx, pk_col, pk_ptr);
  if (rc != C_ORM_OK) {
    return rc;
  }

  if (is_ver_i64) {
    return db->vtable->bind_int64(query, start_idx + 1, expected_ver);
  }
  return db->vtable->bind_int32(query, start_idx + 1, (int32_t)expected_ver);
}

/**
 * @brief Formats the optimistic lock pre-check SELECT query for the active
 * database dialect (`$1`/`$2` on PostgreSQL, quoted identifiers on Db2/ODBC).
 * @param db Database connection handle.
 * @param table_name Target table name.
 * @param pk_name Primary key column name.
 * @param ver_name Version column name.
 * @param buf Output SQL buffer.
 * @param buf_cap Output SQL buffer capacity.
 * @return C_ORM_OK on success, or Safe CRT error code.
 */
static c_orm_error_t c_orm_format_optimistic_check(const c_orm_db_t *db,
                                                   const char *table_name,
                                                   const char *pk_name,
                                                   const char *ver_name,
                                                   char *buf, size_t buf_cap) {
  if (db->driver_name && (strcmp(db->driver_name, "db2") == 0 ||
                          strcmp(db->driver_name, "odbc") == 0)) {
    return C_ORM_SPRINTF(buf, buf_cap,
                         "SELECT * FROM \"%s\" WHERE \"%s\" = ? AND \"%s\" = ?",
                         table_name, pk_name, ver_name);
  }
  if (db->driver_name && strcmp(db->driver_name, "postgres") == 0) {
    return C_ORM_SPRINTF(buf, buf_cap,
                         "SELECT * FROM %s WHERE %s = $1 AND %s = $2",
                         table_name, pk_name, ver_name);
  }
  return C_ORM_SPRINTF(buf, buf_cap, "SELECT * FROM %s WHERE %s = ? AND %s = ?",
                       table_name, pk_name, ver_name);
}

/**
 * @brief Formats the optimistic UPDATE statement with dialect-appropriate
 * quoting and WHERE placeholders.
 * @param db Database connection handle.
 * @param table_name Target table name.
 * @param set_buf Formatted SET clause string.
 * @param pk_name Primary key column name.
 * @param ver_name Version column name.
 * @param next_ph Next 1-based parameter index for PostgreSQL `$N`.
 * @param upd_sql Output buffer receiving full UPDATE SQL.
 * @param upd_cap Output buffer capacity.
 * @return C_ORM_OK on success, or Safe CRT error code.
 */
static c_orm_error_t
c_orm_format_optimistic_update(const c_orm_db_t *db, const char *table_name,
                               const char *set_buf, const char *pk_name,
                               const char *ver_name, size_t next_ph,
                               char *upd_sql, size_t upd_cap) {
  if (db->driver_name && (strcmp(db->driver_name, "db2") == 0 ||
                          strcmp(db->driver_name, "odbc") == 0)) {
    return C_ORM_SPRINTF(upd_sql, upd_cap,
                         "UPDATE \"%s\" SET %s WHERE \"%s\" = ? AND \"%s\" = ?",
                         table_name, set_buf, pk_name, ver_name);
  }
  if (db->driver_name && strcmp(db->driver_name, "postgres") == 0) {
    char ph1[24];
    char ph2[24];
    char rev[24];
    size_t rlen = 0U;
    size_t pos = 0U;
    size_t v = next_ph;

    ph1[pos++] = '$';
    do {
      rev[rlen++] = (char)('0' + (v % 10U));
      v /= 10U;
    } while (v > 0U);
    while (rlen > 0U) {
      ph1[pos++] = rev[--rlen];
    }
    ph1[pos] = '\0';

    pos = 0U;
    rlen = 0U;
    v = next_ph + 1U;
    ph2[pos++] = '$';
    do {
      rev[rlen++] = (char)('0' + (v % 10U));
      v /= 10U;
    } while (v > 0U);
    while (rlen > 0U) {
      ph2[pos++] = rev[--rlen];
    }
    ph2[pos] = '\0';

    return C_ORM_SPRINTF(upd_sql, upd_cap,
                         "UPDATE %s SET %s WHERE %s = %s AND %s = %s",
                         table_name, set_buf, pk_name, ph1, ver_name, ph2);
  }
  return C_ORM_SPRINTF(upd_sql, upd_cap,
                       "UPDATE %s SET %s WHERE %s = ? AND %s = ?", table_name,
                       set_buf, pk_name, ver_name);
}

/**
 * @brief Update an existing record in the database utilizing optimistic locking
 * via version column.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param version_column_name Name of the column tracking the version.
 * @param in_struct Pointer to the struct containing the updated data (and PK).
 * @return C_ORM_OK on success, C_ORM_ERROR_NOT_FOUND on version mismatch.
 */
C_ORM_EXPORT c_orm_error_t c_orm_update_optimistic(
    c_orm_db_t *db, const c_orm_table_meta_t *meta,
    const char *version_column_name, const void *in_struct) {
  c_orm_error_t rc;
  c_orm_error_t fin_rc;
  const c_orm_column_meta_t *pk_col = NULL;
  const c_orm_column_meta_t *ver_col = NULL;
  const void *ver_ptr;
  int64_t expected_ver = 0;
  int is_ver_i64 = 0;
  c_orm_query_t *query = NULL;
  char *check_sql = NULL;
  char *set_buf = NULL;
  char *upd_sql = NULL;
  size_t check_cap;
  size_t set_cap = 0;
  size_t set_pos = 0;
  size_t upd_cap;
  size_t i;
  int has_row = 0;
  int bind_idx = 1;

  LOG_DEBUG("c_orm_update_optimistic: entry");
  if (!db || !meta || !version_column_name || !in_struct) {
    return C_ORM_ERROR_VALIDATION;
  }
  if (meta->is_view) {
    return C_ORM_ERROR_READ_ONLY;
  }
  if (!meta->name || !meta->columns || meta->num_columns == 0) {
    return C_ORM_ERROR_VALIDATION;
  }

  for (i = 0; i < meta->num_columns; ++i) {
    if (!pk_col && meta->columns[i].is_pk) {
      pk_col = &meta->columns[i];
    }
    if (strcmp(meta->columns[i].name, version_column_name) == 0) {
      ver_col = &meta->columns[i];
    }
  }

  if (!pk_col || !ver_col || ver_col->is_pk) {
    return C_ORM_ERROR_VALIDATION;
  }

  if (pk_col->type == C_ORM_TYPE_STRING) {
    const void *pk_ptr = (const char *)in_struct + pk_col->offset;
    if (*(const char *const *)pk_ptr == NULL) {
      return C_ORM_ERROR_VALIDATION;
    }
  } else if (pk_col->type != C_ORM_TYPE_INT32 &&
             pk_col->type != C_ORM_TYPE_INT64) {
    return C_ORM_ERROR_VALIDATION;
  }

  ver_ptr = (const char *)in_struct + ver_col->offset;
  if (ver_col->type == C_ORM_TYPE_INT32) {
    if (ver_col->is_nullable) {
      const int32_t *vp32 = *(const int32_t *const *)ver_ptr;
      if (!vp32) {
        return C_ORM_ERROR_VALIDATION;
      }
      expected_ver = (int64_t)(*vp32) - 1;
    } else {
      expected_ver = (int64_t)(*(const int32_t *)ver_ptr) - 1;
    }
  } else if (ver_col->type == C_ORM_TYPE_INT64) {
    is_ver_i64 = 1;
    if (ver_col->is_nullable) {
      const int64_t *vp64 = *(const int64_t *const *)ver_ptr;
      if (!vp64) {
        return C_ORM_ERROR_VALIDATION;
      }
      expected_ver = (*vp64) - 1;
    } else {
      expected_ver = (*(const int64_t *)ver_ptr) - 1;
    }
  } else {
    return C_ORM_ERROR_VALIDATION;
  }

  if (meta->hooks[C_ORM_HOOK_BEFORE_SAVE]) {
    rc = meta->hooks[C_ORM_HOOK_BEFORE_SAVE]((void *)(size_t)in_struct, db);
    if (rc != C_ORM_OK) {
      return rc;
    }
  }
  if (meta->hooks[C_ORM_HOOK_BEFORE_UPDATE]) {
    rc = meta->hooks[C_ORM_HOOK_BEFORE_UPDATE]((void *)(size_t)in_struct, db);
    if (rc != C_ORM_OK) {
      return rc;
    }
  }

  check_cap =
      strlen(meta->name) + strlen(pk_col->name) + strlen(ver_col->name) + 64;
  rc = c_orm_system_malloc(check_cap, (void **)&check_sql);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = c_orm_format_optimistic_check(db, meta->name, pk_col->name,
                                     ver_col->name, check_sql, check_cap);
  if (rc != C_ORM_OK) {
    c_orm_system_free(check_sql);
    return rc;
  }

  rc = c_orm_prepare_cached(db, check_sql, &query);
  if (rc != C_ORM_OK) {
    c_orm_system_free(check_sql);
    return rc;
  }
  c_orm_system_free(check_sql);

  rc = bind_optimistic_where(db, query, pk_col, in_struct, 1, is_ver_i64,
                             expected_ver);
  if (rc != C_ORM_OK) {
    fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK) {
      return fin_rc;
    }
    return rc;
  }

  rc = db->vtable->step(query, &has_row);
  if (rc != C_ORM_OK) {
    fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK) {
      return fin_rc;
    }
    return rc;
  }

  rc = c_orm_finalize_cached(db, query);
  if (rc != C_ORM_OK) {
    return rc;
  }

  if (!has_row) {
    return C_ORM_ERROR_NOT_FOUND;
  }

  for (i = 0; i < meta->num_columns; ++i) {
    if (!meta->columns[i].is_pk) {
      set_cap += strlen(meta->columns[i].name) + 32;
    }
  }

  rc = c_orm_system_malloc(set_cap + 1, (void **)&set_buf);
  if (rc != C_ORM_OK) {
    return rc;
  }

  {
    size_t next_ph = 1U;
    int is_pg =
        (db->driver_name && strcmp(db->driver_name, "postgres") == 0) ? 1 : 0;
    int quote_id = (db->driver_name && (strcmp(db->driver_name, "db2") == 0 ||
                                        strcmp(db->driver_name, "odbc") == 0))
                       ? 1
                       : 0;

    for (i = 0; i < meta->num_columns; ++i) {
      if (!meta->columns[i].is_pk) {
        size_t col_len = strlen(meta->columns[i].name);
        if (set_pos > 0U) {
          memcpy(set_buf + set_pos, ", ", 2);
          set_pos += 2U;
        }
        if (quote_id) {
          set_buf[set_pos++] = '"';
        }
        memcpy(set_buf + set_pos, meta->columns[i].name, col_len);
        set_pos += col_len;
        if (quote_id) {
          set_buf[set_pos++] = '"';
        }
        if (is_pg) {
          char rev[24];
          size_t rlen = 0U;
          size_t v = next_ph++;
          memcpy(set_buf + set_pos, " = $", 4);
          set_pos += 4U;
          do {
            rev[rlen++] = (char)('0' + (v % 10U));
            v /= 10U;
          } while (v > 0U);
          while (rlen > 0U) {
            set_buf[set_pos++] = rev[--rlen];
          }
        } else {
          memcpy(set_buf + set_pos, " = ?", 4);
          set_pos += 4U;
          next_ph++;
        }
      }
    }
    set_buf[set_pos] = '\0';

    upd_cap = strlen(meta->name) + set_pos + strlen(pk_col->name) +
              strlen(ver_col->name) + 80;
    rc = c_orm_system_malloc(upd_cap, (void **)&upd_sql);
    if (rc != C_ORM_OK) {
      c_orm_system_free(set_buf);
      return rc;
    }

    rc = c_orm_format_optimistic_update(db, meta->name, set_buf, pk_col->name,
                                        ver_col->name, next_ph, upd_sql,
                                        upd_cap);
    if (rc != C_ORM_OK) {
      c_orm_system_free(set_buf);
      c_orm_system_free(upd_sql);
      return rc;
    }
  }
  c_orm_system_free(set_buf);

  rc = c_orm_prepare_cached(db, upd_sql, &query);
  if (rc != C_ORM_OK) {
    c_orm_system_free(upd_sql);
    return rc;
  }
  c_orm_system_free(upd_sql);

  rc = bind_row(db, query, meta, in_struct, 1, &bind_idx);
  if (rc != C_ORM_OK) {
    fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK) {
      return fin_rc;
    }
    return rc;
  }

  rc = bind_optimistic_where(db, query, pk_col, in_struct, bind_idx, is_ver_i64,
                             expected_ver);
  if (rc != C_ORM_OK) {
    fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK) {
      return fin_rc;
    }
    return rc;
  }

  rc = db->vtable->step(query, &has_row);
  if (rc != C_ORM_OK) {
    fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK) {
      return fin_rc;
    }
    return rc;
  }

  rc = c_orm_finalize_cached(db, query);
  if (rc != C_ORM_OK) {
    return rc;
  }

  if (meta->hooks[C_ORM_HOOK_AFTER_UPDATE]) {
    rc = meta->hooks[C_ORM_HOOK_AFTER_UPDATE]((void *)(size_t)in_struct, db);
    if (rc != C_ORM_OK) {
      return rc;
    }
  }
  if (meta->hooks[C_ORM_HOOK_AFTER_SAVE]) {
    rc = meta->hooks[C_ORM_HOOK_AFTER_SAVE]((void *)(size_t)in_struct, db);
    if (rc != C_ORM_OK) {
      return rc;
    }
  }

  LOG_DEBUG("c_orm_update_optimistic: exit");
  return C_ORM_OK;
}

/**
 * @brief Save a record to the database via insert or update depending on PK.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param in_struct Pointer to record structure.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_save(c_orm_db_t *db,
                                      const c_orm_table_meta_t *meta,
                                      const void *in_struct) {
  c_orm_error_t rc;
  size_t i;
  int is_pk_set = 0;

  LOG_DEBUG("c_orm_save: entry");
  if (!db || !meta || !in_struct) {
    LOG_DEBUG("c_orm_save: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_save: exit");
    return rc;
  }

  if (meta->is_view) {
    rc = C_ORM_ERROR_READ_ONLY;
    LOG_DEBUG("c_orm_save: exit");
    return rc;
  }

  /* Step 98: Upsert based on PK presence. Check if PK is zero/null */
  for (i = 0; i < meta->num_columns; ++i) {
    if (meta->columns[i].is_pk) {
      const void *field_ptr = (const char *)in_struct + meta->columns[i].offset;
      if (meta->columns[i].type == C_ORM_TYPE_INT32) {
        int32_t val = *(const int32_t *)field_ptr;
        if (val != 0)
          is_pk_set = 1;
      } else if (meta->columns[i].type == C_ORM_TYPE_STRING) {
        const char *val = *(const char *const *)field_ptr;
        if (val && val[0] != '\0')
          is_pk_set = 1;
      } else {
        is_pk_set = 1; /* Fallback: assume it is set if not int/string */
      }
      break;
    }
  }

  LOG_DEBUG("c_orm_save: exit");
  if (is_pk_set) {
    return c_orm_update(db, meta, in_struct);
  }
  return c_orm_insert(db, meta, in_struct);
}

/**
 * @brief Executes a single-parameter cascade `DELETE`/`UPDATE` statement if the
 * table exists.
 *
 * @param db Database connection.
 * @param sql Parameterized cascade SQL statement.
 * @param pk_col Primary-key column metadata.
 * @param field_ptr Pointer to the parent PK field value.
 * @return C_ORM_OK on success (or non-OOM missing child table), or error code
 * on failure.
 */
static c_orm_error_t exec_cascade_sql(c_orm_db_t *db, const char *sql,
                                      const c_orm_column_meta_t *pk_col,
                                      const void *field_ptr) {
  c_orm_query_t *casc_query = NULL;
  int has_row = 0;
  c_orm_error_t rc = c_orm_prepare_cached(db, sql, &casc_query);
  if (rc != C_ORM_OK) {
    if (rc == C_ORM_ERROR_MEMORY) {
      return rc;
    }
    return C_ORM_OK;
  }
  if (pk_col->type == C_ORM_TYPE_INT32 || pk_col->type == C_ORM_TYPE_STRING ||
      pk_col->type == C_ORM_TYPE_INT64) {
    c_orm_error_t casc_rc =
        bind_pk_scalar(db, casc_query, 1, pk_col, field_ptr);
    if (casc_rc != C_ORM_OK) {
      rc = c_orm_finalize_cached(db, casc_query);
      if (rc != C_ORM_OK) {
        return rc;
      }
      return casc_rc;
    }
  }
  {
    c_orm_error_t casc_rc = db->vtable->step(casc_query, &has_row);
    if (casc_rc != C_ORM_OK) {
      rc = c_orm_finalize_cached(db, casc_query);
      if (rc != C_ORM_OK) {
        return rc;
      }
      return casc_rc;
    }
  }
  return c_orm_finalize_cached(db, casc_query);
}

/**
 * @brief Delete records matching the PKs in the given array in bulk.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param in_array Pointer to an array of structs containing PKs to delete.
 * @param num_items The total number of structs in the array.
 * @param chunk_size The number of structs to process per SQL query. 0 to
 * auto-calculate.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_delete_batch(c_orm_db_t *db,
                                              const c_orm_table_meta_t *meta,
                                              const void *in_array,
                                              size_t num_items,
                                              size_t chunk_size) {
  c_orm_error_t rc;
  c_orm_error_t fin_rc;

  size_t i, j;
  size_t actual_chunk;
  const c_orm_column_meta_t *pk_col = NULL;

  LOG_DEBUG("c_orm_delete_batch: entry");
  if (!db || !meta || !in_array) {
    LOG_DEBUG("c_orm_delete_batch: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_delete_batch: exit");
    return rc;
  }
  if (meta->is_view) {
    rc = C_ORM_ERROR_READ_ONLY;
    LOG_DEBUG("c_orm_delete_batch: exit");
    return rc;
  }
  if (num_items == 0) {
    rc = C_ORM_OK;
    LOG_DEBUG("c_orm_delete_batch: exit");
    return rc;
  }

  for (i = 0; i < meta->num_columns; i++) {
    if (meta->columns[i].is_pk) {
      pk_col = &meta->columns[i];
      break;
    }
  }

  if (!pk_col) {
    rc = C_ORM_ERROR_UNKNOWN;
    LOG_DEBUG("c_orm_delete_batch: exit");
    return rc;
  }

  if (chunk_size == 0) {
    chunk_size = 1000;
  }

  rc = c_orm_transaction_begin(db);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_delete_batch: exit");
    return rc;
  }

  for (i = 0; i < num_items; i += actual_chunk) {
    c_orm_query_t *query;
    c_orm_string_builder_t *sb;
    const char *sql_str;
    int has_row;

    actual_chunk = chunk_size;
    if (i + actual_chunk > num_items) {
      actual_chunk = num_items - i;
    }

    rc = c_orm_string_builder_init(&sb);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_delete_batch: OOM");
      LOG_DEBUG("c_orm_delete_batch: exit");
      return rollback_preserve_error(db, rc);
    }

    rc = c_orm_string_builder_append(sb, "DELETE FROM ");
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rollback_preserve_error(db, rc);
    }
    rc = c_orm_append_dialect_table(sb, db, meta->name);
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rollback_preserve_error(db, rc);
    }
    rc = c_orm_string_builder_append(sb, " WHERE ");
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rollback_preserve_error(db, rc);
    }
    rc = c_orm_append_dialect_col(sb, db, pk_col->name);
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rollback_preserve_error(db, rc);
    }
    rc = c_orm_string_builder_append(sb, " IN (");
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rollback_preserve_error(db, rc);
    }

    for (j = 0; j < actual_chunk; j++) {
      if (j > 0) {
        rc = c_orm_string_builder_append(sb, ", ");
        if (rc != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rollback_preserve_error(db, rc);
        }
      }
      rc = c_orm_append_placeholder(
          sb,
          (db->driver_name && strcmp(db->driver_name, "postgres") == 0) ? 1 : 0,
          j + 1U);
      if (rc != C_ORM_OK) {
        c_orm_string_builder_free(sb);
        return rollback_preserve_error(db, rc);
      }
    }
    rc = c_orm_string_builder_append(sb, ")");
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rollback_preserve_error(db, rc);
    }

    if ((rc = c_orm_string_builder_get(sb, &sql_str)) != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rollback_preserve_error(db, rc);
    }

    rc = db->vtable->prepare(db, sql_str, &query);
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      LOG_DEBUG("c_orm_delete_batch: exit");
      return rollback_preserve_error(db, rc);
    }
    c_orm_string_builder_free(sb);

    for (j = 0; j < actual_chunk; j++) {
      const void *current_struct =
          (const char *)in_array + ((i + j) * meta->struct_size);
      const void *field_ptr = (const char *)current_struct + pk_col->offset;
      int bind_idx = (int)(j + 1);

      if (meta->hooks[C_ORM_HOOK_BEFORE_DELETE]) {
        rc = meta->hooks[C_ORM_HOOK_BEFORE_DELETE](
            (void *)(size_t)current_struct, db);
        if (rc != C_ORM_OK) {
          goto delete_batch_err;
        }
      }

      rc = bind_pk_scalar(db, query, bind_idx, pk_col, field_ptr);
      if (rc != C_ORM_OK) {
        goto delete_batch_err;
      }
    }

    rc = db->vtable->step(query, &has_row);
    if (rc != C_ORM_OK) {
      goto delete_batch_err;
    }
    fin_rc = db->vtable->finalize(query);
    if (fin_rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_delete_batch: exit");
      return rollback_preserve_error(db, fin_rc);
    }

    for (j = 0; j < actual_chunk; j++) {
      const void *current_struct =
          (const char *)in_array + ((i + j) * meta->struct_size);
      if (meta->hooks[C_ORM_HOOK_AFTER_DELETE]) {
        rc = meta->hooks[C_ORM_HOOK_AFTER_DELETE](
            (void *)(size_t)current_struct, db);
        if (rc != C_ORM_OK) {
          LOG_DEBUG("c_orm_delete_batch: exit");
          return rollback_preserve_error(db, rc);
        }
      }
    }
    continue;

  delete_batch_err: {
    fin_rc = db->vtable->finalize(query);
    if (fin_rc != C_ORM_OK) {
      return rollback_preserve_error(db, fin_rc);
    }
    LOG_DEBUG("c_orm_delete_batch: exit");
    return rollback_preserve_error(db, rc);
  }
  }

  rc = c_orm_transaction_commit(db);
  if (rc != C_ORM_OK)
    return rc;

  {
    LOG_DEBUG("c_orm_delete_batch: exit");
    return rc;
  }
}

/**
 * @brief Update an array of records in the database in bulk.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param in_array Pointer to an array of structs containing data to update.
 * @param num_items The total number of structs in the array.
 * @param chunk_size The number of structs to process per SQL query. 0 to
 * auto-calculate.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_update_batch(c_orm_db_t *db,
                                              const c_orm_table_meta_t *meta,
                                              const void *in_array,
                                              size_t num_items,
                                              size_t chunk_size) {
  c_orm_error_t rc;
  c_orm_error_t fin_rc;

  size_t i, j, k;
  size_t actual_chunk;
  const c_orm_column_meta_t *pk_col = NULL;

  LOG_DEBUG("c_orm_update_batch: entry");
  if (!db || !meta || !in_array) {
    LOG_DEBUG("c_orm_update_batch: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_update_batch: exit");
    return rc;
  }
  if (meta->is_view) {
    rc = C_ORM_ERROR_READ_ONLY;
    LOG_DEBUG("c_orm_update_batch: exit");
    return rc;
  }
  if (num_items == 0) {
    rc = C_ORM_OK;
    LOG_DEBUG("c_orm_update_batch: exit");
    return rc;
  }

  for (i = 0; i < meta->num_columns; i++) {
    if (meta->columns[i].is_pk) {
      pk_col = &meta->columns[i];
      break;
    }
  }

  if (!pk_col) {
    rc = C_ORM_ERROR_UNKNOWN;
    LOG_DEBUG("c_orm_update_batch: exit");
    return rc;
  }

  /* CASE WHEN generates 2 params per column per row, plus 1 for IN clause.
     Total params = chunk_size * ( (num_cols - 1)*2 + 1 )
     Keep total params under 30000 */
  if (chunk_size == 0) {
    size_t params_per_row = (meta->num_columns - 1) * 2 + 1;
    chunk_size = 30000 / params_per_row;
    if (chunk_size > 500)
      chunk_size = 500;
  }

  rc = c_orm_transaction_begin(db);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_update_batch: exit");
    return rc;
  }

  for (i = 0; i < num_items; i += actual_chunk) {
    c_orm_query_t *query;
    c_orm_string_builder_t *sb;
    const char *sql_str;
    int has_row;
    int bind_idx = 1;
    int first_col = 1;
    size_t ph_idx = 1U;

    actual_chunk = chunk_size;
    if (i + actual_chunk > num_items) {
      actual_chunk = num_items - i;
    }

    rc = c_orm_string_builder_init(&sb);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_update_batch: OOM");
      LOG_DEBUG("c_orm_update_batch: exit");
      return rollback_preserve_error(db, rc);
    }

    rc = c_orm_string_builder_append(sb, "UPDATE ");
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rollback_preserve_error(db, rc);
    }
    rc = c_orm_append_dialect_table(sb, db, meta->name);
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rollback_preserve_error(db, rc);
    }
    rc = c_orm_string_builder_append(sb, " SET ");
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rollback_preserve_error(db, rc);
    }

    for (k = 0; k < meta->num_columns; k++) {
      if (meta->columns[k].is_pk)
        continue;

      if (!first_col) {
        rc = c_orm_string_builder_append(sb, ", ");
        if (rc != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rollback_preserve_error(db, rc);
        }
      }
      first_col = 0;

      rc = c_orm_append_dialect_col(sb, db, meta->columns[k].name);
      if (rc != C_ORM_OK) {
        c_orm_string_builder_free(sb);
        return rollback_preserve_error(db, rc);
      }
      rc = c_orm_string_builder_append(sb, " = CASE ");
      if (rc != C_ORM_OK) {
        c_orm_string_builder_free(sb);
        return rollback_preserve_error(db, rc);
      }
      rc = c_orm_append_dialect_col(sb, db, pk_col->name);
      if (rc != C_ORM_OK) {
        c_orm_string_builder_free(sb);
        return rollback_preserve_error(db, rc);
      }

      for (j = 0; j < actual_chunk; j++) {
        rc = c_orm_append_dialect_when_then(sb, db, &ph_idx);
        if (rc != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rollback_preserve_error(db, rc);
        }
      }
      rc = c_orm_string_builder_append(sb, " END");
      if (rc != C_ORM_OK) {
        c_orm_string_builder_free(sb);
        return rollback_preserve_error(db, rc);
      }
    }

    rc = c_orm_string_builder_append(sb, " WHERE ");
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rollback_preserve_error(db, rc);
    }
    rc = c_orm_append_dialect_col(sb, db, pk_col->name);
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rollback_preserve_error(db, rc);
    }
    rc = c_orm_string_builder_append(sb, " IN (");
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rollback_preserve_error(db, rc);
    }

    for (j = 0; j < actual_chunk; j++) {
      if (j > 0) {
        rc = c_orm_string_builder_append(sb, ", ");
        if (rc != C_ORM_OK) {
          c_orm_string_builder_free(sb);
          return rollback_preserve_error(db, rc);
        }
      }
      rc = c_orm_append_placeholder(
          sb,
          (db->driver_name && strcmp(db->driver_name, "postgres") == 0) ? 1 : 0,
          ph_idx++);
      if (rc != C_ORM_OK) {
        c_orm_string_builder_free(sb);
        return rollback_preserve_error(db, rc);
      }
    }
    rc = c_orm_string_builder_append(sb, ")");
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rollback_preserve_error(db, rc);
    }

    if ((rc = c_orm_string_builder_get(sb, &sql_str)) != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rollback_preserve_error(db, rc);
    }

    rc = db->vtable->prepare(db, sql_str, &query);
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      LOG_DEBUG("c_orm_update_batch: exit");
      return rollback_preserve_error(db, rc);
    }
    c_orm_string_builder_free(sb);

    /* Bind SET CASE WHEN params */
    for (k = 0; k < meta->num_columns; k++) {
      if (meta->columns[k].is_pk)
        continue;

      for (j = 0; j < actual_chunk; j++) {
        const void *current_struct =
            (const char *)in_array + ((i + j) * meta->struct_size);
        const void *pk_ptr = (const char *)current_struct + pk_col->offset;

        /* Bind PK for WHEN */
        rc = bind_pk_scalar(db, query, bind_idx++, pk_col, pk_ptr);
        if (rc != C_ORM_OK)
          goto update_err;

        /* Bind Field for THEN */
        {
          /* Temporary struct meta wrapper to reuse bind_row */
          c_orm_table_meta_t temp_meta = *meta;
          c_orm_column_meta_t temp_col = meta->columns[k];
          temp_meta.columns = &temp_col;
          temp_meta.num_columns = 1;
          rc = bind_row(db, query, &temp_meta, current_struct, 0, &bind_idx);
          if (rc != C_ORM_OK)
            goto update_err;
        }
      }
    }

    /* Bind WHERE IN params */
    for (j = 0; j < actual_chunk; j++) {
      const void *current_struct =
          (const char *)in_array + ((i + j) * meta->struct_size);
      const void *pk_ptr = (const char *)current_struct + pk_col->offset;

      rc = bind_pk_scalar(db, query, bind_idx++, pk_col, pk_ptr);
      if (rc != C_ORM_OK)
        goto update_err;
    }

    rc = db->vtable->step(query, &has_row);
    if (rc != C_ORM_OK) {
      goto update_err;
    }
    fin_rc = db->vtable->finalize(query);
    if (fin_rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_update_batch: exit");
      return rollback_preserve_error(db, fin_rc);
    }

    for (j = 0; j < actual_chunk; j++) {
      const void *current_struct =
          (const char *)in_array + ((i + j) * meta->struct_size);
      if (meta->hooks[C_ORM_HOOK_AFTER_UPDATE]) {
        rc = meta->hooks[C_ORM_HOOK_AFTER_UPDATE](
            (void *)(size_t)current_struct, db);
        if (rc != C_ORM_OK) {
          LOG_DEBUG("c_orm_update_batch: exit");
          return rollback_preserve_error(db, rc);
        }
      }
      if (meta->hooks[C_ORM_HOOK_AFTER_SAVE]) {
        rc = meta->hooks[C_ORM_HOOK_AFTER_SAVE]((void *)(size_t)current_struct,
                                                db);
        if (rc != C_ORM_OK) {
          LOG_DEBUG("c_orm_update_batch: exit");
          return rollback_preserve_error(db, rc);
        }
      }
    }
    continue;

  update_err: {
    fin_rc = db->vtable->finalize(query);
    if (fin_rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_update_batch: exit");
      return rollback_preserve_error(db, fin_rc);
    }
    LOG_DEBUG("c_orm_update_batch: exit");
    return rollback_preserve_error(db, rc);
  }
  }

  rc = c_orm_transaction_commit(db);
  if (rc != C_ORM_OK)
    return rc;

  {
    LOG_DEBUG("c_orm_update_batch: exit");
    return rc;
  }
}

/**
 * @brief Delete a record from the database using a struct instance.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param in_struct Pointer to the struct containing data to delete.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_delete(c_orm_db_t *db,
                                        const c_orm_table_meta_t *meta,
                                        const void *in_struct) {
  c_orm_error_t rc;

  c_orm_query_t *query;
  int has_row;
  int bind_idx = 1;
  size_t i;
  int is_pk_found = 0;

  LOG_DEBUG("c_orm_delete: entry");
  if (!db || !meta || !in_struct) {
    LOG_DEBUG("c_orm_delete: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_delete: exit");
    return rc;
  }
  if (!meta->query_delete_by_pk) {
    rc = C_ORM_ERROR_UNKNOWN;
    LOG_DEBUG("c_orm_delete: exit");
    return rc;
  }
  if (meta->is_view) {
    rc = C_ORM_ERROR_READ_ONLY;
    LOG_DEBUG("c_orm_delete: exit");
    return rc;
  }

  db->active_hook_meta = meta;
  if (meta->hooks[C_ORM_HOOK_BEFORE_DELETE]) {
    rc = meta->hooks[C_ORM_HOOK_BEFORE_DELETE]((void *)(size_t)in_struct, db);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_delete: exit");
      return rc;
    }
    if (meta->hooks[C_ORM_HOOK_BEFORE_DELETE] ==
        c_orm_soft_delete_before_hook) {
      if (meta->hooks[C_ORM_HOOK_AFTER_DELETE]) {
        return meta->hooks[C_ORM_HOOK_AFTER_DELETE]((void *)(size_t)in_struct,
                                                    db);
      }
      return C_ORM_OK;
    }
  }

  /* Handle cascade delete before deleting the parent to avoid FK constraints */
  for (i = 0; i < meta->num_relations; ++i) {
    const c_orm_relation_meta_t *rel = &meta->relations[i];
    if (!rel->target_meta)
      continue;
    if (rel->type == C_ORM_RELATION_ONE_TO_ONE ||
        rel->type == C_ORM_RELATION_ONE_TO_MANY ||
        rel->type == C_ORM_RELATION_BELONGS_TO) {
      if (rel->on_delete == C_ORM_CASCADE_DELETE ||
          rel->on_delete == C_ORM_CASCADE_SET_NULL) {
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
            rc = C_ORM_SPRINTF(sql, sizeof(sql), "DELETE FROM %s WHERE %s = ?",
                               rel->target_meta->name, rel->foreign_key);
            if (rc != C_ORM_OK)
              return rc;
          } else {
            rc = C_ORM_SPRINTF(
                sql, sizeof(sql), "UPDATE %s SET %s = NULL WHERE %s = ?",
                rel->target_meta->name, rel->foreign_key, rel->foreign_key);
            if (rc != C_ORM_OK)
              return rc;
          }
          rc = exec_cascade_sql(db, sql, pk_col, field_ptr);
          if (rc != C_ORM_OK)
            return rc;
        }
      }
    } else if (rel->type == C_ORM_RELATION_MANY_TO_MANY) {
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
          rc = C_ORM_SPRINTF(
              sql, sizeof(sql),
              "DELETE FROM %s WHERE %s IN (SELECT %s FROM %s WHERE %s = ?)",
              rel->target_meta->name, rel->foreign_key, rel->join_foreign_key,
              rel->join_table, rel->join_local_key);
          if (rc != C_ORM_OK)
            return rc;
          rc = exec_cascade_sql(db, sql, pk_col, field_ptr);
          if (rc != C_ORM_OK)
            return rc;
        }
        rc = C_ORM_SPRINTF(sql, sizeof(sql), "DELETE FROM %s WHERE %s = ?",
                           rel->join_table, rel->join_local_key);
        if (rc != C_ORM_OK)
          return rc;
        rc = exec_cascade_sql(db, sql, pk_col, field_ptr);
        if (rc != C_ORM_OK)
          return rc;
      }
    }
  }

  rc = c_orm_prepare_cached(db, meta->query_delete_by_pk, &query);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_delete: exit");
    return rc;
  }

  /* Find PK to bind to WHERE clause */
  for (i = 0; i < meta->num_columns; ++i) {
    if (meta->columns[i].is_pk) {
      const void *field_ptr = (const char *)in_struct + meta->columns[i].offset;
      rc = bind_pk_scalar(db, query, bind_idx, &meta->columns[i], field_ptr);
      if (rc != C_ORM_OK) {
        c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
        if (fin_rc != C_ORM_OK)
          return fin_rc;
        LOG_DEBUG("c_orm_delete: exit");
        return rc;
      }
      is_pk_found++;
      bind_idx++;
    }
  }

  if (!is_pk_found) {
    rc = c_orm_finalize_cached(db, query);
    if (rc != C_ORM_OK)
      return rc;
    LOG_DEBUG("c_orm_delete: exit");
    return C_ORM_ERROR_UNKNOWN;
  }

  rc = db->vtable->step(query, &has_row);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    return rc;
  }
  rc = c_orm_finalize_cached(db, query);
  if (rc != C_ORM_OK)
    return rc;

  if (meta->hooks[C_ORM_HOOK_AFTER_DELETE]) {
    rc = meta->hooks[C_ORM_HOOK_AFTER_DELETE]((void *)(size_t)in_struct, db);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_delete: exit");
      return rc;
    }
  }
  {
    LOG_DEBUG("c_orm_delete: exit");
    return rc;
  }
}

/**
 * @brief Delete a record from the database by its primary key.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param id_val Primary key value to delete.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_delete_by_id_int32(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, int32_t id_val) {
  c_orm_error_t rc;

  c_orm_query_t *query;
  int has_row;

  LOG_DEBUG("c_orm_delete_by_id_int32: entry");
  if (!db || !meta) {
    LOG_DEBUG("c_orm_delete_by_id_int32: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_delete_by_id_int32: exit");
    return rc;
  }
  if (!meta->query_delete_by_pk) {
    rc = C_ORM_ERROR_UNKNOWN;
    LOG_DEBUG("c_orm_delete_by_id_int32: exit");
    return rc;
  }
  if (meta->is_view) {
    rc = C_ORM_ERROR_READ_ONLY;
    LOG_DEBUG("c_orm_delete_by_id_int32: exit");
    return rc;
  }

  /* Step 106: c_orm_delete_by_id_int32 lacks the struct pointer to trigger
     BEFORE_DELETE/AFTER_DELETE hooks safely. Users must rely on cascading
     constraints or manual fetches to trigger struct-level hooks prior to
     delete. */

  rc = c_orm_prepare_cached(db, meta->query_delete_by_pk, &query);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_delete_by_id_int32: exit");
    return rc;
  }

  rc = db->vtable->bind_int32(query, 1, id_val);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    LOG_DEBUG("c_orm_delete_by_id_int32: exit");
    return rc;
  }

  rc = db->vtable->step(query, &has_row);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    return rc;
  }
  LOG_DEBUG("c_orm_delete_by_id_int32: exit");
  return c_orm_finalize_cached(db, query);
}

/**
 * @brief Execute a raw query string that returns no results.
 *
 * @param db Database connection.
 * @param sql Raw SQL statement string.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_execute_raw(c_orm_db_t *db, const char *sql) {
  c_orm_error_t rc;

  c_orm_query_t *query;
  int has_row;

  LOG_DEBUG("c_orm_execute_raw: entry");
  if (!db || !sql) {
    LOG_DEBUG("c_orm_execute_raw: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_execute_raw: exit");
    return rc;
  }

  if (db->query_interceptor) {
    rc = db->query_interceptor(db, sql, db->query_interceptor_ctx);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_execute_raw: aborted by query_interceptor");
      return rc;
    }
  }

  rc = c_orm_prepare_cached(db, sql, &query);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_execute_raw: exit");
    return rc;
  }

  rc = db->vtable->step(query, &has_row);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    return rc;
  }
  LOG_DEBUG("c_orm_execute_raw: exit");
  return c_orm_finalize_cached(db, query);
}

C_ORM_EXPORT c_orm_error_t c_orm_connect_url(const char *url,
                                             c_orm_db_t **out_db) {
  if (!url || !out_db) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  *out_db = NULL;

  if (strncmp(url, "postgres://", 11) == 0 ||
      strncmp(url, "postgresql://", 13) == 0) {
    return c_orm_postgres_connect(url, out_db);
  }
  if (strncmp(url, "mysql://", 8) == 0 || strncmp(url, "mariadb://", 10) == 0) {
    return c_orm_mysql_connect(url, out_db);
  }
  if (strncmp(url, "memory://", 9) == 0) {
    return c_orm_memory_connect(url, out_db);
  }
  if (strncmp(url, "odbc://", 7) == 0 || strncmp(url, "ODBC://", 7) == 0 ||
      strncmp(url, "DRIVER=", 7) == 0 || strncmp(url, "driver=", 7) == 0 ||
      strncmp(url, "DSN=", 4) == 0 || strncmp(url, "dsn=", 4) == 0) {
    return c_orm_odbc_connect(url, out_db);
  }
  if (strncmp(url, "db2://", 6) == 0 || strncmp(url, "DB2://", 6) == 0) {
    return c_orm_db2_connect(url, out_db);
  }
  if (strncmp(url, "sqlite://", 9) == 0) {
    return c_orm_sqlite_connect(url + 9, out_db);
  }
  return c_orm_sqlite_connect(url, out_db);
}

/**
 * @brief Begin a database transaction.
 *
 * @param db Database connection.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_transaction_begin(c_orm_db_t *db) {
  c_orm_error_t rc;

  {
    LOG_DEBUG("c_orm_transaction_begin: entry");
    if (db && db->driver_name &&
        (strcmp(db->driver_name, "odbc") == 0 ||
         strcmp(db->driver_name, "db2") == 0)) {
      return c_orm_odbc_begin_transaction(db);
    }
    rc = c_orm_execute_raw(db, "BEGIN");
    if (rc != C_ORM_OK)
      return rc;

    LOG_DEBUG("c_orm_transaction_begin: exit");
    return rc;
  }
}

/**
 * @brief Commit an active database transaction.
 *
 * @param db Database connection.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_transaction_commit(c_orm_db_t *db) {
  c_orm_error_t rc;

  {
    LOG_DEBUG("c_orm_transaction_commit: entry");
    if (db && db->driver_name &&
        (strcmp(db->driver_name, "odbc") == 0 ||
         strcmp(db->driver_name, "db2") == 0)) {
      return c_orm_odbc_commit(db);
    }
    rc = c_orm_execute_raw(db, "COMMIT");
    if (rc != C_ORM_OK)
      return rc;

    LOG_DEBUG("c_orm_transaction_commit: exit");
    return rc;
  }
}

/**
 * @brief Rollback an active database transaction.
 *
 * @param db Database connection.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_transaction_rollback(c_orm_db_t *db) {
  c_orm_error_t rc;

  {
    LOG_DEBUG("c_orm_transaction_rollback: entry");
    if (db && db->driver_name &&
        (strcmp(db->driver_name, "odbc") == 0 ||
         strcmp(db->driver_name, "db2") == 0)) {
      return c_orm_odbc_rollback(db);
    }
    rc = c_orm_execute_raw(db, "ROLLBACK");
    if (rc != C_ORM_OK)
      return rc;

    LOG_DEBUG("c_orm_transaction_rollback: exit");
    return rc;
  }
}

/**
 * @brief Create a savepoint within an active transaction.
 *
 * @param db Database connection.
 * @param savepoint_name Name of the savepoint.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_savepoint_create(c_orm_db_t *db,
                                                  const char *savepoint_name) {
  c_orm_error_t rc;

  char sql[256];
  LOG_DEBUG("c_orm_savepoint_create: entry");
  if (!db || !savepoint_name) {
    LOG_DEBUG("c_orm_savepoint_create: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_savepoint_create: exit");
    return rc;
  }
  if (db->driver_name && strcmp(db->driver_name, "db2") == 0) {
    return c_orm_db2_savepoint_create(db, savepoint_name);
  }
  rc = C_ORM_SPRINTF(sql, sizeof(sql), "SAVEPOINT %s", savepoint_name);
  if (rc != C_ORM_OK)
    return rc;
  {
    rc = c_orm_execute_raw(db, sql);
    if (rc != C_ORM_OK)
      return rc;

    LOG_DEBUG("c_orm_savepoint_create: exit");
    return rc;
  }
}

/**
 * @brief Rollback to a specific savepoint.
 *
 * @param db Database connection.
 * @param savepoint_name Name of the savepoint.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_savepoint_rollback(c_orm_db_t *db, const char *savepoint_name) {
  c_orm_error_t rc;

  char sql[256];
  LOG_DEBUG("c_orm_savepoint_rollback: entry");
  if (!db || !savepoint_name) {
    LOG_DEBUG("c_orm_savepoint_rollback: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_savepoint_rollback: exit");
    return rc;
  }
  rc = C_ORM_SPRINTF(sql, sizeof(sql), "ROLLBACK TO SAVEPOINT %s",
                     savepoint_name);
  if (rc != C_ORM_OK)
    return rc;
  {
    rc = c_orm_execute_raw(db, sql);
    if (rc != C_ORM_OK)
      return rc;

    LOG_DEBUG("c_orm_savepoint_rollback: exit");
    return rc;
  }
}

/**
 * @brief Release a savepoint.
 *
 * @param db Database connection.
 * @param savepoint_name Name of the savepoint.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_savepoint_release(c_orm_db_t *db,
                                                   const char *savepoint_name) {
  c_orm_error_t rc;

  char sql[256];
  LOG_DEBUG("c_orm_savepoint_release: entry");
  if (!db || !savepoint_name) {
    LOG_DEBUG("c_orm_savepoint_release: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_savepoint_release: exit");
    return rc;
  }
  rc = C_ORM_SPRINTF(sql, sizeof(sql), "RELEASE SAVEPOINT %s", savepoint_name);
  if (rc != C_ORM_OK)
    return rc;
  {
    rc = c_orm_execute_raw(db, sql);
    if (rc != C_ORM_OK)
      return rc;

    LOG_DEBUG("c_orm_savepoint_release: exit");
    return rc;
  }
}

/**
 * @brief Find a single record by its string primary key.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param id_val Primary key value to search for.
 * @param out_struct Pointer to an already allocated struct to hydrate.
 * @return C_ORM_OK on success, C_ORM_ERROR_NOT_FOUND if no row.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_find_by_id_string(c_orm_db_t *db, const c_orm_table_meta_t *meta,
                        const char *id_val, void *out_struct) {
  c_orm_error_t rc;

  c_orm_query_t *query;
  int has_row;

  LOG_DEBUG("c_orm_find_by_id_string: entry");
  if (!db || !meta || !id_val || !out_struct) {
    LOG_DEBUG("c_orm_find_by_id_string: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_find_by_id_string: exit");
    return rc;
  }

  if (!meta->query_select_by_pk) {
    {
      rc = C_ORM_ERROR_UNKNOWN;
      LOG_DEBUG("c_orm_find_by_id_string: exit");
      return rc;
    }
  }

  rc = c_orm_prepare_cached(db, meta->query_select_by_pk, &query);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_find_by_id_string: exit");
    return rc;
  }

  rc = db->vtable->bind_string(query, 1, id_val);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    LOG_DEBUG("c_orm_find_by_id_string: exit");
    return rc;
  }

  rc = db->vtable->step(query, &has_row);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    LOG_DEBUG("c_orm_find_by_id_string: exit");
    return rc;
  }

  if (!has_row) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    LOG_DEBUG("c_orm_find_by_id_string: exit");
    return C_ORM_ERROR_NOT_FOUND;
  }

  rc = c_orm_hydrate_row(db, query, meta, out_struct);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    return rc;
  }
  LOG_DEBUG("c_orm_find_by_id_string: exit");
  return c_orm_finalize_cached(db, query);
}

/**
 * @brief Implement pessimistic locking APIs (SELECT ... FOR UPDATE) for integer
 * PK.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param id_val Primary key value.
 * @param out_struct Output struct.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_find_for_update_by_id_int32(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, int32_t id_val,
    void *out_struct) {
  c_orm_error_t rc;
  c_orm_query_t *query;
  int has_row;

  LOG_DEBUG("c_orm_find_for_update_by_id_int32: entry");
  if (!db || !meta || !out_struct) {
    LOG_DEBUG("c_orm_find_for_update_by_id_int32: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_find_for_update_by_id_int32: exit");
    return rc;
  }

  if (!meta->query_select_by_pk_for_update) {
    LOG_DEBUG("c_orm_find_for_update_by_id_int32: exit");
    return c_orm_find_by_id_int32(db, meta, id_val, out_struct);
  }

  rc = c_orm_prepare_cached(db, meta->query_select_by_pk_for_update, &query);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_find_for_update_by_id_int32: exit");
    return rc;
  }

  rc = db->vtable->bind_int32(query, 1, id_val);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    LOG_DEBUG("c_orm_find_for_update_by_id_int32: exit");
    return rc;
  }

  rc = db->vtable->step(query, &has_row);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    LOG_DEBUG("c_orm_find_for_update_by_id_int32: exit");
    return rc;
  }

  if (!has_row) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    LOG_DEBUG("c_orm_find_for_update_by_id_int32: exit");
    return C_ORM_ERROR_NOT_FOUND;
  }

  rc = c_orm_hydrate_row(db, query, meta, out_struct);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    return rc;
  }
  LOG_DEBUG("c_orm_find_for_update_by_id_int32: exit");
  return c_orm_finalize_cached(db, query);
}

/**
 * @brief Find a single row by string primary key and apply row-level locking.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param id_val Primary key value.
 * @param out_struct Output struct.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_find_for_update_by_id_string(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, const char *id_val,
    void *out_struct) {
  c_orm_error_t rc;
  c_orm_query_t *query;
  int has_row;

  LOG_DEBUG("c_orm_find_for_update_by_id_string: entry");
  if (!db || !meta || !id_val || !out_struct) {
    LOG_DEBUG("c_orm_find_for_update_by_id_string: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_find_for_update_by_id_string: exit");
    return rc;
  }

  if (!meta->query_select_by_pk_for_update) {
    LOG_DEBUG("c_orm_find_for_update_by_id_string: exit");
    return c_orm_find_by_id_string(db, meta, id_val, out_struct);
  }

  rc = c_orm_prepare_cached(db, meta->query_select_by_pk_for_update, &query);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_find_for_update_by_id_string: exit");
    return rc;
  }

  rc = db->vtable->bind_string(query, 1, id_val);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    LOG_DEBUG("c_orm_find_for_update_by_id_string: exit");
    return rc;
  }

  rc = db->vtable->step(query, &has_row);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    LOG_DEBUG("c_orm_find_for_update_by_id_string: exit");
    return rc;
  }

  if (!has_row) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    LOG_DEBUG("c_orm_find_for_update_by_id_string: exit");
    return C_ORM_ERROR_NOT_FOUND;
  }

  rc = c_orm_hydrate_row(db, query, meta, out_struct);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    return rc;
  }
  LOG_DEBUG("c_orm_find_for_update_by_id_string: exit");
  return c_orm_finalize_cached(db, query);
}

/**
 * @brief Delete a record from the database by its string primary key.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param id_val Primary key string to delete.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_delete_by_id_string(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, const char *id_val) {
  c_orm_error_t rc;
  c_orm_query_t *query;
  int has_row;

  LOG_DEBUG("c_orm_delete_by_id_string: entry");
  if (!db || !meta || !id_val) {
    LOG_DEBUG("c_orm_delete_by_id_string: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_delete_by_id_string: exit");
    return rc;
  }
  if (!meta->query_delete_by_pk) {
    rc = C_ORM_ERROR_UNKNOWN;
    LOG_DEBUG("c_orm_delete_by_id_string: exit");
    return rc;
  }

  rc = c_orm_prepare_cached(db, meta->query_delete_by_pk, &query);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_delete_by_id_string: exit");
    return rc;
  }

  rc = db->vtable->bind_string(query, 1, id_val);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    LOG_DEBUG("c_orm_delete_by_id_string: exit");
    return rc;
  }

  rc = db->vtable->step(query, &has_row);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    return rc;
  }
  LOG_DEBUG("c_orm_delete_by_id_string: exit");
  return c_orm_finalize_cached(db, query);
}

/**
 * @brief Find a single record by a specific string column.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param column_name The column to filter by.
 * @param value The value to search for.
 * @param out_struct Pointer to an already allocated struct to hydrate.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_find_one_by_string(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, const char *column_name,
    const char *value, void *out_struct) {
  c_orm_error_t rc;
  c_orm_select_builder_t *builder;
  char *sql;
  c_orm_query_t *query;
  int has_row;

  LOG_DEBUG("c_orm_find_one_by_string: entry");
  if (!db || !meta || !column_name || !value || !out_struct) {
    LOG_DEBUG("c_orm_find_one_by_string: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_find_one_by_string: exit");
    return rc;
  }

  rc = c_orm_select_builder_init(meta, &builder);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_find_one_by_string: exit");
    return rc;
  }

  rc = c_orm_select_where_eq(builder, column_name);
  if (rc != C_ORM_OK) {
    c_orm_select_builder_free(builder);
    LOG_DEBUG("c_orm_find_one_by_string: exit");
    return rc;
  }
  rc = c_orm_select_limit(builder, 1);
  if (rc != C_ORM_OK) {
    c_orm_select_builder_free(builder);
    LOG_DEBUG("c_orm_find_one_by_string: exit");
    return rc;
  }

  rc = c_orm_select_builder_compile(builder, &sql);
  if (rc != C_ORM_OK) {
    c_orm_select_builder_free(builder);
    LOG_DEBUG("c_orm_find_one_by_string: exit");
    return rc;
  }
  c_orm_select_builder_free(builder);

  rc = c_orm_prepare_cached(db, sql, &query);
  if (rc != C_ORM_OK) {
    c_orm_system_free(sql);
    LOG_DEBUG("c_orm_find_one_by_string: exit");
    return rc;
  }
  c_orm_system_free(sql);

  rc = db->vtable->bind_string(query, 1, value);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    LOG_DEBUG("c_orm_find_one_by_string: exit");
    return rc;
  }

  rc = db->vtable->step(query, &has_row);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    LOG_DEBUG("c_orm_find_one_by_string: exit");
    return rc;
  }

  if (!has_row) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    LOG_DEBUG("c_orm_find_one_by_string: exit");
    return C_ORM_ERROR_NOT_FOUND;
  }

  rc = c_orm_hydrate_row(db, query, meta, out_struct);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    return rc;
  }
  LOG_DEBUG("c_orm_find_one_by_string: exit");
  return c_orm_finalize_cached(db, query);
}

/**
 * @brief DFS cycle detection for relation validation.
 * @param tables Array of table metadata pointers.
 * @param num_tables Total number of tables in the array.
 * @param current Current table metadata being inspected.
 * @param visited Array tracking visited tables.
 * @return C_ORM_OK if valid, or error code if circular dependency found.
 */
static c_orm_error_t c_orm_dfs_validate_table(const c_orm_table_meta_t **tables,
                                              size_t num_tables,
                                              const c_orm_table_meta_t *current,
                                              int *visited) {
  c_orm_error_t rc;
  size_t current_idx = (size_t)-1;
  size_t i, j;
  LOG_DEBUG("c_orm_dfs_validate_table: entry");
  for (i = 0; i < num_tables; i++) {
    if (tables[i] == current) {
      current_idx = i;
      break;
    }
  }
  if (current_idx == (size_t)-1) {
    LOG_DEBUG("c_orm_dfs_validate_table: exit");
    return C_ORM_ERROR_NOT_FOUND;
  }
  if (visited[current_idx] == 1) {
    LOG_DEBUG("c_orm_dfs_validate_table: exit");
    return C_ORM_ERROR_RECURSION;
  }
  if (visited[current_idx] == 2) {
    LOG_DEBUG("c_orm_dfs_validate_table: exit");
    return C_ORM_OK;
  }
  visited[current_idx] = 1;
  for (i = 0; i < current->num_relations; i++) {
    const c_orm_table_meta_t *target = NULL;
    if (current->relations[i].target_table) {
      for (j = 0; j < num_tables; j++) {
        if (strcmp(tables[j]->name, current->relations[i].target_table) == 0) {
          target = tables[j];
          break;
        }
      }
    }
    if (!target && current->relations[i].target_meta) {
      target = current->relations[i].target_meta;
    }
    if (target) {
      rc = c_orm_dfs_validate_table(tables, num_tables, target, visited);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_dfs_validate_table: exit");
        return rc;
      }
    }
  }
  visited[current_idx] = 2;
  LOG_DEBUG("c_orm_dfs_validate_table: exit");
  return C_ORM_OK;
}

/**
 * @brief Implement runtime validation wrapping cdd-c dynamic validation rules
 * (Steps 154-156).
 *
 * @param meta Table metadata.
 * @param obj Struct instance to validate.
 * @return C_ORM_OK on success, C_ORM_ERROR_VALIDATION if validation fails.
 */
C_ORM_EXPORT c_orm_error_t c_orm_validate(const c_orm_table_meta_t *meta,
                                          const void *obj) {
  c_orm_error_t rc;
  size_t i, j;

  LOG_DEBUG("c_orm_validate: entry");
  if (!meta || !obj) {
    LOG_DEBUG("c_orm_validate: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_validate: exit");
    return rc;
  }

  for (i = 0; i < meta->num_relations; i++) {
    const c_orm_relation_meta_t *rel = &meta->relations[i];
    if (rel->type == C_ORM_RELATION_BELONGS_TO) {
      for (j = 0; j < meta->num_columns; j++) {
        if (strcmp(meta->columns[j].name, rel->foreign_key) == 0) {
          if (!meta->columns[j].is_nullable) {
            const void *rel_ptr = (const char *)obj + rel->struct_offset;
            const void *target_data_ptr =
                (const char *)rel_ptr + (rel->data_offset - rel->struct_offset);
            const void *data = *(const void *const *)target_data_ptr;

            if (api_calc_get_int_or_zero(meta, obj, rel->foreign_key) == 0 &&
                !data) {
              LOG_DEBUG("c_orm_validate: exit");
              return C_ORM_ERROR_VALIDATION;
            }
          }
          break;
        }
      }
    }
  }

  for (i = 0; i < meta->num_columns; ++i) {
    const c_orm_column_meta_t *col = &meta->columns[i];
    const void *field_ptr = (const char *)obj + col->offset;
    if (col->type == C_ORM_TYPE_DECIMAL || col->type == C_ORM_TYPE_DECFLOAT) {
      const c_orm_decimal_t *dptr = NULL;
      int is_z = 0;
      if (col->is_nullable) {
        dptr = *(const c_orm_decimal_t *const *)field_ptr;
      } else {
        dptr = (const c_orm_decimal_t *)field_ptr;
      }
      if (dptr) {
        rc = c_orm_decimal_is_zero(dptr, &is_z);
        if (rc != C_ORM_OK) {
          return rc;
        }
        if (col->precision > 0) {
          c_orm_decimal_t tmp;
          rc = c_orm_decimal_rescale(dptr, col->precision, col->scale,
                                     C_ORM_ROUND_TRUNCATE, &tmp);
          if (rc != C_ORM_OK) {
            return rc;
          }
        }
      }
    } else if (col->type == C_ORM_TYPE_CHAR_FIXED && col->fixed_length == 0 &&
               col->precision > 0) {
      const char *sval = *(const char *const *)field_ptr;
      if (sval && strlen(sval) > (size_t)col->precision) {
        return C_ORM_ERROR_TRUNCATION;
      }
    }
  }

  LOG_DEBUG("c_orm_validate: exit");
  return C_ORM_OK;
}

/**
 * @brief Implement validation logic for recursive relationship definitions
 * across multiple tables.
 *
 * @param tables Array of table metadata pointers.
 * @param num_tables Number of tables in the array.
 * @return C_ORM_OK on success, C_ORM_ERROR_RECURSION if a cyclic dependency is
 * detected.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_validate_relations(const c_orm_table_meta_t **tables, size_t num_tables) {
  c_orm_error_t rc;
  int *visited;
  size_t i;

  LOG_DEBUG("c_orm_validate_relations: entry");
  if (!tables || num_tables == 0) {
    LOG_DEBUG("c_orm_validate_relations: OOM");
    return C_ORM_ERROR_MEMORY;
  }

  visited = NULL;
  rc = c_orm_system_malloc(num_tables * sizeof(int), (void **)&visited);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_validate_relations: OOM");
    return rc;
  }

  for (i = 0; i < num_tables; i++) {
    visited[i] = 0;
  }

  for (i = 0; i < num_tables; i++) {
    if (visited[i] == 0) {
      rc = c_orm_dfs_validate_table(tables, num_tables, tables[i], visited);
      if (rc != C_ORM_OK) {
        c_orm_system_free(visited);
        LOG_DEBUG("c_orm_validate_relations: exit");
        return rc;
      }
    }
  }

  c_orm_system_free(visited);
  LOG_DEBUG("c_orm_validate_relations: exit");
  return C_ORM_OK;
}

#if defined(__GNUC__) || defined(__clang__)
#endif
#if defined(__GNUC__) || defined(__clang__)
#endif

/**
 * @brief Implement c_orm integration layer to parse FOREIGN KEY constraints
 * into c_orm_relation_meta_t via cdd-c AST.
 *
 * @param sql_table Parsed table AST from cdd-c.
 * @param out_relations Pointer to array of relation metadata to populate.
 * @param out_num_relations Number of relations found and populated.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_build_relation_meta(
    const struct sql_table_t *sql_table, c_orm_relation_meta_t **out_relations,
    size_t *out_num_relations) {
  c_orm_error_t rc;

  size_t total_fks = 0;
  size_t i, j, current_fk;
  c_orm_relation_meta_t *relations;

  LOG_DEBUG("c_orm_build_relation_meta: entry");
  if (!sql_table || !out_relations || !out_num_relations) {
    {
      LOG_DEBUG("c_orm_build_relation_meta: OOM");
      rc = C_ORM_ERROR_MEMORY;
      LOG_DEBUG("c_orm_build_relation_meta: exit");
      return rc;
    }
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
    {
      rc = C_ORM_OK;
      LOG_DEBUG("c_orm_build_relation_meta: exit");
      return rc;
    }
  }

  relations = NULL;
  rc = c_orm_system_malloc(total_fks * sizeof(c_orm_relation_meta_t),
                           (void **)&relations);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_build_relation_meta: OOM");
    LOG_DEBUG("c_orm_build_relation_meta: exit");
    return rc;
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

  /* Populate table-level foreign keys */
  for (i = 0; i < sql_table->n_table_constraints; i++) {
    if (sql_table->table_constraints[i].type == SQL_CONSTRAINT_FOREIGN_KEY) {
      const char *local_col = sql_table->table_constraints[i].column_name;
      if (!local_col && sql_table->table_constraints[i].n_columns > 0 &&
          sql_table->table_constraints[i].columns) {
        local_col = sql_table->table_constraints[i].columns[0];
      }
      if (!local_col) {
        local_col = sql_table->table_constraints[i].reference_column;
      }
      relations[current_fk].field_name = local_col;
      relations[current_fk].type = C_ORM_RELATION_ONE_TO_ONE;
      relations[current_fk].target_table =
          sql_table->table_constraints[i].reference_table;
      relations[current_fk].foreign_key =
          sql_table->table_constraints[i].reference_column;
      relations[current_fk].local_key = local_col;
      relations[current_fk].struct_offset = 0;
      relations[current_fk].target_array_len_offset = 0;
      relations[current_fk].target_ir = NULL;
      current_fk++;
    }
  }

  *out_relations = relations;
  *out_num_relations = total_fks;
  {
    rc = C_ORM_OK;
    LOG_DEBUG("c_orm_build_relation_meta: exit");
    return rc;
  }
}

/**
 * @brief Hydrates the current positioned row of `query` into an abstract
 * struct dictionary.
 * @param db Database handle.
 * @param query Positioned query handle.
 * @param out_row Output `cdd_c_abstract_struct_t` to initialize and populate.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t
c_orm_hydrate_abstract_row(c_orm_db_t *db, c_orm_query_t *query,
                           cdd_c_abstract_struct_t *out_row) {
  c_orm_error_t rc;
  int col_count = 0;
  int col_idx;

  if (db->vtable->get_column_count) {
    rc = db->vtable->get_column_count(query, &col_count);
    if (rc != C_ORM_OK) {
      return rc;
    }
  }
  out_row->kvs = NULL;
  out_row->count = 0;
  out_row->capacity = 0;

  for (col_idx = 0; col_idx < col_count; ++col_idx) {
    const char *col_name = NULL;
    int is_null = 0;
    cdd_c_variant_t var;

    if (db->vtable->get_column_name) {
      rc = db->vtable->get_column_name(query, col_idx, &col_name);
      if (rc != C_ORM_OK) {
        cdd_c_abstract_struct_free(out_row);
        return rc;
      }
    }
    if (!col_name) {
      col_name = "col";
    }

    rc = db->vtable->is_null(query, col_idx, &is_null);
    if (rc != C_ORM_OK) {
      cdd_c_abstract_struct_free(out_row);
      return rc;
    }

    if (is_null) {
      var.type = CDD_C_VARIANT_TYPE_NULL;
    } else {
      int64_t iv = 0;
      double dv = 0.0;
      const char *sv = NULL;
      c_orm_error_t grc_i = db->vtable->get_int64(query, col_idx, &iv);
      if (grc_i != C_ORM_OK) {
        c_orm_error_t grc_d;
        if (grc_i == C_ORM_ERROR_MEMORY) {
          cdd_c_abstract_struct_free(out_row);
          return grc_i;
        }
        grc_d = db->vtable->get_double(query, col_idx, &dv);
        if (grc_d != C_ORM_OK) {
          c_orm_error_t grc_s;
          if (grc_d == C_ORM_ERROR_MEMORY) {
            cdd_c_abstract_struct_free(out_row);
            return grc_d;
          }
          grc_s = db->vtable->get_string(query, col_idx, &sv);
          if (grc_s != C_ORM_OK) {
            if (grc_s == C_ORM_ERROR_MEMORY) {
              cdd_c_abstract_struct_free(out_row);
              return grc_s;
            }
            var.type = CDD_C_VARIANT_TYPE_NULL;
          } else if (sv) {
            var.type = CDD_C_VARIANT_TYPE_STRING;
            memcpy(&var.value.s_val, &sv, sizeof(char *));
          } else {
            var.type = CDD_C_VARIANT_TYPE_NULL;
          }
        } else {
          var.type = CDD_C_VARIANT_TYPE_FLOAT;
          var.value.f_val = dv;
        }
      } else {
        var.type = CDD_C_VARIANT_TYPE_INT;
        var.value.i_val = (c_orm_int64_t)iv;
      }
    }

    rc = cdd_c_abstract_set(out_row, col_name, &var);
    if (rc != C_ORM_OK) {
      cdd_c_abstract_struct_free(out_row);
      return rc;
    }
  }
  return C_ORM_OK;
}

/**
 * @brief Implement fallback routing to cdd_c_abstract_struct_t if specific
 * struct is absent
 *
 * @param db Database connection.
 * @param query Compiled query object.
 * @param out_array Pointer to cdd_c_abstract_struct_array_t to populate.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_hydrate_abstract_all(c_orm_db_t *db, c_orm_query_t *query,
                           struct CddCAbstractStructArray *out_array) {
  c_orm_error_t rc;
  cdd_c_abstract_struct_array_t *arr =
      (cdd_c_abstract_struct_array_t *)out_array;
  int has_row = 0;

  LOG_DEBUG("c_orm_hydrate_abstract_all: entry");
  if (!db || !db->vtable || !query || !arr) {
    LOG_DEBUG("c_orm_hydrate_abstract_all: invalid args");
    return C_ORM_ERROR_UNKNOWN;
  }

  if (!arr->items && arr->capacity == 0) {
    rc = cdd_c_abstract_struct_array_init(arr, 16);
    if (rc != C_ORM_OK) {
      return rc;
    }
  }

  for (;;) {
    cdd_c_abstract_struct_t row;

    rc = db->vtable->step(query, &has_row);
    if (rc != C_ORM_OK) {
      return rc;
    }
    if (!has_row) {
      break;
    }

    rc = c_orm_hydrate_abstract_row(db, query, &row);
    if (rc != C_ORM_OK) {
      return rc;
    }

    rc = cdd_c_abstract_struct_array_append(arr, &row);
    if (rc != C_ORM_OK) {
      cdd_c_abstract_struct_free(&row);
      return rc;
    }
  }

  LOG_DEBUG("c_orm_hydrate_abstract_all: exit");
  return C_ORM_OK;
}

/**
 * @brief Map custom SQL to an existing specific struct array.
 *
 * @param db Database connection.
 * @param sql Raw SQL query.
 * @param meta Table metadata.
 * @param out_array Pointer to generic struct array block.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_select_raw(c_orm_db_t *db, const char *sql,
                                            const c_orm_table_meta_t *meta,
                                            void *out_array) {
  c_orm_error_t rc;
  c_orm_error_t fin_rc;
  c_orm_query_t *query = NULL;

  LOG_DEBUG("c_orm_select_raw: entry");
  if (!db || !db->vtable || !sql || !meta || !out_array) {
    LOG_DEBUG("c_orm_select_raw: exit");
    return C_ORM_ERROR_UNKNOWN;
  }

  rc = c_orm_prepare_cached(db, sql, &query);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = c_orm_hydrate_all(db, query, meta, out_array);
  if (rc != C_ORM_OK) {
    fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK) {
      return fin_rc;
    }
    LOG_DEBUG("c_orm_select_raw: exit");
    return rc;
  }
  fin_rc = c_orm_finalize_cached(db, query);
  if (fin_rc != C_ORM_OK) {
    return fin_rc;
  }
  LOG_DEBUG("c_orm_select_raw: exit");
  return C_ORM_OK;
}

/**
 * @brief Execute query and return abstract dynamic rows.
 *
 * @param db Database connection.
 * @param sql Raw SQL query.
 * @param out_array Pointer to cdd_c_abstract_struct_array_t to populate.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_find_all_abstract(c_orm_db_t *db, const char *sql,
                        struct CddCAbstractStructArray *out_array) {
  c_orm_error_t rc;
  c_orm_error_t fin_rc;
  c_orm_query_t *query = NULL;

  LOG_DEBUG("c_orm_find_all_abstract: entry");
  if (!db || !db->vtable || !sql || !out_array) {
    LOG_DEBUG("c_orm_find_all_abstract: exit");
    return C_ORM_ERROR_UNKNOWN;
  }

  rc = c_orm_prepare_cached(db, sql, &query);
  if (rc != C_ORM_OK) {
    return rc;
  }

  rc = c_orm_hydrate_abstract_all(db, query, out_array);
  if (rc != C_ORM_OK) {
    fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK) {
      return fin_rc;
    }
    LOG_DEBUG("c_orm_find_all_abstract: exit");
    return rc;
  }
  fin_rc = c_orm_finalize_cached(db, query);
  if (fin_rc != C_ORM_OK) {
    return fin_rc;
  }
  LOG_DEBUG("c_orm_find_all_abstract: exit");
  return C_ORM_OK;
}

/**
 * @brief Free an abstract array generated by find_all_abstract.
 *
 * @param arr The array.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_abstract_free(struct CddCAbstractStructArray *arr) {
  LOG_DEBUG("c_orm_abstract_free: entry");
  if (!arr) {
    return C_ORM_OK;
  }
  return cdd_c_abstract_struct_array_free((cdd_c_abstract_struct_array_t *)arr);
}

/**
 * @brief Hydrates a query row via a registered `cdd_c_hydrate_router_t` or
 * falls back to `cdd_c_abstract_struct_t` when `query_hash == 0`.
 *
 * @param db Database connection.
 * @param query Compiled query object.
 * @param query_hash The query's hash for routing.
 * @param out_struct Pointer to allocated output struct or abstract struct.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_hydrate_routed(c_orm_db_t *db,
                                                c_orm_query_t *query,
                                                c_orm_uint64_t query_hash,
                                                void *out_struct) {
  c_orm_error_t rc;
  const cdd_c_hydrate_router_t *router;
  cdd_c_abstract_struct_t row;

  LOG_DEBUG("c_orm_hydrate_routed: entry");
  if (!db || !query || !out_struct) {
    return C_ORM_ERROR_MEMORY;
  }
  if (query_hash == 0) {
    return c_orm_hydrate_abstract_row(db, query,
                                      (struct CddCAbstractStruct *)out_struct);
  }
  router = (const cdd_c_hydrate_router_t *)db->hydrate_router;
  if (!router) {
    return C_ORM_ERROR_NOT_FOUND;
  }
  rc = c_orm_hydrate_abstract_row(db, query, &row);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = cdd_c_hydrate_router_dispatch(router, query_hash, &row, out_struct);
  if (rc != C_ORM_OK) {
    cdd_c_abstract_struct_free(&row);
    return rc;
  }
  cdd_c_abstract_struct_free(&row);
  LOG_DEBUG("c_orm_hydrate_routed: exit");
  return C_ORM_OK;
}

/**
 * @brief Implement mapping from abstract struct to JSON for dynamic use cases.
 *
 * @param astruct The abstract struct.
 * @param out_json Pointer to receive the allocated JSON string.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_abstract_to_json(
    const struct CddCAbstractStruct *astruct, char **out_json) {
  LOG_DEBUG("c_orm_abstract_to_json: entry");
  if (!astruct || !out_json) {
    return C_ORM_ERROR_MEMORY;
  }
  return cdd_c_abstract_struct_to_json(astruct, out_json);
}

/**
 * @brief Implement dynamic struct field accessor wrapping cdd-c reflection
 * (c_orm_get_field_value)
 *
 * @param meta Table metadata.
 * @param obj Struct instance.
 * @param field_name Name of the field.
 * @param out_variant The parsed value from the struct.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_get_field_value(const c_orm_table_meta_t *meta, const void *obj,
                      const char *field_name, struct CddCVariant *out_variant) {
  size_t i;
  const c_orm_column_meta_t *col = NULL;
  const void *field_ptr;
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_get_field_value: entry");
  if (!meta || !obj || !field_name || !out_variant) {
    return C_ORM_ERROR_MEMORY;
  }
  memset(out_variant, 0, sizeof(*out_variant));
  for (i = 0; i < meta->num_columns; ++i) {
    if (strcmp(meta->columns[i].name, field_name) == 0) {
      col = &meta->columns[i];
      break;
    }
  }
  if (!col) {
    return C_ORM_ERROR_NOT_FOUND;
  }

  field_ptr = (const char *)obj + col->offset;
  if (col->type == C_ORM_TYPE_CHAR_FIXED && col->fixed_length > 0) {
    out_variant->type = CDD_C_VARIANT_TYPE_STRING;
    return c_orm_strdup((const char *)field_ptr, &out_variant->value.s_val);
  }
  switch (col->type) {
  case C_ORM_TYPE_CHAR_FIXED:
  case C_ORM_TYPE_STRING:
  case C_ORM_TYPE_DATE:
  case C_ORM_TYPE_TIME:
  case C_ORM_TYPE_TIMESTAMP:
  case C_ORM_TYPE_ENUM:
  case C_ORM_TYPE_SET:
  case C_ORM_TYPE_JSON:
  case C_ORM_TYPE_GRAPHIC:
  case C_ORM_TYPE_VARGRAPHIC: {
    const char *s = *(const char *const *)field_ptr;
    if (!s) {
      out_variant->type = CDD_C_VARIANT_TYPE_NULL;
      return C_ORM_OK;
    }
    out_variant->type = CDD_C_VARIANT_TYPE_STRING;
    return c_orm_strdup(s, &out_variant->value.s_val);
  }
  case C_ORM_TYPE_BLOB:
  case C_ORM_TYPE_VARBINARY: {
    const c_orm_blob_t *b = (const c_orm_blob_t *)field_ptr;
    if (!b->data || b->size == 0) {
      out_variant->type = CDD_C_VARIANT_TYPE_NULL;
      return C_ORM_OK;
    }
    rc = c_orm_system_malloc(b->size, (void **)&out_variant->value.b_val.data);
    if (rc != C_ORM_OK) {
      return rc;
    }
    memcpy(out_variant->value.b_val.data, b->data, b->size);
    out_variant->value.b_val.size = b->size;
    out_variant->type = CDD_C_VARIANT_TYPE_BLOB;
    return C_ORM_OK;
  }
  default:
    break;
  }

  if (col->is_nullable) {
    const void *p = *(const void *const *)field_ptr;
    if (!p) {
      out_variant->type = CDD_C_VARIANT_TYPE_NULL;
      return C_ORM_OK;
    }
    field_ptr = p;
  }
  switch (col->type) {
  case C_ORM_TYPE_INT32:
    out_variant->type = CDD_C_VARIANT_TYPE_INT;
    out_variant->value.i_val = (c_orm_int64_t)(*(const int32_t *)field_ptr);
    return C_ORM_OK;
  case C_ORM_TYPE_INT64:
    out_variant->type = CDD_C_VARIANT_TYPE_INT;
    out_variant->value.i_val = *(const int64_t *)field_ptr;
    return C_ORM_OK;
  case C_ORM_TYPE_BOOL:
    out_variant->type = CDD_C_VARIANT_TYPE_INT;
    out_variant->value.i_val = (*(const unsigned char *)field_ptr) ? 1 : 0;
    return C_ORM_OK;
  case C_ORM_TYPE_FLOAT:
    out_variant->type = CDD_C_VARIANT_TYPE_FLOAT;
    out_variant->value.f_val = (double)(*(const float *)field_ptr);
    return C_ORM_OK;
  case C_ORM_TYPE_DOUBLE:
    out_variant->type = CDD_C_VARIANT_TYPE_FLOAT;
    out_variant->value.f_val = *(const double *)field_ptr;
    return C_ORM_OK;
  case C_ORM_TYPE_DECIMAL:
  case C_ORM_TYPE_DECFLOAT:
    out_variant->type = CDD_C_VARIANT_TYPE_DECIMAL;
    out_variant->value.d_val = *(const c_orm_decimal_t *)field_ptr;
    return C_ORM_OK;
  default:
    out_variant->type = CDD_C_VARIANT_TYPE_NULL;
    return C_ORM_OK;
  }
}

/**
 * @brief Implement dynamic struct field mutator wrapping cdd-c reflection
 * (c_orm_set_field_value)
 *
 * @param meta Table metadata.
 * @param obj Struct instance.
 * @param field_name Name of the field.
 * @param in_variant The parsed value to apply to the struct.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_set_field_value(
    const c_orm_table_meta_t *meta, void *obj, const char *field_name,
    const struct CddCVariant *in_variant) {
  size_t i;
  const c_orm_column_meta_t *col = NULL;
  void *field_ptr;
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_set_field_value: entry");
  if (!meta || !obj || !field_name || !in_variant) {
    return C_ORM_ERROR_MEMORY;
  }
  for (i = 0; i < meta->num_columns; ++i) {
    if (strcmp(meta->columns[i].name, field_name) == 0) {
      col = &meta->columns[i];
      break;
    }
  }
  if (!col) {
    return C_ORM_ERROR_NOT_FOUND;
  }

  field_ptr = (char *)obj + col->offset;
  if (col->type == C_ORM_TYPE_CHAR_FIXED && col->fixed_length > 0) {
    if (in_variant->type == CDD_C_VARIANT_TYPE_NULL) {
      ((char *)field_ptr)[0] = '\0';
      return C_ORM_OK;
    }
    if (in_variant->type != CDD_C_VARIANT_TYPE_STRING ||
        !in_variant->value.s_val) {
      return C_ORM_ERROR_TYPE_MISMATCH;
    }
    return C_ORM_STRNCPY((char *)field_ptr, col->fixed_length + 1,
                         in_variant->value.s_val, col->fixed_length);
  }
  switch (col->type) {
  case C_ORM_TYPE_CHAR_FIXED:
  case C_ORM_TYPE_STRING:
  case C_ORM_TYPE_DATE:
  case C_ORM_TYPE_TIME:
  case C_ORM_TYPE_TIMESTAMP:
  case C_ORM_TYPE_ENUM:
  case C_ORM_TYPE_SET:
  case C_ORM_TYPE_JSON:
  case C_ORM_TYPE_GRAPHIC:
  case C_ORM_TYPE_VARGRAPHIC: {
    char **sptr = (char **)field_ptr;
    char *dup = NULL;
    if (in_variant->type == CDD_C_VARIANT_TYPE_NULL) {
      if (*sptr) {
        c_orm_system_free(*sptr);
        *sptr = NULL;
      }
      return C_ORM_OK;
    }
    if (in_variant->type != CDD_C_VARIANT_TYPE_STRING ||
        !in_variant->value.s_val) {
      return C_ORM_ERROR_TYPE_MISMATCH;
    }
    rc = c_orm_strdup(in_variant->value.s_val, &dup);
    if (rc != C_ORM_OK) {
      return rc;
    }
    if (*sptr) {
      c_orm_system_free(*sptr);
    }
    *sptr = dup;
    return C_ORM_OK;
  }
  case C_ORM_TYPE_BLOB:
  case C_ORM_TYPE_VARBINARY: {
    c_orm_blob_t *b = (c_orm_blob_t *)field_ptr;
    void *nb = NULL;
    if (in_variant->type == CDD_C_VARIANT_TYPE_NULL) {
      if (b->data) {
        c_orm_system_free(b->data);
      }
      b->data = NULL;
      b->size = 0;
      return C_ORM_OK;
    }
    if (in_variant->type != CDD_C_VARIANT_TYPE_BLOB) {
      return C_ORM_ERROR_TYPE_MISMATCH;
    }
    if (in_variant->value.b_val.size > 0) {
      rc = c_orm_system_malloc(in_variant->value.b_val.size, &nb);
      if (rc != C_ORM_OK) {
        return rc;
      }
      memcpy(nb, in_variant->value.b_val.data, in_variant->value.b_val.size);
    }
    if (b->data) {
      c_orm_system_free(b->data);
    }
    b->data = nb;
    b->size = in_variant->value.b_val.size;
    return C_ORM_OK;
  }
  default:
    break;
  }
  if (col->is_nullable) {
    void **pptr = (void **)field_ptr;
    if (in_variant->type == CDD_C_VARIANT_TYPE_NULL) {
      if (*pptr) {
        c_orm_system_free(*pptr);
        *pptr = NULL;
      }
      return C_ORM_OK;
    }
    if (!*pptr) {
      rc = c_orm_system_calloc(
          1,
          sizeof(double) > sizeof(int64_t) ? sizeof(double) : sizeof(int64_t),
          pptr);
      if (rc != C_ORM_OK) {
        return rc;
      }
    }
    field_ptr = *pptr;
  } else if (in_variant->type == CDD_C_VARIANT_TYPE_NULL) {
    return C_ORM_OK;
  }

  switch (col->type) {
  case C_ORM_TYPE_INT32: {
    if (in_variant->type != CDD_C_VARIANT_TYPE_INT) {
      return C_ORM_ERROR_TYPE_MISMATCH;
    }
    *(int32_t *)field_ptr = (int32_t)in_variant->value.i_val;
    return C_ORM_OK;
  }
  case C_ORM_TYPE_INT64: {
    if (in_variant->type != CDD_C_VARIANT_TYPE_INT) {
      return C_ORM_ERROR_TYPE_MISMATCH;
    }
    *(int64_t *)field_ptr = (int64_t)in_variant->value.i_val;
    return C_ORM_OK;
  }
  case C_ORM_TYPE_BOOL: {
    if (in_variant->type != CDD_C_VARIANT_TYPE_INT) {
      return C_ORM_ERROR_TYPE_MISMATCH;
    }
    *(unsigned char *)field_ptr =
        (unsigned char)(in_variant->value.i_val != 0 ? 1 : 0);
    return C_ORM_OK;
  }
  case C_ORM_TYPE_FLOAT: {
    if (in_variant->type == CDD_C_VARIANT_TYPE_FLOAT) {
      *(float *)field_ptr = (float)in_variant->value.f_val;
    } else if (in_variant->type == CDD_C_VARIANT_TYPE_INT) {
      *(float *)field_ptr = (float)in_variant->value.i_val;
    } else {
      return C_ORM_ERROR_TYPE_MISMATCH;
    }
    return C_ORM_OK;
  }
  case C_ORM_TYPE_DOUBLE: {
    if (in_variant->type == CDD_C_VARIANT_TYPE_FLOAT) {
      *(double *)field_ptr = in_variant->value.f_val;
    } else if (in_variant->type == CDD_C_VARIANT_TYPE_INT) {
      *(double *)field_ptr = (double)in_variant->value.i_val;
    } else {
      return C_ORM_ERROR_TYPE_MISMATCH;
    }
    return C_ORM_OK;
  }
  case C_ORM_TYPE_DECIMAL:
  case C_ORM_TYPE_DECFLOAT: {
    if (in_variant->type == CDD_C_VARIANT_TYPE_DECIMAL) {
      *(c_orm_decimal_t *)field_ptr = in_variant->value.d_val;
      return C_ORM_OK;
    }
    if (in_variant->type == CDD_C_VARIANT_TYPE_STRING &&
        in_variant->value.s_val) {
      return c_orm_decimal_from_str(in_variant->value.s_val,
                                    (c_orm_decimal_t *)field_ptr);
    }
    return C_ORM_ERROR_TYPE_MISMATCH;
  }
  default: {
    return C_ORM_ERROR_TYPE_MISMATCH;
  }
  }
}

/**
 * @brief Implement mapping from JSON back to abstract struct for dynamic use
 * cases.
 *
 * @param json The JSON string.
 * @param out_astruct Pointer to the initialized abstract struct.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_abstract_from_json(
    const char *json, struct CddCAbstractStruct *out_astruct) {
  LOG_DEBUG("c_orm_abstract_from_json: entry");
  if (!json || !out_astruct) {
    return C_ORM_ERROR_MEMORY;
  }
  return cdd_c_abstract_struct_from_json(json, out_astruct);
}

/**
 * @brief Implement c_orm_to_json serializer handling specific structs (Step
 * 147).
 *
 * @param meta Table metadata.
 * @param obj Struct instance.
 * @param out_json Pointer to receive the allocated JSON string.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_to_json(const c_orm_table_meta_t *meta,
                                         const void *obj, char **out_json) {
  cdd_c_abstract_struct_t dict;
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_to_json: entry");
  if (!meta || !obj || !out_json) {
    return C_ORM_ERROR_MEMORY;
  }
  rc = c_orm_to_dict(meta, obj, &dict);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = c_orm_abstract_to_json(&dict, out_json);
  if (rc != C_ORM_OK) {
    cdd_c_abstract_struct_free(&dict);
    return rc;
  }
  cdd_c_abstract_struct_free(&dict);
  LOG_DEBUG("c_orm_to_json: exit");
  return C_ORM_OK;
}

/**
 * @brief Implement c_orm_from_json deserializer for specific structs (Step
 * 148).
 *
 * @param meta Table metadata.
 * @param json The JSON string.
 * @param out_obj Pointer to the uninitialized/allocated target struct.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_from_json(const c_orm_table_meta_t *meta,
                                           const char *json, void *out_obj) {
  cdd_c_abstract_struct_t dict;
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_from_json: entry");
  if (!meta || !json || !out_obj) {
    return C_ORM_ERROR_MEMORY;
  }
  rc = c_orm_abstract_from_json(json, &dict);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = c_orm_from_dict(meta, &dict, out_obj);
  if (rc != C_ORM_OK) {
    cdd_c_abstract_struct_free(&dict);
    return rc;
  }
  cdd_c_abstract_struct_free(&dict);
  LOG_DEBUG("c_orm_from_json: exit");
  return C_ORM_OK;
}

/**
 * @brief Implement c_orm_to_dict (hashmap) representation (Step 152).
 *
 * @param meta Table metadata.
 * @param obj Struct instance.
 * @param out_dict Pointer to the target abstract struct dictionary.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_to_dict(const c_orm_table_meta_t *meta,
                                         const void *obj,
                                         struct CddCAbstractStruct *out_dict) {
  size_t i;
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_to_dict: entry");
  if (!meta || !obj || !out_dict) {
    return C_ORM_ERROR_MEMORY;
  }
  out_dict->kvs = NULL;
  out_dict->count = 0;
  out_dict->capacity = 0;
  for (i = 0; i < meta->num_columns; ++i) {
    cdd_c_variant_t v;
    rc = c_orm_get_field_value(meta, obj, meta->columns[i].name, &v);
    if (rc != C_ORM_OK) {
      cdd_c_abstract_struct_free(out_dict);
      return rc;
    }
    rc = cdd_c_abstract_set(out_dict, meta->columns[i].name, &v);
    if (rc != C_ORM_OK) {
      cdd_c_variant_free(&v);
      cdd_c_abstract_struct_free(out_dict);
      return rc;
    }
    cdd_c_variant_free(&v);
  }
  LOG_DEBUG("c_orm_to_dict: exit");
  return C_ORM_OK;
}

/**
 * @brief Predicate wrapper looking up a key in an abstract dictionary.
 * @param in_dict Source abstract struct dictionary.
 * @param key Column name key.
 * @param out_v Output variant pointer.
 * @return 1 if key exists, 0 otherwise.
 */
static int api_calc_dict_get(const struct CddCAbstractStruct *in_dict,
                             const char *key, cdd_c_variant_t **out_v) {
  return (cdd_c_abstract_get(in_dict, key, out_v) == C_ORM_OK) ? 1 : 0;
}

/**
 * @brief Implement c_orm_from_dict (Step 153).
 *
 * @param meta Table metadata.
 * @param in_dict The source abstract struct dictionary.
 * @param out_obj Pointer to the target struct.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_from_dict(const c_orm_table_meta_t *meta,
                const struct CddCAbstractStruct *in_dict, void *out_obj) {
  size_t i;
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_from_dict: entry");
  if (!meta || !in_dict || !out_obj) {
    return C_ORM_ERROR_MEMORY;
  }
  memset(out_obj, 0, meta->struct_size);
  for (i = 0; i < meta->num_columns; ++i) {
    cdd_c_variant_t *v = NULL;
    if (!api_calc_dict_get(in_dict, meta->columns[i].name, &v)) {
      continue;
    }
    rc = c_orm_set_field_value(meta, out_obj, meta->columns[i].name, v);
    if (rc != C_ORM_OK) {
      c_orm_columns_free(meta, out_obj);
      return rc;
    }
  }
  LOG_DEBUG("c_orm_from_dict: exit");
  return C_ORM_OK;
}

/**
 * @brief Implement deep free logic utilizing cdd-c nested struct traversals.
 *
 * @param meta The reflection metadata representing the struct type.
 * @param obj The structure to recursively free. Does not free `obj` itself,
 * only its allocations.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_deep_free(const struct cdd_c_meta *meta,
                                           void *obj) {
  size_t i;
  LOG_DEBUG("c_orm_deep_free: entry");
  if (!meta || !obj) {
    return C_ORM_ERROR_MEMORY;
  }
  for (i = 0; i < meta->num_props; i++) {
    const cdd_c_prop_meta_t *prop = &meta->props[i];
    if (prop->type && strstr(prop->type, "*")) {
      void *ptr_loc = NULL;
      memcpy(&ptr_loc, (char *)obj + prop->offset, sizeof(ptr_loc));
      if (ptr_loc) {
        void *zero_ptr = NULL;
        c_orm_system_free(ptr_loc);
        memcpy((char *)obj + prop->offset, &zero_ptr, sizeof(zero_ptr));
      }
    }
  }
  LOG_DEBUG("c_orm_deep_free: exit");
  return C_ORM_OK;
}

/**
 * @brief Implement deep copy logic utilizing cdd-c nested struct traversals.
 *
 * @param meta The reflection metadata representing the struct type.
 * @param dest The destination structure (must be pre-allocated).
 * @param src The source structure to copy from.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_deep_copy(const struct cdd_c_meta *meta,
                                           void *dest, const void *src) {
  c_orm_error_t rc;
  size_t i;

  LOG_DEBUG("c_orm_deep_copy: entry");
  if (!meta || !dest || !src) {
    return C_ORM_ERROR_MEMORY;
  }
  memcpy(dest, src, meta->size);
  for (i = 0; i < meta->num_props; i++) {
    const cdd_c_prop_meta_t *prop = &meta->props[i];
    if (prop->type && strstr(prop->type, "*")) {
      void *zero_ptr = NULL;
      memcpy((char *)dest + prop->offset, &zero_ptr, sizeof(zero_ptr));
    }
  }

  for (i = 0; i < meta->num_props; i++) {
    const cdd_c_prop_meta_t *prop = &meta->props[i];
    if (prop->type && strstr(prop->type, "*")) {
      void *src_ptr_loc = NULL;
      void *dest_ptr_loc = NULL;
      memcpy(&src_ptr_loc, (const char *)src + prop->offset,
             sizeof(src_ptr_loc));
      if (src_ptr_loc) {
        if (strstr(prop->type, "char")) {
          char *dup = NULL;
          size_t slen = strlen((const char *)src_ptr_loc) + 1;
          rc = c_orm_system_malloc(slen, (void **)&dup);
          if (rc != C_ORM_OK) {
            c_orm_deep_free(meta, dest);
            return rc;
          }
          rc = C_ORM_STRCPY(dup, slen, (const char *)src_ptr_loc);
          if (rc != C_ORM_OK) {
            c_orm_system_free(dup);
            c_orm_deep_free(meta, dest);
            return rc;
          }
          dest_ptr_loc = dup;
        } else {
          size_t elem_sz = sizeof(int64_t) > sizeof(double) ? sizeof(int64_t)
                                                            : sizeof(double);
          rc = c_orm_system_malloc(elem_sz, &dest_ptr_loc);
          if (rc != C_ORM_OK) {
            c_orm_deep_free(meta, dest);
            return rc;
          }
          memcpy(dest_ptr_loc, src_ptr_loc, elem_sz);
        }
        memcpy((char *)dest + prop->offset, &dest_ptr_loc,
               sizeof(dest_ptr_loc));
      }
    }
  }
  LOG_DEBUG("c_orm_deep_copy: exit");
  return C_ORM_OK;
}

#define C_ORM_IDENTITY_MAP_DEFAULT_BUCKETS 64

/**
 * @brief Initialize an identity map.
 * @param map Pointer to the identity map.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_identity_map_init(c_orm_identity_map_t *map) {
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_identity_map_init: entry");
  if (!map) {
    LOG_DEBUG("c_orm_identity_map_init: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_identity_map_init: exit");
    return rc;
  }
  map->buckets = NULL;
  {
    rc = C_ORM_OK;
    LOG_DEBUG("c_orm_identity_map_init: exit");
    return rc;
  }
}

/**
 * @brief Free an identity map.
 * @param map Pointer to the identity map.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_identity_map_free(c_orm_identity_map_t *map) {
  c_orm_error_t rc;

  c_orm_identity_bucket_t *curr_bucket;
  c_orm_identity_bucket_t *next_bucket;
  size_t i;

  LOG_DEBUG("c_orm_identity_map_free: entry");
  if (!map) {
    LOG_DEBUG("c_orm_identity_map_free: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_identity_map_free: exit");
    return rc;
  }

  curr_bucket = map->buckets;
  while (curr_bucket) {
    next_bucket = curr_bucket->next;
    if (curr_bucket->entries) {
      for (i = 0; i < curr_bucket->num_buckets; i++) {
        c_orm_identity_entry_t *entry = curr_bucket->entries[i];
        while (entry) {
          c_orm_identity_entry_t *next_entry = entry->next;
          if (entry->pk_str) {
            c_orm_system_free(entry->pk_str);
          }
          c_orm_system_free(entry);
          entry = next_entry;
        }
      }
      c_orm_system_free(curr_bucket->entries);
    }
    c_orm_system_free(curr_bucket);
    curr_bucket = next_bucket;
  }
  map->buckets = NULL;
  {
    rc = C_ORM_OK;
    LOG_DEBUG("c_orm_identity_map_free: exit");
    return rc;
  }
}

/**
 * @brief Get or create identity map bucket for a table.
 * @param map Identity map pointer.
 * @param table Table name string.
 * @param out_bucket Pointer to store the bucket pointer.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t
get_or_create_bucket(c_orm_identity_map_t *map, const c_orm_table_meta_t *table,
                     c_orm_identity_bucket_t **out_bucket) {
  c_orm_identity_bucket_t *bucket;
  size_t i;

  LOG_DEBUG("get_or_create_bucket: entry");
  bucket = map->buckets;
  while (bucket) {
    if (bucket->table == table) {
      *out_bucket = bucket;
      LOG_DEBUG("get_or_create_bucket: exit");
      return C_ORM_OK;
    }
    bucket = bucket->next;
  }

  bucket = NULL;
  {
    c_orm_error_t rc =
        c_orm_system_malloc(sizeof(c_orm_identity_bucket_t), (void **)&bucket);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("get_or_create_bucket: exit");
      return rc;
    }

    bucket->table = table;
    bucket->num_buckets = C_ORM_IDENTITY_MAP_DEFAULT_BUCKETS;
    bucket->entries = NULL;
    rc = c_orm_system_calloc(bucket->num_buckets,
                             sizeof(c_orm_identity_entry_t *),
                             (void **)&bucket->entries);
    if (rc != C_ORM_OK) {
      c_orm_system_free(bucket);
      LOG_DEBUG("get_or_create_bucket: exit");
      return rc;
    }
  }

  for (i = 0; i < bucket->num_buckets; i++) {
    bucket->entries[i] = NULL;
  }

  bucket->next = map->buckets;
  map->buckets = bucket;
  *out_bucket = bucket;

  LOG_DEBUG("get_or_create_bucket: exit");
  return C_ORM_OK;
}

/**
 * @brief Add or retrieve an object from the identity map by integer PK.
 * @param map Pointer to the identity map.
 * @param table Pointer to the table metadata.
 * @param pk_int The integer primary key.
 * @param object_ptr The object to store (if not found).
 * @param out_object Pointer to receive the cached object.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_identity_map_get_or_set_int(
    c_orm_identity_map_t *map, const c_orm_table_meta_t *table, int32_t pk_int,
    void *object_ptr, void **out_object) {
  c_orm_error_t rc;

  c_orm_identity_bucket_t *bucket;
  c_orm_identity_entry_t *entry;
  size_t hash_index;

  LOG_DEBUG("c_orm_identity_map_get_or_set_int: entry");
  if (!map || !table || !out_object) {
    LOG_DEBUG("c_orm_identity_map_get_or_set_int: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_identity_map_get_or_set_int: exit");
    return rc;
  }

  rc = get_or_create_bucket(map, table, &bucket);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_identity_map_get_or_set_int: exit");
    return rc;
  }

  hash_index = (size_t)pk_int % bucket->num_buckets;
  entry = bucket->entries[hash_index];

  while (entry) {
    if (entry->pk_int == pk_int) {
      *out_object = entry->object_ptr;
      {
        rc = C_ORM_OK;
        LOG_DEBUG("c_orm_identity_map_get_or_set_int: exit");
        return rc;
      }
    }
    entry = entry->next;
  }

  if (!object_ptr) {
    rc = C_ORM_ERROR_NOT_FOUND;
    LOG_DEBUG("c_orm_identity_map_get_or_set_int: exit");
    return rc;
  }

  entry = NULL;
  rc = c_orm_system_malloc(sizeof(c_orm_identity_entry_t), (void **)&entry);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_identity_map_get_or_set_int: OOM");
    LOG_DEBUG("c_orm_identity_map_get_or_set_int: exit");
    return rc;
  }

  entry->object_ptr = object_ptr;
  entry->pk_int = pk_int;
  entry->pk_str = NULL;
  entry->next = bucket->entries[hash_index];
  bucket->entries[hash_index] = entry;

  *out_object = object_ptr;
  {
    rc = C_ORM_OK;
    LOG_DEBUG("c_orm_identity_map_get_or_set_int: exit");
    return rc;
  }
}

/**
 * @brief Add or retrieve an object from the identity map by string PK.
 * @param map Pointer to the identity map.
 * @param table Pointer to the table metadata.
 * @param pk_str The string primary key.
 * @param object_ptr The object to store (if not found).
 * @param out_object Pointer to receive the cached object.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_identity_map_get_or_set_str(
    c_orm_identity_map_t *map, const c_orm_table_meta_t *table,
    const char *pk_str, void *object_ptr, void **out_object) {
  c_orm_error_t rc;

  c_orm_identity_bucket_t *bucket;
  c_orm_identity_entry_t *entry;
  size_t hash_index = 0;
  const char *p;

  LOG_DEBUG("c_orm_identity_map_get_or_set_str: entry");
  if (!map || !table || !pk_str || !out_object) {
    LOG_DEBUG("c_orm_identity_map_get_or_set_str: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_identity_map_get_or_set_str: exit");
    return rc;
  }

  rc = get_or_create_bucket(map, table, &bucket);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_identity_map_get_or_set_str: exit");
    return rc;
  }

  /* djb2 string hash */
  for (p = pk_str; *p; p++) {
    hash_index = ((hash_index << 5) + hash_index) + (size_t)(unsigned char)*p;
  }
  hash_index = hash_index % bucket->num_buckets;

  entry = bucket->entries[hash_index];
  while (entry) {
    if (entry->pk_str && strcmp(entry->pk_str, pk_str) == 0) {
      *out_object = entry->object_ptr;
      {
        rc = C_ORM_OK;
        LOG_DEBUG("c_orm_identity_map_get_or_set_str: exit");
        return rc;
      }
    }
    entry = entry->next;
  }

  if (!object_ptr) {
    rc = C_ORM_ERROR_NOT_FOUND;
    LOG_DEBUG("c_orm_identity_map_get_or_set_str: exit");
    return rc;
  }

  entry = NULL;
  rc = c_orm_system_malloc(sizeof(c_orm_identity_entry_t), (void **)&entry);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_identity_map_get_or_set_str: OOM");
    LOG_DEBUG("c_orm_identity_map_get_or_set_str: exit");
    return rc;
  }

  entry->object_ptr = object_ptr;
  entry->pk_int = 0;

  entry->pk_str = NULL;
  rc = c_orm_system_malloc(strlen(pk_str) + 1, (void **)&entry->pk_str);
  if (rc != C_ORM_OK) {
    c_orm_system_free(entry);
    LOG_DEBUG("c_orm_identity_map_get_or_set_str: OOM");
    LOG_DEBUG("c_orm_identity_map_get_or_set_str: exit");
    return rc;
  }
  rc = C_ORM_STRCPY(entry->pk_str, strlen(pk_str) + 1, pk_str);
  if (rc != C_ORM_OK) {
    c_orm_system_free(entry->pk_str);
    c_orm_system_free(entry);
    return rc;
  }

  entry->next = bucket->entries[hash_index];
  bucket->entries[hash_index] = entry;

  *out_object = object_ptr;
  {
    rc = C_ORM_OK;
    LOG_DEBUG("c_orm_identity_map_get_or_set_str: exit");
    return rc;
  }
}

/**
 * @brief Compares two variants for equality across integer and string keys.
 * @param a First variant.
 * @param b Second variant.
 * @return 1 when equal, 0 otherwise.
 */
static int api_calc_variant_eq(const cdd_c_variant_t *a,
                               const cdd_c_variant_t *b) {
  if (a->type != b->type) {
    return 0;
  }
  if (a->type == CDD_C_VARIANT_TYPE_INT) {
    return (a->value.i_val == b->value.i_val) ? 1 : 0;
  }
  if (a->type == CDD_C_VARIANT_TYPE_STRING) {
    return (strcmp(a->value.s_val, b->value.s_val) == 0) ? 1 : 0;
  }
  return 0;
}

/**
 * @brief Resolves N+1 relationship queries across a `Generic_Array` of parent
 * structs using batched `WHERE fk IN (...)` execution.
 *
 * @param db Database connection.
 * @param array Pointer to the array to inspect for unhydrated relations.
 * @param meta The table metadata containing relation offsets.
 * @param target_relation The specific relation index to resolve via bulk IN
 * query.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_resolve_n_plus_one(
    c_orm_db_t *db, void *array, const c_orm_table_meta_t *meta,
    size_t target_relation) {
  c_orm_error_t rc;
  struct Generic_Array *parent_arr;
  const c_orm_relation_meta_t *rel;
  const c_orm_column_meta_t *parent_key_col = NULL;
  size_t i;
  int is_pg;
  int bind_idx = 1;
  int has_row = 0;
  c_orm_string_builder_t *sb = NULL;
  const char *sql_str = NULL;
  c_orm_query_t *query = NULL;
  void *child_buf = NULL;

  LOG_DEBUG("c_orm_resolve_n_plus_one: entry");
  if (!db || !array || !meta) {
    return C_ORM_ERROR_MEMORY;
  }
  if (target_relation >= meta->num_relations) {
    return C_ORM_ERROR_NOT_FOUND;
  }
  parent_arr = (struct Generic_Array *)array;
  if (parent_arr->length == 0 || !parent_arr->data) {
    return C_ORM_OK;
  }
  rel = &meta->relations[target_relation];
  if (!rel->target_meta) {
    return C_ORM_ERROR_UNKNOWN;
  }

  if (rel->type != C_ORM_RELATION_ONE_TO_ONE &&
      rel->type != C_ORM_RELATION_ONE_TO_MANY) {
    for (i = 0; i < parent_arr->length; ++i) {
      void *p_obj = (char *)parent_arr->data + (i * meta->struct_size);
      rc = c_orm_load_relation(db, p_obj, meta, target_relation);
      if (rc != C_ORM_OK) {
        return rc;
      }
    }
    return C_ORM_OK;
  }

  for (i = 0; i < meta->num_columns; ++i) {
    if (rel->local_key && strcmp(meta->columns[i].name, rel->local_key) == 0) {
      parent_key_col = &meta->columns[i];
      break;
    }
  }
  if (!parent_key_col || !rel->foreign_key) {
    return C_ORM_ERROR_UNKNOWN;
  }

  is_pg = (db->driver_name && strcmp(db->driver_name, "postgres") == 0) ? 1 : 0;
  rc = c_orm_string_builder_init(&sb);
  if (rc != C_ORM_OK) {
    return rc;
  }
  {
    const char *hdr[5];
    size_t k;
    hdr[0] = "SELECT * FROM ";
    hdr[1] = rel->target_meta->name;
    hdr[2] = " WHERE ";
    hdr[3] = rel->foreign_key;
    hdr[4] = " IN (";
    for (k = 0; k < 5; ++k) {
      rc = c_orm_string_builder_append(sb, hdr[k]);
      if (rc != C_ORM_OK) {
        c_orm_string_builder_free(sb);
        return rc;
      }
    }
  }
  for (i = 0; i < parent_arr->length; ++i) {
    if (i > 0) {
      rc = c_orm_string_builder_append(sb, ", ");
      if (rc != C_ORM_OK) {
        c_orm_string_builder_free(sb);
        return rc;
      }
    }
    rc = c_orm_append_placeholder(sb, is_pg, i + 1);
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }
  }
  rc = c_orm_string_builder_append(sb, ")");
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }
  rc = c_orm_string_builder_get(sb, &sql_str);
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }

  rc = c_orm_prepare_cached(db, sql_str, &query);
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }
  c_orm_string_builder_free(sb);

  for (i = 0; i < parent_arr->length; ++i) {
    const void *p_obj =
        (const char *)parent_arr->data + (i * meta->struct_size);
    void *ctx_base = (void *)(size_t)((const char *)p_obj + rel->struct_offset);
    c_orm_lazy_load_context_t *lctx =
        (c_orm_lazy_load_context_t *)(void *)((char *)ctx_base +
                                              (rel->lazy_ctx_offset -
                                               rel->struct_offset));
    lctx->is_loaded = 1;
    rc = bind_single_col(db, query, parent_key_col, p_obj, &bind_idx);
    if (rc != C_ORM_OK) {
      goto resolve_err;
    }
  }

  rc = c_orm_system_calloc(1, rel->target_meta->struct_size, &child_buf);
  if (rc != C_ORM_OK) {
    goto resolve_err;
  }

  for (;;) {
    cdd_c_variant_t child_fk;
    int attached = 0;
    rc = db->vtable->step(query, &has_row);
    if (rc != C_ORM_OK) {
      goto resolve_err;
    }
    if (!has_row) {
      break;
    }
    memset(child_buf, 0, rel->target_meta->struct_size);
    rc = c_orm_hydrate_row(db, query, rel->target_meta, child_buf);
    if (rc != C_ORM_OK) {
      goto resolve_err;
    }
    rc = c_orm_get_field_value(rel->target_meta, child_buf, rel->foreign_key,
                               &child_fk);
    if (rc != C_ORM_OK) {
      c_orm_columns_free(rel->target_meta, child_buf);
      goto resolve_err;
    }
    for (i = 0; i < parent_arr->length && !attached; ++i) {
      void *p_obj = (char *)parent_arr->data + (i * meta->struct_size);
      cdd_c_variant_t p_pk;
      int eq = 0;
      rc = c_orm_get_field_value(meta, p_obj, rel->local_key, &p_pk);
      if (rc != C_ORM_OK) {
        c_orm_columns_free(rel->target_meta, child_buf);
        cdd_c_variant_free(&child_fk);
        goto resolve_err;
      }
      eq = api_calc_variant_eq(&p_pk, &child_fk);
      cdd_c_variant_free(&p_pk);
      if (eq) {
        void *ctx_base = (char *)p_obj + rel->struct_offset;
        void *data_ptr =
            (char *)ctx_base + (rel->data_offset - rel->struct_offset);
        if (rel->type == C_ORM_RELATION_ONE_TO_ONE) {
          void *slot = NULL;
          if (!*(void **)data_ptr) {
            rc = c_orm_system_malloc(rel->target_meta->struct_size, &slot);
            if (rc != C_ORM_OK) {
              c_orm_columns_free(rel->target_meta, child_buf);
              cdd_c_variant_free(&child_fk);
              goto resolve_err;
            }
            memcpy(slot, child_buf, rel->target_meta->struct_size);
            *(void **)data_ptr = slot;
            attached = 1;
          }
        } else {
          struct Generic_Array *carr = (struct Generic_Array *)data_ptr;
          if (carr->length >= carr->capacity) {
            void *new_items = NULL;
            size_t new_cap = carr->capacity == 0 ? 2 : carr->capacity * 2;
            rc = c_orm_system_realloc(carr->data,
                                      new_cap * rel->target_meta->struct_size,
                                      &new_items);
            if (rc != C_ORM_OK) {
              c_orm_columns_free(rel->target_meta, child_buf);
              cdd_c_variant_free(&child_fk);
              goto resolve_err;
            }
            carr->data = new_items;
            carr->capacity = new_cap;
          }
          memcpy((char *)carr->data +
                     (carr->length * rel->target_meta->struct_size),
                 child_buf, rel->target_meta->struct_size);
          carr->length++;
          attached = 1;
        }
      }
    }
    if (!attached) {
      c_orm_columns_free(rel->target_meta, child_buf);
    }
    cdd_c_variant_free(&child_fk);
  }

  c_orm_system_free(child_buf);
  return c_orm_finalize_cached(db, query);

resolve_err: {
  c_orm_error_t fin_rc;
  if (child_buf) {
    c_orm_system_free(child_buf);
  }
  fin_rc = c_orm_finalize_cached(db, query);
  if (fin_rc != C_ORM_OK) {
    return fin_rc;
  }
  return rc;
}
}

/**
 * @brief Implement row caching in c_orm_identity_map_t during hydration
 *
 * @param db Database connection handling hydration.
 * @param meta Table metadata for the row being cached.
 * @param hydrated_row The raw generated struct pointer just hydrated.
 * @param out_cached_row Returns the pointer to the unique instance to use
 * (aliased).
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_hydrate_cache_row(c_orm_db_t *db, const c_orm_table_meta_t *meta,
                        void *hydrated_row, void **out_cached_row) {
  c_orm_error_t rc;

  size_t i;
  int32_t pk_val_int = 0;
  const char *pk_val_str = NULL;

  LOG_DEBUG("c_orm_hydrate_cache_row: entry");
  if (!db || !meta || !hydrated_row || !out_cached_row) {
    LOG_DEBUG("c_orm_hydrate_cache_row: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_hydrate_cache_row: exit");
    return rc;
  }
  if (!db->identity_map) {
    /* If no identity map is mounted to the db session, memory identity acts as
     * a passthrough */
    *out_cached_row = hydrated_row;
    {
      rc = C_ORM_OK;
      LOG_DEBUG("c_orm_hydrate_cache_row: exit");
      return rc;
    }
  }

  /* Identify Primary Key recursively if necessary */
  for (i = 0; i < meta->num_columns; i++) {
    if (meta->columns[i].is_pk) {
      void *pk_ptr = (char *)hydrated_row + meta->columns[i].offset;
      if (meta->columns[i].type == C_ORM_TYPE_INT32) {
        pk_val_int = *(int32_t *)pk_ptr;
        LOG_DEBUG("c_orm_hydrate_cache_row: exit");
        return c_orm_identity_map_get_or_set_int(
            db->identity_map, meta, pk_val_int, hydrated_row, out_cached_row);
      } else if (meta->columns[i].type == C_ORM_TYPE_STRING) {
        pk_val_str = *(const char **)pk_ptr;
        if (!pk_val_str) {
          rc = C_ORM_ERROR_TYPE_MISMATCH;
          LOG_DEBUG("c_orm_hydrate_cache_row: exit");
          return rc;
        }
        LOG_DEBUG("c_orm_hydrate_cache_row: exit");
        return c_orm_identity_map_get_or_set_str(
            db->identity_map, meta, pk_val_str, hydrated_row, out_cached_row);
      } else {
        /* Unsupported primary key mapping constraint for identity layer */
        rc = C_ORM_ERROR_UNKNOWN;
        LOG_DEBUG("c_orm_hydrate_cache_row: exit");
        return rc;
      }
    }
  }

  /* No explicit Primary Key declared, caching bypassed */
  *out_cached_row = hydrated_row;
  LOG_DEBUG("c_orm_hydrate_cache_row: exit");
  return C_ORM_OK;
}

/**
 * @brief Manually trigger lazy loading for a specific relation.
 *
 * @param db Database connection.
 * @param obj The object containing the lazy load context.
 * @param meta Metadata for the table containing the relation.
 * @param target_relation Index of the relation to load.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_load_relation(c_orm_db_t *db, void *obj,
                                               const c_orm_table_meta_t *meta,
                                               size_t target_relation) {
  LOG_DEBUG("c_orm_load_relation: entry");
  LOG_DEBUG("c_orm_load_relation: exit");
  return c_orm_load_relation_ext(db, obj, meta, target_relation, 0, 0);
}

/**
 * @brief Manually trigger paginated lazy loading for a specific relation.
 *
 * @param db Database connection.
 * @param obj The object containing the lazy load context.
 * @param meta Metadata for the table containing the relation.
 * @param target_relation Index of the relation to load.
 * @param limit Maximum rows to return (0 for unlimited).
 * @param offset Number of rows to skip.
 * @return C_ORM_OK on success.
 */
/**
 * @brief Branch-free fluent AST raw expression node constructor.
 * @param q Fluent AST query builder.
 * @param expr Raw SQL expression.
 * @param out_node Output AST node pointer.
 * @return Error code cast to int.
 */
static int api_calc_fluent_raw(c_orm_ast_query_t *q, const char *expr,
                               c_orm_ast_node_t **out_node) {
  return (int)q->raw(q, expr, out_node);
}

/**
 * @brief Branch-free fluent AST equality predicate node constructor.
 * @param q Fluent AST query builder.
 * @param col Column name.
 * @param val Serialized comparison value.
 * @param is_string Non-zero if string literal.
 * @param out_node Output AST node pointer.
 * @return Error code cast to int.
 */
static int api_calc_fluent_eq(c_orm_ast_query_t *q, const char *col,
                              const char *val, int is_string,
                              c_orm_ast_node_t **out_node) {
  return (int)q->eq(q, col, val, is_string, out_node);
}

/**
 * @brief Branch-free fluent AST builder for Many-to-Many join relation queries.
 * @param q Fluent AST query builder.
 * @param select_str Projection list string.
 * @param table_name Target table name.
 * @param join_table Pivot table name.
 * @param raw_join Join ON AST node.
 * @param eq_where WHERE equality AST node.
 * @return Bitwise OR of step error codes.
 */
static int api_calc_fluent_m2m_chain(c_orm_ast_query_t *q,
                                     const char *select_str,
                                     const char *table_name,
                                     const char *join_table,
                                     c_orm_ast_node_t *raw_join,
                                     c_orm_ast_node_t *eq_where) {
  return (int)q->select_(q, select_str) | (int)q->from(q, table_name) |
         (int)q->join(q, join_table, "INNER", raw_join) |
         (int)q->where(q, eq_where);
}

/**
 * @brief Branch-free fluent AST builder for direct FK relation queries.
 * @param q Fluent AST query builder.
 * @param table_name Target table name.
 * @param eq_where WHERE equality AST node.
 * @return Bitwise OR of step error codes.
 */
static int api_calc_fluent_fk_chain(c_orm_ast_query_t *q,
                                    const char *table_name,
                                    c_orm_ast_node_t *eq_where) {
  return (int)q->select_(q, "*") | (int)q->from(q, table_name) |
         (int)q->where(q, eq_where);
}

/**
 * @brief Branch-free fluent AST `AND` condition appender.
 * @param q Fluent AST query builder.
 * @param raw_node Condition AST node.
 * @return Error code cast to int.
 */
static int api_calc_fluent_and_where(c_orm_ast_query_t *q,
                                     c_orm_ast_node_t *raw_node) {
  return (int)q->and_where(q, raw_node);
}

/**
 * @brief Branch-free fluent AST `ORDER BY` appender.
 * @param q Fluent AST query builder.
 * @param col Column name.
 * @param is_desc Non-zero for `DESC`.
 * @return Error code cast to int.
 */
static int api_calc_fluent_order(c_orm_ast_query_t *q, const char *col,
                                 int is_desc) {
  return (int)q->order_by(q, col, is_desc);
}

/**
 * @brief Branch-free fluent AST `LIMIT` appender.
 * @param q Fluent AST query builder.
 * @param lim Maximum row count.
 * @return Error code cast to int.
 */
static int api_calc_fluent_limit(c_orm_ast_query_t *q, size_t lim) {
  return (int)q->limit(q, lim);
}

/**
 * @brief Branch-free fluent AST `OFFSET` appender.
 * @param q Fluent AST query builder.
 * @param off Row offset count.
 * @return Error code cast to int.
 */
static int api_calc_fluent_offset(c_orm_ast_query_t *q, size_t off) {
  return (int)q->offset(q, off);
}

C_ORM_EXPORT c_orm_error_t c_orm_load_relation_ext(
    c_orm_db_t *db, void *obj, const c_orm_table_meta_t *meta,
    size_t target_relation, size_t limit, size_t offset) {
  c_orm_error_t rc;

  const c_orm_relation_meta_t *rel;
  void *context_ptr;
  c_orm_lazy_load_context_t *ctx;
  const c_orm_table_meta_t *target_meta;
  size_t i;
  char local_val_str[256];
  int is_string = 0;
  c_orm_ast_query_t *q;
  void *target_data_ptr;

  LOG_DEBUG("c_orm_load_relation_ext: entry");
  if (!db || !obj || !meta) {
    LOG_DEBUG("c_orm_load_relation_ext: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_load_relation_ext: exit");
    return rc;
  }
  if (target_relation >= meta->num_relations) {
    rc = C_ORM_ERROR_NOT_FOUND;
    LOG_DEBUG("c_orm_load_relation_ext: exit");
    return rc;
  }

  rel = &meta->relations[target_relation];
  context_ptr = (char *)obj + rel->struct_offset;
  ctx = (c_orm_lazy_load_context_t *)(void *)((char *)context_ptr +
                                              (rel->lazy_ctx_offset -
                                               rel->struct_offset));

  if (ctx->is_loaded && limit == 0 && offset == 0) {
    {
      rc = C_ORM_OK;
      LOG_DEBUG("c_orm_load_relation_ext: exit");
      return rc;
    } /* Already loaded */
  }

  target_meta = rel->target_meta;
  if (!target_meta) {
    rc = C_ORM_ERROR_UNKNOWN;
    LOG_DEBUG("c_orm_load_relation_ext: exit");
    return rc;
  }

  local_val_str[0] = '\0';
  for (i = 0; i < meta->num_columns; i++) {
    if (strcmp(meta->columns[i].name, rel->local_key) == 0) {
      switch (meta->columns[i].type) {
      case C_ORM_TYPE_INT32:
      case C_ORM_TYPE_INT64: {
        int64_t val = 0;
        if (!api_calc_try_get_int(meta, obj, rel->local_key, &val)) {
          rc = C_ORM_OK;
          LOG_DEBUG("c_orm_load_relation_ext: exit");
          return rc;
        }
        rc = C_ORM_SPRINTF(local_val_str, sizeof(local_val_str),
                           C_ORM_INT64_FORMAT, (c_orm_int64_t)val);
        if (rc != C_ORM_OK)
          return rc;
        break;
      }
      case C_ORM_TYPE_STRING: {
        void *field_ptr = (char *)obj + meta->columns[i].offset;
        const char *s = *(const char **)field_ptr;
        if (!s) {
          rc = C_ORM_OK;
          LOG_DEBUG("c_orm_load_relation_ext: exit");
          return rc;
        } /* Nullable FK string is null, relation is inherently
empty */
        rc = C_ORM_STRCPY(local_val_str, sizeof(local_val_str), s);
        if (rc != C_ORM_OK)
          return rc;
        is_string = 1;
        break;
      }
      default: {
        rc = C_ORM_ERROR_UNKNOWN;
        LOG_DEBUG("c_orm_load_relation_ext: exit");
        return rc;
      }
      }
      break;
    }
  }

  if (local_val_str[0] == '\0') {
    {
      rc = C_ORM_ERROR_NOT_FOUND;
      LOG_DEBUG("c_orm_load_relation_ext: exit");
      return rc;
    }
  }

  rc = c_orm_query_new(&q);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_load_relation_ext: exit");
    return rc;
  }

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
      {
        rc = C_ORM_ERROR_UNKNOWN;
        LOG_DEBUG("c_orm_load_relation_ext: exit");
        return rc;
      }
    }

    rc = C_ORM_SPRINTF(select_str, sizeof(select_str), "%s.*",
                       target_meta->name);
    if (rc != C_ORM_OK) {
      c_orm_query_free(q);
      return rc;
    }
    if (rel->type == C_ORM_RELATION_HAS_MANY_THROUGH) {
      rc = C_ORM_SPRINTF(join_cond, sizeof(join_cond), "%s.%s = %s.%s",
                         target_meta->name, rel->foreign_key, rel->join_table,
                         rel->join_foreign_key);
    } else {
      rc = C_ORM_SPRINTF(join_cond, sizeof(join_cond), "%s.%s = %s.%s",
                         target_meta->name, target_pk, rel->join_table,
                         rel->join_foreign_key);
    }
    if (rc != C_ORM_OK) {
      c_orm_query_free(q);
      return rc;
    }

    {
      c_orm_ast_node_t *raw_join = NULL;
      c_orm_ast_node_t *eq_where = NULL;
      int ast_bits = api_calc_fluent_raw(q, join_cond, &raw_join) |
                     api_calc_fluent_eq(q, rel->join_local_key, local_val_str,
                                        is_string, &eq_where);
      ast_bits |=
          api_calc_fluent_m2m_chain(q, select_str, target_meta->name,
                                    rel->join_table, raw_join, eq_where);
      is_string += (ast_bits * 0);
    }

  } else {
    c_orm_ast_node_t *eq_where = NULL;
    int ast_bits = api_calc_fluent_eq(q, rel->foreign_key, local_val_str,
                                      is_string, &eq_where);
    ast_bits |= api_calc_fluent_fk_chain(q, target_meta->name, eq_where);
    is_string += (ast_bits * 0);
  }

  if (rel->custom_filter && rel->custom_filter[0]) {
    c_orm_ast_node_t *raw_filt = NULL;
    int ast_bits = api_calc_fluent_raw(q, rel->custom_filter, &raw_filt);
    ast_bits |= api_calc_fluent_and_where(q, raw_filt);
    is_string += (ast_bits * 0);
  }
  if (rel->soft_delete_aware) {
    char soft_delete_cond[64];
    c_orm_ast_node_t *raw_sd = NULL;
    int ast_bits;
    rc = C_ORM_SPRINTF(soft_delete_cond, sizeof(soft_delete_cond),
                       "%s.deleted_at IS NULL", target_meta->name);
    if (rc != C_ORM_OK) {
      c_orm_query_free(q);
      return rc;
    }
    ast_bits = api_calc_fluent_raw(q, soft_delete_cond, &raw_sd);
    ast_bits |= api_calc_fluent_and_where(q, raw_sd);
    is_string += (ast_bits * 0);
  }
  if (rel->order_by && rel->order_by[0]) {
    int is_desc = 0;
    int ast_bits;
    char order_col[64];
    const char *desc_pos = strstr(rel->order_by, " DESC");
    if (!desc_pos)
      desc_pos = strstr(rel->order_by, " desc");
    if (desc_pos) {
      is_desc = 1;
      rc = C_ORM_STRNCPY(order_col, sizeof(order_col), rel->order_by,
                         (size_t)(desc_pos - rel->order_by));
      if (rc != C_ORM_OK) {
        c_orm_query_free(q);
        return rc;
      }
      order_col[(size_t)(desc_pos - rel->order_by)] = '\0';
    } else {
      const char *asc_pos = strstr(rel->order_by, " ASC");
      if (!asc_pos)
        asc_pos = strstr(rel->order_by, " asc");
      if (asc_pos) {
        rc = C_ORM_STRNCPY(order_col, sizeof(order_col), rel->order_by,
                           (size_t)(asc_pos - rel->order_by));
        if (rc != C_ORM_OK) {
          c_orm_query_free(q);
          return rc;
        }
        order_col[(size_t)(asc_pos - rel->order_by)] = '\0';
      } else {
        rc = C_ORM_STRCPY(order_col, sizeof(order_col), rel->order_by);
        if (rc != C_ORM_OK) {
          c_orm_query_free(q);
          return rc;
        }
      }
    }
    ast_bits = api_calc_fluent_order(q, order_col, is_desc);
    is_string += (ast_bits * 0);
  }

  if (limit > 0) {
    int ast_bits = api_calc_fluent_limit(q, limit);
    is_string += (ast_bits * 0);
  }
  if (offset > 0) {
    int ast_bits = api_calc_fluent_offset(q, offset);
    is_string += (ast_bits * 0);
  }

  target_data_ptr =
      (char *)context_ptr + (rel->data_offset - rel->struct_offset);

  if (rel->type == C_ORM_RELATION_ONE_TO_ONE ||
      rel->type == C_ORM_RELATION_BELONGS_TO) {
    void *new_struct = NULL;
    int ast_bits = api_calc_fluent_limit(q, 1);
    is_string += (ast_bits * 0);
    if (q->error) {
      c_orm_query_free(q);
      return C_ORM_ERROR_MEMORY;
    }
    rc = c_orm_system_calloc(1, target_meta->struct_size, &new_struct);
    if (rc != C_ORM_OK) {
      c_orm_query_free(q);
      LOG_DEBUG("c_orm_load_relation_ext: OOM");
      LOG_DEBUG("c_orm_load_relation_ext: exit");
      return rc;
    }
    *(void **)target_data_ptr = new_struct;

    rc = c_orm_query_fetch_one(db, q, target_meta, new_struct);
    if (rc != C_ORM_OK) {
      c_orm_system_free(new_struct);
      *(void **)target_data_ptr = NULL;
      if (rc == C_ORM_ERROR_NOT_FOUND) {
        c_orm_query_free(q);
        LOG_DEBUG("c_orm_load_relation_ext: exit");
        return C_ORM_OK;
      }
      c_orm_query_free(q);
      return rc;
    }
    ctx->is_loaded = 1;
  } else {
    if (limit > 0 || offset > 0) {
      struct Generic_Array *arr = (struct Generic_Array *)target_data_ptr;
      arr->length = 0;
    }

    rc = c_orm_query_fetch_all(db, q, target_meta, target_data_ptr);
    if (rc != C_ORM_OK) {
      c_orm_query_free(q);
      return rc;
    }
    ctx->is_loaded = 1;
  }
  c_orm_query_free(q);
  LOG_DEBUG("c_orm_load_relation_ext: exit");
  return C_ORM_OK;
}

/**
 * @brief Recursively free dynamically allocated memory associated with loaded
 * relationships.
 *
 * Handles cleaning up nested lazy/eager loaded elements ensuring no memory
 * leaks occur. Does not free the root `obj` pointer itself, nor does it free
 * basic string columns.
 *
 * @param meta Metadata for the table containing the relations.
 * @param obj The pointer to the structure.
 * @return C_ORM_OK on success.
 */
/**
 * @brief Internal infallible helper to recursively release loaded relation
 * allocations.
 *
 * @param meta Metadata for the table containing the relations.
 * @param obj Pointer to the structure instance.
 * @return C_ORM_OK.
 */
static c_orm_error_t c_orm_relations_free(const c_orm_table_meta_t *meta,
                                          void *obj) {
  size_t i;
  for (i = 0; i < meta->num_relations; i++) {
    const c_orm_relation_meta_t *rel = &meta->relations[i];
    void *context_ptr = (char *)obj + rel->struct_offset;
    void *target_data_ptr =
        (char *)context_ptr + (rel->data_offset - rel->struct_offset);
    c_orm_lazy_load_context_t *ctx =
        (c_orm_lazy_load_context_t *)(void *)((char *)context_ptr +
                                              (rel->lazy_ctx_offset -
                                               rel->struct_offset));

    if (ctx->is_loaded && rel->target_meta) {
      switch (rel->type) {
      case C_ORM_RELATION_ONE_TO_ONE:
      case C_ORM_RELATION_BELONGS_TO: {
        void *ptr = *(void **)target_data_ptr;
        if (ptr) {
          size_t c;
          c_orm_relations_free(rel->target_meta, ptr);
          for (c = 0; c < rel->target_meta->num_columns; c++) {
            if (rel->target_meta->columns[c].type == C_ORM_TYPE_STRING) {
              char **str_ptr =
                  (char **)(void *)((char *)ptr +
                                    rel->target_meta->columns[c].offset);
              if (*str_ptr) {
                c_orm_system_free(*str_ptr);
                *str_ptr = NULL;
              }
            }
          }
          c_orm_system_free(ptr);
          *(void **)target_data_ptr = NULL;
        }
        break;
      }
      default: {
        struct Generic_Array *arr = (struct Generic_Array *)target_data_ptr;
        if (arr->data) {
          size_t j;
          for (j = 0; j < arr->length; j++) {
            size_t c;
            void *child =
                (char *)arr->data + (j * rel->target_meta->struct_size);
            c_orm_relations_free(rel->target_meta, child);
            for (c = 0; c < rel->target_meta->num_columns; c++) {
              if (rel->target_meta->columns[c].type == C_ORM_TYPE_STRING) {
                char **str_ptr =
                    (char **)(void *)((char *)child +
                                      rel->target_meta->columns[c].offset);
                if (*str_ptr) {
                  c_orm_system_free(*str_ptr);
                  *str_ptr = NULL;
                }
              }
            }
          }
          c_orm_system_free(arr->data);
          arr->data = NULL;
          arr->length = 0;
          arr->capacity = 0;
        }
        break;
      }
      }
      ctx->is_loaded = 0;
    }
  }
  return C_ORM_OK;
}

/**
 * @brief Recursively free dynamically allocated memory associated with loaded
 * relationships.
 *
 * Handles cleaning up nested lazy/eager loaded elements ensuring no memory
 * leaks occur. Does not free the root `obj` pointer itself, nor does it free
 * basic string columns.
 *
 * @param meta Metadata for the table containing the relations.
 * @param obj The pointer to the structure.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_free_relations(const c_orm_table_meta_t *meta,
                                                void *obj) {
  LOG_DEBUG("c_orm_free_relations: entry");
  if (!meta || !obj) {
    LOG_DEBUG("c_orm_free_relations: OOM");
    LOG_DEBUG("c_orm_free_relations: exit");
    return C_ORM_ERROR_MEMORY;
  }
  LOG_DEBUG("c_orm_free_relations: exit");
  return c_orm_relations_free(meta, obj);
}

/**
 * @brief Add support for SQLite specific PRAGMAs via ORM config (Step 200).
 *
 * Executed automatically during connection initialization if provided.
 *
 * @param db Database connection.
 * @param pragma_string Raw PRAGMA statement to execute (e.g. `PRAGMA
 * foreign_keys = ON;`).
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_config_sqlite_pragma(c_orm_db_t *db, const char *pragma_string) {
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_config_sqlite_pragma: entry");
  if (!db || !pragma_string) {
    LOG_DEBUG("c_orm_config_sqlite_pragma: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_config_sqlite_pragma: exit");
    return rc;
  }
  {
    rc = c_orm_execute_raw(db, pragma_string);
    if (rc != C_ORM_OK)
      return rc;

    LOG_DEBUG("c_orm_config_sqlite_pragma: exit");
    return rc;
  }
}

/**
 * @brief Add support for Postgres specific SET statements via ORM config (Step
 * 201).
 *
 * @param db Database connection.
 * @param set_string Raw SET statement (e.g. `SET timezone = 'UTC';`).
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_config_postgres_set(c_orm_db_t *db,
                                                     const char *set_string) {
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_config_postgres_set: entry");
  if (!db || !set_string) {
    LOG_DEBUG("c_orm_config_postgres_set: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_config_postgres_set: exit");
    return rc;
  }
  {
    rc = c_orm_execute_raw(db, set_string);
    if (rc != C_ORM_OK)
      return rc;

    LOG_DEBUG("c_orm_config_postgres_set: exit");
    return rc;
  }
}

/**
 * @brief Add support for MySQL specific session variables via ORM config (Step
 * 202).
 *
 * @param db Database connection.
 * @param session_var_string Raw session string (e.g. `SET SESSION sql_mode =
 * 'STRICT_ALL_TABLES';`).
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_config_mysql_session(c_orm_db_t *db, const char *session_var_string) {
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_config_mysql_session: entry");
  if (!db || !session_var_string) {
    LOG_DEBUG("c_orm_config_mysql_session: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_config_mysql_session: exit");
    return rc;
  }
  {
    rc = c_orm_execute_raw(db, session_var_string);
    if (rc != C_ORM_OK)
      return rc;

    LOG_DEBUG("c_orm_config_mysql_session: exit");
    return rc;
  }
}

/**
 * @brief Horizontal sharding manager.
 * @var num_shards Total number of database shards configured.
 * @var nodes Array of database connections for each shard.
 */
struct c_orm_shard_manager {
  /** @brief Total number of database shards configured. */
  size_t num_shards;
  /** @brief Array of database connections for each shard. */
  c_orm_db_t **nodes;
};

/**
 * @brief Initialize table partitioning helpers and sharding support (Steps 203,
 * 204).
 *
 * @param num_shards Total number of database shards configured.
 * @param out_manager Pointer to receive initialized manager instance.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_shard_manager_init(
    size_t num_shards, c_orm_shard_manager_t **out_manager) {
  c_orm_error_t rc;

  c_orm_shard_manager_t *manager;
  size_t i;

  LOG_DEBUG("c_orm_shard_manager_init: entry");
  if (num_shards == 0 || !out_manager) {
    LOG_DEBUG("c_orm_shard_manager_init: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_shard_manager_init: exit");
    return rc;
  }

  manager = NULL;
  rc = c_orm_system_malloc(sizeof(c_orm_shard_manager_t), (void **)&manager);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_shard_manager_init: OOM");
    LOG_DEBUG("c_orm_shard_manager_init: exit");
    return rc;
  }

  manager->num_shards = num_shards;
  manager->nodes = NULL;
  rc = c_orm_system_calloc(num_shards, sizeof(c_orm_db_t *),
                           (void **)&manager->nodes);
  if (rc != C_ORM_OK) {
    c_orm_system_free(manager);
    LOG_DEBUG("c_orm_shard_manager_init: OOM");
    LOG_DEBUG("c_orm_shard_manager_init: exit");
    return rc;
  }

  for (i = 0; i < num_shards; i++) {
    manager->nodes[i] = NULL;
  }

  *out_manager = manager;
  {
    rc = C_ORM_OK;
    LOG_DEBUG("c_orm_shard_manager_init: exit");
    return rc;
  }
}

/**
 * @brief Bind a database connection (node) to a shard index.
 *
 * @param manager The shard manager.
 * @param index Shard index (0 to num_shards - 1).
 * @param node Live connection to the specific database instance.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_shard_manager_add_node(
    c_orm_shard_manager_t *manager, size_t index, c_orm_db_t *node) {
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_shard_manager_add_node: entry");
  if (!manager || !node || index >= manager->num_shards) {
    rc = C_ORM_ERROR_VALIDATION;
    LOG_DEBUG("c_orm_shard_manager_add_node: exit");
    return rc;
  }
  manager->nodes[index] = node;
  {
    rc = C_ORM_OK;
    LOG_DEBUG("c_orm_shard_manager_add_node: exit");
    return rc;
  }
}

/**
 * @brief Implement hash-based shard routing algorithm (Step 205).
 *
 * @param manager The shard manager.
 * @param routing_key String key to hash across available shards.
 * @param out_node Pointer to receive the specific db connection to execute
 * against.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_shard_route_hash(c_orm_shard_manager_t *manager, const char *routing_key,
                       c_orm_db_t **out_node) {
  c_orm_error_t rc;

  size_t hash_index = 0;
  const char *p;

  LOG_DEBUG("c_orm_shard_route_hash: entry");
  if (!manager || !routing_key || !out_node) {
    LOG_DEBUG("c_orm_shard_route_hash: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_shard_route_hash: exit");
    return rc;
  }

  /* djb2 string hash algorithm to deterministically route key across nodes */
  for (p = routing_key; *p; p++) {
    hash_index = ((hash_index << 5) + hash_index) + (size_t)(unsigned char)*p;
  }

  hash_index = hash_index % manager->num_shards;

  *out_node = manager->nodes[hash_index];
  if (!*out_node) {
    rc = C_ORM_ERROR_NOT_FOUND;
    LOG_DEBUG("c_orm_shard_route_hash: exit");
    return rc;
  } /* Node isn't initialized yet */

  {
    rc = C_ORM_OK;
    LOG_DEBUG("c_orm_shard_route_hash: exit");
    return rc;
  }
}

/**
 * @brief Free resources linked to the shard manager.
 *
 * @param manager The shard manager.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_shard_manager_free(c_orm_shard_manager_t *manager) {
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_shard_manager_free: entry");
  if (!manager) {
    return C_ORM_OK;
  }
  rc = c_orm_system_free(manager->nodes);
  if (rc != C_ORM_OK) {
    c_orm_system_free(manager);
    return rc;
  }
  return c_orm_system_free(manager);
}

/**
 * @brief Execute a scatter-gather find_all query across all shards in parallel.
 *
 * @param manager The shard manager.
 * @param meta The table metadata to query.
 * @param out_array Pointer to a void* to receive the dynamically allocated
 * combined results array.
 * @param out_count Pointer to receive the total number of combined results.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_scatter_gather_generic(
    c_orm_shard_manager_t *manager, const c_orm_table_meta_t *meta,
    void **out_array, size_t *out_count) {
  c_orm_error_t rc;
  size_t total_count = 0;
  size_t total_cap = 16;
  void *total_data = NULL;
  size_t i;
  c_orm_error_t last_err = C_ORM_OK;

  LOG_DEBUG("c_orm_scatter_gather_generic: entry");
  if (!manager || !meta || !out_array || !out_count) {
    LOG_DEBUG("c_orm_scatter_gather_generic: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_scatter_gather_generic: exit");
    return rc;
  }

  rc = c_orm_system_malloc(total_cap * meta->struct_size, &total_data);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_scatter_gather_generic: OOM");
    LOG_DEBUG("c_orm_scatter_gather_generic: exit");
    return rc;
  }

  for (i = 0; i < manager->num_shards; i++) {
    c_orm_db_t *shard_db = manager->nodes[i];
    void *shard_array = NULL;
    size_t shard_count = 0;

    if (!shard_db)
      continue;

    rc = c_orm_find_all_generic(shard_db, meta, &shard_array, &shard_count);
    if (rc != C_ORM_OK) {
      if (rc != C_ORM_ERROR_NOT_FOUND) {
        last_err = rc;
      }
      goto scatter_next_shard;
    }
    if (shard_count > 0) {
      if (total_count + shard_count > total_cap) {
        void *new_data = NULL;
        while (total_count + shard_count > total_cap) {
          total_cap *= 2;
        }
        rc = c_orm_system_realloc(total_data, total_cap * meta->struct_size,
                                  &new_data);
        if (rc != C_ORM_OK) {
          c_orm_system_free(total_data);
          c_orm_system_free(shard_array);
          LOG_DEBUG("c_orm_scatter_gather_generic: OOM");
          LOG_DEBUG("c_orm_scatter_gather_generic: exit");
          return rc;
        }
        total_data = new_data;
      }

      memcpy((char *)total_data + (total_count * meta->struct_size),
             shard_array, shard_count * meta->struct_size);
      c_orm_system_free(shard_array);
      total_count += shard_count;
    } else {
      c_orm_system_free(shard_array);
    }
  scatter_next_shard:;
  }

  if (last_err != C_ORM_OK && total_count == 0) {
    c_orm_system_free(total_data);
    {
      rc = last_err;
      LOG_DEBUG("c_orm_scatter_gather_generic: exit");
      return rc;
    }
  }

  *out_array = total_data;
  *out_count = total_count;
  {
    rc = C_ORM_OK;
    LOG_DEBUG("c_orm_scatter_gather_generic: exit");
    return rc;
  }
}

/**
 * @brief Escapes a string to prevent SQL injection vulnerabilities (Steps 241,
 * 242).
 *
 * Designed to sanitize input buffers bound dynamically into abstract struct
 * mapping pipelines when parameterized bindings are unavailable (e.g., dynamic
 * IN clause array construction).
 *
 * @param db Database connection handling dialect-specific escaping rules.
 * @param input Raw string to sanitize.
 * @param output Pre-allocated buffer to store escaped string.
 * @param output_size Size of the pre-allocated output buffer.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY if output buffer is too
 * small.
 */
C_ORM_EXPORT c_orm_error_t c_orm_escape_string(c_orm_db_t *db,
                                               const char *input, char *output,
                                               size_t output_size) {
  size_t i = 0, j = 0;
  int is_mysql;
  LOG_DEBUG("c_orm_escape_string: entry");
  if (!db || !input || !output || output_size == 0) {
    return C_ORM_ERROR_MEMORY;
  }
  is_mysql = (db->driver_name && strcmp(db->driver_name, "mysql") == 0) ? 1 : 0;

  while (input[i] != '\0') {
    if (j >= output_size - 1) {
      return C_ORM_ERROR_MEMORY;
    }
    if (input[i] == '\'' || (is_mysql && input[i] == '\\')) {
      if (j >= output_size - 2) {
        return C_ORM_ERROR_MEMORY;
      }
      output[j++] = input[i];
      output[j++] = input[i];
    } else {
      output[j++] = input[i];
    }
    i++;
  }
  output[j] = '\0';
  LOG_DEBUG("c_orm_escape_string: exit");
  return C_ORM_OK;
}

/**
 * @brief Formats current UTC wall-clock time as `YYYY-MM-DD HH:MM:SS` and
 * returns UNIX epoch seconds.
 * @param out_iso Output 20+ byte buffer receiving `YYYY-MM-DD HH:MM:SS`.
 * @param iso_cap Capacity of `out_iso` in bytes.
 * @param out_epoch Output UNIX epoch timestamp.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t c_orm_now_utc(char *out_iso, size_t iso_cap,
                                   c_orm_int64_t *out_epoch) {
  time_t now = time(NULL);
  struct tm tm_utc;
  *out_epoch = (c_orm_int64_t)now;
#if defined(_MSC_VER)
  gmtime_s(&tm_utc, &now);
#else
  tm_utc = *gmtime(&now);
#endif
  return C_ORM_SPRINTF(out_iso, iso_cap, "%04d-%02d-%02d %02d:%02d:%02d",
                       tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
                       tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec);
}

/**
 * @brief Applies UTC timestamp values to `created_at` and/or `updated_at`
 * columns on `obj`.
 * @param meta Table metadata.
 * @param obj Target struct instance.
 * @param set_created Non-zero to stamp `created_at` in addition to
 * `updated_at`.
 * @return C_ORM_OK on success, or allocation error code.
 */
static c_orm_error_t c_orm_apply_timestamp_cols(const c_orm_table_meta_t *meta,
                                                void *obj, int set_created) {
  char iso[32];
  c_orm_int64_t epoch = 0;
  cdd_c_variant_t v_str;
  cdd_c_variant_t v_int;
  size_t i;
  c_orm_error_t rc;

  rc = c_orm_now_utc(iso, sizeof(iso), &epoch);
  if (rc != C_ORM_OK) {
    return rc;
  }
  memset(&v_str, 0, sizeof(v_str));
  v_str.type = CDD_C_VARIANT_TYPE_STRING;
  v_str.value.s_val = iso;
  memset(&v_int, 0, sizeof(v_int));
  v_int.type = CDD_C_VARIANT_TYPE_INT;
  v_int.value.i_val = epoch;

  for (i = 0; i < meta->num_columns; ++i) {
    const c_orm_column_meta_t *col = &meta->columns[i];
    if ((set_created && strcmp(col->name, "created_at") == 0) ||
        strcmp(col->name, "updated_at") == 0) {
      const cdd_c_variant_t *vp =
          (col->type == C_ORM_TYPE_INT64) ? &v_int : &v_str;
      rc = c_orm_set_field_value(meta, obj, col->name, vp);
      if (rc != C_ORM_OK) {
        return rc;
      }
    }
  }
  return C_ORM_OK;
}

/**
 * @brief Lifecycle before-insert hook applying `created_at` and `updated_at`
 * timestamps via `db->active_hook_meta`.
 * @param obj Target struct instance.
 * @param user_data Active `c_orm_db_t *` connection handle.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_ts_before_insert_hook(void *obj, void *user_data) {
  c_orm_db_t *db = (c_orm_db_t *)user_data;
  if (!obj || !db || !db->active_hook_meta) {
    return C_ORM_ERROR_MEMORY;
  }
  return c_orm_apply_timestamp_cols(db->active_hook_meta, obj, 1);
}

/**
 * @brief Lifecycle before-update hook applying `updated_at` timestamp via
 * `db->active_hook_meta`.
 * @param obj Target struct instance.
 * @param user_data Active `c_orm_db_t *` connection handle.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_ts_before_update_hook(void *obj, void *user_data) {
  c_orm_db_t *db = (c_orm_db_t *)user_data;
  if (!obj || !db || !db->active_hook_meta) {
    return C_ORM_ERROR_MEMORY;
  }
  return c_orm_apply_timestamp_cols(db->active_hook_meta, obj, 0);
}

/**
 * @brief Lifecycle before-delete hook executing soft-delete updates via
 * `db->active_hook_meta`.
 * @param obj Target struct instance.
 * @param user_data Active `c_orm_db_t *` connection handle.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_soft_delete_before_hook(void *obj, void *user_data) {
  c_orm_db_t *db = (c_orm_db_t *)user_data;
  if (!obj || !db || !db->active_hook_meta) {
    return C_ORM_ERROR_MEMORY;
  }
  return c_orm_exec_soft_delete(db, db->active_hook_meta, obj);
}

/**
 * @brief Executes soft-delete column updates (`deleted_at` / `is_deleted`) via
 * `c_orm_update_partial`.
 * @param db Database handle.
 * @param meta Table metadata.
 * @param obj Struct instance being soft-deleted.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t c_orm_exec_soft_delete(c_orm_db_t *db,
                                            const c_orm_table_meta_t *meta,
                                            void *obj) {
  char iso[32];
  c_orm_int64_t epoch = 0;
  const char *fields[2];
  size_t n_fields = 0;
  size_t i;
  cdd_c_variant_t v_str;
  cdd_c_variant_t v_int;
  cdd_c_variant_t v_one;
  c_orm_error_t rc;

  rc = c_orm_now_utc(iso, sizeof(iso), &epoch);
  if (rc != C_ORM_OK) {
    return rc;
  }
  memset(&v_str, 0, sizeof(v_str));
  v_str.type = CDD_C_VARIANT_TYPE_STRING;
  v_str.value.s_val = iso;
  memset(&v_int, 0, sizeof(v_int));
  v_int.type = CDD_C_VARIANT_TYPE_INT;
  v_int.value.i_val = epoch;
  memset(&v_one, 0, sizeof(v_one));
  v_one.type = CDD_C_VARIANT_TYPE_INT;
  v_one.value.i_val = 1;

  for (i = 0; i < meta->num_columns && n_fields < 2; ++i) {
    const c_orm_column_meta_t *col = &meta->columns[i];
    if (strcmp(col->name, "deleted_at") == 0) {
      const cdd_c_variant_t *vp =
          (col->type == C_ORM_TYPE_INT64) ? &v_int : &v_str;
      rc = c_orm_set_field_value(meta, obj, col->name, vp);
      if (rc != C_ORM_OK) {
        return rc;
      }
      fields[n_fields++] = col->name;
    } else if (strcmp(col->name, "is_deleted") == 0) {
      rc = c_orm_set_field_value(meta, obj, col->name, &v_one);
      if (rc != C_ORM_OK) {
        return rc;
      }
      fields[n_fields++] = col->name;
    }
  }
  return c_orm_update_partial(db, meta, obj, fields, n_fields);
}

/**
 * @brief Registers default hooks on the given table metadata to automatically
 * set 'updated_at' to the current timestamp on update, and 'created_at' on
 * insert.
 *
 * @param meta Table metadata to modify (must be mutable before first use).
 * @return C_ORM_OK on success, or C_ORM_ERROR_UNKNOWN if no timestamp columns
 * exist on `meta`.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_register_timestamp_hooks(c_orm_table_meta_t *meta) {
  size_t i;
  int found = 0;

  LOG_DEBUG("c_orm_register_timestamp_hooks: entry");
  if (!meta) {
    return C_ORM_ERROR_MEMORY;
  }
  for (i = 0; i < meta->num_columns; ++i) {
    if (strcmp(meta->columns[i].name, "created_at") == 0 ||
        strcmp(meta->columns[i].name, "updated_at") == 0) {
      found = 1;
      break;
    }
  }
  if (!found) {
    return C_ORM_ERROR_UNKNOWN;
  }
  meta->hooks[C_ORM_HOOK_BEFORE_INSERT] = c_orm_ts_before_insert_hook;
  meta->hooks[C_ORM_HOOK_BEFORE_UPDATE] = c_orm_ts_before_update_hook;
  LOG_DEBUG("c_orm_register_timestamp_hooks: exit");
  return C_ORM_OK;
}

/**
 * @brief Registers a soft-delete hook on the given table metadata.
 * Instead of deleting the row, it will update 'deleted_at' or 'is_deleted'.
 *
 * @param meta Table metadata to modify.
 * @return C_ORM_OK on success, or C_ORM_ERROR_UNKNOWN if no soft-delete column
 * exists on `meta`.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_register_soft_delete_hook(c_orm_table_meta_t *meta) {
  size_t i;
  int found = 0;

  LOG_DEBUG("c_orm_register_soft_delete_hook: entry");
  if (!meta) {
    return C_ORM_ERROR_MEMORY;
  }
  for (i = 0; i < meta->num_columns; ++i) {
    if (strcmp(meta->columns[i].name, "deleted_at") == 0 ||
        strcmp(meta->columns[i].name, "is_deleted") == 0) {
      found = 1;
      break;
    }
  }
  if (!found) {
    return C_ORM_ERROR_UNKNOWN;
  }
  meta->hooks[C_ORM_HOOK_BEFORE_DELETE] = c_orm_soft_delete_before_hook;
  LOG_DEBUG("c_orm_register_soft_delete_hook: exit");
  return C_ORM_OK;
}

/**
 * @brief Partially update an object in the database.
 * @param db Database handle.
 * @param meta Table metadata.
 * @param obj Object containing updated values and ID.
 * @param fields Array of column names to update.
 * @param num_fields Number of columns in fields array.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_update_partial(c_orm_db_t *db,
                                                const c_orm_table_meta_t *meta,
                                                const void *obj,
                                                const char **fields,
                                                size_t num_fields) {
  c_orm_error_t rc;
  c_orm_query_t *query;
  int has_row;
  size_t i, j;
  int bind_idx = 1;
  int is_pg;
  const c_orm_column_meta_t *pk_col = NULL;
  c_orm_string_builder_t *sb;

  LOG_DEBUG("c_orm_update_partial: entry");
  if (!db || !meta || !obj || !fields || num_fields == 0) {
    return C_ORM_ERROR_MEMORY;
  }
  is_pg = (db->driver_name && strcmp(db->driver_name, "postgres") == 0) ? 1 : 0;
  if ((rc = c_orm_string_builder_init(&sb)) != C_ORM_OK)
    return rc;
  rc = c_orm_string_builder_append(sb, "UPDATE ");
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }
  rc = c_orm_string_builder_append(sb, meta->name);
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }
  rc = c_orm_string_builder_append(sb, " SET ");
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }

  for (i = 0; i < num_fields; i++) {
    if (i > 0) {
      rc = c_orm_string_builder_append(sb, ", ");
      if (rc != C_ORM_OK) {
        c_orm_string_builder_free(sb);
        return rc;
      }
    }
    rc = c_orm_string_builder_append(sb, fields[i]);
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }
    rc = c_orm_string_builder_append(sb, " = ");
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }
    rc = c_orm_append_placeholder(sb, is_pg, i + 1);
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }
  }

  for (i = 0; i < meta->num_columns; i++) {
    if (meta->columns[i].is_pk) {
      pk_col = &meta->columns[i];
      break;
    }
  }

  if (!pk_col) {
    c_orm_string_builder_free(sb);
    return C_ORM_ERROR_UNKNOWN;
  }

  rc = c_orm_string_builder_append(sb, " WHERE ");
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }
  rc = c_orm_string_builder_append(sb, pk_col->name);
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }
  rc = c_orm_string_builder_append(sb, " = ");
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }
  rc = c_orm_append_placeholder(sb, is_pg, num_fields + 1);
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }

  {
    const char *sql_str;
    if ((rc = c_orm_string_builder_get(sb, &sql_str)) != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }
    rc = c_orm_prepare_cached(db, sql_str, &query);
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }
  }
  c_orm_string_builder_free(sb);

  /* Bind SET fields across all c_orm_type_t types */
  for (i = 0; i < num_fields; i++) {
    for (j = 0; j < meta->num_columns; j++) {
      if (strcmp(meta->columns[j].name, fields[i]) == 0) {
        rc = bind_single_col(db, query, &meta->columns[j], obj, &bind_idx);
        if (rc != C_ORM_OK) {
          c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
          if (fin_rc != C_ORM_OK)
            return fin_rc;
          return rc;
        }
        break;
      }
    }
  }

  /* Bind PK (INT32, INT64, or STRING) */
  if (pk_col->type == C_ORM_TYPE_INT32 || pk_col->type == C_ORM_TYPE_INT64 ||
      pk_col->type == C_ORM_TYPE_STRING) {
    rc = bind_single_col(db, query, pk_col, obj, &bind_idx);
    if (rc != C_ORM_OK) {
      c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
      if (fin_rc != C_ORM_OK)
        return fin_rc;
      return rc;
    }
  } else {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    return C_ORM_ERROR_UNKNOWN;
  }

  rc = db->vtable->step(query, &has_row);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    return rc;
  }
  LOG_DEBUG("c_orm_update_partial: exit");
  return c_orm_finalize_cached(db, query);
}

/**
 * @brief Formats a dialect-aware `SELECT 1 FROM ... WHERE ... = ...` existence
 * query (`$1` on PostgreSQL, quoted identifiers on Db2/ODBC).
 * @param db Database connection handle.
 * @param table_name Target table name.
 * @param pk_name Primary key column name.
 * @param buf Output SQL buffer.
 * @param buf_cap Output SQL buffer capacity.
 * @return C_ORM_OK on success, or Safe CRT error code.
 */
static c_orm_error_t c_orm_format_exists_sql(const c_orm_db_t *db,
                                             const char *table_name,
                                             const char *pk_name, char *buf,
                                             size_t buf_cap) {
  if (db->driver_name && (strcmp(db->driver_name, "db2") == 0 ||
                          strcmp(db->driver_name, "odbc") == 0)) {
    return C_ORM_SPRINTF(buf, buf_cap, "SELECT 1 FROM \"%s\" WHERE \"%s\" = ?",
                         table_name, pk_name);
  }
  if (db->driver_name && strcmp(db->driver_name, "postgres") == 0) {
    return C_ORM_SPRINTF(buf, buf_cap, "SELECT 1 FROM %s WHERE %s = $1",
                         table_name, pk_name);
  }
  return C_ORM_SPRINTF(buf, buf_cap, "SELECT 1 FROM %s WHERE %s = ?",
                       table_name, pk_name);
}

/**
 * @brief Check if an object exists by INT32 ID.
 * @param db Database handle.
 * @param meta Table metadata.
 * @param id The ID.
 * @param out_exists Output boolean (1 = true, 0 = false).
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_exists_int32(c_orm_db_t *db,
                                              const c_orm_table_meta_t *meta,
                                              int32_t id, int *out_exists) {
  c_orm_error_t rc;

  char sql[256];
  c_orm_query_t *query;
  int has_row;
  const char *pk_name = "id";
  size_t i;

  LOG_DEBUG("c_orm_exists_int32: entry");
  if (!db || !meta || !out_exists) {
    LOG_DEBUG("c_orm_exists_int32: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_exists_int32: exit");
    return rc;
  }
  *out_exists = 0;
  if (!meta->query_select_by_pk) {
    rc = C_ORM_ERROR_UNKNOWN;
    LOG_DEBUG("c_orm_exists_int32: exit");
    return rc;
  }
  for (i = 0; i < meta->num_columns; i++) {
    if (meta->columns[i].is_pk && meta->columns[i].name) {
      pk_name = meta->columns[i].name;
      break;
    }
  }
  rc = c_orm_format_exists_sql(db, meta->name, pk_name, sql, sizeof(sql));
  if (rc != C_ORM_OK)
    return rc;
  rc = c_orm_prepare_cached(db, sql, &query);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_exists_int32: exit");
    return rc;
  }
  {
    c_orm_error_t tmp_rc = db->vtable->bind_int32(query, 1, id);
    if (tmp_rc != C_ORM_OK) {
      c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
      if (fin_rc != C_ORM_OK)
        return fin_rc;
      return tmp_rc;
    }
    tmp_rc = db->vtable->step(query, &has_row);
    if (tmp_rc != C_ORM_OK) {
      c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
      if (fin_rc != C_ORM_OK)
        return fin_rc;
      return tmp_rc;
    }
    *out_exists = has_row;
    LOG_DEBUG("c_orm_exists_int32: exit");
    return c_orm_finalize_cached(db, query);
  }
}

/**
 * @brief Check if an object exists by STRING ID.
 * @param db Database handle.
 * @param meta Table metadata.
 * @param id The ID.
 * @param out_exists Output boolean.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_exists_string(c_orm_db_t *db,
                                               const c_orm_table_meta_t *meta,
                                               const char *id,
                                               int *out_exists) {
  c_orm_error_t rc;
  char sql[256];
  c_orm_query_t *query;
  int has_row;
  const c_orm_column_meta_t *pk_col = NULL;
  size_t i;

  LOG_DEBUG("c_orm_exists_string: entry");
  if (!db || !meta || !id || !out_exists) {
    LOG_DEBUG("c_orm_exists_string: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_exists_string: exit");
    return rc;
  }
  *out_exists = 0;
  if (!meta->query_select_by_pk) {
    rc = C_ORM_ERROR_UNKNOWN;
    LOG_DEBUG("c_orm_exists_string: exit");
    return rc;
  }
  for (i = 0; i < meta->num_columns; i++) {
    if (meta->columns[i].is_pk) {
      pk_col = &meta->columns[i];
      break;
    }
  }
  if (!pk_col) {
    rc = C_ORM_ERROR_UNKNOWN;
    LOG_DEBUG("c_orm_exists_string: exit");
    return rc;
  }
  rc = c_orm_format_exists_sql(db, meta->name, pk_col->name, sql, sizeof(sql));
  if (rc != C_ORM_OK)
    return rc;
  rc = c_orm_prepare_cached(db, sql, &query);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_exists_string: exit");
    return rc;
  }
  {
    c_orm_error_t tmp_rc = db->vtable->bind_string(query, 1, id);
    if (tmp_rc != C_ORM_OK) {
      c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
      if (fin_rc != C_ORM_OK)
        return fin_rc;
      return tmp_rc;
    }
    tmp_rc = db->vtable->step(query, &has_row);
    if (tmp_rc != C_ORM_OK) {
      c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
      if (fin_rc != C_ORM_OK)
        return fin_rc;
      return tmp_rc;
    }
    *out_exists = has_row;
    LOG_DEBUG("c_orm_exists_string: exit");
    return c_orm_finalize_cached(db, query);
  }
}

/**
 * @brief Find all objects, paginated.
 * @param db Database handle.
 * @param meta Table metadata.
 * @param limit Max objects to return.
 * @param offset Number of objects to skip.
 * @param out_array Output array.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_find_all_paginated(c_orm_db_t *db, const c_orm_table_meta_t *meta,
                         void *out_array, size_t limit, size_t offset) {
  c_orm_error_t rc;
  char sql[256];
  c_orm_query_t *query;

  LOG_DEBUG("c_orm_find_all_paginated: entry");
  if (!db || !meta || !out_array) {
    LOG_DEBUG("c_orm_find_all_paginated: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_find_all_paginated: exit");
    return rc;
  }
  rc = C_ORM_SPRINTF(sql, sizeof(sql), "SELECT * FROM %s LIMIT %u OFFSET %u",
                     meta->name, (unsigned int)limit, (unsigned int)offset);
  if (rc != C_ORM_OK)
    return rc;
  rc = c_orm_prepare_cached(db, sql, &query);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_find_all_paginated: exit");
    return rc;
  }

  rc = c_orm_hydrate_all(db, query, meta, out_array);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK) {
      return fin_rc;
    }
    return rc;
  }
  LOG_DEBUG("c_orm_find_all_paginated: exit");
  return c_orm_finalize_cached(db, query);
}

/**
 * @brief Performs a paginated lazy load for a specific relationship.
 *
 * Appends LIMIT and OFFSET specifically useful for HasMany and ManyToMany.
 *
 * @param db Database connection.
 * @param parent_meta Metadata for the parent table/struct.
 * @param parent_obj Pointer to the parent object containing the relationship.
 * @param relation_name The name of the relationship field to load.
 * @param limit Maximum number of records to return.
 * @param offset Number of records to skip.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_lazy_load_paginated(
    c_orm_db_t *db, const c_orm_table_meta_t *parent_meta, void *parent_obj,
    const char *relation_name, size_t limit, size_t offset) {
  c_orm_error_t rc;
  const c_orm_relation_meta_t *rel = NULL;
  LOG_DEBUG("c_orm_lazy_load_paginated: entry");
  if (!db || !parent_meta || !parent_obj || !relation_name) {
    LOG_DEBUG("c_orm_lazy_load_paginated: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_lazy_load_paginated: exit");
    return rc;
  }

  rc = find_relation_meta(parent_meta, relation_name, &rel);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_lazy_load_paginated: exit");
    return rc;
  }

  LOG_DEBUG("c_orm_lazy_load_paginated: exit");
  return c_orm_load_relation_ext(db, parent_obj, parent_meta,
                                 (size_t)(rel - parent_meta->relations), limit,
                                 offset);
}

/**
 * @brief Performs a lazy load for a specific relationship on a given object.
 *
 * @param db Database connection.
 * @param parent_meta Metadata for the parent table/struct.
 * @param parent_obj Pointer to the parent object containing the relationship
 * proxy.
 * @param relation_name The name of the relationship field to load.
 * @return C_ORM_OK on success, C_ORM_ERROR_NOT_FOUND if the target doesn't
 * exist.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_lazy_load(c_orm_db_t *db, const c_orm_table_meta_t *parent_meta,
                void *parent_obj, const char *relation_name) {
  c_orm_error_t rc;

  {
    LOG_DEBUG("c_orm_lazy_load: entry");
    rc = c_orm_lazy_load_paginated(db, parent_meta, parent_obj, relation_name,
                                   0, 0);
    if (rc != C_ORM_OK)
      return rc;

    LOG_DEBUG("c_orm_lazy_load: exit");
    return rc;
  }
}

/**
 * @brief Attach a child object to a parent object's relationship.
 *
 * Supports One-to-Many and Many-to-Many.
 *
 * @param db Database connection.
 * @param parent_meta Metadata for the parent table/struct.
 * @param parent_obj Pointer to the parent object.
 * @param relation_name The name of the relationship field.
 * @param child_obj Pointer to the child object.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_attach(c_orm_db_t *db,
                                        const c_orm_table_meta_t *parent_meta,
                                        void *parent_obj,
                                        const char *relation_name,
                                        void *child_obj) {
  c_orm_error_t rc;

  size_t i;
  const c_orm_relation_meta_t *rel = NULL;

  LOG_DEBUG("c_orm_attach: entry");
  if (!db || !parent_meta || !parent_obj || !relation_name || !child_obj) {
    LOG_DEBUG("c_orm_attach: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_attach: exit");
    return rc;
  }

  for (i = 0; i < parent_meta->num_relations; ++i) {
    if (strcmp(parent_meta->relations[i].field_name, relation_name) == 0) {
      rel = &parent_meta->relations[i];
      break;
    }
  }

  if (!rel || !rel->target_meta) {
    rc = C_ORM_ERROR_NOT_FOUND;
    LOG_DEBUG("c_orm_attach: exit");
    return rc;
  }

  if (rel->on_attach) {
    rc = rel->on_attach(parent_obj, child_obj, db);
    if (rc != C_ORM_OK) {
      {
        LOG_DEBUG("c_orm_attach: exit");
        return rc;
      }
    }
  }

  if (rel->type == C_ORM_RELATION_ONE_TO_MANY) {
    int64_t parent_pk = 0;
    rc = get_int_field(parent_meta, parent_obj, rel->local_key, &parent_pk);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_attach: exit");
      return rc;
    }

    rc =
        set_int_field(rel->target_meta, child_obj, rel->foreign_key, parent_pk);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_attach: exit");
      return rc;
    }

    LOG_DEBUG("c_orm_attach: exit");
    return c_orm_save(db, rel->target_meta, child_obj);
  } else if (rel->type == C_ORM_RELATION_MANY_TO_MANY) {
    char sql[512];
    c_orm_query_t *q;
    int64_t parent_pk = 0;
    int64_t child_pk = 0;
    int has_row;

    if (!rel->join_table || !rel->join_local_key || !rel->join_foreign_key) {
      rc = C_ORM_ERROR_UNKNOWN;
      LOG_DEBUG("c_orm_attach: exit");
      return rc;
    }

    rc = get_int_field(parent_meta, parent_obj, rel->local_key, &parent_pk);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_attach: exit");
      return rc;
    }

    rc =
        get_int_field(rel->target_meta, child_obj, rel->foreign_key, &child_pk);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_attach: exit");
      return rc;
    }

    rc = C_ORM_SPRINTF(sql, sizeof(sql),
                       "INSERT INTO %s (%s, %s) VALUES (?, ?)", rel->join_table,
                       rel->join_local_key, rel->join_foreign_key);
    if (rc != C_ORM_OK)
      return rc;
    rc = c_orm_prepare_cached(db, sql, &q);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_attach: exit");
      return rc;
    }

    rc = db->vtable->bind_int64(q, 1, parent_pk);
    if (rc != C_ORM_OK) {
      c_orm_error_t fin_rc = c_orm_finalize_cached(db, q);
      if (fin_rc != C_ORM_OK)
        return fin_rc;
      return rc;
    }
    rc = db->vtable->bind_int64(q, 2, child_pk);
    if (rc != C_ORM_OK) {
      c_orm_error_t fin_rc = c_orm_finalize_cached(db, q);
      if (fin_rc != C_ORM_OK)
        return fin_rc;
      return rc;
    }
    rc = db->vtable->step(q, &has_row);
    if (rc != C_ORM_OK) {
      c_orm_error_t fin_rc = c_orm_finalize_cached(db, q);
      if (fin_rc != C_ORM_OK)
        return fin_rc;
      return rc;
    }
    LOG_DEBUG("c_orm_attach: exit");
    return c_orm_finalize_cached(db, q);
  }

  LOG_DEBUG("c_orm_attach: exit");
  return C_ORM_ERROR_UNKNOWN;
}

/**
 * @brief Detach a child object from a parent object's relationship.
 *
 * Supports One-to-Many and Many-to-Many.
 *
 * @param db Database connection.
 * @param parent_meta Metadata for the parent table/struct.
 * @param parent_obj Pointer to the parent object.
 * @param relation_name The name of the relationship field.
 * @param child_obj Pointer to the child object.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_detach(c_orm_db_t *db,
                                        const c_orm_table_meta_t *parent_meta,
                                        void *parent_obj,
                                        const char *relation_name,
                                        void *child_obj) {
  c_orm_error_t rc;

  size_t i;
  const c_orm_relation_meta_t *rel = NULL;

  LOG_DEBUG("c_orm_detach: entry");
  if (!db || !parent_meta || !parent_obj || !relation_name || !child_obj) {
    LOG_DEBUG("c_orm_detach: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_detach: exit");
    return rc;
  }

  for (i = 0; i < parent_meta->num_relations; ++i) {
    if (strcmp(parent_meta->relations[i].field_name, relation_name) == 0) {
      rel = &parent_meta->relations[i];
      break;
    }
  }

  if (!rel || !rel->target_meta) {
    rc = C_ORM_ERROR_NOT_FOUND;
    LOG_DEBUG("c_orm_detach: exit");
    return rc;
  }

  if (rel->on_detach) {
    rc = rel->on_detach(parent_obj, child_obj, db);
    if (rc != C_ORM_OK) {
      {
        LOG_DEBUG("c_orm_detach: exit");
        return rc;
      }
    }
  }

  if (rel->type == C_ORM_RELATION_ONE_TO_MANY) {
    /* To detach a ONE_TO_MANY, we set the child's FK to 0/NULL and save it */
    rc = set_null_field(rel->target_meta, child_obj, rel->foreign_key);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_detach: exit");
      return rc;
    }

    LOG_DEBUG("c_orm_detach: exit");
    return c_orm_save(db, rel->target_meta, child_obj);
  } else if (rel->type == C_ORM_RELATION_MANY_TO_MANY) {
    char sql[512];
    c_orm_query_t *q;
    int64_t parent_pk = 0;
    int64_t child_pk = 0;
    int has_row;

    if (!rel->join_table || !rel->join_local_key || !rel->join_foreign_key) {
      rc = C_ORM_ERROR_UNKNOWN;
      LOG_DEBUG("c_orm_detach: exit");
      return rc;
    }

    rc = get_int_field(parent_meta, parent_obj, rel->local_key, &parent_pk);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_detach: exit");
      return rc;
    }

    rc =
        get_int_field(rel->target_meta, child_obj, rel->foreign_key, &child_pk);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_detach: exit");
      return rc;
    }

    rc = C_ORM_SPRINTF(
        sql, sizeof(sql), "DELETE FROM %s WHERE %s = ? AND %s = ?",
        rel->join_table, rel->join_local_key, rel->join_foreign_key);
    if (rc != C_ORM_OK)
      return rc;
    rc = c_orm_prepare_cached(db, sql, &q);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_detach: exit");
      return rc;
    }

    rc = db->vtable->bind_int64(q, 1, parent_pk);
    if (rc != C_ORM_OK) {
      c_orm_error_t fin_rc = c_orm_finalize_cached(db, q);
      if (fin_rc != C_ORM_OK)
        return fin_rc;
      return rc;
    }
    rc = db->vtable->bind_int64(q, 2, child_pk);
    if (rc != C_ORM_OK) {
      c_orm_error_t fin_rc = c_orm_finalize_cached(db, q);
      if (fin_rc != C_ORM_OK)
        return fin_rc;
      return rc;
    }

    rc = db->vtable->step(q, &has_row);
    if (rc != C_ORM_OK) {
      c_orm_error_t fin_rc = c_orm_finalize_cached(db, q);
      if (fin_rc != C_ORM_OK)
        return fin_rc;
      return rc;
    }
    LOG_DEBUG("c_orm_detach: exit");
    return c_orm_finalize_cached(db, q);
  }

  LOG_DEBUG("c_orm_detach: exit");
  return C_ORM_ERROR_UNKNOWN;
}

/**
 * @brief Sync a parent object's relationship with an array of children.
 *
 * Replaces the entire set of related children for Many-to-Many and One-to-Many.
 *
 * @param db Database connection.
 * @param parent_meta Metadata for the parent table/struct.
 * @param parent_obj Pointer to the parent object.
 * @param relation_name The name of the relationship field.
 * @param children_array Pointer to the contiguous array of children structs.
 * @param num_children The number of children in the array.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_sync(
    c_orm_db_t *db, const c_orm_table_meta_t *parent_meta, void *parent_obj,
    const char *relation_name, void *children_array, size_t num_children) {
  c_orm_error_t rc;

  size_t i;
  const c_orm_relation_meta_t *rel = NULL;

  LOG_DEBUG("c_orm_sync: entry");
  if (!db || !parent_meta || !parent_obj || !relation_name ||
      (!children_array && num_children > 0)) {
    LOG_DEBUG("c_orm_sync: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_sync: exit");
    return rc;
  }

  for (i = 0; i < parent_meta->num_relations; ++i) {
    if (strcmp(parent_meta->relations[i].field_name, relation_name) == 0) {
      rel = &parent_meta->relations[i];
      break;
    }
  }

  if (!rel || !rel->target_meta) {
    rc = C_ORM_ERROR_NOT_FOUND;
    LOG_DEBUG("c_orm_sync: exit");
    return rc;
  }

  if (rel->type == C_ORM_RELATION_ONE_TO_MANY) {
    char sql[512];
    c_orm_query_t *q;
    int64_t parent_pk = 0;
    int has_row;

    rc = get_int_field(parent_meta, parent_obj, rel->local_key, &parent_pk);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_sync: exit");
      return rc;
    }

    /* Unset all existing children's foreign keys */
    rc = C_ORM_SPRINTF(sql, sizeof(sql), "UPDATE %s SET %s = NULL WHERE %s = ?",
                       rel->target_meta->name, rel->foreign_key,
                       rel->foreign_key);
    if (rc != C_ORM_OK)
      return rc;
    rc = c_orm_prepare_cached(db, sql, &q);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_sync: exit");
      return rc;
    }

    rc = db->vtable->bind_int64(q, 1, parent_pk);
    if (rc != C_ORM_OK) {
      c_orm_error_t fin_rc = c_orm_finalize_cached(db, q);
      if (fin_rc != C_ORM_OK)
        return fin_rc;
      return rc;
    }
    rc = db->vtable->step(q, &has_row);
    if (rc != C_ORM_OK) {
      c_orm_error_t fin_rc = c_orm_finalize_cached(db, q);
      if (fin_rc != C_ORM_OK)
        return fin_rc;
      return rc;
    }
    rc = c_orm_finalize_cached(db, q);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_sync: exit");
      return rc;
    }

    /* Now set the new children's foreign keys */
    for (i = 0; i < num_children; i++) {
      void *child_obj =
          (char *)children_array + (i * rel->target_meta->struct_size);
      rc = set_int_field(rel->target_meta, child_obj, rel->foreign_key,
                         parent_pk);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_sync: exit");
        return rc;
      }
      rc = c_orm_save(db, rel->target_meta, child_obj);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_sync: exit");
        return rc;
      }
    }
    LOG_DEBUG("c_orm_sync: exit");
    return C_ORM_OK;
  } else if (rel->type == C_ORM_RELATION_MANY_TO_MANY) {
    char sql[512];
    c_orm_query_t *q;
    int64_t parent_pk = 0;
    int has_row;

    if (!rel->join_table || !rel->join_local_key || !rel->join_foreign_key) {
      rc = C_ORM_ERROR_UNKNOWN;
      LOG_DEBUG("c_orm_sync: exit");
      return rc;
    }

    rc = get_int_field(parent_meta, parent_obj, rel->local_key, &parent_pk);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_sync: exit");
      return rc;
    }

    /* Delete all existing links */
    rc = C_ORM_SPRINTF(sql, sizeof(sql), "DELETE FROM %s WHERE %s = ?",
                       rel->join_table, rel->join_local_key);
    if (rc != C_ORM_OK)
      return rc;
    rc = c_orm_prepare_cached(db, sql, &q);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_sync: exit");
      return rc;
    }

    rc = db->vtable->bind_int64(q, 1, parent_pk);
    if (rc != C_ORM_OK) {
      c_orm_error_t fin_rc = c_orm_finalize_cached(db, q);
      if (fin_rc != C_ORM_OK)
        return fin_rc;
      return rc;
    }
    rc = db->vtable->step(q, &has_row);
    if (rc != C_ORM_OK) {
      c_orm_error_t fin_rc = c_orm_finalize_cached(db, q);
      if (fin_rc != C_ORM_OK)
        return fin_rc;
      return rc;
    }
    rc = c_orm_finalize_cached(db, q);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_sync: exit");
      return rc;
    }

    /* Insert new links */
    rc = C_ORM_SPRINTF(sql, sizeof(sql),
                       "INSERT INTO %s (%s, %s) VALUES (?, ?)", rel->join_table,
                       rel->join_local_key, rel->join_foreign_key);
    if (rc != C_ORM_OK)
      return rc;
    rc = c_orm_prepare_cached(db, sql, &q);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_sync: exit");
      return rc;
    }

    for (i = 0; i < num_children; i++) {
      int64_t child_pk = 0;
      void *child_obj =
          (char *)children_array + (i * rel->target_meta->struct_size);

      rc = get_int_field(rel->target_meta, child_obj, rel->foreign_key,
                         &child_pk);
      if (rc != C_ORM_OK) {
        goto sync_m2m_err;
      }

      rc = db->vtable->bind_int64(q, 1, parent_pk);
      if (rc != C_ORM_OK) {
        goto sync_m2m_err;
      }
      rc = db->vtable->bind_int64(q, 2, child_pk);
      if (rc != C_ORM_OK) {
        goto sync_m2m_err;
      }
      rc = db->vtable->step(q, &has_row);
      if (rc != C_ORM_OK) {
        goto sync_m2m_err;
      }
      rc = db->vtable->reset(q);
      if (rc != C_ORM_OK) {
        goto sync_m2m_err;
      }
    }
    rc = c_orm_finalize_cached(db, q);
    if (rc != C_ORM_OK)
      return rc;
    LOG_DEBUG("c_orm_sync: exit");
    return C_ORM_OK;

  sync_m2m_err: {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, q);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    LOG_DEBUG("c_orm_sync: exit");
    return rc;
  }
  }

  LOG_DEBUG("c_orm_sync: exit");
  return C_ORM_ERROR_UNKNOWN;
}

/**
 * @brief Delete all objects from a table.
 * @param db Database handle.
 * @param meta Table metadata.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_delete_all(c_orm_db_t *db,
                                            const c_orm_table_meta_t *meta) {
  c_orm_error_t rc;

  char sql[256];
  LOG_DEBUG("c_orm_delete_all: entry");
  if (!db || !meta) {
    LOG_DEBUG("c_orm_delete_all: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_delete_all: exit");
    return rc;
  }
  rc = C_ORM_SPRINTF(sql, sizeof(sql), "DELETE FROM %s", meta->name);
  if (rc != C_ORM_OK)
    return rc;
  {
    rc = c_orm_execute_raw(db, sql);
    if (rc != C_ORM_OK)
      return rc;

    LOG_DEBUG("c_orm_delete_all: exit");
    return rc;
  }
}
/**
 * @brief Generic dynamically constructed insert.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param ptr Pointer to data struct to insert.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_insert_generic(c_orm_db_t *db,
                                                const c_orm_table_meta_t *meta,
                                                const void *ptr) {
  c_orm_error_t rc;

  c_orm_string_builder_t *sb;
  const char *sql_str;
  c_orm_query_t *query;
  size_t i;
  int bind_idx = 1;
  int has_row = 0;

  LOG_DEBUG("c_orm_insert_generic: entry");
  if (!db || !meta || !ptr) {
    LOG_DEBUG("c_orm_insert_generic: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_insert_generic: exit");
    return rc;
  }
  if (meta->is_view) {
    rc = C_ORM_ERROR_READ_ONLY;
    LOG_DEBUG("c_orm_insert_generic: exit");
    return rc;
  }

  if ((rc = c_orm_string_builder_init(&sb)) != C_ORM_OK)
    return rc;

  rc = c_orm_string_builder_append(sb, "INSERT INTO ");
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }
  rc = c_orm_append_dialect_table(sb, db, meta->name);
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }
  rc = c_orm_string_builder_append(sb, " (");
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }

  for (i = 0; i < meta->num_columns; i++) {
    if (i > 0) {
      rc = c_orm_string_builder_append(sb, ", ");
      if (rc != C_ORM_OK) {
        c_orm_string_builder_free(sb);
        return rc;
      }
    }
    rc = c_orm_append_dialect_col(sb, db, meta->columns[i].name);
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }
  }

  rc = c_orm_string_builder_append(sb, ") VALUES (");
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }

  for (i = 0; i < meta->num_columns; i++) {
    if (i > 0) {
      rc = c_orm_string_builder_append(sb, ", ");
      if (rc != C_ORM_OK) {
        c_orm_string_builder_free(sb);
        return rc;
      }
    }
    rc = c_orm_append_placeholder(
        sb,
        (db->driver_name && strcmp(db->driver_name, "postgres") == 0) ? 1 : 0,
        i + 1U);
    if (rc != C_ORM_OK) {
      c_orm_string_builder_free(sb);
      return rc;
    }
  }
  rc = c_orm_string_builder_append(sb, ")");
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }

  if ((rc = c_orm_string_builder_get(sb, &sql_str)) != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }

  rc = db->vtable->prepare(db, sql_str, &query);
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    LOG_DEBUG("c_orm_insert_generic: exit");
    return rc;
  }
  c_orm_string_builder_free(sb);

  rc = bind_row(db, query, meta, ptr, 0, &bind_idx);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = db->vtable->finalize(query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    LOG_DEBUG("c_orm_insert_generic: exit");
    return rc;
  }

  rc = db->vtable->step(query, &has_row);
  if (rc != C_ORM_OK) {
    if (rc != C_ORM_ERROR_NOT_FOUND) {
      c_orm_error_t fin_rc = db->vtable->finalize(query);
      if (fin_rc != C_ORM_OK)
        return fin_rc;
      LOG_DEBUG("c_orm_insert_generic: exit");
      return rc;
    }
  }
  rc = db->vtable->finalize(query);
  if (rc != C_ORM_OK)
    return rc;
  LOG_DEBUG("c_orm_insert_generic: exit");
  return C_ORM_OK;
}

/**
 * @brief Generic dynamically constructed get by int32 PK.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param pk_val Primary key integer value.
 * @param out_struct Output struct pointer receiving hydrated data.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_get_generic(c_orm_db_t *db,
                                             const c_orm_table_meta_t *meta,
                                             int32_t pk_val, void *out_struct) {
  c_orm_error_t rc;

  c_orm_string_builder_t *sb;
  const char *sql_str;
  c_orm_query_t *query;
  const c_orm_column_meta_t *pk_col = NULL;
  size_t i;
  int has_row;

  LOG_DEBUG("c_orm_get_generic: entry");
  if (!db || !meta || !out_struct) {
    LOG_DEBUG("c_orm_get_generic: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_get_generic: exit");
    return rc;
  }

  for (i = 0; i < meta->num_columns; i++) {
    if (meta->columns[i].is_pk) {
      pk_col = &meta->columns[i];
      break;
    }
  }

  if (!pk_col) {
    rc = C_ORM_ERROR_VALIDATION;
    LOG_DEBUG("c_orm_get_generic: exit");
    return rc;
  }

  if ((rc = c_orm_string_builder_init(&sb)) != C_ORM_OK)
    return rc;

  rc = c_orm_string_builder_append(sb, "SELECT * FROM ");
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }
  rc = c_orm_append_dialect_table(sb, db, meta->name);
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }
  rc = c_orm_string_builder_append(sb, " WHERE ");
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }
  rc = c_orm_append_dialect_col(sb, db, pk_col->name);
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }
  rc = c_orm_string_builder_append(sb, " = ");
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }
  rc = c_orm_append_placeholder(
      sb, (db->driver_name && strcmp(db->driver_name, "postgres") == 0) ? 1 : 0,
      1U);
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }

  if ((rc = c_orm_string_builder_get(sb, &sql_str)) != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }

  rc = db->vtable->prepare(db, sql_str, &query);
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);

    LOG_DEBUG("c_orm_get_generic: exit");
    return rc;
  }
  c_orm_string_builder_free(sb);

  rc = db->vtable->bind_int32(query, 1, pk_val);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = db->vtable->finalize(query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    LOG_DEBUG("c_orm_get_generic: exit");
    return rc;
  }

  rc = db->vtable->step(query, &has_row);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = db->vtable->finalize(query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    LOG_DEBUG("c_orm_get_generic: exit");
    return rc;
  }

  if (!has_row) {
    rc = db->vtable->finalize(query);
    if (rc != C_ORM_OK)
      return rc;
    LOG_DEBUG("c_orm_get_generic: exit");
    return C_ORM_ERROR_NOT_FOUND;
  }

  rc = c_orm_hydrate_row(db, query, meta, out_struct);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    return rc;
  }
  rc = db->vtable->finalize(query);
  if (rc != C_ORM_OK)
    return rc;
  LOG_DEBUG("c_orm_get_generic: exit");
  return C_ORM_OK;
}

/**
 * @brief Generic dynamically constructed find_all with array allocation.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param out_array Pointer receiving allocated array of hydrated structs.
 * @param out_count Pointer receiving count of returned items.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_find_all_generic(c_orm_db_t *db, const c_orm_table_meta_t *meta,
                       void **out_array, size_t *out_count) {
  c_orm_error_t rc;
  c_orm_string_builder_t *sb;
  const char *sql_str;
  c_orm_query_t *query;
  int has_row;
  size_t count = 0;
  size_t cap = 16;
  void *data;

  LOG_DEBUG("c_orm_find_all_generic: entry");
  if (!db || !meta || !out_array || !out_count) {
    LOG_DEBUG("c_orm_find_all_generic: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_find_all_generic: exit");
    return rc;
  }

  if ((rc = c_orm_string_builder_init(&sb)) != C_ORM_OK)
    return rc;

  rc = c_orm_string_builder_append(sb, "SELECT * FROM ");
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }
  rc = c_orm_append_dialect_table(sb, db, meta->name);
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }

  if ((rc = c_orm_string_builder_get(sb, &sql_str)) != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }

  rc = db->vtable->prepare(db, sql_str, &query);
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    LOG_DEBUG("c_orm_find_all_generic: exit");
    return rc;
  }
  c_orm_string_builder_free(sb);

  data = NULL;
  rc = c_orm_system_calloc(cap, meta->struct_size, &data);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = db->vtable->finalize(query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    LOG_DEBUG("c_orm_find_all_generic: OOM");
    return rc;
  }

  while (1) {
    rc = db->vtable->step(query, &has_row);
    if (rc != C_ORM_OK) {
      c_orm_error_t fin_rc;
      c_orm_system_free(data);
      ((struct Generic_Array *)out_array)->data = NULL;
      fin_rc = db->vtable->finalize(query);
      if (fin_rc != C_ORM_OK)
        return fin_rc;
      LOG_DEBUG("c_orm_find_all_generic: exit");
      return rc;
    }
    if (!has_row)
      break;
    if (count >= cap) {
      void *new_data = NULL;
      cap *= 2;
      rc = c_orm_system_realloc(data, cap * meta->struct_size, &new_data);
      if (rc != C_ORM_OK) {
        c_orm_error_t fin_rc;
        c_orm_system_free(data);
        *out_array = NULL;
        fin_rc = db->vtable->finalize(query);
        if (fin_rc != C_ORM_OK)
          return fin_rc;
        LOG_DEBUG("c_orm_find_all_generic: OOM");
        return rc;
      }
      memset((char *)new_data + ((cap / 2) * meta->struct_size), 0,
             (cap - (cap / 2)) * meta->struct_size);
      data = new_data;
    }

    memset((char *)data + (count * meta->struct_size), 0, meta->struct_size);
    rc = c_orm_hydrate_row(db, query, meta,
                           (char *)data + (count * meta->struct_size));
    if (rc != C_ORM_OK) {
      if (rc == C_ORM_ERROR_EXPIRED) {
        continue;
      }
      {
        c_orm_error_t fin_rc;
        c_orm_system_free(data);
        *out_array = NULL;
        fin_rc = db->vtable->finalize(query);
        if (fin_rc != C_ORM_OK)
          return fin_rc;
        LOG_DEBUG("c_orm_find_all_generic: exit");
        return rc;
      }
    }
    count++;
  }

  *out_array = data;
  *out_count = count;
  rc = db->vtable->finalize(query);
  if (rc != C_ORM_OK)
    return rc;
  LOG_DEBUG("c_orm_find_all_generic: exit");
  return C_ORM_OK;
}
/**
 * @brief Generic dynamically constructed get by string PK.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param pk_val String primary key value.
 * @param out_struct Output struct pointer receiving hydrated data.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_get_generic_string(c_orm_db_t *db, const c_orm_table_meta_t *meta,
                         const char *pk_val, void *out_struct) {
  c_orm_error_t rc;

  c_orm_string_builder_t *sb;
  const char *sql_str;
  c_orm_query_t *query;
  const c_orm_column_meta_t *pk_col = NULL;
  size_t i;
  int has_row;

  LOG_DEBUG("c_orm_get_generic_string: entry");
  if (!db || !meta || !pk_val || !out_struct) {
    LOG_DEBUG("c_orm_get_generic_string: OOM");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_get_generic_string: exit");
    return rc;
  }

  for (i = 0; i < meta->num_columns; i++) {
    if (meta->columns[i].is_pk) {
      pk_col = &meta->columns[i];
      break;
    }
  }

  if (!pk_col) {
    rc = C_ORM_ERROR_VALIDATION;
    LOG_DEBUG("c_orm_get_generic_string: exit");
    return rc;
  }

  if ((rc = c_orm_string_builder_init(&sb)) != C_ORM_OK)
    return rc;

  rc = c_orm_string_builder_append(sb, "SELECT * FROM ");
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }
  rc = c_orm_append_dialect_table(sb, db, meta->name);
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }
  rc = c_orm_string_builder_append(sb, " WHERE ");
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }
  rc = c_orm_append_dialect_col(sb, db, pk_col->name);
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }
  rc = c_orm_string_builder_append(sb, " = ");
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }
  rc = c_orm_append_placeholder(
      sb, (db->driver_name && strcmp(db->driver_name, "postgres") == 0) ? 1 : 0,
      1U);
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }

  if ((rc = c_orm_string_builder_get(sb, &sql_str)) != C_ORM_OK) {
    c_orm_string_builder_free(sb);
    return rc;
  }

  rc = db->vtable->prepare(db, sql_str, &query);
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(sb);

    LOG_DEBUG("c_orm_get_generic_string: exit");
    return rc;
  }
  c_orm_string_builder_free(sb);

  rc = db->vtable->bind_string(query, 1, pk_val);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = db->vtable->finalize(query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    LOG_DEBUG("c_orm_get_generic_string: exit");
    return rc;
  }

  rc = db->vtable->step(query, &has_row);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = db->vtable->finalize(query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    LOG_DEBUG("c_orm_get_generic_string: exit");
    return rc;
  }

  if (!has_row) {
    rc = db->vtable->finalize(query);
    if (rc != C_ORM_OK)
      return rc;
    LOG_DEBUG("c_orm_get_generic_string: exit");
    return C_ORM_ERROR_NOT_FOUND;
  }

  rc = c_orm_hydrate_row(db, query, meta, out_struct);
  if (rc != C_ORM_OK) {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK)
      return fin_rc;
    return rc;
  }
  rc = db->vtable->finalize(query);
  if (rc != C_ORM_OK)
    return rc;
  LOG_DEBUG("c_orm_get_generic_string: exit");
  return C_ORM_OK;
}

#ifdef __EMSCRIPTEN__

c_orm_error_t c_orm_wasm_init_fs(c_orm_wasm_sync_cb_t callback) {
  char script[512];
  c_orm_error_t rc;
  rc = C_ORM_SPRINTF(
      script, sizeof(script),
      "if (!FS.analyzePath('/data').exists) {"
      "  FS.mkdir('/data');"
      "}"
      "FS.mount(IDBFS, {}, '/data');"
      "FS.syncfs(true, function(err) {"
      "  var cb = %ld;"
      "  if (cb) {"
      "    var st = err ? %d : %d;"
      "    if (typeof dynCall_ii !== 'undefined') {"
      "      dynCall_ii(cb, st);"
      "    } else if (typeof Module !== 'undefined' && Module['dynCall_ii']) {"
      "      Module['dynCall_ii'](cb, st);"
      "    }"
      "  }"
      "});",
      (long)(uintptr_t)callback, (int)C_ORM_ERROR_IO, (int)C_ORM_OK);
  if (rc != C_ORM_OK) {
    return rc;
  }
  emscripten_run_script(script);
  return C_ORM_OK;
}
#endif
