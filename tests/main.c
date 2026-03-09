/* clang-format off */
#include <stdio.h>
/* clang-format on */

/* clang-format off */
#include <stdlib.h>
/* clang-format on */

/* clang-format off */
#include "greatest.h"
/* clang-format on */

/* clang-format off */
#include "test_db_codegen.h"
/* clang-format on */

extern SUITE(c_orm_suite);

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
  GREATEST_MAIN_BEGIN();

  RUN_SUITE(db_codegen_suite);
  RUN_SUITE(c_orm_suite);

  GREATEST_MAIN_END();
}
