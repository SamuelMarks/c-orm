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

#if !defined(C_ORM_NO_THREADS)
#if defined(_MSC_VER)
/** @brief Acquires the abstract struct memory accounting lock. */
#define CDD_C_MEM_LOCK()                                                       \
  do {                                                                         \
    while (_InterlockedCompareExchange(&cdd_c_mem_spinlock, 1L, 0L) != 0L) {   \
    }                                                                          \
  } while (0)
/** @brief Releases the abstract struct memory accounting lock. */
#define CDD_C_MEM_UNLOCK()                                                     \
  do {                                                                         \
    cdd_c_mem_spinlock = 0;                                                    \
  } while (0)
#elif defined(_WIN32)
/** @brief Acquires the abstract struct memory accounting lock. */
#define CDD_C_MEM_LOCK()                                                       \
  do {                                                                         \
    while (__sync_val_compare_and_swap(&cdd_c_mem_spinlock, 0L, 1L) != 0L) {   \
    }                                                                          \
  } while (0)
/** @brief Releases the abstract struct memory accounting lock. */
#define CDD_C_MEM_UNLOCK()                                                     \
  do {                                                                         \
    cdd_c_mem_spinlock = 0;                                                    \
  } while (0)
#else
/** @brief Acquires the abstract struct memory accounting lock. */
#define CDD_C_MEM_LOCK()                                                       \
  do {                                                                         \
    pthread_mutex_lock(&cdd_c_mem_mutex);                                      \
  } while (0)
/** @brief Releases the abstract struct memory accounting lock. */
#define CDD_C_MEM_UNLOCK()                                                     \
  do {                                                                         \
    pthread_mutex_unlock(&cdd_c_mem_mutex);                                    \
  } while (0)
#endif
#else
/** @brief No-op memory accounting lock acquire in single-threaded builds. */
#define CDD_C_MEM_LOCK()                                                       \
  do {                                                                         \
  } while (0)
/** @brief No-op memory accounting lock release in single-threaded builds. */
#define CDD_C_MEM_UNLOCK()                                                     \
  do {                                                                         \
  } while (0)
#endif

/** @brief Zero-initializes a validated non-NULL abstract struct in place. */
#define CDD_C_ASTRUCT_ZERO_INIT(s)                                             \
  do {                                                                         \
    (s)->kvs = NULL;                                                           \
    (s)->count = 0;                                                            \
    (s)->capacity = 0;                                                         \
  } while (0)

/**
 * @brief Allocates memory with internal accounting.
 * @param size Number of bytes to allocate.
 * @param out_ptr Pointer to receive the allocated memory.
 * @return C_ORM_OK on success, C_ORM_ERROR_VALIDATION on error.
 */
static c_orm_error_t cdd_c_malloc(size_t size, void **out_ptr) {
  void *ptr = NULL;
  c_orm_error_t rc;
  if (size > 2147483647) {
    *out_ptr = NULL;
    return C_ORM_ERROR_VALIDATION;
  }
  rc = c_orm_system_malloc(size, &ptr);
  if (rc != C_ORM_OK) {
    *out_ptr = NULL;
    return rc;
  }
  CDD_C_MEM_LOCK();
  cdd_c_memory_allocated += size;
  CDD_C_MEM_UNLOCK();
  *out_ptr = ptr;
  return C_ORM_OK;
}

/**
 * @brief Reallocates memory with internal accounting.
 * @param ptr Pointer to previously allocated buffer.
 * @param size New size in bytes.
 * @param out_ptr Pointer to receive the reallocated memory.
 * @return C_ORM_OK on success, C_ORM_ERROR_VALIDATION on size limit, or
 * C_ORM_ERROR_MEMORY on allocation failure.
 */
static c_orm_error_t cdd_c_realloc(void *ptr, size_t size, void **out_ptr) {
  void *new_ptr = NULL;
  c_orm_error_t rc;
  if (size > 2147483647) {
    *out_ptr = NULL;
    return C_ORM_ERROR_VALIDATION;
  }
  rc = c_orm_system_realloc(ptr, size, &new_ptr);
  if (rc != C_ORM_OK) {
    *out_ptr = NULL;
    return rc;
  }
  if (!ptr) {
    CDD_C_MEM_LOCK();
    cdd_c_memory_allocated += size;
    CDD_C_MEM_UNLOCK();
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
    CDD_C_MEM_LOCK();
    cdd_c_memory_freed++;
    CDD_C_MEM_UNLOCK();
    return c_orm_system_free(ptr);
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
  CDD_C_MEM_LOCK();
  *out_bytes = cdd_c_memory_allocated;
  CDD_C_MEM_UNLOCK();
  return C_ORM_OK;
}

/**
 * @brief Gets count of tracked free calls.
 * @param[out] out_calls Pointer to receive call count.
 * @return C_ORM_OK or error code.
 */
c_orm_error_t cdd_c_get_freed_calls(size_t *out_calls) {
  if (!out_calls)
    return C_ORM_ERROR_VALIDATION;
  CDD_C_MEM_LOCK();
  *out_calls = cdd_c_memory_freed;
  CDD_C_MEM_UNLOCK();
  return C_ORM_OK;
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
  c_orm_error_t rc;
  if (!arr)
    return C_ORM_ERROR_VALIDATION;
  if (capacity > ((size_t)-1) / sizeof(cdd_c_abstract_struct_t))
    return C_ORM_ERROR_VALIDATION;
  arr->items = NULL;
  arr->count = 0;
  arr->capacity = capacity;
  if (capacity > 0) {
    rc = cdd_c_malloc(arr->capacity * sizeof(cdd_c_abstract_struct_t),
                      (void **)&arr->items);
    if (rc != C_ORM_OK)
      return rc;
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
      return err;
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
    cdd_c_abstract_struct_t *items = arr->items;
    size_t count = arr->count;
    arr->items = NULL;
    arr->count = 0;
    arr->capacity = 0;
    for (i = 0; i < count; ++i) {
      cdd_c_abstract_struct_free(&items[i]);
    }
    return cdd_c_free(items);
  }
  arr->items = NULL;
  arr->count = 0;
  arr->capacity = 0;
  return C_ORM_OK;
}

/**
 * @brief Frees a `JSON_Value` tree and returns `C_ORM_OK`.
 * @param val Root JSON value to free (safe if NULL).
 * @return `C_ORM_OK`.
 */
static c_orm_error_t abs_json_value_free(JSON_Value *val) {
  int st = (int)cdd_json_value_free(val);
  return (c_orm_error_t)((int)C_ORM_OK + (st * 0));
}

/**
 * @brief Frees a serialized JSON string and returns `C_ORM_OK`.
 * @param str Serialized JSON string to free (safe if NULL).
 * @return `C_ORM_OK`.
 */
static c_orm_error_t abs_json_str_free(char *str) {
  int st = (int)cdd_json_free_serialized_string(str);
  return (c_orm_error_t)((int)C_ORM_OK + (st * 0));
}

/**
 * @brief Serializes an array of abstract structs to a pretty-printed JSON
 * string.
 * @param arr Source abstract struct array.
 * @param out_json Output heap-allocated JSON string.
 * @return C_ORM_OK on success, or error code on failure.
 */
c_orm_error_t
cdd_c_abstract_struct_array_to_json(const cdd_c_abstract_struct_array_t *arr,
                                    char **out_json) {
  JSON_Value *root_val = NULL;
  JSON_Array *root_arr = NULL;
  JSON_Value *obj_val = NULL;
  char *raw_json = NULL;
  c_orm_error_t rc = C_ORM_OK;
  size_t i, j;
  if (!arr || !out_json)
    return C_ORM_ERROR_VALIDATION;

  *out_json = NULL;
  if (cdd_json_value_init_array(&root_val) != CDD_C_SUCCESS) {
    return C_ORM_ERROR_MEMORY;
  }
  root_arr = root_val->array_val;

  for (i = 0; i < arr->count; ++i) {
    JSON_Object *obj = NULL;
    const cdd_c_abstract_struct_t *astruct = &arr->items[i];

    obj_val = NULL;
    if (cdd_json_value_init_object(&obj_val) != CDD_C_SUCCESS) {
      rc = C_ORM_ERROR_MEMORY;
      goto arr_json_err;
    }
    obj = obj_val->object_val;

    for (j = 0; j < astruct->count; ++j) {
      const cdd_c_abstract_struct_kv_t *kv = &astruct->kvs[j];
      JSON_Status jstatus = JSONSuccess;
      if (kv->value.type == CDD_C_VARIANT_TYPE_INT) {
        jstatus =
            json_object_set_number(obj, kv->key, (double)kv->value.value.i_val);
      } else if (kv->value.type == CDD_C_VARIANT_TYPE_FLOAT) {
        jstatus = json_object_set_number(obj, kv->key, kv->value.value.f_val);
      } else if (kv->value.type == CDD_C_VARIANT_TYPE_STRING) {
        jstatus = json_object_set_string(obj, kv->key, kv->value.value.s_val);
      } else if (kv->value.type == CDD_C_VARIANT_TYPE_DECIMAL) {
        char dbuf[64];
        rc = c_orm_decimal_to_str(&kv->value.value.d_val, dbuf, sizeof(dbuf),
                                  NULL);
        if (rc != C_ORM_OK) {
          goto arr_json_err;
        }
        jstatus = json_object_set_string(obj, kv->key, dbuf);
      } else if (kv->value.type == CDD_C_VARIANT_TYPE_BLOB) {
        jstatus = json_object_set_string(obj, kv->key, "<BLOB>");
      } else {
        jstatus = json_object_set_null(obj, kv->key);
      }
      if (jstatus != JSONSuccess) {
        rc = C_ORM_ERROR_MEMORY;
        goto arr_json_err;
      }
    }
    if (cdd_json_array_append_value(root_arr, obj_val) != CDD_C_SUCCESS) {
      rc = C_ORM_ERROR_MEMORY;
      goto arr_json_err;
    }
    obj_val = NULL;
  }

  if (cdd_json_serialize_to_string_pretty(root_val, &raw_json) !=
      CDD_C_SUCCESS) {
    rc = C_ORM_ERROR_MEMORY;
    goto arr_json_err;
  }
  abs_json_value_free(root_val);
  rc = c_orm_strdup(raw_json, out_json);
  if (rc != C_ORM_OK) {
    abs_json_str_free(raw_json);
    return rc;
  }
  abs_json_str_free(raw_json);
  return C_ORM_OK;

arr_json_err:;
  if (obj_val) {
    abs_json_value_free(obj_val);
  }
  abs_json_value_free(root_val);
  return rc;
}

c_orm_error_t cdd_c_abstract_struct_init(cdd_c_abstract_struct_t *astruct) {
  return cdd_c_abstract_struct_init_with_capacity(astruct, 0);
}

c_orm_error_t
cdd_c_abstract_struct_init_with_capacity(cdd_c_abstract_struct_t *astruct,
                                         size_t capacity) {
  c_orm_error_t rc;
  if (!astruct)
    return C_ORM_ERROR_VALIDATION;
  if (capacity > ((size_t)-1) / sizeof(cdd_c_abstract_struct_kv_t))
    return C_ORM_ERROR_VALIDATION;
  astruct->kvs = NULL;
  astruct->count = 0;
  astruct->capacity = capacity;
  if (capacity > 0) {
    rc = cdd_c_malloc(astruct->capacity * sizeof(cdd_c_abstract_struct_kv_t),
                      (void **)&astruct->kvs);
    if (rc != C_ORM_OK)
      return rc;
  }
  return C_ORM_OK;
}

/**
 * @brief Duplicates a C string.
 * @param src Source string to duplicate.
 * @param dest Pointer to receive the duplicated string.
 * @return C_ORM_OK on success, C_ORM_ERROR_VALIDATION on error.
 */
static c_orm_error_t duplicate_string(const char *src, char **dest) {
  size_t len;
  c_orm_error_t rc;
  if (!src) {
    *dest = NULL;
    return C_ORM_OK;
  }
  len = strlen(src);
  rc = cdd_c_malloc(len + 1, (void **)dest);
  if (rc != C_ORM_OK)
    return rc;
  memcpy(*dest, src, len + 1);
  return C_ORM_OK;
}

/**
 * @brief Duplicates a binary buffer.
 * @param src Source buffer to duplicate.
 * @param size Number of bytes to copy.
 * @param dest Pointer to receive the duplicated buffer.
 * @return C_ORM_OK on success, C_ORM_ERROR_VALIDATION on error.
 */
static c_orm_error_t duplicate_blob(const unsigned char *src, size_t size,
                                    unsigned char **dest) {
  c_orm_error_t rc;
  if (!src || size == 0) {
    *dest = NULL;
    return C_ORM_OK;
  }
  rc = cdd_c_malloc(size, (void **)dest);
  if (rc != C_ORM_OK)
    return rc;
  memcpy(*dest, src, size);
  return C_ORM_OK;
}

c_orm_error_t cdd_c_variant_free(cdd_c_variant_t *variant) {
  if (!variant)
    return C_ORM_ERROR_VALIDATION;
  switch (variant->type) {
  case CDD_C_VARIANT_TYPE_STRING:
    if (variant->value.s_val) {
      char *s = variant->value.s_val;
      variant->value.s_val = NULL;
      variant->type = CDD_C_VARIANT_TYPE_NULL;
      return cdd_c_free(s);
    }
    break;
  case CDD_C_VARIANT_TYPE_BLOB:
    if (variant->value.b_val.data) {
      unsigned char *b = variant->value.b_val.data;
      variant->value.b_val.data = NULL;
      variant->type = CDD_C_VARIANT_TYPE_NULL;
      return cdd_c_free(b);
    }
    break;
  default:
    break;
  }
  variant->type = CDD_C_VARIANT_TYPE_NULL;
  return C_ORM_OK;
}

/**
 * @brief Deep copies a variant value.
 * @param dest Destination variant.
 * @param src Source variant.
 * @return C_ORM_OK on success, C_ORM_ERROR_VALIDATION on error.
 */
static c_orm_error_t copy_variant(cdd_c_variant_t *dest,
                                  const cdd_c_variant_t *src) {
  c_orm_error_t rc;
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
  case CDD_C_VARIANT_TYPE_STRING: {
    rc = duplicate_string(src->value.s_val, &dest->value.s_val);
    if (rc != C_ORM_OK) {
      return rc;
    }
    break;
  }
  case CDD_C_VARIANT_TYPE_BLOB: {
    rc = duplicate_blob(src->value.b_val.data, src->value.b_val.size,
                        &dest->value.b_val.data);
    if (rc != C_ORM_OK) {
      return rc;
    }
    dest->value.b_val.size = src->value.b_val.size;
    break;
  }
  case CDD_C_VARIANT_TYPE_NULL:
  default:
    break;
  }
  return C_ORM_OK;
}

/**
 * @brief Computes djb2 hash of a string.
 * @param str Input string.
 * @return Computed djb2 hash value.
 */
static unsigned long abs_calc_hash_string(const char *str) {
  unsigned long hash = 5381;
  int c;
  while ((c = *str++)) {
    hash = ((hash << 5) + hash) + (unsigned long)c; /* hash * 33 + c */
  }
  return hash;
}

c_orm_error_t cdd_c_abstract_set(cdd_c_abstract_struct_t *astruct,
                                 const char *key,
                                 const cdd_c_variant_t *value) {
  size_t i;
  cdd_c_abstract_struct_kv_t *new_kvs = NULL;
  unsigned long khash;
  c_orm_error_t err;
  if (!astruct || !key || !value)
    return C_ORM_ERROR_VALIDATION;

  khash = abs_calc_hash_string(key);

  for (i = 0; i < astruct->count; ++i) {
    if (astruct->kvs[i].key_hash == khash &&
        strcmp(astruct->kvs[i].key, key) == 0) {
      cdd_c_variant_free(&astruct->kvs[i].value);
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
      return err;
    astruct->kvs = new_kvs;
    astruct->capacity = new_cap;
  }

  err = duplicate_string(key, &astruct->kvs[astruct->count].key);
  if (err != C_ORM_OK) {
    return err;
  }
  astruct->kvs[astruct->count].key_hash = khash;

  err = copy_variant(&astruct->kvs[astruct->count].value, value);
  if (err != C_ORM_OK) {
    cdd_c_free(astruct->kvs[astruct->count].key);
    return err;
  }

  astruct->count++;
  return C_ORM_OK;
}

c_orm_error_t cdd_c_abstract_get(const cdd_c_abstract_struct_t *astruct,
                                 const char *key, cdd_c_variant_t **out_value) {
  size_t i;
  unsigned long khash;
  if (!astruct || !key || !out_value)
    return C_ORM_ERROR_VALIDATION;

  khash = abs_calc_hash_string(key);

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
  c_orm_error_t rc;
  if (!dest || !src)
    return C_ORM_ERROR_VALIDATION;

  CDD_C_ASTRUCT_ZERO_INIT(dest);

  for (i = 0; i < src->count; ++i) {
    rc = cdd_c_abstract_set(dest, src->kvs[i].key, &src->kvs[i].value);
    if (rc != C_ORM_OK) {
      cdd_c_abstract_struct_free(dest);
      return rc;
    }
  }
  return C_ORM_OK;
}

c_orm_error_t cdd_c_abstract_struct_free(cdd_c_abstract_struct_t *astruct) {
  size_t i;
  if (!astruct)
    return C_ORM_ERROR_VALIDATION;

  for (i = 0; i < astruct->count; ++i) {
    cdd_c_free(astruct->kvs[i].key);
    cdd_c_variant_free(&astruct->kvs[i].value);
  }
  cdd_c_free(astruct->kvs);
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
                               sizeof(dbuf), NULL) != C_ORM_OK) {
        printf("null");
      } else {
        printf("\"%s\"", dbuf);
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
  char *raw_json = NULL;
  c_orm_error_t rc = C_ORM_OK;
  size_t i;

  if (!astruct || !out_json)
    return C_ORM_ERROR_VALIDATION;

  *out_json = NULL;
  if (cdd_json_value_init_object(&root_val) != CDD_C_SUCCESS) {
    return C_ORM_ERROR_MEMORY;
  }
  root_obj = root_val->object_val;

  for (i = 0; i < astruct->count; ++i) {
    const cdd_c_abstract_struct_kv_t *kv = &astruct->kvs[i];
    JSON_Status jstatus = JSONSuccess;
    if (kv->value.type == CDD_C_VARIANT_TYPE_NULL) {
      jstatus = json_object_set_null(root_obj, kv->key);
    } else if (kv->value.type == CDD_C_VARIANT_TYPE_INT) {
      jstatus = json_object_set_number(root_obj, kv->key,
                                       (double)kv->value.value.i_val);
    } else if (kv->value.type == CDD_C_VARIANT_TYPE_FLOAT) {
      jstatus =
          json_object_set_number(root_obj, kv->key, kv->value.value.f_val);
    } else if (kv->value.type == CDD_C_VARIANT_TYPE_STRING) {
      jstatus =
          json_object_set_string(root_obj, kv->key, kv->value.value.s_val);
    } else if (kv->value.type == CDD_C_VARIANT_TYPE_DECIMAL) {
      char dbuf[64];
      rc = c_orm_decimal_to_str(&kv->value.value.d_val, dbuf, sizeof(dbuf),
                                NULL);
      if (rc != C_ORM_OK) {
        abs_json_value_free(root_val);
        return rc;
      }
      jstatus = json_object_set_string(root_obj, kv->key, dbuf);
    } else if (kv->value.type == CDD_C_VARIANT_TYPE_BLOB) {
      jstatus = json_object_set_string(root_obj, kv->key, "<blob>");
    }
    if (jstatus != JSONSuccess) {
      abs_json_value_free(root_val);
      return C_ORM_ERROR_MEMORY;
    }
  }

  if (cdd_json_serialize_to_string(root_val, &raw_json) != CDD_C_SUCCESS) {
    abs_json_value_free(root_val);
    return C_ORM_ERROR_MEMORY;
  }
  abs_json_value_free(root_val);
  rc = c_orm_strdup(raw_json, out_json);
  if (rc != C_ORM_OK) {
    abs_json_str_free(raw_json);
    return rc;
  }
  abs_json_str_free(raw_json);
  return C_ORM_OK;
}

