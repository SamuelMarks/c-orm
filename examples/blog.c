/**
 * @file blog.c
 * @brief Demonstrates using the specific struct mapping API in c-orm.
 */

/* clang-format off */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

#include "c_orm_api.h"
#include "c_orm_sqlite.h"
/* clang-format on */

/**
 * @brief BlogPost domain model.
 */
typedef struct BlogPost {
  /** @brief Unique blog post identifier. */
  int32_t id;
  /** @brief Post title string. */
  char *title;
  /** @brief Post body content string. */
  char *content;
  /** @brief Author user ID. */
  int32_t author_id;
} BlogPost;

static c_orm_column_meta_t blog_post_cols[] = {
    {"id", C_ORM_TYPE_INT32, offsetof(BlogPost, id), 1, 0, NULL, 0, 0, 0, 0, 0,
     0},
    {"title", C_ORM_TYPE_STRING, offsetof(BlogPost, title), 0, 0, NULL, 0, 0, 0,
     0, 0, 0},
    {"content", C_ORM_TYPE_STRING, offsetof(BlogPost, content), 0, 0, NULL, 0,
     0, 0, 0, 0, 0},
    {"author_id", C_ORM_TYPE_INT32, offsetof(BlogPost, author_id), 0, 0, NULL,
     0, 0, 0, 0, 0, 0},
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
  char title_buf[] = "Hello c-orm";
  char content_buf[] = "This is a great new object-mapper for C89.";

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
  post.title = title_buf;
  post.content = content_buf;
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
  c_orm_system_free(fetched.title);
  return c_orm_system_free(fetched.content);
}

/**
 * @brief Disconnects `db` during error cleanup.
 * @param db Database connection handle.
 * @return C_ORM_OK on success, or driver disconnect error code.
 */
static c_orm_error_t blog_conn_cleanup_free(c_orm_db_t *db) {
  return db->vtable->disconnect(db);
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
  if (rc != C_ORM_OK) {
    blog_conn_cleanup_free(db);
    return rc;
  }

  return db->vtable->disconnect(db);
}

int main(void);

/**
 * @brief Main entry point for blog example.
 *
 * @return 0 on success.
 */
int main(void) {
  c_orm_db_t *db = NULL;
  int failed = 0;

#if defined(_MSC_VER) && defined(_DEBUG)
  _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
  _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

  remove("blog_err_example.db");
  failed |= (run_blog_example("blog_err_example.db") != C_ORM_OK);
  /* Second run against existing blog_posts table exercises ops error path */
  failed |= (run_blog_example("blog_err_example.db") == C_ORM_OK);
  remove("blog_err_example.db");

  failed |= (run_blog_example(":memory:") != C_ORM_OK);
  failed |= (run_blog_example(NULL) == C_ORM_OK);
#ifdef _WIN32
  failed |= (run_blog_example("Z:\\invalid_dir\\bad.db") == C_ORM_OK);
#else
  failed |= (run_blog_example("/dev/null/invalid_dir/bad.db") == C_ORM_OK);
#endif
  failed |= (run_blog_ops(NULL, 1, 0) == C_ORM_OK);

  failed |= (c_orm_sqlite_connect(":memory:", &db) != C_ORM_OK);
  failed |=
      (c_orm_execute_raw(db, "CREATE TABLE blog_posts (id INT)") != C_ORM_OK);
  failed |= (run_blog_ops(db, 1, 0) == C_ORM_OK);
  failed |= (db->vtable->disconnect(db) != C_ORM_OK);

  db = NULL;
  failed |= (c_orm_sqlite_connect(":memory:", &db) != C_ORM_OK);
  failed |=
      (c_orm_execute_raw(db, "CREATE TABLE blog_posts (id INTEGER PRIMARY KEY, "
                             "title TEXT, content TEXT, author_id INTEGER)") !=
       C_ORM_OK);
  failed |=
      (c_orm_execute_raw(
           db, "INSERT INTO blog_posts VALUES(1, 'title', 'content', 1)") !=
       C_ORM_OK);
  failed |= (run_blog_ops(db, 1, 1) == C_ORM_OK);

  failed |= (c_orm_execute_raw(db, "DELETE FROM blog_posts") != C_ORM_OK);
  failed |= (run_blog_ops(db, 999, 1) == C_ORM_OK);
  failed |= (db->vtable->disconnect(db) != C_ORM_OK);

  return failed;
}
