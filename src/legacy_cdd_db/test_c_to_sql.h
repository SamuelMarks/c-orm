/**
 * @file test_c_to_sql.h
 * @brief Forwarding header to canonical `tests/test_c_to_sql.h`.
 */

#ifndef LEGACY_CDD_DB_TEST_C_TO_SQL_H
#define LEGACY_CDD_DB_TEST_C_TO_SQL_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef TEST_C_TO_SQL_TMP_FILE
/** @brief Distinct temporary filename for legacy C-to-SQL test runner. */
#define TEST_C_TO_SQL_TMP_FILE "test_c_to_sql_legacy.txt"
#endif

/* clang-format off */
#include "../../tests/test_c_to_sql.h"
/* clang-format on */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* LEGACY_CDD_DB_TEST_C_TO_SQL_H */
