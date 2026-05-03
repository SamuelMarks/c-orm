/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_struct.h"
#include "c_orm_sqlite.h"
#include "c_orm_query_builder.h"
#include "greatest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

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

TEST test_c_orm_cascade_delete_and_update(void) {
  c_orm_db_t *db = NULL;
  c_orm_error_t err;
  struct UserCascade user;
  struct Team new_team;
  int exists = 0;

  c_orm_column_meta_t team_cols[3];
  c_orm_column_meta_t user_cols[3];
  c_orm_relation_meta_t user_rels[1];
  c_orm_table_meta_t team_m;
  c_orm_table_meta_t user_m;

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
  new_team.id = 0;
  new_team.name = "Support";

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
    free(user.team.data->name);
  free(user.team.data);

  if (db)
    db->vtable->disconnect(db);
  PASS();
}

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
      db, "INSERT INTO Team (id, name, is_active) VALUES (20, 'Sales', 1)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  err = c_orm_execute_raw(db, "INSERT INTO User (id, team_id) VALUES (2, 20)");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  printf("EAGER LOAD REL OFFSETS: struct=%zu data=%zu lazy=%zu\n",
         user_m.relations[0].struct_offset, user_m.relations[0].data_offset,
         user_m.relations[0].lazy_ctx_offset);
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
    free(user.team.data->name);
  free(user.team.data);
  printf("FREED\n");
  fflush(stdout);

  if (db)
    db->vtable->disconnect(db);
  PASS();
}

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
  new_team.id = 0; /* will be auto-assigned */
  new_team.name = "Marketing";

  user.id = 0;
  user.team_id = 0; /* will be auto-assigned from new_team.id */
  user.team.data = &new_team;
  user.team.lazy_ctx.is_loaded = 1;

  err = c_orm_insert(db, &user_m, &user);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  /* Assert FK was assigned */
  ASSERT(user.team_id > 0);

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
    free(user.posts.data.data[0].title);
  if (user.posts.data.data[1].title)
    free(user.posts.data.data[1].title);
  if (user.posts.data.data)
    free(user.posts.data.data);

  if (db)
    db->vtable->disconnect(db);
  PASS();
}

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
    free(user.posts.data.data[0].title);
  if (user.posts.data.data)
    free(user.posts.data.data);

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
    int count = 0;
    err = c_orm_prepare_cached(db, "SELECT COUNT(*) FROM user_roles", &query);
    ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
    db->vtable->step(query, &exists);
    if (exists) {
      db->vtable->get_int32(query, 0, &count);
    }
    c_orm_finalize_cached(db, query);
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
        free(user.posts.data.data[i].title);
      if (user.posts.data.data[i].comments.data.length > 0) {
        for (j = 0; j < user.posts.data.data[i].comments.data.length; j++) {
          if (user.posts.data.data[i].comments.data.data[j].text)
            free(user.posts.data.data[i].comments.data.data[j].text);
        }
        free(user.posts.data.data[i].comments.data.data);
      }
    }
    free(user.posts.data.data);
  }

  if (db)
    db->vtable->disconnect(db);
  PASS();
}

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
    int has_row = 0;
    int32_t user_id = 0;
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

    c_orm_finalize_cached(db, q);
  }

  free(sql);
  c_orm_select_builder_free(builder);

  if (db)
    db->vtable->disconnect(db);
  PASS();
}

#define NODE_FIELDS(X, S)                                                      \
  X(S, C_ORM_TYPE_INT32, int32_t, id)                                          \
  X(S, C_ORM_TYPE_STRING, char *, name)                                        \
  X(S, C_ORM_TYPE_INT32, int32_t, parent_id)

C_ORM_STRUCT(Node, NODE_FIELDS)

/* Struct Forward Declaration explicitly needed for self-referencing macro */
struct NodeTree;

#define NODE_RELS(X, S)                                                        \
  C_ORM_HAS_MANY(X, S, NodeTree, children, "parent_id", "id")

C_ORM_STRUCT_WITH_RELATIONS(NodeTree, NODE_FIELDS, NODE_RELS)

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
    free(root.name);
  if (root.children.data.length > 0) {
    size_t i;
    for (i = 0; i < root.children.data.length; i++) {
      if (root.children.data.data[i].name)
        free(root.children.data.data[i].name);
      if (root.children.data.data[i].children.data.length > 0) {
        size_t j;
        for (j = 0; j < root.children.data.data[i].children.data.length; j++) {
          if (root.children.data.data[i].children.data.data[j].name)
            free(root.children.data.data[i].children.data.data[j].name);
        }
        free(root.children.data.data[i].children.data.data);
      }
    }
    free(root.children.data.data);
  }

  if (db)
    db->vtable->disconnect(db);
  PASS();
}

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
}
