#if defined(__clang__) || defined(__GNUC__)
#endif

/**
 * @file c_orm_inline_macros.h
 * @brief Compile-time macro engine to generate cdd-c compatible metadata (Steps
 * 136-139).
 */

#ifndef C_ORM_INLINE_MACROS_H
#define C_ORM_INLINE_MACROS_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_meta.h"
/* clang-format on */

/**
 * @brief Macro to define a "Has One" relationship in a struct.
 * Usage: C_ORM_HAS_ONE(struct TargetType, target_field_name);
 */
#define C_ORM_HAS_ONE(Type, name) Type *name

/**
 * @brief Macro to define a "Belongs To" relationship in a struct.
 * Usage: C_ORM_BELONGS_TO(struct TargetType, target_field_name);
 */
#define C_ORM_BELONGS_TO(Type, name) Type *name

/**
 * @brief Macro to define a "Has Many" relationship in a struct.
 * Usage: C_ORM_HAS_MANY(struct TargetType, target_field_name);
 */
#define C_ORM_HAS_MANY(Type, name)                                             \
  size_t num_##name;                                                           \
  Type *name

/**
 * @brief Macro to define a "Many To Many" relationship in a struct.
 * Usage: C_ORM_MANY_TO_MANY(struct TargetType, target_field_name);
 */
#define C_ORM_MANY_TO_MANY(Type, name)                                         \
  size_t num_##name;                                                           \
  Type *name

/* Step 137: Implement C_ORM_DEFINE_COLUMN macro generating cdd-c compatible
 * tokens */
/**
 * @brief Macro to define a column mapping for a struct property.
 * Includes Step 140 (string length bounds) inherently through standard C arrays
 * or dynamic pointer constraints in higher level types, and Step 141 (nullable
 * semantics) via the is_nullable boolean.
 */
#define C_ORM_DEFINE_COLUMN(name_val, type_val, offset_val, is_pk_val,         \
                            is_nullable_val, fk_target_val,                    \
                            on_delete_cascade_val, is_secure_val)              \
  {name_val,                                                                   \
   type_val,                                                                   \
   offset_val,                                                                 \
   is_pk_val,                                                                  \
   is_nullable_val,                                                            \
   fk_target_val,                                                              \
   on_delete_cascade_val,                                                      \
   is_secure_val}

/* Step 138: Implement C_ORM_DEFINE_RELATION macro */
#define C_ORM_DEFINE_RELATION(field_name_val, type_val, target_table_val,      \
                              foreign_key_val, local_key_val,                  \
                              struct_offset_val, target_array_len_offset_val)  \
  {field_name_val,                                                             \
   type_val,                                                                   \
   target_table_val,                                                           \
   foreign_key_val,                                                            \
   local_key_val,                                                              \
   struct_offset_val,                                                          \
   target_array_len_offset_val,                                                \
   NULL,                                                                       \
   NULL,                                                                       \
   NULL,                                                                       \
   0,                                                                          \
   C_ORM_CASCADE_NONE,                                                         \
   C_ORM_CASCADE_NONE,                                                         \
   NULL,                                                                       \
   NULL,                                                                       \
   NULL}

/* Step 139: Create macro engine to generate cdd-c compatible metadata at
 * compile time */
#define C_ORM_DEFINE_MODEL(                                                    \
    table_name_val, cols_array_name, num_cols, struct_size_val, q_select_all,  \
    q_select_pk, q_insert, q_update, q_delete, q_select_pk_for_update,         \
    has_ttl_val, created_at_offset_val, expires_in_offset_val,                 \
    relations_array_name, num_rels)                                            \
  {table_name_val,                                                             \
   cols_array_name,                                                            \
   num_cols,                                                                   \
   struct_size_val,                                                            \
   q_select_all,                                                               \
   q_select_pk,                                                                \
   q_insert,                                                                   \
   q_update,                                                                   \
   q_delete,                                                                   \
   q_select_pk_for_update,                                                     \
   false,                                                                      \
   has_ttl_val,                                                                \
   created_at_offset_val,                                                      \
   expires_in_offset_val,                                                      \
   {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},                           \
   relations_array_name,                                                       \
   num_rels}

#define C_ORM_DEFINE_VIEW(view_name_val, cols_array_name, num_cols,            \
                          struct_size_val, q_select_all)                       \
  {view_name_val,                                                              \
   cols_array_name,                                                            \
   num_cols,                                                                   \
   struct_size_val,                                                            \
   q_select_all,                                                               \
   NULL,                                                                       \
   NULL,                                                                       \
   NULL,                                                                       \
   NULL,                                                                       \
   NULL,                                                                       \
   true,                                                                       \
   false,                                                                      \
   0,                                                                          \
   0,                                                                          \
   {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},                           \
   NULL,                                                                       \
   0}

#if defined(__cplusplus)
}
#endif /* __cplusplus */
#endif /* C_ORM_INLINE_MACROS_H */

#if defined(__clang__) || defined(__GNUC__)
#endif
