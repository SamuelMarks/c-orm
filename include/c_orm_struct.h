/**
 * @file c_orm_struct.h
 * @brief Macro definitions for Diesel/Serde style fully-typed struct mapping.
 *
 * This file provides the `C_ORM_STRUCT` macro which allows developers to define
 * C structures representing database tables and automatically generate the
 * required runtime reflection metadata (`c_orm_table_meta_t`) for `c-orm`.
 */

#ifndef C_ORM_STRUCT_H
#define C_ORM_STRUCT_H

/* clang-format off */
#include "c_orm_meta.h"
#include <stddef.h>
/* clang-format on */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Internal helper to generate a C struct field definition.
 *
 * @param struct_name The name of the struct (passed implicitly by X-Macro).
 * @param orm_type The `c_orm_type_t` enum value indicating the database type.
 * @param c_type The C native data type of the field.
 * @param name The name of the field.
 */
#define C_ORM_FIELD_DEF(struct_name, orm_type, c_type, name) c_type name;

/**
 * @brief Internal helper to generate a `c_orm_column_meta_t` entry for a field.
 *
 * @param struct_name The name of the struct to calculate `offsetof` against.
 * @param orm_type The `c_orm_type_t` enum value indicating the database type.
 * @param c_type The C native data type of the field (ignored here).
 * @param name The name of the field.
 */
#define C_ORM_FIELD_META(struct_name, orm_type, c_type, name)                  \
  {#name, orm_type, offsetof(struct struct_name, name), false, false, NULL,    \
   false, false},

/**
 * @brief Generates a relationship field definition.
 *
 * Uses token pasting to generate the correct internal representation (pointer
 * vs array).
 */
#define C_ORM_RELATION_DEF_C_ORM_RELATION_ONE_TO_ONE(struct_name, target_type, \
                                                     name)                     \
  struct target_type *data;

#define C_ORM_RELATION_DEF_C_ORM_RELATION_BELONGS_TO(struct_name, target_type, \
                                                     name)                     \
  struct target_type *data;

#define C_ORM_RELATION_DEF_C_ORM_RELATION_ONE_TO_MANY(struct_name,             \
                                                      target_type, name)       \
  struct {                                                                     \
    struct target_type *data;                                                  \
    size_t length;                                                             \
    size_t capacity;                                                           \
  } data;

#define C_ORM_RELATION_DEF_C_ORM_RELATION_MANY_TO_MANY(struct_name,            \
                                                       target_type, name)      \
  struct {                                                                     \
    struct target_type *data;                                                  \
    size_t length;                                                             \
    size_t capacity;                                                           \
  } data;

#define C_ORM_RELATION_DEF_C_ORM_RELATION_HAS_MANY_THROUGH(struct_name,        \
                                                           target_type, name)  \
  struct {                                                                     \
    struct target_type *data;                                                  \
    size_t length;                                                             \
    size_t capacity;                                                           \
  } data;

#define C_ORM_RELATION_META_OFFSET_C_ORM_RELATION_ONE_TO_ONE(struct_name,      \
                                                             name)             \
  0
#define C_ORM_RELATION_META_OFFSET_C_ORM_RELATION_BELONGS_TO(struct_name,      \
                                                             name)             \
  0
#define C_ORM_RELATION_META_OFFSET_C_ORM_RELATION_ONE_TO_MANY(struct_name,     \
                                                              name)            \
  offsetof(struct struct_name, name.data.length)
#define C_ORM_RELATION_META_OFFSET_C_ORM_RELATION_MANY_TO_MANY(struct_name,    \
                                                               name)           \
  offsetof(struct struct_name, name.data.length)
#define C_ORM_RELATION_META_OFFSET_C_ORM_RELATION_HAS_MANY_THROUGH(            \
    struct_name, name)                                                         \
  offsetof(struct struct_name, name.data.length)

/**
 * @brief Generates a relationship field definition using a proxy struct for
 * lazy loading.
 *
 * @param struct_name The name of the struct (passed implicitly by X-Macro).
 * @param rel_type The relationship type (`C_ORM_RELATION_BELONGS_TO`, etc).
 * @param target_type The target struct type name.
 * @param name The name of the field.
 * @param foreign_key The foreign key column name.
 * @param local_key The local key column name.
 */
