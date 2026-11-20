/**
 * @file c_orm_odbc_abi.h
 * @brief Self-contained zero-<windows.h> ISO C89 ABI definitions for ODBC 3.52
 * and IBM Db2 Call Level Interface (CLI).
 */

#ifndef C_ORM_ODBC_ABI_H
#define C_ORM_ODBC_ABI_H

/* clang-format off */
#include "c_orm_meta.h"
#include "c_orm_safe_crt.h"
#include <stddef.h>
/* clang-format on */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#if defined(_WIN32) || defined(_WIN64)
#define C_ORM_SQL_API __stdcall
#else
#define C_ORM_SQL_API
#endif

/** @brief Unsigned 8-bit SQL character (SQLCHAR). */
typedef unsigned char c_orm_sqlchar_t;

/** @brief Signed 8-bit SQL character (SQLSCHAR). */
typedef signed char c_orm_sqlschar_t;

/** @brief Unsigned 16-bit UTF-16 SQL wide character (SQLWCHAR). */
typedef unsigned short c_orm_sqlwchar_t;

/** @brief Signed 16-bit integer (SQLSMALLINT). */
typedef short c_orm_sqlsmallint_t;

/** @brief Unsigned 16-bit integer (SQLUSMALLINT). */
typedef unsigned short c_orm_sqlusmallint_t;

/** @brief ODBC/CLI return status code (SQLRETURN). */
typedef short c_orm_sqlreturn_t;

#if defined(_WIN64) || defined(__LP64__) || defined(_LP64) ||                  \
    defined(__x86_64__) || defined(__aarch64__) || defined(__ppc64__) ||       \
    defined(__s390x__)
/** @brief Signed 32-bit integer (SQLINTEGER). */
typedef int c_orm_sqlinteger_t;
/** @brief Unsigned 32-bit integer (SQLUINTEGER). */
typedef unsigned int c_orm_sqluinteger_t;
#else
/** @brief Signed 32-bit integer (SQLINTEGER). */
typedef long c_orm_sqlinteger_t;
/** @brief Unsigned 32-bit integer (SQLUINTEGER). */
typedef unsigned long c_orm_sqluinteger_t;
#endif

#if defined(_WIN64) || defined(__LP64__) || defined(_LP64)
/** @brief Pointer-sized signed length/indicator (SQLLEN). */
typedef c_orm_int64_t c_orm_sqllen_t;
/** @brief Pointer-sized unsigned length (SQLULEN). */
typedef c_orm_uint64_t c_orm_sqlulen_t;
#else
/** @brief Pointer-sized signed length/indicator (SQLLEN). */
typedef long c_orm_sqllen_t;
/** @brief Pointer-sized unsigned length (SQLULEN). */
typedef unsigned long c_orm_sqlulen_t;
#endif

/** @brief Generic untyped data pointer (SQLPOINTER). */
typedef void *c_orm_sqlpointer_t;

/** @brief Generic opaque ODBC/CLI handle (SQLHANDLE). */
typedef void *c_orm_sqlhandle_t;

/** @brief Opaque Environment handle (SQLHENV). */
typedef c_orm_sqlhandle_t c_orm_sqlhenv_t;

/** @brief Opaque Connection handle (SQLHDBC). */
typedef c_orm_sqlhandle_t c_orm_sqlhdbc_t;

/** @brief Opaque Statement handle (SQLHSTMT). */
typedef c_orm_sqlhandle_t c_orm_sqlhstmt_t;

/** @brief Opaque Descriptor handle (SQLHDESC). */
typedef c_orm_sqlhandle_t c_orm_sqlhdesc_t;

/** @brief Opaque window handle for SQLDriverConnect (SQLHWND). */
typedef void *c_orm_sqlhwnd_t;

/** @brief Maximum bytes in SQL_NUMERIC_STRUCT mantissa. */
#define C_ORM_SQL_MAX_NUMERIC_LEN 16

/**
 * @brief Binary-compatible ODBC 3.x SQL_NUMERIC_STRUCT layout.
 * @var precision Total decimal digits (1..38).
 * @var scale Fractional digits after decimal point.
 * @var sign 1 if positive, 0 if negative.
 * @var val 16-byte little-endian unsigned unscaled integer mantissa.
 */
typedef struct c_orm_sql_numeric_struct {
  /** @brief Total decimal digits (1..38). */
  c_orm_sqlchar_t precision;
  /** @brief Fractional scale (-128..127). */
  c_orm_sqlschar_t scale;
  /** @brief Sign indicator: 1 = positive, 0 = negative. */
  c_orm_sqlchar_t sign;
  /** @brief 128-bit little-endian unscaled mantissa. */
  c_orm_sqlchar_t val[C_ORM_SQL_MAX_NUMERIC_LEN];
} c_orm_sql_numeric_struct_t;

/**
 * @brief Binary-compatible ODBC SQL_DATE_STRUCT layout.
 * @var year Calendar year (e.g. 2026).
 * @var month Month of year (1..12).
 * @var day Day of month (1..31).
 */
typedef struct c_orm_sql_date_struct {
  /** @brief Calendar year. */
  c_orm_sqlsmallint_t year;
  /** @brief Month (1..12). */
  c_orm_sqlusmallint_t month;
  /** @brief Day (1..31). */
  c_orm_sqlusmallint_t day;
} c_orm_sql_date_struct_t;

/**
 * @brief Binary-compatible ODBC SQL_TIME_STRUCT layout.
 * @var hour Hour of day (0..23).
 * @var minute Minute of hour (0..59).
 * @var second Second of minute (0..61).
 */
typedef struct c_orm_sql_time_struct {
  /** @brief Hour (0..23). */
  c_orm_sqlusmallint_t hour;
  /** @brief Minute (0..59). */
  c_orm_sqlusmallint_t minute;
  /** @brief Second (0..61). */
  c_orm_sqlusmallint_t second;
} c_orm_sql_time_struct_t;

/**
 * @brief Binary-compatible ODBC SQL_TIMESTAMP_STRUCT layout.
 * @var year Calendar year.
 * @var month Month (1..12).
 * @var day Day (1..31).
 * @var hour Hour (0..23).
 * @var minute Minute (0..59).
 * @var second Second (0..61).
 * @var fraction Fractional seconds in billionths (nanoseconds, 0..999999999).
 */
typedef struct c_orm_sql_timestamp_struct {
  /** @brief Calendar year. */
  c_orm_sqlsmallint_t year;
  /** @brief Month (1..12). */
  c_orm_sqlusmallint_t month;
  /** @brief Day (1..31). */
  c_orm_sqlusmallint_t day;
  /** @brief Hour (0..23). */
  c_orm_sqlusmallint_t hour;
  /** @brief Minute (0..59). */
  c_orm_sqlusmallint_t minute;
  /** @brief Second (0..61). */
  c_orm_sqlusmallint_t second;
  /** @brief Nanosecond fraction (0..999999999). */
  c_orm_sqluinteger_t fraction;
} c_orm_sql_timestamp_struct_t;

/** @brief C89 compile-time check: SQLSMALLINT is 2 bytes. */
typedef char
    c_orm_abi_chk_smallint[(sizeof(c_orm_sqlsmallint_t) == 2) ? 1 : -1];
/** @brief C89 compile-time check: SQLWCHAR is 2 bytes. */
typedef char c_orm_abi_chk_wchar[(sizeof(c_orm_sqlwchar_t) == 2) ? 1 : -1];
/** @brief C89 compile-time check: SQLINTEGER is 4 bytes. */
typedef char c_orm_abi_chk_integer[(sizeof(c_orm_sqlinteger_t) == 4) ? 1 : -1];
/** @brief C89 compile-time check: SQLUINTEGER is 4 bytes. */
typedef char
    c_orm_abi_chk_uinteger[(sizeof(c_orm_sqluinteger_t) == 4) ? 1 : -1];
/** @brief C89 compile-time check: SQLLEN matches pointer width. */
typedef char
    c_orm_abi_chk_sqllen[(sizeof(c_orm_sqllen_t) == sizeof(void *)) ? 1 : -1];
/** @brief C89 compile-time check: SQLULEN matches pointer width. */
typedef char
    c_orm_abi_chk_sqlulen[(sizeof(c_orm_sqlulen_t) == sizeof(void *)) ? 1 : -1];
/** @brief C89 compile-time check: SQL_NUMERIC_STRUCT is 19 bytes. */
typedef char
    c_orm_abi_chk_numeric[(sizeof(c_orm_sql_numeric_struct_t) == 19) ? 1 : -1];
/** @brief C89 compile-time check: SQL_DATE_STRUCT is 6 bytes. */
typedef char
    c_orm_abi_chk_date[(sizeof(c_orm_sql_date_struct_t) == 6) ? 1 : -1];
/** @brief C89 compile-time check: SQL_TIME_STRUCT is 6 bytes. */
typedef char
    c_orm_abi_chk_time[(sizeof(c_orm_sql_time_struct_t) == 6) ? 1 : -1];
/** @brief C89 compile-time check: SQL_TIMESTAMP_STRUCT is 16 bytes. */
typedef char
    c_orm_abi_chk_timestamp[(sizeof(c_orm_sql_timestamp_struct_t) == 16) ? 1
                                                                         : -1];

