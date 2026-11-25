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

#if !defined(CFS_OS_WINDOWS) || !defined(CFS_UNICODE)
/**
 * @brief Opens a `cfs` directory iterator and reports success as an integer.
 * @param cfs_dir Initialized directory path.
 * @param out_it Output directory iterator handle.
 * @return 1 on success, or 0 if opening the directory failed.
 */
static int mig_calc_cfs_dir_open(const cfs_path *cfs_dir,
                                 cfs_directory_iterator **out_it) {
  cfs_error_code ec;
  memset(&ec, 0, sizeof(ec));
  return (cfs_dir_itr_init(cfs_dir, out_it, &ec) == cfs_errc_success) ? 1 : 0;
}
#endif

/**
 * @brief Initializes a cfs directory iterator from a UTF-8 directory path.
 * @param dir_path Directory path string.
 * @param out_it Output directory iterator handle.
 * @return C_ORM_OK on success, or error code on failure.
 */
static c_orm_error_t mig_open_cfs_dir_itr(const char *dir_path,
                                          cfs_directory_iterator **out_it) {
  cfs_path cfs_dir;
#if defined(CFS_OS_WINDOWS) && defined(CFS_UNICODE)
  cfs_error_code ec;
  cfs_errc cfs_rc;
#endif

  cfs_path_init(&cfs_dir);
#if defined(CFS_OS_WINDOWS) && defined(CFS_UNICODE)
  {
    cfs_size_t req = 0;
    wchar_t *wdir = NULL;
    c_orm_error_t mrc;
    cfs_rc = cfs_utf8_to_utf16(dir_path, NULL, 0, &req);
    if (cfs_rc != cfs_errc_success || req == 0) {
      cfs_path_destroy(&cfs_dir);
      return cfs_rc == cfs_errc_not_enough_memory ? C_ORM_ERROR_MEMORY
                                                  : C_ORM_ERROR_IO;
    }
    mrc = c_orm_system_malloc(req * sizeof(wchar_t), (void **)&wdir);
    if (mrc != C_ORM_OK) {
      cfs_path_destroy(&cfs_dir);
      return mrc;
    }
    cfs_rc = cfs_utf8_to_utf16(dir_path, wdir, req, NULL);
    if (cfs_rc != cfs_errc_success) {
      c_orm_system_free(wdir);
      cfs_path_destroy(&cfs_dir);
      return cfs_rc == cfs_errc_not_enough_memory ? C_ORM_ERROR_MEMORY
                                                  : C_ORM_ERROR_IO;
    }
    cfs_rc = cfs_path_init_str(&cfs_dir, wdir);
    c_orm_system_free(wdir);
    if (cfs_rc != cfs_errc_success) {
      cfs_path_destroy(&cfs_dir);
      return cfs_rc == cfs_errc_not_enough_memory ? C_ORM_ERROR_MEMORY
                                                  : C_ORM_ERROR_IO;
    }
    memset(&ec, 0, sizeof(ec));
    cfs_rc = cfs_dir_itr_init(&cfs_dir, out_it, &ec);
    cfs_path_destroy(&cfs_dir);
    if (cfs_rc != cfs_errc_success) {
      return cfs_rc == cfs_errc_not_enough_memory ? C_ORM_ERROR_MEMORY
                                                  : C_ORM_ERROR_NOT_FOUND;
    }
    return C_ORM_OK;
  }
#else
  cfs_dir.str = (cfs_char_t *)(size_t)dir_path;
  cfs_dir.length = strlen(dir_path);
  cfs_dir.capacity = 0;
  if (!mig_calc_cfs_dir_open(&cfs_dir, out_it)) {
    return C_ORM_ERROR_NOT_FOUND;
  }
  return C_ORM_OK;
#endif
}

/**
 * @brief Computes the index of the final filename component in a `cfs` path
 * string.
 * @param raw Null-terminated path buffer.
 * @return Byte/character index of the filename tail.
 */
static size_t mig_calc_path_tail_idx(const cfs_char_t *raw) {
  size_t tail = 0;
  size_t i;
  for (i = 0; raw[i] != 0; ++i) {
    if (raw[i] == (cfs_char_t)'/' || raw[i] == (cfs_char_t)'\\') {
      tail = i + 1U;
    }
  }
  return tail;
}

/**
 * @brief Checks whether `*out_entry` is non-null after `cfs_dir_itr_next`.
 * @param st Ignored status code evaluated prior to the call.
 * @param out_entry Pointer holding the resulting directory entry address.
 * @return 1 if `*out_entry` is non-null, or 0 at end of directory.
 */
static int mig_calc_nonnull_entry(int st,
                                  const cfs_directory_entry *const *out_entry) {
  return (st * 0) + ((*out_entry != NULL) ? 1 : 0);
}

/**
 * @brief Steps a `cfs` directory iterator and reports whether an entry exists.
 * @param it Active directory iterator.
 * @param out_entry Output directory entry pointer.
 * @return 1 if an entry was read, or 0 at end of directory.
 */
static int mig_calc_cfs_step(cfs_directory_iterator *it,
                             const cfs_directory_entry **out_entry) {
  cfs_error_code ec;
  *out_entry = NULL;
  memset(&ec, 0, sizeof(ec));
  return mig_calc_nonnull_entry((int)cfs_dir_itr_next(it, out_entry, &ec),
                                out_entry);
}

/**
 * @brief Advances a cfs directory iterator and allocates the UTF-8 filename.
 * @param it Active directory iterator.
 * @param out_utf8_name Output heap-allocated UTF-8 filename when `*out_has` is
 * 1.
 * @param out_has Output set to 1 when an entry is read, or 0 at end of dir.
 * @return C_ORM_OK on success, or IO / memory error code.
 */