#define C_ORM_RELATION_DEF(struct_name, rel_type, target_type, name,           \
                           foreign_key, local_key)                             \
  struct {                                                                     \
    c_orm_lazy_load_context_t lazy_ctx;                                        \
    C_ORM_RELATION_DEF_##rel_type(struct_name, target_type, name)              \
  } name;

/**
 * @brief Generates a `c_orm_relation_meta_t` entry for a relationship.
 *
 * @param struct_name The name of the struct.
 * @param rel_type The relationship type (`C_ORM_RELATION_BELONGS_TO`, etc).
 * @param target_type The target struct type name.
 * @param name The name of the field.
 * @param foreign_key The foreign key column name.
 * @param local_key The local key column name.
 */
#define C_ORM_RELATION_META(struct_name, rel_type, target_type, name,          \
                            foreign_key, local_key)                            \
  {#name,                                                                      \
   rel_type,                                                                   \
   #target_type,                                                               \
   foreign_key,                                                                \
   local_key,                                                                  \
   offsetof(struct struct_name, name),                                         \
   offsetof(struct struct_name, name.data),                                    \
   offsetof(struct struct_name, name.lazy_ctx),                                \
   C_ORM_RELATION_META_OFFSET_##rel_type(struct_name, name),                   \
   NULL,                                                                       \
   &target_type##_meta,                                                        \
   NULL,                                                                       \
   NULL,                                                                       \
   0,                                                                          \
   C_ORM_CASCADE_NONE,                                                         \
   C_ORM_CASCADE_NONE,                                                         \
   NULL,                                                                       \
   NULL,                                                                       \
   NULL},

/**
 * @brief Generates a `c_orm_relation_meta_t` entry for a relationship with
 * explicit cascading rules.
 *
 * @param struct_name The name of the struct.
 * @param rel_type The relationship type (`C_ORM_RELATION_BELONGS_TO`, etc).
 * @param target_type The target struct type name.
 * @param name The name of the field.
 * @param foreign_key The foreign key column name.
 * @param local_key The local key column name.
 * @param on_delete The cascade rule for deletion (`C_ORM_CASCADE_DELETE`, etc).
 * @param on_update The cascade rule for updates.
 */
#define C_ORM_RELATION_META_CASCADE(struct_name, rel_type, target_type, name,  \
                                    foreign_key, local_key, on_delete,         \
                                    on_update)                                 \
  {#name,                                                                      \
   rel_type,                                                                   \
   #target_type,                                                               \
   foreign_key,                                                                \
   local_key,                                                                  \
   offsetof(struct struct_name, name),                                         \
   offsetof(struct struct_name, name.data),                                    \
   offsetof(struct struct_name, name.lazy_ctx),                                \
   C_ORM_RELATION_META_OFFSET_##rel_type(struct_name, name),                   \
   NULL,                                                                       \
   &target_type##_meta,                                                        \
   NULL,                                                                       \
   NULL,                                                                       \
   0,                                                                          \
   on_delete,                                                                  \
   on_update,                                                                  \
   NULL,                                                                       \
   NULL,                                                                       \
   NULL},

/**
 * @brief Generate a struct and its corresponding ORM metadata.
 *
 * This macro uses the X-Macro pattern to generate both a strongly typed
 * C struct and a `c_orm_table_meta_t` descriptor without duplicating the
 * field definitions.
 *
 * **Example Usage:**
 * ```c
 * #define MY_USER_FIELDS(X, STRUCT_NAME) \
 *   X(STRUCT_NAME, C_ORM_TYPE_INT32, int32_t, id) \
 *   X(STRUCT_NAME, C_ORM_TYPE_STRING, char*, username)
 *
 * C_ORM_STRUCT(User, MY_USER_FIELDS)
 * ```
 *
 * This generates `struct User` and `extern const c_orm_table_meta_t User_meta`.
 *
 * @param STRUCT_NAME The name of the struct to generate.
 * @param FIELDS_MACRO An X-Macro defining the fields.
 */
