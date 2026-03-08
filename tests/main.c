#include <stdio.h>
#include <stdlib.h>
#include "greatest.h"

#include "test_db_codegen.h"

GREATEST_MAIN_DEFS();

int main(int argc, char **argv) {
    GREATEST_MAIN_BEGIN();

    RUN_SUITE(db_codegen_suite);

    GREATEST_MAIN_END();
}
