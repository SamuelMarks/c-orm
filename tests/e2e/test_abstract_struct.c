#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file test_abstract_struct.c
 * @brief Tests for abstract_struct generic representations.
 */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "abstract_struct.h"
#include "c_orm_sql.h"
#include "c_orm_sqlite.h"
#include "cdd_c_orm_meta.h"
#include "parson.h"
#include <errno.h>
#include <greatest.h>
#include <time.h>
#include "test_abstract_struct_oom.h"
/* clang-format on */

TEST test_abstract_struct_memory_layout(void) {
  cdd_c_abstract_struct_t astruct;
  ASSERT_EQ(0, cdd_c_abstract_struct_init(&astruct));
  ASSERT_EQ(0, astruct.count);
  ASSERT_EQ(0, astruct.capacity);
  ASSERT_EQ(NULL, astruct.kvs);
  PASS();
}

TEST test_variant_type_safety(void) {
  cdd_c_variant_t v1, v2;
  v1.type = CDD_C_VARIANT_TYPE_INT;
  v1.value.i_val = 42;

  v2.type = CDD_C_VARIANT_TYPE_STRING;
  v2.value.s_val = (char *)malloc(5);
  C_ORM_STRCPY(v2.value.s_val, 5, "test");

  ASSERT_EQ(CDD_C_VARIANT_TYPE_INT, v1.type);
  ASSERT_EQ(42, v1.value.i_val);

  ASSERT_EQ(CDD_C_VARIANT_TYPE_STRING, v2.type);
  ASSERT_STR_EQ("test", v2.value.s_val);

  cdd_c_variant_free(&v2);
  ASSERT_EQ(CDD_C_VARIANT_TYPE_NULL, v2.type);
  PASS();
}

TEST test_abstract_struct_set_get(void) {
  cdd_c_abstract_struct_t astruct;
  cdd_c_variant_t v_in, *v_out;
  int i;
  char key_buf[16];

  ASSERT_EQ(0, cdd_c_abstract_struct_init(&astruct));

  v_in.type = CDD_C_VARIANT_TYPE_INT;
  v_in.value.i_val = 100;

  ASSERT_EQ(0, cdd_c_abstract_set(&astruct, "my_int", &v_in));
  ASSERT_EQ(1, astruct.count);

  ASSERT_EQ(0, cdd_c_abstract_get(&astruct, "my_int", &v_out));
  ASSERT_EQ(CDD_C_VARIANT_TYPE_INT, v_out->type);
  ASSERT_EQ(100, v_out->value.i_val);

  /* Overwrite my_int */
  v_in.value.i_val = 200;
  ASSERT_EQ(0, cdd_c_abstract_set(&astruct, "my_int", &v_in));
  ASSERT_EQ(1, astruct.count); /* Still 1 */
  ASSERT_EQ(0, cdd_c_abstract_get(&astruct, "my_int", &v_out));
  ASSERT_EQ(200, v_out->value.i_val);

  /* Get missing key */
  ASSERT_EQ(EINVAL, cdd_c_abstract_get(&astruct, "missing", &v_out));

  /* Force resize */
  for (i = 0; i < 10; ++i) {
    C_ORM_SPRINTF(key_buf, sizeof(key_buf), "key_%d", i);
    v_in.value.i_val = i;
    ASSERT_EQ(0, cdd_c_abstract_set(&astruct, key_buf, &v_in));
  }
  ASSERT_EQ(11, astruct.count);

  ASSERT_EQ(0, cdd_c_abstract_struct_free(&astruct));
  PASS();
}

TEST test_abstract_struct_json_roundtrip(void) {
  cdd_c_abstract_struct_t astruct_out, astruct_in;
  cdd_c_variant_t v_in, *v_out;
  char *json_str = NULL;

  ASSERT_EQ(0, cdd_c_abstract_struct_init(&astruct_out));

  v_in.type = CDD_C_VARIANT_TYPE_INT;
  v_in.value.i_val = 100;
  ASSERT_EQ(0, cdd_c_abstract_set(&astruct_out, "my_int", &v_in));

  v_in.type = CDD_C_VARIANT_TYPE_STRING;
  v_in.value.s_val = (char *)"test_string";
  ASSERT_EQ(0, cdd_c_abstract_set(&astruct_out, "my_str", &v_in));

  v_in.type = CDD_C_VARIANT_TYPE_FLOAT;
  v_in.value.f_val = 3.14;
  ASSERT_EQ(0, cdd_c_abstract_set(&astruct_out, "my_float", &v_in));

  ASSERT_EQ(0, cdd_c_abstract_struct_to_json(&astruct_out, &json_str));
  ASSERT_NEQ(NULL, json_str);

  ASSERT_EQ(0, cdd_c_abstract_struct_from_json(json_str, &astruct_in));

  ASSERT_EQ(0, cdd_c_abstract_get(&astruct_in, "my_int", &v_out));
  ASSERT_EQ(CDD_C_VARIANT_TYPE_INT, v_out->type);
  ASSERT_EQ(100, v_out->value.i_val);

  ASSERT_EQ(0, cdd_c_abstract_get(&astruct_in, "my_str", &v_out));
  ASSERT_EQ(CDD_C_VARIANT_TYPE_STRING, v_out->type);
  ASSERT_STR_EQ("test_string", v_out->value.s_val);

  cdd_c_abstract_print(&astruct_in);

  json_free_serialized_string(json_str);
  ASSERT_EQ(0, cdd_c_abstract_struct_free(&astruct_out));
  ASSERT_EQ(0, cdd_c_abstract_struct_free(&astruct_in));
  PASS();
}

