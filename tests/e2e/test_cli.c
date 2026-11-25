/**
 * @file test_cli.c
 * @brief Unit tests for c-orm CLI subcommands and option parsing.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_api.h"
#include "c_orm_db.h"
#include "c_orm_db2.h"
#include "c_orm_mysql.h"
#include "c_orm_odbc.h"
#include "c_orm_odbc_abi.h"
#include "c_orm_postgres.h"
#include "c_orm_sqlite.h"
#define GREATEST_USE_LONGJMP 0
#include <greatest.h>
#include "test_assert_zero_branch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#if !defined(_WIN32) && !defined(_WIN64)
#include <unistd.h>
#endif
#include "c_orm_migrations.h"

/**
 * @brief Mock migration directory loader for CLI tests.
 * @param dir_path Directory path.
 * @param out_migrations Pointer to receive array of migrations.
 * @param out_count Pointer to receive count.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t mock_load_dir(const char *dir_path,
                                   c_orm_migration_t **out_migrations,
                                   size_t *out_count) {
  if (strcmp(dir_path, "test_cli_gen_migs") == 0) {
    return c_orm_migration_load_dir(dir_path, out_migrations, out_count);
  }
  if (strcmp(dir_path, "oom_load_dir") == 0) {
    *out_count = 0;
    *out_migrations = NULL;
    return C_ORM_ERROR_MEMORY;
  }
  if (strcmp(dir_path, "bad_dir") == 0) {
    *out_count = 0;
    *out_migrations = NULL;
    return C_ORM_OK;
  }
  if (strcmp(dir_path, "missing_dir") == 0) {
    *out_count = 0;
    *out_migrations = NULL;
    return C_ORM_ERROR_NOT_FOUND;
  }
  if (strcmp(dir_path, "empty_dir") == 0) {
    *out_count = 0;
    *out_migrations = NULL;
    return C_ORM_OK;
  }
  *out_count = 1;
  *out_migrations = NULL;
  c_orm_system_calloc(1, sizeof(c_orm_migration_t),
                            (void **)out_migrations);
  C_ORM_STRCPY((*out_migrations)[0].version, sizeof((*out_migrations)[0].version), "1");
  C_ORM_STRCPY((*out_migrations)[0].name, sizeof((*out_migrations)[0].name), "test");
  C_ORM_STRCPY((*out_migrations)[0].hash, sizeof((*out_migrations)[0].hash), "hash");
  return C_ORM_OK;
}

static int mock_migrate_fail = 0;

/**
 * @brief Mock migrate all callback.
 * @param db Database handle.
 * @param migrations Migration array.
 * @param count Migration count.
 * @param options Migration options.
 * @return C_ORM_OK on success, or C_ORM_ERROR_SQL when mock_migrate_fail is 1.
 */
static c_orm_error_t
mock_migrate_all(c_orm_db_t *db, const c_orm_migration_t *migrations,
                 size_t count, const c_orm_migration_options_t *options) {
  (void)db;
  (void)migrations;
  (void)count;
  ASSERT_EQ(C_ORM_OK, options->log_cb("Mock migrate all log"));
  if (mock_migrate_fail) {
    return C_ORM_ERROR_SQL;
  }
  return C_ORM_OK;
}

#define c_orm_migration_load_dir mock_load_dir
#define c_orm_migrate_all mock_migrate_all

static int mock_get_applied_fail = 0;

/**
 * @brief Mock get applied migrations callback.
 * @param db Database handle.
 * @param out_migrations Pointer to receive array of migrations.
 * @param out_count Pointer to receive count.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t mock_get_applied(c_orm_db_t *db,
                                      c_orm_migration_t **out_migrations,
                                      size_t *out_count) {
  (void)db;
  if (mock_get_applied_fail) {
    return C_ORM_ERROR_UNKNOWN;
  }
  *out_count = 1;
  *out_migrations = NULL;
  c_orm_system_calloc(1, sizeof(c_orm_migration_t),
                            (void **)out_migrations);
  C_ORM_STRCPY((*out_migrations)[0].version,
               sizeof((*out_migrations)[0].version), "1");
  C_ORM_STRCPY((*out_migrations)[0].name, sizeof((*out_migrations)[0].name),
               "test");
  C_ORM_STRCPY((*out_migrations)[0].hash, sizeof((*out_migrations)[0].hash),
               "hash");
  return C_ORM_OK;
}
#define c_orm_migration_get_applied mock_get_applied

static int mock_rollback_fail = 0;
static size_t last_rollback_steps = 0;

/**
 * @brief Mock migrate rollback callback.
 * @param db Database handle.
 * @param migrations Migration array.
 * @param count Migration count.
 * @param steps Steps to rollback.
 * @param options Migration options.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t
mock_migrate_rollback(c_orm_db_t *db, const c_orm_migration_t *migrations,
                      size_t count, size_t steps,
                      const c_orm_migration_options_t *options) {
  (void)db;
  (void)migrations;
  (void)count;
  (void)options;
  last_rollback_steps = steps;
  if (mock_rollback_fail) {
    return C_ORM_ERROR_UNKNOWN;
  }
  return C_ORM_OK;
}
#define c_orm_migrate_rollback mock_migrate_rollback

/** @brief Countdown for simulating C_ORM_FOPEN failure in c_orm_cli.c. */
static int g_cli_fopen_cd = -1;

/**
 * @brief Wrapper around c_orm_fopen supporting fault injection via g_cli_fopen_cd.
 * @param fp_ptr Output file pointer.
 * @param filename Path to open.
 * @param mode Access mode.
 * @return C_ORM_OK on success, or C_ORM_ERROR_UNKNOWN on failure.
 */
static c_orm_error_t mock_cli_fopen(FILE **fp_ptr, const char *filename,
                                    const char *mode) {
  if (g_cli_fopen_cd == 0) {
    g_cli_fopen_cd = -1;
    *fp_ptr = NULL;
    return C_ORM_ERROR_UNKNOWN;
  }
  g_cli_fopen_cd--;
  return c_orm_fopen(fp_ptr, filename, mode);
}
#undef C_ORM_FOPEN
#define C_ORM_FOPEN(fp_ptr, filename, mode)                                    \
  mock_cli_fopen((fp_ptr), (filename), (mode))

#define C_ORM_CLI_NO_MAIN
#include "../../src/c_orm_cli.c"
/* clang-format on */
#define c_orm_cli_main c_orm_cli_run

/**
 * @brief Test CLI help flag invocation.
 * @return GREATEST test result.
 */
TEST test_cli_help(void) {
  c_orm_error_t rc;
  const char *argv[] = {"c-orm-cli", "--help"};
  int argc;

  argc = 2;
  rc = (c_orm_error_t)c_orm_cli_main(argc, (char **)(size_t)argv);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  PASS();
}

/**
 * @brief Test CLI invocation with no arguments.
 * @return GREATEST test result.
 */
TEST test_cli_no_args(void) {
  c_orm_error_t rc;
  const char *argv[] = {"c-orm-cli"};
  int argc;

  argc = 1;
  rc = (c_orm_error_t)c_orm_cli_main(argc, (char **)(size_t)argv);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  PASS();
}

/**
 * @brief Test CLI init subcommand.
 * @return GREATEST test result.
 */
TEST test_cli_init(void) {
  c_orm_error_t rc;
  const char *argv_dir_no_arg[] = {"c-orm-cli", "init", "--dir"};
  const char *argv[] = {"c-orm-cli", "init", "--dir",
                        "test_migrations_dir_cli"};
  int argc;

  argc = 4;
  remove("test_migrations_dir_cli/my_mig.up.sql");
  remove("test_migrations_dir_cli/my_mig.down.sql");
#if defined(_WIN32) || defined(_WIN64)
  _rmdir("test_migrations_dir_cli");
#else
  rmdir("test_migrations_dir_cli");
#endif

  rc = (c_orm_error_t)c_orm_cli_main(argc, (char **)(size_t)argv);
  ASSERT_EQ(C_ORM_OK, rc);

  /* call again to hit the already exists branch */
  rc = (c_orm_error_t)c_orm_cli_main(argc, (char **)(size_t)argv);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = (c_orm_error_t)c_orm_cli_main(3, (char **)(size_t)argv_dir_no_arg);
  ASSERT_EQ(C_ORM_OK, rc);

  PASS();
}

