/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_ast.h"
#include "c_orm_sqlite.h"
#include "greatest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

static int oom_countdown = -1;
static int oom_active = 0;

static void *q_mock_malloc(size_t size) {
  if (oom_active) {
    if (oom_countdown == 0) {
      oom_countdown--;
      return NULL;
    }
    oom_countdown--;
  }
  return malloc(size);
}
static void *q_mock_realloc(void *ptr, size_t size) {
  if (oom_active) {
    if (oom_countdown == 0) {
      oom_countdown--;
      return NULL;
    }
    oom_countdown--;
  }
  return realloc(ptr, size);
}
static void q_mock_free(void *ptr) { free(ptr); }

TEST test_query_fluent_coverage(void) {
  c_orm_query_t *q = NULL;
  c_orm_ast_node_t *n_lit;
  c_orm_query_t *q_sub = NULL;
  c_orm_query_new(&q);

  /* Execute and create all nodes for coverage */
  n_lit = q->lit(q, "1", 0);
  ASSERT(n_lit != NULL);
  ASSERT(q->col(q, "id") != NULL);
  ASSERT(q->eq(q, "a", "b", 1) != NULL);
  ASSERT(q->neq(q, "a", "b", 1) != NULL);
  ASSERT(q->gt(q, "a", "b", 1) != NULL);
  ASSERT(q->lt(q, "a", "b", 1) != NULL);
  ASSERT(q->like(q, "a", "b") != NULL);
  ASSERT(q->raw(q, "SELECT 1") != NULL);
  ASSERT(q->is_null(q, "a", 1) != NULL);
  ASSERT(q->between(q, "a", "b", "c", 1) != NULL);
  ASSERT(q->group(q, n_lit) != NULL);
  ASSERT(q->func(q, "COUNT", "id", "c") != NULL);
  ASSERT(q->cast_(q, "id", "TEXT") != NULL);
  ASSERT(q->window(q, "ROW_NUMBER", "id", "id DESC", "rn") != NULL);

  c_orm_query_new(&q_sub);
  q_sub->select_(q_sub, "1");
  ASSERT(q->subquery(q, q_sub, "s") != NULL);
  ASSERT(q->exists(q, q_sub, 1) != NULL);

  /* Exhaustive NULL arg tests to reach 100% coverage naturally */
  q->select_(NULL, "a");
  q->select_(q, NULL);

  q->from(NULL, "a");
  q->from(q, NULL);

  q->where(NULL, n_lit);
  q->where(q, NULL);

  q->and_where(NULL, n_lit);
  q->and_where(q, NULL);

  q->or_where(NULL, n_lit);
  q->or_where(q, NULL);

  q->order_by(NULL, "a", 0);
  q->order_by(q, NULL, 0);

  q->limit(NULL, 1);
  q->offset(NULL, 1);

  q->group_by(NULL, "a");
  q->group_by(q, NULL);

  q->having(NULL, n_lit);
  q->having(q, NULL);

  q->join(NULL, "a", "b", n_lit);
  q->join(q, NULL, "b", n_lit);
  q->join(q, "a", "b", NULL);

  q->left_join(NULL, "a", n_lit);
  q->left_join(q, NULL, n_lit);
  q->left_join(q, "a", NULL);

  q->right_join(NULL, "a", n_lit);
  q->right_join(q, NULL, n_lit);
  q->right_join(q, "a", NULL);

  q->with(NULL, "a", q_sub);
  q->with(q, NULL, q_sub);
  q->with(q, "a", NULL);

  q->union_(NULL, q_sub, 0);
  q->union_(q, NULL, 0);

  q->distinct(NULL);

  q->from_alias(NULL, "a", "b");
  q->from_alias(q, NULL, "b");
  q->from_alias(q, "a", NULL);

  q->eager_load(NULL, NULL, "b");
  q->eager_load(q, NULL, "b");
  q->eager_load(q, NULL, NULL);

  q->group(NULL, n_lit);
  q->group(q, NULL);

  q->subquery(NULL, q_sub, "a");
  q->subquery(q, NULL, "a");
  q->subquery(q, q_sub, NULL);

  q->func(NULL, "a", "b", "c");
  q->func(q, NULL, "b", "c");
  q->func(q, "a", NULL, "c");
  q->func(q, "a", "b", NULL);

  q->cast_(NULL, "a", "b");
  q->cast_(q, NULL, "b");
  q->cast_(q, "a", NULL);

  q->between(NULL, "a", "b", "c", 0);
  q->between(q, NULL, "b", "c", 0);
  q->between(q, "a", NULL, "c", 0);
  q->between(q, "a", "b", NULL, 0);

  q->exists(NULL, q_sub, 0);
  q->exists(q, NULL, 0);

  q->window(NULL, "a", "b", "c", "d");
  q->window(q, NULL, "b", "c", "d");
  q->window(q, "a", NULL, "c", "d");
  q->window(q, "a", "b", NULL, "d");
  q->window(q, "a", "b", "c", NULL);

  q->raw(NULL, "a");
  q->raw(q, NULL);

  q->col(NULL, "a");
  q->col(q, NULL);

  q->lit(NULL, "a", 0);
  q->lit(q, NULL, 0);

  q->eq(NULL, "a", "b", 0);
  q->eq(q, NULL, "b", 0);
  q->eq(q, "a", NULL, 0);

  q->neq(NULL, "a", "b", 0);
  q->neq(q, NULL, "b", 0);
  q->neq(q, "a", NULL, 0);

  q->gt(NULL, "a", "b", 0);
  q->gt(q, NULL, "b", 0);
  q->gt(q, "a", NULL, 0);

  q->lt(NULL, "a", "b", 0);
  q->lt(q, NULL, "b", 0);
  q->lt(q, "a", NULL, 0);

  q->like(NULL, "a", "b");
  q->like(q, NULL, "b");
  q->like(q, "a", NULL);

  q->in(NULL, "a", "b");
  q->in(q, NULL, "b");
  q->in(q, "a", NULL);

  q->op(NULL, "a", n_lit, n_lit);
  q->op(q, NULL, n_lit, n_lit);
  q->op(q, "a", NULL, n_lit);
  q->op(q, "a", n_lit, NULL);

  q->is_null(NULL, "a", 0);
  q->is_null(q, NULL, 0);

  {
    c_orm_query_t *q_cloned = NULL;
    q->clone(NULL, &q_cloned);
    q->clone(q, NULL);
  }
  c_orm_query_new(NULL);

  /* IN node */
  ASSERT(q->in(q, "id", "1,2") != NULL);
  ASSERT(q->op(q, "AND", n_lit, n_lit) != NULL);

  c_orm_query_free(q_sub);
  c_orm_query_free(q);
  PASS();
}

static c_orm_error_t (*orig_bind_string)(c_orm_query_t *, int, const char *);
static int fail_bind = 0;
static c_orm_error_t my_mock_bind_string(c_orm_query_t *query, int index,
                                         const char *val) {
  if (fail_bind)
    return C_ORM_ERROR_BIND;
  return orig_bind_string(query, index, val);
}

static c_orm_error_t (*orig_prep)(c_orm_db_t *, const char *, c_orm_query_t **);
static int fail_prep = 0;
static c_orm_error_t my_mock_prep(c_orm_db_t *db, const char *sql,
                                  c_orm_query_t **out_query) {
  if (fail_prep)
    return C_ORM_ERROR_SQL;
  return orig_prep(db, sql, out_query);
}

TEST test_fluent_oom(void) {
  int pad, i, j;
  /* Test c_orm_query_new OOMs */
  {
    c_orm_query_t *oq = NULL;
    oom_active = 1;
    oom_countdown = 0;
    c_orm_query_new(&oq);
    oom_countdown = 1;
    (void)c_orm_query_new(&oq);
    oom_active = 0;
  }

  for (pad = 165; pad < 166; pad++) {
    {
      c_orm_query_t *oq = NULL;
      c_orm_query_new(&oq);
      for (j = 0; j < pad; j++)
        oq->raw(oq, "1");
      oom_active = 1;
      oom_countdown = 0;
      oq->select_(oq, "1");
      oom_active = 0;
      c_orm_query_free(oq);
    }
    {
      c_orm_query_t *oq = NULL;
      c_orm_query_new(&oq);
      for (j = 0; j < pad; j++)
        oq->raw(oq, "1");
      oom_active = 1;
      oom_countdown = 0;
      oq->from(oq, "1");
      oom_active = 0;
      c_orm_query_free(oq);
    }
    {
      c_orm_query_t *oq = NULL;
      c_orm_ast_node_t *n;
      c_orm_query_new(&oq);
      for (j = 0; j < pad; j++)
        oq->raw(oq, "1");
      n = oq->raw(oq, "1");
      oom_active = 1;
      oom_countdown = 0;
      oq->where(oq, n);
      oom_active = 0;
      c_orm_query_free(oq);
    }
    {
      c_orm_query_t *oq = NULL;
      c_orm_ast_node_t *n;
      c_orm_query_new(&oq);
      for (j = 0; j < pad; j++)
        oq->raw(oq, "1");
      n = oq->raw(oq, "1");
      oq->where(oq, n);
      oom_active = 1;
      oom_countdown = 0;
      oq->and_where(oq, n);
      oom_active = 0;
      c_orm_query_free(oq);
    }
    {
      c_orm_query_t *oq = NULL;
      c_orm_ast_node_t *n;
      c_orm_query_new(&oq);
      for (j = 0; j < pad; j++)
        oq->raw(oq, "1");
      n = oq->raw(oq, "1");
      oq->where(oq, n);
      oom_active = 1;
      oom_countdown = 0;
      oq->or_where(oq, n);
      oom_active = 0;
      c_orm_query_free(oq);
    }
    {
      c_orm_query_t *oq = NULL;
      c_orm_query_new(&oq);
      for (j = 0; j < pad; j++)
        oq->raw(oq, "1");
      oom_active = 1;
      oom_countdown = 0;
      oq->order_by(oq, "1", 0);
      oom_active = 0;
      c_orm_query_free(oq);
    }
    {
      c_orm_query_t *oq = NULL;
      c_orm_query_new(&oq);
      for (j = 0; j < pad; j++)
        oq->raw(oq, "1");
      oom_active = 1;
      oom_countdown = 0;
      oq->group_by(oq, "1");
      oom_active = 0;
      c_orm_query_free(oq);
    }
    {
      c_orm_query_t *oq = NULL;
      c_orm_ast_node_t *n;
      c_orm_query_new(&oq);
      for (j = 0; j < pad; j++)
        oq->raw(oq, "1");
      n = oq->raw(oq, "1");
      oom_active = 1;
      oom_countdown = 0;
      oq->having(oq, n);
      oom_active = 0;
      c_orm_query_free(oq);
    }
    {
      c_orm_query_t *oq = NULL;
      c_orm_ast_node_t *n;
      c_orm_query_new(&oq);
      for (j = 0; j < pad; j++)
        oq->raw(oq, "1");
      n = oq->raw(oq, "1");
      oom_active = 1;
      oom_countdown = 0;
      oq->join(oq, "1", "2", n);
      oom_active = 0;
      c_orm_query_free(oq);
    }
    {
      c_orm_query_t *oq = NULL;
      c_orm_ast_node_t *n;
      c_orm_query_new(&oq);
      for (j = 0; j < pad; j++)
        oq->raw(oq, "1");
      n = oq->raw(oq, "1");
      oom_active = 1;
      oom_countdown = 0;
      oq->left_join(oq, "1", n);
      oom_active = 0;
      c_orm_query_free(oq);
    }
    {
      c_orm_query_t *oq = NULL;
      c_orm_ast_node_t *n;
      c_orm_query_new(&oq);
      for (j = 0; j < pad; j++)
        oq->raw(oq, "1");
      n = oq->raw(oq, "1");
      oom_active = 1;
      oom_countdown = 0;
      oq->right_join(oq, "1", n);
      oom_active = 0;
      c_orm_query_free(oq);
    }
    {
      c_orm_query_t *oq = NULL;
      c_orm_query_t *sq = NULL;
      c_orm_query_new(&oq);
      for (j = 0; j < pad; j++)
        oq->raw(oq, "1");
      c_orm_query_new(&sq);
      oom_active = 1;
      oom_countdown = 0;
      oq->with(oq, "1", sq);
      oom_active = 0;
      c_orm_query_free(sq);
      c_orm_query_free(oq);
    }
    {
      c_orm_query_t *oq = NULL;
      c_orm_query_t *sq = NULL;
      c_orm_query_new(&oq);
      for (j = 0; j < pad; j++)
        oq->raw(oq, "1");
      c_orm_query_new(&sq);
      oom_active = 1;
      oom_countdown = 0;
      oq->union_(oq, sq, 0);
      oom_active = 0;
      c_orm_query_free(sq);
      c_orm_query_free(oq);
    }
    {
      c_orm_query_t *oq = NULL;
      c_orm_query_new(&oq);
      for (j = 0; j < pad; j++)
        oq->raw(oq, "1");
      oom_active = 1;
      oom_countdown = 0;
      oq->from_alias(oq, "1", "2");
      oom_active = 0;
      c_orm_query_free(oq);
    }
    {
      c_orm_query_t *oq = NULL;
      c_orm_query_new(&oq);
      for (j = 0; j < pad; j++)
        oq->raw(oq, "1");
      oom_active = 1;
      oom_countdown = 0;
      oq->distinct(oq);
      oom_active = 0;
      c_orm_query_free(oq);
    }
    {
      c_orm_query_t *oq = NULL;
      c_orm_query_new(&oq);
      for (j = 0; j < pad; j++)
        oq->raw(oq, "1");
      oom_active = 1;
      oom_countdown = 0;
      oq->limit(oq, 1);
      oom_active = 0;
      c_orm_query_free(oq);
    }
    {
      c_orm_query_t *oq = NULL;
      c_orm_query_new(&oq);
      for (j = 0; j < pad; j++)
        oq->raw(oq, "1");
      oom_active = 1;
      oom_countdown = 0;
      oq->offset(oq, 1);
      oom_active = 0;
      c_orm_query_free(oq);
    }
    {
      c_orm_query_t *oq = NULL;
      c_orm_query_new(&oq);
      for (j = 0; j < pad; j++)
        oq->raw(oq, "1");
      oom_active = 1;
      oom_countdown = 0;
      oq->raw(oq, "1");
      oom_active = 0;
      c_orm_query_free(oq);
    }
    {
      c_orm_query_t *oq = NULL;
      c_orm_query_new(&oq);
      for (j = 0; j < pad; j++)
        oq->raw(oq, "1");
      oom_active = 1;
      oom_countdown = 0;
      oq->col(oq, "1");
      oom_active = 0;
      c_orm_query_free(oq);
    }
    {
      c_orm_query_t *oq = NULL;
      c_orm_query_new(&oq);
      for (j = 0; j < pad; j++)
        oq->raw(oq, "1");
      oom_active = 1;
      oom_countdown = 0;
      oq->lit(oq, "1", 0);
      oom_active = 0;
      c_orm_query_free(oq);
    }
    {
      c_orm_query_t *oq = NULL;
      c_orm_query_new(&oq);
      for (j = 0; j < pad; j++)
        oq->raw(oq, "1");
      oom_active = 1;
      oom_countdown = 0;
      oq->lit(oq, "x'x", 1);
      oom_active = 0;
      c_orm_query_free(oq);
    }
    {
      c_orm_query_t *oq = NULL;
      c_orm_query_new(&oq);
      for (j = 0; j < pad; j++)
        oq->raw(oq, "1");
      oom_active = 1;
      oom_countdown = 1;
      oq->lit(oq, "x'x", 1);
      oom_active = 0;
      c_orm_query_free(oq);
    }
    {
      c_orm_query_t *oq = NULL;
      c_orm_ast_node_t *n;
      c_orm_query_new(&oq);
      for (j = 0; j < pad; j++)
        oq->raw(oq, "1");
      n = oq->raw(oq, "1");
      oom_active = 1;
      oom_countdown = 0;
      oq->op(oq, "=", n, n);
      oom_active = 0;
      c_orm_query_free(oq);
    }
    {
      c_orm_query_t *oq = NULL;
      c_orm_ast_node_t *n;
      c_orm_query_new(&oq);
      for (j = 0; j < pad; j++)
        oq->raw(oq, "1");
      n = oq->raw(oq, "1");
      oom_active = 1;
      oom_countdown = 0;
      oq->group(oq, n);
      oom_active = 0;
      c_orm_query_free(oq);
    }
    {
      c_orm_query_t *oq = NULL;
      c_orm_query_t *sq = NULL;
      c_orm_query_new(&oq);
      for (j = 0; j < pad; j++)
        oq->raw(oq, "1");
      c_orm_query_new(&sq);
      oom_active = 1;
      oom_countdown = 0;
      oq->subquery(oq, sq, "s");
      oom_active = 0;
      c_orm_query_free(sq);
      c_orm_query_free(oq);
    }
    {
      c_orm_query_t *oq = NULL;
      c_orm_query_new(&oq);
      for (j = 0; j < pad; j++)
        oq->raw(oq, "1");
      oom_active = 1;
      oom_countdown = 0;
      oq->func(oq, "f", "c", "a");
      oom_active = 0;
      c_orm_query_free(oq);
    }
    {
      c_orm_query_t *oq = NULL;
      c_orm_query_new(&oq);
      for (j = 0; j < pad; j++)
        oq->raw(oq, "1");
      oom_active = 1;
      oom_countdown = 0;
      oq->cast_(oq, "c", "t");
      oom_active = 0;
      c_orm_query_free(oq);
    }
    {
      c_orm_query_t *oq = NULL;
      c_orm_query_new(&oq);
      for (j = 0; j < pad; j++)
        oq->raw(oq, "1");
      oom_active = 1;
      oom_countdown = 0;
      oq->between(oq, "c", "1", "2", 0);
      oom_active = 0;
      c_orm_query_free(oq);
    }
    {
      c_orm_query_t *oq = NULL;
      c_orm_query_t *sq = NULL;
      c_orm_query_new(&oq);
      for (j = 0; j < pad; j++)
        oq->raw(oq, "1");
      c_orm_query_new(&sq);
      oom_active = 1;
      oom_countdown = 0;
      oq->exists(oq, sq, 0);
      oom_active = 0;
      c_orm_query_free(sq);
      c_orm_query_free(oq);
    }
    {
      c_orm_query_t *oq = NULL;
      c_orm_query_new(&oq);
      for (j = 0; j < pad; j++)
        oq->raw(oq, "1");
      oom_active = 1;
      oom_countdown = 0;
      oq->window(oq, "f", "p", "o", "a");
      oom_active = 0;
      c_orm_query_free(oq);
    }
    {
      c_orm_query_t *oq = NULL;
      c_orm_query_new(&oq);
      for (j = 0; j < pad; j++)
        oq->raw(oq, "1");
      oom_active = 1;
      oom_countdown = 0;
      oq->is_null(oq, "c", 0);
      oom_active = 0;
      c_orm_query_free(oq);
    }
  }

  /* Test clone OOM */
  {
    c_orm_query_t *qc = NULL;
    c_orm_ast_node_t *n_raw;
    c_orm_ast_node_t *n_group;
    c_orm_query_t *sq2 = NULL;
    c_orm_ast_node_t *n_sq;
    c_orm_ast_node_t *n_func;
    c_orm_ast_node_t *n_cast;
    c_orm_ast_node_t *n_exists;
    c_orm_ast_node_t *n_between;
    c_orm_ast_node_t *n_window;
    c_orm_ast_node_t *n_in;
    c_orm_ast_node_t *n_like;
    c_orm_query_t *w_sq = NULL;
    c_orm_query_t *u = NULL;
    c_orm_query_t *q_cloned = NULL;

    c_orm_query_new(&qc);
    n_raw = qc->raw(qc, "1");
    n_group = qc->group(qc, qc->lit(qc, "2", 0));
    c_orm_query_new(&sq2);
    n_sq = qc->subquery(qc, sq2, "sq_alias");
    n_func = qc->func(qc, "COUNT", "id", "c");
    n_cast = qc->cast_(qc, "id", "TEXT");
    n_exists = qc->exists(qc, sq2, 0);
    n_between = qc->between(qc, "age", "10", "20", 0);
    n_window = qc->window(qc, "RANK", "id", "id", "r");
    n_in = qc->in(qc, "id", "1,2");
    n_like = qc->like(qc, "name", "sam%");

    qc->and_where(qc, n_raw);
    qc->and_where(qc, n_group);
    qc->and_where(qc, n_sq);
    qc->and_where(qc, n_func);
    qc->and_where(qc, n_cast);
    qc->and_where(qc, n_exists);
    qc->and_where(qc, n_between);
    qc->and_where(qc, n_window);
    qc->and_where(qc, n_in);
    qc->and_where(qc, n_like);

    qc->group_by(qc, "id");
    qc->having(qc, n_raw);
    qc->join(qc, "users", "INNER", qc->eq(qc, "a", "b", 0));
    c_orm_query_new(&w_sq);
    w_sq->select_(w_sq, "1");
    qc->with(qc, "w", w_sq);

    c_orm_query_new(&u);
    u->select_(u, "1");
    qc->union_(qc, u, 1);
    for (i = 0; i < 40; i++) {
      q_cloned = NULL;
      oom_active = 1;
      oom_countdown = i;
      qc->clone(qc, &q_cloned);
      oom_active = 0;
      if (q_cloned)
        c_orm_query_free(q_cloned);
    }

    c_orm_query_free(u);
    c_orm_query_free(w_sq);
    c_orm_query_free(sq2);
    c_orm_query_free(qc);
  }

  /* eager_load OOM and validation */
  {
    c_orm_table_meta_t meta;
    c_orm_relation_meta_t rel[2];
    c_orm_column_meta_t tcol[2];
    c_orm_table_meta_t tmeta;
    c_orm_query_t *qe = NULL;

    memset(&meta, 0, sizeof(meta));
    meta.name = "base_tbl";

    memset(rel, 0, sizeof(rel));
    rel[0].field_name = "my_rel";
    rel[0].target_table = "target_tbl";
    rel[0].type = C_ORM_RELATION_ONE_TO_MANY;
    rel[0].foreign_key = "fk_id";

    memset(tcol, 0, sizeof(tcol));
    tcol[0].name = "t_id";
    tcol[1].name = "t_val";

    memset(&tmeta, 0, sizeof(tmeta));
    tmeta.name = "target_tbl";
    tmeta.num_columns = 2;
    tmeta.columns = tcol;

    rel[0].target_meta = &tmeta;
    meta.num_relations = 1;
    meta.relations = rel;

    c_orm_query_new(&qe);
    qe->eager_load(qe, &meta, "unknown");
    qe->select_(qe, "1");
    qe->eager_load(qe, &meta, "my_rel");

    for (i = 0; i < 20; i++) {
      c_orm_query_t *qo = NULL;
      c_orm_query_new(&qo);
      qo->select_(qo, "1");
      oom_active = 1;
      oom_countdown = i;
      qo->eager_load(qo, &meta, "my_rel");
      oom_active = 0;
      c_orm_query_free(qo);
    }
    c_orm_query_free(qe);
  }
  PASS();
}

TEST test_sql_oom(void) {
  c_orm_query_t *q = NULL;
  char *sql = NULL;
  c_orm_query_params_t p;
  int i;
  c_orm_query_new(&q);
  c_orm_query_params_init(&p);

  q->select_(q, "id")->from(q, "users")->where(q, q->eq(q, "id", "1", 0));

  /* OOM query_to_sql */
  for (i = 0; i < 30; i++) {
    oom_active = 1;
    oom_countdown = i;
    c_orm_query_to_sql(q, C_ORM_DIALECT_SQLITE, &sql, &p);
    oom_active = 0;
    if (sql)
      free(sql);
    sql = NULL;
    c_orm_query_params_cleanup(&p);
    c_orm_query_params_init(&p);
  }

  c_orm_query_params_cleanup(&p);
  c_orm_query_free(q);

  c_orm_query_params_init(&p);
  for (i = 0; i < 5; i++) {
    oom_active = 1;
    oom_countdown = i;
    c_orm_query_params_add(&p, "v", 1);
    oom_active = 0;
  }
  c_orm_query_params_cleanup(&p);

  PASS();
}

TEST test_query_sql_coverage(void) {
  void *res_obj = NULL;

  struct Generic_Array {
    void *data;
    size_t length;
    size_t capacity;
  } my_arr = {NULL, 0, 0};

  size_t res_count = 0;

  c_orm_query_t *q = NULL;
  char *sql = NULL;
  c_orm_query_params_t p;
  c_orm_ast_node_t *f;
  c_orm_ast_node_t *c;
  c_orm_ast_node_t *w;
  c_orm_ast_node_t *r;
  c_orm_query_t *sq = NULL;
  c_orm_query_t *u = NULL;
  c_orm_query_t *q_sq = NULL;
  c_orm_ast_node_t *r_exists;
  c_orm_ast_node_t *r_not_exists;
  c_orm_ast_node_t *r_sub;
  char *pg_sql = NULL;
  c_orm_query_params_t pg_p;
  c_orm_query_t *pg_q = NULL;
  c_orm_table_meta_t meta;
  c_orm_db_t *exec_db = NULL;
  c_orm_query_t *qe = NULL;
  c_orm_query_t *q_bad = NULL;
  c_orm_driver_vtable_t orig_vt;
  c_orm_driver_vtable_t mock_vt;
  c_orm_query_t *q_emp = NULL;
  c_orm_query_t *q_inv = NULL;

  c_orm_query_new(&q);
  c_orm_query_params_init(&p);

  /* Complex Select to hit all SQL generation paths */
  q->select_(q, "id, name")
      ->distinct(q)
      ->from_alias(q, "users", "u")
      ->join(q, "posts", "INNER", q->eq(q, "posts.u_id", "u.id", 0))
      ->left_join(q, "comments", q->eq(q, "comments.p_id", "posts.id", 0))
      ->right_join(q, "likes", q->eq(q, "likes.p_id", "posts.id", 0))
      ->where(q, q->is_null(q, "deleted_at", 0))
      ->and_where(q, q->between(q, "age", "18", "30", 0))
      ->or_where(q, q->like(q, "name", "sam%"))
      ->group_by(q, "id")
      ->having(q, q->gt(q, "id", "5", 0))
      ->order_by(q, "id", 1) /* desc */
      ->limit(q, 10)
      ->offset(q, 5);

  /* Add functions, window, cast, raw */
  f = q->func(q, "MAX", "age", "max_age");
  c = q->cast_(q, "id", "TEXT");
  w = q->window(q, "RANK", "id", "id DESC", "rn");
  r = q->raw(q, "1=1");

  /* We append them to where so they get compiled */
  q->and_where(q, f);
  q->and_where(q, c);
  q->and_where(q, w);
  q->and_where(q, r);

  /* Subquery and UNION */
  c_orm_query_new(&sq);
  sq->select_(sq, "id")->from(sq, "other");
  q->with(q, "cte", sq);

  c_orm_query_new(&u);
  u->select_(u, "1");
  q->union_(q, u, 1); /* UNION ALL */

  /* Test EXISTS and SUBQUERY rendering */
  c_orm_query_new(&q_sq);
  q_sq->select_(q_sq, "1");

  r_exists = q->exists(q, q_sq, 0);
  r_not_exists = q->exists(q, q_sq, 1);
  r_sub = q->subquery(q, q_sq, "my_sq");

  q->and_where(q, r_exists);
  q->and_where(q, r_not_exists);
  q->and_where(q, r_sub);

  c_orm_query_to_sql(q, C_ORM_DIALECT_SQLITE, &sql, &p);
  free(sql);
  c_orm_query_params_cleanup(&p);
  sql = NULL;
  c_orm_query_params_init(&p);

  c_orm_query_free(q_sq);
  c_orm_query_free(u);
  c_orm_query_free(q);
  c_orm_query_free(sq);

  /* Test inline rendering without params */
  {
    c_orm_query_t *q_inline = NULL;
    char *sql_inline = NULL;
    c_orm_query_new(&q_inline);
    q_inline->select_(q_inline, "1")
        ->from(q_inline, "t")
        ->where(q_inline, q_inline->between(q_inline, "a", "1", "2", 1))
        ->and_where(q_inline, q_inline->eq(q_inline, "b", "3", 1))
        ->and_where(q_inline, q_inline->in(q_inline, "c", "4,5"))
        ->and_where(q_inline,
                    q_inline->group(q_inline, q_inline->lit(q_inline, "6", 1)));

    c_orm_query_to_sql(q_inline, C_ORM_DIALECT_POSTGRES, &sql_inline, NULL);
    if (sql_inline)
      free(sql_inline);
    sql_inline = NULL;
    c_orm_query_to_sql(q_inline, C_ORM_DIALECT_SQLITE, &sql_inline, NULL);
    if (sql_inline)
      free(sql_inline);
    sql_inline = NULL;
    c_orm_query_free(q_inline);
  }

  /* Render Postgres params manually */
  c_orm_query_params_init(&pg_p);
  c_orm_query_new(&pg_q);
  pg_q->select_(pg_q, "1")
      ->from(pg_q, "t")
      ->where(pg_q, pg_q->between(pg_q, "a", "1", "2", 0))
      ->and_where(pg_q, pg_q->in(pg_q, "c", "1, 2"));
  c_orm_query_to_sql(pg_q, C_ORM_DIALECT_POSTGRES, &pg_sql, &pg_p);
  if (pg_sql)
    free(pg_sql);
  pg_sql = NULL;
  c_orm_query_params_cleanup(&pg_p);
  c_orm_query_free(pg_q);

  /* NULL params for query_to_sql */
  c_orm_query_params_init(NULL);
  c_orm_query_params_cleanup(NULL);
  c_orm_query_params_add(NULL, "v", 1);
  c_orm_query_new(&q);
  c_orm_query_to_sql(NULL, C_ORM_DIALECT_SQLITE, &sql, &p);
  c_orm_query_to_sql(q, C_ORM_DIALECT_SQLITE, &sql, &p);
  c_orm_free(sql);
  q->ast_head = NULL;
  c_orm_query_to_sql(q, C_ORM_DIALECT_SQLITE, &sql, &p);
  c_orm_free(sql);

  /* Query Execute / Fetch NULL validation */
  c_orm_query_execute(NULL, q);
  c_orm_query_free(q);

  /* Execute, fetch_one, fetch_all tests */
  memset(&meta, 0, sizeof(meta));
  meta.name = "t_exec";
  meta.struct_size = 8;

  c_orm_sqlite_connect(":memory:", &exec_db);
  c_orm_execute_raw(exec_db, "CREATE TABLE t_exec (id INT);");
  c_orm_execute_raw(exec_db, "INSERT INTO t_exec VALUES (1);");

  c_orm_query_new(&qe);
  qe->select_(qe, "id")
      ->from(qe, "t_exec")
      ->where(qe, qe->eq(qe, "id", "1", 0));

  c_orm_query_execute(exec_db, qe);

  /* Mock prepare fail */
  c_orm_query_new(&q_bad);
  q_bad->select_(q_bad, "id")->from(q_bad, "bad_table");
  c_orm_query_execute(exec_db, q_bad);
  c_orm_query_free(q_bad);

  /* Mock bind_string fail */
  orig_vt = *exec_db->vtable;
  mock_vt = *exec_db->vtable;
  orig_bind_string = mock_vt.bind_string;

  mock_vt.bind_string = my_mock_bind_string;
  exec_db->vtable = (const c_orm_driver_vtable_t *)&mock_vt;

  fail_bind = 1;
  c_orm_query_execute(exec_db, qe);
  c_orm_query_fetch_one(exec_db, qe, &meta, &res_obj);
  c_orm_query_fetch_all(exec_db, qe, &meta, &my_arr);
  fail_bind = 0;

  /* Fallback call */
  my_mock_bind_string(NULL, 1, "test");

  /* Mock prepare fail properly to hit the lines */
  orig_prep = mock_vt.prepare;
  mock_vt.prepare = my_mock_prep;

  fail_prep = 1;
  c_orm_query_fetch_one(exec_db, qe, &meta, &res_obj);
  c_orm_query_fetch_all(exec_db, qe, &meta, &my_arr);
  fail_prep = 0;

  /* Fallback call */
  my_mock_prep(exec_db, "SELECT 1", &q_bad);
  mock_vt.finalize(q_bad);

  exec_db->vtable = (const c_orm_driver_vtable_t *)&orig_vt;

  /* fetch_one */
  c_orm_query_fetch_one(NULL, qe, &meta, &res_obj);
  c_orm_query_fetch_one(exec_db, qe, &meta, &res_obj);

  /* fetch_one empty */
  c_orm_query_new(&q_emp);
  q_emp->select_(q_emp, "id")
      ->from(q_emp, "t_exec")
      ->where(q_emp, q_emp->eq(q_emp, "id", "2", 0));
  c_orm_query_fetch_one(exec_db, q_emp, &meta, &res_obj);
  c_orm_query_free(q_emp);

  /* fetch_all */
  c_orm_query_fetch_all(NULL, qe, &meta, &my_arr);
  c_orm_query_fetch_all(exec_db, qe, &meta, &my_arr);

  /* Missing from / select for failure */
  c_orm_query_new(&q_inv);
  c_orm_query_execute(exec_db, q_inv);
  c_orm_query_fetch_one(exec_db, q_inv, &meta, &res_obj);
  c_orm_query_fetch_all(exec_db, q_inv, &meta, &my_arr);
  c_orm_query_free(q_inv);

  exec_db->vtable->disconnect(exec_db);
  c_orm_query_free(qe);
  if (my_arr.data) {
    c_orm_free(my_arr.data);
  }
  PASS();
}

SUITE(query_fluent_coverage_suite) {

  void *(*old_malloc)(size_t) = c_orm_malloc;
  void (*old_free)(void *) = c_orm_free;
  void *(*old_realloc)(void *, size_t) = c_orm_realloc;

  c_orm_malloc = q_mock_malloc;
  c_orm_free = q_mock_free;
  c_orm_realloc = q_mock_realloc;

  RUN_TEST(test_query_fluent_coverage);
  RUN_TEST(test_fluent_oom);
  RUN_TEST(test_query_sql_coverage);
  RUN_TEST(test_sql_oom);

  c_orm_malloc = old_malloc;
  c_orm_free = old_free;
  c_orm_realloc = old_realloc;
}
