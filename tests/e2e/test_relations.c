#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file test_relations.c
 * @brief Unit tests for relational mappings, cascade delete/update, lazy/eager
 * loading, and self-referencing relations.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_api.h"
#include "c_orm_struct.h"
#include "c_orm_sqlite.h"
#include "c_orm_query_builder.h"
#include "greatest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

/**
 * @brief Forward declaration for test_c_orm_cascade_delete_and_update.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_c_orm_cascade_delete_and_update(void);

/**
 * @brief Forward declaration for test_c_orm_lazy_load_relations.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_c_orm_lazy_load_relations(void);

/**
 * @brief Forward declaration for test_c_orm_eager_load_relations.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_c_orm_eager_load_relations(void);

/**
 * @brief Forward declaration for test_c_orm_nested_insert_relations.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_c_orm_nested_insert_relations(void);

/**
 * @brief Forward declaration for test_c_orm_one_to_many_lazy_load.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_c_orm_one_to_many_lazy_load(void);

/**
 * @brief Forward declaration for test_c_orm_lazy_load_paginated.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_c_orm_lazy_load_paginated(void);

/**
 * @brief Forward declaration for test_c_orm_many_to_many_cascade_delete.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_c_orm_many_to_many_cascade_delete(void);

/**
 * @brief Forward declaration for test_c_orm_deeply_nested_eager_loads.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_c_orm_deeply_nested_eager_loads(void);

/**
 * @brief Forward declaration for test_c_orm_query_builder_relation_filtering.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_c_orm_query_builder_relation_filtering(void);

/**
 * @brief Forward declaration for test_c_orm_self_referencing_tree.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_c_orm_self_referencing_tree(void);

/**
 * @brief Forward declaration for test_c_orm_relation_advanced_features.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_c_orm_relation_advanced_features(void);

#define TEAM_FIELDS(X, S)                                                      \
  X(S, C_ORM_TYPE_INT32, int32_t, id)                                          \
  X(S, C_ORM_TYPE_STRING, char *, name)                                        \
  X(S, C_ORM_TYPE_BOOL, bool, is_active)

C_ORM_STRUCT(Team, TEAM_FIELDS)

#define USER_FIELDS(X, S)                                                      \
  X(S, C_ORM_TYPE_INT32, int32_t, id)                                          \
  X(S, C_ORM_TYPE_INT32, int32_t, team_id)

#define USER_RELS(X, S) C_ORM_BELONGS_TO(X, S, Team, team, "id", "team_id")

C_ORM_STRUCT_WITH_RELATIONS(User, USER_FIELDS, USER_RELS)

#define USER_CASCADE_RELS(X, S)                                                \
  C_ORM_BELONGS_TO_CASCADE(X, S, Team, team, "id", "team_id",                  \
                           C_ORM_CASCADE_DELETE, C_ORM_CASCADE_UPDATE)

C_ORM_STRUCT_WITH_RELATIONS(UserCascade, USER_FIELDS, USER_CASCADE_RELS)

/**
 * @brief Tests cascade delete and update behavior across relational models.
 * @return GREATEST test result.
 */
