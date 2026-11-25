/**
 * @file orm_gen.c
 * @brief Implementation of ORM wrapper generation.
 *
 * Emits c-orm database bindings from an OpenAPI definition.
 */

/* clang-format off */
#include "orm_gen.h"
#include "classes/emit/struct.h"
#include "cdd_c_orm_meta.h"
#include "c_cdd/json_dom.h"
#include <errno.h>
#include <stdio.h>
#include "c_orm_safe_crt.h"
#include <stdlib.h>
#include <string.h>
#include "c_orm_log.h"
/* clang-format on */

/**
 * @brief Extracts a JSON_Object from a parsed JSON_Value if it is an object.
 * @param val Parsed JSON value.
 * @param out_obj Output JSON object pointer.
 * @return 1 if `val` is a JSON object, 0 otherwise.
 */
static int orm_gen_calc_json_obj(const JSON_Value *val, JSON_Object **out_obj) {
  return cdd_json_value_get_object(val, out_obj) == CDD_C_SUCCESS;
}

/**
 * @brief Extracts a nested JSON_Object property by key.
 * @param obj Parent JSON object.
 * @param key Property name.
 * @param out_sub Output nested JSON object pointer.
 * @return 1 if found and is an object, 0 otherwise.
 */
static int orm_gen_calc_json_subobj(const JSON_Object *obj, const char *key,
                                    JSON_Object **out_sub) {
  return cdd_json_object_get_object(obj, key, out_sub) == CDD_C_SUCCESS;
}

/**
 * @brief Extracts an optional boolean property from a JSON_Object.
 * @param obj Parent JSON object.
 * @param key Property name.
 * @param out_bool Output boolean value (0 or 1).
 * @return 1 if present and boolean, 0 otherwise.
 */
static int orm_gen_calc_json_bool(const JSON_Object *obj, const char *key,
                                  int *out_bool) {
  return cdd_json_object_get_boolean(obj, key, out_bool) == CDD_C_SUCCESS;
}

/**
 * @brief Extracts an optional string property from a JSON_Object.
 * @param obj Parent JSON object.
 * @param key Property name.
 * @param out_str Output string pointer.
 * @return 1 if present and string, 0 otherwise.
 */
static int orm_gen_calc_json_str(const JSON_Object *obj, const char *key,
                                 const char **out_str) {
  return cdd_json_object_get_string(obj, key, out_str) == CDD_C_SUCCESS;
}

/**
 * @brief Extracts an optional numeric property from a JSON_Object.
 * @param obj Parent JSON object.
 * @param key Property name.
 * @param out_num Output double value.
 * @return 1 if present and numeric, 0 otherwise.
 */
static int orm_gen_calc_json_num(const JSON_Object *obj, const char *key,
                                 double *out_num) {
  return cdd_json_object_get_number(obj, key, out_num) == CDD_C_SUCCESS;
}

/**
 * @brief Parses `schema_extra_json` into a `JSON_Value`, ignoring syntax
 * errors.
 * @param json_text Raw JSON string.
 * @param out_val Output parsed `JSON_Value` pointer on success.
 * @return 1 if parsed successfully, 0 otherwise.
 */
static int orm_gen_calc_json_parse_opt(const char *json_text,
                                       JSON_Value **out_val) {
  return cdd_json_parse_string(json_text, out_val) == CDD_C_SUCCESS;
}

/**
 * @brief Opens an optional generated test-stub file for writing if possible.
 * @param fp_ptr Output `FILE *` pointer.
 * @param path Target file path.
 * @return 1 if opened successfully, 0 otherwise.
 */
static int orm_gen_calc_fopen_opt(FILE **fp_ptr, const char *path) {
  return C_ORM_FOPEN(fp_ptr, path, "w") == C_ORM_OK;
}

/**
 * @brief Releases a parsed `JSON_Value` DOM tree silently.
 * @param val Parsed `JSON_Value` pointer.
 * @return Status code cast to int.
 */
static int orm_gen_calc_json_free(JSON_Value *val) {
  return (int)cdd_json_value_free(val);
}

/**
 * @brief Parses the database schema extension from the OpenAPI struct field.
 *
 * @param[in] field The OpenAPI struct field containing metadata and extensions.
 * @param[out] is_pk Pointer to int set to 1 if field is a primary key, 0
 * otherwise.
 * @param[out] is_unique Pointer to int set to 1 if field is unique, 0
 * otherwise.
 * @param[out] is_index Pointer to int set to 1 if field is indexed, 0
 * otherwise.
 * @param[out] fk_buf Buffer to store foreign key reference string.
 * @param[in] fk_buf_size Size of the foreign key buffer.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t check_db_schema(const struct StructField *field,
                                     int *is_pk, int *is_unique, int *is_index,
                                     char *fk_buf, size_t fk_buf_size) {
  *is_pk = 0;
  *is_unique = 0;
  *is_index = 0;
  if (fk_buf)
    fk_buf[0] = '\0';

  if (field->description[0]) {
    if (strstr(field->description, "[PK]") != NULL)
      *is_pk = 1;
    if (strstr(field->description, "[UNIQUE]") != NULL)
      *is_unique = 1;
    if (strstr(field->description, "[INDEX]") != NULL)
      *is_index = 1;
    if (fk_buf) {
      char *fk_start = strstr(field->description, "[FK=");
      if (fk_start) {
        char *fk_end = strchr(fk_start, ']');
        if (fk_end && (size_t)(fk_end - fk_start - 4) < fk_buf_size) {
          size_t fk_len = (size_t)(fk_end - fk_start - 4);
          c_orm_error_t crc =
              C_ORM_STRNCPY(fk_buf, fk_buf_size, fk_start + 4, fk_len);
          if (crc != C_ORM_OK) {
            return crc;
          }
          fk_buf[fk_len] = '\0';
        }
      }
    }
  }

  if (field->schema_extra_json) {
    JSON_Value *extras = NULL;
    JSON_Object *obj = NULL;
    JSON_Object *db = NULL;
    int bval = 0;
    const char *fk = NULL;
    if (!orm_gen_calc_json_parse_opt(field->schema_extra_json, &extras)) {
      return C_ORM_OK;
    }
    if (orm_gen_calc_json_obj(extras, &obj) &&
        orm_gen_calc_json_subobj(obj, "x-db-schema", &db)) {
      bval = 0;
      if (orm_gen_calc_json_bool(db, "primary_key", &bval) && bval == 1) {
        *is_pk = 1;
      }
      bval = 0;
      if (orm_gen_calc_json_bool(db, "unique", &bval) && bval == 1) {
        *is_unique = 1;
      }
      bval = 0;
      if (orm_gen_calc_json_bool(db, "index", &bval) && bval == 1) {
        *is_index = 1;
      }
      if (fk_buf && orm_gen_calc_json_str(db, "fk", &fk)) {
        c_orm_error_t crc =
            C_ORM_STRNCPY(fk_buf, fk_buf_size, fk, fk_buf_size - 1);
        if (crc != C_ORM_OK) {
          int fst = orm_gen_calc_json_free(extras);
          return (c_orm_error_t)((int)crc + (fst * 0));
        }
        fk_buf[fk_buf_size - 1] = '\0';
      }
    }
    {
      int fst = orm_gen_calc_json_free(extras);
      return (c_orm_error_t)((int)C_ORM_OK + (fst * 0));
    }
  }
  return C_ORM_OK;
}

/**
 * @brief Extracts PK/UNIQUE/INDEX flags without FK buffer copying.
 * @param field OpenAPI struct field.
 * @param is_pk Output PK flag.
 * @param is_unique Output UNIQUE flag.
 * @param is_index Output INDEX flag.
 * @return 0 on completion.
 */
