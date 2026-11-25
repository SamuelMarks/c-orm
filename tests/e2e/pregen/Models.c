/**
 * @file Models.c
 * @defgroup e2e_pregen Pre-generated E2E Models
 * @brief Generated implementation for models and arrays.
 */

/* clang-format off */
#include "Models.h"
#include "c_orm_safe_crt.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

/**
 * @brief Initialize a Users_Array.
 * @param arr Array to init.
 * @param initial_capacity Initial capacity.
 * @return C_ORM_OK on success or C_ORM_ERROR_MEMORY.
 */
c_orm_error_t Users_Array_init(struct Users_Array *arr,
                               size_t initial_capacity) {
  c_orm_error_t rc;
  if (!arr)
    return C_ORM_ERROR_MEMORY;
  arr->length = 0;
  arr->capacity = initial_capacity;
  if (initial_capacity > 0) {
    rc = c_orm_system_calloc(initial_capacity, sizeof(struct Users),
                             (void **)&arr->data);
    if (rc != C_ORM_OK) {
      return rc;
    }
  } else {
    arr->data = NULL;
  }
  return C_ORM_OK;
}

/**
 * @brief Free resources inside a Users row.
 * @param item Item to free.
 * @return C_ORM_OK on success.
 */
c_orm_error_t Users_free(struct Users *item) {
  if (!item)
    return C_ORM_OK;
  if (item->username) {
    c_orm_system_free(item->username);
    item->username = NULL;
  }
  if (item->email) {
    c_orm_system_free(item->email);
    item->email = NULL;
  }
  if (item->age) {
    c_orm_system_free(item->age);
    item->age = NULL;
  }
  if (item->score) {
    c_orm_system_free(item->score);
    item->score = NULL;
  }
  if (item->is_active) {
    c_orm_system_free(item->is_active);
    item->is_active = NULL;
  }
  if (item->created_at) {
    c_orm_system_free(item->created_at);
    item->created_at = NULL;
  }
  return C_ORM_OK;
}

/**
 * @brief Free a Users_Array and all its elements.
 * @param arr Array to free.
 * @return C_ORM_OK on success.
 */
c_orm_error_t Users_Array_free(struct Users_Array *arr) {
  size_t i;
  if (!arr)
    return C_ORM_OK;
  if (arr->data) {
    for (i = 0; i < arr->length; ++i) {
      Users_free(&arr->data[i]);
    }
    c_orm_system_free(arr->data);
    arr->data = NULL;
  }
  arr->length = 0;
  arr->capacity = 0;
  return C_ORM_OK;
}

/**
 * @brief Deep copy a Users row.
 * @param src Source item.
 * @param dest Destination item.
 * @return C_ORM_OK on success or C_ORM_ERROR_MEMORY.
 */
c_orm_error_t Users_deepcopy(const struct Users *src, struct Users *dest) {
  c_orm_error_t rc;
  if (!src || !dest)
    return C_ORM_ERROR_MEMORY;
  memset(dest, 0, sizeof(*dest));
  dest->id = src->id;
  if (src->username) {
    rc = c_orm_strdup(src->username, &dest->username);
    if (rc != C_ORM_OK) {
      Users_free(dest);
      return rc;
    }
  } else {
    dest->username = NULL;
  }
  if (src->email) {
    rc = c_orm_strdup(src->email, &dest->email);
    if (rc != C_ORM_OK) {
      Users_free(dest);
      return rc;
    }
  } else {
    dest->email = NULL;
  }
  if (src->age) {
    rc = c_orm_system_malloc(sizeof(int32_t), (void **)&dest->age);
    if (rc != C_ORM_OK) {
      Users_free(dest);
      return rc;
    }
    *dest->age = *src->age;
  } else {
    dest->age = NULL;
  }
  if (src->score) {
    rc = c_orm_system_malloc(sizeof(float), (void **)&dest->score);
    if (rc != C_ORM_OK) {
      Users_free(dest);
      return rc;
    }
    *dest->score = *src->score;
  } else {
    dest->score = NULL;
  }
  if (src->is_active) {
    rc = c_orm_system_malloc(sizeof(bool), (void **)&dest->is_active);
    if (rc != C_ORM_OK) {
      Users_free(dest);
      return rc;
    }
    *dest->is_active = *src->is_active;
  } else {
    dest->is_active = NULL;
  }
  if (src->created_at) {
    rc = c_orm_strdup(src->created_at, &dest->created_at);
    if (rc != C_ORM_OK) {
      Users_free(dest);
      return rc;
    }
  } else {
    dest->created_at = NULL;
  }
  return C_ORM_OK;
}

