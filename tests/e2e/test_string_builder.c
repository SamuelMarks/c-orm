/**
 * @file test_string_builder.c
 * @brief Unit tests for string builder data structure and error handling.
 */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_log.h"
#include "c_orm_string_builder.h"
#include "c_orm_uuid.h"
#define GREATEST_USE_LONGJMP 0
#include <greatest.h>
#include "test_assert_zero_branch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

#ifdef C_ORM_TEST_ALLOCATOR

/** @brief Counter decremented before triggering test malloc failure. */
static int malloc_fail_countdown = -1;

/**
 * @brief Executes my_test_malloc.
 * @param size Parameter size.
 * @param out_ptr Output pointer receiving allocated address.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on failure.
 */
static c_orm_error_t my_test_malloc(size_t size, void **out_ptr) {
  if (malloc_fail_countdown == 0) {
    malloc_fail_countdown--;
    *out_ptr = NULL;
    return C_ORM_ERROR_MEMORY;
  }
  malloc_fail_countdown--;
  return c_orm_default_malloc(size, out_ptr);
}

/** @brief Counter decremented before triggering test realloc failure. */
static int realloc_fail_countdown = -1;

/**
 * @brief Mock realloc callback for testing allocator failures.
 * @param ptr Existing pointer.
 * @param size Requested reallocation size.
 * @param out_ptr Output pointer receiving reallocated address.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on failure.
 */
static c_orm_error_t my_test_realloc(void *ptr, size_t size, void **out_ptr) {
  if (realloc_fail_countdown == 0) {
    realloc_fail_countdown--;
    *out_ptr = NULL;
    return C_ORM_ERROR_MEMORY;
  }
  return c_orm_default_realloc(ptr, size, out_ptr);
}

/** @brief Countdown triggering C_ORM_OK with NULL *out_ptr in mock
 * malloc/realloc. */
static int sb_ok_null_countdown = -1;

/**
 * @brief Mock malloc returning C_ORM_OK with NULL *out_ptr when countdown hits
 * 0.
 * @param size Requested allocation size.
 * @param out_ptr Output pointer receiving allocated address.
 * @return C_ORM_OK or C_ORM_ERROR_MEMORY.
 */
static c_orm_error_t sb_ok_null_malloc(size_t size, void **out_ptr) {
  if (sb_ok_null_countdown == 0) {
    sb_ok_null_countdown--;
    *out_ptr = NULL;
    return C_ORM_OK;
  }
  if (sb_ok_null_countdown > 0) {
    sb_ok_null_countdown--;
  }
  return c_orm_default_malloc(size, out_ptr);
}

/**
 * @brief Mock realloc returning C_ORM_OK with NULL *out_ptr when countdown hits
 * 0.
 * @param ptr Existing pointer.
 * @param size Requested reallocation size.
 * @param out_ptr Output pointer receiving reallocated address.
 * @return C_ORM_OK or C_ORM_ERROR_MEMORY.
 */
static c_orm_error_t sb_ok_null_realloc(void *ptr, size_t size,
                                        void **out_ptr) {
  if (sb_ok_null_countdown == 0) {
    sb_ok_null_countdown--;
    *out_ptr = NULL;
    return C_ORM_OK;
  }
  if (sb_ok_null_countdown > 0) {
    sb_ok_null_countdown--;
  }
  return c_orm_default_realloc(ptr, size, out_ptr);
}

/** @brief Bitmask controlling simulated free failures in sb_fail_free. */
static unsigned int sb_free_fail_mask = 0;
/** @brief Call counter for sb_fail_free. */
static unsigned int sb_free_call_idx = 0;

/**
 * @brief Mock free callback returning C_ORM_ERROR_MEMORY on masked calls.
 * @param ptr Pointer to free.
 * @return C_ORM_OK or C_ORM_ERROR_MEMORY.
 */
static c_orm_error_t sb_fail_free(void *ptr) {
  unsigned int bit = 1U << sb_free_call_idx++;
  c_orm_default_free(ptr);
  if ((sb_free_fail_mask & bit) != 0U) {
    return C_ORM_ERROR_MEMORY;
  }
  return C_ORM_OK;
}
#endif

/**
 * @brief Internal struct definition for testing invalid buffer state.
 * @var buffer Underlying character buffer.
 * @var length Current string length.
 * @var capacity Allocated capacity of buffer.
 * @var valid State validity flag.
 */
