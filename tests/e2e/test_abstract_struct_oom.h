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

/* clang-format off */
#include "test_assert_zero_branch.h"
/* clang-format on */

/** @brief Countdown counter before injecting allocation failure. */
static int astruct_oom_countdown = 0;

/** @brief Saved original malloc allocator pointer. */
static c_orm_malloc_fn_t old_malloc = NULL;

/** @brief Saved original realloc allocator pointer. */
static c_orm_realloc_fn_t old_realloc = NULL;

/**
 * @brief Mock malloc allocator with countdown fault injection.
 * @param size Allocation size in bytes.
 * @param out_ptr Output pointer receiving allocated memory or NULL on failure.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on failure.
 */
static c_orm_error_t mock_malloc_astruct(size_t size, void **out_ptr) {
  if (astruct_oom_countdown == 0) {
    astruct_oom_countdown--;
    if (out_ptr) {
      *out_ptr = NULL;
    }
    return C_ORM_ERROR_MEMORY;
  }
  astruct_oom_countdown--;
  return old_malloc(size, out_ptr);
}

/**
 * @brief Mock realloc allocator with countdown fault injection.
 * @param ptr Pointer to existing memory block.
 * @param size New allocation size in bytes.
 * @param out_ptr Output pointer receiving reallocated memory or NULL on
 * failure.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on failure.
 */
static c_orm_error_t mock_realloc_astruct(void *ptr, size_t size,
                                          void **out_ptr) {
  if (astruct_oom_countdown == 0) {
    astruct_oom_countdown--;
    if (out_ptr) {
      *out_ptr = NULL;
    }
    return C_ORM_ERROR_MEMORY;
  }
  astruct_oom_countdown--;
  return old_realloc(ptr, size, out_ptr);
}

/**
 * @brief Parson-compatible mock malloc callback with countdown fault injection.
 * @param size Allocation size in bytes.
 * @return Allocated memory pointer or NULL on failure.
 */