/**
 * @brief Deep copy a Users_Array.
 * @param src Source array.
 * @param dest Destination array.
 * @return C_ORM_OK on success or C_ORM_ERROR_MEMORY.
 */
c_orm_error_t Users_Array_deepcopy(const struct Users_Array *src,
                                   struct Users_Array *dest) {
  size_t i;
  c_orm_error_t err;
  if (!src || !dest)
    return C_ORM_ERROR_MEMORY;
  err = Users_Array_init(dest, src->capacity);
  if (err != C_ORM_OK)
    return err;
  for (i = 0; i < src->length; ++i) {
    err = Users_deepcopy(&src->data[i], &dest->data[i]);
    if (err != C_ORM_OK) {
      dest->length = i;
      Users_Array_free(dest);
      return err;
    }
  }
  dest->length = src->length;
  return C_ORM_OK;
}

/** @brief SQL SELECT all query for Users. */
#define Users_query_select_all "SELECT * FROM users"

/** @brief SQL SELECT by primary key query for Users. */
#define Users_query_select_by_pk "SELECT * FROM users WHERE id = ?"

/** @brief SQL DELETE by primary key query for Users. */
#define Users_query_delete_by_pk "DELETE FROM users WHERE id = ?"

/** @brief SQL INSERT query for Users. */
#define Users_query_insert                                                     \
  "INSERT INTO users (id, username, email, age, score, is_active, "            \
  "created_at) VALUES (?, ?, ?, ?, ?, ?, ?)"

/** @brief SQL UPDATE query for Users. */
#define Users_query_update                                                     \
  "UPDATE users SET id = ?, username = ?, email = ?, age = ?, score = ?, "     \
  "is_active = ?, created_at = ? WHERE id = ?"

/** @brief Column names for Users. */
const char *Users_col_names[] = {"id",    "username",  "email",     "age",
                                 "score", "is_active", "created_at"};

/** @brief Column types for Users. */
const c_orm_type_t Users_col_types[] = {
    C_ORM_TYPE_INT32, C_ORM_TYPE_STRING, C_ORM_TYPE_STRING,   C_ORM_TYPE_INT32,
    C_ORM_TYPE_FLOAT, C_ORM_TYPE_BOOL,   C_ORM_TYPE_TIMESTAMP};

/** @brief Column struct byte offsets for Users. */
const size_t Users_col_offsets[] = {
    offsetof(struct Users, id),        offsetof(struct Users, username),
    offsetof(struct Users, email),     offsetof(struct Users, age),
    offsetof(struct Users, score),     offsetof(struct Users, is_active),
    offsetof(struct Users, created_at)};

/** @brief Primary key flags for Users columns. */
const bool Users_col_is_pk[] = {true, false, false, false, false, false, false};

/** @brief Nullability flags for Users columns. */
const bool Users_col_is_nullable[] = {false, false, false, true,
                                      true,  true,  true};

/** @brief Foreign key target descriptors for Users columns. */
const char *Users_col_fk_targets[] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL};

/** @brief Size in bytes of struct Users. */
const size_t Users_struct_size = sizeof(struct Users);