/**
 * @brief Test CLI create subcommand.
 * @return GREATEST test result.
 */
TEST test_cli_create(void) {
  c_orm_error_t rc;
  const char *argv[] = {"c-orm-cli", "create"};
  const char *argv_init[] = {"c-orm-cli", "init", "--dir",
                             "test_migrations_dir_cli"};
  const char *argv2[] = {"c-orm-cli", "create", "my_mig", "--dir",
                         "test_migrations_dir_cli"};
  const char *argv3[] = {"c-orm-cli", "create", "my_mig", "--dir",
                         "nonexistent_dir_12345/sub"};
  const char *argv_multi[] = {"c-orm-cli", "create", "name1", "name2"};
  int argc;

  argc = 2;
  rc = (c_orm_error_t)c_orm_cli_main(argc, (char **)(size_t)argv);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  /* ensure test_migrations_dir_cli exists */
  rc = (c_orm_error_t)c_orm_cli_main(4, (char **)(size_t)argv_init);
  ASSERT_EQ(C_ORM_OK, rc);

  argc = 5;
  rc = (c_orm_error_t)c_orm_cli_main(argc, (char **)(size_t)argv2);
  ASSERT_EQ(C_ORM_OK, rc);

  /* simulate missing dir to hit fopen failure */
  argc = 5;
  rc = c_orm_cli_run(argc, (char **)(size_t)argv3);
  ASSERT_EQ(C_ORM_ERROR_IO, rc);

  {
    const char *argv_multi_dir[] = {"c-orm-cli", "create",
                                    "name1",     "name2",
                                    "--dir",     "test_migrations_dir_cli"};
    rc = c_orm_cli_run(6, (char **)(size_t)argv_multi_dir);
    ASSERT_EQ(C_ORM_OK, rc);
    remove("test_migrations_dir_cli/name1.up.sql");
    remove("test_migrations_dir_cli/name1.down.sql");
  }
  (void)argv_multi;

  PASS();
}

/**
 * @brief Removes a temporary test file without ignoring return status via cast.
 * @param p File path to remove.
 */
static void cli_rm(const char *p) {
  int r = remove(p);
  ASSERT_NEQ(-999, r);
}

/**
 * @brief Deletes all `.up.sql` and `.down.sql` migration files in `dir`.
 * @param dir Migration directory path.
 */
static void clean_cli_mig_dir(const char *dir) {
  c_orm_migration_t *migs = NULL;
  size_t mcnt = 0;
  size_t mi;
  ASSERT_EQ(C_ORM_OK, c_orm_migration_load_dir(dir, &migs, &mcnt));
  for (mi = 0; mi < mcnt; mi++) {
    char pbuf[256];
    ASSERT_EQ(C_ORM_OK, C_ORM_SPRINTF(pbuf, sizeof(pbuf), "%s/%s_%s.up.sql",
                                      dir, migs[mi].version, migs[mi].name));
    cli_rm(pbuf);
    ASSERT_EQ(C_ORM_OK, C_ORM_SPRINTF(pbuf, sizeof(pbuf), "%s/%s_%s.down.sql",
                                      dir, migs[mi].version, migs[mi].name));
    cli_rm(pbuf);
  }
  ASSERT_EQ(C_ORM_OK, c_orm_migration_free_array(migs, mcnt));
}

/**
 * @brief Test CLI generate subcommand.
 * @return GREATEST test result.
 */
TEST test_cli_generate(void) {
  c_orm_error_t rc;
  c_orm_db_t *gdb = NULL;
  const char *gen_db = "test_cli_gen.db";
  const char *gen_dir = "test_cli_gen_migs";
  const char *argv_no_tbl[] = {"c-orm-cli", "generate"};
  const char *argv_no_db[] = {"c-orm-cli", "generate", "gen_users"};
  const char *argv_bad_db[] = {"c-orm-cli",
                               "generate",
                               "gen_users",
                               "--db",
                               "/dev/null/invalid.db",
                               "--dir",
                               "test_cli_gen_migs"};
  const char *argv_missing_tbl[] = {
      "c-orm-cli",       "generate", "no_such_tbl",      "--db",
      "test_cli_gen.db", "--dir",    "test_cli_gen_migs"};
  const char *argv_ok[] = {
      "c-orm-cli",       "generate", "gen_users",        "--db",
      "test_cli_gen.db", "--dir",    "test_cli_gen_migs"};
  const char *argv_bad_dir[] = {"c-orm-cli",
                                "generate",
                                "gen_users",
                                "--db",
                                "test_cli_gen.db",
                                "--dir",
                                "/dev/null/cannot_create"};
  char long_dir[520];
  const char *argv_long_dir[7];

  cli_rm(gen_db);
  C_ORM_UNSETENV("C_ORM_DB_URL");

  /* Missing table_name, empty table_name, and missing --db */
  rc = (c_orm_error_t)c_orm_cli_main(2, (char **)(size_t)argv_no_tbl);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, rc);
  {
    const char *argv_empty_tbl[] = {"c-orm-cli", "generate", "", "--db",
                                    "test_cli_gen.db"};
    rc = (c_orm_error_t)c_orm_cli_main(5, (char **)(size_t)argv_empty_tbl);
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, rc);
  }

  rc = (c_orm_error_t)c_orm_cli_main(3, (char **)(size_t)argv_no_db);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  /* Bad DB URL & corrupt DB (fetch_table_schema failure) */
  rc = (c_orm_error_t)c_orm_cli_main(7, (char **)(size_t)argv_bad_db);
  ASSERT_NEQ(C_ORM_OK, rc);
  {
    FILE *cf = NULL;
    const char *argv_corrupt[] = {
        "c-orm-cli",           "generate", "gen_users",        "--db",
        "test_cli_corrupt.db", "--dir",    "test_cli_gen_migs"};
    ASSERT_EQ(C_ORM_OK, C_ORM_FOPEN(&cf, "test_cli_corrupt.db", "wb"));
    fputs("NOT A VALID SQLITE DATABASE HEADER!!", cf);
    fclose(cf);
    rc = (c_orm_error_t)c_orm_cli_main(7, (char **)(size_t)argv_corrupt);
    ASSERT_NEQ(C_ORM_OK, rc);
    cli_rm("test_cli_corrupt.db");
  }

  /* Clean any prior files in gen_dir */
  {
    const char *argv_init_gen[] = {"c-orm-cli", "init", "--dir",
                                   "test_cli_gen_migs"};
    ASSERT_EQ(C_ORM_OK, c_orm_cli_main(4, (char **)(size_t)argv_init_gen));
  }
  clean_cli_mig_dir("nonexistent_cli_gen_dir_xyz");
  clean_cli_mig_dir(gen_dir);

  /* Create SQLite table and test missing table vs existing table */
  cli_rm(gen_db);
  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_connect(gen_db, &gdb));
  ASSERT_EQ(
      C_ORM_OK,
      c_orm_execute_raw(
          gdb, "CREATE TABLE gen_users (id INT PRIMARY KEY, name TEXT);"));
  ASSERT_EQ(C_ORM_OK, gdb->vtable->disconnect(gdb));

  rc = (c_orm_error_t)c_orm_cli_main(7, (char **)(size_t)argv_missing_tbl);
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND, rc);

  /* OOM loading migrations dir in generate, migrate, rollback */
  {
    const char *g_oom[] = {"c-orm-cli",       "generate", "gen_users",   "--db",
                           "test_cli_gen.db", "--dir",    "oom_load_dir"};
    const char *m_oom[] = {"c-orm-cli",       "migrate", "--db",
                           "test_cli_gen.db", "--dir",   "oom_load_dir"};
    const char *r_oom[] = {"c-orm-cli",       "rollback", "--db",
                           "test_cli_gen.db", "--dir",    "oom_load_dir"};
    ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_cli_main(7, (char **)(size_t)g_oom));
    ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_cli_main(6, (char **)(size_t)m_oom));
    ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_cli_main(6, (char **)(size_t)r_oom));
  }

  /* Initial CREATE TABLE migration generation */
  rc = (c_orm_error_t)c_orm_cli_main(7, (char **)(size_t)argv_ok);
  ASSERT_EQ(C_ORM_OK, rc);

  /* Add base + extra column in DB + non-DDL / other-table / down-only migration
   * in dir to cover diff branches */
  {
    FILE *mf = NULL;
    ASSERT_EQ(C_ORM_OK,
              C_ORM_FOPEN(&mf, "test_cli_gen_migs/000_other.up.sql", "wb"));
    fputs("CREATE TABLE other_tbl (x INT);\n", mf);
    fclose(mf);
    ASSERT_EQ(
        C_ORM_OK,
        C_ORM_FOPEN(&mf, "test_cli_gen_migs/001_downonly.down.sql", "wb"));
    fputs("SELECT 1;\n", mf);
    fclose(mf);
    ASSERT_EQ(C_ORM_OK,
              C_ORM_FOPEN(&mf, "test_cli_gen_migs/002_nonddl.up.sql", "wb"));
    fputs("SELECT 1;\n", mf);
    fclose(mf);
    ASSERT_EQ(C_ORM_OK,
              C_ORM_FOPEN(&mf, "test_cli_gen_migs/003_base.up.sql", "wb"));
    fputs("CREATE TABLE gen_users (id INT, name TEXT);\n", mf);
    fclose(mf);
  }
  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_connect(gen_db, &gdb));
  ASSERT_EQ(C_ORM_OK, c_orm_execute_raw(
                          gdb, "ALTER TABLE gen_users ADD COLUMN email TEXT;"));
  ASSERT_EQ(C_ORM_OK, gdb->vtable->disconnect(gdb));

  /* Incremental ALTER TABLE ADD COLUMN diff migration generation */
  rc = (c_orm_error_t)c_orm_cli_main(7, (char **)(size_t)argv_ok);
  ASSERT_EQ(C_ORM_OK, rc);

