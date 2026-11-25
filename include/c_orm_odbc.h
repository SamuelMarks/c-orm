/**
 * @file c_orm_odbc.h
 * @brief Generic ODBC 3.52 driver interface for c-orm (zero <windows.h>).
 */

#ifndef C_ORM_ODBC_H
#define C_ORM_ODBC_H

/* clang-format off */
#include "c_orm_db.h"
#include "c_orm_odbc_abi.h"
/* clang-format on */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @brief Retrieves the Generic ODBC driver virtual method table.
 *
 * @param out_vtable Output pointer receiving the static ODBC vtable.
 * @return C_ORM_OK on success, or C_ORM_ERROR_NULL_POINTER if out_vtable is
 * NULL.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_odbc_get_vtable(const c_orm_driver_vtable_t **out_vtable);

/**
 * @brief Connects to an ODBC data source via `odbc://` URL or raw ODBC
 * connection string (`DRIVER={...};...` / `DSN=...;...`).
 *
 * @param url Connection URL or raw connection string.
 * @param out_db Output pointer receiving the connected c_orm_db_t handle.
 * @return C_ORM_OK on success, or specific c_orm_error_t on load/parse/connect
 * failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_odbc_connect(const char *url,
                                              c_orm_db_t **out_db);

/**
 * @brief Percent-decodes a URL component in strict C89, rejecting embedded
 * `%00` NUL bytes and malformed hex escapes.
 *
 * @param src Source URL substring.
 * @param src_len Number of bytes in `src` to decode.
 * @param plus_as_space Non-zero to decode `'+'` as `' '` (query parameters).
 * @param out_buf Destination buffer receiving NUL-terminated decoded string.
 * @param buf_size Capacity of `out_buf` in bytes (must be >= 1).
 * @param out_len Optional output receiving decoded byte length, or NULL.
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER,
 * C_ORM_ERROR_VALIDATION on `%00` or bad `%XX`, or C_ORM_ERROR_OVERFLOW.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_odbc_percent_decode(const char *src, size_t src_len, int plus_as_space,
                          char *out_buf, size_t buf_size, size_t *out_len);

/**
 * @brief Escapes an ODBC connection-string attribute value against injection.
 *
 * Values containing `;`, `{`, `}`, `=`, quotes, or whitespace are wrapped in
 * `{...}` with internal `}` doubled to `}}` per the ODBC 3.x specification.
 * Embedded NUL or ASCII control characters (CR/LF) are rejected with
 * `C_ORM_ERROR_VALIDATION`.
 *
 * @param value Unescaped UTF-8 attribute value.
 * @param out_buf Destination buffer for escaped attribute value.
 * @param buf_size Capacity of `out_buf` in bytes.
 * @param out_len Optional output receiving written length excluding NUL.
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER,
 * C_ORM_ERROR_VALIDATION, or C_ORM_ERROR_OVERFLOW.
 */
C_ORM_EXPORT c_orm_error_t c_orm_odbc_escape_conn_value(const char *value,
                                                        char *out_buf,
                                                        size_t buf_size,
                                                        size_t *out_len);

/**
 * @brief Parses an `odbc://` URL or raw `DSN=...`/`DRIVER={...}` string into a
 * sanitized ODBC connection string and session options.
 *
 * @param url Input `odbc://...` URL or raw connection string.
 * @param out_conn_str Destination buffer receiving sanitized connection string.
 * @param buf_size Capacity of `out_conn_str` in bytes.
 * @param out_login_timeout Optional output receiving login timeout in seconds
 * (0 if unspecified).
 * @param out_autocommit Optional output receiving initial autocommit flag (1 by
 * default, 0 if disabled).
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER,
 * C_ORM_ERROR_VALIDATION, or C_ORM_ERROR_OVERFLOW.
 */
C_ORM_EXPORT c_orm_error_t c_orm_odbc_build_conn_string(const char *url,
                                                        char *out_conn_str,
                                                        size_t buf_size,
                                                        int *out_login_timeout,
                                                        int *out_autocommit);

/**
 * @brief Establishes an ODBC/CLI connection using an explicit dispatch table,
 * driver name, and optional vtable override (shared with IBM Db2 CLI driver).
 *
 * @param api Loaded ODBC / Db2 CLI function table.
 * @param conn_str Sanitized connection string passed to SQLDriverConnect(W).
 * @param driver_name Driver tag assigned to `db->driver_name` ("odbc" / "db2").
 * @param vtable_override Optional custom vtable, or NULL for generic ODBC.
 * @param login_timeout_sec Login timeout in seconds (>0 to apply).
 * @param autocommit Initial autocommit state (1 = ON, 0 = OFF).
 * @param prefer_wide Non-zero to prefer SQLDriverConnectW when available.
 * @param out_db Output pointer receiving initialized database connection.
 * @return C_ORM_OK on success, or mapped c_orm_error_t on failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_odbc_connect_raw(
    const c_orm_odbc_api_t *api, const char *conn_str, const char *driver_name,
    const c_orm_driver_vtable_t *vtable_override, int login_timeout_sec,
    int autocommit, int prefer_wide, c_orm_db_t **out_db);

/**
 * @brief Extracts underlying ODBC/CLI API table and handles from a connection.
 *
 * @param db Active ODBC or Db2 database connection.
 * @param out_api Optional output receiving the active dispatch table.
 * @param out_henv Optional output receiving the SQLHENV handle.
 * @param out_hdbc Optional output receiving the SQLHDBC handle.
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER, or
 * C_ORM_ERROR_VALIDATION if `db` is not an ODBC/Db2 handle.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_odbc_get_handles(c_orm_db_t *db, const c_orm_odbc_api_t **out_api,
                       c_orm_sqlhenv_t *out_henv, c_orm_sqlhdbc_t *out_hdbc);

/**
 * @brief Harvests diagnostic records from an ENV, DBC, or STMT handle into the
 * connection's error/trace buffers and maps SQLSTATE/SQLCODE to c_orm_error_t.
 *
 * @param db Active ODBC or Db2 database connection.
 * @param handle_type Handle type (`C_ORM_SQL_HANDLE_ENV/DBC/STMT`).
 * @param handle Target ODBC handle.
 * @param context Short operation label for error trace context.
 * @param fallback_err Default c_orm_error_t when no specific SQLSTATE maps.
 * @return Mapped c_orm_error_t error code (never C_ORM_OK).
 */
C_ORM_EXPORT c_orm_error_t c_orm_odbc_harvest_diag(
    c_orm_db_t *db, c_orm_sqlsmallint_t handle_type, c_orm_sqlhandle_t handle,
    const char *context, c_orm_error_t fallback_err);

/**
 * @brief Harvests diagnostic records from a statement handle, frees the
 * statement handle via `SQLFreeHandle(SQL_HANDLE_STMT)`, and returns the
 * mapped `c_orm_error_t`.
 *
 * @param db Active ODBC or Db2 database connection.
 * @param hstmt Target ODBC statement handle to inspect and free.
 * @param context Short operation label for error trace context.
 * @param fallback_err Default c_orm_error_t when no specific SQLSTATE maps.
 * @return Mapped c_orm_error_t error code (never C_ORM_OK).
 */
C_ORM_EXPORT c_orm_error_t c_orm_odbc_harvest_diag_free_stmt(
    c_orm_db_t *db, c_orm_sqlhandle_t hstmt, const char *context,
    c_orm_error_t fallback_err);

/**
 * @brief Retrieves the 5-character ANSI SQLSTATE recorded by the last
 * diagnostic on the connection (or `"00000"` if none).
 *
 * @param db Active ODBC or Db2 database connection.
 * @param out_sqlstate Caller buffer of at least 6 bytes receiving NUL-ended
 * 5-character SQLSTATE.
 * @return C_ORM_OK on success, or C_ORM_ERROR_NULL_POINTER /
 * C_ORM_ERROR_VALIDATION.
 */
C_ORM_EXPORT c_orm_error_t c_orm_odbc_get_sqlstate(c_orm_db_t *db,
                                                   char out_sqlstate[6]);

/**
 * @brief Retrieves the native vendor error code (e.g. Db2 SQLCODE `-803`) from
 * the most recent diagnostic record on `db`.
 *
 * @param db Active ODBC or Db2 database connection.
 * @param out_native_error Output pointer receiving native integer error code.
 * @return C_ORM_OK on success, or C_ORM_ERROR_NULL_POINTER /
 * C_ORM_ERROR_VALIDATION.
 */
C_ORM_EXPORT c_orm_error_t c_orm_odbc_get_native_error(
    c_orm_db_t *db, c_orm_sqlinteger_t *out_native_error);

/**
 * @brief Sets the transaction isolation bitmask on an ODBC/Db2 connection via
 * `SQL_ATTR_TXN_ISOLATION`.
 *
 * @param db Active ODBC or Db2 connection.
 * @param txn_isolation_mask One of `C_ORM_SQL_TXN_READ_UNCOMMITTED`,
 * `C_ORM_SQL_TXN_READ_COMMITTED`, `C_ORM_SQL_TXN_REPEATABLE_READ`, or
 * `C_ORM_SQL_TXN_SERIALIZABLE`.
 * @return C_ORM_OK on success, or mapped diagnostic error.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_odbc_set_isolation(c_orm_db_t *db, unsigned long txn_isolation_mask);

/**
 * @brief Begins a transaction on an ODBC/Db2 connection by disabling
 * `SQL_ATTR_AUTOCOMMIT` (avoiding invalid `BEGIN` SQL statements).
 *
 * @param db Active ODBC or Db2 connection.
 * @return C_ORM_OK on success, or mapped diagnostic error.
 */
C_ORM_EXPORT c_orm_error_t c_orm_odbc_begin_transaction(c_orm_db_t *db);

/**
 * @brief Commits the active ODBC/Db2 transaction via `SQLEndTran(SQL_COMMIT)`
 * and restores the connection's base autocommit mode.
 *
 * @param db Active ODBC or Db2 connection.
 * @return C_ORM_OK on success, or mapped diagnostic error.
 */
C_ORM_EXPORT c_orm_error_t c_orm_odbc_commit(c_orm_db_t *db);

/**
 * @brief Rolls back the active ODBC/Db2 transaction via
 * `SQLEndTran(SQL_ROLLBACK)` and restores the connection's base autocommit
 * mode.
 *
 * @param db Active ODBC or Db2 connection.
 * @return C_ORM_OK on success, or mapped diagnostic error.
 */
C_ORM_EXPORT c_orm_error_t c_orm_odbc_rollback(c_orm_db_t *db);

/**
 * @brief Lists table names in the connected catalog/schema via `SQLTables`.
 *
 * @param db Active ODBC or Db2 connection.
 * @param schema_pattern Optional schema name filter, or NULL for all schemas.
 * @param out_tables Output pointer receiving array of heap-allocated strings.
 * @param out_count Output pointer receiving number of table names returned.
 * @return C_ORM_OK on success, or error code on failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_odbc_list_tables(c_orm_db_t *db,
                                                  const char *schema_pattern,
                                                  char ***out_tables,
                                                  size_t *out_count);

/**
 * @brief Frees a string array returned by `c_orm_odbc_list_tables`.
 *
 * @param tables Array of heap-allocated strings (ignored if NULL).
 * @param count Number of elements in `tables`.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_odbc_free_string_list(char **tables,
                                                       size_t count);

/**
 * @brief Catalog column metadata returned by `c_orm_odbc_list_columns`
 * (`SQLColumns`).
 * @var name Column identifier string (heap-allocated).
 * @var type_name Native SQL type name string (heap-allocated).
 * @var column_size Column length or decimal precision (`0` if unspecified).
 * @var decimal_digits Fractional scale (`0` if unspecified).
 * @var is_nullable `1` if nullable, `0` if `NOT NULL`.
 */
typedef struct c_orm_odbc_column_info {
  /** @brief Column identifier string (heap-allocated). */
  char *name;
  /** @brief Native SQL type name string (heap-allocated). */
  char *type_name;
  /** @brief Column length or decimal precision (`0` if unspecified). */
  int column_size;
  /** @brief Fractional scale (`0` if unspecified). */
  int decimal_digits;
  /** @brief `1` if nullable, `0` if `NOT NULL`. */
  int is_nullable;
} c_orm_odbc_column_info_t;

/**
 * @brief Introspects table columns via the ODBC `SQLColumns` catalog API.
 *
 * @param db Active ODBC or Db2 connection.
 * @param schema_name Optional schema filter (NULL for current/any schema).
 * @param table_name Target table name (required).
 * @param out_cols Output receiving heap-allocated array of column descriptors.
 * @param out_count Output receiving number of column descriptors.
 * @return C_ORM_OK on success, or mapped diagnostic/validation/OOM error.
 */
C_ORM_EXPORT c_orm_error_t c_orm_odbc_list_columns(
    c_orm_db_t *db, const char *schema_name, const char *table_name,
    c_orm_odbc_column_info_t **out_cols, size_t *out_count);

/**
 * @brief Frees a column descriptor array returned by
 * `c_orm_odbc_list_columns`.
 *
 * @param cols Array of column descriptors (safe on NULL).
 * @param count Number of elements in `cols`.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_odbc_free_column_list(c_orm_odbc_column_info_t *cols, size_t count);

/**
 * @brief Executes a parameterized DML statement across an array of rows using
 * ODBC column-wise array binding (`SQL_ATTR_PARAMSET_SIZE`).
 *
 * For `C_ORM_TYPE_INT32`, `col_arrays[c]` points to `const int32_t[row_count]`.
 * For `C_ORM_TYPE_INT64`, `col_arrays[c]` points to `const int64_t[row_count]`.
 * For `C_ORM_TYPE_DOUBLE`, `col_arrays[c]` points to `const double[row_count]`.
 * For `C_ORM_TYPE_STRING`, `col_arrays[c]` points to
 * `const char *const[row_count]` (NULL entries bound as `SQL_NULL_DATA`).
 * For `C_ORM_TYPE_DECIMAL`, `col_arrays[c]` points to
 * `const c_orm_decimal_t[row_count]`.
 *
 * @param db Active ODBC or Db2 connection.
 * @param sql Parameterized DML SQL statement (e.g. `INSERT INTO t VALUES
 * (?,?)`).
 * @param row_count Number of rows in the batch (`>= 1`).
 * @param col_count Number of parameter columns (`>= 1`).
 * @param col_types Array of `col_count` column types.
 * @param col_arrays Array of `col_count` pointers to column value arrays.
 * @param out_rows_processed Optional output receiving rows processed count.
 * @return C_ORM_OK on success, or mapped diagnostic/validation/OOM error.
 */
C_ORM_EXPORT c_orm_error_t c_orm_odbc_execute_batch(
    c_orm_db_t *db, const char *sql, size_t row_count, size_t col_count,
    const c_orm_type_t *col_types, const void *const *col_arrays,
    size_t *out_rows_processed);

/**
 * @brief Binds a UTF-8 string as a retained UTF-16 (`SQL_C_WCHAR`) parameter
 * with an explicit SQL target type (e.g. `C_ORM_SQL_VARGRAPHIC` or
 * `C_ORM_SQL_WVARCHAR`).
 *
 * @param query Prepared ODBC or Db2 statement handle.
 * @param index 1-based parameter index.
 * @param utf8_val Null-terminated UTF-8 string, or NULL to bind SQL NULL.
 * @param sql_param_type Target SQL parameter type (`C_ORM_SQL_*`).
 * @return C_ORM_OK on success, or error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_odbc_bind_wide_param(
    c_orm_query_t *query, int index, const char *utf8_val,
    c_orm_sqlsmallint_t sql_param_type);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* C_ORM_ODBC_H */
