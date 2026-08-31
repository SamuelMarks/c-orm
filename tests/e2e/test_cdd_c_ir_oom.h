#if defined(__clang__) || defined(__GNUC__)
#endif

/* clang-format off */
#include <stddef.h>
#include <stdlib.h>
/* clang-format on */

static int ir_oom_countdown = 0;
static int ir_oom_active = 0;
static void *(*old_malloc_ir)(size_t) = NULL;
static void *(*old_realloc_ir)(void *, size_t) = NULL;

static void *mock_malloc_ir(size_t size) {
  if (ir_oom_active) {
    if (ir_oom_countdown == 0) {
      ir_oom_countdown--;
      return NULL;
    }
    ir_oom_countdown--;
  }
  return old_malloc_ir ? old_malloc_ir(size) : malloc(size);
}

static void *mock_realloc_ir(void *ptr, size_t size) {
  if (ir_oom_active) {
    if (ir_oom_countdown == 0) {
      ir_oom_countdown--;
      return NULL;
    }
    ir_oom_countdown--;
  }
  return old_realloc_ir ? old_realloc_ir(ptr, size) : realloc(ptr, size);
}

TEST test_cdd_c_ir_oom(void) {
  cdd_c_ir_t ir;
  struct sql_table_t tbl;
  cdd_c_query_projection_t proj;

  memset(&tbl, 0, sizeof(tbl));
  cdd_c_query_projection_init(&proj);
  proj.source_table = test_strdup("test");
  proj.mapping_meta.target_name = test_strdup("test_map");

  old_malloc_ir = c_orm_malloc;
  old_realloc_ir = c_orm_realloc;

  /* add_table realloc fail */
  cdd_c_ir_init(&ir);
  ir_oom_active = 1;
  ir_oom_countdown = 0;
  c_orm_set_allocators(old_malloc_ir, mock_realloc_ir, c_orm_free);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, cdd_c_ir_add_table(&ir, &tbl));

  ir_oom_active = 0;
  c_orm_set_allocators(old_malloc_ir, old_realloc_ir, c_orm_free);
  cdd_c_ir_free(&ir);

  /* add_projection realloc fail */
  cdd_c_ir_init(&ir);
  ir_oom_active = 1;
  ir_oom_countdown = 0;
  c_orm_set_allocators(old_malloc_ir, mock_realloc_ir, c_orm_free);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, cdd_c_ir_add_projection(&ir, &proj));

  /* duplicate_projection field alloc fail */
  {
    cdd_c_query_projection_t proj_local;
    cdd_c_query_projection_field_t f;
    cdd_c_query_projection_init(&proj_local);
    memset(&f, 0, sizeof(f));
    f.name = test_strdup("f1");
    cdd_c_query_projection_add_field(&proj_local, &f);

    ir_oom_active = 1;
    ir_oom_countdown = 0; /* fail duplicate_string_qp inside add_field */
    c_orm_set_allocators(mock_malloc_ir, old_realloc_ir, c_orm_free);
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, cdd_c_ir_add_projection(&ir, &proj_local));

    ir_oom_active = 1;
    ir_oom_countdown = 0; /* fail new_fields realloc inside add_field */
    c_orm_set_allocators(old_malloc_ir, mock_realloc_ir, c_orm_free);
    ASSERT_EQ(C_ORM_ERROR_UNKNOWN, cdd_c_ir_add_projection(&ir, &proj_local));

    c_orm_set_allocators(old_malloc_ir, old_realloc_ir, c_orm_free);
    cdd_c_query_projection_free(&proj_local);
  }

  /* duplicate_projection malloc fail 1 - source_table */
  ir_oom_active = 1;
  ir_oom_countdown = 0;
  c_orm_set_allocators(mock_malloc_ir, old_realloc_ir, c_orm_free);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, cdd_c_ir_add_projection(&ir, &proj));

  ir_oom_active = 0;
  c_orm_set_allocators(old_malloc_ir, old_realloc_ir, c_orm_free);
  cdd_c_ir_free(&ir);
  cdd_c_ir_init(&ir);

  /* duplicate_projection malloc fail 2 - target_name */
  ir_oom_active = 1;
  ir_oom_countdown = 1; /* skips source_table alloc */
  c_orm_set_allocators(mock_malloc_ir, old_realloc_ir, c_orm_free);
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, cdd_c_ir_add_projection(&ir, &proj));

  ir_oom_active = 0;
  c_orm_set_allocators(old_malloc_ir, old_realloc_ir, c_orm_free);
  cdd_c_ir_free(&ir);

  /* parse_sql_into_ir failure paths */
  cdd_c_ir_init(&ir);
  ir_oom_active = 1;
  /* Malloc counts for "CREATE TABLE x (id INT);"
     0: sql_token_list_t alloc
     1: sql_table_t alloc
     2: table->name alloc
     3: col.name alloc
     4: col.constraints alloc
     we want to trigger cdd_c_ir_add_table's realloc, which uses realloc.
     So we need to let malloc succeed, but realloc fail. */
  ir_oom_active = 0; /* Let token and table parsing succeed. We will fail in
                        cdd_c_ir_add_table which does REALLOC */
  c_orm_set_allocators(old_malloc_ir, old_realloc_ir, c_orm_free);

  ir_oom_active = 1;
  ir_oom_countdown = 2; /* 0: token_list realloc, 1: table->columns realloc, 2:
                           cdd_c_ir_add_table realloc */
  c_orm_set_allocators(old_malloc_ir, mock_realloc_ir, c_orm_free);
  ASSERT(parse_sql_into_ir("CREATE TABLE x (id INT);", &ir) != C_ORM_OK);

  ir_oom_active = 0;
  c_orm_set_allocators(old_malloc_ir, old_realloc_ir, c_orm_free);
  cdd_c_ir_free(&ir);
  cdd_c_ir_init(&ir);

  ir_oom_active = 1;
  ir_oom_countdown = 1;
  c_orm_set_allocators(old_malloc_ir, mock_realloc_ir, c_orm_free);
  ASSERT(parse_sql_into_ir("SELECT id FROM x;", &ir) != C_ORM_OK);

  ir_oom_active = 1;
  ir_oom_countdown = 1; /* 0: token_list MALLOC, 1: sql_parse_select MALLOC */
  c_orm_set_allocators(mock_malloc_ir, old_realloc_ir, c_orm_free);
  ASSERT(parse_sql_into_ir("SELECT id FROM x;", &ir) != C_ORM_OK);

  ir_oom_active = 0;
  c_orm_set_allocators(old_malloc_ir, old_realloc_ir, c_orm_free);
  cdd_c_ir_free(&ir);
  cdd_c_ir_init(&ir);

  ir_oom_active = 1;
  ir_oom_countdown =
      1; /* 0: token_list MALLOC, 1: sql_parse_returning MALLOC */
  c_orm_set_allocators(mock_malloc_ir, old_realloc_ir, c_orm_free);
  ASSERT(parse_sql_into_ir("INSERT INTO x (id) VALUES (1) RETURNING id;",
                           &ir) != C_ORM_OK);

  {
    int i;
    for (i = 0; i < 10; ++i) {
      ir_oom_active = 1;
      ir_oom_countdown = i;
      c_orm_set_allocators(old_malloc_ir, mock_realloc_ir, c_orm_free);
      if (parse_sql_into_ir("INSERT INTO x (id) VALUES (1) RETURNING id;",
                            &ir) != C_ORM_OK) {
        c_orm_set_allocators(old_malloc_ir, old_realloc_ir, c_orm_free);
        cdd_c_ir_free(&ir);
        cdd_c_ir_init(&ir);
      } else {
        c_orm_set_allocators(old_malloc_ir, old_realloc_ir, c_orm_free);
        cdd_c_ir_free(&ir);
        cdd_c_ir_init(&ir);
        break; /* Success means we passed the OOM bounds */
      }
    }
  }

  PASS();
}

#if defined(__clang__) || defined(__GNUC__)
#endif
