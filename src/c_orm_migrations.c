/**
 * @file c_orm_migrations.c
 * @brief Implementation of database migrations for c-orm.
 */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_migrations.h"
#include "c_orm_odbc.h"
#include "c_orm_log.h"
#include "migration.h"
#include "cfs/cfs.h"
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

  if (!db || !db->vtable) {
    LOG_DEBUG("c_orm_migration_init_table: validation error");
    return C_ORM_ERROR_VALIDATION;
  }

  if (db->driver_name && (strcmp(db->driver_name, "db2") == 0 ||
                          strcmp(db->driver_name, "odbc") == 0)) {
    c_orm_query_t *probe_q = NULL;
    int has_row = 0;
    const char *portable_ddl =
        (strcmp(db->driver_name, "db2") == 0)
            ? "CREATE TABLE _c_orm_migrations ("
              "  version VARCHAR(255) NOT NULL PRIMARY KEY,"
              "  name VARCHAR(255) NOT NULL,"
              "  hash VARCHAR(65) NOT NULL,"
              "  applied_at TIMESTAMP DEFAULT CURRENT TIMESTAMP"
              ")"
            : "CREATE TABLE _c_orm_migrations ("
              "  version VARCHAR(255) NOT NULL PRIMARY KEY,"
              "  name VARCHAR(255) NOT NULL,"
              "  hash VARCHAR(65) NOT NULL,"
              "  applied_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
              ")";

    if (db->vtable->prepare(db,
                            "SELECT version FROM _c_orm_migrations WHERE 1 = 0",
                            &probe_q) == C_ORM_OK) {
      rc = db->vtable->step(probe_q, &has_row);
      {
        c_orm_error_t fin_rc = db->vtable->finalize(probe_q);
        if (fin_rc != C_ORM_OK) {
          return fin_rc;
        }
      }
      if (rc == C_ORM_OK) {
        return C_ORM_OK;
      }
    }
    return c_orm_execute_raw(db, portable_ddl);
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
 * @brief Computes a 64-bit FNV-1a hexadecimal digest of a SQL string.
 * @param sql Null-terminated SQL text (or NULL).
 * @param out_hex Output buffer of at least 17 bytes.
 * @param hex_sz Capacity of out_hex in bytes.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t compute_sql_fnv1a_hex(const char *sql, char *out_hex,
                                           size_t hex_sz) {
  unsigned long hi = 0xCBF29CE4UL;
  unsigned long lo = 0x84222325UL;
  const unsigned char *p = (const unsigned char *)(sql ? sql : "");
  while (*p) {
    unsigned long b = (unsigned long)(*p++);
    unsigned long p0, p1, carry;
    lo = (lo ^ b) & 0xFFFFFFFFUL;
    /* Multiply (hi:lo) by 0x00000100000001B3 in portable 32-bit C89 math */
    p0 = (lo & 0xFFFFUL) * 0x01B3UL;
    p1 = ((lo >> 16) & 0xFFFFUL) * 0x01B3UL;
    carry = ((p0 >> 16) + (p1 & 0xFFFFUL)) >> 16;
    hi = ((hi * 0x01B3UL) + (lo << 8) + (p1 >> 16) + carry) & 0xFFFFFFFFUL;
    lo = (p0 + ((p1 & 0xFFFFUL) << 16)) & 0xFFFFFFFFUL;
  }
  C_ORM_SPRINTF(out_hex, hex_sz, "%08lx%08lx", hi, lo);
  return C_ORM_OK;
}

/**
 * @brief Splits a migration filename stem into `(version, name)`.
 * @param stem Filename without `.sql` / `.up.sql` / `.down.sql` suffix.
 * @param stem_len Length of stem in bytes.
 * @param out_ver Output version buffer.
 * @param ver_sz Capacity of out_ver.
 * @param out_name Output name buffer.
 * @param name_sz Capacity of out_name.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t split_stem_version_name(const char *stem, size_t stem_len,
                                             char *out_ver, size_t ver_sz,
                                             char *out_name, size_t name_sz) {
  const char *uscore = (const char *)memchr(stem, '_', stem_len);
  if (uscore && uscore > stem) {
    size_t vlen = (size_t)(uscore - stem);
    size_t nlen = stem_len - vlen - 1;
    if (vlen >= ver_sz) {
      vlen = ver_sz - 1;
    }
    if (nlen >= name_sz) {
      nlen = name_sz - 1;
    }
    memcpy(out_ver, stem, vlen);
    out_ver[vlen] = '\0';
    memcpy(out_name, uscore + 1, nlen);
    out_name[nlen] = '\0';
  } else {
    size_t copy_len = stem_len < (ver_sz - 1) ? stem_len : (ver_sz - 1);
    memcpy(out_ver, stem, copy_len);
    out_ver[copy_len] = '\0';
    copy_len = stem_len < (name_sz - 1) ? stem_len : (name_sz - 1);
    memcpy(out_name, stem, copy_len);
    out_name[copy_len] = '\0';
  }
  return C_ORM_OK;
}

/**
 * @brief Comparator for qsort ordering migrations lexicographically by version
 * then name.
 * @param a First migration element pointer.
 * @param b Second migration element pointer.
 * @return Negative, zero, or positive ordering integer.
 */
static int compare_c_orm_migration_entries(const void *a, const void *b) {
  const c_orm_migration_t *ma = (const c_orm_migration_t *)a;
  const c_orm_migration_t *mb = (const c_orm_migration_t *)b;
  int cmp = strcmp(ma->version, mb->version);
  if (cmp != 0) {
    return cmp;
  }
  return strcmp(ma->name, mb->name);
}

/**
 * @brief Reads an entire file into a c-orm-allocated NUL-terminated buffer.
 * @param filepath Path to file to read.
 * @param out_buf Output pointer receiving heap-allocated string.
 * @return C_ORM_OK on success, or error code on IO/OOM failure.
 */
static c_orm_error_t read_sql_file_alloc(const char *filepath, char **out_buf) {
  FILE *fp = NULL;
  long sz;
  size_t alloc_sz;
  char *buf = NULL;
  size_t nread;

  *out_buf = NULL;
  if (C_ORM_FOPEN(&fp, filepath, "rb") != C_ORM_OK) {
    return C_ORM_ERROR_IO;
  }
  (void)fseek(fp, 0, SEEK_END);
  sz = ftell(fp);
  alloc_sz = sz > 0 ? (size_t)sz : 0;
  (void)fseek(fp, 0, SEEK_SET);
  if (c_orm_system_malloc(alloc_sz + 1, (void **)&buf) != C_ORM_OK) {
    fclose(fp);
    return C_ORM_ERROR_MEMORY;
  }
  nread = fread(buf, 1, alloc_sz, fp);
  fclose(fp);
  buf[nread] = '\0';
  *out_buf = buf;
  return C_ORM_OK;
}

/**
 * @brief Finds or appends a migration entry matching `(version, name)`.
 * @param arr Pointer to migration array pointer.
 * @param count Pointer to active count.
 * @param cap Pointer to allocated capacity.
 * @param ver Version string.
 * @param name Migration name string.
 * @param out_entry Output pointer to matching/new slot.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on OOM.
 */
static c_orm_error_t
find_or_insert_migration_slot(c_orm_migration_t **arr, size_t *count,
                              size_t *cap, const char *ver, const char *name,
                              c_orm_migration_t **out_entry) {
  size_t i;
  for (i = 0; i < *count; i++) {
    if (strcmp((*arr)[i].version, ver) == 0 &&
        strcmp((*arr)[i].name, name) == 0) {
      *out_entry = &(*arr)[i];
      return C_ORM_OK;
    }
  }
  if (*count >= *cap) {
    size_t new_cap = *cap == 0 ? 8 : (*cap * 2);
    c_orm_migration_t *grown = NULL;
    if (c_orm_system_realloc(*arr, new_cap * sizeof(c_orm_migration_t),
                             (void **)&grown) != C_ORM_OK) {
      return C_ORM_ERROR_MEMORY;
    }
    memset(grown + *cap, 0, (new_cap - *cap) * sizeof(c_orm_migration_t));
    *arr = grown;
    *cap = new_cap;
  }
  *out_entry = &(*arr)[*count];
  memset(*out_entry, 0, sizeof(c_orm_migration_t));
  (void)C_ORM_STRCPY((*out_entry)->version, sizeof((*out_entry)->version), ver);
  (void)C_ORM_STRCPY((*out_entry)->name, sizeof((*out_entry)->name), name);
  (*count)++;
  return C_ORM_OK;
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
  cfs_path cfs_dir;
  cfs_directory_iterator *it = NULL;
  const cfs_directory_entry *entry = NULL;
  cfs_error_code ec;
  cfs_errc cfs_rc;
  c_orm_migration_t *migs = NULL;
  size_t count = 0;
  size_t cap = 0;
  size_t i;

  LOG_DEBUG("c_orm_migration_load_dir: entered");

  if (!out_migrations || !out_count) {
    LOG_DEBUG("c_orm_migration_load_dir: validation error");
    return C_ORM_ERROR_VALIDATION;
  }

  *out_migrations = NULL;
  *out_count = 0;

  if (!dir_path || dir_path[0] == '\0') {
    LOG_DEBUG("c_orm_migration_load_dir: invalid dir_path");
    return C_ORM_ERROR_VALIDATION;
  }

  cfs_path_init(&cfs_dir);
#if defined(CFS_OS_WINDOWS) && defined(CFS_UNICODE)
  {
    cfs_size_t req = 0;
    wchar_t *wdir = NULL;
    cfs_rc = cfs_utf8_to_utf16(dir_path, NULL, 0, &req);
    if (cfs_rc != cfs_errc_success || req == 0) {
      cfs_path_destroy(&cfs_dir);
      return C_ORM_ERROR_IO;
    }
    if (c_orm_system_malloc(req * sizeof(wchar_t), (void **)&wdir) !=
            C_ORM_OK ||
        !wdir) {
      cfs_path_destroy(&cfs_dir);
      return C_ORM_ERROR_MEMORY;
    }
    cfs_rc = cfs_utf8_to_utf16(dir_path, wdir, req, NULL);
    if (cfs_rc == cfs_errc_success) {
      cfs_rc = cfs_path_init_str(&cfs_dir, wdir);
    }
    (void)c_orm_system_free(wdir);
    if (cfs_rc != cfs_errc_success) {
      cfs_path_destroy(&cfs_dir);
      return C_ORM_ERROR_IO;
    }
  }
#else
  cfs_rc = cfs_path_init_str(&cfs_dir, dir_path);
#endif

  memset(&ec, 0, sizeof(ec));
  cfs_rc = cfs_dir_itr_init(&cfs_dir, &it, &ec);
  cfs_path_destroy(&cfs_dir);
  if (cfs_rc != cfs_errc_success) {
    LOG_DEBUG("c_orm_migration_load_dir: directory not found");
    return C_ORM_ERROR_NOT_FOUND;
  }

  for (;;) {
    const cfs_char_t *raw_path = NULL;
    char *utf8_name = NULL;
    char *full_path = NULL;
    size_t flen;
    size_t dlen;
    char ver_buf[64];
    char name_buf[128];
    c_orm_migration_t *slot = NULL;
    c_orm_error_t rc;

    cfs_rc = cfs_dir_itr_next(it, &entry, &ec);
    if (!entry) {
      break;
    }

    cfs_rc = cfs_path_c_str(&entry->path, &raw_path);

#if defined(CFS_OS_WINDOWS) && defined(CFS_UNICODE)
    {
      cfs_size_t req = 0;
      if (cfs_utf16_to_utf8(raw_path, NULL, 0, &req) != cfs_errc_success ||
          req == 0) {
        cfs_dir_itr_close(it);
        (void)c_orm_migration_free_array(migs, count);
        return C_ORM_ERROR_IO;
      }
      if (c_orm_system_malloc(req, (void **)&utf8_name) != C_ORM_OK ||
          !utf8_name) {
        cfs_dir_itr_close(it);
        (void)c_orm_migration_free_array(migs, count);
        return C_ORM_ERROR_MEMORY;
      }
      if (cfs_utf16_to_utf8(raw_path, utf8_name, req, NULL) !=
          cfs_errc_success) {
        (void)c_orm_system_free(utf8_name);
        cfs_dir_itr_close(it);
        (void)c_orm_migration_free_array(migs, count);
        return C_ORM_ERROR_IO;
      }
    }
#else
    if (c_orm_strdup(raw_path, &utf8_name) != C_ORM_OK) {
      cfs_dir_itr_close(it);
      (void)c_orm_migration_free_array(migs, count);
      return C_ORM_ERROR_MEMORY;
    }
#endif

    flen = strlen(utf8_name);
    if (flen <= 4 || strcmp(utf8_name + flen - 4, ".sql") != 0) {
      (void)c_orm_system_free(utf8_name);
      continue;
    }

    dlen = strlen(dir_path);
    if (c_orm_system_malloc(dlen + 1 + flen + 1, (void **)&full_path) !=
        C_ORM_OK) {
      (void)c_orm_system_free(utf8_name);
      cfs_dir_itr_close(it);
      (void)c_orm_migration_free_array(migs, count);
      return C_ORM_ERROR_MEMORY;
    }
    memcpy(full_path, dir_path, dlen);
    full_path[dlen] = '/';
    memcpy(full_path + dlen + 1, utf8_name, flen + 1);

    if (flen > 7 && strcmp(utf8_name + flen - 7, ".up.sql") == 0) {
      char *sql_text = NULL;
      (void)split_stem_version_name(utf8_name, flen - 7, ver_buf,
                                    sizeof(ver_buf), name_buf,
                                    sizeof(name_buf));
      (void)c_orm_system_free(utf8_name);
      rc = read_sql_file_alloc(full_path, &sql_text);
      (void)c_orm_system_free(full_path);
      if (rc != C_ORM_OK) {
        cfs_dir_itr_close(it);
        (void)c_orm_migration_free_array(migs, count);
        return rc;
      }
      rc = find_or_insert_migration_slot(&migs, &count, &cap, ver_buf, name_buf,
                                         &slot);
      if (rc != C_ORM_OK) {
        (void)c_orm_system_free(sql_text);
        cfs_dir_itr_close(it);
        (void)c_orm_migration_free_array(migs, count);
        return rc;
      }
      if (slot->up_sql) {
        (void)c_orm_system_free(slot->up_sql);
      }
      slot->up_sql = sql_text;
    } else if (flen > 9 && strcmp(utf8_name + flen - 9, ".down.sql") == 0) {
      char *sql_text = NULL;
      (void)split_stem_version_name(utf8_name, flen - 9, ver_buf,
                                    sizeof(ver_buf), name_buf,
                                    sizeof(name_buf));
      (void)c_orm_system_free(utf8_name);
      rc = read_sql_file_alloc(full_path, &sql_text);
      (void)c_orm_system_free(full_path);
      if (rc != C_ORM_OK) {
        cfs_dir_itr_close(it);
        (void)c_orm_migration_free_array(migs, count);
        return rc;
      }
      rc = find_or_insert_migration_slot(&migs, &count, &cap, ver_buf, name_buf,
                                         &slot);
      if (rc != C_ORM_OK) {
        (void)c_orm_system_free(sql_text);
        cfs_dir_itr_close(it);
        (void)c_orm_migration_free_array(migs, count);
        return rc;
      }
      if (slot->down_sql) {
        (void)c_orm_system_free(slot->down_sql);
      }
      slot->down_sql = sql_text;
    } else {
      struct MigrationStatements stmts;
      (void)split_stem_version_name(utf8_name, flen - 4, ver_buf,
                                    sizeof(ver_buf), name_buf,
                                    sizeof(name_buf));
      (void)c_orm_system_free(utf8_name);
      rc = parse_migration_file(full_path, &stmts);
      (void)c_orm_system_free(full_path);
      if (rc != C_ORM_OK) {
        cfs_dir_itr_close(it);
        (void)c_orm_migration_free_array(migs, count);
        return rc;
      }
      rc = find_or_insert_migration_slot(&migs, &count, &cap, ver_buf, name_buf,
                                         &slot);
      if (rc != C_ORM_OK) {
        (void)migration_statements_free(&stmts);
        cfs_dir_itr_close(it);
        (void)c_orm_migration_free_array(migs, count);
        return rc;
      }
      if (stmts.up_statement) {
        if (slot->up_sql) {
          (void)c_orm_system_free(slot->up_sql);
        }
        slot->up_sql = stmts.up_statement;
        stmts.up_statement = NULL;
      }
      if (stmts.down_statement) {
        if (slot->down_sql) {
          (void)c_orm_system_free(slot->down_sql);
        }
        slot->down_sql = stmts.down_statement;
        stmts.down_statement = NULL;
      }
      (void)migration_statements_free(&stmts);
    }
  }

  cfs_dir_itr_close(it);

  for (i = 0; i < count; i++) {
    (void)compute_sql_fnv1a_hex(migs[i].up_sql, migs[i].hash,
                                sizeof(migs[i].hash));
  }

  if (count > 1) {
    qsort(migs, count, sizeof(c_orm_migration_t),
          compare_c_orm_migration_entries);
  }

  *out_migrations = migs;
  *out_count = count;
  LOG_DEBUG("c_orm_migration_load_dir: exiting");
  return C_ORM_OK;
}

/**
 * @brief Frees an array of migrations.
 * @param migrations The array of migrations to free.
 * @param count The number of migrations in the array.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_migration_free_array(c_orm_migration_t *migrations, size_t count) {
  size_t i;
  c_orm_error_t rc = C_ORM_OK;
  c_orm_error_t frc;
  LOG_DEBUG("c_orm_migration_free_array: entered");
  if (!migrations) {
    LOG_DEBUG("c_orm_migration_free_array: null migrations, exiting");
    return C_ORM_OK;
  }
  for (i = 0; i < count; i++) {
    if (migrations[i].up_sql) {
      frc = c_orm_system_free(migrations[i].up_sql);
      if (frc != C_ORM_OK)
        rc = frc;
    }
    if (migrations[i].down_sql) {
      frc = c_orm_system_free(migrations[i].down_sql);
      if (frc != C_ORM_OK)
        rc = frc;
    }
  }
  frc = c_orm_system_free(migrations);
  if (frc != C_ORM_OK)
    rc = frc;
  LOG_DEBUG("c_orm_migration_free_array: exiting");
  return rc;
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

    /* Check if applied using parameterized prepared statement */
    {
      const char *chk_sql =
          (db->driver_name && (strcmp(db->driver_name, "db2") == 0 ||
                               strcmp(db->driver_name, "odbc") == 0))
              ? "SELECT version FROM _c_orm_migrations WHERE version = ?"
              : "SELECT id FROM _c_orm_migrations WHERE version = ?";
      if (db->vtable->prepare(db, chk_sql, &q) == C_ORM_OK) {
        if (db->vtable->bind_string) {
          (void)db->vtable->bind_string(q, 1, mig->version);
        }
        (void)db->vtable->step(q, &has_row);
        (void)db->vtable->finalize(q);
        q = NULL;
      }
    }

    if (has_row) {
      continue; /* Already applied */
    }

    if (options && options->log_cb) {
      char log_msg[1024];
      C_ORM_SPRINTF(log_msg, sizeof(log_msg), "Applying migration %s: %s",
                    mig->version, mig->name);
      (void)options->log_cb(log_msg);
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

    rc = c_orm_execute_raw(
        db, (db->driver_name && strcmp(db->driver_name, "db2") == 0)
                ? "SAVEPOINT c_orm_mig_step ON ROLLBACK RETAIN CURSORS"
                : "SAVEPOINT c_orm_mig_step");
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_migrate_all: savepoint error");
      unlock_rc = c_orm_migration_unlock(db);
      if (unlock_rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_migrate_all: unlock failed during savepoint error");
        return unlock_rc;
      }
      return rc;
    }

    if (mig->up_sql && strlen(mig->up_sql) > 0) {
      rc = c_orm_execute_raw(db, mig->up_sql);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_migrate_all: up_sql error");
        (void)c_orm_execute_raw(db, "ROLLBACK TO SAVEPOINT c_orm_mig_step");
        (void)c_orm_execute_raw(db, "RELEASE SAVEPOINT c_orm_mig_step");
        unlock_rc = c_orm_migration_unlock(db);
        if (unlock_rc != C_ORM_OK) {
          LOG_DEBUG("c_orm_migrate_all: unlock failed during up_sql error");
          return unlock_rc;
        }
        return rc;
      }
    }

    rc = db->vtable->prepare(
        db,
        "INSERT INTO _c_orm_migrations (version, name, hash) VALUES (?, ?, ?)",
        &q);
    if (rc == C_ORM_OK) {
      int ins_has_row = 0;
      if (db->vtable->bind_string) {
        rc = db->vtable->bind_string(q, 1, mig->version);
        if (rc == C_ORM_OK) {
          rc = db->vtable->bind_string(q, 2, mig->name);
        }
        if (rc == C_ORM_OK) {
          rc = db->vtable->bind_string(q, 3, mig->hash[0] ? mig->hash : "none");
        }
      }
      if (rc == C_ORM_OK) {
        rc = db->vtable->step(q, &ins_has_row);
      }
      {
        c_orm_error_t fin_rc = db->vtable->finalize(q);
        if (fin_rc != C_ORM_OK && rc == C_ORM_OK) {
          rc = fin_rc;
        }
      }
      q = NULL;
    }
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_migrate_all: insert migration error");
      (void)c_orm_execute_raw(db, "ROLLBACK TO SAVEPOINT c_orm_mig_step");
      (void)c_orm_execute_raw(db, "RELEASE SAVEPOINT c_orm_mig_step");
      unlock_rc = c_orm_migration_unlock(db);
      if (unlock_rc != C_ORM_OK) {
        LOG_DEBUG(
            "c_orm_migrate_all: unlock failed during insert migration error");
        return unlock_rc;
      }
      return rc;
    }

    rc = c_orm_execute_raw(db, "RELEASE SAVEPOINT c_orm_mig_step");
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_migrate_all: release savepoint error");
      unlock_rc = c_orm_migration_unlock(db);
      if (unlock_rc != C_ORM_OK) {
        LOG_DEBUG(
            "c_orm_migrate_all: unlock failed during release savepoint error");
        return unlock_rc;
      }
      return rc;
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
    c_orm_query_t *del_q = NULL;
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
      C_ORM_SPRINTF(log_msg, sizeof(log_msg), "Rolling back migration %s: %s",
                    mig->version, mig->name);
      (void)options->log_cb(log_msg);
    }

    if (options && options->dry_run) {
      rolled_back++;
      continue;
    }

    rc = c_orm_execute_raw(
        db, (db->driver_name && strcmp(db->driver_name, "db2") == 0)
                ? "SAVEPOINT c_orm_mig_step_rb ON ROLLBACK RETAIN CURSORS"
                : "SAVEPOINT c_orm_mig_step_rb");
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_migrate_rollback: savepoint error");
      break;
    }

    if (mig->down_sql && strlen(mig->down_sql) > 0) {
      rc = c_orm_execute_raw(db, mig->down_sql);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_migrate_rollback: down_sql error");
        (void)c_orm_execute_raw(db, "ROLLBACK TO SAVEPOINT c_orm_mig_step_rb");
        (void)c_orm_execute_raw(db, "RELEASE SAVEPOINT c_orm_mig_step_rb");
        break;
      }
    }

    rc = db->vtable->prepare(
        db, "DELETE FROM _c_orm_migrations WHERE version = ?", &del_q);
    if (rc == C_ORM_OK) {
      int del_has_row = 0;
      if (db->vtable->bind_string) {
        rc = db->vtable->bind_string(del_q, 1, mig->version);
      }
      if (rc == C_ORM_OK) {
        rc = db->vtable->step(del_q, &del_has_row);
      }
      {
        c_orm_error_t fin_rc = db->vtable->finalize(del_q);
        if (fin_rc != C_ORM_OK && rc == C_ORM_OK) {
          rc = fin_rc;
        }
      }
      del_q = NULL;
    }
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_migrate_rollback: delete migration error");
      (void)c_orm_execute_raw(db, "ROLLBACK TO SAVEPOINT c_orm_mig_step_rb");
      (void)c_orm_execute_raw(db, "RELEASE SAVEPOINT c_orm_mig_step_rb");
      break;
    }

    rc = c_orm_execute_raw(db, "RELEASE SAVEPOINT c_orm_mig_step_rb");
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_migrate_rollback: release savepoint error");
      break;
    }
    rolled_back++;
  }

  if (applied) {
    (void)c_orm_migration_free_array(applied, applied_count);
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
  c_orm_query_t *q = NULL;
  int has_row = 0;
  size_t cap = 10;
  cdd_c_meta_t *meta = NULL;
  char *meta_name = NULL;
  cdd_c_prop_meta_t *props_arr = NULL;

  LOG_DEBUG("c_orm_migration_fetch_table_schema: entered");

  if (!db || !table_name || !out_schema) {
    LOG_DEBUG("c_orm_migration_fetch_table_schema: validation error");
    rc = C_ORM_ERROR_MEMORY;
    return rc;
  }

  if (db->driver_name && (strcmp(db->driver_name, "db2") == 0 ||
                          strcmp(db->driver_name, "odbc") == 0)) {
    c_orm_odbc_column_info_t *odbc_cols = NULL;
    size_t odbc_count = 0;
    size_t idx;
    int used_catalog_sql = 0;

    if (strcmp(db->driver_name, "db2") == 0) {
      const char *luw_sql =
          "SELECT COLNAME, TYPENAME, LENGTH, SCALE, NULLS FROM SYSCAT.COLUMNS "
          "WHERE TABNAME = ? ORDER BY COLNO ASC";
      const char *zos_sql =
          "SELECT NAME, COLTYPE, LENGTH, SCALE, NULLS FROM SYSIBM.SYSCOLUMNS "
          "WHERE TBNAME = ? ORDER BY COLNO ASC";
      q = NULL;
      if (db->vtable->prepare(db, luw_sql, &q) == C_ORM_OK) {
        if (db->vtable->bind_string(q, 1, table_name) == C_ORM_OK &&
            db->vtable->step(q, &has_row) == C_ORM_OK) {
          used_catalog_sql = 1;
        } else {
          c_orm_error_t fin_rc = db->vtable->finalize(q);
          if (fin_rc != C_ORM_OK) {
            return fin_rc;
          }
          q = NULL;
        }
      }
      if (!used_catalog_sql) {
        q = NULL;
        if (db->vtable->prepare(db, zos_sql, &q) == C_ORM_OK) {
          if (db->vtable->bind_string(q, 1, table_name) == C_ORM_OK &&
              db->vtable->step(q, &has_row) == C_ORM_OK) {
            used_catalog_sql = 1;
          } else {
            c_orm_error_t fin_rc = db->vtable->finalize(q);
            if (fin_rc != C_ORM_OK) {
              return fin_rc;
            }
            q = NULL;
          }
        }
      }
    }

    if (used_catalog_sql) {
      c_orm_error_t fin_rc;
      if (c_orm_system_malloc(sizeof(cdd_c_meta_t), (void **)&meta) !=
          C_ORM_OK) {
        fin_rc = db->vtable->finalize(q);
        return (fin_rc != C_ORM_OK) ? fin_rc : C_ORM_ERROR_MEMORY;
      }
      if (c_orm_system_malloc(strlen(table_name) + 1, (void **)&meta_name) !=
          C_ORM_OK) {
        (void)c_orm_system_free(meta);
        fin_rc = db->vtable->finalize(q);
        return (fin_rc != C_ORM_OK) ? fin_rc : C_ORM_ERROR_MEMORY;
      }
      (void)C_ORM_STRCPY(meta_name, strlen(table_name) + 1, table_name);
      meta->name = meta_name;
      meta->size = 0;
      meta->num_props = 0;
      meta->driver_ctx = NULL;
      if (c_orm_system_malloc(sizeof(cdd_c_prop_meta_t) * cap,
                              (void **)&props_arr) != C_ORM_OK) {
        (void)c_orm_system_free(meta_name);
        (void)c_orm_system_free(meta);
        fin_rc = db->vtable->finalize(q);
        return (fin_rc != C_ORM_OK) ? fin_rc : C_ORM_ERROR_MEMORY;
      }
      meta->props = props_arr;

      while (has_row) {
        const char *col_name = NULL;
        const char *col_type = NULL;
        cdd_c_prop_meta_t *prop;
        char *pname = NULL;
        char *ptype = NULL;

        if (db->vtable->get_string(q, 0, &col_name) != C_ORM_OK) {
          col_name = NULL;
        }
        if (db->vtable->get_string(q, 1, &col_type) != C_ORM_OK) {
          col_type = NULL;
        }
        if (col_name && col_type) {
          if (meta->num_props >= cap) {
            cdd_c_prop_meta_t *new_props = NULL;
            cap *= 2;
            if (c_orm_system_realloc(props_arr, sizeof(cdd_c_prop_meta_t) * cap,
                                     (void **)&new_props) != C_ORM_OK) {
              fin_rc = db->vtable->finalize(q);
              c_orm_migration_free_table_schema(meta);
              return (fin_rc != C_ORM_OK) ? fin_rc : C_ORM_ERROR_MEMORY;
            }
            props_arr = new_props;
            meta->props = props_arr;
          }
          prop = &props_arr[meta->num_props];
          memset(prop, 0, sizeof(cdd_c_prop_meta_t));
          if (c_orm_strdup(col_name, &pname) != C_ORM_OK ||
              c_orm_strdup(col_type, &ptype) != C_ORM_OK) {
            (void)c_orm_system_free(pname);
            (void)c_orm_system_free(ptype);
            fin_rc = db->vtable->finalize(q);
            c_orm_migration_free_table_schema(meta);
            return (fin_rc != C_ORM_OK) ? fin_rc : C_ORM_ERROR_MEMORY;
          }
          prop->name = pname;
          prop->type = ptype;
          meta->num_props++;
        }
        rc = db->vtable->step(q, &has_row);
        if (rc != C_ORM_OK) {
          fin_rc = db->vtable->finalize(q);
          c_orm_migration_free_table_schema(meta);
          return (fin_rc != C_ORM_OK) ? fin_rc : rc;
        }
      }
      rc = db->vtable->finalize(q);
      if (rc != C_ORM_OK) {
        c_orm_migration_free_table_schema(meta);
        return rc;
      }
      *out_schema = meta;
      return C_ORM_OK;
    }

    rc = c_orm_odbc_list_columns(db, NULL, table_name, &odbc_cols, &odbc_count);
    if (rc != C_ORM_OK) {
      return rc;
    }
    if (c_orm_system_malloc(sizeof(cdd_c_meta_t), (void **)&meta) != C_ORM_OK ||
        c_orm_strdup(table_name, &meta_name) != C_ORM_OK) {
      (void)c_orm_system_free(meta);
      (void)c_orm_odbc_free_column_list(odbc_cols, odbc_count);
      return C_ORM_ERROR_MEMORY;
    }
    meta->name = meta_name;
    meta->size = 0;
    meta->num_props = 0;
    meta->driver_ctx = NULL;
    cap = odbc_count > 0 ? odbc_count : 4;
    if (c_orm_system_malloc(sizeof(cdd_c_prop_meta_t) * cap,
                            (void **)&props_arr) != C_ORM_OK) {
      (void)c_orm_system_free(meta_name);
      (void)c_orm_system_free(meta);
      (void)c_orm_odbc_free_column_list(odbc_cols, odbc_count);
      return C_ORM_ERROR_MEMORY;
    }
    meta->props = props_arr;

    for (idx = 0; idx < odbc_count; ++idx) {
      char *pname = NULL;
      char *ptype = NULL;
      memset(&props_arr[meta->num_props], 0, sizeof(cdd_c_prop_meta_t));
      if (c_orm_strdup(odbc_cols[idx].name, &pname) != C_ORM_OK ||
          c_orm_strdup(odbc_cols[idx].type_name, &ptype) != C_ORM_OK) {
        (void)c_orm_system_free(pname);
        (void)c_orm_system_free(ptype);
        (void)c_orm_odbc_free_column_list(odbc_cols, odbc_count);
        (void)c_orm_migration_free_table_schema(meta);
        return C_ORM_ERROR_MEMORY;
      }
      props_arr[meta->num_props].name = pname;
      props_arr[meta->num_props].type = ptype;
      meta->num_props++;
    }
    (void)c_orm_odbc_free_column_list(odbc_cols, odbc_count);
    *out_schema = meta;
    return C_ORM_OK;
  }

  /* Try SQLite first */
  C_ORM_SPRINTF(sql, sizeof(sql), "PRAGMA table_info('%s')", table_name);

  rc = c_orm_prepare_cached(db, sql, &q);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_migration_fetch_table_schema: prepare error");

    return rc;
  }

  if (c_orm_system_malloc(sizeof(cdd_c_meta_t), (void **)&meta) != C_ORM_OK) {
    LOG_DEBUG("c_orm_migration_fetch_table_schema: OOM");
    {
      c_orm_error_t _fin = c_orm_finalize_cached(db, q);
      if (_fin != C_ORM_OK) {
        return _fin;
      }
    }
    return C_ORM_ERROR_MEMORY;
  }

  if (c_orm_system_malloc(strlen(table_name) + 1, (void **)&meta_name) !=
      C_ORM_OK) {
    LOG_DEBUG("c_orm_migration_fetch_table_schema: OOM");
    (void)c_orm_system_free(meta);
    {
      c_orm_error_t _fin = c_orm_finalize_cached(db, q);
      if (_fin != C_ORM_OK) {
        return _fin;
      }
    }
    return C_ORM_ERROR_MEMORY;
  }
  C_ORM_STRCPY(meta_name, strlen(table_name) + 1, table_name);
  meta->name = meta_name;
  meta->size = 0;
  meta->num_props = 0;
  meta->driver_ctx = NULL;
  if (c_orm_system_malloc(sizeof(cdd_c_prop_meta_t) * cap,
                          (void **)&props_arr) != C_ORM_OK) {
    props_arr = NULL;
  }
  meta->props = props_arr;

  if (!props_arr) {
    LOG_DEBUG("c_orm_migration_fetch_table_schema: OOM");
    (void)c_orm_system_free(meta_name);
    (void)c_orm_system_free(meta);
    {
      c_orm_error_t _fin = c_orm_finalize_cached(db, q);
      if (_fin != C_ORM_OK) {
        return _fin;
      }
    }
    return C_ORM_ERROR_MEMORY;
  }

  for (;;) {
    const char *col_name = NULL;
    const char *col_type = NULL;
    cdd_c_prop_meta_t *prop;
    char *pname = NULL;
    char *ptype = NULL;

    rc = db->vtable->step(q, &has_row);
    if (rc != C_ORM_OK) {
      goto fail_schema;
    }
    if (!has_row) {
      break;
    }

    rc = db->vtable->get_string(q, 1, &col_name);
    if (rc != C_ORM_OK) {
      goto fail_schema;
    }
    rc = db->vtable->get_string(q, 2, &col_type);
    if (rc != C_ORM_OK) {
      goto fail_schema;
    }

    if (!col_name || !col_type) {
      continue;
    }

    if (meta->num_props >= cap) {
      cdd_c_prop_meta_t *new_props = NULL;
      cap *= 2;
      if (c_orm_system_realloc(props_arr, sizeof(cdd_c_prop_meta_t) * cap,
                               (void **)&new_props) != C_ORM_OK) {
        LOG_DEBUG("c_orm_migration_fetch_table_schema: OOM during realloc");
        rc = C_ORM_ERROR_MEMORY;
        goto fail_schema;
      }
      props_arr = new_props;
      meta->props = props_arr;
    }

    prop = &props_arr[meta->num_props++];
    memset(prop, 0, sizeof(cdd_c_prop_meta_t));

    if (c_orm_system_malloc(strlen(col_name) + 1, (void **)&pname) !=
        C_ORM_OK) {
      LOG_DEBUG("c_orm_migration_fetch_table_schema: OOM");
      rc = C_ORM_ERROR_MEMORY;
      goto fail_schema;
    }
    C_ORM_STRCPY(pname, strlen(col_name) + 1, col_name);
    prop->name = pname;

    if (c_orm_system_malloc(strlen(col_type) + 1, (void **)&ptype) !=
        C_ORM_OK) {
      LOG_DEBUG("c_orm_migration_fetch_table_schema: OOM");
      rc = C_ORM_ERROR_MEMORY;
      goto fail_schema;
    }
    C_ORM_STRCPY(ptype, strlen(col_type) + 1, col_type);
    prop->type = ptype;
    prop->offset = 0;
  }

  {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, q);
    if (fin_rc != C_ORM_OK) {
      c_orm_error_t free_rc = c_orm_migration_free_table_schema(meta);
      if (free_rc != C_ORM_OK) {
        return free_rc;
      }
      return fin_rc;
    }
  }

  *out_schema = meta;
  LOG_DEBUG("c_orm_migration_fetch_table_schema: exiting");
  return C_ORM_OK;

