#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file c_orm_db.h
 * @brief Core interfaces and vtables for c-orm drivers.
 */

#ifndef C_ORM_DB_H
#define C_ORM_DB_H

/* clang-format off */
#if defined(_MSC_VER)
#if _MSC_VER < 1600
typedef signed __int8 int8_t;
typedef signed __int16 int16_t;
typedef signed __int32 int32_t;
typedef signed __int64 int64_t;
typedef unsigned __int8 uint8_t;
typedef unsigned __int16 uint16_t;
typedef unsigned __int32 uint32_t;
typedef unsigned __int64 uint64_t;
#else
#include <stdint.h>
#endif
#else
#include <stdint.h>
#endif
#include "c_orm_meta.h"
/* clang-format on */

#if defined(_MSC_VER)
#define C_ORM_FMT_SIZE_T "%I64u"
#define C_ORM_CAST_SIZE_T(x) ((unsigned __int64)(x))
#else
#define C_ORM_FMT_SIZE_T "%lu"
#define C_ORM_CAST_SIZE_T(x) ((unsigned long)(x))
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
/**
 * @brief Opaque database connection handle.
 */
typedef struct c_orm_db c_orm_db_t;

/**
 * @brief Opaque query/prepared statement handle.
 */
#ifndef C_ORM_QUERY_T_DEFINED
#define C_ORM_QUERY_T_DEFINED
typedef struct c_orm_query c_orm_query_t;
#endif

/**
 * @brief Error codes returned by c-orm functions.
 */

/**
 * @brief Get the last error message from the database driver.
 *
 * @param db The database connection.
 * @param out_message Returns a string detailing the last error.
 * @return 0 on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_get_last_error_message(c_orm_db_t *db, const char **out_message);

/**
 * @brief Get context-aware stack trace for the last error (Step 265).
 *
 * @param db The database connection.
 * @param out_trace Returns a string detailing the contextual trace stack.
 * @return 0 on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_get_last_error_trace(c_orm_db_t *db,
                                                      const char **out_trace);

/**
 * @brief Query logging callback signature.
 */
typedef void (*c_orm_log_cb)(const char *sql, void *user_data);

/**
 * @brief Record expiration callback signature.
 */
typedef void (*c_orm_expire_cb)(c_orm_db_t *db, const c_orm_table_meta_t *meta,
                                void *record, void *user_data);

/**
 * @brief Virtual table for database driver implementations.
 * @var connect Connects to the database using a URL string.
 * @var disconnect Disconnects the database connection.
 * @var prepare Prepares a SQL statement.
 * @var bind_int32 Binds a 32-bit integer to a parameter index.
 * @var bind_int64 Binds a 64-bit integer to a parameter index.
 * @var bind_double Binds a double float to a parameter index.
 * @var bind_string Binds a null-terminated string to a parameter index.
 * @var bind_blob Binds binary data to a parameter index.
 * @var bind_null Binds NULL to a parameter index.
 * @var step Steps the prepared statement execution.
 * @var get_int32 Retrieves a 32-bit integer from a result column.
 * @var get_int64 Retrieves a 64-bit integer from a result column.
 * @var get_double Retrieves a double float from a result column.
 * @var get_string Retrieves a string from a result column.
 * @var get_blob Retrieves binary data from a result column.
 * @var is_null Checks if a result column is NULL.
 * @var finalize Destroys a prepared statement.
 * @var reset Resets a prepared statement for re-execution.
 * @var get_last_error Retrieves the driver's last error message.
 * @var get_last_trace Retrieves the driver's last error trace.
 * @var get_last_insert_rowid Retrieves the rowid of the last insert.
 * @var get_column_count Retrieves the number of columns in the result set.
 * @var get_column_name Retrieves the name of a result column by index.
 */
typedef struct c_orm_driver_vtable {
  /** @brief Connects to database. */
  c_orm_error_t (*connect)(const char *url, c_orm_db_t **out_db);
  /** @brief Disconnects connection. */
  c_orm_error_t (*disconnect)(c_orm_db_t *db);
  /** @brief Prepares SQL statement. */
  c_orm_error_t (*prepare)(c_orm_db_t *db, const char *sql,
                           c_orm_query_t **out_query);
  /** @brief Binds int32 parameter. */
  c_orm_error_t (*bind_int32)(c_orm_query_t *query, int index, int32_t val);
  /** @brief Binds int64 parameter. */
  c_orm_error_t (*bind_int64)(c_orm_query_t *query, int index, int64_t val);
  /** @brief Binds double parameter. */
  c_orm_error_t (*bind_double)(c_orm_query_t *query, int index, double val);
  /** @brief Binds string parameter. */
  c_orm_error_t (*bind_string)(c_orm_query_t *query, int index,
                               const char *val);
  /** @brief Binds blob parameter. */
  c_orm_error_t (*bind_blob)(c_orm_query_t *query, int index, const void *val,
                             size_t size);
  /** @brief Binds null parameter. */
  c_orm_error_t (*bind_null)(c_orm_query_t *query, int index);
  /** @brief Steps prepared query execution. */
  c_orm_error_t (*step)(c_orm_query_t *query, int *out_has_row);
  /** @brief Gets int32 column value. */
  c_orm_error_t (*get_int32)(c_orm_query_t *query, int index, int32_t *out_val);
  /** @brief Gets int64 column value. */
  c_orm_error_t (*get_int64)(c_orm_query_t *query, int index, int64_t *out_val);
  /** @brief Gets double column value. */
  c_orm_error_t (*get_double)(c_orm_query_t *query, int index, double *out_val);
  /** @brief Gets string column value. */
  c_orm_error_t (*get_string)(c_orm_query_t *query, int index,
                              const char **out_val);
  /** @brief Gets blob column value. */
  c_orm_error_t (*get_blob)(c_orm_query_t *query, int index,
                            const void **out_val, size_t *out_size);
  /** @brief Checks if column is null. */
  c_orm_error_t (*is_null)(c_orm_query_t *query, int index, int *out_is_null);
  /** @brief Finalizes prepared query. */
  c_orm_error_t (*finalize)(c_orm_query_t *query);
  /** @brief Resets prepared query. */
  c_orm_error_t (*reset)(c_orm_query_t *query);
  /** @brief Gets last driver error message. */
  c_orm_error_t (*get_last_error)(c_orm_db_t *db, const char **out_message);
  /** @brief Gets last driver error trace. */
  c_orm_error_t (*get_last_trace)(c_orm_db_t *db, const char **out_trace);
  /** @brief Gets last inserted row ID. */
  c_orm_error_t (*get_last_insert_rowid)(c_orm_db_t *db, int64_t *out_id);
  /** @brief Gets result column count. */
  c_orm_error_t (*get_column_count)(c_orm_query_t *query, int *out_count);
  /** @brief Gets result column name by index. */
  c_orm_error_t (*get_column_name)(c_orm_query_t *query, int index,
                                   const char **out_name);
} c_orm_driver_vtable_t;

/**
 * @brief Telemetry data for a connection pool.
 * @var active_connections Count of currently active connections.
 * @var idle_connections Count of currently idle connections.
 * @var exhaustion_count Count of times the pool was exhausted.
 * @var average_wait_time_ms Average wait time in milliseconds.
 * @var slow_queries_logged Count of slow queries logged.
 */
typedef struct c_orm_pool_telemetry {
  /** @brief Count of active connections. */
  size_t active_connections;
  /** @brief Count of idle connections. */
  size_t idle_connections;
  /** @brief Count of times pool exhausted. */
  size_t exhaustion_count;
  /** @brief Average wait time in milliseconds. */
  double average_wait_time_ms;
  /** @brief Count of slow queries logged. */
  size_t slow_queries_logged;
} c_orm_pool_telemetry_t;

/**
 * @brief Interceptor hooks for plugin architecture.
 */
typedef void (*c_orm_interceptor_cb)(c_orm_db_t *db, const char *sql,
                                     void *context);

/**
 * @brief Crypto hooks for transparent encryption/decryption of
 * C_ORM_SECURE_FIELD (Step 248).
 *
 * @param data The raw data to encrypt/decrypt.
 * @param size The size of the data.
 * @param context Opaque user context.
 * @param out_data Returns pointer to processed buffer.
 * @param out_size Returns size of processed buffer.
 */
typedef c_orm_error_t (*c_orm_crypto_hook_t)(const void *data, size_t size,
                                             void *context, void **out_data,
                                             size_t *out_size);

/**
 * @brief Represents a timezone offset from UTC.
 */
typedef struct c_orm_timezone {
  int offset_minutes; /**< Offset from UTC in minutes */
  const char
      *name; /**< Optional IANA timezone name (e.g. "America/New_York") */
} c_orm_timezone_t;

/**
 * @brief Execution Modalities for query processing.
 */
typedef enum {
  C_ORM_MODALITY_SYNC = 0,    /**< Synchronous blocking execution */
  C_ORM_MODALITY_ASYNC,       /**< Asynchronous non-blocking execution */
  C_ORM_MODALITY_THREAD_POOL, /**< Thread pool execution */
  C_ORM_MODALITY_GREENTHREAD, /**< Cooperative user-space thread execution */
  C_ORM_MODALITY_MESSAGE_PASSING, /**< Actor-model message passing execution */
  C_ORM_MODALITY_MULTIPROCESS     /**< Multi-process preforked execution */
} c_orm_modality_t;

/**
 * @brief Structure holding the generic DB context.
 * @var vtable Driver virtual method table.
 * @var driver_data Driver-specific private data pointer.
 * @var driver_name Name identifier of the driver.
 * @var log_cb Logging callback function.
 * @var log_user_data User context pointer for log callback.
 * @var expire_cb Record expiration callback.
 * @var expire_user_data User context pointer for expire callback.
 * @var identity_map Associated identity map for caching objects.
 * @var hydrate_router Associated cdd-c hydrate router.
 * @var query_interceptor Query execution hook.
 * @var query_interceptor_ctx Context for query interceptor.
 * @var hydration_interceptor Hydration execution hook.
 * @var hydration_interceptor_ctx Context for hydration interceptor.
 * @var encrypt_hook Cryptographic hook for encryption.
 * @var decrypt_hook Cryptographic hook for decryption.
 * @var crypto_context Cryptographic context.
 * @var timezone Timezone configuration for session.
 * @var modality Execution modality setting.
 * @var modality_ctx Modality specific execution context.
 * @var stmt_cache Statement LRU cache.
 * @var slow_query_threshold_ms Threshold in ms for slow query logging.
 * @var telemetry Telemetry metrics data.
 */
struct c_orm_db {
  /** @brief Driver virtual method table. */
  const c_orm_driver_vtable_t *vtable;
  /** @brief Driver-specific private data pointer. */
  void *driver_data;
  /** @brief Driver name identifier. */
  const char *driver_name; /* e.g. sqlite, postgres, mysql */
  /** @brief Logging callback function. */
  c_orm_log_cb log_cb;
  /** @brief Context pointer for log callback. */
  void *log_user_data;
  /** @brief Record expiration callback function. */
  c_orm_expire_cb expire_cb;
  /** @brief Context pointer for expire callback. */
  void *expire_user_data;
  c_orm_identity_map_t *
      identity_map; /**< Phase 1: Associated identity map for caching objects */
  struct CddCHydrateRouter
      *hydrate_router; /**< Phase 2: Associated cdd-c hydrate router */

  c_orm_interceptor_cb query_interceptor; /**< Query execution hook */
  /** @brief Context for query interceptor. */
  void *query_interceptor_ctx;
  c_orm_interceptor_cb hydration_interceptor; /**< Hydration execution hook */
  /** @brief Context for hydration interceptor. */
  void *hydration_interceptor_ctx;

  c_orm_crypto_hook_t encrypt_hook; /**< Cryptographic hook for encryption */
  c_orm_crypto_hook_t decrypt_hook; /**< Cryptographic hook for decryption */
  void *crypto_context;             /**< Cryptographic context */

  c_orm_timezone_t timezone; /**< Timezone configuration for the session */

  c_orm_modality_t modality; /**< Execution modality setting */
  void *modality_ctx;        /**< Modality specific execution context */

  void *stmt_cache; /**< Phase 4: Statement LRU cache */

  /* Telemetry config */
  /** @brief Threshold in ms for slow query logging. */
  uint32_t slow_query_threshold_ms;
  /** @brief Telemetry metrics data. */
  c_orm_pool_telemetry_t telemetry;
};

/**
 * @brief Enable slow query logging and set the millisecond threshold.
 *
 * @param db Database handle.
 * @param threshold_ms Milliseconds a query must take to be logged. Set 0 to
 * disable.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_set_slow_query_threshold(c_orm_db_t *db, uint32_t threshold_ms);

/**
 * @brief Fetch current telemetry data from the database pool.
 *
 * @param db Database handle.
 * @param out_telemetry Pointer to receive the telemetry data struct.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_get_telemetry(c_orm_db_t *db, c_orm_pool_telemetry_t *out_telemetry);

/**
 * @brief Set the global logging callback for a database connection.
 *
 * @param db Database handle.
 * @param cb The callback function.
 * @param user_data Opaque pointer passed to the callback.
 */
C_ORM_EXPORT c_orm_error_t c_orm_set_log_callback(c_orm_db_t *db,
                                                  c_orm_log_cb cb,
                                                  void *user_data);

/**
 * @brief Set the expiration callback for a database connection.
 *
 * @param db Database handle.
 * @param cb The callback function.
 * @param user_data Opaque pointer passed to the callback.
 */
C_ORM_EXPORT c_orm_error_t c_orm_set_expire_callback(c_orm_db_t *db,
                                                     c_orm_expire_cb cb,
                                                     void *user_data);

/**
 * @brief Register cryptographic hooks for transparent encryption/decryption of
 * C_ORM_SECURE_FIELD fields.
 *
 * @param db Database connection.
 * @param encrypt_hook Callback function to encrypt data.
 * @param decrypt_hook Callback function to decrypt data.
 * @param context Opaque user data for the crypto operations.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_register_crypto_hooks(c_orm_db_t *db, c_orm_crypto_hook_t encrypt_hook,
                            c_orm_crypto_hook_t decrypt_hook, void *context);

/**
 * @brief Configure timezone logic for mapping datetime conversions locally on
 * load (Step 252).
 *
 * @param db Database handle.
 * @param tz The timezone struct offset.
 */
C_ORM_EXPORT c_orm_error_t c_orm_set_timezone(c_orm_db_t *db,
                                              c_orm_timezone_t tz);

/**
 * @brief Configure the execution modality paradigm for this database
 * connection.
 *
 * @param db Database handle.
 * @param modality The desired execution paradigm (e.g., SYNC, THREAD_POOL).
 * @param ctx Context for the modality (e.g., pointer to a thread pool object).
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_set_modality(c_orm_db_t *db,
                                              c_orm_modality_t modality,
                                              void *ctx);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* C_ORM_DB_H */

#if defined(__clang__) || defined(__GNUC__)
#endif
