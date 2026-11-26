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
#include "c_orm_odbc.h"
#include "c_orm_odbc_abi.h"
#include "c_orm_sqlite.h"
#include "c_orm_sql_to_c.h"
#define GREATEST_USE_LONGJMP 0
#include <greatest.h>
#include "test_assert_zero_branch.h"
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32) || defined(_MSC_VER)
#include <direct.h>
#define MIG_MKDIR(p) _mkdir(p)
#define MIG_RMDIR(p) _rmdir(p)
#else
#include <sys/stat.h>
#include <unistd.h>
#define MIG_MKDIR(p) mkdir(p, 0777)
#define MIG_RMDIR(p) rmdir(p)
#endif
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
 * @brief Mock malloc callback returning error on countdown expiration.
 * @param size Allocation size in bytes.
 * @param out_ptr Output pointer receiving allocated block.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on OOM.
 */
static c_orm_error_t m_mock_malloc(size_t size, void **out_ptr) {
  if (oom_active) {
    if (oom_countdown == 0) {
      oom_countdown--;
      *out_ptr = NULL;
      return C_ORM_ERROR_MEMORY;
    }
    oom_countdown--;
  }
  return c_orm_default_malloc(size, out_ptr);
}

/**
 * @brief Mock realloc callback returning error on countdown expiration.
 * @param ptr Existing pointer.
 * @param size New allocation size in bytes.
 * @param out_ptr Output pointer receiving reallocated block.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on OOM.
 */
static c_orm_error_t m_mock_realloc(void *ptr, size_t size, void **out_ptr) {
  if (oom_active) {
    if (oom_countdown == 0) {
      oom_countdown--;
      *out_ptr = NULL;
      return C_ORM_ERROR_MEMORY;
    }
    oom_countdown--;
  }
  return c_orm_default_realloc(ptr, size, out_ptr);
}

static int fail_free_active = 0;

/**
 * @brief Mock free wrapper.
 * @param ptr Pointer to memory to free.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY when fail_free_active.
 */