TEST test_abstract_hydrate(void) {
  cdd_c_abstract_struct_t astruct;
  cdd_c_column_meta_t cols[4];
  void *row_data[4];
  c_orm_int64_t mock_int = 42;
  double mock_float = 3.14159;
  char *mock_str = (char *)"Hello, Generic World!";
  char *mock_blob = (char *)"blob_data";
  cdd_c_variant_t *out_val;

  cols[0].name = "id";
  cols[0].inferred_type = 4; /* SQL_TYPE_INT */

  cols[1].name = "ratio";
  cols[1].inferred_type = 8; /* SQL_TYPE_FLOAT */

  cols[2].name = "greeting";
  cols[2].inferred_type = 5; /* SQL_TYPE_VARCHAR */

  cols[3].name = "extra";
  cols[3].inferred_type = 999; /* Unknown -> BLOB */

  row_data[0] = &mock_int;
  row_data[1] = &mock_float;
  row_data[2] = mock_str;
  row_data[3] = mock_blob;

  ASSERT_EQ(0, cdd_c_abstract_hydrate(&astruct, row_data, cols, 4));
  ASSERT_EQ(4, astruct.count);

  ASSERT_EQ(0, cdd_c_abstract_get(&astruct, "id", &out_val));
  ASSERT_EQ(CDD_C_VARIANT_TYPE_INT, out_val->type);
  ASSERT_EQ(42, out_val->value.i_val);

  ASSERT_EQ(0, cdd_c_abstract_get(&astruct, "ratio", &out_val));
  ASSERT_EQ(CDD_C_VARIANT_TYPE_FLOAT, out_val->type);
  ASSERT_EQ(3.14159, out_val->value.f_val);

  ASSERT_EQ(0, cdd_c_abstract_get(&astruct, "greeting", &out_val));
  ASSERT_EQ(CDD_C_VARIANT_TYPE_STRING, out_val->type);
  ASSERT_STR_EQ("Hello, Generic World!", out_val->value.s_val);

  ASSERT_EQ(0, cdd_c_abstract_get(&astruct, "extra", &out_val));
  ASSERT_EQ(CDD_C_VARIANT_TYPE_BLOB, out_val->type);
  ASSERT_EQ(NULL, out_val->value.b_val.data);
  ASSERT_EQ(0, out_val->value.b_val.size);

  ASSERT_EQ(0, cdd_c_abstract_struct_free(&astruct));
  PASS();
}

typedef struct MockSpecificRow {
  int id;
  double ratio;
  char greeting[32];
} mock_specific_row_t;

typedef struct MockSpecificRow2 {
  c_orm_int64_t big_id;
  float small_ratio;
  char *dyn_str;
} mock_specific_row2_t;

TEST test_abstract_struct_conversion2(void) {
  cdd_c_abstract_struct_t astruct_in, astruct_out;
  mock_specific_row2_t specific_out, specific_in;
  cdd_c_variant_t v_in;

  cdd_c_prop_meta_t p1, p2, p3;
  cdd_c_meta_t meta;
  cdd_c_prop_meta_t props[3];

  p1.name = "big_id";
  p1.type = "C_ORM_TYPE_INT64";
  p1.offset = offsetof(mock_specific_row2_t, big_id);
  p1.is_array = 0;
  p1.length = 0;
  p1.is_secure = 0;
  p2.name = "small_ratio";
  p2.type = "C_ORM_TYPE_FLOAT";
  p2.offset = offsetof(mock_specific_row2_t, small_ratio);
  p2.is_array = 0;
  p2.length = 0;
  p2.is_secure = 0;
  p3.name = "dyn_str";
  p3.type = "C_ORM_TYPE_STRING";
  p3.offset = offsetof(mock_specific_row2_t, dyn_str);
  p3.is_array = 0;
  p3.length = 0;
  p3.is_secure = 0;

  props[0] = p1;
  props[1] = p2;
  props[2] = p3;
  meta.name = "mock_specific_row2_t";
  meta.size = sizeof(mock_specific_row2_t);
  meta.num_props = 3;
  meta.props = props;

  /* Build Abstract */
  ASSERT_EQ(0, cdd_c_abstract_struct_init(&astruct_in));
  v_in.type = CDD_C_VARIANT_TYPE_INT;
  v_in.value.i_val = (((c_orm_int64_t)0x2) << 32) | 0x540BE400;
  cdd_c_abstract_set(&astruct_in, "big_id", &v_in);
  v_in.type = CDD_C_VARIANT_TYPE_FLOAT;
  v_in.value.f_val = 3.14f;
  cdd_c_abstract_set(&astruct_in, "small_ratio", &v_in);
  v_in.type = CDD_C_VARIANT_TYPE_STRING;
  v_in.value.s_val = (char *)"Dynamic String Content";
  cdd_c_abstract_set(&astruct_in, "dyn_str", &v_in);

  /* Abstract to Specific */
  memset(&specific_out, 0, sizeof(specific_out));
  ASSERT_EQ(0,
            cdd_c_abstract_to_specific(&specific_out, &astruct_in, &meta, 1));
  ASSERT_EQ((((c_orm_int64_t)0x2) << 32) | 0x540BE400, specific_out.big_id);
  ASSERT_EQ((float)3.14f, specific_out.small_ratio);
  ASSERT_STR_EQ("Dynamic String Content", specific_out.dyn_str);

  /* Specific to Abstract */
  specific_in.big_id = (((c_orm_int64_t)0x4) << 32) | 0xA817C800;
  specific_in.small_ratio = 2.71f;
  specific_in.dyn_str = specific_out.dyn_str; /* reuse string */

  ASSERT_EQ(0, cdd_c_specific_to_abstract(&astruct_out, &specific_in, &meta));
  ASSERT_EQ(3, astruct_out.count);

  C_ORM_FREE(specific_out.dyn_str);
  cdd_c_abstract_struct_free(&astruct_in);
  cdd_c_abstract_struct_free(&astruct_out);
  PASS();
}