/** @brief Column reflection metadata array for Users. */
const c_orm_column_meta_t Users_meta_columns[] = {
    {"id", C_ORM_TYPE_INT32, offsetof(struct Users, id), true, false, NULL,
     false, false, 0, 0, 0, 0},
    {"username", C_ORM_TYPE_STRING, offsetof(struct Users, username), false,
     false, NULL, false, false, 0, 0, 0, 0},
    {"email", C_ORM_TYPE_STRING, offsetof(struct Users, email), false, false,
     NULL, false, false, 0, 0, 0, 0},
    {"age", C_ORM_TYPE_INT32, offsetof(struct Users, age), false, true, NULL,
     false, false, 0, 0, 0, 0},
    {"score", C_ORM_TYPE_FLOAT, offsetof(struct Users, score), false, true,
     NULL, false, false, 0, 0, 0, 0},
    {"is_active", C_ORM_TYPE_BOOL, offsetof(struct Users, is_active), false,
     true, NULL, false, false, 0, 0, 0, 0},
    {"created_at", C_ORM_TYPE_TIMESTAMP, offsetof(struct Users, created_at),
     false, true, NULL, false, false, 0, 0, 0, 0}};

/** @brief Table reflection metadata descriptor for Users. */
const c_orm_table_meta_t Users_meta = {"users",
                                       Users_meta_columns,
                                       7,
                                       sizeof(struct Users),
                                       Users_query_select_all,
                                       Users_query_select_by_pk,
                                       Users_query_insert,
                                       Users_query_update,
                                       Users_query_delete_by_pk,
                                       NULL,
                                       false,
                                       false,
                                       0,
                                       0,
                                       {0},
                                       NULL,
                                       0};

/**
 * @brief Initialize a Posts_Array.
 * @param arr Array to init.
 * @param initial_capacity Initial capacity.
 * @return C_ORM_OK on success or C_ORM_ERROR_MEMORY.
 */
c_orm_error_t Posts_Array_init(struct Posts_Array *arr,
                               size_t initial_capacity) {
  c_orm_error_t rc;
  if (!arr)
    return C_ORM_ERROR_MEMORY;
  arr->length = 0;
  arr->capacity = initial_capacity;
  if (initial_capacity > 0) {
    rc = c_orm_system_calloc(initial_capacity, sizeof(struct Posts),
                             (void **)&arr->data);
    if (rc != C_ORM_OK) {
      return rc;
    }
  } else {
    arr->data = NULL;
  }
  return C_ORM_OK;
}

/**
 * @brief Free resources inside a Posts row.
 * @param item Item to free.
 * @return C_ORM_OK on success.
 */
c_orm_error_t Posts_free(struct Posts *item) {
  if (!item)
    return C_ORM_OK;
  if (item->title) {
    c_orm_system_free(item->title);
    item->title = NULL;
  }
  if (item->content) {
    c_orm_system_free(item->content);
    item->content = NULL;
  }
  if (item->views) {
    c_orm_system_free(item->views);
    item->views = NULL;
  }
  if (item->published_date) {
    c_orm_system_free(item->published_date);
    item->published_date = NULL;
  }
  return C_ORM_OK;
}

/**
 * @brief Free a Posts_Array and all its elements.
 * @param arr Array to free.
 * @return C_ORM_OK on success.
 */
c_orm_error_t Posts_Array_free(struct Posts_Array *arr) {
  size_t i;
  if (!arr)
    return C_ORM_OK;
  if (arr->data) {
    for (i = 0; i < arr->length; ++i) {
      Posts_free(&arr->data[i]);
    }
    c_orm_system_free(arr->data);
    arr->data = NULL;
  }
  arr->length = 0;
  arr->capacity = 0;
  return C_ORM_OK;
}

/**
 * @brief Deep copy a Posts row.
 * @param src Source item.
 * @param dest Destination item.
 * @return C_ORM_OK on success or C_ORM_ERROR_MEMORY.
 */