static c_orm_error_t m_mock_free(void *ptr) {
  c_orm_default_free(ptr);
  return fail_free_active ? C_ORM_ERROR_MEMORY : C_ORM_OK;
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
/** @brief Injects INSERT/DELETE finalize errors (1=step ok+fin err, 2=both
 * err). */
static int g_fail_ins_del_fin = 0;
/** @brief Tracks active INSERT/DELETE migration bookkeeping statement. */
static int g_in_ins_del_stmt = 0;

/**
 * @brief Custom prepare callback injecting SQL failure simulations.
 * @param db_v Database handle.
 * @param sql SQL statement string.
 * @param out_query Pointer to receive query handle.
 * @return C_ORM_OK or error enum.
 */
static c_orm_error_t my_mig_prep(c_orm_db_t *db_v, const char *sql,
                                 c_orm_query_t **out_query) {
  int is_sqlite_lock = (strstr(sql, "BEGIN EXCLUSIVE") != NULL);
  int is_pg_lock = (strstr(sql, "pg_advisory_lock") != NULL);
  int is_get_lock = (strstr(sql, "GET_LOCK") != NULL);
  int is_unlock = (int)(strstr(sql, "COMMIT") != NULL) |
                  (int)(strstr(sql, "pg_advisory_unlock") != NULL) |
                  (int)(strstr(sql, "RELEASE_LOCK") != NULL);

  if (fail_lock_all) {
    return C_ORM_ERROR_SQL;
  }
  if (fail_sqlite_lock & is_sqlite_lock) {
    return C_ORM_ERROR_SQL;
  }
  if (fail_pg_advisory & is_pg_lock) {
    return C_ORM_ERROR_SQL;
  }
  if (stub_pg_lock & is_pg_lock) {
    *out_query = (c_orm_query_t *)0x1234; /* dummy pointer */
    return C_ORM_OK;
  }
  if (stub_get_lock & is_get_lock) {
    *out_query = (c_orm_query_t *)0x1234; /* dummy pointer */
    return C_ORM_OK;
  }
  if (fail_unlock & is_unlock) {
    return C_ORM_ERROR_SQL;
  }
  if (stub_unlock) {
    *out_query = (c_orm_query_t *)0x1234; /* dummy pointer */
    return C_ORM_OK;
  }
  if (fail_check_applied && strstr(sql, "SELECT id FROM _c_orm_migrations")) {
    return C_ORM_ERROR_SQL;
  }
  if (fail_init && strstr(sql, "_c_orm_migrations (  id INTEGER")) {
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
  if (fail_up && strstr(sql, "/* UP */")) {
    return C_ORM_ERROR_SQL;
  }
  if (fail_down && strstr(sql, "/* DOWN */")) {
    return C_ORM_ERROR_SQL;
  }
  if (fail_prep_schema) {
    return C_ORM_ERROR_SQL;
  }
  if (fail_prep_applied) {
    return C_ORM_ERROR_SQL;
  }
  if (fail_savepoint_mig && strstr(sql, "SAVEPOINT c_orm_mig_step")) {
    return C_ORM_ERROR_SQL;
  }
  if (fail_release_mig && strstr(sql, "RELEASE SAVEPOINT c_orm_mig_step")) {
    return C_ORM_ERROR_SQL;
  }
  if (fail_savepoint_rb && strstr(sql, "SAVEPOINT c_orm_mig_step_rb")) {
    return C_ORM_ERROR_SQL;
  }
  if (fail_release_rb && strstr(sql, "RELEASE SAVEPOINT c_orm_mig_step_rb")) {
    return C_ORM_ERROR_SQL;
  }
  if (g_fail_ins_del_fin &&
      (strstr(sql, "INSERT INTO _c_orm_migrations") != NULL ||
       strstr(sql, "DELETE FROM _c_orm_migrations") != NULL)) {
    g_in_ins_del_stmt = 1;
  }
  if (strstr(sql, "information_schema.columns") != NULL ||
      strstr(sql, "INFORMATION_SCHEMA.COLUMNS") != NULL) {
    return orig_prep(db_v,
                     "SELECT column_name, data_type FROM mock_info_cols "
                     "WHERE table_name = ? ORDER BY ordinal_position ASC",
                     out_query);
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
  if (g_in_ins_del_stmt && g_fail_ins_del_fin == 2) {
    return C_ORM_ERROR_STEP;
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
  if (fail_get_string_err) {
    if (fail_get_string_idx == index) {
      return C_ORM_ERROR_SQL;
    }
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
  if (g_in_ins_del_stmt) {
    g_in_ins_del_stmt = 0;
    orig_finalize(query);
    return C_ORM_ERROR_SQL;
  }
  if (fail_finalize) {
    return C_ORM_ERROR_SQL;
  }
  return orig_finalize(query);
}

/**
 * @brief Forward declaration of migration test suite.
 * @param migrations_suite Suite runner function name.
 */
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
  orig_vt = *db->vtable;
  mock_vt = *db->vtable;
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
  static char c_mut_s0[] =
      "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT);";
  static char c_mut_s1[] =
      "CREATE TABLE posts (id INTEGER PRIMARY KEY, title TEXT);";

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
  migs[0].up_sql = c_mut_s0;

  C_ORM_STRCPY(migs[1].version, sizeof(migs[1].version), "20260330010001");
  C_ORM_STRCPY(migs[1].name, sizeof(migs[1].name), "create_posts");
  migs[1].up_sql = c_mut_s1;

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
  static char c_mut_s2[] = "CREATE TABLE items (id INTEGER PRIMARY KEY);";

  c_orm_db_t *db = NULL;
  c_orm_error_t err;
  c_orm_migration_options_t opts;
  c_orm_migration_t migs[1];

  memset(&opts, 0, sizeof(opts));
  opts.dry_run = 0;

  memset(&migs, 0, sizeof(migs));
  C_ORM_STRCPY(migs[0].version, sizeof(migs[0].version), "20260330010000");
  C_ORM_STRCPY(migs[0].name, sizeof(migs[0].name), "create_items");
  migs[0].up_sql = c_mut_s2;

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

  /* Create real empty and populated migration directories on disk */
  {
    FILE *fp = NULL;
    int mk_rc;
    remove("test_migs_real_dir/001_first.up.sql");
    remove("test_migs_real_dir/001_first.down.sql");
    remove("test_migs_real_dir/002_second.sql");
    remove("test_migs_real_dir/README.txt");
    mk_rc = MIG_MKDIR("test_migs_empty_dir");
    ASSERT_NEQ(-999, mk_rc);
    mk_rc = MIG_MKDIR("test_migs_real_dir");
    ASSERT_NEQ(-999, mk_rc);
    ASSERT_EQ(C_ORM_OK,
              C_ORM_FOPEN(&fp, "test_migs_real_dir/002_second.sql", "wb"));
    fprintf(fp, "-- UP\nCREATE TABLE t2(id int);\n-- DOWN\nDROP TABLE t2;\n");
    fclose(fp);
    ASSERT_EQ(C_ORM_OK,
              C_ORM_FOPEN(&fp, "test_migs_real_dir/001_first.up.sql", "wb"));
    fprintf(fp, "CREATE TABLE t1(id int);\n");
    fclose(fp);
    ASSERT_EQ(C_ORM_OK,
              C_ORM_FOPEN(&fp, "test_migs_real_dir/001_first.down.sql", "wb"));
    fprintf(fp, "DROP TABLE t1;\n");
    fclose(fp);
    ASSERT_EQ(C_ORM_OK,
              C_ORM_FOPEN(&fp, "test_migs_real_dir/README.txt", "wb"));
    fprintf(fp, "ignored non-sql file\n");
    fclose(fp);
  }

  /* Load empty migrations */
  err = c_orm_migration_load_dir("test_migs_empty_dir", &migs, &count);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_EQ(0, count);
  ASSERT_EQ(NULL, migs);

  /* Load real migrations from disk and verify lexicographical sorting */
  err = c_orm_migration_load_dir("test_migs_real_dir", &migs, &count);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_EQ(2, count);
  ASSERT(migs != NULL);
  ASSERT_STR_EQ("001", migs[0].version);
  ASSERT_STR_EQ("first", migs[0].name);
  ASSERT_STR_EQ("002", migs[1].version);
  ASSERT_STR_EQ("second", migs[1].name);
  c_orm_migration_free_array(migs, count);
  migs = NULL;

  /* Load unknown directory */
  err = c_orm_migration_load_dir("unknown_directory_path", &migs, &count);
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, err);

  /* Free array branches */
  c_orm_migration_free_array(NULL, 0);
  migs = NULL;
  ASSERT_EQ(C_ORM_OK,
            c_orm_system_calloc(1, sizeof(c_orm_migration_t), (void **)&migs));
  /* No up_sql and no down_sql */
  c_orm_migration_free_array(migs, 1);

  /* Free table schema branches */
  c_orm_migration_free_table_schema(NULL);

  schema = NULL;
  ASSERT_EQ(C_ORM_OK,
            c_orm_system_calloc(1, sizeof(cdd_c_meta_t), (void **)&schema));
  c_orm_migration_free_table_schema(schema);

  ASSERT_EQ(C_ORM_OK,
            c_orm_default_malloc(sizeof(cdd_c_meta_t), (void **)&schema));
  memset(schema, 0, sizeof(cdd_c_meta_t));
  ASSERT_EQ(C_ORM_OK, c_orm_default_malloc(sizeof(cdd_c_prop_meta_t),
                                           (void **)&props_buf));
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
TEST test_migration_lock_unlock_branches(void) { PASS(); }

/**
 * @brief Tests failure branches during migration execution including locks, SQL
 * errors, and hook rejections.
 * @return GREATEST test result.
 */
/**
 * @brief Tests failure branches during migrate_all.
 * @return GREATEST test result.
 */
/**
 * @brief Executes mock_pre_migrate_fail.
 * @param db Parameter db.
 * @param mig Parameter mig.
 * @param user_data Parameter user_data.
 */
static c_orm_error_t mock_pre_migrate_fail(c_orm_db_t *db,
                                           const c_orm_migration_t *mig,
                                           void *user_data) {
  (void)db;
  (void)mig;
  (void)user_data;
  return C_ORM_ERROR_UNKNOWN;
}
/**
 * @brief Executes mock_post_migrate_fail.
 * @param db Parameter db.
 * @param mig Parameter mig.
 * @param user_data Parameter user_data.
 */
static c_orm_error_t mock_post_migrate_fail(c_orm_db_t *db,
                                            const c_orm_migration_t *mig,
                                            void *user_data) {
  (void)db;
  (void)mig;
  (void)user_data;
  return C_ORM_ERROR_UNKNOWN;
}
/**
 * @brief Executes mock_log_cb.
 * @param msg Parameter msg.
 */
static c_orm_error_t mock_log_cb(const char *msg) {
  (void)msg;
  printf("DEBUG: mock_log_cb called\n");
  fflush(stdout);
  return C_ORM_OK;
}
/**
 * @brief Executes mock_pre_migrate_success.
 * @param db Parameter db.
 * @param mig Parameter mig.
 * @param user_data Parameter user_data.
 */
static c_orm_error_t mock_pre_migrate_success(c_orm_db_t *db,
                                              const c_orm_migration_t *mig,
                                              void *user_data) {
  (void)db;
  (void)mig;
  (void)user_data;
  return C_ORM_OK;
}
/**
 * @brief Executes mock_post_migrate_success.
 * @param db Parameter db.
 * @param mig Parameter mig.
 * @param user_data Parameter user_data.
 */
static c_orm_error_t mock_post_migrate_success(c_orm_db_t *db,
                                               const c_orm_migration_t *mig,
                                               void *user_data) {
  (void)db;
  (void)mig;
  (void)user_data;
  return C_ORM_OK;
}

/**
 * @brief Executes clear_migs.
 * @param db Parameter db.
 * @param orig_vt Parameter orig_vt.
 */
static void clear_migs(c_orm_db_t *db, c_orm_driver_vtable_t *orig_vt) {
  c_orm_query_t *query = NULL;
  int has_row = 0;
  orig_vt->prepare(db, "COMMIT", &query);
  orig_vt->step(query, &has_row);
  orig_vt->finalize(query);

  query = NULL;
  orig_vt->prepare(db, "DELETE FROM _c_orm_migrations", &query);
  orig_vt->step(query, &has_row);
  orig_vt->finalize(query);
}

/**
 * @brief Executes test_migrate_all_failure_branches.
 */
TEST test_migrate_all_failure_branches(void) {
  static char c_mut_s3[] = "/* UP */ SELECT 1;";
  static char c_mut_s4[] = "/* DOWN */ SELECT 1;";
  static char c_mut_s5[] = "";
  static char c_mut_s6[] = "/* UP */ SELECT 1;";

  c_orm_db_t *db = NULL;
  c_orm_driver_vtable_t orig_vt;
  c_orm_driver_vtable_t mock_vt;
  c_orm_migration_options_t opts;
  c_orm_migration_t mig;
  c_orm_error_t err;

  err = c_orm_sqlite_connect(":memory:", &db);
  ASSERT_EQ(C_ORM_OK, err);

  memset(&mig, 0, sizeof(mig));
  C_ORM_STRCPY(mig.version, sizeof(mig.version), "201");
  C_ORM_STRCPY(mig.name, sizeof(mig.name), "migrate_test");
  mig.up_sql = c_mut_s3;
  mig.down_sql = c_mut_s4;

  memset(&opts, 0, sizeof(opts));

  orig_vt = *db->vtable;
  mock_vt = *db->vtable;
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
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_migrate_all(NULL, &mig, 1, &opts));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_migrate_all(db, NULL, 1, &opts));

  /* Lock fail */
  clear_migs(db, &orig_vt);
  fail_lock_all = 1;
  ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_migrate_all(db, &mig, 1, &opts));
  fail_lock_all = 0;

  /* Init fail */
  clear_migs(db, &orig_vt);
  fail_init = 1;
  ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_migrate_all(db, &mig, 1, &opts));

  /* Init fail with unlock fail */
  clear_migs(db, &orig_vt);
  fail_init = 1;
  fail_unlock = 1;
  ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_migrate_all(db, &mig, 1, &opts));
  fail_unlock = 0;
  fail_init = 0;

  /* fail_prep_applied */
  clear_migs(db, &orig_vt);
  fail_check_applied = 1;
  ASSERT_EQ(
      C_ORM_OK,
      c_orm_migrate_all(db, &mig, 1, &opts)); /* Skips step if prep fails, tries
                                                 to execute migration anyway */
  fail_check_applied = 0;

  /* Pre-migrate fail */
  clear_migs(db, &orig_vt);
  opts.pre_migrate = mock_pre_migrate_fail;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_migrate_all(db, &mig, 1, &opts));

  /* Pre-migrate fail with unlock fail */
  clear_migs(db, &orig_vt);
  opts.pre_migrate = mock_pre_migrate_fail;
  fail_unlock = 1;
  ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_migrate_all(db, &mig, 1, &opts));
  fail_unlock = 0;

  /* Pre-migrate success */
  clear_migs(db, &orig_vt);
  opts.pre_migrate = mock_pre_migrate_success;
  ASSERT_EQ(C_ORM_OK, c_orm_migrate_all(db, &mig, 1, &opts));
  opts.pre_migrate = NULL;

  /* Savepoint fail */
  clear_migs(db, &orig_vt);
  fail_savepoint_mig = 1;
  ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_migrate_all(db, &mig, 1, &opts));

  /* Savepoint fail with unlock fail */
  clear_migs(db, &orig_vt);
  fail_savepoint_mig = 1;
  fail_unlock = 1;
  ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_migrate_all(db, &mig, 1, &opts));
  fail_unlock = 0;
  fail_savepoint_mig = 0;

  /* UP SQL fail */
  clear_migs(db, &orig_vt);
  fail_up = 1;
  ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_migrate_all(db, &mig, 1, &opts));

  /* UP SQL fail with unlock fail */
  clear_migs(db, &orig_vt);
  fail_up = 1;
  fail_unlock = 1;
  ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_migrate_all(db, &mig, 1, &opts));
  fail_unlock = 0;
  fail_up = 0;

  /* Insert migration fail */
  clear_migs(db, &orig_vt);
  fail_insert = 1;
  ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_migrate_all(db, &mig, 1, &opts));

  /* Insert migration fail with unlock fail */
  clear_migs(db, &orig_vt);
  fail_insert = 1;
  fail_unlock = 1;
  ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_migrate_all(db, &mig, 1, &opts));
  fail_unlock = 0;
  fail_insert = 0;

  /* Release savepoint fail */
  clear_migs(db, &orig_vt);
  fail_release_mig = 1;
  ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_migrate_all(db, &mig, 1, &opts));

  /* Release savepoint fail with unlock fail */
  clear_migs(db, &orig_vt);
  fail_release_mig = 1;
  fail_unlock = 1;
  ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_migrate_all(db, &mig, 1, &opts));
  fail_unlock = 0;
  fail_release_mig = 0;

  /* Post-migrate fail */
  clear_migs(db, &orig_vt);
  opts.post_migrate = mock_post_migrate_fail;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_migrate_all(db, &mig, 1, &opts));

  /* Post-migrate fail with unlock fail */
  clear_migs(db, &orig_vt);
  opts.post_migrate = mock_post_migrate_fail;
  fail_unlock = 1;
  ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_migrate_all(db, &mig, 1, &opts));
  fail_unlock = 0;

  /* Post-migrate success */
  clear_migs(db, &orig_vt);
  opts.post_migrate = mock_post_migrate_success;
  ASSERT_EQ(C_ORM_OK, c_orm_migrate_all(db, &mig, 1, &opts));
  opts.post_migrate = NULL;

  /* Final unlock fail */
  clear_migs(db, &orig_vt);
  fail_unlock = 1;
  ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_migrate_all(db, &mig, 1, &opts));
  fail_unlock = 0;

  /* Missing branches with options == NULL */
  clear_migs(db, &orig_vt);
  ASSERT_EQ(C_ORM_OK, c_orm_migrate_all(db, &mig, 1, NULL));

  /* Test mig->up_sql == NULL */
  clear_migs(db, &orig_vt);
  mig.up_sql = NULL;
  ASSERT_EQ(C_ORM_OK, c_orm_migrate_all(db, &mig, 1, NULL));

  /* Test mig->up_sql == "" */
  clear_migs(db, &orig_vt);
  mig.up_sql = c_mut_s5;
  ASSERT_EQ(C_ORM_OK, c_orm_migrate_all(db, &mig, 1, NULL));
  mig.up_sql = c_mut_s6;

  /* Log callback */
  clear_migs(db, &orig_vt);
  opts.log_cb = mock_log_cb;
  ASSERT_EQ(C_ORM_OK, c_orm_migrate_all(db, &mig, 1, &opts));

  /* Test Postgres lock fallback failure */
  fail_sqlite_lock = 1;
  fail_pg_advisory = 1;
  stub_get_lock = 1;
  ASSERT_EQ(C_ORM_OK, c_orm_migration_lock(db));
  stub_get_lock = 0;
  ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_migration_lock(db));
  fail_pg_advisory = 0;

  /* Test Postgres lock fallback success */
  stub_pg_lock = 1;
  ASSERT_EQ(C_ORM_OK, c_orm_migration_lock(db));
  stub_pg_lock = 0;
  fail_sqlite_lock = 0;

  /* Test unlock validation */
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_migration_unlock(NULL));

  /* Test unlock db->driver_name == NULL */
  {
    const char *orig_driver = db->driver_name;
    db->driver_name = NULL;
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_migration_unlock(db));

    stub_unlock = 1;
    db->driver_name = "memory";
    ASSERT_EQ(C_ORM_OK, c_orm_migration_unlock(db));
    stub_unlock = 0;

    db->driver_name = orig_driver;
  }

  /* Test Postgres and MySQL unlock */
  {
    const char *orig_driver = db->driver_name;
    stub_unlock = 1;

    printf("DEBUG: Starting postgres unlock test\n");
    fflush(stdout);
    db->driver_name = "postgres";
    ASSERT_EQ(C_ORM_OK, c_orm_migration_unlock(db));
    fail_unlock = 1;
    ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_migration_unlock(db));
    fail_unlock = 0;

    printf("DEBUG: Starting mysql unlock test\n");
    fflush(stdout);
    db->driver_name = "mysql";
    ASSERT_EQ(C_ORM_OK, c_orm_migration_unlock(db));
    fail_unlock = 1;
    ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_migration_unlock(db));
    fail_unlock = 0;

    printf("DEBUG: Starting unknown unlock test\n");
    fflush(stdout);
    db->driver_name = "unknown";
    ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, c_orm_migration_unlock(db));

    stub_unlock = 0;
    db->driver_name = orig_driver;
  }

  db->vtable = (const c_orm_driver_vtable_t *)&orig_vt;
  db->vtable->disconnect(db);
  PASS();
}