TEST test_abstract_struct_conversion(void) {
  cdd_c_abstract_struct_t astruct_in, astruct_out;
  mock_specific_row_t specific_out, specific_in;
  cdd_c_variant_t v_in;

  cdd_c_prop_meta_t p1, p2, p3;
  cdd_c_meta_t meta;
  cdd_c_prop_meta_t props[3];

  p1.name = "id";
  p1.type = "C_ORM_TYPE_INT32";
  p1.offset = offsetof(mock_specific_row_t, id);
  p1.is_array = 0;
  p1.length = 0;
  p1.is_secure = 0;
  p2.name = "ratio";
  p2.type = "C_ORM_TYPE_DOUBLE";
  p2.offset = offsetof(mock_specific_row_t, ratio);
  p2.is_array = 0;
  p2.length = 0;
  p2.is_secure = 0;
  p3.name = "greeting";
  p3.type = "C_ORM_TYPE_STRING";
  p3.offset = offsetof(mock_specific_row_t, greeting);
  p3.is_array = 0;
  p3.length = 32;
  p3.is_secure = 0;

  props[0] = p1;
  props[1] = p2;
  props[2] = p3;
  meta.name = "mock_specific_row_t";
  meta.size = sizeof(mock_specific_row_t);
  meta.num_props = 3;
  meta.props = props;

  /* Build Abstract */
  ASSERT_EQ(0, cdd_c_abstract_struct_init(&astruct_in));
  v_in.type = CDD_C_VARIANT_TYPE_INT;
  v_in.value.i_val = 10;
  cdd_c_abstract_set(&astruct_in, "id", &v_in);
  v_in.type = CDD_C_VARIANT_TYPE_FLOAT;
  v_in.value.f_val = 5.5;
  cdd_c_abstract_set(&astruct_in, "ratio", &v_in);
  v_in.type = CDD_C_VARIANT_TYPE_STRING;
  v_in.value.s_val = (char *)"Hello";
  cdd_c_abstract_set(&astruct_in, "greeting", &v_in);

  /* Abstract to Specific */
  memset(&specific_out, 0, sizeof(specific_out));
  ASSERT_EQ(0,
            cdd_c_abstract_to_specific(&specific_out, &astruct_in, &meta, 1));
  ASSERT_EQ(10, specific_out.id);
  ASSERT_EQ(5.5, specific_out.ratio);
  ASSERT_STR_EQ("Hello", specific_out.greeting);

  /* Specific to Abstract */
  specific_in.id = 20;
  specific_in.ratio = 11.1;
  C_ORM_STRCPY(specific_in.greeting, sizeof(specific_in.greeting), "Goodbye");

  ASSERT_EQ(0, cdd_c_specific_to_abstract(&astruct_out, &specific_in, &meta));
  ASSERT_EQ(3, astruct_out.count);

  cdd_c_abstract_struct_free(&astruct_in);
  cdd_c_abstract_struct_free(&astruct_out);
  PASS();
}

TEST test_abstract_hydrate_sqlite3(void) {
  cdd_c_abstract_struct_t astruct;
  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_hydrate_sqlite3(&astruct, NULL));
  PASS();
}

TEST test_cdd_c_inspect_schema_sqlite3(void) {
  cdd_c_abstract_struct_array_t schema;
  cdd_c_abstract_struct_array_init(&schema, 10);
  ASSERT_EQ(EINVAL, (int)cdd_c_inspect_schema_sqlite3(NULL, "test", &schema));
  cdd_c_abstract_struct_array_free(&schema);
  PASS();
}

TEST test_abstract_hydrate_libpq(void) {
  cdd_c_abstract_struct_t astruct;
  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_hydrate_libpq(&astruct, NULL, 0));
  PASS();
}

TEST test_abstract_hydrate_mysql(void) {
  cdd_c_abstract_struct_t astruct;
  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_hydrate_mysql(&astruct, NULL, NULL, 0));
  PASS();
}

