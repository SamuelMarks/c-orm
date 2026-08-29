#if defined(__clang__) || defined(__GNUC__)
#endif
/* clang-format off */
#include "c_orm_c_to_sql.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <greatest.h>
/* clang-format on */

TEST test_write_struct_to_sql_create_table(void) {
  struct StructField fields[3];
  struct StructFields sf;
  char buf[1024];
  FILE *fp;
  int rc;

  memset(buf, 0, sizeof(buf));
  memset(fields, 0, sizeof(fields));
#if defined(_MSC_VER)
  strcpy_s(fields[0].name, sizeof(fields[0].name), "id");
#else
  strcpy(fields[0].name, "id");
#endif

#if defined(_MSC_VER)
  strcpy_s(fields[0].type, sizeof(fields[0].type), "integer");
#else
  strcpy(fields[0].type, "integer");
#endif

  fields[0].required = 1;

#if defined(_MSC_VER)
  strcpy_s(fields[1].name, sizeof(fields[1].name), "username");
#else
  strcpy(fields[1].name, "username");
#endif

#if defined(_MSC_VER)
  strcpy_s(fields[1].type, sizeof(fields[1].type), "string");
#else
  strcpy(fields[1].type, "string");
#endif

#if defined(_MSC_VER)
  strcpy_s(fields[1].description, sizeof(fields[1].description),
           "@unique @notnull");
#else
  strcpy(fields[1].description, "@unique @notnull");
#endif

#if defined(_MSC_VER)
  strcpy_s(fields[2].name, sizeof(fields[2].name), "company_id");
#else
  strcpy(fields[2].name, "company_id");
#endif

#if defined(_MSC_VER)
  strcpy_s(fields[2].type, sizeof(fields[2].type), "integer");
#else
  strcpy(fields[2].type, "integer");
#endif

  sf.size = 3;
  sf.fields = fields;

#if defined(_MSC_VER)
  tmpfile_s(&fp);
#else
  (*&fp = tmpfile(), *&fp == NULL ? 1 : 0);
#endif

  ASSERT(fp != NULL);

  rc = write_struct_to_sql_create_table(fp, "users", &sf,
                                        C_TO_SQL_DIALECT_SQLITE);
  ASSERT_EQ(0, rc);

  rewind(fp);
  fread(buf, 1, sizeof(buf) - 1, fp);
  fclose(fp);

  ASSERT(strstr(buf, "CREATE TABLE users") != NULL);
  ASSERT(strstr(buf, "id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL") != NULL);
  ASSERT(strstr(buf, "username TEXT UNIQUE NOT NULL") != NULL);
  ASSERT(strstr(buf, "company_id INTEGER REFERENCES company(id)") != NULL);

  PASS();
}

TEST test_cdd_c_meta_to_sql_create_table(void) {
  cdd_c_prop_meta_t props[2];
  cdd_c_meta_t meta;
  char *out_sql = NULL;
  int rc;

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
  ASSERT_EQ(0, rc);
  ASSERT(out_sql != NULL);
  ASSERT(strstr(out_sql, "CREATE TABLE company") != NULL);
  ASSERT(strstr(out_sql, "id INT PRIMARY KEY") != NULL);
  ASSERT(strstr(out_sql, "name VARCHAR(255)") != NULL);

  free(out_sql);
  PASS();
}

TEST test_cdd_c_meta_diff_and_sql(void) {
  cdd_c_prop_meta_t props_old[1];
  cdd_c_prop_meta_t props_new[2];
  cdd_c_meta_t old_meta, new_meta;
  cdd_c_meta_diff_t diff;
  char *up_sql = NULL, *down_sql = NULL;
  int rc;

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
  ASSERT_EQ(0, rc);
  ASSERT_EQ(1, diff.num_added);
  ASSERT_EQ(0, diff.num_dropped);
  ASSERT_EQ(0, diff.num_altered);

  rc = cdd_c_meta_diff_to_sql("test_table", &diff, C_TO_SQL_DIALECT_POSTGRESQL,
                              &up_sql, &down_sql);
  ASSERT_EQ(0, rc);
  ASSERT(up_sql != NULL);
  ASSERT(down_sql != NULL);
  ASSERT(strstr(up_sql, "ADD COLUMN description TEXT") != NULL);
  ASSERT(strstr(down_sql, "DROP COLUMN description") != NULL);

  free(up_sql);
  free(down_sql);
  cdd_c_meta_diff_free(&diff);
  PASS();
}

TEST test_cdd_c_get_schema_inspection_query(void) {
  char *query = NULL;
  int rc;

  rc = cdd_c_get_schema_inspection_query(C_TO_SQL_DIALECT_POSTGRESQL, "users",
                                         &query);
  ASSERT_EQ(0, rc);
  ASSERT(query != NULL);
  ASSERT(strstr(query, "information_schema") != NULL);
  free(query);

  PASS();
}

TEST test_cdd_c_emit_index(void) {
  char *query = NULL;
  int rc;

  rc = cdd_c_emit_create_index("users", "idx_users_email", "email", 1, &query);
  ASSERT_EQ(0, rc);
  ASSERT(query != NULL);
  ASSERT(strstr(query, "UNIQUE INDEX idx_users_email ON users (email)") !=
         NULL);
  free(query);

  rc = cdd_c_emit_drop_index("idx_users_email", &query);
  ASSERT_EQ(0, rc);
  ASSERT(query != NULL);
  ASSERT(strstr(query, "DROP INDEX idx_users_email") != NULL);
  free(query);

  PASS();
}

TEST test_cdd_c_meta_topological_sort(void) {
  cdd_c_prop_meta_t p_user[1], p_post[2];
  cdd_c_meta_t m_user, m_post;
  const cdd_c_meta_t *schemas[2];
  const cdd_c_meta_t *out_schemas[2];
  int rc;

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
  ASSERT_EQ(0, rc);
  ASSERT(out_schemas[0] == &m_user);
  ASSERT(out_schemas[1] == &m_post);

  PASS();
}

TEST test_c_to_sql_errors(void) {
  ASSERT_EQ(1, write_struct_to_sql_create_table(NULL, NULL, NULL,
                                                C_TO_SQL_DIALECT_SQLITE));
  ASSERT_EQ(
      1, cdd_c_meta_to_sql_create_table(NULL, C_TO_SQL_DIALECT_SQLITE, NULL));
  ASSERT_EQ(1, cdd_c_meta_diff(NULL, NULL, NULL));
  ASSERT_EQ(1, cdd_c_meta_diff_to_sql(NULL, NULL, C_TO_SQL_DIALECT_SQLITE, NULL,
                                      NULL));
  ASSERT_EQ(1, cdd_c_get_schema_inspection_query(C_TO_SQL_DIALECT_SQLITE, NULL,
                                                 NULL));
  ASSERT_EQ(1, cdd_c_emit_create_index(NULL, NULL, NULL, 0, NULL));
  ASSERT_EQ(1, cdd_c_emit_drop_index(NULL, NULL));
  ASSERT_EQ(1, cdd_c_meta_topological_sort(NULL, 0, NULL));
  PASS();
}

TEST test_cdd_c_meta_to_sql_create_table_pg(void) {
  cdd_c_prop_meta_t props[5];
  cdd_c_meta_t meta;
  char *out_sql = NULL;

  memset(&meta, 0, sizeof(meta));
  memset(props, 0, sizeof(props));

  props[0].name = "id";
  props[0].type = "int";
  props[1].name = "name";
  props[1].type = "char*";
  props[2].name = "score";
  props[2].type = "float";
  props[3].name = "is_active";
  props[3].type = "bool";
  props[4].name = "user_id";
  props[4].type = "int";

  meta.name = "company";
  meta.props = props;
  meta.num_props = 5;

  ASSERT_EQ(0, cdd_c_meta_to_sql_create_table(
                   &meta, C_TO_SQL_DIALECT_POSTGRESQL, &out_sql));
  ASSERT(out_sql != NULL);
  ASSERT(strstr(out_sql, "id INTEGER PRIMARY KEY") != NULL);
  ASSERT(strstr(out_sql, "name TEXT") != NULL);
  ASSERT(strstr(out_sql, "score DOUBLE PRECISION") != NULL);
  ASSERT(strstr(out_sql, "is_active BOOLEAN") != NULL);
  ASSERT(strstr(out_sql, "user_id INTEGER REFERENCES user(id)") != NULL);
  free(out_sql);

  ASSERT_EQ(0, cdd_c_meta_to_sql_create_table(&meta, C_TO_SQL_DIALECT_SQLITE,
                                              &out_sql));
  ASSERT(out_sql != NULL);
  ASSERT(strstr(out_sql, "id INTEGER PRIMARY KEY AUTOINCREMENT") != NULL);
  ASSERT(strstr(out_sql, "is_active INTEGER") != NULL);
  free(out_sql);

  PASS();
}

TEST test_cdd_c_get_schema_inspection_query_sqlite_mysql(void) {
  char *query = NULL;

  ASSERT_EQ(0, cdd_c_get_schema_inspection_query(C_TO_SQL_DIALECT_SQLITE,
                                                 "users", &query));
  ASSERT(strstr(query, "PRAGMA table_info(users);") != NULL);
  free(query);

  ASSERT_EQ(0, cdd_c_get_schema_inspection_query(C_TO_SQL_DIALECT_MYSQL,
                                                 "users", &query));
  ASSERT(strstr(query, "SHOW COLUMNS FROM users;") != NULL);
  free(query);

  /* invalid dialect */
  ASSERT_EQ(1, cdd_c_get_schema_inspection_query(999, "users", &query));

  PASS();
}

TEST test_cdd_c_meta_diff_sqlite(void) {
  cdd_c_prop_meta_t props_old[1];
  cdd_c_prop_meta_t props_new[2];
  cdd_c_meta_t old_meta, new_meta;
  cdd_c_meta_diff_t diff;
  char *up_sql = NULL, *down_sql = NULL;

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

  ASSERT_EQ(0, cdd_c_meta_diff(&old_meta, &new_meta, &diff));

  ASSERT_EQ(0,
            cdd_c_meta_diff_to_sql("test_table", &diff, C_TO_SQL_DIALECT_SQLITE,
                                   &up_sql, &down_sql));
  ASSERT(strstr(down_sql, "ALTER TABLE test_table DROP COLUMN description") !=
         NULL);
  free(up_sql);
  free(down_sql);

  cdd_c_meta_diff_free(&diff);
  cdd_c_meta_diff_free(NULL); /* coverage */

  PASS();
}

TEST test_cdd_c_meta_topological_sort_cycle(void) {
  cdd_c_prop_meta_t p_a[1], p_b[1];
  cdd_c_meta_t m_a, m_b;
  const cdd_c_meta_t *schemas[2];
  const cdd_c_meta_t *out_schemas[2];

  p_a[0].name = "b_id";
  p_a[0].type = "int";
  m_a.name = "a";
  m_a.props = p_a;
  m_a.num_props = 1;

  p_b[0].name = "a_id";
  p_b[0].type = "int";
  m_b.name = "b";
  m_b.props = p_b;
  m_b.num_props = 1;

  schemas[0] = &m_a;
  schemas[1] = &m_b;

  ASSERT_EQ(2, cdd_c_meta_topological_sort(schemas, 2, out_schemas));

  PASS();
}

TEST test_c_to_sql_edge_cases(void) {
  struct StructField fields[2];
  struct StructFields sf;
  cdd_c_prop_meta_t p_old[2], p_new[2];
  cdd_c_meta_t m_old, m_new;
  cdd_c_meta_diff_t diff;
  char buf[4096];
  FILE *fp;
  char *up = NULL, *down = NULL;

  /* C Type fallback and struct mapping edges */
  memset(fields, 0, sizeof(fields));
#if defined(_MSC_VER)
  strcpy_s(fields[0].name, sizeof(fields[0].name), "unknown_field");
#else
  strcpy(fields[0].name, "unknown_field");
#endif

#if defined(_MSC_VER)
  strcpy_s(fields[0].type, sizeof(fields[0].type), "unknown_type");
#else
  strcpy(fields[0].type, "unknown_type");
#endif

#if defined(_MSC_VER)
  strcpy_s(fields[1].name, sizeof(fields[1].name), "id");
#else
  strcpy(fields[1].name, "id");
#endif

#if defined(_MSC_VER)
  strcpy_s(fields[1].type, sizeof(fields[1].type), "int");
#else
  strcpy(fields[1].type, "int");
#endif

#if defined(_MSC_VER)
  strcpy_s(fields[1].description, sizeof(fields[1].description), "@pk");
#else
  strcpy(fields[1].description, "@pk");
#endif

  sf.size = 2;
  sf.fields = fields;

#if defined(_MSC_VER)
  tmpfile_s(&fp);
#else
  (*&fp = tmpfile(), *&fp == NULL ? 1 : 0);
#endif

  ASSERT_EQ(0, write_struct_to_sql_create_table(fp, "test", &sf,
                                                C_TO_SQL_DIALECT_MYSQL));
  rewind(fp);
  memset(buf, 0, sizeof(buf));
  fread(buf, 1, sizeof(buf) - 1, fp);
  fclose(fp);
  ASSERT(strstr(buf, "unknown_field BLOB") != NULL);

  /* Dialect type maps */
#if defined(_MSC_VER)
  tmpfile_s(&fp);
#else
  (*&fp = tmpfile(), *&fp == NULL ? 1 : 0);
#endif

#if defined(_MSC_VER)
  strcpy_s(fields[0].type, sizeof(fields[0].type), "double");
#else
  strcpy(fields[0].type, "double");
#endif

#if defined(_MSC_VER)
  strcpy_s(fields[1].type, sizeof(fields[1].type), "bool");
#else
  strcpy(fields[1].type, "bool");
#endif

  ASSERT_EQ(0, write_struct_to_sql_create_table(fp, "test", &sf,
                                                C_TO_SQL_DIALECT_MYSQL));
  rewind(fp);
  memset(buf, 0, sizeof(buf));
  fread(buf, 1, sizeof(buf) - 1, fp);
  fclose(fp);
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

  ASSERT_EQ(0, cdd_c_meta_diff(&m_old, &m_new, &diff));
  ASSERT_EQ(1, diff.num_dropped);
  ASSERT_EQ(1, diff.num_altered);

  ASSERT_EQ(0, cdd_c_meta_diff_to_sql("diff_table", &diff,
                                      C_TO_SQL_DIALECT_SQLITE, &up, &down));
  free(up);
  free(down);
  ASSERT_EQ(0, cdd_c_meta_diff_to_sql("diff_table", &diff,
                                      C_TO_SQL_DIALECT_MYSQL, &up, &down));
  free(up);
  free(down);

  cdd_c_meta_diff_free(&diff);

  /* NULL check out_sql */
  ASSERT_EQ(1, write_struct_to_sql_create_table(
                   NULL, "test", &sf,
                   C_TO_SQL_DIALECT_SQLITE)); /* Already covered, but just
                                                 ensure coverage hit */

  PASS();
}
SUITE(c_to_sql_suite) {
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
  RUN_TEST(test_c_to_sql_edge_cases);
}

#if defined(__clang__) || defined(__GNUC__)
#endif
