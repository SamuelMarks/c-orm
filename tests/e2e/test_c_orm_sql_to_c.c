#if defined(__clang__) || defined(__GNUC__)
#endif
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
#include <greatest.h>
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

  span = az_span_create_from_str((char *)sql);
  list = NULL;
  table = NULL;
  fp = NULL;

  rc = sql_lex(span, &list);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = sql_parse_table(list, &table, &err_info);
  ASSERT_EQ(C_ORM_OK, rc);

  C_ORM_TMPFILE(&fp);
  ASSERT(fp != NULL);

  rc = sql_to_c_header_emit(fp, table);
  ASSERT_EQ(C_ORM_OK, rc);

  rewind(fp);
  memset(buf, 0, sizeof(buf));
  fread(buf, 1, sizeof(buf) - 1, fp);
  fclose(fp);

  ASSERT(strstr(buf, "#ifndef C_ORM_MODEL_USERS_H") != NULL);
  ASSERT(strstr(buf, "struct Users {") != NULL);
  ASSERT(strstr(buf, "int32_t id;") != NULL);
  ASSERT(strstr(buf, "char * name;") != NULL);
  ASSERT(strstr(buf, "int64_t *role_id; /**< Nullable */") != NULL);
  ASSERT(strstr(buf, "bool *is_active; /**< Nullable */") != NULL);
  ASSERT(strstr(buf, "struct Users_Array {") != NULL);

  sql_table_C_ORM_FREE(table);
  C_ORM_FREE(table);
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

  span = az_span_create_from_str((char *)sql);
  list = NULL;
  table = NULL;
  fp = NULL;

  rc = sql_lex(span, &list);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = sql_parse_table(list, &table, &err_info);
  ASSERT_EQ(C_ORM_OK, rc);

  C_ORM_TMPFILE(&fp);
  ASSERT(fp != NULL);

  rc = sql_to_c_source_emit(fp, table, "users.h");
  ASSERT_EQ(C_ORM_OK, rc);

  rewind(fp);
  memset(buf, 0, sizeof(buf));
  fread(buf, 1, sizeof(buf) - 1, fp);
  fclose(fp);

  ASSERT(strstr(buf, "c_orm_error_t Users_Array_init(") != NULL);
  ASSERT(strstr(buf, "void Users_free(") != NULL);
  ASSERT(strstr(buf, "void Users_Array_free(") != NULL);
  ASSERT(strstr(buf, "c_orm_error_t Users_deepcopy(") != NULL);
  ASSERT(strstr(buf, "c_orm_error_t Users_Array_deepcopy(") != NULL);

  sql_table_C_ORM_FREE(table);
  C_ORM_FREE(table);
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

  C_ORM_FOPEN(&fp, "dummy_err.h", "w");
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

  fclose(fp);
  remove("dummy_err.h");
  PASS();
}

/**
 * @brief Test projection emissions for struct, meta, hydration, and bitmasks.
 * @return GREATEST test result.
 */
TEST test_sql_to_c_projections(void) {
  FILE *fp;
  cdd_c_query_projection_t proj;
  c_orm_uint64_t out_hash;
  c_orm_error_t rc;

  fp = NULL;
  out_hash = 0;
  C_ORM_TMPFILE(&fp);
  ASSERT(fp != NULL);
  memset(&proj, 0, sizeof(proj));

  proj.n_fields = 2;
  proj.fields = (cdd_c_query_projection_field_t *)calloc(
      2, sizeof(cdd_c_query_projection_field_t));
  ASSERT(proj.fields != NULL);

  proj.fields[0].name = "id";
  proj.fields[0].type = SQL_TYPE_INT;
  proj.fields[0].is_array = 0;

  proj.fields[1].name = "tags";
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
  fclose(fp);
  PASS();
}

/**
 * @brief Test emitting polymorphic struct definitions.
 * @return GREATEST test result.
 */
TEST test_sql_to_c_polymorphic(void) {
  FILE *fp;
  cdd_c_query_projection_t proj;
  c_orm_error_t rc;

  fp = NULL;
  C_ORM_TMPFILE(&fp);
  ASSERT(fp != NULL);
  memset(&proj, 0, sizeof(proj));

  proj.n_fields = 1;
  proj.fields = (cdd_c_query_projection_field_t *)calloc(
      1, sizeof(cdd_c_query_projection_field_t));
  ASSERT(proj.fields != NULL);

  proj.fields[0].name = "dynamic_field";
  proj.fields[0].type = SQL_TYPE_DOUBLE;
  proj.fields[0].is_array = 0;

  rc = sql_to_c_projection_polymorphic_struct_emit(fp, &proj, "PolyTest");
  ASSERT_EQ(C_ORM_OK, rc);

  free(proj.fields);
  fclose(fp);
  PASS();
}

/**
 * @brief Test emitting union struct definitions.
 * @return GREATEST test result.
 */
TEST test_sql_to_c_union(void) {
  FILE *fp;
  cdd_c_query_projection_t projs[2];
  c_orm_error_t rc;

  fp = NULL;
  C_ORM_TMPFILE(&fp);
  ASSERT(fp != NULL);
  memset(projs, 0, sizeof(projs));

  projs[0].n_fields = 1;
  projs[0].fields = (cdd_c_query_projection_field_t *)calloc(
      1, sizeof(cdd_c_query_projection_field_t));
  ASSERT(projs[0].fields != NULL);
  projs[0].fields[0].name = "branch_0_f";
  projs[0].fields[0].type = SQL_TYPE_INT;

  projs[1].n_fields = 1;
  projs[1].fields = (cdd_c_query_projection_field_t *)calloc(
      1, sizeof(cdd_c_query_projection_field_t));
  ASSERT(projs[1].fields != NULL);
  projs[1].fields[0].name = "branch_1_f";
  projs[1].fields[0].type = SQL_TYPE_VARCHAR;
  projs[1].fields[0].length = 64;

  rc = sql_to_c_projection_union_struct_emit(fp, projs, 2, "UnionTest");
  ASSERT_EQ(C_ORM_OK, rc);

  free(projs[0].fields);
  free(projs[1].fields);
  fclose(fp);
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
  C_ORM_TMPFILE(&fp);
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

  fclose(fp);
  PASS();
}

/**
 * @brief Test projection emitting with diverse column types.
 * @return GREATEST test result.
 */
TEST test_sql_to_c_projection_types(void) {
  FILE *fp;
  cdd_c_query_projection_t proj;
  c_orm_uint64_t hash;
  c_orm_error_t rc;

  fp = NULL;
  hash = 0;
  C_ORM_TMPFILE(&fp);
  ASSERT(fp != NULL);
  memset(&proj, 0, sizeof(proj));

  proj.n_fields = 4;
  proj.fields = (cdd_c_query_projection_field_t *)calloc(
      4, sizeof(cdd_c_query_projection_field_t));
  ASSERT(proj.fields != NULL);

  proj.fields[0].name = "unknown_field";
  proj.fields[0].type = SQL_TYPE_UNKNOWN;

  proj.fields[1].name = "float_field";
  proj.fields[1].type = SQL_TYPE_FLOAT;

  proj.fields[2].name = "str_no_len";
  proj.fields[2].type = SQL_TYPE_VARCHAR;
  proj.fields[2].length = 0;
  proj.fields[2].is_secure = 1;

  proj.fields[3].name = "str_with_len";
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
  fclose(fp);
  PASS();
}

/**
 * @brief Test edge cases in SQL to C generation and projection handling.
 * @return GREATEST test result.
 */
TEST test_sql_to_c_edge_cases(void) {
  FILE *fp;
  cdd_c_query_projection_t proj;
  cdd_c_query_projection_t projs[1];
  c_orm_error_t rc;
  const char *out;

  fp = NULL;
  out = NULL;
  C_ORM_TMPFILE(&fp);
  ASSERT(fp != NULL);
  memset(&proj, 0, sizeof(proj));
  memset(projs, 0, sizeof(projs));

  proj.n_fields = 2;
  proj.fields = (cdd_c_query_projection_field_t *)calloc(
      2, sizeof(cdd_c_query_projection_field_t));
  ASSERT(proj.fields != NULL);

  /* Fallback double coverage in type mapping */
  proj.fields[0].name = "dbl_field";
  proj.fields[0].type = SQL_TYPE_DOUBLE;
  proj.fields[0].is_array = 0;

  proj.fields[1].name = "unknown_field";
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
    empty_table.name = "";
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
    nopk_table.name = "nopk";
    nopk_table.n_columns = 1;
    nopk_table.columns = cols;
    cols[0].name = "id";
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
    unk_table.name = "unk_col_tbl";
    unk_table.n_columns = 1;
    unk_table.columns = cols;
    cols[0].name = "bad_col";
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
    u_field.name = "unk_field";
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

    fields[0].name = "weird_type";
    fields[0].type = (enum SqlDataType)999;

    fields[1].name = "unsecured_str";
    fields[1].type = SQL_TYPE_VARCHAR;
    fields[1].length = 0;
    fields[1].is_secure = 0;

    fields[2].name = "fixed_str";
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
    table_proj.source_table = "my_source_table";
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
  ASSERT_STR_EQ("C_ORM_TYPE_DOUBLE", out);

  fclose(fp);
  PASS();
}

/**
 * @brief Test suite runner for SQL to C code generation.
 */
SUITE(sql_to_c_suite) {
  RUN_TEST(test_sql_to_c_header_emit);
  RUN_TEST(test_sql_to_c_source_emit);
  RUN_TEST(test_sql_to_c_errors);
  RUN_TEST(test_sql_to_c_projections);
  RUN_TEST(test_sql_to_c_polymorphic);
  RUN_TEST(test_sql_to_c_union);
  RUN_TEST(test_sql_to_c_bitmask_sizes);
  RUN_TEST(test_sql_to_c_projection_types);
  RUN_TEST(test_sql_to_c_edge_cases);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#if defined(__clang__) || defined(__GNUC__)
#endif
