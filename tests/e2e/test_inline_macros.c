#if defined(__clang__) || defined(__GNUC__)
#endif

/* clang-format off */
#include "test_utils.h"
#include "c_orm_api.h"
#include "c_orm_inline_macros.h"
#include "c_orm_sqlite.h"
#include "greatest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

struct SpatialModel {
  int32_t id;
  c_orm_point_t point;
  c_orm_polygon_t polygon;
  double dval;
  float fval;
  double *ndval;
  c_orm_blob_t data;
};

static const c_orm_column_meta_t SpatialModel_cols[] = {
    C_ORM_DEFINE_COLUMN("id", C_ORM_TYPE_INT32,
                        offsetof(struct SpatialModel, id), true, false, NULL,
                        false, false),
    C_ORM_DEFINE_COLUMN("point", C_ORM_TYPE_POINT,
                        offsetof(struct SpatialModel, point), false, false,
                        NULL, false, false),
    C_ORM_DEFINE_COLUMN("polygon", C_ORM_TYPE_POLYGON,
                        offsetof(struct SpatialModel, polygon), false, false,
                        NULL, false, false),
    C_ORM_DEFINE_COLUMN("dval", C_ORM_TYPE_DOUBLE,
                        offsetof(struct SpatialModel, dval), false, false, NULL,
                        false, false),
    C_ORM_DEFINE_COLUMN("fval", C_ORM_TYPE_FLOAT,
                        offsetof(struct SpatialModel, fval), false, false, NULL,
                        false, false),
    C_ORM_DEFINE_COLUMN("ndval", C_ORM_TYPE_DOUBLE,
                        offsetof(struct SpatialModel, ndval), false, true, NULL,
                        false, false),
    C_ORM_DEFINE_COLUMN("data", C_ORM_TYPE_BLOB,
                        offsetof(struct SpatialModel, data), false, false, NULL,
                        false, false)};

static const c_orm_table_meta_t SpatialModel_meta = C_ORM_DEFINE_MODEL(
    "spatial_models", SpatialModel_cols, 7, sizeof(struct SpatialModel),
    "SELECT * FROM spatial_models", "SELECT * FROM spatial_models WHERE id = ?",
    "INSERT INTO spatial_models (id, point, polygon, dval, fval, ndval, data) "
    "VALUES (?, ?, ?, ?, ?, ?, ?)",
    "UPDATE spatial_models SET id = ?, point = ?, polygon = ?, dval = ?, fval "
    "= ?, ndval = ?, data = ? WHERE id = ?",
    "DELETE FROM spatial_models WHERE id = ?", NULL, false, 0, 0, NULL, 0);

TEST test_spatial_crud(void) {
  /* Tests Steps 173, 174, 175: Spatial type CRUD mapping */
  c_orm_db_t *db = NULL;
  c_orm_error_t err;
  struct SpatialModel sm;
  struct SpatialModel fetched;

  err = c_orm_sqlite_connect(":memory:", &db);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  err = c_orm_execute_raw(db, "CREATE TABLE spatial_models (id INTEGER PRIMARY "
                              "KEY, point BLOB, polygon BLOB, dval REAL, fval "
                              "REAL, ndval REAL, data BLOB);");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  memset(&sm, 0, sizeof(sm));
  sm.id = 1;
  sm.point.x = 42.5;
  sm.point.y = -71.2;
  sm.dval = 3.14159;
  sm.fval = 1.234f;
  sm.ndval = malloc(sizeof(double));
  *sm.ndval = 5.5;
  sm.data.data = malloc(5);
  memcpy(sm.data.data, "abcd", 5);
  sm.data.size = 5;

  sm.polygon.num_points = 3;
  sm.polygon.points = (c_orm_point_t *)malloc(3 * sizeof(c_orm_point_t));
  sm.polygon.points[0].x = 0.0;
  sm.polygon.points[0].y = 0.0;
  sm.polygon.points[1].x = 1.0;
  sm.polygon.points[1].y = 0.0;
  sm.polygon.points[2].x = 0.0;
  sm.polygon.points[2].y = 1.0;

  err = c_orm_insert(db, &SpatialModel_meta, &sm);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  memset(&fetched, 0, sizeof(fetched));
  err = c_orm_find_by_id_int32(db, &SpatialModel_meta, 1, &fetched);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  /* Assert Point */
  /* due to precision, strict EQ on doubles initialized statically is safe in C
   */
  ASSERT_EQ_FMT(42.5, fetched.point.x, "%f");
  ASSERT_EQ_FMT(-71.2, fetched.point.y, "%f");
  ASSERT_EQ_FMT(3.14159, fetched.dval, "%f");
  ASSERT(fetched.data.data != NULL);
  ASSERT_EQ_FMT((unsigned long)5, (unsigned long)fetched.data.size, "%lu");
  ASSERT_STR_EQ("abcd", fetched.data.data);

  /* Assert Polygon */
  ASSERT_EQ_FMT((unsigned long)3, (unsigned long)fetched.polygon.num_points,
                "%lu");
  ASSERT(fetched.polygon.points != NULL);
  ASSERT_EQ_FMT(1.0, fetched.polygon.points[1].x, "%f");

  /* Assert floats */
  /* Cast or assert correctly for float */
  ASSERT(fetched.fval > 1.233f && fetched.fval < 1.235f);
  ASSERT(fetched.ndval != NULL);
  ASSERT_EQ_FMT(5.5, *fetched.ndval, "%f");

  /* Test Update */
  sm.point.x = 99.9;
  err = c_orm_update(db, &SpatialModel_meta, &sm);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  /* Trigger error on update */
  {
    c_orm_table_meta_t bad_meta = SpatialModel_meta;
    bad_meta.query_update = "UPDATE non_existent_table SET id = ?";
    err = c_orm_update(db, &bad_meta, &sm);
    printf("DEBUG: bad_meta update err=%d\n", err);
    ASSERT_EQ_FMT(C_ORM_ERROR_SQL, err, "%d");
  }

  C_ORM_FREE(fetched.polygon.points);
  C_ORM_FREE(fetched.data.data);
  C_ORM_FREE(fetched.ndval);
  memset(&fetched, 0, sizeof(fetched));
  err = c_orm_find_by_id_int32(db, &SpatialModel_meta, 1, &fetched);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  ASSERT_EQ_FMT(99.9, fetched.point.x, "%f");

  /* Free */
  C_ORM_FREE(sm.polygon.points);
  C_ORM_FREE(fetched.polygon.points);
  C_ORM_FREE(sm.data.data);
  C_ORM_FREE(fetched.data.data);
  C_ORM_FREE(sm.ndval);
  C_ORM_FREE(fetched.ndval);

  db->vtable->disconnect(db);
  PASS();
}

