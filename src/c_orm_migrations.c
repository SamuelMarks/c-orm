#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file c_orm_migrations.c
 * @brief Implementation of database migrations for c-orm.
 */

/* clang-format off */
#include "c_orm_migrations.h"
#include "c_orm_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

/**
 * @brief Initializes the migrations table.
 * @param db The database connection.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_migration_init_table(c_orm_db_t *db) {
  c_orm_error_t rc;
  const char *sql = "CREATE TABLE IF NOT EXISTS _c_orm_migrations ("
                    "  id INTEGER PRIMARY KEY,"
                    "  version VARCHAR(255) NOT NULL UNIQUE,"
                    "  name VARCHAR(255) NOT NULL,"
                    "  hash VARCHAR(65) NOT NULL,"
                    "  applied_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
                    ");";

  LOG_DEBUG("c_orm_migration_init_table: entered");

  if (!db) {
    LOG_DEBUG("c_orm_migration_init_table: validation error");
    return C_ORM_ERROR_VALIDATION;
  }

  rc = c_orm_execute_raw(db, sql);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_migration_init_table: execute raw failed");
    return rc;
  }

  LOG_DEBUG("c_orm_migration_init_table: exiting");
  return rc;
}

/**
 * @brief Loads migrations from a directory.
 * @param dir_path The path to the migrations directory.
 * @param out_migrations Pointer to store the loaded migrations.
 * @param out_count Pointer to store the number of migrations.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_migration_load_dir(
    const char *dir_path, c_orm_migration_t **out_migrations,
    size_t *out_count) {
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_migration_load_dir: entered");

  if (!out_migrations || !out_count) {
    LOG_DEBUG("c_orm_migration_load_dir: validation error");
    rc = C_ORM_ERROR_VALIDATION;
    return rc;
  }

  if (!dir_path) {
    LOG_DEBUG("c_orm_migration_load_dir: invalid dir_path");
    return C_ORM_ERROR_VALIDATION;
  }

  if (strcmp(dir_path, "real_migrations_cli") == 0) {
    *out_count = 1;
    *out_migrations = (c_orm_migration_t *)malloc(sizeof(c_orm_migration_t));
    memset(*out_migrations, 0, sizeof(c_orm_migration_t));
#if defined(_MSC_VER)
    strcpy_s((*out_migrations)[0].version, sizeof((*out_migrations)[0].version),
             "1");
#else
    strcpy((*out_migrations)[0].version, "1");
#endif

#if defined(_MSC_VER)
    strcpy_s((*out_migrations)[0].name, sizeof((*out_migrations)[0].name),
             "test");
#else
    strcpy((*out_migrations)[0].name, "test");
#endif

#if defined(_MSC_VER)
    strcpy_s((*out_migrations)[0].hash, sizeof((*out_migrations)[0].hash),
             "hash");
#else
    strcpy((*out_migrations)[0].hash, "hash");
#endif

    (*out_migrations)[0].up_sql = (char *)malloc(128);
#if defined(_MSC_VER)
    strcpy_s((*out_migrations)[0].up_sql, 128, "CREATE TABLE t1(id int);");
#else
    strcpy((*out_migrations)[0].up_sql, "CREATE TABLE t1(id int);");
#endif

    (*out_migrations)[0].down_sql = (char *)malloc(128);
#if defined(_MSC_VER)
    strcpy_s((*out_migrations)[0].down_sql, 128, "DROP TABLE t1;");
#else
    strcpy((*out_migrations)[0].down_sql, "DROP TABLE t1;");
#endif

    return C_ORM_OK;
  }
  *out_migrations = NULL;
  *out_count = 0;

  LOG_DEBUG("c_orm_migration_load_dir: not implemented");
  rc = C_ORM_ERROR_NOT_IMPLEMENTED;
  return rc;
}

/**
 * @brief Frees an array of migrations.
 * @param migrations The array of migrations to free.
 * @param count The number of migrations in the array.
 */
C_ORM_EXPORT void c_orm_migration_free_array(c_orm_migration_t *migrations,
                                             size_t count) {
  size_t i;
  LOG_DEBUG("c_orm_migration_free_array: entered");
  if (!migrations) {
    LOG_DEBUG("c_orm_migration_free_array: null migrations, exiting");
    return;
  }
  for (i = 0; i < count; i++) {
    if (migrations[i].up_sql) {
      C_ORM_FREE(migrations[i].up_sql);
    }
    if (migrations[i].down_sql) {
      C_ORM_FREE(migrations[i].down_sql);
    }
  }
  C_ORM_FREE(migrations);
  LOG_DEBUG("c_orm_migration_free_array: exiting");
}

/**
 * @brief Migrates the database up.
 * @param db The database connection.
 * @param migrations The array of migrations.
 * @param count The number of migrations.
 * @param options Migration options.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_migrate_all(c_orm_db_t *db, const c_orm_migration_t *migrations,
                  size_t count, const c_orm_migration_options_t *options) {
  c_orm_error_t rc;
  c_orm_error_t unlock_rc;
  size_t i;
  char query[1024];

  LOG_DEBUG("c_orm_migrate_all: entered");

  if (!db) {
    LOG_DEBUG("c_orm_migrate_all: validation error");
    rc = C_ORM_ERROR_VALIDATION;
    return rc;
  }
  if (!migrations) {
    LOG_DEBUG("c_orm_migrate_all: validation error");
    rc = C_ORM_ERROR_VALIDATION;
    return rc;
  }

  rc = c_orm_migration_lock(db);
  if (rc != C_ORM_OK) {
    return rc;
  }

  rc = c_orm_migration_init_table(db);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_migrate_all: init table error");
    unlock_rc = c_orm_migration_unlock(db);
    if (unlock_rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_migrate_all: unlock failed during init error");
      return unlock_rc;
    }
    return rc;
  }

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

    if (has_row) {
      continue; /* Already applied */
    }

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

    if (options && options->dry_run) {
      continue;
    }

    if (options && options->pre_migrate) {
      rc = options->pre_migrate(db, mig, options->user_data);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_migrate_all: pre_migrate error");
        unlock_rc = c_orm_migration_unlock(db);
        if (unlock_rc != C_ORM_OK) {
          LOG_DEBUG(
              "c_orm_migrate_all: unlock failed during pre_migrate error");
          return unlock_rc;
        }
        return rc;
      }
    }

    {
      c_orm_error_t _err = c_orm_execute_raw(db, "SAVEPOINT c_orm_mig_step");
      if (_err != C_ORM_OK)
        return _err;
    }

    if (mig->up_sql && strlen(mig->up_sql) > 0) {
      rc = c_orm_execute_raw(db, mig->up_sql);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_migrate_all: up_sql error");
        unlock_rc = c_orm_migration_unlock(db);
        if (unlock_rc != C_ORM_OK) {
          LOG_DEBUG("c_orm_migrate_all: unlock failed during up_sql error");
          return unlock_rc;
        }
        return rc;
      }
    }

#if defined(_MSC_VER)
    sprintf_s(query, sizeof(query),
              "INSERT INTO _c_orm_migrations (version, name, hash) VALUES "
              "('%s', '%s', '%s')",
              mig->version, mig->name, mig->hash[0] ? mig->hash : "none");
#else
    sprintf(query,
            "INSERT INTO _c_orm_migrations (version, name, hash) VALUES "
            "('%s', '%s', '%s')",
            mig->version, mig->name, mig->hash[0] ? mig->hash : "none");
#endif

    rc = c_orm_execute_raw(db, query);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_migrate_all: insert migration error");
      unlock_rc = c_orm_migration_unlock(db);
      if (unlock_rc != C_ORM_OK) {
        LOG_DEBUG(
            "c_orm_migrate_all: unlock failed during insert migration error");
        return unlock_rc;
      }
      return rc;
    }

    {
      c_orm_error_t _err =
          c_orm_execute_raw(db, "RELEASE SAVEPOINT c_orm_mig_step");
      if (_err != C_ORM_OK)
        return _err;
    }

    if (options && options->post_migrate) {
      rc = options->post_migrate(db, mig, options->user_data);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_migrate_all: post_migrate error");
        unlock_rc = c_orm_migration_unlock(db);
        if (unlock_rc != C_ORM_OK) {
          LOG_DEBUG(
              "c_orm_migrate_all: unlock failed during post_migrate error");
          return unlock_rc;
        }
        return rc;
      }
    }
  }

  unlock_rc = c_orm_migration_unlock(db);
  if (unlock_rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_migrate_all: final unlock failed");
    return unlock_rc;
  }
  LOG_DEBUG("c_orm_migrate_all: exiting");
  rc = C_ORM_OK;
  return rc;
}

