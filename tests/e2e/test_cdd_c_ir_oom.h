/**
 * @file test_cdd_c_ir_oom.h
 * @brief OOM failure simulation tests for CDD C IR operations.
 */

#ifndef TEST_CDD_C_IR_OOM_H
#define TEST_CDD_C_IR_OOM_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "test_assert_zero_branch.h"
/* clang-format on */

/** @brief Counter decremented before triggering simulated malloc/realloc
 * failure. */
static int ir_oom_countdown = 0;

/** @brief Pointer to original malloc allocator. */
static c_orm_malloc_fn_t old_malloc_ir = NULL;

/** @brief Pointer to original realloc allocator. */
static c_orm_realloc_fn_t old_realloc_ir = NULL;

/**
 * @brief Mock malloc callback for triggering out-of-memory errors.
 * @param size Requested allocation size.
 * @param out_ptr Output pointer receiving allocated block or NULL.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on countdown expiry.
 */
static c_orm_error_t mock_malloc_ir(size_t size, void **out_ptr) {
  if (ir_oom_countdown == 0) {
    ir_oom_countdown--;
    if (out_ptr) {
      *out_ptr = NULL;
    }
    return C_ORM_ERROR_MEMORY;
  }
  ir_oom_countdown--;
  return old_malloc_ir(size, out_ptr);
}

/**
 * @brief Mock realloc callback for triggering out-of-memory errors.
 * @param ptr Existing memory pointer.
 * @param size Requested reallocation size.
 * @param out_ptr Output pointer receiving reallocated block or NULL.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on countdown expiry.
 */
static c_orm_error_t mock_realloc_ir(void *ptr, size_t size, void **out_ptr) {
  if (ir_oom_countdown == 0) {
    ir_oom_countdown--;
    if (out_ptr) {
      *out_ptr = NULL;
    }
    return C_ORM_ERROR_MEMORY;
  }
  ir_oom_countdown--;
  return old_realloc_ir(ptr, size, out_ptr);
}

/**
 * @brief Mock realloc returning C_ORM_OK with NULL *out_ptr to exercise
 * defensive NULL check branches in cdd_c_ir.c.
 * @param ptr Existing memory pointer.
 * @param size Requested reallocation size.
 * @param out_ptr Output pointer receiving NULL.
 * @return C_ORM_OK.
 */
static c_orm_error_t mock_ok_null_realloc_ir(void *ptr, size_t size,
                                             void **out_ptr) {
  (void)ptr;
  (void)size;
  *out_ptr = NULL;
  return C_ORM_OK;
}

/**
 * @brief Mock malloc returning C_ORM_OK with NULL *out_ptr when
 * ir_oom_countdown hits 0.
 * @param size Requested allocation size.
 * @param out_ptr Output pointer receiving allocated block or NULL.
 * @return C_ORM_OK or C_ORM_ERROR_MEMORY.
 */
static c_orm_error_t mock_ok_null_malloc_ir(size_t size, void **out_ptr) {
  if (ir_oom_countdown == 0) {
    ir_oom_countdown--;
    *out_ptr = NULL;
    return C_ORM_OK;
  }
  ir_oom_countdown--;
  return old_malloc_ir(size, out_ptr);
}

/**
 * @brief Tests CDD C IR creation and parsing under simulated OOM conditions.
 * @return GREATEST test result.
 */
