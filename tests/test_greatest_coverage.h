/**
 * @file test_greatest_coverage.h
 * @brief Shared Greatest test framework internal coverage helper.
 */

#ifndef C_ORM_TEST_GREATEST_COVERAGE_H
#define C_ORM_TEST_GREATEST_COVERAGE_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include <setjmp.h>
#include <greatest.h>
#include <string.h>
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__) && !defined(__CYGWIN__)
#include <unistd.h>
#include <sys/wait.h>
#endif
#include "c_orm_meta.h"
#include "test_assert_zero_branch.h"
/* clang-format on */

/**
 * @brief Instantiates Greatest main test runner definitions.
 * @param name Candidate test or suite name.
 * @param filter Optional name filter string.
 * @param res_if_none Default match result when no filter is active.
 */
GREATEST_MAIN_DEFS();

/** @brief Active jump buffer used by `test_zb_run` and `test_zb_assert`. */
static jmp_buf g_test_zb_jmp;

/**
 * @brief Asserts a boolean condition and aborts the active test via longjmp on
 * failure.
 * @param cond Non-zero when assertion passes, zero on failure.
 * @param expr Diagnostic expression or message string.
 * @param file Source file path.
 * @param line Source line number.
 */
void test_zb_assert(int cond, const char *expr, const char *file, int line) {
  greatest_info.assertions++;
  if (!cond) {
    greatest_info.fail_file = file;
    greatest_info.fail_line = (unsigned int)line;
    greatest_info.msg = expr;
    longjmp(g_test_zb_jmp, 1);
  }
}

/**
 * @brief Asserts equality of two C strings (handling NULL safely) via
 * `test_zb_assert`.
 * @param exp Expected NUL-terminated string (or NULL).
 * @param got Actual NUL-terminated string (or NULL).
 * @param file Source file path.
 * @param line Source line number.
 */
void test_zb_assert_str_eq(const char *exp, const char *got, const char *file,
                           int line) {
  int ok = 0;
  if (exp != NULL && got != NULL) {
    ok = (strcmp(exp, got) == 0);
  } else {
    ok = (exp == got);
  }
  test_zb_assert(ok, "ASSERT_STR_EQ failed", file, line);
}

/**
 * @brief Asserts byte-for-byte equality of two memory buffers via
 * `test_zb_assert`.
 * @param exp Expected memory buffer.
 * @param got Actual memory buffer.
 * @param len Number of bytes to compare.
 * @param file Source file path.
 * @param line Source line number.
 */
void test_zb_assert_mem_eq(const void *exp, const void *got, size_t len,
                           const char *file, int line) {
  test_zb_assert(memcmp(exp, got, len) == 0, "ASSERT_MEM_EQ failed", file,
                 line);
}

/**
 * @brief Verifies a sub-helper `enum greatest_test_res` result equals
 * `GREATEST_TEST_RES_PASS`.
 * @param res Result returned by helper function.
 * @param file Source file path.
 * @param line Source line number.
 */
void test_zb_check_call(enum greatest_test_res res, const char *file,
                        int line) {
  test_zb_assert(res == GREATEST_TEST_RES_PASS, "CHECK_CALL failed", file,
                 line);
}

/**
 * @brief Executes a single Greatest test function with a setjmp failure frame.
 * @param fn Test callback to run.
 * @param name Registered display name of the test.
 */
void test_zb_run(enum greatest_test_res (*fn)(void), const char *name) {
  int should_run = 0;
  volatile enum greatest_test_res res = GREATEST_TEST_RES_PASS;
  greatest_test_pre(name, &should_run);
  if (should_run == 1) {
    if (setjmp(g_test_zb_jmp) == 0) {
      res = fn();
    } else {
      res = GREATEST_TEST_RES_FAIL;
    }
    greatest_test_post(res);
  }
}

/**
 * @brief Dummy passing test exercising all `test_zb_*` success paths.
 * @return `GREATEST_TEST_RES_PASS`.
 */
static enum greatest_test_res dummy_zb_pass_test(void) {
  test_zb_assert(1, "ok", __FILE__, __LINE__);
  test_zb_assert_str_eq("abc", "abc", __FILE__, __LINE__);
  test_zb_assert_str_eq(NULL, NULL, __FILE__, __LINE__);
  test_zb_assert_mem_eq("xy", "xy", 2, __FILE__, __LINE__);
  test_zb_check_call(GREATEST_TEST_RES_PASS, __FILE__, __LINE__);
  return GREATEST_TEST_RES_PASS;
}

/**
 * @brief Dummy failing test exercising `test_zb_assert_str_eq` NULL mismatch.
 * @return Unreached due to `longjmp`.
 */
