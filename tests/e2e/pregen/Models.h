#ifndef MODELS_H
#define MODELS_H

/* clang-format off */
#include "c_orm_meta.h"
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
typedef unsigned char bool;
#   define true 1
#   define false 0
#  endif
# endif
#endif
#include <stddef.h>
/* clang-format on */

/**
 * @file users.h
 * @brief Auto-generated C structs for SQL table users.
 */

#ifndef C_ORM_MODEL_USERS_H
#define C_ORM_MODEL_USERS_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @brief Represents a single row of the users table.
 */
struct Users {
  int32_t id;
  char *username;
  char *email;
  int32_t *age;    /**< Nullable */
  float *score;    /**< Nullable */
  bool *is_active; /**< Nullable */
  char *created_at;
};

/**
 * @brief A collection of Users rows.
 */
struct Users_Array {
  struct Users *data;
  size_t length;
  size_t capacity;
};

/**
 * @brief Initialize a Users_Array.
 * @param arr Array to init.
 * @param initial_capacity Initial capacity.
 * @return 0 on success.
 */
c_orm_error_t Users_Array_init(struct Users_Array *arr,
                               size_t initial_capacity);

/**
 * @brief Free resources inside a single Users struct.
 * @param item Pointer to struct.
 */
void Users_free(struct Users *item);

/**
 * @brief Free resources of a Users_Array.
 * @param arr Pointer to array.
 */
void Users_Array_free(struct Users_Array *arr);

/**
 * @brief Deep copy a Users row.
 * @param src Source struct.
 * @param dest Destination struct.
 * @return 0 on success.
 */
c_orm_error_t Users_deepcopy(const struct Users *src, struct Users *dest);

/**
 * @brief Deep copy a Users_Array.
 * @param src Source array.
 * @param dest Destination array.
 * @return 0 on success.
 */
c_orm_error_t Users_Array_deepcopy(const struct Users_Array *src,
                                   struct Users_Array *dest);

#ifdef __cplusplus
}
#endif /* __cplusplus */

extern const c_orm_table_meta_t Users_meta;

#endif /* C_ORM_MODEL_USERS_H */
/**
 * @file posts.h
 * @brief Auto-generated C structs for SQL table posts.
 */

#ifndef C_ORM_MODEL_POSTS_H
#define C_ORM_MODEL_POSTS_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @brief Represents a single row of the posts table.
 */
struct Posts {
  int32_t id;
  int32_t user_id;
  char *title;
  char *content;
  int64_t *views; /**< Nullable */
  char *published_date;
};

/**
 * @brief A collection of Posts rows.
 */
struct Posts_Array {
  struct Posts *data;
  size_t length;
  size_t capacity;
};

/**
 * @brief Initialize a Posts_Array.
 * @param arr Array to init.
 * @param initial_capacity Initial capacity.
 * @return 0 on success.
 */
c_orm_error_t Posts_Array_init(struct Posts_Array *arr,
                               size_t initial_capacity);

/**
 * @brief Free resources inside a single Posts struct.
 * @param item Pointer to struct.
 */
void Posts_free(struct Posts *item);

/**
 * @brief Free resources of a Posts_Array.
 * @param arr Pointer to array.
 */
void Posts_Array_free(struct Posts_Array *arr);

/**
 * @brief Deep copy a Posts row.
 * @param src Source struct.
 * @param dest Destination struct.
 * @return 0 on success.
 */
c_orm_error_t Posts_deepcopy(const struct Posts *src, struct Posts *dest);

/**
 * @brief Deep copy a Posts_Array.
 * @param src Source array.
 * @param dest Destination array.
 * @return 0 on success.
 */
c_orm_error_t Posts_Array_deepcopy(const struct Posts_Array *src,
                                   struct Posts_Array *dest);

#ifdef __cplusplus
}
#endif /* __cplusplus */

extern const c_orm_table_meta_t Posts_meta;

#endif /* C_ORM_MODEL_POSTS_H */
/**
 * @file oauth2_tokens.h
 * @brief Auto-generated C structs for SQL table oauth2_tokens.
 */

#ifndef C_ORM_MODEL_OAUTH2_TOKENS_H
#define C_ORM_MODEL_OAUTH2_TOKENS_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @brief Represents a single row of the oauth2_tokens table.
 */
struct Oauth2_tokens {
  char *access_token;
  char *refresh_token;
  char *token_type;
  int32_t *expires_in; /**< Nullable */
  int64_t *created_at; /**< Nullable */
};

/**
 * @brief A collection of Oauth2_tokens rows.
 */
struct Oauth2_tokens_Array {
  struct Oauth2_tokens *data;
  size_t length;
  size_t capacity;
};

/**
 * @brief Initialize a Oauth2_tokens_Array.
 * @param arr Array to init.
 * @param initial_capacity Initial capacity.
 * @return 0 on success.
 */
c_orm_error_t Oauth2_tokens_Array_init(struct Oauth2_tokens_Array *arr,
                                       size_t initial_capacity);

/**
 * @brief Free resources inside a single Oauth2_tokens struct.
 * @param item Pointer to struct.
 */
void Oauth2_tokens_free(struct Oauth2_tokens *item);

/**
 * @brief Free resources of a Oauth2_tokens_Array.
 * @param arr Pointer to array.
 */
void Oauth2_tokens_Array_free(struct Oauth2_tokens_Array *arr);

/**
 * @brief Deep copy a Oauth2_tokens row.
 * @param src Source struct.
 * @param dest Destination struct.
 * @return 0 on success.
 */
c_orm_error_t Oauth2_tokens_deepcopy(const struct Oauth2_tokens *src,
                                     struct Oauth2_tokens *dest);

/**
 * @brief Deep copy a Oauth2_tokens_Array.
 * @param src Source array.
 * @param dest Destination array.
 * @return 0 on success.
 */
c_orm_error_t
Oauth2_tokens_Array_deepcopy(const struct Oauth2_tokens_Array *src,
                             struct Oauth2_tokens_Array *dest);

#ifdef __cplusplus
}
#endif /* __cplusplus */

extern const c_orm_table_meta_t Oauth2_tokens_meta;

#endif /* C_ORM_MODEL_OAUTH2_TOKENS_H */
#endif
