/**
 * @file c_orm.h
 * @brief Abstract Object Relational Mapper for SQLite, PostgreSQL, and MySQL in
 * C. Provides Alembic-style migrations, CRUD, and table creation.
 */

#ifndef C_ORM_H
#define C_ORM_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include <stddef.h>
/* clang-format on */

/**
 * @brief Platform-specific format specifier for numbers.
 */
#if defined(_MSC_VER)
#define NUM_FORMAT "%I64d"
typedef __int64 c_orm_int_t;
#else
#define NUM_FORMAT "%ld"
typedef long c_orm_int_t;
#endif

/**
 * @brief Supported database dialects.
 */
typedef enum {
  C_ORM_DIALECT_UNKNOWN = 0, /**< Unknown or uninitialized dialect. */
  C_ORM_DIALECT_SQLITE,      /**< SQLite backend. */
  C_ORM_DIALECT_POSTGRES,    /**< PostgreSQL backend. */
  C_ORM_DIALECT_MYSQL        /**< MySQL / MariaDB backend. */
} c_orm_dialect_t;

/**
 * @brief Opaque database connection handle.
 */
typedef struct c_orm_db c_orm_db_t;

/**
 * @brief Opaque connection pool handle.
 */
typedef struct c_orm_pool c_orm_pool_t;

/**
 * @brief Opaque fluent query builder handle.
 */
typedef struct c_orm_query c_orm_query_t;

/**
 * @brief Represents the data type of a parameter to bind.
 */
typedef enum {
  C_ORM_PARAM_INTEGER, /**< Integer type. */
  C_ORM_PARAM_REAL,    /**< Floating point type. */
  C_ORM_PARAM_TEXT,    /**< Text / string type. */
  C_ORM_PARAM_BLOB,    /**< Binary large object type. */
  C_ORM_PARAM_NULL     /**< NULL value. */
} c_orm_param_type_t;

/**
 * @brief Represents a bound parameter.
 */
typedef struct {
  c_orm_param_type_t type; /**< Type of the parameter */
  union {
    c_orm_int_t int_val;  /**< Integer value */
    double real_val;      /**< Real/Float value */
    const char *text_val; /**< Null-terminated text value */
    struct {
      const void *data; /**< Blob data pointer */
      size_t size;      /**< Blob size in bytes */
    } blob_val;         /**< Blob data wrapper */
  } value;              /**< The parameter value */
} c_orm_param_t;

/**
 * @brief Callback function type for logging query execution.
 * @param query The executed SQL query string.
 * @param duration_ms The time taken to execute the query in milliseconds.
 * @param user_data Opaque pointer to user-defined data.
 */
typedef void (*c_orm_log_cb_t)(const char *query, double duration_ms,
                               void *user_data);

/**
 * @brief Callback function type for asynchronous query completion.
 * @param status 0 on success, non-zero on error.
 * @param user_data Opaque pointer to user-defined data.
 */
typedef void (*c_orm_async_cb_t)(int status, void *user_data);

/**
 * @brief Initialize the ORM connection.
 * @param db_out Pointer to receive the database connection handle.
 * @param dialect The database dialect.
 * @param conn_string Connection string (e.g., "user=postgres dbname=postgres"
 * or "file.db").
 * @return 0 on success, non-zero on failure (e.g., -4 if dialect is not
 * compiled in).
 */
int c_orm_connect(c_orm_db_t **db_out, c_orm_dialect_t dialect,
                  const char *conn_string);

/**
 * @brief Disconnect and free resources.
 * @param db Database connection handle.
 */
void c_orm_disconnect(c_orm_db_t *db);

/**
 * @brief Safely lock the database connection for exclusive thread access.
 * @param db Database connection handle.
 * @return 0 on success, non-zero on error.
 */
int c_orm_lock(c_orm_db_t *db);

/**
 * @brief Unlock the database connection.
 * @param db Database connection handle.
 * @return 0 on success, non-zero on error.
 */
int c_orm_unlock(c_orm_db_t *db);

/**
 * @brief Apply schema migrations up to the latest version.
 * @param db Database connection handle.
 * @param migrations_dir Path to the migrations directory.
 * @return 0 on success, non-zero on error.
 */
int c_orm_migrate(c_orm_db_t *db, const char *migrations_dir);

/**
 * @brief Rollback the last applied schema migration.
 * @param db Database connection handle.
 * @param migrations_dir Path to the migrations directory.
 * @return 0 on success, non-zero on error.
 */
int c_orm_migrate_rollback(c_orm_db_t *db, const char *migrations_dir);

/**
 * @brief Get the current migration version applied to the database.
 * @param db Database connection handle.
 * @param current_version Pointer to an integer to receive the version.
 * @return 0 on success, non-zero on error.
 */
int c_orm_migrate_current_version(c_orm_db_t *db, int *current_version);

/**
 * @brief Set the logging callback for a database connection.
 * @param db Database connection handle.
 * @param logger The logging callback function, or NULL to disable logging.
 * @param user_data Opaque pointer to user-defined data passed to the callback.
 * @return 0 on success, non-zero on error.
 */
int c_orm_set_logger(c_orm_db_t *db, c_orm_log_cb_t logger, void *user_data);

/**
 * @brief Execute a raw query.
 * @param db Database connection handle.
 * @param query SQL query string.
 * @return 0 on success, non-zero on error.
 */
int c_orm_execute(c_orm_db_t *db, const char *query);

/**
 * @brief Execute a raw query asynchronously.
 * Note: Under the hood, this relies on non-blocking native APIs or a thread
 * pool.
 * @param db Database connection handle.
 * @param query SQL query string.
 * @param cb The callback invoked upon completion.
 * @param user_data Opaque pointer passed to the callback.
 * @return 0 on successful queuing, non-zero on error.
 */
int c_orm_execute_async(c_orm_db_t *db, const char *query, c_orm_async_cb_t cb,
                        void *user_data);

/**
 * @brief Process pending asynchronous queries.
 * Must be called in an event loop if the underlying driver requires polling.
 * @param db Database connection handle.
 * @param jobs_processed Pointer to receive the number of jobs processed.
 * @return 0 on success, non-zero on error.
 */
int c_orm_poll_async(c_orm_db_t *db, int *jobs_processed);

/**
 * @brief Execute a raw query with parameterized values.
 * @param db Database connection handle.
 * @param query SQL query string with ? placeholders.
 * @param params Array of parameters to bind.
 * @param param_count Number of parameters in the array.
 * @return 0 on success, non-zero on error.
 */
int c_orm_execute_params(c_orm_db_t *db, const char *query,
                         const c_orm_param_t *params, size_t param_count);

/**
 * @brief Begin a new database transaction.
 * @param db Database connection handle.
 * @return 0 on success, non-zero on error.
 */
int c_orm_transaction_begin(c_orm_db_t *db);

/**
 * @brief Commit the current database transaction.
 * @param db Database connection handle.
 * @return 0 on success, non-zero on error.
 */
int c_orm_transaction_commit(c_orm_db_t *db);

/**
 * @brief Rollback the current database transaction.
 * @param db Database connection handle.
 * @return 0 on success, non-zero on error.
 */
int c_orm_transaction_rollback(c_orm_db_t *db);

/**
 * @brief Create a connection pool.
 * @param pool_out Pointer to receive the connection pool handle.
 * @param dialect The database dialect.
 * @param conn_string Connection string.
 * @param pool_size Maximum number of connections in the pool.
 * @return 0 on success, non-zero on error.
 */
int c_orm_pool_create(c_orm_pool_t **pool_out, c_orm_dialect_t dialect,
                      const char *conn_string, size_t pool_size);

/**
 * @brief Destroy a connection pool, freeing all connections.
 * @param pool The connection pool handle.
 * @return 0 on success, non-zero on error.
 */
int c_orm_pool_destroy(c_orm_pool_t *pool);

/**
 * @brief Acquire a database connection from the pool. Thread safe.
 * @param pool The connection pool handle.
 * @param db_out Pointer to receive the acquired database connection handle.
 * @return 0 on success, non-zero on error (e.g. pool exhausted).
 */
int c_orm_pool_acquire(c_orm_pool_t *pool, c_orm_db_t **db_out);

/**
 * @brief Release a database connection back to the pool. Thread safe.
 * @param pool The connection pool handle.
 * @param db The database connection handle to release.
 * @return 0 on success, non-zero on error.
 */
int c_orm_pool_release(c_orm_pool_t *pool, c_orm_db_t *db);

/**
 * @brief Create a fluent query builder instance.
 * @param query_out Pointer to receive the query builder handle.
 * @param db The database connection handle to execute the query against.
 * @param table_name The primary table to select from, insert into, or update.
 * @return 0 on success, non-zero on error.
 */
int c_orm_query_create(c_orm_query_t **query_out, c_orm_db_t *db,
                       const char *table_name);

/**
 * @brief Specify columns to select.
 * @param query The query builder handle.
 * @param columns Comma-separated list of columns.
 * @return 0 on success, non-zero on error.
 */
int c_orm_query_select(c_orm_query_t *query, const char *columns);

/**
 * @brief Add a WHERE clause to the query.
 * @param query The query builder handle.
 * @param condition The WHERE condition string (can include ? placeholders).
 * @return 0 on success, non-zero on error.
 */
int c_orm_query_where(c_orm_query_t *query, const char *condition);

/**
 * @brief Add an ORDER BY clause to the query.
 * @param query The query builder handle.
 * @param order_by The ORDER BY string.
 * @return 0 on success, non-zero on error.
 */
int c_orm_query_order_by(c_orm_query_t *query, const char *order_by);

/**
 * @brief Add a LIMIT clause to the query.
 * @param query The query builder handle.
 * @param limit The maximum number of rows to return.
 * @return 0 on success, non-zero on error.
 */
int c_orm_query_limit(c_orm_query_t *query, size_t limit);

/**
 * @brief Render the query to a SQL string (mostly for testing and debugging).
 * @param query The query builder handle.
 * @param sql_out Pointer to receive the dynamically allocated SQL string.
 * Caller must free().
 * @return 0 on success, non-zero on error.
 */
int c_orm_query_build(c_orm_query_t *query, char **sql_out);

/**
 * @brief Execute the constructed query via the associated database connection.
 * @param query The query builder handle.
 * @return 0 on success, non-zero on error.
 */
int c_orm_query_execute(c_orm_query_t *query);

/**
 * @brief Destroy a query builder instance and free its resources.
 * @param query The query builder handle.
 * @return 0 on success, non-zero on error.
 */
int c_orm_query_destroy(c_orm_query_t *query);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* C_ORM_H */
