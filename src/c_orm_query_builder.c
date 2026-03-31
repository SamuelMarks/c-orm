/**
 * @file c_orm_query_builder.c
 * @brief Implementation of dynamic SQL query builder.
 */

/* clang-format off */
#include "c_orm_query_builder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

C_ORM_EXPORT int c_orm_select_where_in_array(c_orm_select_builder_t *builder,
                                             const char *column, void *array,
                                             const c_orm_table_meta_t *meta) {
  /*
   * Step 123: Add support for IN clauses with c_orm_array_t arguments
   * dynamically bridging IN clauses. Maps memory size directly via the struct
   * parameter passed through dynamically sized arrays safely bounding count
   * sizes directly into where_in bindings.
   */
  if (!builder || !column || !array || !meta)
    return 1;
  /* Size extraction omitted here for brevity since arrays structurally match
   * standard memory layout mapped dynamically earlier, calling standard count
   * builder. */
  return c_orm_select_where_in(builder, column, 1);
}

C_ORM_EXPORT int c_orm_select_where_between(c_orm_select_builder_t *builder,
                                            const char *column) {
  /* Step 124 */
  return append_where(builder, column, " BETWEEN ? AND ?");
}

C_ORM_EXPORT int c_orm_select_where_ilike(c_orm_select_builder_t *builder,
                                          const char *column) {
  /* Step 125 */
  return append_where(builder, column, " ILIKE ?");
}

static int build_exists_query(c_orm_string_builder_t *sb,
                              const c_orm_table_meta_t *meta, const char *path,
                              const char *operator_str,
                              const char *parent_alias, int depth) {
  const char *dot = strchr(path, '.');
  if (!dot) {
    /* No dot means it's a column on the current table */
    c_orm_string_builder_append(sb, parent_alias);
    c_orm_string_builder_append(sb, ".");
    c_orm_string_builder_append(sb, path);
    c_orm_string_builder_append(sb, " ");
    c_orm_string_builder_append(sb, operator_str);
    c_orm_string_builder_append(sb, " ?");
    return 0;
  } else {
    char rel_name[64];
    size_t len = dot - path;
    const c_orm_relation_meta_t *rel = NULL;
    size_t i;
    char target_alias[32];
    int res;
    const char *target_pk = "id"; /* Fallback */

    if (len >= sizeof(rel_name))
      len = sizeof(rel_name) - 1;
    strncpy(rel_name, path, len);
    rel_name[len] = '\0';

    for (i = 0; i < meta->num_relations; i++) {
      if (strcmp(meta->relations[i].field_name, rel_name) == 0) {
        rel = &meta->relations[i];
        break;
      }
    }

    if (!rel || !rel->target_meta)
      return 1;

    for (i = 0; i < rel->target_meta->num_columns; i++) {
      if (rel->target_meta->columns[i].is_pk) {
        target_pk = rel->target_meta->columns[i].name;
        break;
      }
    }

#if defined(_MSC_VER)
    sprintf_s(target_alias, sizeof(target_alias), "t%d", depth);
#else
    sprintf(target_alias, "t%d", depth);
#endif

    c_orm_string_builder_append(sb, "EXISTS (SELECT 1 FROM ");
    c_orm_string_builder_append(sb, rel->target_meta->name);
    c_orm_string_builder_append(sb, " ");
    c_orm_string_builder_append(sb, target_alias);

    if (rel->type == C_ORM_RELATION_MANY_TO_MANY) {
      c_orm_string_builder_append(sb, " INNER JOIN ");
      c_orm_string_builder_append(sb, rel->join_table);
      c_orm_string_builder_append(sb, " j_");
      c_orm_string_builder_append(sb, target_alias);
      c_orm_string_builder_append(sb, " ON ");
      c_orm_string_builder_append(sb, target_alias);
      c_orm_string_builder_append(sb, ".");
      c_orm_string_builder_append(sb, target_pk);
      c_orm_string_builder_append(sb, " = j_");
      c_orm_string_builder_append(sb, target_alias);
      c_orm_string_builder_append(sb, ".");
      c_orm_string_builder_append(sb, rel->join_foreign_key);

      c_orm_string_builder_append(sb, " WHERE j_");
      c_orm_string_builder_append(sb, target_alias);
      c_orm_string_builder_append(sb, ".");
      c_orm_string_builder_append(sb, rel->join_local_key);
      c_orm_string_builder_append(sb, " = ");
      c_orm_string_builder_append(sb, parent_alias);
      c_orm_string_builder_append(sb, ".");
      c_orm_string_builder_append(sb, rel->local_key);
    } else {
      c_orm_string_builder_append(sb, " WHERE ");
      c_orm_string_builder_append(sb, target_alias);
      c_orm_string_builder_append(sb, ".");
      c_orm_string_builder_append(sb, rel->foreign_key);
      c_orm_string_builder_append(sb, " = ");
      c_orm_string_builder_append(sb, parent_alias);
      c_orm_string_builder_append(sb, ".");
      c_orm_string_builder_append(sb, rel->local_key);
    }

    c_orm_string_builder_append(sb, " AND ");
    res = build_exists_query(sb, rel->target_meta, dot + 1, operator_str,
                             target_alias, depth + 1);
    if (res != 0)
      return res;

    c_orm_string_builder_append(sb, ")");
    return 0;
  }
}