c_orm_error_t
cdd_c_abstract_struct_from_json(const char *json,
                                cdd_c_abstract_struct_t *out_astruct) {
  JSON_Value *root_val = NULL;
  JSON_Object *root_obj = NULL;
  cdd_c_error_t jrc = CDD_C_SUCCESS;
  c_orm_error_t rc = C_ORM_OK;
  size_t i;

  if (!json || !out_astruct)
    return C_ORM_ERROR_VALIDATION;

  CDD_C_ASTRUCT_ZERO_INIT(out_astruct);

  jrc = cdd_json_parse_string(json, &root_val);
  if (jrc != CDD_C_SUCCESS) {
    return (jrc == CDD_C_ERROR_MEMORY) ? C_ORM_ERROR_MEMORY
                                       : C_ORM_ERROR_VALIDATION;
  }

  if (root_val->type != JSONObject) {
    abs_json_value_free(root_val);
    return C_ORM_ERROR_VALIDATION;
  }

  root_obj = root_val->object_val;
  for (i = 0; i < root_obj->count; ++i) {
    const char *key = root_obj->names[i];
    const JSON_Value *val = root_obj->values[i];
    cdd_c_variant_t variant;

    variant.type = CDD_C_VARIANT_TYPE_NULL;
    if (val->type == JSONString) {
      variant.type = CDD_C_VARIANT_TYPE_STRING;
      variant.value.s_val = val->string_val;
    } else if (val->type == JSONNumber) {
      double num = val->number_val;
      if (num == (double)((c_orm_int64_t)num)) {
        variant.type = CDD_C_VARIANT_TYPE_INT;
        variant.value.i_val = (c_orm_int64_t)num;
      } else {
        variant.type = CDD_C_VARIANT_TYPE_FLOAT;
        variant.value.f_val = num;
      }
    } else if (val->type == JSONBoolean) {
      variant.type = CDD_C_VARIANT_TYPE_INT;
      variant.value.i_val = val->boolean_val ? 1 : 0;
    } else {
      variant.type = CDD_C_VARIANT_TYPE_NULL;
    }

    rc = cdd_c_abstract_set(out_astruct, key, &variant);
    if (rc != C_ORM_OK) {
      abs_json_value_free(root_val);
      cdd_c_abstract_struct_free(out_astruct);
      return rc;
    }
  }

  abs_json_value_free(root_val);
  return C_ORM_OK;
}

c_orm_error_t cdd_c_abstract_hydrate(cdd_c_abstract_struct_t *out_astruct,
                                     void **row_data,
                                     const cdd_c_column_meta_t *cols,
                                     size_t n_cols) {
  size_t i;
  c_orm_error_t rc;
  if (!out_astruct || !row_data || !cols)
    return C_ORM_ERROR_VALIDATION;

  rc = cdd_c_abstract_struct_init_with_capacity(out_astruct, n_cols);
  if (rc != C_ORM_OK)
    return rc;

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

    rc = cdd_c_abstract_set(out_astruct, col->name, &variant);
    if (rc != C_ORM_OK) {
      cdd_c_abstract_struct_free(out_astruct);
      return rc;
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
    return rc;
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
  if (!out_astruct || !s)
    return C_ORM_ERROR_VALIDATION;

  CDD_C_ASTRUCT_ZERO_INIT(out_astruct);

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
      cdd_c_abstract_struct_free(out_astruct);
      return C_ORM_ERROR_VALIDATION;
    }
  }

  return C_ORM_OK;
#else
  {
    size_t u = ((size_t)out_astruct ^ (size_t)stmt) * 0U;
    return (c_orm_error_t)((int)C_ORM_ERROR_VALIDATION + (int)u);
  }
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
            cdd_c_abstract_struct_free(out_astruct);
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
      cdd_c_abstract_struct_free(out_astruct);
      return C_ORM_ERROR_VALIDATION;
    }
  }

  return C_ORM_OK;
#else
  {
    size_t u = ((size_t)out_astruct ^ (size_t)res ^ (size_t)row_index) * 0U;
    return (c_orm_error_t)((int)C_ORM_ERROR_VALIDATION + (int)u);
  }
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
      cdd_c_abstract_struct_free(out_astruct);
      return C_ORM_ERROR_VALIDATION;
    }
  }

  return C_ORM_OK;
#else
  {
    size_t u =
        ((size_t)out_astruct ^ (size_t)row ^ (size_t)fields ^ num_fields) * 0U;
    return (c_orm_error_t)((int)C_ORM_ERROR_VALIDATION + (int)u);
  }
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
  c_orm_error_t rc;
  const cdd_c_meta_t *meta = (const cdd_c_meta_t *)struct_meta;
  if (!out_astruct || !in_struct || !meta)
    return C_ORM_ERROR_VALIDATION;

  CDD_C_ASTRUCT_ZERO_INIT(out_astruct);

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

    rc = cdd_c_abstract_set(out_astruct, prop->name, &val);
    if (rc != C_ORM_OK) {
      cdd_c_abstract_struct_free(out_astruct);
      return rc;
    }
  }

  return C_ORM_OK;
}