struct c_orm_string_builder {
  char *buffer;
  size_t length;
  size_t capacity;
  int valid;
};

/**
 * @brief Comprehensive tests for c_orm_string_builder APIs and error states.
 * @return GREATEST test result.
 */
TEST test_c_orm_string_builder(void) {
  c_orm_string_builder_t *sb;
  const char *str;
  size_t len;
  c_orm_error_t rc;
#ifdef C_ORM_TEST_ALLOCATOR
  c_orm_malloc_fn_t old_malloc;
  c_orm_realloc_fn_t old_realloc;
#endif

  sb = NULL;
  str = NULL;
  len = 0;

#ifdef C_ORM_TEST_ALLOCATOR
  {
    old_malloc = c_orm_malloc;
    old_realloc = c_orm_realloc;
    c_orm_set_allocators(my_test_malloc, c_orm_realloc, c_orm_free);
    c_orm_set_allocators(c_orm_malloc, my_test_realloc, c_orm_free);

    malloc_fail_countdown = 0;
    rc = c_orm_string_builder_init(&sb);
    ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

    malloc_fail_countdown = 1;
    rc = c_orm_string_builder_init(&sb);
    ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

    malloc_fail_countdown = -1;
    realloc_fail_countdown = -1;
  }
#endif

  rc = c_orm_string_builder_init(NULL);
  ASSERT_NEQ(0, rc);

  rc = c_orm_string_builder_init(&sb);
  ASSERT_EQ(0, rc);
  ASSERT(sb != NULL);

#ifdef C_ORM_TEST_ALLOCATOR
  rc = c_orm_string_builder_append(sb, "Initial");
  ASSERT_EQ(0, rc);
  realloc_fail_countdown = 0;
  rc = c_orm_string_builder_append(
      sb, " This is a very long string that should definitely force a "
          "reallocation of the underlying buffer because it exceeds the "
          "initial capacity of 64 bytes.");
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = c_orm_string_builder_append(sb, " More text");
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = c_orm_string_builder_get(sb, &str);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  rc = c_orm_string_builder_len(sb, &len);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  realloc_fail_countdown = -1;
  c_orm_string_builder_free(sb);
  rc = c_orm_string_builder_init(&sb);
  ASSERT_EQ(0, rc);
#endif

  rc = c_orm_string_builder_append(NULL, "test");
  ASSERT_NEQ(0, rc);
  rc = c_orm_string_builder_append(sb, NULL);
  ASSERT_NEQ(0, rc);

  rc = c_orm_string_builder_append(sb, "");
  ASSERT_EQ(0, rc);

  rc = c_orm_string_builder_append(sb, "Hello, ");
  ASSERT_EQ(0, rc);

  rc = c_orm_string_builder_get(NULL, &str);
  ASSERT_NEQ(0, rc);
  rc = c_orm_string_builder_get(sb, NULL);
  ASSERT_NEQ(0, rc);

  rc = c_orm_string_builder_get(sb, &str);
  ASSERT_EQ(0, rc);
  ASSERT_STR_EQ("Hello, ", str);

  rc = c_orm_string_builder_len(NULL, &len);
  ASSERT_NEQ(0, rc);
  rc = c_orm_string_builder_len(sb, NULL);
  ASSERT_NEQ(0, rc);

  rc = c_orm_string_builder_len(sb, &len);
  ASSERT_EQ(0, rc);
  ASSERT_EQ((size_t)7, len);

  rc = c_orm_string_builder_append(sb, "World!");
  ASSERT_EQ(0, rc);

  rc = c_orm_log_debug("Test %s", "log");
  ASSERT_EQ(C_ORM_OK, rc);
  rc = c_orm_log_debug(NULL);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, rc);

  rc = c_orm_string_builder_get(sb, &str);
  ASSERT_EQ(0, rc);
  ASSERT_STR_EQ("Hello, World!", str);

  /* Force reallocation */
  rc = c_orm_string_builder_append(
      sb, " This is a very long string that should definitely force a "
          "reallocation of the underlying buffer because it exceeds the "
          "initial capacity of 64 bytes.");
  ASSERT_EQ(0, rc);

  c_orm_string_builder_free(sb);
  c_orm_string_builder_free(NULL);

  {
    const char *empty_str;
    c_orm_string_builder_t *manual_sb;

    empty_str = NULL;
    manual_sb = NULL;
    ASSERT_EQ(C_ORM_OK, c_orm_default_malloc(sizeof(c_orm_string_builder_t),
                                             (void **)&manual_sb));
    manual_sb->buffer = NULL;
    manual_sb->valid = 1;
    c_orm_string_builder_get(manual_sb, &empty_str);
    c_orm_string_builder_free(manual_sb);
  }

  /* Test mock append countdown and get fail */
  {
    c_orm_string_builder_t *msb;
    const char *mstr;

    msb = NULL;
    mstr = NULL;
    c_orm_mock_string_builder_append_countdown = 1;
    rc = c_orm_string_builder_init(&msb);
    ASSERT_EQ(C_ORM_OK, rc);
    rc = c_orm_string_builder_append(msb, "first");
    ASSERT_EQ(C_ORM_OK, rc);
    rc = c_orm_string_builder_append(msb, "second");
    ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
    c_orm_string_builder_free(msb);

    c_orm_mock_string_builder_append_countdown = 0;
    rc = c_orm_string_builder_init(&msb);
    ASSERT_EQ(C_ORM_OK, rc);
    rc = c_orm_string_builder_append(msb, "zero");
    ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
    c_orm_string_builder_free(msb);
    c_orm_mock_string_builder_append_countdown = -1;

    rc = c_orm_string_builder_init(&msb);
    ASSERT_EQ(C_ORM_OK, rc);
    c_orm_mock_string_builder_get_fail = 1;
    rc = c_orm_string_builder_get(msb, &mstr);
    ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
    c_orm_mock_string_builder_get_fail = 0;
    c_orm_string_builder_free(msb);
  }

