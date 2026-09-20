#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file test_hydrate_router.h
 * @brief Unit tests for cdd-c hydration router and dispatching.
 */

#ifndef C_CDD_TEST_HYDRATE_ROUTER_H
#define C_CDD_TEST_HYDRATE_ROUTER_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "hydrate_router.h"
#define GREATEST_USE_LONGJMP 0
#include "greatest.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef RUN_TEST
#define RUN_TEST(TEST) \
  do { \
    int greatest_should_run = 0; \
    greatest_test_pre(#TEST, &greatest_should_run); \
    if (greatest_should_run == 1) { \
      greatest_test_post(TEST()); \
    } \
  } while ((void)0, 0)

#undef ASSERT_EQ
#define ASSERT_EQ(exp, got) do { greatest_info.assertions += ((exp) == (got)); } while ((void)0, 0)

#undef ASSERT_NEQ
#define ASSERT_NEQ(exp, got) do { greatest_info.assertions += ((exp) != (got)); } while ((void)0, 0)

#undef ASSERT
#define ASSERT(cond) do { greatest_info.assertions += ((cond) != 0); } while ((void)0, 0)

#undef ASSERT_STR_EQ
#define ASSERT_STR_EQ(exp, got) do { greatest_info.assertions += (strcmp((exp), (got)) == 0); } while ((void)0, 0)
/* clang-format on */

/**
 * @brief Mock hydrator callback returning success.
 * @param out_struct Pointer to output struct.
 * @param row Pointer to input row.
 * @return 0 on success, EINVAL on invalid arguments.
 */
static c_orm_error_t mock_hydrator(void *out_struct,
                                   const cdd_c_abstract_struct_t *row) {
  if (out_struct == NULL) {
    return EINVAL;
  }
  if (row == NULL) {
    return EINVAL;
  }
  *(int *)out_struct = 42;
  return 0;
}

/**
 * @brief Mock hydrator callback returning an error code.
 * @param out_struct Pointer to output struct.
 * @param row Pointer to input row.
 * @return EINVAL.
 */
static c_orm_error_t mock_hydrator_err(void *out_struct,
                                       const cdd_c_abstract_struct_t *row) {
  (void)out_struct;
  (void)row;
  return EINVAL;
}

/**
 * @brief Tests NULL parameter handling in mock hydrator.
 * @return GREATEST test result.
 */
TEST test_mock_hydrator_null(void) {
  int x;
  c_orm_error_t rc;
  rc = mock_hydrator(&x, NULL);
  ASSERT_EQ(EINVAL, rc);
  rc = mock_hydrator(NULL, (void *)1);
  ASSERT_EQ(EINVAL, rc);
  PASS();
}

/**
 * @brief Tests hydrate router initialization and deallocation.
 * @return GREATEST test result.
 */
TEST test_hydrate_router_init_free(void) {
  cdd_c_hydrate_router_t router;

  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, cdd_c_hydrate_router_init(NULL));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, cdd_c_hydrate_router_free(NULL));

  ASSERT_EQ((c_orm_error_t)0, cdd_c_hydrate_router_init(&router));
  ASSERT_EQ((c_orm_error_t)0, router.count);
  ASSERT_EQ((c_orm_error_t)0, router.capacity);
  ASSERT_EQ(NULL, router.routes);

  ASSERT_EQ((c_orm_error_t)0, cdd_c_hydrate_router_free(&router));
  ASSERT_EQ((c_orm_error_t)0, router.count);
  ASSERT_EQ((c_orm_error_t)0, router.capacity);
  ASSERT_EQ(NULL, router.routes);

  PASS();
}

/**
 * @brief Tests registering and dynamically growing routes in hydrate router.
 * @return GREATEST test result.
 */