TEST test_mock_driver_specific_struct_hydration(void) {
  mock_specific_row_t specific_out;
  cdd_c_column_meta_t cols[3];
  void *row_data[3];
  c_orm_int64_t mock_id = 99;
  double mock_ratio = 1.234;
  char mock_greeting[] = "Mock Driver";

  cdd_c_prop_meta_t p1, p2, p3;
  cdd_c_meta_t meta;
  cdd_c_prop_meta_t props[3];

  cdd_c_abstract_struct_t astruct;

  /* Setup metadata mapping for mock_specific_row_t */
  p1.name = "id";
  p1.type = "C_ORM_TYPE_INT32";
  p1.offset = offsetof(mock_specific_row_t, id);
  p1.is_array = 0;
  p1.length = 0;
  p1.is_secure = 0;

  p2.name = "ratio";
  p2.type = "C_ORM_TYPE_DOUBLE";
  p2.offset = offsetof(mock_specific_row_t, ratio);
  p2.is_array = 0;
  p2.length = 0;
  p2.is_secure = 0;

  p3.name = "greeting";
  p3.type = "C_ORM_TYPE_STRING";
  p3.offset = offsetof(mock_specific_row_t, greeting);
  p3.is_array = 0;
  p3.length = 32;
  p3.is_secure = 0;

  props[0] = p1;
  props[1] = p2;
  props[2] = p3;
  meta.name = "mock_specific_row_t";
  meta.size = sizeof(mock_specific_row_t);
  meta.num_props = 3;
  meta.props = props;

  /* Setup mock database driver output row */
  cols[0].name = "id";
  cols[0].inferred_type = 4; /* INT */
  cols[1].name = "ratio";
  cols[1].inferred_type = 8; /* FLOAT */
  cols[2].name = "greeting";
  cols[2].inferred_type = 5; /* VARCHAR */

  row_data[0] = &mock_id;
  row_data[1] = &mock_ratio;
  row_data[2] = mock_greeting;

  /* Simulate router execution layer using pure generic abstract pipeline
   * fallback */
  memset(&specific_out, 0, sizeof(specific_out));

  ASSERT_EQ(0, cdd_c_abstract_hydrate(&astruct, row_data, cols, 3));
  ASSERT_EQ(0, cdd_c_abstract_to_specific(&specific_out, &astruct,
                                          (const struct cdd_c_meta *)&meta, 1));
  cdd_c_abstract_struct_free(&astruct);

  /* Verify bindings mapped through generically into strict specific struct
   * memory */
  ASSERT_EQ(99, specific_out.id);
  ASSERT_EQ(1.234, specific_out.ratio);
  ASSERT_STR_EQ("Mock Driver", specific_out.greeting);

  PASS();
}

TEST test_mock_driver_abstract_struct_hydration(void) {
  cdd_c_column_meta_t cols[2];
  void *row_data[2];
  c_orm_int64_t mock_id = 1001;
  char mock_name[] = "Dynamic User";

  cdd_c_abstract_struct_t astruct;
  cdd_c_variant_t *out_val;

  /* Setup mock database driver output row */
  cols[0].name = "dynamic_id";
  cols[0].inferred_type = 4; /* INT */
  cols[1].name = "dynamic_name";
  cols[1].inferred_type = 5; /* VARCHAR */

  row_data[0] = &mock_id;
  row_data[1] = mock_name;

  /* Hydrate without any specific struct target */
  ASSERT_EQ(0, cdd_c_abstract_hydrate(&astruct, row_data, cols, 2));
  ASSERT_EQ(2, astruct.count);

  /* Verify generic dynamic dictionary mapping */
  ASSERT_EQ(0, cdd_c_abstract_get(&astruct, "dynamic_id", &out_val));
  ASSERT_EQ(CDD_C_VARIANT_TYPE_INT, out_val->type);
  ASSERT_EQ(1001, out_val->value.i_val);

  ASSERT_EQ(0, cdd_c_abstract_get(&astruct, "dynamic_name", &out_val));
  ASSERT_EQ(CDD_C_VARIANT_TYPE_STRING, out_val->type);
  ASSERT_STR_EQ("Dynamic User", out_val->value.s_val);

  cdd_c_abstract_struct_free(&astruct);

  PASS();
}