/* Return codes */
#define C_ORM_SQL_SUCCESS ((c_orm_sqlreturn_t)0)
#define C_ORM_SQL_SUCCESS_WITH_INFO ((c_orm_sqlreturn_t)1)
#define C_ORM_SQL_STILL_EXECUTING ((c_orm_sqlreturn_t)2)
#define C_ORM_SQL_NEED_DATA ((c_orm_sqlreturn_t)99)
#define C_ORM_SQL_NO_DATA ((c_orm_sqlreturn_t)100)
#define C_ORM_SQL_ERROR ((c_orm_sqlreturn_t)(-1))
#define C_ORM_SQL_INVALID_HANDLE ((c_orm_sqlreturn_t)(-2))

#define C_ORM_SQL_SUCCEEDED(rc) (((rc) & (~1)) == 0)

/* Handle types */
#define C_ORM_SQL_HANDLE_ENV ((c_orm_sqlsmallint_t)1)
#define C_ORM_SQL_HANDLE_DBC ((c_orm_sqlsmallint_t)2)
#define C_ORM_SQL_HANDLE_STMT ((c_orm_sqlsmallint_t)3)
#define C_ORM_SQL_HANDLE_DESC ((c_orm_sqlsmallint_t)4)
#define C_ORM_SQL_NULL_HANDLE ((c_orm_sqlhandle_t)0)

/* Length / null sentinels */
#define C_ORM_SQL_NULL_DATA (-1)
#define C_ORM_SQL_DATA_AT_EXEC (-2)
#define C_ORM_SQL_NTS (-3)
#define C_ORM_SQL_NO_TOTAL (-4)

/* DriverConnect prompt completion */
#define C_ORM_SQL_DRIVER_NOPROMPT ((c_orm_sqlusmallint_t)0)

/* SQLEndTran completion types */
#define C_ORM_SQL_COMMIT ((c_orm_sqlsmallint_t)0)
#define C_ORM_SQL_ROLLBACK ((c_orm_sqlsmallint_t)1)

/* SQLFreeStmt options */
#define C_ORM_SQL_CLOSE ((c_orm_sqlusmallint_t)0)
#define C_ORM_SQL_DROP ((c_orm_sqlusmallint_t)1)
#define C_ORM_SQL_UNBIND ((c_orm_sqlusmallint_t)2)
#define C_ORM_SQL_RESET_PARAMS ((c_orm_sqlusmallint_t)3)

/* Parameter input/output directions */
#define C_ORM_SQL_PARAM_INPUT ((c_orm_sqlsmallint_t)1)
#define C_ORM_SQL_PARAM_INPUT_OUTPUT ((c_orm_sqlsmallint_t)2)
#define C_ORM_SQL_PARAM_OUTPUT ((c_orm_sqlsmallint_t)4)

/* Standard SQL data types */
#define C_ORM_SQL_UNKNOWN_TYPE 0
#define C_ORM_SQL_CHAR 1
#define C_ORM_SQL_NUMERIC 2
#define C_ORM_SQL_DECIMAL 3
#define C_ORM_SQL_INTEGER 4
#define C_ORM_SQL_SMALLINT 5
#define C_ORM_SQL_FLOAT 6
#define C_ORM_SQL_REAL 7
#define C_ORM_SQL_DOUBLE 8
#define C_ORM_SQL_DATETIME 9
#define C_ORM_SQL_VARCHAR 12
#define C_ORM_SQL_TYPE_DATE 91
#define C_ORM_SQL_TYPE_TIME 92
#define C_ORM_SQL_TYPE_TIMESTAMP 93
#define C_ORM_SQL_LONGVARCHAR (-1)
#define C_ORM_SQL_BINARY (-2)
#define C_ORM_SQL_VARBINARY (-3)
#define C_ORM_SQL_LONGVARBINARY (-4)
#define C_ORM_SQL_BIGINT (-5)
#define C_ORM_SQL_TINYINT (-6)
#define C_ORM_SQL_BIT (-7)
#define C_ORM_SQL_WCHAR (-8)
#define C_ORM_SQL_WVARCHAR (-9)
#define C_ORM_SQL_WLONGVARCHAR (-10)

/* IBM Db2 CLI extended SQL data types (sqlcli.h / sqlcli1.h) */
#define C_ORM_SQL_GRAPHIC (-95)
#define C_ORM_SQL_VARGRAPHIC (-96)
#define C_ORM_SQL_LONGVARGRAPHIC (-97)
#define C_ORM_SQL_BLOB (-98)
#define C_ORM_SQL_CLOB (-99)
#define C_ORM_SQL_DBCLOB (-350)
#define C_ORM_SQL_DECFLOAT (-360)
#define C_ORM_SQL_XML (-370)

