/* clang-format off */
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
  strncpy(no_pk_fields[0].name, "val", 63);
  strncpy(no_pk_fields[0].type, "integer", 31);
  strncpy(no_pk_fields[1].name, "obj_index", 63);
  strncpy(no_pk_fields[1].type, "unknown", 31);
  strncpy(no_pk_fields[1].description, "[INDEX]", 255);
  spec.defined_schemas[1].size = 2;
  spec.defined_schemas[1].fields = no_pk_fields;

  big_pk_fields = calloc(1, sizeof(struct StructField));
  strncpy(big_pk_fields[0].name, "id", 63);
  strncpy(big_pk_fields[0].type, "integer", 31);
  strncpy(big_pk_fields[0].format, "int64", 31);
  strncpy(big_pk_fields[0].description, "[PK]", 255);
  spec.defined_schemas[2].size = 1;
  spec.defined_schemas[2].fields = big_pk_fields;

  strncpy(fields[0].name, "id", 63);
  strncpy(fields[0].type, "integer", 31);
  strncpy(fields[0].description, "[PK]", 255);

  strncpy(fields[1].name, "username", 63);
  strncpy(fields[1].type, "string", 31);
  strncpy(fields[1].description, "[UNIQUE]", 255);

  strncpy(fields[2].name, "email", 63);
  strncpy(fields[2].type, "string", 31);
  strncpy(fields[2].description, "[INDEX]", 255);

  strncpy(fields[3].name, "org_id", 63);
  strncpy(fields[3].type, "integer", 31);
  strncpy(fields[3].description, "[FK=Org]", 255);

  strncpy(fields[4].name, "score", 63);
  strncpy(fields[4].type, "number", 31);
  strncpy(fields[4].format, "float", 31);

  strncpy(fields[5].name, "is_active", 63);
  strncpy(fields[5].type, "boolean", 31);

  strncpy(fields[6].name, "profile", 63);
  strncpy(fields[6].type, "object", 31);
  strncpy(fields[6].ref, "Profile", 63);

  strncpy(fields[7].name, "data", 63);
  strncpy(fields[7].type, "string", 31);
  strncpy(fields[7].format, "date", 31);
  fields[7].schema_extra_json =
      "{\"x-db-schema\": {\"primary_key\": true, \"unique\": true, \"index\": "
      "true, \"fk\": \"Other\"}, \"x-cdd-shard-key\": true, "
      "\"x-cdd-shard-hash\": true, \"x-cdd-track-telemetry\": true, "
      "\"x-cdd-slow-query\": 100}";

  strncpy(fields[8].name, "big_id", 63);
  strncpy(fields[8].type, "integer", 31);
  strncpy(fields[8].format, "int64", 31);

  strncpy(fields[9].name, "small_id", 63);
  strncpy(fields[9].type, "integer", 31);
  strncpy(fields[9].format, "int32", 31);

  strncpy(fields[10].name, "double_val", 63);
  strncpy(fields[10].type, "number", 31);
  strncpy(fields[10].format, "double", 31);

  strncpy(fields[11].name, "datetime_val", 63);
  strncpy(fields[11].type, "string", 31);
  strncpy(fields[11].format, "date-time", 31);

  strncpy(fields[12].name, "unknown_val", 63);
  strncpy(fields[12].type, "unknown", 31);

  strncpy(fields[13].name, "unref_obj", 63);
  strncpy(fields[13].type, "object", 31);

  strncpy(fields[14].name, "malformed_fk", 63);
  strncpy(fields[14].type, "integer", 31);
  strncpy(fields[14].description, "[FK=Org_But_No_End_Bracket", 255);

  strncpy(fields[15].name, "huge_fk", 63);
  strncpy(fields[15].type, "integer", 31);
  {
    char huge_desc[300] = "[FK=";
    memset(huge_desc + 4, 'A', 150);
    huge_desc[154] = ']';
    huge_desc[155] = '\0';
    strncpy(fields[15].description, huge_desc, 255);
  }

  strncpy(fields[16].name, "bad_json", 63);
  strncpy(fields[16].type, "string", 31);
  fields[16].schema_extra_json = "{bad json}";

  strncpy(fields[17].name, "not_obj_json", 63);
  strncpy(fields[17].type, "string", 31);
  fields[17].schema_extra_json = "[\"array\"]";

  strncpy(fields[18].name, "no_schema_json", 63);
  strncpy(fields[18].type, "string", 31);
  fields[18].schema_extra_json = "{\"x-db-schema\": 1}";

  strncpy(fields[19].name, "schema_false_json", 63);
  strncpy(fields[19].type, "string", 31);
  fields[19].schema_extra_json =
      "{\"x-db-schema\": {\"primary_key\": false, \"unique\": false, "
      "\"index\": "
      "false, \"fk\": 1}, \"x-cdd-shard-key\": false, "
      "\"x-cdd-shard-hash\": false, \"x-cdd-track-telemetry\": false}";

  strncpy(fields[20].name, "schema_empty_json", 63);
  strncpy(fields[20].type, "string", 31);
  fields[20].schema_extra_json = "{\"x-db-schema\": {}}";

  ASSERT_EQ(EINVAL, openapi_orm_generate(NULL, &config));
  ASSERT_EQ(EINVAL, openapi_orm_generate(&spec, NULL));
  config.filename_base = NULL;
  ASSERT_EQ(EINVAL, openapi_orm_generate(&spec, &config));

  config.filename_base = "test_gen";
  config.model_header = "/dev/null";
  {
    int rc_err = openapi_orm_generate(&spec, &config);
    ASSERT_EQ_FMT(EIO, rc_err, "%d");
  }

  config.model_header = ".";
  {
    int rc_err = openapi_orm_generate(&spec, &config);
    ASSERT_EQ_FMT(EIO, rc_err, "%d");
  }

  config.model_header = "/invalid/path/test.h";
  {
    int rc_err = openapi_orm_generate(&spec, &config);
    ASSERT_EQ_FMT(EIO, rc_err, "%d");
  }

  config.model_header = NULL;
  config.filename_base = "/invalid/path/test";
  {
    int rc_err = openapi_orm_generate(&spec, &config);
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
