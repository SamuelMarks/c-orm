/* clang-format off */
#include <greatest.h>
#include <string.h>
#include "c_orm_meta.h"
#include "test_c_to_sql.h"
#include "test_sql.h"
#include "test_sql_to_c.h"
/* clang-format on */

GREATEST_MAIN_DEFS();

static void dummy_setup(void *u) { (void)u; }
static void dummy_teardown(void *u) { (void)u; }

static c_orm_error_t test_greatest_internals_coverage(void) {
  unsigned int v;
  struct greatest_report_t rep;
  int eq_out;
  const char *str;
  greatest_memory_cmp_env mem_env;
  struct greatest_run_info saved_info;
  char *fake_args[16];

  v = 0;
  eq_out = 0;
  str = "abc";
  mem_env.exp = (const unsigned char *)"a";
  mem_env.got = (const unsigned char *)"a";
  mem_env.size = 1;

  memcpy(&saved_info, &greatest_info, sizeof(saved_info));

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

  greatest_info.prng[0].count = 5;
  greatest_prng_init_first_pass(0);
  greatest_prng_init_second_pass(0, 12345, &eq_out);
  greatest_prng_step(0);

  greatest_memory_equal_cb("a", "a", &mem_env);
  greatest_memory_printf_cb("a", &mem_env);
  greatest_string_printf_cb(str, NULL);
  greatest_usage("test");

  greatest_info.flags = 0;
  greatest_do_fail();
  greatest_do_skip();

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
  greatest_parse_options(12, fake_args);

  memcpy(&greatest_info, &saved_info, sizeof(saved_info));
  return C_ORM_OK;
}

int main(int argc, char **argv) {
  test_greatest_internals_coverage();
  GREATEST_MAIN_BEGIN();
  RUN_SUITE(test_c_to_sql_suite);
  RUN_SUITE(sql_suite);
  RUN_SUITE(sql_to_c_suite);
  GREATEST_MAIN_END();
}
