/**
 * @file abstract_struct.c
 * @brief Implementation of CDD_C generic representation.
 *
 * @author Samuel Marks
 */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "abstract_struct.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "c_cdd/json_dom.h"
#include "c_orm_odbc.h"
#include "c_orm_sql.h"
#include "c_orm_string_builder.h"
#include "cdd_c_orm_meta.h"
#if !defined(C_ORM_NO_THREADS)
#if defined(_MSC_VER)
#include <intrin.h>
#elif !defined(_WIN32)
#include <pthread.h>
#endif
#endif
#if defined(USE_SQLITE_LINKED)
#include <sqlite3.h>
#endif
#if !defined(__EMSCRIPTEN__) && defined(USE_LIBPQ_LINKED)
#include <libpq-fe.h>
#endif
#if !defined(__EMSCRIPTEN__) && defined(USE_MYSQL_LINKED)
#include <mysql.h>
#endif
/* clang-format on */

static size_t cdd_c_memory_allocated = 0;
static size_t cdd_c_memory_freed = 0;
#if !defined(C_ORM_NO_THREADS)
#if defined(_WIN32)
static volatile long cdd_c_mem_spinlock = 0;
#else
static pthread_mutex_t cdd_c_mem_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif
#endif

/**
 * @brief Acquires the abstract struct memory accounting lock.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t cdd_c_mem_lock_acquire(void) {
#if !defined(C_ORM_NO_THREADS)
#if defined(_MSC_VER)
  while (_InterlockedCompareExchange(&cdd_c_mem_spinlock, 1L, 0L) != 0L) {
  }
#elif defined(_WIN32)
  while (__sync_val_compare_and_swap(&cdd_c_mem_spinlock, 0L, 1L) != 0L) {
  }
#else
  (void)pthread_mutex_lock(&cdd_c_mem_mutex);
#endif
#endif
  return C_ORM_OK;
}

/**
 * @brief Releases the abstract struct memory accounting lock.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t cdd_c_mem_lock_release(void) {
#if !defined(C_ORM_NO_THREADS)
#if defined(_WIN32)
  cdd_c_mem_spinlock = 0;
#else
  (void)pthread_mutex_unlock(&cdd_c_mem_mutex);
#endif
#endif
  return C_ORM_OK;
}

/**
 * @brief Allocates memory with internal accounting.
 * @param size Number of bytes to allocate.
 * @param out_ptr Pointer to receive the allocated memory.
 * @return 0 on success, non-zero on error.
 */
static c_orm_error_t cdd_c_malloc(size_t size, void **out_ptr) {
  void *ptr = NULL;
  if (size > 2147483647) {
    *out_ptr = NULL;
    return C_ORM_ERROR_MEMORY;
  }
  if (c_orm_system_malloc(size, &ptr) != C_ORM_OK) {
    *out_ptr = NULL;
    return C_ORM_ERROR_MEMORY;
  }
  cdd_c_mem_lock_acquire();
  cdd_c_memory_allocated += size;
  cdd_c_mem_lock_release();
  *out_ptr = ptr;
  return C_ORM_OK;
}

/**
 * @brief Reallocates memory with internal accounting.
 * @param ptr Pointer to previously allocated buffer.
 * @param size New size in bytes.
 * @param out_ptr Pointer to receive the reallocated memory.
 * @return 0 on success, non-zero on error.
 */
static c_orm_error_t cdd_c_realloc(void *ptr, size_t size, void **out_ptr) {
  void *new_ptr = NULL;
  if (c_orm_system_realloc(ptr, size, &new_ptr) != C_ORM_OK) {
    *out_ptr = NULL;
    return C_ORM_ERROR_MEMORY;
  }
  if (!ptr) {
    cdd_c_mem_lock_acquire();
    cdd_c_memory_allocated += size;
    cdd_c_mem_lock_release();
  }
  *out_ptr = new_ptr;
  return C_ORM_OK;
}

/**
 * @brief Frees memory with internal accounting.
 * @param ptr Pointer to buffer to free.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t cdd_c_free(void *ptr) {
  if (ptr) {
    cdd_c_mem_lock_acquire();
    cdd_c_memory_freed++;
    cdd_c_mem_lock_release();
    (void)c_orm_system_free(ptr);
  }
  return C_ORM_OK;
}

/**
 * @brief Gets currently tracked allocated byte count.
 * @param[out] out_bytes Pointer to receive byte count.
 * @return C_ORM_OK or error code.
 */
c_orm_error_t cdd_c_get_allocated_bytes(size_t *out_bytes) {
  if (!out_bytes)
    return C_ORM_ERROR_VALIDATION;
  cdd_c_mem_lock_acquire();
  *out_bytes = cdd_c_memory_allocated;
  return cdd_c_mem_lock_release();
}

/**
 * @brief Gets count of tracked free calls.
 * @param[out] out_calls Pointer to receive call count.
 * @return C_ORM_OK or error code.
 */
c_orm_error_t cdd_c_get_freed_calls(size_t *out_calls) {
  if (!out_calls)
    return C_ORM_ERROR_VALIDATION;
  cdd_c_mem_lock_acquire();
  *out_calls = cdd_c_memory_freed;
  return cdd_c_mem_lock_release();
}

/**
 * @brief Initializes an abstract struct array container.
 * @param[out] arr Array container to initialize.
 * @param[in] capacity Initial element capacity.
 * @return C_ORM_OK or error code.
 */
c_orm_error_t
cdd_c_abstract_struct_array_init(cdd_c_abstract_struct_array_t *arr,
                                 size_t capacity) {
  if (!arr)
    return C_ORM_ERROR_VALIDATION;
  if (capacity > ((size_t)-1) / sizeof(cdd_c_abstract_struct_t))
    return C_ORM_ERROR_VALIDATION;
  arr->items = NULL;
  arr->count = 0;
  arr->capacity = capacity;
  if (capacity > 0) {
    if (cdd_c_malloc(arr->capacity * sizeof(cdd_c_abstract_struct_t),
                     (void **)&arr->items) != C_ORM_OK)
      return C_ORM_ERROR_VALIDATION;
    /* Initialize children to 0 counts so deep frees don't blow up on partial
     * errors */
    memset(arr->items, 0, capacity * sizeof(cdd_c_abstract_struct_t));
  }
  return C_ORM_OK;
}

/**
 * @brief Appends an abstract struct to the array.
 * @param[in,out] arr Array container.
 * @param[in,out] astruct Abstract struct to append.
 * @return C_ORM_OK or error code.
 */
c_orm_error_t
cdd_c_abstract_struct_array_append(cdd_c_abstract_struct_array_t *arr,
                                   cdd_c_abstract_struct_t *astruct) {
  if (!arr || !astruct)
    return C_ORM_ERROR_VALIDATION;
  if (arr->count >= arr->capacity) {
    size_t new_cap;
    cdd_c_abstract_struct_t *new_items = NULL;
    c_orm_error_t err;
    if (arr->capacity > ((size_t)-1) / 2)
      return C_ORM_ERROR_VALIDATION;
    new_cap = arr->capacity == 0 ? 4 : arr->capacity * 2;
    if (new_cap > ((size_t)-1) / sizeof(cdd_c_abstract_struct_t))
      return C_ORM_ERROR_VALIDATION;
    err = cdd_c_realloc(arr->items, new_cap * sizeof(cdd_c_abstract_struct_t),
                        (void **)&new_items);
    if (err != C_ORM_OK)
      return C_ORM_ERROR_VALIDATION;
    arr->items = new_items;
    arr->capacity = new_cap;
  }
  /* Shallow copy the whole struct since it owns its kvs pointer array */
  arr->items[arr->count] = *astruct;
  /* Null out original so consumer doesn't double-free the internal pointer */
  astruct->kvs = NULL;
  astruct->count = 0;
  astruct->capacity = 0;
  arr->count++;
  return C_ORM_OK;
}

/**
 * @brief Frees resources in an abstract struct array.
 * @param[in,out] arr Array container to free.
 * @return C_ORM_OK or error code.
 */
c_orm_error_t
cdd_c_abstract_struct_array_free(cdd_c_abstract_struct_array_t *arr) {
  size_t i;
  if (!arr)
    return C_ORM_ERROR_VALIDATION;
  if (arr->items) {
    for (i = 0; i < arr->count; ++i) {
      (void)cdd_c_abstract_struct_free(&arr->items[i]);
    }
    (void)cdd_c_free(arr->items);
  }
  arr->items = NULL;
  arr->count = 0;
  arr->capacity = 0;
  return C_ORM_OK;
}