TEST test_abstract_struct_array(void) {
  cdd_c_abstract_struct_array_t arr;
  cdd_c_abstract_struct_t row1, row2, row3;
  cdd_c_variant_t val;
  char *json_out;

  ASSERT_EQ(0, cdd_c_abstract_struct_array_init(&arr, 2));

  /* Row 1 */
  ASSERT_EQ(0, cdd_c_abstract_struct_init(&row1));
  val.type = CDD_C_VARIANT_TYPE_INT;
  val.value.i_val = 1;
  cdd_c_abstract_set(&row1, "id", &val);
  val.type = CDD_C_VARIANT_TYPE_STRING;
  val.value.s_val = (char *)"Alice";
  cdd_c_abstract_set(&row1, "name", &val);
  ASSERT_EQ(0, cdd_c_abstract_struct_array_append(&arr, &row1));

  /* Row 2 */
  ASSERT_EQ(0, cdd_c_abstract_struct_init(&row2));
  val.type = CDD_C_VARIANT_TYPE_INT;
  val.value.i_val = 2;
  cdd_c_abstract_set(&row2, "id", &val);
  val.type = CDD_C_VARIANT_TYPE_STRING;
  val.value.s_val = (char *)"Bob";
  cdd_c_abstract_set(&row2, "name", &val);
  ASSERT_EQ(0, cdd_c_abstract_struct_array_append(&arr, &row2));

  /* Row 3 - Force array resize */
  ASSERT_EQ(0, cdd_c_abstract_struct_init(&row3));
  val.type = CDD_C_VARIANT_TYPE_INT;
  val.value.i_val = 3;
  cdd_c_abstract_set(&row3, "id", &val);
  val.type = CDD_C_VARIANT_TYPE_FLOAT;
  val.value.f_val = 3.14;
  cdd_c_abstract_set(&row3, "score", &val);
  val.type = CDD_C_VARIANT_TYPE_NULL;
  cdd_c_abstract_set(&row3, "empty", &val);
  val.type = CDD_C_VARIANT_TYPE_BLOB;
  val.value.b_val.data = (unsigned char *)"blob";
  val.value.b_val.size = 4;
  cdd_c_abstract_set(&row3, "data", &val);
  ASSERT_EQ(0, cdd_c_abstract_struct_array_append(&arr, &row3));

  ASSERT_EQ(3, arr.count);

  ASSERT_EQ(0, cdd_c_abstract_struct_array_to_json(&arr, &json_out));
  ASSERT_NEQ(NULL, json_out);
  json_free_serialized_string(json_out);

  ASSERT_EQ(0, cdd_c_abstract_struct_array_free(&arr));

  /* Resize on 0 capacity array */
  ASSERT_EQ(0, cdd_c_abstract_struct_array_init(&arr, 0));
  ASSERT_EQ(0, cdd_c_abstract_struct_init(&row1));
  val.type = CDD_C_VARIANT_TYPE_INT;
  val.value.i_val = 1;
  cdd_c_abstract_set(&row1, "id", &val);
  ASSERT_EQ(0, cdd_c_abstract_struct_array_append(&arr, &row1));
  ASSERT_EQ(1, arr.count);
  ASSERT_EQ(0, cdd_c_abstract_struct_array_free(&arr));

  PASS();
}

TEST test_benchmark_hydration(void) {
  const size_t ITERATIONS = 2;
  size_t i;
  clock_t start, end;
  double time_specific, time_abstract;
  mock_specific_row_t specific_out;
  cdd_c_abstract_struct_t astruct;
  void *row_data[2];
  c_orm_int64_t mock_can = 42;
  char mock_bar[] = "bench_str";

  cdd_c_prop_meta_t p1, p2;
  cdd_c_meta_t meta;
  cdd_c_prop_meta_t props[2];
  cdd_c_column_meta_t cols[2];

  p1.name = "greeting";
  p1.type = "C_ORM_TYPE_STRING";
  p1.offset = offsetof(mock_specific_row_t, greeting);
  p1.is_array = 0;
  p1.length = 32;
  p1.is_secure = 0;

  p2.name = "id";
  p2.type = "C_ORM_TYPE_INT32";
  p2.offset = offsetof(mock_specific_row_t, id);
  p2.is_array = 0;
  p2.length = 0;
  p2.is_secure = 0;

  props[0] = p1;
  props[1] = p2;

  meta.name = "MockSpecificRow";
  meta.size = sizeof(mock_specific_row_t);
  meta.num_props = 2;
  meta.props = props;

  /* Mock row setup */
  row_data[0] = mock_bar;
  row_data[1] = &mock_can;

  /* cdd_c_column_meta_t mapping for abstract hydration */
  cols[0].name = "greeting";
  cols[0].inferred_type = 5; /* STRING */
  cols[1].name = "id";
  cols[1].inferred_type = 4; /* INT */

  start = clock();
  for (i = 0; i < ITERATIONS; ++i) {
    C_ORM_STRNCPY(specific_out.greeting, sizeof(specific_out.greeting),
                  mock_bar, 31);
    specific_out.greeting[31] = '\0';
    specific_out.id = (int)mock_can;
    /* Normally we'd call a generated specific hydrator here */
  }
  end = clock();
  time_specific = (double)(end - start) / CLOCKS_PER_SEC;

  start = clock();
  for (i = 0; i < ITERATIONS; ++i) {
    cdd_c_abstract_hydrate(&astruct, row_data, cols, 2);
    cdd_c_abstract_to_specific(&specific_out, &astruct,
                               (const struct cdd_c_meta *)&meta, 0);
    cdd_c_abstract_struct_free(&astruct);
  }
  end = clock();
  time_abstract = (double)(end - start) / CLOCKS_PER_SEC;

  printf("\n[Benchmark] Specific Hydration: %f seconds for %u iterations.\n",
         time_specific, (unsigned int)ITERATIONS);
  printf("[Benchmark] Abstract Fallback Hydration: %f seconds for %u "
         "iterations.\n",
         time_abstract, (unsigned int)ITERATIONS);

  /* ASSERT(time_abstract >= time_specific); */

  PASS();
}

