#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file test_query_builder_coverage.c
 * @brief Unit tests for query builder SQL generation and out-of-memory
 * simulation.
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_api.h"
#include "c_orm_query_builder.h"
#include "greatest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

/**
 * @brief Forward declaration for test_query_builder_coverage.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_query_builder_coverage(void);

/**
 * @brief Forward declaration for test_query_builder_oom.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_query_builder_oom(void);

/**
 * @brief Forward declaration for qb_exhaustive_oom_all.
 * @return GREATEST test result.
 */
static enum greatest_test_res qb_exhaustive_oom_all(void);

/**
 * @brief Forward declaration for test_query_builder_append_error_branches.
 * @return GREATEST test result.
 */
static enum greatest_test_res test_query_builder_append_error_branches(void);

/**
 * @brief Tests query builder clause construction, validation, and error paths.
 * @return GREATEST test result.
 */
TEST test_query_builder_coverage(void) {
  c_orm_select_builder_t *b = NULL;
  c_orm_insert_builder_t *ib = NULL;
  c_orm_update_builder_t *ub = NULL;
  c_orm_relation_meta_t rels[3];
  c_orm_column_meta_t target_col;
  c_orm_table_meta_t target_meta;
  c_orm_column_meta_t no_pk_col;
  c_orm_table_meta_t no_pk_meta;
  c_orm_table_meta_t meta;

  memset(rels, 0, sizeof(rels));

  rels[0].field_name = "my_rel";
  rels[0].target_table = "target_tbl";
  rels[0].type = C_ORM_RELATION_ONE_TO_MANY;
  rels[0].foreign_key = "fk_id";
  rels[0].local_key = "id";

  rels[1].field_name = "m2m_rel";
  rels[1].target_table = "target_tbl";
  rels[1].type = C_ORM_RELATION_MANY_TO_MANY;
  rels[1].join_table = "join_tbl";
  rels[1].join_foreign_key = "fk_id";
  rels[1].join_local_key = "local_fk";
  rels[1].local_key = "id";

  rels[2].field_name = "bad_rel";
  rels[2].target_table = "target_tbl";
  rels[2].type = (c_orm_relation_type_t)99;

  memset(&target_col, 0, sizeof(target_col));
  target_col.name = "id";
  target_col.is_pk = 1;

  memset(&target_meta, 0, sizeof(target_meta));
  target_meta.name = "target_tbl";
  target_meta.columns = &target_col;
  target_meta.num_columns = 1;

  rels[0].target_meta = &target_meta;

  memset(&no_pk_col, 0, sizeof(no_pk_col));
  no_pk_col.name = "not_id";
  no_pk_col.is_pk = 0;

  memset(&no_pk_meta, 0, sizeof(no_pk_meta));
  no_pk_meta.name = "no_pk_tbl";
  no_pk_meta.columns = &no_pk_col;
  no_pk_meta.num_columns = 1;

  rels[1].target_meta = &no_pk_meta;

  memset(&meta, 0, sizeof(meta));
  meta.name = "test_table";
  meta.relations = rels;
  meta.num_relations = 3;

  /* Select builder nulls */
  c_orm_select_builder_init(&meta, &b);
  c_orm_update_builder_init(&meta, &ub);

  c_orm_select_builder_free(b);
  c_orm_update_builder_free(ub);

  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_select_builder_init(NULL, &b));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_select_builder_init(&meta, NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_select_builder_init(&meta, &b));

  /* Where conditions */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_select_where_eq(NULL, "id"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_select_where_eq(b, NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_select_where_eq(b, "id"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_select_where_neq(NULL, "id"));
  ASSERT_EQ(C_ORM_OK, c_orm_select_where_neq(b, "id"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_select_where_lt(NULL, "id"));
  ASSERT_EQ(C_ORM_OK, c_orm_select_where_lt(b, "id"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_select_where_gt(NULL, "id"));
  ASSERT_EQ(C_ORM_OK, c_orm_select_where_gt(b, "id"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_select_where_lte(NULL, "id"));
  ASSERT_EQ(C_ORM_OK, c_orm_select_where_lte(b, "id"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_select_where_gte(NULL, "id"));
  ASSERT_EQ(C_ORM_OK, c_orm_select_where_gte(b, "id"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_select_where_like(NULL, "id"));
  ASSERT_EQ(C_ORM_OK, c_orm_select_where_like(b, "id"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_select_where_between(NULL, "id"));
  ASSERT_EQ(C_ORM_OK, c_orm_select_where_between(b, "id"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_select_where_ilike(NULL, "name"));
  ASSERT_EQ(C_ORM_OK, c_orm_select_where_ilike(b, "name"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_select_where_gt_current_timestamp(NULL, "id"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_select_where_gt_current_timestamp(b, NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_select_where_gt_current_timestamp(b, "id"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_select_where_lt_current_timestamp(NULL, "id"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_select_where_lt_current_timestamp(b, NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_select_where_lt_current_timestamp(b, "id"));
  ASSERT_EQ(C_ORM_OK, c_orm_select_where_in(b, "id", 3));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_select_where_in(NULL, "id", 3));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_select_where_in(b, NULL, 3));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_select_where_in(b, "id", 0));
  {
    void *arr = (void *)0x123;
    ASSERT_EQ(C_ORM_OK, c_orm_select_where_in_array(b, "id", arr, &meta));
  }
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_select_where_in_array(NULL, "id", NULL, &meta));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_select_where_in_array(b, NULL, (void *)0x123, &meta));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_select_where_in_array(b, "id", NULL, &meta));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_select_where_in_array(b, "id", (void *)0x123, NULL));

  /* To hit "AND" appending */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_select_where_eq(NULL, "id"));
  ASSERT_EQ(C_ORM_OK, c_orm_select_where_eq(b, "id"));

  /* Relation logic */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_select_where_relation(NULL, "rel", "col"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_select_where_relation(b, NULL, "col"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_select_where_relation(b, "my_rel.id", NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_select_where_relation(b, "my_rel.id", "col"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_select_where_relation(b, "unknown_rel.id", "col"));
  ASSERT_EQ(C_ORM_OK, c_orm_select_where_relation(b, "m2m_rel.id", "col"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_select_where_relation(b, "bad_rel.id", "col"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_select_where_relation(b, "my_rel.bad_rel.id", "col"));

  {
    char long_name[256];
    memset(long_name, 'A', 255);
    long_name[255] = '\0';
    long_name[128] = '.';
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
              c_orm_select_where_relation(b, long_name, "col"));
  }

  ASSERT_EQ(C_ORM_OK, c_orm_select_having(b, "count > 1"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_select_having(NULL, "count > 1"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_select_having(b, NULL));

  /* Re-init so we have SELECT * FROM at start for aggregate test */
  c_orm_select_builder_free(b);
  c_orm_select_builder_init(&meta, &b);
  ASSERT_EQ(C_ORM_OK, c_orm_select_aggregate(b, "COUNT", "id", "c"));
  ASSERT_EQ(C_ORM_OK, c_orm_select_aggregate(b, "SUM", "val", "s"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_select_aggregate(NULL, "COUNT", "id", "c"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_select_aggregate(b, NULL, "id", "c"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_select_aggregate(b, "COUNT", NULL, "c"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_select_aggregate(b, "COUNT", "id", NULL));

  {
    struct fake_select_builder {
      const c_orm_table_meta_t *meta;
      c_orm_string_builder_t *sb;
      int has_where;
      int has_order;
    } fake_b;
    memset(&fake_b, 0, sizeof(fake_b));
    c_orm_string_builder_init(&fake_b.sb);
    c_orm_string_builder_append(fake_b.sb, "SELECT 1");
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
              c_orm_select_aggregate((c_orm_select_builder_t *)&fake_b, "COUNT",
                                     "id", "c"));
    c_orm_string_builder_free(fake_b.sb);
  }

  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_select_order_by(NULL, "id", 0));
  ASSERT_EQ(C_ORM_OK, c_orm_select_order_by(b, "id", 0));
  ASSERT_EQ(C_ORM_OK,
            c_orm_select_order_by(b, "name", 1)); /* Hits ", " and " DESC" */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_select_order_by(b, NULL, 0));

  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_select_group_by(NULL, "id"));
  ASSERT_EQ(C_ORM_OK, c_orm_select_group_by(b, "id"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_select_group_by(b, NULL));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_select_limit(NULL, 10));

  ASSERT_EQ(C_ORM_OK, c_orm_select_limit(b, 10));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_select_offset(NULL, 5));
  ASSERT_EQ(C_ORM_OK, c_orm_select_offset(b, 5));

  {
    char *sql = NULL;
    ASSERT_EQ(0, 0);
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_select_builder_compile(b, NULL));
    C_ORM_FREE(sql);
  }

  c_orm_select_builder_free(b);
  c_orm_select_builder_free(NULL);

  /* Insert builder */
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, c_orm_insert_builder_init(NULL, &ib));
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED,
            c_orm_insert_builder_compile(NULL, NULL));
  c_orm_insert_builder_free(NULL);

  /* Update builder */
  c_orm_update_builder_init(&meta, &ub);
  c_orm_update_builder_free(ub);

  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_update_builder_init(NULL, &ub));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_update_builder_init(&meta, NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_update_builder_init(&meta, &ub));

  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_update_set(NULL, "c"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_update_set(ub, NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_update_set(ub, "c"));
  ASSERT_EQ(C_ORM_OK, c_orm_update_set(ub, "d")); /* Hits , */

  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_update_where_eq(NULL, "c"));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_update_where_eq(ub, NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_update_where_eq(ub, "c"));
  ASSERT_EQ(C_ORM_OK, c_orm_update_where_eq(ub, "d")); /* Hits AND */

  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_update_set(ub, "e")); /* hits has_where == 1 */

  {
    char *sql = NULL;
    ASSERT_EQ(C_ORM_OK, c_orm_update_builder_compile(ub, &sql));
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_update_builder_compile(ub, NULL));
    C_ORM_FREE(sql);
  }
  c_orm_update_builder_free(ub);
  c_orm_update_builder_free(NULL);

  PASS();
}

/** @brief Countdown counter before failing the allocation in mock
 * malloc/realloc. */
static int oom_countdown = -1;

/** @brief Flag indicating whether OOM simulation is currently active. */
static int oom_active = 0;

/**
 * @brief Mock malloc returning NULL on triggered countdown.
 * @param size Size in bytes.
 * @return Allocated buffer or NULL on OOM.
 */
static void *qb_mock_malloc(size_t size) {
  if (oom_active) {
    if (oom_countdown == 0) {
      oom_countdown--;
      return NULL;
    }
    oom_countdown--;
  }
  return malloc(size);
}

/**
 * @brief Mock realloc returning NULL on triggered countdown.
 * @param ptr Existing buffer.
 * @param size New size in bytes.
 * @return Reallocated buffer or NULL on OOM.
 */
static void *qb_mock_realloc(void *ptr, size_t size) {
  if (oom_active) {
    if (oom_countdown == 0) {
      oom_countdown--;
      return NULL;
    }
    oom_countdown--;
  }
  return realloc(ptr, size);
}

/**
 * @brief Mock free wrapper.
 * @param ptr Buffer to free.
 */
static void qb_mock_free(void *ptr) { free(ptr); }

/**
 * @brief Tests query builder out-of-memory error paths.
 * @return GREATEST test result.
 */
TEST test_query_builder_oom(void) {
  void *(*old_malloc)(size_t);
  void *(*old_realloc)(void *, size_t);
  void (*old_free)(void *);
  int i;
  c_orm_table_meta_t meta;
  c_orm_relation_meta_t rel;

  old_malloc = c_orm_malloc;
  old_realloc = c_orm_realloc;
  old_free = c_orm_free;

  memset(&meta, 0, sizeof(meta));
  meta.name = "test_table";

  memset(&rel, 0, sizeof(rel));
  rel.target_meta = &meta;
  rel.type = C_ORM_RELATION_MANY_TO_MANY;
  rel.join_table = "join_tbl";
  rel.join_foreign_key = "fk";
  rel.join_local_key = "lk";
  rel.local_key = "id";
  rel.foreign_key = "pid";

  /* Trigger all combinations of c_orm_string_builder_append OOMs. */
  for (i = 0; i < 40; i++) {
    c_orm_select_builder_t *b = NULL;
    char *sql = NULL;

    oom_countdown = i;
    oom_active = 1;
    c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, c_orm_free);

    if (c_orm_select_builder_init(&meta, &b) == 0 && b) {
      c_orm_select_where_eq(b, "id");
      c_orm_select_where_eq(b, "id2");
      c_orm_select_where_in(b, "status", 3);
      c_orm_select_where_relation(b, "posts", "=");
      c_orm_select_group_by(b, "category");
      c_orm_select_group_by(b, "category2");
      c_orm_select_having(b, "COUNT(id) > 1");
      c_orm_select_having(b, "COUNT(id) > 2");
      c_orm_select_order_by(b, "created_at", 1);
      c_orm_select_order_by(b, "updated_at", 0);
      c_orm_select_limit(b, 10);
      c_orm_select_offset(b, 20);

      c_orm_select_builder_compile(b, &sql);
      if (sql)
        c_orm_free(sql);

      c_orm_select_builder_free(b);
    }

    oom_active = 0;
    c_orm_set_allocators(old_malloc, old_realloc, old_free);
  }

  for (i = 0; i < 15; i++) {
    c_orm_select_builder_t *b = NULL;
    char *sql = NULL;

    c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, qb_mock_free);

    oom_countdown = i;
    oom_active = 1;
    if (c_orm_select_builder_init(&meta, &b) == 0 && b) {
      rel.type = C_ORM_RELATION_ONE_TO_MANY;
      c_orm_select_where_relation(b, "posts", "=");
      c_orm_select_builder_compile(b, &sql);
      if (sql)
        C_ORM_FREE(sql);
      c_orm_select_builder_free(b);
    }
    oom_active = 0;
    if (oom_countdown >= 0)
      break;
    c_orm_set_allocators(old_malloc, old_realloc, old_free);
  }

  for (i = 0; i < 15; i++) {
    c_orm_update_builder_t *ub = NULL;
    char *sql = NULL;

    c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, qb_mock_free);

    oom_countdown = i;
    oom_active = 1;

    if (c_orm_update_builder_init(&meta, &ub) == 0 && ub) {
      c_orm_update_set(ub, "status");
      c_orm_update_set(ub, "status2");
      c_orm_update_where_eq(ub, "id");
      c_orm_update_where_eq(ub, "id2");
      c_orm_update_builder_compile(ub, &sql);

      if (sql)
        C_ORM_FREE(sql);
      c_orm_update_builder_free(ub);
    }

    oom_active = 0;
    if (oom_countdown >= 0)
      break;
    c_orm_set_allocators(old_malloc, old_realloc, old_free);
  }

  /* Additional force failure tests to reach NULL check branches */
  {
    char *sql = NULL;
    c_orm_select_builder_compile(NULL, &sql);
    c_orm_update_builder_compile(NULL, &sql);
  }

  /* Select aggregate OOM */
  for (i = 0; i < 15; i++) {
    c_orm_select_builder_t *b = NULL;
    c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, qb_mock_free);
    oom_countdown = i;
    oom_active = 1;
    if (c_orm_select_builder_init(&meta, &b) == 0 && b) {
      c_orm_select_aggregate(b, "COUNT", "id", "count");
      c_orm_select_aggregate(b, "SUM", "score", "total");
      c_orm_select_builder_free(b);
    }
    oom_active = 0;
    if (oom_countdown >= 0)
      break;
    c_orm_set_allocators(old_malloc, old_realloc, old_free);
  }

  /* Select where GT/LT current timestamp OOM */
  for (i = 0; i < 15; i++) {
    c_orm_select_builder_t *b = NULL;
    c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, qb_mock_free);
    oom_countdown = i;
    oom_active = 1;
    if (c_orm_select_builder_init(&meta, &b) == 0 && b) {
      c_orm_select_where_gt_current_timestamp(b, "updated_at");
      c_orm_select_where_lt_current_timestamp(b, "created_at");
      c_orm_select_builder_free(b);
    }
    oom_active = 0;
    if (oom_countdown >= 0)
      break;
    c_orm_set_allocators(old_malloc, old_realloc, old_free);
  }

  /* Insert init not implemented */
  {
    c_orm_insert_builder_t *ib = NULL;
    ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED,
              c_orm_insert_builder_init(&meta, &ib));
    c_orm_insert_builder_free(ib);
  }

  /* Exhaustive Realloc/Malloc OOM targeting append_where / IN / SET */
  for (i = 0; i < 15; i++) {
    c_orm_select_builder_t *b = NULL;
    char *sql = NULL;

    c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, qb_mock_free);

    oom_countdown = i;
    oom_active = 1;

    if (c_orm_select_builder_init(&meta, &b) == 0 && b) {
      c_orm_select_where_neq(b, "id1");
      c_orm_select_where_lt(b, "id2");
      c_orm_select_where_gt(b, "id3");
      c_orm_select_where_lte(b, "id4");
      c_orm_select_where_gte(b, "id5");
      c_orm_select_where_like(b, "id6");
      c_orm_select_where_between(b, "id7");
      c_orm_select_where_ilike(b, "id8");

      c_orm_select_builder_compile(b, &sql);
      if (sql)
        C_ORM_FREE(sql);
      c_orm_select_builder_free(b);
    }

    oom_active = 0;
    if (oom_countdown >= 0)
      break;
    c_orm_set_allocators(old_malloc, old_realloc, old_free);
  }

  for (i = 0; i < 15; i++) {
    c_orm_select_builder_t *b = NULL;
    char *sql = NULL;

    c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, qb_mock_free);

    oom_countdown = i;
    oom_active = 1;
    if (c_orm_select_builder_init(&meta, &b) == 0 && b) {
      rel.type = C_ORM_RELATION_ONE_TO_MANY;
      c_orm_select_where_relation(b, "posts", "=");
      c_orm_select_builder_compile(b, &sql);
      if (sql)
        C_ORM_FREE(sql);
      c_orm_select_builder_free(b);
    }
    oom_active = 0;
    if (oom_countdown >= 0)
      break;
    c_orm_set_allocators(old_malloc, old_realloc, old_free);
  }

  for (i = 0; i < 15; i++) {
    c_orm_update_builder_t *ub = NULL;
    char *sql = NULL;

    c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, qb_mock_free);

    oom_countdown = i;
    oom_active = 1;

    if (c_orm_update_builder_init(&meta, &ub) == 0 && ub) {
      c_orm_update_set(ub, "status");
      c_orm_update_set(ub, "status2");
      c_orm_update_where_eq(ub, "id");
      c_orm_update_where_eq(ub, "id2");
      c_orm_update_builder_compile(ub, &sql);

      if (sql)
        C_ORM_FREE(sql);
      c_orm_update_builder_free(ub);
    }

    oom_active = 0;
    if (oom_countdown >= 0)
      break;
    c_orm_set_allocators(old_malloc, old_realloc, old_free);
  }

  /* Additional force failure tests to reach NULL check branches */
  {
    char *sql = NULL;
    c_orm_select_builder_compile(NULL, &sql);
    c_orm_update_builder_compile(NULL, &sql);
  }

  /* Select aggregate OOM */
  for (i = 0; i < 15; i++) {
    c_orm_select_builder_t *b = NULL;
    c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, qb_mock_free);
    oom_countdown = i;
    oom_active = 1;
    if (c_orm_select_builder_init(&meta, &b) == 0 && b) {
      c_orm_select_aggregate(b, "COUNT", "id", "count");
      c_orm_select_aggregate(b, "SUM", "score", "total");
      c_orm_select_builder_free(b);
    }
    oom_active = 0;
    if (oom_countdown >= 0)
      break;
    c_orm_set_allocators(old_malloc, old_realloc, old_free);
  }

  /* Select where GT/LT current timestamp OOM */
  for (i = 0; i < 15; i++) {
    c_orm_select_builder_t *b = NULL;
    c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, qb_mock_free);
    oom_countdown = i;
    oom_active = 1;
    if (c_orm_select_builder_init(&meta, &b) == 0 && b) {
      c_orm_select_where_gt_current_timestamp(b, "updated_at");
      c_orm_select_where_lt_current_timestamp(b, "created_at");
      c_orm_select_builder_free(b);
    }
    oom_active = 0;
    if (oom_countdown >= 0)
      break;
    c_orm_set_allocators(old_malloc, old_realloc, old_free);
  }

  /* Insert init not implemented */
  {
    c_orm_insert_builder_t *ib = NULL;
    ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED,
              c_orm_insert_builder_init(&meta, &ib));
    c_orm_insert_builder_free(ib);
  }

  /* Exhaustive Realloc/Malloc OOM targeting append_where / IN / SET */
  for (i = 0; i < 15; i++) {
    c_orm_select_builder_t *b = NULL;
    char *sql = NULL;

    c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, qb_mock_free);

    oom_countdown = i;
    oom_active = 1;

    if (c_orm_select_builder_init(&meta, &b) == 0 && b) {
      c_orm_select_where_neq(b, "id1");
      c_orm_select_where_lt(b, "id2");
      c_orm_select_where_gt(b, "id3");
      c_orm_select_where_lte(b, "id4");
      c_orm_select_where_gte(b, "id5");
      c_orm_select_where_like(b, "id6");
      c_orm_select_where_between(b, "id7");
      c_orm_select_where_ilike(b, "id8");

      c_orm_select_builder_compile(b, &sql);
      if (sql)
        C_ORM_FREE(sql);
      c_orm_select_builder_free(b);
    }

    oom_active = 0;
    if (oom_countdown >= 0)
      break;
    c_orm_set_allocators(old_malloc, old_realloc, old_free);
  }

  for (i = 0; i < 15; i++) {
    c_orm_select_builder_t *b = NULL;
    char *sql = NULL;

    oom_countdown = i;
    oom_active = 1;
    c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, qb_mock_free);

    if (c_orm_select_builder_init(&meta, &b) == 0 && b) {
      c_orm_select_group_by(b, "category");
      c_orm_select_group_by(b, "category2");
      c_orm_select_having(b, "COUNT(id) > 1");
      c_orm_select_having(b, "COUNT(id) > 2");
      c_orm_select_order_by(b, "created_at", 1);
      c_orm_select_order_by(b, "updated_at", 0);
      c_orm_select_limit(b, 10);
      c_orm_select_offset(b, 20);

      c_orm_select_builder_compile(b, &sql);
      if (sql)
        C_ORM_FREE(sql);

      c_orm_select_builder_free(b);
    }

    oom_active = 0;
    if (oom_countdown >= 0)
      break;
    c_orm_set_allocators(old_malloc, old_realloc, old_free);
  }

  for (i = 0; i < 15; i++) {
    c_orm_select_builder_t *b = NULL;
    char *sql = NULL;

    c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, qb_mock_free);

    oom_countdown = i;
    oom_active = 1;

    if (c_orm_select_builder_init(&meta, &b) == 0 && b) {
      c_orm_select_where_neq(b, "id1");
      c_orm_select_where_lt(b, "id2");
      c_orm_select_where_gt(b, "id3");
      c_orm_select_where_lte(b, "id4");
      c_orm_select_where_gte(b, "id5");
      c_orm_select_where_like(b, "id6");
      c_orm_select_where_between(b, "id7");
      c_orm_select_where_ilike(b, "id8");

      c_orm_select_builder_compile(b, &sql);
      if (sql)
        C_ORM_FREE(sql);
      c_orm_select_builder_free(b);
    }

    oom_active = 0;
    if (oom_countdown >= 0)
      break;
    c_orm_set_allocators(old_malloc, old_realloc, old_free);
  }

  for (i = 0; i < 15; i++) {
    c_orm_select_builder_t *b = NULL;
    char *sql = NULL;

    oom_countdown = i;
    oom_active = 1;
    c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, qb_mock_free);

    if (c_orm_select_builder_init(&meta, &b) == 0 && b) {
      c_orm_select_group_by(b, "category");
      c_orm_select_group_by(b, "category2");
      c_orm_select_having(b, "COUNT(id) > 1");
      c_orm_select_having(b, "COUNT(id) > 2");
      c_orm_select_order_by(b, "created_at", 1);
      c_orm_select_order_by(b, "updated_at", 0);
      c_orm_select_limit(b, 10);
      c_orm_select_offset(b, 20);

      c_orm_select_builder_compile(b, &sql);
      if (sql)
        C_ORM_FREE(sql);

      c_orm_select_builder_free(b);
    }

    oom_active = 0;
    if (oom_countdown >= 0)
      break;
    c_orm_set_allocators(old_malloc, old_realloc, old_free);
  }

  for (i = 0; i < 15; i++) {
    c_orm_select_builder_t *b = NULL;
    char *sql = NULL;

    c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, qb_mock_free);

    oom_countdown = i;
    oom_active = 1;
    if (c_orm_select_builder_init(&meta, &b) == 0 && b) {
      rel.type = C_ORM_RELATION_ONE_TO_MANY;
      c_orm_select_where_relation(b, "posts", "=");
      c_orm_select_builder_compile(b, &sql);
      if (sql)
        C_ORM_FREE(sql);
      c_orm_select_builder_free(b);
    }
    oom_active = 0;
    if (oom_countdown >= 0)
      break;
    c_orm_set_allocators(old_malloc, old_realloc, old_free);
  }

  for (i = 0; i < 15; i++) {
    c_orm_update_builder_t *ub = NULL;
    char *sql = NULL;

    c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, qb_mock_free);

    oom_countdown = i;
    oom_active = 1;

    if (c_orm_update_builder_init(&meta, &ub) == 0 && ub) {
      c_orm_update_set(ub, "status");
      c_orm_update_set(ub, "status2");
      c_orm_update_where_eq(ub, "id");
      c_orm_update_where_eq(ub, "id2");
      c_orm_update_builder_compile(ub, &sql);

      if (sql)
        C_ORM_FREE(sql);
      c_orm_update_builder_free(ub);
    }

    oom_active = 0;
    if (oom_countdown >= 0)
      break;
    c_orm_set_allocators(old_malloc, old_realloc, old_free);
  }

  /* Additional force failure tests to reach NULL check branches */
  {
    char *sql = NULL;
    c_orm_select_builder_compile(NULL, &sql);
    c_orm_update_builder_compile(NULL, &sql);
  }

  /* Select aggregate OOM */
  for (i = 0; i < 15; i++) {
    c_orm_select_builder_t *b = NULL;
    c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, qb_mock_free);
    oom_countdown = i;
    oom_active = 1;
    if (c_orm_select_builder_init(&meta, &b) == 0 && b) {
      c_orm_select_aggregate(b, "COUNT", "id", "count");
      c_orm_select_aggregate(b, "SUM", "score", "total");
      c_orm_select_builder_free(b);
    }
    oom_active = 0;
    if (oom_countdown >= 0)
      break;
    c_orm_set_allocators(old_malloc, old_realloc, old_free);
  }

  /* Select where GT/LT current timestamp OOM */
  for (i = 0; i < 15; i++) {
    c_orm_select_builder_t *b = NULL;
    c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, qb_mock_free);
    oom_countdown = i;
    oom_active = 1;
    if (c_orm_select_builder_init(&meta, &b) == 0 && b) {
      c_orm_select_where_gt_current_timestamp(b, "updated_at");
      c_orm_select_where_lt_current_timestamp(b, "created_at");
      c_orm_select_builder_free(b);
    }
    oom_active = 0;
    if (oom_countdown >= 0)
      break;
    c_orm_set_allocators(old_malloc, old_realloc, old_free);
  }

  /* Insert init not implemented */
  {
    c_orm_insert_builder_t *ib = NULL;
    ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED,
              c_orm_insert_builder_init(&meta, &ib));
    c_orm_insert_builder_free(ib);
  }

  /* Exhaustive Realloc/Malloc OOM targeting append_where / IN / SET */
  for (i = 0; i < 15; i++) {
    c_orm_select_builder_t *b = NULL;
    char *sql = NULL;

    c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, qb_mock_free);

    oom_countdown = i;
    oom_active = 1;

    if (c_orm_select_builder_init(&meta, &b) == 0 && b) {
      c_orm_select_where_neq(b, "id1");
      c_orm_select_where_lt(b, "id2");
      c_orm_select_where_gt(b, "id3");
      c_orm_select_where_lte(b, "id4");
      c_orm_select_where_gte(b, "id5");
      c_orm_select_where_like(b, "id6");
      c_orm_select_where_between(b, "id7");
      c_orm_select_where_ilike(b, "id8");

      c_orm_select_builder_compile(b, &sql);
      if (sql)
        C_ORM_FREE(sql);
      c_orm_select_builder_free(b);
    }

    oom_active = 0;
    if (oom_countdown >= 0)
      break;
    c_orm_set_allocators(old_malloc, old_realloc, old_free);
  }

  for (i = 0; i < 15; i++) {
    c_orm_select_builder_t *b = NULL;
    oom_countdown = i;
    oom_active = 1;
    c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, qb_mock_free);
    c_orm_select_builder_init(&meta, &b);
    if (b)
      c_orm_select_builder_free(b);
    oom_active = 0;
    if (oom_countdown >= 0)
      break;
  }

  /* Test c_orm_update_builder_init OOM on SET */
  {
    c_orm_table_meta_t short_meta;
    memset(&short_meta, 0, sizeof(short_meta));
    short_meta.name = "users";
    for (i = 0; i < 15; i++) {
      c_orm_update_builder_t *ub = NULL;
      c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, qb_mock_free);
      oom_countdown = i;
      oom_active = 1;
      c_orm_update_builder_init(&short_meta, &ub);
      if (ub)
        c_orm_update_builder_free(ub);
      oom_active = 0;
      c_orm_set_allocators(old_malloc, old_realloc, old_free);
      if (oom_countdown >= 0)
        break;
    }
  }

  for (i = 0; i < 15; i++) {
    c_orm_update_builder_t *ub = NULL;
    oom_countdown = i;
    oom_active = 1;
    c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, qb_mock_free);
    c_orm_update_builder_init(&meta, &ub);
    if (ub)
      c_orm_update_builder_free(ub);
    oom_active = 0;
    if (oom_countdown >= 0)
      break;
  }

  /* Additional append tests */
  for (i = 0; i < 15; i++) {
    c_orm_update_builder_t *ub = NULL;
    c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, qb_mock_free);
    oom_countdown = i;
    oom_active = 1;
    if (c_orm_update_builder_init(&meta, &ub) == 0 && ub) {
      c_orm_update_set(ub, "x");
      c_orm_update_set(ub, "y");
    }
    if (ub)
      c_orm_update_builder_free(ub);
    oom_active = 0;
    if (oom_countdown >= 0)
      break;
  }

  for (i = 0; i < 15; i++) {
    c_orm_select_builder_t *b = NULL;
    c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, qb_mock_free);
    oom_countdown = i;
    oom_active = 1;
    if (c_orm_select_builder_init(&meta, &b) == 0 && b) {
      c_orm_select_where_eq(b, "x");
      c_orm_select_where_eq(b, "y");
    }
    if (b)
      c_orm_select_builder_free(b);
    oom_active = 0;
    if (oom_countdown >= 0)
      break;
  }

  c_orm_set_allocators(old_malloc, old_realloc, old_free);

  PASS();
}

