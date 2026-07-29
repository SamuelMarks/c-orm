/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_sql_to_c.h"
#include <greatest.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
/* clang-format on */

TEST test_sql_to_c_header_emit(void) {
  const char *sql =
      "CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(255) NOT NULL, "
      "role_id BIGINT REFERENCES roles(id), is_active BOOLEAN DEFAULT true);";
  az_span span = az_span_create_from_str((char *)sql);
  struct sql_token_list_t *list = NULL;
  struct sql_table_t *table = NULL;
  struct sql_parse_error_t err_info;
  int err;

  char buf[4096];
  FILE *fp;

  err = sql_lex(span, &list);
  ASSERT_EQ(0, err);

  err = sql_parse_table(list, &table, &err_info);
  ASSERT_EQ(0, err);

  fp = tmpfile();
  ASSERT(fp != NULL);

  err = sql_to_c_header_emit(fp, table);
  ASSERT_EQ(0, err);

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
  free(table);
  sql_token_list_free(list);
  PASS();
}

TEST test_sql_to_c_source_emit(void) {
  const char *sql =
      "CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(255) NOT NULL, "
      "role_id BIGINT REFERENCES roles(id), is_active BOOLEAN DEFAULT true);";
  az_span span = az_span_create_from_str((char *)sql);
  struct sql_token_list_t *list = NULL;
  struct sql_table_t *table = NULL;
  struct sql_parse_error_t err_info;
  int err;

  char buf[4096];
  FILE *fp;

  err = sql_lex(span, &list);
  ASSERT_EQ(0, err);

  err = sql_parse_table(list, &table, &err_info);
  ASSERT_EQ(0, err);

  fp = tmpfile();
  ASSERT(fp != NULL);

  err = sql_to_c_source_emit(fp, table, "users.h");
  ASSERT_EQ(0, err);

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
  free(table);
  sql_token_list_free(list);
  PASS();
}

