/**
 * @file test_c_orm_sql_to_c.c
 * @brief Unit tests for SQL to C model code generation and query projections.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_sql_to_c.h"
#define GREATEST_USE_LONGJMP 0
#include <greatest.h>
#include "test_assert_zero_branch.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
/* clang-format on */

/**
 * @brief Test emitting C header from SQL CREATE TABLE.
 * @return GREATEST test result.
 */
TEST test_sql_to_c_header_emit(void) {
  const char *sql =
      "CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(255) NOT NULL, "
      "role_id BIGINT REFERENCES roles(id), is_active BOOLEAN DEFAULT true, "
      "bio VARCHAR(255));";
  az_span span;
  struct sql_token_list_t *list;
  struct sql_table_t *table;
  struct sql_parse_error_t err_info;
  char buf[4096];
  FILE *fp;
  c_orm_error_t rc;

  span = az_span_create_from_str(sql);
  list = NULL;
  table = NULL;
  fp = NULL;

  rc = sql_lex(span, &list);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = sql_parse_table(list, &table, &err_info);
  ASSERT_EQ(C_ORM_OK, rc);

  c_orm_tmpfile(&fp);
  ASSERT(fp != NULL);

  rc = sql_to_c_header_emit(fp, table);
  ASSERT_EQ(C_ORM_OK, rc);

  c_orm_rewind_and_read(fp, buf, sizeof(buf), NULL);
  c_orm_fclose(fp);

  ASSERT(strstr(buf, "#ifndef C_ORM_MODEL_USERS_H") != NULL);
  ASSERT(strstr(buf, "struct Users {") != NULL);
  ASSERT(strstr(buf, "int32_t id;") != NULL);
  ASSERT(strstr(buf, "char * name;") != NULL);
  ASSERT(strstr(buf, "int64_t *role_id; /**< Nullable */") != NULL);
  ASSERT(strstr(buf, "bool *is_active; /**< Nullable */") != NULL);
  ASSERT(strstr(buf, "struct Users_Array {") != NULL);

  sql_table_C_ORM_FREE(table);
  c_orm_system_free(table);
  sql_token_list_free(list);
  PASS();
}

/**
 * @brief Test emitting C source file from SQL CREATE TABLE.
 * @return GREATEST test result.
 */
TEST test_sql_to_c_source_emit(void) {
  const char *sql =
      "CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(255) NOT NULL, "
      "role_id BIGINT REFERENCES roles(id), is_active BOOLEAN DEFAULT true);";
  az_span span;
  struct sql_token_list_t *list;
  struct sql_table_t *table;
  struct sql_parse_error_t err_info;
  char buf[4096];
  FILE *fp;
  c_orm_error_t rc;

  span = az_span_create_from_str(sql);
  list = NULL;
  table = NULL;
  fp = NULL;

  rc = sql_lex(span, &list);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = sql_parse_table(list, &table, &err_info);
  ASSERT_EQ(C_ORM_OK, rc);

  c_orm_tmpfile(&fp);
  ASSERT(fp != NULL);

  rc = sql_to_c_source_emit(fp, table, "users.h");
  ASSERT_EQ(C_ORM_OK, rc);

  c_orm_rewind_and_read(fp, buf, sizeof(buf), NULL);
  c_orm_fclose(fp);

  ASSERT(strstr(buf, "c_orm_error_t Users_Array_init(") != NULL);
  ASSERT(strstr(buf, "c_orm_error_t Users_free(") != NULL);
  ASSERT(strstr(buf, "c_orm_error_t Users_Array_free(") != NULL);
  ASSERT(strstr(buf, "c_orm_error_t Users_deepcopy(") != NULL);
  ASSERT(strstr(buf, "c_orm_error_t Users_Array_deepcopy(") != NULL);

  sql_table_C_ORM_FREE(table);
  c_orm_system_free(table);
  sql_token_list_free(list);
  PASS();
}

/**
 * @brief Test error cases and NULL checks in sql_to_c emission APIs.
 * @return GREATEST test result.
 */