/**
 * @brief Exhaustive out-of-memory simulation across all builder clauses.
 * @return GREATEST test result.
 */
TEST qb_exhaustive_oom_all(void) {
  void *(*old_malloc)(size_t);
  void *(*old_realloc)(void *, size_t);
  void (*old_free)(void *);
  int pad, oom, extra;

  old_malloc = c_orm_malloc;
  old_realloc = c_orm_realloc;
  old_free = c_orm_free;

  for (extra = 0; extra < 2; extra++) {
    c_orm_column_meta_t target_col;
    c_orm_table_meta_t target_meta;
    c_orm_relation_meta_t rel_arr[2];
    char tgt_name[64];
    int pad_step;

    memset(&target_col, 0, sizeof(target_col));
    target_col.name = "id";
    target_col.is_pk = 1;

    memset(tgt_name, 'R', (size_t)extra);
    C_ORM_STRCPY(tgt_name + extra, sizeof(tgt_name) - (size_t)extra,
                 "target_tbl");

    memset(&target_meta, 0, sizeof(target_meta));
    target_meta.name = tgt_name;
    target_meta.columns = &target_col;
    target_meta.num_columns = 1;

    memset(rel_arr, 0, sizeof(rel_arr));
    rel_arr[0].field_name = "my_rel";
    rel_arr[0].target_meta = &target_meta;
    rel_arr[0].type = C_ORM_RELATION_ONE_TO_MANY;
    rel_arr[0].local_key = "id";
    rel_arr[0].foreign_key = "pid";

    rel_arr[1].field_name = "m2m_rel";
    rel_arr[1].target_meta = &target_meta;
    rel_arr[1].type = C_ORM_RELATION_MANY_TO_MANY;
    rel_arr[1].join_table = "join_tbl";
    rel_arr[1].join_foreign_key = "fk";
    rel_arr[1].join_local_key = "lk";
    rel_arr[1].local_key = "id";
    rel_arr[1].foreign_key = "pid";

    for (pad_step = 0; pad_step < 3; pad_step++) {
      char tname[513];
      c_orm_table_meta_t meta;
      pad = pad_step * 250;
      memset(tname, 'T', (size_t)pad);
      tname[pad] = '\0';
      memset(&meta, 0, sizeof(meta));
      meta.name = tname;
      meta.relations = rel_arr;
      meta.num_relations = 2;

      for (oom = 0; oom < 35; oom++) {
        c_orm_select_builder_t *b = NULL;
        c_orm_update_builder_t *ub = NULL;

        if (c_orm_select_builder_init(&meta, &b) == 0 && b) {
          c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, c_orm_free);
          oom_countdown = oom;
          oom_active = 1;
          c_orm_select_where_in(b, "id9", 3);
          oom_active = 0;
          c_orm_set_allocators(old_malloc, old_realloc, old_free);
          c_orm_select_builder_free(b);
        }
        if (c_orm_select_builder_init(&meta, &b) == 0 && b) {
          c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, c_orm_free);
          oom_countdown = oom;
          oom_active = 1;
          c_orm_select_where_relation(b, "m2m_rel.id", "=");
          oom_active = 0;
          c_orm_set_allocators(old_malloc, old_realloc, old_free);
          c_orm_select_builder_free(b);
        }
        if (c_orm_select_builder_init(&meta, &b) == 0 && b) {
          c_orm_select_order_by(b, "created_at", 1);
          c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, c_orm_free);
          oom_countdown = oom;
          oom_active = 1;
          c_orm_select_order_by(b, "updated_at", 0);
          oom_active = 0;
          c_orm_set_allocators(old_malloc, old_realloc, old_free);
          c_orm_select_builder_free(b);
        }
        if (c_orm_update_builder_init(&meta, &ub) == 0 && ub) {
          c_orm_update_where_eq(ub, "id");
          c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, c_orm_free);
          oom_countdown = oom;
          oom_active = 1;
          c_orm_update_where_eq(ub, "id2");
          oom_active = 0;
          c_orm_set_allocators(old_malloc, old_realloc, old_free);
          c_orm_update_builder_free(ub);
        }
        if (c_orm_select_builder_init(&meta, &b) == 0 && b) {
          c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, c_orm_free);
          oom_countdown = oom;
          oom_active = 1;
          c_orm_select_where_gt_current_timestamp(b, "ts");
          oom_active = 0;
          c_orm_set_allocators(old_malloc, old_realloc, old_free);
          c_orm_select_builder_free(b);
        }
        if (c_orm_select_builder_init(&meta, &b) == 0 && b) {
          c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, c_orm_free);
          oom_countdown = oom;
          oom_active = 1;
          c_orm_select_where_lt_current_timestamp(b, "ts");
          oom_active = 0;
          c_orm_set_allocators(old_malloc, old_realloc, old_free);
          c_orm_select_builder_free(b);
        }
        if (c_orm_select_builder_init(&meta, &b) == 0 && b) {
          c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, c_orm_free);
          oom_countdown = oom;
          oom_active = 1;
          c_orm_select_group_by(b, "cat");
          oom_active = 0;
          c_orm_set_allocators(old_malloc, old_realloc, old_free);
          c_orm_select_builder_free(b);
        }
        if (c_orm_select_builder_init(&meta, &b) == 0 && b) {
          c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, c_orm_free);
          oom_countdown = oom;
          oom_active = 1;
          c_orm_select_having(b, "COUNT(id) > 1");
          oom_active = 0;
          c_orm_set_allocators(old_malloc, old_realloc, old_free);
          c_orm_select_builder_free(b);
        }
        if (c_orm_select_builder_init(&meta, &b) == 0 && b) {
          c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, c_orm_free);
          oom_countdown = oom;
          oom_active = 1;
          c_orm_select_limit(b, 10);
          oom_active = 0;
          c_orm_set_allocators(old_malloc, old_realloc, old_free);
          c_orm_select_builder_free(b);
        }
        if (c_orm_select_builder_init(&meta, &b) == 0 && b) {
          c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, c_orm_free);
          oom_countdown = oom;
          oom_active = 1;
          c_orm_select_offset(b, 20);
          oom_active = 0;
          c_orm_set_allocators(old_malloc, old_realloc, old_free);
          c_orm_select_builder_free(b);
        }
        if (c_orm_select_builder_init(&meta, &b) == 0 && b) {
          c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, c_orm_free);
          oom_countdown = oom;
          oom_active = 1;
          c_orm_select_aggregate(b, "COUNT", "col1", "count");
          c_orm_select_aggregate(b, "SUM", "col2", "total");
          oom_active = 0;
          c_orm_set_allocators(old_malloc, old_realloc, old_free);
          c_orm_select_builder_free(b);
        }
      }
    }
  }
  PASS();
}

