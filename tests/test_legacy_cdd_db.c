/**
 * @file test_legacy_cdd_db.c
 * @defgroup test_legacy_cdd_db Legacy CDD DB Test Runner
 * @brief Test runner for legacy cdd db test suites.
 */

/* clang-format off */
#define GREATEST_USE_TIME 0
#include <greatest.h>
#include "c_orm_meta.h"
#include "legacy_cdd_db/test_abstract_struct.h"
#include "legacy_cdd_db/test_c_to_sql.h"
#include "legacy_cdd_db/test_cdd_c_ir.h"
#include "legacy_cdd_db/test_hydrate_router.h"
#include "legacy_cdd_db/test_migration.h"
#include "legacy_cdd_db/test_sql.h"
#include "legacy_cdd_db/test_sql_to_c.h"
#include "test_greatest_coverage.h"
/* clang-format on */

/**
 * @brief Main entry point for legacy cdd db tests.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return 0 on test success.
 */
int main(int argc, char **argv) {
  if (test_greatest_internals_coverage() != C_ORM_OK) {
    return 1;
  }
  GREATEST_MAIN_BEGIN();
  RUN_SUITE(abstract_struct_suite);
  RUN_SUITE(test_c_to_sql_suite);
  RUN_SUITE(cdd_c_ir_suite);
  RUN_SUITE(hydrate_router_suite);
  RUN_SUITE(migration_suite);
  RUN_SUITE(sql_suite);
  RUN_SUITE(sql_to_c_suite);
  GREATEST_MAIN_END();
}
