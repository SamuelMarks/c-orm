#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file c_orm_query_builder.c
 * @brief Implementation of dynamic SQL query builder.
 */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_query_builder.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "c_orm_log.h"
/* clang-format on */

/** @brief Select builder structure */
struct c_orm_select_builder {
  const c_orm_table_meta_t *meta;
  c_orm_string_builder_t *sb;
  int has_where;
  int has_order;
};

/** @brief Init select builder */
C_ORM_EXPORT c_orm_error_t c_orm_select_builder_init(
    const c_orm_table_meta_t *meta, c_orm_select_builder_t **out_builder) {
  c_orm_error_t rc;
  c_orm_select_builder_t *b;

  LOG_DEBUG("c_orm_select_builder_init: entry");
  if (!meta || !out_builder) {
    LOG_DEBUG("c_orm_select_builder_init: invalid args");
    return C_ORM_ERROR_UNKNOWN;
  }

  b = (c_orm_select_builder_t *)C_ORM_MALLOC(sizeof(c_orm_select_builder_t));
  if (!b) {
    LOG_DEBUG("c_orm_select_builder_init: OOM");
    return C_ORM_ERROR_UNKNOWN;
  }

  rc = c_orm_string_builder_init(&b->sb);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_select_builder_init: string builder init failed");
    C_ORM_FREE(b);
    return rc;
  }

  b->meta = meta;
  b->has_where = 0;
  b->has_order = 0;

  rc = c_orm_string_builder_append(b->sb, "SELECT * FROM ");
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(b->sb);
    C_ORM_FREE(b);
    return rc;
  }
  rc = c_orm_string_builder_append(b->sb, meta->name);
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(b->sb);
    C_ORM_FREE(b);
    return rc;
  }

  *out_builder = b;

  LOG_DEBUG("c_orm_select_builder_init: exit");
  rc = C_ORM_OK;
  return rc;
}

/** @brief Free select builder */
C_ORM_EXPORT void c_orm_select_builder_free(c_orm_select_builder_t *builder) {
  LOG_DEBUG("c_orm_select_builder_free: entry");
  if (builder) {
    c_orm_string_builder_free(builder->sb);
    C_ORM_FREE(builder);
  }
  LOG_DEBUG("c_orm_select_builder_free: exit");
}

/** @brief Compile select builder */
C_ORM_EXPORT c_orm_error_t
c_orm_select_builder_compile(c_orm_select_builder_t *builder, char **out_sql) {
  c_orm_error_t rc;
  const char *sql_str;

  LOG_DEBUG("c_orm_select_builder_compile: entry");
  if (!builder || !out_sql) {
    LOG_DEBUG("c_orm_select_builder_compile: invalid args");
    return C_ORM_ERROR_UNKNOWN;
  }
  rc = c_orm_string_builder_get(builder->sb, &sql_str);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_select_builder_compile: string get failed");
    return rc;
  }
  if (sql_str) {
    size_t len = strlen(sql_str);
    *out_sql = (char *)C_ORM_MALLOC(len + 1);
    if (*out_sql) {
      C_ORM_STRCPY(*out_sql, len + 1, sql_str);
    } else {
      LOG_DEBUG("c_orm_select_builder_compile: OOM");
    }
  }

  LOG_DEBUG("c_orm_select_builder_compile: exit");
  rc = *out_sql ? C_ORM_OK : C_ORM_ERROR_UNKNOWN;
  return rc;
}

/** @brief Append where clause */
static c_orm_error_t append_where(c_orm_select_builder_t *builder,
                                  const char *column, const char *op) {
  c_orm_error_t rc;

  LOG_DEBUG("append_where: entry");
  if (!builder || !column || !op) {
    LOG_DEBUG("append_where: invalid args");
    return C_ORM_ERROR_UNKNOWN;
  }
  if (!builder->has_where) {
    rc = c_orm_string_builder_append(builder->sb, " WHERE ");
    if (rc != C_ORM_OK)
      return rc;
    builder->has_where = 1;
  } else {
    rc = c_orm_string_builder_append(builder->sb, " AND ");
    if (rc != C_ORM_OK)
      return rc;
  }
  rc = c_orm_string_builder_append(builder->sb, column);
  if (rc != C_ORM_OK)
    return rc;
  rc = c_orm_string_builder_append(builder->sb, op);
  if (rc != C_ORM_OK)
    return rc;

  LOG_DEBUG("append_where: exit");
  return C_ORM_OK;
}