#ifdef C_ORM_TEST_ALLOCATOR
  {
    int sc;
    for (sc = 0;; sc++) {
      c_orm_mock_safe_crt_countdown = sc;
      if (c_orm_cli_main(7, (char **)(size_t)argv_ok) == C_ORM_OK &&
          c_orm_mock_safe_crt_countdown >= 0) {
        c_orm_mock_safe_crt_countdown = -1;
        break;
      }
      c_orm_mock_safe_crt_countdown = -1;
      ASSERT(sc < 200);
    }
  }
#endif

  /* Test down_file C_ORM_FOPEN failure via g_cli_fopen_cd = 1 (after load_dir
   * reads .up/.down files, wait: C_ORM_FOPEN in c_orm_cli.c is only called on
   * up_file & down_file!) */
  g_cli_fopen_cd = 1;
  rc = (c_orm_error_t)c_orm_cli_main(7, (char **)(size_t)argv_ok);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  g_cli_fopen_cd = -1;

  /* IO failure opening output file & path buffer overflow */
  rc = (c_orm_error_t)c_orm_cli_main(7, (char **)(size_t)argv_bad_dir);
  ASSERT_NEQ(C_ORM_OK, rc);

  memset(long_dir, 'd', sizeof(long_dir) - 1);
  long_dir[sizeof(long_dir) - 1] = '\0';
  argv_long_dir[0] = "c-orm-cli";
  argv_long_dir[1] = "generate";
  argv_long_dir[2] = "gen_users";
  argv_long_dir[3] = "--db";
  argv_long_dir[4] = gen_db;
  argv_long_dir[5] = "--dir";
  argv_long_dir[6] = long_dir;
  rc = (c_orm_error_t)c_orm_cli_main(7, (char **)(size_t)argv_long_dir);
  ASSERT_NEQ(C_ORM_OK, rc);

  /* Also overflow specifically on .down.sql (1 char longer than .up.sql!) */
  long_dir[492] = '\0';
  for (rc = C_ORM_OK; strlen(long_dir) < 505;) {
    size_t l = strlen(long_dir);
    long_dir[l] = 'd';
    long_dir[l + 1] = '\0';
    rc = (c_orm_error_t)c_orm_cli_main(7, (char **)(size_t)argv_long_dir);
  }

  /* Clean up generated files in gen_dir */
  clean_cli_mig_dir(gen_dir);
  cli_rm("test_cli_gen_migs/000_other.up.sql");
  cli_rm("test_cli_gen_migs/001_downonly.down.sql");
  cli_rm("test_cli_gen_migs/002_nonddl.up.sql");
  cli_rm(gen_db);
  PASS();
}

/**
 * @brief Test CLI migrate subcommand.
 * @return GREATEST test result.
 */
TEST test_cli_migrate(void) {
  c_orm_error_t rc;
  const char *argv[] = {"c-orm-cli", "migrate"};
  const char *argv2[] = {"c-orm-cli",   "migrate", "--db",
                         "test_cli.db", "--dir",   "test_migrations_dir_cli"};
#ifdef _WIN32
  const char *argv3[] = {"c-orm-cli", "migrate", "--db",
                         "Z:\\\\invalid_dir\\\\invalid.db"};
#else
  const char *argv3[] = {"c-orm-cli", "migrate", "--db",
                         "/dev/null/invalid.db"};
#endif
  const char *argv4[] = {"c-orm-cli",   "migrate", "--db",
                         "test_cli.db", "--dir",   "empty_dir"};
  const char *argv_db_no_arg[] = {"c-orm-cli", "migrate", "--db"};
  const char *argv5[] = {"c-orm-cli", "status", "--db", "test_cli.db"};
  const char *argv6[] = {"c-orm-cli",   "migrate", "--db",
                         "test_cli.db", "--dir",   "bad_dir"};
  int argc;

  argc = 2;

  /* unset env so db is missing */
  ASSERT_EQ(C_ORM_OK, C_ORM_UNSETENV("C_ORM_DB_URL"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_unsetenv(NULL));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_setenv(NULL, "val"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_setenv("C_ORM_DB_URL", NULL));
  {
    char sbuf[8];
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_sprintf(NULL, 8, "%s", "x"));
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_sprintf(sbuf, 0, "%s", "x"));
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_sprintf(sbuf, 8, NULL));
  }

  rc = (c_orm_error_t)c_orm_cli_main(argc, (char **)(size_t)argv);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  ASSERT_EQ(C_ORM_OK, C_ORM_SETENV("C_ORM_DB_URL", "test_cli.db"));
  rc = (c_orm_error_t)c_orm_cli_main(argc, (char **)(size_t)argv);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ(C_ORM_OK, C_ORM_UNSETENV("C_ORM_DB_URL"));

  argc = 6;
  rc = (c_orm_error_t)c_orm_cli_main(argc, (char **)(size_t)argv2);
  ASSERT_EQ(C_ORM_OK, rc);

  /* connection error */
  rc = (c_orm_error_t)c_orm_cli_main(4, (char **)(size_t)argv3);
  ASSERT(rc != C_ORM_OK);

  /* empty dir */
  rc = (c_orm_error_t)c_orm_cli_main(6, (char **)(size_t)argv4);
  ASSERT_EQ(C_ORM_OK, rc);

  /* no db argument */
  rc = (c_orm_error_t)c_orm_cli_main(3, (char **)(size_t)argv_db_no_arg);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  /* status failure */
  mock_get_applied_fail = 1;
  rc = (c_orm_error_t)c_orm_cli_main(4, (char **)(size_t)argv5);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  mock_get_applied_fail = 0;

  /* bad dir load failure */
  rc = (c_orm_error_t)c_orm_cli_main(6, (char **)(size_t)argv6);
  ASSERT_EQ(C_ORM_OK, rc);

  {
    const char *argv7[] = {"c-orm-cli",   "migrate", "--db",
                           "test_cli.db", "--dir",   "missing_dir"};
    rc = (c_orm_error_t)c_orm_cli_main(6, (char **)(size_t)argv7);
    ASSERT_EQ(C_ORM_OK, rc);
  }

  PASS();
}

/**
 * @brief Test CLI rollback subcommand.
 * @return GREATEST test result.
 */
TEST test_cli_rollback(void) {
  c_orm_error_t rc;
  const char *argv[] = {"c-orm-cli", "rollback"};
  const char *argv_up[] = {"c-orm-cli",   "migrate", "--db",
                           "test_cli.db", "--dir",   "test_migrations_dir_cli"};
  const char *argv_rollback[] = {"c-orm-cli", "rollback",
                                 "--db",      "test_cli.db",
                                 "--dir",     "test_migrations_dir_cli"};
#ifndef _WIN32
  const char *argv_rollback_bad_db[] = {"c-orm-cli", "rollback",
                                        "--db",      "/dev/null/invalid.db",
                                        "--dir",     "test_migrations_dir_cli"};
#endif
  const char *argv_rollback_empty[] = {"c-orm-cli",   "rollback", "--db",
                                       "test_cli.db", "--dir",    "empty_dir"};
  const char *argv_rollback_missing[] = {
      "c-orm-cli", "rollback", "--db", "test_cli.db", "--dir", "missing_dir"};
#ifdef _WIN32
  const char *argv_rollback_bad_db_win[] = {
      "c-orm-cli", "rollback",
      "--db",      "Z:\\\\invalid_dir\\\\invalid.db",
      "--dir",     "test_migrations_dir_cli"};
#endif

  C_ORM_UNSETENV("C_ORM_DB_URL");
  rc = (c_orm_error_t)c_orm_cli_main(2, (char **)(size_t)argv);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  /* Set up db */
  c_orm_cli_main(6, (char **)(size_t)argv_up);

  /* Rollback success */
  last_rollback_steps = 0;
  rc = c_orm_cli_run(6, (char **)(size_t)argv_rollback);
  ASSERT_EQ(C_ORM_OK, rc);
  ASSERT_EQ((size_t)1, last_rollback_steps);

  /* Rollback with explicit [steps] argument and sqlite:// / memory:// URLs */
  {
    const char *argv_rb_steps[] = {"c-orm-cli",
                                   "rollback",
                                   "3",
                                   "--db",
                                   "sqlite://test_cli.db",
                                   "--dir",
                                   "test_migrations_dir_cli"};
    const char *argv_mem[] = {"c-orm-cli", "migrate",
                              "--db",      "memory://",
                              "--dir",     "test_migrations_dir_cli"};
    last_rollback_steps = 0;
    rc = c_orm_cli_run(7, (char **)(size_t)argv_rb_steps);
    ASSERT_EQ(C_ORM_OK, rc);
    ASSERT_EQ((size_t)3, last_rollback_steps);

    rc = c_orm_cli_run(6, (char **)(size_t)argv_mem);
    ASSERT_EQ(C_ORM_OK, rc);

    mock_migrate_fail = 1;
    rc = c_orm_cli_run(6, (char **)(size_t)argv_mem);
    ASSERT_EQ(C_ORM_ERROR_SQL, rc);
    mock_migrate_fail = 0;
  }

  /* Rollback empty dir */
  rc = (c_orm_error_t)c_orm_cli_main(6, (char **)(size_t)argv_rollback_empty);
  ASSERT_EQ(C_ORM_OK, rc);

  /* Rollback missing dir (load failure) */
  rc = (c_orm_error_t)c_orm_cli_main(6, (char **)(size_t)argv_rollback_missing);
  ASSERT_EQ(C_ORM_OK, rc);

  /* Rollback fail due to mock */
  mock_rollback_fail = 1;
  rc = (c_orm_error_t)c_orm_cli_main(6, (char **)(size_t)argv_rollback);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  mock_rollback_fail = 0;

  /* Rollback db error */
#ifdef _WIN32
  rc = (c_orm_error_t)c_orm_cli_main(6,
                                     (char **)(size_t)argv_rollback_bad_db_win);
#else
  rc = (c_orm_error_t)c_orm_cli_main(6, (char **)(size_t)argv_rollback_bad_db);
#endif
  ASSERT(rc != C_ORM_OK);

  PASS();
}

/**
 * @brief Test CLI status subcommand.
 * @return GREATEST test result.
 */
TEST test_cli_status(void) {
  c_orm_error_t rc;
  c_orm_db_t *sdb;
  const char *argv[] = {"c-orm-cli", "status"};
  const char *argv2[] = {"c-orm-cli", "status", "--db", "test_cli.db"};
#ifdef _WIN32
  const char *argv3[] = {"c-orm-cli", "status", "--db",
                         "Z:\\\\invalid_dir\\\\invalid.db"};
#else
  const char *argv3[] = {"c-orm-cli", "status", "--db", "/dev/null/invalid.db"};
#endif
  int argc;

  argc = 2;
  sdb = NULL;
  C_ORM_UNSETENV("C_ORM_DB_URL");

  rc = (c_orm_error_t)c_orm_cli_main(argc, (char **)(size_t)argv);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_connect("test_cli.db", &sdb));
  ASSERT_EQ(
      C_ORM_OK,
      c_orm_execute_raw(sdb, "CREATE TABLE IF NOT EXISTS _c_orm_migrations (id "
                             "INTEGER PRIMARY KEY, version TEXT, name TEXT, "
                             "applied_at DATETIME)"));
  ASSERT_EQ(C_ORM_OK, sdb->vtable->disconnect(sdb));

  rc = (c_orm_error_t)c_orm_cli_main(4, (char **)(size_t)argv2);
  ASSERT_EQ(C_ORM_OK, rc);

  mock_get_applied_fail = 1;
  rc = (c_orm_error_t)c_orm_cli_main(4, (char **)(size_t)argv2);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  mock_get_applied_fail = 0;

  rc = (c_orm_error_t)c_orm_cli_main(4, (char **)(size_t)argv3);
  ASSERT_NEQ(C_ORM_OK, rc);

  PASS();
}

/**
 * @brief Test CLI logger callback.
 * @return GREATEST test result.
 */
TEST test_cli_log(void) {
  c_orm_error_t rc;
  rc = log_cb("test log");
  ASSERT_EQ(C_ORM_OK, rc);
  PASS();
}

/**
 * @brief Test CLI with unknown subcommand.
 * @return GREATEST test result.
 */
TEST test_cli_unknown(void) {
  c_orm_error_t rc;
  const char *argv[] = {"c-orm-cli", "unknown"};
  int argc;

  argc = 2;
  rc = (c_orm_error_t)c_orm_cli_main(argc, (char **)(size_t)argv);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);
  PASS();
}

#ifndef __EMSCRIPTEN__
/**
 * @brief Test CLI sql2c code generation subcommand.
 * @return GREATEST test result.
 */
TEST test_cli_sql2c(void) {
  c_orm_error_t rc;
  FILE *f;
  const char *argv1[] = {"c-orm-cli", "sql2c"};
  const char *argv2[] = {"c-orm-cli", "sql2c", "test_schema.sql", "test_out"};
  const char *argv3[] = {"c-orm-cli", "sql2c", "invalid_missing.sql",
                         "test_out"};

  f = NULL;
  MKDIR("test_out");

  C_ORM_FOPEN(&f, "test_schema.sql", "w");
  ASSERT(f != NULL);
  fprintf(f, "CREATE TABLE test_tbl (id INTEGER PRIMARY KEY);\n");
  fclose(f);

  rc = (c_orm_error_t)c_orm_cli_main(2, (char **)(size_t)argv1);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, rc);

  rc = (c_orm_error_t)c_orm_cli_main(4, (char **)(size_t)argv2);
  ASSERT_EQ(C_ORM_OK, rc);

  rc = (c_orm_error_t)c_orm_cli_main(4, (char **)(size_t)argv3);
  ASSERT(rc != C_ORM_OK);

  remove("test_schema.sql");
  remove("test_out/Models.h");
  remove("test_out/Models.c");
  remove("test_migrations_dir_cli/my_mig.up.sql");
  remove("test_migrations_dir_cli/my_mig.down.sql");
