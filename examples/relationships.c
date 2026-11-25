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
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif
/* clang-format on */

#define ROLE_FIELDS(X, S)                                                      \
  X(S, C_ORM_TYPE_INT32, int32_t, id)                                          \
  X(S, C_ORM_TYPE_STRING, char *, name)
/**
 * @brief Role entity structure.
 * @var id Role identifier.
 * @var name Role name string.
 */
C_ORM_STRUCT(Role, ROLE_FIELDS)

#define POST_FIELDS(X, S)                                                      \
  X(S, C_ORM_TYPE_INT32, int32_t, id)                                          \
  X(S, C_ORM_TYPE_STRING, char *, title)                                       \
  X(S, C_ORM_TYPE_INT32, int32_t, user_id)
/**
 * @brief Post entity structure.
 * @var id Post identifier.
 * @var title Post title string.
 * @var user_id Associated user identifier.
 */
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
/**
 * @brief User entity structure with relationships.
 * @var id User identifier.
 * @var username User name string.
 * @var posts Has-many collection of posts.
 * @var roles Many-to-many collection of roles.
 */
C_ORM_STRUCT_WITH_RELATIONS(User, USER_FIELDS, USER_RELS)

/**
 * @brief Forward declaration of relationship operations runner.
 * @param db Database handle.
 * @param rel_name Relationship field name.
 * @param op_mode Operational mode for testing error branches.
 * @return C_ORM_OK on success or error code.
 */
/**
 * @brief Releases loaded relations on non-NULL `obj` via
 * `c_orm_free_relations`.
 * @param meta Non-NULL table metadata.
 * @param obj Non-NULL struct instance.
 * @return Status code cast to int.
 */
static int rel_calc_free_relations(const c_orm_table_meta_t *meta, void *obj) {
  return (int)c_orm_free_relations(meta, obj);
}

/**
 * @brief Disconnects `db` during error cleanup.
 * @param db Database connection handle.
 * @return C_ORM_OK on success, or driver disconnect error code.
 */
static c_orm_error_t rel_conn_cleanup_free(c_orm_db_t *db) {
  return db->vtable->disconnect(db);
}

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
  char alice_buf[] = "Alice";
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
  new_user.username = alice_buf;

  if (op_mode == 1 || op_mode == 5) {
    rc = c_orm_execute_raw(db, (op_mode == 5)
                                   ? "INSERT INTO MissingTbl VALUES (1)"
                                   : "INSERT INTO User VALUES (1, 'dup')");
    if (rc != C_ORM_OK)
      return rc;
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

  c_orm_system_free(fetched.username);
  (i = rel_calc_free_relations(&user_m, &fetched));

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
  if (rc != C_ORM_OK) {
    rel_conn_cleanup_free(db);
    return rc;
  }

  return db->vtable->disconnect(db);
}

int main(void);

/**
 * @brief Main entry point for relationships example.
 *
 * @return 0 on success.
 */
int main(void) {
  c_orm_db_t *db = NULL;
  int mode;
  int failed = 0;

#if defined(_MSC_VER) && defined(_DEBUG)
  _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
  _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

  remove("rel_err_example.db");
  failed |= (run_relationships_example("rel_err_example.db") != C_ORM_OK);
  failed |= (run_relationships_example("rel_err_example.db") == C_ORM_OK);
  remove("rel_err_example.db");

  failed |= (run_relationships_example(":memory:") != C_ORM_OK);
  failed |= (run_relationships_example(NULL) == C_ORM_OK);
#ifdef _WIN32
  failed |= (run_relationships_example("Z:\\invalid_dir\\bad.db") == C_ORM_OK);
#else
  failed |=
      (run_relationships_example("/dev/null/invalid_dir/bad.db") == C_ORM_OK);
#endif
  failed |= (run_relationships_ops(NULL, "roles", 0) == C_ORM_OK);

  failed |= (c_orm_sqlite_connect(":memory:", &db) != C_ORM_OK);
  failed |= (run_relationships_ops(db, NULL, 0) == C_ORM_OK);
  failed |= (c_orm_execute_raw(db, "CREATE TABLE User (id INT)") != C_ORM_OK);
  failed |= (run_relationships_ops(db, "roles", 0) == C_ORM_OK);
  failed |= (db->vtable->disconnect(db) != C_ORM_OK);

  db = NULL;
  failed |= (c_orm_sqlite_connect(":memory:", &db) != C_ORM_OK);
  failed |= (run_relationships_ops(db, "invalid_rel", 0) == C_ORM_OK);
  failed |= (db->vtable->disconnect(db) != C_ORM_OK);

  for (mode = 1; mode <= 5; mode++) {
    db = NULL;
    failed |= (c_orm_sqlite_connect(":memory:", &db) != C_ORM_OK);
    failed |= (run_relationships_ops(db, "roles", mode) == C_ORM_OK);
    failed |= (db->vtable->disconnect(db) != C_ORM_OK);
  }

  return failed;
}
