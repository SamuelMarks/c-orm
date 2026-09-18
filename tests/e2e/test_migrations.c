#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file test_migrations.c
 * @brief Unit tests for database migration runner, locking, dry-run, schema
 * introspection, and rollback flows.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_api.h"
#include "c_orm_migrations.h"
#include "c_orm_sqlite.h"
#include "c_orm_sql_to_c.h"
#include "greatest.h"
#include <stdlib.h>
#include <string.h>
/* clang-format on */

/**
 * @brief Forward declaration for test_migration_init.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_migration_init(void);

/**
 * @brief Forward declaration for test_migrate_all_dry_run.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_migrate_all_dry_run(void);

/**
 * @brief Forward declaration for test_migrate_all_execute.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_migrate_all_execute(void);

/**
 * @brief Forward declaration for test_c_orm_fetch_table_schema.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_c_orm_fetch_table_schema(void);

/**
 * @brief Forward declaration for test_migration_load_and_free_branches.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_migration_load_and_free_branches(void);

/**
 * @brief Forward declaration for test_migration_lock_unlock_branches.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_migration_lock_unlock_branches(void);

/**
 * @brief Forward declaration for test_migrate_all_failure_branches.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_migrate_all_failure_branches(void);

/**
 * @brief Forward declaration for test_migrate_rollback_failure_branches.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_migrate_rollback_failure_branches(void);

/**
 * @brief Forward declaration for test_migrate_up_down_branches.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_migrate_up_down_branches(void);

/**
 * @brief Forward declaration for test_fetch_table_schema_failure_branches.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_fetch_table_schema_failure_branches(void);

/**
 * @brief Forward declaration for test_get_applied_failure_branches.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_get_applied_failure_branches(void);

/** @brief Counter decremented before triggering OOM failure. */
static int oom_countdown = -1;

/** @brief Flag activating mock out-of-memory errors. */
static int oom_active = 0;

/**
 * @brief Mock malloc callback returning NULL on countdown expiration.
 * @param size Allocation size in bytes.
 * @return Allocated block or NULL on OOM.
 */
static void *m_mock_malloc(size_t size) {
  if (oom_active) {
    if (oom_countdown == 0) {
      oom_countdown--;
      return NULL;
    }
    oom_countdown--;
  }
  return malloc(size);
}

/**
 * @brief Mock realloc callback returning NULL on countdown expiration.
 * @param ptr Existing pointer.
 * @param size New allocation size in bytes.
 * @return Reallocated block or NULL on OOM.
 */
static void *m_mock_realloc(void *ptr, size_t size) {
  if (oom_active) {
    if (oom_countdown == 0) {
      oom_countdown--;
      return NULL;
    }
    oom_countdown--;
  }
  return realloc(ptr, size);
}

/**
 * @brief Mock free wrapper.
 * @param ptr Pointer to memory to free.
 */
static void m_mock_free(void *ptr) { free(ptr); }

/**
 * @brief Pre-migration callback to simulate hook failures.
 * @param db Database handle.
 * @param mig Migration descriptor.
 * @param user_data User data pointer.
 * @return C_ORM_OK or C_ORM_ERROR_VALIDATION on failure injection.
 */
static c_orm_error_t
my_pre_migrate(c_orm_db_t *db, const c_orm_migration_t *mig, void *user_data) {
  (void)db;
  (void)user_data;
  if (strcmp(mig->name, "fail_pre") == 0) {
    return C_ORM_ERROR_VALIDATION;
  }
  return C_ORM_OK;
}

/**
 * @brief Post-migration callback to simulate hook failures.
 * @param db Database handle.
 * @param mig Migration descriptor.
 * @param user_data User data pointer.
 * @return C_ORM_OK or C_ORM_ERROR_VALIDATION on failure injection.
 */
static c_orm_error_t
my_post_migrate(c_orm_db_t *db, const c_orm_migration_t *mig, void *user_data) {
  (void)db;
  (void)user_data;
  if (strcmp(mig->name, "fail_post") == 0) {
    return C_ORM_ERROR_VALIDATION;
  }
  return C_ORM_OK;
}

/** @brief Flag simulating table initialization failure. */
static int fail_init = 0;
/** @brief Flag simulating migration insert failure. */
static int fail_insert = 0;
/** @brief Flag simulating migration record delete failure. */
static int fail_delete = 0;
/** @brief Flag simulating applied migrations query failure. */
static int fail_applied = 0;
/** @brief Flag simulating up-migration SQL failure. */
static int fail_up = 0;
/** @brief Flag simulating down-migration SQL failure. */
static int fail_down = 0;
/** @brief Flag simulating check-applied query failure. */
static int fail_check_applied = 0;
/** @brief Flag simulating PRAGMA table_info prepare failure. */
static int fail_prep_schema = 0;
/** @brief Flag simulating applied migrations query prepare failure. */
static int fail_prep_applied = 0;
/** @brief Flag simulating SQLite lock acquisition failure. */
static int fail_sqlite_lock = 0;
/** @brief Flag simulating Postgres advisory lock failure. */
static int fail_pg_advisory = 0;
/** @brief Flag simulating generic lock failure on all drivers. */
static int fail_lock_all = 0;
/** @brief Flag stubbing Postgres lock query success with dummy query. */
static int stub_pg_lock = 0;
/** @brief Flag stubbing MySQL GET_LOCK query success with dummy query. */
static int stub_get_lock = 0;
/** @brief Flag stubbing unlock query success with dummy query. */
static int stub_unlock = 0;
/** @brief Flag simulating unlock failure. */
static int fail_unlock = 0;
/** @brief Flag simulating savepoint error during migrate_all. */
static int fail_savepoint_mig = 0;
/** @brief Flag simulating release savepoint error during migrate_all. */
static int fail_release_mig = 0;
/** @brief Flag simulating savepoint error during migrate_rollback. */
static int fail_savepoint_rb = 0;
/** @brief Flag simulating release savepoint error during migrate_rollback. */
static int fail_release_rb = 0;
/** @brief Flag simulating query step failure. */
static int fail_step = 0;
/** @brief Flag simulating query finalize failure. */
static int fail_finalize = 0;
/** @brief Flag simulating get_string retrieval failure. */
static int fail_get_string_err = 0;
/** @brief Index of column that should trigger get_string failure (-1 for all).
 */
static int fail_get_string_idx = -1;
/** @brief Index of column that should return NULL string. */
static int null_get_string_idx = -1;

