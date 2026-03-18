/**
 * @file c_orm_meta.h
 * @brief Core definitions and structures for c-orm.
 */

#ifndef C_ORM_META_H
#define C_ORM_META_H
/* clang-format off */
#if !defined(_MSC_VER) || _MSC_VER >= 1800

#include <stdbool.h>
#else
#ifndef __cplusplus
#ifndef bool
typedef unsigned char bool;
#define true 1
#define false 0
#endif
#endif
#endif

#include <stddef.h>
/* clang-format on */

#ifdef __cplusplus
extern "C" {
#endif

#ifndef C_ORM_EXPORT
#if defined(_WIN32) || defined(__CYGWIN__)
#if defined(C_ORM_SHARED)
#if defined(c_orm_EXPORTS)
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
  C_ORM_TYPE_INT32,
  C_ORM_TYPE_INT64,
  C_ORM_TYPE_FLOAT,
  C_ORM_TYPE_DOUBLE,
  C_ORM_TYPE_BOOL,
  C_ORM_TYPE_STRING,
  C_ORM_TYPE_BLOB,
  C_ORM_TYPE_DATE,
  C_ORM_TYPE_TIMESTAMP,
  C_ORM_TYPE_UNKNOWN
} c_orm_type_t;

/**
 * @brief Represents binary large object (BLOB) data.
 */
typedef struct {
  void *data;
  size_t size;
} c_orm_blob_t;

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
 * @brief Table metadata definition.
 */
typedef struct {
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

  /* TTL & Expiration Tracking */
  bool has_ttl; /**< True if rows in this table can expire automatically */
  size_t created_at_offset; /**< Offset for the created_at UNIX timestamp
                               (int64) */
  size_t expires_in_offset; /**< Offset for the expires_in duration in seconds
                               (int32) */
} c_orm_table_meta_t;

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* C_ORM_META_H */
