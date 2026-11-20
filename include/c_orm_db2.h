/**
 * @file c_orm_db2.h
 * @brief First-class IBM Db2 (LUW, Db2 for z/OS, Db2 for i) CLI/ODBC driver
 * for c-orm.
 */

#ifndef C_ORM_DB2_H
#define C_ORM_DB2_H

/* clang-format off */
#include "c_orm_db.h"
#include "c_orm_odbc.h"
/* clang-format on */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @brief IBM Db2 transaction isolation levels.
 */
typedef enum c_orm_db2_isolation {
  /** @brief Uncommitted Read (`WITH UR` / `SQL_TXN_READ_UNCOMMITTED`). */
  C_ORM_DB2_ISOLATION_UR = 0,
  /** @brief Cursor Stability (`WITH CS` / `SQL_TXN_READ_COMMITTED`, default).
   */
  C_ORM_DB2_ISOLATION_CS = 1,
  /** @brief Read Stability (`WITH RS` / `SQL_TXN_REPEATABLE_READ`). */
  C_ORM_DB2_ISOLATION_RS = 2,
  /** @brief Repeatable Read (`WITH RR` / `SQL_TXN_SERIALIZABLE` in ODBC). */
  C_ORM_DB2_ISOLATION_RR = 3
} c_orm_db2_isolation_t;

/**
 * @brief Parsed IBM Db2 session attributes extracted from a `db2://` URL.
 */
typedef struct c_orm_db2_url_opts {
  /** @brief Active schema (`CURRENT SCHEMA`), empty if unspecified. */
  char schema[128];
  /** @brief WLM client user ID register (`CLIENT USERID`). */
  char info_userid[128];
  /** @brief WLM client workstation register (`CLIENT WRKSTNNAME`). */
  char info_wrkstn[128];
  /** @brief WLM client application name register (`CLIENT APPLNAME`). */
  char info_applname[128];
  /** @brief WLM client accounting string register (`CLIENT ACCTNG`). */
  char info_acctstr[256];
  /** @brief Non-zero if `isolation` was explicitly specified in the URL. */
  int has_isolation;
  /** @brief Requested Db2 isolation level. */
  c_orm_db2_isolation_t isolation;
  /** @brief Login timeout in seconds (`0` if unspecified). */
  int login_timeout;
  /** @brief Initial autocommit flag (`1` by default, `0` if disabled). */
  int autocommit;
} c_orm_db2_url_opts_t;

/**
 * @brief Retrieves the IBM Db2 driver virtual method table.
 *
 * @param out_vtable Output pointer receiving the static Db2 vtable.
 * @return C_ORM_OK on success, or C_ORM_ERROR_NULL_POINTER.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_db2_get_vtable(const c_orm_driver_vtable_t **out_vtable);

/**
 * @brief Connects to an IBM Db2 server (LUW, z/OS, or IBM i) via `db2://` URL
 * or Db2 CLI connection string.
 *
 * Supports TCP/IP URLs (`db2://user:pass@host:50000/SAMPLE?schema=PROD&...`) as
 * well as cataloged DSN subsystem aliases (`db2://DB2Z_SUBSYS?uid=u&pwd=p`).
 *
 * @param url Db2 URL or raw CLI connection string.
 * @param out_db Output pointer receiving initialized database connection.
 * @return C_ORM_OK on success, or mapped c_orm_error_t on failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_db2_connect(const char *url,
                                             c_orm_db_t **out_db);

/**
 * @brief Parses a `db2://` URL into a sanitized IBM Db2 CLI/ODBC connection
 * string and structured Db2 session options.
 *
 * @param url Input `db2://` URL or raw `DATABASE=...` CLI string.
 * @param is_direct_cli Non-zero if connecting directly via `libdb2` (omitting
 * Driver Manager `DRIVER={IBM DB2 ODBC DRIVER}` prefix unless requested).
 * @param out_conn_str Destination buffer receiving sanitized CLI string.
 * @param buf_size Capacity of `out_conn_str` in bytes.
 * @param out_opts Optional output receiving parsed WLM/schema/isolation opts.
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER,
 * C_ORM_ERROR_VALIDATION, or C_ORM_ERROR_OVERFLOW.
 */
