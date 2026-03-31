#include "c_orm_migrations.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

C_ORM_EXPORT c_orm_error_t c_orm_migration_init_table(c_orm_db_t *db) {
  const char *sql = "CREATE TABLE IF NOT EXISTS _c_orm_migrations ("
                    "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                    "  version VARCHAR(255) NOT NULL UNIQUE,"
                    "  name VARCHAR(255) NOT NULL,"
                    "  applied_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
                    ");";
  return c_orm_execute_raw(db, sql);
}

C_ORM_EXPORT c_orm_error_t c_orm_migration_load_dir(
    const char *dir_path, c_orm_migration_t **out_migrations,
    size_t *out_count) {
  (void)dir_path;
  if (!out_migrations || !out_count)
    return C_ORM_ERROR_VALIDATION;
  /* Not implemented yet because c_fs logic requires handling cross platform
   * directory scanning */
  *out_migrations = NULL;
  *out_count = 0;
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}

C_ORM_EXPORT void c_orm_migration_free_array(c_orm_migration_t *migrations,
                                             size_t count) {
  size_t i;
  if (!migrations)
    return;
  for (i = 0; i < count; i++) {
    if (migrations[i].up_sql)
      free(migrations[i].up_sql);
    if (migrations[i].down_sql)
      free(migrations[i].down_sql);
  }
  free(migrations);
}