c_orm_error_t Posts_deepcopy(const struct Posts *src, struct Posts *dest) {
  c_orm_error_t rc;
  if (!src || !dest)
    return C_ORM_ERROR_MEMORY;
  memset(dest, 0, sizeof(*dest));
  dest->id = src->id;
  dest->user_id = src->user_id;
  if (src->title) {
    rc = c_orm_strdup(src->title, &dest->title);
    if (rc != C_ORM_OK) {
      Posts_free(dest);
      return rc;
    }
  } else {
    dest->title = NULL;
  }
  if (src->content) {
    rc = c_orm_strdup(src->content, &dest->content);
    if (rc != C_ORM_OK) {
      Posts_free(dest);
      return rc;
    }
  } else {
    dest->content = NULL;
  }
  if (src->views) {
    rc = c_orm_system_malloc(sizeof(int64_t), (void **)&dest->views);
    if (rc != C_ORM_OK) {
      Posts_free(dest);
      return rc;
    }
    *dest->views = *src->views;
  } else {
    dest->views = NULL;
  }
  if (src->published_date) {
    rc = c_orm_strdup(src->published_date, &dest->published_date);
    if (rc != C_ORM_OK) {
      Posts_free(dest);
      return rc;
    }
  } else {
    dest->published_date = NULL;
  }
  return C_ORM_OK;
}

/**
 * @brief Deep copy a Posts_Array.
 * @param src Source array.
 * @param dest Destination array.
 * @return C_ORM_OK on success or C_ORM_ERROR_MEMORY.
 */
c_orm_error_t Posts_Array_deepcopy(const struct Posts_Array *src,
                                   struct Posts_Array *dest) {
  size_t i;
  c_orm_error_t err;
  if (!src || !dest)
    return C_ORM_ERROR_MEMORY;
  err = Posts_Array_init(dest, src->capacity);
  if (err != C_ORM_OK)
    return err;
  for (i = 0; i < src->length; ++i) {
    err = Posts_deepcopy(&src->data[i], &dest->data[i]);
    if (err != C_ORM_OK) {
      dest->length = i;
      Posts_Array_free(dest);
      return err;
    }
  }
  dest->length = src->length;
  return C_ORM_OK;
}

/** @brief SQL SELECT all query for Posts. */
#define Posts_query_select_all "SELECT * FROM posts"

/** @brief SQL SELECT by primary key query for Posts. */
#define Posts_query_select_by_pk "SELECT * FROM posts WHERE id = ?"

/** @brief SQL DELETE by primary key query for Posts. */
#define Posts_query_delete_by_pk "DELETE FROM posts WHERE id = ?"

/** @brief SQL INSERT query for Posts. */
#define Posts_query_insert                                                     \
  "INSERT INTO posts (id, user_id, title, content, views, published_date) "    \
  "VALUES (?, ?, ?, ?, ?, ?)"

/** @brief SQL UPDATE query for Posts. */
#define Posts_query_update                                                     \
  "UPDATE posts SET id = ?, user_id = ?, title = ?, content = ?, views = ?, "  \
  "published_date = ? WHERE id = ?"

/** @brief Column names for Posts. */
const char *Posts_col_names[] = {"id",      "user_id", "title",
                                 "content", "views",   "published_date"};

/** @brief Column types for Posts. */
const c_orm_type_t Posts_col_types[] = {C_ORM_TYPE_INT32,  C_ORM_TYPE_INT32,
                                        C_ORM_TYPE_STRING, C_ORM_TYPE_STRING,
                                        C_ORM_TYPE_INT64,  C_ORM_TYPE_DATE};

/** @brief Column struct byte offsets for Posts. */
const size_t Posts_col_offsets[] = {
    offsetof(struct Posts, id),    offsetof(struct Posts, user_id),
    offsetof(struct Posts, title), offsetof(struct Posts, content),
    offsetof(struct Posts, views), offsetof(struct Posts, published_date)};

/** @brief Primary key flags for Posts columns. */
const bool Posts_col_is_pk[] = {true, false, false, false, false, false};