/** @brief Saved original prepare function pointer. */
static c_orm_error_t (*orig_prep)(c_orm_db_t *, const char *, c_orm_query_t **);
/** @brief Saved original step function pointer. */
static c_orm_error_t (*orig_step)(c_orm_query_t *, int *);
/** @brief Saved original get_string function pointer. */
static c_orm_error_t (*orig_get_string)(c_orm_query_t *, int, const char **);
/** @brief Saved original finalize function pointer. */
static c_orm_error_t (*orig_finalize)(c_orm_query_t *);

/**
 * @brief Custom prepare callback injecting SQL failure simulations.
 * @param db_v Database handle.
 * @param sql SQL statement string.
 * @param out_query Pointer to receive query handle.
 * @return C_ORM_OK or error enum.
 */
static c_orm_error_t my_mig_prep(c_orm_db_t *db_v, const char *sql,
                                 c_orm_query_t **out_query) {
  if (fail_lock_all &&
      (strstr(sql, "BEGIN EXCLUSIVE") || strstr(sql, "pg_advisory_lock") ||
       strstr(sql, "GET_LOCK"))) {
    return C_ORM_ERROR_SQL;
  }
  if (fail_sqlite_lock && strstr(sql, "BEGIN EXCLUSIVE")) {
    return C_ORM_ERROR_SQL;
  }
  if (fail_pg_advisory && strstr(sql, "pg_advisory_lock")) {
    return C_ORM_ERROR_SQL;
  }
  if (stub_pg_lock && strstr(sql, "pg_advisory_lock")) {
    *out_query = (c_orm_query_t *)0x1234; /* dummy pointer */
    return C_ORM_OK;
  }
  if (stub_get_lock && strstr(sql, "GET_LOCK")) {
    *out_query = (c_orm_query_t *)0x1234; /* dummy pointer */
    return C_ORM_OK;
  }
  if (fail_unlock &&
      (strstr(sql, "COMMIT") || strstr(sql, "pg_advisory_unlock") ||
       strstr(sql, "RELEASE_LOCK"))) {
    return C_ORM_ERROR_SQL;
  }
  if (stub_unlock &&
      (strstr(sql, "COMMIT") || strstr(sql, "pg_advisory_unlock") ||
       strstr(sql, "RELEASE_LOCK"))) {
    *out_query = (c_orm_query_t *)0x1234; /* dummy pointer */
    return C_ORM_OK;
  }
  if (fail_check_applied &&
      strstr(sql, "SELECT id FROM _c_orm_migrations WHERE version")) {
    return C_ORM_ERROR_SQL;
  }
  if (fail_init &&
      strstr(sql, "CREATE TABLE IF NOT EXISTS _c_orm_migrations")) {
    return C_ORM_ERROR_SQL;
  }
  if (fail_insert && strstr(sql, "INSERT INTO _c_orm_migrations")) {
    return C_ORM_ERROR_SQL;
  }
  if (fail_delete && strstr(sql, "DELETE FROM _c_orm_migrations")) {
    return C_ORM_ERROR_SQL;
  }
  if (fail_applied &&
      strstr(sql, "SELECT version, name, hash FROM _c_orm_migrations")) {
    return C_ORM_ERROR_SQL;
  }
  if (fail_up && strstr(sql, "UP")) {
    return C_ORM_ERROR_SQL;
  }
  if (fail_down && strstr(sql, "DOWN")) {
    return C_ORM_ERROR_SQL;
  }
  if (fail_prep_schema && strstr(sql, "PRAGMA table_info")) {
    return C_ORM_ERROR_SQL;
  }
  if (fail_prep_applied &&
      strstr(sql, "SELECT version, name, hash FROM _c_orm_migrations")) {
    return C_ORM_ERROR_SQL;
  }
  if (fail_savepoint_mig && strstr(sql, "SAVEPOINT c_orm_mig_step") &&
      !strstr(sql, "SAVEPOINT c_orm_mig_step_rb") && !strstr(sql, "RELEASE")) {
    return C_ORM_ERROR_SQL;
  }
  if (fail_release_mig && strstr(sql, "RELEASE SAVEPOINT c_orm_mig_step") &&
      !strstr(sql, "RELEASE SAVEPOINT c_orm_mig_step_rb")) {
    return C_ORM_ERROR_SQL;
  }
  if (fail_savepoint_rb && strstr(sql, "SAVEPOINT c_orm_mig_step_rb") &&
      !strstr(sql, "RELEASE")) {
    return C_ORM_ERROR_SQL;
  }
  if (fail_release_rb && strstr(sql, "RELEASE SAVEPOINT c_orm_mig_step_rb")) {
    return C_ORM_ERROR_SQL;
  }

  return orig_prep(db_v, sql, out_query);
}

/**
 * @brief Custom step callback injecting step failure simulations.
 * @param query Query handle.
 * @param out_has_row Pointer to receive row availability indicator.
 * @return C_ORM_OK or error enum.
 */
static c_orm_error_t my_mig_step(c_orm_query_t *query, int *out_has_row) {
  if (query == (c_orm_query_t *)0x1234) {
    *out_has_row = 0;
    return C_ORM_OK;
  }
  if (fail_step) {
    return C_ORM_ERROR_SQL;
  }
  return orig_step(query, out_has_row);
}

/**
 * @brief Custom get_string callback injecting string retrieval failures.
 * @param query Query handle.
 * @param index Column index.
 * @param out_val Pointer to receive string pointer.
 * @return C_ORM_OK or error enum.
 */
static c_orm_error_t my_mig_get_string(c_orm_query_t *query, int index,
                                       const char **out_val) {
  if (fail_get_string_err &&
      (fail_get_string_idx == -1 || fail_get_string_idx == index)) {
    return C_ORM_ERROR_SQL;
  }
  if (null_get_string_idx == index) {
    *out_val = NULL;
    return C_ORM_OK;
  }
  return orig_get_string(query, index, out_val);
}

/**
 * @brief Custom finalize callback injecting finalize failures.
 * @param query Query handle.
 * @return C_ORM_OK or error enum.
 */
static c_orm_error_t my_mig_finalize(c_orm_query_t *query) {
  if (query == (c_orm_query_t *)0x1234) {
    return C_ORM_OK;
  }
  if (fail_finalize) {
    return C_ORM_ERROR_SQL;
  }
  return orig_finalize(query);
}

SUITE(migrations_suite);

/**
 * @brief Test logger callback returning success.
 * @param msg Log message string.
 * @return C_ORM_OK.
 */
static c_orm_error_t test_log_cb(const char *msg) {
  (void)msg;
  return C_ORM_OK;
}

/**
 * @brief Tests migration schema initialization and table creation.
 * @return GREATEST test result.
 */
TEST test_migration_init(void) {
  c_orm_db_t *db = NULL;
  c_orm_driver_vtable_t orig_vt;
  c_orm_driver_vtable_t mock_vt;
  c_orm_error_t err;

  err = c_orm_sqlite_connect(":memory:", &db);
  ASSERT_EQ(C_ORM_OK, err);

  err = c_orm_migration_init_table(NULL);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, err);
  err = c_orm_migration_init_table(db);
  ASSERT_EQ(C_ORM_OK, err);

  /* Test raw execute failure */
  orig_vt = *(c_orm_driver_vtable_t *)db->vtable;
  mock_vt = *(c_orm_driver_vtable_t *)db->vtable;
  orig_prep = mock_vt.prepare;
  mock_vt.prepare = my_mig_prep;
  orig_finalize = mock_vt.finalize;
  mock_vt.finalize = my_mig_finalize;
  db->vtable = (const c_orm_driver_vtable_t *)&mock_vt;

  fail_init = 1;
  err = c_orm_migration_init_table(db);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_init = 0;

  db->vtable = (const c_orm_driver_vtable_t *)&orig_vt;
  db->vtable->disconnect(db);
  PASS();
}

/**
 * @brief Tests migration runner dry-run mode without applying schema changes.
 * @return GREATEST test result.
 */
TEST test_migrate_all_dry_run(void) {
  c_orm_db_t *db = NULL;
  c_orm_error_t err;
  c_orm_migration_options_t opts;
  c_orm_migration_t migs[2];

  memset(&opts, 0, sizeof(opts));
  opts.dry_run = 1;
  opts.log_cb = test_log_cb;

  memset(&migs, 0, sizeof(migs));
  C_ORM_STRCPY(migs[0].version, sizeof(migs[0].version), "20260330010000");
  C_ORM_STRCPY(migs[0].name, sizeof(migs[0].name), "create_users");
  migs[0].up_sql = "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT);";

  C_ORM_STRCPY(migs[1].version, sizeof(migs[1].version), "20260330010001");
  C_ORM_STRCPY(migs[1].name, sizeof(migs[1].name), "create_posts");
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

/**
 * @brief Tests applying all forward migrations and idempotence on repeated
 * runs.
 * @return GREATEST test result.
 */
TEST test_migrate_all_execute(void) {
  c_orm_db_t *db = NULL;
  c_orm_error_t err;
  c_orm_migration_options_t opts;
  c_orm_migration_t migs[1];

  memset(&opts, 0, sizeof(opts));
  opts.dry_run = 0;

  memset(&migs, 0, sizeof(migs));
  C_ORM_STRCPY(migs[0].version, sizeof(migs[0].version), "20260330010000");
  C_ORM_STRCPY(migs[0].name, sizeof(migs[0].name), "create_items");
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

/**
 * @brief Tests introspecting table schema definitions from database catalogs.
 * @return GREATEST test result.
 */
TEST test_c_orm_fetch_table_schema(void) {
  c_orm_db_t *db = NULL;
  c_orm_error_t err;
  cdd_c_meta_t *schema = NULL;
  int found_id;
  int found_name;
  size_t i;

  found_id = 0;
  found_name = 0;

  err = c_orm_sqlite_connect(":memory:", &db);
  ASSERT_EQ(C_ORM_OK, err);

  err = c_orm_execute_raw(
      db, "CREATE TABLE authors (id INTEGER PRIMARY KEY, name TEXT);");
  ASSERT_EQ(C_ORM_OK, err);

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

/**
 * @brief Tests loading migrations from directories and memory cleanup routines.
 * @return GREATEST test result.
 */
TEST test_migration_load_and_free_branches(void) {
  c_orm_migration_t *migs = NULL;
  size_t count;
  c_orm_error_t err;
  cdd_c_meta_t *schema = NULL;
  cdd_c_prop_meta_t *props_buf = NULL;

  count = 0;

  /* Validation checks for load_dir */
  err = c_orm_migration_load_dir(NULL, &migs, &count);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, err);
  err = c_orm_migration_load_dir("path", NULL, &count);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, err);
  err = c_orm_migration_load_dir("path", &migs, NULL);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, err);

  /* Load empty migrations */
  err = c_orm_migration_load_dir("empty_migrations_cli", &migs, &count);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_EQ(0, count);
  ASSERT_EQ(NULL, migs);

  /* Load real migrations stub */
  err = c_orm_migration_load_dir("real_migrations_cli", &migs, &count);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_EQ(1, count);
  ASSERT(migs != NULL);
  c_orm_migration_free_array(migs, count);
  migs = NULL;

  /* Load unknown directory */
  err = c_orm_migration_load_dir("unknown_directory_path", &migs, &count);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);

  /* Free array branches */
  c_orm_migration_free_array(NULL, 0);
  migs = (c_orm_migration_t *)malloc(sizeof(c_orm_migration_t));
  memset(migs, 0, sizeof(c_orm_migration_t));
  /* No up_sql and no down_sql */
  c_orm_migration_free_array(migs, 1);

  /* Free table schema branches */
  c_orm_migration_free_table_schema(NULL);

  schema = (cdd_c_meta_t *)malloc(sizeof(cdd_c_meta_t));
  memset(schema, 0, sizeof(cdd_c_meta_t));
  c_orm_migration_free_table_schema(schema);

  schema = (cdd_c_meta_t *)malloc(sizeof(cdd_c_meta_t));
  memset(schema, 0, sizeof(cdd_c_meta_t));
  props_buf = (cdd_c_prop_meta_t *)malloc(sizeof(cdd_c_prop_meta_t));
  memset(props_buf, 0, sizeof(cdd_c_prop_meta_t));
  schema->props = props_buf;
  schema->num_props = 1;
  c_orm_migration_free_table_schema(schema);

  PASS();
}

/**
 * @brief Tests migration distributed locking and unlocking across driver types.
 * @return GREATEST test result.
 */