C_ORM_EXPORT c_orm_error_t c_orm_db2_build_conn_string(
    const char *url, int is_direct_cli, char *out_conn_str, size_t buf_size,
    c_orm_db2_url_opts_t *out_opts);

/**
 * @brief Configures IBM Db2 Workload Manager (WLM) client accounting registers
 * (`SQL_ATTR_INFO_USERID`, `WRKSTNNAME`, `APPLNAME`, `ACCTSTR`).
 *
 * NULL or empty arguments are skipped without clearing other registers.
 *
 * @param db Active Db2 connection.
 * @param userid Client user ID (`SQL_ATTR_INFO_USERID`), or NULL.
 * @param wrkstn Client workstation name (`SQL_ATTR_INFO_WRKSTNNAME`), or NULL.
 * @param applname Client application name (`SQL_ATTR_INFO_APPLNAME`), or NULL.
 * @param acctstr Client accounting string (`SQL_ATTR_INFO_ACCTSTR`), or NULL.
 * @return C_ORM_OK on success, or mapped diagnostic error.
 */
C_ORM_EXPORT c_orm_error_t c_orm_db2_set_client_info(c_orm_db_t *db,
                                                     const char *userid,
                                                     const char *wrkstn,
                                                     const char *applname,
                                                     const char *acctstr);

/**
 * @brief Sets the active schema (`CURRENT SCHEMA`) on an IBM Db2 connection.
 *
 * Validates and double-quotes the identifier safely, applying
 * `SQL_ATTR_CURRENT_SCHEMA` when supported or executing
 * `SET CURRENT SCHEMA = "<schema>"`.
 *
 * @param db Active Db2 connection.
 * @param schema Target Db2 schema identifier.
 * @return C_ORM_OK on success, C_ORM_ERROR_VALIDATION on invalid identifier, or
 * mapped SQL error.
 */
C_ORM_EXPORT c_orm_error_t c_orm_db2_set_current_schema(c_orm_db_t *db,
                                                        const char *schema);

/**
 * @brief Sets the IBM Db2 isolation level (`UR`, `CS`, `RS`, or `RR`) on `db`.
 *
 * @param db Active Db2 connection.
 * @param level Desired Db2 isolation level enum.
 * @return C_ORM_OK on success, C_ORM_ERROR_VALIDATION, or diagnostic error.
 */
C_ORM_EXPORT c_orm_error_t c_orm_db2_set_isolation(c_orm_db_t *db,
                                                   c_orm_db2_isolation_t level);

/**
 * @brief Creates a Db2 savepoint (`SAVEPOINT "<name>" ON ROLLBACK RETAIN
 * CURSORS`).
 *
 * @param db Active Db2 connection.
 * @param savepoint_name Savepoint identifier.
 * @return C_ORM_OK on success, or error code.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_db2_savepoint_create(c_orm_db_t *db, const char *savepoint_name);

/**
 * @brief Rolls back to a Db2 savepoint (`ROLLBACK TO SAVEPOINT "<name>"`).
 *
 * @param db Active Db2 connection.
 * @param savepoint_name Savepoint identifier.
 * @return C_ORM_OK on success, or error code.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_db2_savepoint_rollback(c_orm_db_t *db, const char *savepoint_name);

/**
 * @brief Releases a Db2 savepoint (`RELEASE SAVEPOINT "<name>"`).
 *
 * @param db Active Db2 connection.
 * @param savepoint_name Savepoint identifier.
 * @return C_ORM_OK on success, or error code.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_db2_savepoint_release(c_orm_db_t *db, const char *savepoint_name);

/**
 * @brief Retrieves the byte (for `BLOB`/`CLOB`) or double-byte character (for
 * `DBCLOB`) length of a Db2 LOB locator via `SQLGetLength`.
 *
 * @param db Active Db2 connection.
 * @param lob_c_type Locator C type (`C_ORM_SQL_C_BLOB_LOCATOR`,
 * `C_ORM_SQL_C_CLOB_LOCATOR`, or `C_ORM_SQL_C_DBCLOB_LOCATOR`).
 * @param locator 32-bit LOB locator token fetched from a result column.
 * @param out_length Output receiving total length of the referenced LOB.
 * @return C_ORM_OK on success, C_ORM_ERROR_NOT_IMPLEMENTED if CLI lacks
 * `SQLGetLength`, or mapped diagnostic error.
 */
