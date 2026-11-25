/**
 * @file test_abstract_struct.c
 * @brief Tests for abstract_struct generic representations.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "abstract_struct.h"
#include "c_orm_odbc.h"
#include "c_orm_odbc_abi.h"
#include "c_orm_sql.h"
#include "c_orm_sqlite.h"
#include "cdd_c_orm_meta.h"
#include "c_cdd/json_dom.h"
#include <errno.h>
#define GREATEST_USE_LONGJMP 0
#include <greatest.h>
#include "test_assert_zero_branch.h"
#include <time.h>

#include "test_abstract_struct_oom.h"
/* clang-format on */

/**
 * @brief Executes test_abstract_struct_memory_layout.
 */
TEST test_abstract_struct_memory_layout(void) {
  cdd_c_abstract_struct_t astruct;
  ASSERT_EQ(0, cdd_c_abstract_struct_init(&astruct));
  ASSERT_EQ(0, astruct.count);
  ASSERT_EQ(0, astruct.capacity);
  ASSERT_EQ(NULL, astruct.kvs);
  PASS();
}

/**
 * @brief Tests variant type safety.
 * @return GREATEST test result.
 */
TEST test_variant_type_safety(void) {
  cdd_c_variant_t v1, v2;
  v1.type = CDD_C_VARIANT_TYPE_INT;
  v1.value.i_val = 42;

  v2.type = CDD_C_VARIANT_TYPE_STRING;
  ASSERT_EQ(C_ORM_OK, c_orm_default_malloc(5, (void **)&v2.value.s_val));
  C_ORM_STRCPY(v2.value.s_val, 5, "test");

  ASSERT_EQ(CDD_C_VARIANT_TYPE_INT, v1.type);
  ASSERT_EQ(42, v1.value.i_val);

  ASSERT_EQ(CDD_C_VARIANT_TYPE_STRING, v2.type);
  ASSERT_STR_EQ("test", v2.value.s_val);

  cdd_c_variant_free(&v2);
  ASSERT_EQ(CDD_C_VARIANT_TYPE_NULL, v2.type);
  PASS();
}

/**
 * @brief Tests abstract struct set get.
 * @return GREATEST test result.
 */
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
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            cdd_c_abstract_get(&astruct, "missing", &v_out));

  /* Force resize */
  for (i = 0; i < 10; ++i) {
    C_ORM_SPRINTF(key_buf, sizeof(key_buf), "key_%d", i);
    v_in.value.i_val = i;
    ASSERT_EQ(0, cdd_c_abstract_set(&astruct, key_buf, &v_in));
  }
  ASSERT_EQ(11, astruct.count);

  ASSERT_EQ(0, cdd_c_abstract_struct_free(&astruct));

  /* Test hash collision branch */
  {
    cdd_c_variant_t v1, v2, *v_found = NULL;
    cdd_c_abstract_struct_t a_coll;
    unsigned long h = 5381;
    const char *s = "key2";
    int c;

    ASSERT_EQ(0, cdd_c_abstract_struct_init(&a_coll));
    v1.type = CDD_C_VARIANT_TYPE_INT;
    v1.value.i_val = 111;
    v2.type = CDD_C_VARIANT_TYPE_INT;
    v2.value.i_val = 222;
    ASSERT_EQ(0, cdd_c_abstract_set(&a_coll, "key1", &v1));

    while ((c = *s++)) {
      h = ((h << 5) + h) + (unsigned long)c;
    }
    a_coll.kvs[0].key_hash = h;

    ASSERT_EQ(0, cdd_c_abstract_set(&a_coll, "key2", &v2));
    ASSERT_EQ(0, cdd_c_abstract_get(&a_coll, "key2", &v_found));
    ASSERT_EQ(222, v_found->value.i_val);
    cdd_c_abstract_struct_free(&a_coll);
  }

  PASS();
}

/**
 * @brief Tests abstract struct json roundtrip.
 * @return GREATEST test result.
 */
TEST test_abstract_struct_json_roundtrip(void) {
  static char c_mut_s0[] = "test_string";

  cdd_c_abstract_struct_t astruct_out, astruct_in;
  cdd_c_variant_t v_in, *v_out;
  char *json_str = NULL;

  ASSERT_EQ(0, cdd_c_abstract_struct_init(&astruct_out));

  v_in.type = CDD_C_VARIANT_TYPE_INT;
  v_in.value.i_val = 100;
  ASSERT_EQ(0, cdd_c_abstract_set(&astruct_out, "my_int", &v_in));

  v_in.type = CDD_C_VARIANT_TYPE_STRING;
  v_in.value.s_val = c_mut_s0;
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

  ASSERT_EQ(C_ORM_OK, c_orm_system_free(json_str));
  ASSERT_EQ(0, cdd_c_abstract_struct_free(&astruct_out));
  ASSERT_EQ(0, cdd_c_abstract_struct_free(&astruct_in));
  PASS();
}

/**
 * @brief Tests abstract hydrate.
 * @return GREATEST test result.
 */
TEST test_abstract_hydrate(void) {
  static char c_mut_s1[] = "Hello, Generic World!";
  static char c_mut_s2[] = "blob_data";

  cdd_c_abstract_struct_t astruct;
  cdd_c_column_meta_t cols[4];
  void *row_data[4];
  c_orm_int64_t mock_int = 42;
  double mock_float = 3.14159;
  char *mock_str = c_mut_s1;
  char *mock_blob = c_mut_s2;
  cdd_c_variant_t *out_val;

  cols[0].name = "id";
  cols[0].inferred_type = SQL_TYPE_INT;

  cols[1].name = "ratio";
  cols[1].inferred_type = SQL_TYPE_FLOAT;

  cols[2].name = "greeting";
  cols[2].inferred_type = SQL_TYPE_VARCHAR;

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

/**
 * @brief Mock row struct for abstract struct testing.
 */
typedef struct MockSpecificRow {
  /** @brief Identifier field. */
  int id;
  /** @brief Ratio floating-point field. */
  double ratio;
  /** @brief Greeting string field. */
  char greeting[32];
} mock_specific_row_t;

/**
 * @brief Second mock row struct with int64 and dynamic string.
 */
typedef struct MockSpecificRow2 {
  /** @brief 64-bit integer identifier. */
  c_orm_int64_t big_id;
  /** @brief 32-bit floating point ratio. */
  float small_ratio;
  /** @brief Dynamically allocated string pointer. */
  char *dyn_str;
} mock_specific_row2_t;

/**
 * @brief Tests abstract struct conversion2.
 * @return GREATEST test result.
 */
TEST test_abstract_struct_conversion2(void) {
  static char c_mut_s3[] = "Dynamic String Content";

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
  v_in.value.s_val = c_mut_s3;
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

  c_orm_system_free(specific_out.dyn_str);
  cdd_c_abstract_struct_free(&astruct_in);
  cdd_c_abstract_struct_free(&astruct_out);
  PASS();
}

/**
 * @brief Tests abstract struct conversion.
 * @return GREATEST test result.
 */
TEST test_abstract_struct_conversion(void) {
  static char c_mut_s4[] = "Hello";

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
  v_in.value.s_val = c_mut_s4;
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

/**
 * @brief Tests abstract hydrate sqlite3.
 * @return GREATEST test result.
 */
TEST test_abstract_hydrate_sqlite3(void) {
  cdd_c_abstract_struct_t astruct;
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_hydrate_sqlite3(&astruct, NULL));
  PASS();
}

/**
 * @brief Tests cdd c inspect schema sqlite3.
 * @return GREATEST test result.
 */
TEST test_cdd_c_inspect_schema_sqlite3(void) {
  cdd_c_abstract_struct_array_t schema;
  cdd_c_abstract_struct_array_init(&schema, 10);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_inspect_schema_sqlite3(NULL, "test", &schema));
  cdd_c_abstract_struct_array_free(&schema);
  PASS();
}

/**
 * @brief Tests abstract hydrate libpq.
 * @return GREATEST test result.
 */
TEST test_abstract_hydrate_libpq(void) {
  cdd_c_abstract_struct_t astruct;
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_hydrate_libpq(&astruct, NULL, 0));
  PASS();
}

/**
 * @brief Tests abstract hydrate mysql.
 * @return GREATEST test result.
 */
TEST test_abstract_hydrate_mysql(void) {
  cdd_c_abstract_struct_t astruct;
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_hydrate_mysql(&astruct, NULL, NULL, 0));
  PASS();
}

/**
 * @brief Tests mock driver specific struct hydration.
 * @return GREATEST test result.
 */
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
  cols[0].inferred_type = SQL_TYPE_INT;
  cols[1].name = "ratio";
  cols[1].inferred_type = SQL_TYPE_FLOAT;
  cols[2].name = "greeting";
  cols[2].inferred_type = SQL_TYPE_VARCHAR;

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

/**
 * @brief Tests mock driver abstract struct hydration.
 * @return GREATEST test result.
 */
TEST test_mock_driver_abstract_struct_hydration(void) {
  cdd_c_column_meta_t cols[2];
  void *row_data[2];
  c_orm_int64_t mock_id = 1001;
  char mock_name[] = "Dynamic User";

  cdd_c_abstract_struct_t astruct;
  cdd_c_variant_t *out_val;

  /* Setup mock database driver output row */
  cols[0].name = "dynamic_id";
  cols[0].inferred_type = SQL_TYPE_INT;
  cols[1].name = "dynamic_name";
  cols[1].inferred_type = SQL_TYPE_VARCHAR;

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

/**
 * @brief Tests abstract struct array.
 * @return GREATEST test result.
 */
TEST test_abstract_struct_array(void) {
  static char c_mut_s5[] = "Alice";
  static char c_mut_s6[] = "Bob";
  static unsigned char c_mut_s7[] = "blob";

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
  val.value.s_val = c_mut_s5;
  cdd_c_abstract_set(&row1, "name", &val);
  ASSERT_EQ(0, cdd_c_abstract_struct_array_append(&arr, &row1));

  /* Row 2 */
  ASSERT_EQ(0, cdd_c_abstract_struct_init(&row2));
  val.type = CDD_C_VARIANT_TYPE_INT;
  val.value.i_val = 2;
  cdd_c_abstract_set(&row2, "id", &val);
  val.type = CDD_C_VARIANT_TYPE_STRING;
  val.value.s_val = c_mut_s6;
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
  val.type = 999;
  cdd_c_abstract_set(&row3, "unk", &val);
  val.type = CDD_C_VARIANT_TYPE_BLOB;
  val.value.b_val.data = c_mut_s7;
  val.value.b_val.size = 4;
  cdd_c_abstract_set(&row3, "data", &val);
  ASSERT_EQ(0, cdd_c_abstract_struct_array_append(&arr, &row3));

  ASSERT_EQ(3, arr.count);

  ASSERT_EQ(0, cdd_c_abstract_struct_array_to_json(&arr, &json_out));
  ASSERT_NEQ(NULL, json_out);
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(json_out));

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

/**
 * @brief Tests benchmark hydration.
 * @return GREATEST test result.
 */
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
  cols[0].inferred_type = SQL_TYPE_VARCHAR;
  cols[1].name = "id";
  cols[1].inferred_type = SQL_TYPE_INT;

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

/**
 * @brief Tests abstract struct null checks.
 * @return GREATEST test result.
 */
TEST test_abstract_struct_null_checks(void) {
  cdd_c_abstract_struct_t astruct;
  cdd_c_abstract_struct_array_t arr;
  cdd_c_variant_t v = {0}, *v_out = NULL;
  cdd_c_meta_t meta;
  size_t bytes, calls;
  char *json_out;

  memset(&meta, 0, sizeof(meta));

  /* null checks */
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, (int)cdd_c_get_allocated_bytes(NULL));
  ASSERT_EQ(0, (int)cdd_c_get_allocated_bytes(&bytes));

  ASSERT_EQ(C_ORM_ERROR_VALIDATION, (int)cdd_c_get_freed_calls(NULL));
  ASSERT_EQ(0, (int)cdd_c_get_freed_calls(&calls));

  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_struct_array_init(NULL, 10));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_struct_array_append(NULL, &astruct));

  /* Trigger ENOMEM with SIZE_MAX */
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_struct_array_init(&arr, (size_t)-1));
  ASSERT_EQ(
      C_ORM_ERROR_VALIDATION,
      (int)cdd_c_abstract_struct_init_with_capacity(&astruct, (size_t)-1));

  ASSERT_EQ(0, (int)cdd_c_abstract_struct_array_init(&arr, 10));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_struct_array_append(&arr, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_struct_array_free(NULL));

  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_struct_array_to_json(NULL, &json_out));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_struct_array_to_json(&arr, NULL));
  cdd_c_abstract_struct_array_free(&arr);

  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_struct_init_with_capacity(NULL, 10));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, (int)cdd_c_abstract_set(NULL, "k", &v));
  ASSERT_EQ(0, (int)cdd_c_abstract_struct_init(&astruct));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_set(&astruct, NULL, &v));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_set(&astruct, "k", NULL));

  ASSERT_EQ(C_ORM_ERROR_VALIDATION, (int)cdd_c_abstract_get(NULL, "k", &v_out));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_get(&astruct, NULL, &v_out));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_get(&astruct, "k", NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_get(&astruct, "missing_key", &v_out));

  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_struct_deep_copy(NULL, &astruct));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_struct_deep_copy(&astruct, NULL));

  ASSERT_EQ(C_ORM_ERROR_VALIDATION, (int)cdd_c_abstract_struct_free(NULL));

  cdd_c_variant_free(NULL);

  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_struct_to_json(NULL, &json_out));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_struct_to_json(&astruct, NULL));

  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_struct_from_json(NULL, &astruct));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_struct_from_json("{}", NULL));

  cdd_c_abstract_print(NULL);

  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_hydrate(NULL, NULL, NULL, 0));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_hydrate(&astruct, NULL, NULL, 0));
  {
    void *row_data[1];
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              (int)cdd_c_abstract_hydrate(&astruct, row_data, NULL, 1));
  }

  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_meta_offsetof(NULL, "f", &bytes));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_meta_offsetof(&meta, NULL, &bytes));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, (int)cdd_c_meta_offsetof(&meta, "f", NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_specific_to_abstract(NULL, NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_specific_to_abstract(&astruct, NULL, &meta));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_specific_to_abstract(&astruct, &bytes, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_to_specific(NULL, NULL, NULL, 0));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_to_specific(&bytes, NULL, &meta, 0));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_to_specific(&bytes, &astruct, NULL, 0));

  cdd_c_abstract_struct_free(&astruct);

  PASS();
}

/**
 * @brief Tests abstract struct edge types.
 * @return GREATEST test result.
 */
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
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(v.value.b_val.data));

  /* NULL */
  v.type = CDD_C_VARIANT_TYPE_NULL;
  ASSERT_EQ(0, cdd_c_abstract_set(&astruct, "my_null", &v));

  /* Unknown */
  v.type = 999; /* Unknown type */
  ASSERT_EQ(0, cdd_c_abstract_set(&astruct, "my_unknown", &v));

  /* Deep copy should cover duplicate_blob */
  ASSERT_EQ(0, cdd_c_abstract_struct_deep_copy(&astruct2, &astruct));

  ASSERT_EQ(0, cdd_c_abstract_struct_to_json(&astruct2, &json_out));
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(json_out));
  cdd_c_abstract_print(&astruct2);

  cdd_c_abstract_struct_free(&astruct);
  cdd_c_abstract_struct_free(&astruct2);
  PASS();
}

/**
 * @brief Tests inspect schema null.
 * @return GREATEST test result.
 */
TEST test_inspect_schema_null(void) {
  cdd_c_abstract_struct_array_t schema;
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_inspect_schema_libpq(NULL, "test", &schema));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_inspect_schema_mysql(NULL, "test", &schema));
  PASS();
}

/**
 * @brief Tests meta offsetof.
 * @return GREATEST test result.
 */
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
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_meta_offsetof(&meta, "missing", &off));

  PASS();
}

/**
 * @brief Tests json edge cases.
 * @return GREATEST test result.
 */
TEST test_json_edge_cases(void) {
  cdd_c_abstract_struct_t astruct;
  /* Bad JSON */
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_struct_from_json("{ bad json ", &astruct));
  /* Not object */
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_struct_from_json("[]", &astruct));

  /* Parse floats, bools, nulls, and non-primitive JSON value types */
  ASSERT_EQ(0, (int)cdd_c_abstract_struct_from_json(
                   "{\"f\": 1.23, \"b\": true, \"n\": null, \"arr\": [1, 2]}",
                   &astruct));
  cdd_c_abstract_struct_free(&astruct);
  PASS();
}

/**
 * @brief Tests specific edge cases.
 * @return GREATEST test result.
 */
TEST test_specific_edge_cases(void) {
  static char c_mut_s8[] = "bad";
  static char c_mut_s9[] = "bad";
  static char c_mut_s10[] = "bad";
  static char c_mut_s11[] = "bad";

  mock_specific_row_t specific_out;
  cdd_c_abstract_struct_t astruct_in;
  cdd_c_prop_meta_t p1, p2, p3;
  cdd_c_meta_t meta;
  cdd_c_prop_meta_t props[3];

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
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, (int)cdd_c_abstract_to_specific(
                                        &specific_out, &astruct_in, &meta, 1));
  ASSERT_EQ(0, (int)cdd_c_abstract_to_specific(&specific_out, &astruct_in,
                                               &meta, 0)); /* Non-strict ok */
  cdd_c_abstract_struct_free(&astruct_in);

  /* Count matches but key missing from meta */
  {
    cdd_c_prop_meta_t p_single;
    cdd_c_meta_t meta_single;
    cdd_c_variant_t v;
    memset(&p_single, 0, sizeof(p_single));
    memset(&meta_single, 0, sizeof(meta_single));
    p_single.name = "id";
    p_single.type = "C_ORM_TYPE_INT32";
    meta_single.num_props = 1;
    meta_single.props = &p_single;
    v.type = CDD_C_VARIANT_TYPE_INT;
    v.value.i_val = 1;
    cdd_c_abstract_struct_init(&astruct_in);
    cdd_c_abstract_set(&astruct_in, "other", &v);
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              (int)cdd_c_abstract_to_specific(&specific_out, &astruct_in,
                                              &meta_single, 1));
    cdd_c_abstract_struct_free(&astruct_in);
  }

  /* Wrong type for INT32 */
  {
    cdd_c_prop_meta_t p_single;
    cdd_c_meta_t meta_single;
    cdd_c_variant_t v;
    memset(&p_single, 0, sizeof(p_single));
    memset(&meta_single, 0, sizeof(meta_single));
    p_single.name = "id";
    p_single.type = "C_ORM_TYPE_INT32";
    meta_single.num_props = 1;
    meta_single.props = &p_single;
    v.type = CDD_C_VARIANT_TYPE_STRING;
    v.value.s_val = c_mut_s8;
    cdd_c_abstract_struct_init(&astruct_in);
    cdd_c_abstract_set(&astruct_in, "id", &v);
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              (int)cdd_c_abstract_to_specific(&specific_out, &astruct_in,
                                              &meta_single, 1));
    ASSERT_EQ(0, (int)cdd_c_abstract_to_specific(&specific_out, &astruct_in,
                                                 &meta_single, 0));
    cdd_c_abstract_struct_free(&astruct_in);
  }

  /* Wrong type for FLOAT */
  {
    cdd_c_prop_meta_t p_single;
    cdd_c_meta_t meta_single;
    cdd_c_variant_t v;
    memset(&p_single, 0, sizeof(p_single));
    memset(&meta_single, 0, sizeof(meta_single));
    p_single.name = "ratio";
    p_single.type = "C_ORM_TYPE_FLOAT";
    meta_single.num_props = 1;
    meta_single.props = &p_single;
    v.type = CDD_C_VARIANT_TYPE_STRING;
    v.value.s_val = c_mut_s9;
    cdd_c_abstract_struct_init(&astruct_in);
    cdd_c_abstract_set(&astruct_in, "ratio", &v);
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              (int)cdd_c_abstract_to_specific(&specific_out, &astruct_in,
                                              &meta_single, 1));
    ASSERT_EQ(0, (int)cdd_c_abstract_to_specific(&specific_out, &astruct_in,
                                                 &meta_single, 0));
    cdd_c_abstract_struct_free(&astruct_in);
  }

  /* Wrong type for STRING with length > 0 */
  {
    cdd_c_variant_t v;
    v.type = CDD_C_VARIANT_TYPE_INT;
    v.value.i_val = 123;
    cdd_c_abstract_struct_init(&astruct_in);
    cdd_c_abstract_set(&astruct_in, "greeting", &v);
    ASSERT_EQ(
        C_ORM_ERROR_VALIDATION,
        (int)cdd_c_abstract_to_specific(&specific_out, &astruct_in, &meta, 1));
    ASSERT_EQ(0, (int)cdd_c_abstract_to_specific(&specific_out, &astruct_in,
                                                 &meta, 0));
    cdd_c_abstract_struct_free(&astruct_in);
  }

  /* INT64 wrong type */
  {
    cdd_c_prop_meta_t p;
    cdd_c_meta_t m;
    cdd_c_variant_t v;
    memset(&p, 0, sizeof(p));
    memset(&m, 0, sizeof(m));
    p.name = "id64";
    p.type = "C_ORM_TYPE_INT64";
    m.num_props = 1;
    m.props = &p;
    cdd_c_abstract_struct_init(&astruct_in);
    v.type = CDD_C_VARIANT_TYPE_STRING;
    v.value.s_val = c_mut_s10;
    cdd_c_abstract_set(&astruct_in, "id64", &v);
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, (int)cdd_c_abstract_to_specific(
                                          &specific_out, &astruct_in, &m, 1));
    ASSERT_EQ(
        0, (int)cdd_c_abstract_to_specific(&specific_out, &astruct_in, &m, 0));
    cdd_c_abstract_struct_free(&astruct_in);
  }

  /* DOUBLE wrong type */
  {
    cdd_c_prop_meta_t p;
    cdd_c_meta_t m;
    cdd_c_variant_t v;
    memset(&p, 0, sizeof(p));
    memset(&m, 0, sizeof(m));
    p.name = "dbl";
    p.type = "C_ORM_TYPE_DOUBLE";
    m.num_props = 1;
    m.props = &p;
    cdd_c_abstract_struct_init(&astruct_in);
    v.type = CDD_C_VARIANT_TYPE_STRING;
    v.value.s_val = c_mut_s11;
    cdd_c_abstract_set(&astruct_in, "dbl", &v);
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, (int)cdd_c_abstract_to_specific(
                                          &specific_out, &astruct_in, &m, 1));
    ASSERT_EQ(
        0, (int)cdd_c_abstract_to_specific(&specific_out, &astruct_in, &m, 0));
    cdd_c_abstract_struct_free(&astruct_in);
  }

  /* STRING dynamic wrong type */
  {
    cdd_c_prop_meta_t p;
    cdd_c_meta_t m;
    cdd_c_variant_t v;
    memset(&p, 0, sizeof(p));
    memset(&m, 0, sizeof(m));
    p.name = "str_dyn";
    p.type = "C_ORM_TYPE_STRING";
    m.num_props = 1;
    m.props = &p;
    cdd_c_abstract_struct_init(&astruct_in);
    v.type = CDD_C_VARIANT_TYPE_INT;
    v.value.i_val = 123;
    cdd_c_abstract_set(&astruct_in, "str_dyn", &v);
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, (int)cdd_c_abstract_to_specific(
                                          &specific_out, &astruct_in, &m, 1));
    ASSERT_EQ(
        0, (int)cdd_c_abstract_to_specific(&specific_out, &astruct_in, &m, 0));
    cdd_c_abstract_struct_free(&astruct_in);
  }

  /* Unknown type */
  {
    cdd_c_prop_meta_t p;
    cdd_c_meta_t m;
    cdd_c_variant_t v;
    int dummy = 0;
    memset(&p, 0, sizeof(p));
    memset(&m, 0, sizeof(m));
    p.name = "unk";
    p.type = "UNKNOWN_TYPE";
    m.num_props = 1;
    m.props = &p;
    cdd_c_abstract_struct_init(&astruct_in);
    v.type = CDD_C_VARIANT_TYPE_INT;
    v.value.i_val = 123;
    cdd_c_abstract_set(&astruct_in, "unk", &v);
    ASSERT_EQ(0, (int)cdd_c_specific_to_abstract(&astruct_in, &dummy, &m));
    ASSERT_EQ(
        0, (int)cdd_c_abstract_to_specific(&specific_out, &astruct_in, &m, 0));
    cdd_c_abstract_struct_free(&astruct_in);
  }

  PASS();
}

/**
 * @brief Tests hydrate null.
 * @return GREATEST test result.
 */
TEST test_hydrate_null(void) {
  cdd_c_abstract_struct_t astruct;
  cdd_c_column_meta_t cols[1];
  void *row_data[1];
  c_orm_int64_t i_val = 100;
  c_orm_int64_t bool_val = 1;
  double d_val = 2.5;
  char s_val[10];
  unsigned char blob_bytes[3] = {0xAA, 0xBB, 0xCC};
  c_orm_blob_t blob_val;
  cdd_c_variant_t *out_val = NULL;
  int types[13];
  size_t i;

  blob_val.data = blob_bytes;
  blob_val.size = sizeof(blob_bytes);

  types[0] = SQL_TYPE_INT;
  types[1] = SQL_TYPE_BIGINT;
  types[2] = SQL_TYPE_BOOLEAN;
  types[3] = SQL_TYPE_FLOAT;
  types[4] = SQL_TYPE_DOUBLE;
  types[5] = SQL_TYPE_DECIMAL;
  types[6] = SQL_TYPE_VARCHAR;
  types[7] = SQL_TYPE_TEXT;
  types[8] = SQL_TYPE_CHAR;
  types[9] = SQL_TYPE_DATE;
  types[10] = SQL_TYPE_TIMESTAMP;
  types[11] = SQL_TYPE_BLOB;
  types[12] = SQL_TYPE_UNKNOWN;

  C_ORM_STRCPY(s_val, sizeof(s_val), "test");

  cols[0].name = "id";
  cols[0].inferred_type = SQL_TYPE_INT;
  row_data[0] = NULL;

  ASSERT_EQ(0, (int)cdd_c_abstract_hydrate(&astruct, row_data, cols, 1));
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_get(&astruct, "id", &out_val));
  ASSERT_EQ(CDD_C_VARIANT_TYPE_NULL, out_val->type);
  cdd_c_abstract_struct_free(&astruct);

  for (i = 0; i < 13; i++) {
    cols[0].name = "col";
    cols[0].inferred_type = types[i];
    if (types[i] == SQL_TYPE_INT || types[i] == SQL_TYPE_BIGINT) {
      row_data[0] = &i_val;
    } else if (types[i] == SQL_TYPE_BOOLEAN) {
      row_data[0] = &bool_val;
    } else if (types[i] == SQL_TYPE_FLOAT || types[i] == SQL_TYPE_DOUBLE ||
               types[i] == SQL_TYPE_DECIMAL) {
      row_data[0] = &d_val;
    } else if (types[i] == SQL_TYPE_BLOB) {
      row_data[0] = &blob_val;
    } else {
      row_data[0] = s_val;
    }
    ASSERT_EQ(0, (int)cdd_c_abstract_hydrate(&astruct, row_data, cols, 1));
    ASSERT_EQ(C_ORM_OK, cdd_c_abstract_get(&astruct, "col", &out_val));
    if (types[i] == SQL_TYPE_INT || types[i] == SQL_TYPE_BIGINT) {
      ASSERT_EQ(CDD_C_VARIANT_TYPE_INT, out_val->type);
      ASSERT_EQ(100, out_val->value.i_val);
    } else if (types[i] == SQL_TYPE_BOOLEAN) {
      ASSERT_EQ(CDD_C_VARIANT_TYPE_INT, out_val->type);
      ASSERT_EQ(1, out_val->value.i_val);
    } else if (types[i] == SQL_TYPE_FLOAT || types[i] == SQL_TYPE_DOUBLE ||
               types[i] == SQL_TYPE_DECIMAL) {
      ASSERT_EQ(CDD_C_VARIANT_TYPE_FLOAT, out_val->type);
      ASSERT_EQ(2.5, out_val->value.f_val);
    } else if (types[i] == SQL_TYPE_BLOB) {
      ASSERT_EQ(CDD_C_VARIANT_TYPE_BLOB, out_val->type);
      ASSERT_EQ(3U, (unsigned int)out_val->value.b_val.size);
      ASSERT_EQ(0xBB, out_val->value.b_val.data[1]);
    } else if (types[i] == SQL_TYPE_UNKNOWN) {
      ASSERT_EQ(CDD_C_VARIANT_TYPE_BLOB, out_val->type);
      ASSERT_EQ(0U, (unsigned int)out_val->value.b_val.size);
    } else {
      ASSERT_EQ(CDD_C_VARIANT_TYPE_STRING, out_val->type);
      ASSERT_STR_EQ("test", out_val->value.s_val);
    }
    cdd_c_abstract_struct_free(&astruct);
  }
  PASS();
}

/**
 * @brief Tests abstract struct allocation limits.
 * @return GREATEST test result.
 */
TEST test_abstract_struct_allocation_limits(void) {
  static unsigned char c_mut_s12[] = "fake";
  static unsigned char c_mut_s13[] = "fake";

  cdd_c_abstract_struct_array_t arr;
  cdd_c_abstract_struct_t astruct1, astruct2;
  cdd_c_variant_t v;
  char *json = NULL;

  (void)json;
  /* init array with huge size */
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_struct_array_init(
                &arr, ((size_t)-1) / sizeof(cdd_c_abstract_struct_t) + 1));

  /* arr_append integer overflow */
  ASSERT_EQ(0, (int)cdd_c_abstract_struct_array_init(&arr, 1));
  arr.capacity = ((size_t)-1) / 2 + 1; /* fake capacity */
  arr.count = arr.capacity;
  ASSERT_EQ(0, cdd_c_abstract_struct_init(&astruct1));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_struct_array_append(&arr, &astruct1));

  arr.capacity = ((size_t)-1) / sizeof(cdd_c_abstract_struct_t) - 1;
  arr.count = arr.capacity;
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_struct_array_append(&arr, &astruct1));
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
  astruct1.kvs[0].value.value.b_val.data = c_mut_s12;
  astruct1.kvs[0].value.value.b_val.size = (size_t)-1;
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            (int)cdd_c_abstract_struct_deep_copy(&astruct2, &astruct1));
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
  astruct1.kvs[1].value.value.b_val.data = c_mut_s13;
  astruct1.kvs[1].value.value.b_val.size = 0;

  ASSERT_EQ(0, (int)cdd_c_abstract_struct_deep_copy(&astruct2, &astruct1));
  astruct1.kvs[1].value.value.b_val.data = NULL;
  cdd_c_abstract_struct_free(&astruct1);
  cdd_c_abstract_struct_free(&astruct2);

  /* variant free with null pointers */
  v.type = CDD_C_VARIANT_TYPE_STRING;
  v.value.s_val = NULL;
  cdd_c_variant_free(&v);

  v.type = CDD_C_VARIANT_TYPE_BLOB;
  v.value.b_val.data = NULL;
  v.value.b_val.size = 0;
  cdd_c_variant_free(&v);

  PASS();
}

static int g_as_db2_mode = 0;
static int g_as_row = 0;
static int g_as_fail_finalize = 0;
static int g_as_fail_step_on_2 = 0;
static int g_as_fail_bind = 0;
static int g_as_fail_getters = 0;
static int g_as_dummy_h = 1;

/**
 * @brief Mock SQLAllocHandle for abstract_struct ODBC schema inspection tests.
 * @param HandleType ODBC handle type.
 * @param InputHandle Parent handle.
 * @param OutputHandlePtr Output handle pointer.
 * @return C_ORM_SQL_SUCCESS.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API ASMock_AllocHandle(
    c_orm_sqlsmallint_t HandleType, c_orm_sqlhandle_t InputHandle,
    c_orm_sqlhandle_t *OutputHandlePtr) {
  (void)HandleType;
  (void)InputHandle;
  *OutputHandlePtr = &g_as_dummy_h;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLFreeHandle for abstract_struct ODBC schema inspection tests.
 * @param HandleType ODBC handle type.
 * @param Handle Handle to release.
 * @return C_ORM_SQL_SUCCESS.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API
ASMock_FreeHandle(c_orm_sqlsmallint_t HandleType, c_orm_sqlhandle_t Handle) {
  (void)HandleType;
  (void)Handle;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLSetEnvAttr for abstract_struct ODBC schema inspection tests.
 * @param h Environment handle.
 * @param a Attribute identifier.
 * @param v Attribute value pointer.
 * @param l Attribute length.
 * @return C_ORM_SQL_SUCCESS.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API ASMock_SetEnvAttr(c_orm_sqlhenv_t h,
                                                         c_orm_sqlinteger_t a,
                                                         c_orm_sqlpointer_t v,
                                                         c_orm_sqlinteger_t l) {
  (void)h;
  (void)a;
  (void)v;
  (void)l;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLDriverConnect for abstract_struct ODBC schema tests.
 * @param h Connection handle.
 * @param w Window handle.
 * @param in Input connection string.
 * @param l1 Input string length.
 * @param out Output connection string buffer.
 * @param cap Output buffer capacity.
 * @param l2 Output string length pointer.
 * @param drv Driver completion flag.
 * @return C_ORM_SQL_SUCCESS.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API ASMock_DriverConnect(
    c_orm_sqlhdbc_t h, c_orm_sqlhwnd_t w, c_orm_sqlchar_t *in,
    c_orm_sqlsmallint_t l1, c_orm_sqlchar_t *out, c_orm_sqlsmallint_t cap,
    c_orm_sqlsmallint_t *l2, c_orm_sqlusmallint_t drv) {
  (void)h;
  (void)w;
  (void)in;
  (void)l1;
  (void)out;
  (void)cap;
  (void)drv;
  *l2 = 0;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLDisconnect for abstract_struct ODBC schema tests.
 * @param h Connection handle.
 * @return C_ORM_SQL_SUCCESS.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API ASMock_Disconnect(c_orm_sqlhdbc_t h) {
  (void)h;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLEndTran for abstract_struct ODBC schema tests.
 * @param t Handle type.
 * @param h Handle.
 * @param c Completion type.
 * @return C_ORM_SQL_SUCCESS.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API ASMock_EndTran(c_orm_sqlsmallint_t t,
                                                      c_orm_sqlhandle_t h,
                                                      c_orm_sqlsmallint_t c) {
  (void)t;
  (void)h;
  (void)c;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLPrepare for abstract_struct ODBC schema tests.
 * @param h Statement handle.
 * @param s SQL text.
 * @param l SQL text length.
 * @return C_ORM_SQL_SUCCESS.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API ASMock_Prepare(c_orm_sqlhstmt_t h,
                                                      c_orm_sqlchar_t *s,
                                                      c_orm_sqlinteger_t l) {
  (void)h;
  (void)s;
  (void)l;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLExecute for abstract_struct ODBC schema tests.
 * @param h Statement handle.
 * @return C_ORM_SQL_SUCCESS.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API ASMock_Execute(c_orm_sqlhstmt_t h) {
  (void)h;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLBindParameter for abstract_struct ODBC schema tests.
 * @param h Statement handle.
 * @param p 1-based parameter ordinal.
 * @param io Input/output direction.
 * @param vt C value type.
 * @param pt SQL parameter type.
 * @param cs Column size.
 * @param dd Decimal digits.
 * @param vp Parameter value buffer.
 * @param bl Buffer length.
 * @param ind Length/null indicator pointer.
 * @return C_ORM_SQL_SUCCESS.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API ASMock_BindParameter(
    c_orm_sqlhstmt_t h, c_orm_sqlusmallint_t p, c_orm_sqlsmallint_t io,
    c_orm_sqlsmallint_t vt, c_orm_sqlsmallint_t pt, c_orm_sqlulen_t cs,
    c_orm_sqlsmallint_t dd, c_orm_sqlpointer_t vp, c_orm_sqllen_t bl,
    c_orm_sqllen_t *ind) {
  (void)h;
  (void)p;
  (void)io;
  (void)vt;
  (void)pt;
  (void)cs;
  (void)dd;
  (void)vp;
  (void)bl;
  (void)ind;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLNumResultCols for abstract_struct ODBC schema tests.
 * @param h Statement handle.
 * @param c Output column count.
 * @return C_ORM_SQL_SUCCESS.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API
ASMock_NumResultCols(c_orm_sqlhstmt_t h, c_orm_sqlsmallint_t *c) {
  (void)h;
  *c = 0;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLFetch for abstract_struct ODBC schema tests.
 * @param h Statement handle.
 * @return C_ORM_SQL_SUCCESS or C_ORM_SQL_NO_DATA.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API ASMock_Fetch(c_orm_sqlhstmt_t h) {
  (void)h;
  g_as_row++;
  return g_as_row <= g_as_db2_mode ? C_ORM_SQL_SUCCESS : C_ORM_SQL_NO_DATA;
}

/**
 * @brief Mock SQLGetData returning catalog metadata columns.
 * @param h Statement handle.
 * @param col 1-based column ordinal.
 * @param tt Target C type.
 * @param vp Destination value buffer.
 * @param bl Buffer capacity.
 * @param ind Output byte length / null indicator.
 * @return C_ORM_SQL_SUCCESS.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API ASMock_GetData(
    c_orm_sqlhstmt_t h, c_orm_sqlusmallint_t col, c_orm_sqlsmallint_t tt,
    c_orm_sqlpointer_t vp, c_orm_sqllen_t bl, c_orm_sqllen_t *ind) {
  (void)h;
  (void)tt;
  if (col == 4) {
    C_ORM_STRCPY((char *)vp, (size_t)bl, "AMT");
    *ind = 3;
  } else if (col == 6) {
    C_ORM_STRCPY((char *)vp, (size_t)bl, "DECIMAL");
    *ind = 7;
  } else if (col == 7) {
    *(c_orm_sqlinteger_t *)vp = 11;
    *ind = 4;
  } else if (col == 9) {
    *(c_orm_sqlsmallint_t *)vp = 2;
    *ind = 2;
  } else {
    ASSERT_EQ(18, (int)col);
    C_ORM_STRCPY((char *)vp, (size_t)bl, g_as_row == 1 ? "NO" : "YES");
    *ind = g_as_row == 1 ? 2 : 3;
  }
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLFreeStmt for abstract_struct ODBC schema tests.
 * @param h Statement handle.
 * @param o Free option.
 * @return C_ORM_SQL_SUCCESS.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API ASMock_FreeStmt(c_orm_sqlhstmt_t h,
                                                       c_orm_sqlusmallint_t o) {
  (void)h;
  (void)o;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLGetDiagRec for abstract_struct ODBC schema tests.
 * @param ht Handle type.
 * @param h Handle.
 * @param r 1-based record ordinal.
 * @param st Output SQLSTATE buffer.
 * @param ne Output native error code.
 * @param m Output diagnostic message buffer.
 * @param bl Message buffer capacity.
 * @param tl Output message length.
 * @return C_ORM_SQL_NO_DATA.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API ASMock_GetDiagRec(
    c_orm_sqlsmallint_t ht, c_orm_sqlhandle_t h, c_orm_sqlsmallint_t r,
    c_orm_sqlchar_t *st, c_orm_sqlinteger_t *ne, c_orm_sqlchar_t *m,
    c_orm_sqlsmallint_t bl, c_orm_sqlsmallint_t *tl) {
  (void)ht;
  (void)h;
  (void)r;
  (void)st;
  (void)ne;
  (void)m;
  (void)bl;
  (void)tl;
  return C_ORM_SQL_NO_DATA;
}

/**
 * @brief Mock SQLColumns for abstract_struct ODBC schema tests.
 * @param h Statement handle.
 * @param c1 Catalog name.
 * @param l1 Catalog name length.
 * @param c2 Schema name.
 * @param l2 Schema name length.
 * @param c3 Table name.
 * @param l3 Table name length.
 * @param c4 Column name filter.
 * @param l4 Column filter length.
 * @return C_ORM_SQL_SUCCESS or C_ORM_SQL_ERROR.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API ASMock_Columns(
    c_orm_sqlhstmt_t h, c_orm_sqlchar_t *c1, c_orm_sqlsmallint_t l1,
    c_orm_sqlchar_t *c2, c_orm_sqlsmallint_t l2, c_orm_sqlchar_t *c3,
    c_orm_sqlsmallint_t l3, c_orm_sqlchar_t *c4, c_orm_sqlsmallint_t l4) {
  (void)h;
  (void)c1;
  (void)l1;
  (void)c2;
  (void)l2;
  (void)c3;
  (void)l3;
  (void)c4;
  (void)l4;
  if (g_as_db2_mode < 0) {
    return C_ORM_SQL_ERROR;
  }
  g_as_row = 0;
  return C_ORM_SQL_SUCCESS;
}

static int g_as_max_rows = 2;

/**
 * @brief Mock prepare vtable callback for Db2 catalog introspection tests.
 * @param db Database handle.
 * @param sql SQL query string.
 * @param out_q Output prepared query handle.
 * @return C_ORM_OK or C_ORM_ERROR_SQL.
 */
static c_orm_error_t as_mock_prepare(c_orm_db_t *db, const char *sql,
                                     c_orm_query_t **out_q) {
  (void)db;
  if (g_as_db2_mode == 1 && strstr(sql, "SYSCAT.COLUMNS") != NULL) {
    g_as_row = 0;
    *out_q = (c_orm_query_t *)&g_as_dummy_h;
    return C_ORM_OK;
  }
  if (g_as_db2_mode == 2 && strstr(sql, "SYSIBM.SYSCOLUMNS") != NULL) {
    g_as_row = 0;
    *out_q = (c_orm_query_t *)&g_as_dummy_h;
    return C_ORM_OK;
  }
  if (g_as_db2_mode == 3) {
    /* Both prepare succeed, step fails on LUW, z/OS succeeds */
    g_as_row = (strstr(sql, "SYSCAT") != NULL) ? -99 : 0;
    *out_q = (c_orm_query_t *)&g_as_dummy_h;
    return C_ORM_OK;
  }
  if (g_as_db2_mode == 4) {
    /* Both prepare succeed, both fail on bind/step -> falls back to ODBC */
    g_as_row = -99;
    *out_q = (c_orm_query_t *)&g_as_dummy_h;
    return C_ORM_OK;
  }
  if (g_as_db2_mode == 5 && strstr(sql, "SYSIBM.SYSCOLUMNS") != NULL) {
    g_as_row = -99;
    *out_q = (c_orm_query_t *)&g_as_dummy_h;
    return C_ORM_OK;
  }
  return C_ORM_ERROR_SQL;
}

/**
 * @brief Mock bind_string vtable callback for Db2 catalog tests.
 * @param q Query handle.
 * @param idx 1-based bind index.
 * @param v String value.
 * @return C_ORM_OK or C_ORM_ERROR_BIND.
 */
static c_orm_error_t as_mock_bind_str(c_orm_query_t *q, int idx,
                                      const char *v) {
  (void)q;
  (void)idx;
  (void)v;
  return g_as_fail_bind ? C_ORM_ERROR_BIND : C_ORM_OK;
}

/**
 * @brief Mock step vtable callback for Db2 catalog tests.
 * @param q Query handle.
 * @param out_has Output row availability flag.
 * @return C_ORM_OK or C_ORM_ERROR_STEP.
 */
static c_orm_error_t as_mock_step(c_orm_query_t *q, int *out_has) {
  (void)q;
  if (g_as_row == -99) {
    return C_ORM_ERROR_STEP;
  }
  g_as_row++;
  if (g_as_fail_step_on_2 && g_as_row == 2) {
    return C_ORM_ERROR_STEP;
  }
  *out_has = (g_as_row <= g_as_max_rows) ? 1 : 0;
  return C_ORM_OK;
}

/**
 * @brief Mock get_string vtable callback for Db2 catalog tests.
 * @param q Query handle.
 * @param idx 0-based column index.
 * @param out Output string pointer.
 * @return C_ORM_OK or C_ORM_ERROR_SQL.
 */
static c_orm_error_t as_mock_get_str(c_orm_query_t *q, int idx,
                                     const char **out) {
  static const int fail_col[4] = {-1, 0, 1, 4};
  (void)q;
  if (g_as_fail_getters >= 1 && g_as_fail_getters <= 3 &&
      idx == fail_col[g_as_fail_getters]) {
    return C_ORM_ERROR_SQL;
  }
  if (g_as_fail_getters == 6) {
    if (idx == 0) {
      *out = (g_as_row == 1) ? NULL : "COL2";
    } else if (idx == 1) {
      *out = (g_as_row == 2) ? NULL : "DECIMAL";
    } else {
      *out = NULL;
    }
    return C_ORM_OK;
  }
  if (idx == 0) {
    *out = (g_as_row == 1) ? "BAL" : "NOTE";
  } else if (idx == 1) {
    *out = "DECIMAL";
  } else {
    *out = (g_as_row == 1) ? "N" : "Y";
  }
  return C_ORM_OK;
}

/**
 * @brief Mock get_int32 vtable callback for Db2 catalog tests.
 * @param q Query handle.
 * @param idx 0-based column index.
 * @param out Output int32 value.
 * @return C_ORM_OK or C_ORM_ERROR_SQL.
 */
static c_orm_error_t as_mock_get_i32(c_orm_query_t *q, int idx, int32_t *out) {
  (void)q;
  if (g_as_fail_getters == 4 || (g_as_fail_getters == 5 && idx == 3)) {
    return C_ORM_ERROR_SQL;
  }
  *out = (idx == 2) ? 9 : 2;
  return C_ORM_OK;
}

/**
 * @brief Mock finalize vtable callback for Db2 catalog tests.
 * @param q Query handle.
 * @return C_ORM_OK or C_ORM_ERROR_SQL.
 */
static c_orm_error_t as_mock_finalize(c_orm_query_t *q) {
  (void)q;
  return g_as_fail_finalize ? C_ORM_ERROR_SQL : C_ORM_OK;
}

/** @brief Host struct fixture with an exact `c_orm_decimal_t` field. */
typedef struct MockDecStruct {
  /** @brief Price decimal field. */
  c_orm_decimal_t price;
} mock_dec_struct_t;

/** @brief Covers DECIMAL variants and ODBC/Db2 schema inspection in
 * `src/abstract_struct.c`. */
TEST test_abstract_decimal_odbc_db2_coverage(void) {
  static char c_mut_s14[] = "x";
  static char c_mut_s15[] = "hello";
  static char c_mut_s16[] = "77.25";
  static char c_mut_s17[] = "not_a_dec";

  cdd_c_abstract_struct_t as1;
  cdd_c_abstract_struct_t as2;
  cdd_c_abstract_struct_array_t arr;
  cdd_c_variant_t v;
  char *json = NULL;
  mock_dec_struct_t ds_in;
  mock_dec_struct_t ds_out;
  cdd_c_prop_meta_t dprop;
  cdd_c_meta_t dmeta;
  c_orm_table_meta_t tmeta;
  cdd_c_column_meta_t *out_cmeta = NULL;
  size_t out_nc = 0;
  c_orm_odbc_api_t oapi;
  c_orm_db_t *odb = NULL;
  c_orm_driver_vtable_t dvt;
  c_orm_db_t mock_db;
  int step;

  /* 1. DECIMAL variant copy, print, to_json, array_to_json (valid & invalid) */
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_init(&as1));
  v.type = CDD_C_VARIANT_TYPE_DECIMAL;
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("12345.67", &v.value.d_val));
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_set(&as1, "amount", &v));
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_deep_copy(&as2, &as1));
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_print(&as2));
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_to_json(&as2, &json));
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(json));
  json = NULL;

  /* Corrupt precision (>38) to exercise c_orm_decimal_to_str error branches */
  as2.kvs[0].value.value.d_val.precision = 99;
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_print(&as2));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, cdd_c_abstract_struct_to_json(&as2, &json));

  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_array_init(&arr, 2));
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_array_append(&arr, &as1));
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_array_to_json(&arr, &json));
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(json));
  json = NULL;
  ASSERT_EQ(JSONSuccess, json_set_allocation_functions(mock_parson_malloc,
                                                       mock_parson_free));
  for (step = 0; step < 10; step++) {
    astruct_oom_countdown = step;
    if (cdd_c_abstract_struct_to_json(&arr.items[0], &json) == C_ORM_OK) {
      ASSERT_EQ(C_ORM_OK, c_orm_system_free(json));
      json = NULL;
    }
  }
  for (step = 0; step < 15; step++) {
    astruct_oom_countdown = step;
    if (cdd_c_abstract_struct_array_to_json(&arr, &json) == C_ORM_OK) {
      ASSERT_EQ(C_ORM_OK, c_orm_system_free(json));
      json = NULL;
    }
  }
  astruct_oom_countdown = -1;
  ASSERT_EQ(JSONSuccess, json_set_allocation_functions(NULL, NULL));

  /* cdd_c_realloc > 2147483647 size guard */
  {
    cdd_c_abstract_struct_array_t huge_arr;
    memset(&huge_arr, 0, sizeof(huge_arr));
    huge_arr.capacity =
        ((size_t)1073741824U / sizeof(cdd_c_abstract_struct_t)) + 1U;
    huge_arr.count = huge_arr.capacity;
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              cdd_c_abstract_struct_array_append(&huge_arr, &as2));
  }
  arr.items[0].kvs[0].value.value.d_val.precision = 99;
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            cdd_c_abstract_struct_array_to_json(&arr, &json));
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_array_free(&arr));
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_free(&as2));

  /* 2. DECIMAL hydration & table_meta_to_column_meta & specific<->abstract */
  {
    cdd_c_column_meta_t hcols[2];
    void *hrow[2];
    c_orm_decimal_t dval;
    c_orm_decimal_from_str("-99.95", &dval);
    hcols[0].name = "dec_col";
    hcols[0].inferred_type = SQL_TYPE_NUMERIC;
    hcols[1].name = "df_col";
    hcols[1].inferred_type = SQL_TYPE_DECFLOAT;
    hrow[0] = &dval;
    hrow[1] = &dval;
    ASSERT_EQ(C_ORM_OK, cdd_c_abstract_hydrate(&as1, hrow, hcols, 2));
    ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_free(&as1));
  }

  {
    c_orm_column_meta_t all_tcols[11];
    c_orm_type_t all_types[11] = {
        C_ORM_TYPE_INT32,  C_ORM_TYPE_INT64,   C_ORM_TYPE_FLOAT,
        C_ORM_TYPE_DOUBLE, C_ORM_TYPE_DECIMAL, C_ORM_TYPE_BOOL,
        C_ORM_TYPE_BLOB,   C_ORM_TYPE_DATE,    C_ORM_TYPE_TIMESTAMP,
        C_ORM_TYPE_STRING, C_ORM_TYPE_DECFLOAT};
    memset(all_tcols, 0, sizeof(all_tcols));
    for (step = 0; step < 11; step++) {
      all_tcols[step].name = "col";
      all_tcols[step].type = all_types[step];
    }
    ASSERT_EQ(C_ORM_OK, c_orm_free_column_meta(NULL));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_table_meta_to_column_meta(NULL, &out_cmeta, &out_nc));
    memset(&tmeta, 0, sizeof(tmeta));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_table_meta_to_column_meta(&tmeta, NULL, &out_nc));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_table_meta_to_column_meta(&tmeta, &out_cmeta, NULL));
    tmeta.num_columns = 0;
    ASSERT_EQ(C_ORM_OK,
              c_orm_table_meta_to_column_meta(&tmeta, &out_cmeta, &out_nc));
    tmeta.columns = all_tcols;
    tmeta.num_columns = 11;
    old_malloc = c_orm_malloc;
    old_realloc = c_orm_realloc;
    c_orm_set_allocators(mock_malloc_astruct, old_realloc, c_orm_free);
    astruct_oom_countdown = 0;
    ASSERT_EQ(C_ORM_ERROR_MEMORY,
              c_orm_table_meta_to_column_meta(&tmeta, &out_cmeta, &out_nc));
    astruct_oom_countdown = -1;
    c_orm_set_allocators(old_malloc, old_realloc, c_orm_free);
    ASSERT_EQ(C_ORM_OK,
              c_orm_table_meta_to_column_meta(&tmeta, &out_cmeta, &out_nc));
    ASSERT_EQ(C_ORM_OK, c_orm_free_column_meta(out_cmeta));
  }

  /* String abstract_to_specific inline NULL failure & heap strdup OOM */
  {
    /** @brief Local string layout fixture for abstract_to_specific tests. */
    struct AsStrOut {
      /** @brief Fixed-capacity inline string buffer. */
      char fixed_s[8];
      /** @brief Heap-allocated string pointer. */
      char *heap_s;
    } s_out;
    cdd_c_prop_meta_t sprops[2];
    cdd_c_meta_t smeta;
    memset(&s_out, 0, sizeof(s_out));
    memset(sprops, 0, sizeof(sprops));
    sprops[0].name = "fixed_s";
    sprops[0].type = "C_ORM_TYPE_STRING";
    sprops[0].offset = 0;
    sprops[0].length = 8;
    sprops[1].name = "heap_s";
    sprops[1].type = "C_ORM_TYPE_STRING";
    sprops[1].offset = 8;
    sprops[1].length = 0;
    smeta.name = "s_out";
    smeta.size = sizeof(s_out);
    smeta.num_props = 2;
    smeta.props = sprops;

    ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_init(&as1));
    v.type = CDD_C_VARIANT_TYPE_STRING;
    v.value.s_val = c_mut_s14;
    ASSERT_EQ(C_ORM_OK, cdd_c_abstract_set(&as1, "fixed_s", &v));
    as1.kvs[0].value.value.s_val = NULL;
    ASSERT_NEQ(C_ORM_OK, cdd_c_abstract_to_specific(&s_out, &as1, &smeta, 0));
    ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_free(&as1));

    ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_init(&as1));
    v.type = CDD_C_VARIANT_TYPE_STRING;
    v.value.s_val = c_mut_s15;
    ASSERT_EQ(C_ORM_OK, cdd_c_abstract_set(&as1, "heap_s", &v));
    old_malloc = c_orm_malloc;
    old_realloc = c_orm_realloc;
    c_orm_set_allocators(mock_malloc_astruct, old_realloc, c_orm_free);
    astruct_oom_countdown = 0;
    ASSERT_EQ(C_ORM_ERROR_MEMORY,
              cdd_c_abstract_to_specific(&s_out, &as1, &smeta, 0));
    astruct_oom_countdown = -1;
    c_orm_set_allocators(old_malloc, old_realloc, c_orm_free);
    ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_free(&as1));
  }

  memset(&dprop, 0, sizeof(dprop));
  dprop.name = "price";
  dprop.type = "C_ORM_TYPE_DECIMAL";
  dprop.offset = offsetof(mock_dec_struct_t, price);
  dmeta.name = "mock_dec_struct_t";
  dmeta.size = sizeof(mock_dec_struct_t);
  dmeta.num_props = 1;
  dmeta.props = &dprop;

  c_orm_decimal_from_str("42.50", &ds_in.price);
  ASSERT_EQ(C_ORM_OK, cdd_c_specific_to_abstract(&as1, &ds_in, &dmeta));
  memset(&ds_out, 0, sizeof(ds_out));
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_to_specific(&ds_out, &as1, &dmeta, 1));
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_free(&as1));

  /* abstract_to_specific DECIMAL from valid string, bad string, mismatch */
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_init(&as1));
  v.type = CDD_C_VARIANT_TYPE_STRING;
  v.value.s_val = c_mut_s16;
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_set(&as1, "price", &v));
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_to_specific(&ds_out, &as1, &dmeta, 1));
  v.value.s_val = c_mut_s17;
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_set(&as1, "price", &v));
  ASSERT_NEQ(C_ORM_OK, cdd_c_abstract_to_specific(&ds_out, &as1, &dmeta, 1));
  v.type = CDD_C_VARIANT_TYPE_INT;
  v.value.i_val = 5;
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_set(&as1, "price", &v));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            cdd_c_abstract_to_specific(&ds_out, &as1, &dmeta, 1));
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_to_specific(&ds_out, &as1, &dmeta, 0));
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_free(&as1));

  /* JSON non-object/array/boolean/null/float roundtrip branches */
  ASSERT_EQ(
      C_ORM_OK,
      cdd_c_abstract_struct_from_json(
          "{\"i\":1,\"f\":1.5,\"bt\":true,\"bf\":false,\"n\":null,\"a\":[]}",
          &as1));
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_free(&as1));

  /* 3. cdd_c_inspect_schema_odbc & cdd_c_inspect_schema_db2 */
  memset(&oapi, 0, sizeof(oapi));
  oapi.SQLAllocHandle = ASMock_AllocHandle;
  oapi.SQLFreeHandle = ASMock_FreeHandle;
  oapi.SQLSetEnvAttr = ASMock_SetEnvAttr;
  oapi.SQLDriverConnect = ASMock_DriverConnect;
  oapi.SQLDisconnect = ASMock_Disconnect;
  oapi.SQLEndTran = ASMock_EndTran;
  oapi.SQLPrepare = ASMock_Prepare;
  oapi.SQLExecute = ASMock_Execute;
  oapi.SQLBindParameter = ASMock_BindParameter;
  oapi.SQLNumResultCols = ASMock_NumResultCols;
  oapi.SQLFetch = ASMock_Fetch;
  oapi.SQLGetData = ASMock_GetData;
  oapi.SQLFreeStmt = ASMock_FreeStmt;
  oapi.SQLGetDiagRec = ASMock_GetDiagRec;
  oapi.SQLColumns = ASMock_Columns;

  ASSERT_EQ(C_ORM_OK, c_orm_odbc_connect_raw(&oapi, "DSN=x", "odbc", NULL, 0, 1,
                                             0, &odb));
  {
    c_orm_sqlsmallint_t dummy_cols = 0;
    ASSERT_EQ(C_ORM_SQL_SUCCESS, oapi.SQLEndTran(0, NULL, 0));
    ASSERT_EQ(C_ORM_SQL_SUCCESS, oapi.SQLPrepare(NULL, NULL, 0));
    ASSERT_EQ(C_ORM_SQL_SUCCESS, oapi.SQLExecute(NULL));
    ASSERT_EQ(C_ORM_SQL_SUCCESS,
              oapi.SQLBindParameter(NULL, 1, 0, 0, 0, 0, 0, NULL, 0, NULL));
    ASSERT_EQ(C_ORM_SQL_SUCCESS, oapi.SQLNumResultCols(NULL, &dummy_cols));
    ASSERT_EQ(C_ORM_SQL_SUCCESS, oapi.SQLFreeStmt(NULL, 0));
  }
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, cdd_c_inspect_schema_odbc(NULL, "T", &arr));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, cdd_c_inspect_schema_odbc(odb, NULL, &arr));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, cdd_c_inspect_schema_odbc(odb, "T", NULL));

  g_as_db2_mode = -1;
  ASSERT_NEQ(C_ORM_OK, cdd_c_inspect_schema_odbc(odb, "T", &arr));
  g_as_db2_mode = 0; /* 0 columns */
  ASSERT_EQ(C_ORM_OK, cdd_c_inspect_schema_odbc(odb, "T", &arr));
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_array_free(&arr));
  g_as_db2_mode = 2; /* 2 columns (NO and YES nullable) */
  ASSERT_EQ(C_ORM_OK, cdd_c_inspect_schema_odbc(odb, "T", &arr));
  ASSERT_EQ(2U, arr.count);
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_array_free(&arr));

  /* OOM sweep on cdd_c_inspect_schema_odbc */
  old_malloc = c_orm_malloc;
  old_realloc = c_orm_realloc;
  c_orm_set_allocators(mock_malloc_astruct, mock_realloc_astruct, c_orm_free);
  for (step = 0;; step++) {
    memset(&arr, 0, sizeof(arr));
    astruct_oom_countdown = step;
    if (cdd_c_inspect_schema_odbc(odb, "T", &arr) == C_ORM_OK) {
      astruct_oom_countdown = -1;
      cdd_c_abstract_struct_array_free(&arr);
      break;
    }
    astruct_oom_countdown = -1;
    cdd_c_abstract_struct_array_free(&arr);
    ASSERT(step < 60);
  }
  c_orm_set_allocators(old_malloc, old_realloc, c_orm_free);

  /* Db2 inspect schema: NULL args, LUW, z/OS, ODBC fallback, getter/step/fin
   * errors, and OOM sweep */
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, cdd_c_inspect_schema_db2(NULL, "T", &arr));
  memset(&mock_db, 0, sizeof(mock_db));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            cdd_c_inspect_schema_db2(&mock_db, "T", &arr));
  memset(&dvt, 0, sizeof(dvt));
  dvt.prepare = as_mock_prepare;
  dvt.bind_string = as_mock_bind_str;
  dvt.step = as_mock_step;
  dvt.get_string = as_mock_get_str;
  dvt.get_int32 = as_mock_get_i32;
  dvt.finalize = as_mock_finalize;
  mock_db.vtable = &dvt;
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            cdd_c_inspect_schema_db2(&mock_db, NULL, &arr));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            cdd_c_inspect_schema_db2(&mock_db, "T", NULL));

  /* Mode 1: LUW SYSCAT.COLUMNS */
  g_as_db2_mode = 1;
  ASSERT_EQ(C_ORM_OK, cdd_c_inspect_schema_db2(&mock_db, "T", &arr));
  ASSERT_EQ(2U, arr.count);
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_array_free(&arr));

  /* Mode 2: z/OS SYSIBM.SYSCOLUMNS */
  g_as_db2_mode = 2;
  ASSERT_EQ(C_ORM_OK, cdd_c_inspect_schema_db2(&mock_db, "T", &arr));
  ASSERT_EQ(2U, arr.count);
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_array_free(&arr));

  /* Mode 3: LUW step fails -> z/OS succeeds; also LUW finalize fails */
  g_as_db2_mode = 3;
  ASSERT_EQ(C_ORM_OK, cdd_c_inspect_schema_db2(&mock_db, "T", &arr));
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_array_free(&arr));
  g_as_fail_finalize = 1;
  ASSERT_EQ(C_ORM_ERROR_SQL, cdd_c_inspect_schema_db2(&mock_db, "T", &arr));
  g_as_fail_finalize = 0;

  /* Mode 4: both LUW & z/OS step fail -> falls back to ODBC; also LUW bind
   * fail & z/OS fin fail */
  g_as_db2_mode = 4;
  ASSERT_NEQ(C_ORM_OK, cdd_c_inspect_schema_db2(&mock_db, "T", &arr));
  g_as_db2_mode = 1;
  g_as_fail_bind = 1;
  ASSERT_NEQ(C_ORM_OK, cdd_c_inspect_schema_db2(&mock_db, "T", &arr));
  g_as_db2_mode = 2;
  g_as_fail_finalize = 1;
  ASSERT_EQ(C_ORM_ERROR_SQL, cdd_c_inspect_schema_db2(&mock_db, "T", &arr));
  g_as_fail_bind = 0;
  g_as_db2_mode = 5;
  ASSERT_EQ(C_ORM_ERROR_SQL, cdd_c_inspect_schema_db2(&mock_db, "T", &arr));
  g_as_fail_finalize = 0;

  /* Getter failures percolate C_ORM_ERROR_SQL; step error on row 2 (+fin err)
   */
  g_as_db2_mode = 1;
  g_as_fail_getters = 1;
  ASSERT_EQ(C_ORM_ERROR_SQL, cdd_c_inspect_schema_db2(&mock_db, "T", &arr));
  g_as_fail_finalize = 1;
  ASSERT_EQ(C_ORM_ERROR_SQL, cdd_c_inspect_schema_db2(&mock_db, "T", &arr));
  g_as_fail_finalize = 0;
  g_as_fail_getters = 2;
  ASSERT_EQ(C_ORM_ERROR_SQL, cdd_c_inspect_schema_db2(&mock_db, "T", &arr));
  g_as_fail_getters = 3;
  ASSERT_EQ(C_ORM_ERROR_SQL, cdd_c_inspect_schema_db2(&mock_db, "T", &arr));
  g_as_fail_getters = 4;
  ASSERT_EQ(C_ORM_ERROR_SQL, cdd_c_inspect_schema_db2(&mock_db, "T", &arr));
  g_as_fail_getters = 5;
  ASSERT_EQ(C_ORM_ERROR_SQL, cdd_c_inspect_schema_db2(&mock_db, "T", &arr));
  g_as_fail_getters = 6;
  g_as_max_rows = 3;
  ASSERT_EQ(C_ORM_OK, cdd_c_inspect_schema_db2(&mock_db, "T", &arr));
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_array_free(&arr));
  g_as_max_rows = 2;
  g_as_fail_getters = 0;

  g_as_fail_step_on_2 = 1;
  ASSERT_EQ(C_ORM_ERROR_STEP, cdd_c_inspect_schema_db2(&mock_db, "T", &arr));
  g_as_fail_finalize = 1;
  ASSERT_EQ(C_ORM_ERROR_SQL, cdd_c_inspect_schema_db2(&mock_db, "T", &arr));
  g_as_fail_finalize = 0;
  g_as_fail_step_on_2 = 0;

  /* OOM sweep on cdd_c_inspect_schema_db2 with 10 rows (+ with finalize
   * failing) */
  g_as_max_rows = 10;
  c_orm_set_allocators(mock_malloc_astruct, mock_realloc_astruct, c_orm_free);
  for (step = 0;; step++) {
    memset(&arr, 0, sizeof(arr));
    astruct_oom_countdown = step;
    if (cdd_c_inspect_schema_db2(&mock_db, "T", &arr) == C_ORM_OK) {
      astruct_oom_countdown = -1;
      cdd_c_abstract_struct_array_free(&arr);
      break;
    }
    astruct_oom_countdown = -1;
    cdd_c_abstract_struct_array_free(&arr);
    ASSERT(step < 150);
  }
  g_as_fail_finalize = 1;
  for (step = 0; step < 120; step++) {
    memset(&arr, 0, sizeof(arr));
    astruct_oom_countdown = step;
    cdd_c_inspect_schema_db2(&mock_db, "T", &arr);
    astruct_oom_countdown = -1;
    cdd_c_abstract_struct_array_free(&arr);
  }
  g_as_fail_finalize = 0;
  g_as_max_rows = 2;
  c_orm_set_allocators(old_malloc, old_realloc, c_orm_free);

  ASSERT_EQ(C_ORM_OK, odb->vtable->disconnect(odb));
  PASS();
}

/**
 * @brief Abstract struct test suite runner.
 * @param abstract_struct_suite Suite runner function name.
 */
SUITE(abstract_struct_suite) {
  static int recursed = 0;
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
  RUN_TEST(test_abstract_decimal_odbc_db2_coverage);
  if (!recursed) {
    recursed = 1;
    greatest_set_test_filter("never_match_filter");
    abstract_struct_suite();
    greatest_set_test_filter(NULL);
    recursed = 0;
  }
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
