#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file blog.c
 * @brief Demonstrates using the specific struct mapping API in c-orm.
 */

/* clang-format off */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c_orm_api.h"
#include "c_orm_sqlite.h"
/* clang-format on */

typedef struct BlogPost {
  int32_t id;
  char *title;
  char *content;
  int32_t author_id;
} BlogPost;

static c_orm_column_meta_t blog_post_cols[] = {
    {"id", C_ORM_TYPE_INT32, offsetof(BlogPost, id), 1, 0, NULL, 0, 0},
    {"title", C_ORM_TYPE_STRING, offsetof(BlogPost, title), 0, 0, NULL, 0, 0},
    {"content", C_ORM_TYPE_STRING, offsetof(BlogPost, content), 0, 0, NULL, 0,
     0},
    {"author_id", C_ORM_TYPE_INT32, offsetof(BlogPost, author_id), 0, 0, NULL,
     0, 0},
};

static c_orm_table_meta_t BlogPost_meta = {
    "blog_posts",
    blog_post_cols,
    4,
    sizeof(BlogPost),
    "SELECT * FROM blog_posts",
    "SELECT * FROM blog_posts WHERE id=?",
    "INSERT INTO blog_posts VALUES(?,?,?,?)",
    "UPDATE blog_posts SET title=?, content=?, author_id=? WHERE id=?",
    "DELETE FROM blog_posts WHERE id=?",
    NULL,
    0,
    0,
    0,
    0,
    {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
    NULL,
    0};

c_orm_error_t run_blog_ops(c_orm_db_t *db, int32_t find_id, int skip_create);
c_orm_error_t run_blog_example(const char *db_path);

/**
 * @brief Execute blog database operations including create, insert, and find.
 *
 * @param db Database handle.
 * @param find_id ID of the blog post to find.
 * @param skip_create Non-zero to skip table creation.
 * @return C_ORM_OK on success or error code.
 */
c_orm_error_t run_blog_ops(c_orm_db_t *db, int32_t find_id, int skip_create) {
  c_orm_error_t rc;
  BlogPost post;
  BlogPost fetched;

  if (!db)
    return C_ORM_ERROR_MEMORY;

  if (!skip_create) {
    rc = c_orm_execute_raw(db,
                           "CREATE TABLE blog_posts (id INTEGER PRIMARY KEY, "
                           "title TEXT, content TEXT, author_id INTEGER)");
    if (rc != C_ORM_OK) {
      printf("c_orm_execute_raw err %d\n", (int)rc);
      return rc;
    }
  }

  memset(&post, 0, sizeof(post));
  post.id = 1;
  post.title = "Hello c-orm";
  post.content = "This is a great new object-mapper for C89.";
  post.author_id = 99;

  rc = c_orm_insert(db, &BlogPost_meta, &post);
  if (rc != C_ORM_OK) {
    printf("Failed to insert: %d\n", (int)rc);
    return rc;
  }
  printf("Successfully inserted blog post.\n");

  memset(&fetched, 0, sizeof(fetched));
  rc = c_orm_find_by_id_int32(db, &BlogPost_meta, find_id, &fetched);
  if (rc != C_ORM_OK) {
    printf("Failed to find: %d\n", (int)rc);
    return rc;
  }
  printf("Fetched Post: %s -> %s\n", fetched.title, fetched.content);
  free(fetched.title);
  free(fetched.content);

  return C_ORM_OK;
}

/**
 * @brief Run the complete blog example connecting to a database path.
 *
 * @param db_path Database path or :memory:.
 * @return C_ORM_OK on success or error code.
 */
c_orm_error_t run_blog_example(const char *db_path) {
  c_orm_error_t rc;
  c_orm_db_t *db;

  db = NULL;
  if (!db_path)
    return C_ORM_ERROR_MEMORY;

  printf("Starting Blog Example...\n");

  rc = c_orm_sqlite_connect(db_path, &db);
  if (rc != C_ORM_OK) {
    printf("Failed to connect to SQLite: %d\n", (int)rc);
    return rc;
  }

  rc = run_blog_ops(db, 1, 0);
  db->vtable->disconnect(db);

  return rc;
}

int main(void);

/**
 * @brief Main entry point for blog example.
 *
 * @return 0 on success.
 */
int main(void) {
  c_orm_db_t *db;

  run_blog_example(":memory:");
  run_blog_example(NULL);
#ifdef _WIN32
  run_blog_example("Z:\\invalid_dir\\bad.db");
#else
  run_blog_example("/dev/null/invalid_dir/bad.db");
#endif
  run_blog_ops(NULL, 1, 0);

  db = NULL;
  c_orm_sqlite_connect(":memory:", &db);
  c_orm_execute_raw(db, "CREATE TABLE blog_posts (id INT)");
  run_blog_ops(db, 1, 0);
  db->vtable->disconnect(db);

  db = NULL;
  c_orm_sqlite_connect(":memory:", &db);
  c_orm_execute_raw(db, "CREATE TABLE blog_posts (id INTEGER PRIMARY KEY, "
                        "title TEXT, content TEXT, author_id INTEGER)");
  c_orm_execute_raw(db,
                    "INSERT INTO blog_posts VALUES(1, 'title', 'content', 1)");
  run_blog_ops(db, 1, 1);

  c_orm_execute_raw(db, "DELETE FROM blog_posts");
  run_blog_ops(db, 999, 1);
  db->vtable->disconnect(db);

  return 0;
}

#if defined(__clang__) || defined(__GNUC__)
#endif