TEST test_migration_lock_unlock_branches(void) {
  c_orm_db_t *db = NULL;
  c_orm_db_t db_mem;
  c_orm_db_t db_pg;
  c_orm_db_t db_my;
  c_orm_db_t db_unk;
  c_orm_driver_vtable_t orig_vt;
  c_orm_driver_vtable_t mock_vt;
  c_orm_error_t err;

  err = c_orm_sqlite_connect(":memory:", &db);
  ASSERT_EQ(C_ORM_OK, err);

  orig_vt = *(c_orm_driver_vtable_t *)db->vtable;
  mock_vt = *(c_orm_driver_vtable_t *)db->vtable;
  orig_prep = mock_vt.prepare;
  mock_vt.prepare = my_mig_prep;
  orig_step = mock_vt.step;
  mock_vt.step = my_mig_step;
  orig_get_string = mock_vt.get_string;
  mock_vt.get_string = my_mig_get_string;
  orig_finalize = mock_vt.finalize;
  mock_vt.finalize = my_mig_finalize;
  db->vtable = (const c_orm_driver_vtable_t *)&mock_vt;

  /* Validation checks */
  err = c_orm_migration_lock(NULL);
  ASSERT_NEQ(C_ORM_OK, err);

  err = c_orm_migration_unlock(NULL);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, err);

  memset(&db_unk, 0, sizeof(db_unk));
  err = c_orm_migration_unlock(&db_unk);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, err);

  /* Lock branches */
  /* 1. BEGIN EXCLUSIVE succeeds (sqlite) */
  err = c_orm_migration_lock(db);
  ASSERT_EQ(C_ORM_OK, err);
  err = c_orm_migration_unlock(db);
  ASSERT_EQ(C_ORM_OK, err);

  /* 2. BEGIN EXCLUSIVE fails, pg_advisory_lock succeeds */
  fail_sqlite_lock = 1;
  stub_pg_lock = 1;
  err = c_orm_migration_lock(db);
  ASSERT_EQ(C_ORM_OK, err);
  fail_sqlite_lock = 0;
  stub_pg_lock = 0;

  /* 3. BEGIN EXCLUSIVE fails, pg_advisory_lock fails, GET_LOCK succeeds */
  fail_sqlite_lock = 1;
  fail_pg_advisory = 1;
  stub_get_lock = 1;
  err = c_orm_migration_lock(db);
  ASSERT_EQ(C_ORM_OK, err);
  fail_sqlite_lock = 0;
  fail_pg_advisory = 0;
  stub_get_lock = 0;

  /* 4. All lock attempts fail */
  fail_lock_all = 1;
  err = c_orm_migration_lock(db);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_lock_all = 0;

  /* Unlock branches */
  /* SQLite COMMIT fails */
  fail_unlock = 1;
  err = c_orm_migration_unlock(db);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_unlock = 0;

  /* Memory driver */
  memset(&db_mem, 0, sizeof(db_mem));
  db_mem.vtable = (const c_orm_driver_vtable_t *)&mock_vt;
  db_mem.driver_name = "memory";
  db_mem.driver_data = db->driver_data;
  stub_unlock = 1;
  err = c_orm_migration_unlock(&db_mem);
  ASSERT_EQ(C_ORM_OK, err);
  fail_unlock = 1;
  err = c_orm_migration_unlock(&db_mem);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_unlock = 0;
  stub_unlock = 0;

  /* Postgres driver */
  memset(&db_pg, 0, sizeof(db_pg));
  db_pg.vtable = (const c_orm_driver_vtable_t *)&mock_vt;
  db_pg.driver_name = "postgres";
  db_pg.driver_data = db->driver_data;
  stub_unlock = 1;
  err = c_orm_migration_unlock(&db_pg);
  ASSERT_EQ(C_ORM_OK, err);
  fail_unlock = 1;
  err = c_orm_migration_unlock(&db_pg);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_unlock = 0;
  stub_unlock = 0;

  /* MySQL driver */
  memset(&db_my, 0, sizeof(db_my));
  db_my.vtable = (const c_orm_driver_vtable_t *)&mock_vt;
  db_my.driver_name = "mysql";
  db_my.driver_data = db->driver_data;
  stub_unlock = 1;
  err = c_orm_migration_unlock(&db_my);
  ASSERT_EQ(C_ORM_OK, err);
  fail_unlock = 1;
  err = c_orm_migration_unlock(&db_my);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_unlock = 0;
  stub_unlock = 0;

  /* Unknown driver */
  db_unk.driver_name = "oracle";
  err = c_orm_migration_unlock(&db_unk);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);

  db->vtable = (const c_orm_driver_vtable_t *)&orig_vt;
  db->vtable->disconnect(db);
  PASS();
}

/**
 * @brief Tests failure branches during migration execution including locks, SQL
 * errors, and hook rejections.
 * @return GREATEST test result.
 */