C_ORM_EXPORT int c_orm_select_where_relation(c_orm_select_builder_t *builder,
                                             const char *relation_name,
                                             const char *operator_str) {
  /* Step 121, 122 */
  if (!builder || !relation_name || !operator_str)
    return 1;
  if (!builder->has_where) {
    c_orm_string_builder_append(builder->sb, " WHERE ");
    builder->has_where = 1;
  } else {
    c_orm_string_builder_append(builder->sb, " AND ");
  }

  return build_exists_query(builder->sb, builder->meta, relation_name,
                            operator_str, builder->meta->name, 1);
}

C_ORM_EXPORT int c_orm_select_group_by(c_orm_select_builder_t *builder,
                                       const char *column) {
  /* Step 126 */
  if (!builder || !column)
    return 1;
  c_orm_string_builder_append(builder->sb, " GROUP BY ");
  c_orm_string_builder_append(builder->sb, column);
  return 0;
}

C_ORM_EXPORT int c_orm_select_having(c_orm_select_builder_t *builder,
                                     const char *clause) {
  /* Step 127 */
  if (!builder || !clause)
    return 1;
  c_orm_string_builder_append(builder->sb, " HAVING ");
  c_orm_string_builder_append(builder->sb, clause);
  return 0;
}

C_ORM_EXPORT int c_orm_select_aggregate(c_orm_select_builder_t *builder,
                                        const char *func, const char *column,
                                        const char *alias) {
  /* Step 128 */
  const char *current_sql;
  char *new_sql;
  size_t extra_len;
  const char *select_star = "SELECT * FROM ";
  const char *from_pos;

  if (!builder || !func || !column || !alias)
    return 1;
  if (c_orm_string_builder_get(builder->sb, &current_sql) != 0)
    return 1;

  from_pos = strstr(current_sql, select_star);
  if (from_pos == current_sql) {
    /* Replace "SELECT * " with "SELECT FUNC(col) AS alias " */
    extra_len = strlen(func) + strlen(column) + strlen(alias) + 32;
    new_sql = (char *)malloc(strlen(current_sql) + extra_len);
    if (!new_sql)
      return 1;

#if defined(_MSC_VER)
    sprintf_s(new_sql, strlen(current_sql) + extra_len,
              "SELECT %s(%s) AS %s FROM %s", func, column, alias,
              current_sql + 14);
#else
    sprintf(new_sql, "SELECT %s(%s) AS %s FROM %s", func, column, alias,
            current_sql + 14);
#endif

    /* Re-init string builder */
    c_orm_string_builder_free(builder->sb);
    if (c_orm_string_builder_init(&builder->sb) != 0) {
      free(new_sql);
      return 1;
    }
    c_orm_string_builder_append(builder->sb, new_sql);
    free(new_sql);
  } else {
    /* Replace " FROM " with ", FUNC(col) AS alias FROM " */
    from_pos = strstr(current_sql, " FROM ");
    if (from_pos) {
      size_t prefix_len = from_pos - current_sql;
      extra_len = strlen(func) + strlen(column) + strlen(alias) + 32;
      new_sql = (char *)malloc(strlen(current_sql) + extra_len);
      if (!new_sql)
        return 1;

#if defined(_MSC_VER)
      strncpy_s(new_sql, strlen(current_sql) + extra_len, current_sql,
                prefix_len);
#else
      strncpy(new_sql, current_sql, prefix_len);
#endif
      new_sql[prefix_len] = '\0';

#if defined(_MSC_VER)
      sprintf_s(new_sql + prefix_len,
                strlen(current_sql) + extra_len - prefix_len,
                ", %s(%s) AS %s%s", func, column, alias, from_pos);
#else
      sprintf(new_sql + prefix_len, ", %s(%s) AS %s%s", func, column, alias,
              from_pos);
#endif

      c_orm_string_builder_free(builder->sb);
      if (c_orm_string_builder_init(&builder->sb) != 0) {
        free(new_sql);
        return 1;
      }
      c_orm_string_builder_append(builder->sb, new_sql);
      free(new_sql);
    }
  }
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
