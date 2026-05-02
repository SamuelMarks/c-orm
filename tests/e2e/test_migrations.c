/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_migrations.h"
#include "c_orm_sqlite.h"
#include "c_to_sql.h"
#include "greatest.h"
#include <stdlib.h>
#include <string.h>
/* clang-format on */

static int oom_countdown = -1;
static int oom_active = 0;

static void *m_mock_malloc(size_t size) {
  if (oom_active) {
    if (oom_countdown == 0) {
      oom_countdown--;
      return NULL;
    }
    if (oom_countdown > 0) {
      oom_countdown--;
    }
  }
  return malloc(size);
}
static void *m_mock_realloc(void *ptr, size_t size) {
  if (oom_active) {
    if (oom_countdown == 0) {
      oom_countdown--;
      return NULL;
    }
    if (oom_countdown > 0) {
      oom_countdown--;
    }
  }
  return realloc(ptr, size);
}
static void m_mock_free(void *ptr) { free(ptr); }

static c_orm_error_t
my_pre_migrate(c_orm_db_t *db, const c_orm_migration_t *mig, void *user_data) {
  (void)db;
  (void)user_data;
  if (strcmp(mig->name, "fail_pre") == 0)
    return C_ORM_ERROR_VALIDATION;
  return C_ORM_OK;
}
static c_orm_error_t
my_post_migrate(c_orm_db_t *db, const c_orm_migration_t *mig, void *user_data) {
  (void)db;
  (void)user_data;
  if (strcmp(mig->name, "fail_post") == 0)
    return C_ORM_ERROR_VALIDATION;
  return C_ORM_OK;
}

static int fail_init = 0;
static int fail_insert = 0;
static int fail_delete = 0;
static int fail_applied = 0;
static int fail_up = 0;
static int fail_down = 0;
static int fail_rollback_step = 0;
static int fail_rollback_step_rb = 0;
static int fail_prep_schema = 0;
static int fail_prep_applied = 0;
static c_orm_error_t (*orig_prep)(c_orm_db_t *, const char *, c_orm_query_t **);

static c_orm_error_t my_mig_prep(c_orm_db_t *db_v, const char *sql,
                                 c_orm_query_t **out_query) {
  if (fail_init && strstr(sql, "CREATE TABLE IF NOT EXISTS _c_orm_migrations"))
    return C_ORM_ERROR_SQL;
  if (fail_insert && strstr(sql, "INSERT INTO _c_orm_migrations"))
    return C_ORM_ERROR_SQL;
  if (fail_delete && strstr(sql, "DELETE FROM _c_orm_migrations"))
    return C_ORM_ERROR_SQL;
  if (fail_applied &&
      strstr(sql, "SELECT version, name, hash FROM _c_orm_migrations"))
    return C_ORM_ERROR_SQL;
  if (fail_up && strstr(sql, "UP"))
    return C_ORM_ERROR_SQL;
  if (fail_down && strstr(sql, "DOWN"))
    return C_ORM_ERROR_SQL;
  if (fail_rollback_step && strstr(sql, "ROLLBACK TO SAVEPOINT c_orm_mig_step"))
    return C_ORM_ERROR_SQL;
  if (fail_rollback_step_rb &&
      strstr(sql, "ROLLBACK TO SAVEPOINT c_orm_mig_step_rb"))
    return C_ORM_ERROR_SQL;
  if (fail_prep_schema && strstr(sql, "PRAGMA table_info"))
    return C_ORM_ERROR_SQL;
  if (fail_prep_applied &&
      strstr(sql, "SELECT version, name, hash FROM _c_orm_migrations"))
    return C_ORM_ERROR_SQL;
  return orig_prep(db_v, sql, out_query);
}

static int fail_step = 0;
static c_orm_error_t (*orig_step)(c_orm_query_t *, int *);
static c_orm_error_t my_mig_step(c_orm_query_t *query, int *out_has_row) {
  if (fail_step)
    return C_ORM_ERROR_SQL;
  return orig_step(query, out_has_row);
}

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