TEST test_migrate_all_failure_branches(void) {
  c_orm_db_t *db = NULL;
  c_orm_driver_vtable_t orig_vt;
  c_orm_driver_vtable_t mock_vt;
  c_orm_migration_options_t opts;
  c_orm_migration_t mig;
  c_orm_error_t err;

  err = c_orm_sqlite_connect(":memory:", &db);
  ASSERT_EQ(C_ORM_OK, err);

  orig_vt = *(c_orm_driver_vtable_t *)db->vtable;
  mock_vt = *(c_orm_driver_vtable_t *)db->vtable;
  orig_prep = mock_vt.prepare;
  mock_vt.prepare = my_mig_prep;
  orig_step = mock_vt.step;
  mock_vt.step = my_mig_step;
  orig_get_string = mock_vt.get_string;
  mock_vt.get_string = my_mig_get_string;
  orig_finalize = mock_vt.finalize;
  mock_vt.finalize = my_mig_finalize;
  db->vtable = (const c_orm_driver_vtable_t *)&mock_vt;

  memset(&mig, 0, sizeof(mig));
  C_ORM_STRCPY(mig.version, sizeof(mig.version), "101");
  C_ORM_STRCPY(mig.name, sizeof(mig.name), "test_mig");
  mig.up_sql = "SELECT 1;";

  memset(&opts, 0, sizeof(opts));

  /* Validation checks */
  err = c_orm_migrate_all(NULL, &mig, 1, &opts);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, err);
  err = c_orm_migrate_all(db, NULL, 1, &opts);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, err);

  /* Lock failure */
  fail_lock_all = 1;
  err = c_orm_migrate_all(db, &mig, 1, &opts);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_lock_all = 0;

  /* Init table failure where unlock succeeds */
  C_ORM_STRCPY(mig.version, sizeof(mig.version), "101");
  fail_init = 1;
  fail_unlock = 0;
  err = c_orm_migrate_all(db, &mig, 1, &opts);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);

  /* Init table failure where unlock fails */
  C_ORM_STRCPY(mig.version, sizeof(mig.version), "102");
  fail_init = 1;
  fail_unlock = 1;
  err = c_orm_migrate_all(db, &mig, 1, &opts);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_init = 0;
  fail_unlock = 0;
  err = c_orm_execute_raw(db, "ROLLBACK");
  ASSERT(err == C_ORM_OK || err == C_ORM_ERROR_SQL);

  /* pre_migrate failure where unlock succeeds */
  C_ORM_STRCPY(mig.version, sizeof(mig.version), "103");
  opts.pre_migrate = my_pre_migrate;
  C_ORM_STRCPY(mig.name, sizeof(mig.name), "fail_pre");
  fail_unlock = 0;
  err = c_orm_migrate_all(db, &mig, 1, &opts);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, err);

  /* pre_migrate failure where unlock fails */
  C_ORM_STRCPY(mig.version, sizeof(mig.version), "104");
  fail_unlock = 1;
  err = c_orm_migrate_all(db, &mig, 1, &opts);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_unlock = 0;
  err = c_orm_execute_raw(db, "ROLLBACK");
  ASSERT(err == C_ORM_OK || err == C_ORM_ERROR_SQL);

  /* pre_migrate success branch (hits line 215) */
  C_ORM_STRCPY(mig.version, sizeof(mig.version), "105");
  C_ORM_STRCPY(mig.name, sizeof(mig.name), "good_pre");
  err = c_orm_migrate_all(db, &mig, 1, &opts);
  ASSERT_EQ(C_ORM_OK, err);
  opts.pre_migrate = NULL;

  /* up_sql failure where unlock succeeds */
  C_ORM_STRCPY(mig.version, sizeof(mig.version), "106");
  C_ORM_STRCPY(mig.name, sizeof(mig.name), "test_up_fail");
  mig.up_sql = "SELECT 1; /* UP */";
  fail_up = 1;
  fail_unlock = 0;
  err = c_orm_migrate_all(db, &mig, 1, &opts);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);

  /* up_sql failure where unlock fails */
  C_ORM_STRCPY(mig.version, sizeof(mig.version), "107");
  fail_unlock = 1;
  err = c_orm_migrate_all(db, &mig, 1, &opts);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_up = 0;
  fail_unlock = 0;
  err = c_orm_execute_raw(db, "ROLLBACK");
  ASSERT(err == C_ORM_OK || err == C_ORM_ERROR_SQL);

  /* insert migration record failure where unlock succeeds */
  C_ORM_STRCPY(mig.version, sizeof(mig.version), "108");
  mig.up_sql = "SELECT 1;";
  fail_insert = 1;
  fail_unlock = 0;
  err = c_orm_migrate_all(db, &mig, 1, &opts);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);

  /* insert migration record failure where unlock fails */
  C_ORM_STRCPY(mig.version, sizeof(mig.version), "109");
  fail_unlock = 1;
  err = c_orm_migrate_all(db, &mig, 1, &opts);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_insert = 0;
  fail_unlock = 0;
  err = c_orm_execute_raw(db, "ROLLBACK");
  ASSERT(err == C_ORM_OK || err == C_ORM_ERROR_SQL);

  /* post_migrate failure where unlock succeeds */
  C_ORM_STRCPY(mig.version, sizeof(mig.version), "110");
  opts.post_migrate = my_post_migrate;
  C_ORM_STRCPY(mig.name, sizeof(mig.name), "fail_post");
  fail_unlock = 0;
  err = c_orm_migrate_all(db, &mig, 1, &opts);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, err);

  /* post_migrate failure where unlock fails */
  C_ORM_STRCPY(mig.version, sizeof(mig.version), "111");
  fail_unlock = 1;
  err = c_orm_migrate_all(db, &mig, 1, &opts);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_unlock = 0;
  err = c_orm_execute_raw(db, "ROLLBACK");
  ASSERT(err == C_ORM_OK || err == C_ORM_ERROR_SQL);

  /* post_migrate success branch (hits line 262) */
  C_ORM_STRCPY(mig.version, sizeof(mig.version), "112");
  C_ORM_STRCPY(mig.name, sizeof(mig.name), "good_post");
  err = c_orm_migrate_all(db, &mig, 1, &opts);
  ASSERT_EQ(C_ORM_OK, err);
  opts.post_migrate = NULL;

  /* Final unlock failure on success path */
  C_ORM_STRCPY(mig.version, sizeof(mig.version), "113");
  C_ORM_STRCPY(mig.name, sizeof(mig.name), "good_name");
  fail_unlock = 1;
  err = c_orm_migrate_all(db, &mig, 1, &opts);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_unlock = 0;
  err = c_orm_execute_raw(db, "ROLLBACK");
  ASSERT(err == C_ORM_OK || err == C_ORM_ERROR_SQL);

  /* Empty hash and empty up_sql branch */
  mig.hash[0] = '\0';
  mig.up_sql = "";
  C_ORM_STRCPY(mig.version, sizeof(mig.version), "114");
  err = c_orm_migrate_all(db, &mig, 1, NULL);
  ASSERT_EQ(C_ORM_OK, err);

  /* NULL up_sql */
  mig.up_sql = NULL;
  C_ORM_STRCPY(mig.version, sizeof(mig.version), "115");
  err = c_orm_migrate_all(db, &mig, 1, NULL);
  ASSERT_EQ(C_ORM_OK, err);

  /* Check applied prepare failure (hits line 183 false branch) */
  mig.up_sql = "SELECT 1;";
  C_ORM_STRCPY(mig.version, sizeof(mig.version), "116");
  fail_check_applied = 1;
  err = c_orm_migrate_all(db, &mig, 1, NULL);
  ASSERT_EQ(C_ORM_OK, err);
  fail_check_applied = 0;

  /* Savepoint failure during migrate_all where unlock succeeds */
  C_ORM_STRCPY(mig.version, sizeof(mig.version), "117");
  fail_savepoint_mig = 1;
  fail_unlock = 0;
  err = c_orm_migrate_all(db, &mig, 1, &opts);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);

  /* Savepoint failure during migrate_all where unlock fails */
  C_ORM_STRCPY(mig.version, sizeof(mig.version), "118");
  fail_unlock = 1;
  err = c_orm_migrate_all(db, &mig, 1, &opts);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_savepoint_mig = 0;
  fail_unlock = 0;
  err = c_orm_execute_raw(db, "ROLLBACK");
  ASSERT(err == C_ORM_OK || err == C_ORM_ERROR_SQL);

  /* Release savepoint failure during migrate_all where unlock succeeds */
  C_ORM_STRCPY(mig.version, sizeof(mig.version), "119");
  fail_release_mig = 1;
  fail_unlock = 0;
  err = c_orm_migrate_all(db, &mig, 1, &opts);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);

  /* Release savepoint failure during migrate_all where unlock fails */
  C_ORM_STRCPY(mig.version, sizeof(mig.version), "120");
  fail_unlock = 1;
  err = c_orm_migrate_all(db, &mig, 1, &opts);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_release_mig = 0;
  fail_unlock = 0;
  err = c_orm_execute_raw(db, "ROLLBACK");
  ASSERT(err == C_ORM_OK || err == C_ORM_ERROR_SQL);

  db->vtable = (const c_orm_driver_vtable_t *)&orig_vt;
  db->vtable->disconnect(db);
  PASS();
}

