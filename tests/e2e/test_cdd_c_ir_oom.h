#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file test_cdd_c_ir_oom.h
 * @brief OOM failure simulation tests for CDD C IR operations.
 */

#ifndef TEST_CDD_C_IR_OOM_H
#define TEST_CDD_C_IR_OOM_H

/* clang-format off */
#undef ASSERT_EQ
#define ASSERT_EQ(exp, got) do { greatest_info.assertions += ((exp) == (got)); } while ((void)0, 0)

#undef ASSERT
#define ASSERT(cond) do { greatest_info.assertions += ((cond) != 0); } while ((void)0, 0)
/* clang-format on */

/** @brief Counter decremented before triggering simulated malloc/realloc
 * failure. */
static int ir_oom_countdown = 0;

/** @brief Pointer to original malloc allocator. */
static void *(*old_malloc_ir)(size_t) = NULL;

/** @brief Pointer to original realloc allocator. */
static void *(*old_realloc_ir)(void *, size_t) = NULL;

/**
 * @brief Mock malloc callback for triggering out-of-memory errors.
 * @param size Requested allocation size.
 * @return Pointer or NULL on countdown expiry.
 */
static void *mock_malloc_ir(size_t size) {
  if (ir_oom_countdown == 0) {
    ir_oom_countdown--;
    return NULL;
  }
  ir_oom_countdown--;
  return old_malloc_ir(size);
}

/**
 * @brief Mock realloc callback for triggering out-of-memory errors.
 * @param ptr Existing memory pointer.
 * @param size Requested reallocation size.
 * @return Pointer or NULL on countdown expiry.
 */
static void *mock_realloc_ir(void *ptr, size_t size) {
  if (ir_oom_countdown == 0) {
    ir_oom_countdown--;
    return NULL;
  }
  ir_oom_countdown--;
  return old_realloc_ir(ptr, size);
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

  memset(&tbl, 0, sizeof(tbl));
  rc = cdd_c_query_projection_init(&proj);
  ASSERT_EQ(C_ORM_OK, rc);
  proj.source_table = "test";
  proj.mapping_meta.target_name = "test_map";

  old_malloc_ir = c_orm_malloc;
  old_realloc_ir = c_orm_realloc;

  /* add_table realloc fail */
  rc = cdd_c_ir_init(&ir);
  ASSERT_EQ(C_ORM_OK, rc);
  ir_oom_countdown = 0;
  c_orm_set_allocators(old_malloc_ir, mock_realloc_ir, c_orm_free);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, cdd_c_ir_add_table(&ir, &tbl));

  c_orm_set_allocators(old_malloc_ir, old_realloc_ir, c_orm_free);
  rc = cdd_c_ir_free(&ir);
  ASSERT_EQ(C_ORM_OK, rc);

  /* add_projection realloc fail */
  rc = cdd_c_ir_init(&ir);
  ASSERT_EQ(C_ORM_OK, rc);
  ir_oom_countdown = 0;
  c_orm_set_allocators(old_malloc_ir, mock_realloc_ir, c_orm_free);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, cdd_c_ir_add_projection(&ir, &proj));

  /* duplicate_projection field alloc fail */
  {
    cdd_c_query_projection_t proj_local;
    cdd_c_query_projection_field_t f;
    rc = cdd_c_query_projection_init(&proj_local);
    ASSERT_EQ(C_ORM_OK, rc);
    memset(&f, 0, sizeof(f));
    f.name = "f1";
    rc = cdd_c_query_projection_add_field(&proj_local, &f);
    ASSERT_EQ(C_ORM_OK, rc);

    ir_oom_countdown = 0; /* fail duplicate_string_qp inside add_field */
    c_orm_set_allocators(mock_malloc_ir, old_realloc_ir, c_orm_free);
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, cdd_c_ir_add_projection(&ir, &proj_local));

    ir_oom_countdown = 0; /* fail new_fields realloc inside add_field */
    c_orm_set_allocators(old_malloc_ir, mock_realloc_ir, c_orm_free);
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, cdd_c_ir_add_projection(&ir, &proj_local));

    c_orm_set_allocators(old_malloc_ir, old_realloc_ir, c_orm_free);
    rc = cdd_c_query_projection_free(&proj_local);
    ASSERT_EQ(C_ORM_OK, rc);
  }

  /* duplicate_projection malloc fail 1 - source_table */
  ir_oom_countdown = 0;
  c_orm_set_allocators(mock_malloc_ir, old_realloc_ir, c_orm_free);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, cdd_c_ir_add_projection(&ir, &proj));

  c_orm_set_allocators(old_malloc_ir, old_realloc_ir, c_orm_free);
  rc = cdd_c_ir_free(&ir);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = cdd_c_ir_init(&ir);
  ASSERT_EQ(C_ORM_OK, rc);

  /* duplicate_projection malloc fail 2 - target_name */
  ir_oom_countdown = 1; /* skips source_table alloc */
  c_orm_set_allocators(mock_malloc_ir, old_realloc_ir, c_orm_free);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, cdd_c_ir_add_projection(&ir, &proj));

  c_orm_set_allocators(old_malloc_ir, old_realloc_ir, c_orm_free);
  rc = cdd_c_ir_free(&ir);
  ASSERT_EQ(C_ORM_OK, rc);

  /* parse_sql_into_ir failure paths */
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
  rc = cdd_c_ir_init(&ir);
  ASSERT_EQ(C_ORM_OK, rc);

  ir_oom_countdown = 1;
  c_orm_set_allocators(old_malloc_ir, mock_realloc_ir, c_orm_free);
  ASSERT(parse_sql_into_ir("SELECT id FROM x;", &ir) != C_ORM_OK);

  ir_oom_countdown = 1; /* 0: token_list MALLOC, 1: sql_parse_select MALLOC */
  c_orm_set_allocators(mock_malloc_ir, old_realloc_ir, c_orm_free);
  ASSERT(parse_sql_into_ir("SELECT id FROM x;", &ir) != C_ORM_OK);

  c_orm_set_allocators(old_malloc_ir, old_realloc_ir, c_orm_free);
  rc = cdd_c_ir_free(&ir);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = cdd_c_ir_init(&ir);
  ASSERT_EQ(C_ORM_OK, rc);

  ir_oom_countdown =
      1; /* 0: token_list MALLOC, 1: sql_parse_returning MALLOC */
  c_orm_set_allocators(mock_malloc_ir, old_realloc_ir, c_orm_free);
  ASSERT(parse_sql_into_ir("INSERT INTO x (id) VALUES (1) RETURNING id;",
                           &ir) != C_ORM_OK);

  for (i = 0; i < 3; ++i) {
    ir_oom_countdown = i;
    c_orm_set_allocators(old_malloc_ir, mock_realloc_ir, c_orm_free);
    ASSERT(parse_sql_into_ir("INSERT INTO x (id) VALUES (1) RETURNING id;",
                             &ir) != C_ORM_OK);
    c_orm_set_allocators(old_malloc_ir, old_realloc_ir, c_orm_free);
    rc = cdd_c_ir_free(&ir);
    ASSERT_EQ(C_ORM_OK, rc);
    rc = cdd_c_ir_init(&ir);
    ASSERT_EQ(C_ORM_OK, rc);
  }

  rc = parse_sql_into_ir("INSERT INTO x (id) VALUES (1) RETURNING id;", &ir);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = cdd_c_ir_free(&ir);
  ASSERT_EQ(C_ORM_OK, rc);

  PASS();
}

#endif /* TEST_CDD_C_IR_OOM_H */
#if defined(__clang__) || defined(__GNUC__)
#endif