/** @brief Select where eq */
C_ORM_EXPORT c_orm_error_t
c_orm_select_where_eq(c_orm_select_builder_t *builder, const char *column) {
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_select_where_eq: entry");
  rc = append_where(builder, column, " = ?");
  if (rc != C_ORM_OK)
    return rc;
  LOG_DEBUG("c_orm_select_where_eq: exit");
  return rc;
}

/** @brief Select where neq */
C_ORM_EXPORT c_orm_error_t
c_orm_select_where_neq(c_orm_select_builder_t *builder, const char *column) {
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_select_where_neq: entry");
  rc = append_where(builder, column, " != ?");
  if (rc != C_ORM_OK)
    return rc;
  LOG_DEBUG("c_orm_select_where_neq: exit");
  return rc;
}

/** @brief Select where lt */
C_ORM_EXPORT c_orm_error_t
c_orm_select_where_lt(c_orm_select_builder_t *builder, const char *column) {
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_select_where_lt: entry");
  rc = append_where(builder, column, " < ?");
  if (rc != C_ORM_OK)
    return rc;
  LOG_DEBUG("c_orm_select_where_lt: exit");
  return rc;
}

/** @brief Select where gt */
C_ORM_EXPORT c_orm_error_t
c_orm_select_where_gt(c_orm_select_builder_t *builder, const char *column) {
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_select_where_gt: entry");
  rc = append_where(builder, column, " > ?");
  if (rc != C_ORM_OK)
    return rc;
  LOG_DEBUG("c_orm_select_where_gt: exit");
  return rc;
}

/** @brief Select where lte */
C_ORM_EXPORT c_orm_error_t
c_orm_select_where_lte(c_orm_select_builder_t *builder, const char *column) {
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_select_where_lte: entry");
  rc = append_where(builder, column, " <= ?");
  if (rc != C_ORM_OK)
    return rc;
  LOG_DEBUG("c_orm_select_where_lte: exit");
  return rc;
}

/** @brief Select where gte */
C_ORM_EXPORT c_orm_error_t
c_orm_select_where_gte(c_orm_select_builder_t *builder, const char *column) {
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_select_where_gte: entry");
  rc = append_where(builder, column, " >= ?");
  if (rc != C_ORM_OK)
    return rc;
  LOG_DEBUG("c_orm_select_where_gte: exit");
  return rc;
}

/** @brief Select where gt current timestamp */
C_ORM_EXPORT c_orm_error_t c_orm_select_where_gt_current_timestamp(
    c_orm_select_builder_t *builder, const char *column) {
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_select_where_gt_current_timestamp: entry");
  rc = append_where(builder, column, " > CURRENT_TIMESTAMP");
  if (rc != C_ORM_OK)
    return rc;
  LOG_DEBUG("c_orm_select_where_gt_current_timestamp: exit");
  return rc;
}

/** @brief Select where lt current timestamp */
C_ORM_EXPORT c_orm_error_t c_orm_select_where_lt_current_timestamp(
    c_orm_select_builder_t *builder, const char *column) {
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_select_where_lt_current_timestamp: entry");
  rc = append_where(builder, column, " < CURRENT_TIMESTAMP");
  if (rc != C_ORM_OK)
    return rc;
  LOG_DEBUG("c_orm_select_where_lt_current_timestamp: exit");
  return rc;
}

/** @brief Select where like */
C_ORM_EXPORT c_orm_error_t
c_orm_select_where_like(c_orm_select_builder_t *builder, const char *column) {
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_select_where_like: entry");
  rc = append_where(builder, column, " LIKE ?");
  if (rc != C_ORM_OK)
    return rc;
  LOG_DEBUG("c_orm_select_where_like: exit");
  return rc;
}

