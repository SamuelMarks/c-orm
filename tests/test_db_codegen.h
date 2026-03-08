/**
 * @file test_db_codegen.h
 * @brief Unit tests for generating C ORM and DB SQL.
 */

#ifndef TEST_DB_CODEGEN_H
#define TEST_DB_CODEGEN_H

#include <greatest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <c_orm/database.h>
#include <c_orm/db_codegen.h>

TEST test_db_codegen_sql_sqlite(void) {
  struct DatabaseSchema schema;
  struct DatabaseTable table;
  struct DatabaseColumn cols[3];
  char buffer[1024] = {0};
  FILE *f;
  int rc;

  db_schema_init(&schema);
  schema.name = "TestDB";
  schema.tables = &table;
  schema.n_tables = 1;

  table.name = "users";
  table.columns = cols;
  table.n_columns = 3;

  memset(cols, 0, sizeof(cols));

  cols[0].name = "id";
  cols[0].type = DB_COL_TYPE_INTEGER;
  cols[0].is_primary_key = 1;

  cols[1].name = "username";
  cols[1].type = DB_COL_TYPE_VARCHAR;
  cols[1].is_unique = 1;
  cols[1].is_nullable = 1;

  cols[2].name = "age";
  cols[2].type = DB_COL_TYPE_INTEGER;
  cols[2].is_nullable = 0;

#if defined(_MSC_VER) && !defined(__INTEL_COMPILER)
  rc = fopen_s(&f, "test_db_codegen_sqlite.sql", "w+");
  if (rc != 0) FAILm("Could not open file");
#else
  f = fopen("test_db_codegen_sqlite.sql", "w+");
  if (!f) FAILm("Could not open file");
#endif

  rc = db_codegen_sql(&schema, f, "sqlite");
  ASSERT_EQ(0, rc);

  fseek(f, 0, SEEK_SET);
  fread(buffer, 1, sizeof(buffer) - 1, f);
  fclose(f);
  remove("test_db_codegen_sqlite.sql");

  ASSERT(strstr(buffer, "CREATE TABLE IF NOT EXISTS users (") != NULL);
  ASSERT(strstr(buffer, "id INTEGER PRIMARY KEY") != NULL);
  ASSERT(strstr(buffer, "username TEXT UNIQUE") != NULL);
  ASSERT(strstr(buffer, "age INTEGER NOT NULL") != NULL);

  PASS();
}

TEST test_db_codegen_sql_postgres(void) {
  struct DatabaseSchema schema;
  struct DatabaseTable table;
  struct DatabaseColumn cols[2];
  char buffer[1024] = {0};
  FILE *f;
  int rc;

  db_schema_init(&schema);
  schema.name = "TestDB";
  schema.tables = &table;
  schema.n_tables = 1;

  table.name = "posts";
  table.columns = cols;
  table.n_columns = 2;

  memset(cols, 0, sizeof(cols));

  cols[0].name = "id";
  cols[0].type = DB_COL_TYPE_INTEGER;
  cols[0].is_primary_key = 1;

  cols[1].name = "title";
  cols[1].type = DB_COL_TYPE_VARCHAR;
  cols[1].is_nullable = 1;

#if defined(_MSC_VER) && !defined(__INTEL_COMPILER)
  rc = fopen_s(&f, "test_db_codegen_pg.sql", "w+");
  if (rc != 0) FAILm("Could not open file");
#else
  f = fopen("test_db_codegen_pg.sql", "w+");
  if (!f) FAILm("Could not open file");
#endif

  rc = db_codegen_sql(&schema, f, "postgres");
  ASSERT_EQ(0, rc);

  fseek(f, 0, SEEK_SET);
  fread(buffer, 1, sizeof(buffer) - 1, f);
  fclose(f);
  remove("test_db_codegen_pg.sql");

  ASSERT(strstr(buffer, "CREATE TABLE IF NOT EXISTS posts (") != NULL);
  ASSERT(strstr(buffer, "id INTEGER PRIMARY KEY") != NULL);
  ASSERT(strstr(buffer, "title VARCHAR") != NULL);

  PASS();
}