TEST test_abstract_struct_null_checks(void) {
  cdd_c_abstract_struct_t astruct;
  cdd_c_abstract_struct_array_t arr;
  cdd_c_variant_t v = {0}, *v_out = NULL;
  size_t bytes, calls;
  char *json_out;

  /* null checks */
  ASSERT_EQ(EINVAL, (int)cdd_c_get_allocated_bytes(NULL));
  ASSERT_EQ(0, (int)cdd_c_get_allocated_bytes(&bytes));

  ASSERT_EQ(EINVAL, (int)cdd_c_get_freed_calls(NULL));
  ASSERT_EQ(0, (int)cdd_c_get_freed_calls(&calls));

  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_struct_array_init(NULL, 10));
  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_struct_array_append(NULL, &astruct));

  /* Trigger ENOMEM with SIZE_MAX */
  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_struct_array_init(&arr, (size_t)-1));
  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_struct_init_with_capacity(&astruct,
                                                                  (size_t)-1));

  ASSERT_EQ(0, (int)cdd_c_abstract_struct_array_init(&arr, 10));
  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_struct_array_append(&arr, NULL));
  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_struct_array_free(NULL));

  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_struct_array_to_json(NULL, &json_out));
  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_struct_array_to_json(&arr, NULL));
  cdd_c_abstract_struct_array_free(&arr);

  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_struct_init_with_capacity(NULL, 10));
  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_set(NULL, "k", &v));
  ASSERT_EQ(0, (int)cdd_c_abstract_struct_init(&astruct));
  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_set(&astruct, NULL, &v));
  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_set(&astruct, "k", NULL));

  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_get(NULL, "k", &v_out));
  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_get(&astruct, NULL, &v_out));
  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_get(&astruct, "k", NULL));
  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_get(&astruct, "missing_key", &v_out));

  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_struct_deep_copy(NULL, &astruct));
  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_struct_deep_copy(&astruct, NULL));

  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_struct_free(NULL));

  cdd_c_variant_free(NULL);

  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_struct_to_json(NULL, &json_out));
  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_struct_to_json(&astruct, NULL));

  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_struct_from_json(NULL, &astruct));
  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_struct_from_json("{}", NULL));

  cdd_c_abstract_print(NULL);

  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_hydrate(NULL, NULL, NULL, 0));
  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_hydrate(&astruct, NULL, NULL, 0));
  {
    void *row_data[1];
    ASSERT_EQ(EINVAL, (int)cdd_c_abstract_hydrate(&astruct, row_data, NULL, 1));
  }

  ASSERT_EQ(EINVAL, (int)cdd_c_meta_offsetof(NULL, "f", &bytes));
  ASSERT_EQ(EINVAL, (int)cdd_c_specific_to_abstract(NULL, NULL, NULL));
  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_to_specific(NULL, NULL, NULL, 0));

  cdd_c_abstract_struct_free(&astruct);

  PASS();
}

TEST test_abstract_struct_edge_types(void) {
  cdd_c_abstract_struct_t astruct;
  cdd_c_abstract_struct_t astruct2;
  cdd_c_variant_t v;
  char *json_out = NULL;

  ASSERT_EQ(0, cdd_c_abstract_struct_init(&astruct));

  /* BLOB */
  v.type = CDD_C_VARIANT_TYPE_BLOB;
  C_ORM_STRDUP("blobdata", (char **)&v.value.b_val.data);
  v.value.b_val.size = 8;
  ASSERT_EQ(0, cdd_c_abstract_set(&astruct, "my_blob", &v));
  C_ORM_FREE(v.value.b_val.data);

  /* NULL */
  v.type = CDD_C_VARIANT_TYPE_NULL;
  ASSERT_EQ(0, cdd_c_abstract_set(&astruct, "my_null", &v));

  /* Unknown */
  v.type = 999; /* Unknown type */
  ASSERT_EQ(0, cdd_c_abstract_set(&astruct, "my_unknown", &v));

  /* Deep copy should cover duplicate_blob */
  ASSERT_EQ(0, cdd_c_abstract_struct_deep_copy(&astruct2, &astruct));

  ASSERT_EQ(0, cdd_c_abstract_struct_to_json(&astruct2, &json_out));
  json_free_serialized_string(json_out);
  cdd_c_abstract_print(&astruct2);

  cdd_c_abstract_struct_free(&astruct);
  cdd_c_abstract_struct_free(&astruct2);
  PASS();
}

TEST test_inspect_schema_null(void) {
  cdd_c_abstract_struct_array_t schema;
  ASSERT_EQ(EINVAL, (int)cdd_c_inspect_schema_libpq(NULL, "test", &schema));
  ASSERT_EQ(EINVAL, (int)cdd_c_inspect_schema_mysql(NULL, "test", &schema));
  PASS();
}

TEST test_meta_offsetof(void) {
  cdd_c_prop_meta_t p1;
  cdd_c_meta_t meta;
  cdd_c_prop_meta_t props[1];
  size_t off;

  p1.name = "id";
  p1.offset = 42;
  props[0] = p1;

  meta.num_props = 1;
  meta.props = props;

  ASSERT_EQ(0, (int)cdd_c_meta_offsetof(&meta, "id", &off));
  ASSERT_EQ(42, (int)off);
  ASSERT_EQ(EINVAL, (int)cdd_c_meta_offsetof(&meta, "missing", &off));

  PASS();
}