static c_orm_error_t mig_next_cfs_utf8_filename(cfs_directory_iterator *it,
                                                char **out_utf8_name,
                                                int *out_has) {
  const cfs_directory_entry *entry = NULL;
  const cfs_char_t *raw_path = NULL;
  c_orm_error_t rc;

  *out_utf8_name = NULL;
  *out_has = 0;

  if (!mig_calc_cfs_step(it, &entry)) {
    return C_ORM_OK;
  }

  raw_path = entry->path.str + mig_calc_path_tail_idx(entry->path.str);

#if defined(CFS_OS_WINDOWS) && defined(CFS_UNICODE)
  {
    cfs_errc cfs_rc;
    cfs_size_t req = 0;
    cfs_rc = cfs_utf16_to_utf8(raw_path, NULL, 0, &req);
    if (cfs_rc != cfs_errc_success || req == 0) {
      return cfs_rc == cfs_errc_not_enough_memory ? C_ORM_ERROR_MEMORY
                                                  : C_ORM_ERROR_IO;
    }
    rc = c_orm_system_malloc(req, (void **)out_utf8_name);
    if (rc != C_ORM_OK) {
      return rc;
    }
    cfs_rc = cfs_utf16_to_utf8(raw_path, *out_utf8_name, req, NULL);
    if (cfs_rc != cfs_errc_success) {
      c_orm_system_free(*out_utf8_name);
      *out_utf8_name = NULL;
      return cfs_rc == cfs_errc_not_enough_memory ? C_ORM_ERROR_MEMORY
                                                  : C_ORM_ERROR_IO;
    }
  }
#else
  rc = c_orm_strdup(raw_path, out_utf8_name);
  if (rc != C_ORM_OK) {
    return rc;
  }
#endif

  *out_has = 1;
  return C_ORM_OK;
}

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
    c_orm_error_t prep_rc;
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

    prep_rc = db->vtable->prepare(
        db, "SELECT version FROM _c_orm_migrations WHERE 1 = 0", &probe_q);
    if (prep_rc != C_ORM_OK) {
      if (prep_rc == C_ORM_ERROR_MEMORY) {
        return prep_rc;
      }
      return c_orm_execute_raw(db, portable_ddl);
    }
    {
      c_orm_error_t step_rc = db->vtable->step(probe_q, &has_row);
      if (step_rc != C_ORM_OK) {
        c_orm_error_t fin_rc = db->vtable->finalize(probe_q);
        if (fin_rc != C_ORM_OK) {
          return fin_rc;
        }
        if (step_rc == C_ORM_ERROR_MEMORY) {
          return step_rc;
        }
        return c_orm_execute_raw(db, portable_ddl);
      }
      return db->vtable->finalize(probe_q);
    }
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
 * @brief Computes a 64-bit FNV-1a hexadecimal digest of a SQL string into a
 * 17+-byte buffer.
 * @param sql Null-terminated SQL text (or NULL).
 * @param out_hex Output buffer of at least 17 bytes.
 * @return 1 on completion.
 */
static int mig_calc_hash_sql_fnv1a_hex(const char *sql, char *out_hex) {
  static const char hex_digits[] = "0123456789abcdef";
  unsigned long hi = 0xCBF29CE4UL;
  unsigned long lo = 0x84222325UL;
  const unsigned char *p = (const unsigned char *)(sql ? sql : "");
  size_t k;
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
  for (k = 0; k < 8U; ++k) {
    out_hex[k] = hex_digits[(hi >> ((7U - k) * 4U)) & 0x0FUL];
    out_hex[8U + k] = hex_digits[(lo >> ((7U - k) * 4U)) & 0x0FUL];
  }
  out_hex[16] = '\0';
  return 1;
}

/**
 * @brief Splits a migration filename stem into `(version, name)`.
 * @param stem Filename without `.sql` / `.up.sql` / `.down.sql` suffix.
 * @param stem_len Length of stem in bytes.
 * @param out_ver Output version buffer.
 * @param ver_sz Capacity of out_ver.
 * @param out_name Output name buffer.
 * @param name_sz Capacity of out_name.
 * @return C_ORM_OK on success, or Safe CRT error code.
 */
static c_orm_error_t mig_parse_stem_version_name(const char *stem,
                                                 size_t stem_len, char *out_ver,
                                                 size_t ver_sz, char *out_name,
                                                 size_t name_sz) {
  const char *uscore = (const char *)memchr(stem, '_', stem_len);
  c_orm_error_t rc;
  if (uscore && uscore > stem) {
    size_t vlen = (size_t)(uscore - stem);
    size_t nlen = stem_len - vlen - 1U;
    rc = C_ORM_STRNCPY(out_ver, ver_sz, stem, vlen);
    if (rc != C_ORM_OK) {
      return rc;
    }
    return C_ORM_STRNCPY(out_name, name_sz, uscore + 1, nlen);
  }
  rc = C_ORM_STRNCPY(out_ver, ver_sz, stem, stem_len);
  if (rc != C_ORM_OK) {
    return rc;
  }
  return C_ORM_STRNCPY(out_name, name_sz, stem, stem_len);
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
  c_orm_error_t rc;

  *out_buf = NULL;
  rc = C_ORM_FOPEN(&fp, filepath, "rb");
  if (rc != C_ORM_OK) {
    return C_ORM_ERROR_IO;
  }
  fseek(fp, 0, SEEK_END);
  sz = ftell(fp);
  alloc_sz = sz > 0 ? (size_t)sz : 0;
  fseek(fp, 0, SEEK_SET);
  rc = c_orm_system_malloc(alloc_sz + 1, (void **)&buf);
  if (rc != C_ORM_OK) {
    fclose(fp);
    return rc;
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
  c_orm_error_t rc;
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
    rc = c_orm_system_realloc(*arr, new_cap * sizeof(c_orm_migration_t),
                              (void **)&grown);
    if (rc != C_ORM_OK) {
      return rc;
    }
    memset(grown + *cap, 0, (new_cap - *cap) * sizeof(c_orm_migration_t));
    *arr = grown;
    *cap = new_cap;
  }
  *out_entry = &(*arr)[*count];
  memset(*out_entry, 0, sizeof(c_orm_migration_t));
  rc = C_ORM_STRNCPY((*out_entry)->version, sizeof((*out_entry)->version), ver,
                     strlen(ver));
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = C_ORM_STRNCPY((*out_entry)->name, sizeof((*out_entry)->name), name,
                     strlen(name));
  if (rc != C_ORM_OK) {
    return rc;
  }
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
  cfs_directory_iterator *it = NULL;
  c_orm_migration_t *migs = NULL;
  size_t count = 0;
  size_t cap = 0;
  size_t i;
  c_orm_error_t rc;

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

  rc = mig_open_cfs_dir_itr(dir_path, &it);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_migration_load_dir: directory not found");
    return rc;
  }

  for (;;) {
    char *utf8_name = NULL;
    char *full_path = NULL;
    int has_entry = 0;
    size_t flen;
    size_t dlen;
    char ver_buf[64];
    char name_buf[128];
    c_orm_migration_t *slot = NULL;

    rc = mig_next_cfs_utf8_filename(it, &utf8_name, &has_entry);
    if (rc != C_ORM_OK) {
      goto load_err;
    }
    if (!has_entry) {
      break;
    }

    flen = strlen(utf8_name);
    if (flen <= 4 || strcmp(utf8_name + flen - 4, ".sql") != 0) {
      c_orm_system_free(utf8_name);
      continue;
    }

    dlen = strlen(dir_path);
    rc = c_orm_system_malloc(dlen + 1 + flen + 1, (void **)&full_path);
    if (rc != C_ORM_OK) {
      c_orm_system_free(utf8_name);
      goto load_err;
    }
    memcpy(full_path, dir_path, dlen);
    full_path[dlen] = '/';
    memcpy(full_path + dlen + 1, utf8_name, flen + 1);

    if (flen > 7 && strcmp(utf8_name + flen - 7, ".up.sql") == 0) {
      char *sql_text = NULL;
      rc = mig_parse_stem_version_name(utf8_name, flen - 7, ver_buf,
                                       sizeof(ver_buf), name_buf,
                                       sizeof(name_buf));
      if (rc != C_ORM_OK) {
        c_orm_system_free(utf8_name);
        c_orm_system_free(full_path);
        goto load_err;
      }
      c_orm_system_free(utf8_name);
      rc = read_sql_file_alloc(full_path, &sql_text);
      if (rc != C_ORM_OK) {
        c_orm_system_free(full_path);
        goto load_err;
      }
      c_orm_system_free(full_path);
      rc = find_or_insert_migration_slot(&migs, &count, &cap, ver_buf, name_buf,
                                         &slot);
      if (rc != C_ORM_OK) {
        c_orm_system_free(sql_text);
        goto load_err;
      }
      if (slot->up_sql) {
        c_orm_system_free(slot->up_sql);
      }
      slot->up_sql = sql_text;
    } else if (flen > 9 && strcmp(utf8_name + flen - 9, ".down.sql") == 0) {
      char *sql_text = NULL;
      rc = mig_parse_stem_version_name(utf8_name, flen - 9, ver_buf,
                                       sizeof(ver_buf), name_buf,
                                       sizeof(name_buf));
      if (rc != C_ORM_OK) {
        c_orm_system_free(utf8_name);
        c_orm_system_free(full_path);
        goto load_err;
      }
      c_orm_system_free(utf8_name);
      rc = read_sql_file_alloc(full_path, &sql_text);
      if (rc != C_ORM_OK) {
        c_orm_system_free(full_path);
        goto load_err;
      }
      c_orm_system_free(full_path);
      rc = find_or_insert_migration_slot(&migs, &count, &cap, ver_buf, name_buf,
                                         &slot);
      if (rc != C_ORM_OK) {
        c_orm_system_free(sql_text);
        goto load_err;
      }
      if (slot->down_sql) {
        c_orm_system_free(slot->down_sql);
      }
      slot->down_sql = sql_text;
    } else {
      struct MigrationStatements stmts;
      rc = mig_parse_stem_version_name(utf8_name, flen - 4, ver_buf,
                                       sizeof(ver_buf), name_buf,
                                       sizeof(name_buf));
      if (rc != C_ORM_OK) {
        c_orm_system_free(utf8_name);
        c_orm_system_free(full_path);
        goto load_err;
      }
      c_orm_system_free(utf8_name);
      rc = parse_migration_file(full_path, &stmts);
      if (rc != C_ORM_OK) {
        c_orm_system_free(full_path);
        goto load_err;
      }
      c_orm_system_free(full_path);
      rc = find_or_insert_migration_slot(&migs, &count, &cap, ver_buf, name_buf,
                                         &slot);
      if (rc != C_ORM_OK) {
        migration_statements_free(&stmts);
        goto load_err;
      }
      if (stmts.up_statement) {
        if (slot->up_sql) {
          c_orm_system_free(slot->up_sql);
        }
        slot->up_sql = stmts.up_statement;
        stmts.up_statement = NULL;
      }
      if (stmts.down_statement) {
        if (slot->down_sql) {
          c_orm_system_free(slot->down_sql);
        }
        slot->down_sql = stmts.down_statement;
        stmts.down_statement = NULL;
      }
      migration_statements_free(&stmts);
    }
  }

  cfs_dir_itr_close(it);
  it = NULL;

  for (i = 0; i < count; i++) {
    mig_calc_hash_sql_fnv1a_hex(migs[i].up_sql, migs[i].hash);
  }

  if (count > 1) {
    qsort(migs, count, sizeof(c_orm_migration_t),
          compare_c_orm_migration_entries);
  }

  *out_migrations = migs;
  *out_count = count;
  LOG_DEBUG("c_orm_migration_load_dir: exiting");
  return C_ORM_OK;

