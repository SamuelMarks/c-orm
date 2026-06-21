/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_db.h"
#include "c_orm_uuid.h"
#include "c_orm_query_builder.h"
#include "c_orm_string_builder.h"
#include "c_orm_log.h"
#include "greatest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

static int oom_countdown = 0;
static int oom_active = 0;
static int alloc_count = 0;

static void *mock_malloc_oom(size_t size) {
  if (oom_active) {
    if (oom_countdown == 0) {
      oom_countdown--;
      return NULL;
    }
    oom_countdown--;
  }
  alloc_count++;
  return malloc(size);
}

static void mock_free_oom(void *ptr) { free(ptr); }

/* OOM Fuzzer Macro */
#define OOM_TEST(test_func, max_allocs)                                        \
  do {                                                                         \
    int i;                                                                     \
    for (i = 0; i < max_allocs; i++) {                                         \
      oom_countdown = i;                                                       \
      oom_active = 1;                                                          \
      alloc_count = 0;                                                         \
      test_func();                                                             \
      oom_active = 0;                                                          \
      /* If it succeeded without failing allocator, stop early to save time */ \
      if (alloc_count <= i)                                                    \
        break;                                                                 \
    }                                                                          \
  } while (0)

static void do_uuid(void) {
  char uuid_buf[37];
  c_orm_uuid_v4(uuid_buf);
}

#include "c_orm_codegen.h"

static void do_codegen(void) {
  system("echo 'CREATE TABLE t_oom (id int);' > oom_schema.sql");
  c_orm_codegen_generate("oom_schema.sql", ".");
}

TEST test_codegen_oom(void) {
  OOM_TEST(do_codegen, 10);
  PASS();
}
TEST test_uuid_oom(void) {
  OOM_TEST(do_uuid, 2);
  PASS();
}

static void do_string_builder(void) {
  c_orm_string_builder_t *sb = NULL;
  if (c_orm_string_builder_init(&sb) == 0 && sb) {
    c_orm_string_builder_append(sb, "test");
    c_orm_string_builder_free(sb);
  }
}

TEST test_string_builder_oom(void) {
  OOM_TEST(do_string_builder, 5);
  oom_active = 1;
  oom_countdown = 1;
  do_string_builder();
  oom_countdown = 2;
  do_string_builder();
  oom_active = 0;
  PASS();
}

SUITE(oom_coverage_suite) {
  void *(*old_malloc)(size_t) = c_orm_malloc;
  void (*old_free)(void *) = c_orm_free;

  c_orm_malloc = mock_malloc_oom;
  c_orm_free = mock_free_oom;

  RUN_TEST(test_codegen_oom);
  RUN_TEST(test_uuid_oom);
  RUN_TEST(test_string_builder_oom);

  c_orm_malloc = old_malloc;
  c_orm_free = old_free;
}
