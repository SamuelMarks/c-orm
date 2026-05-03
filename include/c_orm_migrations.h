/**
 * @file c_orm_migrations.h
 * @brief Schema migration system for c-orm.
 */

#ifndef C_ORM_MIGRATIONS_H
#define C_ORM_MIGRATIONS_H

/* clang-format off */
#include "c_orm_api.h"
#if defined(_MSC_VER)
#if _MSC_VER < 1600
#else
#include <stdint.h>
#include <stddef.h>
#endif
#else
#include <stdint.h>
#include <stddef.h>
#endif
#include "cdd_c_orm_meta.h"
/* clang-format on */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Representation of a single migration.
 */
typedef struct {
  char version[256];
  char name[256];
  char hash[65];
  char *up_sql;
  char *down_sql;
} c_orm_migration_t;

/**
 * @brief Context/options for migration execution.
 */
typedef struct {
  int dry_run;                     /**< If 1, do not execute, only print SQL */
  void (*log_cb)(const char *msg); /**< Optional logger for migration steps */
  void *user_data;                 /**< Passed to hooks */
  c_orm_error_t (*pre_migrate)(c_orm_db_t *db, const c_orm_migration_t *mig,
                               void *user_data);
  c_orm_error_t (*post_migrate)(c_orm_db_t *db, const c_orm_migration_t *mig,
                                void *user_data);
} c_orm_migration_options_t;

/**
 * @brief Initialize the migrations table if it does not exist.
 *
 * @param db The database connection.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_migration_init_table(c_orm_db_t *db);

/**
 * @brief Fetch applied migrations.
 */
C_ORM_EXPORT c_orm_error_t c_orm_migration_get_applied(
    c_orm_db_t *db, c_orm_migration_t **out_migrations, size_t *out_count);

/**
 * @brief Load migrations from a directory into an array.
 *
 * @param dir_path The directory to scan.
 * @param out_migrations Pointer to an array of migrations (allocated by
 * function).
 * @param out_count Number of migrations loaded.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_migration_load_dir(const char *dir_path,
                         c_orm_migration_t **out_migrations, size_t *out_count);

/**
 * @brief Free an array of migrations returned by c_orm_migration_load_dir.
 *
 * @param migrations The array to free.
 * @param count The number of migrations.
 */
C_ORM_EXPORT void c_orm_migration_free_array(c_orm_migration_t *migrations,
                                             size_t count);

/**
 * @brief Apply all pending migrations.
 *
 * @param db The database connection.
 * @param migrations Array of available migrations.
 * @param count Number of available migrations.
 * @param options Migration options (can be NULL).
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_migrate_all(c_orm_db_t *db, const c_orm_migration_t *migrations,
                  size_t count, const c_orm_migration_options_t *options);

/**
 * @brief Rollback the last N migrations.
 *
 * @param db The database connection.
 * @param migrations Array of available migrations.
 * @param count Number of available migrations.
 * @param steps Number of steps to rollback (0 to rollback all).
 * @param options Migration options (can be NULL).
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_migrate_rollback(
    c_orm_db_t *db, const c_orm_migration_t *migrations, size_t count,
    size_t steps, const c_orm_migration_options_t *options);

/**
 * @brief Migrate up using a directory of migrations.
 *
 * @param db The database connection.
 * @param dir_path The directory containing migration scripts.
 * @param options Migration options (can be NULL).
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_migrate_up(c_orm_db_t *db, const char *dir_path,
                 const c_orm_migration_options_t *options);

/**
 * @brief Migrate down by N steps using a directory of migrations.
 *
 * @param db The database connection.
 * @param dir_path The directory containing migration scripts.
 * @param steps Number of steps to rollback.
 * @param options Migration options (can be NULL).
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_migrate_down(c_orm_db_t *db, const char *dir_path, size_t steps,
                   const c_orm_migration_options_t *options);

/**
 * @brief Acquire a distributed lock for schema migration.
 *
 * @param db The database connection.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_migration_lock(c_orm_db_t *db);

/**
 * @brief Release the distributed lock for schema migration.
 *
 * @param db The database connection.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_migration_unlock(c_orm_db_t *db);

/**
 * @brief Fetch the current schema of a table directly from the database.
 *
 * @param db The database connection.
 * @param table_name The name of the table.
 * @param out_schema Pointer to a cdd_c_meta_t pointer. Memory should be freed
 * by caller.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_migration_fetch_table_schema(
    c_orm_db_t *db, const char *table_name, cdd_c_meta_t **out_schema);

/**
 * @brief Free a fetched table schema.
 */
C_ORM_EXPORT void c_orm_migration_free_table_schema(cdd_c_meta_t *schema);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* C_ORM_MIGRATIONS_H */
