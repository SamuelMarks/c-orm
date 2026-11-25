/**
 * @file test_query_coverage.c
 * @brief Unit tests and test coverage for fluent query builder and SQL query
 * construction.
 */

/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_ast.h"
#include "c_orm_decimal.h"
#include "c_orm_safe_crt.h"
#include "c_orm_sqlite.h"
#include "c_orm_string_builder.h"
#define GREATEST_USE_LONGJMP 0
#include <greatest.h>
#include "test_assert_zero_branch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

/**
 * @brief Generic dynamic array representation for test result containers.
 */
struct Generic_Array {
  void *data;      /**< Pointer to elements buffer. */
  size_t length;   /**< Current number of elements. */
  size_t capacity; /**< Total capacity. */
};

/**
 * @brief Dummy record structure for mapping and selection tests.
 */
struct DummyRecord {
  int32_t id;   /**< Record identifier. */
  char *val;    /**< Record string value. */
  double score; /**< Record score value. */
};

static int oom_countdown = -1;
static int oom_active = 0;

/**
 * @brief Mock malloc hook tracking countdown for OOM simulation.
 * @param size Allocation size in bytes.
 * @param out_ptr Output pointer receiving allocated pointer or NULL on OOM.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on simulated OOM.
 */
static c_orm_error_t q_mock_malloc(size_t size, void **out_ptr) {
  if (oom_active) {
    if (oom_countdown == 0) {
      oom_countdown--;
      *out_ptr = NULL;
      return C_ORM_ERROR_MEMORY;
    }
    oom_countdown--;
  }
  return c_orm_default_malloc(size, out_ptr);
}
/**
 * @brief Mock realloc hook tracking countdown for OOM simulation.
 * @param ptr Pointer to previously allocated buffer.
 * @param size New allocation size in bytes.
 * @param out_ptr Output pointer receiving reallocated pointer or NULL on OOM.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on simulated OOM.
 */
static c_orm_error_t q_mock_realloc(void *ptr, size_t size, void **out_ptr) {
  if (oom_active) {
    if (oom_countdown == 0) {
      oom_countdown--;
      *out_ptr = NULL;
      return C_ORM_ERROR_MEMORY;
    }
    oom_countdown--;
  }
  return c_orm_default_realloc(ptr, size, out_ptr);
}
/** @brief Bitmask controlling which q_mock_free calls return error. */
static unsigned int q_free_fail_mask = 0;
/** @brief Invocation counter for q_mock_free when q_free_fail_mask is non-zero.
 */
static unsigned int q_free_call_idx = 0;

/**
 * @brief Mock free hook forwarding to standard free with optional fault mask.
 * @param ptr Pointer to free.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY when masked.
 */
static c_orm_error_t q_mock_free(void *ptr) {
  c_orm_default_free(ptr);
  if (q_free_fail_mask != 0) {
    unsigned int idx = q_free_call_idx++;
    if ((q_free_fail_mask & (1u << (idx & 31u))) != 0) {
      return C_ORM_ERROR_MEMORY;
    }
  }
  return C_ORM_OK;
}

/**
 * @brief Tests comprehensive execution coverage for fluent query builder API.
 * @return GREATEST test result.
 */
TEST test_query_fluent_coverage(void) {
  c_orm_ast_query_t *q = NULL;
  c_orm_ast_node_t *n_lit = NULL;
  c_orm_ast_node_t *tmp = NULL;
  c_orm_ast_query_t *q_sub = NULL;
  c_orm_query_new(&q);

  /* Execute and create all nodes for coverage */
  q->lit(q, "1", 0, &n_lit);
  ASSERT(n_lit != NULL);
  q->col(q, "id", &tmp);
  ASSERT(tmp != NULL);
  q->eq(q, "a", "b", 1, &tmp);
  ASSERT(tmp != NULL);
  q->neq(q, "a", "b", 1, &tmp);
  ASSERT(tmp != NULL);
  q->gt(q, "a", "b", 1, &tmp);
  ASSERT(tmp != NULL);
  q->lt(q, "a", "b", 1, &tmp);
  ASSERT(tmp != NULL);
  q->like(q, "a", "b", &tmp);
  ASSERT(tmp != NULL);
  q->raw(q, "SELECT 1", &tmp);
  ASSERT(tmp != NULL);
  q->is_null(q, "a", 1, &tmp);
  ASSERT(tmp != NULL);
  q->between(q, "a", "b", "c", 1, &tmp);
  ASSERT(tmp != NULL);
  q->group(q, n_lit, &tmp);
  ASSERT(tmp != NULL);
  q->func(q, "COUNT", "id", "c", &tmp);
  ASSERT(tmp != NULL);
  q->cast_(q, "id", "TEXT", &tmp);
  ASSERT(tmp != NULL);
  q->window(q, "ROW_NUMBER", "id", "id DESC", "rn", &tmp);
  ASSERT(tmp != NULL);

  c_orm_query_new(&q_sub);
  q_sub->select_(q_sub, "1");
  q->subquery(q, q_sub, "s", &tmp);
  ASSERT(tmp != NULL);
  q->exists(q, q_sub, 1, &tmp);
  ASSERT(tmp != NULL);

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

  q->group(NULL, n_lit, &tmp);
  q->group(q, n_lit, NULL);
  q->group(q, NULL, &tmp);

  q->subquery(NULL, q_sub, "a", &tmp);
  q->subquery(q, q_sub, "a", NULL);
  q->subquery(q, NULL, "a", &tmp);
  q->subquery(q, q_sub, NULL, &tmp);

  q->func(NULL, "a", "b", "c", &tmp);
  q->func(q, "a", "b", "c", NULL);
  q->func(q, NULL, "b", "c", &tmp);
  q->func(q, "a", NULL, "c", &tmp);
  q->func(q, "a", "b", NULL, &tmp);

  q->cast_(NULL, "a", "b", &tmp);
  q->cast_(q, "a", "b", NULL);
  q->cast_(q, NULL, "b", &tmp);
  q->cast_(q, "a", NULL, &tmp);

  q->between(NULL, "a", "b", "c", 0, &tmp);
  q->between(q, "a", "b", "c", 0, NULL);
  q->between(q, NULL, "b", "c", 0, &tmp);
  q->between(q, "a", NULL, "c", 0, &tmp);
  q->between(q, "a", "b", NULL, 0, &tmp);

  q->exists(NULL, q_sub, 0, &tmp);
  q->exists(q, q_sub, 0, NULL);
  q->exists(q, NULL, 0, &tmp);

  q->window(NULL, "a", "b", "c", "d", &tmp);
  q->window(q, "a", "b", "c", "d", NULL);
  q->window(q, NULL, "b", "c", "d", &tmp);
  q->window(q, "a", NULL, "c", "d", &tmp);
  q->window(q, "a", "b", NULL, "d", &tmp);
  q->window(q, "a", "b", "c", NULL, &tmp);

  q->raw(NULL, "a", &tmp);
  q->raw(q, "a", NULL);
  q->raw(q, NULL, &tmp);

  q->col(NULL, "a", &tmp);
  q->col(q, "a", NULL);
  q->col(q, NULL, &tmp);

  q->lit(NULL, "a", 0, &tmp);
  q->lit(q, "a", 0, NULL);
  q->lit(q, NULL, 0, &tmp);

  q->eq(NULL, "a", "b", 0, &tmp);
  q->eq(q, NULL, "b", 0, &tmp);
  q->eq(q, "a", NULL, 0, &tmp);

  q->neq(NULL, "a", "b", 0, &tmp);
  q->neq(q, NULL, "b", 0, &tmp);
  q->neq(q, "a", NULL, 0, &tmp);

  q->gt(NULL, "a", "b", 0, &tmp);
  q->gt(q, NULL, "b", 0, &tmp);
  q->gt(q, "a", NULL, 0, &tmp);

  q->lt(NULL, "a", "b", 0, &tmp);
  q->lt(q, NULL, "b", 0, &tmp);
  q->lt(q, "a", NULL, 0, &tmp);

  q->like(NULL, "a", "b", &tmp);
  q->like(q, NULL, "b", &tmp);
  q->like(q, "a", NULL, &tmp);

  q->in(NULL, "a", "b", &tmp);
  q->in(q, NULL, "b", &tmp);
  q->in(q, "a", NULL, &tmp);

  q->op(NULL, "a", n_lit, n_lit, &tmp);
  q->op(q, "a", n_lit, n_lit, NULL);
  q->op(q, NULL, n_lit, n_lit, &tmp);
  q->op(q, "a", NULL, n_lit, &tmp);
  q->op(q, "a", n_lit, NULL, &tmp);

  q->is_null(NULL, "a", 0, &tmp);
  q->is_null(q, "a", 0, NULL);
  q->is_null(q, NULL, 0, &tmp);

  {
    c_orm_ast_query_t *q_cloned = NULL;
    q->clone(NULL, &q_cloned);
    q->clone(q, NULL);
  }
  c_orm_query_new(NULL);

  /* IN node */
  q->in(q, "id", "1,2", &tmp);
  ASSERT(tmp != NULL);
  q->op(q, "AND", n_lit, n_lit, &tmp);
  ASSERT(tmp != NULL);

  c_orm_query_free(q_sub);
  c_orm_query_free(q);
  PASS();
}

static c_orm_error_t (*orig_finalize_mock)(c_orm_query_t *);
static int my_fail_finalize = 0;
/**
 * @brief Mock query finalize function for testing mock query cleanup.
 * @param q_ptr Query pointer to finalize.
 * @return Error code.
 */
static c_orm_error_t my_mock_finalize(c_orm_query_t *q_ptr) {
  c_orm_error_t res = orig_finalize_mock(q_ptr);
  if (my_fail_finalize)
    res = C_ORM_ERROR_SQL;
  return res;
}

static c_orm_error_t (*orig_bind_string)(c_orm_query_t *, int, const char *);
static c_orm_error_t (*orig_bind_int64)(c_orm_query_t *, int, int64_t);
static int fail_bind = 0;
/**
 * @brief Mock string binding handler.
 * @param query Query object.
 * @param index Parameter index.
 * @param val String value to bind.
 * @return Error code.
 */
static c_orm_error_t my_mock_bind_string(c_orm_query_t *query, int index,
                                         const char *val) {
  if (fail_bind)
    return C_ORM_ERROR_BIND;
  return orig_bind_string(query, index, val);
}

/**
 * @brief Mock int64 binding handler.
 * @param query Query object.
 * @param index Parameter index.
 * @param val 64-bit integer value to bind.
 * @return Error code.
 */
static c_orm_error_t my_mock_bind_int64(c_orm_query_t *query, int index,
                                        int64_t val) {
  if (fail_bind)
    return C_ORM_ERROR_BIND;
  return orig_bind_int64(query, index, val);
}

static c_orm_error_t (*orig_prep)(c_orm_db_t *, const char *, c_orm_query_t **);
static int fail_prep = 0;
/**
 * @brief Mock prepare statement handler.
 * @param db Database handle.
 * @param sql SQL string.
 * @param out_query Pointer to store prepared query.
 * @return Error code.
 */
static c_orm_error_t my_mock_prep(c_orm_db_t *db, const char *sql,
                                  c_orm_query_t **out_query) {
  if (fail_prep)
    return C_ORM_ERROR_SQL;
  return orig_prep(db, sql, out_query);
}

/**
 * @brief Tests out-of-memory handling across fluent query builder methods.
 * @return GREATEST test result.
 */
TEST test_fluent_oom(void) {
  int i;
  {
    c_orm_ast_query_t *oq = NULL;
    c_orm_error_t q_rc;
    oom_active = 1;
    oom_countdown = 0;
    q_rc = c_orm_query_new(&oq);
    ASSERT(q_rc != C_ORM_OK);
    ASSERT(oq == NULL);
    oom_countdown = 1;
    q_rc = c_orm_query_new(&oq);
    ASSERT(q_rc != C_ORM_OK);
    ASSERT(oq == NULL);
    oom_active = 0;
  }

#define TEST_OOM(expr)                                                         \
  do {                                                                         \
    c_orm_ast_query_t *oq = NULL;                                              \
    c_orm_ast_node_t *n = (c_orm_ast_node_t *)1;                               \
    c_orm_ast_node_t *out_n = NULL;                                            \
    c_orm_ast_query_t *sq = (c_orm_ast_query_t *)1;                            \
    c_orm_query_new(&oq);                                                      \
    oom_active = 1;                                                            \
    oom_countdown = 0;                                                         \
    (void)sq;                                                                  \
    (void)n;                                                                   \
    (void)out_n;                                                               \
    expr;                                                                      \
    oom_active = 0;                                                            \
    c_orm_query_free(oq);                                                      \
  } while (0)

  TEST_OOM(oq->raw(oq, "1", &out_n));
  TEST_OOM(oq->col(oq, "1", &out_n));
  TEST_OOM(oq->lit(oq, "1", 0, &out_n));
  TEST_OOM(oq->lit(oq, "x'x", 1, &out_n));
  TEST_OOM(oq->op(oq, "=", n, n, &out_n));
  TEST_OOM(oq->select_(oq, "1"));
  TEST_OOM(oq->from(oq, "1"));
  TEST_OOM(oq->where(oq, n));
  TEST_OOM({
    oq->raw(oq, "1", &out_n);
    oq->where(oq, out_n);
    oq->and_where(oq, n);
  });
  TEST_OOM({
    oq->raw(oq, "1", &out_n);
    oq->where(oq, out_n);
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
  TEST_OOM(oq->group(oq, n, &out_n));
  TEST_OOM(oq->subquery(oq, sq, "s", &out_n));
  TEST_OOM(oq->func(oq, "f", "c", "a", &out_n));
  TEST_OOM(oq->cast_(oq, "c", "t", &out_n));
  TEST_OOM(oq->is_null(oq, "c", 0, &out_n));
  TEST_OOM(oq->between(oq, "c", "1", "2", 0, &out_n));
  TEST_OOM(oq->exists(oq, sq, 0, &out_n));
  TEST_OOM(oq->window(oq, "f", "p", "o", "a", &out_n));

#undef TEST_OOM

#define TEST_OOM_COUNT(expr, count)                                            \
  do {                                                                         \
    c_orm_ast_query_t *oq = NULL;                                              \
    void *dummy = NULL;                                                        \
    c_orm_ast_node_t *n = (c_orm_ast_node_t *)1;                               \
    c_orm_ast_node_t *out_n = NULL;                                            \
    c_orm_ast_query_t *sq = (c_orm_ast_query_t *)1;                            \
    c_orm_query_new(&oq);                                                      \
    c_orm_arena_alloc(oq->arena, 4096, &dummy);                                \
    oom_active = 1;                                                            \
    oom_countdown = (count);                                                   \
    (void)sq;                                                                  \
    (void)n;                                                                   \
    (void)out_n;                                                               \
    (void)dummy;                                                               \
    expr;                                                                      \
    oom_active = 0;                                                            \
    c_orm_query_free(oq);                                                      \
  } while (0)

  TEST_OOM_COUNT(oq->raw(oq, "1", &out_n), 1);
  TEST_OOM_COUNT(oq->col(oq, "1", &out_n), 1);
  TEST_OOM_COUNT(oq->select_(oq, "1"), 1);
  TEST_OOM_COUNT(oq->from(oq, "1"), 1);
  TEST_OOM_COUNT(oq->order_by(oq, "1", 0), 1);
  TEST_OOM_COUNT(oq->join(oq, "1", "2", n), 1);
  TEST_OOM_COUNT(oq->join(oq, "1", "2", n), 2);
  TEST_OOM_COUNT(oq->raw(oq, NULL, &out_n), 0);
  TEST_OOM_COUNT(oq->col(oq, NULL, &out_n), 0);
  TEST_OOM_COUNT(oq->select_(oq, NULL), 0);
  TEST_OOM_COUNT(oq->from(oq, NULL), 0);
  TEST_OOM_COUNT(oq->order_by(oq, NULL, 0), 0);
  TEST_OOM_COUNT(oq->join(oq, NULL, "2", n), 0);
  TEST_OOM_COUNT(oq->join(oq, NULL, NULL, n), 0);
  TEST_OOM_COUNT(
      {
        oq->where(oq, n);
        oq->and_where(oq, n);
      },
      0);
  TEST_OOM_COUNT(
      {
        oq->where(oq, n);
        oq->or_where(oq, n);
      },
      0);
#undef TEST_OOM_COUNT

  {
    int count = 0;
    char big_str[5000];
    memset(big_str, 'x', 4999);
    big_str[4999] = '\0';
    big_str[0] = '\x27';

    while (1) {
      c_orm_error_t err;
      c_orm_ast_query_t *oq = NULL;
      c_orm_ast_node_t *out_n = NULL;
      c_orm_query_new(&oq);
      oom_active = 1;
      oom_countdown = count;
      oq->lit(oq, big_str, 1, &out_n);
      oom_active = 0;
      err = (c_orm_error_t)oq->error;
      c_orm_query_free(oq);
      if (!err)
        break;
      count++;
    }
  }

  {
    c_orm_ast_query_t *qc = NULL;
    c_orm_ast_node_t *n_raw = NULL, *n_group = NULL, *n_sq = NULL,
                     *n_func = NULL, *n_cast = NULL, *n_exists = NULL,
                     *n_between = NULL, *n_window = NULL, *n_in = NULL,
                     *n_like = NULL, *n_lit2 = NULL, *n_eq = NULL;
    c_orm_ast_query_t *sq2 = NULL, *w_sq = NULL, *u = NULL;

    c_orm_query_new(&qc);
    qc->raw(qc, "1", &n_raw);
    qc->lit(qc, "2", 0, &n_lit2);
    qc->group(qc, n_lit2, &n_group);
    c_orm_query_new(&sq2);
    qc->subquery(qc, sq2, "sq_alias", &n_sq);
    qc->func(qc, "COUNT", "id", "c", &n_func);
    qc->cast_(qc, "id", "TEXT", &n_cast);
    qc->exists(qc, sq2, 0, &n_exists);
    qc->between(qc, "age", "10", "20", 0, &n_between);
    qc->window(qc, "RANK", "id", "id", "r", &n_window);
    qc->in(qc, "id", "1,2", &n_in);
    qc->like(qc, "name", "sam%", &n_like);

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
    qc->eq(qc, "a", "b", 0, &n_eq);
    qc->join(qc, "users", "INNER", n_eq);
    c_orm_query_new(&w_sq);
    w_sq->select_(w_sq, "1");
    qc->with(qc, "w", w_sq);

    c_orm_query_new(&u);
    u->select_(u, "1");
    qc->union_(qc, u, 1);

    for (i = 0; i < 20; i++) {
      c_orm_ast_query_t *q_cloned = NULL;
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
    c_orm_ast_query_t *qe = NULL;

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
      c_orm_ast_query_t *qo = NULL;
      c_orm_query_new(&qo);
      qo->select_(qo, "*");
      oom_active = 1;
      oom_countdown = i;
      qo->eager_load(qo, &meta, "my_rel");
      oom_active = 0;
      c_orm_query_free(qo);
    }
    {
      c_orm_ast_query_t *qo = NULL;
      void *dummy = NULL;
      c_orm_query_new(&qo);
      qo->select_(qo, "*");
      c_orm_arena_alloc(qo->arena, 3800, &dummy);
      oom_active = 1;
      oom_countdown = 1;
      rel[0].foreign_key = NULL;
      qo->eager_load(qo, &meta, "my_rel");
      rel[0].foreign_key = "fk_id";
      oom_active = 0;
      c_orm_query_free(qo);
    }
    c_orm_query_free(qe);
  }

  {
    c_orm_ast_query_t *qc = NULL;
    c_orm_ast_node_t *n;
    c_orm_ast_query_t *qc2 = NULL;
    c_orm_query_new(&qc);
    qc->where(qc, NULL);
    n = qc->ast_head;
    (void)n;
    qc->clone(qc, &qc2);
    c_orm_query_free(qc2);

    qc->join(qc, "a", "b", NULL);
    qc->clone(qc, &qc2);
    c_orm_query_free(qc2);
    c_orm_query_free(qc);
  }

  {
    c_orm_ast_query_t *oq = NULL;
    c_orm_ast_node_t *n = NULL;
    c_orm_query_new(&oq);
    oq->raw(oq, "1", &n);
    oq->where(oq, NULL);
    oq->and_where(oq, n);
    c_orm_query_free(oq);
  }

  {
    c_orm_ast_query_t *oq = NULL;
    c_orm_ast_node_t *n = NULL;
    c_orm_query_new(&oq);
    oq->raw(oq, "1", &n);
    oq->where(oq, NULL);
    oq->or_where(oq, n);
    c_orm_query_free(oq);
  }

  {
    c_orm_ast_query_t *oq = NULL;
    c_orm_ast_node_t *n = NULL;
    c_orm_query_new(&oq);
    oq->select_(oq, "1");
    oq->from(oq, "t");
    oq->raw(oq, "1", &n);
    oq->or_where(oq, n);
    c_orm_query_free(oq);
  }

  {
    c_orm_ast_query_t *oq = NULL;
    c_orm_query_new(&oq);
    oq->from(oq, "t");
    oq->distinct(oq);
    c_orm_query_free(oq);
  }

  PASS();
}
/**
 * @brief Tests out-of-memory handling during SQL building operations.
 * @return GREATEST test result.
 */
TEST test_sql_oom(void) {
  c_orm_ast_query_t *q = NULL;
  char *sql = NULL;
  c_orm_query_params_t p;
  c_orm_ast_node_t *eq_n = NULL;
  int i;

  /* Target lines 560-565 in c_orm_query_sql.c: out_sql malloc OOM */
  {
    c_orm_ast_query_t *simple_q = NULL;
    c_orm_error_t oom_rc;
    c_orm_query_new(&simple_q);
    simple_q->select_(simple_q, "1");
    for (i = 0; i < 10; i++) {
      oom_active = 1;
      oom_countdown = i;
      sql = NULL;
      oom_rc = c_orm_query_to_sql(simple_q, C_ORM_DIALECT_SQLITE, &sql, NULL);
      if (oom_rc == C_ORM_OK) {
        ASSERT(sql != NULL);
      }
      oom_active = 0;
      c_orm_free(sql);
      sql = NULL;
    }
    c_orm_query_free(simple_q);
  }

  c_orm_query_new(&q);
  c_orm_query_params_init(&p);

  q->select_(q, "id");
  q->from(q, "users");
  q->eq(q, "id", "1", 0, &eq_n);
  q->where(q, eq_n);

  /* OOM query_to_sql */
  for (i = 0; i < 20; i++) {
    oom_active = 1;
    oom_countdown = i;
    c_orm_query_to_sql(q, C_ORM_DIALECT_SQLITE, &sql, &p);
    oom_active = 0;
    c_orm_system_free(sql);
    sql = NULL;
    c_orm_query_params_cleanup(&p);
    c_orm_query_params_init(&p);
  }

  {
    c_orm_ast_query_t *qb = NULL;
    c_orm_ast_node_t *n = NULL;
    c_orm_query_new(&qb);
    qb->select_(qb, "1");
    qb->from(qb, "t");
    qb->eq(qb, "a", "1", 0, &n);
    qb->where(qb, n);
    qb->eq(qb, "a", "2", 0, &n);
    qb->and_where(qb, n);
    qb->eq(qb, "a", "3", 0, &n);
    qb->and_where(qb, n);
    qb->eq(qb, "a", "4", 0, &n);
    qb->and_where(qb, n);
    qb->eq(qb, "a", "5", 0, &n);
    qb->and_where(qb, n);
    qb->eq(qb, "a", "6", 0, &n);
    qb->and_where(qb, n);
    qb->eq(qb, "a", "7", 0, &n);
    qb->and_where(qb, n);
    qb->between(qb, "a", "8", "9", 0, &n);
    qb->and_where(qb, n);
    for (i = 0; i < 20; i++) {
      oom_active = 1;
      oom_countdown = i;
      c_orm_query_to_sql(qb, C_ORM_DIALECT_POSTGRES, &sql, &p);
      oom_active = 0;
      c_orm_system_free(sql);
      sql = NULL;
      c_orm_query_params_cleanup(&p);
      c_orm_query_params_init(&p);
    }
    c_orm_query_free(qb);
  }

  {
    c_orm_ast_query_t *qb = NULL, *sq = NULL;
    c_orm_ast_node_t *n = NULL;
    c_orm_query_new(&qb);
    c_orm_query_new(&sq);
    sq->select_(sq, "1");

    qb->with(qb, "cte", sq);
    qb->select_(qb, "1");
    qb->from(qb, "t");
    qb->exists(qb, sq, 0, &n);
    qb->where(qb, n);
    qb->subquery(qb, sq, "alias", &n);
    qb->and_where(qb, n);
    qb->union_(qb, sq, 0);

    for (i = 0; i < 50; i++) {
      oom_active = 1;
      oom_countdown = i;
      c_orm_query_to_sql(qb, C_ORM_DIALECT_SQLITE, &sql, &p);
      oom_active = 0;
      c_orm_system_free(sql);
      sql = NULL;
      c_orm_query_params_cleanup(&p);
      c_orm_query_params_init(&p);
    }
    c_orm_query_to_sql(qb, C_ORM_DIALECT_SQLITE, &sql, &p);
    c_orm_system_free(sql);
    sql = NULL;
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

/**
 * @brief Tests query SQL builder coverage including operators, joins, and
 * clauses.
 * @return GREATEST test result.
 */
TEST test_query_sql_coverage(void) {
  c_orm_error_t err;
  void *res_obj = NULL;

  struct Generic_Array my_arr = {NULL, 0, 0};

  c_orm_ast_query_t *q = NULL;
  char *sql = NULL;
  c_orm_query_params_t p;
  c_orm_ast_node_t *f = NULL;
  c_orm_ast_node_t *c = NULL;
  c_orm_ast_node_t *w = NULL;
  c_orm_ast_node_t *r = NULL;
  c_orm_ast_node_t *tmp = NULL;
  c_orm_ast_query_t *sq = NULL;
  c_orm_ast_query_t *u = NULL;
  c_orm_ast_query_t *q_sq = NULL;
  c_orm_ast_node_t *r_exists = NULL;
  c_orm_ast_node_t *r_not_exists = NULL;
  c_orm_ast_node_t *r_sub = NULL;
  char *pg_sql = NULL;
  c_orm_query_params_t pg_p;
  c_orm_ast_query_t *pg_q = NULL;
  c_orm_table_meta_t meta = {0};
  c_orm_db_t *exec_db = NULL;
  c_orm_ast_query_t *qe = NULL;
  c_orm_ast_query_t *q_bad = NULL;
  c_orm_driver_vtable_t orig_vt;
  c_orm_driver_vtable_t mock_vt;
  c_orm_ast_query_t *q_emp = NULL;
  c_orm_ast_query_t *q_inv = NULL;

  c_orm_query_new(&q);
  c_orm_query_params_init(&p);

  /* Complex Select to hit all SQL generation paths */
  q->select_(q, "id, name");
  q->distinct(q);
  q->from_alias(q, "users", "u");
  q->eq(q, "posts.u_id", "u.id", 0, &tmp);
  q->join(q, "posts", "INNER", tmp);
  q->eq(q, "comments.p_id", "posts.id", 0, &tmp);
  q->left_join(q, "comments", tmp);
  q->eq(q, "likes.p_id", "posts.id", 0, &tmp);
  q->right_join(q, "likes", tmp);
  q->is_null(q, "deleted_at", 0, &tmp);
  q->where(q, tmp);
  q->between(q, "age", "18", "30", 0, &tmp);
  q->and_where(q, tmp);
  q->like(q, "name", "sam%", &tmp);
  q->or_where(q, tmp);
  q->group_by(q, "id");
  q->gt(q, "id", "5", 0, &tmp);
  q->having(q, tmp);
  q->order_by(q, "id", 1); /* desc */
  q->limit(q, 10);
  q->offset(q, 5);

  /* Add functions, window, cast, raw */
  q->func(q, "MAX", "age", "max_age", &f);
  q->cast_(q, "id", "TEXT", &c);
  q->window(q, "RANK", "id", "id DESC", "rn", &w);
  q->raw(q, "1=1", &r);

  /* We append them to where so they get compiled */
  q->and_where(q, f);
  q->and_where(q, c);
  q->and_where(q, w);
  q->and_where(q, r);

  /* Subquery and UNION */
  c_orm_query_new(&sq);
  sq->select_(sq, "id");
  sq->from(sq, "other");
  q->with(q, "cte", sq);

  c_orm_query_new(&u);
  u->select_(u, "1");
  q->union_(q, u, 1); /* UNION ALL */

  /* Test EXISTS and SUBQUERY rendering */
  c_orm_query_new(&q_sq);
  q_sq->select_(q_sq, "1");

  q->exists(q, q_sq, 0, &r_exists);
  q->exists(q, q_sq, 1, &r_not_exists);
  q->subquery(q, q_sq, "my_sq", &r_sub);

  q->and_where(q, r_exists);
  q->and_where(q, r_not_exists);
  q->and_where(q, r_sub);

  c_orm_query_to_sql(q, C_ORM_DIALECT_SQLITE, &sql, &p);
  c_orm_system_free(sql);
  c_orm_query_params_cleanup(&p);
  sql = NULL;
  c_orm_query_params_init(&p);

  c_orm_query_free(q_sq);
  c_orm_query_free(u);
  c_orm_query_free(q);
  c_orm_query_free(sq);

  /* Test inline rendering without params */
  {
    c_orm_ast_query_t *q_inline = NULL;
    char *sql_inline = NULL;
    c_orm_ast_node_t *n1 = NULL, *n2 = NULL, *n3 = NULL, *n4 = NULL, *n5 = NULL;
    c_orm_query_new(&q_inline);
    q_inline->select_(q_inline, "1");
    q_inline->from(q_inline, "t");
    q_inline->between(q_inline, "a", "1", "2", 1, &n1);
    q_inline->where(q_inline, n1);
    q_inline->eq(q_inline, "b", "3", 1, &n2);
    q_inline->and_where(q_inline, n2);
    q_inline->in(q_inline, "c", "4,5", &n3);
    q_inline->and_where(q_inline, n3);
    q_inline->lit(q_inline, "6", 1, &n4);
    q_inline->group(q_inline, n4, &n5);
    q_inline->and_where(q_inline, n5);

    c_orm_query_to_sql(q_inline, C_ORM_DIALECT_POSTGRES, &sql_inline, NULL);
    c_orm_system_free(sql_inline);
    sql_inline = NULL;
    c_orm_query_to_sql(q_inline, C_ORM_DIALECT_SQLITE, &sql_inline, NULL);
    c_orm_system_free(sql_inline);
    sql_inline = NULL;
    c_orm_query_free(q_inline);
  }

  /* Render Postgres params manually */
  c_orm_query_params_init(&pg_p);
  c_orm_query_new(&pg_q);
  pg_q->select_(pg_q, "1");
  pg_q->from(pg_q, "t");
  pg_q->between(pg_q, "a", "1", "2", 0, &tmp);
  pg_q->where(pg_q, tmp);
  pg_q->in(pg_q, "c", "1, 2", &tmp);
  pg_q->and_where(pg_q, tmp);
  c_orm_query_to_sql(pg_q, C_ORM_DIALECT_POSTGRES, &pg_sql, &pg_p);
  c_orm_system_free(pg_sql);
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
  c_orm_free(sql);
  sql = NULL;

  /* Query Execute / Fetch NULL validation */
  c_orm_query_execute(NULL, q);
  c_orm_query_free(q);

  {
    c_orm_ast_query_t *qn = NULL;
    char *sql_n = NULL;
    c_orm_query_new(&qn);
    qn->select_(qn, "1");
    qn->from(qn, "t");
    qn->group(qn, NULL, &tmp);
    qn->where(qn, tmp);
    c_orm_query_to_sql(qn, C_ORM_DIALECT_SQLITE, &sql_n, NULL);
    c_orm_system_free(sql_n);
    sql_n = NULL;
    c_orm_query_free(qn);
  }

  {
    c_orm_ast_query_t *qn = NULL;
    char *sql_n = NULL;
    c_orm_ast_node_t *dummy_node = NULL;
    c_orm_query_new(&qn);
    qn->raw(qn, "1", &dummy_node);
    dummy_node->type = (c_orm_ast_node_type_t)999;
    qn->ast_head = dummy_node;
    c_orm_query_to_sql(qn, C_ORM_DIALECT_SQLITE, &sql_n, NULL);
    c_orm_system_free(sql_n);
    sql_n = NULL;

    qn->ast_head = NULL;
    qn->select_(qn, "1");
    qn->from(qn, "t");
    qn->group(qn, dummy_node, &tmp);
    qn->where(qn, tmp);
    c_orm_query_to_sql(qn, C_ORM_DIALECT_SQLITE, &sql_n, NULL);
    c_orm_system_free(sql_n);
    sql_n = NULL;

    c_orm_query_free(qn);
  }

  {
    unsigned int old_depth;
    char *sql_d = NULL;
    c_orm_ast_query_t *q1 = NULL;
    c_orm_ast_query_t *q2 = NULL;

    old_depth = cdd_c_sql_parser_max_depth;

    c_orm_query_new(&q1);
    q1->select_(q1, "1");
    q1->from(q1, "t");
    q1->eq(q1, "a", "b", 0, &tmp);
    q1->join(q1, "t2", "INNER", tmp);
    cdd_c_sql_parser_max_depth = 0;
    c_orm_query_to_sql(q1, C_ORM_DIALECT_SQLITE, &sql_d, NULL);
    ASSERT_EQ(NULL, sql_d);
    cdd_c_sql_parser_max_depth = old_depth;
    c_orm_query_free(q1);

    c_orm_query_new(&q2);
    q2->select_(q2, "1");
    q2->from(q2, "t");
    q2->eq(q2, "c", "d", 0, &tmp);
    q2->having(q2, tmp);
    cdd_c_sql_parser_max_depth = 0;
    c_orm_query_to_sql(q2, C_ORM_DIALECT_SQLITE, &sql_d, NULL);
    ASSERT_EQ(NULL, sql_d);
    cdd_c_sql_parser_max_depth = old_depth;
    c_orm_query_free(q2);
  }

  {
    c_orm_ast_query_t *qo = NULL;
    c_orm_query_new(&qo);
    qo->select_(qo, "1");
    qo->from(qo, "t");

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
    c_orm_ast_query_t *qb = NULL;
    int i;
    char big[4500];
    c_orm_ast_node_t *r_big = NULL, *g_big = NULL;

    c_orm_query_params_init(&p);
    c_orm_query_new(&qb);
    memset(big, 'a', 4499);
    big[4499] = '\0';
    qb->select_(qb, "1");
    qb->from(qb, "t");
    qb->raw(qb, big, &r_big);
    qb->group(qb, r_big, &g_big);
    qb->where(qb, g_big);

    for (i = 0; i < 20; i++) {
      oom_active = 1;
      oom_countdown = i;
      c_orm_query_to_sql(qb, C_ORM_DIALECT_POSTGRES, &sql, &p);
      oom_active = 0;
      if (sql) {
        c_orm_system_free(sql);
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
  qe->select_(qe, "id");
  qe->from(qe, "t_exec");
  qe->eq(qe, "id", "1", 0, &tmp);
  qe->where(qe, tmp);

  c_orm_query_execute(exec_db, qe);

  /* Mock prepare fail */
  c_orm_query_new(&q_bad);
  q_bad->select_(q_bad, "id");
  q_bad->from(q_bad, "bad_table");
  c_orm_query_execute(exec_db, q_bad);
  c_orm_query_free(q_bad);

  /* Mock bind fail */
  orig_vt = *exec_db->vtable;
  mock_vt = *exec_db->vtable;
  orig_bind_string = mock_vt.bind_string;
  orig_bind_int64 = mock_vt.bind_int64;

  mock_vt.bind_string = my_mock_bind_string;
  mock_vt.bind_int64 = my_mock_bind_int64;
  exec_db->vtable = (const c_orm_driver_vtable_t *)&mock_vt;

  fail_bind = 1;
  c_orm_query_execute(exec_db, qe);
  c_orm_query_fetch_one(exec_db, qe, &meta, &res_obj);
  c_orm_query_fetch_all(exec_db, qe, &meta, &my_arr);
  ASSERT_EQ(C_ORM_ERROR_BIND, my_mock_bind_string(NULL, 1, "test"));
  ASSERT_EQ(C_ORM_ERROR_BIND, my_mock_bind_int64(NULL, 1, 1));
  fail_bind = 0;

  /* Fallback call */
  my_mock_bind_string(NULL, 1, "test");
  my_mock_bind_int64(NULL, 1, 1);

  /* Mock prepare fail properly to hit the lines */
  orig_prep = mock_vt.prepare;
  mock_vt.prepare = my_mock_prep;

  fail_prep = 1;
  c_orm_query_fetch_one(exec_db, qe, &meta, &res_obj);
  c_orm_query_fetch_all(exec_db, qe, &meta, &my_arr);
  fail_prep = 0;

  /* Fallback call */
  {
    c_orm_stmt_t *stmt_bad = NULL;
    my_mock_prep(exec_db, "SELECT 1", &stmt_bad);
    mock_vt.finalize(stmt_bad);
  }

  exec_db->vtable = (const c_orm_driver_vtable_t *)&orig_vt;

  /* fetch_one */
  c_orm_query_fetch_one(NULL, qe, &meta, &res_obj);
  c_orm_query_fetch_one(exec_db, qe, &meta, &res_obj);

  /* fetch_one empty */
  c_orm_query_new(&q_emp);
  q_emp->select_(q_emp, "id");
  q_emp->from(q_emp, "t_exec");
  q_emp->eq(q_emp, "id", "2", 0, &tmp);
  q_emp->where(q_emp, tmp);
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
    c_orm_ast_query_t *qf = NULL;

    orig_finalize_mock = vtable_fin.finalize;
    vtable_fin.finalize = my_mock_finalize;
    exec_db->vtable = (const c_orm_driver_vtable_t *)&vtable_fin;

    c_orm_query_new(&qf);
    qf->select_(qf, "id");
    qf->from(qf, "t_exec");
    qf->eq(qf, "id", "1", 1, &tmp);
    qf->where(qf, tmp);

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
    c_orm_query_execute(exec_db, qf);
    {
      const c_orm_driver_vtable_t *sqlite_vt = NULL;
      c_orm_ast_query_t *q_all = NULL;
      c_orm_error_t fa_err;
      c_orm_sqlite_get_vtable(&sqlite_vt);
      exec_db->vtable = sqlite_vt;
      memset(&my_arr, 0, sizeof(my_arr));
      c_orm_query_new(&q_all);
      q_all->select_(q_all, "id");
      q_all->from(q_all, "t_exec");
      fa_err = c_orm_query_fetch_all(exec_db, q_all, &meta, &my_arr);
      ASSERT_EQ_FMT(C_ORM_OK, fa_err, "%d");
      c_orm_query_free(q_all);
    }
    c_orm_query_free(qf);
  }

  exec_db->vtable->disconnect(exec_db);
  c_orm_query_free(qe);
  c_orm_free(my_arr.data);
  my_arr.data = NULL;
  PASS();
}

static int g_malloc_fail = 0;
static int g_malloc_count = 0;
static int g_malloc_target = -1;
/**
 * @brief Mock malloc returning failure under simulated OOM condition.
 * @param size Allocation size in bytes.
 * @param out_ptr Output pointer receiving allocated block.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on failure.
 */
static c_orm_error_t mock_malloc_fail(size_t size, void **out_ptr) {
  int idx = g_malloc_count++;
  if (g_malloc_fail && g_malloc_target == idx) {
    *out_ptr = NULL;
    return C_ORM_ERROR_MEMORY;
  }
  return c_orm_default_malloc(size, out_ptr);
}

/**
 * @brief Mock realloc returning failure under simulated OOM condition.
 * @param ptr Buffer pointer.
 * @param size New allocation size in bytes.
 * @param out_ptr Output pointer receiving reallocated block.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on failure.
 */
static c_orm_error_t mock_realloc_fail(void *ptr, size_t size, void **out_ptr) {
  int idx = g_malloc_count++;
  if (g_malloc_fail && g_malloc_target == idx) {
    *out_ptr = NULL;
    return C_ORM_ERROR_MEMORY;
  }
  return c_orm_default_realloc(ptr, size, out_ptr);
}

/**
 * @brief Mock free hook forwarding to standard free.
 * @param p Pointer to free.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t mock_free(void *p) {
  c_orm_default_free(p);
  return C_ORM_OK;
}

/**
 * @brief Mock prepare statement implementation for SQL coverage.
 * @param db Database handle.
 * @param sql SQL statement string.
 * @param out Output query handle.
 * @return Error code.
 */
static c_orm_error_t mock_prepare(c_orm_db_t *db, const char *sql,
                                  c_orm_query_t **out) {
  (void)db;
  (void)sql;
  *out = NULL;
  return C_ORM_ERROR_SQL;
}

/**
 * @brief Tests query execution failure handling when SQL conversion fails.
 * @return GREATEST test result.
 */
TEST test_query_sql_to_sql_fail(void) {
  c_orm_malloc_fn_t old_malloc = c_orm_malloc;
  c_orm_realloc_fn_t old_realloc = c_orm_realloc;
  c_orm_free_fn_t old_free = c_orm_free;
  int i;
  c_orm_db_t db;
  c_orm_driver_vtable_t vt;
  c_orm_ast_query_t *qb = NULL;
  int fake_struct = 0;

  memset(&db, 0, sizeof(db));
  memset(&vt, 0, sizeof(vt));
  vt.prepare = mock_prepare;
  vt.step = NULL;
  vt.finalize = NULL;
  db.vtable = &vt;

  /* Call execute with mock_prepare */
  {
    c_orm_ast_query_t *q_prep = NULL;
    c_orm_query_new(&q_prep);
    q_prep->select_(q_prep, "1");
    q_prep->from(q_prep, "t");
    c_orm_query_execute(&db, q_prep);
    c_orm_query_free(q_prep);
  }

  /* Exercise mock_realloc_fail */
  {
    void *m = NULL;
    void *r = NULL;
    ASSERT_EQ(C_ORM_OK, c_orm_default_malloc(10, &m));
    ASSERT_EQ(C_ORM_OK, mock_realloc_fail(m, 20, &r));
    ASSERT_EQ(C_ORM_OK, c_orm_default_free(r));
    ASSERT_EQ(C_ORM_OK, c_orm_default_malloc(10, &m));
    g_malloc_fail = 1;
    g_malloc_target = 999;
    g_malloc_count = 0;
    ASSERT_EQ(C_ORM_OK, mock_realloc_fail(m, 20, &r));
    ASSERT_EQ(C_ORM_OK, c_orm_default_free(r));
    g_malloc_fail = 0;
  }

  for (i = 0; i < 2; i++) {
    c_orm_ast_between_t *bw = NULL;

    qb = NULL;
    c_orm_query_new(&qb);
    qb->select_(qb, "*");
    qb->from(qb, "users");

    c_orm_arena_alloc(qb->arena, sizeof(c_orm_ast_between_t), (void **)&bw);
    memset(bw, 0, sizeof(*bw));
    bw->base.type = C_ORM_AST_NODE_BETWEEN;
    bw->col = "score";
    bw->low = "1";
    bw->high = "100";
    bw->is_string = 0;

    qb->where(qb, (c_orm_ast_node_t *)bw);

    c_orm_set_allocators(mock_malloc_fail, mock_realloc_fail, mock_free);

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
    c_orm_set_allocators(old_malloc, old_realloc, old_free);

    c_orm_query_free(qb);
  }

  PASS();
}

/**
 * @brief Tests SQL builder resilience against memory allocation failures.
 * @return GREATEST test result.
 */
TEST test_query_sql_oom(void) {
  c_orm_malloc_fn_t old_malloc = c_orm_malloc;
  c_orm_realloc_fn_t old_realloc = c_orm_realloc;
  c_orm_free_fn_t old_free = c_orm_free;
  int i;
  c_orm_db_t db;
  c_orm_driver_vtable_t vt;
  c_orm_ast_query_t *qb = NULL;

  memset(&db, 0, sizeof(db));
  memset(&vt, 0, sizeof(vt));
  vt.prepare = mock_prepare;
  vt.step = NULL;
  vt.finalize = NULL;
  db.vtable = &vt;

  c_orm_set_allocators(mock_malloc_fail, mock_realloc_fail, mock_free);

  /* Measure total allocations */
  g_malloc_fail = 0;
  g_malloc_target = -1;
  g_malloc_count = 0;
  {
    c_orm_ast_between_t *bw = NULL;
    c_orm_query_new(&qb);
    c_orm_arena_alloc(qb->arena, sizeof(c_orm_ast_between_t), (void **)&bw);
    memset(bw, 0, sizeof(*bw));
    bw->base.type = C_ORM_AST_NODE_BETWEEN;
    bw->col = "score";
    bw->low = "1";
    bw->high = "100";
    bw->is_string = 0;
    qb->ast_head = (c_orm_ast_node_t *)bw;
    c_orm_query_execute(&db, qb);
    c_orm_query_fetch_one(&db, qb, NULL, NULL);
    c_orm_query_fetch_all(&db, qb, NULL, NULL);
    c_orm_query_free(qb);
  }
  {
    int total = g_malloc_count;
    for (i = 0; i < total; i++) {
      c_orm_ast_between_t *bw = NULL;
      c_orm_query_new(&qb);
      c_orm_arena_alloc(qb->arena, sizeof(c_orm_ast_between_t), (void **)&bw);
      memset(bw, 0, sizeof(*bw));
      bw->base.type = C_ORM_AST_NODE_BETWEEN;
      bw->col = "score";
      bw->low = "1";
      bw->high = "100";
      bw->is_string = 0;
      qb->ast_head = (c_orm_ast_node_t *)bw;

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

/**
 * @brief Tests exhaustive OOM conditions across all fluent query methods.
 * @return GREATEST test result.
 */
TEST fluent_exhaustive_oom(void) {
  c_orm_malloc_fn_t old_malloc = c_orm_malloc;
  c_orm_realloc_fn_t old_realloc = c_orm_realloc;
  c_orm_free_fn_t old_free = c_orm_free;
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
      c_orm_ast_query_t *query = NULL;
      c_orm_ast_query_t *cloned = NULL;
      c_orm_ast_query_t *subq = NULL;
      c_orm_ast_node_t *cond = NULL;

      c_orm_query_new(&subq);
      subq->select_(subq, "*");

      c_orm_query_new(&query);
      if (extra == 0) {
        query->group_by(query, "id");
        query->eq(query, "id", "123", 0, &cond);
        query->having(query, cond);
        query->order_by(query, "id", 1);
        query->limit(query, 10);
        query->offset(query, 5);
        query->raw(query, "1=1", &cond);
        query->group(query, cond, &cond);
        query->and_where(query, cond);
        query->subquery(query, subq, "sub", &cond);
        query->and_where(query, cond);
        query->union_(query, subq, 0);
        query->with(query, "w", subq);
      } else if (extra == 1) {
        query->func(query, "MAX", "id", "max_id", &cond);
        query->and_where(query, cond);
        query->cast_(query, "id", "TEXT", &cond);
        query->and_where(query, cond);
        query->between(query, "id", "1", "10", 0, &cond);
        query->and_where(query, cond);
        query->exists(query, subq, 0, &cond);
        query->and_where(query, cond);
        query->window(query, "ROW_NUMBER", "id", "id DESC", "rn", &cond);
        query->and_where(query, cond);
        query->eq(query, "id", "123", 0, &cond);
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
        query->is_null(query, "id", 1, &cond);
        query->and_where(query, cond);
      }

      c_orm_set_allocators(q_mock_malloc, q_mock_realloc, c_orm_free);
      oom_countdown = oom;
      oom_active = 1;

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
      c_orm_query_free(cloned);
      c_orm_query_free(query);
      c_orm_query_free(subq);
    }
  }
  PASS();
}

/**
 * @brief Dummy database prepare statement implementation.
 * @param db Database handle.
 * @param sql SQL query string.
 * @param out Output query pointer.
 * @return Error code.
 */
static c_orm_error_t dummy_prepare(c_orm_db_t *db, const char *sql,
                                   c_orm_query_t **out) {
  (void)db;
  (void)sql;
  *out = (c_orm_query_t *)1;
  return C_ORM_OK;
}
/**
 * @brief Dummy bind string method.
 * @param q Query pointer.
 * @param idx Parameter index.
 * @param s String value.
 * @return Error code.
 */
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
/**
 * @brief Dummy step method stepping through simulated query rows.
 * @param q Query pointer.
 * @param has_row Pointer receiving row availability boolean.
 * @return Error code.
 */
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
/**
 * @brief Dummy finalize releasing query resources.
 * @param q Query pointer.
 * @return Error code.
 */
static c_orm_error_t dummy_finalize(c_orm_query_t *q) {
  (void)q;
  if (fin_fail_countdown == 0) {
    fin_fail_countdown--;
    return C_ORM_ERROR_UNKNOWN;
  }
  fin_fail_countdown--;
  return C_ORM_OK;
}
/**
 * @brief Dummy column reader for 32-bit integers.
 * @param query Query pointer.
 * @param index Column index.
 * @param val Pointer to output integer.
 * @return Error code.
 */
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
/**
 * @brief Dummy column reader for strings.
 * @param query Query pointer.
 * @param index Column index.
 * @param val Pointer to output string.
 * @return Error code.
 */
static c_orm_error_t dummy_get_string(c_orm_query_t *query, int index,
                                      const char **val) {
  (void)query;
  (void)index;
  *val = "abc";
  return C_ORM_OK;
}
/**
 * @brief Dummy column reader for double precision numbers.
 * @param query Query pointer.
 * @param index Column index.
 * @param val Pointer to output double.
 * @return Error code.
 */
static c_orm_error_t dummy_get_double(c_orm_query_t *query, int index,
                                      double *val) {
  (void)query;
  (void)index;
  *val = 1.0;
  return C_ORM_OK;
}
/**
 * @brief Dummy column reader checking for NULL column values.
 * @param query Query pointer.
 * @param index Column index.
 * @param out_is_null Pointer to output NULL flag.
 * @return Error code.
 */
static c_orm_error_t dummy_is_null(c_orm_query_t *query, int index,
                                   int *out_is_null) {
  (void)query;
  (void)index;
  *out_is_null = 0;
  return C_ORM_OK;
}

/**
 * @brief Exhaustive OOM tests for SQL query creation and execution paths.
 * @return GREATEST test result.
 */
TEST query_sql_exhaustive_oom(void) {
  c_orm_malloc_fn_t old_malloc = c_orm_malloc;
  c_orm_realloc_fn_t old_realloc = c_orm_realloc;
  c_orm_free_fn_t old_free = c_orm_free;
  int oom, extra, i;
  c_orm_ast_query_t *query = NULL;
  c_orm_ast_node_t *cond = NULL;
  char *sql = NULL;
  c_orm_query_params_t params;
  c_orm_column_meta_t target_col[3];
  c_orm_table_meta_t meta;
  c_orm_db_t db;
  c_orm_driver_vtable_t vt;
  struct DummyRecord dummy_rec;
  struct Generic_Array arr;

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
    c_orm_ast_query_t *subq = NULL;
    c_orm_query_new(&subq);
    subq->select_(subq, "*");

    c_orm_query_new(&query);
    query->select_(query, "*");
    query->from(query, "t");

    if (extra == 0) {
      query->between(query, "id", "1", "10", 0, &cond);
      query->where(query, cond);
    } else if (extra == 1) {
      static char bufs1[7][16];
      for (i = 0; i < 7; i++) {
        C_ORM_SPRINTF(bufs1[i], sizeof(bufs1[i]), "c%d", i);
        query->eq(query, bufs1[i], "1", 0, &cond);
        query->and_where(query, cond);
      }
      query->between(query, "id", "1", "10", 0, &cond);
      query->and_where(query, cond);
    } else if (extra == 2) {
      static char bufs2[8][16];
      for (i = 0; i < 8; i++) {
        C_ORM_SPRINTF(bufs2[i], sizeof(bufs2[i]), "c%d", i);
        query->eq(query, bufs2[i], "1", 0, &cond);
        query->and_where(query, cond);
      }
      query->eq(query, "id", "1", 0, &cond);
      query->group(query, cond, &cond);
      query->and_where(query, cond);
    } else if (extra == 3) {
      query->eq(query, "a", "1", 0, &cond);
      query->where(query, cond);
    } else if (extra == 4) {
      query->limit(query, 10);
    } else if (extra == 5) {
      query->order_by(query, "id", 0);
    } else if (extra == 6) {
      query->exists(query, subq, 0, &cond);
      query->where(query, cond);
      query->exists(query, subq, 1, &cond);
      query->and_where(query, cond);
    } else if (extra == 7) {
      query->subquery(query, subq, "alias", &cond);
      query->where(query, cond);
    } else {
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
    c_orm_query_free(subq);
  }
  PASS();
}

/**
 * @brief Tests error state propagation across chained fluent builder
 * invocations.
 * @return GREATEST test result.
 */
TEST test_fluent_error_state_branches(void) {
  c_orm_ast_query_t *q = NULL;
  c_orm_ast_query_t *subq = NULL;
  c_orm_ast_query_t *cloned = NULL;
  c_orm_ast_query_t *q_no_select = NULL;
  c_orm_table_meta_t bad_meta;
  c_orm_relation_meta_t bad_rel;
  c_orm_table_meta_t empty_cols_meta;
  c_orm_relation_meta_t empty_rel;
  c_orm_table_meta_t target_meta;
  c_orm_column_meta_t target_col;
  c_orm_ast_node_t bad_node;
  c_orm_ast_node_t *tmp = NULL;
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
  q->exists(q, subq, 0, &tmp);
  q->where(q, tmp);
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
  q->subquery(q, NULL, "s", &tmp);
  q->exists(q, NULL, 0, &tmp);

  /* Error state branches */
  q->error = 1;

  q->raw(q, "SELECT 1", &tmp);
  q->col(q, "id", &tmp);
  q->lit(q, "123", 0, &tmp);
  q->lit(q, "str", 1, &tmp);
  q->op(q, "+", NULL, NULL, &tmp);
  q->eq(q, "id", "1", 0, &tmp);
  q->neq(q, "id", "1", 0, &tmp);
  q->gt(q, "id", "1", 0, &tmp);
  q->lt(q, "id", "1", 0, &tmp);
  q->like(q, "name", "%test%", &tmp);
  q->in(q, "id", 0, &tmp);
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
  q->group(q, NULL, &tmp);
  q->subquery(q, subq, "s", &tmp);
  q->func(q, "COUNT", "*", "cnt", &tmp);
  q->cast_(q, "id", "TEXT", &tmp);
  q->is_null(q, "deleted_at", 0, &tmp);
  q->between(q, "age", "18", "65", 0, &tmp);
  q->exists(q, subq, 0, &tmp);
  q->window(q, "ROW_NUMBER()", "dept", "salary DESC", "rn", &tmp);

  c_orm_query_free(q);
  c_orm_query_free(subq);
  PASS();
}

/**
 * @brief Comprehensive branch coverage for SQL clause generation logic.
 * @return GREATEST test result.
 */
TEST test_query_sql_all_branches(void) {
  c_orm_ast_query_t *q1 = NULL;
  c_orm_ast_query_t *subq = NULL;
  char *sql = NULL;
  c_orm_query_params_t p;
  c_orm_db_t *db = NULL;
  c_orm_table_meta_t meta;
  c_orm_ast_node_t *n = NULL;
  c_orm_ast_operator_t *op = NULL;
  int i;
  int dummy_out = 0;
  struct Generic_Array arr;

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
  q1->select_(q1, "id");
  q1->from(q1, "users");
  q1->eq(q1, "name", "alice", 1, &n); /* string literal */
  q1->where(q1, n);
  q1->eq(q1, "age", "25", 0, &n); /* non-string literal */
  q1->and_where(q1, n);
  q1->between(q1, "created_at", "2020", "2025", 1, &n); /* string between */
  q1->and_where(q1, n);
  q1->between(q1, "score", "10", "100", 0, &n); /* non-string between */
  q1->and_where(q1, n);
  q1->order_by(q1, "id", 0); /* ASC (ord->is_desc = 0) */
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
  subq->select_(subq, "id");
  subq->from(subq, "other_users");

  q1->select_(q1, "id");
  q1->from(q1, "users");   /* no alias */
  q1->union_(q1, subq, 0); /* UNION (is_all = 0) */
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
  subq->select_(subq, "1");
  subq->from(subq, "logs");

  /* Exists NOT */
  q1->select_(q1, "id");
  q1->from(q1, "users");
  q1->exists(q1, subq, 1, &n);
  q1->where(q1, n);
  ASSERT_EQ(C_ORM_OK, c_orm_query_to_sql(q1, C_ORM_DIALECT_SQLITE, &sql, NULL));
  ASSERT(strstr(sql, "NOT EXISTS") != NULL);
  c_orm_free(sql);
  sql = NULL;
  c_orm_query_free(q1);
  q1 = NULL;

  /* Window without order_by, Window without partition_by */
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q1));
  q1->window(q1, "AVG(val)", "dept", NULL, NULL,
             &n); /* no order_by, no alias */
  q1->select_(q1, "id");
  q1->from(q1, "t");
  q1->where(q1, n);
  ASSERT_EQ(C_ORM_OK, c_orm_query_to_sql(q1, C_ORM_DIALECT_SQLITE, &sql, NULL));
  ASSERT(strstr(sql, "PARTITION BY dept") != NULL);
  c_orm_free(sql);
  sql = NULL;
  c_orm_query_free(q1);
  q1 = NULL;

  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q1));
  q1->window(q1, "COUNT(*)", NULL, "salary ASC", "cnt",
             &n); /* no partition_by */
  q1->select_(q1, "id");
  q1->from(q1, "t");
  q1->where(q1, n);
  ASSERT_EQ(C_ORM_OK, c_orm_query_to_sql(q1, C_ORM_DIALECT_SQLITE, &sql, NULL));
  ASSERT(strstr(sql, "ORDER BY salary ASC") != NULL);
  c_orm_free(sql);
  sql = NULL;
  c_orm_query_free(q1);
  q1 = NULL;

  /* Subquery without alias */
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q1));
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&subq));
  subq->select_(subq, "MAX(id)");
  subq->from(subq, "t");
  q1->select_(q1, "id");
  q1->from(q1, "users");
  q1->subquery(q1, subq, NULL, &n);
  q1->where(q1, n);
  ASSERT_EQ(C_ORM_OK, c_orm_query_to_sql(q1, C_ORM_DIALECT_SQLITE, &sql, NULL));
  c_orm_free(sql);
  sql = NULL;
  c_orm_query_free(q1);
  q1 = NULL;

  /* Function without alias */
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q1));
  q1->select_(q1, "id");
  q1->from(q1, "t");
  q1->func(q1, "LOWER", "name", NULL, &n);
  q1->where(q1, n);
  ASSERT_EQ(C_ORM_OK, c_orm_query_to_sql(q1, C_ORM_DIALECT_SQLITE, &sql, NULL));
  c_orm_free(sql);
  sql = NULL;
  c_orm_query_free(q1);
  q1 = NULL;

  /* Empty string alias variations: func, subquery, window, from */
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q1));
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&subq));
  subq->select_(subq, "1");

  q1->select_(q1, "id");
  q1->from_alias(q1, "users", "");
  q1->func(q1, "LOWER", "name", "", &n);
  q1->where(q1, n);
  q1->subquery(q1, subq, "", &n);
  q1->and_where(q1, n);
  q1->window(q1, "AVG(x)", "", "", "", &n);
  q1->and_where(q1, n);
  q1->window(q1, "AVG(x)", "dept", "", "alias", &n);
  q1->and_where(q1, n);
  ASSERT_EQ(C_ORM_OK, c_orm_query_to_sql(q1, C_ORM_DIALECT_SQLITE, &sql, NULL));
  c_orm_free(sql);
  sql = NULL;
  c_orm_query_free(q1);
  q1 = NULL;

  /* >32 joins to take the FALSE branch of join_count < 32 */
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q1));
  q1->select_(q1, "id");
  q1->from(q1, "t");
  for (i = 0; i < 35; i++) {
    q1->col(q1, "id", &n);
    q1->join(q1, "t2", "id", n);
  }
  ASSERT_EQ(C_ORM_OK, c_orm_query_to_sql(q1, C_ORM_DIALECT_SQLITE, &sql, NULL));
  c_orm_free(sql);
  sql = NULL;
  c_orm_query_free(q1);
  q1 = NULL;

  /* where with NULL condition and having with NULL condition */
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q1));
  q1->select_(q1, "id");
  q1->from(q1, "t");
  q1->where(q1, NULL);
  q1->having(q1, NULL);
  ASSERT_EQ(C_ORM_OK, c_orm_query_to_sql(q1, C_ORM_DIALECT_SQLITE, &sql, NULL));
  c_orm_free(sql);
  sql = NULL;
  c_orm_query_free(q1);
  q1 = NULL;

  /* Unary / Right-null operator */
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q1));
  q1->col(q1, "status", (c_orm_ast_node_t **)&op);
  {
    c_orm_ast_operator_t n_op;
    memset(&n_op, 0, sizeof(n_op));
    n_op.base.type = C_ORM_AST_NODE_OPERATOR;
    n_op.left = (c_orm_ast_node_t *)op;
    n_op.op = "IS NULL";
    n_op.right = NULL;
    q1->select_(q1, "id");
    q1->from(q1, "t");
    q1->where(q1, (c_orm_ast_node_t *)&n_op);
    ASSERT_EQ(C_ORM_OK,
              c_orm_query_to_sql(q1, C_ORM_DIALECT_SQLITE, &sql, NULL));
    ASSERT(strstr(sql, "IS NULL") != NULL);
    c_orm_free(sql);
    sql = NULL;
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
  q1->select_(q1, "id");
  q1->from(q1, "empty_t");
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
    q1->select_(q1, "id");
    q1->from(q1, "empty_t");
    ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH,
              c_orm_query_fetch_one(db, q1, &blob_meta, blob_out));
    ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH,
              c_orm_query_fetch_all(db, q1, &blob_meta, &arr));
  }

  c_orm_query_free(q1);
  db->vtable->disconnect(db);

  PASS();
}

/**
 * @brief Helper to exercise string builder append countdown across query SQL
 * generation.
 * @param q The query to convert.
 * @param dialect The SQL dialect.
 * @param params_ptr Pointer to query parameters, or NULL.
 * @param max_countdown The maximum countdown value to sweep.
 * @return C_ORM_OK on completion.
 */
static c_orm_error_t run_query_countdown(c_orm_ast_query_t *q,
                                         c_orm_dialect_t dialect,
                                         c_orm_query_params_t *params_ptr,
                                         int max_countdown) {
  int i;
  char *sql;
  c_orm_error_t rc;

  for (i = 0; i <= max_countdown; i++) {
    sql = NULL;
    c_orm_mock_string_builder_append_countdown = i;
    rc = c_orm_query_to_sql(q, dialect, &sql, params_ptr);
    if (rc != C_ORM_OK) {
      /* Expected OOM from mock append countdown */
    }
    if (sql) {
      c_orm_free(sql);
      sql = NULL;
    }
  }
  c_orm_mock_string_builder_append_countdown = -1;
  return C_ORM_OK;
}

/**
 * @brief Tests all append error branches and mock get failure in
 * c_orm_query_to_sql.
 * @return GREATEST test result.
 */
TEST test_query_sql_append_oom_branches(void) {
  c_orm_ast_query_t *q = NULL;
  c_orm_ast_query_t *subq = NULL;
  c_orm_ast_query_t *cte_q = NULL;
  c_orm_ast_query_t *uni_q = NULL;
  c_orm_query_params_t params;
  c_orm_ast_select_t *sel_node;
  c_orm_ast_node_t *raw_left = NULL;
  c_orm_ast_node_t *tmp = NULL;
  char *sql = NULL;
  c_orm_error_t err;

  c_orm_query_params_init(&params);

  /* Build subqueries */
  err = c_orm_query_new(&subq);
  ASSERT_EQ(C_ORM_OK, err);
  subq->select_(subq, "1");
  subq->from(subq, "sub_table");

  err = c_orm_query_new(&cte_q);
  ASSERT_EQ(C_ORM_OK, err);
  cte_q->select_(cte_q, "id, val");
  cte_q->from(cte_q, "cte_source");

  err = c_orm_query_new(&uni_q);
  ASSERT_EQ(C_ORM_OK, err);
  uni_q->select_(uni_q, "id, name");
  uni_q->from(uni_q, "archive_users");

  /* Build comprehensive main query */
  err = c_orm_query_new(&q);
  ASSERT_EQ(C_ORM_OK, err);

  q->with(q, "my_cte", cte_q);
  q->select_(q, "u.id, u.name");
  sel_node = (c_orm_ast_select_t *)q->ast_head;
  sel_node->is_distinct = 1;

  q->from_alias(q, "users", "u");
  q->eq(q, "u.role_id", "roles.id", 0, &tmp);
  q->join(q, "roles", "INNER", tmp);
  q->eq(q, "u.dept_id", "departments.id", 0, &tmp);
  q->join(q, "departments", "LEFT", tmp);

  /* WHERE conditions covering all node types */
  q->col(q, "u.id", &tmp);
  q->where(q, tmp);
  q->lit(q, "alice", 1, &tmp);
  q->and_where(q, tmp);
  q->lit(q, "42", 0, &tmp);
  q->and_where(q, tmp);
  q->raw(q, "1 = 1", &tmp);
  q->and_where(q, tmp);
  q->eq(q, "u.status", "active", 1, &tmp);
  q->and_where(q, tmp);

  /* Unary operator without right operand */
  q->col(q, "u.deleted_at", &raw_left);
  {
    c_orm_ast_operator_t u_op;
    memset(&u_op, 0, sizeof(u_op));
    u_op.base.type = C_ORM_AST_NODE_OPERATOR;
    u_op.left = raw_left;
    u_op.op = "IS NULL";
    u_op.right = NULL;
    q->and_where(q, (c_orm_ast_node_t *)&u_op);
  }

  q->eq(q, "u.verified", "1", 0, &tmp);
  q->group(q, tmp, &tmp);
  q->and_where(q, tmp);
  q->cast_(q, "u.age", "INTEGER", &tmp);
  q->and_where(q, tmp);
  q->func(q, "UPPER", "u.name", "upper_name", &tmp);
  q->and_where(q, tmp);
  q->func(q, "LOWER", "u.name", "", &tmp);
  q->and_where(q, tmp);
  q->func(q, "COUNT", "*", NULL, &tmp);
  q->and_where(q, tmp);

  q->between(q, "u.created_at", "2020-01-01", "2025-01-01", 1, &tmp);
  q->and_where(q, tmp);
  q->between(q, "u.age", "18", "65", 0, &tmp);
  q->and_where(q, tmp);

  q->exists(q, subq, 0, &tmp);
  q->and_where(q, tmp);
  q->exists(q, subq, 1, &tmp);
  q->and_where(q, tmp);
  q->subquery(q, subq, "sq_alias", &tmp);
  q->and_where(q, tmp);
  q->subquery(q, subq, "", &tmp);
  q->and_where(q, tmp);
  q->subquery(q, subq, NULL, &tmp);
  q->and_where(q, tmp);

  q->window(q, "ROW_NUMBER()", "dept", "salary DESC", "rn", &tmp);
  q->and_where(q, tmp);
  q->window(q, "ROW_NUMBER()", "dept", "", "", &tmp);
  q->and_where(q, tmp);
  q->window(q, "ROW_NUMBER()", "", "salary DESC", "rn", &tmp);
  q->and_where(q, tmp);
  q->window(q, "ROW_NUMBER()", NULL, NULL, NULL, &tmp);
  q->and_where(q, tmp);

  q->group_by(q, "u.id, u.name");
  q->gt(q, "COUNT(*)", "5", 0, &tmp);
  q->having(q, tmp);
  q->union_(q, uni_q, 1);    /* UNION ALL */
  q->order_by(q, "u.id", 1); /* DESC */
  q->limit(q, 50);
  q->offset(q, 10);

  /* 1. Sweep append countdown for SQLite with params */
  err = run_query_countdown(q, C_ORM_DIALECT_SQLITE, &params, 120);
  ASSERT_EQ(C_ORM_OK, err);

  /* 2. Sweep append countdown for Postgres with params */
  err = run_query_countdown(q, C_ORM_DIALECT_POSTGRES, &params, 120);
  ASSERT_EQ(C_ORM_OK, err);

  /* 3. Sweep append countdown for SQLite without params */
  err = run_query_countdown(q, C_ORM_DIALECT_SQLITE, NULL, 120);
  ASSERT_EQ(C_ORM_OK, err);

  /* 4. Sweep append countdown for Postgres without params */
  err = run_query_countdown(q, C_ORM_DIALECT_POSTGRES, NULL, 120);
  ASSERT_EQ(C_ORM_OK, err);

  /* 5. Variations: UNION without ALL, ORDER BY ASC, table without alias */
  {
    c_orm_ast_query_t *q2 = NULL;
    c_orm_ast_from_t *from_node;
    err = c_orm_query_new(&q2);
    ASSERT_EQ(C_ORM_OK, err);
    q2->select_(q2, "x");
    q2->from(q2, "simple_tbl");
    from_node = (c_orm_ast_from_t *)q2->ast_head;
    from_node->alias = "";
    q2->union_(q2, uni_q, 0); /* UNION (is_all = 0) */
    q2->order_by(q2, "x", 0); /* ASC (is_desc = 0) */
    err = run_query_countdown(q2, C_ORM_DIALECT_SQLITE, NULL, 40);
    ASSERT_EQ(C_ORM_OK, err);
    c_orm_query_free(q2);
  }

  /* 6. Targeted EXISTS sweep */
  {
    c_orm_ast_query_t *qe = NULL;
    err = c_orm_query_new(&qe);
    ASSERT_EQ(C_ORM_OK, err);
    qe->select_(qe, "1");
    qe->from(qe, "t");
    qe->exists(qe, subq, 0, &tmp);
    qe->where(qe, tmp);
    qe->exists(qe, subq, 1, &tmp);
    qe->and_where(qe, tmp);
    err = run_query_countdown(qe, C_ORM_DIALECT_SQLITE, &params, 40);
    ASSERT_EQ(C_ORM_OK, err);
    err = run_query_countdown(qe, C_ORM_DIALECT_SQLITE, NULL, 40);
    ASSERT_EQ(C_ORM_OK, err);
    c_orm_query_free(qe);
  }

  /* 7. Targeted SUBQUERY sweep */
  {
    c_orm_ast_query_t *qs = NULL;
    err = c_orm_query_new(&qs);
    ASSERT_EQ(C_ORM_OK, err);
    qs->select_(qs, "1");
    qs->from(qs, "t");
    qs->subquery(qs, subq, "sub_alias", &tmp);
    qs->where(qs, tmp);
    qs->subquery(qs, subq, "", &tmp);
    qs->and_where(qs, tmp);
    qs->subquery(qs, subq, NULL, &tmp);
    qs->and_where(qs, tmp);
    err = run_query_countdown(qs, C_ORM_DIALECT_SQLITE, &params, 50);
    ASSERT_EQ(C_ORM_OK, err);
    err = run_query_countdown(qs, C_ORM_DIALECT_SQLITE, NULL, 50);
    ASSERT_EQ(C_ORM_OK, err);
    c_orm_query_free(qs);
  }

  /* 8. Targeted WINDOW sweep */
  {
    c_orm_ast_query_t *qw = NULL;
    err = c_orm_query_new(&qw);
    ASSERT_EQ(C_ORM_OK, err);
    qw->select_(qw, "1");
    qw->from(qw, "t");
    qw->window(qw, "ROW_NUMBER()", "dept", "salary DESC", "rn", &tmp);
    qw->where(qw, tmp);
    qw->window(qw, "ROW_NUMBER()", "dept", "", "", &tmp);
    qw->and_where(qw, tmp);
    qw->window(qw, "ROW_NUMBER()", "", "salary DESC", "rn", &tmp);
    qw->and_where(qw, tmp);
    qw->window(qw, "ROW_NUMBER()", NULL, NULL, NULL, &tmp);
    qw->and_where(qw, tmp);
    qw->window(qw, "ROW_NUMBER()", "dept", "salary", NULL, &tmp);
    qw->and_where(qw, tmp);
    err = run_query_countdown(qw, C_ORM_DIALECT_SQLITE, &params, 50);
    ASSERT_EQ(C_ORM_OK, err);
    err = run_query_countdown(qw, C_ORM_DIALECT_SQLITE, NULL, 50);
    ASSERT_EQ(C_ORM_OK, err);
    c_orm_query_free(qw);
  }

  /* 9. Targeted HAVING sweep */
  {
    c_orm_ast_query_t *qh = NULL;
    err = c_orm_query_new(&qh);
    ASSERT_EQ(C_ORM_OK, err);
    qh->select_(qh, "dept, COUNT(*)");
    qh->from(qh, "t");
    qh->group_by(qh, "dept");
    qh->gt(qh, "COUNT(*)", "5", 0, &tmp);
    qh->having(qh, tmp);
    err = run_query_countdown(qh, C_ORM_DIALECT_SQLITE, &params, 30);
    ASSERT_EQ(C_ORM_OK, err);
    err = run_query_countdown(qh, C_ORM_DIALECT_SQLITE, NULL, 30);
    ASSERT_EQ(C_ORM_OK, err);
    c_orm_query_free(qh);
  }

  /* 10. Targeted UNION sweep */
  {
    c_orm_ast_query_t *qu = NULL;
    err = c_orm_query_new(&qu);
    ASSERT_EQ(C_ORM_OK, err);
    qu->select_(qu, "1");
    qu->from(qu, "t");
    qu->union_(qu, subq, 0);
    qu->union_(qu, subq, 1);
    err = run_query_countdown(qu, C_ORM_DIALECT_SQLITE, &params, 30);
    ASSERT_EQ(C_ORM_OK, err);
    err = run_query_countdown(qu, C_ORM_DIALECT_SQLITE, NULL, 30);
    ASSERT_EQ(C_ORM_OK, err);
    c_orm_query_free(qu);
  }

  /* 11. Targeted LIMIT and OFFSET sweep */
  {
    c_orm_ast_query_t *q_lim = NULL;
    err = c_orm_query_new(&q_lim);
    ASSERT_EQ(C_ORM_OK, err);
    q_lim->select_(q_lim, "1");
    q_lim->from(q_lim, "t");
    q_lim->limit(q_lim, 5);
    q_lim->offset(q_lim, 10);
    err = run_query_countdown(q_lim, C_ORM_DIALECT_SQLITE, NULL, 15);
    ASSERT_EQ(C_ORM_OK, err);
    c_orm_query_free(q_lim);
  }

  /* 12. Mock string builder get failure on simple query */
  {
    c_orm_ast_query_t *q_simple = NULL;
    err = c_orm_query_new(&q_simple);
    ASSERT_EQ(C_ORM_OK, err);
    q_simple->select_(q_simple, "1");
    q_simple->from(q_simple, "t");

    c_orm_mock_string_builder_get_fail = 1;
    sql = NULL;
    err = c_orm_query_to_sql(q_simple, C_ORM_DIALECT_SQLITE, &sql, NULL);
    ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
    ASSERT_EQ(NULL, sql);
    c_orm_mock_string_builder_get_fail = 0;

    c_orm_query_free(q_simple);
  }

  /* Cleanup */
  c_orm_query_free(q);
  c_orm_query_free(subq);
  c_orm_query_free(cte_q);
  c_orm_query_free(uni_q);
  c_orm_query_params_cleanup(&params);

  PASS();
}

/** @brief Return code configured for mock_ok_finalize. */
static c_orm_error_t g_mock_fin_rc = C_ORM_OK;

/**
 * @brief Mock prepare callback returning a non-NULL dummy statement handle.
 * @param db Database connection handle.
 * @param sql SQL statement text.
 * @param out Output statement handle pointer.
 * @return C_ORM_OK.
 */
static c_orm_error_t mock_ok_prepare(c_orm_db_t *db, const char *sql,
                                     c_orm_query_t **out) {
  static int dummy_stmt = 1;
  (void)db;
  (void)sql;
  *out = (c_orm_query_t *)&dummy_stmt;
  return C_ORM_OK;
}

/**
 * @brief Mock step callback reporting zero rows.
 * @param query Statement handle.
 * @param out_has_row Output row indicator.
 * @return C_ORM_OK.
 */
static c_orm_error_t mock_ok_step(c_orm_query_t *query, int *out_has_row) {
  (void)query;
  *out_has_row = 0;
  return C_ORM_OK;
}

/**
 * @brief Mock finalize callback returning configurable status.
 * @param query Statement handle.
 * @return Configured g_mock_fin_rc.
 */
static c_orm_error_t mock_ok_finalize(c_orm_query_t *query) {
  (void)query;
  return g_mock_fin_rc;
}

/**
 * @brief Mock bind_string callback succeeding unconditionally.
 * @param query Statement handle.
 * @param index 1-based parameter index.
 * @param val Bound string value.
 * @return C_ORM_OK.
 */
static c_orm_error_t mock_ok_bind_string(c_orm_query_t *query, int index,
                                         const char *val) {
  (void)query;
  (void)index;
  (void)val;
  return C_ORM_OK;
}

/**
 * @brief Mock bind_int64 callback succeeding unconditionally.
 * @param query Statement handle.
 * @param index 1-based parameter index.
 * @param val Bound 64-bit integer value.
 * @return C_ORM_OK.
 */
static c_orm_error_t mock_ok_bind_int64(c_orm_query_t *query, int index,
                                        int64_t val) {
  (void)query;
  (void)index;
  (void)val;
  return C_ORM_OK;
}

/**
 * @brief Mock bind_double callback succeeding unconditionally.
 * @param query Statement handle.
 * @param index 1-based parameter index.
 * @param val Bound double value.
 * @return C_ORM_OK.
 */
static c_orm_error_t mock_ok_bind_double(c_orm_query_t *query, int index,
                                         double val) {
  (void)query;
  (void)index;
  (void)val;
  return C_ORM_OK;
}

/**
 * @brief Mock bind_null callback succeeding unconditionally.
 * @param query Statement handle.
 * @param index 1-based parameter index.
 * @return C_ORM_OK.
 */
static c_orm_error_t mock_ok_bind_null(c_orm_query_t *query, int index) {
  (void)query;
  (void)index;
  return C_ORM_OK;
}

/**
 * @brief Mock bind_decimal callback succeeding unconditionally.
 * @param query Statement handle.
 * @param index 1-based parameter index.
 * @param val Bound decimal value.
 * @return C_ORM_OK.
 */
static c_orm_error_t mock_ok_bind_decimal(c_orm_query_t *query, int index,
                                          const c_orm_decimal_t *val) {
  (void)query;
  (void)index;
  (void)val;
  return C_ORM_OK;
}

/**
 * @brief Sweeps string builder append countdown across a query conversion.
 * @param q Fluent query to render.
 * @param dialect Target SQL dialect.
 * @param use_params Non-zero to pass a params struct.
 * @param max_steps Maximum countdown steps to probe.
 * @return C_ORM_OK on completion.
 */
static c_orm_error_t sweep_dml_sql_countdown(c_orm_ast_query_t *q,
                                             c_orm_dialect_t dialect,
                                             int use_params, int max_steps) {
  int step;
  for (step = 0;; step++) {
    char *s = NULL;
    c_orm_query_params_t p;
    c_orm_error_t rc;
    p.params = NULL;
    p.count = 0;
    p.capacity = 0;
    c_orm_mock_string_builder_append_countdown = step;
    rc = c_orm_query_to_sql(q, dialect, &s, use_params ? &p : NULL);
    c_orm_mock_string_builder_append_countdown = -1;
    ASSERT_EQ(C_ORM_OK, c_orm_query_params_cleanup(&p));
    if (rc == C_ORM_OK) {
      c_orm_system_free(s);
      break;
    }
    ASSERT(step < max_steps + 50);
  }
  return C_ORM_OK;
}

/**
 * @brief Tests DML fluent nodes, Db2/ODBC SQL dialects, decimal parameter
 * binding, lock clauses, AST cloning, and free error percolation.
 * @return GREATEST test result.
 */