TEST test_db_codegen_sql_mysql(void) {
  struct DatabaseSchema schema;
  struct DatabaseTable table;
  struct DatabaseColumn cols[2];
  char buffer[1024] = {0};
  FILE *f;
  int rc;

  db_schema_init(&schema);
  schema.name = "TestDB";
  schema.tables = &table;
  schema.n_tables = 1;

  table.name = "logs";
  table.columns = cols;
  table.n_columns = 2;

  memset(cols, 0, sizeof(cols));

  cols[0].name = "id";
  cols[0].type = DB_COL_TYPE_INTEGER;
  cols[0].is_primary_key = 1;

  cols[1].name = "message";
  cols[1].type = DB_COL_TYPE_VARCHAR;
  cols[1].is_nullable = 1;

#if defined(_MSC_VER) && !defined(__INTEL_COMPILER)
  rc = fopen_s(&f, "test_db_codegen_mysql.sql", "w+");
  if (rc != 0) FAILm("Could not open file");
#else
  f = fopen("test_db_codegen_mysql.sql", "w+");
  if (!f) FAILm("Could not open file");
#endif

  rc = db_codegen_sql(&schema, f, "mysql");
  ASSERT_EQ(0, rc);

  fseek(f, 0, SEEK_SET);
  fread(buffer, 1, sizeof(buffer) - 1, f);
  fclose(f);
  remove("test_db_codegen_mysql.sql");

  ASSERT(strstr(buffer, "CREATE TABLE IF NOT EXISTS logs (") != NULL);
  ASSERT(strstr(buffer, "id INT AUTO_INCREMENT PRIMARY KEY") != NULL);
  ASSERT(strstr(buffer, "message VARCHAR(255)") != NULL);

  PASS();
}

TEST test_db_codegen_sql_foreign_keys(void) {
  struct DatabaseSchema schema;
  struct DatabaseTable table;
  struct DatabaseColumn cols[3];
  char buffer[1024] = {0};
  FILE *f;
  int rc;

  db_schema_init(&schema);
  schema.name = "TestDB";
  schema.tables = &table;
  schema.n_tables = 1;

  table.name = "comments";
  table.columns = cols;
  table.n_columns = 3;

  memset(cols, 0, sizeof(cols));

  cols[0].name = "id";
  cols[0].type = DB_COL_TYPE_INTEGER;
  cols[0].is_primary_key = 1;

  cols[1].name = "user_id";
  cols[1].type = DB_COL_TYPE_INTEGER;
  cols[1].is_nullable = 0;
  cols[1].foreign_key_table = "users";
  cols[1].foreign_key_column = "id";
  cols[1].on_delete = DB_FK_ACTION_CASCADE;
  cols[1].on_update = DB_FK_ACTION_NONE;

  cols[2].name = "post_id";
  cols[2].type = DB_COL_TYPE_INTEGER;
  cols[2].is_nullable = 1;
  cols[2].foreign_key_table = "posts";
  cols[2].foreign_key_column = NULL; /* Should default to id */
  cols[2].on_delete = DB_FK_ACTION_SET_NULL;
  cols[2].on_update = DB_FK_ACTION_RESTRICT;

#if defined(_MSC_VER) && !defined(__INTEL_COMPILER)
  rc = fopen_s(&f, "test_db_codegen_fk.sql", "w+");
  if (rc != 0) FAILm("Could not open file");
#else
  f = fopen("test_db_codegen_fk.sql", "w+");
  if (!f) FAILm("Could not open file");
#endif

  rc = db_codegen_sql(&schema, f, "postgres");
  ASSERT_EQ(0, rc);

  fseek(f, 0, SEEK_SET);
  fread(buffer, 1, sizeof(buffer) - 1, f);
  fclose(f);
  remove("test_db_codegen_fk.sql");

  ASSERT(strstr(buffer, "CREATE TABLE IF NOT EXISTS comments (") != NULL);
  ASSERT(strstr(buffer, "user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE") != NULL);      
  ASSERT(strstr(buffer, "post_id INTEGER REFERENCES posts(id) ON DELETE SET NULL ON UPDATE RESTRICT") != NULL);

  PASS();
}

TEST test_db_codegen_c_header(void) {
  struct DatabaseSchema schema;
  struct DatabaseTable table;
  struct DatabaseColumn cols[3];
  char buffer[1024] = {0};
  FILE *f;
  int rc;

  db_schema_init(&schema);
  schema.name = "TestDB";
  schema.tables = &table;
  schema.n_tables = 1;

  table.name = "Role";
  table.columns = cols;
  table.n_columns = 3;

  memset(cols, 0, sizeof(cols));

  cols[0].name = "id";
  cols[0].type = DB_COL_TYPE_INTEGER;

  cols[1].name = "name";
  cols[1].type = DB_COL_TYPE_VARCHAR;

  cols[2].name = "parent_id";
  cols[2].type = DB_COL_TYPE_INTEGER;
  cols[2].foreign_key_table = "Role";
  cols[2].foreign_key_column = "id";

#if defined(_MSC_VER) && !defined(__INTEL_COMPILER)
  rc = fopen_s(&f, "test_db_codegen_hdr.h", "w+");
  if (rc != 0) FAILm("Could not open file");
#else
  f = fopen("test_db_codegen_hdr.h", "w+");
  if (!f) FAILm("Could not open file");
#endif

  rc = db_codegen_struct_header(&schema, f, "TEST_DB_CODEGEN_HDR_H");
  ASSERT_EQ(0, rc);

  fseek(f, 0, SEEK_SET);
  fread(buffer, 1, sizeof(buffer) - 1, f);
  fclose(f);
  remove("test_db_codegen_hdr.h");

  ASSERT(strstr(buffer, "#ifndef TEST_DB_CODEGEN_HDR_H") != NULL);
  ASSERT(strstr(buffer, "struct Role {") != NULL);
  ASSERT(strstr(buffer, "int id;") != NULL);
  ASSERT(strstr(buffer, "char * name;") != NULL);
  ASSERT(strstr(buffer, "/* FK -> Role.id */") != NULL);
  ASSERT(strstr(buffer, "int parent_id;") != NULL);

  PASS();
}

TEST test_db_codegen_parse_sql_valid(void) {
    struct DatabaseSchema schema;
    int rc;
    const char* sql = "CREATE TABLE users (id INTEGER PRIMARY KEY);";

    rc = db_codegen_parse_sql(sql, &schema);
    ASSERT_EQ(0, rc);
    ASSERT_NEQ(NULL, schema.name);
    ASSERT_EQ(1, schema.n_tables);
    ASSERT_NEQ(NULL, schema.tables);
    ASSERT_STR_EQ("parsed_table", schema.tables[0].name);
    ASSERT_EQ(1, schema.tables[0].n_columns);

    db_schema_free(&schema);
    PASS();
}

TEST test_db_codegen_parse_sql_invalid(void) {
    struct DatabaseSchema schema;
    int rc;

    /* Null tests */
    rc = db_codegen_parse_sql(NULL, &schema);
    ASSERT_EQ(EINVAL, rc);

    rc = db_codegen_parse_sql("CREATE TABLE users", NULL);
    ASSERT_EQ(EINVAL, rc);

    /* Invalid SQL command */
    rc = db_codegen_parse_sql("DROP TABLE users", &schema);
    ASSERT_EQ(EINVAL, rc);
    db_schema_free(&schema);

    PASS();
}

TEST test_db_codegen_crud_h_c(void) {
  struct DatabaseSchema schema;
  struct DatabaseTable table;
  struct DatabaseColumn cols[1];
  char buffer[1024] = {0};
  FILE *f;
  int rc;

  db_schema_init(&schema);
  schema.name = "TestDB";
  schema.tables = &table;
  schema.n_tables = 1;

  table.name = "users";
  table.columns = cols;
  table.n_columns = 1;

  memset(cols, 0, sizeof(cols));
  cols[0].name = "id";
  cols[0].type = DB_COL_TYPE_INTEGER;
  cols[0].is_primary_key = 1;

#if defined(_MSC_VER) && !defined(__INTEL_COMPILER)
  rc = fopen_s(&f, "test_db_codegen_crud.h", "w+");
  if (rc != 0) FAILm("Could not open file");
#else
  f = fopen("test_db_codegen_crud.h", "w+");
  if (!f) FAILm("Could not open file");
#endif

  rc = db_codegen_crud_h(&schema, f, "TEST_CRUD_H", "models.h");
  ASSERT_EQ(0, rc);

  fseek(f, 0, SEEK_SET);
  fread(buffer, 1, sizeof(buffer) - 1, f);
  fclose(f);
  remove("test_db_codegen_crud.h");

  ASSERT(strstr(buffer, "#ifndef TEST_CRUD_H") != NULL);
  ASSERT(strstr(buffer, "#include \"models.h\"") != NULL);
  ASSERT(strstr(buffer, "int users_insert(const struct users *item);") != NULL);

#if defined(_MSC_VER) && !defined(__INTEL_COMPILER)
  rc = fopen_s(&f, "test_db_codegen_crud.c", "w+");
  if (rc != 0) FAILm("Could not open file");
#else
  f = fopen("test_db_codegen_crud.c", "w+");
  if (!f) FAILm("Could not open file");
#endif

  rc = db_codegen_crud_c(&schema, f, "crud.h", "sqlite");
  ASSERT_EQ(0, rc);

  memset(buffer, 0, sizeof(buffer));
  fseek(f, 0, SEEK_SET);
  fread(buffer, 1, sizeof(buffer) - 1, f);
  fclose(f);
  remove("test_db_codegen_crud.c");

  ASSERT(strstr(buffer, "#include \"crud.h\"") != NULL);
  ASSERT(strstr(buffer, "int users_insert(const struct users *item)") != NULL);
  ASSERT(strstr(buffer, "int users_delete(int id)") != NULL);

  PASS();
}

SUITE(db_codegen_suite) {
  RUN_TEST(test_db_codegen_sql_sqlite);
  RUN_TEST(test_db_codegen_sql_postgres);
  RUN_TEST(test_db_codegen_sql_mysql);
  RUN_TEST(test_db_codegen_sql_foreign_keys);
  RUN_TEST(test_db_codegen_c_header);
  RUN_TEST(test_db_codegen_parse_sql_valid);
  RUN_TEST(test_db_codegen_parse_sql_invalid);
  RUN_TEST(test_db_codegen_crud_h_c);
}

#endif /* TEST_DB_CODEGEN_H */
