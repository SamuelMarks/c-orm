/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_query_builder.h"
#include "greatest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

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

  rels[1].field_name = "m2m_rel";
  rels[1].target_table = "target_tbl";
  rels[1].type = C_ORM_RELATION_MANY_TO_MANY;
  rels[1].join_table = "join_tbl";

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

  ASSERT_EQ(1, c_orm_select_builder_init(NULL, &b));
  ASSERT_EQ(1, c_orm_select_builder_init(&meta, NULL));
  ASSERT_EQ(0, c_orm_select_builder_init(&meta, &b));

  /* Where conditions */
  ASSERT_EQ(1, c_orm_select_where_eq(NULL, "id"));
  ASSERT_EQ(0, c_orm_select_where_eq(b, "id"));
  ASSERT_EQ(1, c_orm_select_where_neq(NULL, "id"));
  ASSERT_EQ(0, c_orm_select_where_neq(b, "id"));
  ASSERT_EQ(1, c_orm_select_where_lt(NULL, "id"));
  ASSERT_EQ(0, c_orm_select_where_lt(b, "id"));
  ASSERT_EQ(1, c_orm_select_where_gt(NULL, "id"));
  ASSERT_EQ(0, c_orm_select_where_gt(b, "id"));
  ASSERT_EQ(1, c_orm_select_where_lte(NULL, "id"));
  ASSERT_EQ(0, c_orm_select_where_lte(b, "id"));
  ASSERT_EQ(1, c_orm_select_where_gte(NULL, "id"));
  ASSERT_EQ(0, c_orm_select_where_gte(b, "id"));
  ASSERT_EQ(1, c_orm_select_where_like(NULL, "id"));
  ASSERT_EQ(0, c_orm_select_where_like(b, "id"));
  ASSERT_EQ(1, c_orm_select_where_between(NULL, "id"));
  ASSERT_EQ(0, c_orm_select_where_between(b, "id"));
  ASSERT_EQ(1, c_orm_select_where_ilike(NULL, "name"));
  ASSERT_EQ(0, c_orm_select_where_ilike(b, "name"));
  ASSERT_EQ(0, c_orm_select_where_in(b, "id", 3));
  ASSERT_EQ(1, c_orm_select_where_in(NULL, "id", 3));
  {
    void *arr = (void *)0x123;
    ASSERT_EQ(0, c_orm_select_where_in_array(b, "id", arr, &meta));
  }
  ASSERT_EQ(1, c_orm_select_where_in_array(NULL, "id", NULL, &meta));

  /* To hit "AND" appending */
  ASSERT_EQ(1, c_orm_select_where_eq(NULL, "id"));
  ASSERT_EQ(0, c_orm_select_where_eq(b, "id"));

  /* Relation logic */
  ASSERT_EQ(1, c_orm_select_where_relation(NULL, "rel", "col"));
  ASSERT_EQ(0, c_orm_select_where_relation(b, "my_rel.id", "col"));
  ASSERT_EQ(1, c_orm_select_where_relation(b, "unknown_rel.id", "col"));
  ASSERT_EQ(0, c_orm_select_where_relation(b, "m2m_rel.id", "col"));
  ASSERT_EQ(1, c_orm_select_where_relation(b, "bad_rel.id", "col"));
  ASSERT_EQ(1, c_orm_select_where_relation(b, "my_rel.bad_rel.id", "col"));

  {
    char long_name[256];
    memset(long_name, 'A', 255);
    long_name[255] = '\0';
    long_name[128] = '.';
    ASSERT_EQ(1, c_orm_select_where_relation(b, long_name, "col"));
  }

  ASSERT_EQ(0, c_orm_select_having(b, "count > 1"));
  ASSERT_EQ(1, c_orm_select_having(NULL, "count > 1"));

  /* Re-init so we have SELECT * FROM at start for aggregate test */
  c_orm_select_builder_free(b);
  c_orm_select_builder_init(&meta, &b);
  ASSERT_EQ(0, c_orm_select_aggregate(b, "COUNT", "id", "c"));
  ASSERT_EQ(0, c_orm_select_aggregate(b, "SUM", "val", "s"));
  ASSERT_EQ(1, c_orm_select_aggregate(NULL, "COUNT", "id", "c"));

  ASSERT_EQ(0, c_orm_select_order_by(b, "id", 0));
  ASSERT_EQ(0, c_orm_select_order_by(b, "name", 1)); /* Hits ", " and " DESC" */
  ASSERT_EQ(1, c_orm_select_order_by(b, NULL, 0));

  ASSERT_EQ(1, c_orm_select_group_by(NULL, "id"));
  ASSERT_EQ(0, c_orm_select_group_by(b, "id"));
  ASSERT_EQ(1, c_orm_select_group_by(b, NULL));
  ASSERT_EQ(1, c_orm_select_limit(NULL, 10));

  ASSERT_EQ(0, c_orm_select_limit(b, 10));
  ASSERT_EQ(1, c_orm_select_offset(NULL, 5));
  ASSERT_EQ(0, c_orm_select_offset(b, 5));

  {
    char *sql = NULL;
    ASSERT_EQ(0, 0);
    ASSERT_EQ(1, c_orm_select_builder_compile(b, NULL));
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

  ASSERT_EQ(1, c_orm_update_builder_init(NULL, &ub));
  ASSERT_EQ(1, c_orm_update_builder_init(&meta, NULL));
  ASSERT_EQ(0, c_orm_update_builder_init(&meta, &ub));

  ASSERT_EQ(1, c_orm_update_set(NULL, "c"));
  ASSERT_EQ(1, c_orm_update_set(ub, NULL));
  ASSERT_EQ(0, c_orm_update_set(ub, "c"));
  ASSERT_EQ(0, c_orm_update_set(ub, "d")); /* Hits , */

  ASSERT_EQ(1, c_orm_update_where_eq(NULL, "c"));
  ASSERT_EQ(1, c_orm_update_where_eq(ub, NULL));
  ASSERT_EQ(0, c_orm_update_where_eq(ub, "c"));
  ASSERT_EQ(0, c_orm_update_where_eq(ub, "d")); /* Hits AND */

  ASSERT_EQ(1, c_orm_update_set(ub, "e")); /* hits has_where == 1 */

  {
    char *sql = NULL;
    ASSERT_EQ(0, c_orm_update_builder_compile(ub, &sql));
    ASSERT_EQ(1, c_orm_update_builder_compile(ub, NULL));
    C_ORM_FREE(sql);
  }
  c_orm_update_builder_free(ub);
  c_orm_update_builder_free(NULL);

  PASS();
}

static int oom_countdown = -1;
static int oom_active = 0;

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

static void qb_mock_free(void *ptr) { free(ptr); }

TEST test_query_builder_oom(void) {
  c_orm_select_builder_t *b = NULL;
  c_orm_update_builder_t *ub = NULL;

  int rc1, rc2, rc3, rc4, rc_sel_compile, rc_upd_compile;
  int rc_agg1, rc_agg2, rc_agg_extra1;
  int rc_agg3, rc_agg4, rc_agg_extra2;
  int rc_agg5, rc_agg6, rc_agg7;
  char *sql = NULL;
  void *(*old_malloc)(size_t) = c_orm_malloc;
  void (*old_free)(void *) = c_orm_free;

  c_orm_table_meta_t meta;
  memset(&meta, 0, sizeof(meta));
  meta.name = "test_table";

  old_malloc = c_orm_malloc;
  old_free = c_orm_free;
  c_orm_malloc = qb_mock_malloc;
  c_orm_free = qb_mock_free;

  /* Select init OOM */
  oom_active = 1;
  oom_countdown = 0;
  rc1 = c_orm_select_builder_init(&meta, &b);
  oom_countdown = 1;
  rc2 = c_orm_select_builder_init(&meta, &b);
  oom_active = 0;
  ASSERT_EQ(1, rc1);
  ASSERT_EQ(1, rc2);

  /* Select compile OOM */
  c_orm_select_builder_init(&meta, &b);
  oom_active = 1;
  oom_countdown = 0;
  rc_sel_compile = c_orm_select_builder_compile(b, &sql);
  oom_active = 0;
  ASSERT_EQ(1, rc_sel_compile);
  c_orm_select_builder_free(b);

  /* Update init OOM */
  oom_active = 1;
  oom_countdown = 0;
  rc3 = c_orm_update_builder_init(&meta, &ub);
  oom_countdown = 1;
  rc4 = c_orm_update_builder_init(&meta, &ub);
  oom_active = 0;
  ASSERT_EQ(1, rc3);
  ASSERT_EQ(1, rc4);

  /* Update compile OOM */
  c_orm_update_builder_init(&meta, &ub);
  oom_active = 1;
  oom_countdown = 0;
  rc_upd_compile = c_orm_update_builder_compile(ub, &sql);
  oom_active = 0;
  ASSERT_EQ(1, rc_upd_compile);
  c_orm_update_builder_free(ub);

  /* Aggregate OOMs */
  c_orm_select_builder_init(&meta, &b);
  oom_active = 1;
  oom_countdown = 0;
  rc_agg1 = c_orm_select_aggregate(b, "AVG", "x", "a");
  oom_countdown = 1;
  rc_agg2 = c_orm_select_aggregate(b, "AVG", "x", "a");
  oom_countdown = 2;
  rc_agg_extra1 = c_orm_select_aggregate(b, "AVG", "x", "a");
  oom_active = 0;
  ASSERT_EQ(1, rc_agg1);
  ASSERT_EQ(1, rc_agg2);
  ASSERT_EQ(1, rc_agg_extra1);

  /* Aggregate OOMs (else branch) */
  c_orm_select_where_eq(b, "id");
  oom_active = 1;
  oom_countdown = 0;
  rc_agg3 = c_orm_select_aggregate(b, "AVG", "x", "a");
  oom_countdown = 1;
  rc_agg4 = c_orm_select_aggregate(b, "AVG", "x", "a");
  oom_countdown = 2;
  rc_agg_extra2 = c_orm_select_aggregate(b, "AVG", "x", "a");
  oom_active = 0;
  ASSERT_EQ(1, rc_agg3);
  ASSERT_EQ(1, rc_agg4);
  ASSERT_EQ(1, rc_agg_extra2);

  /* OOM for second aggregate */
  /* First we need to make it successfully hit the first branch so the string is
   * modified */
  c_orm_select_builder_free(b);
  c_orm_select_builder_init(&meta, &b);
  c_orm_select_aggregate(b, "COUNT", "id",
                         "c"); /* now it is SELECT COUNT(id) AS c FROM ... */

  oom_active = 1;
  oom_countdown = 0;
  rc_agg5 = c_orm_select_aggregate(b, "AVG", "y", "b");
  oom_countdown = 1;
  rc_agg6 = c_orm_select_aggregate(b, "AVG", "y", "b");
  oom_countdown = 2;
  rc_agg7 = c_orm_select_aggregate(b, "AVG", "y", "b");
  oom_active = 0;
  ASSERT_EQ(1, rc_agg5);
  ASSERT_EQ(1, rc_agg6);
  ASSERT_EQ(1, rc_agg7);

  c_orm_select_builder_free(b);

  c_orm_malloc = old_malloc;
  c_orm_free = old_free;
  PASS();
}

SUITE(query_builder_coverage_suite) {
  RUN_TEST(test_query_builder_coverage);
  RUN_TEST(test_query_builder_oom);
}
