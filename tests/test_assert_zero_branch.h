/**
 * @file test_assert_zero_branch.h
 * @brief Zero-call-site-branch Greatest assertion wrappers backed by setjmp.
 */

#ifndef C_ORM_TEST_ASSERT_ZERO_BRANCH_H
#define C_ORM_TEST_ASSERT_ZERO_BRANCH_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include <stddef.h>
#include <greatest.h>
/* clang-format on */

/**
 * @brief Executes a single Greatest test function with a setjmp failure frame.
 * @param fn Test callback to run.
 * @param name Registered display name of the test.
 */
void test_zb_run(enum greatest_test_res (*fn)(void), const char *name);

/**
 * @brief Asserts a boolean condition and aborts the active test via longjmp on
 * failure.
 * @param cond Non-zero when assertion passes, zero on failure.
 * @param expr Diagnostic expression or message string.
 * @param file Source file path.
 * @param line Source line number.
 */
void test_zb_assert(int cond, const char *expr, const char *file, int line);

/**
 * @brief Asserts equality of two C strings (handling NULL safely) via
 * `test_zb_assert`.
 * @param exp Expected NUL-terminated string (or NULL).
 * @param got Actual NUL-terminated string (or NULL).
 * @param file Source file path.
 * @param line Source line number.
 */
void test_zb_assert_str_eq(const char *exp, const char *got, const char *file,
                           int line);

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
                           const char *file, int line);

/**
 * @brief Verifies a sub-helper `enum greatest_test_res` result equals
 * `GREATEST_TEST_RES_PASS`.
 * @param res Result returned by helper function.
 * @param file Source file path.
 * @param line Source line number.
 */
void test_zb_check_call(enum greatest_test_res res, const char *file, int line);

#undef RUN_TEST
/** @brief Runs `TEST` through `test_zb_run` without call-site branches. */
#define RUN_TEST(TEST) test_zb_run((TEST), #TEST)

#undef ASSERT
/** @brief Asserts `COND` is non-zero without call-site branches. */
#define ASSERT(COND) test_zb_assert((COND) != 0, #COND, __FILE__, __LINE__)

#undef ASSERTm
/** @brief Asserts `COND` is non-zero with custom `MSG`. */
#define ASSERTm(MSG, COND)                                                     \
  test_zb_assert((COND) != 0, (MSG), __FILE__, __LINE__)

#undef ASSERT_FALSE
/** @brief Asserts `COND` is zero without call-site branches. */
#define ASSERT_FALSE(COND)                                                     \
  test_zb_assert((COND) == 0, #COND, __FILE__, __LINE__)

#undef ASSERT_FALSEm
/** @brief Asserts `COND` is zero with custom `MSG`. */
#define ASSERT_FALSEm(MSG, COND)                                               \
  test_zb_assert((COND) == 0, (MSG), __FILE__, __LINE__)

#undef ASSERT_EQ
/** @brief Asserts `EXP == GOT` without call-site branches. */
#define ASSERT_EQ(EXP, GOT)                                                    \
  test_zb_assert((EXP) == (GOT), #EXP " != " #GOT, __FILE__, __LINE__)

#undef ASSERT_EQm
/** @brief Asserts `EXP == GOT` with custom `MSG`. */
#define ASSERT_EQm(MSG, EXP, GOT)                                              \
  test_zb_assert((EXP) == (GOT), (MSG), __FILE__, __LINE__)

#undef ASSERT_EQ_FMT
/** @brief Asserts `EXP == GOT` (format-compatible alias) without branches. */
#define ASSERT_EQ_FMT(EXP, GOT, FMT)                                           \
  test_zb_assert((EXP) == (GOT), #EXP " != " #GOT, __FILE__, __LINE__)

#undef ASSERT_EQ_FMTm
/** @brief Asserts `EXP == GOT` with custom `MSG` and format parameter. */
#define ASSERT_EQ_FMTm(MSG, EXP, GOT, FMT)                                     \
  test_zb_assert((EXP) == (GOT), (MSG), __FILE__, __LINE__)

#undef ASSERT_NEQ
/** @brief Asserts `EXP != GOT` without call-site branches. */
#define ASSERT_NEQ(EXP, GOT)                                                   \
  test_zb_assert((EXP) != (GOT), #EXP " == " #GOT, __FILE__, __LINE__)

#undef ASSERT_NEQm
/** @brief Asserts `EXP != GOT` with custom `MSG`. */
#define ASSERT_NEQm(MSG, EXP, GOT)                                             \
  test_zb_assert((EXP) != (GOT), (MSG), __FILE__, __LINE__)

#undef ASSERT_STR_EQ
/** @brief Asserts two C strings are equal without call-site branches. */
#define ASSERT_STR_EQ(EXP, GOT)                                                \
  test_zb_assert_str_eq((EXP), (GOT), __FILE__, __LINE__)

#undef ASSERT_STR_EQm
/** @brief Asserts two C strings are equal with custom `MSG`. */
#define ASSERT_STR_EQm(MSG, EXP, GOT)                                          \
  test_zb_assert_str_eq((EXP), (GOT), __FILE__, __LINE__)

#undef ASSERT_MEM_EQ
/** @brief Asserts two memory buffers are equal without call-site branches. */
#define ASSERT_MEM_EQ(EXP, GOT, LEN)                                           \
  test_zb_assert_mem_eq((EXP), (GOT), (LEN), __FILE__, __LINE__)

#undef CHECK_CALL
/** @brief Checks a `greatest_test_res` helper call without branches. */
#define CHECK_CALL(RES) test_zb_check_call((RES), __FILE__, __LINE__)

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* C_ORM_TEST_ASSERT_ZERO_BRANCH_H */