/** @brief Select where in */
C_ORM_EXPORT c_orm_error_t c_orm_select_where_in(
    c_orm_select_builder_t *builder, const char *column, size_t count) {
  c_orm_error_t rc;
  size_t i;

  LOG_DEBUG("c_orm_select_where_in: entry");
  if (!builder || !column || count == 0) {
    LOG_DEBUG("c_orm_select_where_in: invalid args");
    return C_ORM_ERROR_UNKNOWN;
  }
  if (!builder->has_where) {
    rc = c_orm_string_builder_append(builder->sb, " WHERE ");
    if (rc != C_ORM_OK)
      return rc;
    builder->has_where = 1;
  } else {
    rc = c_orm_string_builder_append(builder->sb, " AND ");
    if (rc != C_ORM_OK)
      return rc;
  }
  rc = c_orm_string_builder_append(builder->sb, column);
  if (rc != C_ORM_OK)
    return rc;
  rc = c_orm_string_builder_append(builder->sb, " IN (");
  if (rc != C_ORM_OK)
    return rc;
  for (i = 0; i < count; ++i) {
    rc = c_orm_string_builder_append(builder->sb, "?");
    if (rc != C_ORM_OK)
      return rc;
    if (i < count - 1) {
      rc = c_orm_string_builder_append(builder->sb, ", ");
      if (rc != C_ORM_OK)
        return rc;
    }
  }
  rc = c_orm_string_builder_append(builder->sb, ")");
  if (rc != C_ORM_OK)
    return rc;

  LOG_DEBUG("c_orm_select_where_in: exit");
  rc = C_ORM_OK;
  return rc;
}

/** @brief Select where in array */
C_ORM_EXPORT c_orm_error_t
c_orm_select_where_in_array(c_orm_select_builder_t *builder, const char *column,
                            void *array, const c_orm_table_meta_t *meta) {
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_select_where_in_array: entry");
  if (!builder || !column || !array || !meta) {
    LOG_DEBUG("c_orm_select_where_in_array: invalid args");
    return C_ORM_ERROR_UNKNOWN;
  }

  rc = c_orm_select_where_in(builder, column, 1);
  LOG_DEBUG("c_orm_select_where_in_array: exit");
  return rc;
}

/** @brief Select where between */
C_ORM_EXPORT c_orm_error_t c_orm_select_where_between(
    c_orm_select_builder_t *builder, const char *column) {
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_select_where_between: entry");
  rc = append_where(builder, column, " BETWEEN ? AND ?");
  if (rc != C_ORM_OK)
    return rc;
  LOG_DEBUG("c_orm_select_where_between: exit");
  return rc;
}

/** @brief Select where ilike */
C_ORM_EXPORT c_orm_error_t
c_orm_select_where_ilike(c_orm_select_builder_t *builder, const char *column) {
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_select_where_ilike: entry");
  rc = append_where(builder, column, " ILIKE ?");
  if (rc != C_ORM_OK)
    return rc;
  LOG_DEBUG("c_orm_select_where_ilike: exit");
  return rc;
}