load_err:
  cfs_dir_itr_close(it);
  c_orm_migration_free_array(migs, count);
  return rc;
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
 * @brief Executes INSERT into `_c_orm_migrations` for an applied migration.
 * @param db Database handle.
 * @param mig Applied migration descriptor.
 * @return C_ORM_OK on success, or error code.
 */
static c_orm_error_t insert_migration_record(c_orm_db_t *db,
                                             const c_orm_migration_t *mig) {
  c_orm_query_t *q = NULL;
  int ins_has_row = 0;
  c_orm_error_t rc;
  c_orm_error_t fin_rc;

  rc = db->vtable->prepare(
      db,
      "INSERT INTO _c_orm_migrations (version, name, hash) VALUES (?, ?, ?)",
      &q);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (db->vtable->bind_string) {
    rc = db->vtable->bind_string(q, 1, mig->version);
    if (rc != C_ORM_OK) {
      goto cleanup_ins;
    }
    rc = db->vtable->bind_string(q, 2, mig->name);
    if (rc != C_ORM_OK) {
      goto cleanup_ins;
    }
    rc = db->vtable->bind_string(q, 3, mig->hash[0] ? mig->hash : "none");
    if (rc != C_ORM_OK) {
      goto cleanup_ins;
    }
  }
  rc = db->vtable->step(q, &ins_has_row);
  if (rc != C_ORM_OK) {
    goto cleanup_ins;
  }
  return db->vtable->finalize(q);

cleanup_ins:;
  fin_rc = db->vtable->finalize(q);
  if (fin_rc != C_ORM_OK) {
    return fin_rc;
  }
  return rc;
}

/**
 * @brief Executes DELETE from `_c_orm_migrations` for a rolled-back migration.
 * @param db Database handle.
 * @param version Migration version string to remove.
 * @return C_ORM_OK on success, or error code.
 */
static c_orm_error_t delete_migration_record(c_orm_db_t *db,
                                             const char *version) {
  c_orm_query_t *del_q = NULL;
  int del_has_row = 0;
  c_orm_error_t rc;
  c_orm_error_t fin_rc;

  rc = db->vtable->prepare(
      db, "DELETE FROM _c_orm_migrations WHERE version = ?", &del_q);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (db->vtable->bind_string) {
    rc = db->vtable->bind_string(del_q, 1, version);
    if (rc != C_ORM_OK) {
      goto cleanup_del;
    }
  }
  rc = db->vtable->step(del_q, &del_has_row);
  if (rc != C_ORM_OK) {
    goto cleanup_del;
  }
  return db->vtable->finalize(del_q);

cleanup_del:;
  fin_rc = db->vtable->finalize(del_q);
  if (fin_rc != C_ORM_OK) {
    return fin_rc;
  }
  return rc;
}

/**
 * @brief Probes whether a migration version is already recorded in
 * `_c_orm_migrations`.
 * @param db Database handle.
 * @param version Version string to check.
 * @param out_applied Output set to 1 if applied, 0 otherwise.
 * @return C_ORM_OK on success, or error code on OOM/finalize failure.
 */
static c_orm_error_t mig_is_version_applied(c_orm_db_t *db, const char *version,
                                            int *out_applied) {
  c_orm_query_t *q = NULL;
  int has_row = 0;
  c_orm_error_t prep_rc;
  c_orm_error_t bind_rc;
  c_orm_error_t step_rc;
  c_orm_error_t fin_rc;
  const char *chk_sql =
      (db->driver_name && (strcmp(db->driver_name, "db2") == 0 ||
                           strcmp(db->driver_name, "odbc") == 0))
          ? "SELECT version FROM _c_orm_migrations WHERE version = ?"
          : "SELECT id FROM _c_orm_migrations WHERE version = ?";

  *out_applied = 0;
  prep_rc = db->vtable->prepare(db, chk_sql, &q);
  if (prep_rc != C_ORM_OK) {
    return (prep_rc == C_ORM_ERROR_MEMORY) ? prep_rc : C_ORM_OK;
  }
  if (db->vtable->bind_string) {
    bind_rc = db->vtable->bind_string(q, 1, version);
    if (bind_rc != C_ORM_OK) {
      fin_rc = db->vtable->finalize(q);
      if (fin_rc != C_ORM_OK) {
        return fin_rc;
      }
      return (bind_rc == C_ORM_ERROR_MEMORY) ? bind_rc : C_ORM_OK;
    }
  }
  step_rc = db->vtable->step(q, &has_row);
  if (step_rc != C_ORM_OK) {
    fin_rc = db->vtable->finalize(q);
    if (fin_rc != C_ORM_OK) {
      return fin_rc;
    }
    return (step_rc == C_ORM_ERROR_MEMORY) ? step_rc : C_ORM_OK;
  }
  fin_rc = db->vtable->finalize(q);
  if (fin_rc != C_ORM_OK) {
    return fin_rc;
  }
  if (has_row) {
    *out_applied = 1;
  }
  return C_ORM_OK;
}

/**
 * @brief Rolls back and releases a migration step savepoint during error
 * recovery.
 * @param db Database handle.
 * @param rb_sql Rollback-to-savepoint SQL command.
 * @param rel_sql Release-savepoint SQL command.
 * @return C_ORM_OK on success, or error code if savepoint cleanup failed.
 */