#ifdef C_ORM_TEST_ALLOCATOR
  c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);
  c_orm_set_allocators(c_orm_malloc, old_realloc, c_orm_free);
#endif

  PASS();
}

/**
 * @brief Unit tests for `c_orm_quote_identifier`, `c_orm_strtoint64`,
 * bounds-checked Safe CRT helpers, and `c_orm_random_bytes`.
 * @return GREATEST test result.
 */
TEST test_quote_identifier_and_safe_crt(void) {
  c_orm_string_builder_t *sb = NULL;
  const char *s = NULL;
  int64_t iv = 0;
  const char *ep = NULL;
  char buf[16];
  unsigned char rnd[32];
  int non_zero = 0;
  size_t i;

  ASSERT_EQ(C_ORM_OK, c_orm_string_builder_init(&sb));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_quote_identifier(NULL, "t", '"'));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_quote_identifier(sb, NULL, '"'));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_quote_identifier(sb, "", '"'));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_quote_identifier(sb, "t", '\''));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_quote_identifier(sb, "bad\nname", '"'));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_quote_identifier(sb, "bad\x7fname", '"'));

  ASSERT_EQ(C_ORM_OK, c_orm_quote_identifier(sb, "my\"table", '"'));
  ASSERT_EQ(C_ORM_OK, c_orm_string_builder_append(sb, ","));
  ASSERT_EQ(C_ORM_OK, c_orm_quote_identifier(sb, "my`col", '`'));
  ASSERT_EQ(C_ORM_OK, c_orm_string_builder_get(sb, &s));
  ASSERT_STR_EQ("\"my\"\"table\",`my``col`", s);
  ASSERT_EQ(C_ORM_OK, c_orm_string_builder_free(sb));

#ifdef C_ORM_TEST_ALLOCATOR
  ASSERT_EQ(C_ORM_OK, c_orm_string_builder_init(&sb));
  c_orm_mock_string_builder_append_countdown = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_quote_identifier(sb, "tbl", '"'));
  c_orm_mock_string_builder_append_countdown = -1;
  ASSERT_EQ(C_ORM_OK, c_orm_string_builder_free(sb));

  ASSERT_EQ(C_ORM_OK, c_orm_string_builder_init(&sb));
  c_orm_mock_string_builder_append_countdown = 1;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_quote_identifier(sb, "tbl", '"'));
  c_orm_mock_string_builder_append_countdown = -1;
  ASSERT_EQ(C_ORM_OK, c_orm_string_builder_free(sb));