static void *mock_parson_malloc(size_t size) {
  if (astruct_oom_countdown == 0) {
    astruct_oom_countdown--;
    return NULL;
  }
  astruct_oom_countdown--;
  return malloc(size == 0 ? 1 : size);
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
  c_orm_error_t rc;

  json = NULL;
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_init(&astruct));

  old_malloc = c_orm_malloc;
  old_realloc = c_orm_realloc;

  astruct_oom_countdown = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, mock_malloc_astruct(16, NULL));
  astruct_oom_countdown = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, mock_realloc_astruct(NULL, 16, NULL));

  /* test array init malloc fail */
  astruct_oom_countdown = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(mock_malloc_astruct, c_orm_realloc,
                                           c_orm_free));
  rc = cdd_c_abstract_struct_array_init(&arr, 10);
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(old_malloc, old_realloc, c_orm_free));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  /* array push realloc fail */
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_array_init(&arr, 1));
  ASSERT_EQ(C_ORM_OK,
            cdd_c_abstract_struct_array_append(&arr, &astruct)); /* fills cap */
  astruct_oom_countdown = 0;
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(old_malloc, mock_realloc_astruct, c_orm_free));
  rc = cdd_c_abstract_struct_array_append(&arr, &astruct);
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(old_malloc, old_realloc, c_orm_free));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  /* test mock_realloc_astruct with countdown > 0 */
  {
    void *tmp = NULL;
    astruct_oom_countdown = 1;
    ASSERT_EQ(C_ORM_OK, mock_realloc_astruct(NULL, 16, &tmp));
    ASSERT_EQ(C_ORM_OK, c_orm_system_free(tmp));
  }

  /* test mock_malloc_astruct with countdown > 0 */
  {
    void *tmp = NULL;
    astruct_oom_countdown = 1;
    ASSERT_EQ(C_ORM_OK, mock_malloc_astruct(16, &tmp));
    ASSERT_EQ(C_ORM_OK, c_orm_system_free(tmp));
  }

  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_array_free(&arr));

  astruct_oom_countdown = 0;
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(mock_malloc_astruct, old_realloc, c_orm_free));
  rc = cdd_c_abstract_struct_init_with_capacity(&astruct, 10);
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(old_malloc, old_realloc, c_orm_free));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  /* abstract set initial kvs capacity realloc failure */
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_init(&astruct));
  astruct_oom_countdown = 0;
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(old_malloc, mock_realloc_astruct, c_orm_free));
  v.type = CDD_C_VARIANT_TYPE_INT;
  v.value.i_val = 1;
  rc = cdd_c_abstract_set(&astruct, "key", &v);
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(old_malloc, old_realloc, c_orm_free));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  /* duplicate_string failure for key */
  astruct_oom_countdown = 0;
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(mock_malloc_astruct, old_realloc, c_orm_free));
  v.type = CDD_C_VARIANT_TYPE_INT;
  v.value.i_val = 1;
  rc = cdd_c_abstract_set(&astruct, "key", &v);
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(old_malloc, old_realloc, c_orm_free));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_set(&astruct, "key", &v));
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_set(&astruct, "key2", &v));

  /* realloc in abstract_set */
  /* The capacity is initially 4, so we need to add 3 more to force realloc */
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_set(&astruct, "key3", &v));
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_set(&astruct, "key4", &v));
  astruct_oom_countdown = 0;
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(old_malloc, mock_realloc_astruct, c_orm_free));
  rc = cdd_c_abstract_set(&astruct, "key5", &v);
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(old_malloc, old_realloc, c_orm_free));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  /* Expand capacity with default allocators before testing key/value malloc */
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_set(&astruct, "key5", &v));

  /* duplicate_string failure in copy_variant */
  {
    char hello_buf[] = "hello";
    v.type = CDD_C_VARIANT_TYPE_STRING;
    v.value.s_val = hello_buf;
    astruct_oom_countdown = 1;
    ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(mock_malloc_astruct, old_realloc,
                                             c_orm_free));
    rc = cdd_c_abstract_set(&astruct, "key6", &v);
    ASSERT_EQ(C_ORM_OK,
              c_orm_set_allocators(old_malloc, old_realloc, c_orm_free));
    ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  }

  /* duplicate_blob failure in copy_variant */
  {
    unsigned char blob_buf[] = {'d', 'a', 't', 'a'};
    v.type = CDD_C_VARIANT_TYPE_BLOB;
    v.value.b_val.data = blob_buf;
    v.value.b_val.size = 4;
    astruct_oom_countdown = 1;
    ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(mock_malloc_astruct, old_realloc,
                                             c_orm_free));
    rc = cdd_c_abstract_set(&astruct, "key7", &v);
    ASSERT_EQ(C_ORM_OK,
              c_orm_set_allocators(old_malloc, old_realloc, c_orm_free));
    ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  }

  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_free(&astruct));

  /* to_json failure */
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            cdd_c_abstract_struct_array_to_json(NULL, &json));

  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_array_init(&arr, 1));
  ASSERT_EQ(JSONSuccess, json_set_allocation_functions(mock_parson_malloc,
                                                       mock_parson_free));
  astruct_oom_countdown = 0;
  rc = cdd_c_abstract_struct_array_to_json(&arr, &json);
  ASSERT_EQ(JSONSuccess, json_set_allocation_functions(NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  ASSERT_EQ(JSONSuccess, json_set_allocation_functions(mock_parson_malloc,
                                                       mock_parson_free));
  astruct_oom_countdown = 1;
  rc = cdd_c_abstract_struct_array_to_json(&arr, &json);
  ASSERT_EQ(JSONSuccess, json_set_allocation_functions(NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  astruct_oom_countdown = 0;
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(mock_malloc_astruct, old_realloc, c_orm_free));
  rc = cdd_c_abstract_struct_array_to_json(&arr, &json);
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(old_malloc, old_realloc, c_orm_free));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  ASSERT_EQ(JSONSuccess, json_set_allocation_functions(mock_parson_malloc,
                                                       mock_parson_free));
  astruct_oom_countdown = 0;
  rc = cdd_c_abstract_struct_to_json(&astruct, &json);
  ASSERT_EQ(JSONSuccess, json_set_allocation_functions(NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  ASSERT_EQ(JSONSuccess, json_set_allocation_functions(mock_parson_malloc,
                                                       mock_parson_free));
  astruct_oom_countdown = 1;
  rc = cdd_c_abstract_struct_to_json(&astruct, &json);
  ASSERT_EQ(JSONSuccess, json_set_allocation_functions(NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  /* cdd_c_abstract_from_json failure in cdd_c_abstract_set */
  astruct_oom_countdown = 0;
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(mock_malloc_astruct, old_realloc, c_orm_free));
  rc = cdd_c_abstract_struct_from_json("{\"key\": \"val\"}", &astruct);
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(old_malloc, old_realloc, c_orm_free));
  ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);

  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_array_free(&arr));

  /* cdd_c_abstract_hydrate failure in init_with_capacity */
  {
    cdd_c_column_meta_t col;
    void *val_ptr;
    void *row[1];
    val_ptr = &v;
    row[0] = val_ptr;
    col.name = "c";
    col.inferred_type = SQL_TYPE_INT;
    astruct_oom_countdown = 0;
    ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(mock_malloc_astruct, old_realloc,
                                             c_orm_free));
    rc = cdd_c_abstract_hydrate(&astruct, row, &col, 1);
    ASSERT_EQ(C_ORM_OK,
              c_orm_set_allocators(old_malloc, old_realloc, c_orm_free));
    ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  }

  /* cdd_c_abstract_hydrate failure in cdd_c_abstract_set */
  {
    cdd_c_column_meta_t col;
    void *val_ptr;
    void *row[1];
    val_ptr = &v;
    row[0] = val_ptr;
    col.name = "c";
    col.inferred_type = SQL_TYPE_INT;
    astruct_oom_countdown = 1;
    ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(mock_malloc_astruct, old_realloc,
                                             c_orm_free));
    rc = cdd_c_abstract_hydrate(&astruct, row, &col, 1);
    ASSERT_EQ(C_ORM_OK,
              c_orm_set_allocators(old_malloc, old_realloc, c_orm_free));
    ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
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
    astruct_oom_countdown = 0;
    ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(old_malloc, mock_realloc_astruct,
                                             c_orm_free));
    rc = cdd_c_specific_to_abstract(&astruct, &dummy, &meta);
    ASSERT_EQ(C_ORM_OK,
              c_orm_set_allocators(old_malloc, old_realloc, c_orm_free));
    ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
  }

  /* serialize failure on mock malloc */
  {
    cdd_c_abstract_struct_t astruct_empty;
    ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_init(&astruct_empty));
    astruct_oom_countdown = 2;
    ASSERT_EQ(JSONSuccess, json_set_allocation_functions(mock_parson_malloc,
                                                         mock_parson_free));
    rc = cdd_c_abstract_struct_to_json(&astruct_empty, &json);
    ASSERT_EQ(JSONSuccess, json_set_allocation_functions(NULL, NULL));
    ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
    ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_free(&astruct_empty));
  }

  /* restore */
  ASSERT_EQ(C_ORM_OK,
            c_orm_set_allocators(old_malloc, old_realloc, c_orm_free));
  ASSERT_EQ(JSONSuccess, json_set_allocation_functions(NULL, NULL));

  PASS();
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* TEST_ABSTRACT_STRUCT_OOM_H */