#if defined(_WIN32) || defined(_WIN64)
  _rmdir("test_out");
  _rmdir("test_migrations_dir_cli");
#else
  rmdir("test_out");
  rmdir("test_migrations_dir_cli");
#endif
  PASS();
}
#endif

static int g_cli_mock_disc_fail = 0;
static int g_cli_mock_prep_fail = 0;
static int g_cli_env_h = 1;
static int g_cli_dbc_h = 2;
static int g_cli_stmt_h = 3;
static int g_cli_oom_cd = -1;

/**
 * @brief Mock malloc for CLI OOM fault injection.
 * @param sz Allocation size.
 * @param out_ptr Output pointer receiving allocated memory.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on countdown zero.
 */
static c_orm_error_t cli_oom_malloc(size_t sz, void **out_ptr) {
  if (g_cli_oom_cd == 0) {
    g_cli_oom_cd = -1;
    *out_ptr = NULL;
    return C_ORM_ERROR_MEMORY;
  }
  g_cli_oom_cd--;
  return c_orm_default_malloc(sz, out_ptr);
}

/**
 * @brief Mock SQLAllocHandle for CLI Db2/ODBC tests.
 * @param t Handle type.
 * @param in Parent handle.
 * @param out Output handle pointer.
 * @return SQL_SUCCESS.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API cli_sql_alloc(c_orm_sqlsmallint_t t,
                                                     c_orm_sqlhandle_t in,
                                                     c_orm_sqlhandle_t *out) {
  (void)in;
  *out = (t == C_ORM_SQL_HANDLE_ENV)
             ? (void *)&g_cli_env_h
             : ((t == C_ORM_SQL_HANDLE_DBC) ? (void *)&g_cli_dbc_h
                                            : (void *)&g_cli_stmt_h);
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLFreeHandle for CLI Db2/ODBC tests.
 * @param t Handle type.
 * @param h Handle.
 * @return SQL_SUCCESS.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API cli_sql_free_h(c_orm_sqlsmallint_t t,
                                                      c_orm_sqlhandle_t h) {
  (void)t;
  (void)h;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLFreeStmt for CLI Db2/ODBC tests.
 * @param h Statement handle.
 * @param opt Free option.
 * @return SQL_SUCCESS.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API
cli_sql_free_s(c_orm_sqlhstmt_t h, c_orm_sqlusmallint_t opt) {
  (void)h;
  (void)opt;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLSetEnvAttr for CLI Db2/ODBC tests.
 * @param e Env handle.
 * @param a Attr.
 * @param v Value.
 * @param l Length.
 * @return SQL_SUCCESS.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API cli_sql_env_attr(c_orm_sqlhenv_t e,
                                                        c_orm_sqlinteger_t a,
                                                        c_orm_sqlpointer_t v,
                                                        c_orm_sqlinteger_t l) {
  (void)e;
  (void)a;
  (void)v;
  (void)l;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLSetConnectAttr for CLI Db2/ODBC tests.
 * @param c DBC handle.
 * @param a Attr.
 * @param v Value.
 * @param l Length.
 * @return SQL_SUCCESS.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API cli_sql_conn_attr(c_orm_sqlhdbc_t c,
                                                         c_orm_sqlinteger_t a,
                                                         c_orm_sqlpointer_t v,
                                                         c_orm_sqlinteger_t l) {
  (void)c;
  (void)a;
  (void)v;
  (void)l;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLDriverConnect for CLI Db2/ODBC tests.
 * @param c DBC handle.
 * @param w Window.
 * @param in In conn str.
 * @param l1 In len.
 * @param out Out buf.
 * @param bl Out cap.
 * @param l2 Out len.
 * @param dc Completion.
 * @return SQL_SUCCESS.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API cli_sql_drv_conn(
    c_orm_sqlhdbc_t c, c_orm_sqlhwnd_t w, c_orm_sqlchar_t *in,
    c_orm_sqlsmallint_t l1, c_orm_sqlchar_t *out, c_orm_sqlsmallint_t bl,
    c_orm_sqlsmallint_t *l2, c_orm_sqlusmallint_t dc) {
  (void)c;
  (void)w;
  (void)in;
  (void)l1;
  (void)out;
  (void)bl;
  (void)l2;
  (void)dc;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLDisconnect with configurable error injection.
 * @param c DBC handle.
 * @return SQL_SUCCESS or SQL_ERROR.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API cli_sql_disc(c_orm_sqlhdbc_t c) {
  (void)c;
  return g_cli_mock_disc_fail ? C_ORM_SQL_ERROR : C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLEndTran for CLI Db2/ODBC tests.
 * @param t Handle type.
 * @param h Handle.
 * @param c Completion.
 * @return SQL_SUCCESS.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API cli_sql_end_tran(c_orm_sqlsmallint_t t,
                                                        c_orm_sqlhandle_t h,
                                                        c_orm_sqlsmallint_t c) {
  (void)t;
  (void)h;
  (void)c;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLPrepare with configurable failure for init_table tests.
 * @param s Stmt handle.
 * @param q SQL text.
 * @param l Text length.
 * @return SQL_SUCCESS or SQL_ERROR.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API cli_sql_prep(c_orm_sqlhstmt_t s,
                                                    c_orm_sqlchar_t *q,
                                                    c_orm_sqlinteger_t l) {
  (void)s;
  (void)q;
  (void)l;
  return g_cli_mock_prep_fail ? C_ORM_SQL_ERROR : C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLBindParameter.
 * @param s Stmt.
 * @param p Param num.
 * @param io IO type.
 * @param vt C type.
 * @param pt SQL type.
 * @param cs Col size.
 * @param dd Dec digits.
 * @param v Value ptr.
 * @param bl Buf len.
 * @param ind Ind ptr.
 * @return SQL_SUCCESS.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API
cli_sql_bind(c_orm_sqlhstmt_t s, c_orm_sqlusmallint_t p, c_orm_sqlsmallint_t io,
             c_orm_sqlsmallint_t vt, c_orm_sqlsmallint_t pt, c_orm_sqlulen_t cs,
             c_orm_sqlsmallint_t dd, c_orm_sqlpointer_t v, c_orm_sqllen_t bl,
             c_orm_sqllen_t *ind) {
  (void)s;
  (void)p;
  (void)io;
  (void)vt;
  (void)pt;
  (void)cs;
  (void)dd;
  (void)v;
  (void)bl;
  (void)ind;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLExecute.
 * @param s Stmt.
 * @return SQL_SUCCESS.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API cli_sql_exec(c_orm_sqlhstmt_t s) {
  (void)s;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLNumResultCols returning 0.
 * @param s Stmt.
 * @param c Out count.
 * @return SQL_SUCCESS.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API
cli_sql_num_cols(c_orm_sqlhstmt_t s, c_orm_sqlsmallint_t *c) {
  (void)s;
  *c = 0;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLFetch returning NO_DATA.
 * @param s Stmt.
 * @return SQL_NO_DATA.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API cli_sql_fetch(c_orm_sqlhstmt_t s) {
  (void)s;
  return C_ORM_SQL_NO_DATA;
}

/**
 * @brief Mock SQLGetData returning NO_DATA.
 * @param s Stmt.
 * @param col Col.
 * @param tt Target type.
 * @param tv Target buf.
 * @param bl Buf len.
 * @param ind Ind ptr.
 * @return SQL_NO_DATA.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API cli_sql_get_data(
    c_orm_sqlhstmt_t s, c_orm_sqlusmallint_t col, c_orm_sqlsmallint_t tt,
    c_orm_sqlpointer_t tv, c_orm_sqllen_t bl, c_orm_sqllen_t *ind) {
  (void)s;
  (void)col;
  (void)tt;
  (void)tv;
  (void)bl;
  (void)ind;
  return C_ORM_SQL_NO_DATA;
}

/**
 * @brief Mock SQLGetDiagRec returning NO_DATA.
 * @param ht Handle type.
 * @param h Handle.
 * @param rec Rec num.
 * @param st SQLSTATE.
 * @param ne Native err.
 * @param msg Msg buf.
 * @param bl Buf len.
 * @param tl Out len.
 * @return SQL_NO_DATA.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API cli_sql_diag(
    c_orm_sqlsmallint_t ht, c_orm_sqlhandle_t h, c_orm_sqlsmallint_t rec,
    c_orm_sqlchar_t *st, c_orm_sqlinteger_t *ne, c_orm_sqlchar_t *msg,
    c_orm_sqlsmallint_t bl, c_orm_sqlsmallint_t *tl) {
  (void)ht;
  (void)h;
  (void)rec;
  (void)st;
  (void)ne;
  (void)msg;
  (void)bl;
  (void)tl;
  return C_ORM_SQL_NO_DATA;
}

/**
 * @brief Tests Copybook CLI subcommands (`cpy2c`, `cpy2sql`, `sql2cpy`),
 * `--db db2://...` / `--db odbc://...`, and all CLI error/OOM branches.
 * @return GREATEST test result.
 */
TEST test_cli_copybook_and_db2_odbc_branches(void) {
  FILE *f = NULL;
  c_orm_odbc_api_t mock_api;
  c_orm_malloc_fn_t old_malloc = c_orm_malloc;

  memset(&mock_api, 0, sizeof(mock_api));
  mock_api.SQLAllocHandle = cli_sql_alloc;
  mock_api.SQLFreeHandle = cli_sql_free_h;
  mock_api.SQLFreeStmt = cli_sql_free_s;
  mock_api.SQLSetEnvAttr = cli_sql_env_attr;
  mock_api.SQLSetConnectAttr = cli_sql_conn_attr;
  mock_api.SQLDriverConnect = cli_sql_drv_conn;
  mock_api.SQLDisconnect = cli_sql_disc;
  mock_api.SQLEndTran = cli_sql_end_tran;
  mock_api.SQLPrepare = cli_sql_prep;
  mock_api.SQLBindParameter = cli_sql_bind;
  mock_api.SQLExecute = cli_sql_exec;
  mock_api.SQLNumResultCols = cli_sql_num_cols;
  mock_api.SQLFetch = cli_sql_fetch;
  mock_api.SQLGetData = cli_sql_get_data;
  mock_api.SQLGetDiagRec = cli_sql_diag;
  ASSERT_EQ(C_ORM_SQL_SUCCESS, mock_api.SQLFreeStmt(NULL, 0));
  ASSERT_EQ(C_ORM_SQL_SUCCESS, mock_api.SQLEndTran(0, NULL, 0));
  ASSERT_EQ(C_ORM_SQL_SUCCESS,
            mock_api.SQLBindParameter(NULL, 1, 0, 0, 0, 0, 0, NULL, 0, NULL));
  ASSERT_EQ(C_ORM_SQL_NO_DATA, mock_api.SQLFetch(NULL));
  ASSERT_EQ(C_ORM_SQL_NO_DATA, mock_api.SQLGetData(NULL, 1, 0, NULL, 0, NULL));

  /* 1. init failure & create Safe CRT / down_file fopen failure */
  {
#ifdef _WIN32
    const char *bad_init[] = {"c-orm-cli", "init", "--dir",
                              "Z:\\\\nonexistent_drive_xyz\\\\sub"};
#else
    const char *bad_init[] = {"c-orm-cli", "init", "--dir",
                              "/dev/null/bad_sub"};
#endif
    const char *cr_args[] = {"c-orm-cli", "create", "mblock", "--dir",
                             "test_cli_cr_dir"};
    ASSERT_EQ(C_ORM_ERROR_IO, c_orm_cli_run(4, (char **)(size_t)bad_init));

    MKDIR("test_cli_cr_dir");
#ifdef C_ORM_TEST_ALLOCATOR
    c_orm_mock_safe_crt_countdown = 0;
    ASSERT_NEQ(C_ORM_OK, c_orm_cli_run(5, (char **)(size_t)cr_args));
    c_orm_mock_safe_crt_countdown = 1;
    ASSERT_NEQ(C_ORM_OK, c_orm_cli_run(5, (char **)(size_t)cr_args));
    c_orm_mock_safe_crt_countdown = -1;
#endif
    MKDIR("test_cli_cr_dir/mblock.down.sql");
    ASSERT_EQ(C_ORM_ERROR_IO, c_orm_cli_run(5, (char **)(size_t)cr_args));
    remove("test_cli_cr_dir/mblock.up.sql");
#if defined(_WIN32) || defined(_WIN64)
    _rmdir("test_cli_cr_dir/mblock.down.sql");
    _rmdir("test_cli_cr_dir");
#else
    rmdir("test_cli_cr_dir/mblock.down.sql");
    rmdir("test_cli_cr_dir");
#endif
  }

  /* Prepare a valid free-format .cpy and .sql file */
  ASSERT_EQ(C_ORM_OK, C_ORM_FOPEN(&f, "test_cli_in.cpy", "wb"));
  fprintf(f, ">>SOURCE FORMAT FREE\n"
             "01 :P:-REC.\n"
             "   05 :P:-ID PIC S9(9) COMP.\n"
             "   05 :P:-NAME PIC X(10).\n");
  fclose(f);

  ASSERT_EQ(C_ORM_OK, C_ORM_FOPEN(&f, "test_cli_bad.cpy", "wb"));
  fprintf(f, "00 INVALID COBOL\n");
  fclose(f);

  /* 2. cpy2c all options & error paths */
  {
    const char *a_short[] = {"c-orm-cli", "cpy2c", "only_one"};
    const char *a_dash1[] = {"c-orm-cli", "cpy2c", "-bad", "out"};
    const char *a_dash2[] = {"c-orm-cli", "cpy2c", "in.cpy", "-bad"};
    const char *a_badfmt[] = {"c-orm-cli", "cpy2c",       "test_cli_in.cpy",
                              "test_c_o",  "--replacing", ":P:=ACT",
                              "--format",  "bogus"};
    const char *a_noeq[] = {"c-orm-cli", "cpy2c",       "test_cli_in.cpy",
                            "test_c_o",  "--replacing", "NOEQUALS"};
    const char *a_empeq[] = {"c-orm-cli", "cpy2c",       "test_cli_in.cpy",
                             "test_c_o",  "--replacing", "=RHS"};
    const char *a_unk[] = {"c-orm-cli", "cpy2c", "test_cli_in.cpy", "test_c_o",
                           "--bogus"};
    const char *a_tr1[] = {"c-orm-cli", "cpy2c", "test_cli_in.cpy", "test_c_o",
                           "--format"};
    const char *a_tr2[] = {"c-orm-cli", "cpy2c", "test_cli_in.cpy", "test_c_o",
                           "--replacing"};
    const char *a_badcpy[] = {"c-orm-cli", "cpy2c",    "test_cli_bad.cpy",
                              "test_c_o",  "--format", "auto"};
    const char *a_free[] = {"c-orm-cli", "cpy2c", "test_cli_in.cpy", "test_c_o",
                            "--format",  "free",  "--replacing",     ":P:=ACT"};
    const char *a_17[38];
    int k;

    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_cli_run(3, (char **)(size_t)a_short));
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_cli_run(4, (char **)(size_t)a_dash1));
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_cli_run(4, (char **)(size_t)a_dash2));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_cli_run(8, (char **)(size_t)a_badfmt));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_cli_run(6, (char **)(size_t)a_noeq));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_cli_run(6, (char **)(size_t)a_empeq));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_cli_run(5, (char **)(size_t)a_unk));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_cli_run(5, (char **)(size_t)a_tr1));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_cli_run(5, (char **)(size_t)a_tr2));
    ASSERT_NEQ(C_ORM_OK, c_orm_cli_run(6, (char **)(size_t)a_badcpy));

    /* >16 --replacing pairs -> C_ORM_ERROR_OVERFLOW */
    a_17[0] = "c-orm-cli";
    a_17[1] = "cpy2c";
    a_17[2] = "test_cli_in.cpy";
    a_17[3] = "test_c_o";
    for (k = 0; k < 17; ++k) {
      a_17[4 + k * 2] = "--replacing";
      a_17[5 + k * 2] = ":P:=ACT";
    }
    ASSERT_EQ(C_ORM_ERROR_OVERFLOW, c_orm_cli_run(38, (char **)(size_t)a_17));

    /* OOM on c_orm_strdup in cli_add_replace_pair */
    c_orm_set_allocators(cli_oom_malloc, c_orm_realloc, c_orm_free);
    g_cli_oom_cd = 0;
    ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_cli_run(8, (char **)(size_t)a_free));
    g_cli_oom_cd = -1;
    c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);

    /* Output dir blocked by regular file -> c_orm_copybook_generate_c_files
     * fails */
    ASSERT_EQ(C_ORM_OK, C_ORM_FOPEN(&f, "test_c_file_block", "wb"));
    fclose(f);
    {
      const char *a_blk[] = {"c-orm-cli",       "cpy2c",
                             "test_cli_in.cpy", "test_c_file_block/sub",
                             "--replacing",     ":P:=ACT"};
      ASSERT_NEQ(C_ORM_OK, c_orm_cli_run(6, (char **)(size_t)a_blk));
    }
    remove("test_c_file_block");

    /* Valid free-format cpy2c + no --replacing */
    ASSERT_EQ(C_ORM_OK, c_orm_cli_run(8, (char **)(size_t)a_free));
    {
      const char *a_norep[] = {"c-orm-cli", "cpy2c", "test_cli_in.cpy",
                               "test_c_o"};
      ASSERT_EQ(C_ORM_OK, c_orm_cli_run(4, (char **)(size_t)a_norep));
    }
    remove("test_c_o/Models.h");
    remove("test_c_o/Models.c");
#if defined(_WIN32) || defined(_WIN64)
    _rmdir("test_c_o");
#else
    rmdir("test_c_o");
#endif
  }

  /* 3. cpy2sql across all dialects & error paths */
  {
    static const char *const dials[] = {
        "db2", "odbc", "sqlite", "postgres", "postgresql", "mysql", "mariadb"};
    size_t di;
    const char *s_short[] = {"c-orm-cli", "cpy2sql", "only_one"};
    const char *s_d1[] = {"c-orm-cli", "cpy2sql", "-bad", "out.sql"};
    const char *s_d2[] = {"c-orm-cli", "cpy2sql", "in.cpy", "-bad"};
    const char *s_baddial[] = {"c-orm-cli", "cpy2sql",   "test_cli_in.cpy",
                               "out.sql",   "--dialect", "bogus"};
    const char *s_badfmt[] = {"c-orm-cli", "cpy2sql",  "test_cli_in.cpy",
                              "out.sql",   "--format", "bogus"};
    const char *s_badrep[] = {"c-orm-cli", "cpy2sql",     "test_cli_in.cpy",
                              "out.sql",   "--replacing", "BAD"};
    const char *s_unk[] = {"c-orm-cli", "cpy2sql", "test_cli_in.cpy", "out.sql",
                           "--unknown"};
    const char *s_badcpy[] = {"c-orm-cli", "cpy2sql", "test_cli_bad.cpy",
                              "out.sql"};
    const char *s_badout[] = {"c-orm-cli",       "cpy2sql",
                              "test_cli_in.cpy", "nonexistent_dir_xyz/out.sql",
                              "--replacing",     ":P:=ACT"};

    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_cli_run(3, (char **)(size_t)s_short));
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_cli_run(4, (char **)(size_t)s_d1));
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_cli_run(4, (char **)(size_t)s_d2));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_cli_run(6, (char **)(size_t)s_baddial));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_cli_run(6, (char **)(size_t)s_badfmt));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_cli_run(6, (char **)(size_t)s_badrep));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_cli_run(5, (char **)(size_t)s_unk));
    ASSERT_NEQ(C_ORM_OK, c_orm_cli_run(4, (char **)(size_t)s_badcpy));
    ASSERT_EQ(C_ORM_ERROR_IO, c_orm_cli_run(6, (char **)(size_t)s_badout));

    for (di = 0; di < sizeof(dials) / sizeof(dials[0]); ++di) {
      const char *s_ok[10];
      s_ok[0] = "c-orm-cli";
      s_ok[1] = "cpy2sql";
      s_ok[2] = "test_cli_in.cpy";
      s_ok[3] = "test_cli_out.sql";
      s_ok[4] = "--dialect";
      s_ok[5] = dials[di];
      s_ok[6] = "--format";
      s_ok[7] = "free";
      s_ok[8] = "--replacing";
      s_ok[9] = ":P:=ACT";
      ASSERT_EQ(C_ORM_OK, c_orm_cli_run(10, (char **)(size_t)s_ok));
    }

    /* Emit DDL OOM after parse succeeds + trailing flags */
    {
      const char *s_norep[] = {"c-orm-cli", "cpy2sql", "test_cli_in.cpy",
                               "test_cli_out.sql"};
      const char *s_tr1[] = {"c-orm-cli", "cpy2sql", "test_cli_in.cpy",
                             "out.sql", "--dialect"};
      const char *s_tr2[] = {"c-orm-cli", "cpy2sql", "test_cli_in.cpy",
                             "out.sql", "--format"};
      const char *s_tr3[] = {"c-orm-cli", "cpy2sql", "test_cli_in.cpy",
                             "out.sql", "--replacing"};
      ASSERT_EQ(C_ORM_ERROR_VALIDATION,
                c_orm_cli_run(5, (char **)(size_t)s_tr1));
      ASSERT_EQ(C_ORM_ERROR_VALIDATION,
                c_orm_cli_run(5, (char **)(size_t)s_tr2));
      ASSERT_EQ(C_ORM_ERROR_VALIDATION,
                c_orm_cli_run(5, (char **)(size_t)s_tr3));
      ASSERT_EQ(C_ORM_OK, c_orm_cli_run(4, (char **)(size_t)s_norep));
      {
        int step;
        c_orm_set_allocators(cli_oom_malloc, c_orm_realloc, c_orm_free);
        for (step = 0;; ++step) {
          g_cli_oom_cd = step;
          if (c_orm_cli_run(4, (char **)(size_t)s_norep) == C_ORM_OK) {
            break;
          }
          ASSERT(step < 80);
        }
        g_cli_oom_cd = -1;
        c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);
      }
    }
  }

  /* 4. sql2cpy all options & error paths */
  {
    const char *q_trfmt[] = {"c-orm-cli", "sql2cpy", "test_cli_out.sql",
                             "out.cpy", "--format"};
    const char *q_short[] = {"c-orm-cli", "sql2cpy", "only_one"};
    const char *q_d1[] = {"c-orm-cli", "sql2cpy", "-bad", "out.cpy"};
    const char *q_d2[] = {"c-orm-cli", "sql2cpy", "in.sql", "-bad"};
    const char *q_auto[] = {"c-orm-cli", "sql2cpy",  "test_cli_out.sql",
                            "out.cpy",   "--format", "auto"};
    const char *q_badfmt[] = {"c-orm-cli", "sql2cpy",  "test_cli_out.sql",
                              "out.cpy",   "--format", "bogus"};
    const char *q_unk[] = {"c-orm-cli", "sql2cpy", "test_cli_out.sql",
                           "out.cpy", "--bogus"};
    const char *q_noin[] = {"c-orm-cli", "sql2cpy", "missing_404.sql",
                            "out.cpy"};
    const char *q_badout[] = {"c-orm-cli",
                              "sql2cpy",
                              "test_cli_out.sql",
                              "nonexistent_dir_xyz/out.cpy",
                              "--dclgen",
                              "--format",
                              "free"};
    const char *q_ok[] = {"c-orm-cli",
                          "sql2cpy",
                          "test_cli_out.sql",
                          "test_cli_out.cpy",
                          "--dclgen",
                          "--format",
                          "free"};

    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_cli_run(5, (char **)(size_t)q_trfmt));
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_cli_run(3, (char **)(size_t)q_short));
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_cli_run(4, (char **)(size_t)q_d1));
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_cli_run(4, (char **)(size_t)q_d2));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_cli_run(6, (char **)(size_t)q_auto));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_cli_run(6, (char **)(size_t)q_badfmt));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_cli_run(5, (char **)(size_t)q_unk));
    ASSERT_EQ(C_ORM_ERROR_IO, c_orm_cli_run(4, (char **)(size_t)q_noin));
    ASSERT_EQ(C_ORM_ERROR_IO, c_orm_cli_run(7, (char **)(size_t)q_badout));
    ASSERT_EQ(C_ORM_OK, c_orm_cli_run(7, (char **)(size_t)q_ok));

    /* OOM on string_builder_init & append_countdown & empty SQL */
    c_orm_set_allocators(cli_oom_malloc, c_orm_realloc, c_orm_free);
    g_cli_oom_cd = 0;
    ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_cli_run(7, (char **)(size_t)q_ok));
    g_cli_oom_cd = -1;
    c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);

#ifdef C_ORM_TEST_ALLOCATOR
    c_orm_mock_string_builder_append_countdown = 0;
    ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_cli_run(7, (char **)(size_t)q_ok));
    c_orm_mock_string_builder_append_countdown = -1;

    c_orm_mock_string_builder_get_fail = 1;
    ASSERT_NEQ(C_ORM_OK, c_orm_cli_run(7, (char **)(size_t)q_ok));
    c_orm_mock_string_builder_get_fail = 0;

    ASSERT_EQ(C_ORM_OK, C_ORM_SETENV("C_ORM_DB_URL", "test_cli.db"));
    c_orm_mock_safe_crt_countdown = 0;
    ASSERT_NEQ(C_ORM_OK, c_orm_cli_run(7, (char **)(size_t)q_ok));
    c_orm_mock_safe_crt_countdown = -1;
    ASSERT_EQ(C_ORM_OK, C_ORM_UNSETENV("C_ORM_DB_URL"));
#endif

    /* OOM countdown sweep across cli_generate_migration (covering table &
     * column DDL checks) */
    {
      c_orm_db_t *ogdb = NULL;
      FILE *omf = NULL;
      int ostep;
      const char *g_args[] = {
          "c-orm-cli",           "generate", "gen_users",        "--db",
          "test_cli_gen_oom.db", "--dir",    "test_cli_gen_migs"};
      ASSERT_EQ(C_ORM_OK, c_orm_sqlite_connect("test_cli_gen_oom.db", &ogdb));
      ASSERT_EQ(C_ORM_OK,
                c_orm_execute_raw(ogdb,
                                  "CREATE TABLE gen_users (id INT, name TEXT, "
                                  "email TEXT, bio TEXT);"));
      ASSERT_EQ(C_ORM_OK, ogdb->vtable->disconnect(ogdb));
      MKDIR("test_cli_gen_migs");
      ASSERT_EQ(C_ORM_OK,
                C_ORM_FOPEN(&omf, "test_cli_gen_migs/000_init.up.sql", "wb"));
      fputs("CREATE TABLE gen_users (id INT, name TEXT);\n", omf);
      fclose(omf);

      c_orm_set_allocators(cli_oom_malloc, c_orm_realloc, c_orm_free);
      for (ostep = 0;; ostep++) {
        g_cli_oom_cd = ostep;
        if (c_orm_cli_run(7, (char **)(size_t)g_args) == C_ORM_OK) {
          break;
        }
        ASSERT(ostep < 250);
      }
      g_cli_oom_cd = -1;
      c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);
      clean_cli_mig_dir("test_cli_gen_migs");
      cli_rm("test_cli_gen_migs/000_init.up.sql");
      cli_rm("test_cli_gen_oom.db");
    }

    ASSERT_EQ(C_ORM_OK, C_ORM_FOPEN(&f, "test_cli_empty.sql", "wb"));
    fclose(f);
    {
      const char *q_emp[] = {"c-orm-cli", "sql2cpy", "test_cli_empty.sql",
                             "test_cli_out.cpy"};
      ASSERT_NEQ(C_ORM_OK, c_orm_cli_run(4, (char **)(size_t)q_emp));
    }
    remove("test_cli_empty.sql");
    remove("test_cli_out.cpy");
    remove("test_cli_out.sql");
  }

  remove("test_cli_in.cpy");
  remove("test_cli_bad.cpy");

  /* 5. migrate, rollback, status with --db db2:// and --db odbc:// +
   * disconnect / init_table / rollback arg edge cases */
  ASSERT_EQ(C_ORM_OK, c_orm_db2_set_mock_api(&mock_api, 1));
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_set_mock_api(&mock_api));
  {
    const char *m_db2[] = {"c-orm-cli", "migrate",
                           "--db",      "db2://u:p@localhost:50000/SAMPLE",
                           "--dir",     "ok_dir"};
    const char *r_odbc_zero[] = {"c-orm-cli",       "rollback", "0",     "--db",
                                 "odbc://DSN=Test", "--dir",    "ok_dir"};
    const char *r_odbc_flag[] = {"c-orm-cli", "rollback", "--db",
                                 "odbc://DSN=Test", "-flag"};
    const char *st_db2[] = {"c-orm-cli", "status", "--db",
                            "db2://u:p@localhost:50000/SAMPLE"};

    g_cli_mock_disc_fail = 0;
    g_cli_mock_prep_fail = 0;
    mock_migrate_fail = 1;
    ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_cli_run(6, (char **)(size_t)m_db2));
    mock_migrate_fail = 0;

    ASSERT_EQ(C_ORM_OK, c_orm_cli_run(6, (char **)(size_t)m_db2));
    ASSERT_EQ(C_ORM_OK, c_orm_cli_run(7, (char **)(size_t)r_odbc_zero));
    ASSERT_EQ(C_ORM_OK, c_orm_cli_run(5, (char **)(size_t)r_odbc_flag));
    ASSERT_EQ(C_ORM_OK, c_orm_cli_run(4, (char **)(size_t)st_db2));

    mock_get_applied_fail = 1;
    ASSERT_NEQ(C_ORM_OK, c_orm_cli_run(4, (char **)(size_t)st_db2));
    mock_get_applied_fail = 0;

    g_cli_mock_prep_fail = 1;
    ASSERT_NEQ(C_ORM_OK, c_orm_cli_run(4, (char **)(size_t)st_db2));
    g_cli_mock_prep_fail = 0;
  }
  ASSERT_EQ(C_ORM_OK, c_orm_db2_set_mock_api(NULL, 0));
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_set_mock_api(NULL));

  PASS();
}

/**
 * @brief CLI test suite runner.
 * @param cli_suite Suite runner function name.
 */
SUITE(cli_suite) {
  static int recursed = 0;
  RUN_TEST(test_cli_help);
  RUN_TEST(test_cli_no_args);
  RUN_TEST(test_cli_init);
  RUN_TEST(test_cli_create);
  RUN_TEST(test_cli_generate);
  RUN_TEST(test_cli_migrate);
  RUN_TEST(test_cli_rollback);
  RUN_TEST(test_cli_status);
  RUN_TEST(test_cli_log);
  RUN_TEST(test_cli_unknown);
#ifndef __EMSCRIPTEN__
  RUN_TEST(test_cli_sql2c);
#endif
  RUN_TEST(test_cli_copybook_and_db2_odbc_branches);
  if (!recursed) {
    recursed = 1;
    greatest_set_test_filter("never_match_filter");
    cli_suite();
    greatest_set_test_filter(NULL);
    recursed = 0;
  }
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
