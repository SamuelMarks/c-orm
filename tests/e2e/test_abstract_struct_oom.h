#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file test_abstract_struct_oom.h
 * @brief Out-of-memory fault injection tests for abstract struct data
 * structures.
 */

#ifndef TEST_ABSTRACT_STRUCT_OOM_H
#define TEST_ABSTRACT_STRUCT_OOM_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** @brief Countdown counter before injecting allocation failure. */
static int astruct_oom_countdown = 0;

/** @brief Flag indicating whether abstract struct OOM simulation is active. */
static int astruct_oom_active = 0;

/** @brief Saved original malloc allocator pointer. */
static void *(*old_malloc)(size_t) = NULL;

/** @brief Saved original realloc allocator pointer. */
static void *(*old_realloc)(void *, size_t) = NULL;

/**
 * @brief Mock malloc allocator with countdown fault injection.
 * @param size Allocation size in bytes.
 * @return Allocated memory pointer or NULL on failure.
 */
static void *mock_malloc_astruct(size_t size) {
  if (astruct_oom_active) {
    if (astruct_oom_countdown == 0) {
      astruct_oom_countdown--;
      return NULL;
    }
    astruct_oom_countdown--;
  }
  return old_malloc ? old_malloc(size) : malloc(size);
}

/**
 * @brief Mock realloc allocator with countdown fault injection.
 * @param ptr Pointer to existing memory block.
 * @param size New allocation size in bytes.
 * @return Reallocated memory pointer or NULL on failure.
 */
static void *mock_realloc_astruct(void *ptr, size_t size) {
  if (astruct_oom_active) {
    if (astruct_oom_countdown == 0) {
      astruct_oom_countdown--;
      return NULL;
    }
    astruct_oom_countdown--;
  }
  return old_realloc ? old_realloc(ptr, size) : realloc(ptr, size);
}

/**
 * @brief Parson-compatible mock free callback.
 * @param ptr Pointer to free.
 */
static void mock_parson_free(void *ptr) { free(ptr); }

/**
 * @brief Forward declaration for test_abstract_struct_oom_coverage.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_abstract_struct_oom_coverage(void);

/**
 * @brief Comprehensive OOM coverage tests for abstract struct operations and
 * serializations.
 * @return GREATEST test result.
 */