/**
 * @brief cdd_c_abstract_struct_array_to_json
 * @param arr arr
 * @param out_json out_json
 */
c_orm_error_t
cdd_c_abstract_struct_array_to_json(const cdd_c_abstract_struct_array_t *arr,
                                    char **out_json) {
  JSON_Value *root_val = NULL;
  JSON_Array *root_arr = NULL;
  cdd_c_error_t jrc;
  JSON_Status jstatus = JSONSuccess;
  size_t i, j;
  if (!arr || !out_json)
    return C_ORM_ERROR_VALIDATION;

  *out_json = NULL;
  if (cdd_json_value_init_array(&root_val) != CDD_C_SUCCESS)
    return C_ORM_ERROR_VALIDATION;
  jrc = cdd_json_value_get_array(root_val, &root_arr);

  for (i = 0; i < arr->count; ++i) {
    JSON_Value *obj_val = NULL;
    JSON_Object *obj = NULL;
    const cdd_c_abstract_struct_t *astruct = &arr->items[i];

    if (cdd_json_value_init_object(&obj_val) != CDD_C_SUCCESS) {
      (void)cdd_json_value_free(root_val);
      return C_ORM_ERROR_VALIDATION;
    }
    jrc = cdd_json_value_get_object(obj_val, &obj);

    for (j = 0; j < astruct->count; ++j) {
      const cdd_c_abstract_struct_kv_t *kv = &astruct->kvs[j];
      switch (kv->value.type) {
      case CDD_C_VARIANT_TYPE_INT:
        jstatus =
            json_object_set_number(obj, kv->key, (double)kv->value.value.i_val);
        break;
      case CDD_C_VARIANT_TYPE_FLOAT:
        jstatus = json_object_set_number(obj, kv->key, kv->value.value.f_val);
        break;
      case CDD_C_VARIANT_TYPE_STRING:
        jstatus = json_object_set_string(obj, kv->key, kv->value.value.s_val);
        break;
      case CDD_C_VARIANT_TYPE_DECIMAL: {
        char dbuf[64];
        if (c_orm_decimal_to_str(&kv->value.value.d_val, dbuf, sizeof(dbuf),
                                 NULL) != C_ORM_OK) {
          (void)cdd_json_value_free(obj_val);
          (void)cdd_json_value_free(root_val);
          return C_ORM_ERROR_VALIDATION;
        }
        jstatus = json_object_set_string(obj, kv->key, dbuf);
        break;
      }
      case CDD_C_VARIANT_TYPE_BLOB:
        jstatus = json_object_set_string(obj, kv->key, "<BLOB>");
        break;
      case CDD_C_VARIANT_TYPE_NULL:
      default:
        jstatus = json_object_set_null(obj, kv->key);
        break;
      }
    }
    jrc = cdd_json_array_append_value(root_arr, obj_val);
  }
  (void)sizeof(jrc);
  (void)sizeof(jstatus);

  if (cdd_json_serialize_to_string_pretty(root_val, out_json) !=
      CDD_C_SUCCESS) {
    (void)cdd_json_value_free(root_val);
    return C_ORM_ERROR_VALIDATION;
  }
  (void)cdd_json_value_free(root_val);
  return C_ORM_OK;
}

c_orm_error_t cdd_c_abstract_struct_init(cdd_c_abstract_struct_t *astruct) {
  return cdd_c_abstract_struct_init_with_capacity(astruct, 0);
}

c_orm_error_t
cdd_c_abstract_struct_init_with_capacity(cdd_c_abstract_struct_t *astruct,
                                         size_t capacity) {
  if (!astruct)
    return C_ORM_ERROR_VALIDATION;
  if (capacity > ((size_t)-1) / sizeof(cdd_c_abstract_struct_kv_t))
    return C_ORM_ERROR_VALIDATION;
  astruct->kvs = NULL;
  astruct->count = 0;
  astruct->capacity = capacity;
  if (capacity > 0) {
    if (cdd_c_malloc(astruct->capacity * sizeof(cdd_c_abstract_struct_kv_t),
                     (void **)&astruct->kvs) != C_ORM_OK)
      return C_ORM_ERROR_VALIDATION;
  }
  return C_ORM_OK;
}

/**
 * @brief Duplicates a C string.
 * @param src Source string to duplicate.
 * @param dest Pointer to receive the duplicated string.
 * @return 0 on success, non-zero on error.
 */
static c_orm_error_t duplicate_string(const char *src, char **dest) {
  size_t len;
  if (!src) {
    *dest = NULL;
    return C_ORM_OK;
  }
  len = strlen(src);
  if (cdd_c_malloc(len + 1, (void **)dest) != C_ORM_OK)
    return C_ORM_ERROR_VALIDATION;
  memcpy(*dest, src, len + 1);
  return C_ORM_OK;
}

/**
 * @brief Duplicates a binary buffer.
 * @param src Source buffer to duplicate.
 * @param size Number of bytes to copy.
 * @param dest Pointer to receive the duplicated buffer.
 * @return 0 on success, non-zero on error.
 */
static c_orm_error_t duplicate_blob(const unsigned char *src, size_t size,
                                    unsigned char **dest) {
  if (!src || size == 0) {
    *dest = NULL;
    return C_ORM_OK;
  }
  if (cdd_c_malloc(size, (void **)dest) != C_ORM_OK)
    return C_ORM_ERROR_VALIDATION;
  memcpy(*dest, src, size);
  return C_ORM_OK;
}

c_orm_error_t cdd_c_variant_free(cdd_c_variant_t *variant) {
  c_orm_error_t rc = C_ORM_OK;
  if (!variant)
    return C_ORM_ERROR_VALIDATION;
  switch (variant->type) {
  case CDD_C_VARIANT_TYPE_STRING:
    if (variant->value.s_val) {
      rc = cdd_c_free(variant->value.s_val);
      variant->value.s_val = NULL;
    }
    break;
  case CDD_C_VARIANT_TYPE_BLOB:
    if (variant->value.b_val.data) {
      rc = cdd_c_free(variant->value.b_val.data);
      variant->value.b_val.data = NULL;
    }
    break;
  default:
    break;
  }
  variant->type = CDD_C_VARIANT_TYPE_NULL;
  return rc;
}

/**
 * @brief Deep copies a variant value.
 * @param dest Destination variant.
 * @param src Source variant.
 * @return 0 on success, non-zero on error.
 */
static c_orm_error_t copy_variant(cdd_c_variant_t *dest,
                                  const cdd_c_variant_t *src) {
  dest->type = src->type;
  switch (src->type) {
  case CDD_C_VARIANT_TYPE_INT:
    dest->value.i_val = src->value.i_val;
    break;
  case CDD_C_VARIANT_TYPE_FLOAT:
    dest->value.f_val = src->value.f_val;
    break;
  case CDD_C_VARIANT_TYPE_DECIMAL:
    dest->value.d_val = src->value.d_val;
    break;
  case CDD_C_VARIANT_TYPE_STRING:
    if (duplicate_string(src->value.s_val, &dest->value.s_val) != C_ORM_OK) {
      return C_ORM_ERROR_VALIDATION;
    }
    break;
  case CDD_C_VARIANT_TYPE_BLOB:
    if (duplicate_blob(src->value.b_val.data, src->value.b_val.size,
                       &dest->value.b_val.data) != C_ORM_OK) {
      return C_ORM_ERROR_VALIDATION;
    }
    dest->value.b_val.size = src->value.b_val.size;
    break;
  case CDD_C_VARIANT_TYPE_NULL:
  default:
    break;
  }
  return C_ORM_OK;
}

/**
 * @brief Computes djb2 hash of a string.
 * @param str Input string.
 * @param out_hash Pointer to receive the computed hash.
 * @return 0 on success.
 */
static c_orm_error_t hash_string(const char *str, unsigned long *out_hash) {
  unsigned long hash = 5381;
  int c;
  while ((c = *str++)) {
    hash = ((hash << 5) + hash) + (unsigned long)c; /* hash * 33 + c */
  }
  *out_hash = hash;
  return C_ORM_OK;
}

c_orm_error_t cdd_c_abstract_set(cdd_c_abstract_struct_t *astruct,
                                 const char *key,
                                 const cdd_c_variant_t *value) {
  size_t i;
  cdd_c_abstract_struct_kv_t *new_kvs = NULL;
  unsigned long khash = 0;
  c_orm_error_t err;
  if (!astruct || !key || !value)
    return C_ORM_ERROR_VALIDATION;

  hash_string(key, &khash);

  for (i = 0; i < astruct->count; ++i) {
    if (astruct->kvs[i].key_hash == khash &&
        strcmp(astruct->kvs[i].key, key) == 0) {
      (void)cdd_c_variant_free(&astruct->kvs[i].value);
      return copy_variant(&astruct->kvs[i].value, value);
    }
  }

  if (astruct->count >= astruct->capacity) {
    size_t new_cap;
    new_cap = astruct->capacity == 0 ? 4 : astruct->capacity * 2;
    err = cdd_c_realloc(astruct->kvs,
                        new_cap * sizeof(cdd_c_abstract_struct_kv_t),
                        (void **)&new_kvs);
    if (err != C_ORM_OK)
      return C_ORM_ERROR_VALIDATION;
    astruct->kvs = new_kvs;
    astruct->capacity = new_cap;
  }

  if (duplicate_string(key, &astruct->kvs[astruct->count].key) != C_ORM_OK) {
    return C_ORM_ERROR_VALIDATION;
  }
  astruct->kvs[astruct->count].key_hash = khash;

  if (copy_variant(&astruct->kvs[astruct->count].value, value) != C_ORM_OK) {
    (void)cdd_c_free(astruct->kvs[astruct->count].key);
    return C_ORM_ERROR_VALIDATION;
  }

  astruct->count++;
  return C_ORM_OK;
}

c_orm_error_t cdd_c_abstract_get(const cdd_c_abstract_struct_t *astruct,
                                 const char *key, cdd_c_variant_t **out_value) {
  size_t i;
  unsigned long khash = 0;
  if (!astruct || !key || !out_value)
    return C_ORM_ERROR_VALIDATION;

  hash_string(key, &khash);

  for (i = 0; i < astruct->count; ++i) {
    if (astruct->kvs[i].key_hash == khash &&
        strcmp(astruct->kvs[i].key, key) == 0) {
      *out_value = &astruct->kvs[i].value;
      return C_ORM_OK;
    }
  }
  return C_ORM_ERROR_VALIDATION; /* Not found */
}

c_orm_error_t
cdd_c_abstract_struct_deep_copy(cdd_c_abstract_struct_t *dest,
                                const cdd_c_abstract_struct_t *src) {
  size_t i;
  if (!dest || !src)
    return C_ORM_ERROR_VALIDATION;

  cdd_c_abstract_struct_init(dest);

  for (i = 0; i < src->count; ++i) {
    if (cdd_c_abstract_set(dest, src->kvs[i].key, &src->kvs[i].value) !=
        C_ORM_OK) {
      (void)cdd_c_abstract_struct_free(dest);
      return C_ORM_ERROR_VALIDATION;
    }
  }
  return C_ORM_OK;
}

c_orm_error_t cdd_c_abstract_struct_free(cdd_c_abstract_struct_t *astruct) {
  size_t i;
  if (!astruct)
    return C_ORM_ERROR_VALIDATION;

  for (i = 0; i < astruct->count; ++i) {
    (void)cdd_c_free(astruct->kvs[i].key);
    (void)cdd_c_variant_free(&astruct->kvs[i].value);
  }
  (void)cdd_c_free(astruct->kvs);
  astruct->kvs = NULL;
  astruct->count = 0;
  astruct->capacity = 0;
  return C_ORM_OK;
}

c_orm_error_t cdd_c_abstract_print(const cdd_c_abstract_struct_t *astruct) {
  size_t i;
  if (!astruct) {
    printf("NULL astruct\n");
    return C_ORM_ERROR_VALIDATION;
  }
  printf("{\n");
  for (i = 0; i < astruct->count; ++i) {
    printf("  \"%s\": ", astruct->kvs[i].key);
    switch (astruct->kvs[i].value.type) {
    case CDD_C_VARIANT_TYPE_NULL:
      printf("null");
      break;
    case CDD_C_VARIANT_TYPE_INT:
      printf(
          "%d",
          (int)astruct->kvs[i].value.value.i_val); /* Simplified for safety */
      break;
    case CDD_C_VARIANT_TYPE_FLOAT:
      printf("%f", astruct->kvs[i].value.value.f_val);
      break;
    case CDD_C_VARIANT_TYPE_STRING:
      printf("\"%s\"", astruct->kvs[i].value.value.s_val);
      break;
    case CDD_C_VARIANT_TYPE_DECIMAL: {
      char dbuf[64];
      if (c_orm_decimal_to_str(&astruct->kvs[i].value.value.d_val, dbuf,
                               sizeof(dbuf), NULL) == C_ORM_OK) {
        printf("\"%s\"", dbuf);
      } else {
        printf("null");
      }
      break;
    }
    case CDD_C_VARIANT_TYPE_BLOB:
      printf("<blob>");
      break;
    default:
      printf("<unknown>");
      break;
    }
    if (i < astruct->count - 1) {
      printf(",");
    }
    printf("\n");
  }
  printf("}\n");
  return C_ORM_OK;
}

c_orm_error_t
cdd_c_abstract_struct_to_json(const cdd_c_abstract_struct_t *astruct,
                              char **out_json) {
  JSON_Value *root_val = NULL;
  JSON_Object *root_obj = NULL;
  cdd_c_error_t jrc;
  JSON_Status jstatus = JSONSuccess;
  size_t i;

  if (!astruct || !out_json)
    return C_ORM_ERROR_VALIDATION;

  *out_json = NULL;
  if (cdd_json_value_init_object(&root_val) != CDD_C_SUCCESS)
    return C_ORM_ERROR_VALIDATION;
  jrc = cdd_json_value_get_object(root_val, &root_obj);
  (void)sizeof(jrc);

  for (i = 0; i < astruct->count; ++i) {
    const cdd_c_abstract_struct_kv_t *kv = &astruct->kvs[i];
    switch (kv->value.type) {
    case CDD_C_VARIANT_TYPE_NULL:
      jstatus = json_object_set_null(root_obj, kv->key);
      break;
    case CDD_C_VARIANT_TYPE_INT:
      jstatus = json_object_set_number(root_obj, kv->key,
                                       (double)kv->value.value.i_val);
      break;
    case CDD_C_VARIANT_TYPE_FLOAT:
      jstatus =
          json_object_set_number(root_obj, kv->key, kv->value.value.f_val);
      break;
    case CDD_C_VARIANT_TYPE_STRING:
      jstatus =
          json_object_set_string(root_obj, kv->key, kv->value.value.s_val);
      break;
    case CDD_C_VARIANT_TYPE_DECIMAL: {
      char dbuf[64];
      if (c_orm_decimal_to_str(&kv->value.value.d_val, dbuf, sizeof(dbuf),
                               NULL) != C_ORM_OK) {
        (void)cdd_json_value_free(root_val);
        return C_ORM_ERROR_VALIDATION;
      }
      jstatus = json_object_set_string(root_obj, kv->key, dbuf);
      break;
    }
    case CDD_C_VARIANT_TYPE_BLOB:
      jstatus = json_object_set_string(root_obj, kv->key, "<blob>");
      break;
    default:
      break;
    }
  }
  (void)sizeof(jstatus);

  if (cdd_json_serialize_to_string(root_val, out_json) != CDD_C_SUCCESS) {
    (void)cdd_json_value_free(root_val);
    return C_ORM_ERROR_VALIDATION;
  }
  (void)cdd_json_value_free(root_val);
  return C_ORM_OK;
}