c_orm_error_t cdd_c_abstract_to_specific(
    void *out_struct, const cdd_c_abstract_struct_t *in_astruct,
    const struct cdd_c_meta *struct_meta, int strict_mapping) {
  size_t i;
  c_orm_error_t rc;
  const cdd_c_meta_t *meta = (const cdd_c_meta_t *)struct_meta;
  if (!out_struct || !in_astruct || !meta)
    return C_ORM_ERROR_VALIDATION;

  if (strict_mapping && in_astruct->count != meta->num_props) {
    return C_ORM_ERROR_VALIDATION;
  }

  for (i = 0; i < meta->num_props; ++i) {
    const cdd_c_prop_meta_t *prop = &meta->props[i];
    cdd_c_variant_t *val = NULL;

    rc = cdd_c_abstract_get(in_astruct, prop->name, &val);
    if (rc != C_ORM_OK) {
      if (strict_mapping)
        return rc;
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
    c_orm_string_builder_free(sb);
    return C_ORM_ERROR_VALIDATION;
  }

  prep_rc = sqlite3_prepare_v2(sql_db, query, -1, &stmt, NULL);
  c_orm_string_builder_free(sb);
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
      cdd_c_abstract_struct_free(&col_def);
      sqlite3_finalize(stmt);
      return C_ORM_ERROR_MEMORY;
    }

    v_type.type = CDD_C_VARIANT_TYPE_STRING;
    v_type.value.s_val = (char *)sqlite3_column_text(stmt, 2);
    if (cdd_c_abstract_set(&col_def, "type", &v_type) != C_ORM_OK) {
      cdd_c_abstract_struct_free(&col_def);
      sqlite3_finalize(stmt);
      return C_ORM_ERROR_MEMORY;
    }

    v_notnull.type = CDD_C_VARIANT_TYPE_INT;
    v_notnull.value.i_val = sqlite3_column_int(stmt, 3);
    if (cdd_c_abstract_set(&col_def, "notnull", &v_notnull) != C_ORM_OK) {
      cdd_c_abstract_struct_free(&col_def);
      sqlite3_finalize(stmt);
      return C_ORM_ERROR_MEMORY;
    }

    v_pk.type = CDD_C_VARIANT_TYPE_INT;
    v_pk.value.i_val = sqlite3_column_int(stmt, 5);
    if (cdd_c_abstract_set(&col_def, "pk", &v_pk) != C_ORM_OK) {
      cdd_c_abstract_struct_free(&col_def);
      sqlite3_finalize(stmt);
      return C_ORM_ERROR_MEMORY;
    }

    if (cdd_c_abstract_struct_array_append(out_schema, &col_def) != C_ORM_OK) {
      cdd_c_abstract_struct_free(&col_def);
      sqlite3_finalize(stmt);
      return C_ORM_ERROR_MEMORY;
    }
  }
  sqlite3_finalize(stmt);
  return C_ORM_OK;
#else
  {
    size_t u = ((size_t)db ^ (size_t)table_name ^ (size_t)out_schema) * 0U;
    return (c_orm_error_t)((int)C_ORM_ERROR_VALIDATION + (int)u);
  }
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
      c_orm_error_t frc = cdd_c_abstract_struct_free(&col_def);
      PQclear(res);
      if (frc != C_ORM_OK) {
        return frc;
      }
      return C_ORM_ERROR_MEMORY;
    }

    v_type.type = CDD_C_VARIANT_TYPE_STRING;
    v_type.value.s_val = type_val ? type_val : (char *)"";
    if (cdd_c_abstract_set(&col_def, "type", &v_type) != C_ORM_OK) {
      c_orm_error_t frc = cdd_c_abstract_struct_free(&col_def);
      PQclear(res);
      if (frc != C_ORM_OK) {
        return frc;
      }
      return C_ORM_ERROR_MEMORY;
    }

    v_notnull.type = CDD_C_VARIANT_TYPE_INT;
    v_notnull.value.i_val =
        (is_nullable_val && strcmp(is_nullable_val, "YES") != 0) ? 1 : 0;
    if (cdd_c_abstract_set(&col_def, "notnull", &v_notnull) != C_ORM_OK) {
      c_orm_error_t frc = cdd_c_abstract_struct_free(&col_def);
      PQclear(res);
      if (frc != C_ORM_OK) {
        return frc;
      }
      return C_ORM_ERROR_MEMORY;
    }

    if (cdd_c_abstract_struct_array_append(out_schema, &col_def) != C_ORM_OK) {
      c_orm_error_t frc = cdd_c_abstract_struct_free(&col_def);
      PQclear(res);
      if (frc != C_ORM_OK) {
        return frc;
      }
      return C_ORM_ERROR_MEMORY;
    }
  }
  PQclear(res);
  return C_ORM_OK;