fail_schema: {
  c_orm_error_t free_rc = c_orm_migration_free_table_schema(meta);
  c_orm_error_t fin_rc = c_orm_finalize_cached(db, q);
  if (fin_rc != C_ORM_OK) {
    return fin_rc;
  }
  if (free_rc != C_ORM_OK) {
    return free_rc;
  }
  return rc;
}
}

/**
 * @brief Frees a table schema.
 * @param schema The schema to free.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_migration_free_table_schema(cdd_c_meta_t *schema) {
  size_t i;
  c_orm_error_t rc = C_ORM_OK;
  c_orm_error_t frc;
  LOG_DEBUG("c_orm_migration_free_table_schema: entered");
  if (!schema) {
    LOG_DEBUG("c_orm_migration_free_table_schema: null schema, exiting");
    return C_ORM_OK;
  }
  if (schema->name) {
    frc = c_orm_system_free((void *)(size_t)schema->name);
    if (frc != C_ORM_OK)
      rc = frc;
  }
  if (schema->props) {
    for (i = 0; i < schema->num_props; i++) {
      if (schema->props[i].name) {
        frc = c_orm_system_free((void *)(size_t)schema->props[i].name);
        if (frc != C_ORM_OK)
          rc = frc;
      }
      if (schema->props[i].type) {
        frc = c_orm_system_free((void *)(size_t)schema->props[i].type);
        if (frc != C_ORM_OK)
          rc = frc;
      }
    }
    frc = c_orm_system_free((void *)(size_t)schema->props);
    if (frc != C_ORM_OK)
      rc = frc;
  }
  frc = c_orm_system_free(schema);
  if (frc != C_ORM_OK)
    rc = frc;
  LOG_DEBUG("c_orm_migration_free_table_schema: exiting");
  return rc;
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
  c_orm_migration_t *migs = NULL;

  LOG_DEBUG("c_orm_migration_get_applied: entered");

  if (!db || !out_migrations || !out_count) {
    LOG_DEBUG("c_orm_migration_get_applied: validation error");
    rc = C_ORM_ERROR_VALIDATION;
    return rc;
  }

  {
    const char *app_sql =
        (db->driver_name && (strcmp(db->driver_name, "db2") == 0 ||
                             strcmp(db->driver_name, "odbc") == 0))
            ? "SELECT version, name, hash FROM _c_orm_migrations ORDER BY "
              "version ASC"
            : "SELECT version, name, hash FROM _c_orm_migrations ORDER BY id "
              "ASC";
    rc = c_orm_prepare_cached(db, app_sql, &q);
  }
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_migration_get_applied: prepare error");

    return rc;
  }

  if (c_orm_system_malloc(cap * sizeof(c_orm_migration_t), (void **)&migs) !=
      C_ORM_OK) {
    LOG_DEBUG("c_orm_migration_get_applied: OOM");
    {
      c_orm_error_t _fin = c_orm_finalize_cached(db, q);
      if (_fin != C_ORM_OK) {
        return _fin;
      }
    }
    return C_ORM_ERROR_MEMORY;
  }

  for (;;) {
    const char *version = NULL;
    const char *name = NULL;
    const char *hash = NULL;

    rc = db->vtable->step(q, &has_row);
    if (rc != C_ORM_OK) {
      c_orm_error_t fin_rc = c_orm_finalize_cached(db, q);
      (void)c_orm_migration_free_array(migs, count);
      if (fin_rc != C_ORM_OK) {
        return fin_rc;
      }
      return rc;
    }
    if (!has_row) {
      break;
    }

    rc = db->vtable->get_string(q, 0, &version);
    if (rc != C_ORM_OK) {
      c_orm_error_t fin_rc = c_orm_finalize_cached(db, q);
      (void)c_orm_migration_free_array(migs, count);
      if (fin_rc != C_ORM_OK) {
        return fin_rc;
      }
      return rc;
    }
    rc = db->vtable->get_string(q, 1, &name);
    if (rc != C_ORM_OK) {
      c_orm_error_t fin_rc = c_orm_finalize_cached(db, q);
      (void)c_orm_migration_free_array(migs, count);
      if (fin_rc != C_ORM_OK) {
        return fin_rc;
      }
      return rc;
    }
    rc = db->vtable->get_string(q, 2, &hash);
    if (rc != C_ORM_OK) {
      c_orm_error_t fin_rc = c_orm_finalize_cached(db, q);
      (void)c_orm_migration_free_array(migs, count);
      if (fin_rc != C_ORM_OK) {
        return fin_rc;
      }
      return rc;
    }

    if (count >= cap) {
      c_orm_migration_t *new_migs = NULL;
      cap *= 2;
      if (c_orm_system_realloc(migs, cap * sizeof(c_orm_migration_t),
                               (void **)&new_migs) != C_ORM_OK) {
        LOG_DEBUG("c_orm_migration_get_applied: OOM during realloc");
        (void)c_orm_migration_free_array(migs, count);
        {
          c_orm_error_t _fin = c_orm_finalize_cached(db, q);
          if (_fin != C_ORM_OK) {
            return _fin;
          }
        }
        return C_ORM_ERROR_MEMORY;
      }
      migs = new_migs;
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

  {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, q);
    if (fin_rc != C_ORM_OK) {
      (void)c_orm_migration_free_array(migs, count);
      return fin_rc;
    }
  }

  if (count == 0) {
    (void)c_orm_system_free(migs);
    migs = NULL;
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

  if (!db) {
    return C_ORM_ERROR_VALIDATION;
  }

  if (db->driver_name && strcmp(db->driver_name, "db2") == 0) {
    rc = c_orm_migration_init_table(db);
    if (rc != C_ORM_OK) {
      return rc;
    }
    rc = c_orm_transaction_begin(db);
    if (rc != C_ORM_OK) {
      return rc;
    }
    rc =
        c_orm_execute_raw(db, "LOCK TABLE _c_orm_migrations IN EXCLUSIVE MODE");
    if (rc != C_ORM_OK) {
      c_orm_error_t rb_rc = c_orm_transaction_rollback(db);
      return (rb_rc != C_ORM_OK) ? rb_rc : rc;
    }
    return C_ORM_OK;
  }

  if (db->driver_name && strcmp(db->driver_name, "odbc") == 0) {
    rc = c_orm_migration_init_table(db);
    if (rc != C_ORM_OK) {
      return rc;
    }
    return c_orm_transaction_begin(db);
  }

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
  } else if (strcmp(db->driver_name, "db2") == 0 ||
             strcmp(db->driver_name, "odbc") == 0) {
    rc = c_orm_transaction_commit(db);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_migration_unlock: transaction commit failed");
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