c_orm_error_t
cdd_c_abstract_struct_from_json(const char *json,
                                cdd_c_abstract_struct_t *out_astruct) {
  JSON_Value *root_val = NULL;
  JSON_Object *root_obj = NULL;
  JSON_Value_Type rtype = JSONError;
  cdd_c_error_t jrc;
  size_t count = 0;
  size_t i;

  if (!json || !out_astruct)
    return C_ORM_ERROR_VALIDATION;

  cdd_c_abstract_struct_init(out_astruct);

  if (cdd_json_parse_string(json, &root_val) != CDD_C_SUCCESS)
    return C_ORM_ERROR_VALIDATION;

  jrc = cdd_json_value_get_type(root_val, &rtype);
  if (rtype != JSONObject) {
    (void)cdd_json_value_free(root_val);
    (void)cdd_c_abstract_struct_free(out_astruct);
    return C_ORM_ERROR_VALIDATION;
  }

  jrc = cdd_json_value_get_object(root_val, &root_obj);
  jrc = cdd_json_object_get_count(root_obj, &count);

  for (i = 0; i < count; ++i) {
    const char *key = NULL;
    JSON_Value *val = NULL;
    JSON_Value_Type vtype = JSONError;
    cdd_c_variant_t variant;

    jrc = cdd_json_object_get_name(root_obj, i, &key);
    jrc = cdd_json_object_get_value_at(root_obj, i, &val);
    jrc = cdd_json_value_get_type(val, &vtype);
    variant.type = CDD_C_VARIANT_TYPE_NULL;

    switch (vtype) {
    case JSONString: {
      const char *jstr = NULL;
      jrc = cdd_json_value_get_string(val, &jstr);
      variant.type = CDD_C_VARIANT_TYPE_STRING;
      memcpy(&variant.value.s_val, &jstr, sizeof(char *));
      break;
    }
    case JSONNumber: {
      double num = 0.0;
      jrc = cdd_json_value_get_number(val, &num);
      if (num == (double)((c_orm_int64_t)num)) {
        variant.type = CDD_C_VARIANT_TYPE_INT;
        variant.value.i_val = (c_orm_int64_t)num;
      } else {
        variant.type = CDD_C_VARIANT_TYPE_FLOAT;
        variant.value.f_val = num;
      }
    } break;
    case JSONBoolean: {
      int bval = 0;
      jrc = cdd_json_value_get_boolean(val, &bval);
      variant.type = CDD_C_VARIANT_TYPE_INT;
      variant.value.i_val = bval ? 1 : 0;
      break;
    }
    case JSONNull:
    default:
      variant.type = CDD_C_VARIANT_TYPE_NULL;
      break;
    }

    if (cdd_c_abstract_set(out_astruct, key, &variant) != C_ORM_OK) {
      (void)cdd_json_value_free(root_val);
      (void)cdd_c_abstract_struct_free(out_astruct);
      return C_ORM_ERROR_VALIDATION;
    }
  }
  (void)sizeof(jrc);

  (void)cdd_json_value_free(root_val);
  return C_ORM_OK;
}

c_orm_error_t cdd_c_abstract_hydrate(cdd_c_abstract_struct_t *out_astruct,
                                     void **row_data,
                                     const cdd_c_column_meta_t *cols,
                                     size_t n_cols) {
  size_t i;
  if (!out_astruct || !row_data || !cols)
    return C_ORM_ERROR_VALIDATION;

  if (cdd_c_abstract_struct_init_with_capacity(out_astruct, n_cols) != C_ORM_OK)
    return C_ORM_ERROR_VALIDATION;

  for (i = 0; i < n_cols; ++i) {
    cdd_c_variant_t variant;
    const cdd_c_column_meta_t *col = &cols[i];
    void *raw_val = row_data[i];

    variant.type = CDD_C_VARIANT_TYPE_NULL;

    if (!raw_val) {
      /* Keep it null */
    } else {
      switch ((enum SqlDataType)col->inferred_type) {
      case SQL_TYPE_SMALLINT:
      case SQL_TYPE_INT:
      case SQL_TYPE_BIGINT:
      case SQL_TYPE_BOOLEAN:
        variant.type = CDD_C_VARIANT_TYPE_INT;
        variant.value.i_val = *(c_orm_int64_t *)raw_val;
        break;
      case SQL_TYPE_FLOAT:
      case SQL_TYPE_DOUBLE:
      case SQL_TYPE_DECIMAL:
        variant.type = CDD_C_VARIANT_TYPE_FLOAT;
        variant.value.f_val = *(double *)raw_val;
        break;
      case SQL_TYPE_NUMERIC:
      case SQL_TYPE_DECFLOAT:
        variant.type = CDD_C_VARIANT_TYPE_DECIMAL;
        variant.value.d_val = *(const c_orm_decimal_t *)raw_val;
        break;
      case SQL_TYPE_VARCHAR:
      case SQL_TYPE_TEXT:
      case SQL_TYPE_CHAR:
      case SQL_TYPE_GRAPHIC:
      case SQL_TYPE_VARGRAPHIC:
      case SQL_TYPE_CLOB:
      case SQL_TYPE_DBCLOB:
      case SQL_TYPE_DATE:
      case SQL_TYPE_TIME:
      case SQL_TYPE_TIMESTAMP:
        variant.type = CDD_C_VARIANT_TYPE_STRING;
        variant.value.s_val = (char *)raw_val;
        break;
      case SQL_TYPE_BINARY:
      case SQL_TYPE_VARBINARY:
      case SQL_TYPE_BLOB: {
        const c_orm_blob_t *b = (const c_orm_blob_t *)raw_val;
        variant.type = CDD_C_VARIANT_TYPE_BLOB;
        variant.value.b_val.data = (uint8_t *)b->data;
        variant.value.b_val.size = b->size;
        break;
      }
      case SQL_TYPE_UNKNOWN:
      default:
        variant.type = CDD_C_VARIANT_TYPE_BLOB;
        variant.value.b_val.data = NULL;
        variant.value.b_val.size = 0;
        break;
      }
    }

    if (cdd_c_abstract_set(out_astruct, col->name, &variant) != C_ORM_OK) {
      (void)cdd_c_abstract_struct_free(out_astruct);
      return C_ORM_ERROR_VALIDATION;
    }
  }

  return C_ORM_OK;
}

c_orm_error_t
c_orm_table_meta_to_column_meta(const c_orm_table_meta_t *table_meta,
                                cdd_c_column_meta_t **out_cols,
                                size_t *out_n_cols) {
  cdd_c_column_meta_t *cols = NULL;
  size_t i;
  c_orm_error_t rc;

  if (!table_meta || !out_cols || !out_n_cols) {
    return C_ORM_ERROR_VALIDATION;
  }

  *out_cols = NULL;
  *out_n_cols = table_meta->num_columns;
  if (table_meta->num_columns == 0) {
    return C_ORM_OK;
  }

  rc = c_orm_system_calloc(table_meta->num_columns, sizeof(cdd_c_column_meta_t),
                           (void **)&cols);
  if (rc != C_ORM_OK) {
    return C_ORM_ERROR_MEMORY;
  }

  for (i = 0; i < table_meta->num_columns; ++i) {
    cols[i].name = table_meta->columns[i].name;
    switch (table_meta->columns[i].type) {
    case C_ORM_TYPE_INT32:
      cols[i].inferred_type = (int)SQL_TYPE_INT;
      break;
    case C_ORM_TYPE_INT64:
      cols[i].inferred_type = (int)SQL_TYPE_BIGINT;
      break;
    case C_ORM_TYPE_FLOAT:
      cols[i].inferred_type = (int)SQL_TYPE_FLOAT;
      break;
    case C_ORM_TYPE_DOUBLE:
      cols[i].inferred_type = (int)SQL_TYPE_DOUBLE;
      break;
    case C_ORM_TYPE_DECIMAL:
      cols[i].inferred_type = (int)SQL_TYPE_NUMERIC;
      break;
    case C_ORM_TYPE_BOOL:
      cols[i].inferred_type = (int)SQL_TYPE_BOOLEAN;
      break;
    case C_ORM_TYPE_BLOB:
      cols[i].inferred_type = (int)SQL_TYPE_BLOB;
      break;
    case C_ORM_TYPE_DATE:
      cols[i].inferred_type = (int)SQL_TYPE_DATE;
      break;
    case C_ORM_TYPE_TIMESTAMP:
      cols[i].inferred_type = (int)SQL_TYPE_TIMESTAMP;
      break;
    case C_ORM_TYPE_STRING:
    default:
      cols[i].inferred_type = (int)SQL_TYPE_TEXT;
      break;
    }
  }

  *out_cols = cols;
  return C_ORM_OK;
}

c_orm_error_t c_orm_free_column_meta(cdd_c_column_meta_t *cols) {
  if (!cols) {
    return C_ORM_OK;
  }
  return c_orm_system_free(cols);
}

c_orm_error_t
cdd_c_abstract_hydrate_sqlite3(cdd_c_abstract_struct_t *out_astruct,
                               void *stmt) {
#if defined(USE_SQLITE_LINKED)
  sqlite3_stmt *s = (sqlite3_stmt *)stmt;
  int i, n_cols;
  c_orm_error_t rc;
  if (!out_astruct || !s)
    return C_ORM_ERROR_VALIDATION;

  rc = cdd_c_abstract_struct_init(out_astruct);
  if (rc != C_ORM_OK)
    return rc;

  n_cols = sqlite3_column_count(s);
  for (i = 0; i < n_cols; ++i) {
    cdd_c_variant_t variant;
    const char *col_name = sqlite3_column_name(s, i);
    int type = sqlite3_column_type(s, i);

    variant.type = CDD_C_VARIANT_TYPE_NULL;

    switch (type) {
    case SQLITE_INTEGER:
      variant.type = CDD_C_VARIANT_TYPE_INT;
      variant.value.i_val = sqlite3_column_int64(s, i);
      break;
    case SQLITE_FLOAT:
      variant.type = CDD_C_VARIANT_TYPE_FLOAT;
      variant.value.f_val = sqlite3_column_double(s, i);
      break;
    case SQLITE_TEXT:
      variant.type = CDD_C_VARIANT_TYPE_STRING;
      /* Note: cdd_c_abstract_set duplicates the string */
      variant.value.s_val = (char *)sqlite3_column_text(s, i);
      break;
    case SQLITE_BLOB:
      variant.type = CDD_C_VARIANT_TYPE_BLOB;
      /* Note: cdd_c_abstract_set duplicates the blob */
      variant.value.b_val.data = (unsigned char *)sqlite3_column_blob(s, i);
      variant.value.b_val.size = (size_t)sqlite3_column_bytes(s, i);
      break;
    case SQLITE_NULL:
    default:
      variant.type = CDD_C_VARIANT_TYPE_NULL;
      break;
    }

    if (cdd_c_abstract_set(out_astruct, col_name, &variant) != C_ORM_OK) {
      (void)cdd_c_abstract_struct_free(out_astruct);
      return C_ORM_ERROR_VALIDATION;
    }
  }

  return C_ORM_OK;
#else
  (void)out_astruct;
  (void)stmt;
  return C_ORM_ERROR_VALIDATION;
#endif
}
c_orm_error_t cdd_c_abstract_hydrate_libpq(cdd_c_abstract_struct_t *out_astruct,
                                           void *res, int row_index) {
#if !defined(__EMSCRIPTEN__) && defined(USE_LIBPQ_LINKED)
  PGresult *pq_res = (PGresult *)res;
  int i, n_cols;
  c_orm_error_t rc;
  if (!out_astruct || !pq_res)
    return C_ORM_ERROR_VALIDATION;

  rc = cdd_c_abstract_struct_init(out_astruct);
  if (rc != C_ORM_OK)
    return rc;

  n_cols = PQnfields(pq_res);
  for (i = 0; i < n_cols; ++i) {
    cdd_c_variant_t variant;
    const char *col_name = PQfname(pq_res, i);
    int is_null = PQgetisnull(pq_res, row_index, i);

    variant.type = CDD_C_VARIANT_TYPE_NULL;

    if (!is_null) {
      unsigned int type = (unsigned int)PQftype(pq_res, i);
      char *val_str = PQgetvalue(pq_res, row_index, i);

      if (type == 20 || type == 21 || type == 23) { /* int8, int2, int4 */
        int64_t parsed_i = 0;
        if (c_orm_strtoint64(val_str, NULL, 10, &parsed_i) != C_ORM_OK) {
          parsed_i = 0;
        }
        variant.type = CDD_C_VARIANT_TYPE_INT;
        variant.value.i_val = parsed_i;
      } else if (type == 16) { /* bool */
        variant.type = CDD_C_VARIANT_TYPE_INT;
        variant.value.i_val = (val_str[0] == 't') ? 1 : 0;
      } else if (type == 700 || type == 701) { /* float4, float8 */
        variant.type = CDD_C_VARIANT_TYPE_FLOAT;
        variant.value.f_val = strtod(val_str, NULL);
      } else if (type == 17) { /* bytea */
        size_t len = 0;
        unsigned char *unescaped =
            PQunescapeBytea((const unsigned char *)val_str, &len);
        variant.type = CDD_C_VARIANT_TYPE_BLOB;
        if (unescaped) {
          c_orm_error_t set_res;
          variant.value.b_val.data = unescaped;
          variant.value.b_val.size = len;
          set_res = cdd_c_abstract_set(out_astruct, col_name, &variant);
          PQfreemem(unescaped);
          if (set_res != C_ORM_OK) {
            (void)cdd_c_abstract_struct_free(out_astruct);
            return C_ORM_ERROR_VALIDATION;
          }
          continue;
        } else {
          variant.value.b_val.data = NULL;
          variant.value.b_val.size = 0;
        }
      } else {
        variant.type = CDD_C_VARIANT_TYPE_STRING;
        variant.value.s_val = val_str;
      }
    }

    if (cdd_c_abstract_set(out_astruct, col_name, &variant) != C_ORM_OK) {
      (void)cdd_c_abstract_struct_free(out_astruct);
      return C_ORM_ERROR_VALIDATION;
    }
  }

  return C_ORM_OK;
#else
  (void)out_astruct;
  (void)res;
  (void)row_index;
  return C_ORM_ERROR_VALIDATION;
#endif
}
c_orm_error_t cdd_c_abstract_hydrate_mysql(cdd_c_abstract_struct_t *out_astruct,
                                           void *row, void *fields,
                                           unsigned int num_fields) {
#if defined(USE_MYSQL_LINKED)
  MYSQL_ROW mysql_row = (MYSQL_ROW)row;
  MYSQL_FIELD *mysql_fields = (MYSQL_FIELD *)fields;
  unsigned int i;
  c_orm_error_t rc;
  if (!out_astruct || !mysql_row || !mysql_fields)
    return C_ORM_ERROR_VALIDATION;

  rc = cdd_c_abstract_struct_init(out_astruct);
  if (rc != C_ORM_OK)
    return rc;

  for (i = 0; i < num_fields; ++i) {
    cdd_c_variant_t variant;
    const char *col_name = mysql_fields[i].name;
    char *val_str = mysql_row[i];
    enum enum_field_types type = mysql_fields[i].type;

    variant.type = CDD_C_VARIANT_TYPE_NULL;

    if (val_str) {
      switch (type) {
      case MYSQL_TYPE_TINY:
      case MYSQL_TYPE_SHORT:
      case MYSQL_TYPE_LONG:
      case MYSQL_TYPE_LONGLONG:
      case MYSQL_TYPE_INT24:
      case MYSQL_TYPE_YEAR:
      case MYSQL_TYPE_BIT: {
        int64_t parsed_i = 0;
        if (c_orm_strtoint64(val_str, NULL, 10, &parsed_i) != C_ORM_OK) {
          parsed_i = 0;
        }
        variant.type = CDD_C_VARIANT_TYPE_INT;
        variant.value.i_val = parsed_i;
        break;
      }
      case MYSQL_TYPE_DECIMAL:
      case MYSQL_TYPE_NEWDECIMAL:
      case MYSQL_TYPE_FLOAT:
      case MYSQL_TYPE_DOUBLE:
        variant.type = CDD_C_VARIANT_TYPE_FLOAT;
        variant.value.f_val = strtod(val_str, NULL);
        break;
      case MYSQL_TYPE_BLOB:
      case MYSQL_TYPE_TINY_BLOB:
      case MYSQL_TYPE_MEDIUM_BLOB:
      case MYSQL_TYPE_LONG_BLOB:
        variant.type = CDD_C_VARIANT_TYPE_BLOB;
        variant.value.b_val.data = (unsigned char *)val_str;
        variant.value.b_val.size = strlen(val_str);
        break;
      default:
        variant.type = CDD_C_VARIANT_TYPE_STRING;
        variant.value.s_val = val_str;
        break;
      }
    }

    if (cdd_c_abstract_set(out_astruct, col_name, &variant) != C_ORM_OK) {
      (void)cdd_c_abstract_struct_free(out_astruct);
      return C_ORM_ERROR_VALIDATION;
    }
  }

  return C_ORM_OK;
#else
  (void)out_astruct;
  (void)row;
  (void)fields;
  (void)num_fields;
  return C_ORM_ERROR_VALIDATION;
#endif
}
c_orm_error_t cdd_c_meta_offsetof(const struct cdd_c_meta *meta,
                                  const char *field, size_t *out_offset) {
  size_t i;
  const cdd_c_meta_t *cmeta = (const cdd_c_meta_t *)meta;
  if (!meta || !field || !out_offset)
    return C_ORM_ERROR_VALIDATION;

  for (i = 0; i < cmeta->num_props; ++i) {
    if (strcmp(cmeta->props[i].name, field) == 0) {
      *out_offset = cmeta->props[i].offset;
      return C_ORM_OK;
    }
  }
  return C_ORM_ERROR_VALIDATION;
}