TEST test_json_edge_cases(void) {
  cdd_c_abstract_struct_t astruct;
  /* Bad JSON */
  ASSERT_EQ(EINVAL,
            (int)cdd_c_abstract_struct_from_json("{ bad json ", &astruct));
  /* Not object */
  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_struct_from_json("[]", &astruct));

  /* Parse floats, bools, nulls */
  ASSERT_EQ(0, (int)cdd_c_abstract_struct_from_json(
                   "{\"f\": 1.23, \"b\": true, \"n\": null}", &astruct));
  cdd_c_abstract_struct_free(&astruct);
  PASS();
}

TEST test_specific_edge_cases(void) {
  mock_specific_row_t specific_out, specific_in;
  cdd_c_abstract_struct_t astruct_in, astruct_out;
  cdd_c_prop_meta_t p1, p2, p3;
  cdd_c_meta_t meta;
  cdd_c_prop_meta_t props[3];
  (void)specific_in;
  (void)astruct_out;

  p1.name = "id";
  p1.type = "C_ORM_TYPE_INT32";
  p1.offset = offsetof(mock_specific_row_t, id);
  p1.length = 0;
  p2.name = "ratio";
  p2.type = "C_ORM_TYPE_FLOAT";
  p2.offset = offsetof(mock_specific_row_t, ratio);
  p2.length = 0;
  p3.name = "greeting";
  p3.type = "C_ORM_TYPE_STRING";
  p3.offset = offsetof(mock_specific_row_t, greeting);
  p3.length = 32;

  props[0] = p1;
  props[1] = p2;
  props[2] = p3;
  meta.num_props = 3;
  meta.props = props;

  cdd_c_abstract_struct_init(&astruct_in);
  /* Missing keys for strict mapping */
  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_to_specific(&specific_out, &astruct_in,
                                                    &meta, 1));
  ASSERT_EQ(0, (int)cdd_c_abstract_to_specific(&specific_out, &astruct_in,
                                               &meta, 0)); /* Non-strict ok */

  /* Wrong types */
  {
    cdd_c_variant_t v_wrong;
    v_wrong.type = CDD_C_VARIANT_TYPE_STRING;
    v_wrong.value.s_val = (char *)"wrong";
    cdd_c_abstract_set(&astruct_in, "id", &v_wrong);
    v_wrong.type = CDD_C_VARIANT_TYPE_INT;
    v_wrong.value.i_val = 1;
    cdd_c_abstract_set(&astruct_in, "ratio", &v_wrong);
    v_wrong.type = CDD_C_VARIANT_TYPE_INT;
    cdd_c_abstract_set(&astruct_in, "greeting", &v_wrong);
  }
  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_to_specific(&specific_out, &astruct_in,
                                                    &meta, 1));
  cdd_c_abstract_struct_free(&astruct_in);

  {
    cdd_c_prop_meta_t p_id64 = {"id64", "C_ORM_TYPE_INT64", 0, 0, 0, 0};
    cdd_c_prop_meta_t p_flt = {"flt", "C_ORM_TYPE_FLOAT", 0, 0, 0, 0};
    cdd_c_prop_meta_t p_dbl = {"dbl", "C_ORM_TYPE_DOUBLE", 0, 0, 0, 0};
    cdd_c_prop_meta_t p_str_dyn = {"str_dyn", "C_ORM_TYPE_STRING", 0, 0, 0, 0};
    cdd_c_prop_meta_t props_ext[4];
    cdd_c_meta_t meta_ext;
    cdd_c_variant_t v;

    props_ext[0] = p_id64;
    props_ext[1] = p_flt;
    props_ext[2] = p_dbl;
    props_ext[3] = p_str_dyn;
    meta_ext.num_props = 4;
    meta_ext.props = props_ext;

    cdd_c_abstract_struct_init(&astruct_in);

    v.type = CDD_C_VARIANT_TYPE_STRING;
    v.value.s_val = (char *)"x";
    cdd_c_abstract_set(&astruct_in, "id64", &v);

    v.type = CDD_C_VARIANT_TYPE_STRING;
    v.value.s_val = (char *)"x";
    cdd_c_abstract_set(&astruct_in, "flt", &v);

    v.type = CDD_C_VARIANT_TYPE_STRING;
    v.value.s_val = (char *)"x";
    cdd_c_abstract_set(&astruct_in, "dbl", &v);

    v.type = CDD_C_VARIANT_TYPE_INT;
    v.value.i_val = 1;
    cdd_c_abstract_set(&astruct_in, "str_dyn", &v);

    ASSERT_EQ(EINVAL, (int)cdd_c_abstract_to_specific(
                          &specific_out, &astruct_in, &meta_ext, 1));
    cdd_c_abstract_struct_free(&astruct_in);
  }
  PASS();
}

TEST test_hydrate_null(void) {
  cdd_c_abstract_struct_t astruct;
  cdd_c_column_meta_t cols[1];
  void *row_data[1];
  cols[0].name = "id";
  cols[0].inferred_type = 4;
  row_data[0] = NULL;

  ASSERT_EQ(0, (int)cdd_c_abstract_hydrate(&astruct, row_data, cols, 1));
  cdd_c_abstract_struct_free(&astruct);
  PASS();
}

