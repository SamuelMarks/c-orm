/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_struct.h"
#include "c_orm_sqlite.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

#define ROLE_FIELDS(X, S)                                                      \
  X(S, C_ORM_TYPE_INT32, int32_t, id)                                          \
  X(S, C_ORM_TYPE_STRING, char *, name)
C_ORM_STRUCT(Role, ROLE_FIELDS)

#define POST_FIELDS(X, S)                                                      \
  X(S, C_ORM_TYPE_INT32, int32_t, id)                                          \
  X(S, C_ORM_TYPE_STRING, char *, title)                                       \
  X(S, C_ORM_TYPE_INT32, int32_t, user_id)
C_ORM_STRUCT(Post, POST_FIELDS)

#define USER_FIELDS(X, S)                                                      \
  X(S, C_ORM_TYPE_INT32, int32_t, id)                                          \
  X(S, C_ORM_TYPE_STRING, char *, username)

#define USER_RELS(X, S)                                                        \
  C_ORM_HAS_MANY_CASCADE(X, S, Post, posts, "user_id", "id",                   \
                         C_ORM_CASCADE_DELETE, C_ORM_CASCADE_UPDATE)           \
  C_ORM_MANY_TO_MANY_CASCADE(X, S, Role, roles, "id", "id", "user_roles",      \
                             "user_id", "role_id", C_ORM_CASCADE_DELETE,       \
                             C_ORM_CASCADE_UPDATE)
C_ORM_STRUCT_WITH_RELATIONS(User, USER_FIELDS, USER_RELS)

int main(void) {
  c_orm_db_t *db = NULL;
  c_orm_error_t err;
  struct User new_user;

  c_orm_column_meta_t post_cols[3];
  c_orm_column_meta_t user_cols[2];
  c_orm_column_meta_t role_cols[2];
  c_orm_relation_meta_t user_rels[2];

  c_orm_table_meta_t post_m;
  c_orm_table_meta_t user_m;
  c_orm_table_meta_t role_m;

  /* Initialize Metadata */
  memcpy(post_cols, Post_columns, sizeof(Post_columns));
  memcpy(user_cols, User_columns, sizeof(User_columns));
  memcpy(role_cols, Role_columns, sizeof(Role_columns));
  memcpy(user_rels, User_relations, sizeof(User_relations));

  post_cols[0].is_pk = true;
  user_cols[0].is_pk = true;
  role_cols[0].is_pk = true;

  post_m = Post_meta;
  user_m = User_meta;
  role_m = Role_meta;

  post_m.columns = post_cols;
  user_m.columns = user_cols;
  role_m.columns = role_cols;

  post_m.name = "Post";
  user_m.name = "User";
  role_m.name = "Role";

  user_rels[0].target_meta = &post_m;
  user_rels[1].target_meta = &role_m;
  user_m.relations = user_rels;
  user_m.num_relations = 2;

  user_m.query_select_by_pk = "SELECT * FROM User WHERE id = ?";
  user_m.query_insert =
      "INSERT INTO User (id, username) VALUES (NULLIF(?, 0), ?)";
  user_m.query_delete_by_pk = "DELETE FROM User WHERE id = ?";

  printf("Connecting to memory DB...\n");
  err = c_orm_sqlite_connect(":memory:", &db);
  if (err != C_ORM_OK)
    return 1;

  printf("Creating tables...\n");
  c_orm_execute_raw(
      db, "CREATE TABLE User (id INTEGER PRIMARY KEY, username TEXT)");
  c_orm_execute_raw(db, "CREATE TABLE Post (id INTEGER PRIMARY KEY, title "
                        "TEXT, user_id INTEGER)");
  c_orm_execute_raw(db,
                    "CREATE TABLE Role (id INTEGER PRIMARY KEY, name TEXT)");
  c_orm_execute_raw(
      db, "CREATE TABLE user_roles (user_id INTEGER, role_id INTEGER)");

  c_orm_execute_raw(
      db, "INSERT INTO Role (id, name) VALUES (1, 'Admin'), (2, 'Editor')");

  /* Insert nested User */
  memset(&new_user, 0, sizeof(new_user));
  new_user.username = "Alice";

  printf("Inserting User Alice...\n");
  err = c_orm_insert(db, &user_m, &new_user);
  if (err != C_ORM_OK) {
    printf("Insert failed\n");
    return 1;
  }

  printf("Attaching Role 1 & 2 to Alice...\n");
  {
    struct Role r;
    memset(&r, 0, sizeof(r));
    r.id = 1;
    c_orm_attach(db, &user_m, &new_user, "roles", &r);
    r.id = 2;
    c_orm_attach(db, &user_m, &new_user, "roles", &r);
  }

  printf("Fetching User with nested relationships natively...\n");
  {
    struct User fetched;
    const char *paths[] = {"roles", "posts"};
    memset(&fetched, 0, sizeof(fetched));

    err = c_orm_find_with_relations_int32(db, &user_m, new_user.id, paths, 2,
                                          &fetched);
    if (err == C_ORM_OK) {
      printf("Found User: %s (ID: %d)\n", fetched.username, fetched.id);
      printf("  -> Roles loaded: %d\n", (int)fetched.roles.data.length);
      if (fetched.roles.data.length > 0) {
        printf("     First Role ID: %d\n", fetched.roles.data.data[0].id);
      }
    }

    c_orm_free_relations(&user_m, &fetched);
    if (fetched.username)
      free(fetched.username);
  }

  printf("Demonstrating Cascade Delete (Cleans User, Posts, and Join Table "
         "rows)...\n");
  err = c_orm_delete(db, &user_m, &new_user);
  if (err == C_ORM_OK) {
    printf("Alice Deleted Safely.\n");
  }

  if (db)
    db->vtable->disconnect(db);
  return 0;
}