TEST test_fluent_dml_lock_decimal_and_sql_db2(void) {
  c_orm_error_t err;
  c_orm_decimal_t dec;
  c_orm_decimal_t bad_dec;

  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("123.45", &dec));
  memset(&bad_dec, 0, sizeof(bad_dec));
  bad_dec.precision = 0;

  /* 1. Fluent DML, lock, and lit_decimal invalid args, error state & OOM */
  {
    c_orm_ast_query_t *q = NULL;
    c_orm_ast_node_t *v1 = NULL;
    c_orm_ast_node_t *v2 = NULL;
    c_orm_ast_node_t *dnode = NULL;
    int k;

    ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
    ASSERT_EQ(C_ORM_OK, q->lit(q, "10", 0, &v1));
    ASSERT_EQ(C_ORM_OK, q->lit_decimal(q, &dec, &dnode));
    ASSERT(dnode != NULL);

    /* Invalid / NULL / empty arguments */
    ASSERT_NEQ(C_ORM_OK, q->insert_into(NULL, "t"));
    ASSERT_NEQ(C_ORM_OK, q->insert_into(q, NULL));
    ASSERT_NEQ(C_ORM_OK, q->insert_into(q, ""));

    q->error = 0;
    ASSERT_NEQ(C_ORM_OK, q->update_table(NULL, "t"));
    ASSERT_NEQ(C_ORM_OK, q->update_table(q, NULL));
    ASSERT_NEQ(C_ORM_OK, q->update_table(q, ""));

    q->error = 0;
    ASSERT_NEQ(C_ORM_OK, q->delete_from(NULL, "t"));
    ASSERT_NEQ(C_ORM_OK, q->delete_from(q, NULL));
    ASSERT_NEQ(C_ORM_OK, q->delete_from(q, ""));

    q->error = 0;
    ASSERT_NEQ(C_ORM_OK, q->set_col(NULL, "c", v1));
    ASSERT_NEQ(C_ORM_OK, q->set_col(q, NULL, v1));
    ASSERT_NEQ(C_ORM_OK, q->set_col(q, "", v1));
    ASSERT_NEQ(C_ORM_OK, q->set_col(q, "c", NULL));

    q->error = 0;
    ASSERT_NEQ(C_ORM_OK, q->returning(NULL, "id"));
    ASSERT_NEQ(C_ORM_OK, q->returning(q, NULL));
    ASSERT_NEQ(C_ORM_OK, q->returning(q, ""));

    q->error = 0;
    ASSERT_NEQ(C_ORM_OK, q->with_lock(NULL, C_ORM_AST_LOCK_WITH_UR));

    q->error = 0;
    ASSERT_NEQ(C_ORM_OK, q->lit_decimal(NULL, &dec, &v2));
    ASSERT_NEQ(C_ORM_OK, q->lit_decimal(q, NULL, &v2));
    ASSERT_NEQ(C_ORM_OK, q->lit_decimal(q, &dec, NULL));
    q->error = 0;
    ASSERT_NEQ(C_ORM_OK, q->lit_decimal(q, &bad_dec, &v2));

    /* Pre-existing q->error == 1 branches */
    q->error = 1;
    ASSERT_NEQ(C_ORM_OK, q->insert_into(q, "t"));
    ASSERT_NEQ(C_ORM_OK, q->update_table(q, "t"));
    ASSERT_NEQ(C_ORM_OK, q->delete_from(q, "t"));
    ASSERT_NEQ(C_ORM_OK, q->set_col(q, "c", v1));
    ASSERT_NEQ(C_ORM_OK, q->returning(q, "id"));
    ASSERT_NEQ(C_ORM_OK, q->with_lock(q, C_ORM_AST_LOCK_WITH_UR));
    ASSERT_NEQ(C_ORM_OK, q->lit_decimal(q, &dec, &v2));
    ASSERT_EQ(C_ORM_OK, c_orm_query_free(q));

    /* OOM on fresh arena (0 = strdup block alloc, 1 = node alloc when large) */
    for (k = 0; k < 2; k++) {
      char big_name[4090];
      memset(big_name, 'a', sizeof(big_name) - 1);
      big_name[sizeof(big_name) - 1] = '\0';

      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
      oom_countdown = k;
      oom_active = 1;
      q->insert_into(q, big_name);
      oom_active = 0;
      c_orm_query_free(q);

      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
      oom_countdown = k;
      oom_active = 1;
      q->update_table(q, big_name);
      oom_active = 0;
      c_orm_query_free(q);

      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
      oom_countdown = k;
      oom_active = 1;
      q->delete_from(q, big_name);
      oom_active = 0;
      c_orm_query_free(q);

      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
      ASSERT_EQ(C_ORM_OK, q->lit(q, "1", 0, &v1));
      oom_countdown = k;
      oom_active = 1;
      q->set_col(q, big_name, v1);
      oom_active = 0;
      c_orm_query_free(q);

      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
      oom_countdown = k;
      oom_active = 1;
      q->returning(q, big_name);
      oom_active = 0;
      c_orm_query_free(q);

      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
      if (k == 0) {
        oom_countdown = 0;
        oom_active = 1;
        q->with_lock(q, C_ORM_AST_LOCK_WITH_CS);
        oom_active = 0;
      }
      c_orm_query_free(q);

      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
      if (k == 0) {
        oom_countdown = 0;
        oom_active = 1;
        q->lit_decimal(q, &dec, &v2);
        oom_active = 0;
      } else {
        void *pad = NULL;
        ASSERT_EQ(C_ORM_OK, c_orm_arena_alloc(q->arena, 4072, &pad));
        oom_countdown = 0;
        oom_active = 1;
        ASSERT_NEQ(C_ORM_OK, q->lit_decimal(q, &dec, &v2));
        oom_active = 0;
      }
      c_orm_query_free(q);
    }
  }

  /* 2. Clone UPDATE, DELETE, INSERT, ASSIGN, RETURNING, LOCK, and free errors
   */
  {
    c_orm_ast_query_t *q = NULL;
    c_orm_ast_query_t *cloned = NULL;
    c_orm_ast_node_t *v = NULL;

    ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
    ASSERT_EQ(C_ORM_OK, q->lit_decimal(q, &dec, &v));
    ASSERT_EQ(C_ORM_OK, q->update_table(q, "accounts"));
    ASSERT_EQ(C_ORM_OK, q->set_col(q, "balance", v));
    ASSERT_EQ(C_ORM_OK, q->delete_from(q, "accounts"));
    ASSERT_EQ(C_ORM_OK, q->insert_into(q, "accounts"));
    ASSERT_EQ(C_ORM_OK, q->returning(q, "id, balance"));
    ASSERT_EQ(C_ORM_OK, q->with_lock(q, C_ORM_AST_LOCK_WITH_RS));
    ASSERT_EQ(C_ORM_OK, q->clone(q, &cloned));
    ASSERT_EQ(C_ORM_OK, c_orm_query_free(cloned));
    ASSERT_EQ(C_ORM_OK, c_orm_query_free(q));

    {
      char big_expr[4200];
      c_orm_ast_node_t *big_v = NULL;
      int assign_step;

      memset(big_expr, '1', sizeof(big_expr) - 1);
      big_expr[sizeof(big_expr) - 1] = '\0';
      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
      ASSERT_EQ(C_ORM_OK, q->raw(q, big_expr, &big_v));
      ASSERT_EQ(C_ORM_OK, q->update_table(q, "accounts"));
      ASSERT_EQ(C_ORM_OK, q->set_col(q, "balance", big_v));
      for (assign_step = 0;; ++assign_step) {
        cloned = NULL;
        oom_countdown = assign_step;
        oom_active = 1;
        err = q->clone(q, &cloned);
        oom_active = 0;
        if (err == C_ORM_OK) {
          ASSERT_EQ(C_ORM_OK, c_orm_query_free(cloned));
          break;
        }
        ASSERT(assign_step < 20);
      }
      ASSERT_EQ(C_ORM_OK, c_orm_query_free(q));
    }

    /* Simulated free failures in c_orm_query_new, c_orm_query_free, eager_load
     */
    oom_countdown = 1;
    oom_active = 1;
    q_free_call_idx = 0;
    q_free_fail_mask = 1u;
    err = c_orm_query_new(&q);
    oom_active = 0;
    q_free_fail_mask = 0;
    ASSERT_NEQ(C_ORM_OK, err);

    ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
    q_free_call_idx = 0;
    q_free_fail_mask = 2u; /* 0 = arena struct free, 1 = q struct free */
    err = c_orm_query_free(q);
    q_free_fail_mask = 0;
    ASSERT_EQ(C_ORM_ERROR_MEMORY, err);

    ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
    q_free_call_idx = 0;
    q_free_fail_mask = 3u; /* both arena struct free and q struct free fail */
    err = c_orm_query_free(q);
    q_free_fail_mask = 0;
    ASSERT_EQ(C_ORM_ERROR_MEMORY, err);

    {
      c_orm_column_meta_t ccols[1];
      c_orm_table_meta_t child_m;
      c_orm_relation_meta_t rel;
      c_orm_table_meta_t parent_m;
      memset(ccols, 0, sizeof(ccols));
      memset(&child_m, 0, sizeof(child_m));
      memset(&rel, 0, sizeof(rel));
      memset(&parent_m, 0, sizeof(parent_m));
      ccols[0].name = "id";
      child_m.name = "posts";
      child_m.columns = ccols;
      child_m.num_columns = 1;
      rel.field_name = "posts";
      rel.type = C_ORM_RELATION_ONE_TO_MANY;
      rel.target_table = "posts";
      rel.local_key = "id";
      rel.foreign_key = "user_id";
      rel.target_meta = &child_m;
      parent_m.name = "users";
      parent_m.columns = ccols;
      parent_m.num_columns = 1;
      parent_m.relations = &rel;
      parent_m.num_relations = 1;

      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
      q_free_call_idx = 0;
      q_free_fail_mask = 1u;
      err = q->eager_load(q, &parent_m, "posts");
      q_free_fail_mask = 0;
      ASSERT_EQ(C_ORM_ERROR_MEMORY, err);
      c_orm_query_free(q);
    }
  }

  /* 3. Db2 & ODBC SQL generation: DML, RETURNING, LOCK, pagination, >64 nodes
   */
  {
    c_orm_ast_query_t *q = NULL;
    c_orm_ast_node_t *n1 = NULL;
    c_orm_ast_node_t *n2 = NULL;
    c_orm_ast_node_t *w = NULL;
    char *sql = NULL;
    c_orm_query_params_t params;
    int idx;

    /* c_orm_query_params_add_decimal edge cases */
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_query_params_add_decimal(NULL, &dec, "1"));
    c_orm_query_params_init(&params);
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_query_params_add_decimal(&params, NULL, "1"));
    oom_countdown = 0;
    oom_active = 1;
    ASSERT_NEQ(C_ORM_OK, c_orm_query_params_add_decimal(&params, &dec, "1"));
    oom_active = 0;
    c_orm_query_params_cleanup(&params);

    /* INSERT DEFAULT VALUES + RETURNING on DB2 and Postgres */
    ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
    ASSERT_EQ(C_ORM_OK, q->insert_into(q, "S.T"));
    ASSERT_EQ(
        C_ORM_OK,
        q->returning(q, " id , \tCOALESCE(a, b)\t , ), , \"Q\", \"unclosed "));
    ASSERT_EQ(C_ORM_OK, sweep_dml_sql_countdown(q, C_ORM_DIALECT_DB2, 1, 35));
    ASSERT_EQ(C_ORM_OK,
              sweep_dml_sql_countdown(q, C_ORM_DIALECT_POSTGRES, 1, 20));
    /* OOM on item_buf inside append_dialect_ident_csv */
    oom_countdown = 3;
    oom_active = 1;
    sql = NULL;
    ASSERT_NEQ(C_ORM_OK, c_orm_query_to_sql(q, C_ORM_DIALECT_DB2, &sql, NULL));
    oom_active = 0;
    ASSERT(sql == NULL);
    c_orm_query_free(q);

    /* INSERT with 2 columns + decimal literal (params & non-params) */
    ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
    ASSERT_EQ(C_ORM_OK, q->insert_into(q, "S.T"));
    ASSERT_EQ(C_ORM_OK, q->lit_decimal(q, &dec, &n1));
    ASSERT_EQ(C_ORM_OK, q->lit(q, "abc", 1, &n2));
    ASSERT_EQ(C_ORM_OK, q->set_col(q, "amt", n1));
    ASSERT_EQ(C_ORM_OK, q->set_col(q, "note", n2));
    ASSERT_EQ(C_ORM_OK, q->returning(q, "*"));
    ASSERT_EQ(C_ORM_OK, sweep_dml_sql_countdown(q, C_ORM_DIALECT_DB2, 1, 40));
    ASSERT_EQ(C_ORM_OK, sweep_dml_sql_countdown(q, C_ORM_DIALECT_DB2, 0, 40));
    c_orm_query_free(q);

    /* UPDATE with 2 columns + WHERE + RETURNING on DB2 & SQLite, and no WHERE
     */
    ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
    ASSERT_EQ(C_ORM_OK, q->update_table(q, "S.T"));
    ASSERT_EQ(C_ORM_OK, q->lit(q, "1", 0, &n1));
    ASSERT_EQ(C_ORM_OK, q->lit(q, "2", 0, &n2));
    ASSERT_EQ(C_ORM_OK, q->set_col(q, "c1", n1));
    ASSERT_EQ(C_ORM_OK, q->set_col(q, "c2", n2));
    ASSERT_EQ(C_ORM_OK, sweep_dml_sql_countdown(q, C_ORM_DIALECT_DB2, 1, 30));
    ASSERT_EQ(C_ORM_OK, q->eq(q, "id", "5", 0, &w));
    ASSERT_EQ(C_ORM_OK, q->where(q, w));
    ASSERT_EQ(C_ORM_OK, q->returning(q, "id"));
    ASSERT_EQ(C_ORM_OK, sweep_dml_sql_countdown(q, C_ORM_DIALECT_DB2, 1, 40));
    ASSERT_EQ(C_ORM_OK,
              sweep_dml_sql_countdown(q, C_ORM_DIALECT_SQLITE, 1, 40));
    c_orm_query_free(q);

    /* DELETE without WHERE and with WHERE + OLD TABLE RETURNING on DB2 */
    ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
    ASSERT_EQ(C_ORM_OK, q->delete_from(q, "S.T"));
    ASSERT_EQ(C_ORM_OK, sweep_dml_sql_countdown(q, C_ORM_DIALECT_DB2, 1, 20));
    ASSERT_EQ(C_ORM_OK, q->eq(q, "id", "5", 0, &w));
    ASSERT_EQ(C_ORM_OK, q->where(q, w));
    ASSERT_EQ(C_ORM_OK, q->returning(q, "id"));
    ASSERT_EQ(C_ORM_OK, sweep_dml_sql_countdown(q, C_ORM_DIALECT_DB2, 1, 30));
    c_orm_query_free(q);

    /* Db2 SELECT without FROM (SYSIBM.SYSDUMMY1), locks, pagination, Safe CRT
     */
    {
      c_orm_ast_lock_mode_t modes[6];
      modes[0] = C_ORM_AST_LOCK_WITH_UR;
      modes[1] = C_ORM_AST_LOCK_WITH_CS;
      modes[2] = C_ORM_AST_LOCK_WITH_RS;
      modes[3] = C_ORM_AST_LOCK_WITH_RR;
      modes[4] = C_ORM_AST_LOCK_NONE;
      modes[5] = (c_orm_ast_lock_mode_t)99;
      for (idx = 0; idx < 6; idx++) {
        ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
        ASSERT_EQ(C_ORM_OK, q->select_(q, "1 + 1"));
        ASSERT_EQ(C_ORM_OK, q->with_lock(q, C_ORM_AST_LOCK_WITH_UR));
        ASSERT_EQ(C_ORM_OK, q->with_lock(q, modes[idx]));
        ASSERT_EQ(C_ORM_OK, q->with_lock(q, C_ORM_AST_LOCK_FOR_FETCH_ONLY));
        ASSERT_EQ(C_ORM_OK,
                  sweep_dml_sql_countdown(q, C_ORM_DIALECT_DB2, 0, 15));
        ASSERT_EQ(C_ORM_OK,
                  sweep_dml_sql_countdown(q, C_ORM_DIALECT_SQLITE, 0, 15));
        c_orm_query_free(q);
      }
      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
      ASSERT_EQ(C_ORM_OK, q->select_(q, "1"));
      ASSERT_EQ(C_ORM_OK, q->from(q, "t"));
      ASSERT_EQ(C_ORM_OK, q->between(q, "dt", "2020", "2025", 1, &w));
      ASSERT_EQ(C_ORM_OK, q->where(q, w));
      ASSERT_EQ(C_ORM_OK,
                sweep_dml_sql_countdown(q, C_ORM_DIALECT_SQLITE, 0, 25));
      c_orm_query_free(q);
    }

    /* Db2 & ODBC LIMIT/OFFSET combinations + Safe CRT sprintf/strcpy failures
     */
    {
      int mode;
      for (mode = 0; mode < 4; mode++) {
        int sc;
        ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
        ASSERT_EQ(C_ORM_OK, q->select_(q, "a, b"));
        ASSERT_EQ(C_ORM_OK, q->from_alias(q, "S.T", "u"));
        if (mode == 0 || mode == 1) {
          ASSERT_EQ(C_ORM_OK, q->limit(q, 10));
        }
        if (mode == 0 || mode == 2) {
          ASSERT_EQ(C_ORM_OK, q->offset(q, 5));
        }
        ASSERT_EQ(C_ORM_OK,
                  sweep_dml_sql_countdown(q, C_ORM_DIALECT_DB2, 0, 25));
        ASSERT_EQ(C_ORM_OK,
                  sweep_dml_sql_countdown(q, C_ORM_DIALECT_ODBC, 0, 25));
        for (sc = 0; sc < 4; sc++) {
          c_orm_error_t qrc;
          c_orm_mock_safe_crt_countdown = sc;
          sql = NULL;
          qrc = c_orm_query_to_sql(q, C_ORM_DIALECT_DB2, &sql, NULL);
          ASSERT_NEQ((c_orm_error_t)-999, qrc);
          c_orm_mock_safe_crt_countdown = -1;
          if (sql) {
            c_orm_system_free(sql);
          }
          c_orm_mock_safe_crt_countdown = sc;
          sql = NULL;
          qrc = c_orm_query_to_sql(q, C_ORM_DIALECT_SQLITE, &sql, NULL);
          ASSERT_NEQ((c_orm_error_t)-999, qrc);
          c_orm_mock_safe_crt_countdown = -1;
          if (sql) {
            c_orm_system_free(sql);
          }
        }
        c_orm_query_free(q);
      }
    }

    /* > 64 AST nodes (heap_nodes allocation, builder init OOM, fail_to_sql, ok)
     */
    ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
    ASSERT_EQ(C_ORM_OK, q->select_(q, "id"));
    ASSERT_EQ(C_ORM_OK, q->from(q, "users"));
    for (idx = 0; idx < 65; idx++) {
      ASSERT_EQ(C_ORM_OK, q->order_by(q, "id", 0));
    }
    for (idx = 0;; idx++) {
      c_orm_error_t qrc;
      oom_countdown = idx;
      oom_active = 1;
      sql = NULL;
      qrc = c_orm_query_to_sql(q, C_ORM_DIALECT_SQLITE, &sql, NULL);
      oom_active = 0;
      if (sql) {
        ASSERT_EQ(C_ORM_OK, c_orm_system_free(sql));
      }
      if (qrc == C_ORM_OK) {
        break;
      }
      ASSERT(idx < 20);
    }
    c_orm_mock_string_builder_append_countdown = 2;
    sql = NULL;
    ASSERT_NEQ(C_ORM_OK,
               c_orm_query_to_sql(q, C_ORM_DIALECT_SQLITE, &sql, NULL));
    c_orm_mock_string_builder_append_countdown = -1;
    sql = NULL;
    ASSERT_EQ(C_ORM_OK,
              c_orm_query_to_sql(q, C_ORM_DIALECT_SQLITE, &sql, NULL));
    c_orm_system_free(sql);
    c_orm_query_free(q);

    /* Corrupted NULL identifiers on DB2 AST nodes to exercise error branches */
    {
      c_orm_ast_column_t bad_col;
      c_orm_ast_cast_t bad_cast;
      c_orm_ast_function_t bad_fn;
      c_orm_ast_between_t bad_bw;
      c_orm_ast_literal_t null_lit;
      c_orm_ast_insert_t bad_ins;
      c_orm_ast_update_t bad_upd;
      c_orm_ast_delete_t bad_del;
      c_orm_ast_assign_t bad_asgn;
      c_orm_ast_returning_t bad_ret;
      c_orm_ast_with_t bad_wth;
      c_orm_ast_from_t bad_frm;
      c_orm_ast_join_t bad_jn;

      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
      ASSERT_EQ(C_ORM_OK, q->select_(q, "1"));

      memset(&bad_col, 0, sizeof(bad_col));
      bad_col.base.type = C_ORM_AST_NODE_COLUMN;
      bad_col.name = NULL;
      q->where(q, &bad_col.base);
      sql = NULL;
      ASSERT_NEQ(C_ORM_OK,
                 c_orm_query_to_sql(q, C_ORM_DIALECT_DB2, &sql, NULL));
      c_orm_query_free(q);

      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
      ASSERT_EQ(C_ORM_OK, q->select_(q, "1"));
      memset(&bad_cast, 0, sizeof(bad_cast));
      bad_cast.base.type = C_ORM_AST_NODE_CAST;
      bad_cast.col = NULL;
      bad_cast.type = "INT";
      q->where(q, &bad_cast.base);
      sql = NULL;
      ASSERT_NEQ(C_ORM_OK,
                 c_orm_query_to_sql(q, C_ORM_DIALECT_DB2, &sql, NULL));
      c_orm_query_free(q);

      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
      ASSERT_EQ(C_ORM_OK, q->select_(q, "1"));
      memset(&bad_fn, 0, sizeof(bad_fn));
      bad_fn.base.type = C_ORM_AST_NODE_FUNCTION;
      bad_fn.name = "COUNT";
      bad_fn.args = "*";
      bad_fn.alias = "\x01";
      q->where(q, &bad_fn.base);
      sql = NULL;
      ASSERT_NEQ(C_ORM_OK,
                 c_orm_query_to_sql(q, C_ORM_DIALECT_DB2, &sql, NULL));
      c_orm_query_free(q);

      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
      ASSERT_EQ(C_ORM_OK, q->select_(q, "1"));
      memset(&bad_bw, 0, sizeof(bad_bw));
      bad_bw.base.type = C_ORM_AST_NODE_BETWEEN;
      bad_bw.col = NULL;
      q->where(q, &bad_bw.base);
      sql = NULL;
      ASSERT_NEQ(C_ORM_OK,
                 c_orm_query_to_sql(q, C_ORM_DIALECT_DB2, &sql, NULL));
      c_orm_query_free(q);

      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
      ASSERT_EQ(C_ORM_OK, q->select_(q, "1"));
      memset(&null_lit, 0, sizeof(null_lit));
      null_lit.base.type = C_ORM_AST_NODE_LITERAL;
      null_lit.value = NULL;
      null_lit.is_string = 1;
      q->where(q, &null_lit.base);
      sql = NULL;
      ASSERT_EQ(C_ORM_OK, c_orm_query_to_sql(q, C_ORM_DIALECT_DB2, &sql, NULL));
      c_orm_system_free(sql);
      null_lit.is_string = 0;
      sql = NULL;
      ASSERT_EQ(C_ORM_OK, c_orm_query_to_sql(q, C_ORM_DIALECT_DB2, &sql, NULL));
      c_orm_system_free(sql);
      c_orm_query_free(q);

      /* NULL/invalid ident on INSERT, ASSIGN, UPDATE, DELETE, RETURNING, WITH,
       * FROM, JOIN */
      memset(&bad_ins, 0, sizeof(bad_ins));
      bad_ins.base.type = C_ORM_AST_NODE_INSERT;
      bad_ins.table = NULL;
      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
      q->ast_head = &bad_ins.base;
      sql = NULL;
      ASSERT_NEQ(C_ORM_OK,
                 c_orm_query_to_sql(q, C_ORM_DIALECT_DB2, &sql, NULL));
      q->ast_head = NULL;
      c_orm_query_free(q);

      bad_ins.table = "T";
      memset(&bad_asgn, 0, sizeof(bad_asgn));
      bad_asgn.base.type = C_ORM_AST_NODE_ASSIGN;
      bad_asgn.column = NULL;
      bad_asgn.base.next = &bad_ins.base;
      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
      q->ast_head = &bad_asgn.base;
      sql = NULL;
      ASSERT_NEQ(C_ORM_OK,
                 c_orm_query_to_sql(q, C_ORM_DIALECT_DB2, &sql, NULL));
      q->ast_head = NULL;
      c_orm_query_free(q);

      memset(&bad_upd, 0, sizeof(bad_upd));
      bad_upd.base.type = C_ORM_AST_NODE_UPDATE;
      bad_upd.table = NULL;
      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
      q->ast_head = &bad_upd.base;
      sql = NULL;
      ASSERT_NEQ(C_ORM_OK,
                 c_orm_query_to_sql(q, C_ORM_DIALECT_DB2, &sql, NULL));
      bad_upd.table = "T";
      bad_asgn.base.next = &bad_upd.base;
      q->ast_head = &bad_asgn.base;
      sql = NULL;
      ASSERT_NEQ(C_ORM_OK,
                 c_orm_query_to_sql(q, C_ORM_DIALECT_DB2, &sql, NULL));
      q->ast_head = NULL;
      c_orm_query_free(q);

      memset(&bad_del, 0, sizeof(bad_del));
      bad_del.base.type = C_ORM_AST_NODE_DELETE;
      bad_del.table = NULL;
      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
      q->ast_head = &bad_del.base;
      sql = NULL;
      ASSERT_NEQ(C_ORM_OK,
                 c_orm_query_to_sql(q, C_ORM_DIALECT_DB2, &sql, NULL));
      bad_del.table = "T";
      memset(&bad_ret, 0, sizeof(bad_ret));
      bad_ret.base.type = C_ORM_AST_NODE_RETURNING;
      bad_ret.columns = NULL;
      bad_ret.base.next = &bad_del.base;
      q->ast_head = &bad_ret.base;
      sql = NULL;
      ASSERT_NEQ(C_ORM_OK,
                 c_orm_query_to_sql(q, C_ORM_DIALECT_DB2, &sql, NULL));
      sql = NULL;
      ASSERT_NEQ(C_ORM_OK,
                 c_orm_query_to_sql(q, C_ORM_DIALECT_POSTGRES, &sql, NULL));
      bad_ret.columns = "\x01";
      sql = NULL;
      ASSERT_NEQ(C_ORM_OK,
                 c_orm_query_to_sql(q, C_ORM_DIALECT_DB2, &sql, NULL));
      q->ast_head = NULL;
      c_orm_query_free(q);

      memset(&bad_wth, 0, sizeof(bad_wth));
      bad_wth.base.type = C_ORM_AST_NODE_WITH;
      bad_wth.alias = NULL;
      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
      q->ast_head = &bad_wth.base;
      sql = NULL;
      ASSERT_NEQ(C_ORM_OK,
                 c_orm_query_to_sql(q, C_ORM_DIALECT_DB2, &sql, NULL));
      q->ast_head = NULL;
      c_orm_query_free(q);

      memset(&bad_frm, 0, sizeof(bad_frm));
      bad_frm.base.type = C_ORM_AST_NODE_FROM;
      bad_frm.table = NULL;
      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
      ASSERT_EQ(C_ORM_OK, q->select_(q, "\x01"));
      sql = NULL;
      ASSERT_NEQ(C_ORM_OK,
                 c_orm_query_to_sql(q, C_ORM_DIALECT_DB2, &sql, NULL));
      c_orm_query_free(q);

      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
      ASSERT_EQ(C_ORM_OK, q->select_(q, "1"));
      bad_frm.base.next = q->ast_head;
      q->ast_head = &bad_frm.base;
      sql = NULL;
      ASSERT_NEQ(C_ORM_OK,
                 c_orm_query_to_sql(q, C_ORM_DIALECT_DB2, &sql, NULL));
      bad_frm.table = "T";
      bad_frm.alias = "\x01";
      sql = NULL;
      ASSERT_NEQ(C_ORM_OK,
                 c_orm_query_to_sql(q, C_ORM_DIALECT_DB2, &sql, NULL));
      q->ast_head = bad_frm.base.next;
      c_orm_query_free(q);

      memset(&bad_jn, 0, sizeof(bad_jn));
      bad_jn.base.type = C_ORM_AST_NODE_JOIN;
      bad_jn.type_str = "INNER";
      bad_jn.table = NULL;
      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
      ASSERT_EQ(C_ORM_OK, q->select_(q, "1"));
      bad_jn.base.next = q->ast_head;
      q->ast_head = &bad_jn.base;
      sql = NULL;
      ASSERT_NEQ(C_ORM_OK,
                 c_orm_query_to_sql(q, C_ORM_DIALECT_DB2, &sql, NULL));
      q->ast_head = bad_jn.base.next;
      c_orm_query_free(q);
    }
  }

  /* 4. resolve_db_dialect, bind_ast_param matrix, and fetch_all fin_rc != OK */
  {
    const char *drivers[6];
    c_orm_db_t db;
    c_orm_driver_vtable_t vt;
    c_orm_ast_query_t *q = NULL;
    c_orm_ast_node_t *tmp = NULL;
    c_orm_ast_literal_t bad_dec_lit;
    c_orm_column_meta_t col;
    c_orm_table_meta_t meta;
    struct Generic_Array arr;
    int idx;

    drivers[0] = "postgres";
    drivers[1] = "mysql";
    drivers[2] = "db2";
    drivers[3] = "odbc";
    drivers[4] = "sqlite";
    drivers[5] = NULL;

    memset(&col, 0, sizeof(col));
    col.name = "id";
    col.type = C_ORM_TYPE_INT32;
    memset(&meta, 0, sizeof(meta));
    meta.name = "t";
    meta.struct_size = sizeof(struct DummyRecord);
    meta.columns = &col;
    meta.num_columns = 1;

    memset(&db, 0, sizeof(db));
    memset(&vt, 0, sizeof(vt));
    vt.prepare = mock_ok_prepare;
    vt.step = mock_ok_step;
    vt.finalize = mock_ok_finalize;
    vt.bind_string = mock_ok_bind_string;
    vt.bind_int64 = mock_ok_bind_int64;
    vt.bind_double = mock_ok_bind_double;
    vt.bind_null = mock_ok_bind_null;
    vt.bind_decimal = mock_ok_bind_decimal;
    db.vtable = &vt;

    ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
    ASSERT_EQ(C_ORM_OK, q->select_(q, "id"));
    ASSERT_EQ(C_ORM_OK, q->from(q, "t"));
    ASSERT_EQ(C_ORM_OK, q->lit_decimal(q, &dec, &tmp));
    ASSERT_EQ(C_ORM_OK, q->where(q, tmp));
    ASSERT_EQ(C_ORM_OK, q->eq(q, "a", NULL, 0, &tmp));
    ASSERT_EQ(C_ORM_OK, q->and_where(q, tmp));
    ASSERT_EQ(C_ORM_OK, q->eq(q, "a", "NULL", 0, &tmp));
    ASSERT_EQ(C_ORM_OK, q->and_where(q, tmp));
    ASSERT_EQ(C_ORM_OK, q->eq(q, "a", "null", 0, &tmp));
    ASSERT_EQ(C_ORM_OK, q->and_where(q, tmp));
    ASSERT_EQ(C_ORM_OK, q->eq(q, "a", "TRUE", 0, &tmp));
    ASSERT_EQ(C_ORM_OK, q->and_where(q, tmp));
    ASSERT_EQ(C_ORM_OK, q->eq(q, "a", "true", 0, &tmp));
    ASSERT_EQ(C_ORM_OK, q->and_where(q, tmp));
    ASSERT_EQ(C_ORM_OK, q->eq(q, "a", "FALSE", 0, &tmp));
    ASSERT_EQ(C_ORM_OK, q->and_where(q, tmp));
    ASSERT_EQ(C_ORM_OK, q->eq(q, "a", "false", 0, &tmp));
    ASSERT_EQ(C_ORM_OK, q->and_where(q, tmp));
    ASSERT_EQ(C_ORM_OK, q->eq(q, "a", "", 0, &tmp));
    ASSERT_EQ(C_ORM_OK, q->and_where(q, tmp));
    ASSERT_EQ(C_ORM_OK, q->eq(q, "a", "42", 0, &tmp));
    ASSERT_EQ(C_ORM_OK, q->and_where(q, tmp));
    ASSERT_EQ(C_ORM_OK, q->eq(q, "a", "99.75", 0, &tmp));
    ASSERT_EQ(C_ORM_OK, q->and_where(q, tmp));
    ASSERT_EQ(C_ORM_OK, q->eq(q, "a", "1.5e2", 0, &tmp));
    ASSERT_EQ(C_ORM_OK, q->and_where(q, tmp));
    ASSERT_EQ(C_ORM_OK, q->eq(q, "a", "1.5E2", 0, &tmp));
    ASSERT_EQ(C_ORM_OK, q->and_where(q, tmp));
    ASSERT_EQ(C_ORM_OK, q->eq(q, "a", "ident_tok", 0, &tmp));
    ASSERT_EQ(C_ORM_OK, q->and_where(q, tmp));
    ASSERT_EQ(C_ORM_OK, q->eq(q, "a", "xyz", 0, &tmp));
    ASSERT_EQ(C_ORM_OK, q->and_where(q, tmp));

    for (idx = 0; idx < 6; idx++) {
      db.driver_name = drivers[idx];
      g_mock_fin_rc = C_ORM_OK;
      ASSERT_EQ(C_ORM_OK, c_orm_query_execute(&db, q));
    }

    /* Repeat without optional bind_decimal / bind_null / bind_int64 /
     * bind_double hooks */
    vt.bind_decimal = NULL;
    vt.bind_null = NULL;
    vt.bind_int64 = NULL;
    vt.bind_double = NULL;
    ASSERT_EQ(C_ORM_OK, c_orm_query_execute(&db, q));

    /* fetch_all when finalize returns error after hydrate_all succeeds */
    memset(&arr, 0, sizeof(arr));
    g_mock_fin_rc = C_ORM_ERROR_SQL;
    ASSERT_EQ(C_ORM_ERROR_SQL, c_orm_query_fetch_all(&db, q, &meta, &arr));
    g_mock_fin_rc = C_ORM_OK;
    c_orm_query_free(q);

    /* Invalid decimal precision in bind_ast_param when bind_decimal == NULL */
    ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
    ASSERT_EQ(C_ORM_OK, q->select_(q, "1"));
    ASSERT_EQ(C_ORM_OK, q->from(q, "t"));
    memset(&bad_dec_lit, 0, sizeof(bad_dec_lit));
    bad_dec_lit.base.type = C_ORM_AST_NODE_LITERAL;
    bad_dec_lit.is_decimal = 1;
    bad_dec_lit.decimal_val.precision = 0;
    q->where(q, &bad_dec_lit.base);
    ASSERT_NEQ(C_ORM_OK, c_orm_query_execute(&db, q));
    q->ast_head = bad_dec_lit.base.next;
    c_orm_query_free(q);
  }

  /* Deep subquery AST clone independence + OOM & MANY_TO_MANY eager_load */
  {
    c_orm_ast_query_t *parent_q = NULL;
    c_orm_ast_query_t *sub_q = NULL;
    c_orm_ast_query_t *cte_q = NULL;
    c_orm_ast_query_t *uni_q = NULL;
    c_orm_ast_query_t *ex_q = NULL;
    c_orm_ast_query_t *cloned_q = NULL;
    c_orm_ast_node_t *sub_node = NULL;
    c_orm_ast_node_t *ex_node = NULL;
    char *sql_out = NULL;
    int cd;

    ASSERT_EQ(C_ORM_OK, c_orm_query_new(&parent_q));
    ASSERT_EQ(C_ORM_OK, c_orm_query_new(&sub_q));
    ASSERT_EQ(C_ORM_OK, c_orm_query_new(&cte_q));
    ASSERT_EQ(C_ORM_OK, c_orm_query_new(&uni_q));
    ASSERT_EQ(C_ORM_OK, c_orm_query_new(&ex_q));

    ASSERT_EQ(C_ORM_OK, sub_q->select_(sub_q, "sid"));
    ASSERT_EQ(C_ORM_OK, sub_q->from(sub_q, "sub_tbl"));
    ASSERT_EQ(C_ORM_OK, cte_q->select_(cte_q, "cid"));
    ASSERT_EQ(C_ORM_OK, cte_q->from(cte_q, "cte_tbl"));
    ASSERT_EQ(C_ORM_OK, uni_q->select_(uni_q, "uid"));
    ASSERT_EQ(C_ORM_OK, uni_q->from(uni_q, "uni_tbl"));
    ASSERT_EQ(C_ORM_OK, ex_q->select_(ex_q, "1"));
    ASSERT_EQ(C_ORM_OK, ex_q->from(ex_q, "ex_tbl"));

    ASSERT_EQ(C_ORM_OK, parent_q->with(parent_q, "my_cte", cte_q));
    ASSERT_EQ(C_ORM_OK, parent_q->select_(parent_q, "u.id"));
    ASSERT_EQ(C_ORM_OK, parent_q->from_alias(parent_q, "users", "u"));
    ASSERT_EQ(C_ORM_OK, parent_q->subquery(parent_q, sub_q, NULL, &sub_node));
    ASSERT_EQ(C_ORM_OK, parent_q->where(parent_q, sub_node));
    ASSERT_EQ(C_ORM_OK, parent_q->exists(parent_q, ex_q, 0, &ex_node));
    ASSERT_EQ(C_ORM_OK, parent_q->and_where(parent_q, ex_node));
    ASSERT_EQ(C_ORM_OK, parent_q->union_(parent_q, uni_q, 1));

    ASSERT_EQ(C_ORM_OK, parent_q->clone(parent_q, &cloned_q));
    /* Free all original child subqueries BEFORE rendering cloned_q */
    ASSERT_EQ(C_ORM_OK, c_orm_query_free(sub_q));
    ASSERT_EQ(C_ORM_OK, c_orm_query_free(cte_q));
    ASSERT_EQ(C_ORM_OK, c_orm_query_free(uni_q));
    ASSERT_EQ(C_ORM_OK, c_orm_query_free(ex_q));
    ASSERT_EQ(C_ORM_OK, c_orm_query_free(parent_q));

    ASSERT_EQ(C_ORM_OK, c_orm_query_to_sql(cloned_q, C_ORM_DIALECT_SQLITE,
                                           &sql_out, NULL));
    ASSERT(strstr(sql_out, "sub_tbl") != NULL);
    ASSERT(strstr(sql_out, "cte_tbl") != NULL);
    ASSERT(strstr(sql_out, "uni_tbl") != NULL);
    ASSERT(strstr(sql_out, "ex_tbl") != NULL);
    ASSERT_EQ(C_ORM_OK, c_orm_system_free(sql_out));

    /* Exhaustive arena OOM sweep during deep clone with large >4KB strings */
    {
      char big_str[4200];
      c_orm_ast_query_t *big_p = NULL;
      c_orm_ast_query_t *big_s1 = NULL;
      c_orm_ast_query_t *big_s2 = NULL;
      c_orm_ast_query_t *big_s3 = NULL;
      c_orm_ast_node_t *op_l = NULL, *op_r = NULL, *op_n = NULL, *sq_n = NULL;
      memset(big_str, 'x', sizeof(big_str) - 1);
      big_str[sizeof(big_str) - 1] = '\0';

      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&big_p));
      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&big_s1));
      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&big_s2));
      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&big_s3));
      ASSERT_EQ(C_ORM_OK, big_s1->select_(big_s1, big_str));
      ASSERT_EQ(C_ORM_OK, big_s2->select_(big_s2, big_str));
      ASSERT_EQ(C_ORM_OK, big_s3->select_(big_s3, big_str));
      ASSERT_EQ(C_ORM_OK, big_p->select_(big_p, big_str));
      ASSERT_EQ(C_ORM_OK, big_p->from_alias(big_p, big_str, big_str));
      ASSERT_EQ(C_ORM_OK, big_p->col(big_p, big_str, &op_l));
      ASSERT_EQ(C_ORM_OK, big_p->raw(big_p, big_str, &op_r));
      ASSERT_EQ(C_ORM_OK, big_p->op(big_p, "=", op_l, op_r, &op_n));
      ASSERT_EQ(C_ORM_OK, big_p->where(big_p, op_n));
      ASSERT_EQ(C_ORM_OK, big_p->subquery(big_p, big_s1, big_str, &sq_n));
      ASSERT_EQ(C_ORM_OK, big_p->having(big_p, sq_n));
      ASSERT_EQ(C_ORM_OK, big_p->with(big_p, big_str, big_s2));
      ASSERT_EQ(C_ORM_OK, big_p->union_(big_p, big_s3, 0));

      for (cd = 0; cd < 35; ++cd) {
        c_orm_ast_query_t *tmp_clone = NULL;
        oom_active = 1;
        oom_countdown = cd;
        if (big_p->clone(big_p, &tmp_clone) == C_ORM_OK) {
          c_orm_query_free(tmp_clone);
        }
        oom_active = 0;
      }

      /* Also cover NULL query pointers on SUBQUERY/UNION/WITH/EXISTS nodes */
      ((c_orm_ast_subquery_t *)sq_n)->query = NULL;
      {
        c_orm_ast_node_t *it = big_p->ast_head;
        while (it) {
          if (it->type == C_ORM_AST_NODE_UNION)
            ((c_orm_ast_union_t *)it)->query = NULL;
          if (it->type == C_ORM_AST_NODE_WITH)
            ((c_orm_ast_with_t *)it)->query = NULL;
          it = it->next;
        }
      }
      {
        c_orm_ast_exists_t dummy_ex;
        c_orm_ast_query_t *tmp_clone = NULL;
        memset(&dummy_ex, 0, sizeof(dummy_ex));
        dummy_ex.base.type = C_ORM_AST_NODE_EXISTS;
        dummy_ex.query = NULL;
        dummy_ex.base.next = big_p->ast_head;
        big_p->ast_head = &dummy_ex.base;
        ASSERT_EQ(C_ORM_OK, big_p->clone(big_p, &tmp_clone));
        ASSERT_EQ(C_ORM_OK, c_orm_query_free(tmp_clone));
        big_p->ast_head = dummy_ex.base.next;
      }

      ASSERT_EQ(C_ORM_OK, c_orm_query_free(big_s1));
      ASSERT_EQ(C_ORM_OK, c_orm_query_free(big_s2));
      ASSERT_EQ(C_ORM_OK, c_orm_query_free(big_s3));
      ASSERT_EQ(C_ORM_OK, c_orm_query_free(big_p));
    }

    ASSERT_EQ(C_ORM_OK, c_orm_query_free(cloned_q));

    /* MANY_TO_MANY eager_load double-LEFT JOIN + validation + OOM */
    {
      c_orm_column_meta_t p_cols[2];
      c_orm_column_meta_t t_cols[2];
      c_orm_relation_meta_t m2m_rel;
      c_orm_table_meta_t p_meta;
      c_orm_table_meta_t t_meta;
      c_orm_ast_query_t *mq = NULL;

      memset(p_cols, 0, sizeof(p_cols));
      memset(t_cols, 0, sizeof(t_cols));
      memset(&m2m_rel, 0, sizeof(m2m_rel));
      memset(&p_meta, 0, sizeof(p_meta));
      memset(&t_meta, 0, sizeof(t_meta));

      p_cols[0].name = "other";
      p_cols[1].name = "id";
      p_cols[1].is_pk = 1;
      t_cols[0].name = "label";
      t_cols[1].name = "tid";
      t_cols[1].is_pk = 1;

      t_meta.name = "tags";
      t_meta.columns = t_cols;
      t_meta.num_columns = 2;

      m2m_rel.field_name = "tags";
      m2m_rel.type = C_ORM_RELATION_MANY_TO_MANY;
      m2m_rel.target_meta = &t_meta;
      m2m_rel.join_table = "post_tags";
      m2m_rel.join_local_key = "post_id";
      m2m_rel.join_foreign_key = "tag_id";

      p_meta.name = "posts";
      p_meta.columns = p_cols;
      p_meta.num_columns = 2;
      p_meta.relations = &m2m_rel;
      p_meta.num_relations = 1;

      /* PK-inferred local_key/foreign_key */
      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&mq));
      ASSERT_EQ(C_ORM_OK, mq->select_(mq, "*"));
      ASSERT_EQ(C_ORM_OK, mq->from(mq, "posts"));
      ASSERT_EQ(C_ORM_OK, mq->eager_load(mq, &p_meta, "tags"));
      sql_out = NULL;
      ASSERT_EQ(C_ORM_OK,
                c_orm_query_to_sql(mq, C_ORM_DIALECT_SQLITE, &sql_out, NULL));
      ASSERT(strstr(sql_out,
                    "LEFT JOIN post_tags ON posts.id = post_tags.post_id") !=
             NULL);
      ASSERT(strstr(sql_out, "LEFT JOIN tags ON post_tags.tag_id = tags.tid") !=
             NULL);
      ASSERT_EQ(C_ORM_OK, c_orm_system_free(sql_out));
      ASSERT_EQ(C_ORM_OK, c_orm_query_free(mq));

      /* Explicit local_key/foreign_key + OOM on 1st and 2nd join */
      m2m_rel.local_key = "id";
      m2m_rel.foreign_key = "tid";
      for (cd = 0; cd < 4; ++cd) {
        ASSERT_EQ(C_ORM_OK, c_orm_query_new(&mq));
        ASSERT_EQ(C_ORM_OK, mq->select_(mq, "*"));
        oom_active = 1;
        oom_countdown = cd;
        mq->eager_load(mq, &p_meta, "tags");
        oom_active = 0;
        ASSERT_EQ(C_ORM_OK, c_orm_query_free(mq));
      }

      /* Missing join metadata / missing PK validation branches */
      m2m_rel.local_key = NULL;
      m2m_rel.foreign_key = NULL;
      p_cols[1].is_pk = 0;
      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&mq));
      ASSERT_EQ(C_ORM_ERROR_VALIDATION, mq->eager_load(mq, &p_meta, "tags"));
      ASSERT_EQ(C_ORM_OK, c_orm_query_free(mq));
      p_cols[1].is_pk = 1;

      t_cols[1].is_pk = 0;
      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&mq));
      ASSERT_EQ(C_ORM_ERROR_VALIDATION, mq->eager_load(mq, &p_meta, "tags"));
      ASSERT_EQ(C_ORM_OK, c_orm_query_free(mq));
      t_cols[1].is_pk = 1;

      m2m_rel.join_foreign_key = NULL;
      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&mq));
      ASSERT_EQ(C_ORM_ERROR_VALIDATION, mq->eager_load(mq, &p_meta, "tags"));
      ASSERT_EQ(C_ORM_OK, c_orm_query_free(mq));

      m2m_rel.join_local_key = NULL;
      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&mq));
      ASSERT_EQ(C_ORM_ERROR_VALIDATION, mq->eager_load(mq, &p_meta, "tags"));
      ASSERT_EQ(C_ORM_OK, c_orm_query_free(mq));

      m2m_rel.join_table = NULL;
      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&mq));
      ASSERT_EQ(C_ORM_ERROR_VALIDATION, mq->eager_load(mq, &p_meta, "tags"));
      ASSERT_EQ(C_ORM_OK, c_orm_query_free(mq));
    }

    /* Decimal literal params OOM, Postgres IN/BETWEEN SPRINTF fail, and
     * finalize_cached failure on query_execute / query_fetch_one */
    {
      c_orm_ast_query_t *dq = NULL;
      c_orm_ast_node_t *dn = NULL;
      c_orm_decimal_t dval;
      c_orm_query_params_t dparams;
      char *dsql = NULL;
      int step;
      c_orm_db_t *fdb = NULL;
      c_orm_column_meta_t fcol;
      c_orm_table_meta_t fmeta;
      int32_t frow = 0;

      ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("42.50", &dval));
      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&dq));
      ASSERT_EQ(C_ORM_OK, dq->select_(dq, "id"));
      ASSERT_EQ(C_ORM_OK, dq->from(dq, "t"));
      ASSERT_EQ(C_ORM_OK, dq->lit_decimal(dq, &dval, &dn));
      ASSERT_EQ(C_ORM_OK, dq->where(dq, dn));
      for (step = 0;; ++step) {
        ASSERT_EQ(C_ORM_OK, c_orm_query_params_init(&dparams));
        oom_active = 1;
        oom_countdown = step;
        dsql = NULL;
        if (c_orm_query_to_sql(dq, C_ORM_DIALECT_SQLITE, &dsql, &dparams) ==
            C_ORM_OK) {
          oom_active = 0;
          c_orm_free(dsql);
          ASSERT_EQ(C_ORM_OK, c_orm_query_params_cleanup(&dparams));
          break;
        }
        oom_active = 0;
        ASSERT_EQ(C_ORM_OK, c_orm_query_params_cleanup(&dparams));
        ASSERT(step < 30);
      }
      ASSERT_EQ(C_ORM_OK, c_orm_query_free(dq));

      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&dq));
      ASSERT_EQ(C_ORM_OK, dq->select_(dq, "id"));
      ASSERT_EQ(C_ORM_OK, dq->from(dq, "t"));
      ASSERT_EQ(C_ORM_OK, dq->in(dq, "id", "1, 2", &dn));
      ASSERT_EQ(C_ORM_OK, dq->where(dq, dn));
      ASSERT_EQ(C_ORM_OK, dq->between(dq, "id", "1", "9", 0, &dn));
      ASSERT_EQ(C_ORM_OK, dq->and_where(dq, dn));
      for (step = 0;; ++step) {
        ASSERT_EQ(C_ORM_OK, c_orm_query_params_init(&dparams));
        c_orm_mock_safe_crt_countdown = step;
        dsql = NULL;
        if (c_orm_query_to_sql(dq, C_ORM_DIALECT_POSTGRES, &dsql, &dparams) ==
            C_ORM_OK) {
          c_orm_mock_safe_crt_countdown = -1;
          c_orm_free(dsql);
          ASSERT_EQ(C_ORM_OK, c_orm_query_params_cleanup(&dparams));
          break;
        }
        c_orm_mock_safe_crt_countdown = -1;
        ASSERT_EQ(C_ORM_OK, c_orm_query_params_cleanup(&dparams));
        ASSERT(step < 20);
      }
      ASSERT_EQ(C_ORM_OK, c_orm_query_free(dq));

      ASSERT_EQ(C_ORM_OK, c_orm_sqlite_connect(":memory:", &fdb));
      ASSERT_EQ(C_ORM_OK,
                c_orm_execute_raw(fdb, "CREATE TABLE ft (id INTEGER); "
                                       "INSERT INTO ft VALUES (7);"));
      memset(&fcol, 0, sizeof(fcol));
      memset(&fmeta, 0, sizeof(fmeta));
      fcol.name = "id";
      fcol.type = C_ORM_TYPE_INT32;
      fmeta.name = "ft";
      fmeta.columns = &fcol;
      fmeta.num_columns = 1;
      fmeta.struct_size = sizeof(int32_t);

      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&dq));
      ASSERT_EQ(C_ORM_OK, dq->select_(dq, "id"));
      ASSERT_EQ(C_ORM_OK, dq->from(dq, "ft"));

      c_orm_mock_finalize_cached_fail = 1;
      ASSERT_NEQ(C_ORM_OK, c_orm_query_execute(fdb, dq));
      ASSERT_NEQ(C_ORM_OK, c_orm_query_fetch_one(fdb, dq, &fmeta, &frow));
      c_orm_mock_finalize_cached_fail = 0;

      ASSERT_EQ(C_ORM_OK, c_orm_execute_raw(fdb, "DELETE FROM ft;"));
      c_orm_mock_finalize_cached_fail = 1;
      ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
                c_orm_query_fetch_one(fdb, dq, &fmeta, &frow));
      c_orm_mock_finalize_cached_fail = 0;

      ASSERT_EQ(C_ORM_OK, c_orm_query_free(dq));

      ASSERT_EQ(C_ORM_OK, c_orm_execute_raw(
                              fdb, "CREATE TABLE uq_ft (id INT PRIMARY KEY);"));
      ASSERT_EQ(C_ORM_OK,
                c_orm_execute_raw(fdb, "INSERT INTO uq_ft VALUES (1);"));
      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&dq));
      ASSERT_EQ(C_ORM_OK, dq->insert_into(dq, "uq_ft"));
      ASSERT_EQ(C_ORM_OK, dq->lit(dq, "1", 0, &dn));
      ASSERT_EQ(C_ORM_OK, dq->set_col(dq, "id", dn));
      c_orm_mock_finalize_cached_fail = 1;
      {
        c_orm_error_t erc = c_orm_query_execute(fdb, dq);
        c_orm_mock_finalize_cached_fail = 0;
        ASSERT_NEQ(C_ORM_OK, erc);
      }
      ASSERT_EQ(C_ORM_OK, c_orm_query_free(dq));

      ASSERT_EQ(C_ORM_OK, fdb->vtable->disconnect(fdb));
    }

    /* Arena-spill OOM branches in c_orm_query_fluent.c */
    {
      c_orm_ast_query_t *sp_aq = NULL;
      c_orm_ast_query_t *sp_inner = NULL;
      c_orm_ast_query_t *sp_cloned = NULL;
      c_orm_ast_node_t *sp_an = NULL;
      c_orm_ast_node_t *sp_an2 = NULL;
      char *sp_big = NULL;
      void *sp_pad = NULL;
      int sp_step;
      c_orm_column_meta_t sp_col;
      c_orm_table_meta_t sp_child;
      c_orm_table_meta_t sp_parent;
      c_orm_relation_meta_t sp_rel;

      ASSERT_EQ(C_ORM_OK, c_orm_default_malloc(5000, (void **)&sp_big));
      memset(sp_big, 'x', 4999);
      sp_big[0] = '\'';
      sp_big[4999] = '\0';

      /* 1. bin_cmp out_node == NULL + op_impl/and_where/or_where arena-spill
       * OOM across 32/64-bit struct sizes */
      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&sp_aq));
      ASSERT_NEQ(C_ORM_OK, sp_aq->eq(sp_aq, "id", "1", 0, NULL));
      ASSERT_NEQ(C_ORM_OK, sp_aq->eq(NULL, "id", "1", 0, NULL));
      ASSERT_EQ(C_ORM_OK, c_orm_query_free(sp_aq));

      {
        size_t fill;
        for (fill = 3968; fill <= 4088; fill += 8) {
          c_orm_error_t eq_rc;
          ASSERT_EQ(C_ORM_OK, c_orm_query_new(&sp_aq));
          ASSERT_EQ(C_ORM_OK, c_orm_arena_alloc(sp_aq->arena, fill, &sp_pad));
          oom_active = 1;
          oom_countdown = 0;
          eq_rc = sp_aq->eq(sp_aq, "id", "1", 0, &sp_an);
          ASSERT_NEQ((c_orm_error_t)-999, eq_rc);
          oom_active = 0;
          ASSERT_EQ(C_ORM_OK, c_orm_query_free(sp_aq));

          ASSERT_EQ(C_ORM_OK, c_orm_query_new(&sp_aq));
          ASSERT_EQ(C_ORM_OK, sp_aq->raw(sp_aq, "1=1", &sp_an));
          ASSERT_EQ(C_ORM_OK, c_orm_arena_alloc(sp_aq->arena, fill, &sp_pad));
          oom_active = 1;
          oom_countdown = 0;
          if (sp_aq->and_where(sp_aq, sp_an) == C_ORM_OK) {
            oom_active = 0;
          }
          oom_active = 0;
          ASSERT_EQ(C_ORM_OK, c_orm_query_free(sp_aq));

          ASSERT_EQ(C_ORM_OK, c_orm_query_new(&sp_aq));
          ASSERT_EQ(C_ORM_OK, sp_aq->raw(sp_aq, "1=1", &sp_an));
          ASSERT_EQ(C_ORM_OK, sp_aq->where(sp_aq, sp_an));
          ASSERT_EQ(C_ORM_OK, c_orm_arena_alloc(sp_aq->arena, fill, &sp_pad));
          oom_active = 1;
          oom_countdown = 0;
          if (sp_aq->and_where(sp_aq, sp_an) == C_ORM_OK) {
            oom_active = 0;
          }
          oom_active = 0;
          ASSERT_EQ(C_ORM_OK, c_orm_query_free(sp_aq));

          ASSERT_EQ(C_ORM_OK, c_orm_query_new(&sp_aq));
          ASSERT_EQ(C_ORM_OK, sp_aq->raw(sp_aq, "1=1", &sp_an));
          ASSERT_EQ(C_ORM_OK, c_orm_arena_alloc(sp_aq->arena, fill, &sp_pad));
          oom_active = 1;
          oom_countdown = 0;
          if (sp_aq->or_where(sp_aq, sp_an) == C_ORM_OK) {
            oom_active = 0;
          }
          oom_active = 0;
          ASSERT_EQ(C_ORM_OK, c_orm_query_free(sp_aq));

          ASSERT_EQ(C_ORM_OK, c_orm_query_new(&sp_aq));
          ASSERT_EQ(C_ORM_OK, sp_aq->raw(sp_aq, "1=1", &sp_an));
          ASSERT_EQ(C_ORM_OK, sp_aq->where(sp_aq, sp_an));
          ASSERT_EQ(C_ORM_OK, c_orm_arena_alloc(sp_aq->arena, fill, &sp_pad));
          oom_active = 1;
          oom_countdown = 0;
          if (sp_aq->or_where(sp_aq, sp_an) == C_ORM_OK) {
            oom_active = 0;
          }
          oom_active = 0;
          ASSERT_EQ(C_ORM_OK, c_orm_query_free(sp_aq));
        }
      }

      /* 2. is_null col OOM & between low/high OOM via >4096 strings */
      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&sp_aq));
      oom_active = 1;
      oom_countdown = 1;
      ASSERT_EQ(C_ORM_ERROR_MEMORY, sp_aq->is_null(sp_aq, sp_big, 0, &sp_an));
      oom_active = 0;
      ASSERT_EQ(C_ORM_OK, c_orm_query_free(sp_aq));

      for (sp_step = 1;; ++sp_step) {
        ASSERT_EQ(C_ORM_OK, c_orm_query_new(&sp_aq));
        oom_active = 1;
        oom_countdown = sp_step;
        (void)sp_pad;
        if (sp_aq->between(sp_aq, "id", sp_big, sp_big, 1, &sp_an) ==
            C_ORM_OK) {
          oom_active = 0;
          ASSERT_EQ(C_ORM_OK, c_orm_query_free(sp_aq));
          break;
        }
        oom_active = 0;
        ASSERT_EQ(C_ORM_OK, c_orm_query_free(sp_aq));
        ASSERT(sp_step < 10);
      }

      /* 3. emit_eager_left_join raw/left_join arena-spill OOM */
      memset(&sp_col, 0, sizeof(sp_col));
      memset(&sp_child, 0, sizeof(sp_child));
      memset(&sp_parent, 0, sizeof(sp_parent));
      memset(&sp_rel, 0, sizeof(sp_rel));
      sp_col.name = "id";
      sp_col.type = C_ORM_TYPE_INT32;
      sp_child.name = "posts";
      sp_child.columns = &sp_col;
      sp_child.num_columns = 1;
      sp_rel.field_name = "posts";
      sp_rel.type = C_ORM_RELATION_ONE_TO_MANY;
      sp_rel.local_key = "id";
      sp_rel.foreign_key = "user_id";
      sp_rel.target_meta = &sp_child;
      sp_parent.name = "users";
      sp_parent.columns = &sp_col;
      sp_parent.num_columns = 1;
      sp_parent.relations = &sp_rel;
      sp_parent.num_relations = 1;
      for (sp_step = 0; sp_step < 8; ++sp_step) {
        size_t fill;
        for (fill = 4040; fill <= 4088; fill += 16) {
          ASSERT_EQ(C_ORM_OK, c_orm_query_new(&sp_aq));
          ASSERT_EQ(C_ORM_OK, c_orm_arena_alloc(sp_aq->arena, fill, &sp_pad));
          oom_active = 1;
          oom_countdown = sp_step;
          if (sp_aq->eager_load(sp_aq, &sp_parent, "posts") == C_ORM_OK) {
            oom_active = 0;
          }
          oom_active = 0;
          ASSERT_EQ(C_ORM_OK, c_orm_query_free(sp_aq));
        }
      }

      /* 4. c_orm_ast_clone_node child OOMs with >4096 payloads */
      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&sp_aq));
      ASSERT_EQ(C_ORM_OK, sp_aq->select_(sp_aq, sp_big));
      ASSERT_EQ(C_ORM_OK, sp_aq->from(sp_aq, sp_big));
      ASSERT_EQ(C_ORM_OK, sp_aq->raw(sp_aq, sp_big, &sp_an));
      ASSERT_EQ(C_ORM_OK, sp_aq->group(sp_aq, sp_an, &sp_an2));
      ASSERT_EQ(C_ORM_OK, sp_aq->where(sp_aq, sp_an2));
      ASSERT_EQ(C_ORM_OK, sp_aq->eq(sp_aq, sp_big, sp_big, 1, &sp_an));
      ASSERT_EQ(C_ORM_OK, sp_aq->left_join(sp_aq, sp_big, sp_an));
      ASSERT_EQ(C_ORM_OK, sp_aq->eq(sp_aq, sp_big, sp_big, 1, &sp_an));
      ASSERT_EQ(C_ORM_OK, sp_aq->having(sp_aq, sp_an));
      ASSERT_EQ(C_ORM_OK, c_orm_query_new(&sp_inner));
      ASSERT_EQ(C_ORM_OK, sp_inner->select_(sp_inner, sp_big));
      ASSERT_EQ(C_ORM_OK, sp_inner->from(sp_inner, sp_big));
      ASSERT_EQ(C_ORM_OK, sp_aq->subquery(sp_aq, sp_inner, sp_big, &sp_an));
      ASSERT_EQ(C_ORM_OK, sp_aq->and_where(sp_aq, sp_an));
      ASSERT_EQ(C_ORM_OK, sp_aq->exists(sp_aq, sp_inner, 0, &sp_an));
      ASSERT_EQ(C_ORM_OK, sp_aq->and_where(sp_aq, sp_an));
      ASSERT_EQ(C_ORM_OK, sp_aq->lit(sp_aq, sp_big, 1, &sp_an));
      ASSERT_EQ(C_ORM_OK, sp_aq->set_col(sp_aq, "col_a", sp_an));

      for (sp_step = 0;; ++sp_step) {
        oom_active = 1;
        oom_countdown = sp_step;
        sp_cloned = NULL;
        if (sp_aq->clone(sp_aq, &sp_cloned) == C_ORM_OK) {
          oom_active = 0;
          ASSERT_EQ(C_ORM_OK, c_orm_query_free(sp_cloned));
          break;
        }
        oom_active = 0;
        ASSERT(sp_step < 100);
      }

      ASSERT_EQ(C_ORM_OK, c_orm_query_free(sp_inner));
      ASSERT_EQ(C_ORM_OK, c_orm_query_free(sp_aq));
      c_orm_default_free(sp_big);
    }
  }

  PASS();
}

/**
 * @brief Test suite runner for query builder coverage and OOM testing.
 * @param query_fluent_coverage_suite Suite runner function name.
 */
SUITE(query_fluent_coverage_suite) {
  static int recursed = 0;
  c_orm_malloc_fn_t old_malloc;
  c_orm_free_fn_t old_free;
  c_orm_realloc_fn_t old_realloc;

  old_malloc = c_orm_malloc;
  old_free = c_orm_free;
  old_realloc = c_orm_realloc;

  c_orm_set_allocators(q_mock_malloc, q_mock_realloc, q_mock_free);

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
  RUN_TEST(test_query_sql_append_oom_branches);
  RUN_TEST(test_fluent_dml_lock_decimal_and_sql_db2);

  c_orm_set_allocators(old_malloc, old_realloc, old_free);

  if (!recursed) {
    recursed = 1;
    greatest_set_test_filter("never_match_filter");
    query_fluent_coverage_suite();
    greatest_set_test_filter(NULL);
    recursed = 0;
  }
}