TEST test_c_orm_cascade_delete_and_update(void) {
  c_orm_db_t *db;
  c_orm_error_t err;
  struct UserCascade user;
  struct Team new_team;
  int exists;

  c_orm_column_meta_t team_cols[3];
  c_orm_column_meta_t user_cols[3];
  c_orm_relation_meta_t user_rels[1];
  c_orm_table_meta_t team_m;
  c_orm_table_meta_t user_m;

  db = NULL;
  exists = 0;

  memcpy(team_cols, Team_columns, sizeof(Team_columns));
  memcpy(user_cols, UserCascade_columns, sizeof(UserCascade_columns));
  memcpy(user_rels, UserCascade_relations, sizeof(UserCascade_relations));
  team_cols[0].is_pk = true;
  user_cols[0].is_pk = true;
  user_cols[2].is_nullable = true;

  team_m = Team_meta;
  user_m = UserCascade_meta;
  team_m.columns = team_cols;
  user_m.columns = user_cols;

  user_rels[0].target_meta = &team_m;
  user_m.relations = user_rels;
  user_m.num_relations = 1;

  team_m.query_insert =
      "INSERT INTO Team (id, name, is_active) VALUES (NULLIF(?, 0), ?, ?)";
  team_m.query_update =
      "UPDATE Team SET id = ?, name = ?, is_active = ? WHERE id = ?";
  team_m.query_select_by_pk = "SELECT * FROM Team WHERE id = ?";
  team_m.query_delete_by_pk = "DELETE FROM Team WHERE id = ?";

  user_m.query_insert =
      "INSERT INTO User (id, team_id) VALUES (NULLIF(?, 0), ?)";
  user_m.query_update = "UPDATE User SET id = ?, team_id = ? WHERE id = ?";
  user_m.query_delete_by_pk = "DELETE FROM User WHERE id = ?";

  user_m.name = "User";

  err = c_orm_sqlite_connect(":memory:", &db);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  err = c_orm_execute_raw(db, "CREATE TABLE Team (id INTEGER PRIMARY KEY "
                              "AUTOINCREMENT, name TEXT, is_active INTEGER)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err = c_orm_execute_raw(db, "CREATE TABLE User (id INTEGER PRIMARY KEY "
                              "AUTOINCREMENT, team_id INTEGER)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  /* Setup nested struct */
  memset(&new_team, 0, sizeof(new_team));
  new_team.id = 0;
  new_team.name = "Support";

  memset(&user, 0, sizeof(user));
  user.id = 0;
  user.team_id = 0;
  user.team.data = &new_team;
  user.team.lazy_ctx.is_loaded = 1;

  err = c_orm_insert(db, &user_m, &user);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  /* Update the child */
  new_team.name = "Customer Success";
  err = c_orm_update(db, &user_m, &user);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  /* Delete parent */
  err = c_orm_delete(db, &user_m, &user);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  /* Assert Child was cascade deleted */
  err = c_orm_exists_int32(db, &team_m, new_team.id, &exists);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  ASSERT_EQ_FMT(0, exists, "%d");

  if (db)
    db->vtable->disconnect(db);
  PASS();
}

/**
 * @brief Tests lazy-loading related entity models on demand.
 * @return GREATEST test result.
 */
TEST test_c_orm_lazy_load_relations(void) {
  /* Using sqlite in-memory for testing relations via query building/routing
   * conceptually */
  c_orm_db_t *db = NULL;
  c_orm_error_t err;
  struct User user;

  c_orm_column_meta_t team_cols[3];
  c_orm_column_meta_t user_cols[3];
  c_orm_table_meta_t team_m;
  c_orm_table_meta_t user_m;

  memcpy(team_cols, Team_columns, sizeof(Team_columns));
  memcpy(user_cols, User_columns, sizeof(User_columns));
  team_cols[0].is_pk = true;
  user_cols[0].is_pk = true;
  user_cols[2].is_nullable = true;

  team_m = Team_meta;
  user_m = User_meta;
  team_m.columns = team_cols;
  (void)team_m;
  user_m.columns = user_cols;

  err = c_orm_sqlite_connect(":memory:", &db);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  err = c_orm_execute_raw(db, "CREATE TABLE Team (id INTEGER PRIMARY KEY, name "
                              "TEXT, is_active INTEGER)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err = c_orm_execute_raw(
      db,
      "CREATE TABLE User (id INTEGER PRIMARY KEY, team_id INTEGER, data BLOB)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  /* Setup mock data */
  err = c_orm_execute_raw(
      db,
      "INSERT INTO Team (id, name, is_active) VALUES (10, 'Engineering', 1)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err = c_orm_execute_raw(db, "INSERT INTO User (id, team_id) VALUES (1, 10)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  user.id = 1;
  user.team_id = 10;
  user.team.data = NULL;
  user.team.lazy_ctx.is_loaded = 0;

  /* Let's try lazy loading Team from User */
  err = c_orm_lazy_load(db, &user_m, &user, "team");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  ASSERT_EQ_FMT(1, user.team.lazy_ctx.is_loaded, "%d");
  ASSERT(user.team.data != NULL);
  ASSERT_EQ_FMT(10, user.team.data->id, "%d");
  ASSERT_STR_EQ("Engineering", user.team.data->name);

  if (user.team.data->name)
    C_ORM_FREE(user.team.data->name);
  C_ORM_FREE(user.team.data);

  if (db)
    db->vtable->disconnect(db);
  PASS();
}

/**
 * @brief Tests eager-loading related entity models during query execution.
 * @return GREATEST test result.
 */
TEST test_c_orm_eager_load_relations(void) {
  c_orm_db_t *db = NULL;
  c_orm_error_t err;
  struct User user;

  c_orm_column_meta_t team_cols[3];
  c_orm_column_meta_t user_cols[3];
  c_orm_table_meta_t team_m;
  c_orm_table_meta_t user_m;

  memcpy(team_cols, Team_columns, sizeof(Team_columns));
  memcpy(user_cols, User_columns, sizeof(User_columns));
  team_cols[0].is_pk = true;
  user_cols[0].is_pk = true;
  user_cols[2].is_nullable = true;

  team_m = Team_meta;
  user_m = User_meta;
  team_m.columns = team_cols;
  team_m.query_select_all = "SELECT id, name, is_active FROM Team";
  team_m.query_select_by_pk =
      "SELECT id, name, is_active FROM Team WHERE id = ?";
  (void)team_m;
  user_m.columns = user_cols;
  user_m.query_select_all = "SELECT id, team_id FROM User";
  user_m.query_select_by_pk = "SELECT id, team_id FROM User WHERE id = ?";

  err = c_orm_sqlite_connect(":memory:", &db);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  err = c_orm_execute_raw(db, "CREATE TABLE Team (id INTEGER PRIMARY KEY, name "
                              "TEXT, is_active INTEGER)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err = c_orm_execute_raw(
      db,
      "CREATE TABLE User (id INTEGER PRIMARY KEY, team_id INTEGER, data BLOB)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  /* Setup mock data */
  err = c_orm_execute_raw(
      db, "INSERT INTO Team (id, name, is_active) VALUES (20, 'Sales', 1)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err = c_orm_execute_raw(
      db, "INSERT INTO Team (id, name, is_active) VALUES (2, 'Sales2', 1)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err = c_orm_execute_raw(db, "INSERT INTO User (id, team_id) VALUES (2, 20)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  printf("EAGER LOAD REL OFFSETS: struct=%lu data=%lu lazy=%lu\n",
         (unsigned long)user_m.relations[0].struct_offset,
         (unsigned long)user_m.relations[0].data_offset,
         (unsigned long)user_m.relations[0].lazy_ctx_offset);
  fflush(stdout);
  /* EAGER load Team from User via JOIN */
  err = c_orm_find_with_relation_int32(db, &user_m, 2, "team", &user);
  printf("err = %d\n", err);
  fflush(stdout);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  printf("ASSERT 1\n");
  fflush(stdout);
  ASSERT_EQ_FMT(1, user.team.lazy_ctx.is_loaded, "%d");
  printf("ASSERT 2\n");
  fflush(stdout);
  ASSERT(user.team.data != NULL);
  printf("ASSERT 3\n");
  fflush(stdout);
  ASSERT_EQ_FMT(20, user.team.data->id, "%d");
  printf("ASSERT 4\n");
  fflush(stdout);
  ASSERT_STR_EQ("Sales", user.team.data->name);
  printf("ASSERT 5\n");
  fflush(stdout);

  if (user.team.data->name)
    C_ORM_FREE(user.team.data->name);
  C_ORM_FREE(user.team.data);
  printf("FREED\n");
  fflush(stdout);

  {
    struct {
      void *data;
      size_t length;
      size_t capacity;
    } user_arr;
    memset(&user_arr, 0, sizeof(user_arr));
    err = c_orm_find_all_with_relation(db, &user_m, "team", &user_arr);
    ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
    if (user_arr.length > 0) {
      struct User *u0 = (struct User *)user_arr.data;
      if (u0->team.data) {
        if (u0->team.data->name)
          C_ORM_FREE(u0->team.data->name);
        C_ORM_FREE(u0->team.data);
      }
    }
    if (user_arr.data)
      C_ORM_FREE(user_arr.data);
  }

  {
    const char *paths[1];
    struct {
      void *data;
      size_t length;
      size_t capacity;
    } user_arr;
    paths[0] = "team";
    memset(&user_arr, 0, sizeof(user_arr));
    err = c_orm_find_all_with_relations(db, &user_m, paths, 1, &user_arr);
    ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
    if (user_arr.length > 0) {
      struct User *u0 = (struct User *)user_arr.data;
      if (u0->team.data) {
        if (u0->team.data->name)
          C_ORM_FREE(u0->team.data->name);
        C_ORM_FREE(u0->team.data);
      }
    }
    if (user_arr.data)
      C_ORM_FREE(user_arr.data);
  }

  if (db)
    db->vtable->disconnect(db);
  PASS();
}

/**
 * @brief Tests inserting nested relational entities and auto-assigning foreign
 * keys.
 * @return GREATEST test result.
 */
TEST test_c_orm_nested_insert_relations(void) {
  c_orm_db_t *db = NULL;
  c_orm_error_t err;
  struct User user;
  struct Team new_team;

  c_orm_column_meta_t team_cols[3];
  c_orm_column_meta_t user_cols[3];
  c_orm_relation_meta_t user_rels[1];
  c_orm_table_meta_t team_m;
  c_orm_table_meta_t user_m;

  memcpy(team_cols, Team_columns, sizeof(Team_columns));
  memcpy(user_cols, User_columns, sizeof(User_columns));
  memcpy(user_rels, User_relations, sizeof(User_relations));
  team_cols[0].is_pk = true;
  user_cols[0].is_pk = true;
  user_cols[2].is_nullable = true;

  team_m = Team_meta;
  user_m = User_meta;
  team_m.columns = team_cols;
  (void)team_m;
  user_m.columns = user_cols;

  user_rels[0].target_meta = &team_m;
  user_m.relations = user_rels;
  user_m.num_relations = 1;

  team_m.query_insert =
      "INSERT INTO Team (id, name, is_active) VALUES (NULLIF(?, 0), ?, ?)";
  user_m.query_insert =
      "INSERT INTO User (id, team_id) VALUES (NULLIF(?, 0), ?)";

  err = c_orm_sqlite_connect(":memory:", &db);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  err = c_orm_execute_raw(db, "CREATE TABLE Team (id INTEGER PRIMARY KEY "
                              "AUTOINCREMENT, name TEXT, is_active INTEGER)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err = c_orm_execute_raw(db, "CREATE TABLE User (id INTEGER PRIMARY KEY "
                              "AUTOINCREMENT, team_id INTEGER)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  /* Test if last_insert_rowid works */
  err = c_orm_execute_raw(db, "INSERT INTO Team (name) VALUES ('TestTeam')");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  {
    int64_t lid = 0;
    db->vtable->get_last_insert_rowid(db, &lid);
  }

  /* Setup nested struct */
  memset(&new_team, 0, sizeof(new_team));
  new_team.id = 0; /* will be auto-assigned */
  new_team.name = "Marketing";

  memset(&user, 0, sizeof(user));
  user.id = 0;
  user.team_id = 0; /* will be auto-assigned from new_team.id */
  user.team.data = &new_team;
  user.team.lazy_ctx.is_loaded = 1;

  err = c_orm_insert(db, &user_m, &user);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  /* Assert FK was assigned */
  ASSERT(user.team_id > 0);

  /* Test ONE_TO_ONE relation insertion */
  user_rels[0].type = C_ORM_RELATION_ONE_TO_ONE;
  user_rels[0].foreign_key = "id";
  user_rels[0].local_key = "id";
  new_team.id = 0;
  new_team.name = "Engineering";
  user.id = 99;
  user.team_id = 99;
  err = c_orm_insert(db, &user_m, &user);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  if (db)
    db->vtable->disconnect(db);
  PASS();
}

#define POST_FIELDS(X, S)                                                      \
  X(S, C_ORM_TYPE_INT32, int32_t, id)                                          \
  X(S, C_ORM_TYPE_STRING, char *, title)                                       \
  X(S, C_ORM_TYPE_INT32, int32_t, author_id)

C_ORM_STRUCT(Post, POST_FIELDS)

#define USER_WITH_POSTS_RELS(X, S)                                             \
  C_ORM_HAS_MANY(X, S, Post, posts, "author_id", "id")

C_ORM_STRUCT_WITH_RELATIONS(UserWithPosts, USER_FIELDS, USER_WITH_POSTS_RELS)

/**
 * @brief Tests one-to-many lazy loading of child collections.
 * @return GREATEST test result.
 */
TEST test_c_orm_one_to_many_lazy_load(void) {
  c_orm_db_t *db = NULL;
  c_orm_error_t err;
  struct UserWithPosts user;

  c_orm_column_meta_t post_cols[3];
  c_orm_column_meta_t user_cols[3];
  c_orm_relation_meta_t user_rels[1];
  c_orm_table_meta_t post_m;
  c_orm_table_meta_t user_m;

  memcpy(post_cols, Post_columns, sizeof(Post_columns));
  memcpy(user_cols, UserWithPosts_columns, sizeof(UserWithPosts_columns));
  memcpy(user_rels, UserWithPosts_relations, sizeof(UserWithPosts_relations));
  post_cols[0].is_pk = true;
  user_cols[0].is_pk = true;

  post_m = Post_meta;
  user_m = UserWithPosts_meta;
  post_m.columns = post_cols;
  user_m.columns = user_cols;

  user_rels[0].target_meta = &post_m;
  user_m.relations = user_rels;
  user_m.num_relations = 1;

  post_m.query_select_all = "SELECT * FROM Post";
  user_m.name = "User";

  err = c_orm_sqlite_connect(":memory:", &db);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  err = c_orm_execute_raw(
      db,
      "CREATE TABLE User (id INTEGER PRIMARY KEY, team_id INTEGER, data BLOB)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err = c_orm_execute_raw(db, "CREATE TABLE Post (id INTEGER PRIMARY KEY, "
                              "title TEXT, author_id INTEGER)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  /* Setup mock data */
  err = c_orm_execute_raw(db, "INSERT INTO User (id, team_id) VALUES (5, 10)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err = c_orm_execute_raw(
      db,
      "INSERT INTO Post (id, title, author_id) VALUES (1, 'First Post', 5)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err = c_orm_execute_raw(
      db,
      "INSERT INTO Post (id, title, author_id) VALUES (2, 'Second Post', 5)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  user.id = 5;
  user.team_id = 10;
  user.posts.data.data = NULL;
  user.posts.data.length = 0;
  user.posts.data.capacity = 0;
  user.posts.lazy_ctx.is_loaded = 0;

  /* Let's try lazy loading Posts from User */
  err = c_orm_lazy_load(db, &user_m, &user, "posts");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  ASSERT_EQ_FMT(1, user.posts.lazy_ctx.is_loaded, "%d");
  ASSERT_EQ_FMT(2, (int)user.posts.data.length, "%d");
  ASSERT(user.posts.data.data != NULL);

  ASSERT_EQ_FMT(1, user.posts.data.data[0].id, "%d");
  ASSERT_STR_EQ("First Post", user.posts.data.data[0].title);
  ASSERT_EQ_FMT(2, user.posts.data.data[1].id, "%d");
  ASSERT_STR_EQ("Second Post", user.posts.data.data[1].title);

  if (user.posts.data.data[0].title)
    C_ORM_FREE(user.posts.data.data[0].title);
  if (user.posts.data.data[1].title)
    C_ORM_FREE(user.posts.data.data[1].title);
  if (user.posts.data.data)
    C_ORM_FREE(user.posts.data.data);

  {
    struct UserWithPosts eager_user;
    memset(&eager_user, 0, sizeof(eager_user));
    err = c_orm_find_with_relation_int32(db, &user_m, 5, "posts", &eager_user);
    ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
    if (eager_user.posts.data.data) {
      size_t pi;
      for (pi = 0; pi < eager_user.posts.data.length; pi++) {
        if (eager_user.posts.data.data[pi].title)
          C_ORM_FREE(eager_user.posts.data.data[pi].title);
      }
      C_ORM_FREE(eager_user.posts.data.data);
    }
  }

  if (db)
    db->vtable->disconnect(db);
  PASS();
}

/**
 * @brief Tests paginated lazy loading of child collections using LIMIT and
 * OFFSET.
 * @return GREATEST test result.
 */
TEST test_c_orm_lazy_load_paginated(void) {
  c_orm_db_t *db = NULL;
  c_orm_error_t err;
  struct UserWithPosts user;

  c_orm_column_meta_t post_cols[3];
  c_orm_column_meta_t user_cols[3];
  c_orm_relation_meta_t user_rels[1];
  c_orm_table_meta_t post_m;
  c_orm_table_meta_t user_m;

  memcpy(post_cols, Post_columns, sizeof(Post_columns));
  memcpy(user_cols, UserWithPosts_columns, sizeof(UserWithPosts_columns));
  memcpy(user_rels, UserWithPosts_relations, sizeof(UserWithPosts_relations));
  post_cols[0].is_pk = true;
  user_cols[0].is_pk = true;

  post_m = Post_meta;
  user_m = UserWithPosts_meta;
  post_m.columns = post_cols;
  user_m.columns = user_cols;
  post_m.name = "Post";
  user_m.name = "User";

  user_rels[0].target_meta = &post_m;
  user_m.relations = user_rels;
  user_m.num_relations = 1;

  err = c_orm_sqlite_connect(":memory:", &db);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  err = c_orm_execute_raw(
      db,
      "CREATE TABLE User (id INTEGER PRIMARY KEY, team_id INTEGER, data BLOB)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err = c_orm_execute_raw(db, "CREATE TABLE Post (id INTEGER PRIMARY KEY, "
                              "title TEXT, author_id INTEGER)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  err = c_orm_execute_raw(db, "INSERT INTO User (id, team_id) VALUES (5, 10)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err = c_orm_execute_raw(
      db,
      "INSERT INTO Post (id, title, author_id) VALUES (1, 'First Post', 5)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err = c_orm_execute_raw(
      db,
      "INSERT INTO Post (id, title, author_id) VALUES (2, 'Second Post', 5)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err = c_orm_execute_raw(
      db,
      "INSERT INTO Post (id, title, author_id) VALUES (3, 'Third Post', 5)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  user.id = 5;
  user.team_id = 10;
  user.posts.lazy_ctx.is_loaded = 0;
  user.posts.data.data = NULL;
  user.posts.data.length = 0;
  user.posts.data.capacity = 0;

  /* Load paginated: LIMIT 1 OFFSET 1 -> Should fetch the second post */
  err = c_orm_lazy_load_paginated(db, &user_m, &user, "posts", 1, 1);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  ASSERT_EQ_FMT(1, user.posts.lazy_ctx.is_loaded, "%d");
  ASSERT_EQ_FMT(1, (int)user.posts.data.length, "%d");

  ASSERT_EQ_FMT(2, user.posts.data.data[0].id, "%d");
  ASSERT_STR_EQ("Second Post", user.posts.data.data[0].title);

  if (user.posts.data.data[0].title)
    C_ORM_FREE(user.posts.data.data[0].title);
  if (user.posts.data.data)
    C_ORM_FREE(user.posts.data.data);

  /* Test with order_by DESC, soft_delete_aware, and custom_filter */
  user_rels[0].order_by = "id DESC";
  user_rels[0].custom_filter = "id > 0";
  user_rels[0].soft_delete_aware = 0;
  user.posts.lazy_ctx.is_loaded = 0;
  user.posts.data.data = NULL;
  user.posts.data.length = 0;
  user.posts.data.capacity = 0;

  err = c_orm_lazy_load_paginated(db, &user_m, &user, "posts", 1, 0);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  if (user.posts.data.length > 0) {
    ASSERT_EQ_FMT(3, user.posts.data.data[0].id, "%d");
    if (user.posts.data.data[0].title)
      C_ORM_FREE(user.posts.data.data[0].title);
    C_ORM_FREE(user.posts.data.data);
  }

  /* Test with order_by ASC */
  user_rels[0].order_by = "id ASC";
  user_rels[0].custom_filter = NULL;
  user.posts.lazy_ctx.is_loaded = 0;
  user.posts.data.data = NULL;
  user.posts.data.length = 0;
  user.posts.data.capacity = 0;

  err = c_orm_lazy_load_paginated(db, &user_m, &user, "posts", 1, 0);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  if (user.posts.data.length > 0) {
    ASSERT_EQ_FMT(1, user.posts.data.data[0].id, "%d");
    if (user.posts.data.data[0].title)
      C_ORM_FREE(user.posts.data.data[0].title);
    C_ORM_FREE(user.posts.data.data);
  }

  /* Test with plain order_by */
  user_rels[0].order_by = "id";
  user.posts.lazy_ctx.is_loaded = 0;
  user.posts.data.data = NULL;
  user.posts.data.length = 0;
  user.posts.data.capacity = 0;

  err = c_orm_lazy_load_paginated(db, &user_m, &user, "posts", 1, 0);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  if (user.posts.data.length > 0) {
    if (user.posts.data.data[0].title)
      C_ORM_FREE(user.posts.data.data[0].title);
    C_ORM_FREE(user.posts.data.data);
  }

  /* Test c_orm_delete with C_ORM_CASCADE_SET_NULL */
  user_rels[0].on_delete = C_ORM_CASCADE_SET_NULL;
  user_m.query_delete_by_pk = "DELETE FROM User WHERE id = ?";
  err = c_orm_delete(db, &user_m, &user);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  if (db)
    db->vtable->disconnect(db);
  PASS();
}

#define USER_WITH_ROLES_RELS(X, S)                                             \
  C_ORM_MANY_TO_MANY_CASCADE(X, S, Role, roles, "id", "id", "user_roles",      \
                             "user_id", "role_id", C_ORM_CASCADE_DELETE,       \
                             C_ORM_CASCADE_UPDATE)

C_ORM_STRUCT_WITH_RELATIONS(UserWithRoles, USER_FIELDS, USER_WITH_ROLES_RELS)

#define ROLE_FIELDS(X, S)                                                      \
  X(S, C_ORM_TYPE_INT32, int32_t, id)                                          \
  X(S, C_ORM_TYPE_STRING, char *, name)

C_ORM_STRUCT(Role, ROLE_FIELDS)

/**
 * @brief Tests many-to-many relationship cascade deletion through join table.
 * @return GREATEST test result.
 */
TEST test_c_orm_many_to_many_cascade_delete(void) {
  c_orm_db_t *db = NULL;
  c_orm_error_t err;
  struct UserWithRoles user;

  c_orm_column_meta_t role_cols[2];
  c_orm_column_meta_t user_cols[3];
  c_orm_relation_meta_t user_rels[1];
  c_orm_table_meta_t role_m;
  c_orm_table_meta_t user_m;
  int exists = 0;

  memcpy(role_cols, Role_columns, sizeof(Role_columns));
  memcpy(user_cols, UserWithRoles_columns, sizeof(UserWithRoles_columns));
  memcpy(user_rels, UserWithRoles_relations, sizeof(UserWithRoles_relations));
  role_cols[0].is_pk = true;
  user_cols[0].is_pk = true;

  role_m = Role_meta;
  user_m = UserWithRoles_meta;
  role_m.columns = role_cols;
  user_m.columns = user_cols;
  role_m.name = "Role";
  user_m.name = "User";

  user_rels[0].target_meta = &role_m;
  user_m.relations = user_rels;
  user_m.num_relations = 1;

  role_m.query_select_by_pk = "SELECT * FROM Role WHERE id = ?";
  user_m.query_delete_by_pk = "DELETE FROM User WHERE id = ?";

  err = c_orm_sqlite_connect(":memory:", &db);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  err = c_orm_execute_raw(
      db,
      "CREATE TABLE User (id INTEGER PRIMARY KEY, team_id INTEGER, data BLOB)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err = c_orm_execute_raw(
      db, "CREATE TABLE Role (id INTEGER PRIMARY KEY, name TEXT)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err = c_orm_execute_raw(
      db, "CREATE TABLE user_roles (user_id INTEGER, role_id INTEGER)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  /* Setup mock data */
  err = c_orm_execute_raw(db, "INSERT INTO User (id, team_id) VALUES (5, 10)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err =
      c_orm_execute_raw(db, "INSERT INTO Role (id, name) VALUES (1, 'Admin')");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err =
      c_orm_execute_raw(db, "INSERT INTO Role (id, name) VALUES (2, 'Editor')");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err = c_orm_execute_raw(
      db, "INSERT INTO user_roles (user_id, role_id) VALUES (5, 1)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err = c_orm_execute_raw(
      db, "INSERT INTO user_roles (user_id, role_id) VALUES (5, 2)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  user.id = 5;
  user.team_id = 10;

  /* Assert rows exist */
  err = c_orm_exists_int32(db, &role_m, 1, &exists);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  ASSERT_EQ_FMT(1, exists, "%d");
  err = c_orm_exists_int32(db, &role_m, 2, &exists);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  ASSERT_EQ_FMT(1, exists, "%d");

  {
    struct UserWithRoles eager_user;
    memset(&eager_user, 0, sizeof(eager_user));
    eager_user.id = 5;
    err = c_orm_find_with_relation_int32(db, &user_m, 5, "roles", &eager_user);
    ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
    err = c_orm_lazy_load(db, &user_m, &eager_user, "roles");
    ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
    if (eager_user.roles.data.data) {
      size_t ri;
      for (ri = 0; ri < eager_user.roles.data.length; ri++) {
        if (eager_user.roles.data.data[ri].name)
          C_ORM_FREE(eager_user.roles.data.data[ri].name);
      }
      C_ORM_FREE(eager_user.roles.data.data);
    }
  }

  {
    struct {
      void *data;
      size_t length;
      size_t capacity;
    } user_arr;
    user_m.query_select_all = "SELECT id, team_id FROM User";
    memset(&user_arr, 0, sizeof(user_arr));
    err = c_orm_find_all_with_relation(db, &user_m, "roles", &user_arr);
    ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
    if (user_arr.length > 0) {
      struct UserWithRoles *u_roles = (struct UserWithRoles *)user_arr.data;
      size_t ui;
      for (ui = 0; ui < user_arr.length; ui++) {
        if (u_roles[ui].roles.data.data) {
          size_t ri;
          for (ri = 0; ri < u_roles[ui].roles.data.length; ri++) {
            if (u_roles[ui].roles.data.data[ri].name)
              C_ORM_FREE(u_roles[ui].roles.data.data[ri].name);
          }
          C_ORM_FREE(u_roles[ui].roles.data.data);
        }
      }
    }
    if (user_arr.data)
      C_ORM_FREE(user_arr.data);
  }

  /* Delete parent */
  err = c_orm_delete(db, &user_m, &user);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  /* Assert targets were cascade deleted */
  err = c_orm_exists_int32(db, &role_m, 1, &exists);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  ASSERT_EQ_FMT(0, exists, "%d");
  err = c_orm_exists_int32(db, &role_m, 2, &exists);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  ASSERT_EQ_FMT(0, exists, "%d");

  /* Assert join table is empty */
  {
    c_orm_query_t *query;
    int count;
    count = 0;
    err = c_orm_prepare_cached(db, "SELECT COUNT(*) FROM user_roles", &query);
    ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
    db->vtable->step(query, &exists);
    if (exists) {
      db->vtable->get_int32(query, 0, &count);
    }
    err = c_orm_finalize_cached(db, query);
    ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
    ASSERT_EQ_FMT(0, count, "%d");
  }

  if (db)
    db->vtable->disconnect(db);
  PASS();
}

#define COMMENT_FIELDS(X, S)                                                   \
  X(S, C_ORM_TYPE_INT32, int32_t, id)                                          \
  X(S, C_ORM_TYPE_STRING, char *, text)                                        \
  X(S, C_ORM_TYPE_INT32, int32_t, post_id)

C_ORM_STRUCT(Comment, COMMENT_FIELDS)

#define POST_WITH_COMMENTS_RELS(X, S)                                          \
  C_ORM_HAS_MANY(X, S, Comment, comments, "post_id", "id")

C_ORM_STRUCT_WITH_RELATIONS(PostWithComments, POST_FIELDS,
                            POST_WITH_COMMENTS_RELS)

#define USER_WITH_DEEP_POSTS_RELS(X, S)                                        \
  C_ORM_HAS_MANY(X, S, PostWithComments, posts, "author_id", "id")

C_ORM_STRUCT_WITH_RELATIONS(UserWithDeepPosts, USER_FIELDS,
                            USER_WITH_DEEP_POSTS_RELS)

/**
 * @brief Tests deeply nested eager-loading across multiple relation levels.
 * @return GREATEST test result.
 */
TEST test_c_orm_deeply_nested_eager_loads(void) {
  c_orm_db_t *db = NULL;
  c_orm_error_t err;
  struct UserWithDeepPosts user;
  const char *paths[] = {"posts.comments"};

  c_orm_column_meta_t comment_cols[3];
  c_orm_column_meta_t post_cols[3];
  c_orm_column_meta_t user_cols[3];
  c_orm_relation_meta_t post_rels[1];
  c_orm_relation_meta_t user_rels[1];
  c_orm_table_meta_t comment_m;
  c_orm_table_meta_t post_m;
  c_orm_table_meta_t user_m;

  memcpy(comment_cols, Comment_columns, sizeof(Comment_columns));
  memcpy(post_cols, PostWithComments_columns, sizeof(PostWithComments_columns));
  memcpy(user_cols, UserWithDeepPosts_columns,
         sizeof(UserWithDeepPosts_columns));
  memcpy(post_rels, PostWithComments_relations,
         sizeof(PostWithComments_relations));
  memcpy(user_rels, UserWithDeepPosts_relations,
         sizeof(UserWithDeepPosts_relations));

  comment_cols[0].is_pk = true;
  post_cols[0].is_pk = true;
  user_cols[0].is_pk = true;
  user_cols[2].is_nullable = true;

  comment_m = Comment_meta;
  post_m = PostWithComments_meta;
  user_m = UserWithDeepPosts_meta;

  comment_m.columns = comment_cols;
  post_m.columns = post_cols;
  user_m.columns = user_cols;

  comment_m.name = "Comment";
  post_m.name = "Post";
  user_m.name = "User";

  user_m.query_select_by_pk = "SELECT * FROM User WHERE id = ?";

  post_rels[0].target_meta = &comment_m;
  post_m.relations = post_rels;
  post_m.num_relations = 1;

  user_rels[0].target_meta = &post_m;
  user_m.relations = user_rels;
  user_m.num_relations = 1;

  err = c_orm_sqlite_connect(":memory:", &db);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  err = c_orm_execute_raw(
      db,
      "CREATE TABLE User (id INTEGER PRIMARY KEY, team_id INTEGER, data BLOB)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err = c_orm_execute_raw(db, "CREATE TABLE Post (id INTEGER PRIMARY KEY, "
                              "title TEXT, author_id INTEGER)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err = c_orm_execute_raw(db, "CREATE TABLE Comment (id INTEGER PRIMARY KEY, "
                              "text TEXT, post_id INTEGER)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  err = c_orm_execute_raw(db, "INSERT INTO User (id, team_id) VALUES (5, 10)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err = c_orm_execute_raw(
      db,
      "INSERT INTO Post (id, title, author_id) VALUES (1, 'First Post', 5)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err = c_orm_execute_raw(
      db,
      "INSERT INTO Post (id, title, author_id) VALUES (2, 'Second Post', 5)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err = c_orm_execute_raw(
      db,
      "INSERT INTO Comment (id, text, post_id) VALUES (101, 'Nice post', 1)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err = c_orm_execute_raw(
      db, "INSERT INTO Comment (id, text, post_id) VALUES (102, 'Awesome', 1)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err = c_orm_execute_raw(
      db, "INSERT INTO Comment (id, text, post_id) VALUES (103, 'Meh', 2)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  memset(&user, 0, sizeof(user));

  err = c_orm_find_with_relations_int32(db, &user_m, 5, paths, 1, &user);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  ASSERT_EQ_FMT(5, user.id, "%d");
  ASSERT_EQ_FMT(1, user.posts.lazy_ctx.is_loaded, "%d");
  ASSERT_EQ_FMT(2, (int)user.posts.data.length, "%d");

  if (user.posts.data.length >= 2) {
    ASSERT_EQ_FMT(1, user.posts.data.data[0].id, "%d");
    ASSERT_EQ_FMT(1, user.posts.data.data[0].comments.lazy_ctx.is_loaded, "%d");
    ASSERT_EQ_FMT(2, (int)user.posts.data.data[0].comments.data.length, "%d");

    if (user.posts.data.data[0].comments.data.length >= 2) {
      ASSERT_EQ_FMT(101, user.posts.data.data[0].comments.data.data[0].id,
                    "%d");
      ASSERT_STR_EQ("Nice post",
                    user.posts.data.data[0].comments.data.data[0].text);
      ASSERT_EQ_FMT(102, user.posts.data.data[0].comments.data.data[1].id,
                    "%d");
      ASSERT_STR_EQ("Awesome",
                    user.posts.data.data[0].comments.data.data[1].text);
    }

    ASSERT_EQ_FMT(2, user.posts.data.data[1].id, "%d");
    ASSERT_EQ_FMT(1, user.posts.data.data[1].comments.lazy_ctx.is_loaded, "%d");
    ASSERT_EQ_FMT(1, (int)user.posts.data.data[1].comments.data.length, "%d");

    if (user.posts.data.data[1].comments.data.length >= 1) {
      ASSERT_EQ_FMT(103, user.posts.data.data[1].comments.data.data[0].id,
                    "%d");
      ASSERT_STR_EQ("Meh", user.posts.data.data[1].comments.data.data[0].text);
    }
  }

  /* Cleanup */
  if (user.posts.data.length > 0) {
    size_t i, j;
    for (i = 0; i < user.posts.data.length; i++) {
      if (user.posts.data.data[i].title)
        C_ORM_FREE(user.posts.data.data[i].title);
      if (user.posts.data.data[i].comments.data.length > 0) {
        for (j = 0; j < user.posts.data.data[i].comments.data.length; j++) {
          if (user.posts.data.data[i].comments.data.data[j].text)
            C_ORM_FREE(user.posts.data.data[i].comments.data.data[j].text);
        }
        C_ORM_FREE(user.posts.data.data[i].comments.data.data);
      }
    }
    C_ORM_FREE(user.posts.data.data);
  }

  {
    struct {
      void *data;
      size_t length;
      size_t capacity;
    } deep_arr;
    user_m.query_select_all = "SELECT id, team_id FROM User";
    memset(&deep_arr, 0, sizeof(deep_arr));
    err = c_orm_find_all_with_relations(db, &user_m, paths, 1, &deep_arr);
    ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
    if (deep_arr.length > 0) {
      struct UserWithDeepPosts *u_deep =
          (struct UserWithDeepPosts *)deep_arr.data;
      size_t ui, pi, ci;
      for (ui = 0; ui < deep_arr.length; ui++) {
        if (u_deep[ui].posts.data.data) {
          for (pi = 0; pi < u_deep[ui].posts.data.length; pi++) {
            if (u_deep[ui].posts.data.data[pi].title)
              C_ORM_FREE(u_deep[ui].posts.data.data[pi].title);
            if (u_deep[ui].posts.data.data[pi].comments.data.data) {
              for (ci = 0;
                   ci < u_deep[ui].posts.data.data[pi].comments.data.length;
                   ci++) {
                if (u_deep[ui].posts.data.data[pi].comments.data.data[ci].text)
                  C_ORM_FREE(u_deep[ui]
                                 .posts.data.data[pi]
                                 .comments.data.data[ci]
                                 .text);
              }
              C_ORM_FREE(u_deep[ui].posts.data.data[pi].comments.data.data);
            }
          }
          C_ORM_FREE(u_deep[ui].posts.data.data);
        }
      }
    }
    if (deep_arr.data)
      C_ORM_FREE(deep_arr.data);
  }

  if (db)
    db->vtable->disconnect(db);
  PASS();
}

/**
 * @brief Tests query builder relation filtering and SQL generation with EXISTS
 * subqueries.
 * @return GREATEST test result.
 */
TEST test_c_orm_query_builder_relation_filtering(void) {
  c_orm_db_t *db = NULL;
  c_orm_error_t err;
  c_orm_select_builder_t *builder = NULL;
  char *sql = NULL;

  c_orm_column_meta_t post_cols[3];
  c_orm_column_meta_t user_cols[3];
  c_orm_relation_meta_t user_rels[1];
  c_orm_table_meta_t post_m;
  c_orm_table_meta_t user_m;

  memcpy(post_cols, Post_columns, sizeof(Post_columns));
  memcpy(user_cols, UserWithPosts_columns, sizeof(UserWithPosts_columns));
  memcpy(user_rels, UserWithPosts_relations, sizeof(UserWithPosts_relations));
  post_cols[0].is_pk = true;
  user_cols[0].is_pk = true;

  post_m = Post_meta;
  user_m = UserWithPosts_meta;
  post_m.columns = post_cols;
  user_m.columns = user_cols;
  post_m.name = "Post";
  user_m.name = "User";

  user_rels[0].target_meta = &post_m;
  user_m.relations = user_rels;
  user_m.num_relations = 1;

  err = c_orm_sqlite_connect(":memory:", &db);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  err = c_orm_execute_raw(
      db,
      "CREATE TABLE User (id INTEGER PRIMARY KEY, team_id INTEGER, data BLOB)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err = c_orm_execute_raw(db, "CREATE TABLE Post (id INTEGER PRIMARY KEY, "
                              "title TEXT, author_id INTEGER)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  err = c_orm_execute_raw(db, "INSERT INTO User (id, team_id) VALUES (5, 10)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err = c_orm_execute_raw(db, "INSERT INTO User (id, team_id) VALUES (6, 11)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  err = c_orm_execute_raw(
      db,
      "INSERT INTO Post (id, title, author_id) VALUES (1, 'Target Post', 5)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  ASSERT_EQ_FMT(0, c_orm_select_builder_init(&user_m, &builder), "%d");
  ASSERT_EQ_FMT(0, c_orm_select_where_relation(builder, "posts.title", "="),
                "%d");
  ASSERT_EQ_FMT(0, c_orm_select_builder_compile(builder, &sql), "%d");

  /* Validate SQL string */
  ASSERT_STR_EQ("SELECT * FROM User WHERE EXISTS (SELECT 1 FROM Post t1 WHERE "
                "t1.author_id = User.id AND t1.title = ?)",
                sql);

  {
    c_orm_query_t *q;
    int has_row;
    int32_t user_id;

    has_row = 0;
    user_id = 0;
    err = c_orm_prepare_cached(db, sql, &q);
    ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
    err = db->vtable->bind_string(q, 1, "Target Post");
    ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
    err = db->vtable->step(q, &has_row);
    ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
    ASSERT_EQ_FMT(1, has_row, "%d");

    err = db->vtable->get_int32(q, 0, &user_id);
    ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
    ASSERT_EQ_FMT(5, user_id, "%d");

    err = c_orm_finalize_cached(db, q);
    ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  }

  C_ORM_FREE(sql);
  c_orm_select_builder_free(builder);

  if (db)
    db->vtable->disconnect(db);
  PASS();
}

#define NODE_FIELDS(X, S)                                                      \
  X(S, C_ORM_TYPE_INT32, int32_t, id)                                          \
  X(S, C_ORM_TYPE_STRING, char *, name)                                        \
  X(S, C_ORM_TYPE_INT32, int32_t *, parent_id)

C_ORM_STRUCT(Node, NODE_FIELDS)

/* Struct Forward Declaration explicitly needed for self-referencing macro */
struct NodeTree;

#define NODE_RELS(X, S)                                                        \
  C_ORM_HAS_MANY(X, S, NodeTree, children, "parent_id", "id")

C_ORM_STRUCT_WITH_RELATIONS(NodeTree, NODE_FIELDS, NODE_RELS)

/**
 * @brief Tests self-referencing hierarchy and tree structure queries.
 * @return GREATEST test result.
 */
TEST test_c_orm_self_referencing_tree(void) {
  c_orm_db_t *db = NULL;
  c_orm_error_t err;
  struct NodeTree root;
  c_orm_column_meta_t node_cols[3];
  c_orm_relation_meta_t node_rels[1];
  c_orm_table_meta_t node_m;

  memcpy(node_cols, NodeTree_columns, sizeof(NodeTree_columns));
  memcpy(node_rels, NodeTree_relations, sizeof(NodeTree_relations));
  node_cols[0].is_pk = true;
  node_cols[0].type = C_ORM_TYPE_INT32;
  node_cols[2].is_nullable = true;

  node_m = NodeTree_meta;
  node_m.columns = node_cols;
  node_m.name = "Node";

  node_rels[0].target_meta = &node_m;
  node_m.relations = node_rels;
  node_m.num_relations = 1;
  node_m.query_select_by_pk =
      "SELECT id, name, parent_id FROM Node WHERE id = ?";

  err = c_orm_sqlite_connect(":memory:", &db);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  err = c_orm_execute_raw(db, "CREATE TABLE Node (id INTEGER PRIMARY KEY, name "
                              "TEXT, parent_id INTEGER)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  err = c_orm_execute_raw(
      db, "INSERT INTO Node (id, name, parent_id) VALUES (1, 'Root', NULL)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err = c_orm_execute_raw(
      db, "INSERT INTO Node (id, name, parent_id) VALUES (2, 'Child A', 1)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err = c_orm_execute_raw(
      db, "INSERT INTO Node (id, name, parent_id) VALUES (3, 'Child B', 1)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err = c_orm_execute_raw(
      db, "INSERT INTO Node (id, name, parent_id) VALUES (4, 'Grandchild', 2)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  memset(&root, 0, sizeof(root));
  printf("DEBUG: cols types: %d, %d, %d\n", node_m.columns[0].type,
         node_m.columns[1].type, node_m.columns[2].type);
  err = c_orm_find_by_id_int32(db, &node_m, 1, &root);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  ASSERT_STR_EQ("Root", root.name);

  /* Lazy Load Children */
  err = c_orm_lazy_load(db, &node_m, &root, "children");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  ASSERT_EQ_FMT(1, root.children.lazy_ctx.is_loaded, "%d");
  ASSERT_EQ_FMT(2, (int)root.children.data.length, "%d");

  if (root.children.data.length >= 2) {
    ASSERT_STR_EQ("Child A", root.children.data.data[0].name);
    ASSERT_STR_EQ("Child B", root.children.data.data[1].name);

    /* Lazy Load Grandchildren */
    err = c_orm_lazy_load(db, &node_m, &root.children.data.data[0], "children");
    ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
    ASSERT_EQ_FMT(1, (int)root.children.data.data[0].children.data.length,
                  "%d");
    ASSERT_STR_EQ("Grandchild",
                  root.children.data.data[0].children.data.data[0].name);
  }

  /* Cleanup */
  if (root.name)
    C_ORM_FREE(root.name);
  if (root.children.data.length > 0) {
    size_t i;
    for (i = 0; i < root.children.data.length; i++) {
      if (root.children.data.data[i].name)
        C_ORM_FREE(root.children.data.data[i].name);
      if (root.children.data.data[i].parent_id)
        C_ORM_FREE(root.children.data.data[i].parent_id);
      if (root.children.data.data[i].children.data.length > 0) {
        size_t j;
        for (j = 0; j < root.children.data.data[i].children.data.length; j++) {
          if (root.children.data.data[i].children.data.data[j].name)
            C_ORM_FREE(root.children.data.data[i].children.data.data[j].name);
          if (root.children.data.data[i].children.data.data[j].parent_id)
            C_ORM_FREE(
                root.children.data.data[i].children.data.data[j].parent_id);
        }
        C_ORM_FREE(root.children.data.data[i].children.data.data);
      }
    }
    C_ORM_FREE(root.children.data.data);
  }

  if (db)
    db->vtable->disconnect(db);
  PASS();
}

/**
 * @brief Generic array container for test relations.
 */
struct Generic_Array {
  void *data;      /**< Pointer to array elements buffer. */
  size_t length;   /**< Current number of elements. */
  size_t capacity; /**< Total allocated capacity. */
};

/**
 * @brief Test structure containing foreign keys and lazy relation contexts.
 */
struct TestObj {
  char *str_fk; /**< String foreign key. */
  float flt_fk; /**< Float foreign key for invalid type testing. */
  void *child;  /**< Pointer to child entity. */
  struct Generic_Array items_arr; /**< Array of through-relation items. */
  c_orm_lazy_load_context_t ctx;  /**< Lazy loading context. */
};

/**
 * @brief Tests advanced relation features including string foreign keys,
 * through relations, and error handling.
 * @return GREATEST test result.
 */
TEST test_c_orm_relation_advanced_features(void) {
  c_orm_db_t *db;
  c_orm_error_t err;
  c_orm_table_meta_t p_meta;
  c_orm_table_meta_t c_meta;
  c_orm_column_meta_t p_cols[2];
  c_orm_column_meta_t c_cols[3];
  c_orm_relation_meta_t rels[2];
  struct TestObj obj;

  db = NULL;
  memset(&obj, 0, sizeof(obj));
  memset(p_cols, 0, sizeof(p_cols));
  memset(c_cols, 0, sizeof(c_cols));
  memset(rels, 0, sizeof(rels));
  memset(&p_meta, 0, sizeof(p_meta));
  memset(&c_meta, 0, sizeof(c_meta));

  err = c_orm_sqlite_connect(":memory:", &db);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  /* Setup c_meta (target) */
  c_cols[0].name = "id";
  c_cols[0].type = C_ORM_TYPE_INT32;
  c_cols[0].is_pk = 1;
  c_cols[0].offset = 0;

  c_cols[1].name = "code";
  c_cols[1].type = C_ORM_TYPE_STRING;
  c_cols[1].offset = sizeof(void *);

  c_cols[2].name = "deleted_at";
  c_cols[2].type = C_ORM_TYPE_STRING;
  c_cols[2].is_nullable = 1;
  c_cols[2].offset = sizeof(void *) + sizeof(char *);

  c_meta.name = "items";
  c_meta.columns = c_cols;
  c_meta.num_columns = 3;
  c_meta.struct_size = sizeof(void *) + 2 * sizeof(char *);

  /* Setup p_meta (source) */
  p_cols[0].name = "str_fk";
  p_cols[0].type = C_ORM_TYPE_STRING;
  p_cols[0].offset = offsetof(struct TestObj, str_fk);

  p_cols[1].name = "flt_fk";
  p_cols[1].type = C_ORM_TYPE_FLOAT;
  p_cols[1].offset = offsetof(struct TestObj, flt_fk);

  p_meta.name = "source_tbl";
  p_meta.columns = p_cols;
  p_meta.num_columns = 2;
  p_meta.struct_size = sizeof(obj);

  /* Rel 0: ONE_TO_ONE with string FK */
  rels[0].field_name = "item";
  rels[0].type = C_ORM_RELATION_ONE_TO_ONE;
  rels[0].local_key = "str_fk";
  rels[0].foreign_key = "code";
  rels[0].target_meta = &c_meta;
  rels[0].data_offset = offsetof(struct TestObj, child);
  rels[0].struct_offset = offsetof(struct TestObj, child);
  rels[0].lazy_ctx_offset = offsetof(struct TestObj, ctx);

  /* Rel 1: HAS_MANY_THROUGH */
  rels[1].field_name = "through_items";
  rels[1].type = C_ORM_RELATION_HAS_MANY_THROUGH;
  rels[1].local_key = "str_fk";
  rels[1].foreign_key = "id";
  rels[1].join_table = "bridge";
  rels[1].join_local_key = "src_str";
  rels[1].join_foreign_key = "item_id";
  rels[1].target_meta = &c_meta;
  rels[1].data_offset = offsetof(struct TestObj, items_arr);
  rels[1].struct_offset = offsetof(struct TestObj, items_arr);
  rels[1].lazy_ctx_offset = offsetof(struct TestObj, ctx);
  rels[1].custom_filter = "items.id > 0";
  rels[1].order_by = "items.id DESC";
  rels[1].soft_delete_aware = 1;

  p_meta.relations = rels;
  p_meta.num_relations = 2;

  /* 1. Nullable string FK == NULL -> returns C_ORM_OK immediately */
  obj.str_fk = NULL;
  err = c_orm_load_relation_ext(db, &obj, &p_meta, 0, 0, 0);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  /* 2. String FK != NULL, ONE_TO_ONE query with table created */
  c_orm_execute_raw(db,
                    "CREATE TABLE items (id INT, code TEXT, deleted_at TEXT);");
  c_orm_execute_raw(db, "CREATE TABLE bridge (src_str TEXT, item_id INT);");

  obj.str_fk = "code_123";
  err = c_orm_load_relation_ext(db, &obj, &p_meta, 0, 0, 0);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  ASSERT_EQ(NULL, obj.child);

  /* 3. HAS_MANY_THROUGH with filter, order, soft delete, limit, offset */
  c_orm_execute_raw(
      db,
      "INSERT INTO items (id, code, deleted_at) VALUES (1, 'item1', NULL);");
  c_orm_execute_raw(
      db, "INSERT INTO bridge (src_str, item_id) VALUES ('code_123', 1);");
  err = c_orm_load_relation_ext(db, &obj, &p_meta, 1, 10, 0);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  if (obj.items_arr.data) {
    C_ORM_FREE(obj.items_arr.data);
    obj.items_arr.data = NULL;
  }
  obj.ctx.is_loaded = 0;
  err = c_orm_load_relation_ext(db, &obj, &p_meta, 1, 10, 5);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  /* 4. Missing join_table in HAS_MANY_THROUGH */
  obj.ctx.is_loaded = 0;
  rels[1].join_table = NULL;
  err = c_orm_load_relation_ext(db, &obj, &p_meta, 1, 0, 0);
  ASSERT_EQ_FMT(C_ORM_ERROR_UNKNOWN, err, "%d");
  rels[1].join_table = "bridge";

  /* 5. Invalid FK column type (FLOAT) */
  rels[0].local_key = "flt_fk";
  err = c_orm_load_relation_ext(db, &obj, &p_meta, 0, 0, 0);
  ASSERT_EQ_FMT(C_ORM_ERROR_UNKNOWN, err, "%d");
  rels[0].local_key = "str_fk";

  /* 6. Local key not found in table columns */
  rels[0].local_key = "nonexistent_col";
  err = c_orm_load_relation_ext(db, &obj, &p_meta, 0, 0, 0);
  ASSERT_EQ_FMT(C_ORM_ERROR_NOT_FOUND, err, "%d");
  rels[0].local_key = "str_fk";

  db->vtable->disconnect(db);
  PASS();
}

/**
 * @brief Test suite registering relational mapping and cascade operation tests.
 */
SUITE(relations_suite) {
  RUN_TEST(test_c_orm_lazy_load_relations);
  RUN_TEST(test_c_orm_eager_load_relations);
  RUN_TEST(test_c_orm_nested_insert_relations);
  RUN_TEST(test_c_orm_cascade_delete_and_update);
  RUN_TEST(test_c_orm_one_to_many_lazy_load);
  RUN_TEST(test_c_orm_lazy_load_paginated);
  RUN_TEST(test_c_orm_many_to_many_cascade_delete);
  RUN_TEST(test_c_orm_query_builder_relation_filtering);
  RUN_TEST(test_c_orm_deeply_nested_eager_loads);
  RUN_TEST(test_c_orm_self_referencing_tree);
  RUN_TEST(test_c_orm_relation_advanced_features);
}

#if defined(__clang__) || defined(__GNUC__)
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */
