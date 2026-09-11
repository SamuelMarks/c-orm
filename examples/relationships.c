#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file relationships.c
 * @brief Demonstrates entity relationship mappings and cascade operations in
 * c-orm.
 */

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

c_orm_error_t run_relationships_ops(c_orm_db_t *db, const char *rel_name,
                                    int op_mode);
c_orm_error_t run_relationships_example(const char *db_path);

/**
 * @brief Execute relationship operations including insert, attach, and cascade
 * delete.
 *
 * @param db Database handle.
 * @param rel_name Relationship field name.
 * @param op_mode Operational mode for testing error branches.
 * @return C_ORM_OK on success or error code.
 */
c_orm_error_t run_relationships_ops(c_orm_db_t *db, const char *rel_name,
                                    int op_mode) {
  c_orm_error_t rc;
  struct User new_user;
  struct Role r;
  struct User fetched;
  const char *paths[2];
  int i;

  c_orm_column_meta_t post_cols[3];
  c_orm_column_meta_t user_cols[2];
  c_orm_column_meta_t role_cols[2];
  c_orm_relation_meta_t user_rels[2];

  c_orm_table_meta_t post_m;
  c_orm_table_meta_t user_m;
  c_orm_table_meta_t role_m;

  static const char *stmts[] = {
      "CREATE TABLE User (id INTEGER PRIMARY KEY, username TEXT)",
      "CREATE TABLE Post (id INTEGER PRIMARY KEY, title TEXT, user_id INTEGER)",
      "CREATE TABLE Role (id INTEGER PRIMARY KEY, name TEXT)",
      "CREATE TABLE user_roles (user_id INTEGER, role_id INTEGER)",
      "INSERT INTO Role (id, name) VALUES (1, 'Admin'), (2, 'Editor')",
      NULL};

  if (!db)
    return C_ORM_ERROR_MEMORY;
  if (!rel_name)
    return C_ORM_ERROR_MEMORY;

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
  user_m.query_delete_by_pk = (op_mode == 4)
                                  ? "DELETE FROM InvalidTbl WHERE id = ?"
                                  : "DELETE FROM User WHERE id = ?";

  printf("Creating tables...\n");
  for (i = 0; stmts[i] != NULL; ++i) {
    rc = c_orm_execute_raw(db, stmts[i]);
    if (rc != C_ORM_OK)
      return rc;
  }

  /* Insert nested User */
  memset(&new_user, 0, sizeof(new_user));
  new_user.id = 1;
  new_user.username = "Alice";

  if (op_mode == 1) {
    c_orm_execute_raw(db, "INSERT INTO User VALUES (1, 'dup')");
  }

  printf("Inserting User Alice...\n");
  rc = c_orm_insert(db, &user_m, &new_user);
  if (rc != C_ORM_OK)
    return rc;

  printf("Attaching Role 1 & 2 to Alice...\n");
  memset(&r, 0, sizeof(r));
  r.id = 1;
  rc = c_orm_attach(db, &user_m, &new_user, rel_name, &r);
  if (rc != C_ORM_OK)
    return rc;

  r.id = 2;
  rc = c_orm_attach(db, &user_m, &new_user,
                    (op_mode == 2) ? "bad_rel" : rel_name, &r);
  if (rc != C_ORM_OK)
    return rc;

  printf("Fetching User with nested relationships natively...\n");
  paths[0] = (op_mode == 3) ? "bad_rel" : "roles";
  paths[1] = "posts";
  memset(&fetched, 0, sizeof(fetched));

  rc = c_orm_find_with_relations_int32(db, &user_m, new_user.id, paths, 2,
                                       &fetched);
  if (rc != C_ORM_OK)
    return rc;

  printf("Found User: %s (ID: %d)\n", fetched.username, fetched.id);
  printf("  -> Roles loaded: %d\n", (int)fetched.roles.data.length);
  printf("     First Role ID: %d\n", fetched.roles.data.data[0].id);

  c_orm_free_relations(&user_m, &fetched);
  free(fetched.username);

  printf("Demonstrating Cascade Delete (Cleans User, Posts, and Join Table "
         "rows)...\n");
  rc = c_orm_delete(db, &user_m, &new_user);
  if (rc != C_ORM_OK)
    return rc;

  printf("Alice Deleted Safely.\n");
  return C_ORM_OK;
}

/**
 * @brief Run the complete relationships example connecting to a database path.
 *
 * @param db_path Database path or :memory:.
 * @return C_ORM_OK on success or error code.
 */
c_orm_error_t run_relationships_example(const char *db_path) {
  c_orm_error_t rc;
  c_orm_db_t *db;

  db = NULL;
  if (!db_path)
    return C_ORM_ERROR_MEMORY;

  printf("Connecting to memory DB...\n");
  rc = c_orm_sqlite_connect(db_path, &db);
  if (rc != C_ORM_OK)
    return rc;

  rc = run_relationships_ops(db, "roles", 0);
  db->vtable->disconnect(db);

  return rc;
}

int main(void);

/**
 * @brief Main entry point for relationships example.
 *
 * @return 0 on success.
 */
int main(void) {
  c_orm_db_t *db;

  run_relationships_example(":memory:");
  run_relationships_example(NULL);
#ifdef _WIN32
  run_relationships_example("Z:\\invalid_dir\\bad.db");
#else
  run_relationships_example("/dev/null/invalid_dir/bad.db");
#endif
  run_relationships_ops(NULL, "roles", 0);

  db = NULL;
  c_orm_sqlite_connect(":memory:", &db);
  run_relationships_ops(db, NULL, 0);
  c_orm_execute_raw(db, "CREATE TABLE User (id INT)");
  run_relationships_ops(db, "roles", 0);
  db->vtable->disconnect(db);

  db = NULL;
  c_orm_sqlite_connect(":memory:", &db);
  run_relationships_ops(db, "invalid_rel", 0);
  db->vtable->disconnect(db);

  db = NULL;
  c_orm_sqlite_connect(":memory:", &db);
  run_relationships_ops(db, "roles", 1);
  db->vtable->disconnect(db);

  db = NULL;
  c_orm_sqlite_connect(":memory:", &db);
  run_relationships_ops(db, "roles", 2);
  db->vtable->disconnect(db);

  db = NULL;
  c_orm_sqlite_connect(":memory:", &db);
  run_relationships_ops(db, "roles", 3);
  db->vtable->disconnect(db);

  db = NULL;
  c_orm_sqlite_connect(":memory:", &db);
  run_relationships_ops(db, "roles", 4);
  db->vtable->disconnect(db);

  return 0;
}

#if defined(__clang__) || defined(__GNUC__)
#endif