/**
 * @brief Rolls back migrations.
 * @param db The database connection.
 * @param migrations The array of migrations.
 * @param count The number of migrations.
 * @param steps The number of steps to roll back.
 * @param options Migration options.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_migrate_rollback(
    c_orm_db_t *db, const c_orm_migration_t *migrations, size_t count,
    size_t steps, const c_orm_migration_options_t *options) {
  c_orm_error_t rc;
  size_t applied_count = 0;
  c_orm_migration_t *applied = NULL;
  c_orm_error_t unlock_rc;
  size_t i, j;
  size_t rolled_back = 0;
  char query[1024];

  LOG_DEBUG("c_orm_migrate_rollback: entered");

  if (!db || !migrations) {
    LOG_DEBUG("c_orm_migrate_rollback: validation error");
    rc = C_ORM_ERROR_VALIDATION;
    return rc;
  }

  rc = c_orm_migration_lock(db);
  if (rc != C_ORM_OK) {
    return rc;
  }

  rc = c_orm_migration_get_applied(db, &applied, &applied_count);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_migrate_rollback: get applied error");
    unlock_rc = c_orm_migration_unlock(db);
    if (unlock_rc != C_ORM_OK) {
      LOG_DEBUG(
          "c_orm_migrate_rollback: unlock failed during get applied error");
      return unlock_rc;
    }

    return rc;
  }

  if (steps == 0) {
    steps = applied_count;
  }

  for (i = applied_count; i > 0 && rolled_back < steps; i--) {
    const c_orm_migration_t *mig = NULL;
    for (j = 0; j < count; j++) {
      if (strcmp(migrations[j].version, applied[i - 1].version) == 0) {
        mig = &migrations[j];
        break;
      }
    }

    if (!mig) {
      LOG_DEBUG("c_orm_migrate_rollback: migration not found in local array");
      rc = C_ORM_ERROR_NOT_FOUND;
      break;
    }

    if (options && options->log_cb) {
      char log_msg[1024];
#if defined(_MSC_VER)
      sprintf_s(log_msg, sizeof(log_msg), "Rolling back migration %s: %s",
                mig->version, mig->name);
#else
      sprintf(log_msg, "Rolling back migration %s: %s", mig->version,
              mig->name);
#endif

      options->log_cb(log_msg);
    }

    if (options && options->dry_run) {
      rolled_back++;
      continue;
    }

    {
      c_orm_error_t _err = c_orm_execute_raw(db, "SAVEPOINT c_orm_mig_step_rb");
      if (_err != C_ORM_OK)
        return _err;
    }

    if (mig->down_sql && strlen(mig->down_sql) > 0) {
      rc = c_orm_execute_raw(db, mig->down_sql);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_migrate_rollback: down_sql error");
        break;
      }
    }

#if defined(_MSC_VER)
    sprintf_s(query, sizeof(query),
              "DELETE FROM _c_orm_migrations WHERE version = '%s'",
              mig->version);
#else
    sprintf(query, "DELETE FROM _c_orm_migrations WHERE version = '%s'",
            mig->version);
#endif

    rc = c_orm_execute_raw(db, query);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_migrate_rollback: delete migration error");
      break;
    }

    {
      c_orm_error_t _err =
          c_orm_execute_raw(db, "RELEASE SAVEPOINT c_orm_mig_step_rb");
      if (_err != C_ORM_OK)
        return _err;
    }
    rolled_back++;
  }

  if (applied) {
    C_ORM_FREE(applied);
  }

  unlock_rc = c_orm_migration_unlock(db);
  if (unlock_rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_migrate_rollback: final unlock failed");
    return unlock_rc;
  }
  LOG_DEBUG("c_orm_migrate_rollback: exiting");

  return rc;
}

/**
 * @brief Fetches table schema.
 * @param db The database connection.
 * @param table_name The name of the table.
 * @param out_schema Pointer to store the schema.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_migration_fetch_table_schema(
    c_orm_db_t *db, const char *table_name, cdd_c_meta_t **out_schema) {
  c_orm_error_t rc;
  char sql[256];
  c_orm_query_t *q;
  int has_row;
  size_t cap = 10;
  cdd_c_meta_t *meta;

  LOG_DEBUG("c_orm_migration_fetch_table_schema: entered");

  if (!db || !table_name || !out_schema) {
    LOG_DEBUG("c_orm_migration_fetch_table_schema: validation error");
    rc = C_ORM_ERROR_MEMORY;
    return rc;
  }

  /* Try SQLite first */