c_orm_error_t cdd_c_specific_to_abstract(cdd_c_abstract_struct_t *out_astruct,
                                         const void *in_struct,
                                         const struct cdd_c_meta *struct_meta) {
  size_t i;
  const cdd_c_meta_t *meta = (const cdd_c_meta_t *)struct_meta;
  if (!out_astruct || !in_struct || !meta)
    return C_ORM_ERROR_VALIDATION;

  cdd_c_abstract_struct_init(out_astruct);

  for (i = 0; i < meta->num_props; ++i) {
    const cdd_c_prop_meta_t *prop = &meta->props[i];
    cdd_c_variant_t val;
    val.type = CDD_C_VARIANT_TYPE_NULL;

    if (strcmp(prop->type, "C_ORM_TYPE_INT32") == 0) {
      val.type = CDD_C_VARIANT_TYPE_INT;
      memcpy(&val.value.i_val, (const char *)in_struct + prop->offset,
             sizeof(int));
    } else if (strcmp(prop->type, "C_ORM_TYPE_INT64") == 0) {
      val.type = CDD_C_VARIANT_TYPE_INT;
      memcpy(&val.value.i_val, (const char *)in_struct + prop->offset,
             sizeof(c_orm_int64_t));
    } else if (strcmp(prop->type, "C_ORM_TYPE_FLOAT") == 0) {
      val.type = CDD_C_VARIANT_TYPE_FLOAT;
      memcpy(&val.value.f_val, (const char *)in_struct + prop->offset,
             sizeof(float));
    } else if (strcmp(prop->type, "C_ORM_TYPE_DOUBLE") == 0) {
      val.type = CDD_C_VARIANT_TYPE_FLOAT;
      memcpy(&val.value.f_val, (const char *)in_struct + prop->offset,
             sizeof(double));
    } else if (strcmp(prop->type, "C_ORM_TYPE_DECIMAL") == 0) {
      val.type = CDD_C_VARIANT_TYPE_DECIMAL;
      memcpy(&val.value.d_val, (const char *)in_struct + prop->offset,
             sizeof(c_orm_decimal_t));
    } else if (strcmp(prop->type, "C_ORM_TYPE_STRING") == 0) {
      val.type = CDD_C_VARIANT_TYPE_STRING;
      if (prop->length > 0) {
        const char *inline_str = (const char *)in_struct + prop->offset;
        memcpy(&val.value.s_val, &inline_str, sizeof(char *));
      } else {
        memcpy(&val.value.s_val, (const char *)in_struct + prop->offset,
               sizeof(char *));
      }
    }

    if (cdd_c_abstract_set(out_astruct, prop->name, &val) != C_ORM_OK) {
      (void)cdd_c_abstract_struct_free(out_astruct);
      return C_ORM_ERROR_VALIDATION;
    }
  }

  return C_ORM_OK;
}