/* ODBC C data types */
#define C_ORM_SQL_SIGNED_OFFSET (-20)
#define C_ORM_SQL_UNSIGNED_OFFSET (-22)
#define C_ORM_SQL_C_CHAR C_ORM_SQL_CHAR
#define C_ORM_SQL_C_WCHAR C_ORM_SQL_WCHAR
#define C_ORM_SQL_C_LONG C_ORM_SQL_INTEGER
#define C_ORM_SQL_C_SHORT C_ORM_SQL_SMALLINT
#define C_ORM_SQL_C_FLOAT C_ORM_SQL_REAL
#define C_ORM_SQL_C_DOUBLE C_ORM_SQL_DOUBLE
#define C_ORM_SQL_C_NUMERIC C_ORM_SQL_NUMERIC
#define C_ORM_SQL_C_DEFAULT 99
#define C_ORM_SQL_C_SLONG (C_ORM_SQL_C_LONG + C_ORM_SQL_SIGNED_OFFSET)
#define C_ORM_SQL_C_ULONG (C_ORM_SQL_C_LONG + C_ORM_SQL_UNSIGNED_OFFSET)
#define C_ORM_SQL_C_SSHORT (C_ORM_SQL_C_SHORT + C_ORM_SQL_SIGNED_OFFSET)
#define C_ORM_SQL_C_USHORT (C_ORM_SQL_C_SHORT + C_ORM_SQL_UNSIGNED_OFFSET)
#define C_ORM_SQL_C_SBIGINT (C_ORM_SQL_BIGINT + C_ORM_SQL_SIGNED_OFFSET)
#define C_ORM_SQL_C_UBIGINT (C_ORM_SQL_BIGINT + C_ORM_SQL_UNSIGNED_OFFSET)
#define C_ORM_SQL_C_BINARY C_ORM_SQL_BINARY
#define C_ORM_SQL_C_BIT C_ORM_SQL_BIT
#define C_ORM_SQL_C_TYPE_DATE C_ORM_SQL_TYPE_DATE
#define C_ORM_SQL_C_TYPE_TIME C_ORM_SQL_TYPE_TIME
#define C_ORM_SQL_C_TYPE_TIMESTAMP C_ORM_SQL_TYPE_TIMESTAMP

/* IBM Db2 CLI LOB Locator C types */
#define C_ORM_SQL_C_BLOB_LOCATOR 31
#define C_ORM_SQL_C_CLOB_LOCATOR 41
#define C_ORM_SQL_C_DBCLOB_LOCATOR 351
#define C_ORM_SQL_C_DBCLOB_LOCATOR_CLI (-351)

/* Environment, Connection, and Statement Attributes */
#define C_ORM_SQL_ATTR_ODBC_VERSION 200
#define C_ORM_SQL_ATTR_CONNECTION_POOLING 201
#define C_ORM_SQL_OV_ODBC3 3UL

#define C_ORM_SQL_ATTR_AUTOCOMMIT 102
#define C_ORM_SQL_AUTOCOMMIT_OFF 0UL
#define C_ORM_SQL_AUTOCOMMIT_ON 1UL
#define C_ORM_SQL_ATTR_LOGIN_TIMEOUT 103
#define C_ORM_SQL_ATTR_TXN_ISOLATION 108
#define C_ORM_SQL_ATTR_CONNECTION_TIMEOUT 113

#define C_ORM_SQL_TXN_READ_UNCOMMITTED 0x00000001L
#define C_ORM_SQL_TXN_READ_COMMITTED 0x00000002L
#define C_ORM_SQL_TXN_REPEATABLE_READ 0x00000004L
#define C_ORM_SQL_TXN_SERIALIZABLE 0x00000008L

#define C_ORM_SQL_ATTR_QUERY_TIMEOUT 0
#define C_ORM_SQL_ATTR_PARAM_STATUS_PTR 20
#define C_ORM_SQL_ATTR_PARAMS_PROCESSED_PTR 21
#define C_ORM_SQL_ATTR_PARAMSET_SIZE 22

#define C_ORM_SQL_IS_POINTER (-4)
#define C_ORM_SQL_IS_UINTEGER (-5)
#define C_ORM_SQL_IS_INTEGER (-6)

/* IBM Db2 CLI specific connection & statement attributes */
#define C_ORM_SQL_ATTR_CURRENT_SCHEMA 1254
#define C_ORM_SQL_ATTR_INFO_USERID 1281
#define C_ORM_SQL_ATTR_INFO_WRKSTNNAME 1282
#define C_ORM_SQL_ATTR_INFO_APPLNAME 1283
#define C_ORM_SQL_ATTR_INFO_ACCTSTR 1284
#define C_ORM_SQL_ATTR_BLOB_LOCATOR (-98)

/* SQLGetInfo info types */
#define C_ORM_SQL_DBMS_NAME 17
#define C_ORM_SQL_DBMS_VER 18

/**
 * @brief Dispatch table encapsulating all ODBC 3.52 and IBM Db2 CLI entry
 * points.
 */
typedef struct c_orm_odbc_api {
  /** @brief Allocates an environment, connection, statement, or descriptor. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLAllocHandle)(
      c_orm_sqlsmallint_t HandleType, c_orm_sqlhandle_t InputHandle,
      c_orm_sqlhandle_t *OutputHandlePtr);
  /** @brief Frees resources associated with a specific handle. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLFreeHandle)(
      c_orm_sqlsmallint_t HandleType, c_orm_sqlhandle_t Handle);
  /** @brief Sets an attribute governing environment behavior. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLSetEnvAttr)(
      c_orm_sqlhenv_t EnvironmentHandle, c_orm_sqlinteger_t Attribute,
      c_orm_sqlpointer_t ValuePtr, c_orm_sqlinteger_t StringLength);
  /** @brief Sets an ANSI connection attribute. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLSetConnectAttr)(
      c_orm_sqlhdbc_t ConnectionHandle, c_orm_sqlinteger_t Attribute,
      c_orm_sqlpointer_t ValuePtr, c_orm_sqlinteger_t StringLength);
  /** @brief Sets a UTF-16 wide connection attribute. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLSetConnectAttrW)(
      c_orm_sqlhdbc_t ConnectionHandle, c_orm_sqlinteger_t Attribute,
      c_orm_sqlpointer_t ValuePtr, c_orm_sqlinteger_t StringLength);
  /** @brief Retrieves the current setting of a connection attribute. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLGetConnectAttr)(
      c_orm_sqlhdbc_t ConnectionHandle, c_orm_sqlinteger_t Attribute,
      c_orm_sqlpointer_t ValuePtr, c_orm_sqlinteger_t BufferLength,
      c_orm_sqlinteger_t *StringLengthPtr);
  /** @brief Establishes a connection using an ANSI/UTF-8 connection string. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLDriverConnect)(
      c_orm_sqlhdbc_t ConnectionHandle, c_orm_sqlhwnd_t WindowHandle,
      c_orm_sqlchar_t *InConnectionString, c_orm_sqlsmallint_t StringLength1,
      c_orm_sqlchar_t *OutConnectionString, c_orm_sqlsmallint_t BufferLength,
      c_orm_sqlsmallint_t *StringLength2Ptr,
      c_orm_sqlusmallint_t DriverCompletion);
  /** @brief Establishes a connection using a UTF-16 connection string. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLDriverConnectW)(
      c_orm_sqlhdbc_t ConnectionHandle, c_orm_sqlhwnd_t WindowHandle,
      c_orm_sqlwchar_t *InConnectionString, c_orm_sqlsmallint_t StringLength1,
      c_orm_sqlwchar_t *OutConnectionString, c_orm_sqlsmallint_t BufferLength,
      c_orm_sqlsmallint_t *StringLength2Ptr,
      c_orm_sqlusmallint_t DriverCompletion);
  /** @brief Connects to a cataloged DSN with username and password. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLConnect)(c_orm_sqlhdbc_t ConnectionHandle,
                                               c_orm_sqlchar_t *ServerName,
                                               c_orm_sqlsmallint_t NameLength1,
                                               c_orm_sqlchar_t *UserName,
                                               c_orm_sqlsmallint_t NameLength2,
                                               c_orm_sqlchar_t *Authentication,
                                               c_orm_sqlsmallint_t NameLength3);
  /** @brief Closes the connection associated with a connection handle. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLDisconnect)(
      c_orm_sqlhdbc_t ConnectionHandle);
  /** @brief Commits or rolls back an active transaction on ENV or DBC. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLEndTran)(
      c_orm_sqlsmallint_t HandleType, c_orm_sqlhandle_t Handle,
      c_orm_sqlsmallint_t CompletionType);
  /** @brief Sets an attribute related to a statement handle. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLSetStmtAttr)(
      c_orm_sqlhstmt_t StatementHandle, c_orm_sqlinteger_t Attribute,
      c_orm_sqlpointer_t ValuePtr, c_orm_sqlinteger_t StringLength);
  /** @brief Prepares an ANSI/UTF-8 SQL string for execution. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLPrepare)(c_orm_sqlhstmt_t StatementHandle,
                                               c_orm_sqlchar_t *StatementText,
                                               c_orm_sqlinteger_t TextLength);
  /** @brief Prepares a UTF-16 SQL string for execution. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLPrepareW)(
      c_orm_sqlhstmt_t StatementHandle, c_orm_sqlwchar_t *StatementText,
      c_orm_sqlinteger_t TextLength);
  /** @brief Executes a prepared statement. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLExecute)(
      c_orm_sqlhstmt_t StatementHandle);
  /** @brief Directly executes a SQL statement without prior preparation. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLExecDirect)(
      c_orm_sqlhstmt_t StatementHandle, c_orm_sqlchar_t *StatementText,
      c_orm_sqlinteger_t TextLength);
  /** @brief Binds a buffer to a parameter marker in a SQL statement. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLBindParameter)(
      c_orm_sqlhstmt_t StatementHandle, c_orm_sqlusmallint_t ParameterNumber,
      c_orm_sqlsmallint_t InputOutputType, c_orm_sqlsmallint_t ValueType,
      c_orm_sqlsmallint_t ParameterType, c_orm_sqlulen_t ColumnSize,
      c_orm_sqlsmallint_t DecimalDigits, c_orm_sqlpointer_t ParameterValuePtr,
      c_orm_sqllen_t BufferLength, c_orm_sqllen_t *StrLen_or_IndPtr);
  /** @brief Returns the number of columns in a result set. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLNumResultCols)(
      c_orm_sqlhstmt_t StatementHandle, c_orm_sqlsmallint_t *ColumnCountPtr);
  /** @brief Describes a column in the result set (ANSI/UTF-8). */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLDescribeCol)(
      c_orm_sqlhstmt_t StatementHandle, c_orm_sqlusmallint_t ColumnNumber,
      c_orm_sqlchar_t *ColumnName, c_orm_sqlsmallint_t BufferLength,
      c_orm_sqlsmallint_t *NameLengthPtr, c_orm_sqlsmallint_t *DataTypePtr,
      c_orm_sqlulen_t *ColumnSizePtr, c_orm_sqlsmallint_t *DecimalDigitsPtr,
      c_orm_sqlsmallint_t *NullablePtr);
  /** @brief Describes a column in the result set (UTF-16). */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLDescribeColW)(
      c_orm_sqlhstmt_t StatementHandle, c_orm_sqlusmallint_t ColumnNumber,
      c_orm_sqlwchar_t *ColumnName, c_orm_sqlsmallint_t BufferLength,
      c_orm_sqlsmallint_t *NameLengthPtr, c_orm_sqlsmallint_t *DataTypePtr,
      c_orm_sqlulen_t *ColumnSizePtr, c_orm_sqlsmallint_t *DecimalDigitsPtr,
      c_orm_sqlsmallint_t *NullablePtr);
  /** @brief Returns descriptor information for a column in a result set. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLColAttribute)(
      c_orm_sqlhstmt_t StatementHandle, c_orm_sqlusmallint_t ColumnNumber,
      c_orm_sqlusmallint_t FieldIdentifier,
      c_orm_sqlpointer_t CharacterAttributePtr,
      c_orm_sqlsmallint_t BufferLength, c_orm_sqlsmallint_t *StringLengthPtr,
      c_orm_sqllen_t *NumericAttributePtr);
  /** @brief Fetches the next rowset of data from the result set. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLFetch)(c_orm_sqlhstmt_t StatementHandle);
  /** @brief Retrieves data for a single column in the current row. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLGetData)(
      c_orm_sqlhstmt_t StatementHandle, c_orm_sqlusmallint_t Col_or_Param_Num,
      c_orm_sqlsmallint_t TargetType, c_orm_sqlpointer_t TargetValuePtr,
      c_orm_sqllen_t BufferLength, c_orm_sqllen_t *StrLen_or_IndPtr);
  /** @brief Sends part or all of a data value for a data-at-execution param. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLPutData)(c_orm_sqlhstmt_t StatementHandle,
                                               c_orm_sqlpointer_t DataPtr,
                                               c_orm_sqllen_t StrLen_or_Ind);
  /** @brief Advances data-at-execution parameter processing. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLParamData)(
      c_orm_sqlhstmt_t StatementHandle, c_orm_sqlpointer_t *ValuePtrPtr);
  /** @brief Returns the number of rows affected by UPDATE/INSERT/DELETE. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLRowCount)(
      c_orm_sqlhstmt_t StatementHandle, c_orm_sqllen_t *RowCountPtr);
  /** @brief Determines whether more result sets are available. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLMoreResults)(
      c_orm_sqlhstmt_t StatementHandle);
  /** @brief Stops processing / closes cursor / resets params on statement. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLFreeStmt)(
      c_orm_sqlhstmt_t StatementHandle, c_orm_sqlusmallint_t Option);
  /** @brief Closes a cursor opened on a statement handle. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLCloseCursor)(
      c_orm_sqlhstmt_t StatementHandle);
  /** @brief Retrieves diagnostic record fields (ANSI/UTF-8). */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLGetDiagRec)(
      c_orm_sqlsmallint_t HandleType, c_orm_sqlhandle_t Handle,
      c_orm_sqlsmallint_t RecNumber, c_orm_sqlchar_t *SQLState,
      c_orm_sqlinteger_t *NativeErrorPtr, c_orm_sqlchar_t *MessageText,
      c_orm_sqlsmallint_t BufferLength, c_orm_sqlsmallint_t *TextLengthPtr);
  /** @brief Retrieves diagnostic record fields (UTF-16). */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLGetDiagRecW)(
      c_orm_sqlsmallint_t HandleType, c_orm_sqlhandle_t Handle,
      c_orm_sqlsmallint_t RecNumber, c_orm_sqlwchar_t *SQLState,
      c_orm_sqlinteger_t *NativeErrorPtr, c_orm_sqlwchar_t *MessageText,
      c_orm_sqlsmallint_t BufferLength, c_orm_sqlsmallint_t *TextLengthPtr);
  /** @brief Returns general information about the driver and data source. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLGetInfo)(
      c_orm_sqlhdbc_t ConnectionHandle, c_orm_sqlusmallint_t InfoType,
      c_orm_sqlpointer_t InfoValuePtr, c_orm_sqlsmallint_t BufferLength,
      c_orm_sqlsmallint_t *StringLengthPtr);
  /** @brief Returns catalog list of table, catalog, or schema names. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLTables)(
      c_orm_sqlhstmt_t StatementHandle, c_orm_sqlchar_t *CatalogName,
      c_orm_sqlsmallint_t NameLength1, c_orm_sqlchar_t *SchemaName,
      c_orm_sqlsmallint_t NameLength2, c_orm_sqlchar_t *TableName,
      c_orm_sqlsmallint_t NameLength3, c_orm_sqlchar_t *TableType,
      c_orm_sqlsmallint_t NameLength4);
  /** @brief Returns catalog column metadata for specified tables. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLColumns)(
      c_orm_sqlhstmt_t StatementHandle, c_orm_sqlchar_t *CatalogName,
      c_orm_sqlsmallint_t NameLength1, c_orm_sqlchar_t *SchemaName,
      c_orm_sqlsmallint_t NameLength2, c_orm_sqlchar_t *TableName,
      c_orm_sqlsmallint_t NameLength3, c_orm_sqlchar_t *ColumnName,
      c_orm_sqlsmallint_t NameLength4);
  /** @brief Returns primary key column names for a table. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLPrimaryKeys)(
      c_orm_sqlhstmt_t StatementHandle, c_orm_sqlchar_t *CatalogName,
      c_orm_sqlsmallint_t NameLength1, c_orm_sqlchar_t *SchemaName,
      c_orm_sqlsmallint_t NameLength2, c_orm_sqlchar_t *TableName,
      c_orm_sqlsmallint_t NameLength3);
  /** @brief IBM Db2 CLI extension: retrieves byte/char length of a LOB
   * locator. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLGetLength)(
      c_orm_sqlhstmt_t StatementHandle, c_orm_sqlsmallint_t LocatorCType,
      c_orm_sqlinteger_t Locator, c_orm_sqlinteger_t *StringLength,
      c_orm_sqlinteger_t *IndicatorValue);
  /** @brief IBM Db2 CLI extension: fetches a slice of a LOB via locator. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLGetSubString)(
      c_orm_sqlhstmt_t StatementHandle, c_orm_sqlsmallint_t LocatorCType,
      c_orm_sqlinteger_t SourceLocator, c_orm_sqluinteger_t FromPosition,
      c_orm_sqluinteger_t ForLength, c_orm_sqlsmallint_t TargetCType,
      c_orm_sqlpointer_t DataPtr, c_orm_sqlinteger_t BufferLength,
      c_orm_sqlinteger_t *StringLength, c_orm_sqlinteger_t *IndicatorValue);
  /** @brief IBM Db2 CLI extension: locates a substring within a LOB locator. */
  c_orm_sqlreturn_t(C_ORM_SQL_API *SQLGetPosition)(
      c_orm_sqlhstmt_t StatementHandle, c_orm_sqlsmallint_t LocatorCType,
      c_orm_sqlinteger_t SourceLocator, c_orm_sqlinteger_t SearchLocator,
      c_orm_sqlchar_t *SearchLiteral, c_orm_sqlinteger_t SearchLiteralLength,
      c_orm_sqluinteger_t FromPosition, c_orm_sqluinteger_t *LocatedAt,
      c_orm_sqlinteger_t *IndicatorValue);
} c_orm_odbc_api_t;

