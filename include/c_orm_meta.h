/**
 * @file c_orm_meta.h
 * @brief Core definitions and structures for c-orm.
 */

#ifndef C_ORM_META_H
#define C_ORM_META_H

/* clang-format off */
#include <stddef.h>

#ifdef __cplusplus
#define C_ORM_IN_CPLUSPLUS 1
extern "C" {
#endif /* __cplusplus */

#if defined(_MSC_VER)
#if _MSC_VER < 1600
typedef signed __int8 int8_t;
typedef signed __int16 int16_t;
typedef signed __int32 int32_t;
typedef signed __int64 int64_t;
typedef unsigned __int8 uint8_t;
typedef unsigned __int16 uint16_t;
typedef unsigned __int32 uint32_t;
typedef unsigned __int64 uint64_t;
#else
#include <stdint.h>
#endif
#ifndef C_ORM_IN_CPLUSPLUS
#ifndef _STDBOOL_H
#define _STDBOOL_H
typedef unsigned char bool;
#define true 1
#define false 0
#endif
#endif
#else
#include <stdint.h>
#ifndef C_ORM_IN_CPLUSPLUS
#ifndef _STDBOOL_H
#define _STDBOOL_H
typedef unsigned char bool;
#define true 1
#define false 0
#endif
#endif
#endif

#include <stdlib.h>
/* clang-format on */

#ifndef C_ORM_EXPORT
#if defined(_WIN32) || defined(__CYGWIN__)
#if defined(C_ORM_SHARED)
#if defined(c_orm_EXPORTS) || defined(c_orm_async_EXPORTS)
#define C_ORM_EXPORT __declspec(dllexport)
#else
#define C_ORM_EXPORT __declspec(dllimport)
#endif
#else
#define C_ORM_EXPORT
#endif
#else
#if defined(__GNUC__) && __GNUC__ >= 4
#define C_ORM_EXPORT __attribute__((visibility("default")))
#else
#define C_ORM_EXPORT
#endif
#endif
#endif

#define CMP_SECURE_FIELD true

/**
 * @brief Data types supported by c-orm.
 */
typedef enum {
  C_ORM_OK = 0,
  C_ORM_ERROR_MEMORY,
  C_ORM_ERROR_CONNECTION,
  C_ORM_ERROR_SQL,
  C_ORM_ERROR_BIND,
  C_ORM_ERROR_STEP,
  C_ORM_ERROR_TYPE_MISMATCH,
  C_ORM_ERROR_NOT_FOUND,
  C_ORM_ERROR_NOT_IMPLEMENTED,
  C_ORM_ERROR_UNKNOWN,
  C_ORM_ERROR_EXPIRED,
  C_ORM_ERROR_VALIDATION,
  C_ORM_ERROR_RECURSION,
  C_ORM_ERROR_READ_ONLY
} c_orm_error_t;

#ifndef C_ORM_TEST_ALLOCATOR
#define C_ORM_MALLOC malloc
#define C_ORM_FREE free
#define C_ORM_REALLOC realloc
#else
C_ORM_EXPORT extern void *(*c_orm_malloc)(size_t size);
C_ORM_EXPORT extern void (*c_orm_free)(void *ptr);
C_ORM_EXPORT extern void *(*c_orm_realloc)(void *ptr, size_t size);

/**
 * @brief Sets the allocator functions. Useful for testing across DLL
 * boundaries.
 */

C_ORM_EXPORT c_orm_error_t c_orm_set_allocators(void *(*m)(size_t),
                                                void *(*r)(void *, size_t),
                                                void (*f)(void *));

#define C_ORM_MALLOC c_orm_malloc
#define C_ORM_FREE c_orm_free
#define C_ORM_REALLOC c_orm_realloc
#endif

/**
 * @brief Duplicates a string using c_orm_malloc.
 * @param s String to duplicate.
 * @return Duplicated string, or NULL on failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_strdup(const char *s, char **out);
#define C_ORM_STRDUP c_orm_strdup

typedef enum {
  C_ORM_TYPE_INT32,
  C_ORM_TYPE_INT64,
  C_ORM_TYPE_FLOAT,
  C_ORM_TYPE_DOUBLE,
  C_ORM_TYPE_BOOL,
  C_ORM_TYPE_STRING,
  C_ORM_TYPE_BLOB,
  C_ORM_TYPE_DATE,
  C_ORM_TYPE_TIMESTAMP,
  C_ORM_TYPE_JSON,    /**< Dynamic JSON type (Postgres JSONB) */
  C_ORM_TYPE_ENUM,    /**< Enumeration mapped via cdd-c (MySQL/PG ENUM) */
  C_ORM_TYPE_SET,     /**< Set mapping via cdd-c (MySQL SET) */
  C_ORM_TYPE_ARRAY,   /**< Homogeneous array mapping via cdd-c (PG Arrays) */
  C_ORM_TYPE_POINT,   /**< Spatial Point (PostGIS/MySQL Spatial) */
  C_ORM_TYPE_POLYGON, /**< Spatial Polygon (PostGIS/MySQL Spatial) */
  C_ORM_TYPE_UNKNOWN
} c_orm_type_t;

/**
 * @brief Represents a 2D spatial point (Step 172).
 */
typedef struct {
  double x;
  double y;
} c_orm_point_t;

/**
 * @brief Represents a 2D spatial polygon (Step 172).
 */
typedef struct {
  c_orm_point_t *points;
  size_t num_points;
} c_orm_polygon_t;

/**
 * @brief Relationship types for mapping logic.
 */
typedef enum {
  C_ORM_RELATION_ONE_TO_ONE,
  C_ORM_RELATION_BELONGS_TO,
  C_ORM_RELATION_ONE_TO_MANY,
  C_ORM_RELATION_MANY_TO_MANY,
  C_ORM_RELATION_HAS_MANY_THROUGH,
  C_ORM_RELATION_POLYMORPHIC /**< Association type where the target table is
                                determined dynamically by a discriminator
                                column. */
} c_orm_relation_type_t;

/**
 * @brief Represents binary large object (BLOB) data.
 */
typedef struct {
  void *data;
  size_t size;
} c_orm_blob_t;

/**
 * @brief Context structure embedded in generated proxy structs to track
 * deferred hydration.
 */
typedef struct c_orm_lazy_load_context {
  bool
      is_loaded; /**< True if the relationship has been fully loaded from DB. */
  void *db_connection;         /**< Pointer to the c_orm_db_t connection. */
  const char *foreign_key_val; /**< Pre-extracted foreign key value for delayed
                                  query. */
} c_orm_lazy_load_context_t;

struct cdd_c_meta;

/**
 * @brief Defines a specific polymorphic target mapping.
 */
typedef struct c_orm_polymorphic_target {
  const char *discriminator_value; /**< Value inside the discriminator column
                                      identifying this target. */
  const char *target_table;        /**< Table to query for this target type. */
  const struct cdd_c_meta
      *target_ir; /**< Reflection metadata for the target struct. */
} c_orm_polymorphic_target_t;

/**
 * @brief Cascading rules for relationships
 */
typedef enum {
  C_ORM_CASCADE_NONE = 0,
  C_ORM_CASCADE_DELETE = 1,
  C_ORM_CASCADE_SET_NULL = 2,
  C_ORM_CASCADE_RESTRICT = 3,
  C_ORM_CASCADE_UPDATE = 4
} c_orm_cascade_rule_t;

typedef struct c_orm_relation_meta {
  const char *field_name;     /**< Name of the field in the C struct. */
  c_orm_relation_type_t type; /**< Relationship type (ONE_TO_ONE, etc). */
  const char *target_table;   /**< Target table name. NULL if POLYMORPHIC. */
  const char *foreign_key;    /**< Foreign key column name. */
  const char *local_key;      /**< Local key column name (usually PK). */
  size_t struct_offset; /**< Offset of the relation pointer or array within the
                           struct. */
  size_t data_offset;
  size_t lazy_ctx_offset;
  size_t target_array_len_offset; /**< Offset for the length field of a
                                     C_ORM_RELATION_ONE_TO_MANY array. */
  const struct cdd_c_meta
      *target_ir; /**< Pointer to the cdd-c IR metadata for the target struct.
                     NULL if POLYMORPHIC. */
  const struct c_orm_table_meta
      *target_meta; /**< Pointer to the c-orm table metadata for the target
                       struct. */

  /* Polymorphic specific fields */
  const char *discriminator_column; /**< Column name containing the string
                                       discriminator mapping to targets. */
  const c_orm_polymorphic_target_t
      *polymorphic_targets;       /**< Array of polymorphic targets. */
  size_t num_polymorphic_targets; /**< Number of targets in the array. */

  /* Cascading rules */
  c_orm_cascade_rule_t on_delete;
  c_orm_cascade_rule_t on_update;

  /* Join table for many to many */
  const char *join_table;
  const char *join_local_key;
  const char *join_foreign_key;

  /* Phase 4 features */
  c_orm_error_t (*on_attach)(void *parent_obj, void *child_obj, void *db_ctx);
  c_orm_error_t (*on_detach)(void *parent_obj, void *child_obj, void *db_ctx);
  const char *custom_filter;
  const char *order_by;
  int soft_delete_aware;

} c_orm_relation_meta_t;

/**
 * @brief Column metadata definition.
 */
typedef struct {
  const char *name;       /**< Column name. */
  c_orm_type_t type;      /**< Data type. */
  size_t offset;          /**< Offset in the target C struct. */
  bool is_pk;             /**< True if column is a Primary Key. */
  bool is_nullable;       /**< True if column can be NULL. */
  const char *fk_target;  /**< Target table name if Foreign Key, else NULL. */
  bool on_delete_cascade; /**< True if deleting the fk_target cascades to this
                             row. */
  bool is_secure; /**< True if the field data must be encrypted at rest (e.g.
                     DPAPI, Keychain). */
} c_orm_column_meta_t;

/**
 * @brief Function pointer signature for ORM lifecycle hooks.
 *
 * @param obj The object triggering the hook.
 * @param user_data Opaque pointer passed to the connection layer.
 * @return 0 on success, non-zero to abort the operation.
 */
typedef c_orm_error_t (*c_orm_lifecycle_hook_t)(void *obj, void *user_data);

/**
 * @brief Hook triggers.
 */
typedef enum {
  C_ORM_HOOK_BEFORE_SAVE,
  C_ORM_HOOK_AFTER_SAVE,
  C_ORM_HOOK_BEFORE_INSERT,
  C_ORM_HOOK_AFTER_INSERT,
  C_ORM_HOOK_BEFORE_UPDATE,
  C_ORM_HOOK_AFTER_UPDATE,
  C_ORM_HOOK_BEFORE_DELETE,
  C_ORM_HOOK_AFTER_DELETE,
  C_ORM_HOOK_COUNT
} c_orm_lifecycle_hook_type_t;

/**
 * @brief Table metadata definition.
 */
typedef struct c_orm_table_meta {
  const char *name;                   /**< Table name. */
  const c_orm_column_meta_t *columns; /**< Array of column metadata. */
  size_t num_columns;                 /**< Number of columns. */
  size_t struct_size; /**< Size of the generated struct in bytes. */

  /* Pre-compiled basic query templates */
  const char *query_select_all;
  const char *query_select_by_pk;
  const char *query_insert;
  const char *query_update;
  const char *query_delete_by_pk;
  const char *query_select_by_pk_for_update;

  /* Step 160: Support for SQL views (read-only models) generated via cdd-c */
  bool is_view; /**< True if this is a view and mutations are restricted. */

  /* TTL & Expiration Tracking */
  bool has_ttl; /**< True if rows in this table can expire automatically */
  size_t created_at_offset; /**< Offset for the created_at UNIX timestamp
                               (int64) */
  size_t expires_in_offset; /**< Offset for the expires_in duration in seconds
                               (int32) */

  /* Lifecycle hooks */
  c_orm_lifecycle_hook_t
      hooks[C_ORM_HOOK_COUNT]; /**< Array of active lifecycle hooks */

  const c_orm_relation_meta_t
      *relations;       /**< Array of relationship metadata. */
  size_t num_relations; /**< Number of relationships. */
} c_orm_table_meta_t;

/**
 * @brief Represents a bitmask of dirty fields (up to 64 fields).
 */
typedef uint64_t c_orm_dirty_flags_t;

/**
 * @brief Helper macro to mark a field as dirty.
 * @param obj The structure instance containing the dirty_flags field.
 * @param bit_index The index of the field to flag as dirty.
 */
#define C_ORM_SET_FIELD_DIRTY(obj, bit_index)                                  \
  ((obj)->dirty_flags |= (1ULL << (bit_index)))

/**
 * @brief Helper macro to mark a field as clean.
 * @param obj The structure instance containing the dirty_flags field.
 * @param bit_index The index of the field to flag as clean.
 */
#define C_ORM_CLEAR_FIELD_DIRTY(obj, bit_index)                                \
  ((obj)->dirty_flags &= ~(1ULL << (bit_index)))

/**
 * @brief Helper macro to check if a field is dirty.
 * @param obj The structure instance containing the dirty_flags field.
 * @param bit_index The index of the field to check.
 */
#define C_ORM_IS_FIELD_DIRTY(obj, bit_index)                                   \
  (((obj)->dirty_flags & (1ULL << (bit_index))) != 0)

/**
 * @brief Represents a single cached object entry in the Identity Map.
 */
typedef struct c_orm_identity_entry {
  struct c_orm_identity_entry
      *next;        /**< Linked list pointer for hash collisions */
  void *object_ptr; /**< The cached C struct instance */
  int32_t pk_int;   /**< The integer primary key (if applicable) */
  char *pk_str;     /**< The string primary key (if applicable) */
} c_orm_identity_entry_t;

/**
 * @brief Represents a bucketed hash table caching active object pointers per
 * table.
 */
typedef struct c_orm_identity_bucket {
  const c_orm_table_meta_t *table; /**< The table this bucket caches */
  c_orm_identity_entry_t *
      *entries;       /**< Array of hash map buckets for this table */
  size_t num_buckets; /**< Size of the entries array */
  struct c_orm_identity_bucket
      *next; /**< Linked list for multiple table buckets */
} c_orm_identity_bucket_t;

/**
 * @brief High-level Identity Map holding references to active objects.
 */
typedef struct c_orm_identity_map {
  c_orm_identity_bucket_t *buckets; /**< Head of the bucket list */
} c_orm_identity_map_t;

#ifdef __cplusplus
}
#endif /* __cplusplus */

/**
 * @brief Free memory allocated by the c-orm library.
 */
C_ORM_EXPORT void c_orm_system_free(void *ptr);

C_ORM_EXPORT c_orm_error_t c_orm_system_malloc(size_t size, void **out_ptr);
C_ORM_EXPORT c_orm_error_t c_orm_system_calloc(size_t nmemb, size_t size,
                                               void **out_ptr);
C_ORM_EXPORT c_orm_error_t c_orm_system_realloc(void *ptr, size_t size,
                                                void **out_ptr);

#endif /* C_ORM_META_H */
