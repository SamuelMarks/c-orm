#if defined(__clang__) || defined(__GNUC__)
#endif
/* clang-format off */
#include "c_orm_safe_crt.h"
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
  c_orm_error_t rc;

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
  c_orm_error_t rc;

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
  c_orm_error_t rc;

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
  c_orm_error_t rc;

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
  c_orm_error_t rc;

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
  ASSERT_EQ(0, rc);
  ASSERT(out_schemas[0] == &m_user);
  ASSERT(out_schemas[1] == &m_post);

  PASS();
}

TEST test_c_to_sql_errors(void) {
  struct StructFields sf;
  cdd_c_meta_t meta;
  cdd_c_meta_diff_t diff;
  char *str = NULL;
  FILE *fp;
  const cdd_c_meta_t *schemas[1];
  const cdd_c_meta_t *out_schemas[1];

  memset(&sf, 0, sizeof(sf));
  memset(&meta, 0, sizeof(meta));
  memset(&diff, 0, sizeof(diff));

  C_ORM_TMPFILE(&fp);

  /* write_struct_to_sql_create_table NULL variations */
  ASSERT_EQ(1, write_struct_to_sql_create_table(NULL, "t", &sf,
                                                C_TO_SQL_DIALECT_SQLITE));
  ASSERT_EQ(1, write_struct_to_sql_create_table(fp, NULL, &sf,
                                                C_TO_SQL_DIALECT_SQLITE));
  ASSERT_EQ(1, write_struct_to_sql_create_table(fp, "t", NULL,
                                                C_TO_SQL_DIALECT_SQLITE));
  fclose(fp);

  /* cdd_c_meta_to_sql_create_table NULL variations */
  ASSERT_EQ(
      1, cdd_c_meta_to_sql_create_table(NULL, C_TO_SQL_DIALECT_SQLITE, &str));
  ASSERT_EQ(
      1, cdd_c_meta_to_sql_create_table(&meta, C_TO_SQL_DIALECT_SQLITE, NULL));

  /* cdd_c_meta_diff NULL variations */
  ASSERT_EQ(1, cdd_c_meta_diff(NULL, &meta, &diff));
  ASSERT_EQ(1, cdd_c_meta_diff(&meta, NULL, &diff));
  ASSERT_EQ(1, cdd_c_meta_diff(&meta, &meta, NULL));

  /* cdd_c_meta_diff_to_sql NULL variations */
  ASSERT_EQ(1, cdd_c_meta_diff_to_sql(NULL, &diff, C_TO_SQL_DIALECT_SQLITE,
                                      &str, &str));
  ASSERT_EQ(1, cdd_c_meta_diff_to_sql("t", NULL, C_TO_SQL_DIALECT_SQLITE, &str,
                                      &str));
  ASSERT_EQ(1, cdd_c_meta_diff_to_sql("t", &diff, C_TO_SQL_DIALECT_SQLITE, NULL,
                                      &str));
  ASSERT_EQ(1, cdd_c_meta_diff_to_sql("t", &diff, C_TO_SQL_DIALECT_SQLITE, &str,
                                      NULL));

  /* cdd_c_get_schema_inspection_query NULL & dialect variations */
  ASSERT_EQ(1, cdd_c_get_schema_inspection_query(C_TO_SQL_DIALECT_SQLITE, NULL,
                                                 &str));
  ASSERT_EQ(
      1, cdd_c_get_schema_inspection_query(C_TO_SQL_DIALECT_SQLITE, "t", NULL));
  ASSERT_EQ(
      1, cdd_c_get_schema_inspection_query((c_to_sql_dialect_t)99, "t", &str));

  /* cdd_c_emit_create_index NULL variations */
  ASSERT_EQ(1, cdd_c_emit_create_index(NULL, "i", "c", 0, &str));
  ASSERT_EQ(1, cdd_c_emit_create_index("t", NULL, "c", 0, &str));
  ASSERT_EQ(1, cdd_c_emit_create_index("t", "i", NULL, 0, &str));
  ASSERT_EQ(1, cdd_c_emit_create_index("t", "i", "c", 0, NULL));

  /* cdd_c_emit_drop_index NULL variations */
  ASSERT_EQ(1, cdd_c_emit_drop_index(NULL, &str));
  ASSERT_EQ(1, cdd_c_emit_drop_index("i", NULL));

  /* cdd_c_meta_topological_sort NULL variations */
  ASSERT_EQ(1, cdd_c_meta_topological_sort(NULL, 1, out_schemas));
  ASSERT_EQ(1, cdd_c_meta_topological_sort(schemas, 1, NULL));
  ASSERT_EQ(1, cdd_c_meta_topological_sort(schemas, 0, out_schemas));

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
  C_ORM_STRCPY(fields[0].name, sizeof(fields[0].name), "unknown_field");
  C_ORM_STRCPY(fields[0].type, sizeof(fields[0].type), "unknown_type");
  C_ORM_STRCPY(fields[1].name, sizeof(fields[1].name), "id");
  C_ORM_STRCPY(fields[1].type, sizeof(fields[1].type), "int");
  C_ORM_STRCPY(fields[1].description, sizeof(fields[1].description), "@pk");
  sf.size = 2;
  sf.fields = fields;

  C_ORM_TMPFILE(&fp);
  ASSERT_EQ(0, write_struct_to_sql_create_table(fp, "test", &sf,
                                                C_TO_SQL_DIALECT_MYSQL));
  rewind(fp);
  memset(buf, 0, sizeof(buf));
  fread(buf, 1, sizeof(buf) - 1, fp);
  fclose(fp);
  ASSERT(strstr(buf, "unknown_field BLOB") != NULL);

  /* Dialect type maps */
  C_ORM_TMPFILE(&fp);
  C_ORM_STRCPY(fields[0].type, sizeof(fields[0].type), "double");
  C_ORM_STRCPY(fields[1].type, sizeof(fields[1].type), "bool");
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

TEST test_c_to_sql_additional_branches(void) {
  struct StructField fields[6];
  struct StructFields sf;
  cdd_c_prop_meta_t p_props[3];
  cdd_c_meta_t m_meta;
  char *out_sql = NULL;
  FILE *fp;
  char buf[2048];

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
  ASSERT_EQ(0, write_struct_to_sql_create_table(fp, "my_table", &sf,
                                                C_TO_SQL_DIALECT_SQLITE));
  rewind(fp);
  memset(buf, 0, sizeof(buf));
  fread(buf, 1, sizeof(buf) - 1, fp);
  fclose(fp);
  ASSERT(strstr(buf, "Id TEXT PRIMARY KEY") != NULL);
  ASSERT(strstr(buf, "ID REAL PRIMARY KEY NOT NULL") != NULL);

  /* PostgreSQL dialect check for double, number, bool, string */
  C_ORM_TMPFILE(&fp);
  ASSERT_EQ(0, write_struct_to_sql_create_table(fp, "my_table_pg", &sf,
                                                C_TO_SQL_DIALECT_POSTGRESQL));
  rewind(fp);
  memset(buf, 0, sizeof(buf));
  fread(buf, 1, sizeof(buf) - 1, fp);
  fclose(fp);
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

  ASSERT_EQ(0, cdd_c_meta_to_sql_create_table(&m_meta, C_TO_SQL_DIALECT_SQLITE,
                                              &out_sql));
  ASSERT(strstr(out_sql, "id TEXT PRIMARY KEY") != NULL);
  ASSERT(strstr(out_sql, "AUTOINCREMENT") == NULL);
  free(out_sql);

  /* 3. Multi-level / multi-dependency topological sort */
  {
    cdd_c_prop_meta_t p_u[1], p_c[1], p_p[5], p_cm[2];
    cdd_c_meta_t m_u, m_c, m_p, m_cm;
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

    ASSERT_EQ(0, cdd_c_meta_topological_sort(all_schemas, 4, sorted));
    ASSERT(sorted[2] == &m_p);
    ASSERT(sorted[3] == &m_cm);
  }

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
  RUN_TEST(test_c_to_sql_additional_branches);
}

#if defined(__clang__) || defined(__GNUC__)
#endif