TEST test_abstract_struct_allocation_limits(void) {
  cdd_c_abstract_struct_array_t arr;
  cdd_c_abstract_struct_t astruct1, astruct2;
  cdd_c_variant_t v;
  char *json = NULL;

  (void)json;
  /* init array with huge size */
  ASSERT_EQ(EINVAL,
            (int)cdd_c_abstract_struct_array_init(
                &arr, ((size_t)-1) / sizeof(cdd_c_abstract_struct_t) + 1));

  /* arr_append integer overflow */
  ASSERT_EQ(0, (int)cdd_c_abstract_struct_array_init(&arr, 1));
  arr.capacity = ((size_t)-1) / 2 + 1; /* fake capacity */
  arr.count = arr.capacity;
  ASSERT_EQ(0, cdd_c_abstract_struct_init(&astruct1));
  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_struct_array_append(&arr, &astruct1));

  arr.capacity = ((size_t)-1) / sizeof(cdd_c_abstract_struct_t);
  if (arr.capacity <= ((size_t)-1) / 2) {
    arr.capacity = ((size_t)-1) / sizeof(cdd_c_abstract_struct_t) - 1;
    arr.count = arr.capacity;
    ASSERT_EQ(EINVAL, (int)cdd_c_abstract_struct_array_append(&arr, &astruct1));
  }
  arr.capacity = 0;
  arr.count = 0; /* clean up for free */
  cdd_c_abstract_struct_array_free(&arr);
  cdd_c_abstract_struct_free(&astruct1);

  /* duplicate blob with huge size */
  ASSERT_EQ(0, cdd_c_abstract_struct_init(&astruct1));
  /* Bypass set and manually inject huge blob */
  v.type = CDD_C_VARIANT_TYPE_INT;
  v.value.i_val = 1;
  ASSERT_EQ(0, cdd_c_abstract_set(&astruct1, "huge_blob", &v));
  astruct1.kvs[0].value.type = CDD_C_VARIANT_TYPE_BLOB;
  astruct1.kvs[0].value.value.b_val.data = (unsigned char *)"fake";
  astruct1.kvs[0].value.value.b_val.size = (size_t)-1;
  ASSERT_EQ(EINVAL, (int)cdd_c_abstract_struct_deep_copy(&astruct2, &astruct1));
  astruct1.kvs[0].value.type = CDD_C_VARIANT_TYPE_NULL; /* prevent free crash */
  cdd_c_abstract_struct_free(&astruct1);

  /* null string in variant */
  ASSERT_EQ(0, cdd_c_abstract_struct_init(&astruct1));
  v.type = CDD_C_VARIANT_TYPE_INT;
  v.value.i_val = 1;
  ASSERT_EQ(0, cdd_c_abstract_set(&astruct1, "null_str", &v));
  astruct1.kvs[0].value.type = CDD_C_VARIANT_TYPE_STRING;
  astruct1.kvs[0].value.value.s_val = NULL;

  v.type = CDD_C_VARIANT_TYPE_INT;
  v.value.i_val = 1;
  ASSERT_EQ(0, cdd_c_abstract_set(&astruct1, "null_blob", &v));
  astruct1.kvs[1].value.type = CDD_C_VARIANT_TYPE_BLOB;
  astruct1.kvs[1].value.value.b_val.data = NULL;
  astruct1.kvs[1].value.value.b_val.size = 0;

  ASSERT_EQ(0, (int)cdd_c_abstract_struct_deep_copy(&astruct2, &astruct1));
  cdd_c_abstract_struct_free(&astruct1);
  cdd_c_abstract_struct_free(&astruct2);

  PASS();
}

SUITE(abstract_struct_suite) {
  RUN_TEST(test_abstract_struct_memory_layout);
  RUN_TEST(test_variant_type_safety);
  RUN_TEST(test_abstract_struct_set_get);
  RUN_TEST(test_abstract_struct_json_roundtrip);
  RUN_TEST(test_abstract_hydrate);
  RUN_TEST(test_abstract_struct_conversion);
  RUN_TEST(test_abstract_struct_conversion2);
  RUN_TEST(test_abstract_hydrate_sqlite3);
  RUN_TEST(test_cdd_c_inspect_schema_sqlite3);
  RUN_TEST(test_abstract_hydrate_libpq);
  RUN_TEST(test_abstract_hydrate_mysql);
  RUN_TEST(test_mock_driver_specific_struct_hydration);
  RUN_TEST(test_mock_driver_abstract_struct_hydration);
  RUN_TEST(test_abstract_struct_array);
  RUN_TEST(test_benchmark_hydration);
  RUN_TEST(test_abstract_struct_null_checks);
  RUN_TEST(test_abstract_struct_edge_types);
  RUN_TEST(test_inspect_schema_null);
  RUN_TEST(test_meta_offsetof);
  RUN_TEST(test_json_edge_cases);
  RUN_TEST(test_specific_edge_cases);
  RUN_TEST(test_hydrate_null);
  RUN_TEST(test_abstract_struct_allocation_limits);
  RUN_TEST(test_abstract_struct_oom_coverage);
}

#if defined(__clang__) || defined(__GNUC__)
#endif