c_orm_error_t cdd_c_abstract_to_specific(
    void *out_struct, const cdd_c_abstract_struct_t *in_astruct,
    const struct cdd_c_meta *struct_meta, int strict_mapping) {
  size_t i;
  const cdd_c_meta_t *meta = (const cdd_c_meta_t *)struct_meta;
  if (!out_struct || !in_astruct || !meta)
    return C_ORM_ERROR_VALIDATION;

  if (strict_mapping && in_astruct->count != meta->num_props) {
    return C_ORM_ERROR_VALIDATION;
  }

  for (i = 0; i < meta->num_props; ++i) {
    const cdd_c_prop_meta_t *prop = &meta->props[i];
    cdd_c_variant_t *val = NULL;

    if (cdd_c_abstract_get(in_astruct, prop->name, &val) != C_ORM_OK) {
      if (strict_mapping)
        return C_ORM_ERROR_VALIDATION;
      continue; /* Partial mapping allowed */
    }

    if (strcmp(prop->type, "C_ORM_TYPE_INT32") == 0) {
      if (val->type == CDD_C_VARIANT_TYPE_INT) {
        {
          int _v = (int)val->value.i_val;
          memcpy((char *)out_struct + prop->offset, &_v, sizeof(int));
        }
      } else if (strict_mapping)
        return C_ORM_ERROR_VALIDATION;
    } else if (strcmp(prop->type, "C_ORM_TYPE_INT64") == 0) {
      if (val->type == CDD_C_VARIANT_TYPE_INT) {
        memcpy((char *)out_struct + prop->offset, &val->value.i_val,
               sizeof(c_orm_int64_t));
      } else if (strict_mapping)
        return C_ORM_ERROR_VALIDATION;
    } else if (strcmp(prop->type, "C_ORM_TYPE_FLOAT") == 0) {
      if (val->type == CDD_C_VARIANT_TYPE_FLOAT) {
        {
          float _v = (float)val->value.f_val;
          memcpy((char *)out_struct + prop->offset, &_v, sizeof(float));
        }
      } else if (strict_mapping)
        return C_ORM_ERROR_VALIDATION;
    } else if (strcmp(prop->type, "C_ORM_TYPE_DOUBLE") == 0) {
      if (val->type == CDD_C_VARIANT_TYPE_FLOAT) {
        memcpy((char *)out_struct + prop->offset, &val->value.f_val,
               sizeof(double));
      } else if (strict_mapping)
        return C_ORM_ERROR_VALIDATION;
    } else if (strcmp(prop->type, "C_ORM_TYPE_DECIMAL") == 0) {
      if (val->type == CDD_C_VARIANT_TYPE_DECIMAL) {
        memcpy((char *)out_struct + prop->offset, &val->value.d_val,
               sizeof(c_orm_decimal_t));
      } else if (val->type == CDD_C_VARIANT_TYPE_STRING) {
        c_orm_decimal_t parsed_dec;
        c_orm_error_t drc =
            c_orm_decimal_from_str(val->value.s_val, &parsed_dec);
        if (drc != C_ORM_OK)
          return drc;
        memcpy((char *)out_struct + prop->offset, &parsed_dec,
               sizeof(c_orm_decimal_t));
      } else if (strict_mapping)
        return C_ORM_ERROR_VALIDATION;
    } else if (strcmp(prop->type, "C_ORM_TYPE_STRING") == 0) {
      if (val->type == CDD_C_VARIANT_TYPE_STRING) {
        if (prop->length > 0) {
          c_orm_error_t cpy_rc;
          cpy_rc =
              C_ORM_STRNCPY((char *)out_struct + prop->offset, prop->length,
                            val->value.s_val, prop->length - 1);
          if (cpy_rc != C_ORM_OK)
            return cpy_rc;
          ((char *)out_struct + prop->offset)[prop->length - 1] = '\0';
        } else {
          {
            char *_v = NULL;
            c_orm_error_t dup_rc;
            dup_rc = C_ORM_STRDUP(val->value.s_val, &_v);
            if (dup_rc != C_ORM_OK)
              return dup_rc;
            memcpy((char *)out_struct + prop->offset, &_v, sizeof(char *));
          }
        }
      } else if (strict_mapping)
        return C_ORM_ERROR_VALIDATION;
    }
  }

  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t
cdd_c_inspect_schema_sqlite3(void *db, const char *table_name,
                             cdd_c_abstract_struct_array_t *out_schema) {
#if defined(USE_SQLITE_LINKED)
  sqlite3 *sql_db = (sqlite3 *)db;
  sqlite3_stmt *stmt = NULL;
  c_orm_string_builder_t *sb = NULL;
  const char *query = NULL;
  int prep_rc;

  if (!db || !table_name || !out_schema)
    return C_ORM_ERROR_VALIDATION;

  if (c_orm_string_builder_init(&sb) != C_ORM_OK)
    return C_ORM_ERROR_MEMORY;
  if (c_orm_string_builder_append(sb, "PRAGMA table_info(") != C_ORM_OK ||
      c_orm_quote_identifier(sb, table_name, '"') != C_ORM_OK ||
      c_orm_string_builder_append(sb, ")") != C_ORM_OK ||
      c_orm_string_builder_get(sb, &query) != C_ORM_OK) {
    (void)c_orm_string_builder_free(sb);
    return C_ORM_ERROR_VALIDATION;
  }

  prep_rc = sqlite3_prepare_v2(sql_db, query, -1, &stmt, NULL);
  (void)c_orm_string_builder_free(sb);
  if (prep_rc != SQLITE_OK) {
    return C_ORM_ERROR_VALIDATION;
  }

  while (sqlite3_step(stmt) == SQLITE_ROW) {
    cdd_c_abstract_struct_t col_def;
    cdd_c_variant_t v_name, v_type, v_notnull, v_pk;

    if (cdd_c_abstract_struct_init(&col_def) != C_ORM_OK) {
      sqlite3_finalize(stmt);
      return C_ORM_ERROR_MEMORY;
    }

    v_name.type = CDD_C_VARIANT_TYPE_STRING;
    v_name.value.s_val = (char *)sqlite3_column_text(stmt, 1);
    if (cdd_c_abstract_set(&col_def, "name", &v_name) != C_ORM_OK) {
      (void)cdd_c_abstract_struct_free(&col_def);
      sqlite3_finalize(stmt);
      return C_ORM_ERROR_MEMORY;
    }

    v_type.type = CDD_C_VARIANT_TYPE_STRING;
    v_type.value.s_val = (char *)sqlite3_column_text(stmt, 2);
    if (cdd_c_abstract_set(&col_def, "type", &v_type) != C_ORM_OK) {
      (void)cdd_c_abstract_struct_free(&col_def);
      sqlite3_finalize(stmt);
      return C_ORM_ERROR_MEMORY;
    }

    v_notnull.type = CDD_C_VARIANT_TYPE_INT;
    v_notnull.value.i_val = sqlite3_column_int(stmt, 3);
    if (cdd_c_abstract_set(&col_def, "notnull", &v_notnull) != C_ORM_OK) {
      (void)cdd_c_abstract_struct_free(&col_def);
      sqlite3_finalize(stmt);
      return C_ORM_ERROR_MEMORY;
    }

    v_pk.type = CDD_C_VARIANT_TYPE_INT;
    v_pk.value.i_val = sqlite3_column_int(stmt, 5);
    if (cdd_c_abstract_set(&col_def, "pk", &v_pk) != C_ORM_OK) {
      (void)cdd_c_abstract_struct_free(&col_def);
      sqlite3_finalize(stmt);
      return C_ORM_ERROR_MEMORY;
    }

    if (cdd_c_abstract_struct_array_append(out_schema, &col_def) != C_ORM_OK) {
      (void)cdd_c_abstract_struct_free(&col_def);
      sqlite3_finalize(stmt);
      return C_ORM_ERROR_MEMORY;
    }
  }
  sqlite3_finalize(stmt);
  return C_ORM_OK;
#else
  (void)db;
  (void)table_name;
  (void)out_schema;
  return C_ORM_ERROR_VALIDATION;
#endif
}

C_ORM_EXPORT c_orm_error_t
cdd_c_inspect_schema_libpq(void *conn, const char *table_name,
                           cdd_c_abstract_struct_array_t *out_schema) {
#if !defined(__EMSCRIPTEN__) && defined(USE_LIBPQ_LINKED)
  PGconn *pq_conn = (PGconn *)conn;
  PGresult *res;
  const char *param_vals[1];
  int i, num_rows;

  if (!conn || !table_name || !out_schema || table_name[0] == '\0')
    return C_ORM_ERROR_VALIDATION;

  param_vals[0] = table_name;
  res = PQexecParams(pq_conn,
                     "SELECT column_name, data_type, is_nullable FROM "
                     "information_schema.columns WHERE table_name = $1 "
                     "ORDER BY ordinal_position;",
                     1, NULL, param_vals, NULL, NULL, 0);
  if (PQresultStatus(res) != PGRES_TUPLES_OK) {
    PQclear(res);
    return C_ORM_ERROR_VALIDATION;
  }

  num_rows = PQntuples(res);
  for (i = 0; i < num_rows; i++) {
    cdd_c_abstract_struct_t col_def;
    cdd_c_variant_t v_name, v_type, v_notnull;
    char *name_val, *type_val, *is_nullable_val;

    if (cdd_c_abstract_struct_init(&col_def) != C_ORM_OK) {
      PQclear(res);
      return C_ORM_ERROR_MEMORY;
    }

    name_val = PQgetvalue(res, i, 0);
    type_val = PQgetvalue(res, i, 1);
    is_nullable_val = PQgetvalue(res, i, 2);

    v_name.type = CDD_C_VARIANT_TYPE_STRING;
    v_name.value.s_val = name_val ? name_val : (char *)"";
    if (cdd_c_abstract_set(&col_def, "name", &v_name) != C_ORM_OK) {
      (void)cdd_c_abstract_struct_free(&col_def);
      PQclear(res);
      return C_ORM_ERROR_MEMORY;
    }

    v_type.type = CDD_C_VARIANT_TYPE_STRING;
    v_type.value.s_val = type_val ? type_val : (char *)"";
    if (cdd_c_abstract_set(&col_def, "type", &v_type) != C_ORM_OK) {
      (void)cdd_c_abstract_struct_free(&col_def);
      PQclear(res);
      return C_ORM_ERROR_MEMORY;
    }

    v_notnull.type = CDD_C_VARIANT_TYPE_INT;
    v_notnull.value.i_val =
        (is_nullable_val && strcmp(is_nullable_val, "YES") != 0) ? 1 : 0;
    if (cdd_c_abstract_set(&col_def, "notnull", &v_notnull) != C_ORM_OK) {
      (void)cdd_c_abstract_struct_free(&col_def);
      PQclear(res);
      return C_ORM_ERROR_MEMORY;
    }

    if (cdd_c_abstract_struct_array_append(out_schema, &col_def) != C_ORM_OK) {
      (void)cdd_c_abstract_struct_free(&col_def);
      PQclear(res);
      return C_ORM_ERROR_MEMORY;
    }
  }
  PQclear(res);
  return C_ORM_OK;
#else
  (void)conn;
  (void)table_name;
  (void)out_schema;
  return C_ORM_ERROR_VALIDATION;
#endif
}

C_ORM_EXPORT c_orm_error_t
cdd_c_inspect_schema_mysql(void *conn, const char *table_name,
                           cdd_c_abstract_struct_array_t *out_schema) {
#if defined(USE_MYSQL_LINKED)
  MYSQL *mysql_conn = (MYSQL *)conn;
  MYSQL_RES *res;
  MYSQL_ROW row;
  c_orm_string_builder_t *sb = NULL;
  const char *query = NULL;
  int qrc;

  if (!conn || !table_name || !out_schema)
    return C_ORM_ERROR_VALIDATION;

  if (c_orm_string_builder_init(&sb) != C_ORM_OK)
    return C_ORM_ERROR_MEMORY;
  if (c_orm_string_builder_append(sb, "SHOW COLUMNS FROM ") != C_ORM_OK ||
      c_orm_quote_identifier(sb, table_name, '`') != C_ORM_OK ||
      c_orm_string_builder_get(sb, &query) != C_ORM_OK) {
    (void)c_orm_string_builder_free(sb);
    return C_ORM_ERROR_VALIDATION;
  }

  qrc = mysql_query(mysql_conn, query);
  (void)c_orm_string_builder_free(sb);
  if (qrc) {
    return C_ORM_ERROR_VALIDATION;
  }

  res = mysql_store_result(mysql_conn);
  if (!res) {
    return C_ORM_ERROR_VALIDATION;
  }

  while ((row = mysql_fetch_row(res))) {
    cdd_c_abstract_struct_t col_def;
    cdd_c_variant_t v_name, v_type, v_notnull, v_pk;
    char *name_val = row[0];
    char *type_val = row[1];
    char *null_val = row[2]; /* YES or NO */
    char *key_val = row[3];  /* PRI, MUL, etc */

    if (cdd_c_abstract_struct_init(&col_def) != C_ORM_OK) {
      mysql_free_result(res);
      return C_ORM_ERROR_MEMORY;
    }

    v_name.type = CDD_C_VARIANT_TYPE_STRING;
    v_name.value.s_val = name_val ? name_val : (char *)"";
    if (cdd_c_abstract_set(&col_def, "name", &v_name) != C_ORM_OK) {
      (void)cdd_c_abstract_struct_free(&col_def);
      mysql_free_result(res);
      return C_ORM_ERROR_MEMORY;
    }

    v_type.type = CDD_C_VARIANT_TYPE_STRING;
    v_type.value.s_val = type_val ? type_val : (char *)"";
    if (cdd_c_abstract_set(&col_def, "type", &v_type) != C_ORM_OK) {
      (void)cdd_c_abstract_struct_free(&col_def);
      mysql_free_result(res);
      return C_ORM_ERROR_MEMORY;
    }

    v_notnull.type = CDD_C_VARIANT_TYPE_INT;
    v_notnull.value.i_val = (null_val && strcmp(null_val, "YES") != 0) ? 1 : 0;
    if (cdd_c_abstract_set(&col_def, "notnull", &v_notnull) != C_ORM_OK) {
      (void)cdd_c_abstract_struct_free(&col_def);
      mysql_free_result(res);
      return C_ORM_ERROR_MEMORY;
    }

    v_pk.type = CDD_C_VARIANT_TYPE_INT;
    v_pk.value.i_val = (key_val && strcmp(key_val, "PRI") == 0) ? 1 : 0;
    if (cdd_c_abstract_set(&col_def, "pk", &v_pk) != C_ORM_OK) {
      (void)cdd_c_abstract_struct_free(&col_def);
      mysql_free_result(res);
      return C_ORM_ERROR_MEMORY;
    }

    if (cdd_c_abstract_struct_array_append(out_schema, &col_def) != C_ORM_OK) {
      (void)cdd_c_abstract_struct_free(&col_def);
      mysql_free_result(res);
      return C_ORM_ERROR_MEMORY;
    }
  }
  mysql_free_result(res);
  return C_ORM_OK;
#else
  (void)conn;
  (void)table_name;
  (void)out_schema;
  return C_ORM_ERROR_VALIDATION;
#endif
}

C_ORM_EXPORT c_orm_error_t
cdd_c_inspect_schema_odbc(c_orm_db_t *db, const char *table_name,
                          cdd_c_abstract_struct_array_t *out_schema) {
  c_orm_odbc_column_info_t *cols = NULL;
  size_t count = 0;
  size_t i;
  c_orm_error_t rc;

  if (!db || !table_name || !out_schema) {
    return C_ORM_ERROR_VALIDATION;
  }
  rc = c_orm_odbc_list_columns(db, NULL, table_name, &cols, &count);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (cdd_c_abstract_struct_array_init(out_schema, count > 0 ? count : 4) !=
      C_ORM_OK) {
    c_orm_odbc_free_column_list(cols, count);
    return C_ORM_ERROR_MEMORY;
  }

  for (i = 0; i < count; ++i) {
    cdd_c_abstract_struct_t col_def;
    cdd_c_variant_t v_name;
    cdd_c_variant_t v_type;
    cdd_c_variant_t v_len;
    cdd_c_variant_t v_scale;
    cdd_c_variant_t v_notnull;

    cdd_c_abstract_struct_init(&col_def);
    v_name.type = CDD_C_VARIANT_TYPE_STRING;
    v_name.value.s_val = cols[i].name;
    v_type.type = CDD_C_VARIANT_TYPE_STRING;
    v_type.value.s_val = cols[i].type_name;
    v_len.type = CDD_C_VARIANT_TYPE_INT;
    v_len.value.i_val = (c_orm_int64_t)cols[i].column_size;
    v_scale.type = CDD_C_VARIANT_TYPE_INT;
    v_scale.value.i_val = (c_orm_int64_t)cols[i].decimal_digits;
    v_notnull.type = CDD_C_VARIANT_TYPE_INT;
    v_notnull.value.i_val = cols[i].is_nullable ? 0 : 1;

    if (cdd_c_abstract_set(&col_def, "name", &v_name) != C_ORM_OK ||
        cdd_c_abstract_set(&col_def, "type", &v_type) != C_ORM_OK ||
        cdd_c_abstract_set(&col_def, "length", &v_len) != C_ORM_OK ||
        cdd_c_abstract_set(&col_def, "scale", &v_scale) != C_ORM_OK ||
        cdd_c_abstract_set(&col_def, "notnull", &v_notnull) != C_ORM_OK) {
      (void)cdd_c_abstract_struct_free(&col_def);
      c_orm_odbc_free_column_list(cols, count);
      return C_ORM_ERROR_MEMORY;
    }
    cdd_c_abstract_struct_array_append(out_schema, &col_def);
  }

  c_orm_odbc_free_column_list(cols, count);
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t
cdd_c_inspect_schema_db2(c_orm_db_t *db, const char *table_name,
                         cdd_c_abstract_struct_array_t *out_schema) {
  c_orm_query_t *q = NULL;
  int has_row = 0;
  c_orm_error_t rc;
  c_orm_error_t fin_rc;
  const char *luw_sql =
      "SELECT COLNAME, TYPENAME, LENGTH, SCALE, NULLS FROM SYSCAT.COLUMNS "
      "WHERE TABNAME = ? ORDER BY COLNO ASC";
  const char *zos_sql =
      "SELECT NAME, COLTYPE, LENGTH, SCALE, NULLS FROM SYSIBM.SYSCOLUMNS "
      "WHERE TBNAME = ? ORDER BY COLNO ASC";

  if (!db || !db->vtable || !table_name || !out_schema) {
    return C_ORM_ERROR_VALIDATION;
  }

  rc = db->vtable->prepare(db, luw_sql, &q);
  if (rc == C_ORM_OK) {
    rc = db->vtable->bind_string(q, 1, table_name);
    if (rc == C_ORM_OK) {
      rc = db->vtable->step(q, &has_row);
    }
    if (rc != C_ORM_OK) {
      fin_rc = db->vtable->finalize(q);
      if (fin_rc != C_ORM_OK) {
        return fin_rc;
      }
      q = NULL;
    }
  } else {
    q = NULL;
  }

  if (!q) {
    rc = db->vtable->prepare(db, zos_sql, &q);
    if (rc == C_ORM_OK) {
      rc = db->vtable->bind_string(q, 1, table_name);
      if (rc == C_ORM_OK) {
        rc = db->vtable->step(q, &has_row);
      }
      if (rc != C_ORM_OK) {
        fin_rc = db->vtable->finalize(q);
        if (fin_rc != C_ORM_OK) {
          return fin_rc;
        }
        q = NULL;
      }
    } else {
      q = NULL;
    }
  }

  if (!q) {
    return cdd_c_inspect_schema_odbc(db, table_name, out_schema);
  }

  if (cdd_c_abstract_struct_array_init(out_schema, 8) != C_ORM_OK) {
    fin_rc = db->vtable->finalize(q);
    return (fin_rc != C_ORM_OK) ? fin_rc : C_ORM_ERROR_MEMORY;
  }

  while (has_row) {
    const char *cname = NULL;
    const char *tname = NULL;
    const char *nulls = NULL;
    int32_t clen = 0;
    int32_t cscale = 0;
    cdd_c_abstract_struct_t col_def;
    cdd_c_variant_t v_name;
    cdd_c_variant_t v_type;
    cdd_c_variant_t v_len;
    cdd_c_variant_t v_scale;
    cdd_c_variant_t v_notnull;

    if (db->vtable->get_string(q, 0, &cname) != C_ORM_OK) {
      cname = NULL;
    }
    if (db->vtable->get_string(q, 1, &tname) != C_ORM_OK) {
      tname = NULL;
    }
    if (db->vtable->get_int32(q, 2, &clen) != C_ORM_OK) {
      clen = 0;
    }
    if (db->vtable->get_int32(q, 3, &cscale) != C_ORM_OK) {
      cscale = 0;
    }
    if (db->vtable->get_string(q, 4, &nulls) != C_ORM_OK) {
      nulls = NULL;
    }

    if (cname && tname) {
      cdd_c_abstract_struct_init(&col_def);
      v_name.type = CDD_C_VARIANT_TYPE_STRING;
      v_name.value.s_val = (char *)(size_t)cname;
      v_type.type = CDD_C_VARIANT_TYPE_STRING;
      v_type.value.s_val = (char *)(size_t)tname;
      v_len.type = CDD_C_VARIANT_TYPE_INT;
      v_len.value.i_val = (c_orm_int64_t)clen;
      v_scale.type = CDD_C_VARIANT_TYPE_INT;
      v_scale.value.i_val = (c_orm_int64_t)cscale;
      v_notnull.type = CDD_C_VARIANT_TYPE_INT;
      v_notnull.value.i_val = (nulls && nulls[0] == 'N') ? 1 : 0;

      if (cdd_c_abstract_set(&col_def, "name", &v_name) != C_ORM_OK ||
          cdd_c_abstract_set(&col_def, "type", &v_type) != C_ORM_OK ||
          cdd_c_abstract_set(&col_def, "length", &v_len) != C_ORM_OK ||
          cdd_c_abstract_set(&col_def, "scale", &v_scale) != C_ORM_OK ||
          cdd_c_abstract_set(&col_def, "notnull", &v_notnull) != C_ORM_OK ||
          cdd_c_abstract_struct_array_append(out_schema, &col_def) !=
              C_ORM_OK) {
        (void)cdd_c_abstract_struct_free(&col_def);
        fin_rc = db->vtable->finalize(q);
        return (fin_rc != C_ORM_OK) ? fin_rc : C_ORM_ERROR_MEMORY;
      }
    }

    rc = db->vtable->step(q, &has_row);
    if (rc != C_ORM_OK) {
      fin_rc = db->vtable->finalize(q);
      return (fin_rc != C_ORM_OK) ? fin_rc : rc;
    }
  }

  return db->vtable->finalize(q);
}