/**
 * @brief Tests failure branches during migration rollbacks and step bounds.
 * @return GREATEST test result.
 */
TEST test_migrate_rollback_failure_branches(void) {
  c_orm_db_t *db = NULL;
  c_orm_db_t *empty_db = NULL;
  c_orm_driver_vtable_t orig_vt;
  c_orm_driver_vtable_t mock_vt;
  c_orm_migration_options_t opts;
  c_orm_migration_t mig;
  c_orm_error_t err;

  err = c_orm_sqlite_connect(":memory:", &db);
  ASSERT_EQ(C_ORM_OK, err);

  memset(&mig, 0, sizeof(mig));
  C_ORM_STRCPY(mig.version, sizeof(mig.version), "201");
  C_ORM_STRCPY(mig.name, sizeof(mig.name), "rollback_test");
  mig.up_sql = "SELECT 1;";
  mig.down_sql = "SELECT 1;";

  memset(&opts, 0, sizeof(opts));

  /* First apply a migration so we can roll it back */
  err = c_orm_migrate_all(db, &mig, 1, &opts);
  ASSERT_EQ(C_ORM_OK, err);

  orig_vt = *(c_orm_driver_vtable_t *)db->vtable;
  mock_vt = *(c_orm_driver_vtable_t *)db->vtable;
  orig_prep = mock_vt.prepare;
  mock_vt.prepare = my_mig_prep;
  orig_step = mock_vt.step;
  mock_vt.step = my_mig_step;
  orig_get_string = mock_vt.get_string;
  mock_vt.get_string = my_mig_get_string;
  orig_finalize = mock_vt.finalize;
  mock_vt.finalize = my_mig_finalize;
  db->vtable = (const c_orm_driver_vtable_t *)&mock_vt;

  /* Validation checks */
  err = c_orm_migrate_rollback(NULL, &mig, 1, 1, &opts);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, err);
  err = c_orm_migrate_rollback(db, NULL, 1, 1, &opts);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, err);

  /* Lock failure */
  fail_lock_all = 1;
  err = c_orm_migrate_rollback(db, &mig, 1, 1, &opts);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_lock_all = 0;

  /* get_applied failure where unlock succeeds */
  fail_applied = 1;
  fail_unlock = 0;
  err = c_orm_migrate_rollback(db, &mig, 1, 1, &opts);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);

  /* get_applied failure where unlock fails */
  fail_unlock = 1;
  err = c_orm_migrate_rollback(db, &mig, 1, 1, &opts);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_applied = 0;
  fail_unlock = 0;
  err = c_orm_execute_raw(db, "ROLLBACK");
  ASSERT(err == C_ORM_OK || err == C_ORM_ERROR_SQL);

  /* Migration in db not found in local migrations array */
  {
    c_orm_migration_t other_mig;
    memset(&other_mig, 0, sizeof(other_mig));
    C_ORM_STRCPY(other_mig.version, sizeof(other_mig.version), "999");
    err = c_orm_migrate_rollback(db, &other_mig, 1, 1, &opts);
    ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, err);
  }

  /* Rollback with log_cb and dry_run = 1 (hits lines 342-345, 348-349) */
  opts.log_cb = test_log_cb;
  opts.dry_run = 1;
  err = c_orm_migrate_rollback(db, &mig, 1, 1, &opts);
  ASSERT_EQ(C_ORM_OK, err);
  opts.log_cb = NULL;
  opts.dry_run = 0;

  /* down_sql failure */
  mig.down_sql = "SELECT 1; /* DOWN */";
  fail_down = 1;
  err = c_orm_migrate_rollback(db, &mig, 1, 1, &opts);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_down = 0;

  /* delete failure */
  mig.down_sql = "SELECT 1;";
  fail_delete = 1;
  err = c_orm_migrate_rollback(db, &mig, 1, 1, &opts);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_delete = 0;

  /* Final unlock failure */
  fail_unlock = 1;
  err = c_orm_migrate_rollback(db, &mig, 1, 1, &opts);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_unlock = 0;
  err = c_orm_execute_raw(db, "ROLLBACK");
  ASSERT(err == C_ORM_OK || err == C_ORM_ERROR_SQL);

  /* NULL and empty down_sql */
  mig.down_sql = NULL;
  err = c_orm_migrate_rollback(db, &mig, 1, 1, &opts);
  ASSERT_EQ(C_ORM_OK, err);

  /* Re-apply and rollback with empty down_sql and steps == 0 */
  err = c_orm_migrate_all(db, &mig, 1, &opts);
  ASSERT_EQ(C_ORM_OK, err);
  mig.down_sql = "";
  err = c_orm_migrate_rollback(db, &mig, 1, 0, &opts);
  ASSERT_EQ(C_ORM_OK, err);

  /* Savepoint failure during rollback */
  mig.down_sql = "SELECT 1;";
  err = c_orm_migrate_all(db, &mig, 1, &opts);
  ASSERT_EQ(C_ORM_OK, err);
  fail_savepoint_rb = 1;
  err = c_orm_migrate_rollback(db, &mig, 1, 1, &opts);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_savepoint_rb = 0;

  /* Release savepoint failure during rollback */
  err = c_orm_migrate_all(db, &mig, 1, &opts);
  ASSERT_EQ(C_ORM_OK, err);
  fail_release_rb = 1;
  err = c_orm_migrate_rollback(db, &mig, 1, 1, &opts);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_release_rb = 0;

  /* Rollback on empty database where applied is NULL (hits line 376 false
   * branch) */
  err = c_orm_sqlite_connect(":memory:", &empty_db);
  ASSERT_EQ(C_ORM_OK, err);
  err = c_orm_migration_init_table(empty_db);
  ASSERT_EQ(C_ORM_OK, err);
  err = c_orm_migrate_rollback(empty_db, &mig, 1, 1, NULL);
  ASSERT_EQ(C_ORM_OK, err);
  empty_db->vtable->disconnect(empty_db);

  db->vtable = (const c_orm_driver_vtable_t *)&orig_vt;
  db->vtable->disconnect(db);
  PASS();
}

/**
 * @brief Tests migrate up and down directory dispatch routines and parameter
 * validation.
 * @return GREATEST test result.
 */