#endif

  /* c_orm_fopen validation branches */
  {
    FILE *tfp = NULL;
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_fopen(&tfp, NULL, "r"));
    ASSERT_EQ(NULL, tfp);
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_fopen(&tfp, "dummy", NULL));
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_fopen(&tfp, "", "r"));
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_fopen(&tfp, "dummy", ""));
  }

  /* c_orm_setenv & c_orm_unsetenv validation branches */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_setenv(NULL, "1"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_setenv("K", NULL));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_setenv("", "1"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_setenv("BAD=KEY", "1"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_unsetenv(NULL));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_unsetenv(""));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_unsetenv("BAD=KEY"));

  /* c_orm_strtoint64 */
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_strtoint64(NULL, NULL, 10, &iv));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_strtoint64("1", NULL, 10, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_strtoint64("10", &ep, 1, &iv));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_strtoint64("10", NULL, 1, &iv));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_strtoint64("10", &ep, 37, &iv));
  ASSERT_EQ(C_ORM_ERROR_PARSE, c_orm_strtoint64("   xyz", &ep, 10, &iv));
  ASSERT_EQ(C_ORM_ERROR_PARSE, c_orm_strtoint64("xyz", NULL, 10, &iv));
  ASSERT_EQ(C_ORM_OK, c_orm_strtoint64(" \t\n\r\f\v+42abc", &ep, 10, &iv));
  ASSERT_EQ(42, (int)iv);
  ASSERT_STR_EQ("abc", ep);
  ASSERT_EQ(C_ORM_OK, c_orm_strtoint64("-9876543210", &ep, 10, &iv));
  ASSERT(iv == -((int64_t)9876543 * (int64_t)1000 + (int64_t)210));
  ASSERT_EQ(C_ORM_OK, c_orm_strtoint64("0x1aF", &ep, 0, &iv));
  ASSERT_EQ(0x1af, (int)iv);
  ASSERT_EQ(C_ORM_OK, c_orm_strtoint64("0X1A", &ep, 16, &iv));
  ASSERT_EQ(0x1a, (int)iv);
  ASSERT_EQ(C_ORM_OK, c_orm_strtoint64("007", &ep, 16, &iv));
  ASSERT_EQ(7, (int)iv);
  ASSERT_EQ(C_ORM_OK, c_orm_strtoint64("10{", &ep, 10, &iv));
  ASSERT_EQ(10, (int)iv);
  ASSERT_STR_EQ("{", ep);
  ASSERT_EQ(C_ORM_OK, c_orm_strtoint64("10[", &ep, 10, &iv));
  ASSERT_EQ(10, (int)iv);
  ASSERT_STR_EQ("[", ep);
  ASSERT_EQ(C_ORM_OK, c_orm_strtoint64("0755", &ep, 0, &iv));
  ASSERT_EQ(493, (int)iv);
  ASSERT_EQ(C_ORM_OK, c_orm_strtoint64("99", NULL, 0, &iv));
  ASSERT_EQ(99, (int)iv);

  /* Safe CRT bounds helpers */
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_strcpy_s(NULL, 4, "a"));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_strcpy_s(buf, 4, NULL));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW, c_orm_strcpy_s(buf, 0, "a"));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW, c_orm_strcpy_s(buf, 3, "abcd"));
  ASSERT_EQ(C_ORM_OK, c_orm_strcpy_s(buf, sizeof(buf), "hi"));
  ASSERT_STR_EQ("hi", buf);

  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_strncpy_s(NULL, 4, "a", 1));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_strncpy_s(buf, 4, NULL, 1));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW, c_orm_strncpy_s(buf, 0, "a", 1));
  ASSERT_EQ(C_ORM_OK, c_orm_strncpy_s(buf, 4, "abcdef", 10));
  ASSERT_STR_EQ("abc", buf);

  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_strcat_s(NULL, 4, "a"));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_strcat_s(buf, 4, NULL));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW, c_orm_strcat_s(buf, 0, "a"));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW, c_orm_strcat_s(buf, 4, "d"));
  memset(buf, 'X', 4);
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW, c_orm_strcat_s(buf, 4, "a"));
  ASSERT_EQ(C_ORM_OK, c_orm_strcpy_s(buf, sizeof(buf), "ab"));
  ASSERT_EQ(C_ORM_OK, c_orm_strcat_s(buf, sizeof(buf), "cd"));
  ASSERT_STR_EQ("abcd", buf);

  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_strncat_s(NULL, 4, "a", 1));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_strncat_s(buf, 4, NULL, 1));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW, c_orm_strncat_s(buf, 0, "a", 1));
  ASSERT_EQ(C_ORM_OK, c_orm_strncat_s(buf, sizeof(buf), "", 4));
  ASSERT_STR_EQ("abcd", buf);
  ASSERT_EQ(C_ORM_OK, c_orm_strncat_s(buf, sizeof(buf), "ef", 2));
  ASSERT_STR_EQ("abcdef", buf);
  ASSERT_EQ(C_ORM_OK, c_orm_strcpy_s(buf, sizeof(buf), "abcd"));
  ASSERT_EQ(C_ORM_OK, c_orm_strncat_s(buf, 6, "efgh", 4));
  ASSERT_STR_EQ("abcde", buf);
  memset(buf, 'Y', 4);
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW, c_orm_strncat_s(buf, 4, "z", 1));

  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_getenv_s(NULL, 4, "PATH"));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_getenv_s(buf, 4, NULL));

