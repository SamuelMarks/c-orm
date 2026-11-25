/**
 * @file Models.h
 * @defgroup e2e_pregen Pre-generated E2E Models
 * @brief Generated C models and arrays.
 */

#ifndef MODELS_H
#define MODELS_H

/* clang-format off */
#include "c_orm_meta.h"
#include "c_orm_decimal.h"
#if defined(_MSC_VER)
# if _MSC_VER < 1600
typedef signed __int8 int8_t;
typedef unsigned __int8 uint8_t;
typedef signed __int16 int16_t;
typedef unsigned __int16 uint16_t;
typedef signed __int32 int32_t;
typedef unsigned __int32 uint32_t;
typedef signed __int64 int64_t;
typedef unsigned __int64 uint64_t;
# else
#  include <stdint.h>
# endif
#  ifndef __cplusplus
#   ifndef _STDBOOL_H
#    define _STDBOOL_H
#    ifndef __STDBOOL_H
#     define __STDBOOL_H
#    endif
#    ifndef __bool_true_false_are_defined
#     define __bool_true_false_are_defined 1
#    endif
typedef unsigned char bool;
#    define true 1
#    define false 0
#   endif
#  endif
#else
# include <stdint.h>
# ifndef __cplusplus
#  ifndef _STDBOOL_H
#   define _STDBOOL_H
#   ifndef __STDBOOL_H
#    define __STDBOOL_H
#   endif
#   ifndef __bool_true_false_are_defined
#    define __bool_true_false_are_defined 1
#   endif
typedef unsigned char bool;
#   define true 1
#   define false 0
#  endif
# endif
#endif
#include <stddef.h>
/* clang-format on */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef C_ORM_MODEL_USERS_H
#define C_ORM_MODEL_USERS_H

/**
 * @brief Represents a single row of the users table.
 */
struct Users {
  /** @brief Column id of table users. */
  int32_t id;
  /** @brief Column username of table users. */
  char *username;
  /** @brief Column email of table users. */
  char *email;
  /** @brief Column age of table users (nullable). */
  int32_t *age; /**< Nullable */
  /** @brief Column score of table users (nullable). */
  float *score; /**< Nullable */
  /** @brief Column is_active of table users (nullable). */
  bool *is_active; /**< Nullable */
  /** @brief Column created_at of table users. */
  char *created_at;
};

/**
 * @brief A collection of Users rows.
 */
struct Users_Array {
  /** @brief Pointer to allocated Users row array. */
  struct Users *data;
  /** @brief Number of populated elements. */
  size_t length;
  /** @brief Allocated element capacity. */
  size_t capacity;
};

/**
 * @brief Initialize a Users_Array.
 * @param arr Array to init.
 * @param initial_capacity Initial capacity.
 * @return C_ORM_OK on success.
 */
c_orm_error_t Users_Array_init(struct Users_Array *arr,
                               size_t initial_capacity);

/**
 * @brief Free resources inside a single Users struct.
 * @param item Pointer to struct.
 * @return C_ORM_OK on success.
 */
c_orm_error_t Users_free(struct Users *item);

/**
 * @brief Free resources of a Users_Array.
 * @param arr Pointer to array.
 * @return C_ORM_OK on success.
 */
c_orm_error_t Users_Array_free(struct Users_Array *arr);

/**
 * @brief Deep copy a Users row.
 * @param src Source struct.
 * @param dest Destination struct.
 * @return C_ORM_OK on success.
 */
c_orm_error_t Users_deepcopy(const struct Users *src, struct Users *dest);

/**
 * @brief Deep copy a Users_Array.
 * @param src Source array.
 * @param dest Destination array.
 * @return C_ORM_OK on success.
 */
c_orm_error_t Users_Array_deepcopy(const struct Users_Array *src,
                                   struct Users_Array *dest);

extern const c_orm_table_meta_t Users_meta;

#endif /* C_ORM_MODEL_USERS_H */

#ifndef C_ORM_MODEL_POSTS_H
#define C_ORM_MODEL_POSTS_H

/**
 * @brief Represents a single row of the posts table.
 */
struct Posts {
  /** @brief Column id of table posts. */
  int32_t id;
  /** @brief Column user_id of table posts. */
  int32_t user_id;
  /** @brief Column title of table posts. */
  char *title;
  /** @brief Column content of table posts. */
  char *content;
  /** @brief Column views of table posts (nullable). */
  int64_t *views; /**< Nullable */
  /** @brief Column published_date of table posts. */
  char *published_date;
};

/**
 * @brief A collection of Posts rows.
 */
struct Posts_Array {
  /** @brief Pointer to allocated Posts row array. */
  struct Posts *data;
  /** @brief Number of populated elements. */
  size_t length;
  /** @brief Allocated element capacity. */
  size_t capacity;
};

/**
 * @brief Initialize a Posts_Array.
 * @param arr Array to init.
 * @param initial_capacity Initial capacity.
 * @return C_ORM_OK on success.
 */
c_orm_error_t Posts_Array_init(struct Posts_Array *arr,
                               size_t initial_capacity);

/**
 * @brief Free resources inside a single Posts struct.
 * @param item Pointer to struct.
 * @return C_ORM_OK on success.
 */
c_orm_error_t Posts_free(struct Posts *item);

/**
 * @brief Free resources of a Posts_Array.
 * @param arr Pointer to array.
 * @return C_ORM_OK on success.
 */
c_orm_error_t Posts_Array_free(struct Posts_Array *arr);

/**
 * @brief Deep copy a Posts row.
 * @param src Source struct.
 * @param dest Destination struct.
 * @return C_ORM_OK on success.
 */
c_orm_error_t Posts_deepcopy(const struct Posts *src, struct Posts *dest);

/**
 * @brief Deep copy a Posts_Array.
 * @param src Source array.
 * @param dest Destination array.
 * @return C_ORM_OK on success.
 */
c_orm_error_t Posts_Array_deepcopy(const struct Posts_Array *src,
                                   struct Posts_Array *dest);

extern const c_orm_table_meta_t Posts_meta;

#endif /* C_ORM_MODEL_POSTS_H */

#ifndef C_ORM_MODEL_OAUTH2_TOKENS_H
#define C_ORM_MODEL_OAUTH2_TOKENS_H

/**
 * @brief Represents a single row of the oauth2_tokens table.
 */
struct Oauth2_tokens {
  /** @brief Column access_token of table oauth2_tokens. */
  char *access_token;
  /** @brief Column refresh_token of table oauth2_tokens. */
  char *refresh_token;
  /** @brief Column token_type of table oauth2_tokens. */
  char *token_type;
  /** @brief Column expires_in of table oauth2_tokens (nullable). */
  int32_t *expires_in; /**< Nullable */
  /** @brief Column created_at of table oauth2_tokens (nullable). */
  int64_t *created_at; /**< Nullable */
};

/**
 * @brief A collection of Oauth2_tokens rows.
 */
struct Oauth2_tokens_Array {
  /** @brief Pointer to allocated Oauth2_tokens row array. */
  struct Oauth2_tokens *data;
  /** @brief Number of populated elements. */
  size_t length;
  /** @brief Allocated element capacity. */
  size_t capacity;
};

/**
 * @brief Initialize a Oauth2_tokens_Array.
 * @param arr Array to init.
 * @param initial_capacity Initial capacity.
 * @return C_ORM_OK on success.
 */
c_orm_error_t Oauth2_tokens_Array_init(struct Oauth2_tokens_Array *arr,
                                       size_t initial_capacity);

/**
 * @brief Free resources inside a single Oauth2_tokens struct.
 * @param item Pointer to struct.
 * @return C_ORM_OK on success.
 */
c_orm_error_t Oauth2_tokens_free(struct Oauth2_tokens *item);

/**
 * @brief Free resources of a Oauth2_tokens_Array.
 * @param arr Pointer to array.
 * @return C_ORM_OK on success.
 */
c_orm_error_t Oauth2_tokens_Array_free(struct Oauth2_tokens_Array *arr);

/**
 * @brief Deep copy a Oauth2_tokens row.
 * @param src Source struct.
 * @param dest Destination struct.
 * @return C_ORM_OK on success.
 */
c_orm_error_t Oauth2_tokens_deepcopy(const struct Oauth2_tokens *src,
                                     struct Oauth2_tokens *dest);

/**
 * @brief Deep copy a Oauth2_tokens_Array.
 * @param src Source array.
 * @param dest Destination array.
 * @return C_ORM_OK on success.
 */
c_orm_error_t
Oauth2_tokens_Array_deepcopy(const struct Oauth2_tokens_Array *src,
                             struct Oauth2_tokens_Array *dest);

extern const c_orm_table_meta_t Oauth2_tokens_meta;

#endif /* C_ORM_MODEL_OAUTH2_TOKENS_H */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* MODELS_H */
