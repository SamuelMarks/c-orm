#if defined(__clang__) || defined(__GNUC__)
#endif
/* clang-format off */
#include "hydrate_router.h"
#include <greatest.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
/* clang-format on */

static c_orm_error_t mock_hydrator(void *out_struct,
                                   const cdd_c_abstract_struct_t *row) {
  if (!out_struct || !row)
    return EINVAL;
  *(int *)out_struct = 42;
  return 0;
}

static c_orm_error_t mock_hydrator_err(void *out_struct,
                                       const cdd_c_abstract_struct_t *row) {
  (void)out_struct;
  (void)row;
  return EINVAL;
}

TEST test_mock_hydrator_null(void) {
  int x;
  c_orm_error_t rc;
  rc = mock_hydrator(&x, NULL);
  ASSERT_EQ(EINVAL, rc);
  rc = mock_hydrator(NULL, (void *)1);
  ASSERT_EQ(EINVAL, rc);
  PASS();
}
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

TEST test_hydrate_router_registration(void) {
  cdd_c_hydrate_router_t router;
  cdd_c_meta_t m1, m2;
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
  {
    int i;
    for (i = 0; i < 20; i++) {
      ASSERT_EQ((c_orm_error_t)0, cdd_c_hydrate_router_register(
                                      &router, 1000 + i, &m1, mock_hydrator));
    }
    ASSERT_EQ(21, router.count);
    ASSERT(router.capacity >= 21);
  }

  ASSERT_EQ((c_orm_error_t)0, cdd_c_hydrate_router_free(&router));
  PASS();
}

TEST test_hydrate_router_dispatch(void) {
  cdd_c_hydrate_router_t router;
  cdd_c_meta_t m1;
  cdd_c_abstract_struct_t row;
  int out_val = 0;
  const char *err_msg = NULL;

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

static void *mock_realloc_hydrate(void *ptr, size_t size) {
  (void)ptr;
  (void)size;
  return NULL;
}

TEST test_hydrate_router_register_oom(void) {
  cdd_c_hydrate_router_t router;
  cdd_c_meta_t m1;
  void *(*old_realloc)(void *, size_t) = c_orm_realloc;

  memset(&m1, 0, sizeof(m1));
  ASSERT_EQ(0, cdd_c_hydrate_router_init(&router));

  c_orm_set_allocators(c_orm_malloc, mock_realloc_hydrate, c_orm_free);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            cdd_c_hydrate_router_register(&router, 1, &m1, mock_hydrator));

  c_orm_set_allocators(c_orm_malloc, old_realloc, c_orm_free);
  cdd_c_hydrate_router_free(&router);
  PASS();
}
C_ORM_EXPORT extern int c_orm_mock_hydrate_router_set_last_error_fail;

TEST test_hydrate_router_set_error_fail(void) {
  cdd_c_hydrate_router_t router;
  cdd_c_meta_t m1;
  cdd_c_abstract_struct_t row;
  int out_val = 0;

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
SUITE(hydrate_router_suite) {
  RUN_TEST(test_mock_hydrator_null);
  RUN_TEST(test_hydrate_router_init_free);
  RUN_TEST(test_hydrate_router_registration);
  RUN_TEST(test_hydrate_router_dispatch);
  RUN_TEST(test_hydrate_router_register_oom);
  RUN_TEST(test_hydrate_router_set_error_fail);
}

#if defined(__clang__) || defined(__GNUC__)
#endif