static int orm_gen_calc_db_flags(const struct StructField *field, int *is_pk,
                                 int *is_unique, int *is_index) {
  return (int)check_db_schema(field, is_pk, is_unique, is_index, NULL, 0);
}

/**
 * @brief Parses and extracts cdd-c annotations from a struct field.
 *
 * @param[in] field The struct field to inspect.
 * @param[out] is_shard_key Pointer to an int that will receive 1 if this field
 * is the shard key.
 * @param[out] is_shard_hash Pointer to an int that will receive 1 if this field
 * uses a consistent hash.
 * @param[out] is_track_telemetry Pointer to an int that will receive 1 if
 * telemetry is tracked.
 * @param[out] slow_query_ms Pointer to an int that will receive the slow query
 * warning threshold.
 * @return 0 on completion.
 */
static int orm_gen_calc_cdd_annotations(const struct StructField *field,
                                        int *is_shard_key, int *is_shard_hash,
                                        int *is_track_telemetry,
                                        int *slow_query_ms) {
  *is_shard_key = 0;
  *is_shard_hash = 0;
  *is_track_telemetry = 0;
  *slow_query_ms = 0;

  if (field->schema_extra_json) {
    JSON_Value *extras = NULL;
    JSON_Object *obj = NULL;
    int bval = 0;
    double dval = 0.0;
    if (!orm_gen_calc_json_parse_opt(field->schema_extra_json, &extras)) {
      return 0;
    }
    if (orm_gen_calc_json_obj(extras, &obj)) {
      bval = 0;
      if (orm_gen_calc_json_bool(obj, "x-cdd-shard-key", &bval) && bval == 1) {
        *is_shard_key = 1;
      }
      bval = 0;
      if (orm_gen_calc_json_bool(obj, "x-cdd-shard-hash", &bval) && bval == 1) {
        *is_shard_hash = 1;
      }
      bval = 0;
      if (orm_gen_calc_json_bool(obj, "x-cdd-track-telemetry", &bval) &&
          bval == 1) {
        *is_track_telemetry = 1;
      }
      if (orm_gen_calc_json_num(obj, "x-cdd-slow-query", &dval)) {
        *slow_query_ms = (int)dval;
      }
    }
    return orm_gen_calc_json_free(extras);
  }
  return 0;
}

/** @brief Static c-orm column type constant names indexed by field kind. */
static const char *const k_orm_gen_c_orm_types[] = {
    "C_ORM_TYPE_INT64",     "C_ORM_TYPE_INT32",  "C_ORM_TYPE_FLOAT",
    "C_ORM_TYPE_DOUBLE",    "C_ORM_TYPE_BOOL",   "C_ORM_TYPE_DATE",
    "C_ORM_TYPE_TIMESTAMP", "C_ORM_TYPE_STRING", "C_ORM_TYPE_UNKNOWN"};

/**
 * @brief Computes the index into `k_orm_gen_c_orm_types` for an OpenAPI field.
 * @param field The OpenAPI struct field.
 * @return Integer table index in `0..8`.
 */
static int orm_gen_calc_c_orm_type_idx(const struct StructField *field) {
  if (strcmp(field->type, "integer") == 0) {
    if (field->format[0]) {
      if (strcmp(field->format, "int64") == 0) {
        return 0;
      }
      return 1;
    }
    return 1;
  }
  if (strcmp(field->type, "number") == 0) {
    if (strcmp(field->format, "float") == 0) {
      return 2;
    }
    return 3;
  }
  if (strcmp(field->type, "boolean") == 0) {
    return 4;
  }
  if (strcmp(field->type, "string") == 0) {
    if (strcmp(field->format, "date") == 0) {
      return 5;
    }
    if (strcmp(field->format, "date-time") == 0) {
      return 6;
    }
    return 7;
  }
  return 8;
}

/** @brief Static SQL DDL type literals indexed by OpenAPI field kind. */
static const char *const k_orm_gen_sql_types[] = {"BIGINT", "INTEGER", "REAL",
                                                  "TEXT", "BLOB"};

/**
 * @brief Computes the index into `k_orm_gen_sql_types` for an OpenAPI field.
 * @param field The OpenAPI struct field.
 * @return Integer table index in `0..4`.
 */
static int orm_gen_calc_sql_type_idx(const struct StructField *field) {
  if (strcmp(field->type, "integer") == 0) {
    if (field->format[0] && strcmp(field->format, "int64") == 0) {
      return 0;
    }
    return 1;
  }
  if (strcmp(field->type, "number") == 0) {
    return 2;
  }
  if (strcmp(field->type, "boolean") == 0) {
    return 1;
  }
  if (strcmp(field->type, "string") == 0) {
    return 3;
  }
  return 4;
}

/**
 * @brief Maps an OpenAPI struct field to a C type string representation
 * reentrantly using a caller-supplied scratch buffer for struct pointer refs.
 * @param field The OpenAPI struct field.
 * @param out_buf Caller-supplied buffer for dynamic struct pointer types.
 * @param buf_sz Capacity of out_buf in bytes.
 * @param out_type Output pointer receiving the C type string.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t openapi_type_to_c_type(const struct StructField *field,
                                            char *out_buf, size_t buf_sz,
                                            const char **out_type) {
  if (strcmp(field->type, "integer") == 0) {
    if (field->format[0]) {
      if (strcmp(field->format, "int64") == 0) {
        *out_type = "int64_t";
        return C_ORM_OK;
      }
      *out_type = "int32_t";
      return C_ORM_OK;
    }
    *out_type = "int32_t";
    return C_ORM_OK;
  }
  if (strcmp(field->type, "number") == 0) {
    if (strcmp(field->format, "float") == 0) {
      *out_type = "float";
      return C_ORM_OK;
    }
    *out_type = "double";
    return C_ORM_OK;
  }
  if (strcmp(field->type, "boolean") == 0) {
    *out_type = "bool";
    return C_ORM_OK;
  }
  if (strcmp(field->type, "string") == 0) {
    *out_type = "char*";
    return C_ORM_OK;
  }
  if (strcmp(field->type, "object") == 0) {
    if (field->ref[0]) {
      c_orm_error_t rc =
          C_ORM_SPRINTF(out_buf, buf_sz, "struct %s*", field->ref);
      if (rc != C_ORM_OK) {
        return rc;
      }
      *out_type = out_buf;
      return C_ORM_OK;
    }
  }
  *out_type = "void*";
  return C_ORM_OK;
}

/**
 * @brief Executes the openapi orm generate operation.
 * @param spec The parsed OpenAPI specification.
 * @param config Configuration options.
 * @return C_ORM_OK on success, or error code on failure.
 */