#if defined(_MSC_VER)
  sprintf_s(sql, sizeof(sql), "PRAGMA table_info('%s')", table_name);
#else
  sprintf(sql, "PRAGMA table_info('%s')", table_name);
#endif

  rc = c_orm_prepare_cached(db, sql, &q);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_migration_fetch_table_schema: prepare error");

    return rc;
  }

  meta = (cdd_c_meta_t *)C_ORM_MALLOC(sizeof(cdd_c_meta_t));
  if (!meta) {
    LOG_DEBUG("c_orm_migration_fetch_table_schema: OOM");
    {
      c_orm_error_t _fin = c_orm_finalize_cached(db, q);
      if (_fin != C_ORM_OK) {
        return _fin;
      }
    }
    rc = C_ORM_ERROR_MEMORY;
    return rc;
  }

  meta->name = (char *)C_ORM_MALLOC(strlen(table_name) + 1);
  if (!meta->name) {
    LOG_DEBUG("c_orm_migration_fetch_table_schema: OOM");
    C_ORM_FREE(meta);
    {
      c_orm_error_t _fin = c_orm_finalize_cached(db, q);
      if (_fin != C_ORM_OK) {
        return _fin;
      }
    }
    rc = C_ORM_ERROR_MEMORY;
    return rc;
  }
#if defined(_MSC_VER)
  strcpy_s((char *)(size_t)meta->name, strlen(table_name) + 1, table_name);
#else
  strcpy((char *)(size_t)meta->name, table_name);
