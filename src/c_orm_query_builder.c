/**
 * @file c_orm_query_builder.c
 * @brief Implementation of dynamic SQL query builder.
 */

/* clang-format off */
#include "c_orm_query_builder.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
/* clang-format on */

struct c_orm_select_builder {
  const c_orm_table_meta_t *meta;
  c_orm_string_builder_t *sb;
  int has_where;
  int has_order;
};

C_ORM_EXPORT int
c_orm_select_builder_init(const c_orm_table_meta_t *meta,
                          c_orm_select_builder_t **out_builder) {
  c_orm_select_builder_t *b;
  if (!meta || !out_builder)
    return 1;

  b = (c_orm_select_builder_t *)malloc(sizeof(c_orm_select_builder_t));
  if (!b)
    return 1;

  if (c_orm_string_builder_init(&b->sb) != 0) {
    free(b);
    return 1;
  }

  b->meta = meta;
  b->has_where = 0;
  b->has_order = 0;

  c_orm_string_builder_append(b->sb, "SELECT * FROM ");
  c_orm_string_builder_append(b->sb, meta->name);

  *out_builder = b;
  return 0;
}

C_ORM_EXPORT void c_orm_select_builder_free(c_orm_select_builder_t *builder) {
  if (builder) {
    c_orm_string_builder_free(builder->sb);
    free(builder);
  }
}

C_ORM_EXPORT int c_orm_select_builder_compile(c_orm_select_builder_t *builder,
                                              char **out_sql) {
  const char *sql_str;
  if (!builder || !out_sql ||
      c_orm_string_builder_get(builder->sb, &sql_str) != 0)
    return 1;
  if (sql_str) {
    size_t len = strlen(sql_str);
    *out_sql = (char *)malloc(len + 1);
    if (*out_sql) {
#if defined(_MSC_VER)
      strcpy_s(*out_sql, len + 1, sql_str);
#else
      strcpy(*out_sql, sql_str);
#endif
    }
  } else {
    *out_sql = NULL;
  }
  return *out_sql ? 0 : 1;
}

static int append_where(c_orm_select_builder_t *builder, const char *column,
                        const char *op) {
  if (!builder || !column || !op)
    return 1;
  if (!builder->has_where) {
    c_orm_string_builder_append(builder->sb, " WHERE ");
    builder->has_where = 1;
  } else {
    c_orm_string_builder_append(builder->sb, " AND ");
  }
  c_orm_string_builder_append(builder->sb, column);
  c_orm_string_builder_append(builder->sb, op);
  return 0;
}

C_ORM_EXPORT int c_orm_select_where_eq(c_orm_select_builder_t *builder,
                                       const char *column) {
  return append_where(builder, column, " = ?");
}

C_ORM_EXPORT int c_orm_select_where_neq(c_orm_select_builder_t *builder,
                                        const char *column) {
  return append_where(builder, column, " != ?");
}

C_ORM_EXPORT int c_orm_select_where_lt(c_orm_select_builder_t *builder,
                                       const char *column) {
  return append_where(builder, column, " < ?");
}

C_ORM_EXPORT int c_orm_select_where_gt(c_orm_select_builder_t *builder,
                                       const char *column) {
  return append_where(builder, column, " > ?");
}

C_ORM_EXPORT int c_orm_select_where_lte(c_orm_select_builder_t *builder,
                                        const char *column) {
  return append_where(builder, column, " <= ?");
}

C_ORM_EXPORT int c_orm_select_where_gte(c_orm_select_builder_t *builder,
                                        const char *column) {
  return append_where(builder, column, " >= ?");
}

C_ORM_EXPORT int
c_orm_select_where_gt_current_timestamp(c_orm_select_builder_t *builder,
                                        const char *column) {
  return append_where(builder, column, " > CURRENT_TIMESTAMP");
}

C_ORM_EXPORT int
c_orm_select_where_lt_current_timestamp(c_orm_select_builder_t *builder,
                                        const char *column) {
  return append_where(builder, column, " < CURRENT_TIMESTAMP");
}

C_ORM_EXPORT int c_orm_select_where_like(c_orm_select_builder_t *builder,
                                         const char *column) {
  return append_where(builder, column, " LIKE ?");
}

C_ORM_EXPORT int c_orm_select_where_in(c_orm_select_builder_t *builder,
                                       const char *column, size_t count) {
  size_t i;
  if (!builder || !column || count == 0)
    return 1;
  if (!builder->has_where) {
    c_orm_string_builder_append(builder->sb, " WHERE ");
    builder->has_where = 1;
  } else {
    c_orm_string_builder_append(builder->sb, " AND ");
  }
  c_orm_string_builder_append(builder->sb, column);
  c_orm_string_builder_append(builder->sb, " IN (");
  for (i = 0; i < count; ++i) {
    c_orm_string_builder_append(builder->sb, "?");
    if (i < count - 1)
      c_orm_string_builder_append(builder->sb, ", ");
  }
  c_orm_string_builder_append(builder->sb, ")");
  return 0;
}