TEST test_cdd_c_ir_oom(void) {
  cdd_c_ir_t ir;
  struct sql_table_t tbl;
  cdd_c_query_projection_t proj;
  c_orm_error_t rc;
  int i;
  char src_tbl[] = "test";
  char tgt_name[] = "test_map";

  memset(&tbl, 0, sizeof(tbl));
  rc = cdd_c_query_projection_init(&proj);
  ASSERT_EQ(C_ORM_OK, rc);
  proj.source_table = src_tbl;
  proj.mapping_meta.target_name = tgt_name;

  old_malloc_ir = c_orm_malloc;
  old_realloc_ir = c_orm_realloc;

  ir_oom_countdown = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, mock_malloc_ir(16, NULL));
  ir_oom_countdown = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, mock_realloc_ir(NULL, 16, NULL));

  /* add_table realloc fail & defensive NULL realloc */
  rc = cdd_c_ir_init(&ir);
  ASSERT_EQ(C_ORM_OK, rc);
  ir_oom_countdown = 0;
  c_orm_set_allocators(old_malloc_ir, mock_realloc_ir, c_orm_free);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, cdd_c_ir_add_table(&ir, &tbl));

  c_orm_set_allocators(old_malloc_ir, mock_ok_null_realloc_ir, c_orm_free);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, cdd_c_ir_add_table(&ir, &tbl));

  c_orm_set_allocators(old_malloc_ir, old_realloc_ir, c_orm_free);
  rc = cdd_c_ir_free(&ir);
  ASSERT_EQ(C_ORM_OK, rc);

  /* add_projection realloc fail & defensive NULL realloc */
  rc = cdd_c_ir_init(&ir);
  ASSERT_EQ(C_ORM_OK, rc);
  ir_oom_countdown = 0;
  c_orm_set_allocators(old_malloc_ir, mock_realloc_ir, c_orm_free);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, cdd_c_ir_add_projection(&ir, &proj));

  c_orm_set_allocators(old_malloc_ir, mock_ok_null_realloc_ir, c_orm_free);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, cdd_c_ir_add_projection(&ir, &proj));
  c_orm_set_allocators(old_malloc_ir, old_realloc_ir, c_orm_free);

  /* duplicate_projection field alloc fail */
  {
    cdd_c_query_projection_t proj_local;
    cdd_c_query_projection_field_t f;
    char f1_name[] = "f1";
    rc = cdd_c_query_projection_init(&proj_local);
    ASSERT_EQ(C_ORM_OK, rc);
    memset(&f, 0, sizeof(f));
    f.name = f1_name;
    rc = cdd_c_query_projection_add_field(&proj_local, &f);
    ASSERT_EQ(C_ORM_OK, rc);

    ir_oom_countdown = 0; /* fail c_orm_strdup inside add_field */
    c_orm_set_allocators(mock_malloc_ir, old_realloc_ir, c_orm_free);
    ASSERT_EQ(C_ORM_ERROR_MEMORY, cdd_c_ir_add_projection(&ir, &proj_local));

    ir_oom_countdown = 0; /* fail new_fields realloc inside add_field */
    c_orm_set_allocators(old_malloc_ir, mock_realloc_ir, c_orm_free);
    ASSERT_EQ(C_ORM_ERROR_MEMORY, cdd_c_ir_add_projection(&ir, &proj_local));

    c_orm_set_allocators(old_malloc_ir, old_realloc_ir, c_orm_free);
    rc = cdd_c_query_projection_free(&proj_local);
    ASSERT_EQ(C_ORM_OK, rc);
  }

  /* duplicate_projection malloc fail & defensive NULL - source_table */
  ir_oom_countdown = 0;
  c_orm_set_allocators(mock_malloc_ir, old_realloc_ir, c_orm_free);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, cdd_c_ir_add_projection(&ir, &proj));

  ir_oom_countdown = 0;
  c_orm_set_allocators(mock_ok_null_malloc_ir, old_realloc_ir, c_orm_free);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, cdd_c_ir_add_projection(&ir, &proj));

  c_orm_set_allocators(old_malloc_ir, old_realloc_ir, c_orm_free);
  rc = cdd_c_ir_free(&ir);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = cdd_c_ir_init(&ir);
  ASSERT_EQ(C_ORM_OK, rc);

  /* duplicate_projection malloc fail & defensive NULL - target_name */
  ir_oom_countdown = 1; /* skips source_table alloc */
  c_orm_set_allocators(mock_malloc_ir, old_realloc_ir, c_orm_free);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, cdd_c_ir_add_projection(&ir, &proj));

  ir_oom_countdown = 1;
  c_orm_set_allocators(mock_ok_null_malloc_ir, old_realloc_ir, c_orm_free);
  ASSERT_EQ(C_ORM_ERROR_MEMORY, cdd_c_ir_add_projection(&ir, &proj));

  c_orm_set_allocators(old_malloc_ir, old_realloc_ir, c_orm_free);
  rc = cdd_c_ir_free(&ir);
  ASSERT_EQ(C_ORM_OK, rc);

  /* parse_sql_into_ir failure paths across realloc and malloc countdowns */
  rc = cdd_c_ir_init(&ir);
  ASSERT_EQ(C_ORM_OK, rc);
  c_orm_set_allocators(old_malloc_ir, old_realloc_ir, c_orm_free);

  ir_oom_countdown = 2; /* 0: token_list realloc, 1: table->columns realloc, 2:
                           cdd_c_ir_add_table realloc */
  c_orm_set_allocators(old_malloc_ir, mock_realloc_ir, c_orm_free);
  ASSERT(parse_sql_into_ir("CREATE TABLE x (id INT);", &ir) != C_ORM_OK);
  c_orm_set_allocators(old_malloc_ir, old_realloc_ir, c_orm_free);
  rc = cdd_c_ir_free(&ir);
  ASSERT_EQ(C_ORM_OK, rc);

  for (i = 0; i < 6; ++i) {
    rc = cdd_c_ir_init(&ir);
    ASSERT_EQ(C_ORM_OK, rc);
    ir_oom_countdown = i;
    c_orm_set_allocators(old_malloc_ir, mock_realloc_ir, c_orm_free);
    rc = parse_sql_into_ir("SELECT id FROM x;", &ir);
    ASSERT((rc == C_ORM_OK) + (rc == C_ORM_ERROR_MEMORY));
    c_orm_set_allocators(old_malloc_ir, old_realloc_ir, c_orm_free);
    rc = cdd_c_ir_free(&ir);
    ASSERT_EQ(C_ORM_OK, rc);

    rc = cdd_c_ir_init(&ir);
    ASSERT_EQ(C_ORM_OK, rc);
    ir_oom_countdown = i;
    c_orm_set_allocators(mock_malloc_ir, old_realloc_ir, c_orm_free);
    rc = parse_sql_into_ir("SELECT id FROM x;", &ir);
    ASSERT((rc == C_ORM_OK) + (rc == C_ORM_ERROR_MEMORY));
    c_orm_set_allocators(old_malloc_ir, old_realloc_ir, c_orm_free);
    rc = cdd_c_ir_free(&ir);
    ASSERT_EQ(C_ORM_OK, rc);

    rc = cdd_c_ir_init(&ir);
    ASSERT_EQ(C_ORM_OK, rc);
    ir_oom_countdown = i;
    c_orm_set_allocators(old_malloc_ir, mock_realloc_ir, c_orm_free);
    rc = parse_sql_into_ir("INSERT INTO x (id) VALUES (1) RETURNING id;", &ir);
    ASSERT((rc == C_ORM_OK) + (rc == C_ORM_ERROR_MEMORY));
    c_orm_set_allocators(old_malloc_ir, old_realloc_ir, c_orm_free);
    rc = cdd_c_ir_free(&ir);
    ASSERT_EQ(C_ORM_OK, rc);

    rc = cdd_c_ir_init(&ir);
    ASSERT_EQ(C_ORM_OK, rc);
    ir_oom_countdown = i;
    c_orm_set_allocators(mock_malloc_ir, old_realloc_ir, c_orm_free);
    rc = parse_sql_into_ir("INSERT INTO x (id) VALUES (1) RETURNING id;", &ir);
    ASSERT((rc == C_ORM_OK) + (rc == C_ORM_ERROR_MEMORY));
    c_orm_set_allocators(old_malloc_ir, old_realloc_ir, c_orm_free);
    rc = cdd_c_ir_free(&ir);
    ASSERT_EQ(C_ORM_OK, rc);
  }

  rc = cdd_c_ir_init(&ir);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = parse_sql_into_ir("INSERT INTO x (id) VALUES (1) RETURNING id;", &ir);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = cdd_c_ir_free(&ir);
  ASSERT_EQ(C_ORM_OK, rc);

  PASS();
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* TEST_CDD_C_IR_OOM_H */
