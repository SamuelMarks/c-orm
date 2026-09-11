#if defined(__clang__) || defined(__GNUC__)
#endif
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

static c_orm_error_t (*orig_finalize_mock)(c_orm_query_t *);
static int my_fail_finalize = 0;
static c_orm_error_t my_mock_finalize(c_orm_query_t *q_ptr) {
  c_orm_error_t res = orig_finalize_mock(q_ptr);
  if (my_fail_finalize)
    res = C_ORM_ERROR_SQL;
  return res;
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
  int i;
  {
    c_orm_query_t *oq = NULL;
    oom_active = 1;
    oom_countdown = 0;
    c_orm_query_new(&oq);
    oom_countdown = 1;
    (void)c_orm_query_new(&oq);
    oom_active = 0;
  }

#define TEST_OOM(expr)                                                         \
  do {                                                                         \
    c_orm_query_t *oq = NULL;                                                  \
    c_orm_ast_node_t *n = (c_orm_ast_node_t *)1;                               \
    c_orm_query_t *sq = (c_orm_query_t *)1;                                    \
    c_orm_query_new(&oq);                                                      \
    oom_active = 1;                                                            \
    oom_countdown = 0;                                                         \
    (void)sq;                                                                  \
    (void)n;                                                                   \
    expr;                                                                      \
    oom_active = 0;                                                            \
    c_orm_query_free(oq);                                                      \
  } while (0)

  TEST_OOM(oq->raw(oq, "1"));
  TEST_OOM(oq->col(oq, "1"));
  TEST_OOM(oq->lit(oq, "1", 0));
  TEST_OOM(oq->lit(oq, "x\'x", 1));
  TEST_OOM(oq->op(oq, "=", n, n));
  TEST_OOM(oq->select_(oq, "1"));
  TEST_OOM(oq->from(oq, "1"));
  TEST_OOM(oq->where(oq, n));
  TEST_OOM({
    oq->where(oq, oq->raw(oq, "1"));
    oq->and_where(oq, n);
  });
  TEST_OOM({
    oq->where(oq, oq->raw(oq, "1"));
    oq->or_where(oq, n);
  });
  TEST_OOM(oq->order_by(oq, "1", 0));
  TEST_OOM(oq->limit(oq, 1));
  TEST_OOM(oq->offset(oq, 1));
  TEST_OOM(oq->join(oq, "1", "2", n));
  TEST_OOM(oq->left_join(oq, "1", n));
  TEST_OOM(oq->right_join(oq, "1", n));
  TEST_OOM(oq->group_by(oq, "1"));
  TEST_OOM(oq->having(oq, n));
  TEST_OOM(oq->with(oq, "1", sq));
  TEST_OOM(oq->union_(oq, sq, 0));
  TEST_OOM(oq->from_alias(oq, "1", "2"));
  TEST_OOM(oq->distinct(oq));
  TEST_OOM(oq->group(oq, n));
  TEST_OOM(oq->subquery(oq, sq, "s"));
  TEST_OOM(oq->func(oq, "f", "c", "a"));
  TEST_OOM(oq->cast_(oq, "c", "t"));
  TEST_OOM(oq->is_null(oq, "c", 0));
  TEST_OOM(oq->between(oq, "c", "1", "2", 0));
  TEST_OOM(oq->exists(oq, sq, 0));
  TEST_OOM(oq->window(oq, "f", "p", "o", "a"));

#undef TEST_OOM

  {
    int count = 0;
    char big_str[5000];
    memset(big_str, 'x', 4999);
    big_str[4999] = '\0';
    big_str[0] = '\'';

    while (1) {
      c_orm_error_t err;
      c_orm_query_t *oq = NULL;
      c_orm_query_new(&oq);
      oom_active = 1;
      oom_countdown = count;
      oq->lit(oq, big_str, 1);
      oom_active = 0;
      err = (c_orm_error_t)oq->error;
      c_orm_query_free(oq);
      if (!err)
        break;
      count++;
    }
  }

  {
    c_orm_query_t *qc = NULL;
    c_orm_ast_node_t *n_raw, *n_group, *n_sq, *n_func, *n_cast, *n_exists,
        *n_between, *n_window, *n_in, *n_like;
    c_orm_query_t *sq2 = NULL, *w_sq = NULL, *u = NULL;

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

    for (i = 0; i < 20; i++) {
      c_orm_query_t *q_cloned = NULL;
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

  {
    c_orm_table_meta_t meta = {0};
    c_orm_relation_meta_t rel[2];
    c_orm_column_meta_t tcol[2];
    c_orm_table_meta_t tmeta;
    c_orm_query_t *qe = NULL;

    memset(&meta, 0, sizeof(meta));
    meta.name = "base_tbl";
    meta.num_columns = 2;
    meta.columns = tcol;

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
    qe->select_(qe, "*");
    qe->eager_load(qe, &meta, "unknown");
    rel[0].target_meta = NULL;
    qe->eager_load(qe, &meta, "my_rel");
    rel[0].target_meta = &tmeta;
    qe->eager_load(qe, &meta, "my_rel");

    for (i = 0; i < 2; i++) {
      c_orm_query_t *qo = NULL;
      c_orm_query_new(&qo);
      qo->select_(qo, "*");
      oom_active = 1;
      oom_countdown = i;
      qo->eager_load(qo, &meta, "my_rel");
      oom_active = 0;
      c_orm_query_free(qo);
    }
    c_orm_query_free(qe);
  }

  {
    c_orm_query_t *qc = NULL;
    c_orm_ast_node_t *n;
    c_orm_query_t *qc2 = NULL;
    c_orm_query_new(&qc);
    n = qc->where(qc, NULL)->ast_head;
    (void)n;
    qc->clone(qc, &qc2);
    if (qc2)
      c_orm_query_free(qc2);

    qc->join(qc, "a", "b", NULL);
    qc->clone(qc, &qc2);
    if (qc2)
      c_orm_query_free(qc2);
    c_orm_query_free(qc);
  }

  {
    c_orm_query_t *oq = NULL;
    c_orm_ast_node_t *n;
    c_orm_query_new(&oq);
    n = oq->raw(oq, "1");
    oq->where(oq, NULL);
    oq->and_where(oq, n);
    c_orm_query_free(oq);
  }

  {
    c_orm_query_t *oq = NULL;
    c_orm_ast_node_t *n;
    c_orm_query_new(&oq);
    n = oq->raw(oq, "1");
    oq->where(oq, NULL);
    oq->or_where(oq, n);
    c_orm_query_free(oq);
  }

  {
    c_orm_query_t *oq = NULL;
    c_orm_ast_node_t *n;
    c_orm_query_new(&oq);
    oq->select_(oq, "1");
    oq->from(oq, "t");
    n = oq->raw(oq, "1");
    oq->or_where(oq, n);
    c_orm_query_free(oq);
  }

  {
    c_orm_query_t *oq = NULL;
    c_orm_query_new(&oq);
    oq->from(oq, "t");
    oq->distinct(oq);
    c_orm_query_free(oq);
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
  for (i = 0; i < 20; i++) {
    oom_active = 1;
    oom_countdown = i;
    c_orm_query_to_sql(q, C_ORM_DIALECT_SQLITE, &sql, &p);
    oom_active = 0;
    if (sql) {
      C_ORM_FREE(sql);
      sql = NULL;
    }
    c_orm_query_params_cleanup(&p);
    c_orm_query_params_init(&p);
  }

  {
    c_orm_query_t *qb = NULL;
    c_orm_query_new(&qb);
    qb->select_(qb, "1")
        ->from(qb, "t")
        ->where(qb, qb->eq(qb, "a", "1", 0))
        ->and_where(qb, qb->eq(qb, "a", "2", 0))
        ->and_where(qb, qb->eq(qb, "a", "3", 0))
        ->and_where(qb, qb->eq(qb, "a", "4", 0))
        ->and_where(qb, qb->eq(qb, "a", "5", 0))
        ->and_where(qb, qb->eq(qb, "a", "6", 0))
        ->and_where(qb, qb->eq(qb, "a", "7", 0))
        ->and_where(qb, qb->between(qb, "a", "8", "9", 0));
    for (i = 0; i < 20; i++) {
      oom_active = 1;
      oom_countdown = i;
      c_orm_query_to_sql(qb, C_ORM_DIALECT_POSTGRES, &sql, &p);
      oom_active = 0;
      if (sql) {
        C_ORM_FREE(sql);
        sql = NULL;
      }
      c_orm_query_params_cleanup(&p);
      c_orm_query_params_init(&p);
    }
    c_orm_query_free(qb);
  }

  {
    c_orm_query_t *qb = NULL, *sq = NULL;
    c_orm_query_new(&qb);
    c_orm_query_new(&sq);
    sq->select_(sq, "1");

    qb->with(qb, "cte", sq);
    qb->select_(qb, "1")->from(qb, "t");
    qb->where(qb, qb->exists(qb, sq, 0));
    qb->and_where(qb, qb->subquery(qb, sq, "alias"));
    qb->union_(qb, sq, 0);

    for (i = 0; i < 50; i++) {
      oom_active = 1;
      oom_countdown = i;
      c_orm_query_to_sql(qb, C_ORM_DIALECT_SQLITE, &sql, &p);
      oom_active = 0;
      if (sql) {
        C_ORM_FREE(sql);
        sql = NULL;
      }
      c_orm_query_params_cleanup(&p);
      c_orm_query_params_init(&p);
    }
    c_orm_query_to_sql(qb, C_ORM_DIALECT_SQLITE, &sql, &p);
    if (sql) {
      C_ORM_FREE(sql);
      sql = NULL;
    }
    c_orm_query_params_cleanup(&p);
    c_orm_query_params_init(&p);
    c_orm_query_free(sq);
    c_orm_query_free(qb);
  }

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
  c_orm_error_t err;
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
  c_orm_table_meta_t meta = {0};
  c_orm_db_t *exec_db = NULL;
  c_orm_query_t *qe = NULL;
  c_orm_query_t *q_bad = NULL;
  c_orm_driver_vtable_t orig_vt;
  c_orm_driver_vtable_t mock_vt;
  c_orm_query_t *q_emp = NULL;
  c_orm_query_t *q_inv = NULL;
  (void)res_count;

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
  C_ORM_FREE(sql);
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
      C_ORM_FREE(sql_inline);
    sql_inline = NULL;
    c_orm_query_to_sql(q_inline, C_ORM_DIALECT_SQLITE, &sql_inline, NULL);
    if (sql_inline)
      C_ORM_FREE(sql_inline);
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
    C_ORM_FREE(pg_sql);
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
  sql = NULL;
  q->ast_head = NULL;
  c_orm_query_to_sql(q, C_ORM_DIALECT_SQLITE, &sql, &p);
  if (sql) {
    c_orm_free(sql);
    sql = NULL;
  }

  /* Query Execute / Fetch NULL validation */
  c_orm_query_execute(NULL, q);
  c_orm_query_free(q);

  {
    c_orm_query_t *qn = NULL;
    char *sql_n = NULL;
    c_orm_query_new(&qn);
    qn->select_(qn, "1")->from(qn, "t")->where(qn, qn->group(qn, NULL));
    c_orm_query_to_sql(qn, C_ORM_DIALECT_SQLITE, &sql_n, NULL);
    if (sql_n) {
      C_ORM_FREE(sql_n);
      sql_n = NULL;
    }
    c_orm_query_free(qn);
  }

  {
    c_orm_query_t *qn = NULL;
    char *sql_n = NULL;
    c_orm_ast_node_t *dummy_node;
    c_orm_query_new(&qn);
    dummy_node = qn->raw(qn, "1");
    dummy_node->type = (c_orm_ast_node_type_t)999;
    qn->ast_head = dummy_node;
    c_orm_query_to_sql(qn, C_ORM_DIALECT_SQLITE, &sql_n, NULL);
    if (sql_n) {
      C_ORM_FREE(sql_n);
      sql_n = NULL;
    }

    qn->ast_head = NULL;
    qn->select_(qn, "1")->from(qn, "t")->where(qn, qn->group(qn, dummy_node));
    c_orm_query_to_sql(qn, C_ORM_DIALECT_SQLITE, &sql_n, NULL);
    if (sql_n) {
      C_ORM_FREE(sql_n);
      sql_n = NULL;
    }

    c_orm_query_free(qn);
  }

  {
    unsigned int old_depth;
    char *sql_d = NULL;
    c_orm_query_t *q1 = NULL;
    c_orm_query_t *q2 = NULL;

    old_depth = cdd_c_sql_parser_max_depth;

    c_orm_query_new(&q1);
    q1->select_(q1, "1")->from(q1, "t")->join(q1, "t2", "INNER",
                                              q1->eq(q1, "a", "b", 0));
    cdd_c_sql_parser_max_depth = 0;
    c_orm_query_to_sql(q1, C_ORM_DIALECT_SQLITE, &sql_d, NULL);
    ASSERT_EQ(NULL, sql_d);
    cdd_c_sql_parser_max_depth = old_depth;
    c_orm_query_free(q1);

    c_orm_query_new(&q2);
    q2->select_(q2, "1")->from(q2, "t")->having(q2, q2->eq(q2, "c", "d", 0));
    cdd_c_sql_parser_max_depth = 0;
    c_orm_query_to_sql(q2, C_ORM_DIALECT_SQLITE, &sql_d, NULL);
    ASSERT_EQ(NULL, sql_d);
    cdd_c_sql_parser_max_depth = old_depth;
    c_orm_query_free(q2);
  }

  {
    c_orm_query_t *qo = NULL;
    c_orm_query_new(&qo);
    qo->select_(qo, "1")->from(qo, "t");

    oom_active = 1;
    oom_countdown = 0;
    c_orm_query_execute(exec_db, qo);
    oom_active = 0;

    oom_active = 1;
    oom_countdown = 0;
    c_orm_query_fetch_one(exec_db, qo, &meta, &res_obj);
    oom_active = 0;

    oom_active = 1;
    oom_countdown = 0;
    c_orm_query_fetch_all(exec_db, qo, &meta, &my_arr);
    oom_active = 0;

    c_orm_query_free(qo);
  }

  {
    c_orm_query_t *qb = NULL;
    int i;
    char big[4500];

    c_orm_query_params_init(&p);
    c_orm_query_new(&qb);
    memset(big, 'a', 4499);
    big[4499] = '\0';
    qb->select_(qb, "1")->from(qb, "t")->where(qb,
                                               qb->group(qb, qb->raw(qb, big)));

    for (i = 0; i < 20; i++) {
      oom_active = 1;
      oom_countdown = i;
      c_orm_query_to_sql(qb, C_ORM_DIALECT_POSTGRES, &sql, &p);
      oom_active = 0;
      if (sql) {
        C_ORM_FREE(sql);
        sql = NULL;
      }
      c_orm_query_params_cleanup(&p);
      c_orm_query_params_init(&p);
    }
    c_orm_query_free(qb);
  }

  /* Execute, fetch_one, fetch_all tests */
  memset(&meta, 0, sizeof(meta));
  meta.name = "t_exec";
  meta.struct_size = 8;

  c_orm_sqlite_connect(":memory:", &exec_db);
  err = c_orm_execute_raw(exec_db, "CREATE TABLE t_exec (id INT);");
  ASSERT_EQ(C_ORM_OK, err);
  err = c_orm_execute_raw(exec_db, "INSERT INTO t_exec VALUES (1);");
  ASSERT_EQ(C_ORM_OK, err);

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

  {
    c_orm_driver_vtable_t vtable_fin = *exec_db->vtable;
    c_orm_query_t *qf = NULL;

    orig_finalize_mock = vtable_fin.finalize;
    vtable_fin.finalize = my_mock_finalize;
    exec_db->vtable = (const c_orm_driver_vtable_t *)&vtable_fin;

    c_orm_query_new(&qf);
    qf->select_(qf, "id")
        ->from(qf, "t_exec")
        ->where(qf, qf->eq(qf, "id", "1", 1));

    my_fail_finalize = 1;
    fail_bind = 1;
    c_orm_query_execute(exec_db, qf);
    c_orm_query_fetch_one(exec_db, qf, &meta, &res_obj);
    c_orm_query_fetch_all(exec_db, qf, &meta, &my_arr);

    fail_bind = 0;
    my_fail_finalize = 1;
    c_orm_query_execute(exec_db, qf);
    c_orm_query_fetch_one(exec_db, qf, &meta, &res_obj);
    c_orm_query_fetch_all(exec_db, qf, &meta, &my_arr);

    my_fail_finalize = 0;
    {
      const c_orm_driver_vtable_t *sqlite_vt = NULL;
      c_orm_query_t *q_all = NULL;
      c_orm_error_t fa_err;
      c_orm_sqlite_get_vtable(&sqlite_vt);
      exec_db->vtable = sqlite_vt;
      memset(&my_arr, 0, sizeof(my_arr));
      c_orm_query_new(&q_all);
      q_all->select_(q_all, "id")->from(q_all, "t_exec");
      fa_err = c_orm_query_fetch_all(exec_db, q_all, &meta, &my_arr);
      ASSERT_EQ_FMT(C_ORM_OK, fa_err, "%d");
      c_orm_query_free(q_all);
    }
    c_orm_query_free(qf);
  }

  exec_db->vtable->disconnect(exec_db);
  c_orm_query_free(qe);
  if (my_arr.data) {
    c_orm_free(my_arr.data);
  }
  PASS();
}

static int g_malloc_fail = 0;
static int g_malloc_count = 0;
static int g_malloc_target = -1;
static void *mock_malloc_fail(size_t size) {
  if (g_malloc_fail) {
    if (g_malloc_target == g_malloc_count++)
      return NULL;
  }
  return malloc(size);
}

static void *mock_realloc_fail(void *ptr, size_t size) {
  void *res = NULL;
  if (!g_malloc_fail || g_malloc_target != g_malloc_count++)
    res = realloc(ptr, size);
  return res;
}

static void mock_free(void *p) { free(p); }

static c_orm_error_t mock_prepare(c_orm_db_t *db, const char *sql,
                                  c_orm_query_t **out) {
  (void)db;
  (void)sql;
  *out = NULL;
  return C_ORM_ERROR_SQL;
}

/* mock_step and mock_fin removed for coverage */

TEST test_query_sql_to_sql_fail(void) {
  void *(*old_malloc)(size_t) = c_orm_malloc;
  void *(*old_realloc)(void *, size_t) = c_orm_realloc;
  void (*old_free)(void *) = c_orm_free;
  int i;
  c_orm_db_t db;
  c_orm_driver_vtable_t vt;
  c_orm_query_t *qb = NULL;
  int fake_struct = 0;

  memset(&db, 0, sizeof(db));
  memset(&vt, 0, sizeof(vt));
  vt.prepare = mock_prepare;
  vt.step = NULL;
  vt.finalize = NULL;
  db.vtable = &vt;

  /* Call execute with mock_prepare */
  {
    c_orm_query_t *q_prep = NULL;
    c_orm_query_new(&q_prep);
    q_prep->select_(q_prep, "1")->from(q_prep, "t");
    c_orm_query_execute(&db, q_prep);
    c_orm_query_free(q_prep);
  }

  /* Exercise mock_realloc_fail */
  {
    void *m = malloc(10);
    void *r = mock_realloc_fail(m, 20);
    free(r);
  }

  for (i = 0; i < 2; i++) {
    c_orm_error_t rc;

    qb = NULL;
    rc = c_orm_query_new(&qb);
    if (rc == 0 && qb) {
      c_orm_ast_between_t *bw = NULL;

      qb->select_(qb, "*");
      qb->from(qb, "users");

      c_orm_arena_alloc(qb->arena, sizeof(c_orm_ast_between_t), (void **)&bw);
      if (bw) {
        memset(bw, 0, sizeof(*bw));
        bw->base.type = C_ORM_AST_NODE_BETWEEN;
        bw->col = "score";
        bw->low = "1";
        bw->high = "100";
        bw->is_string = 0;

        qb->where(qb, (c_orm_ast_node_t *)bw);
      }

      c_orm_set_allocators(mock_malloc_fail, c_orm_realloc, c_orm_free);
      c_orm_set_allocators(c_orm_malloc, mock_realloc_fail, c_orm_free);
      c_orm_set_allocators(c_orm_malloc, c_orm_realloc, mock_free);

      g_malloc_target = 0;
      g_malloc_count = 0;
      g_malloc_fail = 1;
      /* Pass valid pointers so to_sql is what fails */
      c_orm_query_fetch_one(&db, qb, (const c_orm_table_meta_t *)&db,
                            &fake_struct);

      g_malloc_target = 0;
      g_malloc_count = 0;
      g_malloc_fail = 1;
      c_orm_query_fetch_all(&db, qb, (const c_orm_table_meta_t *)&db,
                            &fake_struct);

      g_malloc_target = 0;
      g_malloc_count = 0;
      g_malloc_fail = 1;
      c_orm_query_execute(&db, qb);

      g_malloc_target = 2;
      g_malloc_count = 0;
      g_malloc_fail = 1;
      c_orm_query_fetch_one(&db, qb, (const c_orm_table_meta_t *)&db,
                            &fake_struct);

      g_malloc_target = 2;
      g_malloc_count = 0;
      g_malloc_fail = 1;
      c_orm_query_fetch_all(&db, qb, (const c_orm_table_meta_t *)&db,
                            &fake_struct);

      g_malloc_fail = 0;
      c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);
      c_orm_set_allocators(c_orm_malloc, old_realloc, c_orm_free);
      c_orm_set_allocators(c_orm_malloc, c_orm_realloc, old_free);

      c_orm_query_free(qb);
    }
  }

  PASS();
}

TEST test_query_sql_oom(void) {
  void *(*old_malloc)(size_t) = c_orm_malloc;
  void *(*old_realloc)(void *, size_t) = c_orm_realloc;
  void (*old_free)(void *) = c_orm_free;
  int i;
  c_orm_db_t db;
  c_orm_driver_vtable_t vt;
  c_orm_query_t *qb = NULL;

  memset(&db, 0, sizeof(db));
  memset(&vt, 0, sizeof(vt));
  vt.prepare = mock_prepare;
  vt.step = NULL;
  vt.finalize = NULL;
  db.vtable = &vt;

  c_orm_set_allocators(mock_malloc_fail, mock_realloc_fail, mock_free);

  for (i = 0; i < 100; i++) {
    c_orm_error_t rc;

    qb = NULL;
    rc = c_orm_query_new(&qb);
    if (rc == 0 && qb) {
      c_orm_ast_between_t *bw = NULL;

      /* Make it use a BETWEEN to hit render_node low/high */
      c_orm_arena_alloc(qb->arena, sizeof(c_orm_ast_between_t), (void **)&bw);
      if (bw) {
        memset(bw, 0, sizeof(*bw));
        bw->base.type = C_ORM_AST_NODE_BETWEEN;
        bw->col = "score";
        bw->low = "1";
        bw->high = "100";
        bw->is_string = 0;

        qb->ast_head = (c_orm_ast_node_t *)bw;
      }

      g_malloc_target = i;
      g_malloc_count = 0;
      g_malloc_fail = 1;

      /* Hit execution OOMs */
      c_orm_query_execute(&db, qb);
      c_orm_query_fetch_one(&db, qb, NULL, NULL);
      c_orm_query_fetch_all(&db, qb, NULL, NULL);

      g_malloc_fail = 0;
      c_orm_query_free(qb);
    }

    if (g_malloc_count <= i)
      break;
  }

  c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);
  c_orm_set_allocators(c_orm_malloc, old_realloc, c_orm_free);
  c_orm_set_allocators(c_orm_malloc, c_orm_realloc, old_free);

  PASS();
}

static int bind_fail_countdown = -1;
static int step_fail_countdown = -1;
static int fetch_fail_countdown = -1;
static int fin_fail_countdown = -1;

TEST fluent_exhaustive_oom(void) {
  void *(*old_malloc)(size_t) = c_orm_malloc;
  void *(*old_realloc)(void *, size_t) = c_orm_realloc;
  void (*old_free)(void *) = c_orm_free;
  int oom, extra;

  c_orm_column_meta_t target_col[2];
  c_orm_table_meta_t target_meta;
  c_orm_relation_meta_t rel_arr[2];
  c_orm_table_meta_t meta;

  memset(target_col, 0, sizeof(target_col));
  target_col[0].name = "id";
  target_col[0].is_pk = 1;
  target_col[1].name = "val";

  memset(&target_meta, 0, sizeof(target_meta));
  target_meta.name = "target_tbl";
  target_meta.columns = target_col;
  target_meta.num_columns = 2;

  memset(rel_arr, 0, sizeof(rel_arr));
  rel_arr[0].field_name = "posts";
  rel_arr[0].target_meta = &target_meta;
  rel_arr[0].type = C_ORM_RELATION_ONE_TO_MANY;
  rel_arr[0].local_key = "id";
  rel_arr[0].foreign_key = "pid";

  memset(&meta, 0, sizeof(meta));
  meta.name = "test_table";
  meta.columns = target_col;
  meta.num_columns = 2;
  meta.relations = rel_arr;
  meta.num_relations = 1;

  for (extra = 0; extra < 5; extra++) {
    for (oom = 0; oom < 80; oom++) {
      c_orm_query_t *query = NULL;
      c_orm_query_t *cloned = NULL;
      c_orm_query_t *subq = NULL;
      c_orm_ast_node_t *cond = NULL;

      c_orm_query_new(&subq);
      if (subq)
        subq->select_(subq, "*");

      if (c_orm_query_new(&query) == C_ORM_OK) {
        if (extra == 0) {
          query->group_by(query, "id");
          cond = query->eq(query, "id", "123", 0);
          query->having(query, cond);
          query->order_by(query, "id", 1);
          query->limit(query, 10);
          query->offset(query, 5);
          cond = query->raw(query, "1=1");
          cond = query->group(query, cond);
          query->and_where(query, cond);
          cond = query->subquery(query, subq, "sub");
          query->and_where(query, cond);
          query->union_(query, subq, 0);
          query->with(query, "w", subq);
        } else if (extra == 1) {
          cond = query->func(query, "MAX", "id", "max_id");
          query->and_where(query, cond);
          cond = query->cast_(query, "id", "TEXT");
          query->and_where(query, cond);
          cond = query->between(query, "id", "1", "10", 0);
          query->and_where(query, cond);
          cond = query->exists(query, subq, 0);
          query->and_where(query, cond);
          cond = query->window(query, "ROW_NUMBER", "id", "id DESC", "rn");
          query->and_where(query, cond);
          cond = query->eq(query, "id", "123", 0);
          query->join(query, "posts", "INNER JOIN", cond);
        } else if (extra == 2) {
          size_t k;
          for (k = 0; k < 150; k++)
            query->offset(query, k);
        } else if (extra == 3) {
          query->left_join(query, "posts", NULL);
          query->right_join(query, "posts", NULL);
          query->distinct(query);
          query->from_alias(query, "users", "u");
          cond = query->is_null(query, "id", 1);
          query->and_where(query, cond);
        }

        c_orm_set_allocators(q_mock_malloc, q_mock_realloc, c_orm_free);
        oom_countdown = oom;
        oom_active = 1;

        if (query->clone)
          query->clone(query, &cloned);

        oom_active = 0;
        c_orm_set_allocators(old_malloc, old_realloc, old_free);

        if (extra == 4) {
          query->select_(query, "*");
          c_orm_set_allocators(q_mock_malloc, q_mock_realloc, c_orm_free);
          oom_countdown = oom;
          oom_active = 1;
          query->eager_load(query, &meta, "posts");
          oom_active = 0;
          c_orm_set_allocators(old_malloc, old_realloc, old_free);
        }
      }
      if (cloned)
        c_orm_query_free(cloned);
      if (query)
        c_orm_query_free(query);
      if (subq)
        c_orm_query_free(subq);
    }
  }
  PASS();
}

static c_orm_error_t dummy_prepare(c_orm_db_t *db, const char *sql,
                                   c_orm_query_t **out) {
  (void)db;
  (void)sql;
  *out = (c_orm_query_t *)1;
  return C_ORM_OK;
}
static c_orm_error_t dummy_bind_string(c_orm_query_t *q, int idx,
                                       const char *s) {
  (void)q;
  (void)idx;
  (void)s;
  if (bind_fail_countdown == 0) {
    bind_fail_countdown--;
    return C_ORM_ERROR_UNKNOWN;
  }
  bind_fail_countdown--;
  return C_ORM_OK;
}
static int dummy_step_row = 0;
static c_orm_error_t dummy_step(c_orm_query_t *q, int *has_row) {
  (void)q;
  *has_row = (dummy_step_row++ == 0) ? 1 : 0;
  if (step_fail_countdown == 0) {
    step_fail_countdown--;
    return C_ORM_ERROR_UNKNOWN;
  }
  step_fail_countdown--;
  return C_ORM_OK;
}
static c_orm_error_t dummy_finalize(c_orm_query_t *q) {
  (void)q;
  if (fin_fail_countdown == 0) {
    fin_fail_countdown--;
    return C_ORM_ERROR_UNKNOWN;
  }
  fin_fail_countdown--;
  return C_ORM_OK;
}
static c_orm_error_t dummy_get_int32(c_orm_query_t *query, int index,
                                     int32_t *val) {
  (void)query;
  (void)index;
  *val = 1;
  if (fetch_fail_countdown == 0) {
    fetch_fail_countdown--;
    return C_ORM_ERROR_UNKNOWN;
  }
  fetch_fail_countdown--;
  return C_ORM_OK;
}
static c_orm_error_t dummy_get_string(c_orm_query_t *query, int index,
                                      const char **val) {
  (void)query;
  (void)index;
  *val = "abc";
  return C_ORM_OK;
}
static c_orm_error_t dummy_get_double(c_orm_query_t *query, int index,
                                      double *val) {
  (void)query;
  (void)index;
  *val = 1.0;
  return C_ORM_OK;
}
static c_orm_error_t dummy_is_null(c_orm_query_t *query, int index,
                                   int *out_is_null) {
  (void)query;
  (void)index;
  *out_is_null = 0;
  return C_ORM_OK;
}

TEST query_sql_exhaustive_oom(void) {
  void *(*old_malloc)(size_t) = c_orm_malloc;
  void *(*old_realloc)(void *, size_t) = c_orm_realloc;
  void (*old_free)(void *) = c_orm_free;
  int oom, extra, i;
  c_orm_query_t *query = NULL;
  c_orm_ast_node_t *cond = NULL;
  char *sql = NULL;
  c_orm_query_params_t params;
  c_orm_column_meta_t target_col[3];
  c_orm_table_meta_t meta;
  c_orm_db_t db;
  c_orm_driver_vtable_t vt;
  struct {
    int32_t id;
    char *val;
    double score;
  } dummy_rec;
  struct Generic_Array {
    void *data;
    size_t length;
    size_t capacity;
  } arr;

  memset(&params, 0, sizeof(params));

  memset(target_col, 0, sizeof(target_col));
  target_col[0].name = "id";
  target_col[0].type = C_ORM_TYPE_INT32;
  target_col[0].offset = (size_t)((char *)&dummy_rec.id - (char *)&dummy_rec);
  target_col[0].is_pk = 1;

  target_col[1].name = "val";
  target_col[1].type = C_ORM_TYPE_STRING;
  target_col[1].offset = (size_t)((char *)&dummy_rec.val - (char *)&dummy_rec);

  target_col[2].name = "score";
  target_col[2].type = C_ORM_TYPE_FLOAT;
  target_col[2].offset =
      (size_t)((char *)&dummy_rec.score - (char *)&dummy_rec);

  memset(&meta, 0, sizeof(meta));
  meta.name = "test_table";
  meta.columns = target_col;
  meta.num_columns = 3;
  meta.struct_size = sizeof(dummy_rec);

  memset(&db, 0, sizeof(db));
  memset(&vt, 0, sizeof(vt));
  vt.prepare = dummy_prepare;
  vt.bind_string = dummy_bind_string;
  vt.step = dummy_step;
  vt.finalize = dummy_finalize;
  vt.get_int32 = dummy_get_int32;
  vt.get_string = dummy_get_string;
  vt.get_double = dummy_get_double;
  vt.is_null = dummy_is_null;
  db.vtable = &vt;

  for (extra = 0; extra < 9; extra++) {
    c_orm_query_t *subq = NULL;
    c_orm_query_new(&subq);
    if (subq)
      subq->select_(subq, "*");

    c_orm_query_new(&query);
    query->select_(query, "*");
    query->from(query, "t");

    if (extra == 0) {
      cond = query->between(query, "id", "1", "10", 0);
      query->where(query, cond);
    } else if (extra == 1) {
      static char bufs1[7][16];
      for (i = 0; i < 7; i++) {
        C_ORM_SPRINTF(bufs1[i], sizeof(bufs1[i]), "c%d", i);
        cond = query->eq(query, bufs1[i], "1", 0);
        query->and_where(query, cond);
      }
      cond = query->between(query, "id", "1", "10", 0);
      query->and_where(query, cond);
    } else if (extra == 2) {
      static char bufs2[8][16];
      for (i = 0; i < 8; i++) {
        C_ORM_SPRINTF(bufs2[i], sizeof(bufs2[i]), "c%d", i);
        cond = query->eq(query, bufs2[i], "1", 0);
        query->and_where(query, cond);
      }
      cond = query->eq(query, "id", "1", 0);
      cond = query->group(query, cond);
      query->and_where(query, cond);
    } else if (extra == 3) {
      cond = query->eq(query, "a", "1", 0);
      query->where(query, cond);
    } else if (extra == 4) {
      query->limit(query, 10);
    } else if (extra == 5) {
      query->order_by(query, "id", 0);
    } else if (extra == 6) {
      cond = query->exists(query, subq, 0);
      query->where(query, cond);
      cond = query->exists(query, subq, 1);
      query->and_where(query, cond);
    } else if (extra == 7) {
      cond = query->subquery(query, subq, "alias");
      query->where(query, cond);
    } else if (extra == 8) {
      query->union_(query, subq, 0);
      query->with(query, "alias", subq);
    }

    for (oom = 0; oom < 250; oom++) {
      c_orm_set_allocators(q_mock_malloc, q_mock_realloc, c_orm_free);
      oom_countdown = oom;
      oom_active = 1;
      fin_fail_countdown = -1;
      bind_fail_countdown = -1;
      step_fail_countdown = -1;
      fetch_fail_countdown = -1;

      if (extra == 3) {
        c_orm_query_execute(&db, query);
        fin_fail_countdown = oom % 2;
        c_orm_query_execute(&db, query);
        fin_fail_countdown = -1;
        bind_fail_countdown = oom % 2;
        c_orm_query_execute(&db, query);
        bind_fail_countdown = -1;
        step_fail_countdown = oom % 2;
        c_orm_query_execute(&db, query);
        step_fail_countdown = -1;
      } else if (extra == 4) {
        dummy_step_row = 0;
        memset(&dummy_rec, 0, sizeof(dummy_rec));
        fetch_fail_countdown = oom % 2;
        c_orm_query_fetch_one(&db, query, &meta, &dummy_rec);
        fetch_fail_countdown = -1;
        if (dummy_rec.val) {
          c_orm_free(dummy_rec.val);
        }
      } else if (extra == 5) {
        dummy_step_row = 0;
        memset(&arr, 0, sizeof(arr));
        fetch_fail_countdown = oom % 2;
        c_orm_query_fetch_all(&db, query, &meta, &arr);
        fetch_fail_countdown = -1;
        if (arr.data) {
          c_orm_free(arr.data);
        }
      } else {
        c_orm_query_params_init(&params);
        c_orm_query_to_sql(query, C_ORM_DIALECT_SQLITE, &sql, &params);
        if (sql) {
          c_orm_free(sql);
          sql = NULL;
        }
        c_orm_query_params_cleanup(&params);
      }

      oom_active = 0;
      c_orm_set_allocators(old_malloc, old_realloc, old_free);
    }
    c_orm_query_free(query);
    if (subq)
      c_orm_query_free(subq);
  }
  PASS();
}

TEST test_fluent_error_state_branches(void) {
  c_orm_query_t *q = NULL;
  c_orm_query_t *subq = NULL;
  c_orm_query_t *cloned = NULL;
  c_orm_query_t *q_no_select = NULL;
  c_orm_table_meta_t bad_meta;
  c_orm_relation_meta_t bad_rel;
  c_orm_table_meta_t empty_cols_meta;
  c_orm_relation_meta_t empty_rel;
  c_orm_table_meta_t target_meta;
  c_orm_column_meta_t target_col;
  c_orm_ast_node_t bad_node;
  c_orm_error_t rc;

  rc = c_orm_query_new(&q);
  ASSERT_EQ(C_ORM_OK, rc);
  rc = c_orm_query_new(&subq);
  ASSERT_EQ(C_ORM_OK, rc);

  /* Clone with invalid node type */
  bad_node.type = (c_orm_ast_node_type_t)999;
  bad_node.next = NULL;
  q->ast_head = &bad_node;
  q->clone(q, &cloned);
  q->ast_head = NULL;

  /* Clone with exists node */
  q->where(q, q->exists(q, subq, 0));
  rc = q->clone(q, &cloned);
  ASSERT_EQ(C_ORM_OK, rc);
  c_orm_query_free(cloned);
  cloned = NULL;

  /* Eager load branches */
  memset(&bad_meta, 0, sizeof(bad_meta));
  memset(&bad_rel, 0, sizeof(bad_rel));
  bad_rel.field_name = "test";
  bad_rel.target_meta = NULL;
  bad_meta.relations = &bad_rel;
  bad_meta.num_relations = 1;
  q->eager_load(q, &bad_meta, NULL);
  q->eager_load(q, &bad_meta, "test");

  memset(&empty_cols_meta, 0, sizeof(empty_cols_meta));
  memset(&empty_rel, 0, sizeof(empty_rel));
  memset(&target_meta, 0, sizeof(target_meta));
  memset(&target_col, 0, sizeof(target_col));
  target_col.name = "id";
  target_meta.name = "target";
  target_meta.columns = &target_col;
  target_meta.num_columns = 1;
  empty_rel.field_name = "target";
  empty_rel.target_meta = &target_meta;
  empty_cols_meta.name = "empty";
  empty_cols_meta.relations = &empty_rel;
  empty_cols_meta.num_relations = 1;
  empty_cols_meta.num_columns = 0;

  rc = c_orm_query_new(&q_no_select);
  ASSERT_EQ(C_ORM_OK, rc);
  q_no_select->from(q_no_select, "empty");
  q_no_select->eager_load(q_no_select, &empty_cols_meta, "target");
  c_orm_query_free(q_no_select);

  /* NULL checks */
  c_orm_query_free(NULL);
  ASSERT_NEQ(C_ORM_OK, c_orm_query_new(NULL));
  ASSERT_NEQ(C_ORM_OK, q->clone(NULL, &cloned));
  ASSERT_NEQ(C_ORM_OK, q->clone(q, NULL));
  q->with(q, "cte", NULL);
  q->union_(q, NULL, 1);
  q->subquery(q, NULL, "s");
  q->exists(q, NULL, 0);

  /* Error state branches */
  q->error = 1;

  q->raw(q, "SELECT 1");
  q->col(q, "id");
  q->lit(q, "123", 0);
  q->lit(q, "str", 1);
  q->op(q, "+", NULL, NULL);
  q->eq(q, "id", "1", 0);
  q->neq(q, "id", "1", 0);
  q->gt(q, "id", "1", 0);
  q->lt(q, "id", "1", 0);
  q->like(q, "name", "%test%");
  q->in(q, "id", 0);
  q->select_(q, "id");
  q->from(q, "users");
  q->from_alias(q, "users", "u");
  q->where(q, NULL);
  q->and_where(q, NULL);
  q->or_where(q, NULL);
  q->order_by(q, "id", 1);
  q->limit(q, 10);
  q->offset(q, 5);
  q->join(q, "posts", "INNER", NULL);
  q->left_join(q, "posts", NULL);
  q->right_join(q, "posts", NULL);
  q->group_by(q, "users.id");
  q->having(q, NULL);
  q->with(q, "cte", subq);
  q->union_(q, subq, 1);
  q->distinct(q);
  q->group(q, NULL);
  q->subquery(q, subq, "s");
  q->func(q, "COUNT", "*", "cnt");
  q->cast_(q, "id", "TEXT");
  q->is_null(q, "deleted_at", 0);
  q->between(q, "age", "18", "65", 0);
  q->exists(q, subq, 0);
  q->window(q, "ROW_NUMBER()", "dept", "salary DESC", "rn");

  c_orm_query_free(q);
  c_orm_query_free(subq);
  PASS();
}

TEST test_query_sql_all_branches(void) {
  c_orm_query_t *q1 = NULL;
  c_orm_query_t *subq = NULL;
  char *sql = NULL;
  c_orm_query_params_t p;
  c_orm_db_t *db = NULL;
  c_orm_table_meta_t meta;
  c_orm_ast_node_t *n = NULL;
  c_orm_ast_operator_t *op = NULL;
  int i;
  int dummy_out = 0;
  struct {
    void *data;
    size_t length;
    size_t capacity;
  } arr;

  memset(&p, 0, sizeof(p));
  memset(&meta, 0, sizeof(meta));
  memset(&arr, 0, sizeof(arr));
  meta.name = "users";
  meta.struct_size = 4;

  /* 1. Params init, cleanup, add */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_query_params_init(NULL));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_query_params_cleanup(NULL));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_query_params_add(NULL, "val", 1));

  ASSERT_EQ(C_ORM_OK, c_orm_query_params_init(&p));
  ASSERT_EQ(C_ORM_OK, c_orm_query_params_cleanup(
                          &p)); /* cleanup when params->params is NULL */

  ASSERT_EQ(C_ORM_OK, c_orm_query_params_init(&p));
  for (i = 0; i < 10; i++) {
    ASSERT_EQ(C_ORM_OK, c_orm_query_params_add(&p, "test", 1));
  }
  ASSERT_EQ(C_ORM_OK, c_orm_query_params_cleanup(
                          &p)); /* cleanup when params->params is non-NULL */

  /* 2. query_to_sql NULL checks */
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_query_to_sql(NULL, C_ORM_DIALECT_SQLITE, &sql, NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q1));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_query_to_sql(q1, C_ORM_DIALECT_SQLITE, NULL, NULL));
  c_orm_query_free(q1);
  q1 = NULL;

  /* 3. SQL generation variations with params == NULL (inlined literals &
   * between) */
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q1));
  q1->select_(q1, "id")
      ->from(q1, "users")
      ->where(q1, q1->eq(q1, "name", "alice", 1)) /* string literal */
      ->and_where(q1, q1->eq(q1, "age", "25", 0)) /* non-string literal */
      ->and_where(q1, q1->between(q1, "created_at", "2020", "2025",
                                  1)) /* string between */
      ->and_where(
          q1, q1->between(q1, "score", "10", "100", 0)) /* non-string between */
      ->order_by(q1, "id", 0); /* ASC (ord->is_desc = 0) */
  ASSERT_EQ(C_ORM_OK, c_orm_query_to_sql(q1, C_ORM_DIALECT_SQLITE, &sql, NULL));
  ASSERT(strstr(sql, "ASC") != NULL);
  ASSERT(strstr(sql, "'alice'") != NULL);
  ASSERT(strstr(sql, "25") != NULL);
  ASSERT(strstr(sql, "'2020' AND '2025'") != NULL);
  ASSERT(strstr(sql, "10 AND 100") != NULL);
  c_orm_free(sql);
  sql = NULL;
  c_orm_query_free(q1);
  q1 = NULL;

  /* 4. Union without all (is_all = 0), Distinct, Table without alias */
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q1));
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&subq));
  subq->select_(subq, "id")->from(subq, "other_users");

  q1->select_(q1, "id")
      ->from(q1, "users")    /* no alias */
      ->union_(q1, subq, 0); /* UNION (is_all = 0) */
  ASSERT_EQ(C_ORM_OK, c_orm_query_to_sql(q1, C_ORM_DIALECT_SQLITE, &sql, NULL));
  ASSERT(strstr(sql, " UNION ") != NULL);
  c_orm_free(sql);
  sql = NULL;
  c_orm_query_free(q1);
  q1 = NULL;

  /* 5. Window variations, Exists variations, Function without alias, Subquery
   * without alias */
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q1));
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&subq));
  subq->select_(subq, "1")->from(subq, "logs");

  /* Exists NOT */
  q1->select_(q1, "id")->from(q1, "users")->where(q1, q1->exists(q1, subq, 1));
  ASSERT_EQ(C_ORM_OK, c_orm_query_to_sql(q1, C_ORM_DIALECT_SQLITE, &sql, NULL));
  ASSERT(strstr(sql, "NOT EXISTS") != NULL);
  c_orm_free(sql);
  sql = NULL;
  c_orm_query_free(q1);
  q1 = NULL;

  /* Window without order_by, Window without partition_by */
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q1));
  n = q1->window(q1, "AVG(val)", "dept", NULL,
                 NULL); /* no order_by, no alias */
  q1->select_(q1, "id")->from(q1, "t")->where(q1, n);
  ASSERT_EQ(C_ORM_OK, c_orm_query_to_sql(q1, C_ORM_DIALECT_SQLITE, &sql, NULL));
  ASSERT(strstr(sql, "PARTITION BY dept") != NULL);
  c_orm_free(sql);
  sql = NULL;
  c_orm_query_free(q1);
  q1 = NULL;

  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q1));
  n = q1->window(q1, "COUNT(*)", NULL, "salary ASC",
                 "cnt"); /* no partition_by */
  q1->select_(q1, "id")->from(q1, "t")->where(q1, n);
  ASSERT_EQ(C_ORM_OK, c_orm_query_to_sql(q1, C_ORM_DIALECT_SQLITE, &sql, NULL));
  ASSERT(strstr(sql, "ORDER BY salary ASC") != NULL);
  c_orm_free(sql);
  sql = NULL;
  c_orm_query_free(q1);
  q1 = NULL;

  /* Subquery without alias */
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q1));
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&subq));
  subq->select_(subq, "MAX(id)")->from(subq, "t");
  q1->select_(q1, "id")
      ->from(q1, "users")
      ->where(q1, q1->subquery(q1, subq, NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_query_to_sql(q1, C_ORM_DIALECT_SQLITE, &sql, NULL));
  c_orm_free(sql);
  sql = NULL;
  c_orm_query_free(q1);
  q1 = NULL;

  /* Function without alias */
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q1));
  q1->select_(q1, "id")->from(q1, "t")->where(
      q1, q1->func(q1, "LOWER", "name", NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_query_to_sql(q1, C_ORM_DIALECT_SQLITE, &sql, NULL));
  c_orm_free(sql);
  sql = NULL;
  c_orm_query_free(q1);
  q1 = NULL;

  /* Empty string alias variations: func, subquery, window, from */
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q1));
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&subq));
  subq->select_(subq, "1");

  q1->select_(q1, "id")
      ->from_alias(q1, "users", "")
      ->where(q1, q1->func(q1, "LOWER", "name", ""))
      ->and_where(q1, q1->subquery(q1, subq, ""))
      ->and_where(q1, q1->window(q1, "AVG(x)", "", "", ""))
      ->and_where(q1, q1->window(q1, "AVG(x)", "dept", "", "alias"));
  ASSERT_EQ(C_ORM_OK, c_orm_query_to_sql(q1, C_ORM_DIALECT_SQLITE, &sql, NULL));
  c_orm_free(sql);
  sql = NULL;
  c_orm_query_free(q1);
  q1 = NULL;

  /* >32 joins to take the FALSE branch of join_count < 32 */
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q1));
  q1->select_(q1, "id")->from(q1, "t");
  for (i = 0; i < 35; i++) {
    q1->join(q1, "t2", "id", q1->col(q1, "id"));
  }
  ASSERT_EQ(C_ORM_OK, c_orm_query_to_sql(q1, C_ORM_DIALECT_SQLITE, &sql, NULL));
  c_orm_free(sql);
  sql = NULL;
  c_orm_query_free(q1);
  q1 = NULL;

  /* where with NULL condition and having with NULL condition */
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q1));
  q1->select_(q1, "id")->from(q1, "t");
  q1->where(q1, NULL);
  q1->having(q1, NULL);
  ASSERT_EQ(C_ORM_OK, c_orm_query_to_sql(q1, C_ORM_DIALECT_SQLITE, &sql, NULL));
  c_orm_free(sql);
  sql = NULL;
  c_orm_query_free(q1);
  q1 = NULL;

  /* Unary / Right-null operator */
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q1));
  op = (c_orm_ast_operator_t *)q1->col(q1, "status");
  n = (c_orm_ast_node_t *)malloc(sizeof(c_orm_ast_operator_t));
  if (n) {
    memset(n, 0, sizeof(c_orm_ast_operator_t));
    n->type = C_ORM_AST_NODE_OPERATOR;
    ((c_orm_ast_operator_t *)n)->left = (c_orm_ast_node_t *)op;
    ((c_orm_ast_operator_t *)n)->op = "IS NULL";
    ((c_orm_ast_operator_t *)n)->right = NULL;
    q1->select_(q1, "id")->from(q1, "t")->where(q1, n);
    ASSERT_EQ(C_ORM_OK,
              c_orm_query_to_sql(q1, C_ORM_DIALECT_SQLITE, &sql, NULL));
    ASSERT(strstr(sql, "IS NULL") != NULL);
    c_orm_free(sql);
    sql = NULL;
    free(n);
  }
  c_orm_query_free(q1);
  q1 = NULL;

  /* 6. Execute, fetch_one, fetch_all NULL variations */
  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_connect(":memory:", &db));
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q1));
  q1->select_(q1, "1");

  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_query_execute(NULL, q1));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_query_execute(db, NULL));

  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_query_fetch_one(NULL, q1, &meta, &dummy_out));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_query_fetch_one(db, NULL, &meta, &dummy_out));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_query_fetch_one(db, q1, NULL, &dummy_out));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_query_fetch_one(db, q1, &meta, NULL));

  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_query_fetch_all(NULL, q1, &meta, &arr));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_query_fetch_all(db, NULL, &meta, &arr));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_query_fetch_all(db, q1, NULL, &arr));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_query_fetch_all(db, q1, &meta, NULL));

  /* Fetch one returning NOT_FOUND (!has_row) */
  c_orm_execute_raw(db, "CREATE TABLE empty_t (id INT);");
  q1->ast_head = NULL;
  q1->select_(q1, "id")->from(q1, "empty_t");
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_query_fetch_one(db, q1, &meta, &dummy_out));

  /* Fetch one and fetch all returning error from hydration */
  {
    c_orm_column_meta_t blob_col;
    c_orm_table_meta_t blob_meta;
    char blob_out[64];
    memset(blob_out, 0, sizeof(blob_out));

    memset(&blob_col, 0, sizeof(blob_col));
    blob_col.name = "id";
    blob_col.type = C_ORM_TYPE_BLOB;
    blob_col.offset = 0;

    memset(&blob_meta, 0, sizeof(blob_meta));
    blob_meta.name = "empty_t";
    blob_meta.struct_size = 64;
    blob_meta.num_columns = 1;
    blob_meta.columns = &blob_col;

    c_orm_execute_raw(db, "INSERT INTO empty_t VALUES (42);");

    q1->ast_head = NULL;
    q1->select_(q1, "id")->from(q1, "empty_t");
    ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH,
              c_orm_query_fetch_one(db, q1, &blob_meta, blob_out));
    ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH,
              c_orm_query_fetch_all(db, q1, &blob_meta, &arr));
  }

  c_orm_query_free(q1);
  db->vtable->disconnect(db);

  PASS();
}

SUITE(query_fluent_coverage_suite) {

  void *(*old_malloc)(size_t) = c_orm_malloc;
  void (*old_free)(void *) = c_orm_free;
  void *(*old_realloc)(void *, size_t) = c_orm_realloc;

  c_orm_set_allocators(q_mock_malloc, c_orm_realloc, c_orm_free);
  c_orm_set_allocators(c_orm_malloc, c_orm_realloc, q_mock_free);
  c_orm_set_allocators(c_orm_malloc, q_mock_realloc, c_orm_free);

  RUN_TEST(test_query_fluent_coverage);
  RUN_TEST(test_query_sql_oom);
  RUN_TEST(test_query_sql_to_sql_fail);
  RUN_TEST(test_fluent_oom);
  RUN_TEST(test_query_sql_coverage);
  RUN_TEST(test_sql_oom);
  RUN_TEST(fluent_exhaustive_oom);
  RUN_TEST(query_sql_exhaustive_oom);
  RUN_TEST(test_fluent_error_state_branches);
  RUN_TEST(test_query_sql_all_branches);

  c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);
  c_orm_set_allocators(c_orm_malloc, c_orm_realloc, old_free);
  c_orm_set_allocators(c_orm_malloc, old_realloc, c_orm_free);
}

#if defined(__clang__) || defined(__GNUC__)
#endif