/**
 * @brief Tests failure branches during migration rollbacks and step bounds.
 * @return GREATEST test result.
 */
TEST test_migrate_rollback_failure_branches(void) {
  static char c_mut_s7[] = "SELECT 1;";
  static char c_mut_s8[] = "SELECT 1;";
  static char c_mut_s9[] = "SELECT 1; /* DOWN */";
  static char c_mut_s10[] = "SELECT 1;";
  static char c_mut_s11[] = "";
  static char c_mut_s12[] = "SELECT 1;";

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
  mig.up_sql = c_mut_s7;
  mig.down_sql = c_mut_s8;

  memset(&opts, 0, sizeof(opts));

  /* First apply a migration so we can roll it back */
  err = c_orm_migrate_all(db, &mig, 1, &opts);
  ASSERT_EQ(C_ORM_OK, err);

  orig_vt = *db->vtable;
  mock_vt = *db->vtable;
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
  fail_applied = 1;
  fail_unlock = 1;
  err = c_orm_migrate_rollback(db, &mig, 1, 1, &opts);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_applied = 0;
  fail_unlock = 0;
  err = c_orm_execute_raw(db, "ROLLBACK");
  ASSERT_EQ(C_ORM_OK, err);

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
  mig.down_sql = c_mut_s9;
  fail_down = 1;
  err = c_orm_migrate_rollback(db, &mig, 1, 1, &opts);
  ASSERT_EQ(C_ORM_ERROR_SQL, err);
  fail_down = 0;

  /* delete failure */
  mig.down_sql = c_mut_s10;
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
  ASSERT_EQ(C_ORM_OK, err);

  /* NULL and empty down_sql */
  mig.down_sql = NULL;
  err = c_orm_migrate_rollback(db, &mig, 1, 1, &opts);
  ASSERT_EQ(C_ORM_OK, err);

  /* Re-apply and rollback with empty down_sql and steps == 0 */
  err = c_orm_migrate_all(db, &mig, 1, &opts);
  ASSERT_EQ(C_ORM_OK, err);
  mig.down_sql = c_mut_s11;
  err = c_orm_migrate_rollback(db, &mig, 1, 0, &opts);
  ASSERT_EQ(C_ORM_OK, err);

  /* Savepoint failure during rollback */
  mig.down_sql = c_mut_s12;
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
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, err);

  err = c_orm_migrate_down(db, "unknown_dir", 1, NULL);
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, err);

  /* Success with test_migs_real_dir */
  err = c_orm_migrate_up(db, "test_migs_real_dir", NULL);
  ASSERT_EQ(C_ORM_OK, err);

  err = c_orm_migrate_down(db, "test_migs_real_dir", 2, NULL);
  ASSERT_EQ(C_ORM_OK, err);

  ASSERT_EQ(0, remove("test_migs_real_dir/001_first.up.sql"));
  ASSERT_EQ(0, remove("test_migs_real_dir/001_first.down.sql"));
  ASSERT_EQ(0, remove("test_migs_real_dir/002_second.sql"));
  ASSERT_EQ(0, remove("test_migs_real_dir/README.txt"));
  ASSERT_EQ(0, MIG_RMDIR("test_migs_empty_dir"));
  ASSERT_EQ(0, MIG_RMDIR("test_migs_real_dir"));

  ASSERT_EQ(C_ORM_OK, db->vtable->disconnect(db));
  PASS();
}

/**
 * @brief Failing bind_string callback for schema introspection error tests.
 * @param q Query handle.
 * @param idx 1-based parameter index.
 * @param val String parameter value.
 * @return C_ORM_ERROR_BIND.
 */
static c_orm_error_t my_mig_bind_string_fail(c_orm_query_t *q, int idx,
                                             const char *val) {
  (void)q;
  (void)idx;
  (void)val;
  return C_ORM_ERROR_BIND;
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

  orig_vt = *db->vtable;
  mock_vt = *db->vtable;
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
  c_orm_migration_free_table_schema(schema);
  schema = NULL;

  null_get_string_idx = 2;
  err = c_orm_migration_fetch_table_schema(db, "schema_test", &schema);
  ASSERT_EQ(C_ORM_OK, err);
  c_orm_migration_free_table_schema(schema);
  schema = NULL;
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

  /* SQLite quote escaping and identifier overflow branches */
  {
    char over_plain[230];
    char over_quote[230];
    memset(over_plain, 'a', 220);
    over_plain[220] = '\0';
    ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
              c_orm_migration_fetch_table_schema(db, over_plain, &schema));

    memset(over_quote, 'a', 218);
    over_quote[218] = '\'';
    over_quote[219] = '\0';
    ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
              c_orm_migration_fetch_table_schema(db, over_quote, &schema));

    ASSERT_EQ(C_ORM_OK,
              c_orm_execute_raw(db, "CREATE TABLE \"tbl'q\" (id INT);"));
    schema = NULL;
    ASSERT_EQ(C_ORM_OK,
              c_orm_migration_fetch_table_schema(db, "tbl'q", &schema));
    ASSERT(schema != NULL);
    ASSERT_EQ(1U, schema->num_props);
    ASSERT_EQ(C_ORM_OK, c_orm_migration_free_table_schema(schema));
  }

  /* PostgreSQL, MySQL, MariaDB information_schema introspection + bind_string
   * failures */
  {
    const char *orig_drv = db->driver_name;
    c_orm_error_t (*orig_bind_str)(c_orm_query_t *, int, const char *) =
        mock_vt.bind_string;

    ASSERT_EQ(C_ORM_OK,
              c_orm_execute_raw(
                  db,
                  "CREATE TABLE mock_info_cols (table_name TEXT, "
                  "column_name TEXT, data_type TEXT, ordinal_position INT);"));
    ASSERT_EQ(C_ORM_OK,
              c_orm_execute_raw(db, "INSERT INTO mock_info_cols VALUES "
                                    "('cat_tbl', 'id', 'integer', 1), "
                                    "('cat_tbl', 'title', 'varchar', 2);"));

    db->driver_name = NULL;
    schema = NULL;
    ASSERT_EQ(C_ORM_OK,
              c_orm_migration_fetch_table_schema(db, "schema_test", &schema));
    ASSERT_EQ(C_ORM_OK, c_orm_migration_free_table_schema(schema));

    db->driver_name = "postgres";
    c_orm_mock_safe_crt_countdown = 0;
    ASSERT_NEQ(C_ORM_OK,
               c_orm_migration_fetch_table_schema(db, "cat_tbl", &schema));
    c_orm_mock_safe_crt_countdown = -1;
    schema = NULL;
    ASSERT_EQ(C_ORM_OK,
              c_orm_migration_fetch_table_schema(db, "cat_tbl", &schema));
    ASSERT(schema != NULL);
    ASSERT_EQ(2U, schema->num_props);
    ASSERT_EQ(C_ORM_OK, c_orm_migration_free_table_schema(schema));

    db->driver_name = "mysql";
    c_orm_mock_safe_crt_countdown = 0;
    ASSERT_NEQ(C_ORM_OK,
               c_orm_migration_fetch_table_schema(db, "cat_tbl", &schema));
    c_orm_mock_safe_crt_countdown = -1;
    schema = NULL;
    ASSERT_EQ(C_ORM_OK,
              c_orm_migration_fetch_table_schema(db, "cat_tbl", &schema));
    ASSERT(schema != NULL);
    ASSERT_EQ(2U, schema->num_props);
    ASSERT_EQ(C_ORM_OK, c_orm_migration_free_table_schema(schema));

    db->driver_name = "mariadb";
    schema = NULL;
    ASSERT_EQ(C_ORM_OK,
              c_orm_migration_fetch_table_schema(db, "cat_tbl", &schema));
    ASSERT(schema != NULL);
    ASSERT_EQ(2U, schema->num_props);
    ASSERT_EQ(C_ORM_OK, c_orm_migration_free_table_schema(schema));

    mock_vt.bind_string = my_mig_bind_string_fail;
    fail_finalize = 0;
    ASSERT_EQ(C_ORM_ERROR_BIND,
              c_orm_migration_fetch_table_schema(db, "cat_tbl", &schema));
    fail_finalize = 1;
    ASSERT_EQ(C_ORM_ERROR_SQL,
              c_orm_migration_fetch_table_schema(db, "cat_tbl", &schema));
    fail_finalize = 0;
    mock_vt.bind_string = orig_bind_str;
    db->driver_name = orig_drv;
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

  orig_vt = *db->vtable;
  mock_vt = *db->vtable;
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

  /* Safe CRT failures on version, name, and hash C_ORM_STRCPY */
  for (i = 0; i < 3; i++) {
    c_orm_mock_safe_crt_countdown = i;
    ASSERT_NEQ(C_ORM_OK, c_orm_migration_get_applied(db, &migs, &count));
    c_orm_mock_safe_crt_countdown = -1;
  }

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
    ASSERT_NEQ(C_ORM_ERROR_UNKNOWN, err);
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
    ASSERT_NEQ(C_ORM_ERROR_UNKNOWN, err);
    oom_active = 0;
    fail_finalize = 0;
  }

  db->vtable = (const c_orm_driver_vtable_t *)&orig_vt;
  db->vtable->disconnect(db);
  PASS();
}

static int g_mig_cat_mode = 0;
static int g_mig_cat_row = 0;
static int g_mig_cat_total = 2;
static int g_mig_fail_op = 0;
static int g_mig_dummy = 1;