static c_orm_error_t mig_savepoint_destroy(c_orm_db_t *db, const char *rb_sql,
                                           const char *rel_sql) {
  c_orm_error_t rb_rc = c_orm_execute_raw(db, rb_sql);
  if (rb_rc != C_ORM_OK) {
    c_orm_error_t rel_rc = c_orm_execute_raw(db, rel_sql);
    if (rel_rc != C_ORM_OK) {
      return rel_rc;
    }
    return rb_rc;
  }
  return c_orm_execute_raw(db, rel_sql);
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
    return C_ORM_ERROR_VALIDATION;
  }
  if (!migrations) {
    LOG_DEBUG("c_orm_migrate_all: validation error");
    return C_ORM_ERROR_VALIDATION;
  }

  rc = c_orm_migration_lock(db);
  if (rc != C_ORM_OK) {
    return rc;
  }

  rc = c_orm_migration_init_table(db);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_migrate_all: init table error");
    goto migrate_all_err;
  }

  for (i = 0; i < count; i++) {
    const c_orm_migration_t *mig = &migrations[i];
    int already_applied = 0;

    rc = mig_is_version_applied(db, mig->version, &already_applied);
    if (rc != C_ORM_OK) {
      goto migrate_all_err;
    }
    if (already_applied) {
      continue;
    }

    if (options && options->log_cb) {
      char log_msg[1024];
      rc = C_ORM_SPRINTF(log_msg, sizeof(log_msg), "Applying migration %s: %s",
                         mig->version, mig->name);
      if (rc != C_ORM_OK) {
        goto migrate_all_err;
      }
      rc = options->log_cb(log_msg);
      if (rc != C_ORM_OK) {
        goto migrate_all_err;
      }
    }

    if (options && options->dry_run) {
      continue;
    }

    if (options && options->pre_migrate) {
      rc = options->pre_migrate(db, mig, options->user_data);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_migrate_all: pre_migrate error");
        goto migrate_all_err;
      }
    }

    rc = c_orm_execute_raw(
        db, (db->driver_name && strcmp(db->driver_name, "db2") == 0)
                ? "SAVEPOINT c_orm_mig_step ON ROLLBACK RETAIN CURSORS"
                : "SAVEPOINT c_orm_mig_step");
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_migrate_all: savepoint error");
      goto migrate_all_err;
    }

    {
      const char *up_p = mig->up_sql;
      while (up_p && (unsigned char)*up_p > 0U && (unsigned char)*up_p <= 32U) {
        up_p++;
      }
      if (up_p && *up_p != '\0') {
        rc = c_orm_execute_raw(db, up_p);
        if (rc != C_ORM_OK) {
          LOG_DEBUG("c_orm_migrate_all: up_sql error");
          mig_savepoint_destroy(db, "ROLLBACK TO SAVEPOINT c_orm_mig_step",
                                "RELEASE SAVEPOINT c_orm_mig_step");
          goto migrate_all_err;
        }
      }
    }

    rc = insert_migration_record(db, mig);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_migrate_all: insert migration error");
      mig_savepoint_destroy(db, "ROLLBACK TO SAVEPOINT c_orm_mig_step",
                            "RELEASE SAVEPOINT c_orm_mig_step");
      goto migrate_all_err;
    }

    rc = c_orm_execute_raw(db, "RELEASE SAVEPOINT c_orm_mig_step");
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_migrate_all: release savepoint error");
      goto migrate_all_err;
    }

    if (options && options->post_migrate) {
      rc = options->post_migrate(db, mig, options->user_data);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_migrate_all: post_migrate error");
        goto migrate_all_err;
      }
    }
  }

  unlock_rc = c_orm_migration_unlock(db);
  if (unlock_rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_migrate_all: final unlock failed");
    return unlock_rc;
  }
  LOG_DEBUG("c_orm_migrate_all: exiting");
  return C_ORM_OK;

migrate_all_err:;
  unlock_rc = c_orm_migration_unlock(db);
  if (unlock_rc != C_ORM_OK) {
    return unlock_rc;
  }
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
    return C_ORM_ERROR_VALIDATION;
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
      goto rollback_done;
    }

    if (options && options->log_cb) {
      char log_msg[1024];
      rc = C_ORM_SPRINTF(log_msg, sizeof(log_msg),
                         "Rolling back migration %s: %s", mig->version,
                         mig->name);
      if (rc != C_ORM_OK) {
        goto rollback_done;
      }
      rc = options->log_cb(log_msg);
      if (rc != C_ORM_OK) {
        goto rollback_done;
      }
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
      goto rollback_done;
    }

    {
      const char *dn_p = mig->down_sql;
      while (dn_p && (unsigned char)*dn_p > 0U && (unsigned char)*dn_p <= 32U) {
        dn_p++;
      }
      if (dn_p && *dn_p != '\0') {
        rc = c_orm_execute_raw(db, dn_p);
        if (rc != C_ORM_OK) {
          LOG_DEBUG("c_orm_migrate_rollback: down_sql error");
          mig_savepoint_destroy(db, "ROLLBACK TO SAVEPOINT c_orm_mig_step_rb",
                                "RELEASE SAVEPOINT c_orm_mig_step_rb");
          goto rollback_done;
        }
      }
    }

    rc = delete_migration_record(db, mig->version);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_migrate_rollback: delete migration error");
      mig_savepoint_destroy(db, "ROLLBACK TO SAVEPOINT c_orm_mig_step_rb",
                            "RELEASE SAVEPOINT c_orm_mig_step_rb");
      goto rollback_done;
    }

    rc = c_orm_execute_raw(db, "RELEASE SAVEPOINT c_orm_mig_step_rb");
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_migrate_rollback: release savepoint error");
      goto rollback_done;
    }
    rolled_back++;
  }

rollback_done:
  if (applied) {
    c_orm_migration_free_array(applied, applied_count);
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
 * @brief Allocates and populates a property name/type pair inside a schema
 * array.
 * @param prop Target property metadata slot.
 * @param col_name Column name string.
 * @param col_type Column SQL type string.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on OOM.
 */
static c_orm_error_t populate_schema_prop(cdd_c_prop_meta_t *prop,
                                          const char *col_name,
                                          const char *col_type) {
  char *pname = NULL;
  char *ptype = NULL;
  c_orm_error_t rc;

  memset(prop, 0, sizeof(cdd_c_prop_meta_t));
  rc = c_orm_strdup(col_name, &pname);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = c_orm_strdup(col_type, &ptype);
  if (rc != C_ORM_OK) {
    c_orm_system_free(pname);
    return rc;
  }
  prop->name = pname;
  prop->type = ptype;
  prop->offset = 0;
  return C_ORM_OK;
}

/**
 * @brief Allocates a `cdd_c_meta_t` header and initial property array.
 * @param table_name Table name string.
 * @param cap Initial property capacity.
 * @param out_meta Output allocated metadata pointer.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on OOM.
 */
static c_orm_error_t alloc_schema_meta(const char *table_name, size_t cap,
                                       cdd_c_meta_t **out_meta) {
  cdd_c_meta_t *meta = NULL;
  char *meta_name = NULL;
  cdd_c_prop_meta_t *props_arr = NULL;
  c_orm_error_t rc;

  *out_meta = NULL;
  rc = c_orm_system_malloc(sizeof(cdd_c_meta_t), (void **)&meta);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = c_orm_strdup(table_name, &meta_name);
  if (rc != C_ORM_OK) {
    c_orm_system_free(meta);
    return rc;
  }
  rc =
      c_orm_system_malloc(sizeof(cdd_c_prop_meta_t) * cap, (void **)&props_arr);
  if (rc != C_ORM_OK) {
    c_orm_system_free(meta_name);
    c_orm_system_free(meta);
    return rc;
  }
  meta->name = meta_name;
  meta->size = 0;
  meta->num_props = 0;
  meta->driver_ctx = NULL;
  meta->props = props_arr;
  *out_meta = meta;
  return C_ORM_OK;
}

/**
 * @brief Attempts to prepare, bind `table_name`, and step a Db2 catalog query.
 * @param db Database handle.
 * @param sql Catalog SQL query.
 * @param table_name Table name parameter.
 * @param out_q Output prepared query handle when `*out_ok` is 1.
 * @param out_has_row Output initial step flag.
 * @param out_ok Output set to 1 if catalog query succeeded, 0 if unsupported.
 * @return C_ORM_OK on success, or finalize error code.
 */
static c_orm_error_t mig_open_db2_catalog_query(c_orm_db_t *db, const char *sql,
                                                const char *table_name,
                                                c_orm_query_t **out_q,
                                                int *out_has_row, int *out_ok) {
  c_orm_query_t *q = NULL;
  c_orm_error_t prep_rc;
  c_orm_error_t bind_rc;
  c_orm_error_t step_rc;
  c_orm_error_t fin_rc;

  *out_q = NULL;
  *out_has_row = 0;
  *out_ok = 0;

  prep_rc = db->vtable->prepare(db, sql, &q);
  if (prep_rc != C_ORM_OK) {
    return (prep_rc == C_ORM_ERROR_MEMORY) ? prep_rc : C_ORM_OK;
  }
  bind_rc = db->vtable->bind_string(q, 1, table_name);
  if (bind_rc != C_ORM_OK) {
    fin_rc = db->vtable->finalize(q);
    if (fin_rc != C_ORM_OK) {
      return fin_rc;
    }
    return (bind_rc == C_ORM_ERROR_MEMORY) ? bind_rc : C_ORM_OK;
  }
  step_rc = db->vtable->step(q, out_has_row);
  if (step_rc != C_ORM_OK) {
    fin_rc = db->vtable->finalize(q);
    if (fin_rc != C_ORM_OK) {
      return fin_rc;
    }
    return (step_rc == C_ORM_ERROR_MEMORY) ? step_rc : C_ORM_OK;
  }
  *out_q = q;
  *out_ok = 1;
  return C_ORM_OK;
}

/**
 * @brief Releases a `cdd_c_meta_t` table schema during cleanup.
 * @param schema Schema pointer to free.
 * @return C_ORM_OK.
 */
static c_orm_error_t mig_table_schema_free(cdd_c_meta_t *schema) {
  return c_orm_migration_free_table_schema(schema);
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

  LOG_DEBUG("c_orm_migration_fetch_table_schema: entered");

  if (!db || !table_name || !out_schema) {
    LOG_DEBUG("c_orm_migration_fetch_table_schema: validation error");
    return C_ORM_ERROR_MEMORY;
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
      rc = mig_open_db2_catalog_query(db, luw_sql, table_name, &q, &has_row,
                                      &used_catalog_sql);
      if (rc != C_ORM_OK) {
        return rc;
      }
      if (!used_catalog_sql) {
        rc = mig_open_db2_catalog_query(db, zos_sql, table_name, &q, &has_row,
                                        &used_catalog_sql);
        if (rc != C_ORM_OK) {
          return rc;
        }
      }
    }

    if (used_catalog_sql) {
      c_orm_error_t fin_rc;
      rc = alloc_schema_meta(table_name, cap, &meta);
      if (rc != C_ORM_OK) {
        fin_rc = db->vtable->finalize(q);
        if (fin_rc != C_ORM_OK) {
          return fin_rc;
        }
        return rc;
      }

      while (has_row) {
        const char *col_name = NULL;
        const char *col_type = NULL;

        rc = db->vtable->get_string(q, 0, &col_name);
        if (rc != C_ORM_OK) {
          if (rc == C_ORM_ERROR_MEMORY) {
            goto db2_cat_err;
          }
          col_name = NULL;
        }
        rc = db->vtable->get_string(q, 1, &col_type);
        if (rc != C_ORM_OK) {
          if (rc == C_ORM_ERROR_MEMORY) {
            goto db2_cat_err;
          }
          col_type = NULL;
        }
        if (col_name && col_type) {
          if (meta->num_props >= cap) {
            cdd_c_prop_meta_t *new_props = NULL;
            cap *= 2;
            rc = c_orm_system_realloc((void *)(size_t)meta->props,
                                      sizeof(cdd_c_prop_meta_t) * cap,
                                      (void **)&new_props);
            if (rc != C_ORM_OK) {
              goto db2_cat_err;
            }
            meta->props = new_props;
          }
          rc = populate_schema_prop(
              (cdd_c_prop_meta_t *)(size_t)&meta->props[meta->num_props],
              col_name, col_type);
          if (rc != C_ORM_OK) {
            goto db2_cat_err;
          }
          meta->num_props++;
        }
        rc = db->vtable->step(q, &has_row);
        if (rc != C_ORM_OK) {
          goto db2_cat_err;
        }
      }
      rc = db->vtable->finalize(q);
      if (rc != C_ORM_OK) {
        mig_table_schema_free(meta);
        return rc;
      }
      *out_schema = meta;
      return C_ORM_OK;

    db2_cat_err:;
      mig_table_schema_free(meta);
      fin_rc = db->vtable->finalize(q);
      if (fin_rc != C_ORM_OK) {
        return fin_rc;
      }
      return rc;
    }

    rc = c_orm_odbc_list_columns(db, NULL, table_name, &odbc_cols, &odbc_count);
    if (rc != C_ORM_OK) {
      return rc;
    }
    cap = odbc_count > 0 ? odbc_count : 4;
    rc = alloc_schema_meta(table_name, cap, &meta);
    if (rc != C_ORM_OK) {
      c_orm_odbc_free_column_list(odbc_cols, odbc_count);
      return rc;
    }

    for (idx = 0; idx < odbc_count; ++idx) {
      rc = populate_schema_prop(
          (cdd_c_prop_meta_t *)(size_t)&meta->props[meta->num_props],
          odbc_cols[idx].name, odbc_cols[idx].type_name);
      if (rc != C_ORM_OK) {
        c_orm_migration_free_table_schema(meta);
        c_orm_odbc_free_column_list(odbc_cols, odbc_count);
        return rc;
      }
      meta->num_props++;
    }
    c_orm_odbc_free_column_list(odbc_cols, odbc_count);
    *out_schema = meta;
    return C_ORM_OK;
  }

  {
    int col_name_idx = 1;
    int col_type_idx = 2;
    int bind_tbl_param = 0;

    if (db->driver_name && strcmp(db->driver_name, "postgres") == 0) {
      rc = C_ORM_STRCPY(sql, sizeof(sql),
                        "SELECT column_name, data_type FROM "
                        "information_schema.columns WHERE table_name = $1 "
                        "ORDER BY ordinal_position ASC");
      if (rc != C_ORM_OK) {
        return rc;
      }
      col_name_idx = 0;
      col_type_idx = 1;
      bind_tbl_param = 1;
    } else if (db->driver_name && (strcmp(db->driver_name, "mysql") == 0 ||
                                   strcmp(db->driver_name, "mariadb") == 0)) {
      rc = C_ORM_STRCPY(sql, sizeof(sql),
                        "SELECT COLUMN_NAME, DATA_TYPE FROM "
                        "INFORMATION_SCHEMA.COLUMNS WHERE TABLE_NAME = ? "
                        "ORDER BY ORDINAL_POSITION ASC");
      if (rc != C_ORM_OK) {
        return rc;
      }
      col_name_idx = 0;
      col_type_idx = 1;
      bind_tbl_param = 1;
    } else {
      char escaped_tbl[220];
      size_t ti = 0;
      size_t ei = 0;
      while (table_name[ti] != '\0') {
        if (table_name[ti] == '\'') {
          if (ei + 2 >= sizeof(escaped_tbl)) {
            return C_ORM_ERROR_OVERFLOW;
          }
          escaped_tbl[ei++] = '\'';
          escaped_tbl[ei++] = '\'';
        } else {
          if (ei + 1 >= sizeof(escaped_tbl)) {
            return C_ORM_ERROR_OVERFLOW;
          }
          escaped_tbl[ei++] = table_name[ti];
        }
        ti++;
      }
      escaped_tbl[ei] = '\0';
      rc = C_ORM_SPRINTF(sql, sizeof(sql), "PRAGMA table_info('%s')",
                         escaped_tbl);
      if (rc != C_ORM_OK) {
        return rc;
      }
    }

    rc = c_orm_prepare_cached(db, sql, &q);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_migration_fetch_table_schema: prepare error");
      return rc;
    }

    if (bind_tbl_param) {
      rc = db->vtable->bind_string(q, 1, table_name);
      if (rc != C_ORM_OK) {
        c_orm_error_t fin_rc = c_orm_finalize_cached(db, q);
        if (fin_rc != C_ORM_OK) {
          return fin_rc;
        }
        return rc;
      }
    }

    rc = alloc_schema_meta(table_name, cap, &meta);
    if (rc != C_ORM_OK) {
      c_orm_error_t fin_rc = c_orm_finalize_cached(db, q);
      if (fin_rc != C_ORM_OK) {
        return fin_rc;
      }
      return rc;
    }

    for (;;) {
      const char *col_name = NULL;
      const char *col_type = NULL;

      rc = db->vtable->step(q, &has_row);
      if (rc != C_ORM_OK) {
        goto fail_schema;
      }
      if (!has_row) {
        break;
      }

      rc = db->vtable->get_string(q, col_name_idx, &col_name);
      if (rc != C_ORM_OK) {
        goto fail_schema;
      }
      rc = db->vtable->get_string(q, col_type_idx, &col_type);
      if (rc != C_ORM_OK) {
        goto fail_schema;
      }

      if (!col_name || !col_type) {
        continue;
      }

      if (meta->num_props >= cap) {
        cdd_c_prop_meta_t *new_props = NULL;
        cap *= 2;
        rc = c_orm_system_realloc((void *)(size_t)meta->props,
                                  sizeof(cdd_c_prop_meta_t) * cap,
                                  (void **)&new_props);
        if (rc != C_ORM_OK) {
          LOG_DEBUG("c_orm_migration_fetch_table_schema: OOM during realloc");
          goto fail_schema;
        }
        meta->props = new_props;
      }

      rc = populate_schema_prop(
          (cdd_c_prop_meta_t *)(size_t)&meta->props[meta->num_props], col_name,
          col_type);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_migration_fetch_table_schema: OOM");
        goto fail_schema;
      }
      meta->num_props++;
    }
  }

  {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, q);
    if (fin_rc != C_ORM_OK) {
      mig_table_schema_free(meta);
      return fin_rc;
    }
  }

  *out_schema = meta;
  LOG_DEBUG("c_orm_migration_fetch_table_schema: exiting");
  return C_ORM_OK;

fail_schema: {
  c_orm_error_t fin_rc;
  mig_table_schema_free(meta);
  fin_rc = c_orm_finalize_cached(db, q);
  if (fin_rc != C_ORM_OK) {
    return fin_rc;
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
  c_orm_error_t fin_rc;
  c_orm_query_t *q = NULL;
  int has_row;
  size_t count = 0;
  size_t cap = 10;
  c_orm_migration_t *migs = NULL;
  const char *app_sql;

  LOG_DEBUG("c_orm_migration_get_applied: entered");

  if (!db || !out_migrations || !out_count) {
    LOG_DEBUG("c_orm_migration_get_applied: validation error");
    return C_ORM_ERROR_VALIDATION;
  }

  app_sql = (db->driver_name && (strcmp(db->driver_name, "db2") == 0 ||
                                 strcmp(db->driver_name, "odbc") == 0))
                ? "SELECT version, name, hash FROM _c_orm_migrations ORDER BY "
                  "version ASC"
                : "SELECT version, name, hash FROM _c_orm_migrations ORDER BY "
                  "id ASC";
  rc = c_orm_prepare_cached(db, app_sql, &q);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_migration_get_applied: prepare error");
    return rc;
  }

  rc = c_orm_system_malloc(cap * sizeof(c_orm_migration_t), (void **)&migs);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_migration_get_applied: OOM");
    fin_rc = c_orm_finalize_cached(db, q);
    if (fin_rc != C_ORM_OK) {
      return fin_rc;
    }
    return rc;
  }

  for (;;) {
    const char *version = NULL;
    const char *name = NULL;
    const char *hash = NULL;

    rc = db->vtable->step(q, &has_row);
    if (rc != C_ORM_OK) {
      goto applied_err;
    }
    if (!has_row) {
      break;
    }

    rc = db->vtable->get_string(q, 0, &version);
    if (rc != C_ORM_OK) {
      goto applied_err;
    }
    rc = db->vtable->get_string(q, 1, &name);
    if (rc != C_ORM_OK) {
      goto applied_err;
    }
    rc = db->vtable->get_string(q, 2, &hash);
    if (rc != C_ORM_OK) {
      goto applied_err;
    }

    if (count >= cap) {
      c_orm_migration_t *new_migs = NULL;
      cap *= 2;
      rc = c_orm_system_realloc(migs, cap * sizeof(c_orm_migration_t),
                                (void **)&new_migs);
      if (rc != C_ORM_OK) {
        LOG_DEBUG("c_orm_migration_get_applied: OOM during realloc");
        goto applied_err;
      }
      migs = new_migs;
    }

    memset(&migs[count], 0, sizeof(c_orm_migration_t));
    if (version) {
      rc = C_ORM_STRNCPY(migs[count].version, sizeof(migs[count].version),
                         version, strlen(version));
      if (rc != C_ORM_OK) {
        goto applied_err;
      }
    }
    if (name) {
      rc = C_ORM_STRNCPY(migs[count].name, sizeof(migs[count].name), name,
                         strlen(name));
      if (rc != C_ORM_OK) {
        goto applied_err;
      }
    }
    if (hash) {
      rc = C_ORM_STRNCPY(migs[count].hash, sizeof(migs[count].hash), hash,
                         strlen(hash));
      if (rc != C_ORM_OK) {
        goto applied_err;
      }
    }
    count++;
  }

  fin_rc = c_orm_finalize_cached(db, q);
  if (fin_rc != C_ORM_OK) {
    c_orm_migration_free_array(migs, count);
    return fin_rc;
  }

  if (count == 0) {
    c_orm_system_free(migs);
    migs = NULL;
  }

  *out_migrations = migs;
  *out_count = count;

  LOG_DEBUG("c_orm_migration_get_applied: exiting");
  return C_ORM_OK;

