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

  memset(&spec, 0, sizeof(spec));
  memset(&config, 0, sizeof(config));
  memset(&sf, 0, sizeof(sf));

  fields = calloc(13, sizeof(struct StructField));

  config.filename_base = "test_gen";

  spec.n_defined_schemas = 3;
  spec.defined_schema_names = malloc(3 * sizeof(char *));
  spec.defined_schema_names[0] = "User";
  spec.defined_schema_names[1] = "NoPKModel";
  spec.defined_schema_names[2] = NULL;

  spec.defined_schemas = calloc(3, sizeof(struct StructFields));
  spec.defined_schemas[0].size = 13;
  spec.defined_schemas[0].fields = fields;

  no_pk_fields = calloc(2, sizeof(struct StructField));
  strncpy(no_pk_fields[0].name, "val", 63);
  strncpy(no_pk_fields[0].type, "integer", 31);
  strncpy(no_pk_fields[1].name, "obj_index", 63);
  strncpy(no_pk_fields[1].type, "unknown", 31);
  strncpy(no_pk_fields[1].description, "[INDEX]", 255);
  spec.defined_schemas[1].size = 2;
  spec.defined_schemas[1].fields = no_pk_fields;

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

  ASSERT_EQ(EINVAL, openapi_orm_generate(NULL, &config));
  ASSERT_EQ(EINVAL, openapi_orm_generate(&spec, NULL));
  config.filename_base = NULL;
  ASSERT_EQ(EINVAL, openapi_orm_generate(&spec, &config));

  config.filename_base = "test_gen";
  config.model_header = "/invalid/path/test.h";
  ASSERT_EQ(EIO, openapi_orm_generate(&spec, &config));

  config.model_header = NULL;
  config.filename_base = "/invalid/path/test";
  ASSERT_EQ(EIO, openapi_orm_generate(&spec, &config));

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

  config.model_header = "test_gen_models.h";
  ASSERT_EQ(0, openapi_orm_generate(&spec, &config));

  free(fields);
  free(no_pk_fields);
  free(spec.defined_schemas);
  free(spec.defined_schema_names);
  PASS();
}

SUITE(orm_gen_suite) { RUN_TEST(test_orm_gen_basic); }
