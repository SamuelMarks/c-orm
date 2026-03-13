/* clang-format off */
#ifndef __STDC_WANT_LIB_EXT1__
#define __STDC_WANT_LIB_EXT1__ 1
#endif
#include <stdio.h>

#include <stdlib.h>

#include <string.h>

#include "greatest.h"

#include "test_db_codegen.h"
/* clang-format on */

extern SUITE(c_orm_suite);

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
  GREATEST_MAIN_BEGIN();

  RUN_SUITE(db_codegen_suite);
  RUN_SUITE(c_orm_suite);

  GREATEST_MAIN_END();
  return 0;
}
