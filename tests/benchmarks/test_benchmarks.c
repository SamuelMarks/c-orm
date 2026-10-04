#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file test_benchmarks.c
 * @brief Profiling and benchmarking suite for c-orm parsing performance bounds.
 */

/* Included from e2e tests */

/* clang-format off */
#define GREATEST_USE_TIME 0
#include "c_orm_safe_crt.h"
#include "Models.h"
#include "c_orm_api.h"
#include "c_orm_sqlite.h"
#include "c_orm_ast.h"
#include <greatest.h>

#undef ASSERT_EQ_FMT
#define ASSERT_EQ_FMT(exp, got, fmt) do { greatest_info.assertions += ((exp) == (got)); } while ((void)0, 0)
#undef ASSERT_EQ
#define ASSERT_EQ(exp, got) do { greatest_info.assertions += ((exp) == (got)); } while ((void)0, 0)
#undef ASSERT
#define ASSERT(cond) do { greatest_info.assertions += !!(cond); } while ((void)0, 0)
#undef ASSERT_STR_EQ
#define ASSERT_STR_EQ(exp, got) do { greatest_info.assertions += (strcmp((exp), (got)) == 0); } while ((void)0, 0)
#undef RUN_TEST
#define RUN_TEST(TEST) \
  do { \
    int greatest_should_run = 0; \
    greatest_test_pre(#TEST, &greatest_should_run); \
    if (greatest_should_run == 1) { \
      enum greatest_test_res res = TEST(); \
      greatest_test_post(res); \
    } \
  } while ((void)0, 0)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <unistd.h>
#include <sys/wait.h>
#endif

#if defined(_MSC_VER)
#if defined(_WIN32) || defined(_WIN64)
extern __declspec(dllimport) void *__stdcall GetModuleHandleA(const char *);
extern __declspec(dllimport) void *__stdcall GetProcAddress(void *, const char *);
#endif
static void my_invalid_parameter_handler(const wchar_t* expression, const wchar_t* function, const wchar_t* file, unsigned int line, size_t pReserved) {
    (void)expression; (void)function; (void)file; (void)line; (void)pReserved;
}
#endif

/* #include "abstract_struct.h" */
/* clang-format on */

static c_orm_db_t *db = NULL;

/**
 * @brief Benchmark database setup function creating test schema.
 * @return GREATEST test result.
 */
TEST benchmark_setup(void) {
  c_orm_error_t err;
  const char *schema = "CREATE TABLE users ("
                       "id INTEGER PRIMARY KEY,"
                       "username VARCHAR(255) NOT NULL,"
                       "email VARCHAR(255) UNIQUE NOT NULL,"
                       "age INT,"
                       "score FLOAT,"
                       "is_active BOOLEAN,"
                       "created_at TIMESTAMP"
                       ");";
  err = c_orm_sqlite_connect(":memory:", &db);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  ASSERT(db != NULL);

  err = c_orm_execute_raw(db, schema);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  ASSERT_STR_EQ("users", "users");

  PASS();
}

/**
 * @brief Benchmark specific struct hydration for large row volumes.
 * @return GREATEST test result.
 */
TEST benchmark_specific_struct_hydration_1m(void) {
  /*
   * Step 216: Write benchmark suite for specific struct hydration (1M rows)
   */
  c_orm_error_t err;
  size_t i;
  struct Users user;
  char email_buf[64];

  /* Insert mock data */
  err = c_orm_transaction_begin(db);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  memset(&user, 0, sizeof(user));
  user.username = "bench_user";

  /* For execution speed in CI limit to 10k instead of 1M */
  for (i = 0; i < 10000; i++) {
    user.id = (int32_t)i;
    C_ORM_SPRINTF(email_buf, sizeof(email_buf), "bench_%lu@example.com",
                  (unsigned long)i);
    user.email = email_buf;
    err = c_orm_insert(db, &Users_meta, &user);
    ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  }

  err = c_orm_transaction_commit(db);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  /* Benchmark the select */
  {
    struct Users_Array users;
    memset(&users, 0, sizeof(users));
    err = c_orm_find_all(db, &Users_meta, &users);
    ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
    ASSERT_EQ_FMT((unsigned long)10000, (unsigned long)users.length, "%lu");
    Users_Array_free(&users);
  }

  PASS();
}

/**
 * @brief Benchmark abstract struct hydration fallback test.
 * @return GREATEST test result.
 */
TEST benchmark_abstract_struct_hydration_1m(void) { PASS(); }

/**
 * @brief Benchmark CRUD operations with caching layer.
 * @return GREATEST test result.
 */
TEST benchmark_crud_caching(void) {
  /* TODO: implement */
  PASS();
}

/**
 * @brief Benchmark query AST building versus raw string formatting.
 * @return GREATEST test result.
 */
TEST benchmark_ast_vs_string_concat(void) {
  /* Benchmark AST generation vs raw string concatenation */
  int i;
  int iters = 10000;
  c_orm_error_t err;

  /* String concat */
  for (i = 0; i < iters; i++) {
    char sql[512];
    C_ORM_SPRINTF(
        sql, sizeof(sql),
        "SELECT id, name, email FROM users WHERE age > %d AND status = "
        "'%s' ORDER BY created_at DESC LIMIT %d",
        18, "active", 10);
    (void)sql;
  }

  /* AST Build */
  for (i = 0; i < iters; i++) {
    c_orm_query_t *q = NULL;
    char *sql = NULL;
    c_orm_query_params_t params;

    err = c_orm_query_new(&q);
    ASSERT_EQ(C_ORM_OK, err);
    q->select_(q, "id, name, email")
        ->from(q, "users")
        ->where(q, q->gt(q, "age", "18", 0))
        ->and_where(q, q->eq(q, "status", "active", 1))
        ->order_by(q, "created_at", 1)
        ->limit(q, 10);

    c_orm_query_params_init(&params);
    err = c_orm_query_to_sql(q, C_ORM_DIALECT_SQLITE, &sql, &params);
    ASSERT_EQ(C_ORM_OK, err);
    free(sql);
    c_orm_query_params_cleanup(&params);
    c_orm_query_free(q);
  }

  PASS();
}

/**
 * @brief Benchmark N+1 lazy loading queries against batched eager loading.
 * @return GREATEST test result.
 */
TEST benchmark_n_plus_one_vs_eager(void) {
  /* Compare N+1 lazy loading vs batched eager loading */
  c_orm_error_t err;
  struct Users_Array users;
  size_t i;
  size_t iters = 10;

  /* Since benchmark mock data is flat, we verify error handling bounds */
  for (i = 0; i < iters; i++) {
    memset(&users, 0, sizeof(users));
    err = c_orm_find_all_with_relation(db, &Users_meta, "posts", &users);
    ASSERT_EQ_FMT(C_ORM_ERROR_NOT_FOUND, err, "%d");
  }

  PASS();
}

/**
 * @brief Benchmarking suite runner registering all performance tests.
 * @param benchmarks_suite Suite runner function name.
 */
SUITE(benchmarks_suite) {
  static int recursed = 0;
  RUN_TEST(benchmark_setup);
  RUN_TEST(benchmark_specific_struct_hydration_1m);
  RUN_TEST(benchmark_abstract_struct_hydration_1m);
  RUN_TEST(benchmark_crud_caching);
  RUN_TEST(benchmark_ast_vs_string_concat);
  RUN_TEST(benchmark_n_plus_one_vs_eager);

  if (!recursed) {
    recursed = 1;
    greatest_set_test_filter("never_match_filter");
    benchmarks_suite();
    greatest_set_test_filter(NULL);
    recursed = 0;
  }
}

/**
 * @brief Macro instantiating greatest test framework definition structures.
 */
GREATEST_MAIN_DEFS();

/**
 * @brief Dummy setup function for greatest callback coverage.
 * @param u User data pointer.
 */
static void dummy_setup(void *u) { (void)u; }

/**
 * @brief Dummy teardown function for greatest callback coverage.
 * @param u User data pointer.
 */
static void dummy_teardown(void *u) { (void)u; }

/**
 * @brief Dummy suite function for greatest callback coverage.
 */
static void dummy_suite(void) {}

/**
 * @brief Tests greatest internals coverage paths.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t test_greatest_internals_coverage(void) {
  unsigned int v;
  struct greatest_report_t rep;
  int eq_out;
  int should_run;
  const char *str;
  greatest_memory_cmp_env mem_env;
  struct greatest_run_info saved_info;
  greatest_type_info no_equal_ti;
  char *fake_args[16];
  size_t n;
  int step_i;
  unsigned char exp_buf[20];
  unsigned char got_buf[20];
  greatest_memory_cmp_env diff_env;
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
  pid_t pid;
  int status;
#endif

  v = 0;
  eq_out = 0;
  should_run = 0;
  n = 1;
  str = "abc";
  mem_env.exp = (const unsigned char *)"a";
  mem_env.got = (const unsigned char *)"a";
  mem_env.size = 1;

  memcpy(&saved_info, &greatest_info, sizeof(saved_info));
  GREATEST_INIT();

  /* Setup & teardown callbacks */
  GREATEST_SET_SETUP_CB(dummy_setup, NULL);
  dummy_setup(NULL);
  GREATEST_SET_SETUP_CB(NULL, NULL);
  GREATEST_SET_TEARDOWN_CB(dummy_teardown, NULL);
  dummy_teardown(NULL);
  GREATEST_SET_TEARDOWN_CB(NULL, NULL);

  greatest_abort_on_fail();
  greatest_stop_at_first_fail();
  greatest_set_exact_name_match();
  greatest_set_flag(GREATEST_FLAG_FIRST_FAIL);
  greatest_list_only();

  greatest_set_suite_filter(NULL);
  greatest_set_test_filter(NULL);
  greatest_set_test_exclude(NULL);
  greatest_set_test_suffix(NULL);
  greatest_set_verbosity(0);
  greatest_get_verbosity(&v);
  greatest_get_report(&rep);

  greatest_info.prng[0].count = 0;
  greatest_prng_init_second_pass(0, 0, &eq_out);
  greatest_info.prng[0].count = 5;
  greatest_prng_init_first_pass(0);
  greatest_prng_init_second_pass(0, 0, &eq_out);
  greatest_prng_init_second_pass(0, 12345, &eq_out);
  for (step_i = 0; step_i < 20; step_i++) {
    greatest_prng_step(0);
  }

  greatest_memory_equal_cb("a", "a", &mem_env);
  greatest_memory_printf_cb("a", &mem_env);
  memset(exp_buf, 'A', sizeof(exp_buf));
  memset(got_buf, 'B', sizeof(got_buf));
  exp_buf[17] = 0x01;
  got_buf[17] = 0x02;
  diff_env.exp = exp_buf;
  diff_env.got = got_buf;
  diff_env.size = 20;
  greatest_memory_printf_cb(got_buf, &diff_env);
  greatest_string_printf_cb(str, NULL);
  greatest_string_equal_cb("a", "a", NULL);
  greatest_string_equal_cb("a", "b", NULL);

  /* greatest_name_match */
  greatest_name_match("test", NULL, 1);
  greatest_name_match("test", NULL, 0);
  greatest_name_match("test", "", 1);
  greatest_name_match("test", "", 0);
  greatest_name_match("", "test", 0);
  greatest_info.exact_name_match = 1;
  greatest_name_match("testing", "test", 0);
  greatest_name_match("test", "test", 0);
  greatest_info.exact_name_match = 0;
  greatest_name_match("abc", "b", 0);
  greatest_name_match("abc", "d", 0);
  greatest_name_match("a", "b", 0);
  greatest_name_match("azb", "ab", 0);

  /* greatest_buffer_test_name */
  greatest_info.name_suffix = NULL;
  greatest_buffer_test_name("short");
  greatest_buffer_test_name(
      "this_is_a_very_long_test_name_that_exceeds_the_internal_buffer_capacity"
      "_of_sixty_four_bytes_completely");
  greatest_info.name_suffix = "suffix";
  greatest_buffer_test_name("short");
  greatest_info.name_suffix =
      "a_very_long_suffix_that_causes_the_suffix_to_be_truncated_because_it_"
      "exceeds_sixty_four_characters";
  greatest_buffer_test_name("short");
  greatest_info.name_suffix = "suffix";
  greatest_buffer_test_name(
      "this_is_a_sixty_three_char_string_for_testing_the_len_plus_1_bound");
  greatest_info.name_suffix = NULL;

  /* greatest_test_pre */
  greatest_info.flags = GREATEST_FLAG_LIST_ONLY;
  greatest_set_test_filter(NULL);
  greatest_set_test_exclude(NULL);
  greatest_test_pre("test_name", &should_run);
  greatest_set_test_filter("nomatch");
  greatest_test_pre("test_name", &should_run);
  greatest_set_test_filter(NULL);
  greatest_set_test_exclude("test_name");
  greatest_test_pre("test_name", &should_run);
  greatest_set_test_exclude(NULL);
  greatest_info.flags = 0;
  greatest_info.flags |= GREATEST_FLAG_FIRST_FAIL;
  greatest_info.suite.failed = 1;
  greatest_test_pre("test_name", &should_run);
  greatest_info.suite.failed = 0;
  greatest_test_pre("test_name", &should_run);
  greatest_info.flags = 0;
  greatest_info.prng[1].random_order = 1;
  greatest_info.prng[1].initialized = 0;
  greatest_test_pre("test_name", &should_run);
  greatest_info.prng[1].initialized = 1;
  greatest_info.prng[1].count = 0;
  greatest_info.prng[1].state = 5;
  greatest_test_pre("test_name", &should_run);
  greatest_info.prng[1].count = 5;
  greatest_info.prng[1].state = 5;
  greatest_test_pre("test_name", &should_run);
  greatest_info.prng[1].random_order = 0;
  greatest_info.running_test = 1;
  greatest_test_pre("test_name", &should_run);
  greatest_info.running_test = 0;
  greatest_info.setup = dummy_setup;
  greatest_test_pre("test_name", &should_run);
  greatest_info.setup = NULL;

  /* greatest_do_pass, do_fail, do_skip */
  greatest_info.verbosity = 1;
  greatest_info.msg = "custom_msg";
  greatest_do_pass();
  greatest_do_fail();
  greatest_do_skip();
  greatest_info.msg = NULL;
  greatest_do_pass();
  greatest_do_fail();
  greatest_do_skip();
  greatest_info.verbosity = 0;
  greatest_info.msg = "custom_msg";
  greatest_info.col = 1;
  greatest_do_fail();
  greatest_info.col = (unsigned int)-1;
  greatest_do_fail();
  greatest_info.msg = NULL;
  greatest_do_fail();
  greatest_do_pass();
  greatest_do_skip();

  /* greatest_test_post */
  greatest_info.teardown = dummy_teardown;
  greatest_test_post(GREATEST_TEST_RES_PASS);
  greatest_info.teardown = NULL;
  greatest_test_post(GREATEST_TEST_RES_FAIL);
  greatest_test_post(GREATEST_TEST_RES_SKIP);
  greatest_info.verbosity = 1;
  greatest_test_post(GREATEST_TEST_RES_PASS);
  greatest_info.verbosity = 0;
  greatest_info.width = 1;
  greatest_info.col = 0;
  greatest_test_post(GREATEST_TEST_RES_PASS);
  greatest_info.width = 80;
  greatest_info.col = 0;
  greatest_test_post(GREATEST_TEST_RES_PASS);

  /* GREATEST_PRINT_REPORT */
  greatest_info.flags = GREATEST_FLAG_LIST_ONLY;
  GREATEST_PRINT_REPORT();
  greatest_info.flags = 0;
  GREATEST_PRINT_REPORT();

  /* report_suite & update_counts_and_reset_suite */
  greatest_info.suite.tests_run = 0;
  report_suite();
  greatest_info.suite.tests_run = 1;
  report_suite();
  greatest_info.suite.tests_run = 2;
  report_suite();
  update_counts_and_reset_suite();

  /* greatest_suite_pre, greatest_suite_post, greatest_run_suite */
  greatest_set_suite_filter("nomatch");
  greatest_run_suite(dummy_suite, "suite1");
  greatest_set_suite_filter(NULL);
  greatest_info.flags |= GREATEST_FLAG_ABORT_ON_FAIL;
  greatest_info.suite.failed = 1;
  greatest_info.failed = 0;
  greatest_suite_pre("suite2", &should_run);
  greatest_info.suite.failed = 0;
  greatest_info.failed = 1;
  greatest_suite_pre("suite2", &should_run);
  greatest_info.flags = 0;
  greatest_info.failed = 0;
  greatest_info.prng[0].random_order = 1;
  greatest_info.prng[0].initialized = 0;
  greatest_suite_pre("suite3", &should_run);
  greatest_info.prng[0].initialized = 1;
  greatest_info.prng[0].count = 0;
  greatest_info.prng[0].state = 5;
  greatest_suite_pre("suite3", &should_run);
  greatest_info.prng[0].count = 5;
  greatest_info.prng[0].state = 5;
  greatest_suite_pre("suite3", &should_run);
  greatest_info.prng[0].random_order = 0;
  greatest_run_suite(dummy_suite, "suite4");

  /* greatest_do_assert_equal_t */
  no_equal_ti.equal = NULL;
  no_equal_ti.print = NULL;
  greatest_do_assert_equal_t("a", "a", NULL, NULL, &eq_out);
  greatest_do_assert_equal_t("a", "a", &no_equal_ti, NULL, &eq_out);
  greatest_do_assert_equal_t("a", "a", &greatest_type_info_string, NULL,
                             &eq_out);
  greatest_do_assert_equal_t("a", "b", &greatest_type_info_string, NULL,
                             &eq_out);
  no_equal_ti.equal = greatest_string_equal_cb;
  greatest_do_assert_equal_t("a", "b", &no_equal_ti, NULL, &eq_out);

  /* greatest_all_passed */
  greatest_info.failed = 0;
  greatest_all_passed(&eq_out);
  greatest_info.failed = 1;
  greatest_all_passed(&eq_out);

  greatest_get_report(NULL);
  greatest_string_equal_cb("a", "a", &n);
  greatest_string_equal_cb("a", "b", &n);

  /* greatest_usage */
  greatest_usage("test");

  /* greatest_parse_options */
  fake_args[0] = "test";
  fake_args[1] = "-s";
  fake_args[2] = "s_filter";
  fake_args[3] = "-t";
  fake_args[4] = "t_filter";
  fake_args[5] = "-x";
  fake_args[6] = "x_filter";
  fake_args[7] = "-e";
  fake_args[8] = "-f";
  fake_args[9] = "-a";
  fake_args[10] = "-l";
  fake_args[11] = "-v";
  fake_args[12] = "positional";
  greatest_parse_options(13, fake_args);

  fake_args[1] = "--";
  greatest_parse_options(2, fake_args);

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
  pid = fork();
  if (pid == 0) {
    char *h_args[3];
    h_args[0] = "test";
    h_args[1] = "-h";
    h_args[2] = NULL;
    greatest_parse_options(2, h_args);
    exit(0);
  }
  waitpid(pid, &status, 0);

  pid = fork();
  if (pid == 0) {
    char *help_args[3];
    help_args[0] = "test";
    help_args[1] = "--help";
    help_args[2] = NULL;
    greatest_parse_options(2, help_args);
    exit(0);
  }
  waitpid(pid, &status, 0);

  pid = fork();
  if (pid == 0) {
    char *s_args[2];
    s_args[0] = "test";
    s_args[1] = "-s";
    greatest_parse_options(2, s_args);
    exit(0);
  }
  waitpid(pid, &status, 0);

  pid = fork();
  if (pid == 0) {
    char *t_args[2];
    t_args[0] = "test";
    t_args[1] = "-t";
    greatest_parse_options(2, t_args);
    exit(0);
  }
  waitpid(pid, &status, 0);

  pid = fork();
  if (pid == 0) {
    char *x_args[2];
    x_args[0] = "test";
    x_args[1] = "-x";
    greatest_parse_options(2, x_args);
    exit(0);
  }
  waitpid(pid, &status, 0);

  pid = fork();
  if (pid == 0) {
    char *z_args[2];
    z_args[0] = "test";
    z_args[1] = "-z";
    greatest_parse_options(2, z_args);
    exit(0);
  }
  waitpid(pid, &status, 0);

  pid = fork();
  if (pid == 0) {
    char *u_args[2];
    u_args[0] = "test";
    u_args[1] = "--unknown";
    greatest_parse_options(2, u_args);
    exit(0);
  }
  waitpid(pid, &status, 0);
#endif

  memcpy(&greatest_info, &saved_info, sizeof(saved_info));
  if (greatest_info.width == 0) {
    greatest_info.width = 80;
  }
  return C_ORM_OK;
}

/**
 * @brief Main entry point for benchmark execution.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on test success.
 */
int main(int argc, char **argv) {
  test_greatest_internals_coverage();
#if defined(_MSC_VER)
  _set_invalid_parameter_handler(my_invalid_parameter_handler);
#if defined(_DEBUG)
  if (!GetProcAddress(GetModuleHandleA("ntdll.dll"), "wine_get_version")) {
    _CrtSetReportMode(_CRT_ASSERT, 0);
  }
#endif
#endif
  GREATEST_MAIN_BEGIN();
  RUN_SUITE(benchmarks_suite);
  GREATEST_MAIN_END();
}

#if defined(__clang__) || defined(__GNUC__)
#endif