/** @brief Nullability flags for Posts columns. */
const bool Posts_col_is_nullable[] = {false, false, false, true, true, true};

/** @brief Foreign key target descriptors for Posts columns. */
const char *Posts_col_fk_targets[] = {NULL, NULL, NULL, NULL, NULL, NULL};

/** @brief Size in bytes of struct Posts. */
const size_t Posts_struct_size = sizeof(struct Posts);

/** @brief Column reflection metadata array for Posts. */
const c_orm_column_meta_t Posts_meta_columns[] = {
    {"id", C_ORM_TYPE_INT32, offsetof(struct Posts, id), true, false, NULL,
     false, false, 0, 0, 0, 0},
    {"user_id", C_ORM_TYPE_INT32, offsetof(struct Posts, user_id), false, false,
     NULL, false, false, 0, 0, 0, 0},
    {"title", C_ORM_TYPE_STRING, offsetof(struct Posts, title), false, false,
     NULL, false, false, 0, 0, 0, 0},
    {"content", C_ORM_TYPE_STRING, offsetof(struct Posts, content), false, true,
     NULL, false, false, 0, 0, 0, 0},
    {"views", C_ORM_TYPE_INT64, offsetof(struct Posts, views), false, true,
     NULL, false, false, 0, 0, 0, 0},
    {"published_date", C_ORM_TYPE_DATE, offsetof(struct Posts, published_date),
     false, true, NULL, false, false, 0, 0, 0, 0}};

/** @brief Table reflection metadata descriptor for Posts. */
const c_orm_table_meta_t Posts_meta = {"posts",
                                       Posts_meta_columns,
                                       6,
                                       sizeof(struct Posts),
                                       Posts_query_select_all,
                                       Posts_query_select_by_pk,
                                       Posts_query_insert,
                                       Posts_query_update,
                                       Posts_query_delete_by_pk,
                                       NULL,
                                       false,
                                       false,
                                       0,
                                       0,
                                       {0},
                                       NULL,
                                       0};

/**
 * @brief Initialize a Oauth2_tokens_Array.
 * @param arr Array to init.
 * @param initial_capacity Initial capacity.
 * @return C_ORM_OK on success or C_ORM_ERROR_MEMORY.
 */
c_orm_error_t Oauth2_tokens_Array_init(struct Oauth2_tokens_Array *arr,
                                       size_t initial_capacity) {
  c_orm_error_t rc;
  if (!arr)
    return C_ORM_ERROR_MEMORY;
  arr->length = 0;
  arr->capacity = initial_capacity;
  if (initial_capacity > 0) {
    rc = c_orm_system_calloc(initial_capacity, sizeof(struct Oauth2_tokens),
                             (void **)&arr->data);
    if (rc != C_ORM_OK) {
      return rc;
    }
  } else {
    arr->data = NULL;
  }
  return C_ORM_OK;
}

/**
 * @brief Free resources inside a Oauth2_tokens row.
 * @param item Item to free.
 * @return C_ORM_OK on success.
 */
c_orm_error_t Oauth2_tokens_free(struct Oauth2_tokens *item) {
  if (!item)
    return C_ORM_OK;
  if (item->access_token) {
    c_orm_system_free(item->access_token);
    item->access_token = NULL;
  }
  if (item->refresh_token) {
    c_orm_system_free(item->refresh_token);
    item->refresh_token = NULL;
  }
  if (item->token_type) {
    c_orm_system_free(item->token_type);
    item->token_type = NULL;
  }
  if (item->expires_in) {
    c_orm_system_free(item->expires_in);
    item->expires_in = NULL;
  }
  if (item->created_at) {
    c_orm_system_free(item->created_at);
    item->created_at = NULL;
  }
  return C_ORM_OK;
}

/**
 * @brief Free a Oauth2_tokens_Array and all its elements.
 * @param arr Array to free.
 * @return C_ORM_OK on success.
 */
c_orm_error_t Oauth2_tokens_Array_free(struct Oauth2_tokens_Array *arr) {
  size_t i;
  if (!arr)
    return C_ORM_OK;
  if (arr->data) {
    for (i = 0; i < arr->length; ++i) {
      Oauth2_tokens_free(&arr->data[i]);
    }
    c_orm_system_free(arr->data);
    arr->data = NULL;
  }
  arr->length = 0;
  arr->capacity = 0;
  return C_ORM_OK;
}

/**
 * @brief Deep copy a Oauth2_tokens row.
 * @param src Source item.
 * @param dest Destination item.
 * @return C_ORM_OK on success or C_ORM_ERROR_MEMORY.
 */
c_orm_error_t Oauth2_tokens_deepcopy(const struct Oauth2_tokens *src,
                                     struct Oauth2_tokens *dest) {
  c_orm_error_t rc;
  if (!src || !dest)
    return C_ORM_ERROR_MEMORY;
  memset(dest, 0, sizeof(*dest));
  if (src->access_token) {
    rc = c_orm_strdup(src->access_token, &dest->access_token);
    if (rc != C_ORM_OK) {
      Oauth2_tokens_free(dest);
      return rc;
    }
  } else {
    dest->access_token = NULL;
  }
  if (src->refresh_token) {
    rc = c_orm_strdup(src->refresh_token, &dest->refresh_token);
    if (rc != C_ORM_OK) {
      Oauth2_tokens_free(dest);
      return rc;
    }
  } else {
    dest->refresh_token = NULL;
  }
  if (src->token_type) {
    rc = c_orm_strdup(src->token_type, &dest->token_type);
    if (rc != C_ORM_OK) {
      Oauth2_tokens_free(dest);
      return rc;
    }
  } else {
    dest->token_type = NULL;
  }
  if (src->expires_in) {
    rc = c_orm_system_malloc(sizeof(int32_t), (void **)&dest->expires_in);
    if (rc != C_ORM_OK) {
      Oauth2_tokens_free(dest);
      return rc;
    }
    *dest->expires_in = *src->expires_in;
  } else {
    dest->expires_in = NULL;
  }
  if (src->created_at) {
    rc = c_orm_system_malloc(sizeof(int64_t), (void **)&dest->created_at);
    if (rc != C_ORM_OK) {
      Oauth2_tokens_free(dest);
      return rc;
    }
    *dest->created_at = *src->created_at;
  } else {
    dest->created_at = NULL;
  }
  return C_ORM_OK;
}

/**
 * @brief Deep copy a Oauth2_tokens_Array.
 * @param src Source array.
 * @param dest Destination array.
 * @return C_ORM_OK on success or C_ORM_ERROR_MEMORY.
 */
c_orm_error_t
Oauth2_tokens_Array_deepcopy(const struct Oauth2_tokens_Array *src,
                             struct Oauth2_tokens_Array *dest) {
  size_t i;
  c_orm_error_t err;
  if (!src || !dest)
    return C_ORM_ERROR_MEMORY;
  err = Oauth2_tokens_Array_init(dest, src->capacity);
  if (err != C_ORM_OK)
    return err;
  for (i = 0; i < src->length; ++i) {
    err = Oauth2_tokens_deepcopy(&src->data[i], &dest->data[i]);
    if (err != C_ORM_OK) {
      dest->length = i;
      Oauth2_tokens_Array_free(dest);
      return err;
    }
  }
  dest->length = src->length;
  return C_ORM_OK;
}

/** @brief SQL SELECT all query for Oauth2_tokens. */
#define Oauth2_tokens_query_select_all "SELECT * FROM oauth2_tokens"

/** @brief SQL SELECT by primary key query for Oauth2_tokens. */
#define Oauth2_tokens_query_select_by_pk                                       \
  "SELECT * FROM oauth2_tokens WHERE access_token = ?"

/** @brief SQL DELETE by primary key query for Oauth2_tokens. */
#define Oauth2_tokens_query_delete_by_pk                                       \
  "DELETE FROM oauth2_tokens WHERE access_token = ?"

/** @brief SQL INSERT query for Oauth2_tokens. */
#define Oauth2_tokens_query_insert                                             \
  "INSERT INTO oauth2_tokens (access_token, refresh_token, token_type, "       \
  "expires_in, created_at) VALUES (?, ?, ?, ?, ?)"

/** @brief SQL UPDATE query for Oauth2_tokens. */
#define Oauth2_tokens_query_update                                             \
  "UPDATE oauth2_tokens SET access_token = ?, refresh_token = ?, token_type "  \
  "= ?, expires_in = ?, created_at = ? WHERE access_token = ?"

/** @brief Column names for Oauth2_tokens. */
const char *Oauth2_tokens_col_names[] = {
    "access_token", "refresh_token", "token_type", "expires_in", "created_at"};

/** @brief Column types for Oauth2_tokens. */
const c_orm_type_t Oauth2_tokens_col_types[] = {
    C_ORM_TYPE_STRING, C_ORM_TYPE_STRING, C_ORM_TYPE_STRING, C_ORM_TYPE_INT32,
    C_ORM_TYPE_INT64};

/** @brief Column struct byte offsets for Oauth2_tokens. */
const size_t Oauth2_tokens_col_offsets[] = {
    offsetof(struct Oauth2_tokens, access_token),
    offsetof(struct Oauth2_tokens, refresh_token),
    offsetof(struct Oauth2_tokens, token_type),
    offsetof(struct Oauth2_tokens, expires_in),
    offsetof(struct Oauth2_tokens, created_at)};

/** @brief Primary key flags for Oauth2_tokens columns. */
const bool Oauth2_tokens_col_is_pk[] = {true, false, false, false, false};

/** @brief Nullability flags for Oauth2_tokens columns. */
const bool Oauth2_tokens_col_is_nullable[] = {false, true, true, true, true};

/** @brief Foreign key target descriptors for Oauth2_tokens columns. */
const char *Oauth2_tokens_col_fk_targets[] = {NULL, NULL, NULL, NULL, NULL};

/** @brief Size in bytes of struct Oauth2_tokens. */
const size_t Oauth2_tokens_struct_size = sizeof(struct Oauth2_tokens);

/** @brief Column reflection metadata array for Oauth2_tokens. */
const c_orm_column_meta_t Oauth2_tokens_meta_columns[] = {
    {"access_token", C_ORM_TYPE_STRING,
     offsetof(struct Oauth2_tokens, access_token), true, false, NULL, false,
     false, 0, 0, 0, 0},
    {"refresh_token", C_ORM_TYPE_STRING,
     offsetof(struct Oauth2_tokens, refresh_token), false, true, NULL, false,
     false, 0, 0, 0, 0},
    {"token_type", C_ORM_TYPE_STRING,
     offsetof(struct Oauth2_tokens, token_type), false, true, NULL, false,
     false, 0, 0, 0, 0},
    {"expires_in", C_ORM_TYPE_INT32, offsetof(struct Oauth2_tokens, expires_in),
     false, true, NULL, false, false, 0, 0, 0, 0},
    {"created_at", C_ORM_TYPE_INT64, offsetof(struct Oauth2_tokens, created_at),
     false, true, NULL, false, false, 0, 0, 0, 0}};

/** @brief Table reflection metadata descriptor for Oauth2_tokens. */
const c_orm_table_meta_t Oauth2_tokens_meta = {"oauth2_tokens",
                                               Oauth2_tokens_meta_columns,
                                               5,
                                               sizeof(struct Oauth2_tokens),
                                               Oauth2_tokens_query_select_all,
                                               Oauth2_tokens_query_select_by_pk,
                                               Oauth2_tokens_query_insert,
                                               Oauth2_tokens_query_update,
                                               Oauth2_tokens_query_delete_by_pk,
                                               NULL,
                                               false,
                                               false,
                                               0,
                                               0,
                                               {0},
                                               NULL,
                                               0};