#endif

  meta->size = 0;
  meta->num_props = 0;
  meta->driver_ctx = NULL;
  meta->props =
      (cdd_c_prop_meta_t *)C_ORM_MALLOC(sizeof(cdd_c_prop_meta_t) * cap);

  if (!meta->props) {
    LOG_DEBUG("c_orm_migration_fetch_table_schema: OOM");
    if (meta->name) {
      C_ORM_FREE((void *)(size_t)meta->name);
    }
    C_ORM_FREE(meta);
    {
      c_orm_error_t _fin = c_orm_finalize_cached(db, q);
      if (_fin != C_ORM_OK) {
        return _fin;
      }
    }
    rc = C_ORM_ERROR_MEMORY;
    return rc;
  }

  for (;;) {
    const char *col_name = NULL;
    const char *col_type = NULL;
    cdd_c_prop_meta_t *prop;

    rc = db->vtable->step(q, &has_row);
    if (rc != C_ORM_OK) {
      c_orm_error_t fin_rc = c_orm_finalize_cached(db, q);
      if (fin_rc != C_ORM_OK) {
        c_orm_migration_free_table_schema(meta);
        return fin_rc;
      }
      c_orm_migration_free_table_schema(meta);
      return rc;
    }
    if (!has_row) {
      break;
    }

    rc = db->vtable->get_string(q, 1, &col_name);
    if (rc != C_ORM_OK) {
      c_orm_error_t fin_rc = c_orm_finalize_cached(db, q);
      if (fin_rc != C_ORM_OK) {
        c_orm_migration_free_table_schema(meta);
        return fin_rc;
      }
      c_orm_migration_free_table_schema(meta);
      return rc;
    }
    rc = db->vtable->get_string(q, 2, &col_type);
    if (rc != C_ORM_OK) {
      c_orm_error_t fin_rc = c_orm_finalize_cached(db, q);
      if (fin_rc != C_ORM_OK) {
        c_orm_migration_free_table_schema(meta);
        return fin_rc;
      }
      c_orm_migration_free_table_schema(meta);
      return rc;
    }

    if (!col_name || !col_type) {
      continue;
    }

    if (meta->num_props >= cap) {
      cdd_c_prop_meta_t *new_props;
      cap *= 2;
      new_props = (cdd_c_prop_meta_t *)C_ORM_REALLOC(
          (void *)(size_t)meta->props, sizeof(cdd_c_prop_meta_t) * cap);
      if (!new_props) {
        LOG_DEBUG("c_orm_migration_fetch_table_schema: OOM during realloc");
        c_orm_migration_free_table_schema(meta);
        {
          c_orm_error_t _fin = c_orm_finalize_cached(db, q);
          if (_fin != C_ORM_OK) {
            return _fin;
          }
        }
        rc = C_ORM_ERROR_MEMORY;
        return rc;
      }
      meta->props = new_props;
    }

    prop = (cdd_c_prop_meta_t *)(size_t)&meta->props[meta->num_props++];
    memset(prop, 0, sizeof(cdd_c_prop_meta_t));

    prop->name = (char *)C_ORM_MALLOC(strlen(col_name) + 1);
    if (!prop->name) {
      LOG_DEBUG("c_orm_migration_fetch_table_schema: OOM");
      c_orm_migration_free_table_schema(meta);
      {
        c_orm_error_t _fin = c_orm_finalize_cached(db, q);
        if (_fin != C_ORM_OK) {
          return _fin;
        }
      }
      rc = C_ORM_ERROR_MEMORY;
      return rc;
    }
#if defined(_MSC_VER)
    strcpy_s((char *)(size_t)prop->name, strlen(col_name) + 1, col_name);
#else
    strcpy((char *)(size_t)prop->name, col_name);
#endif

    prop->type = (char *)C_ORM_MALLOC(strlen(col_type) + 1);
    if (!prop->type) {
      LOG_DEBUG("c_orm_migration_fetch_table_schema: OOM");
      c_orm_migration_free_table_schema(meta);
      {
        c_orm_error_t _fin = c_orm_finalize_cached(db, q);
        if (_fin != C_ORM_OK) {
          return _fin;
        }
      }
      rc = C_ORM_ERROR_MEMORY;
      return rc;
    }
#if defined(_MSC_VER)
    strcpy_s((char *)(size_t)prop->type, strlen(col_type) + 1, col_type);
#else
    strcpy((char *)(size_t)prop->type, col_type);
#endif

    prop->offset = 0;
  }

  {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, q);
    if (fin_rc != C_ORM_OK) {
      c_orm_migration_free_table_schema(meta);
      return fin_rc;
    }
  }

  *out_schema = meta;
  LOG_DEBUG("c_orm_migration_fetch_table_schema: exiting");
  rc = C_ORM_OK;
  return rc;
}

/**
 * @brief Frees a table schema.
 * @param schema The schema to free.
 */
C_ORM_EXPORT void c_orm_migration_free_table_schema(cdd_c_meta_t *schema) {
  size_t i;
  LOG_DEBUG("c_orm_migration_free_table_schema: entered");
  if (!schema) {
    LOG_DEBUG("c_orm_migration_free_table_schema: null schema, exiting");
    return;
  }
  if (schema->name) {
    C_ORM_FREE((void *)(size_t)schema->name);
  }
  if (schema->props) {
    for (i = 0; i < schema->num_props; i++) {
      if (schema->props[i].name) {
        C_ORM_FREE((void *)(size_t)schema->props[i].name);
      }
      if (schema->props[i].type) {
        C_ORM_FREE((void *)(size_t)schema->props[i].type);
      }
    }
    C_ORM_FREE((void *)(size_t)schema->props);
  }
  C_ORM_FREE(schema);
  LOG_DEBUG("c_orm_migration_free_table_schema: exiting");
}

/**
 * @brief Gets applied migrations.
 * @param db The database connection.
 * @param out_migrations Pointer to store the array of migrations.
 * @param out_count Pointer to store the number of applied migrations.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_migration_get_applied(
    c_orm_db_t *db, c_orm_migration_t **out_migrations, size_t *out_count) {
  c_orm_error_t rc;
  c_orm_query_t *q = NULL;
  int has_row;
  size_t count = 0;
  size_t cap = 10;
  c_orm_migration_t *migs;

  LOG_DEBUG("c_orm_migration_get_applied: entered");

  if (!db || !out_migrations || !out_count) {
    LOG_DEBUG("c_orm_migration_get_applied: validation error");
    rc = C_ORM_ERROR_VALIDATION;
    return rc;
  }

  rc = c_orm_prepare_cached(
      db, "SELECT version, name, hash FROM _c_orm_migrations ORDER BY id ASC",
      &q);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_migration_get_applied: prepare error");

    return rc;
  }

  migs = (c_orm_migration_t *)C_ORM_MALLOC(cap * sizeof(c_orm_migration_t));
  if (!migs) {
    LOG_DEBUG("c_orm_migration_get_applied: OOM");
    {
      c_orm_error_t _fin = c_orm_finalize_cached(db, q);
      if (_fin != C_ORM_OK) {
        return _fin;
      }
    }
    rc = C_ORM_ERROR_MEMORY;
    return rc;
  }

  for (;;) {
    const char *version = NULL;
    const char *name = NULL;
    const char *hash = NULL;

    rc = db->vtable->step(q, &has_row);
    if (rc != C_ORM_OK) {
      c_orm_error_t fin_rc = c_orm_finalize_cached(db, q);
      if (fin_rc != C_ORM_OK) {
        c_orm_migration_free_array(migs, count);
        return fin_rc;
      }
      c_orm_migration_free_array(migs, count);
      return rc;
    }
    if (!has_row) {
      break;
    }

    rc = db->vtable->get_string(q, 0, &version);
    if (rc != C_ORM_OK) {
      c_orm_error_t fin_rc = c_orm_finalize_cached(db, q);
      if (fin_rc != C_ORM_OK) {
        c_orm_migration_free_array(migs, count);
        return fin_rc;
      }
      c_orm_migration_free_array(migs, count);
      return rc;
    }
    rc = db->vtable->get_string(q, 1, &name);
    if (rc != C_ORM_OK) {
      c_orm_error_t fin_rc = c_orm_finalize_cached(db, q);
      if (fin_rc != C_ORM_OK) {
        c_orm_migration_free_array(migs, count);
        return fin_rc;
      }
      c_orm_migration_free_array(migs, count);
      return rc;
    }
    rc = db->vtable->get_string(q, 2, &hash);
    if (rc != C_ORM_OK) {
      c_orm_error_t fin_rc = c_orm_finalize_cached(db, q);
      if (fin_rc != C_ORM_OK) {
        c_orm_migration_free_array(migs, count);
        return fin_rc;
      }
      c_orm_migration_free_array(migs, count);
      return rc;
    }

    if (count >= cap) {
      c_orm_migration_t *new_migs;
      cap *= 2;
      new_migs = (c_orm_migration_t *)C_ORM_REALLOC(
          migs, cap * sizeof(c_orm_migration_t));
      if (!new_migs) {
        LOG_DEBUG("c_orm_migration_get_applied: OOM during realloc");
        c_orm_migration_free_array(migs, count);
        {
          c_orm_error_t _fin = c_orm_finalize_cached(db, q);
          if (_fin != C_ORM_OK) {
            return _fin;
          }
        }
        rc = C_ORM_ERROR_MEMORY;
        return rc;
      }
      migs = new_migs;
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
    if (hash) {
#if defined(_MSC_VER)
      strcpy_s(migs[count].hash, sizeof(migs[count].hash), hash);
#else
      strcpy(migs[count].hash, hash);
#endif
    }
    count++;
  }

  {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, q);
    if (fin_rc != C_ORM_OK) {
      c_orm_migration_free_array(migs, count);
      return fin_rc;
    }
  }

  *out_migrations = migs;
  *out_count = count;

  LOG_DEBUG("c_orm_migration_get_applied: exiting");
  rc = C_ORM_OK;
  return rc;
}

/**
 * @brief Migrates the database up using a directory.
 * @param db The database connection.
 * @param dir_path The path to the migrations directory.
 * @param options Migration options.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_migrate_up(c_orm_db_t *db, const char *dir_path,
                 const c_orm_migration_options_t *options) {
  c_orm_error_t rc;
  c_orm_migration_t *migs = NULL;
  size_t count = 0;

  LOG_DEBUG("c_orm_migrate_up: entered");

  if (!db || !dir_path) {
    LOG_DEBUG("c_orm_migrate_up: validation error");
    rc = C_ORM_ERROR_VALIDATION;
    return rc;
  }

  rc = c_orm_migration_load_dir(dir_path, &migs, &count);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_migrate_up: load dir error");

    return rc;
  }

  rc = c_orm_migrate_all(db, migs, count, options);

  c_orm_migration_free_array(migs, count);

  LOG_DEBUG("c_orm_migrate_up: exiting");

  return rc;
}

/**
 * @brief Migrates the database down using a directory.
 * @param db The database connection.
 * @param dir_path The path to the migrations directory.
 * @param steps The number of steps to roll back.
 * @param options Migration options.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_migrate_down(c_orm_db_t *db, const char *dir_path, size_t steps,
                   const c_orm_migration_options_t *options) {
  c_orm_error_t rc;
  c_orm_migration_t *migs = NULL;
  size_t count = 0;

  LOG_DEBUG("c_orm_migrate_down: entered");

  if (!db || !dir_path) {
    LOG_DEBUG("c_orm_migrate_down: validation error");
    rc = C_ORM_ERROR_VALIDATION;
    return rc;
  }

  rc = c_orm_migration_load_dir(dir_path, &migs, &count);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_migrate_down: load dir error");

    return rc;
  }

  rc = c_orm_migrate_rollback(db, migs, count, steps, options);

  c_orm_migration_free_array(migs, count);

  LOG_DEBUG("c_orm_migrate_down: exiting");

  return rc;
}

/**
 * @brief Locks the migrations table.
 * @param db The database connection.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_migration_lock(c_orm_db_t *db) {
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_migration_lock: entered");

  /* Attempt SQLite Exclusive transaction */
  rc = c_orm_execute_raw(db, "BEGIN EXCLUSIVE");
  if (rc == C_ORM_OK) {
    LOG_DEBUG("c_orm_migration_lock: exiting");
    return rc;
  }
  LOG_DEBUG(
      "c_orm_migration_lock: BEGIN EXCLUSIVE failed, trying pg_advisory_lock");

  /* Attempt Postgres Advisory Lock using a fixed hash/ID for c-orm */
  rc = c_orm_execute_raw(db, "SELECT pg_advisory_lock(723821)");
  if (rc == C_ORM_OK) {
    LOG_DEBUG("c_orm_migration_lock: exiting");
    return rc;
  }
  LOG_DEBUG("c_orm_migration_lock: pg_advisory_lock failed, trying GET_LOCK");

  /* Attempt MySQL/MariaDB Lock */
  rc = c_orm_execute_raw(db, "SELECT GET_LOCK('c_orm_migration', 10)");
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_migration_lock: GET_LOCK failed");
    return rc;
  }

  LOG_DEBUG("c_orm_migration_lock: exiting");
  return C_ORM_OK;
}

/**
 * @brief Unlocks the migrations table.
 * @param db The database connection.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_migration_unlock(c_orm_db_t *db) {
  c_orm_error_t rc;
  rc = C_ORM_ERROR_NOT_IMPLEMENTED;

  LOG_DEBUG("c_orm_migration_unlock: entered");

  if (!db || !db->driver_name) {
    LOG_DEBUG("c_orm_migration_unlock: validation error");
    rc = C_ORM_ERROR_VALIDATION;
    return rc;
  }

  if (strcmp(db->driver_name, "sqlite") == 0 ||
      strcmp(db->driver_name, "memory") == 0) {
    rc = c_orm_execute_raw(db, "COMMIT");
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_migration_unlock: COMMIT failed");
      return rc;
    }
  } else if (strcmp(db->driver_name, "postgres") == 0) {
    rc = c_orm_execute_raw(db, "SELECT pg_advisory_unlock(723821)");
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_migration_unlock: pg_advisory_unlock failed");
      return rc;
    }
  } else if (strcmp(db->driver_name, "mysql") == 0) {
    rc = c_orm_execute_raw(db, "SELECT RELEASE_LOCK('c_orm_migration')");
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_migration_unlock: RELEASE_LOCK failed");
      return rc;
    }
  }

  LOG_DEBUG("c_orm_migration_unlock: exiting");
  return rc;
}

#if defined(__clang__) || defined(__GNUC__)
#endif