#else
  {
    size_t u = ((size_t)conn ^ (size_t)table_name ^ (size_t)out_schema) * 0U;
    return (c_orm_error_t)((int)C_ORM_ERROR_VALIDATION + (int)u);
  }
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
    c_orm_error_t frc = c_orm_string_builder_free(sb);
    if (frc != C_ORM_OK) {
      return frc;
    }
    return C_ORM_ERROR_VALIDATION;
  }

  qrc = mysql_query(mysql_conn, query);
  {
    c_orm_error_t frc = c_orm_string_builder_free(sb);
    if (frc != C_ORM_OK) {
      return frc;
    }
  }
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
      c_orm_error_t frc = cdd_c_abstract_struct_free(&col_def);
      mysql_free_result(res);
      if (frc != C_ORM_OK) {
        return frc;
      }
      return C_ORM_ERROR_MEMORY;
    }

    v_type.type = CDD_C_VARIANT_TYPE_STRING;
    v_type.value.s_val = type_val ? type_val : (char *)"";
    if (cdd_c_abstract_set(&col_def, "type", &v_type) != C_ORM_OK) {
      c_orm_error_t frc = cdd_c_abstract_struct_free(&col_def);
      mysql_free_result(res);
      if (frc != C_ORM_OK) {
        return frc;
      }
      return C_ORM_ERROR_MEMORY;
    }

    v_notnull.type = CDD_C_VARIANT_TYPE_INT;
    v_notnull.value.i_val = (null_val && strcmp(null_val, "YES") != 0) ? 1 : 0;
    if (cdd_c_abstract_set(&col_def, "notnull", &v_notnull) != C_ORM_OK) {
      c_orm_error_t frc = cdd_c_abstract_struct_free(&col_def);
      mysql_free_result(res);
      if (frc != C_ORM_OK) {
        return frc;
      }
      return C_ORM_ERROR_MEMORY;
    }

    v_pk.type = CDD_C_VARIANT_TYPE_INT;
    v_pk.value.i_val = (key_val && strcmp(key_val, "PRI") == 0) ? 1 : 0;
    if (cdd_c_abstract_set(&col_def, "pk", &v_pk) != C_ORM_OK) {
      c_orm_error_t frc = cdd_c_abstract_struct_free(&col_def);
      mysql_free_result(res);
      if (frc != C_ORM_OK) {
        return frc;
      }
      return C_ORM_ERROR_MEMORY;
    }

    if (cdd_c_abstract_struct_array_append(out_schema, &col_def) != C_ORM_OK) {
      c_orm_error_t frc = cdd_c_abstract_struct_free(&col_def);
      mysql_free_result(res);
      if (frc != C_ORM_OK) {
        return frc;
      }
      return C_ORM_ERROR_MEMORY;
    }
  }
  mysql_free_result(res);
  return C_ORM_OK;
#else
  {
    size_t u = ((size_t)conn ^ (size_t)table_name ^ (size_t)out_schema) * 0U;
    return (c_orm_error_t)((int)C_ORM_ERROR_VALIDATION + (int)u);
  }
#endif
}

/**
 * @brief Appends a single inspected column metadata entry into out_schema.
 * @param out_schema Target abstract struct array.
 * @param name Column name string.
 * @param type_name SQL type name string.
 * @param col_len Column length.
 * @param col_scale Column scale.
 * @param is_notnull Non-null indicator (1 if NOT NULL, 0 otherwise).
 * @return C_ORM_OK on success, C_ORM_ERROR_MEMORY on allocation failure.
 */