TEST test_abstract_struct_oom_coverage(void) {
  cdd_c_abstract_struct_array_t arr;
  cdd_c_abstract_struct_t astruct;
  cdd_c_variant_t v;
  char *json;

  json = NULL;
  cdd_c_abstract_struct_init(&astruct);

  old_malloc = c_orm_malloc;
  old_realloc = c_orm_realloc;

  /* test array init malloc fail */
  astruct_oom_active = 1;
  astruct_oom_countdown = 0;
  c_orm_set_allocators(mock_malloc_astruct, c_orm_realloc, c_orm_free);
  ASSERT_EQ(EINVAL, cdd_c_abstract_struct_array_init(&arr, 10));

  /* array push realloc fail */
  astruct_oom_active = 0;
  c_orm_set_allocators(old_malloc, old_realloc, c_orm_free);
  cdd_c_abstract_struct_array_init(&arr, 1);
  cdd_c_abstract_struct_array_append(&arr, &astruct); /* fills capacity */
  astruct_oom_active = 1;
  astruct_oom_countdown = 0;
  c_orm_set_allocators(old_malloc, mock_realloc_astruct, c_orm_free);
  ASSERT_EQ(EINVAL, cdd_c_abstract_struct_array_append(&arr, &astruct));

  /* test mock_realloc_astruct with countdown > 0 */
  {
    void *tmp;
    astruct_oom_active = 1;
    astruct_oom_countdown = 1;
    tmp = mock_realloc_astruct(NULL, 16);
    free(tmp);
    astruct_oom_active = 0;
  }

  astruct_oom_active = 0;
  c_orm_set_allocators(old_malloc, old_realloc, c_orm_free);
  cdd_c_abstract_struct_array_free(&arr);

  astruct_oom_active = 1;
  astruct_oom_countdown = 0;
  c_orm_set_allocators(mock_malloc_astruct, old_realloc, c_orm_free);
  ASSERT_EQ(EINVAL, cdd_c_abstract_struct_init_with_capacity(&astruct, 10));

  /* abstract set array capacity malloc */
  cdd_c_abstract_struct_init(&astruct);
  astruct_oom_active = 1;
  astruct_oom_countdown = 0;
  c_orm_set_allocators(mock_malloc_astruct, old_realloc, c_orm_free);
  v.type = CDD_C_VARIANT_TYPE_INT;
  v.value.i_val = 1;
  ASSERT_EQ(EINVAL, cdd_c_abstract_set(&astruct, "key", &v));

  astruct_oom_active = 0;
  c_orm_set_allocators(old_malloc, old_realloc, c_orm_free);
  cdd_c_abstract_set(&astruct, "key", &v);
  cdd_c_abstract_set(&astruct, "key2", &v);

  /* realloc in abstract_set */
  astruct_oom_active = 1;
  astruct_oom_countdown = 0;
  c_orm_set_allocators(old_malloc, mock_realloc_astruct, c_orm_free);
  /* The capacity is initially 4, so we need to add 3 more to force realloc */
  cdd_c_abstract_set(&astruct, "key3", &v);
  cdd_c_abstract_set(&astruct, "key4", &v);
  ASSERT_EQ(EINVAL, cdd_c_abstract_set(&astruct, "key5", &v));

  /* duplicate_string failure in copy_variant */
  v.type = CDD_C_VARIANT_TYPE_STRING;
  v.value.s_val = "hello";
  astruct_oom_active = 1;
  astruct_oom_countdown = 1;
  c_orm_set_allocators(mock_malloc_astruct, old_realloc, c_orm_free);
  ASSERT_EQ(EINVAL, cdd_c_abstract_set(&astruct, "key6", &v));

  /* duplicate_blob failure in copy_variant */
  v.type = CDD_C_VARIANT_TYPE_BLOB;
  v.value.b_val.data = (unsigned char *)"data";
  v.value.b_val.size = 4;
  astruct_oom_active = 1;
  astruct_oom_countdown = 1;
  c_orm_set_allocators(mock_malloc_astruct, old_realloc, c_orm_free);
  ASSERT_EQ(EINVAL, cdd_c_abstract_set(&astruct, "key7", &v));

  astruct_oom_active = 0;
  c_orm_set_allocators(old_malloc, old_realloc, c_orm_free);
  cdd_c_abstract_struct_free(&astruct);

  /* to_json failure */
  astruct_oom_active = 1;
  astruct_oom_countdown = 0;
  c_orm_set_allocators(mock_malloc_astruct, old_realloc, c_orm_free);
  json_set_allocation_functions(mock_malloc_astruct, mock_parson_free);
  ASSERT_EQ(EINVAL, cdd_c_abstract_struct_array_to_json(NULL, &json));

  cdd_c_abstract_struct_array_init(&arr, 1);
  astruct_oom_active = 1;
  astruct_oom_countdown = 0;
  ASSERT_EQ(EINVAL, cdd_c_abstract_struct_array_to_json(&arr, &json));

  astruct_oom_active = 1;
  astruct_oom_countdown = 1;
  ASSERT_EQ(EINVAL, cdd_c_abstract_struct_array_to_json(&arr, &json));

  astruct_oom_active = 1;
  astruct_oom_countdown = 0;
  ASSERT_EQ(EINVAL, cdd_c_abstract_struct_to_json(&astruct, &json));

  astruct_oom_active = 1;
  astruct_oom_countdown = 1;
  ASSERT_EQ(EINVAL, cdd_c_abstract_struct_to_json(&astruct, &json));

  /* cdd_c_abstract_from_json failure in cdd_c_abstract_set */
  astruct_oom_active = 1;
  astruct_oom_countdown = 0;
  json_set_allocation_functions(malloc, free);
  c_orm_set_allocators(mock_malloc_astruct, old_realloc, c_orm_free);
  ASSERT_EQ(EINVAL,
            cdd_c_abstract_struct_from_json("{\"key\": \"val\"}", &astruct));

  astruct_oom_active = 0;
  c_orm_set_allocators(old_malloc, old_realloc, c_orm_free);
  cdd_c_abstract_struct_array_free(&arr);

  /* cdd_c_abstract_hydrate failure in init_with_capacity */
  {
    cdd_c_column_meta_t col;
    void *val_ptr;
    void *row[1];
    val_ptr = &v;
    row[0] = val_ptr;
    col.name = "c";
    col.inferred_type = 4;
    astruct_oom_active = 1;
    astruct_oom_countdown = 0;
    c_orm_set_allocators(mock_malloc_astruct, old_realloc, c_orm_free);
    ASSERT_EQ(EINVAL, cdd_c_abstract_hydrate(&astruct, row, &col, 1));
    astruct_oom_active = 0;
    c_orm_set_allocators(old_malloc, old_realloc, c_orm_free);
  }

  /* cdd_c_abstract_hydrate failure in cdd_c_abstract_set */
  {
    cdd_c_column_meta_t col;
    void *val_ptr;
    void *row[1];
    val_ptr = &v;
    row[0] = val_ptr;
    col.name = "c";
    col.inferred_type = 4;
    astruct_oom_active = 1;
    astruct_oom_countdown = 1;
    c_orm_set_allocators(mock_malloc_astruct, old_realloc, c_orm_free);
    ASSERT_EQ(EINVAL, cdd_c_abstract_hydrate(&astruct, row, &col, 1));
    astruct_oom_active = 0;
    c_orm_set_allocators(old_malloc, old_realloc, c_orm_free);
  }

  /* cdd_c_specific_to_abstract failure in cdd_c_abstract_set */
  {
    cdd_c_prop_meta_t prop;
    cdd_c_meta_t meta;
    int dummy;
    dummy = 42;
    memset(&prop, 0, sizeof(prop));
    memset(&meta, 0, sizeof(meta));
    prop.name = "p";
    prop.type = "C_ORM_TYPE_INT32";
    meta.props = &prop;
    meta.num_props = 1;
    astruct_oom_active = 1;
    astruct_oom_countdown = 0;
    c_orm_set_allocators(old_malloc, mock_realloc_astruct, c_orm_free);
    ASSERT_EQ(EINVAL, cdd_c_specific_to_abstract(&astruct, &dummy, &meta));
    astruct_oom_active = 0;
    c_orm_set_allocators(old_malloc, old_realloc, c_orm_free);
  }

  /* serialize failure on mock malloc */
  {
    cdd_c_abstract_struct_t astruct_empty;
    cdd_c_abstract_struct_init(&astruct_empty);
    astruct_oom_active = 1;
    astruct_oom_countdown = 2;
    json_set_allocation_functions(mock_malloc_astruct, mock_parson_free);
    ASSERT_EQ(EINVAL, cdd_c_abstract_struct_to_json(&astruct_empty, &json));
    json_set_allocation_functions(malloc, free);
    astruct_oom_active = 0;
    cdd_c_abstract_struct_free(&astruct_empty);
  }

  /* restore */
  astruct_oom_active = 0;
  c_orm_set_allocators(old_malloc, old_realloc, c_orm_free);
  json_set_allocation_functions(malloc, free);

  PASS();
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* TEST_ABSTRACT_STRUCT_OOM_H */

#if defined(__clang__) || defined(__GNUC__)
#endif
