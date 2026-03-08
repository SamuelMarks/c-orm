/**
 * @file c_orm.h
 * @brief Abstract Object Relational Mapper for SQLite and PostgreSQL in C.
 * Provides Alembic-style migrations, CRUD, and table creation.
 */

#ifndef C_ORM_H
#define C_ORM_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <stddef.h>

/**
 * @brief Supported database dialects.
 */
typedef enum {
    C_ORM_DIALECT_SQLITE,
    C_ORM_DIALECT_POSTGRES
} c_orm_dialect_t;

/**
 * @brief Opaque database connection handle.
 */
typedef struct c_orm_db c_orm_db_t;

/**
 * @brief Initialize the ORM connection.
 * @param dialect The database dialect.
 * @param conn_string Connection string (e.g., "user=postgres dbname=postgres" or "file.db").
 * @return Database connection handle or NULL on failure.
 */
c_orm_db_t* c_orm_connect(c_orm_dialect_t dialect, const char* conn_string);

/**
 * @brief Disconnect and free resources.
 * @param db Database connection handle.
 */
void c_orm_disconnect(c_orm_db_t* db);

/**
 * @brief Apply schema migrations (Alembic/Diesel style).
 * @param db Database connection handle.
 * @param migrations_dir Path to the migrations directory.
 * @return 0 on success, non-zero on error.
 */
int c_orm_migrate(c_orm_db_t* db, const char* migrations_dir);

/**
 * @brief Execute a raw query.
 * @param db Database connection handle.
 * @param query SQL query string.
 * @return 0 on success, non-zero on error.
 */
int c_orm_execute(c_orm_db_t* db, const char* query);

/* TODO: Provide generic CRUD bindings that `db_codegen` will generate calls for */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* C_ORM_H */