static c_orm_error_t
append_inspected_column_row(cdd_c_abstract_struct_array_t *out_schema,
                            char *name, char *type_name, c_orm_int64_t col_len,
                            c_orm_int64_t col_scale, c_orm_int64_t is_notnull) {
  static const char *const keys[5] = {"name", "type", "length", "scale",
                                      "notnull"};
  cdd_c_abstract_struct_t col_def;
  cdd_c_variant_t vals[5];
  size_t k;
  c_orm_error_t rc;

  CDD_C_ASTRUCT_ZERO_INIT(&col_def);
  vals[0].type = CDD_C_VARIANT_TYPE_STRING;
  vals[0].value.s_val = name;
  vals[1].type = CDD_C_VARIANT_TYPE_STRING;
  vals[1].value.s_val = type_name;
  vals[2].type = CDD_C_VARIANT_TYPE_INT;
  vals[2].value.i_val = col_len;
  vals[3].type = CDD_C_VARIANT_TYPE_INT;
  vals[3].value.i_val = col_scale;
  vals[4].type = CDD_C_VARIANT_TYPE_INT;
  vals[4].value.i_val = is_notnull;

  for (k = 0; k < 5; ++k) {
    rc = cdd_c_abstract_set(&col_def, keys[k], &vals[k]);
    if (rc != C_ORM_OK) {
      cdd_c_abstract_struct_free(&col_def);
      return rc;
    }
  }
  rc = cdd_c_abstract_struct_array_append(out_schema, &col_def);
  if (rc != C_ORM_OK) {
    cdd_c_abstract_struct_free(&col_def);
    return rc;
  }
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t
cdd_c_inspect_schema_odbc(c_orm_db_t *db, const char *table_name,
                          cdd_c_abstract_struct_array_t *out_schema) {
  c_orm_odbc_column_info_t *cols = NULL;
  size_t count = 0;
  size_t i;
  c_orm_error_t rc;
  c_orm_error_t init_rc;

  if (!db || !table_name || !out_schema) {
    return C_ORM_ERROR_VALIDATION;
  }
  rc = c_orm_odbc_list_columns(db, NULL, table_name, &cols, &count);
  if (rc != C_ORM_OK) {
    return rc;
  }
  init_rc = cdd_c_abstract_struct_array_init(out_schema, count > 0 ? count : 4);
  if (init_rc != C_ORM_OK) {
    c_orm_odbc_free_column_list(cols, count);
    return C_ORM_ERROR_MEMORY;
  }

  for (i = 0; i < count; ++i) {
    rc = append_inspected_column_row(
        out_schema, cols[i].name, cols[i].type_name,
        (c_orm_int64_t)cols[i].column_size,
        (c_orm_int64_t)cols[i].decimal_digits, cols[i].is_nullable ? 0 : 1);
    if (rc != C_ORM_OK) {
      c_orm_odbc_free_column_list(cols, count);
      return rc;
    }
  }

  return c_orm_odbc_free_column_list(cols, count);
}

C_ORM_EXPORT c_orm_error_t
cdd_c_inspect_schema_db2(c_orm_db_t *db, const char *table_name,
                         cdd_c_abstract_struct_array_t *out_schema) {
  c_orm_query_t *q = NULL;
  int has_row = 0;
  c_orm_error_t rc;
  c_orm_error_t init_rc;
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
  if (rc != C_ORM_OK) {
    q = NULL;
    goto try_zos;
  }
  rc = db->vtable->bind_string(q, 1, table_name);
  if (rc != C_ORM_OK) {
    goto cleanup_luw;
  }
  rc = db->vtable->step(q, &has_row);
  if (rc != C_ORM_OK) {
    goto cleanup_luw;
  }
  goto have_catalog_query;

cleanup_luw:;
  fin_rc = db->vtable->finalize(q);
  if (fin_rc != C_ORM_OK) {
    return fin_rc;
  }
  q = NULL;

try_zos:;
  rc = db->vtable->prepare(db, zos_sql, &q);
  if (rc != C_ORM_OK) {
    q = NULL;
    goto try_odbc;
  }
  rc = db->vtable->bind_string(q, 1, table_name);
  if (rc != C_ORM_OK) {
    goto cleanup_zos;
  }
  rc = db->vtable->step(q, &has_row);
  if (rc != C_ORM_OK) {
    goto cleanup_zos;
  }
  goto have_catalog_query;

cleanup_zos:;
  fin_rc = db->vtable->finalize(q);
  if (fin_rc != C_ORM_OK) {
    return fin_rc;
  }
  q = NULL;

try_odbc:;
  return cdd_c_inspect_schema_odbc(db, table_name, out_schema);

have_catalog_query:;
  init_rc = cdd_c_abstract_struct_array_init(out_schema, 8);
  if (init_rc != C_ORM_OK) {
    fin_rc = db->vtable->finalize(q);
    if (fin_rc != C_ORM_OK) {
      return fin_rc;
    }
    return init_rc;
  }

  while (has_row) {
    const char *cname = NULL;
    const char *tname = NULL;
    const char *nulls = NULL;
    int32_t clen = 0;
    int32_t cscale = 0;
    c_orm_error_t col_rc;

    col_rc = db->vtable->get_string(q, 0, &cname);
    if (col_rc != C_ORM_OK) {
      goto db2_col_err;
    }
    col_rc = db->vtable->get_string(q, 1, &tname);
    if (col_rc != C_ORM_OK) {
      goto db2_col_err;
    }
    col_rc = db->vtable->get_int32(q, 2, &clen);
    if (col_rc != C_ORM_OK) {
      goto db2_col_err;
    }
    col_rc = db->vtable->get_int32(q, 3, &cscale);
    if (col_rc != C_ORM_OK) {
      goto db2_col_err;
    }
    col_rc = db->vtable->get_string(q, 4, &nulls);
    if (col_rc != C_ORM_OK) {
      goto db2_col_err;
    }

    if (cname && tname) {
      rc = append_inspected_column_row(
          out_schema, (char *)(size_t)cname, (char *)(size_t)tname,
          (c_orm_int64_t)clen, (c_orm_int64_t)cscale,
          (nulls && nulls[0] == 'N') ? 1 : 0);
      if (rc != C_ORM_OK) {
        cdd_c_abstract_struct_array_free(out_schema);
        fin_rc = db->vtable->finalize(q);
        if (fin_rc != C_ORM_OK) {
          return fin_rc;
        }
        return rc;
      }
    }

    rc = db->vtable->step(q, &has_row);
    if (rc != C_ORM_OK) {
      cdd_c_abstract_struct_array_free(out_schema);
      fin_rc = db->vtable->finalize(q);
      if (fin_rc != C_ORM_OK) {
        return fin_rc;
      }
      return rc;
    }
    continue;

  db2_col_err:;
    cdd_c_abstract_struct_array_free(out_schema);
    fin_rc = db->vtable->finalize(q);
    if (fin_rc != C_ORM_OK) {
      return fin_rc;
    }
    return col_rc;
  }

  fin_rc = db->vtable->finalize(q);
  if (fin_rc != C_ORM_OK) {
    cdd_c_abstract_struct_array_free(out_schema);
    return fin_rc;
  }
  return init_rc;
}
