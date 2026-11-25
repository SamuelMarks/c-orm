/**
 * @file test_models_coverage.c
 * @brief Unit tests for generated C model structs, arrays, deep copies, and
 * memory management.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "Models.h"
#include "c_orm_api.h"
#define GREATEST_USE_LONGJMP 0
#include <greatest.h>
#include "test_assert_zero_branch.h"
/* clang-format on */

/** @brief Global flag to activate mock out-of-memory errors for model tests. */
int models_oom_active = 0;

/** @brief Counter decremented on each allocation before failing when reaching
 * zero. */
int models_oom_countdown = 0;

/**
 * @brief Mock malloc callback with countdown OOM simulation.
 * @param size Allocation size in bytes.
 * @param out_ptr Output pointer receiving allocated memory block.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on injected failure.
 */
c_orm_error_t e2e_mock_malloc(size_t size, void **out_ptr);

/**
 * @brief Mock calloc callback with countdown OOM simulation.
 * @param nmemb Number of elements.
 * @param size Size of each element in bytes.
 * @param out_ptr Output pointer receiving zero-initialized memory block.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on injected failure.
 */
c_orm_error_t e2e_mock_calloc(size_t nmemb, size_t size, void **out_ptr);

c_orm_error_t e2e_mock_malloc(size_t size, void **out_ptr) {
  if (models_oom_active) {
    if (models_oom_countdown == 0) {
      *out_ptr = NULL;
      return C_ORM_ERROR_MEMORY;
    }
    models_oom_countdown--;
  }
  return c_orm_default_malloc(size, out_ptr);
}

c_orm_error_t e2e_mock_calloc(size_t nmemb, size_t size, void **out_ptr) {
  c_orm_error_t rc;
  size_t total;
  if (models_oom_active) {
    if (models_oom_countdown == 0) {
      *out_ptr = NULL;
      return C_ORM_ERROR_MEMORY;
    }
    models_oom_countdown--;
  }
  total = nmemb * size;
  rc = c_orm_default_malloc(total, out_ptr);
  memset(*out_ptr, 0, total);
  return rc;
}

/**
 * @brief Forward declaration for test_users_models.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_users_models(void);

/**
 * @brief Tests Users model lifecycle, arrays, deepcopy, and out-of-memory error
 * paths.
 * @return GREATEST test result.
 */