/**
 * @brief Symbol lookup callback signature used to populate c_orm_odbc_api_t.
 * @param handle Opaque library handle or user context.
 * @param symbol_name Null-terminated ASCII symbol name (e.g. "SQLAllocHandle").
 * @param out_fn Output pointer receiving resolved function address (or NULL).
 * @return C_ORM_OK on lookup completion (even if optional symbol is NULL), or
 * error code.
 */
typedef c_orm_error_t (*c_orm_odbc_sym_fn)(void *handle,
                                           const char *symbol_name,
                                           void **out_fn);

/**
 * @brief Populates a c_orm_odbc_api_t dispatch table using a symbol lookup
 * callback and validates that all mandatory ODBC 3.x entry points are present.
 *
 * @param lookup_fn Symbol resolution callback.
 * @param handle Opaque module handle passed to lookup_fn.
 * @param out_api Output dispatch table to populate.
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER, or
 * C_ORM_ERROR_DRIVER_LOAD if mandatory symbols are missing.
 */
C_ORM_EXPORT c_orm_error_t c_orm_odbc_populate_api(c_orm_odbc_sym_fn lookup_fn,
                                                   void *handle,
                                                   c_orm_odbc_api_t *out_api);

/**
 * @brief Resets cached dynamic loader state (releasing any dynamically opened
 * library handles and clearing mock pointers).
 *
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_odbc_reset_loader(void);

/**
 * @brief Loads or returns the active ODBC Driver Manager dispatch table.
 *
 * Inspects any registered mock API first, then compile-time linked symbols if
 * `C_ORM_LINK_ODBC` is enabled, or dynamically loads `C_ORM_ODBC_LIB` /
 * platform default (`odbc32.dll`, `libodbc.so.2`, `libodbc.dylib`,
 * `libiodbc.dylib`).
 *
 * @param out_api Output pointer receiving the loaded function table.
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER, or
 * C_ORM_ERROR_DRIVER_LOAD if no ODBC library or required symbol was found.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_odbc_load_driver_manager(const c_orm_odbc_api_t **out_api);

/**
 * @brief Loads or returns the IBM Db2 CLI dispatch table, falling back to the
 * ODBC Driver Manager if direct `libdb2` / `db2cli64` is not present.
 *
 * @param out_api Output pointer receiving the loaded function table.
 * @param out_is_direct_db2_cli Optional output set to 1 if loaded directly from
 * IBM Db2 CLI (`libdb2`), or 0 if routed via an ODBC Driver Manager.
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER, or
 * C_ORM_ERROR_DRIVER_LOAD.
 */
C_ORM_EXPORT c_orm_error_t c_orm_db2_load_cli(const c_orm_odbc_api_t **out_api,
                                              int *out_is_direct_db2_cli);

/**
 * @brief Injects or clears a deterministic mock ODBC API dispatch table for
 * testing.
 *
 * @param mock_api Pointer to mock dispatch table, or NULL to restore runtime
 * loader behavior.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_odbc_set_mock_api(const c_orm_odbc_api_t *mock_api);

/**
 * @brief Injects or clears a deterministic mock IBM Db2 CLI dispatch table for
 * testing.
 *
 * @param mock_api Pointer to mock dispatch table, or NULL to restore runtime
 * loader behavior.
 * @param is_direct_cli Flag reported via `out_is_direct_db2_cli` while mock is
 * active.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_db2_set_mock_api(const c_orm_odbc_api_t *mock_api, int is_direct_cli);

#ifdef C_ORM_TEST_ALLOCATOR
/**
 * @brief Test callback typedef for intercepting OS shared-library opening.
 * @param path UTF-8 library path or soname.
 * @param out_handle Output opaque module handle.
 * @return C_ORM_OK on success, or C_ORM_ERROR_DRIVER_LOAD.
 */
typedef c_orm_error_t (*c_orm_odbc_dlopen_fn)(const char *path,
                                              void **out_handle);

/**
 * @brief Test callback typedef for intercepting OS shared-library closing.
 * @param handle Opaque module handle.
 * @return C_ORM_OK on success.
 */
typedef c_orm_error_t (*c_orm_odbc_dlclose_fn)(void *handle);

/**
 * @brief Installs or clears deterministic OS dynamic-loader hooks for unit
 * testing runtime loader fallback chains without external driver SDKs.
 *
 * @param open_fn Library open hook (or NULL to restore OS dlopen/LoadLibrary).
 * @param sym_fn Symbol lookup hook (or NULL to restore OS
 * dlsym/GetProcAddress).
 * @param close_fn Library close hook (or NULL to restore OS
 * dlclose/FreeLibrary).
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_odbc_set_test_dl_hooks(
    c_orm_odbc_dlopen_fn open_fn, c_orm_odbc_sym_fn sym_fn,
    c_orm_odbc_dlclose_fn close_fn);
#endif /* C_ORM_TEST_ALLOCATOR */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* C_ORM_ODBC_ABI_H */