applied_err:;
  c_orm_migration_free_array(migs, count);
  fin_rc = c_orm_finalize_cached(db, q);
  if (fin_rc != C_ORM_OK) {
    return fin_rc;
  }
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
    return C_ORM_ERROR_VALIDATION;
  }

  rc = c_orm_migration_load_dir(dir_path, &migs, &count);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_migrate_up: load dir error");
    return rc;
  }

  rc = c_orm_migrate_all(db, migs, count, options);
  if (rc != C_ORM_OK) {
    c_orm_migration_free_array(migs, count);
    return rc;
  }

  LOG_DEBUG("c_orm_migrate_up: exiting");
  return c_orm_migration_free_array(migs, count);
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
    return C_ORM_ERROR_VALIDATION;
  }

  rc = c_orm_migration_load_dir(dir_path, &migs, &count);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_migrate_down: load dir error");
    return rc;
  }

  rc = c_orm_migrate_rollback(db, migs, count, steps, options);
  if (rc != C_ORM_OK) {
    c_orm_migration_free_array(migs, count);
    return rc;
  }

  LOG_DEBUG("c_orm_migrate_down: exiting");
  return c_orm_migration_free_array(migs, count);
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
      if (rb_rc != C_ORM_OK) {
        return rb_rc;
      }
      return rc;
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
