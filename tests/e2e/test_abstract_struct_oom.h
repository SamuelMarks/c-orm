#if defined(__clang__) || defined(__GNUC__)
#endif

/* clang-format off */
#include <stddef.h>
#include <stdlib.h>
/* clang-format on */

static int astruct_oom_countdown = 0;
static int astruct_oom_active = 0;
static void *(*old_malloc)(size_t) = NULL;
static void *(*old_realloc)(void *, size_t) = NULL;

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

static void mock_parson_free(void *ptr) { free(ptr); }

TEST test_abstract_struct_oom_coverage(void) {
  cdd_c_abstract_struct_array_t arr;
  cdd_c_abstract_struct_t astruct;
  cdd_c_variant_t v;
  char *json = NULL;

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
  v.value.b_val.data = (unsigned char *)test_strdup("data");
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

  /* restore */
  astruct_oom_active = 0;
  c_orm_set_allocators(old_malloc, old_realloc, c_orm_free);
  json_set_allocation_functions(malloc, free);

  PASS();
}

#if defined(__clang__) || defined(__GNUC__)
#endif