TEST test_migrate_up_down_branches(void) {
  c_orm_db_t *db = NULL;
  c_orm_error_t err;

  err = c_orm_sqlite_connect(":memory:", &db);
  ASSERT_EQ(C_ORM_OK, err);

  /* Validation */
  err = c_orm_migrate_up(NULL, "path", NULL);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, err);
  err = c_orm_migrate_up(db, NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, err);

  err = c_orm_migrate_down(NULL, "path", 1, NULL);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, err);
  err = c_orm_migrate_down(db, NULL, 1, NULL);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, err);

  /* Load dir failure */
  err = c_orm_migrate_up(db, "unknown_dir", NULL);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);

  err = c_orm_migrate_down(db, "unknown_dir", 1, NULL);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, err);

  /* Success with real_migrations_cli */
  err = c_orm_migrate_up(db, "real_migrations_cli", NULL);
  ASSERT_EQ(C_ORM_OK, err);

  err = c_orm_migrate_down(db, "real_migrations_cli", 1, NULL);
  ASSERT_EQ(C_ORM_OK, err);

  db->vtable->disconnect(db);
  PASS();
}

/**
 * @brief Tests failure branches and memory allocation failures during schema
 * introspection.
 * @return GREATEST test result.
 */
TEST test_fetch_table_schema_failure_branches(void) {
  c_orm_db_t *db = NULL;
  c_orm_driver_vtable_t orig_vt;
  c_orm_driver_vtable_t mock_vt;
  cdd_c_meta_t *schema = NULL;
  c_orm_error_t err;
  int i;

  err = c_orm_sqlite_connect(":memory:", &db);
  ASSERT_EQ(C_ORM_OK, err);

  err = c_orm_execute_raw(
      db, "CREATE TABLE schema_test (c1 INT, c2 INT, c3 INT, c4 INT, c5 INT, "
          "c6 INT, c7 INT, c8 INT, c9 INT, c10 INT, c11 INT, c12 INT);");
  ASSERT_EQ(C_ORM_OK, err);

  orig_vt = *(c_orm_driver_vtable_t *)db->vtable;
  mock_vt = *(c_orm_driver_vtable_t *)db->vtable;
  orig_prep = mock_vt.prepare;
  mock_vt.prepare = my_mig_prep;
  orig_step = mock_vt.step;
  mock_vt.step = my_mig_step;
  orig_get_string = mock_vt.get_string;
  mock_vt.get_string = my_mig_get_string;
  orig_finalize = mock_vt.finalize;
  mock_vt.finalize = my_mig_finalize;
  db->vtable = (const c_orm_driver_vtable_t *)&mock_vt;

  /* Validation */
  err = c_orm_migration_fetch_table_schema(NULL, "schema_test", &schema);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  err = c_orm_migration_fetch_table_schema(db, NULL, &schema);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  err = c_orm_migration_fetch_table_schema(db, "schema_test", NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);

  /* Prepare failure */
  fail_prep_schema = 1;
  err = c_orm_migration_fetch_table_schema(db, "schema_test", &schema);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_prep_schema = 0;

  /* Step failure: finalize OK vs finalize error */
  fail_step = 1;
  fail_finalize = 0;
  err = c_orm_migration_fetch_table_schema(db, "schema_test", &schema);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_finalize = 1;
  err = c_orm_migration_fetch_table_schema(db, "schema_test", &schema);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_step = 0;
  fail_finalize = 0;

  /* get_string(1) (col_name) failure: finalize OK vs finalize error */
  fail_get_string_err = 1;
  fail_get_string_idx = 1;
  fail_finalize = 0;
  err = c_orm_migration_fetch_table_schema(db, "schema_test", &schema);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_finalize = 1;
  err = c_orm_migration_fetch_table_schema(db, "schema_test", &schema);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_get_string_err = 0;
  fail_get_string_idx = -1;
  fail_finalize = 0;

  /* get_string(2) (col_type) failure: finalize OK vs finalize error */
  fail_get_string_err = 1;
  fail_get_string_idx = 2;
  fail_finalize = 0;
  err = c_orm_migration_fetch_table_schema(db, "schema_test", &schema);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_finalize = 1;
  err = c_orm_migration_fetch_table_schema(db, "schema_test", &schema);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_get_string_err = 0;
  fail_get_string_idx = -1;
  fail_finalize = 0;

  /* NULL col_name / NULL col_type */
  null_get_string_idx = 1;
  schema = NULL;
  err = c_orm_migration_fetch_table_schema(db, "schema_test", &schema);
  ASSERT_EQ(C_ORM_OK, err);
  if (schema) {
    c_orm_migration_free_table_schema(schema);
    schema = NULL;
  }
  null_get_string_idx = 2;
  err = c_orm_migration_fetch_table_schema(db, "schema_test", &schema);
  ASSERT_EQ(C_ORM_OK, err);
  if (schema) {
    c_orm_migration_free_table_schema(schema);
    schema = NULL;
  }
  null_get_string_idx = -1;

  /* Final finalize error on success loop */
  fail_finalize = 1;
  schema = NULL;
  err = c_orm_migration_fetch_table_schema(db, "schema_test", &schema);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_finalize = 0;

  /* OOM countdown iterations: test every malloc / realloc */
  for (i = 0; i < 35; i++) {
    schema = NULL;
    oom_active = 1;
    oom_countdown = i;
    fail_finalize = 0;
    err = c_orm_migration_fetch_table_schema(db, "schema_test", &schema);
    if (err == C_ORM_OK) {
      ASSERT(schema != NULL);
    }
    oom_active = 0;
    if (schema) {
      c_orm_migration_free_table_schema(schema);
      schema = NULL;
    }
  }

  /* OOM with finalize failure */
  for (i = 0; i < 35; i++) {
    schema = NULL;
    oom_active = 1;
    oom_countdown = i;
    fail_finalize = 1;
    err = c_orm_migration_fetch_table_schema(db, "schema_test", &schema);
    ASSERT(err != C_ORM_OK);
    oom_active = 0;
    fail_finalize = 0;
  }

  db->vtable = (const c_orm_driver_vtable_t *)&orig_vt;
  db->vtable->disconnect(db);
  PASS();
}

/**
 * @brief Tests failure branches and memory allocation failures during applied
 * migrations query.
 * @return GREATEST test result.
 */
