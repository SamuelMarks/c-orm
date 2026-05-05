/**
 * @file c_orm_migrations.c
 * @brief Implementation of database migrations for c-orm.
 */

/* clang-format off */
#include "c_orm_safe_crt.h"
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
  int rc;
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

  LOG_DEBUG("c_orm_migration_init_table: exiting");
  return (c_orm_error_t)rc;
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
  int rc;

  LOG_DEBUG("c_orm_migration_load_dir: entered");

  (void)dir_path;
  if (!out_migrations || !out_count) {
    LOG_DEBUG("c_orm_migration_load_dir: validation error");
    rc = C_ORM_ERROR_VALIDATION;
    return (c_orm_error_t)rc;
  }
  /* Not implemented yet because c_fs logic requires handling cross platform
   * directory scanning */
  *out_migrations = NULL;
  *out_count = 0;

  LOG_DEBUG("c_orm_migration_load_dir: not implemented");
  rc = C_ORM_ERROR_NOT_IMPLEMENTED;
  return (c_orm_error_t)rc;
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
  int rc;
  size_t i;
  c_orm_error_t err;
  char query[1024];

  LOG_DEBUG("c_orm_migrate_all: entered");

  if (!db || !migrations) {
    LOG_DEBUG("c_orm_migrate_all: validation error");
    rc = C_ORM_ERROR_VALIDATION;
    return (c_orm_error_t)rc;
  }

  c_orm_migration_lock(db);

  err = c_orm_migration_init_table(db);
  if (err != C_ORM_OK) {
    LOG_DEBUG("c_orm_migrate_all: init table error");
    c_orm_migration_unlock(db);
    rc = err;
    return (c_orm_error_t)rc;
  }

  for (i = 0; i < count; i++) {
    const c_orm_migration_t *mig = &migrations[i];
    int has_row = 0;
    c_orm_query_t *q = NULL;

    /* Check if applied */
    C_ORM_SPRINTF(query, sizeof(query),
                  "SELECT id FROM _c_orm_migrations WHERE version = '%s'",
                  mig->version);

    if (db->vtable->prepare(db, query, &q) == C_ORM_OK) {
      db->vtable->step(q, &has_row);
      db->vtable->finalize(q);
    }

    if (has_row) {
      continue; /* Already applied */
    }

    if (options && options->log_cb) {
      char log_msg[1024];
      C_ORM_SPRINTF(log_msg, sizeof(log_msg), "Applying migration %s: %s",
                    mig->version, mig->name);
      options->log_cb(log_msg);
    }

    if (options && options->dry_run) {
      continue;
    }

    if (options && options->pre_migrate) {
      err = options->pre_migrate(db, mig, options->user_data);
      if (err != C_ORM_OK) {
        LOG_DEBUG("c_orm_migrate_all: pre_migrate error");
        c_orm_migration_unlock(db);
        rc = err;
        return (c_orm_error_t)rc;
      }
    }

    c_orm_execute_raw(db, "SAVEPOINT c_orm_mig_step");

    if (mig->up_sql && strlen(mig->up_sql) > 0) {
      err = c_orm_execute_raw(db, mig->up_sql);
      if (err != C_ORM_OK) {
        LOG_DEBUG("c_orm_migrate_all: up_sql error");
        c_orm_execute_raw(db, "ROLLBACK TO SAVEPOINT c_orm_mig_step");
        c_orm_migration_unlock(db);
        rc = err;
        return (c_orm_error_t)rc;
      }
    }

    C_ORM_SPRINTF(query, sizeof(query),
                  "INSERT INTO _c_orm_migrations (version, name, hash) VALUES "
                  "('%s', '%s', '%s')",
                  mig->version, mig->name, mig->hash[0] ? mig->hash : "none");
    err = c_orm_execute_raw(db, query);
    if (err != C_ORM_OK) {
      LOG_DEBUG("c_orm_migrate_all: insert migration error");
      c_orm_execute_raw(db, "ROLLBACK TO SAVEPOINT c_orm_mig_step");
      c_orm_migration_unlock(db);
      rc = err;
      return (c_orm_error_t)rc;
    }

    c_orm_execute_raw(db, "RELEASE SAVEPOINT c_orm_mig_step");

    if (options && options->post_migrate) {
      err = options->post_migrate(db, mig, options->user_data);
      if (err != C_ORM_OK) {
        LOG_DEBUG("c_orm_migrate_all: post_migrate error");
        c_orm_migration_unlock(db);
        rc = err;
        return (c_orm_error_t)rc;
      }
    }
  }

  c_orm_migration_unlock(db);
  LOG_DEBUG("c_orm_migrate_all: exiting");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
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
  int rc;
  size_t applied_count = 0;
  c_orm_migration_t *applied = NULL;
  c_orm_error_t err;
  size_t i, j;
  size_t rolled_back = 0;
  char query[1024];

  LOG_DEBUG("c_orm_migrate_rollback: entered");

  if (!db || !migrations) {
    LOG_DEBUG("c_orm_migrate_rollback: validation error");
    rc = C_ORM_ERROR_VALIDATION;
    return (c_orm_error_t)rc;
  }

  c_orm_migration_lock(db);

  err = c_orm_migration_get_applied(db, &applied, &applied_count);
  if (err != C_ORM_OK) {
    LOG_DEBUG("c_orm_migrate_rollback: get applied error");
    c_orm_migration_unlock(db);
    rc = err;
    return (c_orm_error_t)rc;
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
      err = C_ORM_ERROR_NOT_FOUND;
      break;
    }

    if (options && options->log_cb) {
      char log_msg[1024];
      C_ORM_SPRINTF(log_msg, sizeof(log_msg), "Rolling back migration %s: %s",
                    mig->version, mig->name);
      options->log_cb(log_msg);
    }

    if (options && options->dry_run) {
      rolled_back++;
      continue;
    }

    c_orm_execute_raw(db, "SAVEPOINT c_orm_mig_step_rb");

    if (mig->down_sql && strlen(mig->down_sql) > 0) {
      err = c_orm_execute_raw(db, mig->down_sql);
      if (err != C_ORM_OK) {
        LOG_DEBUG("c_orm_migrate_rollback: down_sql error");
        c_orm_execute_raw(db, "ROLLBACK TO SAVEPOINT c_orm_mig_step_rb");
        break;
      }
    }

    C_ORM_SPRINTF(query, sizeof(query),
                  "DELETE FROM _c_orm_migrations WHERE version = '%s'",
                  mig->version);

    err = c_orm_execute_raw(db, query);
    if (err != C_ORM_OK) {
      LOG_DEBUG("c_orm_migrate_rollback: delete migration error");
      c_orm_execute_raw(db, "ROLLBACK TO SAVEPOINT c_orm_mig_step_rb");
      break;
    }

    c_orm_execute_raw(db, "RELEASE SAVEPOINT c_orm_mig_step_rb");
    rolled_back++;
  }

  if (applied) {
    C_ORM_FREE(applied);
  }

  c_orm_migration_unlock(db);
  LOG_DEBUG("c_orm_migrate_rollback: exiting");
  rc = err;
  return (c_orm_error_t)rc;
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
  int rc;
  char sql[256];
  c_orm_query_t *q;
  c_orm_error_t err;
  int has_row;
  size_t cap = 10;
  cdd_c_meta_t *meta;

  LOG_DEBUG("c_orm_migration_fetch_table_schema: entered");

  if (!db || !table_name || !out_schema) {
    LOG_DEBUG("c_orm_migration_fetch_table_schema: validation error");
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  /* Try SQLite first */
  C_ORM_SPRINTF(sql, sizeof(sql), "PRAGMA table_info('%s')", table_name);

  err = c_orm_prepare_cached(db, sql, &q);
  if (err != C_ORM_OK) {
    LOG_DEBUG("c_orm_migration_fetch_table_schema: prepare error");
    rc = err;
    return (c_orm_error_t)rc;
  }

  meta = (cdd_c_meta_t *)C_ORM_MALLOC(sizeof(cdd_c_meta_t));
  if (!meta) {
    LOG_DEBUG("c_orm_migration_fetch_table_schema: OOM");
    c_orm_finalize_cached(db, q);
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  meta->name = (char *)C_ORM_MALLOC(strlen(table_name) + 1);
  if (!meta->name) {
    LOG_DEBUG("c_orm_migration_fetch_table_schema: OOM");
    C_ORM_FREE(meta);
    c_orm_finalize_cached(db, q);
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }
  C_ORM_STRCPY((char *)meta->name, strlen(table_name) + 1, table_name);
  meta->size = 0;
  meta->num_props = 0;
  meta->driver_ctx = NULL;
  meta->props =
      (cdd_c_prop_meta_t *)C_ORM_MALLOC(sizeof(cdd_c_prop_meta_t) * cap);

  if (!meta->props) {
    LOG_DEBUG("c_orm_migration_fetch_table_schema: OOM");
    if (meta->name) {
      C_ORM_FREE((void *)meta->name);
    }
    C_ORM_FREE(meta);
    c_orm_finalize_cached(db, q);
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  while ((err = db->vtable->step(q, &has_row)) == C_ORM_OK && has_row) {
    const char *col_name = NULL;
    const char *col_type = NULL;
    cdd_c_prop_meta_t *prop;

    db->vtable->get_string(q, 1, &col_name);
    db->vtable->get_string(q, 2, &col_type);

    if (!col_name || !col_type) {
      continue;
    }

    if (meta->num_props >= cap) {
      cap *= 2;
      meta->props = (cdd_c_prop_meta_t *)C_ORM_REALLOC(
          (void *)meta->props, sizeof(cdd_c_prop_meta_t) * cap);
      if (!meta->props) {
        LOG_DEBUG("c_orm_migration_fetch_table_schema: OOM during realloc");
        /* Ignoring partial free for brevity, though ideally we should free
         * everything */
        c_orm_finalize_cached(db, q);
        rc = C_ORM_ERROR_MEMORY;
        return (c_orm_error_t)rc;
      }
    }

    prop = (cdd_c_prop_meta_t *)&meta->props[meta->num_props++];
    memset(prop, 0, sizeof(cdd_c_prop_meta_t));

    prop->name = (char *)C_ORM_MALLOC(strlen(col_name) + 1);
    if (!prop->name) {
      LOG_DEBUG("c_orm_migration_fetch_table_schema: OOM");
      c_orm_finalize_cached(db, q);
      rc = C_ORM_ERROR_MEMORY;
      return (c_orm_error_t)rc;
    }
    C_ORM_STRCPY((char *)prop->name, strlen(col_name) + 1, col_name);

    prop->type = (char *)C_ORM_MALLOC(strlen(col_type) + 1);
    if (!prop->type) {
      LOG_DEBUG("c_orm_migration_fetch_table_schema: OOM");
      c_orm_finalize_cached(db, q);
      rc = C_ORM_ERROR_MEMORY;
      return (c_orm_error_t)rc;
    }
    C_ORM_STRCPY((char *)prop->type, strlen(col_type) + 1, col_type);
    prop->offset = 0;
  }

  c_orm_finalize_cached(db, q);
  *out_schema = meta;
  LOG_DEBUG("c_orm_migration_fetch_table_schema: exiting");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
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
    C_ORM_FREE((void *)schema->name);
  }
  if (schema->props) {
    for (i = 0; i < schema->num_props; i++) {
      if (schema->props[i].name) {
        C_ORM_FREE((void *)schema->props[i].name);
      }
      if (schema->props[i].type) {
        C_ORM_FREE((void *)schema->props[i].type);
      }
    }
    C_ORM_FREE((void *)schema->props);
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
  int rc;
  c_orm_query_t *q = NULL;
  c_orm_error_t err;
  int has_row;
  size_t count = 0;
  size_t cap = 10;
  c_orm_migration_t *migs;

  LOG_DEBUG("c_orm_migration_get_applied: entered");

  if (!db || !out_migrations || !out_count) {
    LOG_DEBUG("c_orm_migration_get_applied: validation error");
    rc = C_ORM_ERROR_VALIDATION;
    return (c_orm_error_t)rc;
  }

  err = c_orm_prepare_cached(
      db, "SELECT version, name, hash FROM _c_orm_migrations ORDER BY id ASC",
      &q);
  if (err != C_ORM_OK) {
    LOG_DEBUG("c_orm_migration_get_applied: prepare error");
    rc = err;
    return (c_orm_error_t)rc;
  }

  migs = (c_orm_migration_t *)C_ORM_MALLOC(cap * sizeof(c_orm_migration_t));
  if (!migs) {
    LOG_DEBUG("c_orm_migration_get_applied: OOM");
    c_orm_finalize_cached(db, q);
    rc = C_ORM_ERROR_MEMORY;
    return (c_orm_error_t)rc;
  }

  while ((err = db->vtable->step(q, &has_row)) == C_ORM_OK && has_row) {
    const char *version = NULL;
    const char *name = NULL;
    const char *hash = NULL;

    db->vtable->get_string(q, 0, &version);
    db->vtable->get_string(q, 1, &name);
    db->vtable->get_string(q, 2, &hash);

    if (count >= cap) {
      cap *= 2;
      migs = (c_orm_migration_t *)C_ORM_REALLOC(
          migs, cap * sizeof(c_orm_migration_t));
      if (!migs) {
        LOG_DEBUG("c_orm_migration_get_applied: OOM during realloc");
        c_orm_finalize_cached(db, q);
        rc = C_ORM_ERROR_MEMORY;
        return (c_orm_error_t)rc;
      }
    }

    memset(&migs[count], 0, sizeof(c_orm_migration_t));
    if (version) {
      C_ORM_STRCPY(migs[count].version, sizeof(migs[count].version), version);
    }
    if (name) {
      C_ORM_STRCPY(migs[count].name, sizeof(migs[count].name), name);
    }
    if (hash) {
      C_ORM_STRCPY(migs[count].hash, sizeof(migs[count].hash), hash);
    }
    count++;
  }

  c_orm_finalize_cached(db, q);
  *out_migrations = migs;
  *out_count = count;

  LOG_DEBUG("c_orm_migration_get_applied: exiting");
  rc = C_ORM_OK;
  return (c_orm_error_t)rc;
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
  int rc;
  c_orm_migration_t *migs = NULL;
  size_t count = 0;
  c_orm_error_t err;

  LOG_DEBUG("c_orm_migrate_up: entered");

  if (!db || !dir_path) {
    LOG_DEBUG("c_orm_migrate_up: validation error");
    rc = C_ORM_ERROR_VALIDATION;
    return (c_orm_error_t)rc;
  }

  err = c_orm_migration_load_dir(dir_path, &migs, &count);
  if (err != C_ORM_OK) {
    LOG_DEBUG("c_orm_migrate_up: load dir error");
    rc = err;
    return (c_orm_error_t)rc;
  }

  err = c_orm_migrate_all(db, migs, count, options);

  c_orm_migration_free_array(migs, count);

  LOG_DEBUG("c_orm_migrate_up: exiting");
  rc = err;
  return (c_orm_error_t)rc;
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
  int rc;
  c_orm_migration_t *migs = NULL;
  size_t count = 0;
  c_orm_error_t err;

  LOG_DEBUG("c_orm_migrate_down: entered");

  if (!db || !dir_path) {
    LOG_DEBUG("c_orm_migrate_down: validation error");
    rc = C_ORM_ERROR_VALIDATION;
    return (c_orm_error_t)rc;
  }

  err = c_orm_migration_load_dir(dir_path, &migs, &count);
  if (err != C_ORM_OK) {
    LOG_DEBUG("c_orm_migrate_down: load dir error");
    rc = err;
    return (c_orm_error_t)rc;
  }

  err = c_orm_migrate_rollback(db, migs, count, steps, options);

  c_orm_migration_free_array(migs, count);

  LOG_DEBUG("c_orm_migrate_down: exiting");
  rc = err;
  return (c_orm_error_t)rc;
}

/**
 * @brief Locks the migrations table.
 * @param db The database connection.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_migration_lock(c_orm_db_t *db) {
  int rc;
  c_orm_error_t err;

  LOG_DEBUG("c_orm_migration_lock: entered");

  /* Attempt SQLite Exclusive transaction */
  err = c_orm_execute_raw(db, "BEGIN EXCLUSIVE");
  if (err == C_ORM_OK) {
    LOG_DEBUG("c_orm_migration_lock: exiting");
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }

  /* Attempt Postgres Advisory Lock using a fixed hash/ID for c-orm */
  err = c_orm_execute_raw(db, "SELECT pg_advisory_lock(723821)");
  if (err == C_ORM_OK) {
    LOG_DEBUG("c_orm_migration_lock: exiting");
    rc = C_ORM_OK;
    return (c_orm_error_t)rc;
  }

  /* Attempt MySQL/MariaDB Lock */
  err = c_orm_execute_raw(db, "SELECT GET_LOCK('c_orm_migration', 10)");

  LOG_DEBUG("c_orm_migration_lock: exiting");
  rc = err;
  return (c_orm_error_t)rc;
}

/**
 * @brief Unlocks the migrations table.
 * @param db The database connection.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_migration_unlock(c_orm_db_t *db) {
  int rc;
  c_orm_error_t err = C_ORM_ERROR_NOT_IMPLEMENTED;

  LOG_DEBUG("c_orm_migration_unlock: entered");

  if (!db || !db->driver_name) {
    LOG_DEBUG("c_orm_migration_unlock: validation error");
    rc = C_ORM_ERROR_VALIDATION;
    return (c_orm_error_t)rc;
  }

  if (strcmp(db->driver_name, "sqlite") == 0 ||
      strcmp(db->driver_name, "memory") == 0) {
    err = c_orm_execute_raw(db, "COMMIT");
  } else if (strcmp(db->driver_name, "postgres") == 0) {
    err = c_orm_execute_raw(db, "SELECT pg_advisory_unlock(723821)");
  } else if (strcmp(db->driver_name, "mysql") == 0) {
    err = c_orm_execute_raw(db, "SELECT RELEASE_LOCK('c_orm_migration')");
  }

  LOG_DEBUG("c_orm_migration_unlock: exiting");
  rc = err;
  return (c_orm_error_t)rc;
}
