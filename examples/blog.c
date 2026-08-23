#if defined(__clang__) || defined(__GNUC__)
#endif
/*
 * blog.c - c-orm Blog Example (Step 282)
 * Demonstrates using the specific struct mapping API.
 * Requires: cdd-c generated Models.h linked.
 */

/* clang-format off */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c_orm_api.h"
#include "c_orm_sqlite.h"
/* clang-format on */
/* Assuming Models.h is available via cdd-c code generation in actual project */

/* STUB STRUCTS to represent generated output */
typedef struct BlogPost {
  int32_t id;
  char *title;
  char *content;
  int32_t author_id;
} BlogPost;

static c_orm_column_meta_t blog_post_cols[] = {
    {"id", C_ORM_TYPE_INT32, 0, 1, 0, NULL, 0, 0},
    {"title", C_ORM_TYPE_STRING, 4, 0, 0, NULL, 0, 0},
    {"content", C_ORM_TYPE_STRING, 12, 0, 0, NULL, 0, 0},
    {"author_id", C_ORM_TYPE_INT32, 20, 0, 0, NULL, 0, 0},
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

int main(void) {
  int rc;

  c_orm_db_t *db = NULL;
  c_orm_error_t err;
  BlogPost post;
  BlogPost fetched;

  printf("Starting Blog Example...\n");

  err = c_orm_sqlite_connect(":memory:", &db);
  if (err != C_ORM_OK) {
    printf("Failed to connect to SQLite in memory.\n");
    {
      rc = 1;
      return rc;
    }
  }

  err =
      c_orm_execute_raw(db, "CREATE TABLE blog_posts (id INTEGER PRIMARY KEY, "
                            "title TEXT, content TEXT, author_id INTEGER)");
  if (err != C_ORM_OK) {
    printf("c_orm_execute_raw err %d\n", (int)err);
    return 1;
  }

  memset(&post, 0, sizeof(post));
  post.id = 1;
  post.title = "Hello c-orm";
  post.content = "This is a great new object-mapper for C89.";
  post.author_id = 99;

  err = c_orm_insert(db, &BlogPost_meta, &post);
  if (err == C_ORM_OK) {
    printf("Successfully inserted blog post.\n");
  } else {
    printf("Failed to insert.\n");
  }

  memset(&fetched, 0, sizeof(fetched));
  err = c_orm_find_by_id_int32(db, &BlogPost_meta, 1, &fetched);
  if (err == C_ORM_OK) {
    printf("Fetched Post: %s -> %s\n", fetched.title, fetched.content);
    /* We must free strings allocated by hydrate */
    if (fetched.title)
      free(fetched.title);
    if (fetched.content)
      free(fetched.content);
  }

  /* Disconnect not available directly in high level api. Rely on internal
   * teardowns in full apps */

  {
    rc = 0;
    return rc;
  }
}

#if defined(__clang__) || defined(__GNUC__)
#endif