struct InlineUser {
  int32_t id;
  char *username;
};

static const c_orm_column_meta_t InlineUser_cols[] = {
    C_ORM_DEFINE_COLUMN("id", C_ORM_TYPE_INT32, offsetof(struct InlineUser, id),
                        true, false, NULL, false, false),
    C_ORM_DEFINE_COLUMN("username", C_ORM_TYPE_STRING,
                        offsetof(struct InlineUser, username), false, false,
                        NULL, false, false)};

static const c_orm_table_meta_t InlineUser_meta = C_ORM_DEFINE_MODEL(
    "inline_users", InlineUser_cols, 2, sizeof(struct InlineUser),
    "SELECT * FROM inline_users", "SELECT * FROM inline_users WHERE id = ?",
    "INSERT INTO inline_users (id, username) VALUES (?, ?)",
    "UPDATE inline_users SET id = ?, username = ? WHERE id = ?",
    "DELETE FROM inline_users WHERE id = ?", NULL, false, 0, 0, NULL, 0);

static const c_orm_table_meta_t InlineUserView_meta = C_ORM_DEFINE_VIEW(
    "inline_users_view", InlineUser_cols, 2, sizeof(struct InlineUser),
    "SELECT * FROM inline_users_view");

TEST test_inline_macros_crud(void) {
  c_orm_db_t *db = NULL;
  c_orm_error_t err;
  struct InlineUser u;
  struct InlineUser fetched;

  err = c_orm_sqlite_connect(":memory:", &db);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  err = c_orm_execute_raw(db, "CREATE TABLE inline_users (id INTEGER PRIMARY "
                              "KEY, username VARCHAR(255) NOT NULL);");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  /* Tests Steps 160, 161 */
  err = c_orm_execute_raw(
      db, "CREATE VIEW inline_users_view AS SELECT * FROM inline_users;");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  u.id = 1;
  u.username = test_strdup("inline_test");

  printf("DEBUG: is_view=%d, query_insert=%p\n",
         (int)InlineUserView_meta.is_view,
         (void *)(void *)InlineUserView_meta.query_insert);
  fflush(stdout);

  err = c_orm_insert(db, &InlineUserView_meta, &u);
  printf("DEBUG: c_orm_insert err=%d\n", err);
  fflush(stdout);
  ASSERT_EQ_FMT(C_ORM_ERROR_READ_ONLY, err, "%d");

  err = c_orm_update(db, &InlineUserView_meta, &u);
  ASSERT_EQ_FMT(C_ORM_ERROR_READ_ONLY, err, "%d");

  err = c_orm_insert(db, &InlineUser_meta, &u);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  memset(&fetched, 0, sizeof(fetched));
  err = c_orm_find_by_id_int32(db, &InlineUser_meta, 1, &fetched);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  ASSERT_STR_EQ("inline_test", fetched.username);
  if (fetched.username)
    C_ORM_FREE(fetched.username);

  db->vtable->disconnect(db);
  PASS();
}

SUITE(inline_macros_suite) {
  RUN_TEST(test_inline_macros_crud);
  RUN_TEST(test_spatial_crud);
}

#if defined(__clang__) || defined(__GNUC__)
#endif