C_ORM_EXPORT c_orm_error_t
c_orm_migrate_all(c_orm_db_t *db, const c_orm_migration_t *migrations,
                  size_t count, const c_orm_migration_options_t *options) {
  size_t i;
  c_orm_error_t err;
  char query[1024];

  if (!db || !migrations)
    return C_ORM_ERROR_VALIDATION;

  err = c_orm_migration_init_table(db);
  if (err != C_ORM_OK)
    return err;

  for (i = 0; i < count; i++) {
    const c_orm_migration_t *mig = &migrations[i];
    int has_row = 0;
    c_orm_query_t *q = NULL;

    /* Check if applied */
#if defined(_MSC_VER)
    sprintf_s(query, sizeof(query),
              "SELECT id FROM _c_orm_migrations WHERE version = '%s'",
              mig->version);
#else
    sprintf(query, "SELECT id FROM _c_orm_migrations WHERE version = '%s'",
            mig->version);
#endif

    if (db->vtable->prepare(db, query, &q) == C_ORM_OK) {
      db->vtable->step(q, &has_row);
      db->vtable->finalize(q);
    }

    if (has_row)
      continue; /* Already applied */

    if (options && options->log_cb) {
      char log_msg[1024];
#if defined(_MSC_VER)
      sprintf_s(log_msg, sizeof(log_msg), "Applying migration %s: %s",
                mig->version, mig->name);
#else
      sprintf(log_msg, "Applying migration %s: %s", mig->version, mig->name);
#endif
      options->log_cb(log_msg);
    }

    if (options && options->dry_run)
      continue;

    if (options && options->pre_migrate) {
      err = options->pre_migrate(db, mig, options->user_data);
      if (err != C_ORM_OK)
        return err;
    }

    c_orm_transaction_begin(db);

    if (mig->up_sql && strlen(mig->up_sql) > 0) {
      err = c_orm_execute_raw(db, mig->up_sql);
      if (err != C_ORM_OK) {
        c_orm_transaction_rollback(db);
        return err;
      }
    }

#if defined(_MSC_VER)
    sprintf_s(
        query, sizeof(query),
        "INSERT INTO _c_orm_migrations (version, name) VALUES ('%s', '%s')",
        mig->version, mig->name);
#else
    sprintf(query,
            "INSERT INTO _c_orm_migrations (version, name) VALUES ('%s', '%s')",
            mig->version, mig->name);
#endif
    err = c_orm_execute_raw(db, query);
    if (err != C_ORM_OK) {
      c_orm_transaction_rollback(db);
      return err;
    }

    c_orm_transaction_commit(db);

    if (options && options->post_migrate) {
      err = options->post_migrate(db, mig, options->user_data);
      if (err != C_ORM_OK)
        return err;
    }
  }

  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_migrate_rollback(
    c_orm_db_t *db, const c_orm_migration_t *migrations, size_t count,
    size_t steps, const c_orm_migration_options_t *options) {
  (void)db;
  (void)migrations;
  (void)count;
  (void)steps;
  (void)options;
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}
C_ORM_EXPORT c_orm_error_t c_orm_migration_fetch_table_schema(
    c_orm_db_t *db, const char *table_name, cdd_c_meta_t **out_schema) {
  char sql[256];
  c_orm_query_t *q;
  c_orm_error_t err;
  int has_row;
  size_t cap = 10;
  cdd_c_meta_t *meta;

  if (!db || !table_name || !out_schema)
    return C_ORM_ERROR_MEMORY;

  /* Try SQLite first */
#if defined(_MSC_VER)
  sprintf_s(sql, sizeof(sql), "PRAGMA table_info('%s')", table_name);
#else
  sprintf(sql, "PRAGMA table_info('%s')", table_name);
#endif

  err = c_orm_prepare_cached(db, sql, &q);
  if (err != C_ORM_OK) {
    return err; /* SQLite fallback for now */
  }

  meta = (cdd_c_meta_t *)malloc(sizeof(cdd_c_meta_t));
  if (!meta) {
    c_orm_finalize_cached(db, q);
    return C_ORM_ERROR_MEMORY;
  }
  meta->name = (char *)malloc(strlen(table_name) + 1);
  if (meta->name)
    strcpy((char *)meta->name, table_name);
  meta->size = 0;
  meta->num_props = 0;
  meta->driver_ctx = NULL;
  meta->props = (cdd_c_prop_meta_t *)malloc(sizeof(cdd_c_prop_meta_t) * cap);
  if (!meta->props) {
    if (meta->name)
      free((void *)meta->name);
    free(meta);
    c_orm_finalize_cached(db, q);
    return C_ORM_ERROR_MEMORY;
  }

  while ((err = db->vtable->step(q, &has_row)) == C_ORM_OK && has_row) {
    const char *col_name = NULL;
    const char *col_type = NULL;
    cdd_c_prop_meta_t *prop;

    db->vtable->get_string(q, 1, &col_name);
    db->vtable->get_string(q, 2, &col_type);

    if (!col_name || !col_type)
      continue;

    if (meta->num_props >= cap) {
      cap *= 2;
      meta->props = (cdd_c_prop_meta_t *)realloc(
          (void *)meta->props, sizeof(cdd_c_prop_meta_t) * cap);
    }

    prop = (cdd_c_prop_meta_t *)&meta->props[meta->num_props++];
    memset(prop, 0, sizeof(cdd_c_prop_meta_t));

    prop->name = (char *)malloc(strlen(col_name) + 1);
    if (prop->name)
      strcpy((char *)prop->name, col_name);

    prop->type = (char *)malloc(strlen(col_type) + 1);
    if (prop->type)
      strcpy((char *)prop->type, col_type);
    prop->offset = 0;
  }

  c_orm_finalize_cached(db, q);
  *out_schema = meta;
  return C_ORM_OK;
}

C_ORM_EXPORT void c_orm_migration_free_table_schema(cdd_c_meta_t *schema) {
  size_t i;
  if (!schema)
    return;
  if (schema->name)
    free((void *)schema->name);
  if (schema->props) {
    for (i = 0; i < schema->num_props; i++) {
      if (schema->props[i].name)
        free((void *)schema->props[i].name);
      if (schema->props[i].type)
        free((void *)schema->props[i].type);
    }
    free((void *)schema->props);
  }
  free(schema);
}
C_ORM_EXPORT c_orm_error_t c_orm_migration_get_applied(
    c_orm_db_t *db, c_orm_migration_t **out_migrations, size_t *out_count) {
  c_orm_query_t *q = NULL;
  c_orm_error_t err;
  int has_row;
  size_t count = 0;
  size_t cap = 10;
  c_orm_migration_t *migs;

  if (!db || !out_migrations || !out_count)
    return C_ORM_ERROR_VALIDATION;

  err = c_orm_prepare_cached(
      db, "SELECT version, name FROM _c_orm_migrations ORDER BY id ASC", &q);
  if (err != C_ORM_OK)
    return err;

  migs = (c_orm_migration_t *)malloc(cap * sizeof(c_orm_migration_t));
  if (!migs) {
    c_orm_finalize_cached(db, q);
    return C_ORM_ERROR_MEMORY;
  }

  while ((err = db->vtable->step(q, &has_row)) == C_ORM_OK && has_row) {
    const char *version = NULL;
    const char *name = NULL;

    db->vtable->get_string(q, 0, &version);
    db->vtable->get_string(q, 1, &name);

    if (count >= cap) {
      cap *= 2;
      migs =
          (c_orm_migration_t *)realloc(migs, cap * sizeof(c_orm_migration_t));
    }

    memset(&migs[count], 0, sizeof(c_orm_migration_t));
    if (version) {
#if defined(_MSC_VER)
      strcpy_s(migs[count].version, sizeof(migs[count].version), version);
#else
      strcpy(migs[count].version, version);
#endif
    }
    if (name) {
#if defined(_MSC_VER)
      strcpy_s(migs[count].name, sizeof(migs[count].name), name);
#else
      strcpy(migs[count].name, name);
#endif
    }
    count++;
  }

  c_orm_finalize_cached(db, q);
  *out_migrations = migs;
  *out_count = count;
  return C_ORM_OK;
}