TEST test_users_models(void) {
  struct Users_Array arr, arr2;
  struct Users u1, u2;
  c_orm_error_t err;

  int i;
  err = Users_Array_init(NULL, 10);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);

  err = Users_Array_init(&arr, 0);
  ASSERT_EQ(C_ORM_OK, err);
  Users_Array_free(&arr);

  /* Test OOM in Users_Array_init */
  models_oom_active = 1;
  models_oom_countdown = 0;
  err = Users_Array_init(&arr, 10);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  models_oom_active = 0;

  err = Users_Array_init(&arr, 10);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_EQ(0, arr.length);
  ASSERT_EQ(10, arr.capacity);

  memset(&u1, 0, sizeof(u1));
  u1.id = 1;
  ASSERT_EQ(C_ORM_OK, c_orm_default_malloc(10, (void **)&u1.username));
  C_ORM_STRCPY(u1.username, 10, "test");
  ASSERT_EQ(C_ORM_OK, c_orm_default_malloc(10, (void **)&u1.email));
  C_ORM_STRCPY(u1.email, 10, "e@ma.il");
  ASSERT_EQ(C_ORM_OK, c_orm_default_malloc(sizeof(int32_t), (void **)&u1.age));
  *u1.age = 30;
  ASSERT_EQ(C_ORM_OK, c_orm_default_malloc(sizeof(float), (void **)&u1.score));
  *u1.score = 5.5f;
  ASSERT_EQ(C_ORM_OK,
            c_orm_default_malloc(sizeof(bool), (void **)&u1.is_active));
  *u1.is_active = true;
  ASSERT_EQ(C_ORM_OK, c_orm_default_malloc(10, (void **)&u1.created_at));
  C_ORM_STRCPY(u1.created_at, 10, "now");

  err = Users_deepcopy(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  memset(&u2, 0, sizeof(u2));
  err = Users_deepcopy(NULL, &u2);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  memset(&u2, 0, sizeof(u2));
  err = Users_deepcopy(&u1, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);

  /* Test OOM on each dynamic field: username, email, age, score, is_active,
   * created_at */
  for (i = 0; i < 6; i++) {
    models_oom_countdown = i;
    models_oom_active = 1;
    memset(&u2, 0, sizeof(u2));
    err = Users_deepcopy(&u1, &u2);
    ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
    models_oom_active = 0;
  }

  memset(&u2, 0, sizeof(u2));
  err = Users_deepcopy(&u1, &u2);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_STR_EQ(u1.username, u2.username);

  arr.data[0] = u1;
  arr.length = 1;

  err = Users_Array_deepcopy(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  memset(&arr2, 0, sizeof(arr2));
  err = Users_Array_deepcopy(NULL, &arr2);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  err = Users_Array_deepcopy(&arr, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);

  memset(&arr2, 0, sizeof(arr2));
  err = Users_Array_deepcopy(&arr, &arr2);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_EQ(1, arr2.length);
  Users_Array_free(&arr2);

  /* OOM on Array_init within Array_deepcopy */
  models_oom_active = 1;
  models_oom_countdown = 0;
  memset(&arr2, 0, sizeof(arr2));
  err = Users_Array_deepcopy(&arr, &arr2);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  models_oom_active = 0;

  /* OOM on element deepcopy within Array_deepcopy */
  models_oom_active = 1;
  models_oom_countdown = 1;
  memset(&arr2, 0, sizeof(arr2));
  err = Users_Array_deepcopy(&arr, &arr2);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  models_oom_active = 0;

  Users_free(&u2);
  Users_Array_free(&arr);
  Users_Array_free(&arr2);

  /* Test NULL fields in deepcopy */
  memset(&u1, 0, sizeof(u1));
  err = Users_deepcopy(&u1, &u2);
  ASSERT_EQ(C_ORM_OK, err);
  Users_free(&u2);

  Users_free(NULL);
  Users_Array_free(NULL);

  arr.data = NULL;
  Users_Array_free(&arr);

  PASS();
}

/**
 * @brief Forward declaration for test_posts_models.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_posts_models(void);

/**
 * @brief Tests Posts model lifecycle, arrays, deepcopy, and out-of-memory error
 * paths.
 * @return GREATEST test result.
 */
TEST test_posts_models(void) {
  struct Posts_Array arr, arr2;
  struct Posts p1, p2;
  c_orm_error_t err;
  int i;

  err = Posts_Array_init(NULL, 10);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);

  err = Posts_Array_init(&arr, 0);
  ASSERT_EQ(C_ORM_OK, err);
  Posts_Array_free(&arr);

  /* Test OOM in Posts_Array_init */
  models_oom_active = 1;
  models_oom_countdown = 0;
  err = Posts_Array_init(&arr, 10);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  models_oom_active = 0;

  err = Posts_Array_init(&arr, 10);
  ASSERT_EQ(C_ORM_OK, err);

  memset(&p1, 0, sizeof(p1));
  p1.id = 1;
  ASSERT_EQ(C_ORM_OK, c_orm_default_malloc(10, (void **)&p1.title));
  C_ORM_STRCPY(p1.title, 10, "title");
  ASSERT_EQ(C_ORM_OK, c_orm_default_malloc(10, (void **)&p1.content));
  C_ORM_STRCPY(p1.content, 10, "content");
  ASSERT_EQ(C_ORM_OK,
            c_orm_default_malloc(sizeof(int64_t), (void **)&p1.views));
  *p1.views = 100;
  ASSERT_EQ(C_ORM_OK, c_orm_default_malloc(10, (void **)&p1.published_date));
  C_ORM_STRCPY(p1.published_date, 10, "date");

  err = Posts_deepcopy(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  memset(&p2, 0, sizeof(p2));
  err = Posts_deepcopy(NULL, &p2);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  memset(&p2, 0, sizeof(p2));
  err = Posts_deepcopy(&p1, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);

  /* Test OOM on each dynamic field: title, content, views, published_date */
  for (i = 0; i < 4; i++) {
    models_oom_countdown = i;
    models_oom_active = 1;
    memset(&p2, 0, sizeof(p2));
    err = Posts_deepcopy(&p1, &p2);
    ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
    models_oom_active = 0;
  }

  memset(&p2, 0, sizeof(p2));
  err = Posts_deepcopy(&p1, &p2);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_STR_EQ(p1.title, p2.title);

  arr.data[0] = p1;
  arr.length = 1;

  err = Posts_Array_deepcopy(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  memset(&arr2, 0, sizeof(arr2));
  err = Posts_Array_deepcopy(NULL, &arr2);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  err = Posts_Array_deepcopy(&arr, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);

  memset(&arr2, 0, sizeof(arr2));
  err = Posts_Array_deepcopy(&arr, &arr2);
  ASSERT_EQ(C_ORM_OK, err);
  Posts_Array_free(&arr2);

  /* OOM on Array_init within Array_deepcopy */
  models_oom_active = 1;
  models_oom_countdown = 0;
  memset(&arr2, 0, sizeof(arr2));
  err = Posts_Array_deepcopy(&arr, &arr2);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  models_oom_active = 0;

  /* OOM on element deepcopy within Array_deepcopy */
  models_oom_active = 1;
  models_oom_countdown = 1;
  memset(&arr2, 0, sizeof(arr2));
  err = Posts_Array_deepcopy(&arr, &arr2);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  models_oom_active = 0;

  Posts_free(&p2);
  Posts_Array_free(&arr);
  Posts_Array_free(&arr2);

  /* Test NULL fields */
  memset(&p1, 0, sizeof(p1));
  err = Posts_deepcopy(&p1, &p2);
  ASSERT_EQ(C_ORM_OK, err);
  Posts_free(&p2);

  Posts_free(NULL);
  Posts_Array_free(NULL);

  arr.data = NULL;
  Posts_Array_free(&arr);

  PASS();
}

/**
 * @brief Forward declaration for test_oauth2_models.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_oauth2_models(void);

/**
 * @brief Tests Oauth2_tokens model lifecycle, arrays, deepcopy, and
 * out-of-memory error paths.
 * @return GREATEST test result.
 */
TEST test_oauth2_models(void) {
  struct Oauth2_tokens_Array arr, arr2;
  struct Oauth2_tokens t1, t2;
  c_orm_error_t err;
  int i;

  err = Oauth2_tokens_Array_init(NULL, 10);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);

  err = Oauth2_tokens_Array_init(&arr, 0);
  ASSERT_EQ(C_ORM_OK, err);
  Oauth2_tokens_Array_free(&arr);

  /* Test OOM in Oauth2_tokens_Array_init */
  models_oom_active = 1;
  models_oom_countdown = 0;
  err = Oauth2_tokens_Array_init(&arr, 10);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  models_oom_active = 0;

  err = Oauth2_tokens_Array_init(&arr, 10);
  ASSERT_EQ(C_ORM_OK, err);

  memset(&t1, 0, sizeof(t1));
  ASSERT_EQ(C_ORM_OK, c_orm_default_malloc(10, (void **)&t1.access_token));
  C_ORM_STRCPY(t1.access_token, 10, "access");
  ASSERT_EQ(C_ORM_OK, c_orm_default_malloc(10, (void **)&t1.refresh_token));
  C_ORM_STRCPY(t1.refresh_token, 10, "refresh");
  ASSERT_EQ(C_ORM_OK, c_orm_default_malloc(10, (void **)&t1.token_type));
  C_ORM_STRCPY(t1.token_type, 10, "type");
  ASSERT_EQ(C_ORM_OK,
            c_orm_default_malloc(sizeof(int32_t), (void **)&t1.expires_in));
  *t1.expires_in = 3600;
  ASSERT_EQ(C_ORM_OK,
            c_orm_default_malloc(sizeof(int64_t), (void **)&t1.created_at));
  *t1.created_at = 12345;

  err = Oauth2_tokens_deepcopy(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  memset(&t2, 0, sizeof(t2));
  err = Oauth2_tokens_deepcopy(NULL, &t2);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  memset(&t2, 0, sizeof(t2));
  err = Oauth2_tokens_deepcopy(&t1, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);

  /* Test OOM on each dynamic field: access_token, refresh_token, token_type,
   * expires_in, created_at */
  for (i = 0; i < 5; i++) {
    models_oom_countdown = i;
    models_oom_active = 1;
    memset(&t2, 0, sizeof(t2));
    err = Oauth2_tokens_deepcopy(&t1, &t2);
    ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
    models_oom_active = 0;
  }

  memset(&t2, 0, sizeof(t2));
  err = Oauth2_tokens_deepcopy(&t1, &t2);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_STR_EQ(t1.access_token, t2.access_token);

  arr.data[0] = t1;
  arr.length = 1;

  err = Oauth2_tokens_Array_deepcopy(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  memset(&arr2, 0, sizeof(arr2));
  err = Oauth2_tokens_Array_deepcopy(NULL, &arr2);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  err = Oauth2_tokens_Array_deepcopy(&arr, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);

  memset(&arr2, 0, sizeof(arr2));
  err = Oauth2_tokens_Array_deepcopy(&arr, &arr2);
  ASSERT_EQ(C_ORM_OK, err);
  Oauth2_tokens_Array_free(&arr2);

  /* OOM on Array_init within Array_deepcopy */
  models_oom_active = 1;
  models_oom_countdown = 0;
  memset(&arr2, 0, sizeof(arr2));
  err = Oauth2_tokens_Array_deepcopy(&arr, &arr2);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  models_oom_active = 0;

  /* OOM on element deepcopy within Array_deepcopy */
  models_oom_active = 1;
  models_oom_countdown = 1;
  memset(&arr2, 0, sizeof(arr2));
  err = Oauth2_tokens_Array_deepcopy(&arr, &arr2);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  models_oom_active = 0;

  Oauth2_tokens_free(&t2);
  Oauth2_tokens_Array_free(&arr);
  Oauth2_tokens_Array_free(&arr2);

  /* Test NULL fields */
  memset(&t1, 0, sizeof(t1));
  err = Oauth2_tokens_deepcopy(&t1, &t2);
  ASSERT_EQ(C_ORM_OK, err);
  Oauth2_tokens_free(&t2);

  Oauth2_tokens_free(NULL);
  Oauth2_tokens_Array_free(NULL);

  arr.data = NULL;
  Oauth2_tokens_Array_free(&arr);

  PASS();
}

/**
 * @brief Forward declaration for test_mock_allocators.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_mock_allocators(void);

/**
 * @brief Tests mock allocation and countdown behaviors.
 * @return GREATEST test result.
 */
TEST test_mock_allocators(void) {
  void *p = NULL;
  models_oom_active = 1;
  models_oom_countdown = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, e2e_mock_malloc(10, &p));
  ASSERT_EQ(NULL, p);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, e2e_mock_calloc(1, 10, &p));
  ASSERT_EQ(NULL, p);

  models_oom_countdown = 1;
  ASSERT_EQ(C_ORM_OK, e2e_mock_malloc(10, &p));
  ASSERT(p != NULL);
  ASSERT_EQ(C_ORM_OK, c_orm_default_free(p));

  models_oom_countdown = 1;
  ASSERT_EQ(C_ORM_OK, e2e_mock_calloc(1, 10, &p));
  ASSERT(p != NULL);
  ASSERT_EQ(C_ORM_OK, c_orm_default_free(p));

  models_oom_active = 0;
  ASSERT_EQ(C_ORM_OK, e2e_mock_malloc(10, &p));
  ASSERT(p != NULL);
  ASSERT_EQ(C_ORM_OK, c_orm_default_free(p));

  ASSERT_EQ(C_ORM_OK, e2e_mock_calloc(1, 10, &p));
  ASSERT(p != NULL);
  ASSERT_EQ(C_ORM_OK, c_orm_default_free(p));

  PASS();
}

/**
 * @brief Test suite runner for generated model coverage.
 * @param models_coverage_suite Suite runner function name.
 * @return GREATEST suite result.
 */
SUITE(models_coverage_suite) {
  c_orm_malloc_fn_t old_malloc = c_orm_malloc;
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(e2e_mock_malloc, c_orm_realloc, c_orm_free));

  RUN_TEST(test_users_models);
  RUN_TEST(test_posts_models);
  RUN_TEST(test_oauth2_models);
  RUN_TEST(test_mock_allocators);
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free));
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