/** @brief Build exists query */
static c_orm_error_t build_exists_query(c_orm_string_builder_t *sb,
                                        const c_orm_table_meta_t *meta,
                                        const char *path,
                                        const char *operator_str,
                                        const char *parent_alias, int depth) {
  c_orm_error_t rc;
  const char *dot;

  LOG_DEBUG("build_exists_query: entry");
  dot = strchr(path, '.');
  if (!dot) {
    /* No dot means it's a column on the current table */
    rc = c_orm_string_builder_append(sb, parent_alias);
    if (rc != C_ORM_OK)
      return rc;
    rc = c_orm_string_builder_append(sb, ".");
    if (rc != C_ORM_OK)
      return rc;
    rc = c_orm_string_builder_append(sb, path);
    if (rc != C_ORM_OK)
      return rc;
    rc = c_orm_string_builder_append(sb, " ");
    if (rc != C_ORM_OK)
      return rc;
    rc = c_orm_string_builder_append(sb, operator_str);
    if (rc != C_ORM_OK)
      return rc;
    rc = c_orm_string_builder_append(sb, " ?");
    if (rc != C_ORM_OK)
      return rc;

    LOG_DEBUG("build_exists_query: exit");
    rc = C_ORM_OK;
    return rc;
  } else {
    char rel_name[64];
    size_t len = (size_t)(dot - path);
    const c_orm_relation_meta_t *rel = NULL;
    size_t i;
    char target_alias[32];

    const char *target_pk = "id"; /* Fallback */

    if (len >= sizeof(rel_name))
      len = sizeof(rel_name) - 1;
    C_ORM_STRNCPY(rel_name, sizeof(rel_name), path, len);
    rel_name[len] = '\0';

    for (i = 0; i < meta->num_relations; i++) {
      if (strcmp(meta->relations[i].field_name, rel_name) == 0) {
        rel = &meta->relations[i];
        break;
      }
    }

    if (!rel || !rel->target_meta) {
      LOG_DEBUG("build_exists_query: invalid rel");
      return C_ORM_ERROR_UNKNOWN;
    }

    for (i = 0; i < rel->target_meta->num_columns; i++) {
      if (rel->target_meta->columns[i].is_pk) {
        target_pk = rel->target_meta->columns[i].name;
        break;
      }
    }

    C_ORM_SPRINTF(target_alias, sizeof(target_alias), "t%d", depth);

    rc = c_orm_string_builder_append(sb, "EXISTS (SELECT 1 FROM ");
    if (rc != C_ORM_OK)
      return rc;
    rc = c_orm_string_builder_append(sb, rel->target_meta->name);
    if (rc != C_ORM_OK)
      return rc;
    rc = c_orm_string_builder_append(sb, " ");
    if (rc != C_ORM_OK)
      return rc;
    rc = c_orm_string_builder_append(sb, target_alias);
    if (rc != C_ORM_OK)
      return rc;

    if (rel->type == C_ORM_RELATION_MANY_TO_MANY) {
      rc = c_orm_string_builder_append(sb, " INNER JOIN ");
      if (rc != C_ORM_OK)
        return rc;
      rc = c_orm_string_builder_append(sb, rel->join_table);
      if (rc != C_ORM_OK)
        return rc;
      rc = c_orm_string_builder_append(sb, " j_");
      if (rc != C_ORM_OK)
        return rc;
      rc = c_orm_string_builder_append(sb, target_alias);
      if (rc != C_ORM_OK)
        return rc;
      rc = c_orm_string_builder_append(sb, " ON ");
      if (rc != C_ORM_OK)
        return rc;
      rc = c_orm_string_builder_append(sb, target_alias);
      if (rc != C_ORM_OK)
        return rc;
      rc = c_orm_string_builder_append(sb, ".");
      if (rc != C_ORM_OK)
        return rc;
      rc = c_orm_string_builder_append(sb, target_pk);
      if (rc != C_ORM_OK)
        return rc;
      rc = c_orm_string_builder_append(sb, " = j_");
      if (rc != C_ORM_OK)
        return rc;
      rc = c_orm_string_builder_append(sb, target_alias);
      if (rc != C_ORM_OK)
        return rc;
      rc = c_orm_string_builder_append(sb, ".");
      if (rc != C_ORM_OK)
        return rc;
      rc = c_orm_string_builder_append(sb, rel->join_foreign_key);
      if (rc != C_ORM_OK)
        return rc;

      rc = c_orm_string_builder_append(sb, " WHERE j_");
      if (rc != C_ORM_OK)
        return rc;
      rc = c_orm_string_builder_append(sb, target_alias);
      if (rc != C_ORM_OK)
        return rc;
      rc = c_orm_string_builder_append(sb, ".");
      if (rc != C_ORM_OK)
        return rc;
      rc = c_orm_string_builder_append(sb, rel->join_local_key);
      if (rc != C_ORM_OK)
        return rc;
      rc = c_orm_string_builder_append(sb, " = ");
      if (rc != C_ORM_OK)
        return rc;
      rc = c_orm_string_builder_append(sb, parent_alias);
      if (rc != C_ORM_OK)
        return rc;
      rc = c_orm_string_builder_append(sb, ".");
      if (rc != C_ORM_OK)
        return rc;
      rc = c_orm_string_builder_append(sb, rel->local_key);
      if (rc != C_ORM_OK)
        return rc;
    } else {
      rc = c_orm_string_builder_append(sb, " WHERE ");
      if (rc != C_ORM_OK)
        return rc;
      rc = c_orm_string_builder_append(sb, target_alias);
      if (rc != C_ORM_OK)
        return rc;
      rc = c_orm_string_builder_append(sb, ".");
      if (rc != C_ORM_OK)
        return rc;
      rc = c_orm_string_builder_append(sb, rel->foreign_key);
      if (rc != C_ORM_OK)
        return rc;
      rc = c_orm_string_builder_append(sb, " = ");
      if (rc != C_ORM_OK)
        return rc;
      rc = c_orm_string_builder_append(sb, parent_alias);
      if (rc != C_ORM_OK)
        return rc;
      rc = c_orm_string_builder_append(sb, ".");
      if (rc != C_ORM_OK)
        return rc;
      rc = c_orm_string_builder_append(sb, rel->local_key);
      if (rc != C_ORM_OK)
        return rc;
    }

    rc = c_orm_string_builder_append(sb, " AND ");
    if (rc != C_ORM_OK)
      return rc;
    rc = build_exists_query(sb, rel->target_meta, dot + 1, operator_str,
                            target_alias, depth + 1);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("build_exists_query: recursive failed");

      return rc;
    }

    rc = c_orm_string_builder_append(sb, ")");
    if (rc != C_ORM_OK)
      return rc;

    LOG_DEBUG("build_exists_query: exit");
    rc = C_ORM_OK;
    return rc;
  }
}

/** @brief Select where relation */
C_ORM_EXPORT c_orm_error_t c_orm_select_where_relation(
    c_orm_select_builder_t *builder, const char *relation_name,
    const char *operator_str) {
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_select_where_relation: entry");
  if (!builder || !relation_name || !operator_str) {
    LOG_DEBUG("c_orm_select_where_relation: invalid args");
    return C_ORM_ERROR_UNKNOWN;
  }
  if (!builder->has_where) {
    rc = c_orm_string_builder_append(builder->sb, " WHERE ");
    if (rc != C_ORM_OK)
      return rc;
    builder->has_where = 1;
  } else {
    rc = c_orm_string_builder_append(builder->sb, " AND ");
    if (rc != C_ORM_OK)
      return rc;
  }

  rc = build_exists_query(builder->sb, builder->meta, relation_name,
                          operator_str, builder->meta->name, 1);
  if (rc != C_ORM_OK)
    return rc;
  LOG_DEBUG("c_orm_select_where_relation: exit");
  return rc;
}

/** @brief Select group by */
C_ORM_EXPORT c_orm_error_t
c_orm_select_group_by(c_orm_select_builder_t *builder, const char *column) {
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_select_group_by: entry");
  if (!builder || !column) {
    LOG_DEBUG("c_orm_select_group_by: invalid args");
    return C_ORM_ERROR_UNKNOWN;
  }
  rc = c_orm_string_builder_append(builder->sb, " GROUP BY ");
  if (rc != C_ORM_OK)
    return rc;
  rc = c_orm_string_builder_append(builder->sb, column);
  if (rc != C_ORM_OK)
    return rc;

  LOG_DEBUG("c_orm_select_group_by: exit");
  rc = C_ORM_OK;
  return rc;
}

/** @brief Select having */
C_ORM_EXPORT c_orm_error_t c_orm_select_having(c_orm_select_builder_t *builder,
                                               const char *clause) {
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_select_having: entry");
  if (!builder || !clause) {
    LOG_DEBUG("c_orm_select_having: invalid args");
    return C_ORM_ERROR_UNKNOWN;
  }
  rc = c_orm_string_builder_append(builder->sb, " HAVING ");
  if (rc != C_ORM_OK)
    return rc;
  rc = c_orm_string_builder_append(builder->sb, clause);
  if (rc != C_ORM_OK)
    return rc;

  LOG_DEBUG("c_orm_select_having: exit");
  rc = C_ORM_OK;
  return rc;
}

/** @brief Select aggregate */
C_ORM_EXPORT c_orm_error_t
c_orm_select_aggregate(c_orm_select_builder_t *builder, const char *func,
                       const char *column, const char *alias) {
  c_orm_error_t rc;
  const char *current_sql;
  char *new_sql;
  size_t extra_len;
  const char *select_star = "SELECT * FROM ";
  const char *from_pos;

  LOG_DEBUG("c_orm_select_aggregate: entry");

  if (!builder || !func || !column || !alias) {
    LOG_DEBUG("c_orm_select_aggregate: invalid args");
    return C_ORM_ERROR_UNKNOWN;
  }
  rc = c_orm_string_builder_get(builder->sb, &current_sql);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_select_aggregate: string get failed");
    return rc;
  }

  from_pos = strstr(current_sql, select_star);
  if (from_pos == current_sql) {
    extra_len = strlen(func) + strlen(column) + strlen(alias) + 32;
    new_sql = (char *)C_ORM_MALLOC(strlen(current_sql) + extra_len);
    if (!new_sql) {
      LOG_DEBUG("c_orm_select_aggregate: OOM new_sql 1");
      return C_ORM_ERROR_UNKNOWN;
    }

    C_ORM_SPRINTF(new_sql, strlen(current_sql) + extra_len,
                  "SELECT %s(%s) AS %s FROM %s", func, column, alias,
                  current_sql + 14);

    c_orm_string_builder_free(builder->sb);
    builder->sb = NULL;
    rc = c_orm_string_builder_init(&builder->sb);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_select_aggregate: reinit fail 1");
      C_ORM_FREE(new_sql);
      return rc;
    }
    rc = c_orm_string_builder_append(builder->sb, new_sql);
    if (rc != C_ORM_OK) {
      C_ORM_FREE(new_sql);
      return rc;
    }
    C_ORM_FREE(new_sql);
  } else {
    from_pos = strstr(current_sql, " FROM ");
    if (from_pos) {
      size_t prefix_len = (size_t)(from_pos - current_sql);
      extra_len = strlen(func) + strlen(column) + strlen(alias) + 32;
      new_sql = (char *)C_ORM_MALLOC(strlen(current_sql) + extra_len);
      if (!new_sql) {
        LOG_DEBUG("c_orm_select_aggregate: OOM new_sql 2");
        return C_ORM_ERROR_MEMORY;
      }

      C_ORM_STRNCPY(new_sql, strlen(current_sql) + extra_len, current_sql,
                    prefix_len);
      new_sql[prefix_len] = '\0';

      C_ORM_SPRINTF(new_sql + prefix_len,
                    strlen(current_sql) + extra_len - prefix_len,
                    ", %s(%s) AS %s%s", func, column, alias, from_pos);

      c_orm_string_builder_free(builder->sb);
      builder->sb = NULL;
      rc = c_orm_string_builder_init(&builder->sb);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_select_aggregate: reinit fail 2");
        C_ORM_FREE(new_sql);
        return rc;
      }
      rc = c_orm_string_builder_append(builder->sb, new_sql);
      if (rc != C_ORM_OK) {
        C_ORM_FREE(new_sql);
        return rc;
      }
      C_ORM_FREE(new_sql);
    }
  }

  LOG_DEBUG("c_orm_select_aggregate: exit");
  rc = C_ORM_OK;
  return rc;
}

/** @brief Select order by */
C_ORM_EXPORT c_orm_error_t c_orm_select_order_by(
    c_orm_select_builder_t *builder, const char *column, int is_desc) {
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_select_order_by: entry");
  if (!builder || !column) {
    LOG_DEBUG("c_orm_select_order_by: invalid args");
    return C_ORM_ERROR_UNKNOWN;
  }
  if (!builder->has_order) {
    rc = c_orm_string_builder_append(builder->sb, " ORDER BY ");
    if (rc != C_ORM_OK)
      return rc;
    builder->has_order = 1;
  } else {
    rc = c_orm_string_builder_append(builder->sb, ", ");
    if (rc != C_ORM_OK)
      return rc;
  }
  rc = c_orm_string_builder_append(builder->sb, column);
  if (rc != C_ORM_OK)
    return rc;
  if (is_desc) {
    rc = c_orm_string_builder_append(builder->sb, " DESC");
    if (rc != C_ORM_OK)
      return rc;
  } else {
    rc = c_orm_string_builder_append(builder->sb, " ASC");
    if (rc != C_ORM_OK)
      return rc;
  }

  LOG_DEBUG("c_orm_select_order_by: exit");
  rc = C_ORM_OK;
  return rc;
}