TEST test_sql_to_c_errors(void) {
  ASSERT_EQ(C_ORM_ERROR_MEMORY, sql_to_c_header_emit(NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, sql_to_c_source_emit(NULL, NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            sql_to_c_projection_struct_emit(NULL, NULL, NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            sql_to_c_projection_free_emit(NULL, NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            sql_to_c_projection_meta_emit(NULL, NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            sql_to_c_projection_hydrate_emit(NULL, NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            sql_to_c_projection_dehydrate_emit(NULL, NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            sql_to_c_projection_nested_struct_emit(NULL, NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            sql_to_c_projection_nested_array_emit(NULL, NULL, NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            sql_to_c_projection_dirty_bitmask_emit(NULL, NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            sql_to_c_projection_union_struct_emit(NULL, NULL, 0, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            sql_to_c_projection_polymorphic_struct_emit(NULL, NULL, NULL));
  PASS();
}

TEST test_sql_to_c_projections(void) {
  FILE *fp = tmpfile();
  cdd_c_query_projection_t proj;
  c_orm_uint64_t out_hash;
  memset(&proj, 0, sizeof(proj));

  proj.n_fields = 2;
  proj.fields = calloc(2, sizeof(*proj.fields));
  proj.fields[0].name = "id";
  proj.fields[0].type = SQL_TYPE_INT;
  proj.fields[0].is_array = 0;

  proj.fields[1].name = "tags";
  proj.fields[1].type = SQL_TYPE_VARCHAR;
  proj.fields[1].is_array = 1;

  ASSERT_EQ(0,
            sql_to_c_projection_struct_emit(fp, &proj, "ProjTest", &out_hash));
  ASSERT_EQ(0, sql_to_c_projection_free_emit(fp, &proj, "ProjTest"));
  ASSERT_EQ(0, sql_to_c_projection_meta_emit(fp, &proj, "ProjTest"));
  ASSERT_EQ(0, sql_to_c_projection_hydrate_emit(fp, &proj, "ProjTest"));
  ASSERT_EQ(0, sql_to_c_projection_dehydrate_emit(fp, &proj, "ProjTest"));
  ASSERT_EQ(0, sql_to_c_projection_nested_struct_emit(fp, &proj, "ProjTest"));
  ASSERT_EQ(0, sql_to_c_projection_nested_array_emit(fp, &proj, "ProjTest",
                                                     "ProjTestArray"));
  ASSERT_EQ(0, sql_to_c_projection_dirty_bitmask_emit(fp, &proj, "ProjTest"));
  ASSERT_EQ(0, sql_to_c_projection_union_struct_emit(fp, &proj, 1, "ProjTest"));
  ASSERT_EQ(0,
            sql_to_c_projection_polymorphic_struct_emit(fp, &proj, "ProjTest"));

  free(proj.fields);
  fclose(fp);
  PASS();
}

TEST test_sql_to_c_polymorphic(void) {
  FILE *fp = tmpfile();
  cdd_c_query_projection_t proj;
  memset(&proj, 0, sizeof(proj));

  proj.n_fields = 1;
  proj.fields = calloc(1, sizeof(*proj.fields));
  proj.fields[0].name = "dynamic_field";
  proj.fields[0].type = SQL_TYPE_DOUBLE;
  proj.fields[0].is_array = 0;

  ASSERT_EQ(0,
            sql_to_c_projection_polymorphic_struct_emit(fp, &proj, "PolyTest"));

  free(proj.fields);
  fclose(fp);
  PASS();
}

TEST test_sql_to_c_union(void) {
  FILE *fp = tmpfile();
  cdd_c_query_projection_t projs[2];
  memset(projs, 0, sizeof(projs));

  projs[0].n_fields = 1;
  projs[0].fields = calloc(1, sizeof(cdd_c_query_projection_field_t));
  projs[0].fields[0].name = "branch_0_f";
  projs[0].fields[0].type = SQL_TYPE_INT;

  projs[1].n_fields = 1;
  projs[1].fields = calloc(1, sizeof(cdd_c_query_projection_field_t));
  projs[1].fields[0].name = "branch_1_f";
  projs[1].fields[0].type = SQL_TYPE_VARCHAR;
  projs[1].fields[0].length = 64;

  ASSERT_EQ(0,
            sql_to_c_projection_union_struct_emit(fp, projs, 2, "UnionTest"));

  free(projs[0].fields);
  free(projs[1].fields);
  fclose(fp);
  PASS();
}

TEST test_sql_to_c_bitmask_sizes(void) {
  FILE *fp = tmpfile();
  cdd_c_query_projection_t proj;
  memset(&proj, 0, sizeof(proj));

  /* 0 fields */
  proj.n_fields = 0;
  ASSERT_EQ(0, sql_to_c_projection_dirty_bitmask_emit(fp, &proj, "Mask0"));

  /* 8 fields */
  proj.n_fields = 8;
  ASSERT_EQ(0, sql_to_c_projection_dirty_bitmask_emit(fp, &proj, "Mask8"));

  /* 16 fields */
  proj.n_fields = 16;
  ASSERT_EQ(0, sql_to_c_projection_dirty_bitmask_emit(fp, &proj, "Mask16"));

  /* 32 fields */
  proj.n_fields = 32;
  ASSERT_EQ(0, sql_to_c_projection_dirty_bitmask_emit(fp, &proj, "Mask32"));

  /* 64 fields */
  proj.n_fields = 64;
  ASSERT_EQ(0, sql_to_c_projection_dirty_bitmask_emit(fp, &proj, "Mask64"));

  /* 100 fields */
  proj.n_fields = 100;
  ASSERT_EQ(0, sql_to_c_projection_dirty_bitmask_emit(fp, &proj, "Mask100"));

  fclose(fp);
  PASS();
}

TEST test_sql_to_c_projection_types(void) {
  FILE *fp = tmpfile();
  cdd_c_query_projection_t proj;
  c_orm_uint64_t hash;
  memset(&proj, 0, sizeof(proj));

  proj.n_fields = 4;
  proj.fields = calloc(4, sizeof(*proj.fields));
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

  ASSERT_EQ(0, sql_to_c_projection_struct_emit(fp, &proj, "ProjTypes", &hash));
  ASSERT_EQ(0, sql_to_c_projection_free_emit(fp, &proj, "ProjTypes"));
  ASSERT_EQ(0, sql_to_c_projection_hydrate_emit(fp, &proj, "ProjTypes"));
  ASSERT_EQ(0, sql_to_c_projection_dehydrate_emit(fp, &proj, "ProjTypes"));

  free(proj.fields);
  fclose(fp);
  PASS();
}

TEST test_sql_to_c_edge_cases(void) {
  FILE *fp = tmpfile();
  cdd_c_query_projection_t proj;
  cdd_c_query_projection_t projs[1];
  memset(&proj, 0, sizeof(proj));
  memset(projs, 0, sizeof(projs));

  proj.n_fields = 2;
  proj.fields = calloc(2, sizeof(*proj.fields));

  /* Fallback double coverage in type mapping */
  proj.fields[0].name = "dbl_field";
  proj.fields[0].type = SQL_TYPE_DOUBLE;
  proj.fields[0].is_array = 0;

  /* Force str_to_upper empty */
  /* This is hard to force directly without private API access, but we'll cover
   * other types */
  proj.fields[1].name = "unknown_field";
  proj.fields[1].type = SQL_TYPE_DATE; /* maps to C_ORM_TYPE_DATE but no
                                          hydration case explicitly */

  ASSERT_EQ(0, sql_to_c_projection_struct_emit(fp, &proj, "EdgeType", NULL));
  ASSERT_EQ(0, sql_to_c_projection_meta_emit(fp, &proj, "EdgeType"));
  ASSERT_EQ(0, sql_to_c_projection_hydrate_emit(fp, &proj, "EdgeType"));
  ASSERT_EQ(0, sql_to_c_projection_dehydrate_emit(fp, &proj, "EdgeType"));

  /* Polymorphic specific coverage */
  ASSERT_EQ(0,
            sql_to_c_projection_polymorphic_struct_emit(fp, &proj, "PolyEdge"));

  /* Union coverage */
  projs[0] = proj;
  ASSERT_EQ(0,
            sql_to_c_projection_union_struct_emit(fp, projs, 1, "UnionEdge"));

  /* Nested array failure propagation coverage */
  ASSERT_EQ(C_ORM_ERROR_MEMORY, sql_to_c_projection_nested_array_emit(
                                    NULL, &proj, "struct", "arr"));

  /* Null array name coverage */
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            sql_to_c_projection_nested_array_emit(fp, &proj, "struct", NULL));

  free(proj.fields);

  /* Empty table name coverage */
  {
    struct sql_table_t empty_table;
    memset(&empty_table, 0, sizeof(empty_table));
    empty_table.name = "";
    ASSERT_EQ(0, sql_to_c_header_emit(fp, &empty_table));
    ASSERT_EQ(0, sql_to_c_source_emit(fp, &empty_table, "empty.h"));
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
    ASSERT_EQ(0, sql_to_c_source_emit(fp, &nopk_table, "nopk.h"));
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
    fields[0].type = 999; /* Unknown type */

    fields[1].name = "unsecured_str";
    fields[1].type = SQL_TYPE_VARCHAR;
    fields[1].length = 0;
    fields[1].is_secure = 0;

    fields[2].name = "fixed_str";
    fields[2].type = SQL_TYPE_VARCHAR;
    fields[2].length = 128;
    fields[2].is_secure = 0;

    ASSERT_EQ(0,
              sql_to_c_projection_struct_emit(fp, &unk_proj, "UnkType", NULL));
    ASSERT_EQ(0, sql_to_c_projection_free_emit(fp, &unk_proj, "UnkType"));
    /* Hit polymorphic string array/length logic */
    ASSERT_EQ(0, sql_to_c_projection_polymorphic_struct_emit(fp, &unk_proj,
                                                             "UnkPoly"));
  }

  /* Target table name emit */
  {
    cdd_c_query_projection_t table_proj;
    memset(&table_proj, 0, sizeof(table_proj));
    table_proj.source_table = "my_source_table";
    ASSERT_EQ(
        0, sql_to_c_projection_struct_emit(fp, &table_proj, "TableProj", NULL));
  }

  /* sql_type_to_c_orm_type coverage */
  {
    const char *out;
    ASSERT_EQ(C_ORM_ERROR_MEMORY, sql_type_to_c_orm_type(SQL_TYPE_INT, NULL));
    ASSERT_EQ(0, sql_type_to_c_orm_type(999, &out));
    ASSERT_STR_EQ("C_ORM_TYPE_UNKNOWN", out);
  }

  fclose(fp);
  PASS();
}
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