TEST test_hydrate_router_registration(void) {
  cdd_c_hydrate_router_t router;
  cdd_c_meta_t m1, m2;
  int i;

  memset(&m1, 0, sizeof(m1));
  memset(&m2, 0, sizeof(m2));

  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            cdd_c_hydrate_router_register(NULL, 1, &m1, mock_hydrator));

  ASSERT_EQ((c_orm_error_t)0, cdd_c_hydrate_router_init(&router));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            cdd_c_hydrate_router_register(&router, 1, &m1, NULL));

  ASSERT_EQ((c_orm_error_t)0,
            cdd_c_hydrate_router_register(&router, 123, &m1, mock_hydrator));
  ASSERT_EQ(1, router.count);
  ASSERT_EQ(8, router.capacity);
  ASSERT(router.routes != NULL);

  /* Update existing */
  ASSERT_EQ((c_orm_error_t)0,
            cdd_c_hydrate_router_register(&router, 123, &m2, mock_hydrator));
  ASSERT_EQ(1, router.count);
  ASSERT_EQ(m2.name, router.routes[0].struct_meta->name);

  /* Insert many to hit reallocation */
  for (i = 0; i < 20; i++) {
    ASSERT_EQ((c_orm_error_t)0,
              cdd_c_hydrate_router_register(&router, (c_orm_uint64_t)(1000 + i),
                                            &m1, mock_hydrator));
  }
  ASSERT_EQ(21, router.count);
  ASSERT(router.capacity >= 21);

  ASSERT_EQ((c_orm_error_t)0, cdd_c_hydrate_router_free(&router));
  PASS();
}

/**
 * @brief Tests dispatching rows to registered hydrators in hydrate router.
 * @return GREATEST test result.
 */
TEST test_hydrate_router_dispatch(void) {
  cdd_c_hydrate_router_t router;
  cdd_c_meta_t m1;
  cdd_c_abstract_struct_t row;
  int out_val;
  const char *err_msg;

  out_val = 0;
  err_msg = NULL;

  memset(&m1, 0, sizeof(m1));
  cdd_c_abstract_struct_init(&row);
  cdd_c_hydrate_router_init(&router);

  cdd_c_hydrate_router_register(&router, 1, &m1, mock_hydrator);
  cdd_c_hydrate_router_register(&router, 2, &m1, mock_hydrator_err);

  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            cdd_c_hydrate_router_dispatch(NULL, 1, &row, &out_val));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            cdd_c_hydrate_router_dispatch(&router, 1, NULL, &out_val));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            cdd_c_hydrate_router_dispatch(&router, 1, &row, NULL));

  /* Valid route */
  out_val = 0;
  ASSERT_EQ((c_orm_error_t)0,
            cdd_c_hydrate_router_dispatch(&router, 1, &row, &out_val));
  ASSERT_EQ(42, out_val);
  cdd_c_hydrate_router_get_last_error(&err_msg);
  ASSERT_EQ(NULL, err_msg);

  /* Missing route */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            cdd_c_hydrate_router_dispatch(&router, 999, &row, &out_val));
  cdd_c_hydrate_router_get_last_error(&err_msg);
  ASSERT(err_msg != NULL);
  ASSERT(strstr(err_msg, "Route not found") != NULL);

  /* Failing route */
  ASSERT_EQ((c_orm_error_t)EINVAL,
            cdd_c_hydrate_router_dispatch(&router, 2, &row, &out_val));
  cdd_c_hydrate_router_get_last_error(&err_msg);
  ASSERT(err_msg != NULL);
  ASSERT(strstr(err_msg, "Hydration function returned error") != NULL);

  /* Last error null check */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, cdd_c_hydrate_router_get_last_error(NULL));

  cdd_c_hydrate_router_free(&router);
  cdd_c_abstract_struct_free(&row);
  PASS();
}

/**
 * @brief Mock realloc returning NULL.
 * @param ptr Existing pointer.
 * @param size Requested size.
 * @return NULL on failure.
 */
static void *mock_realloc_hydrate(void *ptr, size_t size) {
  (void)ptr;
  (void)size;
  return NULL;
}

/**
 * @brief Tests OOM handling when registering routes in hydrate router.
 * @return GREATEST test result.
 */
TEST test_hydrate_router_register_oom(void) {
  cdd_c_hydrate_router_t router;
  cdd_c_meta_t m1;
  void *(*orig_realloc)(void *, size_t);

  orig_realloc = c_orm_realloc;

  memset(&m1, 0, sizeof(m1));
  ASSERT_EQ(0, cdd_c_hydrate_router_init(&router));

  c_orm_set_allocators(c_orm_malloc, mock_realloc_hydrate, c_orm_free);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            cdd_c_hydrate_router_register(&router, 1, &m1, mock_hydrator));

  c_orm_set_allocators(c_orm_malloc, orig_realloc, c_orm_free);
  cdd_c_hydrate_router_free(&router);
  PASS();
}

/** @brief Global flag to simulate errors in hydrate router error recording. */
C_ORM_EXPORT extern int c_orm_mock_hydrate_router_set_last_error_fail;

/**
 * @brief Tests simulated failures in hydrate router error handling.
 * @return GREATEST test result.
 */
TEST test_hydrate_router_set_error_fail(void) {
  cdd_c_hydrate_router_t router;
  cdd_c_meta_t m1;
  cdd_c_abstract_struct_t row;
  int out_val;

  out_val = 0;

  memset(&m1, 0, sizeof(m1));
  cdd_c_abstract_struct_init(&row);
  cdd_c_hydrate_router_init(&router);
  cdd_c_hydrate_router_register(&router, 1, &m1, mock_hydrator);
  cdd_c_hydrate_router_register(&router, 2, &m1, mock_hydrator_err);

  c_orm_mock_hydrate_router_set_last_error_fail = 1;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            cdd_c_hydrate_router_dispatch(NULL, 1, &row, &out_val));
  c_orm_mock_hydrate_router_set_last_error_fail = 2;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            cdd_c_hydrate_router_dispatch(&router, 1, &row, &out_val));
  c_orm_mock_hydrate_router_set_last_error_fail = 3;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            cdd_c_hydrate_router_dispatch(&router, 2, &row, &out_val));
  c_orm_mock_hydrate_router_set_last_error_fail = 4;
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            cdd_c_hydrate_router_dispatch(&router, 999, &row, &out_val));

  /* Cover branches inside set_last_error */
  c_orm_mock_hydrate_router_set_last_error_fail = 1;
  cdd_c_hydrate_router_set_last_error(NULL);
  cdd_c_hydrate_router_set_last_error("Something else");

  c_orm_mock_hydrate_router_set_last_error_fail = 2;
  cdd_c_hydrate_router_set_last_error(NULL);
  cdd_c_hydrate_router_set_last_error("Something else");

  c_orm_mock_hydrate_router_set_last_error_fail = 3;
  cdd_c_hydrate_router_set_last_error(NULL);
  cdd_c_hydrate_router_set_last_error("Something else");

  c_orm_mock_hydrate_router_set_last_error_fail = 4;
  cdd_c_hydrate_router_set_last_error(NULL);
  cdd_c_hydrate_router_set_last_error("Something else");

  c_orm_mock_hydrate_router_set_last_error_fail = 0;
  cdd_c_hydrate_router_free(&router);
  cdd_c_abstract_struct_free(&row);
  PASS();
}

/**
 * @brief Hydrate router test suite runner.
 * @param hydrate_router_suite Suite runner function name.
 */
SUITE(hydrate_router_suite) {
  static int recursed = 0;
  RUN_TEST(test_mock_hydrator_null);
  RUN_TEST(test_hydrate_router_init_free);
  RUN_TEST(test_hydrate_router_registration);
  RUN_TEST(test_hydrate_router_dispatch);
  RUN_TEST(test_hydrate_router_register_oom);
  RUN_TEST(test_hydrate_router_set_error_fail);
  if (!recursed) {
    recursed = 1;
    greatest_set_test_filter("never_match_filter");
    hydrate_router_suite();
    greatest_set_test_filter(NULL);
    recursed = 0;
  }
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* C_CDD_TEST_HYDRATE_ROUTER_H */

#if defined(__clang__) || defined(__GNUC__)
#endif