C_ORM_EXPORT c_orm_error_t openapi_orm_generate(
    const struct OpenAPI_Spec *spec, const struct OpenApiClientConfig *config) {
  char path_h[1024];
  char path_c[1024];
  FILE *fp_h = NULL;
  FILE *fp_c = NULL;
  size_t i, j;
  char *model_h = NULL;
  c_orm_error_t rc;

  if (!spec || !config || !config->filename_base)
    return C_ORM_ERROR_VALIDATION;

  if (config->model_header) {
    size_t header_len = strlen(config->model_header);
    rc = c_orm_system_malloc(header_len + 1, (void **)&model_h);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("C_ORM_ERROR_MEMORY: OOM\n");
      return rc;
    }
    rc = C_ORM_STRCPY(model_h, header_len + 1, config->model_header);
    if (rc != C_ORM_OK) {
      c_orm_system_free(model_h);
      return rc;
    }
  } else {
    size_t len = strlen(config->filename_base) + 10;
    rc = c_orm_system_malloc(len + 1, (void **)&model_h);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("C_ORM_ERROR_MEMORY: OOM\n");
      return rc;
    }
    rc = C_ORM_SPRINTF(model_h, len + 1, "%s_models.h", config->filename_base);
    if (rc != C_ORM_OK) {
      c_orm_system_free(model_h);
      return rc;
    }
  }

  rc = C_ORM_SPRINTF(path_h, sizeof(path_h), "%s", model_h);
  if (rc != C_ORM_OK) {
    c_orm_system_free(model_h);
    return rc;
  }
  rc = C_ORM_SPRINTF(path_c, sizeof(path_c), "%.*s.c",
                     (int)(strlen(model_h) - 2), model_h);
  if (rc != C_ORM_OK) {
    c_orm_system_free(model_h);
    return rc;
  }

  rc = C_ORM_FOPEN(&fp_h, path_h, "w");
  if (rc != C_ORM_OK) {
    c_orm_system_free(model_h);
    return rc;
  }
  rc = C_ORM_FOPEN(&fp_c, path_c, "w");
  if (rc != C_ORM_OK) {
    c_orm_system_free(model_h);
    fclose(fp_h);
    return rc;
  }

  /* Header */
  fprintf(fp_h, "/* Generated ORM Models from OpenAPI Specification */\n\n");
  fprintf(fp_h, "#ifndef C_ORM_MODELS_H\n");
  fprintf(fp_h, "#define C_ORM_MODELS_H\n\n");
  fprintf(fp_h, "#ifdef __cplusplus\n");
  fprintf(fp_h, "extern \"C\" {\n");
  fprintf(fp_h, "#endif /* __cplusplus */\n\n");

  fprintf(fp_h, "#if defined(_MSC_VER)\n");
  fprintf(fp_h, "# if _MSC_VER < 1600\n");
  fprintf(fp_h, "typedef signed __int8 int8_t;\n");
  fprintf(fp_h, "typedef unsigned __int8 uint8_t;\n");
  fprintf(fp_h, "typedef signed __int16 int16_t;\n");
  fprintf(fp_h, "typedef unsigned __int16 uint16_t;\n");
  fprintf(fp_h, "typedef signed __int32 int32_t;\n");
  fprintf(fp_h, "typedef unsigned __int32 uint32_t;\n");
  fprintf(fp_h, "typedef signed __int64 int64_t;\n");
  fprintf(fp_h, "typedef unsigned __int64 uint64_t;\n");
  fprintf(fp_h, "# else\n");
  fprintf(fp_h, "#  include <stdint.h>\n");
  fprintf(fp_h, "# endif\n");
  fprintf(fp_h, "# ifndef __cplusplus\n");
  fprintf(fp_h, "#  ifndef _STDBOOL_H\n");
  fprintf(fp_h, "#   define _STDBOOL_H\n");
  fprintf(fp_h, "#   ifndef __STDBOOL_H\n");
  fprintf(fp_h, "#    define __STDBOOL_H\n");
  fprintf(fp_h, "#   endif\n");
  fprintf(fp_h, "#   ifndef __bool_true_false_are_defined\n");
  fprintf(fp_h, "#    define __bool_true_false_are_defined 1\n");
  fprintf(fp_h, "#   endif\n");
  fprintf(fp_h, "typedef unsigned char bool;\n");
  fprintf(fp_h, "#   define true 1\n");
  fprintf(fp_h, "#   define false 0\n");
  fprintf(fp_h, "#  endif\n");
  fprintf(fp_h, "# endif\n");
  fprintf(fp_h, "#else\n");
  fprintf(fp_h, "# include <stdint.h>\n");
  fprintf(fp_h, "# ifndef __cplusplus\n");
  fprintf(fp_h, "#  ifndef _STDBOOL_H\n");
  fprintf(fp_h, "#   define _STDBOOL_H\n");
  fprintf(fp_h, "#   ifndef __STDBOOL_H\n");
  fprintf(fp_h, "#    define __STDBOOL_H\n");
  fprintf(fp_h, "#   endif\n");
  fprintf(fp_h, "#   ifndef __bool_true_false_are_defined\n");
  fprintf(fp_h, "#    define __bool_true_false_are_defined 1\n");
  fprintf(fp_h, "#   endif\n");
  fprintf(fp_h, "typedef unsigned char bool;\n");
  fprintf(fp_h, "#   define true 1\n");
  fprintf(fp_h, "#   define false 0\n");
  fprintf(fp_h, "#  endif\n");
  fprintf(fp_h, "# endif\n");
  fprintf(fp_h, "#endif\n\n");
  fprintf(fp_h, "/* clang-format "
                "off */\n");
  fprintf(fp_h, "#include <stddef.h>\n");
  fprintf(fp_h, "#include \"classes/emit/cdd_c_orm_meta.h\"\n");
  fprintf(fp_h, "#include \"c_orm_db.h\"\n");
  fprintf(fp_h, "#include \"c_orm_api.h\"\n");
  fprintf(fp_h, "/* clang-format "
                "on */\n\n");

  /* Source */
  fprintf(fp_c, "/* Generated ORM Models Implementation */\n\n");
  fprintf(fp_c, "/* clang-format "
                "off */\n");
  fprintf(fp_c, "#include \"%s\"\n", model_h);
  fprintf(fp_c, "#include <errno.h>\n");
  fprintf(fp_c, "#include <stdlib.h>\n");
  fprintf(fp_c, "#include <string.h>\n");
  fprintf(fp_c, "/* clang-format "
                "on */\n\n");

  for (i = 0; i < spec->n_defined_schemas; ++i) {
    const char *struct_name = spec->defined_schema_names[i];
    const struct StructFields *sf = &spec->defined_schemas[i];

    if (!struct_name)
      continue;

    /* Generate C Struct */
    fprintf(fp_h, "struct %s {\n", struct_name);
    for (j = 0; j < sf->size; ++j) {
      const struct StructField *field = &sf->fields[j];
      char type_buf[128];
      const char *c_type = NULL;
      rc = openapi_type_to_c_type(field, type_buf, sizeof(type_buf), &c_type);
      if (rc != C_ORM_OK) {
        goto gen_err;
      }
      fprintf(fp_h, "  %s %s;\n", c_type, field->name);
    }
    fprintf(fp_h, "};\n\n");

    /* ORM Metadata declarations */
    fprintf(fp_h, "extern const c_orm_column_meta_t %s_cols[%lu];\n",
            struct_name, (unsigned long)sf->size);
    fprintf(fp_h, "extern const c_orm_table_meta_t %s_meta;\n\n", struct_name);

    fprintf(fp_h,
            "struct %s_telemetry_payload {\n"
            "  bool track_telemetry;\n"
            "  int slow_query_warn_ms;\n"
            "  const char *query_name;\n"
            "};\n\n",
            struct_name);
    fprintf(fp_h,
            "struct %s_shard_routing {\n"
            "  const char *shard_key;\n"
            "  bool shard_hash_consistent;\n"
            "  int shard_offset_multipliers[10];\n" /* Precalculated directly */
            "};\n\n",
            struct_name);

    fprintf(fp_h, "extern const struct %s_telemetry_payload %s_telemetry;\n",
            struct_name, struct_name);
    fprintf(fp_h, "extern const struct %s_shard_routing %s_sharding;\n\n",
            struct_name, struct_name);
    fprintf(fp_h, "c_orm_error_t create_table_%s(c_orm_db_t* db);\n",
            struct_name);
    fprintf(fp_h, "c_orm_error_t %s_free(struct %s* obj);\n", struct_name,
            struct_name);
    fprintf(fp_h,
            "c_orm_error_t insert_%s(c_orm_db_t* db, const struct %s* obj);\n",
            struct_name, struct_name);
    fprintf(fp_h,
            "c_orm_error_t insert_async_%s(c_orm_db_t* db, const struct %s* "
            "obj, c_orm_async_cb_t cb, void* ctx);\n",
            struct_name, struct_name);
    fprintf(fp_h,
            "struct %s_worker_payload {\n"
            "  struct %s *items;\n"
            "  size_t count;\n"
            "  c_orm_db_t *db;\n"
            "  c_orm_async_cb_t cb;\n"
            "  void *ctx;\n"
            "};\n",
            struct_name, struct_name);
    for (j = 0; j < sf->size; ++j) {
      const struct StructField *field = &sf->fields[j];
      int is_pk, is_unique, is_index;
      char type_buf[128];
      const char *c_type = NULL;
      int dst = orm_gen_calc_db_flags(field, &is_pk, &is_unique, &is_index);
      (void)dst;
      if (is_pk || is_unique || is_index) {
        rc = openapi_type_to_c_type(field, type_buf, sizeof(type_buf), &c_type);
        if (rc != C_ORM_OK) {
          goto gen_err;
        }
        fprintf(fp_h,
                "c_orm_error_t get_%s_by_%s(c_orm_db_t* db, const %s %s, "
                "struct %s** out_obj);\n",
                struct_name, field->name, c_type, field->name, struct_name);
      }
    }
    fprintf(fp_h, "\n");

    /* ORM Metadata definitions */
    fprintf(fp_c, "const c_orm_column_meta_t %s_cols[%lu] = {\n", struct_name,
            (unsigned long)sf->size);
    for (j = 0; j < sf->size; ++j) {
      const struct StructField *field = &sf->fields[j];
      int is_pk, is_unique, is_index;
      char fk_buf[128];
      const char *orm_type = NULL;
      rc = check_db_schema(field, &is_pk, &is_unique, &is_index, fk_buf,
                           sizeof(fk_buf));
      if (rc != C_ORM_OK) {
        goto gen_err;
      }
      orm_type = k_orm_gen_c_orm_types[orm_gen_calc_c_orm_type_idx(field)];

      if (fk_buf[0]) {
        fprintf(fp_c,
                "  { \"%s\", %s, offsetof(struct %s, %s), %s, %s, \"%s\", "
                "false, false, 0, 0, 0, 0 }%s\n",
                field->name, orm_type, struct_name, field->name,
                is_pk ? "true" : "false", is_pk ? "false" : "true", fk_buf,
                j == sf->size - 1 ? "" : ",");
      } else {
        fprintf(fp_c,
                "  { \"%s\", %s, offsetof(struct %s, %s), %s, %s, NULL, false, "
                "false, 0, 0, 0, 0 }%s\n",
                field->name, orm_type, struct_name, field->name,
                is_pk ? "true" : "false", is_pk ? "false" : "true",
                j == sf->size - 1 ? "" : ",");
      }
    }
    fprintf(fp_c, "};\n\n");

    fprintf(fp_c, "const c_orm_table_meta_t %s_meta = {\n", struct_name);
    fprintf(fp_c, "  \"%s\",\n", struct_name);
    fprintf(fp_c, "  %s_cols,\n", struct_name);
    fprintf(fp_c, "  %lu,\n", (unsigned long)sf->size);
    fprintf(fp_c, "  sizeof(struct %s),\n", struct_name);
    fprintf(fp_c, "  \"SELECT * FROM %s\",\n", struct_name);
    {
      const char *pk_name = NULL;
      size_t k;
      int first_update = 1;
      for (k = 0; k < sf->size; ++k) {
        int is_pk, is_unique, is_index;
        int dst = orm_gen_calc_db_flags(&sf->fields[k], &is_pk, &is_unique,
                                        &is_index);
        (void)dst;
        if (is_pk) {
          pk_name = sf->fields[k].name;
          break;
        }
      }

      if (pk_name) {
        fprintf(fp_c, "  \"SELECT * FROM %s WHERE %s = ?\",\n", struct_name,
                pk_name);
      } else {
        fprintf(fp_c, "  NULL,\n");
      }

      fprintf(fp_c, "  \"INSERT INTO %s (", struct_name);
      for (k = 0; k < sf->size; ++k) {
        fprintf(fp_c, "%s%s", sf->fields[k].name,
                k == sf->size - 1 ? "" : ", ");
      }
      fprintf(fp_c, ") VALUES (");
      for (k = 0; k < sf->size; ++k) {
        fprintf(fp_c, "?%s", k == sf->size - 1 ? "" : ", ");
      }
      fprintf(fp_c, ")\",\n");

      if (pk_name) {
        fprintf(fp_c, "  \"UPDATE %s SET ", struct_name);
        for (k = 0; k < sf->size; ++k) {
          if (strcmp(sf->fields[k].name, pk_name) == 0)
            continue;
          if (!first_update) {
            fprintf(fp_c, ", ");
          }
          fprintf(fp_c, "%s = ?", sf->fields[k].name);
          first_update = 0;
        }
        fprintf(fp_c, " WHERE %s = ?\",\n", pk_name);
        fprintf(fp_c, "  \"DELETE FROM %s WHERE %s = ?\",\n", struct_name,
                pk_name);
        fprintf(fp_c, "  \"SELECT * FROM %s WHERE %s = ? FOR UPDATE\",\n",
                struct_name, pk_name);
      } else {
        fprintf(fp_c, "  NULL,\n  NULL,\n  NULL,\n");
      }
      fprintf(fp_c, "  false, false, 0, 0, { 0 }, NULL, 0\n");
    }
    fprintf(fp_c, "};\n\n");

    {
      const char *shard_key = "NULL";
      int shard_hash = 0;
      int track_tel = 0;
      int slow_ms = 0;
      size_t k;

      for (k = 0; k < sf->size; ++k) {
        int is_sk, is_sh, is_tt, s_ms;
        int ast = orm_gen_calc_cdd_annotations(&sf->fields[k], &is_sk, &is_sh,
                                               &is_tt, &s_ms);
        (void)ast;
        if (is_sk)
          shard_key = sf->fields[k].name;
        if (is_sh)
          shard_hash = 1;
        if (is_tt)
          track_tel = 1;
        if (s_ms > slow_ms)
          slow_ms = s_ms;
      }

      fprintf(fp_c,
              "const struct %s_telemetry_payload %s_telemetry = {\n"
              "  %s,\n"
              "  %d,\n"
              "  \"%s\"\n"
              "};\n\n",
              struct_name, struct_name, track_tel ? "true" : "false", slow_ms,
              struct_name);

      fprintf(fp_c,
              "const struct %s_shard_routing %s_sharding = {\n"
              "  %s%s%s,\n"
              "  %s,\n"
              "  { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 }\n"
              "};\n\n",
              struct_name, struct_name,
              strcmp(shard_key, "NULL") == 0 ? "" : "\"", shard_key,
              strcmp(shard_key, "NULL") == 0 ? "" : "\"",
              shard_hash ? "true" : "false");
    }

    /* CRUD Boilerplate definitions */
    fprintf(fp_c, "c_orm_error_t create_table_%s(c_orm_db_t* db) {\n",
            struct_name);
    fprintf(fp_c, "  c_orm_query_t *query = NULL;\n");
    fprintf(fp_c, "  int has_row = 0;\n");
    fprintf(fp_c, "  c_orm_error_t rc;\n");
    fprintf(fp_c, "  if (!db || !db->vtable || !db->vtable->prepare) return "
                  "C_ORM_ERROR_VALIDATION;\n");
    fprintf(fp_c,
            "  rc = db->vtable->prepare(db, \"CREATE TABLE IF NOT EXISTS "
            "%s (",
            struct_name);
    for (j = 0; j < sf->size; ++j) {
      int is_pk, is_unique, is_index;
      char fk_buf[128];
      const char *sql_type = NULL;
      rc = check_db_schema(&sf->fields[j], &is_pk, &is_unique, &is_index,
                           fk_buf, sizeof(fk_buf));
      if (rc != C_ORM_OK) {
        goto gen_err;
      }
      sql_type = k_orm_gen_sql_types[orm_gen_calc_sql_type_idx(&sf->fields[j])];
      fprintf(fp_c, "%s %s", sf->fields[j].name, sql_type);
      if (is_pk) {
        fprintf(fp_c, " PRIMARY KEY");
      } else if (is_unique) {
        fprintf(fp_c, " UNIQUE");
      }
      if (fk_buf[0]) {
        fprintf(fp_c, " REFERENCES %s(id)", fk_buf);
      }
      if (j + 1 < sf->size) {
        fprintf(fp_c, ", ");
      }
    }
    fprintf(fp_c, ")\", &query);\n");
    fprintf(fp_c, "  if (rc != C_ORM_OK) return rc;\n");
    fprintf(fp_c, "  rc = db->vtable->step(query, &has_row);\n");
    fprintf(fp_c, "  { c_orm_error_t frc = db->vtable->finalize(query); if (rc "
                  "== C_ORM_OK) rc = frc; }\n");
    fprintf(fp_c, "  if (rc != C_ORM_OK) return rc;\n");
    for (j = 0; j < sf->size; ++j) {
      int is_pk, is_unique, is_index;
      int dst =
          orm_gen_calc_db_flags(&sf->fields[j], &is_pk, &is_unique, &is_index);
      (void)dst;

      if (is_index) {
        fprintf(fp_c,
                "  rc = db->vtable->prepare(db, \"CREATE INDEX IF NOT EXISTS "
                "idx_%s_%s ON %s(%s)\", &query);\n",
                struct_name, sf->fields[j].name, struct_name,
                sf->fields[j].name);
        fprintf(fp_c, "  if (rc != C_ORM_OK) return rc;\n");
        fprintf(fp_c, "  rc = db->vtable->step(query, &has_row);\n");
        fprintf(fp_c, "  { c_orm_error_t frc = db->vtable->finalize(query); if "
                      "(rc == C_ORM_OK) rc = frc; }\n");
        fprintf(fp_c, "  if (rc != C_ORM_OK) return rc;\n");
      }
      if (is_unique) {
        fprintf(fp_c,
                "  rc = db->vtable->prepare(db, \"CREATE UNIQUE INDEX IF NOT "
                "EXISTS uidx_%s_%s ON %s(%s)\", &query);\n",
                struct_name, sf->fields[j].name, struct_name,
                sf->fields[j].name);
        fprintf(fp_c, "  if (rc != C_ORM_OK) return rc;\n");
        fprintf(fp_c, "  rc = db->vtable->step(query, &has_row);\n");
        fprintf(fp_c, "  { c_orm_error_t frc = db->vtable->finalize(query); if "
                      "(rc == C_ORM_OK) rc = frc; }\n");
        fprintf(fp_c, "  if (rc != C_ORM_OK) return rc;\n");
      }
    }
    fprintf(fp_c, "  return C_ORM_OK;\n");
    fprintf(fp_c, "}\n\n");

    fprintf(fp_c, "c_orm_error_t %s_free(struct %s* obj) {\n", struct_name,
            struct_name);
    fprintf(fp_c, "  if (!obj) return C_ORM_OK;\n");
    fprintf(fp_c, "  return c_orm_free_columns(&%s_meta, obj);\n", struct_name);
    fprintf(fp_c, "}\n\n");

    fprintf(fp_c,
            "c_orm_error_t insert_%s(c_orm_db_t* db, const struct %s* obj) {\n",
            struct_name, struct_name);
    fprintf(fp_c,
            "  if (%s_telemetry.track_telemetry && "
            "%s_telemetry.slow_query_warn_ms > 0) {\n"
            "    c_orm_error_t trc = c_orm_set_slow_query_threshold(db, "
            "(double)%s_telemetry.slow_query_warn_ms);\n"
            "    if (trc != C_ORM_OK) return trc;\n"
            "  }\n",
            struct_name, struct_name, struct_name);
    fprintf(fp_c, "  return c_orm_insert(db, &%s_meta, obj);\n", struct_name);
    fprintf(fp_c, "}\n\n");

    fprintf(fp_c,
            "c_orm_error_t insert_async_%s(c_orm_db_t* db, const struct %s* "
            "obj, c_orm_async_cb_t cb, void* ctx) {\n",
            struct_name, struct_name);
    fprintf(fp_c, "  return c_orm_insert_async(db, &%s_meta, obj, cb, ctx);\n",
            struct_name);
    fprintf(fp_c, "}\n\n");

    for (j = 0; j < sf->size; ++j) {
      const struct StructField *field = &sf->fields[j];
      int is_pk, is_unique, is_index;
      char type_buf[128];
      const char *c_type = NULL;
      int dst = orm_gen_calc_db_flags(field, &is_pk, &is_unique, &is_index);
      (void)dst;

      if (is_pk || is_unique || is_index) {
        rc = openapi_type_to_c_type(field, type_buf, sizeof(type_buf), &c_type);
        if (rc != C_ORM_OK) {
          goto gen_err;
        }
        fprintf(fp_c,
                "c_orm_error_t get_%s_by_%s(c_orm_db_t* db, const %s "
                "%s, struct %s** out_obj) {\n",
                struct_name, field->name, c_type, field->name, struct_name);
        if (strcmp(c_type, "int32_t") == 0 && is_pk) {
          fprintf(fp_c, "  c_orm_error_t rc;\n");
          fprintf(fp_c, "  if (!out_obj) return C_ORM_ERROR_VALIDATION;\n");
          fprintf(fp_c, "  *out_obj = NULL;\n");
          fprintf(fp_c,
                  "  if (c_orm_system_calloc(1, sizeof(struct %s), (void "
                  "**)out_obj) != C_ORM_OK || !*out_obj) return "
                  "C_ORM_ERROR_MEMORY;\n",
                  struct_name);
          fprintf(fp_c,
                  "  rc = c_orm_find_by_id_int32(db, &%s_meta, %s, "
                  "*out_obj);\n",
                  struct_name, field->name);
          fprintf(fp_c, "  if (rc != C_ORM_OK) {\n");
          fprintf(fp_c, "    c_orm_system_free(*out_obj);\n");
          fprintf(fp_c, "    *out_obj = NULL;\n");
          fprintf(fp_c, "  }\n");
          fprintf(fp_c, "  return rc;\n");
        } else if (strcmp(c_type, "int32_t") == 0 ||
                   strcmp(c_type, "int64_t") == 0 ||
                   strcmp(c_type, "bool") == 0 ||
                   strcmp(c_type, "double") == 0 ||
                   strcmp(c_type, "float") == 0) {
          const char *bind_fn = "bind_int32";
          const char *bind_expr_prefix = "";
          const char *bind_expr_suffix = "";
          if (strcmp(c_type, "int64_t") == 0) {
            bind_fn = "bind_int64";
          } else if (strcmp(c_type, "double") == 0 ||
                     strcmp(c_type, "float") == 0) {
            bind_fn = "bind_double";
            bind_expr_prefix = "(double)";
          } else if (strcmp(c_type, "bool") == 0) {
            bind_expr_suffix = " ? 1 : 0";
          }
          fprintf(fp_c, "  c_orm_query_t *q = NULL;\n");
          fprintf(fp_c, "  int has_row = 0;\n");
          fprintf(fp_c, "  c_orm_error_t rc;\n");
          fprintf(fp_c, "  c_orm_error_t frc;\n");
          fprintf(fp_c,
                  "  if (!db || !out_obj) return C_ORM_ERROR_VALIDATION;\n");
          fprintf(fp_c, "  *out_obj = NULL;\n");
          fprintf(fp_c,
                  "  rc = c_orm_prepare_cached(db, \"SELECT * FROM %s WHERE "
                  "%s = ? LIMIT 1\", &q);\n",
                  struct_name, field->name);
          fprintf(fp_c, "  if (rc != C_ORM_OK) return rc;\n");
          fprintf(fp_c, "  rc = db->vtable->%s(q, 1, %s%s%s);\n", bind_fn,
                  bind_expr_prefix, field->name, bind_expr_suffix);
          fprintf(fp_c, "  if (rc == C_ORM_OK) rc = db->vtable->step(q, "
                        "&has_row);\n");
          fprintf(fp_c, "  if (rc != C_ORM_OK) {\n");
          fprintf(fp_c, "    frc = c_orm_finalize_cached(db, q);\n");
          fprintf(fp_c, "    return rc;\n");
          fprintf(fp_c, "  }\n");
          fprintf(fp_c, "  if (!has_row) {\n");
          fprintf(fp_c, "    frc = c_orm_finalize_cached(db, q);\n");
          fprintf(
              fp_c,
              "    return frc != C_ORM_OK ? frc : C_ORM_ERROR_NOT_FOUND;\n");
          fprintf(fp_c, "  }\n");
          fprintf(fp_c,
                  "  if (c_orm_system_calloc(1, sizeof(struct %s), (void "
                  "**)out_obj) != C_ORM_OK || !*out_obj) {\n",
                  struct_name);
          fprintf(fp_c, "    frc = c_orm_finalize_cached(db, q);\n");
          fprintf(fp_c,
                  "    return frc != C_ORM_OK ? frc : C_ORM_ERROR_MEMORY;\n");
          fprintf(fp_c, "  }\n");
          fprintf(fp_c,
                  "  rc = c_orm_hydrate_row(db, q, &%s_meta, *out_obj);\n",
                  struct_name);
          fprintf(fp_c, "  frc = c_orm_finalize_cached(db, q);\n");
          fprintf(fp_c, "  if (rc == C_ORM_OK) rc = frc;\n");
          fprintf(fp_c, "  if (rc != C_ORM_OK) {\n");
          fprintf(fp_c, "    c_orm_system_free(*out_obj);\n");
          fprintf(fp_c, "    *out_obj = NULL;\n");
          fprintf(fp_c, "  }\n");
          fprintf(fp_c, "  return rc;\n");
        } else if (strcmp(c_type, "char*") == 0) {
          fprintf(fp_c, "  c_orm_error_t rc;\n");
          fprintf(fp_c, "  if (!out_obj) return C_ORM_ERROR_VALIDATION;\n");
          fprintf(fp_c, "  *out_obj = NULL;\n");
          fprintf(fp_c,
                  "  if (c_orm_system_calloc(1, sizeof(struct %s), (void "
                  "**)out_obj) != C_ORM_OK || !*out_obj) return "
                  "C_ORM_ERROR_MEMORY;\n",
                  struct_name);
          fprintf(fp_c,
                  "  rc = c_orm_find_one_by_string(db, &%s_meta, \"%s\", "
                  "%s, *out_obj);\n",
                  struct_name, field->name, field->name);
          fprintf(fp_c, "  if (rc != C_ORM_OK) {\n");
          fprintf(fp_c, "    c_orm_system_free(*out_obj);\n");
          fprintf(fp_c, "    *out_obj = NULL;\n");
          fprintf(fp_c, "  }\n");
          fprintf(fp_c, "  return rc;\n");
        } else {
          fprintf(fp_c, "  c_orm_query_t *q = NULL;\n");
          fprintf(fp_c, "  int has_row = 0;\n");
          fprintf(fp_c, "  c_orm_error_t rc;\n");
          fprintf(fp_c, "  c_orm_error_t frc;\n");
          fprintf(fp_c,
                  "  if (!db || !%s || !out_obj) return "
                  "C_ORM_ERROR_VALIDATION;\n",
                  field->name);
          fprintf(fp_c, "  *out_obj = NULL;\n");
          fprintf(fp_c,
                  "  rc = c_orm_prepare_cached(db, \"SELECT * FROM %s WHERE "
                  "%s = ? LIMIT 1\", &q);\n",
                  struct_name, field->name);
          fprintf(fp_c, "  if (rc != C_ORM_OK) return rc;\n");
          fprintf(fp_c, "  rc = db->vtable->bind_blob(q, 1, %s, 0);\n",
                  field->name);
          fprintf(fp_c, "  if (rc == C_ORM_OK) rc = db->vtable->step(q, "
                        "&has_row);\n");
          fprintf(fp_c, "  if (rc != C_ORM_OK) {\n");
          fprintf(fp_c, "    frc = c_orm_finalize_cached(db, q);\n");
          fprintf(fp_c, "    return frc != C_ORM_OK ? frc : rc;\n");
          fprintf(fp_c, "  }\n");
          fprintf(fp_c, "  if (!has_row) {\n");
          fprintf(fp_c, "    frc = c_orm_finalize_cached(db, q);\n");
          fprintf(
              fp_c,
              "    return frc != C_ORM_OK ? frc : C_ORM_ERROR_NOT_FOUND;\n");
          fprintf(fp_c, "  }\n");
          fprintf(fp_c,
                  "  if (c_orm_system_calloc(1, sizeof(struct %s), (void "
                  "**)out_obj) != C_ORM_OK || !*out_obj) {\n",
                  struct_name);
          fprintf(fp_c, "    frc = c_orm_finalize_cached(db, q);\n");
          fprintf(fp_c,
                  "    return frc != C_ORM_OK ? frc : C_ORM_ERROR_MEMORY;\n");
          fprintf(fp_c, "  }\n");
          fprintf(fp_c,
                  "  rc = c_orm_hydrate_row(db, q, &%s_meta, *out_obj);\n",
                  struct_name);
          fprintf(fp_c, "  frc = c_orm_finalize_cached(db, q);\n");
          fprintf(fp_c, "  if (rc == C_ORM_OK) rc = frc;\n");
          fprintf(fp_c, "  if (rc != C_ORM_OK) {\n");
          fprintf(fp_c, "    c_orm_system_free(*out_obj);\n");
          fprintf(fp_c, "    *out_obj = NULL;\n");
          fprintf(fp_c, "  }\n");
          fprintf(fp_c, "  return rc;\n");
        }
        fprintf(fp_c, "}\n\n");
      }
    }
  }

  fprintf(fp_h, "#ifdef __cplusplus\n}\n#endif /* __cplusplus */\n");
  fprintf(fp_h, "#endif /* C_ORM_MODELS_H */\n");

  /* Test Stub Generation */
  {
    char test_path[1024];
    FILE *fp_test = NULL;
    rc = C_ORM_SPRINTF(test_path, sizeof(test_path), "test_%s_models.c",
                       config->filename_base);
    if (rc != C_ORM_OK) {
      goto gen_err;
    }
    if (orm_gen_calc_fopen_opt(&fp_test, test_path)) {
      fprintf(fp_test,
              "/* Auto-generated greatest.h test stub for ORM models */\n\n");
      fprintf(fp_test, "/* clang-format "
                       "off */\n");
      fprintf(fp_test, "#include <stdlib.h>\n");
      fprintf(fp_test, "#include <string.h>\n");
      fprintf(fp_test, "#include <greatest.h>\n");
      fprintf(fp_test, "#include \"%s\"\n", model_h);
      fprintf(fp_test, "/* clang-format "
                       "on */\n\n");

      for (i = 0; i < spec->n_defined_schemas; i++) {
        const char *struct_name = spec->defined_schema_names[i];
        fprintf(fp_test, "TEST test_orm_%s_meta(void) {\n", struct_name);
        fprintf(fp_test, "  /* Check C-ORM meta structure fields */\n");
        fprintf(fp_test, "  ASSERT_STR_EQ(\"%s\", %s_meta.name);\n",
                struct_name, struct_name);
        fprintf(fp_test, "  PASS();\n");
        fprintf(fp_test, "}\n\n");
      }

      fprintf(fp_test, "SUITE(orm_models_suite) {\n");
      for (i = 0; i < spec->n_defined_schemas; i++) {
        fprintf(fp_test, "  RUN_TEST(test_orm_%s_meta);\n",
                spec->defined_schema_names[i]);
      }
      fprintf(fp_test, "}\n");
      fclose(fp_test);
    }
  }

  fclose(fp_h);
  fclose(fp_c);
  return c_orm_system_free(model_h);

gen_err:;
  fclose(fp_h);
  fclose(fp_c);
  c_orm_system_free(model_h);
  return rc;
}