#ifdef C_ORM_TEST_ALLOCATOR
  c_orm_mock_safe_crt_countdown = 1;
  ASSERT_EQ(C_ORM_OK, c_orm_sprintf(buf, sizeof(buf), "ok"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_sprintf(buf, sizeof(buf), "fail"));

  c_orm_mock_safe_crt_countdown = 1;
  ASSERT_EQ(C_ORM_OK, c_orm_strcpy_s(buf, sizeof(buf), "ok"));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW, c_orm_strcpy_s(buf, sizeof(buf), "fail"));

  c_orm_mock_safe_crt_countdown = 1;
  ASSERT_EQ(C_ORM_OK, c_orm_strncpy_s(buf, sizeof(buf), "ok", 2));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW, c_orm_strncpy_s(buf, sizeof(buf), "fail", 4));
  c_orm_mock_safe_crt_countdown = -1;
#endif
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW, c_orm_getenv_s(buf, 0, "PATH"));
  ASSERT_EQ(C_ORM_OK, c_orm_setenv("C_ORM_TEST_SAFE_ENV", "hello_env"));
  ASSERT_EQ(C_ORM_OK, c_orm_getenv_s(buf, sizeof(buf), "C_ORM_TEST_SAFE_ENV"));
  ASSERT_STR_EQ("hello_env", buf);
  ASSERT_EQ(C_ORM_OK, c_orm_unsetenv("C_ORM_TEST_SAFE_ENV"));
  ASSERT_EQ(C_ORM_OK,
            c_orm_getenv_s(buf, sizeof(buf), "C_ORM_DEFINITELY_UNSET_VAR"));
  ASSERT_STR_EQ("", buf);

  /* c_orm_random_bytes */
  ASSERT_EQ(C_ORM_OK, c_orm_random_bytes(NULL, 0));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_random_bytes(NULL, 8));
  memset(rnd, 0, sizeof(rnd));
  ASSERT_EQ(C_ORM_OK, c_orm_random_bytes(rnd, sizeof(rnd)));
  for (i = 0; i < sizeof(rnd); ++i) {
    non_zero |= (int)rnd[i];
  }
  ASSERT_NEQ(0, non_zero);

  PASS();
}

/**
 * @brief Exhaustive line and branch tests for
 * `c_orm_quote_qualified_identifier`, `c_orm_append_placeholder`,
 * `c_orm_append_string_literal`, and defensive allocator/free error paths in
 * `c_orm_string_builder.c`.
 * @return GREATEST test result.
 */
TEST test_qualified_placeholder_and_literal_coverage(void) {
  c_orm_string_builder_t *sb = NULL;
  const char *s = NULL;

  /* 1. c_orm_quote_qualified_identifier validation & happy paths */
  ASSERT_EQ(C_ORM_OK, c_orm_string_builder_init(&sb));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_quote_qualified_identifier(NULL, "a.b", '"'));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_quote_qualified_identifier(sb, NULL, '"'));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_quote_qualified_identifier(sb, "", '"'));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_quote_qualified_identifier(sb, "a.b", '\''));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_quote_qualified_identifier(sb, ".b", '"'));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_quote_qualified_identifier(sb, "a.", '"'));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_quote_qualified_identifier(sb, "a..b", '"'));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_quote_qualified_identifier(sb, "a.bad\x01", '"'));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_quote_qualified_identifier(sb, "a.bad\x7f", '"'));

  ASSERT_EQ(C_ORM_OK, c_orm_quote_qualified_identifier(sb, "*", '"'));
  ASSERT_EQ(C_ORM_OK, c_orm_string_builder_append(sb, ","));
  ASSERT_EQ(C_ORM_OK, c_orm_quote_qualified_identifier(sb, "sch.tbl.*", '"'));
  ASSERT_EQ(C_ORM_OK, c_orm_string_builder_append(sb, ","));
  ASSERT_EQ(C_ORM_OK, c_orm_quote_qualified_identifier(sb, "*.col", '`'));
  ASSERT_EQ(C_ORM_OK, c_orm_string_builder_get(sb, &s));
  ASSERT_STR_EQ("*,\"sch\".\"tbl\".*,`*`.`col`", s);
  ASSERT_EQ(C_ORM_OK, c_orm_string_builder_free(sb));

  /* 2. c_orm_append_placeholder & c_orm_append_string_literal */
  ASSERT_EQ(C_ORM_OK, c_orm_string_builder_init(&sb));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_append_placeholder(NULL, 0, 1));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_append_placeholder(sb, 1, 0));
  ASSERT_EQ(C_ORM_OK, c_orm_append_placeholder(sb, 0, 0));
  ASSERT_EQ(C_ORM_OK, c_orm_string_builder_append(sb, ","));
  ASSERT_EQ(C_ORM_OK, c_orm_append_placeholder(sb, 1, 15));
  ASSERT_EQ(C_ORM_OK, c_orm_string_builder_append(sb, ","));

  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_append_string_literal(NULL, "abc"));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_append_string_literal(sb, NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_append_string_literal(sb, "O'Reilly"));
  ASSERT_EQ(C_ORM_OK, c_orm_string_builder_get(sb, &s));
  ASSERT_STR_EQ("?,$15,'O''Reilly'", s);
  ASSERT_EQ(C_ORM_OK, c_orm_string_builder_free(sb));

