#if defined(__clang__) || defined(__GNUC__)
#endif
/* clang-format off */
#include "Models.h"
#include "c_orm_api.h"
#include "greatest.h"
/* clang-format on */

int models_oom_active = 0;
int models_oom_countdown = 0;

void *e2e_mock_malloc(size_t size);
void *e2e_mock_calloc(size_t nmemb, size_t size);

void *e2e_mock_malloc(size_t size) {
  if (models_oom_active) {
    if (models_oom_countdown == 0) {
      return NULL;
    }
    models_oom_countdown--;
  }
  return malloc(size);
}

void *e2e_mock_calloc(size_t nmemb, size_t size) {
  if (models_oom_active) {
    if (models_oom_countdown == 0) {
      return NULL;
    }
    models_oom_countdown--;
  }
  return calloc(nmemb, size);
}

TEST test_users_models(void) {
  struct Users_Array arr, arr2;
  struct Users u1, u2;
  int err;

  err = Users_Array_init(NULL, 10);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);

  err = Users_Array_init(&arr, 0);
  ASSERT_EQ(C_ORM_OK, err);
  Users_Array_free(&arr);

  err = Users_Array_init(&arr, 10);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_EQ(0, arr.length);
  ASSERT_EQ(10, arr.capacity);

  memset(&u1, 0, sizeof(u1));
  u1.id = 1;
  u1.username = (char *)malloc(10);
#if defined(_MSC_VER)
  strcpy_s(u1.username, 10, "test");
#else
  strcpy(u1.username, "test");
#endif

  u1.email = (char *)malloc(10);
#if defined(_MSC_VER)
  strcpy_s(u1.email, 10, "e@ma.il");
#else
  strcpy(u1.email, "e@ma.il");
#endif

  u1.age = (int32_t *)malloc(sizeof(int32_t));
  *u1.age = 30;
  u1.score = (float *)malloc(sizeof(float));
  *u1.score = 5.5f;
  u1.is_active = (bool *)malloc(sizeof(bool));
  *u1.is_active = true;
  u1.created_at = (char *)malloc(10);
#if defined(_MSC_VER)
  strcpy_s(u1.created_at, 10, "now");
#else
  strcpy(u1.created_at, "now");
#endif

  err = Users_deepcopy(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);

  err = Users_deepcopy(&u1, &u2);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_STR_EQ(u1.username, u2.username);

  arr.data[0] = u1;
  arr.length = 1;

  err = Users_Array_deepcopy(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);

  err = Users_Array_deepcopy(&arr, &arr2);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_EQ(1, arr2.length);
  Users_Array_free(&arr2);

  models_oom_active = 1;
  models_oom_countdown = 1; /* First alloc succeeds, second fails */
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

  PASS();
}

TEST test_posts_models(void) {
  struct Posts_Array arr, arr2;
  struct Posts p1, p2;
  int err;

  err = Posts_Array_init(NULL, 10);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);

  err = Posts_Array_init(&arr, 0);
  ASSERT_EQ(C_ORM_OK, err);
  Posts_Array_free(&arr);

  err = Posts_Array_init(&arr, 10);
  ASSERT_EQ(C_ORM_OK, err);

  memset(&p1, 0, sizeof(p1));
  p1.id = 1;
  p1.title = (char *)malloc(10);
#if defined(_MSC_VER)
  strcpy_s(p1.title, 10, "title");
#else
  strcpy(p1.title, "title");
#endif

  p1.content = (char *)malloc(10);
#if defined(_MSC_VER)
  strcpy_s(p1.content, 10, "content");
#else
  strcpy(p1.content, "content");
#endif

  p1.views = (int64_t *)malloc(sizeof(int64_t));
  *p1.views = 100;
  p1.published_date = (char *)malloc(10);
#if defined(_MSC_VER)
  strcpy_s(p1.published_date, 10, "date");
#else
  strcpy(p1.published_date, "date");
#endif

  err = Posts_deepcopy(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);

  err = Posts_deepcopy(&p1, &p2);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_STR_EQ(p1.title, p2.title);

  arr.data[0] = p1;
  arr.length = 1;

  err = Posts_Array_deepcopy(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);

  err = Posts_Array_deepcopy(&arr, &arr2);
  ASSERT_EQ(C_ORM_OK, err);
  Posts_Array_free(&arr2);

  models_oom_active = 1;
  models_oom_countdown = 1;
  err = Posts_Array_deepcopy(&arr, &arr2);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  Posts_Array_free(&arr2);
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

  PASS();
}

TEST test_oauth2_models(void) {
  struct Oauth2_tokens_Array arr, arr2;
  struct Oauth2_tokens t1, t2;
  int err;

  err = Oauth2_tokens_Array_init(NULL, 10);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);

  err = Oauth2_tokens_Array_init(&arr, 0);
  ASSERT_EQ(C_ORM_OK, err);
  Oauth2_tokens_Array_free(&arr);

  err = Oauth2_tokens_Array_init(&arr, 10);
  ASSERT_EQ(C_ORM_OK, err);

  memset(&t1, 0, sizeof(t1));
  t1.access_token = (char *)malloc(10);
#if defined(_MSC_VER)
  strcpy_s(t1.access_token, 10, "access");
#else
  strcpy(t1.access_token, "access");
#endif

  t1.refresh_token = (char *)malloc(10);
#if defined(_MSC_VER)
  strcpy_s(t1.refresh_token, 10, "refresh");
#else
  strcpy(t1.refresh_token, "refresh");
#endif

  t1.token_type = (char *)malloc(10);
#if defined(_MSC_VER)
  strcpy_s(t1.token_type, 10, "type");
#else
  strcpy(t1.token_type, "type");
#endif

  t1.expires_in = (int32_t *)malloc(sizeof(int32_t));
  *t1.expires_in = 3600;
  t1.created_at = (int64_t *)malloc(sizeof(int64_t));
  *t1.created_at = 12345;

  err = Oauth2_tokens_deepcopy(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);

  err = Oauth2_tokens_deepcopy(&t1, &t2);
  ASSERT_EQ(C_ORM_OK, err);
  ASSERT_STR_EQ(t1.access_token, t2.access_token);

  arr.data[0] = t1;
  arr.length = 1;

  err = Oauth2_tokens_Array_deepcopy(NULL, NULL);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);

  err = Oauth2_tokens_Array_deepcopy(&arr, &arr2);
  ASSERT_EQ(C_ORM_OK, err);
  Oauth2_tokens_Array_free(&arr2);

  models_oom_active = 1;
  models_oom_countdown = 1;
  err = Oauth2_tokens_Array_deepcopy(&arr, &arr2);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
  Oauth2_tokens_Array_free(&arr2);
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

  PASS();
}

SUITE(models_coverage_suite) {
  c_orm_set_allocators(e2e_mock_malloc, realloc, free);

  RUN_TEST(test_users_models);
  RUN_TEST(test_posts_models);
  RUN_TEST(test_oauth2_models);
  c_orm_set_allocators(malloc, realloc, free);
}

#if defined(__clang__) || defined(__GNUC__)
#endif
