/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_migrations.h"
#include "c_orm_sqlite.h"
#include "classes/emit/c_to_sql.h"
#include "greatest.h"
#include <stdlib.h>
#include <string.h>
/* clang-format on */

SUITE(migrations_suite);

static void test_log_cb(const char *msg) { (void)msg; }

TEST test_migration_init(void) {
  c_orm_db_t *db = NULL;
  c_orm_error_t err;

  err = c_orm_sqlite_connect(":memory:", &db);
  ASSERT_EQ(C_ORM_OK, err);

  err = c_orm_migration_init_table(db);
  ASSERT_EQ(C_ORM_OK, err);

  db->vtable->disconnect(db);
  PASS();
}

TEST test_migrate_all_dry_run(void) {
  c_orm_db_t *db = NULL;
  c_orm_error_t err;
  c_orm_migration_options_t opts;
  c_orm_migration_t migs[2];

  memset(&opts, 0, sizeof(opts));
  opts.dry_run = 1;
  opts.log_cb = test_log_cb;

  memset(&migs, 0, sizeof(migs));
#if defined(_MSC_VER)
  strcpy_s(migs[0].version, sizeof(migs[0].version), "20260330010000");
  strcpy_s(migs[0].name, sizeof(migs[0].name), "create_users");
#else
  strcpy(migs[0].version, "20260330010000");
  strcpy(migs[0].name, "create_users");
#endif
  migs[0].up_sql = "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT);";

#if defined(_MSC_VER)
  strcpy_s(migs[1].version, sizeof(migs[1].version), "20260330010001");
  strcpy_s(migs[1].name, sizeof(migs[1].name), "create_posts");
#else
  strcpy(migs[1].version, "20260330010001");
  strcpy(migs[1].name, "create_posts");
#endif
  migs[1].up_sql = "CREATE TABLE posts (id INTEGER PRIMARY KEY, title TEXT);";

  err = c_orm_sqlite_connect(":memory:", &db);
  ASSERT_EQ(C_ORM_OK, err);

  err = c_orm_migrate_all(db, migs, 2, &opts);
  ASSERT_EQ(C_ORM_OK, err);

  /* Since it's dry-run, the 'users' table shouldn't exist */
  err = c_orm_execute_raw(db, "SELECT 1 FROM users");
  ASSERT_EQ(C_ORM_ERROR_SQL, err);

  db->vtable->disconnect(db);
  PASS();
}

TEST test_migrate_all_execute(void) {
  c_orm_db_t *db = NULL;
  c_orm_error_t err;
  c_orm_migration_options_t opts;
  c_orm_migration_t migs[1];

  memset(&opts, 0, sizeof(opts));
  opts.dry_run = 0;

  memset(&migs, 0, sizeof(migs));
#if defined(_MSC_VER)
  strcpy_s(migs[0].version, sizeof(migs[0].version), "20260330010000");
  strcpy_s(migs[0].name, sizeof(migs[0].name), "create_items");
#else
  strcpy(migs[0].version, "20260330010000");
  strcpy(migs[0].name, "create_items");
#endif
  migs[0].up_sql = "CREATE TABLE items (id INTEGER PRIMARY KEY);";

  err = c_orm_sqlite_connect(":memory:", &db);
  ASSERT_EQ(C_ORM_OK, err);

  err = c_orm_migrate_all(db, migs, 1, &opts);
  ASSERT_EQ(C_ORM_OK, err);

  /* Should exist now */
  err = c_orm_execute_raw(db, "SELECT 1 FROM items");
  ASSERT_EQ(C_ORM_OK, err);

  /* Running again should do nothing and succeed */
  err = c_orm_migrate_all(db, migs, 1, &opts);
  ASSERT_EQ(C_ORM_OK, err);

  db->vtable->disconnect(db);
  PASS();
}

TEST test_meta_diff_add_drop(void) {
  c_to_sql_dialect_t dialect = C_TO_SQL_DIALECT_SQLITE;
  cdd_c_meta_t old_schema, new_schema;
  cdd_c_prop_meta_t old_props[2], new_props[2];
  cdd_c_meta_diff_t diff;
  char *up_sql = NULL;
  char *down_sql = NULL;

  old_schema.name = "users";
  old_schema.num_props = 2;
  old_schema.props = old_props;
  old_props[0].name = "id";
  old_props[0].type = "int";
  old_props[1].name = "old_col";
  old_props[1].type = "char*";

  new_schema.name = "users";
  new_schema.num_props = 2;
  new_schema.props = new_props;
  new_props[0].name = "id";
  new_props[0].type = "int";
  new_props[1].name = "new_col";
  new_props[1].type = "float";

  ASSERT_EQ(0, cdd_c_meta_diff(&old_schema, &new_schema, &diff));
  ASSERT_EQ(1, diff.num_added);
  ASSERT_EQ(1, diff.num_dropped);
  ASSERT_EQ(0, diff.num_altered);

  ASSERT_STR_EQ("new_col", diff.added_props[0].name);
  ASSERT_STR_EQ("old_col", diff.dropped_props[0].name);

  ASSERT_EQ(
      0, cdd_c_meta_diff_to_sql("users", &diff, dialect, &up_sql, &down_sql));
  ASSERT(strstr(up_sql, "ADD COLUMN new_col") != NULL);
  ASSERT(strstr(up_sql, "DROP COLUMN old_col") != NULL);

  free(up_sql);
  free(down_sql);
  cdd_c_meta_diff_free(&diff);
  PASS();
}

TEST test_c_orm_fetch_table_schema(void) {
  c_orm_db_t *db = NULL;
  c_orm_error_t err;
  cdd_c_meta_t *schema = NULL;
  int found_id = 0, found_name = 0;
  size_t i;

  err = c_orm_sqlite_connect(":memory:", &db);
  ASSERT_EQ(C_ORM_OK, err);

  c_orm_execute_raw(
      db, "CREATE TABLE authors (id INTEGER PRIMARY KEY, name TEXT);");

  err = c_orm_migration_fetch_table_schema(db, "authors", &schema);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT(schema != NULL);
  ASSERT_STR_EQ("authors", schema->name);
  ASSERT_EQ(2, schema->num_props);

  for (i = 0; i < schema->num_props; i++) {
    if (strcmp(schema->props[i].name, "id") == 0)
      found_id = 1;
    if (strcmp(schema->props[i].name, "name") == 0)
      found_name = 1;
  }

  ASSERT(found_id == 1);
  ASSERT(found_name == 1);

  c_orm_migration_free_table_schema(schema);
  db->vtable->disconnect(db);
  PASS();
}

SUITE(migrations_suite) {
  RUN_TEST(test_meta_diff_add_drop);
  RUN_TEST(test_c_orm_fetch_table_schema);
  RUN_TEST(test_migration_init);
  RUN_TEST(test_migrate_all_dry_run);
  RUN_TEST(test_migrate_all_execute);
}
