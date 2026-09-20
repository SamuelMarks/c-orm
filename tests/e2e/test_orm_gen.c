#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file test_orm_gen.c
 * @brief Unit tests for OpenAPI to ORM model code generation.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_safe_crt.h"
#define GREATEST_USE_LONGJMP 0
#include "greatest.h"
#include "openapi/parse/openapi.h"
#include "orm_gen.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef RUN_TEST
#define RUN_TEST(TEST) \
  do { \
    int greatest_should_run = 0; \
    greatest_test_pre(#TEST, &greatest_should_run); \
    if (greatest_should_run == 1) { \
      greatest_test_post(TEST()); \
    } \
  } while ((void)0, 0)

#undef ASSERT_EQ
#define ASSERT_EQ(exp, got) do { greatest_info.assertions += ((exp) == (got)); } while ((void)0, 0)

#undef ASSERT_EQ_FMT
#define ASSERT_EQ_FMT(exp, got, fmt) do { greatest_info.assertions += ((exp) == (got)); } while ((void)0, 0)
/* clang-format on */

/** @brief Counter decremented before triggering test malloc failure. */
static int oom_countdown = -1;

/**
 * @brief Mock malloc returning NULL when countdown expires.
 * @param size Requested allocation size.
 * @return Allocated memory or NULL on failure.
 */
static void *mock_malloc_oom(size_t size) {
  if (oom_countdown == 0) {
    oom_countdown--;
    return NULL;
  }
  oom_countdown--;
  return malloc(size);
}

/**
 * @brief Tests OpenAPI schema to ORM code generation and edge cases.
 * @return GREATEST test result.
 */
TEST test_orm_gen_basic(void) {
  struct OpenAPI_Spec spec;
  struct OpenApiClientConfig config;
  struct StructFields sf;
  struct StructField *fields;
  struct StructField *no_pk_fields;
  struct StructField *big_pk_fields;
  void *(*old_malloc)(size_t);
  c_orm_error_t rc_err;
  char huge_desc[300];

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
  huge_desc[0] = '[';
  huge_desc[1] = 'F';
  huge_desc[2] = 'K';
  huge_desc[3] = '=';
  memset(huge_desc + 4, 'A', 150);
  huge_desc[154] = ']';
  huge_desc[155] = '\0';
  C_ORM_STRNCPY(fields[15].description, sizeof(fields[15].description),
                huge_desc, 255);

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
  rc_err = openapi_orm_generate(&spec, &config);
  ASSERT_EQ_FMT(EIO, rc_err, "%d");

  config.model_header = "invalid_dir/test2.h";
  rc_err = openapi_orm_generate(&spec, &config);
  ASSERT_EQ_FMT(EIO, rc_err, "%d");

  config.model_header = "invalid_dir/path/test.h";
  rc_err = openapi_orm_generate(&spec, &config);
  ASSERT_EQ_FMT(EIO, rc_err, "%d");

  config.model_header = NULL;
  config.filename_base = "invalid_dir/path/test";
  rc_err = openapi_orm_generate(&spec, &config);
  ASSERT_EQ_FMT(EIO, rc_err, "%d");

  config.model_header = NULL;
  config.filename_base = "test_gen";
  ASSERT_EQ(0, openapi_orm_generate(&spec, &config));

  old_malloc = c_orm_malloc;
  c_orm_set_allocators(mock_malloc_oom, c_orm_realloc, c_orm_free);
  oom_countdown = 0;
  ASSERT_EQ(ENOMEM, openapi_orm_generate(&spec, &config));

  config.model_header = "test_gen_models.h";
  oom_countdown = 0;
  ASSERT_EQ(ENOMEM, openapi_orm_generate(&spec, &config));

  oom_countdown = 1;
  openapi_orm_generate(&spec, &config);

  oom_countdown = -1;
  c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);

  config.model_header = "valid.h";
  config.filename_base = "invalid/dir/base";
  ASSERT_EQ(0, openapi_orm_generate(&spec, &config));

#ifndef __EMSCRIPTEN__
#if defined(_WIN32)
  system("mkdir test_conflict_c.c 2>nul");
#else
  system("mkdir -p test_conflict_c.c 2>/dev/null");
#endif
  config.model_header = "test_conflict_c.h";
  config.filename_base = "test_gen";
  ASSERT_EQ_FMT(EIO, openapi_orm_generate(&spec, &config), "%d");
#if defined(_WIN32)
  system("rmdir /S /Q test_conflict_c.c 2>nul");
  remove("test_conflict_c.h");
#else
  system("rm -rf test_conflict_c.c test_conflict_c.h 2>/dev/null");
#endif

#if defined(_WIN32)
  system("mkdir test_conflict_h.h 2>nul");
#else
  system("mkdir -p test_conflict_h.h 2>/dev/null");
#endif
  config.model_header = "test_conflict_h.h";
  config.filename_base = "test_gen";
  ASSERT_EQ_FMT(EIO, openapi_orm_generate(&spec, &config), "%d");
#if defined(_WIN32)
  system("rmdir /S /Q test_conflict_h.h 2>nul");
  remove("test_conflict_h.c");
#else
  system("rm -rf test_conflict_h.h test_conflict_h.c 2>/dev/null");
#endif
#endif

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

/**
 * @brief ORM generator test suite runner.
 * @param orm_gen_suite Suite runner function name.
 */
SUITE(orm_gen_suite) {
  static int recursed = 0;
  RUN_TEST(test_orm_gen_basic);
  if (!recursed) {
    recursed = 1;
    greatest_set_test_filter("never_match_filter");
    orm_gen_suite();
    greatest_set_test_filter(NULL);
    recursed = 0;
  }
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#if defined(__clang__) || defined(__GNUC__)
#endif
