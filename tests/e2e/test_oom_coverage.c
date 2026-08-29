#if defined(__clang__) || defined(__GNUC__)
#endif
/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_db.h"
#include "c_orm_uuid.h"
#include "c_orm_query_builder.h"
#include "c_orm_string_builder.h"
#include "c_orm_log.h"
#include "greatest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "c_orm_codegen.h"
#include "cdd_c_ir.h"
/* clang-format on */

static int oom_countdown = 0;
static int oom_active = 0;
static int alloc_count = 0;

static void *mock_malloc_oom(size_t size) {
  if (oom_active) {
    if (oom_countdown == 0) {
      oom_countdown--;
      return NULL;
    }
    oom_countdown--;
  }
  alloc_count++;
  if (oom_active)
    printf("oom malloc\n");
  return malloc(size);
}

static void mock_free_oom(void *ptr) { free(ptr); }

static void *mock_realloc_oom(void *ptr, size_t size) {
  if (oom_active) {
    if (oom_countdown == 0) {
      oom_countdown--;
      return NULL;
    }
    oom_countdown--;
  }
  alloc_count++;
  if (oom_active)
    printf("oom realloc\n");
  return realloc(ptr, size);
}

/* OOM Fuzzer Macro */
#define OOM_TEST(test_func, max_allocs)                                        \
  do {                                                                         \
    int i;                                                                     \
    for (i = 0; i < max_allocs; i++) {                                         \
      oom_countdown = i;                                                       \
      oom_active = 1;                                                          \
      alloc_count = 0;                                                         \
      test_func();                                                             \
      oom_active = 0;                                                          \
      if (alloc_count < i)                                                     \
        break;                                                                 \
      /* If it succeeded without failing allocator, stop early to save time */ \
      if (alloc_count < i)                                                     \
        break;                                                                 \
    }                                                                          \
  } while (0)

static void do_uuid(void) {
  char uuid_buf[37];
  c_orm_uuid_v4(uuid_buf);
}

static void do_codegen(void) {
  {
    FILE *f;
#if defined(_MSC_VER)
    fopen_s(&f, "oom_schema.sql", "w");
#else
    f = fopen("oom_schema.sql", "w");
#endif

    if (f) {
      fprintf(f, "CREATE TABLE t_oom (id int);\n");
      fclose(f);
    }
  }
  c_orm_codegen_generate("oom_schema.sql", "test_out");
}

TEST test_codegen_oom(void) {
  OOM_TEST(do_codegen, 2);
  PASS();
}
TEST test_uuid_oom(void) {
  OOM_TEST(do_uuid, 2);
  PASS();
}

static void do_string_builder(void) {
  c_orm_string_builder_t *sb = NULL;
  if (c_orm_string_builder_init(&sb) == 0 && sb) {
    c_orm_string_builder_append(sb, "test");
    c_orm_string_builder_free(sb);
  }
}

TEST test_string_builder_oom(void) {
  OOM_TEST(do_string_builder, 2);
  oom_active = 1;
  oom_countdown = 1;
  do_string_builder();
  oom_countdown = 2;
  do_string_builder();
  oom_active = 0;
  PASS();
}

static void do_cdd_c_ir(void) {
  cdd_c_ir_t ir;
  struct sql_table_t tbl;
  cdd_c_query_projection_t proj;
  cdd_c_query_projection_field_t field;

  memset(&tbl, 0, sizeof(tbl));
  memset(&field, 0, sizeof(field));
  field.name = "test";
  field.original_name = "test";

  if (cdd_c_ir_init(&ir) == 0) {
    cdd_c_ir_add_table(&ir, &tbl);
    if (cdd_c_query_projection_init(&proj) == 0) {
      proj.source_table = (char *)malloc(5);
      if (proj.source_table)
#if defined(_MSC_VER)
        strcpy_s(proj.source_table, 5, "test");
#else
        strcpy(proj.source_table, "test");
#endif

      cdd_c_query_projection_add_field(&proj, &field);
      cdd_c_ir_add_projection(&ir, &proj);
      cdd_c_query_projection_free(&proj);
    }
    parse_sql_into_ir("CREATE TABLE x (id INT);", &ir);
    parse_sql_into_ir("SELECT id FROM x;", &ir);
    parse_sql_into_ir("INSERT INTO x (id) VALUES (1) RETURNING id;", &ir);
    cdd_c_ir_free(&ir);
  }
}

static void do_qb_oom(void) {
  c_orm_select_builder_t *sb = NULL;
  c_orm_insert_builder_t *ib = NULL;
  c_orm_update_builder_t *ub = NULL;
  c_orm_table_meta_t meta;
  char *sql = NULL;
  memset(&meta, 0, sizeof(meta));
  meta.name = "12345678901234";

  if (c_orm_select_builder_init(&meta, &sb) == C_ORM_OK && sb) {
    int j;
    for (j = 0; j < 50; j++)
      c_orm_select_where_eq(sb, "1234567");
    c_orm_select_builder_compile(sb, &sql);
    if (sql) {
      c_orm_free(sql);
      sql = NULL;
    }
    c_orm_select_builder_free(sb);
  }

  if (c_orm_insert_builder_init(&meta, &ib) == C_ORM_OK && ib) {
    c_orm_insert_builder_compile(ib, &sql);
    if (sql) {
      c_orm_free(sql);
      sql = NULL;
    }
    c_orm_insert_builder_free(ib);
  }

  if (c_orm_update_builder_init(&meta, &ub) == C_ORM_OK && ub) {
    c_orm_update_set(ub, "1234567");
    c_orm_update_where_eq(ub, "1234567");
    c_orm_update_builder_compile(ub, &sql);
    if (sql) {
      c_orm_free(sql);
      sql = NULL;
    }
    c_orm_update_builder_free(ub);
  }
}

TEST test_qb_oom(void) {
  OOM_TEST(do_qb_oom, 300);
  PASS();
}

TEST test_cdd_c_ir_oom(void) {
  OOM_TEST(do_cdd_c_ir, 2);
  PASS();
}

SUITE(oom_coverage_suite) {
  void *(*old_malloc)(size_t) = c_orm_malloc;
  void (*old_free)(void *) = c_orm_free;
  void *(*old_realloc)(void *, size_t) = c_orm_realloc;

  c_orm_set_allocators(mock_malloc_oom, mock_realloc_oom, c_orm_free);
  c_orm_set_allocators(c_orm_malloc, c_orm_realloc, mock_free_oom);
  c_orm_set_allocators(c_orm_malloc, mock_realloc_oom, c_orm_free);
  /* Not mocking realloc for now because I need mock_realloc_oom if used, but
   * realloc acts like malloc if ptr is NULL */

  RUN_TEST(test_codegen_oom);
  RUN_TEST(test_uuid_oom);
  RUN_TEST(test_string_builder_oom);
  RUN_TEST(test_cdd_c_ir_oom);
  RUN_TEST(test_qb_oom);

  c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);
  c_orm_set_allocators(c_orm_malloc, c_orm_realloc, old_free);
  c_orm_set_allocators(c_orm_malloc, old_realloc, c_orm_free);
}

#if defined(__clang__) || defined(__GNUC__)
#endif