C_ORM_EXPORT int c_orm_select_order_by(c_orm_select_builder_t *builder,
                                       const char *column, int is_desc) {
  if (!builder || !column)
    return 1;
  if (!builder->has_order) {
    c_orm_string_builder_append(builder->sb, " ORDER BY ");
    builder->has_order = 1;
  } else {
    c_orm_string_builder_append(builder->sb, ", ");
  }
  c_orm_string_builder_append(builder->sb, column);
  if (is_desc) {
    c_orm_string_builder_append(builder->sb, " DESC");
  } else {
    c_orm_string_builder_append(builder->sb, " ASC");
  }
  return 0;
}

C_ORM_EXPORT int c_orm_select_limit(c_orm_select_builder_t *builder,
                                    size_t limit) {
  char buf[32];
  if (!builder)
    return 1;
#if defined(_MSC_VER)
  sprintf_s(buf, sizeof(buf), " LIMIT " C_ORM_FMT_SIZE_T,
            C_ORM_CAST_SIZE_T(limit));
#else
  sprintf(buf, " LIMIT " C_ORM_FMT_SIZE_T, C_ORM_CAST_SIZE_T(limit));
#endif
  c_orm_string_builder_append(builder->sb, buf);
  return 0;
}

C_ORM_EXPORT int c_orm_select_offset(c_orm_select_builder_t *builder,
                                     size_t offset) {
  char buf[32];
  if (!builder)
    return 1;
#if defined(_MSC_VER)
  sprintf_s(buf, sizeof(buf), " OFFSET " C_ORM_FMT_SIZE_T,
            C_ORM_CAST_SIZE_T(offset));
#else
  sprintf(buf, " OFFSET " C_ORM_FMT_SIZE_T, C_ORM_CAST_SIZE_T(offset));
#endif
  c_orm_string_builder_append(builder->sb, buf);
  return 0;
}

/* INSERT BUILDER */
struct c_orm_insert_builder {
  const c_orm_table_meta_t *meta;
  c_orm_string_builder_t *sb;
};

C_ORM_EXPORT int
c_orm_insert_builder_init(const c_orm_table_meta_t *meta,
                          c_orm_insert_builder_t **out_builder) {
  /* Using standard pre-compiled insert for now */
  (void)meta;
  (void)out_builder;
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}

C_ORM_EXPORT void c_orm_insert_builder_free(c_orm_insert_builder_t *builder) {
  (void)builder;
}

C_ORM_EXPORT int c_orm_insert_builder_compile(c_orm_insert_builder_t *builder,
                                              char **out_sql) {
  (void)builder;
  (void)out_sql;
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}

/* UPDATE BUILDER */
struct c_orm_update_builder {
  const c_orm_table_meta_t *meta;
  c_orm_string_builder_t *sb;
  int has_set;
  int has_where;
};

C_ORM_EXPORT int
c_orm_update_builder_init(const c_orm_table_meta_t *meta,
                          c_orm_update_builder_t **out_builder) {
  c_orm_update_builder_t *b;
  if (!meta || !out_builder)
    return 1;

  b = (c_orm_update_builder_t *)malloc(sizeof(c_orm_update_builder_t));
  if (!b)
    return 1;

  if (c_orm_string_builder_init(&b->sb) != 0) {
    free(b);
    return 1;
  }

  b->meta = meta;
  b->has_set = 0;
  b->has_where = 0;

  c_orm_string_builder_append(b->sb, "UPDATE ");
  c_orm_string_builder_append(b->sb, meta->name);
  c_orm_string_builder_append(b->sb, " SET ");

  *out_builder = b;
  return 0;
}

C_ORM_EXPORT void c_orm_update_builder_free(c_orm_update_builder_t *builder) {
  if (builder) {
    c_orm_string_builder_free(builder->sb);
    free(builder);
  }
}

C_ORM_EXPORT int c_orm_update_set(c_orm_update_builder_t *builder,
                                  const char *column) {
  if (!builder || !column)
    return 1;
  if (builder->has_where)
    return 1; /* Cannot SET after WHERE */

  if (builder->has_set) {
    c_orm_string_builder_append(builder->sb, ", ");
  }
  c_orm_string_builder_append(builder->sb, column);
  c_orm_string_builder_append(builder->sb, " = ?");
  builder->has_set = 1;
  return 0;
}

C_ORM_EXPORT int c_orm_update_where_eq(c_orm_update_builder_t *builder,
                                       const char *column) {
  if (!builder || !column)
    return 1;
  if (!builder->has_where) {
    c_orm_string_builder_append(builder->sb, " WHERE ");
    builder->has_where = 1;
  } else {
    c_orm_string_builder_append(builder->sb, " AND ");
  }
  c_orm_string_builder_append(builder->sb, column);
  c_orm_string_builder_append(builder->sb, " = ?");
  return 0;
}

C_ORM_EXPORT int c_orm_update_builder_compile(c_orm_update_builder_t *builder,
                                              char **out_sql) {
  const char *sql_str;
  if (!builder || !out_sql ||
      c_orm_string_builder_get(builder->sb, &sql_str) != 0)
    return 1;
  if (sql_str) {
    size_t len = strlen(sql_str);
    *out_sql = (char *)malloc(len + 1);
    if (*out_sql) {
#if defined(_MSC_VER)
      strcpy_s(*out_sql, len + 1, sql_str);
#else
      strcpy(*out_sql, sql_str);
#endif
    }
  } else {
    *out_sql = NULL;
  }
  return *out_sql ? 0 : 1;
}
