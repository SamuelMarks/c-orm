/**
 * @file test_db_codegen.h
 * @brief Unit tests for generating C ORM and DB SQL.
 */

#ifndef TEST_DB_CODEGEN_H
#define TEST_DB_CODEGEN_H

/* clang-format off */
#include <greatest.h>

#include <stdio.h>

#include <stdlib.h>

#include <errno.h>

#include <string.h>

#include <c_orm/database.h>

#include <c_orm/db_codegen.h>
/* clang-format on */

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
  if (rc != 0)
    FAILm("Could not open file");
#else
  f = fopen("test_db_codegen_sqlite.sql", "w+");
  if (!f)
    FAILm("Could not open file");
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
  if (rc != 0)
    FAILm("Could not open file");
#else
  f = fopen("test_db_codegen_pg.sql", "w+");
  if (!f)
    FAILm("Could not open file");
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
  if (rc != 0)
    FAILm("Could not open file");
#else
  f = fopen("test_db_codegen_mysql.sql", "w+");
  if (!f)
    FAILm("Could not open file");
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
  if (rc != 0)
    FAILm("Could not open file");
#else
  f = fopen("test_db_codegen_fk.sql", "w+");
  if (!f)
    FAILm("Could not open file");
#endif

  rc = db_codegen_sql(&schema, f, "postgres");
  ASSERT_EQ(0, rc);

  fseek(f, 0, SEEK_SET);
  fread(buffer, 1, sizeof(buffer) - 1, f);
  fclose(f);
  remove("test_db_codegen_fk.sql");

  ASSERT(strstr(buffer, "CREATE TABLE IF NOT EXISTS comments (") != NULL);
  ASSERT(
      strstr(
          buffer,
          "user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE") !=
      NULL);
  ASSERT(strstr(buffer, "post_id INTEGER REFERENCES posts(id) ON DELETE SET "
                        "NULL ON UPDATE RESTRICT") != NULL);

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
  if (rc != 0)
    FAILm("Could not open file");
#else
  f = fopen("test_db_codegen_hdr.h", "w+");
  if (!f)
    FAILm("Could not open file");
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
  const char *sql = "CREATE TABLE users (id INTEGER PRIMARY KEY);";

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
  if (rc != 0)
    FAILm("Could not open file");
#else
  f = fopen("test_db_codegen_crud.h", "w+");
  if (!f)
    FAILm("Could not open file");
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
  if (rc != 0)
    FAILm("Could not open file");
#else
  f = fopen("test_db_codegen_crud.c", "w+");
  if (!f)
    FAILm("Could not open file");
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

TEST test_db_schema_free_full(void) {
  struct DatabaseSchema schema;
  db_schema_init(&schema);

  schema.name = malloc(10);
#if defined(_MSC_VER)
  strcpy_s(schema.name, 10, "test");
#else
  strcpy(schema.name, "test");
#endif

  schema.tables = calloc(1, sizeof(struct DatabaseTable));
  schema.n_tables = 1;
  schema.tables[0].name = malloc(10);
#if defined(_MSC_VER)
  strcpy_s(schema.tables[0].name, 10, "table");
#else
  strcpy(schema.tables[0].name, "table");
#endif

  schema.tables[0].columns = calloc(1, sizeof(struct DatabaseColumn));
  schema.tables[0].n_columns = 1;
  schema.tables[0].columns[0].name = malloc(10);
#if defined(_MSC_VER)
  strcpy_s(schema.tables[0].columns[0].name, 10, "col");
#else
  strcpy(schema.tables[0].columns[0].name, "col");
#endif

  schema.tables[0].columns[0].default_value = malloc(10);
#if defined(_MSC_VER)
  strcpy_s(schema.tables[0].columns[0].default_value, 10, "def");
#else
  strcpy(schema.tables[0].columns[0].default_value, "def");
#endif

  schema.tables[0].columns[0].foreign_key_table = malloc(10);
#if defined(_MSC_VER)
  strcpy_s(schema.tables[0].columns[0].foreign_key_table, 10, "fk_table");
#else
  strcpy(schema.tables[0].columns[0].foreign_key_table, "fk_table");
#endif

  schema.tables[0].columns[0].foreign_key_column = malloc(10);
#if defined(_MSC_VER)
  strcpy_s(schema.tables[0].columns[0].foreign_key_column, 10, "fk_col");
#else
  strcpy(schema.tables[0].columns[0].foreign_key_column, "fk_col");
#endif

  db_schema_free(&schema);

  /* Cover NULL free */
  db_schema_free(NULL);
  db_schema_init(NULL);

  PASS();
}

TEST test_db_codegen_invalid_args(void) {
  struct DatabaseSchema schema;
  FILE *f = NULL;
  int rc = 0;

  (void)rc;
  db_schema_init(&schema);

#if defined(_MSC_VER) && !defined(__INTEL_COMPILER)
  rc = fopen_s(&f, "test_db_codegen_null.tmp", "w+");
#else
  f = fopen("test_db_codegen_null.tmp", "w+");
#endif

  ASSERT_EQ(EINVAL, db_codegen_struct_header(NULL, f, "HDR"));
  ASSERT_EQ(EINVAL, db_codegen_struct_header(&schema, NULL, "HDR"));
  ASSERT_EQ(EINVAL, db_codegen_struct_header(&schema, f, NULL));

  ASSERT_EQ(EINVAL, db_codegen_sql(NULL, f, "sqlite"));
  ASSERT_EQ(EINVAL, db_codegen_sql(&schema, NULL, "sqlite"));
  ASSERT_EQ(EINVAL, db_codegen_sql(&schema, f, NULL));

  ASSERT_EQ(EINVAL, db_codegen_crud_h(NULL, f, "H", "H"));
  ASSERT_EQ(EINVAL, db_codegen_crud_h(&schema, NULL, "H", "H"));
  ASSERT_EQ(EINVAL, db_codegen_crud_h(&schema, f, NULL, "H"));
  ASSERT_EQ(EINVAL, db_codegen_crud_h(&schema, f, "H", NULL));

  ASSERT_EQ(EINVAL, db_codegen_crud_c(NULL, f, "H", "S"));
  ASSERT_EQ(EINVAL, db_codegen_crud_c(&schema, NULL, "H", "S"));
  ASSERT_EQ(EINVAL, db_codegen_crud_c(&schema, f, NULL, "S"));
  ASSERT_EQ(EINVAL, db_codegen_crud_c(&schema, f, "H", NULL));

  if (f) {
    fclose(f);
    remove("test_db_codegen_null.tmp");
  }

  PASS();
}

TEST test_db_codegen_all_types_and_fks(void) {
  struct DatabaseSchema schema;
  struct DatabaseTable table;
  struct DatabaseColumn cols[8];
  char buffer[2048] = {0};
  FILE *f;
  int rc;

  db_schema_init(&schema);
  schema.name = "TestDB";
  schema.tables = &table;
  schema.n_tables = 1;

  table.name = "all_types";
  table.columns = cols;
  table.n_columns = 8;

  memset(cols, 0, sizeof(cols));

  cols[0].name = "col_text";
  cols[0].type = DB_COL_TYPE_TEXT;

  cols[1].name = "col_real";
  cols[1].type = DB_COL_TYPE_REAL;

  cols[2].name = "col_blob";
  cols[2].type = DB_COL_TYPE_BLOB;

  cols[3].name = "col_bool";
  cols[3].type = DB_COL_TYPE_BOOLEAN;

  cols[4].name = "col_date";
  cols[4].type = DB_COL_TYPE_DATE;

  cols[5].name = "col_datetime";
  cols[5].type = DB_COL_TYPE_DATETIME;

  cols[6].name = "col_unknown";
  cols[6].type = DB_COL_TYPE_UNKNOWN;

  cols[7].name = "fk_default";
  cols[7].type = DB_COL_TYPE_INTEGER;
  cols[7].foreign_key_table = "users";
  cols[7].foreign_key_column = "id";
  cols[7].on_delete = DB_FK_ACTION_SET_DEFAULT;
  cols[7].on_update =
      (enum DatabaseForeignKeyAction)999; /* trigger default switch */

#if defined(_MSC_VER) && !defined(__INTEL_COMPILER)
  rc = fopen_s(&f, "test_all_types.sql", "w+");
#else
  f = fopen("test_all_types.sql", "w+");
#endif

  rc = db_codegen_sql(&schema, f, "postgres");
  ASSERT_EQ(0, rc);

  fseek(f, 0, SEEK_SET);
  fread(buffer, 1, sizeof(buffer) - 1, f);
  fclose(f);
  remove("test_all_types.sql");

#if defined(_MSC_VER) && !defined(__INTEL_COMPILER)
  rc = fopen_s(&f, "test_all_types.h", "w+");
#else
  f = fopen("test_all_types.h", "w+");
#endif

  rc = db_codegen_struct_header(&schema, f, "ALL_TYPES_H");
  ASSERT_EQ(0, rc);
  fclose(f);
  remove("test_all_types.h");

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
  RUN_TEST(test_db_schema_free_full);
  RUN_TEST(test_db_codegen_invalid_args);
  RUN_TEST(test_db_codegen_all_types_and_fks);
}

#endif /* TEST_DB_CODEGEN_H */