C_ORM_EXPORT c_orm_error_t c_orm_db2_lob_get_length(c_orm_db_t *db,
                                                    short lob_c_type,
                                                    unsigned int locator,
                                                    c_orm_int64_t *out_length);

/**
 * @brief Reads a slice of a Db2 LOB locator without materializing the full LOB
 * in memory via `SQLGetSubString`.
 *
 * @param db Active Db2 connection.
 * @param lob_c_type Locator C type (`C_ORM_SQL_C_BLOB_LOCATOR`,
 * `C_ORM_SQL_C_CLOB_LOCATOR`, or `C_ORM_SQL_C_DBCLOB_LOCATOR`).
 * @param locator 32-bit LOB locator token.
 * @param offset_1based 1-based starting byte/character offset in the LOB.
 * @param out_buf Caller destination buffer.
 * @param buf_len Maximum bytes to read into `out_buf`.
 * @param out_read Output receiving actual bytes written to `out_buf`.
 * @return C_ORM_OK on success, or error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_db2_lob_read(c_orm_db_t *db, short lob_c_type,
                                              unsigned int locator,
                                              c_orm_int64_t offset_1based,
                                              void *out_buf, size_t buf_len,
                                              size_t *out_read);

/**
 * @brief Releases a server-side Db2 LOB locator (`FREE LOCATOR ?`).
 *
 * @param db Active Db2 connection.
 * @param lob_c_type Locator C type (`C_ORM_SQL_C_BLOB_LOCATOR`,
 * `C_ORM_SQL_C_CLOB_LOCATOR`, or `C_ORM_SQL_C_DBCLOB_LOCATOR`).
 * @param locator 32-bit LOB locator token.
 * @return C_ORM_OK on success, or mapped diagnostic error.
 */
C_ORM_EXPORT c_orm_error_t c_orm_db2_lob_free_locator(c_orm_db_t *db,
                                                      short lob_c_type,
                                                      unsigned int locator);

/**
 * @brief Normalizes IBM Db2 z/OS & LUW dash-dot timestamp strings
 * (`YYYY-MM-DD-HH.MM.SS[.nnnnnnnnnnnn]`) or ISO timestamps into canonical ISO
 * `YYYY-MM-DD HH:MM:SS[.nnnnnnnnnnnn]` format.
 *
 * @param db2_ts Input Db2 or ISO timestamp string.
 * @param out_iso_ts Destination buffer receiving normalized timestamp.
 * @param buf_size Capacity of `out_iso_ts` in bytes.
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER,
 * C_ORM_ERROR_VALIDATION, or C_ORM_ERROR_OVERFLOW.
 */
C_ORM_EXPORT c_orm_error_t c_orm_db2_normalize_timestamp(const char *db2_ts,
                                                         char *out_iso_ts,
                                                         size_t buf_size);

/**
 * @brief Binds a UTF-8 string to a Db2 DBCS `GRAPHIC` / `VARGRAPHIC` / `DBCLOB`
 * parameter (transcoding to UTF-16 `SQL_C_WCHAR` / `SQL_VARGRAPHIC`).
 *
 * @param query Prepared Db2 statement handle.
 * @param index 1-based parameter index.
 * @param utf8_val Null-terminated UTF-8 string, or NULL to bind SQL NULL.
 * @return C_ORM_OK on success, or error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_db2_bind_graphic(c_orm_query_t *query,
                                                  int index,
                                                  const char *utf8_val);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* C_ORM_DB2_H */