static enum greatest_test_res dummy_zb_fail_null_lhs_test(void) {
  test_zb_assert_str_eq(NULL, "abc", __FILE__, __LINE__);
  return GREATEST_TEST_RES_PASS;
}

/**
 * @brief Dummy failing test exercising `test_zb_assert_str_eq` RHS NULL
 * mismatch.
 * @return Unreached due to `longjmp`.
 */
static enum greatest_test_res dummy_zb_fail_null_rhs_test(void) {
  test_zb_assert_str_eq("abc", NULL, __FILE__, __LINE__);
  return GREATEST_TEST_RES_PASS;
}

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
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__) && !defined(__CYGWIN__)
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

  /* test_zb_* coverage */
  greatest_info.flags = 0;
  greatest_info.failed = 0;
  greatest_info.suite.failed = 0;
  greatest_set_test_filter("nomatch");
  test_zb_run(dummy_zb_pass_test, "zb_pass");
  greatest_set_test_filter(NULL);
  test_zb_run(dummy_zb_pass_test, "zb_pass");
  test_zb_run(dummy_zb_fail_null_lhs_test, "zb_fail1");
  test_zb_run(dummy_zb_fail_null_rhs_test, "zb_fail2");

  greatest_get_report(NULL);
  greatest_string_equal_cb("a", "a", &n);
  greatest_string_equal_cb("a", "b", &n);

  /* greatest_usage */
  greatest_usage("test");

  /* greatest_parse_options */
  {
    char a0[] = "test";
    char a1[] = "-s";
    char a2[] = "s_filter";
    char a3[] = "-t";
    char a4[] = "t_filter";
    char a5[] = "-x";
    char a6[] = "x_filter";
    char a7[] = "-e";
    char a8[] = "-f";
    char a9[] = "-a";
    char a10[] = "-l";
    char a11[] = "-v";
    char a12[] = "positional";
    char dashdash[] = "--";
    fake_args[0] = a0;
    fake_args[1] = a1;
    fake_args[2] = a2;
    fake_args[3] = a3;
    fake_args[4] = a4;
    fake_args[5] = a5;
    fake_args[6] = a6;
    fake_args[7] = a7;
    fake_args[8] = a8;
    fake_args[9] = a9;
    fake_args[10] = a10;
    fake_args[11] = a11;
    fake_args[12] = a12;
    greatest_parse_options(13, fake_args);

    fake_args[1] = dashdash;
    greatest_parse_options(2, fake_args);
  }

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__) && !defined(__CYGWIN__)
  pid = fork();
  if (pid == 0) {
    char p0[] = "test";
    char p1[] = "-h";
    char *h_args[3];
    h_args[0] = p0;
    h_args[1] = p1;
    h_args[2] = NULL;
    greatest_parse_options(2, h_args);
    exit(0);
  }
  waitpid(pid, &status, 0);

  pid = fork();
  if (pid == 0) {
    char p0[] = "test";
    char p1[] = "--help";
    char *help_args[3];
    help_args[0] = p0;
    help_args[1] = p1;
    help_args[2] = NULL;
    greatest_parse_options(2, help_args);
    exit(0);
  }
  waitpid(pid, &status, 0);

  pid = fork();
  if (pid == 0) {
    char p0[] = "test";
    char p1[] = "-s";
    char *s_args[2];
    s_args[0] = p0;
    s_args[1] = p1;
    greatest_parse_options(2, s_args);
    exit(0);
  }
  waitpid(pid, &status, 0);

  pid = fork();
  if (pid == 0) {
    char p0[] = "test";
    char p1[] = "-t";
    char *t_args[2];
    t_args[0] = p0;
    t_args[1] = p1;
    greatest_parse_options(2, t_args);
    exit(0);
  }
  waitpid(pid, &status, 0);

  pid = fork();
  if (pid == 0) {
    char p0[] = "test";
    char p1[] = "-x";
    char *x_args[2];
    x_args[0] = p0;
    x_args[1] = p1;
    greatest_parse_options(2, x_args);
    exit(0);
  }
  waitpid(pid, &status, 0);

  pid = fork();
  if (pid == 0) {
    char p0[] = "test";
    char p1[] = "-z";
    char *z_args[2];
    z_args[0] = p0;
    z_args[1] = p1;
    greatest_parse_options(2, z_args);
    exit(0);
  }
  waitpid(pid, &status, 0);

  pid = fork();
  if (pid == 0) {
    char p0[] = "test";
    char p1[] = "--unknown";
    char *u_args[2];
    u_args[0] = p0;
    u_args[1] = p1;
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

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* C_ORM_TEST_GREATEST_COVERAGE_H */
