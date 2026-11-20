/**
 * @file test_standalone_fixtures.c
 * @defgroup test_standalone_fixtures Standalone Fixtures Test Runner
 * @brief Test runner for standalone database fixtures.
 */

/* clang-format off */
#define GREATEST_USE_TIME 0
#include <greatest.h>
#include "c_orm_meta.h"
#include "test_c_to_sql.h"
#include "test_sql.h"
#include "test_sql_to_c.h"
#include "test_greatest_coverage.h"
/* clang-format on */

/**
 * @brief Main entry point for standalone database fixture tests.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on test success.
 */
int main(int argc, char **argv) {
  if (test_greatest_internals_coverage() != C_ORM_OK) {
    return 1;
  }
  GREATEST_MAIN_BEGIN();
  RUN_SUITE(test_c_to_sql_suite);
  RUN_SUITE(sql_suite);
  RUN_SUITE(sql_to_c_suite);
  GREATEST_MAIN_END();
}