TEST test_migrations_oom(void) {
  c_orm_db_t *db = NULL;
  c_orm_sqlite_connect(":memory:", &db);

  c_orm_migration_t *out_migs = NULL;
  size_t out_count;
  c_orm_migration_load_dir(NULL, NULL, &out_count);
  c_orm_migration_load_dir("some", &out_migs, &out_count);

  c_orm_migration_free_array(NULL, 0);

  /* Free array with actual data */
  {
    c_orm_migration_t *m = malloc(sizeof(c_orm_migration_t));
    memset(m, 0, sizeof(*m));
    m->up_sql = strdup("UP");
    m->down_sql = strdup("DOWN");
    c_orm_migration_free_array(m, 1);
  }

  c_orm_migration_options_t opts;
  memset(&opts, 0, sizeof(opts));

  /* migrate all NULLs */
  c_orm_migrate_all(NULL, NULL, 0, &opts);

  /* rollback NULLs */
  c_orm_migrate_rollback(NULL, NULL, 0, 0, &opts);

  /* Lock / Unlock */
  c_orm_migration_lock(db);
  c_orm_migration_unlock(db);

  opts.pre_migrate = my_pre_migrate;
  opts.post_migrate = my_post_migrate;

  c_orm_migration_t m_fail[2];
  memset(m_fail, 0, sizeof(m_fail));
#if defined(_MSC_VER)
  strcpy_s(m_fail[0].version, sizeof(m_fail[0].version), "1");
  strcpy_s(m_fail[0].name, sizeof(m_fail[0].name), "fail_pre");
  strcpy_s(m_fail[1].version, sizeof(m_fail[1].version), "2");
  strcpy_s(m_fail[1].name, sizeof(m_fail[1].name), "fail_post");
#else
  strcpy(m_fail[0].version, "1");
  strcpy(m_fail[0].name, "fail_pre");
  strcpy(m_fail[1].version, "2");
  strcpy(m_fail[1].name, "fail_post");
#endif
  m_fail[0].up_sql = "SELECT 1;";
  m_fail[1].up_sql = "SELECT 1;";

  c_orm_migrate_all(db, &m_fail[0], 1, &opts);
  c_orm_migrate_all(db, &m_fail[1], 1, &opts);

  c_orm_migration_t m_good;
  memset(&m_good, 0, sizeof(m_good));
#if defined(_MSC_VER)
  strcpy_s(m_good.version, sizeof(m_good.version), "3");
  strcpy_s(m_good.name, sizeof(m_good.name), "good");
#else
  strcpy(m_good.version, "3");
  strcpy(m_good.name, "good");
#endif
  m_good.up_sql = "CREATE TABLE dummy (id INT); /* UP */";
  m_good.down_sql = "DROP TABLE dummy; /* DOWN */";

  c_orm_migrate_all(db, &m_good, 1, &opts);

  opts.dry_run = 1;
  c_orm_migrate_rollback(db, &m_good, 1, 1, &opts);
  opts.dry_run = 0;
  c_orm_migrate_rollback(db, &m_good, 1, 1, &opts);
  c_orm_migrate_rollback(db, &m_good, 1, 1, &opts);

  c_orm_migrate_up(NULL, "dir", &opts);
  c_orm_migrate_up(db, "dir", &opts);
  c_orm_migrate_down(NULL, "dir", 1, &opts);
  c_orm_migrate_down(db, "dir", 1, &opts);

  c_orm_execute_raw(db, "CREATE TABLE schema_test (id INT, v TEXT);");
  {
    int i;
    for (i = 0; i < 25; i++) {
      cdd_c_meta_t *out_s = NULL;
      oom_active = 1;
      oom_countdown = i;
      c_orm_migration_fetch_table_schema(db, "schema_test", &out_s);
      oom_active = 0;
      if (out_s)
        c_orm_migration_free_table_schema(out_s);
    }
    for (i = 0; i < 15; i++) {
      c_orm_migration_t *m_out = NULL;
      size_t c_out = 0;
      oom_active = 1;
      oom_countdown = i;
      c_orm_migration_get_applied(db, &m_out, &c_out);
      oom_active = 0;
      if (m_out)
        c_orm_migration_free_array(m_out, c_out);
    }

    c_orm_migration_free_table_schema(NULL);

    cdd_c_meta_t *null_name_meta;
    null_name_meta = malloc(sizeof(cdd_c_meta_t));
    memset(null_name_meta, 0, sizeof(*null_name_meta));
    c_orm_migration_free_table_schema(null_name_meta);
  }

  /* Mock vtable for failures */
  c_orm_driver_vtable_t orig_vt = *(c_orm_driver_vtable_t *)db->vtable;
  c_orm_driver_vtable_t mock_vt = *(c_orm_driver_vtable_t *)db->vtable;
  orig_prep = mock_vt.prepare;
  mock_vt.prepare = my_mig_prep;
  orig_step = mock_vt.step;
  mock_vt.step = my_mig_step;
  db->vtable = (const c_orm_driver_vtable_t *)&mock_vt;

  fail_init = 1;
  c_orm_migrate_all(db, &m_good, 1, &opts);
  fail_init = 0;

  fail_applied = 1;
  c_orm_migrate_rollback(db, &m_good, 1, 1, &opts);
  fail_applied = 0;

  fail_up = 1;
  c_orm_migrate_all(db, &m_good, 1, &opts); /* fails in up_sql */
  fail_up = 0;

  fail_insert = 1;
  c_orm_migrate_all(db, &m_good, 1, &opts);
  fail_insert = 0;

  /* Need to ensure it's applied for down to work */
  c_orm_migrate_all(db, &m_good, 1, &opts);

  fail_down = 1;
  c_orm_migrate_rollback(db, &m_good, 1, 1, &opts); /* fails down_sql */
  fail_down = 0;

  fail_delete = 1;
  c_orm_migrate_rollback(db, &m_good, 1, 1, &opts);
  fail_delete = 0;

  fail_step = 1;
  cdd_c_meta_t *step_meta = NULL;
  c_orm_migration_fetch_table_schema(db, "schema_test", &step_meta);
  if (step_meta)
    c_orm_migration_free_table_schema(step_meta);
  fail_step = 0;

  fail_rollback_step = 1;
  c_orm_migrate_all(db, &m_fail[0], 1, &opts);
  c_orm_migrate_all(db, &m_fail[1], 1, &opts);
  fail_rollback_step = 0;

  fail_rollback_step_rb = 1;
  fail_down = 1;
  c_orm_migrate_rollback(db, &m_good, 1, 1, &opts);
  fail_down = 0;
  fail_delete = 1;
  c_orm_migrate_rollback(db, &m_good, 1, 1, &opts);
  fail_delete = 0;
  fail_rollback_step_rb = 0;

  /* Extra branch coverage */
  opts.log_cb = test_log_cb;
  c_orm_migrate_rollback(db, &m_good, 1, 1, &opts); /* log_cb in rollback */
  c_orm_migrate_rollback(db, &m_good, 1, 0, &opts); /* steps == 0 */

  fail_prep_schema = 1;
  cdd_c_meta_t *s_meta = NULL;
  c_orm_migration_fetch_table_schema(db, "schema_test", &s_meta);
  if (s_meta)
    c_orm_migration_free_table_schema(s_meta);
  fail_prep_schema = 0;

  fail_prep_applied = 1;
  c_orm_migration_t *a_migs = NULL;
  size_t a_count = 0;
  c_orm_migration_get_applied(db, &a_migs, &a_count);
  if (a_migs)
    c_orm_migration_free_array(a_migs, a_count);
  fail_prep_applied = 0;

  db->vtable = (const c_orm_driver_vtable_t *)&orig_vt;

  c_orm_db_t db_pg;
  memset(&db_pg, 0, sizeof(db_pg));
  db_pg.vtable = (const c_orm_driver_vtable_t *)&orig_vt;
  db_pg.driver_name = "postgres";
  db_pg.driver_data = db->driver_data;
  c_orm_migration_lock(&db_pg);
  c_orm_migration_unlock(&db_pg);

  c_orm_db_t db_my;
  memset(&db_my, 0, sizeof(db_my));
  db_my.vtable = (const c_orm_driver_vtable_t *)&orig_vt;
  db_my.driver_name = "mysql";
  db_my.driver_data = db->driver_data;
  c_orm_migration_lock(&db_my);
  c_orm_migration_unlock(&db_my);

  db->vtable->disconnect(db);
  PASS();
}

SUITE(migrations_suite) {

  void *(*old_malloc)(size_t) = c_orm_malloc;
  void (*old_free)(void *) = c_orm_free;
  void *(*old_realloc)(void *, size_t) = c_orm_realloc;

  c_orm_malloc = m_mock_malloc;
  c_orm_free = m_mock_free;
  c_orm_realloc = m_mock_realloc;
  RUN_TEST(test_meta_diff_add_drop);
  RUN_TEST(test_c_orm_fetch_table_schema);
  RUN_TEST(test_migration_init);
  RUN_TEST(test_migrate_all_dry_run);
  RUN_TEST(test_migrate_all_execute);
  RUN_TEST(test_migrations_oom);

  c_orm_malloc = old_malloc;
  c_orm_free = old_free;
  c_orm_realloc = old_realloc;
}