#ifdef C_ORM_TEST_ALLOCATOR
  /* 3. OOM & defensive NULL-with-OK + free error branches */
  {
    c_orm_malloc_fn_t saved_malloc = c_orm_malloc;
    c_orm_realloc_fn_t saved_realloc = c_orm_realloc;
    c_orm_free_fn_t saved_free = c_orm_free;

    /* NULL-with-OK on sb struct, sb->buffer, and realloc */
    ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(sb_ok_null_malloc,
                                             sb_ok_null_realloc, saved_free));
    sb_ok_null_countdown = 0;
    ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_string_builder_init(&sb));

    sb_ok_null_countdown = 1;
    ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_string_builder_init(&sb));

    sb_ok_null_countdown = -1;
    ASSERT_EQ(C_ORM_OK, c_orm_string_builder_init(&sb));
    ASSERT_EQ(C_ORM_OK, c_orm_string_builder_append(
                            sb, "0123456789012345678901234567890123456789012345"
                                "6789012345678901234"));
    sb_ok_null_countdown = 0;
    ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_string_builder_append(
                                      sb, "012345678901234567890123456789012345"
                                          "67890123456789012345678901234"));
    ASSERT_EQ(C_ORM_OK, c_orm_string_builder_free(sb));

    /* NULL-with-OK and malloc error on seg_buf in quote_qualified_identifier */
    sb_ok_null_countdown = -1;
    ASSERT_EQ(C_ORM_OK, c_orm_string_builder_init(&sb));
    sb_ok_null_countdown = 0;
    ASSERT_EQ(C_ORM_ERROR_MEMORY,
              c_orm_quote_qualified_identifier(sb, "tbl.col", '"'));
    ASSERT_EQ(C_ORM_OK, c_orm_string_builder_free(sb));

    ASSERT_EQ(C_ORM_OK,
              c_orm_set_allocators(my_test_malloc, saved_realloc, saved_free));
    malloc_fail_countdown = -1;
    ASSERT_EQ(C_ORM_OK, c_orm_string_builder_init(&sb));
    malloc_fail_countdown = 0;
    ASSERT_EQ(C_ORM_ERROR_MEMORY,
              c_orm_quote_qualified_identifier(sb, "tbl.col", '"'));
    malloc_fail_countdown = -1;
    ASSERT_EQ(C_ORM_OK, c_orm_string_builder_free(sb));

    ASSERT_EQ(C_ORM_OK,
              c_orm_set_allocators(saved_malloc, saved_realloc, saved_free));

    /* Append countdown failures inside quote_identifier ('""' escape),
     * quote_qualified_identifier ('*', seg quote, '.' separator), and
     * append_string_literal (opening "'", char, "''", closing "'") */
    ASSERT_EQ(C_ORM_OK, c_orm_string_builder_init(&sb));
    c_orm_mock_string_builder_append_countdown = 1;
    ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_quote_identifier(sb, "\"x", '"'));
    c_orm_mock_string_builder_append_countdown = -1;
    ASSERT_EQ(C_ORM_OK, c_orm_string_builder_free(sb));

    ASSERT_EQ(C_ORM_OK, c_orm_string_builder_init(&sb));
    c_orm_mock_string_builder_append_countdown = 0;
    ASSERT_EQ(C_ORM_ERROR_MEMORY,
              c_orm_quote_qualified_identifier(sb, "*", '"'));
    c_orm_mock_string_builder_append_countdown = -1;
    ASSERT_EQ(C_ORM_OK, c_orm_string_builder_free(sb));

    ASSERT_EQ(C_ORM_OK, c_orm_string_builder_init(&sb));
    c_orm_mock_string_builder_append_countdown = 3;
    /* "a" uses 3 appends ('"', 'a', '"'), then '.' is 4th append (idx 3) */
    ASSERT_EQ(C_ORM_ERROR_MEMORY,
              c_orm_quote_qualified_identifier(sb, "a.b", '"'));
    c_orm_mock_string_builder_append_countdown = -1;
    ASSERT_EQ(C_ORM_OK, c_orm_string_builder_free(sb));

    ASSERT_EQ(C_ORM_OK, c_orm_string_builder_init(&sb));
    c_orm_mock_string_builder_append_countdown = 0;
    ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_append_string_literal(sb, "a"));
    c_orm_mock_string_builder_append_countdown = -1;
    ASSERT_EQ(C_ORM_OK, c_orm_string_builder_free(sb));

    ASSERT_EQ(C_ORM_OK, c_orm_string_builder_init(&sb));
    c_orm_mock_string_builder_append_countdown = 1;
    ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_append_string_literal(sb, "a"));
    c_orm_mock_string_builder_append_countdown = -1;
    ASSERT_EQ(C_ORM_OK, c_orm_string_builder_free(sb));

    ASSERT_EQ(C_ORM_OK, c_orm_string_builder_init(&sb));
    c_orm_mock_string_builder_append_countdown = 1;
    ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_append_string_literal(sb, "'"));
    c_orm_mock_string_builder_append_countdown = -1;
    ASSERT_EQ(C_ORM_OK, c_orm_string_builder_free(sb));

    /* Free error branches in c_orm_string_builder_free */
    ASSERT_EQ(C_ORM_OK, c_orm_string_builder_init(&sb));
    sb_free_call_idx = 0;
    sb_free_fail_mask = 0x03U; /* both buffer free and struct free fail */
    ASSERT_EQ(C_ORM_OK,
              c_orm_set_allocators(saved_malloc, saved_realloc, sb_fail_free));
    ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_string_builder_free(sb));

    ASSERT_EQ(C_ORM_OK, c_orm_string_builder_init(&sb));
    sb_free_call_idx = 0;
    sb_free_fail_mask = 0x02U; /* buffer free succeeds, struct free fails */
    ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_string_builder_free(sb));

    sb_free_fail_mask = 0;
    ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(saved_malloc, sb_ok_null_realloc,
                                             saved_free));
    ASSERT_EQ(C_ORM_OK, c_orm_string_builder_init(&sb));
    sb_ok_null_countdown = 1;
    ASSERT_EQ(C_ORM_OK, c_orm_string_builder_append(sb, "grow_once"));
    ASSERT_EQ(C_ORM_ERROR_MEMORY,
              c_orm_string_builder_append(sb, "grow_twice_fail_with_null"));
    sb_ok_null_countdown = -1;
    ASSERT_EQ(C_ORM_OK,
              c_orm_set_allocators(saved_malloc, saved_realloc, saved_free));
    ASSERT_EQ(C_ORM_OK, c_orm_string_builder_free(sb));
  }
#endif

  PASS();
}

/**
 * @brief String builder test suite runner.
 * @param string_builder_suite Suite runner function name.
 */
SUITE(string_builder_suite) {
  RUN_TEST(test_c_orm_string_builder);
  RUN_TEST(test_quote_identifier_and_safe_crt);
  RUN_TEST(test_qualified_placeholder_and_literal_coverage);
}