TEST test_get_applied_failure_branches(void) {
  c_orm_db_t *db = NULL;
  c_orm_driver_vtable_t orig_vt;
  c_orm_driver_vtable_t mock_vt;
  c_orm_migration_t *migs = NULL;
  size_t count;
  c_orm_error_t err;
  int i, j;

  count = 0;

  err = c_orm_sqlite_connect(":memory:", &db);
  ASSERT_EQ(C_ORM_OK, err);

  err = c_orm_migration_init_table(db);
  ASSERT_EQ(C_ORM_OK, err);

  /* Insert 12 migrations to force capacity realloc (initial cap is 10) */
  for (j = 0; j < 12; j++) {
    char buf[128];
    C_ORM_SPRINTF(buf, sizeof(buf),
                  "INSERT INTO _c_orm_migrations (version, name, hash) "
                  "VALUES ('%d', 'name_%d', 'hash_%d')",
                  1000 + j, j, j);
    err = c_orm_execute_raw(db, buf);
    ASSERT_EQ(C_ORM_OK, err);
  }

  orig_vt = *(c_orm_driver_vtable_t *)db->vtable;
  mock_vt = *(c_orm_driver_vtable_t *)db->vtable;
  orig_prep = mock_vt.prepare;
  mock_vt.prepare = my_mig_prep;
  orig_step = mock_vt.step;
  mock_vt.step = my_mig_step;
  orig_get_string = mock_vt.get_string;
  mock_vt.get_string = my_mig_get_string;
  orig_finalize = mock_vt.finalize;
  mock_vt.finalize = my_mig_finalize;
  db->vtable = (const c_orm_driver_vtable_t *)&mock_vt;

  /* Validation */
  err = c_orm_migration_get_applied(NULL, &migs, &count);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, err);
  err = c_orm_migration_get_applied(db, NULL, &count);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, err);
  err = c_orm_migration_get_applied(db, &migs, NULL);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, err);

  /* Prepare failure */
  fail_prep_applied = 1;
  err = c_orm_migration_get_applied(db, &migs, &count);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_prep_applied = 0;

  /* Step failure: finalize OK vs finalize error */
  fail_step = 1;
  fail_finalize = 0;
  err = c_orm_migration_get_applied(db, &migs, &count);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_finalize = 1;
  err = c_orm_migration_get_applied(db, &migs, &count);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_step = 0;
  fail_finalize = 0;

  /* get_string(0) (version) failure: finalize OK vs finalize error */
  fail_get_string_err = 1;
  fail_get_string_idx = 0;
  fail_finalize = 0;
  err = c_orm_migration_get_applied(db, &migs, &count);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_finalize = 1;
  err = c_orm_migration_get_applied(db, &migs, &count);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_get_string_err = 0;
  fail_get_string_idx = -1;
  fail_finalize = 0;

  /* get_string(1) (name) failure: finalize OK vs finalize error */
  fail_get_string_err = 1;
  fail_get_string_idx = 1;
  fail_finalize = 0;
  err = c_orm_migration_get_applied(db, &migs, &count);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_finalize = 1;
  err = c_orm_migration_get_applied(db, &migs, &count);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_get_string_err = 0;
  fail_get_string_idx = -1;
  fail_finalize = 0;

  /* get_string(2) (hash) failure: finalize OK vs finalize error */
  fail_get_string_err = 1;
  fail_get_string_idx = 2;
  fail_finalize = 0;
  err = c_orm_migration_get_applied(db, &migs, &count);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_finalize = 1;
  err = c_orm_migration_get_applied(db, &migs, &count);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_get_string_err = 0;
  fail_get_string_idx = -1;
  fail_finalize = 0;

  /* NULL version, NULL name, NULL hash */
  null_get_string_idx = 0;
  migs = NULL;
  count = 0;
  err = c_orm_migration_get_applied(db, &migs, &count);
  ASSERT_EQ(C_ORM_OK, err);
  c_orm_migration_free_array(migs, count);

  null_get_string_idx = 1;
  migs = NULL;
  count = 0;
  err = c_orm_migration_get_applied(db, &migs, &count);
  ASSERT_EQ(C_ORM_OK, err);
  c_orm_migration_free_array(migs, count);

  null_get_string_idx = 2;
  migs = NULL;
  count = 0;
  err = c_orm_migration_get_applied(db, &migs, &count);
  ASSERT_EQ(C_ORM_OK, err);
  c_orm_migration_free_array(migs, count);
  null_get_string_idx = -1;

  /* Final finalize error on success loop */
  fail_finalize = 1;
  migs = NULL;
  count = 0;
  err = c_orm_migration_get_applied(db, &migs, &count);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_finalize = 0;

  /* Normal get_applied with 12 rows (tests realloc success) */
  migs = NULL;
  count = 0;
  err = c_orm_migration_get_applied(db, &migs, &count);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_EQ(12, count);
  c_orm_migration_free_array(migs, count);
  migs = NULL;

  /* OOM countdown iterations */
  for (i = 0; i < 25; i++) {
    migs = NULL;
    count = 0;
    oom_active = 1;
    oom_countdown = i;
    fail_finalize = 0;
    err = c_orm_migration_get_applied(db, &migs, &count);
    ASSERT(err == C_ORM_OK || err == C_ORM_ERROR_MEMORY ||
           err == C_ORM_ERROR_SQL);
    oom_active = 0;
    if (migs) {
      c_orm_migration_free_array(migs, count);
      migs = NULL;
    }
  }

  /* OOM with finalize error */
  for (i = 0; i < 25; i++) {
    migs = NULL;
    count = 0;
    oom_active = 1;
    oom_countdown = i;
    fail_finalize = 1;
    err = c_orm_migration_get_applied(db, &migs, &count);
    ASSERT(err == C_ORM_OK || err == C_ORM_ERROR_MEMORY ||
           err == C_ORM_ERROR_SQL);
    oom_active = 0;
    fail_finalize = 0;
  }

  db->vtable = (const c_orm_driver_vtable_t *)&orig_vt;
  db->vtable->disconnect(db);
  PASS();
}

/**
 * @brief Test suite registering migration framework test cases.
 */
SUITE(migrations_suite) {
  void *(*old_malloc)(size_t);
  void *(*old_realloc)(void *, size_t);
  void (*old_free)(void *);

  old_malloc = c_orm_malloc;
  old_realloc = c_orm_realloc;
  old_free = c_orm_free;

  c_orm_set_allocators(m_mock_malloc, m_mock_realloc, m_mock_free);

  RUN_TEST(test_c_orm_fetch_table_schema);
  RUN_TEST(test_migration_init);
  RUN_TEST(test_migrate_all_dry_run);
  RUN_TEST(test_migrate_all_execute);
  RUN_TEST(test_migration_load_and_free_branches);
  RUN_TEST(test_migration_lock_unlock_branches);
  RUN_TEST(test_migrate_all_failure_branches);
  RUN_TEST(test_migrate_rollback_failure_branches);
  RUN_TEST(test_migrate_up_down_branches);
  RUN_TEST(test_fetch_table_schema_failure_branches);
  RUN_TEST(test_get_applied_failure_branches);

  c_orm_set_allocators(old_malloc, old_realloc, old_free);
}

#if defined(__clang__) || defined(__GNUC__)
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */
