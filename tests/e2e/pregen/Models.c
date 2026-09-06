/* clang-format off */
#include "Models.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

c_orm_error_t Users_Array_init(struct Users_Array *arr,
                               size_t initial_capacity) {
  if (!arr)
    return C_ORM_ERROR_MEMORY;
  arr->length = 0;
  arr->capacity = initial_capacity;
  if (initial_capacity > 0) {
    if (c_orm_system_calloc(initial_capacity, sizeof(struct Users),
                            (void **)&arr->data) != C_ORM_OK) {
      return C_ORM_ERROR_MEMORY;
    }
    if (!arr->data)
      return C_ORM_ERROR_MEMORY;
  } else {
    arr->data = NULL;
  }
  return C_ORM_OK;
}

void Users_free(struct Users *item) {
  if (!item)
    return;
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
}

void Users_Array_free(struct Users_Array *arr) {
  size_t i;
  if (!arr)
    return;
  if (arr->data) {
    for (i = 0; i < arr->length; ++i) {
      Users_free(&arr->data[i]);
    }
    c_orm_system_free(arr->data);
    arr->data = NULL;
  }
  arr->length = 0;
  arr->capacity = 0;
}

c_orm_error_t Users_deepcopy(const struct Users *src, struct Users *dest) {
  if (!src || !dest)
    return C_ORM_ERROR_MEMORY;
  dest->id = src->id;
  if (src->username) {
    if (c_orm_system_malloc(strlen(src->username) + 1,
                            (void **)&dest->username) != C_ORM_OK) {
      Users_free(dest);
      return C_ORM_ERROR_MEMORY;
    }
#if defined(_MSC_VER)
    strcpy_s(dest->username, strlen(src->username) + 1, src->username);
#else
    strcpy(dest->username, src->username);
#endif
  } else {
    dest->username = NULL;
  }
  if (src->email) {
    if (c_orm_system_malloc(strlen(src->email) + 1, (void **)&dest->email) !=
        C_ORM_OK) {
      Users_free(dest);
      return C_ORM_ERROR_MEMORY;
    }
#if defined(_MSC_VER)
    strcpy_s(dest->email, strlen(src->email) + 1, src->email);
#else
    strcpy(dest->email, src->email);
#endif
  } else {
    dest->email = NULL;
  }
  if (src->age) {
    if (c_orm_system_malloc(sizeof(int32_t), (void **)&dest->age) != C_ORM_OK) {
      Users_free(dest);
      return C_ORM_ERROR_MEMORY;
    }
    *dest->age = *src->age;
  } else {
    dest->age = NULL;
  }
  if (src->score) {
    if (c_orm_system_malloc(sizeof(float), (void **)&dest->score) != C_ORM_OK) {
      Users_free(dest);
      return C_ORM_ERROR_MEMORY;
    }
    *dest->score = *src->score;
  } else {
    dest->score = NULL;
  }
  if (src->is_active) {
    if (c_orm_system_malloc(sizeof(bool), (void **)&dest->is_active) !=
        C_ORM_OK) {
      Users_free(dest);
      return C_ORM_ERROR_MEMORY;
    }
    *dest->is_active = *src->is_active;
  } else {
    dest->is_active = NULL;
  }
  if (src->created_at) {
    if (c_orm_system_malloc(strlen(src->created_at) + 1,
                            (void **)&dest->created_at) != C_ORM_OK) {
      Users_free(dest);
      return C_ORM_ERROR_MEMORY;
    }
#if defined(_MSC_VER)
    strcpy_s(dest->created_at, strlen(src->created_at) + 1, src->created_at);
#else
    strcpy(dest->created_at, src->created_at);
#endif
  } else {
    dest->created_at = NULL;
  }
  return C_ORM_OK;
}

c_orm_error_t Users_Array_deepcopy(const struct Users_Array *src,
                                   struct Users_Array *dest) {
  size_t i;
  c_orm_error_t err;
  if (!src || !dest)
    return C_ORM_ERROR_MEMORY;
  err = Users_Array_init(dest, src->capacity);
  if (err)
    return err;
  for (i = 0; i < src->length; ++i) {
    err = Users_deepcopy(&src->data[i], &dest->data[i]);
    if (err) {
      dest->length = i;
      Users_Array_free(dest);
      return err;
    }
  }
  dest->length = src->length;
  return C_ORM_OK;
}

#define Users_query_select_all "SELECT * FROM users"

#define Users_query_select_by_pk "SELECT * FROM users WHERE id = ?"

#define Users_query_delete_by_pk "DELETE FROM users WHERE id = ?"

#define Users_query_insert                                                     \
  "INSERT INTO users (id, username, email, age, score, is_active, "            \
  "created_at) VALUES (?, ?, ?, ?, ?, ?, ?)"

#define Users_query_update                                                     \
  "UPDATE users SET id = ?, username = ?, email = ?, age = ?, score = ?, "     \
  "is_active = ?, created_at = ? WHERE id = ?"

const char *Users_col_names[] = {"id",    "username",  "email",     "age",
                                 "score", "is_active", "created_at"};

const c_orm_type_t Users_col_types[] = {
    C_ORM_TYPE_INT32, C_ORM_TYPE_STRING, C_ORM_TYPE_STRING,   C_ORM_TYPE_INT32,
    C_ORM_TYPE_FLOAT, C_ORM_TYPE_BOOL,   C_ORM_TYPE_TIMESTAMP};

const size_t Users_col_offsets[] = {
    offsetof(struct Users, id),        offsetof(struct Users, username),
    offsetof(struct Users, email),     offsetof(struct Users, age),
    offsetof(struct Users, score),     offsetof(struct Users, is_active),
    offsetof(struct Users, created_at)};

const bool Users_col_is_pk[] = {true, false, false, false, false, false, false};

const bool Users_col_is_nullable[] = {false, false, false, true,
                                      true,  true,  true};

const char *Users_col_fk_targets[] = {NULL, NULL, NULL, NULL, NULL, NULL, NULL};

const size_t Users_struct_size = sizeof(struct Users);

const c_orm_column_meta_t Users_meta_columns[] = {
    {"id", C_ORM_TYPE_INT32, offsetof(struct Users, id), true, false, NULL,
     false, false},
    {"username", C_ORM_TYPE_STRING, offsetof(struct Users, username), false,
     false, NULL, false, false},
    {"email", C_ORM_TYPE_STRING, offsetof(struct Users, email), false, false,
     NULL, false, false},
    {"age", C_ORM_TYPE_INT32, offsetof(struct Users, age), false, true, NULL,
     false, false},
    {"score", C_ORM_TYPE_FLOAT, offsetof(struct Users, score), false, true,
     NULL, false, false},
    {"is_active", C_ORM_TYPE_BOOL, offsetof(struct Users, is_active), false,
     true, NULL, false, false},
    {"created_at", C_ORM_TYPE_TIMESTAMP, offsetof(struct Users, created_at),
     false, true, NULL, false, false}};

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

c_orm_error_t Posts_Array_init(struct Posts_Array *arr,
                               size_t initial_capacity) {
  if (!arr)
    return C_ORM_ERROR_MEMORY;
  arr->length = 0;
  arr->capacity = initial_capacity;
  if (initial_capacity > 0) {
    if (c_orm_system_calloc(initial_capacity, sizeof(struct Posts),
                            (void **)&arr->data) != C_ORM_OK) {
      return C_ORM_ERROR_MEMORY;
    }
    if (!arr->data)
      return C_ORM_ERROR_MEMORY;
  } else {
    arr->data = NULL;
  }
  return C_ORM_OK;
}

void Posts_free(struct Posts *item) {
  if (!item)
    return;
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
}

void Posts_Array_free(struct Posts_Array *arr) {
  size_t i;
  if (!arr)
    return;
  if (arr->data) {
    for (i = 0; i < arr->length; ++i) {
      Posts_free(&arr->data[i]);
    }
    c_orm_system_free(arr->data);
    arr->data = NULL;
  }
  arr->length = 0;
  arr->capacity = 0;
}

c_orm_error_t Posts_deepcopy(const struct Posts *src, struct Posts *dest) {
  if (!src || !dest)
    return C_ORM_ERROR_MEMORY;
  dest->id = src->id;
  dest->user_id = src->user_id;
  if (src->title) {
    if (c_orm_system_malloc(strlen(src->title) + 1, (void **)&dest->title) !=
        C_ORM_OK) {
      Posts_free(dest);
      return C_ORM_ERROR_MEMORY;
    }
#if defined(_MSC_VER)
    strcpy_s(dest->title, strlen(src->title) + 1, src->title);
#else
    strcpy(dest->title, src->title);
#endif
  } else {
    dest->title = NULL;
  }
  if (src->content) {
    if (c_orm_system_malloc(strlen(src->content) + 1,
                            (void **)&dest->content) != C_ORM_OK) {
      Posts_free(dest);
      return C_ORM_ERROR_MEMORY;
    }
#if defined(_MSC_VER)
    strcpy_s(dest->content, strlen(src->content) + 1, src->content);
#else
    strcpy(dest->content, src->content);
#endif
  } else {
    dest->content = NULL;
  }
  if (src->views) {
    if (c_orm_system_malloc(sizeof(int64_t), (void **)&dest->views) !=
        C_ORM_OK) {
      Posts_free(dest);
      return C_ORM_ERROR_MEMORY;
    }
    *dest->views = *src->views;
  } else {
    dest->views = NULL;
  }
  if (src->published_date) {
    if (c_orm_system_malloc(strlen(src->published_date) + 1,
                            (void **)&dest->published_date) != C_ORM_OK) {
      Posts_free(dest);
      return C_ORM_ERROR_MEMORY;
    }
#if defined(_MSC_VER)
    strcpy_s(dest->published_date, strlen(src->published_date) + 1,
             src->published_date);
#else
    strcpy(dest->published_date, src->published_date);
#endif
  } else {
    dest->published_date = NULL;
  }
  return C_ORM_OK;
}

c_orm_error_t Posts_Array_deepcopy(const struct Posts_Array *src,
                                   struct Posts_Array *dest) {
  size_t i;
  c_orm_error_t err;
  if (!src || !dest)
    return C_ORM_ERROR_MEMORY;
  err = Posts_Array_init(dest, src->capacity);
  if (err)
    return err;
  for (i = 0; i < src->length; ++i) {
    err = Posts_deepcopy(&src->data[i], &dest->data[i]);
    if (err) {
      dest->length = i;
      Posts_Array_free(dest);
      return err;
    }
  }
  dest->length = src->length;
  return C_ORM_OK;
}

#define Posts_query_select_all "SELECT * FROM posts"

#define Posts_query_select_by_pk "SELECT * FROM posts WHERE id = ?"

#define Posts_query_delete_by_pk "DELETE FROM posts WHERE id = ?"

#define Posts_query_insert                                                     \
  "INSERT INTO posts (id, user_id, title, content, views, published_date) "    \
  "VALUES (?, ?, ?, ?, ?, ?)"

#define Posts_query_update                                                     \
  "UPDATE posts SET id = ?, user_id = ?, title = ?, content = ?, views = ?, "  \
  "published_date = ? WHERE id = ?"

const char *Posts_col_names[] = {"id",      "user_id", "title",
                                 "content", "views",   "published_date"};

const c_orm_type_t Posts_col_types[] = {C_ORM_TYPE_INT32,  C_ORM_TYPE_INT32,
                                        C_ORM_TYPE_STRING, C_ORM_TYPE_STRING,
                                        C_ORM_TYPE_INT64,  C_ORM_TYPE_DATE};

const size_t Posts_col_offsets[] = {
    offsetof(struct Posts, id),    offsetof(struct Posts, user_id),
    offsetof(struct Posts, title), offsetof(struct Posts, content),
    offsetof(struct Posts, views), offsetof(struct Posts, published_date)};

const bool Posts_col_is_pk[] = {true, false, false, false, false, false};

const bool Posts_col_is_nullable[] = {false, false, false, true, true, true};

const char *Posts_col_fk_targets[] = {NULL, NULL, NULL, NULL, NULL, NULL};

const size_t Posts_struct_size = sizeof(struct Posts);

const c_orm_column_meta_t Posts_meta_columns[] = {
    {"id", C_ORM_TYPE_INT32, offsetof(struct Posts, id), true, false, NULL,
     false, false},
    {"user_id", C_ORM_TYPE_INT32, offsetof(struct Posts, user_id), false, false,
     NULL, false, false},
    {"title", C_ORM_TYPE_STRING, offsetof(struct Posts, title), false, false,
     NULL, false, false},
    {"content", C_ORM_TYPE_STRING, offsetof(struct Posts, content), false, true,
     NULL, false, false},
    {"views", C_ORM_TYPE_INT64, offsetof(struct Posts, views), false, true,
     NULL, false, false},
    {"published_date", C_ORM_TYPE_DATE, offsetof(struct Posts, published_date),
     false, true, NULL, false, false}};

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

c_orm_error_t Oauth2_tokens_Array_init(struct Oauth2_tokens_Array *arr,
                                       size_t initial_capacity) {
  if (!arr)
    return C_ORM_ERROR_MEMORY;
  arr->length = 0;
  arr->capacity = initial_capacity;
  if (initial_capacity > 0) {
    if (c_orm_system_calloc(initial_capacity, sizeof(struct Oauth2_tokens),
                            (void **)&arr->data) != C_ORM_OK) {
      return C_ORM_ERROR_MEMORY;
    }
    if (!arr->data)
      return C_ORM_ERROR_MEMORY;
  } else {
    arr->data = NULL;
  }
  return C_ORM_OK;
}

void Oauth2_tokens_free(struct Oauth2_tokens *item) {
  if (!item)
    return;
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
}

void Oauth2_tokens_Array_free(struct Oauth2_tokens_Array *arr) {
  size_t i;
  if (!arr)
    return;
  if (arr->data) {
    for (i = 0; i < arr->length; ++i) {
      Oauth2_tokens_free(&arr->data[i]);
    }
    c_orm_system_free(arr->data);
    arr->data = NULL;
  }
  arr->length = 0;
  arr->capacity = 0;
}

c_orm_error_t Oauth2_tokens_deepcopy(const struct Oauth2_tokens *src,
                                     struct Oauth2_tokens *dest) {
  if (!src || !dest)
    return C_ORM_ERROR_MEMORY;
  if (src->access_token) {
    if (c_orm_system_malloc(strlen(src->access_token) + 1,
                            (void **)&dest->access_token) != C_ORM_OK) {
      Oauth2_tokens_free(dest);
      return C_ORM_ERROR_MEMORY;
    }
#if defined(_MSC_VER)
    strcpy_s(dest->access_token, strlen(src->access_token) + 1,
             src->access_token);
#else
    strcpy(dest->access_token, src->access_token);
#endif
  } else {
    dest->access_token = NULL;
  }
  if (src->refresh_token) {
    if (c_orm_system_malloc(strlen(src->refresh_token) + 1,
                            (void **)&dest->refresh_token) != C_ORM_OK) {
      Oauth2_tokens_free(dest);
      return C_ORM_ERROR_MEMORY;
    }
#if defined(_MSC_VER)
    strcpy_s(dest->refresh_token, strlen(src->refresh_token) + 1,
             src->refresh_token);
#else
    strcpy(dest->refresh_token, src->refresh_token);
#endif
  } else {
    dest->refresh_token = NULL;
  }
  if (src->token_type) {
    if (c_orm_system_malloc(strlen(src->token_type) + 1,
                            (void **)&dest->token_type) != C_ORM_OK) {
      Oauth2_tokens_free(dest);
      return C_ORM_ERROR_MEMORY;
    }
#if defined(_MSC_VER)
    strcpy_s(dest->token_type, strlen(src->token_type) + 1, src->token_type);
#else
    strcpy(dest->token_type, src->token_type);
#endif
  } else {
    dest->token_type = NULL;
  }
  if (src->expires_in) {
    if (c_orm_system_malloc(sizeof(int32_t), (void **)&dest->expires_in) !=
        C_ORM_OK) {
      Oauth2_tokens_free(dest);
      return C_ORM_ERROR_MEMORY;
    }
    *dest->expires_in = *src->expires_in;
  } else {
    dest->expires_in = NULL;
  }
  if (src->created_at) {
    if (c_orm_system_malloc(sizeof(int64_t), (void **)&dest->created_at) !=
        C_ORM_OK) {
      Oauth2_tokens_free(dest);
      return C_ORM_ERROR_MEMORY;
    }
    *dest->created_at = *src->created_at;
  } else {
    dest->created_at = NULL;
  }
  return C_ORM_OK;
}

c_orm_error_t
Oauth2_tokens_Array_deepcopy(const struct Oauth2_tokens_Array *src,
                             struct Oauth2_tokens_Array *dest) {
  size_t i;
  c_orm_error_t err;
  if (!src || !dest)
    return C_ORM_ERROR_MEMORY;
  err = Oauth2_tokens_Array_init(dest, src->capacity);
  if (err)
    return err;
  for (i = 0; i < src->length; ++i) {
    err = Oauth2_tokens_deepcopy(&src->data[i], &dest->data[i]);
    if (err) {
      dest->length = i;
      Oauth2_tokens_Array_free(dest);
      return err;
    }
  }
  dest->length = src->length;
  return C_ORM_OK;
}

#define Oauth2_tokens_query_select_all "SELECT * FROM oauth2_tokens"

#define Oauth2_tokens_query_select_by_pk                                       \
  "SELECT * FROM oauth2_tokens WHERE access_token = ?"

#define Oauth2_tokens_query_delete_by_pk                                       \
  "DELETE FROM oauth2_tokens WHERE access_token = ?"

#define Oauth2_tokens_query_insert                                             \
  "INSERT INTO oauth2_tokens (access_token, refresh_token, token_type, "       \
  "expires_in, created_at) VALUES (?, ?, ?, ?, ?)"

#define Oauth2_tokens_query_update                                             \
  "UPDATE oauth2_tokens SET access_token = ?, refresh_token = ?, token_type "  \
  "= ?, expires_in = ?, created_at = ? WHERE access_token = ?"

const char *Oauth2_tokens_col_names[] = {
    "access_token", "refresh_token", "token_type", "expires_in", "created_at"};

const c_orm_type_t Oauth2_tokens_col_types[] = {
    C_ORM_TYPE_STRING, C_ORM_TYPE_STRING, C_ORM_TYPE_STRING, C_ORM_TYPE_INT32,
    C_ORM_TYPE_INT64};

const size_t Oauth2_tokens_col_offsets[] = {
    offsetof(struct Oauth2_tokens, access_token),
    offsetof(struct Oauth2_tokens, refresh_token),
    offsetof(struct Oauth2_tokens, token_type),
    offsetof(struct Oauth2_tokens, expires_in),
    offsetof(struct Oauth2_tokens, created_at)};

const bool Oauth2_tokens_col_is_pk[] = {true, false, false, false, false};

const bool Oauth2_tokens_col_is_nullable[] = {false, true, true, true, true};

const char *Oauth2_tokens_col_fk_targets[] = {NULL, NULL, NULL, NULL, NULL};

const size_t Oauth2_tokens_struct_size = sizeof(struct Oauth2_tokens);

const c_orm_column_meta_t Oauth2_tokens_meta_columns[] = {
    {"access_token", C_ORM_TYPE_STRING,
     offsetof(struct Oauth2_tokens, access_token), true, false, NULL, false,
     false},
    {"refresh_token", C_ORM_TYPE_STRING,
     offsetof(struct Oauth2_tokens, refresh_token), false, true, NULL, false,
     false},
    {"token_type", C_ORM_TYPE_STRING,
     offsetof(struct Oauth2_tokens, token_type), false, true, NULL, false,
     false},
    {"expires_in", C_ORM_TYPE_INT32, offsetof(struct Oauth2_tokens, expires_in),
     false, true, NULL, false, false},
    {"created_at", C_ORM_TYPE_INT64, offsetof(struct Oauth2_tokens, created_at),
     false, true, NULL, false, false}};

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