/** @brief Select limit */
C_ORM_EXPORT c_orm_error_t c_orm_select_limit(c_orm_select_builder_t *builder,
                                              size_t limit) {
  c_orm_error_t rc;
  char buf[32];

  LOG_DEBUG("c_orm_select_limit: entry");
  if (!builder) {
    LOG_DEBUG("c_orm_select_limit: invalid args");
    return C_ORM_ERROR_UNKNOWN;
  }
  C_ORM_SPRINTF(buf, sizeof(buf), " LIMIT " C_ORM_FMT_SIZE_T,
                C_ORM_CAST_SIZE_T(limit));
  rc = c_orm_string_builder_append(builder->sb, buf);
  if (rc != C_ORM_OK)
    return rc;

  LOG_DEBUG("c_orm_select_limit: exit");
  rc = C_ORM_OK;
  return rc;
}

/** @brief Select offset */
C_ORM_EXPORT c_orm_error_t c_orm_select_offset(c_orm_select_builder_t *builder,
                                               size_t offset) {
  c_orm_error_t rc;
  char buf[32];

  LOG_DEBUG("c_orm_select_offset: entry");
  if (!builder) {
    LOG_DEBUG("c_orm_select_offset: invalid args");
    return C_ORM_ERROR_UNKNOWN;
  }
  C_ORM_SPRINTF(buf, sizeof(buf), " OFFSET " C_ORM_FMT_SIZE_T,
                C_ORM_CAST_SIZE_T(offset));
  rc = c_orm_string_builder_append(builder->sb, buf);
  if (rc != C_ORM_OK)
    return rc;

  LOG_DEBUG("c_orm_select_offset: exit");
  rc = C_ORM_OK;
  return rc;
}

/** @brief Insert builder structure */
struct c_orm_insert_builder {
  const c_orm_table_meta_t *meta;
  c_orm_string_builder_t *sb;
};

/** @brief Init insert builder */
C_ORM_EXPORT c_orm_error_t c_orm_insert_builder_init(
    const c_orm_table_meta_t *meta, c_orm_insert_builder_t **out_builder) {
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_insert_builder_init: entry");
  (void)meta;
  (void)out_builder;
  LOG_DEBUG("c_orm_insert_builder_init: exit");
  rc = C_ORM_ERROR_NOT_IMPLEMENTED;
  return rc;
}

/** @brief Free insert builder */
C_ORM_EXPORT void c_orm_insert_builder_free(c_orm_insert_builder_t *builder) {
  LOG_DEBUG("c_orm_insert_builder_free: entry");
  (void)builder;
  LOG_DEBUG("c_orm_insert_builder_free: exit");
}

/** @brief Compile insert builder */
C_ORM_EXPORT c_orm_error_t
c_orm_insert_builder_compile(c_orm_insert_builder_t *builder, char **out_sql) {
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_insert_builder_compile: entry");
  (void)builder;
  (void)out_sql;
  LOG_DEBUG("c_orm_insert_builder_compile: exit");
  rc = C_ORM_ERROR_NOT_IMPLEMENTED;
  return rc;
}

/** @brief Update builder structure */
struct c_orm_update_builder {
  const c_orm_table_meta_t *meta;
  c_orm_string_builder_t *sb;
  int has_set;
  int has_where;
};

/** @brief Init update builder */
C_ORM_EXPORT c_orm_error_t c_orm_update_builder_init(
    const c_orm_table_meta_t *meta, c_orm_update_builder_t **out_builder) {
  c_orm_error_t rc;
  c_orm_update_builder_t *b;

  LOG_DEBUG("c_orm_update_builder_init: entry");
  if (!meta || !out_builder) {
    LOG_DEBUG("c_orm_update_builder_init: invalid args");
    return C_ORM_ERROR_UNKNOWN;
  }

  b = (c_orm_update_builder_t *)C_ORM_MALLOC(sizeof(c_orm_update_builder_t));
  if (!b) {
    LOG_DEBUG("c_orm_update_builder_init: OOM");
    return C_ORM_ERROR_UNKNOWN;
  }

  rc = c_orm_string_builder_init(&b->sb);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_update_builder_init: string builder init failed");
    C_ORM_FREE(b);
    return rc;
  }

  b->meta = meta;
  b->has_set = 0;
  b->has_where = 0;

  rc = c_orm_string_builder_append(b->sb, "UPDATE ");
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(b->sb);
    C_ORM_FREE(b);
    return rc;
  }
  rc = c_orm_string_builder_append(b->sb, meta->name);
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(b->sb);
    C_ORM_FREE(b);
    return rc;
  }
  rc = c_orm_string_builder_append(b->sb, " SET ");
  if (rc != C_ORM_OK) {
    c_orm_string_builder_free(b->sb);
    C_ORM_FREE(b);
    return rc;
  }

  *out_builder = b;

  LOG_DEBUG("c_orm_update_builder_init: exit");
  rc = C_ORM_OK;
  return rc;
}

/** @brief Free update builder */
C_ORM_EXPORT void c_orm_update_builder_free(c_orm_update_builder_t *builder) {
  LOG_DEBUG("c_orm_update_builder_free: entry");
  if (builder) {
    c_orm_string_builder_free(builder->sb);
    C_ORM_FREE(builder);
  }
  LOG_DEBUG("c_orm_update_builder_free: exit");
}

/** @brief Update set */
C_ORM_EXPORT c_orm_error_t c_orm_update_set(c_orm_update_builder_t *builder,
                                            const char *column) {
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_update_set: entry");
  if (!builder || !column) {
    LOG_DEBUG("c_orm_update_set: invalid args");
    return C_ORM_ERROR_UNKNOWN;
  }
  if (builder->has_where) {
    LOG_DEBUG("c_orm_update_set: has where");
    return C_ORM_ERROR_UNKNOWN;
  }

  if (builder->has_set) {
    rc = c_orm_string_builder_append(builder->sb, ", ");
    if (rc != C_ORM_OK)
      return rc;
  }
  rc = c_orm_string_builder_append(builder->sb, column);
  if (rc != C_ORM_OK)
    return rc;
  rc = c_orm_string_builder_append(builder->sb, " = ?");
  if (rc != C_ORM_OK)
    return rc;
  builder->has_set = 1;

  LOG_DEBUG("c_orm_update_set: exit");
  rc = C_ORM_OK;
  return rc;
}

/** @brief Update where eq */
C_ORM_EXPORT c_orm_error_t
c_orm_update_where_eq(c_orm_update_builder_t *builder, const char *column) {
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_update_where_eq: entry");
  if (!builder || !column) {
    LOG_DEBUG("c_orm_update_where_eq: invalid args");
    return C_ORM_ERROR_UNKNOWN;
  }
  if (!builder->has_where) {
    rc = c_orm_string_builder_append(builder->sb, " WHERE ");
    if (rc != C_ORM_OK)
      return rc;
    builder->has_where = 1;
  } else {
    rc = c_orm_string_builder_append(builder->sb, " AND ");
    if (rc != C_ORM_OK)
      return rc;
  }
  rc = c_orm_string_builder_append(builder->sb, column);
  if (rc != C_ORM_OK)
    return rc;
  rc = c_orm_string_builder_append(builder->sb, " = ?");
  if (rc != C_ORM_OK)
    return rc;

  LOG_DEBUG("c_orm_update_where_eq: exit");
  rc = C_ORM_OK;
  return rc;
}

/** @brief Compile update builder */
C_ORM_EXPORT c_orm_error_t
c_orm_update_builder_compile(c_orm_update_builder_t *builder, char **out_sql) {
  c_orm_error_t rc;
  const char *sql_str;

  LOG_DEBUG("c_orm_update_builder_compile: entry");
  if (!builder || !out_sql) {
    LOG_DEBUG("c_orm_update_builder_compile: invalid args");
    return C_ORM_ERROR_UNKNOWN;
  }
  rc = c_orm_string_builder_get(builder->sb, &sql_str);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_update_builder_compile: string get failed");
    return rc;
  }
  if (sql_str) {
    size_t len = strlen(sql_str);
    *out_sql = (char *)C_ORM_MALLOC(len + 1);
    if (*out_sql) {
      C_ORM_STRCPY(*out_sql, len + 1, sql_str);
    } else {
      LOG_DEBUG("c_orm_update_builder_compile: OOM");
    }
  }

  LOG_DEBUG("c_orm_update_builder_compile: exit");
  rc = *out_sql ? C_ORM_OK : C_ORM_ERROR_UNKNOWN;
  return rc;
}

#if defined(__clang__) || defined(__GNUC__)
#endif
