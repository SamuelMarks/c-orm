/**
 * @file test_inline_macros.c
 * @brief Unit tests for C-ORM inline macros and model/view definitions.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_inline_macros.h"
#include "c_orm_struct.h"
#include "c_orm_sqlite.h"
#define GREATEST_USE_LONGJMP 0
#include <greatest.h>
#include "test_assert_zero_branch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

/** @brief Spatial test model structure. */
struct SpatialModel {
  /** @brief Primary key identifier. */
  int32_t id;
  /** @brief Point coordinate structure. */
  c_orm_point_t point;
  /** @brief Polygon spatial structure. */
  c_orm_polygon_t polygon;
  /** @brief Double precision float value. */
  double dval;
  /** @brief Single precision float value. */
  float fval;
  /** @brief Nullable double precision pointer value. */
  double *ndval;
  /** @brief Binary large object payload. */
  c_orm_blob_t data;
};

/** @brief Column metadata array for SpatialModel. */
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

/** @brief Table metadata for SpatialModel. */
static const c_orm_table_meta_t SpatialModel_meta = C_ORM_DEFINE_MODEL(
    "spatial_models", SpatialModel_cols, 7, sizeof(struct SpatialModel),
    "SELECT * FROM spatial_models", "SELECT * FROM spatial_models WHERE id = ?",
    "INSERT INTO spatial_models (id, point, polygon, dval, fval, ndval, data) "
    "VALUES (?, ?, ?, ?, ?, ?, ?)",
    "UPDATE spatial_models SET id = ?, point = ?, polygon = ?, dval = ?, fval "
    "= ?, ndval = ?, data = ? WHERE id = ?",
    "DELETE FROM spatial_models WHERE id = ?", NULL, false, 0, 0, NULL, 0);

/**
 * @brief Tests CRUD operations with spatial data types.
 * @return GREATEST test result.
 */
TEST test_spatial_crud(void) {
  c_orm_db_t *db = NULL;
  c_orm_error_t err;
  struct SpatialModel sm;
  struct SpatialModel fetched;
  c_orm_table_meta_t bad_meta;

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
  ASSERT_EQ(C_ORM_OK, c_orm_default_malloc(sizeof(double), (void **)&sm.ndval));
  *sm.ndval = 5.5;
  ASSERT_EQ(C_ORM_OK, c_orm_default_malloc(5, &sm.data.data));
  memcpy(sm.data.data, "abcd", 5);
  sm.data.size = 5;

  sm.polygon.num_points = 3;
  ASSERT_EQ(C_ORM_OK, c_orm_default_malloc(3 * sizeof(c_orm_point_t),
                                           (void **)&sm.polygon.points));
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

  ASSERT_EQ_FMT(42.5, fetched.point.x, "%f");
  ASSERT_EQ_FMT(-71.2, fetched.point.y, "%f");
  ASSERT_EQ_FMT(3.14159, fetched.dval, "%f");
  ASSERT(fetched.data.data != NULL);
  ASSERT_EQ_FMT((unsigned long)5, (unsigned long)fetched.data.size, "%lu");
  ASSERT_STR_EQ("abcd", (char *)fetched.data.data);

  ASSERT_EQ_FMT((unsigned long)3, (unsigned long)fetched.polygon.num_points,
                "%lu");
  ASSERT(fetched.polygon.points != NULL);
  ASSERT_EQ_FMT(1.0, fetched.polygon.points[1].x, "%f");

  ASSERT(fetched.fval > 1.233f);
  ASSERT(fetched.fval < 1.235f);
  ASSERT(fetched.ndval != NULL);
  ASSERT_EQ_FMT(5.5, *fetched.ndval, "%f");

  sm.point.x = 99.9;
  err = c_orm_update(db, &SpatialModel_meta, &sm);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  bad_meta = SpatialModel_meta;
  bad_meta.query_update = "UPDATE non_existent_table SET id = ?";
  err = c_orm_update(db, &bad_meta, &sm);
  ASSERT_EQ_FMT(C_ORM_ERROR_SQL, err, "%d");

  ASSERT_EQ(C_ORM_OK, c_orm_system_free(fetched.polygon.points));
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(fetched.data.data));
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(fetched.ndval));
  memset(&fetched, 0, sizeof(fetched));
  err = c_orm_find_by_id_int32(db, &SpatialModel_meta, 1, &fetched);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  ASSERT_EQ_FMT(99.9, fetched.point.x, "%f");

  ASSERT_EQ(C_ORM_OK, c_orm_system_free(sm.polygon.points));
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(fetched.polygon.points));
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(sm.data.data));
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(fetched.data.data));
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(sm.ndval));
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(fetched.ndval));

  ASSERT_EQ(C_ORM_OK, db->vtable->disconnect(db));
  PASS();
}

/** @brief Inline user test structure. */
struct InlineUser {
  /** @brief Primary key identifier. */
  int32_t id;
  /** @brief User name string. */
  char *username;
  /** @brief Inline parent user pointer. */
  C_ORM_INLINE_BELONGS_TO(struct InlineUser, manager);
};

#define DECL_TAG_FIELDS(X, S)                                                  \
  X(S, C_ORM_TYPE_INT32, int32_t, id)                                          \
  X(S, C_ORM_TYPE_STRING, char *, label)

/**
 * @brief Test struct exercising split C_ORM_STRUCT_DECL and C_ORM_STRUCT_IMPL.
 * @var id Primary key tag identifier.
 * @var label Tag display label.
 */
C_ORM_STRUCT_DECL(DeclTag, DECL_TAG_FIELDS)
C_ORM_STRUCT_IMPL(DeclTag, DECL_TAG_FIELDS)

/** @brief Column metadata array for InlineUser. */
static const c_orm_column_meta_t InlineUser_cols[] = {
    C_ORM_DEFINE_COLUMN("id", C_ORM_TYPE_INT32, offsetof(struct InlineUser, id),
                        true, false, NULL, false, false),
    C_ORM_DEFINE_COLUMN("username", C_ORM_TYPE_STRING,
                        offsetof(struct InlineUser, username), false, false,
                        NULL, false, false)};

/** @brief Relation metadata array for InlineUser. */
static const c_orm_relation_meta_t InlineUser_rels[] = {C_ORM_DEFINE_RELATION(
    "manager", C_ORM_RELATION_BELONGS_TO, "inline_users", "manager_id", "id",
    offsetof(struct InlineUser, manager), 0, &DeclTag_meta)};

/** @brief Table metadata for InlineUser model. */
static const c_orm_table_meta_t InlineUser_meta = C_ORM_DEFINE_MODEL(
    "inline_users", InlineUser_cols, 2, sizeof(struct InlineUser),
    "SELECT * FROM inline_users", "SELECT * FROM inline_users WHERE id = ?",
    "INSERT INTO inline_users (id, username) VALUES (?, ?)",
    "UPDATE inline_users SET id = ?, username = ? WHERE id = ?",
    "DELETE FROM inline_users WHERE id = ?", NULL, false, 0, 0, NULL, 0);

/** @brief View metadata for InlineUserView. */
static const c_orm_table_meta_t InlineUserView_meta = C_ORM_DEFINE_VIEW(
    "inline_users_view", InlineUser_cols, 2, sizeof(struct InlineUser),
    "SELECT * FROM inline_users_view");

/**
 * @brief Tests CRUD and view read-only semantics with inline macro definitions.
 * @return GREATEST test result.
 */
TEST test_inline_macros_crud(void) {
  c_orm_db_t *db = NULL;
  c_orm_error_t err;
  struct InlineUser u;
  struct InlineUser fetched;
  char s_inline[] = "inline_test";

  err = c_orm_sqlite_connect(":memory:", &db);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  err = c_orm_execute_raw(db, "CREATE TABLE inline_users (id INTEGER PRIMARY "
                              "KEY, username VARCHAR(255) NOT NULL);");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  err = c_orm_execute_raw(
      db, "CREATE VIEW inline_users_view AS SELECT * FROM inline_users;");
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  u.id = 1;
  u.username = s_inline;

  err = c_orm_insert(db, &InlineUserView_meta, &u);
  ASSERT_EQ_FMT(C_ORM_ERROR_READ_ONLY, err, "%d");

  err = c_orm_update(db, &InlineUserView_meta, &u);
  ASSERT_EQ_FMT(C_ORM_ERROR_READ_ONLY, err, "%d");

  err = c_orm_insert(db, &InlineUser_meta, &u);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");

  memset(&fetched, 0, sizeof(fetched));
  err = c_orm_find_by_id_int32(db, &InlineUser_meta, 1, &fetched);
  ASSERT_EQ_FMT(C_ORM_OK, err, "%d");
  ASSERT_STR_EQ("inline_test", fetched.username);
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(fetched.username));

  ASSERT_STR_EQ("manager", InlineUser_rels[0].field_name);
  ASSERT(InlineUser_rels[0].target_meta == &DeclTag_meta);
  ASSERT_STR_EQ("DeclTag", DeclTag_meta.name);
  ASSERT_EQ((size_t)2, DeclTag_meta.num_columns);

  ASSERT_EQ_FMT(C_ORM_OK, db->vtable->disconnect(db), "%d");
  PASS();
}

/**
 * @brief Inline macros test suite runner.
 * @param inline_macros_suite Suite runner function name.
 */
SUITE(inline_macros_suite) {
  RUN_TEST(test_inline_macros_crud);
  RUN_TEST(test_spatial_crud);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */
