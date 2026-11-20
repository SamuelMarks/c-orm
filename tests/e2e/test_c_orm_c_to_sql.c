/**
 * @file test_c_orm_c_to_sql.c
 * @brief Unit tests for C struct to SQL generation, schema diffing, and
 * topological sort.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_c_to_sql.h"
#include "c_orm_string_builder.h"
#include "abstract_struct.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#define GREATEST_USE_LONGJMP 0
#include <greatest.h>

/* clang-format on */

/**
 * @brief Test write_struct_to_sql_create_table with basic SQLite schema.
 * @return GREATEST test result.
 */

#undef ASSERT_EQ_FMT
#define ASSERT_EQ_FMT(exp, got, fmt)                                           \
  do {                                                                         \
    greatest_info.assertions++;                                                \
    if (!((exp) == (got))) {                                                   \
      fprintf(stderr, "\nFAIL %s:%d: %s != %s\n", __FILE__, __LINE__, #exp,    \
              #got);                                                           \
      return GREATEST_TEST_RES_FAIL;                                           \
    }                                                                          \
  } while ((void)0, 0)
#undef ASSERT_EQ
#define ASSERT_EQ(exp, got)                                                    \
  do {                                                                         \
    greatest_info.assertions++;                                                \
    if (!((exp) == (got))) {                                                   \
      fprintf(stderr, "\nFAIL %s:%d: %s != %s\n", __FILE__, __LINE__, #exp,    \
              #got);                                                           \
      return GREATEST_TEST_RES_FAIL;                                           \
    }                                                                          \
  } while ((void)0, 0)
#undef ASSERT
#define ASSERT(cond)                                                           \
  do {                                                                         \
    greatest_info.assertions++;                                                \
    if (!((cond) != 0)) {                                                      \
      fprintf(stderr, "\nFAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
      return GREATEST_TEST_RES_FAIL;                                           \
    }                                                                          \
  } while ((void)0, 0)
#undef ASSERT_STR_EQ
#define ASSERT_STR_EQ(exp, got)                                                \
  do {                                                                         \
    greatest_info.assertions++;                                                \
    if (strcmp((exp), (got)) != 0) {                                           \
      fprintf(stderr, "\nFAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__,  \
              (exp), (got));                                                   \
      return GREATEST_TEST_RES_FAIL;                                           \
    }                                                                          \
  } while ((void)0, 0)
#undef ASSERT_NEQ
#define ASSERT_NEQ(exp, got)                                                   \
  do {                                                                         \
    greatest_info.assertions++;                                                \
    if (!((exp) != (got))) {                                                   \
      fprintf(stderr, "\nFAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #exp,    \
              #got);                                                           \
      return GREATEST_TEST_RES_FAIL;                                           \
    }                                                                          \
  } while ((void)0, 0)
#undef RUN_TEST
#define RUN_TEST(TEST)                                                         \
  do {                                                                         \
    int should = 0;                                                            \
    enum greatest_test_res res;                                                \
    greatest_test_pre(#TEST, &should);                                         \
    res = TEST();                                                              \
    greatest_test_post(res);                                                   \
  } while ((void)0, 0)
#undef CHECK_CALL
#define CHECK_CALL(res)                                                        \
  do {                                                                         \
    enum greatest_test_res _rc = (res);                                        \
    greatest_info.assertions++;                                                \
    if (_rc != GREATEST_TEST_RES_PASS) {                                       \
      return _rc;                                                              \
    }                                                                          \
  } while ((void)0, 0)

/** @brief Countdown for mock malloc in c_to_sql tests. */
static int c2s_oom_countdown = -1;

/**
 * @brief Mock malloc for c_to_sql OOM testing.
 * @param size Allocation size in bytes.
 * @param out_ptr Output pointer receiving allocated address.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on countdown expiry.
 */
static c_orm_error_t c2s_mock_malloc(size_t size, void **out_ptr) {
  if (c2s_oom_countdown == 0) {
    c2s_oom_countdown--;
    if (out_ptr) {
      *out_ptr = NULL;
    }
    return C_ORM_ERROR_MEMORY;
  }
  if (c2s_oom_countdown > 0) {
    c2s_oom_countdown--;
  }
  *out_ptr = malloc(size == 0 ? 1 : size);
  return *out_ptr ? C_ORM_OK : C_ORM_ERROR_MEMORY;
}

/**
 * @brief Mock realloc for c_to_sql OOM testing.
 * @param ptr Existing pointer.
 * @param size New allocation size in bytes.
 * @param out_ptr Output pointer receiving reallocated address.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on countdown expiry.
 */
static c_orm_error_t c2s_mock_realloc(void *ptr, size_t size, void **out_ptr) {
  if (c2s_oom_countdown == 0) {
    c2s_oom_countdown--;
    if (out_ptr) {
      *out_ptr = NULL;
    }
    return C_ORM_ERROR_MEMORY;
  }
  if (c2s_oom_countdown > 0) {
    c2s_oom_countdown--;
  }
  *out_ptr = realloc(ptr, size == 0 ? 1 : size);
  return *out_ptr ? C_ORM_OK : C_ORM_ERROR_MEMORY;
}

/**
 * @brief Executes test_write_struct_to_sql_create_table.
 */
TEST test_write_struct_to_sql_create_table(void) {
  struct StructField fields[3];
  struct StructFields sf;
  char buf[1024];
  FILE *fp;
  c_orm_error_t rc;

  fp = NULL;
  memset(buf, 0, sizeof(buf));
  memset(fields, 0, sizeof(fields));
  C_ORM_STRCPY(fields[0].name, sizeof(fields[0].name), "id");
  C_ORM_STRCPY(fields[0].type, sizeof(fields[0].type), "integer");
  fields[0].required = 1;

  C_ORM_STRCPY(fields[1].name, sizeof(fields[1].name), "username");
  C_ORM_STRCPY(fields[1].type, sizeof(fields[1].type), "string");
  C_ORM_STRCPY(fields[1].description, sizeof(fields[1].description),
               "@unique @notnull");

  C_ORM_STRCPY(fields[2].name, sizeof(fields[2].name), "company_id");
  C_ORM_STRCPY(fields[2].type, sizeof(fields[2].type), "integer");

  sf.size = 3;
  sf.fields = fields;

  C_ORM_TMPFILE(&fp);
  ASSERT(fp != NULL);

  rc = write_struct_to_sql_create_table(fp, "users", &sf,
                                        C_TO_SQL_DIALECT_SQLITE);
  ASSERT_EQ(C_ORM_OK, rc);

  rewind(fp);
  fread(buf, 1, sizeof(buf) - 1, fp);
  fclose(fp);

  ASSERT(strstr(buf, "CREATE TABLE users") != NULL);
  ASSERT(strstr(buf, "id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL") != NULL);
  ASSERT(strstr(buf, "username TEXT UNIQUE NOT NULL") != NULL);
  ASSERT(strstr(buf, "company_id INTEGER REFERENCES company(id)") != NULL);

  PASS();
}

/**
 * @brief Test cdd_c_meta_to_sql_create_table with MySQL dialect.
 * @return GREATEST test result.
 */
TEST test_cdd_c_meta_to_sql_create_table(void) {
  cdd_c_prop_meta_t props[2];
  cdd_c_meta_t meta;
  char *out_sql;
  c_orm_error_t rc;

  out_sql = NULL;
  memset(&meta, 0, sizeof(meta));
  memset(props, 0, sizeof(props));

  props[0].name = "id";
  props[0].type = "int";

  props[1].name = "name";
  props[1].type = "char*";

  meta.name = "company";
  meta.props = props;
  meta.num_props = 2;

  rc = cdd_c_meta_to_sql_create_table(&meta, C_TO_SQL_DIALECT_MYSQL, &out_sql);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT(out_sql != NULL);
  ASSERT(strstr(out_sql, "CREATE TABLE company") != NULL);
  ASSERT(strstr(out_sql, "id INT PRIMARY KEY") != NULL);
  ASSERT(strstr(out_sql, "name VARCHAR(255)") != NULL);

  (void)c_orm_system_free(out_sql);
  PASS();
}

/**
 * @brief Test schema difference calculation and SQL generation.
 * @return GREATEST test result.
 */
TEST test_cdd_c_meta_diff_and_sql(void) {
  cdd_c_prop_meta_t props_old[1];
  cdd_c_prop_meta_t props_new[2];
  cdd_c_meta_t old_meta;
  cdd_c_meta_t new_meta;
  cdd_c_meta_diff_t diff;
  char *up_sql;
  char *down_sql;
  c_orm_error_t rc;

  up_sql = NULL;
  down_sql = NULL;
  memset(&old_meta, 0, sizeof(old_meta));
  memset(&new_meta, 0, sizeof(new_meta));
  memset(props_old, 0, sizeof(props_old));
  memset(props_new, 0, sizeof(props_new));

  props_old[0].name = "id";
  props_old[0].type = "int";

  old_meta.name = "test_table";
  old_meta.props = props_old;
  old_meta.num_props = 1;

  props_new[0].name = "id";
  props_new[0].type = "int";

  props_new[1].name = "description";
  props_new[1].type = "char*";

  new_meta.name = "test_table";
  new_meta.props = props_new;
  new_meta.num_props = 2;

  rc = cdd_c_meta_diff(&old_meta, &new_meta, &diff);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ(1, diff.num_added);
  ASSERT_EQ(0, diff.num_dropped);
  ASSERT_EQ(0, diff.num_altered);

  rc = cdd_c_meta_diff_to_sql("test_table", &diff, C_TO_SQL_DIALECT_POSTGRESQL,
                              &up_sql, &down_sql);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT(up_sql != NULL);
  ASSERT(down_sql != NULL);
  ASSERT(strstr(up_sql, "ADD COLUMN description TEXT") != NULL);
  ASSERT(strstr(down_sql, "DROP COLUMN description") != NULL);

  (void)c_orm_system_free(up_sql);
  (void)c_orm_system_free(down_sql);
  cdd_c_meta_diff_free(&diff);
  PASS();
}

/**
 * @brief Test retrieving schema inspection queries.
 * @return GREATEST test result.
 */
TEST test_cdd_c_get_schema_inspection_query(void) {
  char *query;
  c_orm_error_t rc;

  query = NULL;
  rc = cdd_c_get_schema_inspection_query(C_TO_SQL_DIALECT_POSTGRESQL, "users",
                                         &query);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT(query != NULL);
  ASSERT(strstr(query, "information_schema") != NULL);
  (void)c_orm_system_free(query);

  PASS();
}

/**
 * @brief Test CREATE and DROP INDEX SQL emission.
 * @return GREATEST test result.
 */
TEST test_cdd_c_emit_index(void) {
  char *query;
  c_orm_error_t rc;

  query = NULL;
  rc = cdd_c_emit_create_index("users", "idx_users_email", "email", 1, &query);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT(query != NULL);
  ASSERT(strstr(query, "UNIQUE INDEX idx_users_email ON users (email)") !=
         NULL);
  (void)c_orm_system_free(query);
  query = NULL;

  rc = cdd_c_emit_drop_index("idx_users_email", &query);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT(query != NULL);
  ASSERT(strstr(query, "DROP INDEX idx_users_email") != NULL);
  (void)c_orm_system_free(query);

  PASS();
}

/**
 * @brief Test topological sorting of interdependent schemas.
 * @return GREATEST test result.
 */
TEST test_cdd_c_meta_topological_sort(void) {
  cdd_c_prop_meta_t p_user[1];
  cdd_c_prop_meta_t p_post[2];
  cdd_c_meta_t m_user;
  cdd_c_meta_t m_post;
  const cdd_c_meta_t *schemas[2];
  const cdd_c_meta_t *out_schemas[2];
  c_orm_error_t rc;

  memset(&m_user, 0, sizeof(m_user));
  memset(&m_post, 0, sizeof(m_post));
  memset(p_user, 0, sizeof(p_user));
  memset(p_post, 0, sizeof(p_post));

  p_user[0].name = "id";
  p_user[0].type = "int";
  m_user.name = "user";
  m_user.props = p_user;
  m_user.num_props = 1;

  p_post[0].name = "id";
  p_post[0].type = "int";
  p_post[1].name = "user_id";
  p_post[1].type = "int";
  m_post.name = "post";
  m_post.props = p_post;
  m_post.num_props = 2;

  /* Put dependent post first to see if sort works */
  schemas[0] = &m_post;
  schemas[1] = &m_user;

  rc = cdd_c_meta_topological_sort(schemas, 2, out_schemas);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT(out_schemas[0] == &m_user);
  ASSERT(out_schemas[1] == &m_post);

  PASS();
}

/**
 * @brief Test error validation and NULL inputs across c_to_sql API.
 * @return GREATEST test result.
 */
TEST test_c_to_sql_errors(void) {
  struct StructFields sf;
  cdd_c_meta_t meta;
  cdd_c_meta_diff_t diff;
  char *str;
  FILE *fp;
  const cdd_c_meta_t *schemas[1];
  const cdd_c_meta_t *out_schemas[1];
  c_orm_error_t rc;

  str = NULL;
  fp = NULL;
  memset(&sf, 0, sizeof(sf));
  memset(&meta, 0, sizeof(meta));
  memset(&diff, 0, sizeof(diff));

  C_ORM_TMPFILE(&fp);

  /* write_struct_to_sql_create_table NULL variations */
  rc =
      write_struct_to_sql_create_table(NULL, "t", &sf, C_TO_SQL_DIALECT_SQLITE);
  ASSERT(rc != C_ORM_OK);
  rc = write_struct_to_sql_create_table(fp, NULL, &sf, C_TO_SQL_DIALECT_SQLITE);
  ASSERT(rc != C_ORM_OK);
  rc = write_struct_to_sql_create_table(fp, "t", NULL, C_TO_SQL_DIALECT_SQLITE);
  ASSERT(rc != C_ORM_OK);
  fclose(fp);
  fp = NULL;

  /* cdd_c_meta_to_sql_create_table NULL variations */
  rc = cdd_c_meta_to_sql_create_table(NULL, C_TO_SQL_DIALECT_SQLITE, &str);
  ASSERT(rc != C_ORM_OK);
  rc = cdd_c_meta_to_sql_create_table(&meta, C_TO_SQL_DIALECT_SQLITE, NULL);
  ASSERT(rc != C_ORM_OK);

  /* cdd_c_meta_diff NULL variations */
  rc = cdd_c_meta_diff(NULL, &meta, &diff);
  ASSERT(rc != C_ORM_OK);
  rc = cdd_c_meta_diff(&meta, NULL, &diff);
  ASSERT(rc != C_ORM_OK);
  rc = cdd_c_meta_diff(&meta, &meta, NULL);
  ASSERT(rc != C_ORM_OK);

  /* cdd_c_meta_diff_to_sql NULL variations */
  rc = cdd_c_meta_diff_to_sql(NULL, &diff, C_TO_SQL_DIALECT_SQLITE, &str, &str);
  ASSERT(rc != C_ORM_OK);
  rc = cdd_c_meta_diff_to_sql("t", NULL, C_TO_SQL_DIALECT_SQLITE, &str, &str);
  ASSERT(rc != C_ORM_OK);
  rc = cdd_c_meta_diff_to_sql("t", &diff, C_TO_SQL_DIALECT_SQLITE, NULL, &str);
  ASSERT(rc != C_ORM_OK);
  rc = cdd_c_meta_diff_to_sql("t", &diff, C_TO_SQL_DIALECT_SQLITE, &str, NULL);
  ASSERT(rc != C_ORM_OK);

  /* cdd_c_get_schema_inspection_query NULL & dialect variations */
  rc = cdd_c_get_schema_inspection_query(C_TO_SQL_DIALECT_SQLITE, NULL, &str);
  ASSERT(rc != C_ORM_OK);
  rc = cdd_c_get_schema_inspection_query(C_TO_SQL_DIALECT_SQLITE, "t", NULL);
  ASSERT(rc != C_ORM_OK);
  rc = cdd_c_get_schema_inspection_query((c_to_sql_dialect_t)99, "t", &str);
  ASSERT(rc != C_ORM_OK);

  /* cdd_c_emit_create_index NULL variations */
  rc = cdd_c_emit_create_index(NULL, "i", "c", 0, &str);
  ASSERT(rc != C_ORM_OK);
  rc = cdd_c_emit_create_index("t", NULL, "c", 0, &str);
  ASSERT(rc != C_ORM_OK);
  rc = cdd_c_emit_create_index("t", "i", NULL, 0, &str);
  ASSERT(rc != C_ORM_OK);
  rc = cdd_c_emit_create_index("t", "i", "c", 0, NULL);
  ASSERT(rc != C_ORM_OK);

  /* cdd_c_emit_drop_index NULL variations */
  rc = cdd_c_emit_drop_index(NULL, &str);
  ASSERT(rc != C_ORM_OK);
  rc = cdd_c_emit_drop_index("i", NULL);
  ASSERT(rc != C_ORM_OK);

  /* cdd_c_meta_topological_sort NULL variations */
  rc = cdd_c_meta_topological_sort(NULL, 1, out_schemas);
  ASSERT(rc != C_ORM_OK);
  rc = cdd_c_meta_topological_sort(schemas, 1, NULL);
  ASSERT(rc != C_ORM_OK);
  rc = cdd_c_meta_topological_sort(schemas, 0, out_schemas);
  ASSERT(rc != C_ORM_OK);

  PASS();
}

/**
 * @brief Test cdd_c_meta_to_sql_create_table with PostgreSQL and SQLite
 * dialect.
 * @return GREATEST test result.
 */
TEST test_cdd_c_meta_to_sql_create_table_pg(void) {
  cdd_c_prop_meta_t props[5];
  cdd_c_meta_t meta;
  char *out_sql;
  c_orm_error_t rc;

  out_sql = NULL;
  memset(&meta, 0, sizeof(meta));
  memset(props, 0, sizeof(props));

  props[0].name = "id";
  props[0].type = "int";

  props[1].name = "name";
  props[1].type = "char*";

  props[2].name = "score";
  props[2].type = "double";

  props[3].name = "is_active";
  props[3].type = "bool";

  props[4].name = "user_id";
  props[4].type = "int";

  meta.name = "users";
  meta.props = props;
  meta.num_props = 5;

  rc = cdd_c_meta_to_sql_create_table(&meta, C_TO_SQL_DIALECT_POSTGRESQL,
                                      &out_sql);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT(out_sql != NULL);
  ASSERT(strstr(out_sql, "id INTEGER PRIMARY KEY") != NULL);
  ASSERT(strstr(out_sql, "name TEXT") != NULL);
  ASSERT(strstr(out_sql, "score DOUBLE PRECISION") != NULL);
  ASSERT(strstr(out_sql, "is_active BOOLEAN") != NULL);
  ASSERT(strstr(out_sql, "user_id INTEGER REFERENCES user(id)") != NULL);
  (void)c_orm_system_free(out_sql);
  out_sql = NULL;

  rc = cdd_c_meta_to_sql_create_table(&meta, C_TO_SQL_DIALECT_SQLITE, &out_sql);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT(out_sql != NULL);
  ASSERT(strstr(out_sql, "id INTEGER PRIMARY KEY AUTOINCREMENT") != NULL);
  ASSERT(strstr(out_sql, "is_active INTEGER") != NULL);
  (void)c_orm_system_free(out_sql);

  PASS();
}

/**
 * @brief Test inspection queries for SQLite and MySQL.
 * @return GREATEST test result.
 */
TEST test_cdd_c_get_schema_inspection_query_sqlite_mysql(void) {
  char *query;
  c_orm_error_t rc;

  query = NULL;
  rc = cdd_c_get_schema_inspection_query(C_TO_SQL_DIALECT_SQLITE, "users",
                                         &query);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT(query != NULL);
  ASSERT(strstr(query, "PRAGMA table_info(users);") != NULL);
  (void)c_orm_system_free(query);
  query = NULL;

  rc = cdd_c_get_schema_inspection_query(C_TO_SQL_DIALECT_MYSQL, "users",
                                         &query);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT(query != NULL);
  ASSERT(strstr(query, "SHOW COLUMNS FROM users;") != NULL);
  (void)c_orm_system_free(query);
  query = NULL;

  /* invalid dialect */
  rc = cdd_c_get_schema_inspection_query((c_to_sql_dialect_t)999, "users",
                                         &query);
  ASSERT(rc != C_ORM_OK);

  PASS();
}

/**
 * @brief Test SQLite schema diff handling without DROP COLUMN support.
 * @return GREATEST test result.
 */
TEST test_cdd_c_meta_diff_sqlite(void) {
  cdd_c_prop_meta_t props_old[1];
  cdd_c_prop_meta_t props_new[2];
  cdd_c_meta_t old_meta;
  cdd_c_meta_t new_meta;
  cdd_c_meta_diff_t diff;
  char *up_sql;
  char *down_sql;
  c_orm_error_t rc;

  up_sql = NULL;
  down_sql = NULL;
  memset(&old_meta, 0, sizeof(old_meta));
  memset(&new_meta, 0, sizeof(new_meta));
  memset(props_old, 0, sizeof(props_old));
  memset(props_new, 0, sizeof(props_new));

  props_old[0].name = "id";
  props_old[0].type = "int";
  props_new[0].name = "id";
  props_new[0].type = "int";
  props_new[1].name = "description";
  props_new[1].type = "char*";

  old_meta.name = "test_table";
  old_meta.props = props_old;
  old_meta.num_props = 1;
  new_meta.name = "test_table";
  new_meta.props = props_new;
  new_meta.num_props = 2;

  rc = cdd_c_meta_diff(&old_meta, &new_meta, &diff);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = cdd_c_meta_diff_to_sql("test_table", &diff, C_TO_SQL_DIALECT_SQLITE,
                              &up_sql, &down_sql);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT(down_sql != NULL);
  ASSERT(strstr(down_sql, "ALTER TABLE test_table DROP COLUMN description") !=
         NULL);
  (void)c_orm_system_free(up_sql);
  (void)c_orm_system_free(down_sql);

  cdd_c_meta_diff_free(&diff);
  cdd_c_meta_diff_free(NULL); /* coverage */

  PASS();
}

/**
 * @brief Test cycle detection in topological sorting.
 * @return GREATEST test result.
 */
TEST test_cdd_c_meta_topological_sort_cycle(void) {
  cdd_c_prop_meta_t p_a[2];
  cdd_c_prop_meta_t p_b[2];
  cdd_c_meta_t m_a;
  cdd_c_meta_t m_b;
  const cdd_c_meta_t *schemas[2];
  const cdd_c_meta_t *out_schemas[2];
  c_orm_error_t rc;

  memset(&m_a, 0, sizeof(m_a));
  memset(&m_b, 0, sizeof(m_b));
  memset(p_a, 0, sizeof(p_a));
  memset(p_b, 0, sizeof(p_b));

  p_a[0].name = "id";
  p_a[0].type = "int";
  p_a[1].name = "b_id";
  p_a[1].type = "int";
  m_a.name = "a";
  m_a.props = p_a;
  m_a.num_props = 2;

  p_b[0].name = "id";
  p_b[0].type = "int";
  p_b[1].name = "a_id";
  p_b[1].type = "int";
  m_b.name = "b";
  m_b.props = p_b;
  m_b.num_props = 2;

  schemas[0] = &m_a;
  schemas[1] = &m_b;

  rc = cdd_c_meta_topological_sort(schemas, 2, out_schemas);
  ASSERT_EQ(C_ORM_ERROR_CYCLE, rc);

  PASS();
}

/**
 * @brief Test OOM resilience and >64-character foreign key identifiers in
 * c_to_sql.
 * @return GREATEST test result.
 */
TEST test_c_to_sql_oom_and_long_identifiers(void) {
  c_orm_malloc_fn_t old_malloc = c_orm_malloc;
  c_orm_realloc_fn_t old_realloc = c_orm_realloc;
  const char *long_tbl =
      "organization_department_division_team_subteam_member_profile_record_"
      "alpha_beta";
  const char *long_fk =
      "organization_department_division_team_subteam_member_profile_record_"
      "alpha_beta_id";
  cdd_c_prop_meta_t p_parent[1];
  cdd_c_prop_meta_t p_child[2];
  cdd_c_meta_t m_parent;
  cdd_c_meta_t m_child;
  cdd_c_meta_diff_t diff;
  const cdd_c_meta_t *schemas[2];
  const cdd_c_meta_t *sorted[2];
  char *out_sql = NULL;
  char *up_sql = NULL;
  char *down_sql = NULL;
  int step;

  memset(&m_parent, 0, sizeof(m_parent));
  memset(&m_child, 0, sizeof(m_child));
  memset(p_parent, 0, sizeof(p_parent));
  memset(p_child, 0, sizeof(p_child));

  p_parent[0].name = "id";
  p_parent[0].type = "int";
  m_parent.name = long_tbl;
  m_parent.props = p_parent;
  m_parent.num_props = 1;

  p_child[0].name = "id";
  p_child[0].type = "char*";
  p_child[1].name = long_fk;
  p_child[1].type = "int";
  m_child.name = "child_item";
  m_child.props = p_child;
  m_child.num_props = 2;

  /* Verify >64-char FK identifier works in CREATE TABLE and topological sort */
  ASSERT_EQ(C_ORM_OK, cdd_c_meta_to_sql_create_table(
                          &m_child, C_TO_SQL_DIALECT_SQLITE, &out_sql));
  ASSERT(out_sql != NULL);
  ASSERT(strstr(out_sql, long_tbl) != NULL);
  (void)c_orm_system_free(out_sql);
  out_sql = NULL;

  schemas[0] = &m_child;
  schemas[1] = &m_parent;
  ASSERT_EQ(C_ORM_OK, cdd_c_meta_topological_sort(schemas, 2, sorted));
  ASSERT(sorted[0] == &m_parent);
  ASSERT(sorted[1] == &m_child);

  /* OOM sweep across cdd_c_meta_to_sql_create_table */
  c_orm_set_allocators(c2s_mock_malloc, c2s_mock_realloc, c_orm_free);
  for (step = 0; step < 8; ++step) {
    c2s_oom_countdown = step;
    out_sql = NULL;
    if (cdd_c_meta_to_sql_create_table(&m_child, C_TO_SQL_DIALECT_SQLITE,
                                       &out_sql) == C_ORM_OK) {
      (void)c_orm_system_free(out_sql);
      break;
    }
  }

  /* OOM sweep across cdd_c_meta_diff (altered, added, dropped) */
  for (step = 0; step < 4; ++step) {
    c2s_oom_countdown = step;
    if (cdd_c_meta_diff(&m_parent, &m_child, &diff) == C_ORM_OK) {
      cdd_c_meta_diff_free(&diff);
      break;
    }
  }
  for (step = 0; step < 4; ++step) {
    c2s_oom_countdown = step;
    if (cdd_c_meta_diff(&m_child, &m_parent, &diff) == C_ORM_OK) {
      cdd_c_meta_diff_free(&diff);
      break;
    }
  }

  /* OOM sweep across cdd_c_meta_diff_to_sql */
  c2s_oom_countdown = -1;
  ASSERT_EQ(C_ORM_OK, cdd_c_meta_diff(&m_parent, &m_child, &diff));
  for (step = 0; step < 8; ++step) {
    c2s_oom_countdown = step;
    up_sql = NULL;
    down_sql = NULL;
    if (cdd_c_meta_diff_to_sql("child_item", &diff, C_TO_SQL_DIALECT_POSTGRESQL,
                               &up_sql, &down_sql) == C_ORM_OK) {
      (void)c_orm_system_free(up_sql);
      (void)c_orm_system_free(down_sql);
      break;
    }
  }
  cdd_c_meta_diff_free(&diff);

  /* OOM sweep across cdd_c_meta_topological_sort */
  c2s_oom_countdown = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            cdd_c_meta_topological_sort(schemas, 2, sorted));
  c2s_oom_countdown = 1;
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            cdd_c_meta_topological_sort(schemas, 2, sorted));

  /* OOM sweep across inspection & index emitters */
  c2s_oom_countdown = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, cdd_c_get_schema_inspection_query(
                                    C_TO_SQL_DIALECT_SQLITE, "t", &out_sql));
  c2s_oom_countdown = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            cdd_c_emit_create_index("t", "idx", "c", 1, &out_sql));
  c2s_oom_countdown = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, cdd_c_emit_drop_index("idx", &out_sql));

  c2s_oom_countdown = -1;
  c_orm_set_allocators(old_malloc, old_realloc, c_orm_free);
  PASS();
}

/**
 * @brief Test edge cases in type mappings and schema diffing.
 * @return GREATEST test result.
 */
TEST test_c_to_sql_edge_cases(void) {
  struct StructField fields[2];
  struct StructFields sf;
  cdd_c_prop_meta_t p_old[2];
  cdd_c_prop_meta_t p_new[2];
  cdd_c_meta_t m_old;
  cdd_c_meta_t m_new;
  cdd_c_meta_diff_t diff;
  char buf[4096];
  FILE *fp;
  char *up;
  char *down;
  c_orm_error_t rc;

  up = NULL;
  down = NULL;
  fp = NULL;

  /* C Type fallback and struct mapping edges */
  memset(fields, 0, sizeof(fields));
  C_ORM_STRCPY(fields[0].name, sizeof(fields[0].name), "unknown_field");
  C_ORM_STRCPY(fields[0].type, sizeof(fields[0].type), "unknown_type");
  C_ORM_STRCPY(fields[1].name, sizeof(fields[1].name), "id");
  C_ORM_STRCPY(fields[1].type, sizeof(fields[1].type), "int");
  C_ORM_STRCPY(fields[1].description, sizeof(fields[1].description), "@pk");
  sf.size = 2;
  sf.fields = fields;

  C_ORM_TMPFILE(&fp);
  ASSERT(fp != NULL);
  rc =
      write_struct_to_sql_create_table(fp, "test", &sf, C_TO_SQL_DIALECT_MYSQL);
  ASSERT_EQ(C_ORM_OK, rc);
  rewind(fp);
  memset(buf, 0, sizeof(buf));
  fread(buf, 1, sizeof(buf) - 1, fp);
  fclose(fp);
  fp = NULL;
  ASSERT(strstr(buf, "unknown_field BLOB") != NULL);

  /* Dialect type maps */
  C_ORM_TMPFILE(&fp);
  ASSERT(fp != NULL);
  C_ORM_STRCPY(fields[0].type, sizeof(fields[0].type), "float");
  C_ORM_STRCPY(fields[1].type, sizeof(fields[1].type), "bool");
  rc =
      write_struct_to_sql_create_table(fp, "test", &sf, C_TO_SQL_DIALECT_MYSQL);
  ASSERT_EQ(C_ORM_OK, rc);
  fclose(fp);
  fp = NULL;

  C_ORM_TMPFILE(&fp);
  ASSERT(fp != NULL);
  C_ORM_STRCPY(fields[0].type, sizeof(fields[0].type), "double");
  C_ORM_STRCPY(fields[1].type, sizeof(fields[1].type), "bool");
  rc =
      write_struct_to_sql_create_table(fp, "test", &sf, C_TO_SQL_DIALECT_MYSQL);
  ASSERT_EQ(C_ORM_OK, rc);
  rewind(fp);
  memset(buf, 0, sizeof(buf));
  fread(buf, 1, sizeof(buf) - 1, fp);
  fclose(fp);
  fp = NULL;
  ASSERT(strstr(buf, "unknown_field DOUBLE") != NULL);
  ASSERT(strstr(buf, "id TINYINT(1)") != NULL);

  /* Missing diff / Alter / Drop coverage */
  memset(p_old, 0, sizeof(p_old));
  memset(p_new, 0, sizeof(p_new));

  p_old[0].name = "to_drop";
  p_old[0].type = "int";
  p_old[1].name = "to_alter";
  p_old[1].type = "int";
  m_old.name = "diff_table";
  m_old.props = p_old;
  m_old.num_props = 2;

  p_new[0].name = "to_alter";
  p_new[0].type = "string";
  m_new.name = "diff_table";
  m_new.props = p_new;
  m_new.num_props = 1;

  rc = cdd_c_meta_diff(&m_old, &m_new, &diff);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ(1, diff.num_dropped);
  ASSERT_EQ(1, diff.num_altered);

  rc = cdd_c_meta_diff_to_sql("diff_table", &diff, C_TO_SQL_DIALECT_SQLITE, &up,
                              &down);
  ASSERT_EQ(C_ORM_OK, rc);
  (void)c_orm_system_free(up);
  (void)c_orm_system_free(down);
  up = NULL;
  down = NULL;

  rc = cdd_c_meta_diff_to_sql("diff_table", &diff, C_TO_SQL_DIALECT_MYSQL, &up,
                              &down);
  ASSERT_EQ(C_ORM_OK, rc);
  (void)c_orm_system_free(up);
  (void)c_orm_system_free(down);
  up = NULL;
  down = NULL;

  cdd_c_meta_diff_free(&diff);

  /* NULL check out_sql */
  rc = write_struct_to_sql_create_table(NULL, "test", &sf,
                                        C_TO_SQL_DIALECT_SQLITE);
  ASSERT(rc != C_ORM_OK);

  PASS();
}

/**
 * @brief Test additional branch coverage for table dialect emission and sort.
 * @return GREATEST test result.
 */
TEST test_c_to_sql_additional_branches(void) {
  struct StructField fields[6];
  struct StructFields sf;
  cdd_c_prop_meta_t p_props[3];
  cdd_c_meta_t m_meta;
  char *out_sql;
  FILE *fp;
  char buf[2048];
  c_orm_error_t rc;

  out_sql = NULL;
  fp = NULL;

  /* 1. Primary key name variations: "Id", "ID", and descriptions */
  memset(fields, 0, sizeof(fields));
  C_ORM_STRCPY(fields[0].name, sizeof(fields[0].name), "Id");
  C_ORM_STRCPY(fields[0].type, sizeof(fields[0].type), "char*");
  C_ORM_STRCPY(fields[0].description, sizeof(fields[0].description),
               "@primary_key");

  C_ORM_STRCPY(fields[1].name, sizeof(fields[1].name), "ID");
  C_ORM_STRCPY(fields[1].type, sizeof(fields[1].type), "number");
  C_ORM_STRCPY(fields[1].description, sizeof(fields[1].description),
               "@required");

  C_ORM_STRCPY(fields[2].name, sizeof(fields[2].name), "long_field_not_fk");
  C_ORM_STRCPY(fields[2].type, sizeof(fields[2].type), "double");

  C_ORM_STRCPY(fields[3].name, sizeof(fields[3].name), "a");
  C_ORM_STRCPY(fields[3].type, sizeof(fields[3].type), "bool");

  C_ORM_STRCPY(fields[4].name, sizeof(fields[4].name), "blob_field");
  C_ORM_STRCPY(fields[4].type, sizeof(fields[4].type), "custom_type");

  C_ORM_STRCPY(fields[5].name, sizeof(fields[5].name), "target_id");
  C_ORM_STRCPY(fields[5].type, sizeof(fields[5].type), "string");

  sf.size = 6;
  sf.fields = fields;

  /* SQLite: Id is char* (TEXT), so AUTOINCREMENT is NOT emitted! */
  C_ORM_TMPFILE(&fp);
  ASSERT(fp != NULL);
  rc = write_struct_to_sql_create_table(fp, "my_table", &sf,
                                        C_TO_SQL_DIALECT_SQLITE);
  ASSERT_EQ(C_ORM_OK, rc);
  rewind(fp);
  memset(buf, 0, sizeof(buf));
  fread(buf, 1, sizeof(buf) - 1, fp);
  fclose(fp);
  fp = NULL;
  ASSERT(strstr(buf, "Id TEXT PRIMARY KEY") != NULL);
  ASSERT(strstr(buf, "ID REAL PRIMARY KEY NOT NULL") != NULL);

  /* PostgreSQL dialect check for double, number, bool, string */
  C_ORM_TMPFILE(&fp);
  ASSERT(fp != NULL);
  rc = write_struct_to_sql_create_table(fp, "my_table_pg", &sf,
                                        C_TO_SQL_DIALECT_POSTGRESQL);
  ASSERT_EQ(C_ORM_OK, rc);
  rewind(fp);
  memset(buf, 0, sizeof(buf));
  fread(buf, 1, sizeof(buf) - 1, fp);
  fclose(fp);
  fp = NULL;
  ASSERT(strstr(buf, "ID DOUBLE PRECISION PRIMARY KEY NOT NULL") != NULL);
  ASSERT(strstr(buf, "a BOOLEAN") != NULL);
  ASSERT(strstr(buf, "target_id TEXT REFERENCES target(id)") != NULL);

  /* 2. cdd_c_meta_to_sql_create_table SQLite with non-INTEGER id */
  memset(p_props, 0, sizeof(p_props));
  p_props[0].name = "id";
  p_props[0].type = "char*";
  p_props[1].name = "short";
  p_props[1].type = "int";
  p_props[2].name = "a";
  p_props[2].type = "int";

  memset(&m_meta, 0, sizeof(m_meta));
  m_meta.name = "table_meta";
  m_meta.props = p_props;
  m_meta.num_props = 3;

  rc = cdd_c_meta_to_sql_create_table(&m_meta, C_TO_SQL_DIALECT_SQLITE,
                                      &out_sql);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT(strstr(out_sql, "id TEXT PRIMARY KEY") != NULL);
  ASSERT(strstr(out_sql, "AUTOINCREMENT") == NULL);
  (void)c_orm_system_free(out_sql);
  out_sql = NULL;

  /* 3. Multi-level / multi-dependency topological sort */
  {
    cdd_c_prop_meta_t p_u[1];
    cdd_c_prop_meta_t p_c[1];
    cdd_c_prop_meta_t p_p[5];
    cdd_c_prop_meta_t p_cm[2];
    cdd_c_meta_t m_u;
    cdd_c_meta_t m_c;
    cdd_c_meta_t m_p;
    cdd_c_meta_t m_cm;
    const cdd_c_meta_t *all_schemas[4];
    const cdd_c_meta_t *sorted[4];

    memset(&m_u, 0, sizeof(m_u));
    memset(&m_c, 0, sizeof(m_c));
    memset(&m_p, 0, sizeof(m_p));
    memset(&m_cm, 0, sizeof(m_cm));
    memset(p_u, 0, sizeof(p_u));
    memset(p_c, 0, sizeof(p_c));
    memset(p_p, 0, sizeof(p_p));
    memset(p_cm, 0, sizeof(p_cm));

    p_u[0].name = "id";
    p_u[0].type = "int";
    m_u.name = "user";
    m_u.props = p_u;
    m_u.num_props = 1;

    p_c[0].name = "id";
    p_c[0].type = "int";
    m_c.name = "category";
    m_c.props = p_c;
    m_c.num_props = 1;

    p_p[0].name = "id";
    p_p[0].type = "int";
    p_p[1].name = "user_id";
    p_p[1].type = "int";
    p_p[2].name = "category_id";
    p_p[2].type = "int";
    p_p[3].name = "title";
    p_p[3].type = "char*";
    p_p[4].name = "external_id";
    p_p[4].type = "int";
    m_p.name = "post";
    m_p.props = p_p;
    m_p.num_props = 5;

    p_cm[0].name = "id";
    p_cm[0].type = "int";
    p_cm[1].name = "post_id";
    p_cm[1].type = "int";
    m_cm.name = "comment";
    m_cm.props = p_cm;
    m_cm.num_props = 2;

    all_schemas[0] = &m_cm;
    all_schemas[1] = &m_p;
    all_schemas[2] = &m_u;
    all_schemas[3] = &m_c;

    rc = cdd_c_meta_topological_sort(all_schemas, 4, sorted);
    ASSERT_EQ(C_ORM_OK, rc);
    ASSERT(sorted[2] == &m_p);
    ASSERT(sorted[3] == &m_cm);
  }

  /* 4. c_orm_table_meta_to_cdd_meta coverage */
  {
    c_orm_column_meta_t cols[6];
    c_orm_table_meta_t tmeta;
    cdd_c_meta_t *cmeta = NULL;

    memset(cols, 0, sizeof(cols));
    cols[0].name = "id";
    cols[0].type = C_ORM_TYPE_INT32;
    cols[0].offset = 0;
    cols[0].is_nullable = false;

    cols[1].name = "big_id";
    cols[1].type = C_ORM_TYPE_INT64;
    cols[1].offset = 8;
    cols[1].is_nullable = true;

    cols[2].name = "flt";
    cols[2].type = C_ORM_TYPE_FLOAT;
    cols[2].offset = 16;

    cols[3].name = "dbl";
    cols[3].type = C_ORM_TYPE_DOUBLE;
    cols[3].offset = 24;

    cols[4].name = "flag";
    cols[4].type = C_ORM_TYPE_BOOL;
    cols[4].offset = 32;

    cols[5].name = "title";
    cols[5].type = C_ORM_TYPE_STRING;
    cols[5].offset = 40;

    memset(&tmeta, 0, sizeof(tmeta));
    tmeta.name = "items";
    tmeta.struct_size = 48;
    tmeta.num_columns = 6;
    tmeta.columns = cols;

    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_table_meta_to_cdd_meta(NULL, &cmeta));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_table_meta_to_cdd_meta(&tmeta, NULL));

    ASSERT_EQ(C_ORM_OK, c_orm_table_meta_to_cdd_meta(&tmeta, &cmeta));
    ASSERT(cmeta != NULL);
    ASSERT_STR_EQ("items", cmeta->name);
    ASSERT_EQ(6, cmeta->num_props);
    ASSERT_STR_EQ("C_ORM_TYPE_INT32", cmeta->props[0].type);
    ASSERT_STR_EQ("C_ORM_TYPE_INT64", cmeta->props[1].type);
    ASSERT_STR_EQ("C_ORM_TYPE_FLOAT", cmeta->props[2].type);
    ASSERT_STR_EQ("C_ORM_TYPE_DOUBLE", cmeta->props[3].type);
    ASSERT_STR_EQ("C_ORM_TYPE_INT32", cmeta->props[4].type);
    ASSERT_STR_EQ("C_ORM_TYPE_STRING", cmeta->props[5].type);
    /* Round-trip cdd_meta -> table_meta -> column_meta and free helpers */
    {
      c_orm_table_meta_t *rt_tmeta = NULL;
      cdd_c_column_meta_t *col_meta = NULL;
      size_t n_col_meta = 0;

      ASSERT_EQ(C_ORM_OK, c_orm_free_cdd_meta(NULL));
      ASSERT_EQ(C_ORM_OK, c_orm_free_table_meta(NULL));
      ASSERT_EQ(C_ORM_OK, c_orm_free_column_meta(NULL));
      ASSERT_EQ(C_ORM_ERROR_VALIDATION,
                c_orm_cdd_meta_to_table_meta(NULL, &rt_tmeta));
      ASSERT_EQ(C_ORM_ERROR_VALIDATION,
                c_orm_table_meta_to_column_meta(NULL, &col_meta, &n_col_meta));

      ASSERT_EQ(C_ORM_OK, c_orm_cdd_meta_to_table_meta(cmeta, &rt_tmeta));
      ASSERT(rt_tmeta != NULL);
      ASSERT_STR_EQ("items", rt_tmeta->name);
      ASSERT_EQ(6, rt_tmeta->num_columns);
      ASSERT_EQ(C_ORM_TYPE_INT32, rt_tmeta->columns[0].type);
      ASSERT_EQ(C_ORM_TYPE_INT64, rt_tmeta->columns[1].type);
      ASSERT_EQ(C_ORM_TYPE_FLOAT, rt_tmeta->columns[2].type);
      ASSERT_EQ(C_ORM_TYPE_DOUBLE, rt_tmeta->columns[3].type);

      ASSERT_EQ(C_ORM_OK, c_orm_table_meta_to_column_meta(rt_tmeta, &col_meta,
                                                          &n_col_meta));
      ASSERT_EQ(6, n_col_meta);
      ASSERT(col_meta != NULL);
      ASSERT_STR_EQ("id", col_meta[0].name);

      ASSERT_EQ(C_ORM_OK, c_orm_free_column_meta(col_meta));
      ASSERT_EQ(C_ORM_OK, c_orm_free_table_meta(rt_tmeta));
    }
    ASSERT_EQ(C_ORM_OK, c_orm_free_cdd_meta(cmeta));
    cmeta = NULL;

    /* OOM on cm alloc and cm->props alloc */
    {
      c_orm_malloc_fn_t old_malloc = c_orm_malloc;
      tmeta.num_columns = 6;
      c_orm_set_allocators(c2s_mock_malloc, c_orm_realloc, c_orm_free);
      c2s_oom_countdown = 0;
      ASSERT_EQ(C_ORM_ERROR_MEMORY,
                c_orm_table_meta_to_cdd_meta(&tmeta, &cmeta));
      c2s_oom_countdown = 1;
      ASSERT_EQ(C_ORM_ERROR_MEMORY,
                c_orm_table_meta_to_cdd_meta(&tmeta, &cmeta));
      c2s_oom_countdown = -1;
      c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);
    }

    /* Empty columns table */
    tmeta.num_columns = 0;
    ASSERT_EQ(C_ORM_OK, c_orm_table_meta_to_cdd_meta(&tmeta, &cmeta));
    ASSERT(cmeta != NULL);
    ASSERT_EQ(NULL, cmeta->props);
    ASSERT_EQ(C_ORM_OK, c_orm_free_cdd_meta(cmeta));
    cmeta = NULL;
  }

  PASS();
}

/**
 * @brief Exhaustive coverage of Db2/ODBC DDL, type mappings, metadata
 * conversions, string-builder append countdowns, and Safe CRT failures.
 * @return GREATEST test result.
 */
TEST test_c_to_sql_exhaustive_db2_odbc_and_oom(void) {
  static const char *const all_c_types[] = {"int64_t",
                                            "c_orm_int64_t",
                                            "C_ORM_TYPE_INT64",
                                            "bigint",
                                            "integer",
                                            "int",
                                            "int32_t",
                                            "size_t",
                                            "C_ORM_TYPE_INT32",
                                            "DECIMAL(12, 2)",
                                            "NUMERIC(10, 4)",
                                            "DECFLOAT(16)",
                                            "GRAPHIC(8)",
                                            "VARGRAPHIC(16)",
                                            "CHAR(4)",
                                            "VARCHAR(20)",
                                            "c_orm_decimal_t",
                                            "decimal",
                                            "C_ORM_TYPE_DECIMAL",
                                            "decfloat",
                                            "C_ORM_TYPE_DECFLOAT",
                                            "char_fixed",
                                            "C_ORM_TYPE_CHAR_FIXED",
                                            "graphic",
                                            "C_ORM_TYPE_GRAPHIC",
                                            "vargraphic",
                                            "C_ORM_TYPE_VARGRAPHIC",
                                            "binary_fixed",
                                            "C_ORM_TYPE_BINARY_FIXED",
                                            "varbinary",
                                            "C_ORM_TYPE_VARBINARY",
                                            "clob",
                                            "CLOB",
                                            "date",
                                            "C_ORM_TYPE_DATE",
                                            "time",
                                            "C_ORM_TYPE_TIME",
                                            "timestamp",
                                            "C_ORM_TYPE_TIMESTAMP",
                                            "string",
                                            "char*",
                                            "const char*",
                                            "C_ORM_TYPE_STRING",
                                            "number",
                                            "double",
                                            "float",
                                            "C_ORM_TYPE_FLOAT",
                                            "C_ORM_TYPE_DOUBLE",
                                            "boolean",
                                            "bool",
                                            "C_ORM_TYPE_BOOL",
                                            "raw_blob_custom",
                                            NULL};
  static const c_to_sql_dialect_t all_dialects[5] = {
      C_TO_SQL_DIALECT_SQLITE, C_TO_SQL_DIALECT_POSTGRESQL,
      C_TO_SQL_DIALECT_MYSQL, C_TO_SQL_DIALECT_DB2, C_TO_SQL_DIALECT_ODBC};
  size_t d;
  size_t t;
  int step;
  char *out_sql = NULL;
  char *up_sql = NULL;
  char *down_sql = NULL;
  c_orm_error_t rc;

  /* 1. Exercise every c_type_to_sql_type branch across all 5 dialects */
  for (d = 0; d < 5; ++d) {
    cdd_c_prop_meta_t props[56];
    cdd_c_meta_t meta;
    char names[56][16];

    memset(props, 0, sizeof(props));
    memset(&meta, 0, sizeof(meta));
    for (t = 0; all_c_types[t] != NULL; ++t) {
      ASSERT_EQ(C_ORM_OK, C_ORM_SPRINTF(names[t], sizeof(names[t]), "c%u",
                                        (unsigned int)t));
      props[t].name = (t == 0) ? "id" : names[t];
      props[t].type = all_c_types[t];
    }
    /* Add a second PK test where PK is int32_t and char* on DB2 */
    meta.name = "all_types_tbl";
    meta.props = props;
    meta.num_props = t;

    ASSERT_EQ(C_ORM_OK,
              cdd_c_meta_to_sql_create_table(&meta, all_dialects[d], &out_sql));
    ASSERT(out_sql != NULL);
    (void)c_orm_system_free(out_sql);
    out_sql = NULL;

    /* DB2 with INTEGER PK and VARCHAR PK in cdd_c_meta_to_sql_create_table */
    props[0].type = "int32_t";
    ASSERT_EQ(C_ORM_OK,
              cdd_c_meta_to_sql_create_table(&meta, all_dialects[d], &out_sql));
    (void)c_orm_system_free(out_sql);
    out_sql = NULL;

    props[0].type = "char*";
    ASSERT_EQ(C_ORM_OK,
              cdd_c_meta_to_sql_create_table(&meta, all_dialects[d], &out_sql));
    (void)c_orm_system_free(out_sql);
    out_sql = NULL;
  }

  /* NULL prop type error in c_type_to_sql_type + meta->name == NULL */
  {
    cdd_c_prop_meta_t bad_p[1];
    cdd_c_meta_t bad_m;
    cdd_c_meta_diff_t bad_diff;

    memset(bad_p, 0, sizeof(bad_p));
    memset(&bad_m, 0, sizeof(bad_m));
    bad_m.name = NULL;
    ASSERT_EQ(
        C_ORM_ERROR_NULL_POINTER,
        cdd_c_meta_to_sql_create_table(&bad_m, C_TO_SQL_DIALECT_DB2, &out_sql));

    bad_p[0].name = "col1";
    bad_p[0].type = NULL;
    bad_m.name = "tbl";
    bad_m.props = bad_p;
    bad_m.num_props = 1;
    ASSERT_EQ(
        C_ORM_ERROR_NULL_POINTER,
        cdd_c_meta_to_sql_create_table(&bad_m, C_TO_SQL_DIALECT_DB2, &out_sql));

    memset(&bad_diff, 0, sizeof(bad_diff));
    bad_diff.added_props = bad_p;
    bad_diff.num_added = 1;
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              cdd_c_meta_diff_to_sql("tbl", &bad_diff, C_TO_SQL_DIALECT_DB2,
                                     &up_sql, &down_sql));

    memset(&bad_diff, 0, sizeof(bad_diff));
    bad_diff.altered_props = bad_p;
    bad_diff.num_altered = 1;
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              cdd_c_meta_diff_to_sql("tbl", &bad_diff, C_TO_SQL_DIALECT_DB2,
                                     &up_sql, &down_sql));
  }

  /* 2. write_struct_to_sql_create_table on DB2 with INTEGER, BIGINT, and TEXT
   * PKs */
  {
    struct StructField sfields[3];
    struct StructFields sf;
    FILE *fp = NULL;

    memset(sfields, 0, sizeof(sfields));
    C_ORM_STRCPY(sfields[0].name, sizeof(sfields[0].name), "id");
    C_ORM_STRCPY(sfields[0].type, sizeof(sfields[0].type), "int32_t");
    C_ORM_STRCPY(sfields[1].name, sizeof(sfields[1].name), "ID");
    C_ORM_STRCPY(sfields[1].type, sizeof(sfields[1].type), "int64_t");
    C_ORM_STRCPY(sfields[2].name, sizeof(sfields[2].name), "Id");
    C_ORM_STRCPY(sfields[2].type, sizeof(sfields[2].type), "char*");
    sf.size = 3;
    sf.fields = sfields;

    C_ORM_TMPFILE(&fp);
    ASSERT(fp != NULL);
    ASSERT_EQ(C_ORM_OK, write_struct_to_sql_create_table(
                            fp, "db2_struct_t", &sf, C_TO_SQL_DIALECT_DB2));
    fclose(fp);
  }

  /* 3. cdd_c_meta_to_sql_create_table append countdown + get_fail + sb_append_n
   * OOM */
  {
    cdd_c_prop_meta_t p[2];
    cdd_c_meta_t m;

    memset(p, 0, sizeof(p));
    memset(&m, 0, sizeof(m));
    p[0].name = "id";
    p[0].type = "int32_t";
    p[1].name = "org_id";
    p[1].type = "int64_t";
    m.name = "emp";
    m.props = p;
    m.num_props = 2;

    for (d = 0; d < 5; ++d) {
      for (step = 0; step < 25; ++step) {
        c_orm_mock_string_builder_append_countdown = step;
        out_sql = NULL;
        rc = cdd_c_meta_to_sql_create_table(&m, all_dialects[d], &out_sql);
        c_orm_mock_string_builder_append_countdown = -1;
        if (rc == C_ORM_OK) {
          (void)c_orm_system_free(out_sql);
          break;
        }
      }
    }
    c_orm_mock_string_builder_get_fail = 1;
    ASSERT_NEQ(C_ORM_OK, cdd_c_meta_to_sql_create_table(
                             &m, C_TO_SQL_DIALECT_SQLITE, &out_sql));
    c_orm_mock_string_builder_get_fail = 0;
  }

  /* 4. cdd_c_meta_diff_to_sql across all 5 dialects + append countdowns +
   * down_sb finish failure */
  {
    cdd_c_prop_meta_t p_old[2];
    cdd_c_prop_meta_t p_new[2];
    cdd_c_meta_t m_old;
    cdd_c_meta_t m_new;
    cdd_c_meta_diff_t diff;
    c_orm_malloc_fn_t old_malloc = c_orm_malloc;

    memset(p_old, 0, sizeof(p_old));
    memset(p_new, 0, sizeof(p_new));
    memset(&m_old, 0, sizeof(m_old));
    memset(&m_new, 0, sizeof(m_new));

    p_old[0].name = "dropped_col";
    p_old[0].type = "int";
    p_old[1].name = "altered_col";
    p_old[1].type = "int";
    m_old.name = "t";
    m_old.props = p_old;
    m_old.num_props = 2;

    p_new[0].name = "altered_col";
    p_new[0].type = "c_orm_decimal_t";
    p_new[1].name = "added_col";
    p_new[1].type = "string";
    m_new.name = "t";
    m_new.props = p_new;
    m_new.num_props = 2;

    ASSERT_EQ(C_ORM_OK, cdd_c_meta_diff(&m_old, &m_new, &diff));

    for (d = 0; d < 5; ++d) {
      for (step = 0; step < 120; ++step) {
        c_orm_mock_string_builder_append_countdown = step;
        up_sql = NULL;
        down_sql = NULL;
        rc = cdd_c_meta_diff_to_sql("my_tbl", &diff, all_dialects[d], &up_sql,
                                    &down_sql);
        c_orm_mock_string_builder_append_countdown = -1;
        if (rc == C_ORM_OK) {
          (void)c_orm_system_free(up_sql);
          (void)c_orm_system_free(down_sql);
          break;
        }
      }
    }

    cdd_c_meta_diff_free(&diff);

    /* Empty diff OOM sweep hits up_sb finish & down_sb finish cleanly */
    memset(&diff, 0, sizeof(diff));
    c_orm_set_allocators(c2s_mock_malloc, c2s_mock_realloc, c_orm_free);
    for (step = 0; step < 10; ++step) {
      c2s_oom_countdown = step;
      up_sql = NULL;
      down_sql = NULL;
      rc = cdd_c_meta_diff_to_sql("my_tbl", &diff, C_TO_SQL_DIALECT_DB2,
                                  &up_sql, &down_sql);
      c2s_oom_countdown = -1;
      if (rc == C_ORM_OK) {
        (void)c_orm_system_free(up_sql);
        (void)c_orm_system_free(down_sql);
        break;
      }
    }
    c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);

    /* Topological sort prefix superstring ("users" vs "user_id") and 7-char
     * non-_id field ("created" when u="user") */
    {
      cdd_c_prop_meta_t pu[1];
      cdd_c_prop_meta_t pus[1];
      cdd_c_prop_meta_t pp[3];
      cdd_c_meta_t mu;
      cdd_c_meta_t mus;
      cdd_c_meta_t mp;
      const cdd_c_meta_t *sch[3];
      const cdd_c_meta_t *srt[3];

      memset(pu, 0, sizeof(pu));
      memset(pus, 0, sizeof(pus));
      memset(pp, 0, sizeof(pp));
      memset(&mu, 0, sizeof(mu));
      memset(&mus, 0, sizeof(mus));
      memset(&mp, 0, sizeof(mp));

      pu[0].name = "id";
      pu[0].type = "int";
      mu.name = "user";
      mu.props = pu;
      mu.num_props = 1;

      pus[0].name = "id";
      pus[0].type = "int";
      mus.name = "users";
      mus.props = pus;
      mus.num_props = 1;

      pp[0].name = "id";
      pp[0].type = "int";
      pp[1].name = "created"; /* len == strlen("user") + 3 == 7, not "_id" */
      pp[1].type = "string";
      pp[2].name = "user_id";
      pp[2].type = "int";
      mp.name = "post";
      mp.props = pp;
      mp.num_props = 3;

      sch[0] = &mus;
      sch[1] = &mp;
      sch[2] = &mu;
      ASSERT_EQ(C_ORM_OK, cdd_c_meta_topological_sort(sch, 3, srt));
    }
  }

  /* 5. Schema inspection queries (LUW Db2, ODBC, z/OS) + append countdowns */
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            cdd_c_get_zos_schema_inspection_query(NULL, &out_sql));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            cdd_c_get_zos_schema_inspection_query("T", NULL));
  {
    c_orm_malloc_fn_t old_malloc = c_orm_malloc;
    c_orm_set_allocators(c2s_mock_malloc, c_orm_realloc, c_orm_free);
    c2s_oom_countdown = 0;
    ASSERT_EQ(C_ORM_ERROR_MEMORY,
              cdd_c_get_zos_schema_inspection_query("T", &out_sql));
    c2s_oom_countdown = -1;
    c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);
  }
  for (step = 0; step < 10; ++step) {
    c_orm_mock_string_builder_append_countdown = step;
    out_sql = NULL;
    rc = cdd_c_get_zos_schema_inspection_query("ACCOUNTS", &out_sql);
    c_orm_mock_string_builder_append_countdown = -1;
    if (rc == C_ORM_OK) {
      ASSERT(strstr(out_sql, "SYSIBM.SYSCOLUMNS") != NULL);
      (void)c_orm_system_free(out_sql);
      break;
    }
  }

  for (d = 0; d < 5; ++d) {
    for (step = 0; step < 10; ++step) {
      c_orm_mock_string_builder_append_countdown = step;
      out_sql = NULL;
      rc = cdd_c_get_schema_inspection_query(all_dialects[d], "ACCOUNTS",
                                             &out_sql);
      c_orm_mock_string_builder_append_countdown = -1;
      if (rc == C_ORM_OK) {
        (void)c_orm_system_free(out_sql);
        break;
      }
    }
  }

  /* 6. Index emitters: non-unique + append countdowns */
  for (step = 0; step < 10; ++step) {
    c_orm_mock_string_builder_append_countdown = step;
    out_sql = NULL;
    rc = cdd_c_emit_create_index("tbl", "idx_a", "col_a", 0, &out_sql);
    c_orm_mock_string_builder_append_countdown = -1;
    if (rc == C_ORM_OK) {
      ASSERT(strstr(out_sql, "CREATE INDEX idx_a") != NULL);
      (void)c_orm_system_free(out_sql);
      break;
    }
  }
  for (step = 0; step < 6; ++step) {
    c_orm_mock_string_builder_append_countdown = step;
    out_sql = NULL;
    rc = cdd_c_emit_drop_index("idx_a", &out_sql);
    c_orm_mock_string_builder_append_countdown = -1;
    if (rc == C_ORM_OK) {
      (void)c_orm_system_free(out_sql);
      break;
    }
  }

  /* 7. c_orm_table_meta_to_sql_create_table across all types, dialects,
   * Safe CRT failures, and string_builder append failures */
  {
    c_orm_column_meta_t cols[22];
    c_orm_table_meta_t tmeta;
    c_orm_malloc_fn_t old_malloc = c_orm_malloc;

    memset(cols, 0, sizeof(cols));
    memset(&tmeta, 0, sizeof(tmeta));

    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_table_meta_to_sql_create_table(NULL, C_TO_SQL_DIALECT_DB2,
                                                   &out_sql));
    tmeta.name = NULL;
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_table_meta_to_sql_create_table(&tmeta, C_TO_SQL_DIALECT_DB2,
                                                   &out_sql));
    tmeta.name = "ledger";
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_table_meta_to_sql_create_table(&tmeta, C_TO_SQL_DIALECT_DB2,
                                                   NULL));
    tmeta.num_columns = 0;
    tmeta.columns = cols;
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_table_meta_to_sql_create_table(&tmeta, C_TO_SQL_DIALECT_DB2,
                                                   &out_sql));
    tmeta.num_columns = 1;
    tmeta.columns = NULL;
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_table_meta_to_sql_create_table(&tmeta, C_TO_SQL_DIALECT_DB2,
                                                   &out_sql));

    cols[0].name = "id";
    cols[0].type = C_ORM_TYPE_INT32;
    cols[0].is_pk = true;
    cols[1].name = "id64";
    cols[1].type = C_ORM_TYPE_INT64;
    cols[1].is_pk = true;
    cols[2].name = "pk_str";
    cols[2].type = C_ORM_TYPE_STRING;
    cols[2].is_pk = true;
    cols[3].name = "b";
    cols[3].type = C_ORM_TYPE_BOOL;
    cols[3].is_nullable = false;
    cols[4].name = "f";
    cols[4].type = C_ORM_TYPE_FLOAT;
    cols[4].is_nullable = true;
    cols[5].name = "dbl";
    cols[5].type = C_ORM_TYPE_DOUBLE;
    cols[5].is_nullable = true;
    cols[6].name = "dec_def";
    cols[6].type = C_ORM_TYPE_DECIMAL;
    cols[6].precision = 0;
    cols[6].scale = 2;
    cols[6].is_nullable = true;
    cols[7].name = "dec_15";
    cols[7].type = C_ORM_TYPE_DECIMAL;
    cols[7].precision = 15;
    cols[7].scale = 4;
    cols[7].is_nullable = false;
    cols[8].name = "df_16";
    cols[8].type = C_ORM_TYPE_DECFLOAT;
    cols[8].precision = 16;
    cols[8].is_nullable = true;
    cols[9].name = "df_34";
    cols[9].type = C_ORM_TYPE_DECFLOAT;
    cols[9].precision = 34;
    cols[9].is_nullable = true;
    cols[10].name = "df_0";
    cols[10].type = C_ORM_TYPE_DECFLOAT;
    cols[10].precision = 0;
    cols[10].is_nullable = true;
    cols[11].name = "cf_0";
    cols[11].type = C_ORM_TYPE_CHAR_FIXED;
    cols[11].fixed_length = 0;
    cols[11].is_nullable = true;
    cols[12].name = "cf_10";
    cols[12].type = C_ORM_TYPE_CHAR_FIXED;
    cols[12].fixed_length = 10;
    cols[12].is_nullable = true;
    cols[13].name = "gr_12";
    cols[13].type = C_ORM_TYPE_GRAPHIC;
    cols[13].fixed_length = 12;
    cols[13].is_nullable = true;
    cols[14].name = "vg_0";
    cols[14].type = C_ORM_TYPE_VARGRAPHIC;
    cols[14].fixed_length = 0;
    cols[14].is_nullable = true;
    cols[15].name = "vg_40";
    cols[15].type = C_ORM_TYPE_VARGRAPHIC;
    cols[15].fixed_length = 40;
    cols[15].is_nullable = true;
    cols[16].name = "bf_0";
    cols[16].type = C_ORM_TYPE_BINARY_FIXED;
    cols[16].fixed_length = 0;
    cols[16].is_nullable = true;
    cols[17].name = "bf_8";
    cols[17].type = C_ORM_TYPE_BINARY_FIXED;
    cols[17].fixed_length = 8;
    cols[17].is_nullable = true;
    cols[18].name = "vb_0";
    cols[18].type = C_ORM_TYPE_VARBINARY;
    cols[18].fixed_length = 0;
    cols[18].is_nullable = true;
    cols[19].name = "vb_64";
    cols[19].type = C_ORM_TYPE_VARBINARY;
    cols[19].fixed_length = 64;
    cols[19].is_nullable = true;
    cols[20].name = "str_len";
    cols[20].type = C_ORM_TYPE_STRING;
    cols[20].fixed_length = 80;
    cols[20].is_nullable = true;
    cols[21].name = "blb";
    cols[21].type = C_ORM_TYPE_BLOB;
    cols[21].is_nullable = true;

    tmeta.columns = cols;
    tmeta.num_columns = 22;

    c_orm_set_allocators(c2s_mock_malloc, c_orm_realloc, c_orm_free);
    c2s_oom_countdown = 0;
    ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_table_meta_to_sql_create_table(
                                      &tmeta, C_TO_SQL_DIALECT_DB2, &out_sql));
    c2s_oom_countdown = -1;
    c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);

    for (d = 0; d < 5; ++d) {
      for (step = 0; step < 18; ++step) {
        c_orm_mock_safe_crt_countdown = step;
        out_sql = NULL;
        rc = c_orm_table_meta_to_sql_create_table(&tmeta, all_dialects[d],
                                                  &out_sql);
        c_orm_mock_safe_crt_countdown = -1;
        if (rc == C_ORM_OK) {
          (void)c_orm_system_free(out_sql);
          break;
        }
      }
      for (step = 0; step < 140; ++step) {
        c_orm_mock_string_builder_append_countdown = step;
        out_sql = NULL;
        rc = c_orm_table_meta_to_sql_create_table(&tmeta, all_dialects[d],
                                                  &out_sql);
        c_orm_mock_string_builder_append_countdown = -1;
        if (rc == C_ORM_OK) {
          (void)c_orm_system_free(out_sql);
          break;
        }
      }
    }

    /* Also cover DATE, TIME, TIMESTAMP in c_orm_table_meta_to_sql_create_table
     * & c_orm_table_meta_to_cdd_meta -> c_orm_cdd_meta_to_table_meta */
    {
      c_orm_column_meta_t all_enum_cols[16];
      c_orm_table_meta_t emeta;
      cdd_c_meta_t *cmeta = NULL;
      c_orm_table_meta_t *rt_meta = NULL;
      static const c_orm_type_t enum_types[16] = {
          C_ORM_TYPE_INT32,       C_ORM_TYPE_INT64,   C_ORM_TYPE_FLOAT,
          C_ORM_TYPE_DOUBLE,      C_ORM_TYPE_BOOL,    C_ORM_TYPE_STRING,
          C_ORM_TYPE_BLOB,        C_ORM_TYPE_DATE,    C_ORM_TYPE_TIME,
          C_ORM_TYPE_TIMESTAMP,   C_ORM_TYPE_DECIMAL, C_ORM_TYPE_DECFLOAT,
          C_ORM_TYPE_CHAR_FIXED,  C_ORM_TYPE_GRAPHIC, C_ORM_TYPE_VARGRAPHIC,
          C_ORM_TYPE_BINARY_FIXED};
      size_t k;

      memset(all_enum_cols, 0, sizeof(all_enum_cols));
      for (k = 0; k < 16; ++k) {
        all_enum_cols[k].name = (k == 0) ? "id" : "col";
        all_enum_cols[k].type = enum_types[k];
        all_enum_cols[k].is_secure = (k % 2 == 0) ? true : false;
        all_enum_cols[k].is_nullable = true;
      }
      memset(&emeta, 0, sizeof(emeta));
      emeta.name = "enum_tbl";
      emeta.struct_size = 128;
      emeta.columns = all_enum_cols;
      emeta.num_columns = 16;

      ASSERT_EQ(C_ORM_OK, c_orm_table_meta_to_sql_create_table(
                              &emeta, C_TO_SQL_DIALECT_DB2, &out_sql));
      (void)c_orm_system_free(out_sql);

      all_enum_cols[15].type = C_ORM_TYPE_VARBINARY;
      ASSERT_EQ(C_ORM_OK, c_orm_table_meta_to_cdd_meta(&emeta, &cmeta));
      ASSERT_EQ(C_ORM_OK, c_orm_cdd_meta_to_table_meta(cmeta, &rt_meta));
      ASSERT_EQ(C_ORM_OK, c_orm_free_table_meta(rt_meta));
      ASSERT_EQ(C_ORM_OK, c_orm_free_cdd_meta(cmeta));

      /* Test map_col_type_to_cdd_str withBINARY_FIXED as well */
      all_enum_cols[15].type = C_ORM_TYPE_BINARY_FIXED;
      ASSERT_EQ(C_ORM_OK, c_orm_table_meta_to_cdd_meta(&emeta, &cmeta));
      ASSERT_EQ(C_ORM_OK, c_orm_cdd_meta_to_table_meta(cmeta, &rt_meta));
      ASSERT_EQ(C_ORM_OK, c_orm_free_table_meta(rt_meta));
      ASSERT_EQ(C_ORM_OK, c_orm_free_cdd_meta(cmeta));
    }

    /* Test all string aliases in c_orm_cdd_meta_to_table_meta + empty props +
     * OOM */
    {
      static const char *const alias_types[] = {
          NULL,         "int",          "int32_t",
          "int64_t",    "bigint",       "float",
          "double",     "bool",         "C_ORM_TYPE_BOOL",
          "blob",       "decimal",      "c_orm_decimal_t",
          "decfloat",   "char_fixed",   "graphic",
          "vargraphic", "binary_fixed", "varbinary",
          "date",       "time",         "timestamp",
          "other_str"};
      cdd_c_prop_meta_t aprops[22];
      cdd_c_meta_t ameta;
      c_orm_table_meta_t *rt = NULL;
      size_t k;

      memset(aprops, 0, sizeof(aprops));
      for (k = 0; k < 22; ++k) {
        aprops[k].name = (k == 0) ? NULL : ((k == 1) ? "id" : "field");
        aprops[k].type = alias_types[k];
        aprops[k].is_secure = (int)(k % 2);
      }
      memset(&ameta, 0, sizeof(ameta));
      ameta.name = "alias_tbl";
      ameta.size = 64;
      ameta.props = aprops;
      ameta.num_props = 22;

      ASSERT_EQ(C_ORM_ERROR_VALIDATION,
                c_orm_cdd_meta_to_table_meta(&ameta, NULL));
      ASSERT_EQ(C_ORM_OK, c_orm_cdd_meta_to_table_meta(&ameta, &rt));
      ASSERT_EQ(C_ORM_OK, c_orm_free_table_meta(rt));

      /* OOM on tm and cols in c_orm_cdd_meta_to_table_meta */
      c_orm_set_allocators(c2s_mock_malloc, c_orm_realloc, c_orm_free);
      c2s_oom_countdown = 0;
      ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_cdd_meta_to_table_meta(&ameta, &rt));
      c2s_oom_countdown = 1;
      ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_cdd_meta_to_table_meta(&ameta, &rt));
      c2s_oom_countdown = -1;
      c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);

      /* num_props == 0 */
      ameta.num_props = 0;
      ameta.props = NULL;
      ASSERT_EQ(C_ORM_OK, c_orm_cdd_meta_to_table_meta(&ameta, &rt));
      ASSERT_EQ(C_ORM_OK, c_orm_free_table_meta(rt));
    }
  }

  PASS();
}

/**
 * @brief Test suite runner for C to SQL generation.
 * @param c_to_sql_suite Suite runner function name.
 */
SUITE(c_to_sql_suite) {
  static int recursed = 0;
  RUN_TEST(test_write_struct_to_sql_create_table);
  RUN_TEST(test_cdd_c_meta_to_sql_create_table);
  RUN_TEST(test_cdd_c_meta_diff_and_sql);
  RUN_TEST(test_cdd_c_get_schema_inspection_query);
  RUN_TEST(test_cdd_c_emit_index);
  RUN_TEST(test_cdd_c_meta_topological_sort);
  RUN_TEST(test_c_to_sql_errors);
  RUN_TEST(test_cdd_c_meta_to_sql_create_table_pg);
  RUN_TEST(test_cdd_c_get_schema_inspection_query_sqlite_mysql);
  RUN_TEST(test_cdd_c_meta_diff_sqlite);
  RUN_TEST(test_cdd_c_meta_topological_sort_cycle);
  RUN_TEST(test_c_to_sql_oom_and_long_identifiers);
  RUN_TEST(test_c_to_sql_edge_cases);
  RUN_TEST(test_c_to_sql_additional_branches);
  RUN_TEST(test_c_to_sql_exhaustive_db2_odbc_and_oom);
  if (!recursed) {
    recursed = 1;
    greatest_set_test_filter("never_match_filter");
    c_to_sql_suite();
    greatest_set_test_filter(NULL);
    recursed = 0;
  }
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