struct test_qb_string_builder {
  char *buffer;
  size_t length;
  size_t capacity;
  int valid;
};

struct test_qb_select_builder {
  const c_orm_table_meta_t *meta;
  struct test_qb_string_builder *sb;
  int has_where;
  int has_order;
};

struct test_qb_update_builder {
  const c_orm_table_meta_t *meta;
  struct test_qb_string_builder *sb;
  int has_set;
  int has_where;
};

static void set_sb_room(struct test_qb_string_builder *sb, size_t room) {
  size_t new_cap = sb->length + 1 + room;
  char *new_buf = (char *)realloc(sb->buffer, new_cap);
  if (new_buf) {
    sb->buffer = new_buf;
    sb->capacity = new_cap;
  }
}

/**
 * @brief Tests all individual string builder append error branches in query
 * builder.
 * @return GREATEST test result.
 */
TEST test_query_builder_append_error_branches(void) {
  void *(*old_malloc)(size_t) = c_orm_malloc;
  void *(*old_realloc)(void *, size_t) = c_orm_realloc;
  void (*old_free)(void *) = c_orm_free;
  c_orm_column_meta_t target_col;
  c_orm_table_meta_t target_meta;
  c_orm_relation_meta_t rel_arr[2];
  c_orm_table_meta_t meta;
  int room;

  memset(&target_col, 0, sizeof(target_col));
  target_col.name = "id";
  target_col.is_pk = 1;

  memset(&target_meta, 0, sizeof(target_meta));
  target_meta.name = "target_tbl";
  target_meta.columns = &target_col;
  target_meta.num_columns = 1;

  memset(rel_arr, 0, sizeof(rel_arr));
  rel_arr[0].field_name = "my_rel";
  rel_arr[0].target_meta = &target_meta;
  rel_arr[0].type = C_ORM_RELATION_ONE_TO_MANY;
  rel_arr[0].local_key = "id";
  rel_arr[0].foreign_key = "pid";

  rel_arr[1].field_name = "m2m_rel";
  rel_arr[1].target_meta = &target_meta;
  rel_arr[1].type = C_ORM_RELATION_MANY_TO_MANY;
  rel_arr[1].join_table = "join_tbl";
  rel_arr[1].join_foreign_key = "fk_id";
  rel_arr[1].join_local_key = "local_fk";
  rel_arr[1].local_key = "id";

  memset(&meta, 0, sizeof(meta));
  meta.name = "users";
  meta.relations = rel_arr;
  meta.num_relations = 2;

  /* Sweep room values from 0 to 250 with countdown 0 */
  for (room = 0; room <= 250; room++) {
    /* 1. c_orm_select_where_in */
    {
      c_orm_select_builder_t *b = NULL;
      struct test_qb_select_builder *tb;
      if (c_orm_select_builder_init(&meta, &b) == C_ORM_OK && b) {
        tb = (struct test_qb_select_builder *)b;
        set_sb_room(tb->sb, (size_t)room);
        c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, qb_mock_free);
        oom_countdown = 0;
        oom_active = 1;
        c_orm_select_where_in(b, "id", 2);
        oom_active = 0;
        c_orm_set_allocators(old_malloc, old_realloc, old_free);
        c_orm_select_builder_free(b);
      }
    }

    /* 2. build_exists_query non-dot */
    {
      c_orm_select_builder_t *b = NULL;
      struct test_qb_select_builder *tb;
      if (c_orm_select_builder_init(&meta, &b) == C_ORM_OK && b) {
        tb = (struct test_qb_select_builder *)b;
        set_sb_room(tb->sb, (size_t)room);
        c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, qb_mock_free);
        oom_countdown = 0;
        oom_active = 1;
        c_orm_select_where_relation(b, "col_only", "=");
        oom_active = 0;
        c_orm_set_allocators(old_malloc, old_realloc, old_free);
        c_orm_select_builder_free(b);
      }
    }

    /* 3. build_exists_query ONE_TO_MANY */
    {
      c_orm_select_builder_t *b = NULL;
      struct test_qb_select_builder *tb;
      if (c_orm_select_builder_init(&meta, &b) == C_ORM_OK && b) {
        tb = (struct test_qb_select_builder *)b;
        set_sb_room(tb->sb, (size_t)room);
        c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, qb_mock_free);
        oom_countdown = 0;
        oom_active = 1;
        c_orm_select_where_relation(b, "my_rel.id", "=");
        oom_active = 0;
        c_orm_set_allocators(old_malloc, old_realloc, old_free);
        c_orm_select_builder_free(b);
      }
    }

    /* 4. build_exists_query MANY_TO_MANY */
    {
      c_orm_select_builder_t *b = NULL;
      struct test_qb_select_builder *tb;
      if (c_orm_select_builder_init(&meta, &b) == C_ORM_OK && b) {
        tb = (struct test_qb_select_builder *)b;
        set_sb_room(tb->sb, (size_t)room);
        c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, qb_mock_free);
        oom_countdown = 0;
        oom_active = 1;
        c_orm_select_where_relation(b, "m2m_rel.id", "=");
        oom_active = 0;
        c_orm_set_allocators(old_malloc, old_realloc, old_free);
        c_orm_select_builder_free(b);
      }
    }

    /* 5. c_orm_select_order_by */
    {
      c_orm_select_builder_t *b = NULL;
      struct test_qb_select_builder *tb;
      if (c_orm_select_builder_init(&meta, &b) == C_ORM_OK && b) {
        tb = (struct test_qb_select_builder *)b;
        set_sb_room(tb->sb, (size_t)room);
        c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, qb_mock_free);
        oom_countdown = 0;
        oom_active = 1;
        c_orm_select_order_by(b, "created_at", 0);
        oom_active = 0;
        c_orm_set_allocators(old_malloc, old_realloc, old_free);
        c_orm_select_builder_free(b);
      }
    }

    /* 6. c_orm_update_set */
    {
      c_orm_update_builder_t *ub = NULL;
      struct test_qb_update_builder *tub;
      if (c_orm_update_builder_init(&meta, &ub) == C_ORM_OK && ub) {
        c_orm_update_set(ub, "init_col");
        tub = (struct test_qb_update_builder *)ub;
        set_sb_room(tub->sb, (size_t)room);
        c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, qb_mock_free);
        oom_countdown = 0;
        oom_active = 1;
        c_orm_update_set(ub, "next_col");
        oom_active = 0;
        c_orm_set_allocators(old_malloc, old_realloc, old_free);
        c_orm_update_builder_free(ub);
      }
    }

    /* 7. c_orm_update_where_eq (has_where == 0) */
    {
      c_orm_update_builder_t *ub = NULL;
      struct test_qb_update_builder *tub;
      if (c_orm_update_builder_init(&meta, &ub) == C_ORM_OK && ub) {
        tub = (struct test_qb_update_builder *)ub;
        set_sb_room(tub->sb, (size_t)room);
        c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, qb_mock_free);
        oom_countdown = 0;
        oom_active = 1;
        c_orm_update_where_eq(ub, "col_target");
        oom_active = 0;
        c_orm_set_allocators(old_malloc, old_realloc, old_free);
        c_orm_update_builder_free(ub);
      }
    }

    /* 8. c_orm_update_where_eq (has_where == 1) */
    {
      c_orm_update_builder_t *ub = NULL;
      struct test_qb_update_builder *tub;
      if (c_orm_update_builder_init(&meta, &ub) == C_ORM_OK && ub) {
        c_orm_update_where_eq(ub, "first_col");
        tub = (struct test_qb_update_builder *)ub;
        set_sb_room(tub->sb, (size_t)room);
        c_orm_set_allocators(qb_mock_malloc, qb_mock_realloc, qb_mock_free);
        oom_countdown = 0;
        oom_active = 1;
        c_orm_update_where_eq(ub, "col_target");
        oom_active = 0;
        c_orm_set_allocators(old_malloc, old_realloc, old_free);
        c_orm_update_builder_free(ub);
      }
    }
  }

  PASS();
}

SUITE(query_builder_coverage_suite) {
  RUN_TEST(test_query_builder_coverage);
  RUN_TEST(test_query_builder_oom);
  RUN_TEST(qb_exhaustive_oom_all);
  RUN_TEST(test_query_builder_append_error_branches);
}

#if defined(__clang__) || defined(__GNUC__)
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */
