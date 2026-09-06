#if defined(__clang__) || defined(__GNUC__)
#endif
/* clang-format off */
#include "c_orm_safe_crt.h"
#include <errno.h>
#include <string.h>
#include "greatest.h"
#include "orm_gen.h"
#include "openapi/parse/openapi.h"
/* clang-format on */

static int oom_active = 0;
static int oom_countdown = 0;
static void *mock_malloc_oom(size_t size) {
  void *res = NULL;
  if (!oom_active || oom_countdown-- > 0)
    res = malloc(size);
  return res;
}

TEST test_orm_gen_basic(void) {
  struct OpenAPI_Spec spec;
  struct OpenApiClientConfig config;
  struct StructFields sf;
  struct StructField *fields;
  struct StructField *no_pk_fields;
  void *(*old_malloc)(size_t);

  struct StructField *big_pk_fields;

  memset(&spec, 0, sizeof(spec));
  memset(&config, 0, sizeof(config));
  memset(&sf, 0, sizeof(sf));

  fields = calloc(21, sizeof(struct StructField));

  config.filename_base = "test_gen";

  spec.n_defined_schemas = 4;
  spec.defined_schema_names = malloc(4 * sizeof(char *));
  spec.defined_schema_names[0] = "User";
  spec.defined_schema_names[1] = "NoPKModel";
  spec.defined_schema_names[2] = "BigPKModel";
  spec.defined_schema_names[3] = NULL;

  spec.defined_schemas = calloc(4, sizeof(struct StructFields));
  spec.defined_schemas[0].size = 21;
  spec.defined_schemas[0].fields = fields;

  no_pk_fields = calloc(2, sizeof(struct StructField));
  C_ORM_STRNCPY(no_pk_fields[0].name, sizeof(no_pk_fields[0].name), "val", 63);
  C_ORM_STRNCPY(no_pk_fields[0].type, sizeof(no_pk_fields[0].type), "integer",
                31);
  C_ORM_STRNCPY(no_pk_fields[1].name, sizeof(no_pk_fields[1].name), "obj_index",
                63);
  C_ORM_STRNCPY(no_pk_fields[1].type, sizeof(no_pk_fields[1].type), "unknown",
                31);
  C_ORM_STRNCPY(no_pk_fields[1].description,
                sizeof(no_pk_fields[1].description), "[INDEX]", 255);
  spec.defined_schemas[1].size = 2;
  spec.defined_schemas[1].fields = no_pk_fields;

  big_pk_fields = calloc(1, sizeof(struct StructField));
  C_ORM_STRNCPY(big_pk_fields[0].name, sizeof(big_pk_fields[0].name), "id", 63);
  C_ORM_STRNCPY(big_pk_fields[0].type, sizeof(big_pk_fields[0].type), "integer",
                31);
  C_ORM_STRNCPY(big_pk_fields[0].format, sizeof(big_pk_fields[0].format),
                "int64", 31);
  C_ORM_STRNCPY(big_pk_fields[0].description,
                sizeof(big_pk_fields[0].description), "[PK]", 255);
  spec.defined_schemas[2].size = 1;
  spec.defined_schemas[2].fields = big_pk_fields;

  C_ORM_STRNCPY(fields[0].name, sizeof(fields[0].name), "id", 63);
  C_ORM_STRNCPY(fields[0].type, sizeof(fields[0].type), "integer", 31);
  C_ORM_STRNCPY(fields[0].description, sizeof(fields[0].description), "[PK]",
                255);

  C_ORM_STRNCPY(fields[1].name, sizeof(fields[1].name), "username", 63);
  C_ORM_STRNCPY(fields[1].type, sizeof(fields[1].type), "string", 31);
  C_ORM_STRNCPY(fields[1].description, sizeof(fields[1].description),
                "[UNIQUE]", 255);

  C_ORM_STRNCPY(fields[2].name, sizeof(fields[2].name), "email", 63);
  C_ORM_STRNCPY(fields[2].type, sizeof(fields[2].type), "string", 31);
  C_ORM_STRNCPY(fields[2].description, sizeof(fields[2].description), "[INDEX]",
                255);

  C_ORM_STRNCPY(fields[3].name, sizeof(fields[3].name), "org_id", 63);
  C_ORM_STRNCPY(fields[3].type, sizeof(fields[3].type), "integer", 31);
  C_ORM_STRNCPY(fields[3].description, sizeof(fields[3].description),
                "[FK=Org]", 255);

  C_ORM_STRNCPY(fields[4].name, sizeof(fields[4].name), "score", 63);
  C_ORM_STRNCPY(fields[4].type, sizeof(fields[4].type), "number", 31);
  C_ORM_STRNCPY(fields[4].format, sizeof(fields[4].format), "float", 31);

  C_ORM_STRNCPY(fields[5].name, sizeof(fields[5].name), "is_active", 63);
  C_ORM_STRNCPY(fields[5].type, sizeof(fields[5].type), "boolean", 31);

  C_ORM_STRNCPY(fields[6].name, sizeof(fields[6].name), "profile", 63);
  C_ORM_STRNCPY(fields[6].type, sizeof(fields[6].type), "object", 31);
  C_ORM_STRNCPY(fields[6].ref, sizeof(fields[6].ref), "Profile", 63);

  C_ORM_STRNCPY(fields[7].name, sizeof(fields[7].name), "data", 63);
  C_ORM_STRNCPY(fields[7].type, sizeof(fields[7].type), "string", 31);
  C_ORM_STRNCPY(fields[7].format, sizeof(fields[7].format), "date", 31);
  fields[7].schema_extra_json =
      "{\"x-db-schema\": {\"primary_key\": true, \"unique\": true, \"index\": "
      "true, \"fk\": \"Other\"}, \"x-cdd-shard-key\": true, "
      "\"x-cdd-shard-hash\": true, \"x-cdd-track-telemetry\": true, "
      "\"x-cdd-slow-query\": 100}";

  C_ORM_STRNCPY(fields[8].name, sizeof(fields[8].name), "big_id", 63);
  C_ORM_STRNCPY(fields[8].type, sizeof(fields[8].type), "integer", 31);
  C_ORM_STRNCPY(fields[8].format, sizeof(fields[8].format), "int64", 31);

  C_ORM_STRNCPY(fields[9].name, sizeof(fields[9].name), "small_id", 63);
  C_ORM_STRNCPY(fields[9].type, sizeof(fields[9].type), "integer", 31);
  C_ORM_STRNCPY(fields[9].format, sizeof(fields[9].format), "int32", 31);

  C_ORM_STRNCPY(fields[10].name, sizeof(fields[10].name), "double_val", 63);
  C_ORM_STRNCPY(fields[10].type, sizeof(fields[10].type), "number", 31);
  C_ORM_STRNCPY(fields[10].format, sizeof(fields[10].format), "double", 31);

  C_ORM_STRNCPY(fields[11].name, sizeof(fields[11].name), "datetime_val", 63);
  C_ORM_STRNCPY(fields[11].type, sizeof(fields[11].type), "string", 31);
  C_ORM_STRNCPY(fields[11].format, sizeof(fields[11].format), "date-time", 31);

  C_ORM_STRNCPY(fields[12].name, sizeof(fields[12].name), "unknown_val", 63);
  C_ORM_STRNCPY(fields[12].type, sizeof(fields[12].type), "unknown", 31);

  C_ORM_STRNCPY(fields[13].name, sizeof(fields[13].name), "unref_obj", 63);
  C_ORM_STRNCPY(fields[13].type, sizeof(fields[13].type), "object", 31);

  C_ORM_STRNCPY(fields[14].name, sizeof(fields[14].name), "malformed_fk", 63);
  C_ORM_STRNCPY(fields[14].type, sizeof(fields[14].type), "integer", 31);
  C_ORM_STRNCPY(fields[14].description, sizeof(fields[14].description),
                "[FK=Org_But_No_End_Bracket", 255);

  C_ORM_STRNCPY(fields[15].name, sizeof(fields[15].name), "huge_fk", 63);
  C_ORM_STRNCPY(fields[15].type, sizeof(fields[15].type), "integer", 31);
  {
    char huge_desc[300] = "[FK=";
    memset(huge_desc + 4, 'A', 150);
    huge_desc[154] = ']';
    huge_desc[155] = '\0';
    C_ORM_STRNCPY(fields[15].description, sizeof(fields[15].description),
                  huge_desc, 255);
  }

  C_ORM_STRNCPY(fields[16].name, sizeof(fields[16].name), "bad_json", 63);
  C_ORM_STRNCPY(fields[16].type, sizeof(fields[16].type), "string", 31);
  fields[16].schema_extra_json = "{bad json}";

  C_ORM_STRNCPY(fields[17].name, sizeof(fields[17].name), "not_obj_json", 63);
  C_ORM_STRNCPY(fields[17].type, sizeof(fields[17].type), "string", 31);
  fields[17].schema_extra_json = "[\"array\"]";

  C_ORM_STRNCPY(fields[18].name, sizeof(fields[18].name), "no_schema_json", 63);
  C_ORM_STRNCPY(fields[18].type, sizeof(fields[18].type), "string", 31);
  fields[18].schema_extra_json = "{\"x-db-schema\": 1}";

  C_ORM_STRNCPY(fields[19].name, sizeof(fields[19].name), "schema_false_json",
                63);
  C_ORM_STRNCPY(fields[19].type, sizeof(fields[19].type), "string", 31);
  fields[19].schema_extra_json =
      "{\"x-db-schema\": {\"primary_key\": false, \"unique\": false, "
      "\"index\": "
      "false, \"fk\": 1}, \"x-cdd-shard-key\": false, "
      "\"x-cdd-shard-hash\": false, \"x-cdd-track-telemetry\": false}";

  C_ORM_STRNCPY(fields[20].name, sizeof(fields[20].name), "schema_empty_json",
                63);
  C_ORM_STRNCPY(fields[20].type, sizeof(fields[20].type), "string", 31);
  fields[20].schema_extra_json = "{\"x-db-schema\": {}}";

  ASSERT_EQ(EINVAL, openapi_orm_generate(NULL, &config));
  ASSERT_EQ(EINVAL, openapi_orm_generate(&spec, NULL));
  config.filename_base = NULL;
  ASSERT_EQ(EINVAL, openapi_orm_generate(&spec, &config));

  config.filename_base = "test_gen";
  config.model_header = "invalid_dir/dev_null";
  {
    c_orm_error_t rc_err = openapi_orm_generate(&spec, &config);
    ASSERT_EQ_FMT(EIO, rc_err, "%d");
  }

  config.model_header = "invalid_dir/test2.h";
  {
    c_orm_error_t rc_err = openapi_orm_generate(&spec, &config);
    ASSERT_EQ_FMT(EIO, rc_err, "%d");
  }

  config.model_header = "invalid_dir/path/test.h";
  {
    c_orm_error_t rc_err = openapi_orm_generate(&spec, &config);
    ASSERT_EQ_FMT(EIO, rc_err, "%d");
  }

  config.model_header = NULL;
  config.filename_base = "invalid_dir/path/test";
  {
    c_orm_error_t rc_err = openapi_orm_generate(&spec, &config);
    ASSERT_EQ_FMT(EIO, rc_err, "%d");
  }

  config.model_header = NULL;
  config.filename_base = "test_gen";
  ASSERT_EQ(0, openapi_orm_generate(&spec, &config));

  old_malloc = c_orm_malloc;
  c_orm_set_allocators(mock_malloc_oom, c_orm_realloc, c_orm_free);
  oom_active = 1;
  oom_countdown = 0;
  ASSERT_EQ(ENOMEM, openapi_orm_generate(&spec, &config));

  config.model_header = "test_gen_models.h";
  oom_countdown = 0;
  ASSERT_EQ(ENOMEM, openapi_orm_generate(&spec, &config));

  oom_active = 0;
  c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);

  config.model_header = "valid.h";
  config.filename_base = "invalid/dir/base";
  ASSERT_EQ(0, openapi_orm_generate(&spec, &config));

  config.model_header = "test_gen_models.h";
  config.filename_base = "test_gen";
  ASSERT_EQ(0, openapi_orm_generate(&spec, &config));

  free(fields);
  free(no_pk_fields);
  free(big_pk_fields);
  free(spec.defined_schemas);
  free(spec.defined_schema_names);
  PASS();
}

SUITE(orm_gen_suite) { RUN_TEST(test_orm_gen_basic); }

#if defined(__clang__) || defined(__GNUC__)
#endif