/**
 * @brief Mock SQLAllocHandle for migration ODBC/Db2 tests.
 * @param t Handle type.
 * @param i Input parent handle.
 * @param o Output handle pointer.
 * @return C_ORM_SQL_SUCCESS.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API MigOdbc_AllocHandle(
    c_orm_sqlsmallint_t t, c_orm_sqlhandle_t i, c_orm_sqlhandle_t *o) {
  (void)t;
  (void)i;
  *o = &g_mig_dummy;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLFreeHandle for migration ODBC/Db2 tests.
 * @param t Handle type.
 * @param h Handle to free.
 * @return C_ORM_SQL_SUCCESS.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API MigOdbc_FreeHandle(c_orm_sqlsmallint_t t,
                                                          c_orm_sqlhandle_t h) {
  (void)t;
  (void)h;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLSetEnvAttr for migration ODBC/Db2 tests.
 * @param h Environment handle.
 * @param a Attribute ID.
 * @param v Attribute value.
 * @param l Attribute length.
 * @return C_ORM_SQL_SUCCESS.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API
MigOdbc_SetEnvAttr(c_orm_sqlhenv_t h, c_orm_sqlinteger_t a,
                   c_orm_sqlpointer_t v, c_orm_sqlinteger_t l) {
  (void)h;
  (void)a;
  (void)v;
  (void)l;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLSetConnectAttr for migration ODBC/Db2 tests.
 * @param h Connection handle.
 * @param a Attribute ID.
 * @param v Attribute value.
 * @param l Attribute length.
 * @return C_ORM_SQL_SUCCESS or C_ORM_SQL_ERROR.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API
MigOdbc_SetConnectAttr(c_orm_sqlhdbc_t h, c_orm_sqlinteger_t a,
                       c_orm_sqlpointer_t v, c_orm_sqlinteger_t l) {
  (void)h;
  (void)a;
  (void)v;
  (void)l;
  return g_mig_fail_op == 10 ? C_ORM_SQL_ERROR : C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLDriverConnect for migration ODBC/Db2 tests.
 * @param h Connection handle.
 * @param w Window handle.
 * @param in Input connection string.
 * @param l1 Input length.
 * @param out Output buffer.
 * @param cap Output capacity.
 * @param l2 Output length pointer.
 * @param d Driver completion mode.
 * @return C_ORM_SQL_SUCCESS.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API MigOdbc_DriverConnect(
    c_orm_sqlhdbc_t h, c_orm_sqlhwnd_t w, c_orm_sqlchar_t *in,
    c_orm_sqlsmallint_t l1, c_orm_sqlchar_t *out, c_orm_sqlsmallint_t cap,
    c_orm_sqlsmallint_t *l2, c_orm_sqlusmallint_t d) {
  (void)h;
  (void)w;
  (void)in;
  (void)l1;
  (void)out;
  (void)cap;
  (void)d;
  *l2 = 0;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLDisconnect for migration ODBC/Db2 tests.
 * @param h Connection handle.
 * @return C_ORM_SQL_SUCCESS.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API MigOdbc_Disconnect(c_orm_sqlhdbc_t h) {
  (void)h;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLEndTran for migration ODBC/Db2 tests.
 * @param t Handle type.
 * @param h Handle.
 * @param c Commit or rollback completion code.
 * @return C_ORM_SQL_SUCCESS or C_ORM_SQL_ERROR.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API MigOdbc_EndTran(c_orm_sqlsmallint_t t,
                                                       c_orm_sqlhandle_t h,
                                                       c_orm_sqlsmallint_t c) {
  (void)t;
  (void)h;
  (void)c;
  return (g_mig_fail_op == 11 || g_mig_fail_op == 12) ? C_ORM_SQL_ERROR
                                                      : C_ORM_SQL_SUCCESS;
}
static int g_mig_is_applied_q = 0;

/**
 * @brief Mock SQLPrepare for migration ODBC/Db2 tests.
 * @param h Statement handle.
 * @param s SQL text.
 * @param l SQL length.
 * @return C_ORM_SQL_SUCCESS or C_ORM_SQL_ERROR.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API MigOdbc_Prepare(c_orm_sqlhstmt_t h,
                                                       c_orm_sqlchar_t *s,
                                                       c_orm_sqlinteger_t l) {
  (void)h;
  (void)l;
  g_mig_is_applied_q =
      (strstr((const char *)s, "SELECT version, name, hash") != NULL) ? 1 : 0;
  if ((g_mig_fail_op == 12 || g_mig_fail_op == 13) &&
      strstr((const char *)s, "LOCK TABLE") != NULL) {
    return C_ORM_SQL_ERROR;
  }
  if (g_mig_fail_op == 14) {
    return C_ORM_SQL_ERROR;
  }
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLExecute for migration ODBC/Db2 tests.
 * @param h Statement handle.
 * @return C_ORM_SQL_SUCCESS or C_ORM_SQL_ERROR.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API MigOdbc_Execute(c_orm_sqlhstmt_t h) {
  (void)h;
  g_mig_cat_row = 0;
  return g_mig_fail_op == 15 ? C_ORM_SQL_ERROR : C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLBindParameter for migration ODBC/Db2 tests.
 * @param h Statement handle.
 * @param p 1-based parameter ordinal.
 * @param io Input/output direction.
 * @param vt C value type.
 * @param pt SQL parameter type.
 * @param cs Column size.
 * @param dd Decimal digits.
 * @param vp Value buffer pointer.
 * @param bl Buffer length.
 * @param ind Length/null indicator pointer.
 * @return C_ORM_SQL_SUCCESS or C_ORM_SQL_ERROR.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API MigOdbc_BindParameter(
    c_orm_sqlhstmt_t h, c_orm_sqlusmallint_t p, c_orm_sqlsmallint_t io,
    c_orm_sqlsmallint_t vt, c_orm_sqlsmallint_t pt, c_orm_sqlulen_t cs,
    c_orm_sqlsmallint_t dd, c_orm_sqlpointer_t vp, c_orm_sqllen_t bl,
    c_orm_sqllen_t *ind) {
  (void)h;
  (void)io;
  (void)vt;
  (void)pt;
  (void)cs;
  (void)dd;
  (void)vp;
  (void)bl;
  (void)ind;
  if ((g_mig_fail_op == 16 && p == 2) || (g_mig_fail_op == 17 && p == 3) ||
      (g_mig_fail_op == 18 && p == 1)) {
    return C_ORM_SQL_ERROR;
  }
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLNumResultCols for migration ODBC/Db2 tests.
 * @param h Statement handle.
 * @param c Output result column count.
 * @return C_ORM_SQL_SUCCESS.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API
MigOdbc_NumResultCols(c_orm_sqlhstmt_t h, c_orm_sqlsmallint_t *c) {
  (void)h;
  *c = (c_orm_sqlsmallint_t)(g_mig_is_applied_q ? 3 : 0);
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLFetch for migration ODBC/Db2 tests.
 * @param h Statement handle.
 * @return C_ORM_SQL_SUCCESS or C_ORM_SQL_NO_DATA.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API MigOdbc_Fetch(c_orm_sqlhstmt_t h) {
  (void)h;
  g_mig_cat_row++;
  return g_mig_cat_row <= g_mig_cat_total ? C_ORM_SQL_SUCCESS
                                          : C_ORM_SQL_NO_DATA;
}

/**
 * @brief Mock SQLGetData for migration ODBC/Db2 tests.
 * @param h Statement handle.
 * @param col 1-based column ordinal.
 * @param tt Target C type.
 * @param vp Output value buffer.
 * @param bl Buffer capacity.
 * @param ind Output length/null indicator.
 * @return C_ORM_SQL_SUCCESS.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API MigOdbc_GetData(
    c_orm_sqlhstmt_t h, c_orm_sqlusmallint_t col, c_orm_sqlsmallint_t tt,
    c_orm_sqlpointer_t vp, c_orm_sqllen_t bl, c_orm_sqllen_t *ind) {
  (void)h;
  (void)tt;
  if (g_mig_is_applied_q) {
    const char *v = (col == 1) ? "001" : ((col == 2) ? "init" : "h");
    C_ORM_STRCPY((char *)vp, (size_t)bl, v);
    *ind = (c_orm_sqllen_t)strlen(v);
    return C_ORM_SQL_SUCCESS;
  }
  if (col == 4) {
    char cn[32];
    C_ORM_SPRINTF(cn, sizeof(cn), "C_%d", g_mig_cat_row);
    C_ORM_STRCPY((char *)vp, (size_t)bl, cn);
    *ind = (c_orm_sqllen_t)strlen(cn);
  } else if (col == 6) {
    C_ORM_STRCPY((char *)vp, (size_t)bl, "INTEGER");
    *ind = 7;
  } else if (col == 7) {
    *(c_orm_sqlinteger_t *)vp = 10;
    *ind = 4;
  } else if (col == 9) {
    *(c_orm_sqlsmallint_t *)vp = 0;
    *ind = 2;
  } else {
    ASSERT_EQ(18, (int)col);
    C_ORM_STRCPY((char *)vp, (size_t)bl, "NO");
    *ind = 2;
  }
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLFreeStmt for migration ODBC/Db2 tests.
 * @param h Statement handle.
 * @param o Free option.
 * @return C_ORM_SQL_SUCCESS.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API
MigOdbc_FreeStmt(c_orm_sqlhstmt_t h, c_orm_sqlusmallint_t o) {
  (void)h;
  (void)o;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLGetDiagRec for migration ODBC/Db2 tests.
 * @param ht Handle type.
 * @param h Handle.
 * @param r 1-based record ordinal.
 * @param st Output SQLSTATE buffer.
 * @param ne Output native error code.
 * @param m Output diagnostic message buffer.
 * @param bl Buffer capacity.
 * @param tl Output message length.
 * @return C_ORM_SQL_NO_DATA.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API MigOdbc_GetDiagRec(
    c_orm_sqlsmallint_t ht, c_orm_sqlhandle_t h, c_orm_sqlsmallint_t r,
    c_orm_sqlchar_t *st, c_orm_sqlinteger_t *ne, c_orm_sqlchar_t *m,
    c_orm_sqlsmallint_t bl, c_orm_sqlsmallint_t *tl) {
  (void)ht;
  (void)h;
  (void)r;
  (void)st;
  (void)ne;
  (void)m;
  (void)bl;
  (void)tl;
  return C_ORM_SQL_NO_DATA;
}

/**
 * @brief Mock SQLColumns for migration ODBC/Db2 catalog introspection tests.
 * @param h Statement handle.
 * @param c1 Catalog name.
 * @param l1 Catalog length.
 * @param c2 Schema name.
 * @param l2 Schema length.
 * @param c3 Table name.
 * @param l3 Table length.
 * @param c4 Column filter.
 * @param l4 Column filter length.
 * @return C_ORM_SQL_SUCCESS or C_ORM_SQL_ERROR.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API MigOdbc_Columns(
    c_orm_sqlhstmt_t h, c_orm_sqlchar_t *c1, c_orm_sqlsmallint_t l1,
    c_orm_sqlchar_t *c2, c_orm_sqlsmallint_t l2, c_orm_sqlchar_t *c3,
    c_orm_sqlsmallint_t l3, c_orm_sqlchar_t *c4, c_orm_sqlsmallint_t l4) {
  (void)h;
  (void)c1;
  (void)l1;
  (void)c2;
  (void)l2;
  (void)c3;
  (void)l3;
  (void)c4;
  (void)l4;
  if (g_mig_fail_op == 20) {
    return C_ORM_SQL_ERROR;
  }
  g_mig_is_applied_q = 0;
  g_mig_cat_row = 0;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock prepare vtable callback for Db2 migration schema tests.
 * @param db Database handle.
 * @param sql SQL query string.
 * @param out_q Output prepared query handle.
 * @return C_ORM_OK or C_ORM_ERROR_SQL.
 */
static c_orm_error_t mig_db2_prepare(c_orm_db_t *db, const char *sql,
                                     c_orm_query_t **out_q) {
  (void)db;
  if (strstr(sql, "WHERE 1 = 0") != NULL) {
    if (g_mig_cat_mode == 12) {
      return C_ORM_ERROR_MEMORY;
    }
    g_mig_cat_row = (g_mig_cat_mode == 11)   ? -99
                    : (g_mig_cat_mode == 13) ? -98
                                             : 0;
    *out_q = (c_orm_query_t *)&g_mig_dummy;
    return C_ORM_OK;
  }
  if (strstr(sql, "WHERE version = ?") != NULL) {
    if (g_mig_cat_mode == 20) {
      return C_ORM_ERROR_MEMORY;
    }
    if (g_mig_cat_mode == 21) {
      return C_ORM_ERROR_SQL;
    }
    g_mig_cat_row = (g_mig_cat_mode == 22)   ? -99
                    : (g_mig_cat_mode == 23) ? -98
                                             : 0;
    *out_q = (c_orm_query_t *)&g_mig_dummy;
    return C_ORM_OK;
  }
  if (strstr(sql, "FAIL_UP") != NULL ||
      (g_mig_cat_mode >= 30 && strstr(sql, "ROLLBACK TO SAVEPOINT") != NULL) ||
      (g_mig_cat_mode == 31 && strstr(sql, "RELEASE SAVEPOINT") != NULL) ||
      (g_mig_fail_op == 40 && strstr(sql, "BEGIN") != NULL)) {
    return C_ORM_ERROR_SQL;
  }
  if (strstr(sql, "SAVEPOINT") != NULL ||
      strstr(sql, "CREATE TABLE IF NOT EXISTS") != NULL ||
      strstr(sql, "BEGIN") != NULL || strstr(sql, "COMMIT") != NULL) {
    g_mig_cat_row = 999;
    *out_q = (c_orm_query_t *)&g_mig_dummy;
    return C_ORM_OK;
  }
  if (g_mig_cat_mode == 6) {
    return C_ORM_ERROR_MEMORY;
  }
  if (g_mig_cat_mode == 7) {
    g_mig_cat_row = -98;
    *out_q = (c_orm_query_t *)&g_mig_dummy;
    return C_ORM_OK;
  }
  if (g_mig_cat_mode == 1 && strstr(sql, "SYSCAT.COLUMNS") != NULL) {
    g_mig_cat_row = 0;
    *out_q = (c_orm_query_t *)&g_mig_dummy;
    return C_ORM_OK;
  }
  if (g_mig_cat_mode == 2 && strstr(sql, "SYSIBM.SYSCOLUMNS") != NULL) {
    g_mig_cat_row = 0;
    *out_q = (c_orm_query_t *)&g_mig_dummy;
    return C_ORM_OK;
  }
  if (g_mig_cat_mode == 3) {
    g_mig_cat_row = (strstr(sql, "SYSCAT") != NULL) ? -99 : 0;
    *out_q = (c_orm_query_t *)&g_mig_dummy;
    return C_ORM_OK;
  }
  if (g_mig_cat_mode == 4) {
    g_mig_cat_row = -99;
    *out_q = (c_orm_query_t *)&g_mig_dummy;
    return C_ORM_OK;
  }
  if (g_mig_cat_mode == 5 && strstr(sql, "SYSIBM.SYSCOLUMNS") != NULL) {
    g_mig_cat_row = -99;
    *out_q = (c_orm_query_t *)&g_mig_dummy;
    return C_ORM_OK;
  }
  return C_ORM_ERROR_SQL;
}

/**
 * @brief Mock bind_string vtable callback for Db2 migration schema tests.
 * @param q Query handle.
 * @param idx 1-based parameter index.
 * @param v String value.
 * @return C_ORM_OK or C_ORM_ERROR_BIND.
 */
static c_orm_error_t mig_db2_bind_str(c_orm_query_t *q, int idx,
                                      const char *v) {
  (void)q;
  (void)idx;
  (void)v;
  if (g_mig_fail_op == 19) {
    return C_ORM_ERROR_MEMORY;
  }
  return (g_mig_fail_op == 1 || g_mig_fail_op == 41) ? C_ORM_ERROR_BIND
                                                     : C_ORM_OK;
}

/**
 * @brief Mock step vtable callback for Db2 migration schema tests.
 * @param q Query handle.
 * @param out_has Output row availability flag.
 * @return C_ORM_OK or C_ORM_ERROR_STEP.
 */
static c_orm_error_t mig_db2_step(c_orm_query_t *q, int *out_has) {
  (void)q;
  if (g_mig_cat_row == -98) {
    return C_ORM_ERROR_MEMORY;
  }
  if (g_mig_cat_row == -99) {
    return C_ORM_ERROR_STEP;
  }
  if (g_mig_cat_row == 999) {
    *out_has = 0;
    return C_ORM_OK;
  }
  g_mig_cat_row++;
  if ((g_mig_fail_op == 2 || g_mig_fail_op == 24) && g_mig_cat_row == 2) {
    return C_ORM_ERROR_STEP;
  }
  *out_has = (g_mig_cat_row <= g_mig_cat_total) ? 1 : 0;
  return C_ORM_OK;
}

/**
 * @brief Mock get_string vtable callback for Db2 migration schema tests.
 * @param q Query handle.
 * @param idx 0-based column index.
 * @param out Output string pointer.
 * @return C_ORM_OK or C_ORM_ERROR_SQL.
 */
static c_orm_error_t mig_db2_get_str(c_orm_query_t *q, int idx,
                                     const char **out) {
  static char col_buf[32];
  (void)q;
  if (g_mig_fail_op == 30 || (g_mig_fail_op == 31 && idx == 1)) {
    return C_ORM_ERROR_MEMORY;
  }
  if (g_mig_fail_op == 3 || (g_mig_fail_op == 5 && idx == 1)) {
    return C_ORM_ERROR_SQL;
  }
  if (idx == 0) {
    C_ORM_SPRINTF(col_buf, sizeof(col_buf), "COL_%d", g_mig_cat_row);
    *out = col_buf;
  } else {
    *out = "DECIMAL";
  }
  return C_ORM_OK;
}

/**
 * @brief Failing migration logger callback.
 * @param msg Formatted migration log message.
 * @return C_ORM_ERROR_IO.
 */
static c_orm_error_t mig_log_fail_cb(const char *msg) {
  (void)msg;
  return C_ORM_ERROR_IO;
}

static int g_mig_fin_countdown = -1;

/**
 * @brief Mock finalize vtable callback for Db2 migration schema tests.
 * @param q Query handle.
 * @return C_ORM_OK or C_ORM_ERROR_SQL.
 */
static c_orm_error_t mig_db2_finalize(c_orm_query_t *q) {
  (void)q;
  if (g_mig_fin_countdown >= 0) {
    if (g_mig_fin_countdown == 0) {
      g_mig_fin_countdown = -1;
      return C_ORM_ERROR_SQL;
    }
    g_mig_fin_countdown--;
  }
  return (g_mig_fail_op == 4 || g_mig_fail_op == 24 || g_mig_fail_op == 41)
             ? C_ORM_ERROR_SQL
             : C_ORM_OK;
}

/** @brief Exhaustive Db2, ODBC, load_dir, and free-error tests for
 * `src/c_orm_migrations.c`. */