#define C_ORM_STRUCT(STRUCT_NAME, FIELDS_MACRO)                                \
  struct STRUCT_NAME {                                                         \
    FIELDS_MACRO(C_ORM_FIELD_DEF, STRUCT_NAME)                                 \
  };                                                                           \
  const c_orm_column_meta_t STRUCT_NAME##_columns[] = {                        \
      FIELDS_MACRO(C_ORM_FIELD_META, STRUCT_NAME)};                            \
  const c_orm_table_meta_t STRUCT_NAME##_meta = {                              \
      #STRUCT_NAME,                                                            \
      STRUCT_NAME##_columns,                                                   \
      sizeof(STRUCT_NAME##_columns) / sizeof(STRUCT_NAME##_columns[0]),        \
      sizeof(struct STRUCT_NAME),                                              \
      NULL,                                                                    \
      NULL,                                                                    \
      NULL,                                                                    \
      NULL,                                                                    \
      NULL,                                                                    \
      NULL,                                                                    \
      false,                                                                   \
      false,                                                                   \
      0,                                                                       \
      0,                                                                       \
      {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},                        \
      NULL,                                                                    \
      0};

#define C_ORM_RELATION_META_ALL(struct_name, rel_type, target_type, name,      \
                                foreign_key, local_key, on_delete, on_update,  \
                                join_table, join_lkey, join_fkey)              \
  {#name,                                                                      \
   rel_type,                                                                   \
   #target_type,                                                               \
   foreign_key,                                                                \
   local_key,                                                                  \
   offsetof(struct struct_name, name),                                         \
   offsetof(struct struct_name, name.data),                                    \
   offsetof(struct struct_name, name.lazy_ctx),                                \
   C_ORM_RELATION_META_OFFSET_##rel_type(struct_name, name),                   \
   NULL,                                                                       \
   &target_type##_meta,                                                        \
   NULL,                                                                       \
   NULL,                                                                       \
   0,                                                                          \
   on_delete,                                                                  \
   on_update,                                                                  \
   join_table,                                                                 \
   join_lkey,                                                                  \
   join_fkey,                                                                  \
   NULL,                                                                       \
   NULL,                                                                       \
   NULL,                                                                       \
   NULL,                                                                       \
   0},

#define C_ORM_RELATION_FWD_ALL(struct_name, rel_type, target_type, name,       \
                               foreign_key, local_key, on_delete, on_update,   \
                               join_table, join_lkey, join_fkey)               \
  extern const c_orm_table_meta_t target_type##_meta;

#define C_ORM_RELATION_DEF_ALL(struct_name, rel_type, target_type, name,       \
                               foreign_key, local_key, on_delete, on_update,   \
                               join_table, join_lkey, join_fkey)               \
  struct {                                                                     \
    c_orm_lazy_load_context_t lazy_ctx;                                        \
    C_ORM_RELATION_DEF_##rel_type(struct_name, target_type, name)              \
  } name;

#define C_ORM_BELONGS_TO(X, S, target, name, fkey, lkey)                       \
  X(S, C_ORM_RELATION_BELONGS_TO, target, name, fkey, lkey,                    \
    C_ORM_CASCADE_NONE, C_ORM_CASCADE_NONE, NULL, NULL, NULL)
#define C_ORM_HAS_ONE(X, S, target, name, fkey, lkey)                          \
  X(S, C_ORM_RELATION_ONE_TO_ONE, target, name, fkey, lkey,                    \
    C_ORM_CASCADE_NONE, C_ORM_CASCADE_NONE, NULL, NULL, NULL)
#define C_ORM_HAS_MANY(X, S, target, name, fkey, lkey)                         \
  X(S, C_ORM_RELATION_ONE_TO_MANY, target, name, fkey, lkey,                   \
    C_ORM_CASCADE_NONE, C_ORM_CASCADE_NONE, NULL, NULL, NULL)
#define C_ORM_MANY_TO_MANY(X, S, target, name, fkey, lkey, jt, jlk, jfk)       \
  X(S, C_ORM_RELATION_MANY_TO_MANY, target, name, fkey, lkey,                  \
    C_ORM_CASCADE_NONE, C_ORM_CASCADE_NONE, jt, jlk, jfk)
/**
 * @brief Defines a Has-Many relationship through a pivot table mapping directly
 * to a target model.
 */
#define C_ORM_HAS_MANY_THROUGH(X, S, target, name, fkey, lkey, jt, jlk, jfk)   \
  X(S, C_ORM_RELATION_HAS_MANY_THROUGH, target, name, fkey, lkey,              \
    C_ORM_CASCADE_NONE, C_ORM_CASCADE_NONE, jt, jlk, jfk)

/**
 * @brief Defines a Belongs-To relationship with cascade rules.
 */
#define C_ORM_BELONGS_TO_CASCADE(X, S, target, name, fkey, lkey, od, ou)       \
  X(S, C_ORM_RELATION_BELONGS_TO, target, name, fkey, lkey, od, ou, NULL,      \
    NULL, NULL)

/**
 * @brief Defines a Has-One relationship with cascade rules.
 */
#define C_ORM_HAS_ONE_CASCADE(X, S, target, name, fkey, lkey, od, ou)          \
  X(S, C_ORM_RELATION_ONE_TO_ONE, target, name, fkey, lkey, od, ou, NULL,      \
    NULL, NULL)

/**
 * @brief Defines a Has-Many relationship with cascade rules.
 */
#define C_ORM_HAS_MANY_CASCADE(X, S, target, name, fkey, lkey, od, ou)         \
  X(S, C_ORM_RELATION_ONE_TO_MANY, target, name, fkey, lkey, od, ou, NULL,     \
    NULL, NULL)

/**
 * @brief Defines a Many-To-Many relationship with cascade rules.
 */
#define C_ORM_MANY_TO_MANY_CASCADE(X, S, target, name, fkey, lkey, jt, jlk,    \
                                   jfk, od, ou)                                \
  X(S, C_ORM_RELATION_MANY_TO_MANY, target, name, fkey, lkey, od, ou, jt, jlk, \
    jfk)

#define C_ORM_STRUCT_WITH_RELATIONS(STRUCT_NAME, FIELDS_MACRO,                 \
                                    RELATIONS_MACRO)                           \
  RELATIONS_MACRO(C_ORM_RELATION_FWD_ALL, STRUCT_NAME)                         \
  struct STRUCT_NAME {                                                         \
    FIELDS_MACRO(C_ORM_FIELD_DEF, STRUCT_NAME)                                 \
    RELATIONS_MACRO(C_ORM_RELATION_DEF_ALL, STRUCT_NAME)                       \
  };                                                                           \
  const c_orm_column_meta_t STRUCT_NAME##_columns[] = {                        \
      FIELDS_MACRO(C_ORM_FIELD_META, STRUCT_NAME)};                            \
  const c_orm_relation_meta_t STRUCT_NAME##_relations[] = {                    \
      RELATIONS_MACRO(C_ORM_RELATION_META_ALL, STRUCT_NAME)};                  \
  const c_orm_table_meta_t STRUCT_NAME##_meta = {                              \
      #STRUCT_NAME,                                                            \
      STRUCT_NAME##_columns,                                                   \
      sizeof(STRUCT_NAME##_columns) / sizeof(STRUCT_NAME##_columns[0]),        \
      sizeof(struct STRUCT_NAME),                                              \
      NULL,                                                                    \
      NULL,                                                                    \
      NULL,                                                                    \
      NULL,                                                                    \
      NULL,                                                                    \
      NULL,                                                                    \
      false,                                                                   \
      false,                                                                   \
      0,                                                                       \
      0,                                                                       \
      {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},                        \
      STRUCT_NAME##_relations,                                                 \
      sizeof(STRUCT_NAME##_relations) / sizeof(STRUCT_NAME##_relations[0])};

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* C_ORM_STRUCT_H */