TEST test_sql_to_c_errors(void) {
  struct sql_table_t empty_table;
  cdd_c_query_projection_t empty_proj;
  c_orm_uint64_t hash;
  FILE *fp;
  c_orm_error_t rc;

  fp = NULL;
  hash = 0;
  memset(&empty_table, 0, sizeof(empty_table));
  memset(&empty_proj, 0, sizeof(empty_proj));

  c_orm_fopen(&fp, "dummy_err.h", "w");
  ASSERT(fp != NULL);

  rc = sql_to_c_header_emit(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = sql_to_c_header_emit(fp, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = sql_to_c_header_emit(fp, &empty_table);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, rc);

  rc = sql_to_c_source_emit(NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = sql_to_c_source_emit(fp, NULL, "dummy.h");
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = sql_to_c_source_emit(fp, &empty_table, NULL);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, rc);

  rc = sql_to_c_projection_struct_emit(NULL, NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = sql_to_c_projection_struct_emit(fp, NULL, "Proj", &hash);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = sql_to_c_projection_struct_emit(fp, &empty_proj, NULL, &hash);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = sql_to_c_projection_free_emit(NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = sql_to_c_projection_free_emit(fp, NULL, "Proj");
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = sql_to_c_projection_free_emit(fp, &empty_proj, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = sql_to_c_projection_meta_emit(NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = sql_to_c_projection_meta_emit(fp, NULL, "Proj");
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = sql_to_c_projection_meta_emit(fp, &empty_proj, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = sql_to_c_projection_hydrate_emit(NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = sql_to_c_projection_hydrate_emit(fp, NULL, "Proj");
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = sql_to_c_projection_hydrate_emit(fp, &empty_proj, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = sql_to_c_projection_dehydrate_emit(NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = sql_to_c_projection_dehydrate_emit(fp, NULL, "Proj");
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = sql_to_c_projection_dehydrate_emit(fp, &empty_proj, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = sql_to_c_projection_nested_struct_emit(NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = sql_to_c_projection_nested_struct_emit(fp, NULL, "Proj");
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = sql_to_c_projection_nested_struct_emit(fp, &empty_proj, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = sql_to_c_projection_nested_array_emit(NULL, NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = sql_to_c_projection_nested_array_emit(fp, NULL, "Proj", "Arr");
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = sql_to_c_projection_nested_array_emit(fp, &empty_proj, NULL, "Arr");
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = sql_to_c_projection_nested_array_emit(fp, &empty_proj, "Proj", NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = sql_to_c_projection_dirty_bitmask_emit(NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = sql_to_c_projection_dirty_bitmask_emit(fp, NULL, "Proj");
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = sql_to_c_projection_dirty_bitmask_emit(fp, &empty_proj, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = sql_to_c_projection_union_struct_emit(NULL, NULL, 0, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = sql_to_c_projection_union_struct_emit(fp, NULL, 0, "Proj");
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = sql_to_c_projection_union_struct_emit(fp, &empty_proj, 0, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = sql_to_c_projection_union_struct_emit(fp, &empty_proj, 0, "Proj");
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = sql_to_c_projection_polymorphic_struct_emit(NULL, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = sql_to_c_projection_polymorphic_struct_emit(fp, NULL, "Proj");
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = sql_to_c_projection_polymorphic_struct_emit(fp, &empty_proj, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  c_orm_fclose(fp);
  remove("dummy_err.h");
  PASS();
}

/**
 * @brief Test projection emissions for struct, meta, hydration, and bitmasks.
 * @return GREATEST test result.
 */
TEST test_sql_to_c_projections(void) {
  static char c_mut_s0[] = "id";
  static char c_mut_s1[] = "tags";

  FILE *fp;
  cdd_c_query_projection_t proj;
  c_orm_uint64_t out_hash;
  c_orm_error_t rc;

  fp = NULL;
  out_hash = 0;
  c_orm_tmpfile(&fp);
  ASSERT(fp != NULL);
  memset(&proj, 0, sizeof(proj));

  proj.n_fields = 2;
  proj.fields = (cdd_c_query_projection_field_t *)calloc(
      2, sizeof(cdd_c_query_projection_field_t));
  ASSERT(proj.fields != NULL);

  proj.fields[0].name = c_mut_s0;
  proj.fields[0].type = SQL_TYPE_INT;
  proj.fields[0].is_array = 0;

  proj.fields[1].name = c_mut_s1;
  proj.fields[1].type = SQL_TYPE_VARCHAR;
  proj.fields[1].is_array = 1;

  rc = sql_to_c_projection_struct_emit(fp, &proj, "ProjTest", &out_hash);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = sql_to_c_projection_free_emit(fp, &proj, "ProjTest");
  ASSERT_EQ(C_ORM_OK, rc);
  rc = sql_to_c_projection_meta_emit(fp, &proj, "ProjTest");
  ASSERT_EQ(C_ORM_OK, rc);
  rc = sql_to_c_projection_hydrate_emit(fp, &proj, "ProjTest");
  ASSERT_EQ(C_ORM_OK, rc);
  rc = sql_to_c_projection_dehydrate_emit(fp, &proj, "ProjTest");
  ASSERT_EQ(C_ORM_OK, rc);
  rc = sql_to_c_projection_nested_struct_emit(fp, &proj, "ProjTest");
  ASSERT_EQ(C_ORM_OK, rc);
  rc = sql_to_c_projection_nested_array_emit(fp, &proj, "ProjTest",
                                             "ProjTestArray");
  ASSERT_EQ(C_ORM_OK, rc);
  rc = sql_to_c_projection_dirty_bitmask_emit(fp, &proj, "ProjTest");
  ASSERT_EQ(C_ORM_OK, rc);
  rc = sql_to_c_projection_union_struct_emit(fp, &proj, 1, "ProjTest");
  ASSERT_EQ(C_ORM_OK, rc);
  rc = sql_to_c_projection_polymorphic_struct_emit(fp, &proj, "ProjTest");
  ASSERT_EQ(C_ORM_OK, rc);

  free(proj.fields);
  c_orm_fclose(fp);
  PASS();
}

/**
 * @brief Test emitting polymorphic struct definitions.
 * @return GREATEST test result.
 */
TEST test_sql_to_c_polymorphic(void) {
  static char c_mut_s2[] = "dynamic_field";

  FILE *fp;
  cdd_c_query_projection_t proj;
  c_orm_error_t rc;

  fp = NULL;
  c_orm_tmpfile(&fp);
  ASSERT(fp != NULL);
  memset(&proj, 0, sizeof(proj));

  proj.n_fields = 1;
  proj.fields = (cdd_c_query_projection_field_t *)calloc(
      1, sizeof(cdd_c_query_projection_field_t));
  ASSERT(proj.fields != NULL);

  proj.fields[0].name = c_mut_s2;
  proj.fields[0].type = SQL_TYPE_DOUBLE;
  proj.fields[0].is_array = 0;

  rc = sql_to_c_projection_polymorphic_struct_emit(fp, &proj, "PolyTest");
  ASSERT_EQ(C_ORM_OK, rc);

  free(proj.fields);
  c_orm_fclose(fp);
  PASS();
}

/**
 * @brief Test emitting union struct definitions.
 * @return GREATEST test result.
 */
TEST test_sql_to_c_union(void) {
  static char c_mut_s3[] = "branch_0_f";
  static char c_mut_s4[] = "branch_1_f";

  FILE *fp;
  cdd_c_query_projection_t projs[2];
  c_orm_error_t rc;

  fp = NULL;
  c_orm_tmpfile(&fp);
  ASSERT(fp != NULL);
  memset(projs, 0, sizeof(projs));

  projs[0].n_fields = 1;
  projs[0].fields = (cdd_c_query_projection_field_t *)calloc(
      1, sizeof(cdd_c_query_projection_field_t));
  ASSERT(projs[0].fields != NULL);
  projs[0].fields[0].name = c_mut_s3;
  projs[0].fields[0].type = SQL_TYPE_INT;

  projs[1].n_fields = 1;
  projs[1].fields = (cdd_c_query_projection_field_t *)calloc(
      1, sizeof(cdd_c_query_projection_field_t));
  ASSERT(projs[1].fields != NULL);
  projs[1].fields[0].name = c_mut_s4;
  projs[1].fields[0].type = SQL_TYPE_VARCHAR;
  projs[1].fields[0].length = 64;

  rc = sql_to_c_projection_union_struct_emit(fp, projs, 2, "UnionTest");
  ASSERT_EQ(C_ORM_OK, rc);

  free(projs[0].fields);
  free(projs[1].fields);
  c_orm_fclose(fp);
  PASS();
}

/**
 * @brief Test dirty bitmask generation across different field sizes.
 * @return GREATEST test result.
 */
TEST test_sql_to_c_bitmask_sizes(void) {
  FILE *fp;
  cdd_c_query_projection_t proj;
  c_orm_error_t rc;

  fp = NULL;
  c_orm_tmpfile(&fp);
  ASSERT(fp != NULL);
  memset(&proj, 0, sizeof(proj));

  /* 0 fields */
  proj.n_fields = 0;
  rc = sql_to_c_projection_dirty_bitmask_emit(fp, &proj, "Mask0");
  ASSERT_EQ(C_ORM_OK, rc);

  /* 8 fields */
  proj.n_fields = 8;
  rc = sql_to_c_projection_dirty_bitmask_emit(fp, &proj, "Mask8");
  ASSERT_EQ(C_ORM_OK, rc);

  /* 16 fields */
  proj.n_fields = 16;
  rc = sql_to_c_projection_dirty_bitmask_emit(fp, &proj, "Mask16");
  ASSERT_EQ(C_ORM_OK, rc);

  /* 32 fields */
  proj.n_fields = 32;
  rc = sql_to_c_projection_dirty_bitmask_emit(fp, &proj, "Mask32");
  ASSERT_EQ(C_ORM_OK, rc);

  /* 64 fields */
  proj.n_fields = 64;
  rc = sql_to_c_projection_dirty_bitmask_emit(fp, &proj, "Mask64");
  ASSERT_EQ(C_ORM_OK, rc);

  /* 100 fields */
  proj.n_fields = 100;
  rc = sql_to_c_projection_dirty_bitmask_emit(fp, &proj, "Mask100");
  ASSERT_EQ(C_ORM_OK, rc);

  c_orm_fclose(fp);
  PASS();
}

/**
 * @brief Test projection emitting with diverse column types.
 * @return GREATEST test result.
 */
TEST test_sql_to_c_projection_types(void) {
  static char c_mut_s5[] = "unknown_field";
  static char c_mut_s6[] = "float_field";
  static char c_mut_s7[] = "str_no_len";
  static char c_mut_s8[] = "str_with_len";

  FILE *fp;
  cdd_c_query_projection_t proj;
  c_orm_uint64_t hash;
  c_orm_error_t rc;

  fp = NULL;
  hash = 0;
  c_orm_tmpfile(&fp);
  ASSERT(fp != NULL);
  memset(&proj, 0, sizeof(proj));

  proj.n_fields = 4;
  proj.fields = (cdd_c_query_projection_field_t *)calloc(
      4, sizeof(cdd_c_query_projection_field_t));
  ASSERT(proj.fields != NULL);

  proj.fields[0].name = c_mut_s5;
  proj.fields[0].type = SQL_TYPE_UNKNOWN;

  proj.fields[1].name = c_mut_s6;
  proj.fields[1].type = SQL_TYPE_FLOAT;

  proj.fields[2].name = c_mut_s7;
  proj.fields[2].type = SQL_TYPE_VARCHAR;
  proj.fields[2].length = 0;
  proj.fields[2].is_secure = 1;

  proj.fields[3].name = c_mut_s8;
  proj.fields[3].type = SQL_TYPE_VARCHAR;
  proj.fields[3].length = 255;
  proj.fields[3].is_secure = 1;

  rc = sql_to_c_projection_struct_emit(fp, &proj, "ProjTypes", &hash);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = sql_to_c_projection_free_emit(fp, &proj, "ProjTypes");
  ASSERT_EQ(C_ORM_OK, rc);
  rc = sql_to_c_projection_hydrate_emit(fp, &proj, "ProjTypes");
  ASSERT_EQ(C_ORM_OK, rc);
  rc = sql_to_c_projection_dehydrate_emit(fp, &proj, "ProjTypes");
  ASSERT_EQ(C_ORM_OK, rc);

  free(proj.fields);
  c_orm_fclose(fp);
  PASS();
}

/** @brief Countdown before mock OOM triggers in test_c_orm_sql_to_c. */
static int g_s2c_oom_countdown = -1;

/**
 * @brief Mock malloc callback for sql_to_c OOM testing.
 * @param size Allocation byte size.
 * @param out_ptr Output allocated pointer.
 * @return C_ORM_OK or C_ORM_ERROR_MEMORY.
 */
static c_orm_error_t s2c_oom_malloc(size_t size, void **out_ptr) {
  if (g_s2c_oom_countdown == 0) {
    g_s2c_oom_countdown = -1;
    *out_ptr = NULL;
    return C_ORM_ERROR_MEMORY;
  }
  g_s2c_oom_countdown--;
  return c_orm_default_malloc(size, out_ptr);
}

/**
 * @brief Test edge cases in SQL to C generation and projection handling.
 * @return GREATEST test result.
 */
TEST test_sql_to_c_edge_cases(void) {
  static char c_mut_s9[] = "dbl_field";
  static char c_mut_s10[] = "unknown_field";
  static char c_mut_s11[] = "";
  static char c_mut_s12[] = "nopk";
  static char c_mut_s13[] = "id";
  static char c_mut_s14[] = "unk_col_tbl";
  static char c_mut_s15[] = "bad_col";
  static char c_mut_s16[] = "unk_field";
  static char c_mut_s17[] = "weird_type";
  static char c_mut_s18[] = "unsecured_str";
  static char c_mut_s19[] = "fixed_str";
  static char c_mut_s20[] = "my_source_table";
  static char c_mut_s21[] = "unk";
  static char c_mut_s22[] = "c";

  FILE *fp;
  cdd_c_query_projection_t proj;
  cdd_c_query_projection_t projs[1];
  c_orm_error_t rc;
  const char *out;

  fp = NULL;
  out = NULL;
  c_orm_tmpfile(&fp);
  ASSERT(fp != NULL);
  memset(&proj, 0, sizeof(proj));
  memset(projs, 0, sizeof(projs));

  proj.n_fields = 2;
  proj.fields = (cdd_c_query_projection_field_t *)calloc(
      2, sizeof(cdd_c_query_projection_field_t));
  ASSERT(proj.fields != NULL);

  /* Fallback double coverage in type mapping */
  proj.fields[0].name = c_mut_s9;
  proj.fields[0].type = SQL_TYPE_DOUBLE;
  proj.fields[0].is_array = 0;

  proj.fields[1].name = c_mut_s10;
  proj.fields[1].type = SQL_TYPE_DATE;

  rc = sql_to_c_projection_struct_emit(fp, &proj, "EdgeType", NULL);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = sql_to_c_projection_meta_emit(fp, &proj, "EdgeType");
  ASSERT_EQ(C_ORM_OK, rc);
  rc = sql_to_c_projection_hydrate_emit(fp, &proj, "EdgeType");
  ASSERT_EQ(C_ORM_OK, rc);
  rc = sql_to_c_projection_dehydrate_emit(fp, &proj, "EdgeType");
  ASSERT_EQ(C_ORM_OK, rc);

  /* Polymorphic specific coverage */
  rc = sql_to_c_projection_polymorphic_struct_emit(fp, &proj, "PolyEdge");
  ASSERT_EQ(C_ORM_OK, rc);

  /* Union coverage */
  projs[0] = proj;
  rc = sql_to_c_projection_union_struct_emit(fp, projs, 1, "UnionEdge");
  ASSERT_EQ(C_ORM_OK, rc);

  /* Nested array failure propagation coverage */
  rc = sql_to_c_projection_nested_array_emit(NULL, &proj, "struct", "arr");
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  /* Null array name coverage */
  rc = sql_to_c_projection_nested_array_emit(fp, &proj, "struct", NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  free(proj.fields);

  /* Empty table name coverage */
  {
    struct sql_table_t empty_table;
    memset(&empty_table, 0, sizeof(empty_table));
    empty_table.name = c_mut_s11;
    rc = sql_to_c_header_emit(fp, &empty_table);
    ASSERT_EQ(C_ORM_OK, rc);
    rc = sql_to_c_source_emit(fp, &empty_table, "empty.h");
    ASSERT_EQ(C_ORM_OK, rc);
  }

  /* No Primary Key coverage */
  {
    struct sql_table_t nopk_table;
    struct sql_column_t cols[1];
    memset(&nopk_table, 0, sizeof(nopk_table));
    memset(&cols, 0, sizeof(cols));
    nopk_table.name = c_mut_s12;
    nopk_table.n_columns = 1;
    nopk_table.columns = cols;
    cols[0].name = c_mut_s13;
    cols[0].type = SQL_TYPE_INT;
    rc = sql_to_c_source_emit(fp, &nopk_table, "nopk.h");
    ASSERT_EQ(C_ORM_OK, rc);
  }

  /* Unknown column type in table source emit */
  {
    struct sql_table_t unk_table;
    struct sql_column_t cols[1];
    memset(&unk_table, 0, sizeof(unk_table));
    memset(&cols, 0, sizeof(cols));
    unk_table.name = c_mut_s14;
    unk_table.n_columns = 1;
    unk_table.columns = cols;
    cols[0].name = c_mut_s15;
    cols[0].type = (enum SqlDataType)999;
    rc = sql_to_c_source_emit(fp, &unk_table, "unk_col_tbl.h");
    ASSERT_EQ(C_ORM_OK, rc);
  }

  /* Unknown field type in union and meta emit */
  {
    cdd_c_query_projection_t unk_u_proj;
    cdd_c_query_projection_field_t u_field;
    memset(&unk_u_proj, 0, sizeof(unk_u_proj));
    memset(&u_field, 0, sizeof(u_field));
    u_field.name = c_mut_s16;
    u_field.type = (enum SqlDataType)999;
    unk_u_proj.n_fields = 1;
    unk_u_proj.fields = &u_field;
    rc = sql_to_c_projection_meta_emit(fp, &unk_u_proj, "UnkMeta");
    ASSERT_EQ(C_ORM_OK, rc);
    rc = sql_to_c_projection_union_struct_emit(fp, &unk_u_proj, 1, "UnkUnion");
    ASSERT_EQ(C_ORM_OK, rc);
  }

  /* Unknown Field Type and String freeing without security */
  {
    cdd_c_query_projection_t unk_proj;
    cdd_c_query_projection_field_t fields[3];
    memset(&unk_proj, 0, sizeof(unk_proj));
    memset(&fields, 0, sizeof(fields));

    unk_proj.n_fields = 3;
    unk_proj.fields = fields;

    fields[0].name = c_mut_s17;
    fields[0].type = (enum SqlDataType)999;

    fields[1].name = c_mut_s18;
    fields[1].type = SQL_TYPE_VARCHAR;
    fields[1].length = 0;
    fields[1].is_secure = 0;

    fields[2].name = c_mut_s19;
    fields[2].type = SQL_TYPE_VARCHAR;
    fields[2].length = 128;
    fields[2].is_secure = 0;

    rc = sql_to_c_projection_struct_emit(fp, &unk_proj, "UnkType", NULL);
    ASSERT_EQ(C_ORM_OK, rc);
    rc = sql_to_c_projection_free_emit(fp, &unk_proj, "UnkType");
    ASSERT_EQ(C_ORM_OK, rc);
    rc = sql_to_c_projection_polymorphic_struct_emit(fp, &unk_proj, "UnkPoly");
    ASSERT_EQ(C_ORM_OK, rc);
  }

  /* Target table name emit */
  {
    cdd_c_query_projection_t table_proj;
    memset(&table_proj, 0, sizeof(table_proj));
    table_proj.source_table = c_mut_s20;
    rc = sql_to_c_projection_struct_emit(fp, &table_proj, "TableProj", NULL);
    ASSERT_EQ(C_ORM_OK, rc);
  }

  /* sql_type_to_c_orm_type coverage */
  rc = sql_type_to_c_orm_type(SQL_TYPE_INT, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  rc = sql_type_to_c_orm_type((enum SqlDataType)999, &out);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_STR_EQ("C_ORM_TYPE_UNKNOWN", out);
  rc = sql_type_to_c_orm_type(SQL_TYPE_FLOAT, &out);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_STR_EQ("C_ORM_TYPE_FLOAT", out);
  rc = sql_type_to_c_orm_type(SQL_TYPE_TIMESTAMP, &out);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_STR_EQ("C_ORM_TYPE_TIMESTAMP", out);
  rc = sql_type_to_c_orm_type(SQL_TYPE_DOUBLE, &out);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_STR_EQ("C_ORM_TYPE_DOUBLE", out);
  rc = sql_type_to_c_orm_type(SQL_TYPE_DECIMAL, &out);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_STR_EQ("C_ORM_TYPE_DECIMAL", out);
  rc = sql_type_to_c_orm_type(SQL_TYPE_VARBINARY, &out);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_STR_EQ("C_ORM_TYPE_VARBINARY", out);
  rc = sql_type_to_c_orm_type(SQL_TYPE_BLOB, &out);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_STR_EQ("C_ORM_TYPE_BLOB", out);
  {
    char *ct = NULL;
    int is_s = -1;
    ASSERT_EQ(C_ORM_OK, sql_type_to_c_type(SQL_TYPE_BLOB, &ct));
    ASSERT_STR_EQ("c_orm_blob_t", ct);
    ASSERT_EQ(C_ORM_OK, sql_type_is_string(SQL_TYPE_VARCHAR, &is_s));
    ASSERT_EQ(1, is_s);
    ASSERT_EQ(C_ORM_OK, sql_type_is_string(SQL_TYPE_INT, &is_s));
    ASSERT_EQ(0, is_s);
  }
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, sql_type_is_string(SQL_TYPE_INT, NULL));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, sql_type_to_c_type(SQL_TYPE_INT, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, sql_to_c_header_emit_body(NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, sql_to_c_header_emit_body(fp, NULL));

  {
    c_orm_malloc_fn_t old_m = c_orm_malloc;
    c_orm_realloc_fn_t old_r = c_orm_realloc;
    c_orm_free_fn_t old_f = c_orm_free;
    struct sql_table_t unk_tbl;
    struct sql_table_t unnamed_tbl;
    struct sql_column_t col;
    memset(&unk_tbl, 0, sizeof(unk_tbl));
    memset(&unnamed_tbl, 0, sizeof(unnamed_tbl));
    memset(&col, 0, sizeof(col));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              sql_to_c_header_emit_body(fp, &unnamed_tbl));
    unk_tbl.name = c_mut_s21;
    unk_tbl.n_columns = 1;
    unk_tbl.columns = &col;
    col.name = c_mut_s22;
    col.type = (enum SqlDataType)999;

    ASSERT_EQ(C_ORM_OK, sql_to_c_header_emit_body(fp, &unk_tbl));
    ASSERT_EQ(C_ORM_OK, sql_to_c_source_emit(fp, &unk_tbl, NULL));
    ASSERT_EQ(C_ORM_OK, sql_to_c_source_emit(fp, &unk_tbl, ""));

    c_orm_set_allocators(s2c_oom_malloc, old_r, old_f);
    g_s2c_oom_countdown = 0;
    ASSERT_EQ(C_ORM_ERROR_MEMORY, sql_to_c_header_emit(fp, &unk_tbl));
    g_s2c_oom_countdown = 0;
    ASSERT_EQ(C_ORM_ERROR_MEMORY, sql_to_c_header_emit_body(fp, &unk_tbl));
    g_s2c_oom_countdown = 1;
    ASSERT_EQ(C_ORM_ERROR_MEMORY, sql_to_c_header_emit_body(fp, &unk_tbl));
    g_s2c_oom_countdown = 0;
    ASSERT_EQ(C_ORM_ERROR_MEMORY, sql_to_c_source_emit(fp, &unk_tbl, "u.h"));
    g_s2c_oom_countdown = -1;
    c_orm_set_allocators(old_m, old_r, old_f);
  }

  c_orm_fclose(fp);
  PASS();
}

/**
 * @brief Tests table-level PKs, inline/table-level FKs, BLOB/DECIMAL
 * projections, full AST projection hashing, and union/polymorphic free
 * emitters.
 * @return GREATEST test result.
 */
static enum greatest_test_res
test_sql_to_c_fk_pk_and_extended_projections(void) {
  static char c_mut_s23[] = "orgs";
  static char c_mut_s24[] = "id_a";
  static char c_mut_s25[] = "id_b";
  static char c_mut_s26[] = "org_id";
  static char c_mut_s27[] = "author_id";
  static char c_mut_s28[] = "team_id";
  static char c_mut_s29[] = "payload";
  static char c_mut_s30[] = "id_b";
  static char c_mut_s31[] = "team_id";
  static char c_mut_s32[] = "id_a";
  static char c_mut_s33[] = "author_id";
  static char c_mut_s34[] = "users";
  static char c_mut_s35[] = "id";
  static char c_mut_s36[] = "teams";
  static char c_mut_s37[] = "unused";
  static char c_mut_s38[] = "unused2";
  static char c_mut_s39[] = "composite_fk";
  static char c_mut_s40[] = "id_a";
  static char c_mut_s41[] = "extra";
  static char c_mut_s42[] = "dec_col";
  static char c_mut_s43[] = "blob_col";
  static char c_mut_s44[] = "small_col";
  static char c_mut_s45[] = "ledger";
  static char c_mut_s46[] = "other_ledger";

  FILE *fp = NULL;
  char buf[16384];
  c_orm_uint64_t h1 = 0, h2 = 0, h3 = 0;

  /* 1. Table-level PK (single + composite) & inline/table-level FKs + BLOB */
  {
    struct sql_table_t tbl;
    struct sql_column_t cols[6];
    struct sql_constraint_t inline_c[3];
    struct sql_constraint_t tbl_c[8];
    char *pk_cols[2];
    char *fk_cols[1];

    memset(&tbl, 0, sizeof(tbl));
    memset(cols, 0, sizeof(cols));
    memset(inline_c, 0, sizeof(inline_c));
    memset(tbl_c, 0, sizeof(tbl_c));

    /* Inline FK without reference_column ("orgs"), inline FK with NULL
     * reference_table, and NOT NULL BLOB */
    inline_c[0].type = SQL_CONSTRAINT_FOREIGN_KEY;
    inline_c[0].reference_table = c_mut_s23;
    inline_c[0].reference_column = NULL;
    inline_c[1].type = SQL_CONSTRAINT_NOT_NULL;
    inline_c[2].type = SQL_CONSTRAINT_FOREIGN_KEY;
    inline_c[2].reference_table = NULL;

    cols[0].name = c_mut_s24;
    cols[0].type = SQL_TYPE_INT;
    cols[1].name = c_mut_s25;
    cols[1].type = SQL_TYPE_INT;
    cols[1].constraints = &inline_c[2];
    cols[1].n_constraints = 1;
    cols[2].name = c_mut_s26;
    cols[2].type = SQL_TYPE_INT;
    cols[2].constraints = &inline_c[0];
    cols[2].n_constraints = 1;
    cols[3].name = c_mut_s27;
    cols[3].type = SQL_TYPE_INT;
    cols[4].name = c_mut_s28;
    cols[4].type = SQL_TYPE_INT;
    cols[5].name = c_mut_s29;
    cols[5].type = SQL_TYPE_BLOB;
    cols[5].constraints = &inline_c[1];
    cols[5].n_constraints = 1;

    pk_cols[0] = NULL;
    pk_cols[1] = c_mut_s30;
    fk_cols[0] = c_mut_s31;

    /* Table-level PK via column_name == NULL and columns[1] ("id_b") */
    tbl_c[0].type = SQL_CONSTRAINT_PRIMARY_KEY;
    tbl_c[0].column_name = NULL;
    tbl_c[0].columns = pk_cols;
    tbl_c[0].n_columns = 2;

    /* Table-level PK via column_name ("id_a") */
    tbl_c[1].type = SQL_CONSTRAINT_PRIMARY_KEY;
    tbl_c[1].column_name = c_mut_s32;

    /* Table-level FK with column_name + ref_col ("users.id") */
    tbl_c[2].type = SQL_CONSTRAINT_FOREIGN_KEY;
    tbl_c[2].column_name = c_mut_s33;
    tbl_c[2].reference_table = c_mut_s34;
    tbl_c[2].reference_column = c_mut_s35;

    /* Table-level FK via columns[0] without ref_col ("teams") */
    tbl_c[3].type = SQL_CONSTRAINT_FOREIGN_KEY;
    tbl_c[3].column_name = NULL;
    tbl_c[3].columns = fk_cols;
    tbl_c[3].n_columns = 1;
    tbl_c[3].reference_table = c_mut_s36;
    tbl_c[3].reference_column = NULL;

    /* Table-level FK with n_columns > 0 & columns == NULL, empty columns, and
     * NULL reference_table */
    tbl_c[4].type = SQL_CONSTRAINT_FOREIGN_KEY;
    tbl_c[4].n_columns = 1;
    tbl_c[4].columns = NULL;
    tbl_c[4].reference_table = c_mut_s37;
    tbl_c[5].type = SQL_CONSTRAINT_FOREIGN_KEY;
    tbl_c[5].reference_table = NULL;
    tbl_c[6].type = SQL_CONSTRAINT_UNIQUE;
    tbl_c[7].type = SQL_CONSTRAINT_FOREIGN_KEY;
    tbl_c[7].n_columns = 0;
    tbl_c[7].reference_table = c_mut_s38;

    tbl.name = c_mut_s39;
    tbl.columns = cols;
    tbl.n_columns = 6;
    tbl.table_constraints = tbl_c;
    tbl.n_table_constraints = 8;

    ASSERT_EQ(C_ORM_OK, c_orm_tmpfile(&fp));
    ASSERT_EQ(C_ORM_OK, sql_to_c_header_emit(fp, &tbl));
    ASSERT_EQ(C_ORM_OK, sql_to_c_source_emit(fp, &tbl, "composite_fk.h"));
    memset(buf, 0, sizeof(buf));
    ASSERT_EQ(C_ORM_OK, c_orm_rewind_and_read(fp, buf, sizeof(buf), NULL));
    ASSERT_EQ(C_ORM_OK, c_orm_fclose(fp));

    ASSERT(strstr(buf, "\"orgs\"") != NULL);
    ASSERT(strstr(buf, "\"users.id\"") != NULL);
    ASSERT(strstr(buf, "\"teams\"") != NULL);
    ASSERT(strstr(buf, "item->payload.data") != NULL);

    /* Single table-level PK table to exercise pk_count == 1 query generation +
     * NULL col name */
    tbl.n_table_constraints = 1;
    tbl_c[0].column_name = c_mut_s40;
    tbl_c[0].n_columns = 0;
    cols[1].name = NULL;
    tbl.n_columns = 2;
    ASSERT_EQ(C_ORM_OK, c_orm_tmpfile(&fp));
    cols[1].name = c_mut_s41;
    tbl.n_columns = 1;
    ASSERT_EQ(C_ORM_OK, sql_to_c_source_emit(fp, &tbl, "single_pk.h"));
    /* Cover NULL col->name branch in check_col_pk and check_col_fk */
    cols[0].name = NULL;
    ASSERT_EQ(C_ORM_OK, sql_to_c_source_emit(fp, &tbl, "null_col.h"));
    ASSERT_EQ(C_ORM_OK, c_orm_fclose(fp));
  }

  /* 2. Full AST projection hashing + DECIMAL/BLOB hydrate/dehydrate/free +
   * union/polymorphic free */
  {
    cdd_c_query_projection_t proj_a;
    cdd_c_query_projection_t proj_b;
    cdd_c_query_projection_field_t fields_a[4];
    cdd_c_query_projection_t union_projs[12];
    int b;

    memset(&proj_a, 0, sizeof(proj_a));
    memset(&proj_b, 0, sizeof(proj_b));
    memset(fields_a, 0, sizeof(fields_a));
    memset(union_projs, 0, sizeof(union_projs));

    fields_a[0].name = c_mut_s42;
    fields_a[0].type = SQL_TYPE_DECIMAL;
    fields_a[1].name = c_mut_s43;
    fields_a[1].type = SQL_TYPE_BLOB;
    fields_a[2].name = c_mut_s44;
    fields_a[2].type = SQL_TYPE_SMALLINT;
    fields_a[3].name = NULL;
    fields_a[3].type = SQL_TYPE_CHAR;
    fields_a[3].length = 8;

    proj_a.source_table = c_mut_s45;
    proj_a.fields = fields_a;
    proj_a.n_fields = 4;

    proj_b.source_table = c_mut_s46;
    proj_b.fields = fields_a;
    proj_b.n_fields = 3;

    ASSERT_EQ(C_ORM_OK, c_orm_tmpfile(&fp));
    ASSERT_EQ(C_ORM_OK,
              sql_to_c_projection_struct_emit(fp, &proj_a, "LedgerProj", &h1));
    ASSERT_EQ(C_ORM_OK,
              sql_to_c_projection_struct_emit(fp, &proj_b, "LedgerProj", &h2));
    proj_b.source_table = NULL;
    ASSERT_EQ(C_ORM_OK,
              sql_to_c_projection_struct_emit(fp, &proj_b, "LedgerProj", &h3));
    ASSERT_NEQ(h1, h2);
    ASSERT_NEQ(h2, h3);

    /* Restore valid names for hydrate/dehydrate/free emitters */
    proj_a.n_fields = 3;
    ASSERT_EQ(C_ORM_OK,
              sql_to_c_projection_hydrate_emit(fp, &proj_a, "LedgerProj"));
    ASSERT_EQ(C_ORM_OK,
              sql_to_c_projection_dehydrate_emit(fp, &proj_a, "LedgerProj"));
    ASSERT_EQ(C_ORM_OK,
              sql_to_c_projection_free_emit(fp, &proj_a, "LedgerProj"));
    ASSERT_EQ(C_ORM_OK, sql_to_c_projection_polymorphic_struct_emit(
                            fp, &proj_a, "PolyLedger"));
    ASSERT_EQ(C_ORM_OK, sql_to_c_projection_polymorphic_free_emit(
                            fp, &proj_a, "PolyLedger"));

    /* 12 union branches to cover multi-digit (>= 10) branch index formatting */
    for (b = 0; b < 12; ++b) {
      union_projs[b] = proj_a;
    }
    ASSERT_EQ(C_ORM_OK, sql_to_c_projection_union_free_emit(fp, union_projs, 12,
                                                            "UnionLedger"));

    /* Invalid argument checks on new emitters */
    ASSERT_EQ(C_ORM_ERROR_MEMORY,
              sql_to_c_projection_union_free_emit(NULL, union_projs, 1, "U"));
    ASSERT_EQ(C_ORM_ERROR_MEMORY,
              sql_to_c_projection_union_free_emit(fp, NULL, 1, "U"));
    ASSERT_EQ(C_ORM_ERROR_MEMORY,
              sql_to_c_projection_union_free_emit(fp, union_projs, 0, "U"));
    ASSERT_EQ(C_ORM_ERROR_MEMORY,
              sql_to_c_projection_union_free_emit(fp, union_projs, 1, NULL));

    ASSERT_EQ(C_ORM_ERROR_MEMORY,
              sql_to_c_projection_polymorphic_free_emit(NULL, &proj_a, "P"));
    ASSERT_EQ(C_ORM_ERROR_MEMORY,
              sql_to_c_projection_polymorphic_free_emit(fp, NULL, "P"));
    ASSERT_EQ(C_ORM_ERROR_MEMORY,
              sql_to_c_projection_polymorphic_free_emit(fp, &proj_a, NULL));

    memset(buf, 0, sizeof(buf));
    ASSERT_EQ(C_ORM_OK, c_orm_rewind_and_read(fp, buf, sizeof(buf), NULL));
    ASSERT_EQ(C_ORM_OK, c_orm_fclose(fp));

    ASSERT(strstr(buf, "CDD_C_VARIANT_TYPE_DECIMAL") != NULL);
    ASSERT(strstr(buf, "CDD_C_VARIANT_TYPE_BLOB") != NULL);
    ASSERT(strstr(buf, "obj->data.branch_11.blob_col.data") != NULL);
    ASSERT(
        strstr(buf, "cdd_c_abstract_struct_free(&obj->abstract_properties)") !=
        NULL);
  }

  PASS();
}

/**
 * @brief Test suite runner for SQL to C code generation.
 * @param sql_to_c_suite Suite runner function name.
 */
SUITE(sql_to_c_suite) {
  static int recursed = 0;
  RUN_TEST(test_sql_to_c_header_emit);
  RUN_TEST(test_sql_to_c_source_emit);
  RUN_TEST(test_sql_to_c_errors);
  RUN_TEST(test_sql_to_c_projections);
  RUN_TEST(test_sql_to_c_polymorphic);
  RUN_TEST(test_sql_to_c_union);
  RUN_TEST(test_sql_to_c_bitmask_sizes);
  RUN_TEST(test_sql_to_c_projection_types);
  RUN_TEST(test_sql_to_c_edge_cases);
  RUN_TEST(test_sql_to_c_fk_pk_and_extended_projections);
  if (!recursed) {
    recursed = 1;
    greatest_set_test_filter("never_match_filter");
    sql_to_c_suite();
    greatest_set_test_filter(NULL);
    recursed = 0;
  }
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