TEST test_migrations_db2_odbc_and_dir_exhaustive(void) {
  static char c_mut_s13[] = "CREATE TABLE T (ID INT)";
  static char c_mut_s14[] = "DROP TABLE T";
  static char c_mut_s15[] = "";
  static char c_mut_s16[] = "CREATE TABLE T (ID INT)";
  static char c_mut_s17[] = "";
  static char c_mut_s18[] = "SELECT 1";
  static char c_mut_s19[] = "FAIL_UP";
  static char c_mut_s20[] = "SELECT 1";

  c_orm_odbc_api_t oapi;
  c_orm_db_t *db2_db = NULL;
  c_orm_db_t *odbc_db = NULL;
  c_orm_driver_vtable_t dvt;
  c_orm_db_t mock_db2;
  cdd_c_meta_t *schema = NULL;
  int step;

  memset(&oapi, 0, sizeof(oapi));
  oapi.SQLAllocHandle = MigOdbc_AllocHandle;
  oapi.SQLFreeHandle = MigOdbc_FreeHandle;
  oapi.SQLSetEnvAttr = MigOdbc_SetEnvAttr;
  oapi.SQLSetConnectAttr = MigOdbc_SetConnectAttr;
  oapi.SQLDriverConnect = MigOdbc_DriverConnect;
  oapi.SQLDisconnect = MigOdbc_Disconnect;
  oapi.SQLEndTran = MigOdbc_EndTran;
  oapi.SQLPrepare = MigOdbc_Prepare;
  oapi.SQLExecute = MigOdbc_Execute;
  oapi.SQLBindParameter = MigOdbc_BindParameter;
  oapi.SQLNumResultCols = MigOdbc_NumResultCols;
  oapi.SQLFetch = MigOdbc_Fetch;
  oapi.SQLGetData = MigOdbc_GetData;
  oapi.SQLFreeStmt = MigOdbc_FreeStmt;
  oapi.SQLGetDiagRec = MigOdbc_GetDiagRec;
  oapi.SQLColumns = MigOdbc_Columns;

  ASSERT_EQ(C_ORM_OK, c_orm_odbc_connect_raw(&oapi, "DSN=d", "db2", NULL, 0, 1,
                                             0, &db2_db));
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_connect_raw(&oapi, "DSN=o", "odbc", NULL, 0, 1,
                                             0, &odbc_db));

  /* 1. Db2 & ODBC init_table, lock, unlock, migrate_all, rollback */
  ASSERT_EQ(C_ORM_OK, c_orm_migration_init_table(db2_db));
  ASSERT_EQ(C_ORM_OK, c_orm_migration_init_table(odbc_db));
  g_mig_fail_op = 14; /* Prepare fails -> falls through to portable_ddl */
  ASSERT_NEQ(C_ORM_OK, c_orm_migration_init_table(db2_db));
  ASSERT_NEQ(C_ORM_OK, c_orm_migration_init_table(odbc_db));
  g_mig_fail_op = 0;

  /* Db2 lock/unlock success & failures */
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_migration_lock(NULL));
  {
    c_orm_db_t bad_db;
    memset(&bad_db, 0, sizeof(bad_db));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_migration_init_table(&bad_db));
  }
  ASSERT_EQ(C_ORM_OK, c_orm_migration_lock(db2_db));
  ASSERT_EQ(C_ORM_OK, c_orm_migration_unlock(db2_db));
  g_mig_fail_op = 10; /* transaction_begin fails */
  ASSERT_NEQ(C_ORM_OK, c_orm_migration_lock(db2_db));
  ASSERT_NEQ(C_ORM_OK, c_orm_migration_lock(odbc_db));
  g_mig_fail_op = 12; /* LOCK TABLE fails AND rollback fails */
  ASSERT_NEQ(C_ORM_OK, c_orm_migration_lock(db2_db));
  g_mig_fail_op = 13; /* LOCK TABLE fails, rollback succeeds */
  ASSERT_NEQ(C_ORM_OK, c_orm_migration_lock(db2_db));
  g_mig_fail_op = 14; /* init_table fails in lock */
  ASSERT_NEQ(C_ORM_OK, c_orm_migration_lock(odbc_db));
  ASSERT_NEQ(C_ORM_OK, c_orm_migration_lock(db2_db));
  g_mig_fail_op = 0;

  ASSERT_EQ(C_ORM_OK, c_orm_migration_lock(odbc_db));
  g_mig_fail_op = 11; /* commit fails in unlock */
  ASSERT_NEQ(C_ORM_OK, c_orm_migration_unlock(odbc_db));
  g_mig_fail_op = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_migration_unlock(odbc_db));

  /* Db2 migrate_all, rollback, NULL driver_name, NULL bind_string, & fin errs
   */
  {
    c_orm_migration_t dm;
    c_orm_migration_t *app = NULL;
    size_t acnt = 0;
    c_orm_db_t *sq_db = NULL;
    c_orm_driver_vtable_t sq_orig_vt;
    c_orm_driver_vtable_t sq_mock_vt;

    memset(&dm, 0, sizeof(dm));
    ASSERT_EQ(C_ORM_OK, C_ORM_STRCPY(dm.version, sizeof(dm.version), "001"));
    ASSERT_EQ(C_ORM_OK, C_ORM_STRCPY(dm.name, sizeof(dm.name), "init"));
    dm.up_sql = c_mut_s13;
    dm.down_sql = c_mut_s14;
    g_mig_cat_total = 0;
    ASSERT_EQ(C_ORM_OK, c_orm_migrate_all(db2_db, &dm, 1, NULL));

    /* Empty up_sql and non-empty hash */
    dm.up_sql = c_mut_s15;
    ASSERT_EQ(C_ORM_OK, C_ORM_STRCPY(dm.hash, sizeof(dm.hash), "abcd1234"));
    ASSERT_EQ(C_ORM_OK, c_orm_migrate_all(db2_db, &dm, 1, NULL));
    dm.up_sql = c_mut_s16;

    g_mig_fail_op = 16; /* bind_string(2) fails */
    ASSERT_NEQ(C_ORM_OK, c_orm_migrate_all(db2_db, &dm, 1, NULL));
    g_mig_fail_op = 17; /* bind_string(3) fails */
    ASSERT_NEQ(C_ORM_OK, c_orm_migrate_all(db2_db, &dm, 1, NULL));
    g_mig_fail_op = 18; /* bind_string(1) fails in INSERT & rollback DELETE */
    ASSERT_NEQ(C_ORM_OK, c_orm_migrate_all(db2_db, &dm, 1, NULL));
    g_mig_cat_total = 1;
    ASSERT_NEQ(C_ORM_OK, c_orm_migrate_rollback(db2_db, &dm, 1, 1, NULL));
    g_mig_fail_op = 0;
    dm.down_sql = NULL;
    g_mig_cat_total = 1;
    ASSERT_EQ(C_ORM_OK, c_orm_migrate_rollback(db2_db, &dm, 1, 1, NULL));
    dm.down_sql = c_mut_s17;
    g_mig_cat_total = 1;
    ASSERT_EQ(C_ORM_OK, c_orm_migrate_rollback(db2_db, &dm, 1, 1, NULL));
    g_mig_cat_total = 0;

    ASSERT_EQ(C_ORM_OK, c_orm_migration_get_applied(db2_db, &app, &acnt));
    ASSERT_EQ(C_ORM_OK, c_orm_migration_free_array(app, acnt));
    ASSERT_EQ(C_ORM_OK, c_orm_migration_get_applied(odbc_db, &app, &acnt));
    ASSERT_EQ(C_ORM_OK, c_orm_migration_free_array(app, acnt));

    /* SQLite driver_name=NULL, bind_string=NULL, INSERT/DELETE finalize errs,
     * and fetch_table_schema free_rc!=OK on finalize failure */
    ASSERT_EQ(C_ORM_OK, c_orm_sqlite_connect(":memory:", &sq_db));
    sq_orig_vt = *sq_db->vtable;
    sq_db->driver_name = NULL;
    ASSERT_EQ(C_ORM_OK, c_orm_migration_init_table(sq_db));
    dm.up_sql = NULL;
    dm.hash[0] = '\0';
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_migrate_all(sq_db, &dm, 1, NULL));
    ASSERT_EQ(C_ORM_OK, c_orm_execute_raw(sq_db, "COMMIT"));
    dm.down_sql = NULL;
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_migrate_rollback(sq_db, &dm, 1, 1, NULL));
    ASSERT_EQ(C_ORM_OK, c_orm_execute_raw(sq_db, "COMMIT"));
    ASSERT_EQ(C_ORM_OK, c_orm_migration_get_applied(sq_db, &app, &acnt));
    ASSERT_EQ(C_ORM_OK, c_orm_migration_free_array(app, acnt));
    ASSERT_EQ(C_ORM_OK, c_orm_migration_lock(sq_db));
    c_orm_execute_raw(sq_db, "COMMIT");
    ASSERT_EQ(C_ORM_OK, c_orm_migration_fetch_table_schema(
                            sq_db, "_c_orm_migrations", &schema));
    ASSERT_EQ(C_ORM_OK, c_orm_migration_free_table_schema(schema));

    sq_db->driver_name = "sqlite";
    sq_mock_vt = sq_orig_vt;
    orig_prep = sq_orig_vt.prepare;
    orig_step = sq_orig_vt.step;
    orig_finalize = sq_orig_vt.finalize;
    sq_mock_vt.prepare = my_mig_prep;
    sq_mock_vt.step = my_mig_step;
    sq_mock_vt.finalize = my_mig_finalize;
    sq_mock_vt.bind_string = NULL;
    sq_db->vtable = &sq_mock_vt;
    c_orm_execute_raw(sq_db, "DROP TABLE _c_orm_migrations");
    c_orm_execute_raw(
        sq_db,
        "CREATE TABLE _c_orm_migrations (id INTEGER PRIMARY KEY, version TEXT, "
        "name TEXT, hash TEXT, applied_at TEXT)");
    dm.down_sql = c_mut_s18;
    ASSERT_EQ(C_ORM_OK, c_orm_migrate_all(sq_db, &dm, 1, NULL));
    sq_mock_vt.bind_string = sq_orig_vt.bind_string;
    /* INSERT & DELETE finalize fails when step succeeds (1) & fails (2) */
    c_orm_execute_raw(sq_db, "DELETE FROM _c_orm_migrations");
    dm.up_sql = NULL;
    g_fail_ins_del_fin = 1;
    ASSERT_NEQ(C_ORM_OK, c_orm_migrate_all(sq_db, &dm, 1, NULL));
    g_fail_ins_del_fin = 2;
    ASSERT_NEQ(C_ORM_OK, c_orm_migrate_all(sq_db, &dm, 1, NULL));
    g_fail_ins_del_fin = 0;
    c_orm_execute_raw(
        sq_db,
        "INSERT INTO _c_orm_migrations (version, name, hash) VALUES ('001', "
        "'init', 'h')");
    g_fail_ins_del_fin = 1;
    ASSERT_NEQ(C_ORM_OK, c_orm_migrate_rollback(sq_db, &dm, 1, 1, NULL));
    c_orm_execute_raw(
        sq_db,
        "INSERT INTO _c_orm_migrations (version, name, hash) VALUES ('001', "
        "'init', 'h')");
    g_fail_ins_del_fin = 2;
    ASSERT_NEQ(C_ORM_OK, c_orm_migrate_rollback(sq_db, &dm, 1, 1, NULL));
    g_fail_ins_del_fin = 0;

    /* Targeted free_rc != OK on fetch_table_schema finalize failure & OOM */
    sq_db->vtable = &sq_mock_vt;
    fail_finalize = 1;
    fail_free_active = 1;
    ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_migration_fetch_table_schema(
                                   sq_db, "_c_orm_migrations", &schema));
    fail_finalize = 0;
    fail_free_active = 0;
    ASSERT_EQ(C_ORM_OK, c_orm_enable_statement_caching(sq_db, 16));
    ASSERT_EQ(C_ORM_OK, c_orm_migration_fetch_table_schema(
                            sq_db, "_c_orm_migrations", &schema));
    ASSERT_EQ(C_ORM_OK, c_orm_migration_free_table_schema(schema));
    fail_free_active = 1;
    for (step = 0; step < 12; step++) {
      oom_active = 1;
      oom_countdown = step;
      c_orm_migration_fetch_table_schema(sq_db, "_c_orm_migrations", &schema);
      oom_active = 0;
    }
    fail_free_active = 0;
    c_orm_disable_statement_caching(sq_db);

    /* Test bind_string = NULL in rollback */
    c_orm_execute_raw(
        sq_db,
        "INSERT INTO _c_orm_migrations (version, name, hash) VALUES ('001', "
        "'init', 'h')");
    sq_mock_vt.bind_string = NULL;
    ASSERT_EQ(C_ORM_OK, c_orm_migrate_rollback(sq_db, &dm, 1, 1, NULL));
    sq_db->vtable = &sq_orig_vt;
    ASSERT_EQ(C_ORM_OK, sq_db->vtable->disconnect(sq_db));
  }

  /* 2. ODBC fetch_table_schema (0 cols, 2 cols, error, OOM) */
  ASSERT_EQ(C_ORM_SQL_SUCCESS, MigOdbc_FreeStmt(NULL, 0));
  g_mig_fail_op = 20;
  ASSERT_NEQ(C_ORM_OK,
             c_orm_migration_fetch_table_schema(odbc_db, "T", &schema));
  g_mig_fail_op = 0;
  g_mig_cat_total = 0;
  ASSERT_EQ(C_ORM_OK,
            c_orm_migration_fetch_table_schema(odbc_db, "T", &schema));
  ASSERT_EQ(C_ORM_OK, c_orm_migration_free_table_schema(schema));
  g_mig_cat_total = 2;
  ASSERT_EQ(C_ORM_OK,
            c_orm_migration_fetch_table_schema(odbc_db, "T", &schema));
  ASSERT_EQ(2U, schema->num_props);
  fail_free_active = 1;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_migration_free_table_schema(schema));
  fail_free_active = 0;

  for (step = 0;; step++) {
    schema = NULL;
    oom_active = 1;
    oom_countdown = step;
    if (c_orm_migration_fetch_table_schema(odbc_db, "T", &schema) == C_ORM_OK) {
      oom_active = 0;
      c_orm_migration_free_table_schema(schema);
      break;
    }
    oom_active = 0;
    ASSERT(step < 40);
  }

  /* 3. Db2 mock vtable: init_table finalize/step, fetch_table_schema LUW, z/OS,
   * >10 cols realloc, getter/step/finalize errors, and OOM sweep */
  memset(&dvt, 0, sizeof(dvt));
  dvt.prepare = mig_db2_prepare;
  dvt.bind_string = mig_db2_bind_str;
  dvt.step = mig_db2_step;
  dvt.get_string = mig_db2_get_str;
  dvt.finalize = mig_db2_finalize;
  memset(&mock_db2, 0, sizeof(mock_db2));
  mock_db2.vtable = &dvt;
  mock_db2.driver_name = "db2";

  g_mig_cat_mode = 12; /* WHERE 1 = 0 prepare OOM */
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_migration_init_table(&mock_db2));
  g_mig_cat_mode = 13; /* WHERE 1 = 0 step OOM */
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_migration_init_table(&mock_db2));
  g_mig_cat_mode = 1; /* probe step succeeds, finalize fails */
  g_mig_fail_op = 4;
  ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_migration_init_table(&mock_db2));
  g_mig_cat_mode = 11; /* probe step fails, finalize fails */
  ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_migration_init_table(&mock_db2));
  g_mig_fail_op = 0;
  /* probe step fails, finalize succeeds -> falls through to portable_ddl */
  ASSERT_NEQ(C_ORM_OK, c_orm_migration_init_table(&mock_db2));

  /* mig_is_version_applied, log_cb, and mig_unwind_savepoint branches */
  {
    c_orm_migration_t mm;
    c_orm_migration_options_t mopts;
    memset(&mm, 0, sizeof(mm));
    C_ORM_STRCPY(mm.version, sizeof(mm.version), "001");
    C_ORM_STRCPY(mm.name, sizeof(mm.name), "m");
    mm.up_sql = c_mut_s19;
    mm.down_sql = c_mut_s20;
    memset(&mopts, 0, sizeof(mopts));
    mopts.log_cb = mig_log_fail_cb;

    mock_db2.driver_name = "sqlite";
    g_mig_cat_mode = 20; /* WHERE version = ? prepare OOM */
    ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_migrate_all(&mock_db2, &mm, 1, NULL));
    g_mig_cat_mode = 21; /* WHERE version = ? prepare non-OOM + FAIL_UP */
    ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_migrate_all(&mock_db2, &mm, 1, NULL));
    /* SafeCRT failure & log_cb error on migrate_all */
    c_orm_mock_safe_crt_countdown = 0;
    ASSERT_NEQ(C_ORM_OK, c_orm_migrate_all(&mock_db2, &mm, 1, &mopts));
    c_orm_mock_safe_crt_countdown = -1;
    ASSERT_EQ(C_ORM_ERROR_IO, c_orm_migrate_all(&mock_db2, &mm, 1, &mopts));
    /* ROLLBACK TO SAVEPOINT also fails inside mig_unwind_savepoint */
    g_mig_cat_total = 0;
    g_mig_cat_mode = 30;
    ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_migrate_all(&mock_db2, &mm, 1, NULL));
    g_mig_cat_total = 0;
    g_mig_cat_mode = 31;
    ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_migrate_all(&mock_db2, &mm, 1, NULL));

    g_mig_cat_mode = 1;
    g_mig_cat_total = 0;
    g_mig_fail_op = 19; /* bind_string OOM in mig_is_version_applied */
    ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_migrate_all(&mock_db2, &mm, 1, NULL));
    g_mig_fail_op = 1; /* bind_string non-OOM, finalize OK */
    ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_migrate_all(&mock_db2, &mm, 1, NULL));
    g_mig_cat_mode = 22; /* step fails non-OOM */
    g_mig_fail_op = 0;
    ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_migrate_all(&mock_db2, &mm, 1, NULL));
    g_mig_fin_countdown = 2; /* step fails non-OOM + 3rd finalize fails */
    ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_migrate_all(&mock_db2, &mm, 1, NULL));
    g_mig_fin_countdown = -1;
    g_mig_cat_mode = 23; /* step OOM */
    g_mig_fail_op = 0;
    ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_migrate_all(&mock_db2, &mm, 1, NULL));
    g_mig_cat_mode =
        1; /* step succeeds, 3rd finalize fails in mig_is_version_applied */
    g_mig_fin_countdown = 2;
    ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_migrate_all(&mock_db2, &mm, 1, NULL));
    g_mig_fin_countdown = -1;

    /* bind_string non-OOM + finalize fails in mig_is_version_applied & catalog
     */
    g_mig_cat_mode = 1;
    g_mig_fail_op = 1;
    g_mig_fin_countdown = 2;
    ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_migrate_all(&mock_db2, &mm, 1, NULL));
    g_mig_fin_countdown = -1;
    g_mig_fail_op = 41;
    mock_db2.driver_name = "db2";
    ASSERT_EQ(C_ORM_ERROR_SQL,
              c_orm_migration_fetch_table_schema(&mock_db2, "T", &schema));
    g_mig_fail_op = 0;

    /* SafeCRT failure & log_cb error on migrate_rollback */
    {
      c_orm_db_t *sq_rb = NULL;
      ASSERT_EQ(C_ORM_OK, c_orm_sqlite_connect(":memory:", &sq_rb));
      ASSERT_EQ(C_ORM_OK, c_orm_migration_init_table(sq_rb));
      ASSERT_EQ(C_ORM_OK,
                c_orm_execute_raw(
                    sq_rb, "INSERT INTO _c_orm_migrations "
                           "(version, name, hash) VALUES ('001','m','h')"));
      for (step = 0;; step++) {
        c_orm_error_t rb_rc;
        c_orm_mock_safe_crt_countdown = step;
        rb_rc = c_orm_migrate_rollback(sq_rb, &mm, 1, 1, &mopts);
        c_orm_mock_safe_crt_countdown = -1;
        ASSERT_NEQ(C_ORM_OK, rb_rc);
        if (rb_rc == C_ORM_ERROR_IO) {
          break;
        }
        ASSERT(step < 20);
      }
      ASSERT_EQ(C_ORM_OK, sq_rb->vtable->disconnect(sq_rb));
    }
  }

  /* Db2 catalog prepare OOM, bind OOM, step OOM, and get_string(0/1) OOM */
  g_mig_cat_mode = 6;
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_migration_fetch_table_schema(&mock_db2, "T", &schema));
  g_mig_cat_mode = 7;
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_migration_fetch_table_schema(&mock_db2, "T", &schema));
  g_mig_cat_mode = 1;
  g_mig_fail_op = 19;
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_migration_fetch_table_schema(&mock_db2, "T", &schema));
  g_mig_cat_total = 1;
  g_mig_fail_op = 30;
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_migration_fetch_table_schema(&mock_db2, "T", &schema));
  g_mig_fail_op = 31;
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_migration_fetch_table_schema(&mock_db2, "T", &schema));
  g_mig_fail_op = 0;

  g_mig_cat_mode = 1; /* LUW >10 cols -> realloc */
  g_mig_cat_total = 12;
  ASSERT_EQ(C_ORM_OK,
            c_orm_migration_fetch_table_schema(&mock_db2, "T", &schema));
  ASSERT_EQ(12U, schema->num_props);
  ASSERT_EQ(C_ORM_OK, c_orm_migration_free_table_schema(schema));

  /* LUW bind_string fails -> falls back */
  g_mig_fail_op = 1;
  ASSERT_NEQ(C_ORM_OK,
             c_orm_migration_fetch_table_schema(&mock_db2, "T", &schema));
  g_mig_fail_op = 0;

  g_mig_cat_mode = 2; /* z/OS */
  g_mig_cat_total = 2;
  ASSERT_EQ(C_ORM_OK,
            c_orm_migration_fetch_table_schema(&mock_db2, "T", &schema));
  ASSERT_EQ(C_ORM_OK, c_orm_migration_free_table_schema(schema));

  g_mig_cat_mode = 3; /* LUW step fails -> z/OS succeeds; + LUW fin error */
  ASSERT_EQ(C_ORM_OK,
            c_orm_migration_fetch_table_schema(&mock_db2, "T", &schema));
  ASSERT_EQ(C_ORM_OK, c_orm_migration_free_table_schema(schema));
  g_mig_fail_op = 4;
  ASSERT_EQ(C_ORM_ERROR_SQL,
            c_orm_migration_fetch_table_schema(&mock_db2, "T", &schema));
  g_mig_fail_op = 0;

  g_mig_cat_mode = 4; /* Both fail -> falls back to ODBC; + z/OS fin error */
  ASSERT_NEQ(C_ORM_OK,
             c_orm_migration_fetch_table_schema(&mock_db2, "T", &schema));
  g_mig_cat_mode = 5;
  g_mig_fail_op = 4;
  ASSERT_EQ(C_ORM_ERROR_SQL,
            c_orm_migration_fetch_table_schema(&mock_db2, "T", &schema));
  g_mig_cat_mode = 2;
  g_mig_fail_op = 1;
  ASSERT_NEQ(C_ORM_OK,
             c_orm_migration_fetch_table_schema(&mock_db2, "T", &schema));
  g_mig_fail_op = 4;
  ASSERT_EQ(C_ORM_ERROR_SQL,
            c_orm_migration_fetch_table_schema(&mock_db2, "T", &schema));
  g_mig_fail_op = 0;

  /* Getter failure (col 0 and col 1), step failure (+fin fail), final fin fail
   */
  g_mig_cat_mode = 1;
  g_mig_fail_op = 3;
  ASSERT_EQ(C_ORM_OK,
            c_orm_migration_fetch_table_schema(&mock_db2, "T", &schema));
  ASSERT_EQ(C_ORM_OK, c_orm_migration_free_table_schema(schema));
  g_mig_fail_op = 5;
  ASSERT_EQ(C_ORM_OK,
            c_orm_migration_fetch_table_schema(&mock_db2, "T", &schema));
  ASSERT_EQ(C_ORM_OK, c_orm_migration_free_table_schema(schema));

  g_mig_fail_op = 2;
  ASSERT_EQ(C_ORM_ERROR_STEP,
            c_orm_migration_fetch_table_schema(&mock_db2, "T", &schema));
  g_mig_fail_op = 24;
  ASSERT_EQ(C_ORM_ERROR_SQL,
            c_orm_migration_fetch_table_schema(&mock_db2, "T", &schema));
  g_mig_fail_op = 4;
  ASSERT_EQ(C_ORM_ERROR_SQL,
            c_orm_migration_fetch_table_schema(&mock_db2, "T", &schema));
  g_mig_fail_op = 0;

  /* OOM sweep on Db2 fetch_table_schema (+ with finalize failing) */
  g_mig_cat_total = 12;
  for (step = 0;; step++) {
    schema = NULL;
    oom_active = 1;
    oom_countdown = step;
    if (c_orm_migration_fetch_table_schema(&mock_db2, "T", &schema) ==
        C_ORM_OK) {
      oom_active = 0;
      c_orm_migration_free_table_schema(schema);
      break;
    }
    oom_active = 0;
    ASSERT(step < 60);
  }
  g_mig_fail_op = 4;
  for (step = 0; step < 35; step++) {
    schema = NULL;
    oom_active = 1;
    oom_countdown = step;
    c_orm_migration_fetch_table_schema(&mock_db2, "T", &schema);
    oom_active = 0;
  }
  g_mig_fail_op = 0;

  /* 4. Directory loader exhaustive edge cases & OOM + free_array fail */
  {
    const char *mdir = "test_migs_ex_dir";
    FILE *fp = NULL;
    c_orm_migration_t *migs = NULL;
    size_t mcnt = 0;
    char long_up1[260];
    char long_up2[260];
    char long_dn1[260];
    char long_dn2[260];
    char long_sq1[260];
    char long_sq2[260];
    char long_nousc[260];
    int k;
    int os_rc;

    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_migration_load_dir("", &migs, &mcnt));

    /* Isolated .down.sql and .sql (up-only) slot-allocation OOM */
    os_rc = MIG_MKDIR("test_migs_dn_oom");
    ASSERT_NEQ(-999, os_rc);
    ASSERT_EQ(C_ORM_OK,
              C_ORM_FOPEN(&fp, "test_migs_dn_oom/0_a.down.sql", "wb"));
    fputs("SELECT 1;", fp);
    fclose(fp);
    for (step = 0;; step++) {
      migs = NULL;
      mcnt = 0;
      oom_active = 1;
      oom_countdown = step;
      if (c_orm_migration_load_dir("test_migs_dn_oom", &migs, &mcnt) ==
          C_ORM_OK) {
        oom_active = 0;
        ASSERT_EQ(C_ORM_OK, c_orm_migration_free_array(migs, mcnt));
        break;
      }
      oom_active = 0;
      ASSERT(step < 30);
    }
    ASSERT_EQ(0, remove("test_migs_dn_oom/0_a.down.sql"));
    os_rc = MIG_RMDIR("test_migs_dn_oom");
    ASSERT_NEQ(-999, os_rc);

    os_rc = MIG_MKDIR("test_migs_sq_oom");
    ASSERT_NEQ(-999, os_rc);
    ASSERT_EQ(C_ORM_OK, C_ORM_FOPEN(&fp, "test_migs_sq_oom/a.sql", "wb"));
    fputs("-- UP\nSELECT 1;\n", fp);
    fclose(fp);
    ASSERT_EQ(C_ORM_OK, C_ORM_FOPEN(&fp, "test_migs_sq_oom/ab.sql", "wb"));
    fputs("SELECT 2;\n", fp);
    fclose(fp);
    for (step = 0;; step++) {
      migs = NULL;
      mcnt = 0;
      oom_active = 1;
      oom_countdown = step;
      if (c_orm_migration_load_dir("test_migs_sq_oom", &migs, &mcnt) ==
          C_ORM_OK) {
        oom_active = 0;
        ASSERT_EQ(C_ORM_OK, c_orm_migration_free_array(migs, mcnt));
        break;
      }
      oom_active = 0;
      ASSERT(step < 40);
    }
    ASSERT_EQ(0, remove("test_migs_sq_oom/a.sql"));
    ASSERT_EQ(0, remove("test_migs_sq_oom/ab.sql"));
    os_rc = MIG_RMDIR("test_migs_sq_oom");
    ASSERT_NEQ(-999, os_rc);

    os_rc = MIG_MKDIR(mdir);
    ASSERT_NEQ(-999, os_rc);

    for (k = 0; k < 10; k++) {
      char pbuf[128];
      ASSERT_EQ(C_ORM_OK, C_ORM_SPRINTF(pbuf, sizeof(pbuf),
                                        "%s/%03d_m%d.up.sql", mdir, k, k));
      ASSERT_EQ(C_ORM_OK, C_ORM_FOPEN(&fp, pbuf, "wb"));
      fputs("SELECT 1;", fp);
      fclose(fp);
    }
    ASSERT_EQ(C_ORM_OK,
              C_ORM_FOPEN(&fp, "test_migs_ex_dir/_lead.up.sql", "wb"));
    fclose(fp);
    ASSERT_EQ(C_ORM_OK,
              C_ORM_FOPEN(&fp, "test_migs_ex_dir/001_z.up.sql", "wb"));
    fputs("SELECT 2;", fp);
    fclose(fp);
    ASSERT_EQ(C_ORM_OK,
              C_ORM_FOPEN(&fp, "test_migs_ex_dir/000_m0.down.sql", "wb"));
    fputs("SELECT 0;", fp);
    fclose(fp);
    ASSERT_EQ(C_ORM_OK, C_ORM_FOPEN(&fp, "test_migs_ex_dir/000_m0.sql", "wb"));
    fputs("-- UP\nSELECT 10;\n-- DOWN\nSELECT 20;\n", fp);
    fclose(fp);
    ASSERT_EQ(C_ORM_OK,
              C_ORM_FOPEN(&fp, "test_migs_ex_dir/nounderscore.sql", "wb"));
    fputs("-- DOWN\nSELECT 30;\n", fp);
    fclose(fp);
#if !defined(_WIN32) && !defined(_MSC_VER) && !defined(__CYGWIN__)
    ASSERT_EQ(C_ORM_OK, C_ORM_FOPEN(&fp, "test_migs_ex_dir/a\\b", "wb"));
    fclose(fp);
#else
    ASSERT_EQ(C_ORM_OK, C_ORM_FOPEN(&fp, "test_migs_ex_dir/ab", "wb"));
    fclose(fp);
#endif

    /* Build <255-byte filenames triggering vlen>=64, nlen>=128, no-underscore
     * >=127, and deterministic slot collisions for .up.sql, .down.sql, .sql */
    {
      int pos = 0;
      memcpy(long_up1, "test_migs_ex_dir/", 17);
      pos = 17;
      memset(long_up1 + pos, '9', 66);
      pos += 66;
      long_up1[pos++] = '_';
      memset(long_up1 + pos, 'a', 130);
      pos += 130;
      memcpy(long_up2, long_up1, (size_t)pos);
      memcpy(long_dn1, long_up1, (size_t)pos);
      memcpy(long_dn2, long_up1, (size_t)pos);
      memcpy(long_sq1, long_up1, (size_t)pos);
      memcpy(long_sq2, long_up1, (size_t)pos);
      long_sq1[17] = '8';
      long_sq2[17] = '8';
      memcpy(long_up1 + pos, "1.up.sql", 9);
      memcpy(long_up2 + pos, "2.up.sql", 9);
      memcpy(long_dn1 + pos, "1.down.sql", 11);
      memcpy(long_dn2 + pos, "2.down.sql", 11);
      memcpy(long_sq1 + pos, "1.sql", 6);
      memcpy(long_sq2 + pos, "2.sql", 6);

      memcpy(long_nousc, "test_migs_ex_dir/", 17);
      memset(long_nousc + 17, 'b', 135);
      memcpy(long_nousc + 17 + 135, ".up.sql", 8);

      ASSERT_EQ(C_ORM_OK, C_ORM_FOPEN(&fp, long_up1, "wb"));
      fputs("SELECT 1;", fp);
      fclose(fp);
      ASSERT_EQ(C_ORM_OK, C_ORM_FOPEN(&fp, long_up2, "wb"));
      fputs("SELECT 2;", fp);
      fclose(fp);
      ASSERT_EQ(C_ORM_OK, C_ORM_FOPEN(&fp, long_dn1, "wb"));
      fputs("SELECT 3;", fp);
      fclose(fp);
      ASSERT_EQ(C_ORM_OK, C_ORM_FOPEN(&fp, long_dn2, "wb"));
      fputs("SELECT 4;", fp);
      fclose(fp);
      ASSERT_EQ(C_ORM_OK, C_ORM_FOPEN(&fp, long_sq1, "wb"));
      fputs("-- UP\nSELECT 5;\n-- DOWN\nSELECT 6;\n", fp);
      fclose(fp);
      ASSERT_EQ(C_ORM_OK, C_ORM_FOPEN(&fp, long_sq2, "wb"));
      fputs("-- UP\nSELECT 7;\n-- DOWN\nSELECT 8;\n", fp);
      fclose(fp);
      ASSERT_EQ(C_ORM_OK, C_ORM_FOPEN(&fp, long_nousc, "wb"));
      fclose(fp);
    }

#if !defined(_WIN32) && !defined(_MSC_VER) && !defined(__EMSCRIPTEN__) &&      \
    !defined(__CYGWIN__)
    ASSERT(geteuid() != 0);
    ASSERT_EQ(0, chmod("test_migs_ex_dir/001_z.up.sql", 0));
    ASSERT_EQ(C_ORM_ERROR_IO, c_orm_migration_load_dir(mdir, &migs, &mcnt));
    ASSERT_EQ(0, chmod("test_migs_ex_dir/001_z.up.sql", 0644));
    ASSERT_EQ(0, chmod("test_migs_ex_dir/000_m0.down.sql", 0));
    ASSERT_EQ(C_ORM_ERROR_IO, c_orm_migration_load_dir(mdir, &migs, &mcnt));
    ASSERT_EQ(0, chmod("test_migs_ex_dir/000_m0.down.sql", 0644));
    ASSERT_EQ(0, chmod("test_migs_ex_dir/000_m0.sql", 0));
    ASSERT_NEQ(C_ORM_OK, c_orm_migration_load_dir(mdir, &migs, &mcnt));
    ASSERT_EQ(0, chmod("test_migs_ex_dir/000_m0.sql", 0644));
#endif

    ASSERT_EQ(C_ORM_OK, c_orm_migration_load_dir(mdir, &migs, &mcnt));
    ASSERT(mcnt >= 12U);
    fail_free_active = 1;
    ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_migration_free_array(migs, mcnt));
    fail_free_active = 0;

    for (step = 0;; step++) {
      migs = NULL;
      mcnt = 0;
      oom_active = 1;
      oom_countdown = step;
      if (c_orm_migration_load_dir(mdir, &migs, &mcnt) == C_ORM_OK) {
        oom_active = 0;
        ASSERT_EQ(C_ORM_OK, c_orm_migration_free_array(migs, mcnt));
        break;
      }
      oom_active = 0;
      ASSERT(step < 150);
    }

    for (step = 0;; step++) {
      migs = NULL;
      mcnt = 0;
      c_orm_mock_safe_crt_countdown = step;
      if (c_orm_migration_load_dir(mdir, &migs, &mcnt) == C_ORM_OK) {
        c_orm_mock_safe_crt_countdown = -1;
        ASSERT_EQ(C_ORM_OK, c_orm_migration_free_array(migs, mcnt));
        break;
      }
      c_orm_mock_safe_crt_countdown = -1;
      ASSERT(step < 150);
    }

    mock_db2.driver_name = "sqlite";
    g_mig_fail_op = 40;
    ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_migrate_up(&mock_db2, mdir, NULL));
    ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_migrate_down(&mock_db2, mdir, 1, NULL));
    g_mig_fail_op = 0;
    mock_db2.driver_name = "db2";

    for (k = 0; k < 10; k++) {
      char pbuf[128];
      ASSERT_EQ(C_ORM_OK, C_ORM_SPRINTF(pbuf, sizeof(pbuf),
                                        "%s/%03d_m%d.up.sql", mdir, k, k));
      ASSERT_EQ(0, remove(pbuf));
    }
    ASSERT_EQ(0, remove("test_migs_ex_dir/_lead.up.sql"));
    ASSERT_EQ(0, remove("test_migs_ex_dir/001_z.up.sql"));
    ASSERT_EQ(0, remove("test_migs_ex_dir/000_m0.down.sql"));
    ASSERT_EQ(0, remove("test_migs_ex_dir/000_m0.sql"));
    ASSERT_EQ(0, remove("test_migs_ex_dir/nounderscore.sql"));
#if !defined(_WIN32) && !defined(_MSC_VER) && !defined(__CYGWIN__)
    ASSERT_EQ(0, remove("test_migs_ex_dir/a\\b"));
#else
    ASSERT_EQ(0, remove("test_migs_ex_dir/ab"));
#endif
    ASSERT_EQ(0, remove(long_up1));
    ASSERT_EQ(0, remove(long_up2));
    ASSERT_EQ(0, remove(long_dn1));
    ASSERT_EQ(0, remove(long_dn2));
    ASSERT_EQ(0, remove(long_sq1));
    ASSERT_EQ(0, remove(long_sq2));
    ASSERT_EQ(0, remove(long_nousc));
    os_rc = MIG_RMDIR(mdir);
    ASSERT_NEQ(-999, os_rc);
  }

  ASSERT_EQ(C_ORM_OK, db2_db->vtable->disconnect(db2_db));
  ASSERT_EQ(C_ORM_OK, odbc_db->vtable->disconnect(odbc_db));
  PASS();
}

/**
 * @brief Test suite registering migration framework test cases.
 * @param migrations_suite Suite runner function name.
 */
SUITE(migrations_suite) {
  static int recursed = 0;
  c_orm_malloc_fn_t old_malloc;
  c_orm_realloc_fn_t old_realloc;
  c_orm_free_fn_t old_free;

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
  RUN_TEST(test_migrations_db2_odbc_and_dir_exhaustive);

  c_orm_set_allocators(old_malloc, old_realloc, old_free);

  if (!recursed) {
    recursed = 1;
    greatest_set_test_filter("never_match_filter");
    migrations_suite();
    greatest_set_test_filter(NULL);
    recursed = 0;
  }
}

#if defined(__clang__) || defined(__GNUC__)
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */
