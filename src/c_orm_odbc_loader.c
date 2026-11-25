/**
 * @file c_orm_odbc_loader.c
 * @brief Cross-platform zero-<windows.h> dynamic/static loader and mock
 * injector for ODBC Driver Managers and IBM Db2 CLI.
 */

/* clang-format off */
#include "c_orm_odbc_abi.h"
#include "c_orm_ebcdic.h"
#include "c_orm_log.h"
#include "c_orm_safe_crt.h"
#include <stddef.h>
#include <string.h>
#if !defined(C_ORM_NO_THREADS) && !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <pthread.h>
#endif
#if defined(_WIN32)
#if !defined(C_ORM_NO_THREADS) && defined(_MSC_VER)
#include <intrin.h>
#endif
#elif !defined(__EMSCRIPTEN__)
#include <dlfcn.h>
#endif
/* clang-format on */

#if defined(_WIN32)
/**
 * @brief Loads an ANSI-named Win32 dynamic-link library into the process.
 * @param lpLibFileName Null-terminated ANSI library path or module name.
 * @return Module handle on success, or NULL on failure.
 */
extern __declspec(dllimport) void *__stdcall LoadLibraryA(
    const char *lpLibFileName);

/**
 * @brief Loads a UTF-16-named Win32 dynamic-link library into the process.
 * @param lpLibFileName Null-terminated UTF-16 library path or module name.
 * @return Module handle on success, or NULL on failure.
 */
extern __declspec(dllimport) void *__stdcall LoadLibraryW(
    const unsigned short *lpLibFileName);

/**
 * @brief Retrieves the address of an exported function from a Win32 DLL.
 * @param hModule Handle to the loaded DLL module.
 * @param lpProcName Null-terminated ASCII symbol name.
 * @return Symbol address on success, or NULL on failure.
 */
extern __declspec(dllimport) void *__stdcall GetProcAddress(
    void *hModule, const char *lpProcName);

/**
 * @brief Decrements the reference count of a loaded Win32 DLL module.
 * @param hLibModule Handle to the loaded DLL module.
 * @return Non-zero on success, or 0 on failure.
 */
extern __declspec(dllimport) int __stdcall FreeLibrary(void *hLibModule);
#endif

#if defined(C_ORM_LINK_ODBC) || defined(C_ORM_LINK_DB2)
/**
 * @brief Allocates an ODBC environment, connection, statement, or descriptor.
 * @param HandleType Type of handle to allocate.
 * @param InputHandle Parent handle context, or SQL_NULL_HANDLE.
 * @param OutputHandlePtr Output pointer receiving the allocated handle.
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API
SQLAllocHandle(c_orm_sqlsmallint_t HandleType, c_orm_sqlhandle_t InputHandle,
               c_orm_sqlhandle_t *OutputHandlePtr);

/**
 * @brief Frees resources associated with an ODBC handle.
 * @param HandleType Type of handle to free.
 * @param Handle Handle instance to release.
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API
SQLFreeHandle(c_orm_sqlsmallint_t HandleType, c_orm_sqlhandle_t Handle);

/**
 * @brief Sets an attribute governing ODBC environment behavior.
 * @param EnvironmentHandle Target ODBC environment handle.
 * @param Attribute Environment attribute identifier.
 * @param ValuePtr Value or integer cast associated with Attribute.
 * @param StringLength Byte length of ValuePtr or indicator flag.
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API
SQLSetEnvAttr(c_orm_sqlhenv_t EnvironmentHandle, c_orm_sqlinteger_t Attribute,
              c_orm_sqlpointer_t ValuePtr, c_orm_sqlinteger_t StringLength);

/**
 * @brief Sets an ANSI connection attribute on an ODBC connection handle.
 * @param ConnectionHandle Target ODBC connection handle.
 * @param Attribute Connection attribute identifier.
 * @param ValuePtr Attribute value buffer or integer cast.
 * @param StringLength Byte length of ValuePtr or indicator flag.
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API SQLSetConnectAttr(
    c_orm_sqlhdbc_t ConnectionHandle, c_orm_sqlinteger_t Attribute,
    c_orm_sqlpointer_t ValuePtr, c_orm_sqlinteger_t StringLength);

/**
 * @brief Sets a UTF-16 connection attribute on an ODBC connection handle.
 * @param ConnectionHandle Target ODBC connection handle.
 * @param Attribute Connection attribute identifier.
 * @param ValuePtr Attribute value buffer or integer cast.
 * @param StringLength Byte length of ValuePtr or indicator flag.
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API SQLSetConnectAttrW(
    c_orm_sqlhdbc_t ConnectionHandle, c_orm_sqlinteger_t Attribute,
    c_orm_sqlpointer_t ValuePtr, c_orm_sqlinteger_t StringLength);

/**
 * @brief Queries the current value of an ODBC connection attribute.
 * @param ConnectionHandle Target ODBC connection handle.
 * @param Attribute Connection attribute identifier.
 * @param ValuePtr Output buffer receiving the attribute value.
 * @param BufferLength Capacity of ValuePtr in bytes.
 * @param StringLengthPtr Output pointer receiving actual byte count.
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API SQLGetConnectAttr(
    c_orm_sqlhdbc_t ConnectionHandle, c_orm_sqlinteger_t Attribute,
    c_orm_sqlpointer_t ValuePtr, c_orm_sqlinteger_t BufferLength,
    c_orm_sqlinteger_t *StringLengthPtr);

/**
 * @brief Establishes an ODBC connection using an ANSI connection string.
 * @param ConnectionHandle Target ODBC connection handle.
 * @param WindowHandle Optional parent window handle.
 * @param InConnectionString Input ANSI connection string.
 * @param StringLength1 Length of InConnectionString or SQL_NTS.
 * @param OutConnectionString Optional output buffer for completed string.
 * @param BufferLength Capacity of OutConnectionString.
 * @param StringLength2Ptr Output pointer receiving completed string length.
 * @param DriverCompletion Prompting mode flag (e.g. SQL_DRIVER_NOPROMPT).
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API SQLDriverConnect(
    c_orm_sqlhdbc_t ConnectionHandle, c_orm_sqlhwnd_t WindowHandle,
    c_orm_sqlchar_t *InConnectionString, c_orm_sqlsmallint_t StringLength1,
    c_orm_sqlchar_t *OutConnectionString, c_orm_sqlsmallint_t BufferLength,
    c_orm_sqlsmallint_t *StringLength2Ptr,
    c_orm_sqlusmallint_t DriverCompletion);

/**
 * @brief Establishes an ODBC connection using a UTF-16 connection string.
 * @param ConnectionHandle Target ODBC connection handle.
 * @param WindowHandle Optional parent window handle.
 * @param InConnectionString Input UTF-16 connection string.
 * @param StringLength1 Length of InConnectionString in characters or SQL_NTS.
 * @param OutConnectionString Optional output UTF-16 buffer.
 * @param BufferLength Capacity of OutConnectionString in characters.
 * @param StringLength2Ptr Output pointer receiving completed character length.
 * @param DriverCompletion Prompting mode flag (e.g. SQL_DRIVER_NOPROMPT).
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API SQLDriverConnectW(
    c_orm_sqlhdbc_t ConnectionHandle, c_orm_sqlhwnd_t WindowHandle,
    c_orm_sqlwchar_t *InConnectionString, c_orm_sqlsmallint_t StringLength1,
    c_orm_sqlwchar_t *OutConnectionString, c_orm_sqlsmallint_t BufferLength,
    c_orm_sqlsmallint_t *StringLength2Ptr,
    c_orm_sqlusmallint_t DriverCompletion);

/**
 * @brief Connects to a DSN using ANSI server, user, and password strings.
 * @param ConnectionHandle Target ODBC connection handle.
 * @param ServerName Data source name string.
 * @param NameLength1 Length of ServerName or SQL_NTS.
 * @param UserName User identifier string.
 * @param NameLength2 Length of UserName or SQL_NTS.
 * @param Authentication Password or authentication token string.
 * @param NameLength3 Length of Authentication or SQL_NTS.
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API
SQLConnect(c_orm_sqlhdbc_t ConnectionHandle, c_orm_sqlchar_t *ServerName,
           c_orm_sqlsmallint_t NameLength1, c_orm_sqlchar_t *UserName,
           c_orm_sqlsmallint_t NameLength2, c_orm_sqlchar_t *Authentication,
           c_orm_sqlsmallint_t NameLength3);

/**
 * @brief Closes the connection associated with an ODBC connection handle.
 * @param ConnectionHandle Target ODBC connection handle.
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API
SQLDisconnect(c_orm_sqlhdbc_t ConnectionHandle);

/**
 * @brief Commits or rolls back an active transaction on an ODBC handle.
 * @param HandleType Handle type (environment or connection).
 * @param Handle Target environment or connection handle.
 * @param CompletionType SQL_COMMIT or SQL_ROLLBACK.
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API
SQLEndTran(c_orm_sqlsmallint_t HandleType, c_orm_sqlhandle_t Handle,
           c_orm_sqlsmallint_t CompletionType);

/**
 * @brief Sets an attribute on an ODBC statement handle.
 * @param StatementHandle Target ODBC statement handle.
 * @param Attribute Statement attribute identifier.
 * @param ValuePtr Attribute value pointer or integer cast.
 * @param StringLength Byte length of ValuePtr or indicator flag.
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API
SQLSetStmtAttr(c_orm_sqlhstmt_t StatementHandle, c_orm_sqlinteger_t Attribute,
               c_orm_sqlpointer_t ValuePtr, c_orm_sqlinteger_t StringLength);

/**
 * @brief Prepares an ANSI SQL statement string for execution.
 * @param StatementHandle Target ODBC statement handle.
 * @param StatementText ANSI SQL text buffer.
 * @param TextLength Length of StatementText or SQL_NTS.
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API
SQLPrepare(c_orm_sqlhstmt_t StatementHandle, c_orm_sqlchar_t *StatementText,
           c_orm_sqlinteger_t TextLength);

/**
 * @brief Prepares a UTF-16 SQL statement string for execution.
 * @param StatementHandle Target ODBC statement handle.
 * @param StatementText UTF-16 SQL text buffer.
 * @param TextLength Length of StatementText in characters or SQL_NTS.
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API
SQLPrepareW(c_orm_sqlhstmt_t StatementHandle, c_orm_sqlwchar_t *StatementText,
            c_orm_sqlinteger_t TextLength);

/**
 * @brief Executes a prepared ODBC statement.
 * @param StatementHandle Target ODBC statement handle.
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API
SQLExecute(c_orm_sqlhstmt_t StatementHandle);

/**
 * @brief Directly executes an ANSI SQL statement string.
 * @param StatementHandle Target ODBC statement handle.
 * @param StatementText ANSI SQL text buffer.
 * @param TextLength Length of StatementText or SQL_NTS.
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API
SQLExecDirect(c_orm_sqlhstmt_t StatementHandle, c_orm_sqlchar_t *StatementText,
              c_orm_sqlinteger_t TextLength);

/**
 * @brief Binds a parameter buffer to a parameter marker in an SQL statement.
 * @param StatementHandle Target ODBC statement handle.
 * @param ParameterNumber 1-based parameter ordinal.
 * @param InputOutputType Parameter direction (e.g. SQL_PARAM_INPUT).
 * @param ValueType C data type identifier of ParameterValuePtr.
 * @param ParameterType SQL data type identifier of the parameter marker.
 * @param ColumnSize Precision or column size of the parameter marker.
 * @param DecimalDigits Scale of the parameter marker.
 * @param ParameterValuePtr Pointer to parameter data buffer.
 * @param BufferLength Byte capacity of ParameterValuePtr.
 * @param StrLen_or_IndPtr Pointer to byte length or indicator (e.g.
 * SQL_NULL_DATA).
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API SQLBindParameter(
    c_orm_sqlhstmt_t StatementHandle, c_orm_sqlusmallint_t ParameterNumber,
    c_orm_sqlsmallint_t InputOutputType, c_orm_sqlsmallint_t ValueType,
    c_orm_sqlsmallint_t ParameterType, c_orm_sqlulen_t ColumnSize,
    c_orm_sqlsmallint_t DecimalDigits, c_orm_sqlpointer_t ParameterValuePtr,
    c_orm_sqllen_t BufferLength, c_orm_sqllen_t *StrLen_or_IndPtr);

/**
 * @brief Returns the number of columns in a result set.
 * @param StatementHandle Target ODBC statement handle.
 * @param ColumnCountPtr Output pointer receiving result column count.
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API SQLNumResultCols(
    c_orm_sqlhstmt_t StatementHandle, c_orm_sqlsmallint_t *ColumnCountPtr);

/**
 * @brief Describes an ANSI result column's name, type, size, scale, and
 * nullability.
 * @param StatementHandle Target ODBC statement handle.
 * @param ColumnNumber 1-based column ordinal.
 * @param ColumnName Output buffer receiving ANSI column name.
 * @param BufferLength Capacity of ColumnName buffer.
 * @param NameLengthPtr Output pointer receiving column name length.
 * @param DataTypePtr Output pointer receiving SQL data type code.
 * @param ColumnSizePtr Output pointer receiving column precision/size.
 * @param DecimalDigitsPtr Output pointer receiving decimal scale.
 * @param NullablePtr Output pointer receiving nullability indicator.
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API SQLDescribeCol(
    c_orm_sqlhstmt_t StatementHandle, c_orm_sqlusmallint_t ColumnNumber,
    c_orm_sqlchar_t *ColumnName, c_orm_sqlsmallint_t BufferLength,
    c_orm_sqlsmallint_t *NameLengthPtr, c_orm_sqlsmallint_t *DataTypePtr,
    c_orm_sqlulen_t *ColumnSizePtr, c_orm_sqlsmallint_t *DecimalDigitsPtr,
    c_orm_sqlsmallint_t *NullablePtr);

/**
 * @brief Describes a UTF-16 result column's metadata.
 * @param StatementHandle Target ODBC statement handle.
 * @param ColumnNumber 1-based column ordinal.
 * @param ColumnName Output buffer receiving UTF-16 column name.
 * @param BufferLength Capacity of ColumnName in characters.
 * @param NameLengthPtr Output pointer receiving column name character count.
 * @param DataTypePtr Output pointer receiving SQL data type code.
 * @param ColumnSizePtr Output pointer receiving column precision/size.
 * @param DecimalDigitsPtr Output pointer receiving decimal scale.
 * @param NullablePtr Output pointer receiving nullability indicator.
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API SQLDescribeColW(
    c_orm_sqlhstmt_t StatementHandle, c_orm_sqlusmallint_t ColumnNumber,
    c_orm_sqlwchar_t *ColumnName, c_orm_sqlsmallint_t BufferLength,
    c_orm_sqlsmallint_t *NameLengthPtr, c_orm_sqlsmallint_t *DataTypePtr,
    c_orm_sqlulen_t *ColumnSizePtr, c_orm_sqlsmallint_t *DecimalDigitsPtr,
    c_orm_sqlsmallint_t *NullablePtr);

/**
 * @brief Returns descriptor information for a column in a result set.
 * @param StatementHandle Target ODBC statement handle.
 * @param ColumnNumber 1-based column ordinal.
 * @param FieldIdentifier Descriptor field identifier.
 * @param CharacterAttributePtr Output buffer for string attributes.
 * @param BufferLength Capacity of CharacterAttributePtr in bytes.
 * @param StringLengthPtr Output pointer receiving byte length.
 * @param NumericAttributePtr Output pointer receiving numeric attribute.
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API SQLColAttribute(
    c_orm_sqlhstmt_t StatementHandle, c_orm_sqlusmallint_t ColumnNumber,
    c_orm_sqlusmallint_t FieldIdentifier,
    c_orm_sqlpointer_t CharacterAttributePtr, c_orm_sqlsmallint_t BufferLength,
    c_orm_sqlsmallint_t *StringLengthPtr, c_orm_sqllen_t *NumericAttributePtr);

/**
 * @brief Fetches the next rowset of data from a result set.
 * @param StatementHandle Target ODBC statement handle.
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API
SQLFetch(c_orm_sqlhstmt_t StatementHandle);

/**
 * @brief Retrieves data for a single column in the current result row.
 * @param StatementHandle Target ODBC statement handle.
 * @param Col_or_Param_Num 1-based column ordinal.
 * @param TargetType Desired C data type identifier.
 * @param TargetValuePtr Output buffer receiving converted column value.
 * @param BufferLength Byte capacity of TargetValuePtr.
 * @param StrLen_or_IndPtr Output pointer receiving byte length or
 * SQL_NULL_DATA.
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API SQLGetData(
    c_orm_sqlhstmt_t StatementHandle, c_orm_sqlusmallint_t Col_or_Param_Num,
    c_orm_sqlsmallint_t TargetType, c_orm_sqlpointer_t TargetValuePtr,
    c_orm_sqllen_t BufferLength, c_orm_sqllen_t *StrLen_or_IndPtr);

/**
 * @brief Sends part or all of a data-at-execution parameter value.
 * @param StatementHandle Target ODBC statement handle.
 * @param DataPtr Input chunk buffer pointer.
 * @param StrLen_or_Ind Chunk byte length or SQL_NULL_DATA.
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API
SQLPutData(c_orm_sqlhstmt_t StatementHandle, c_orm_sqlpointer_t DataPtr,
           c_orm_sqllen_t StrLen_or_Ind);

/**
 * @brief Advances data-at-execution parameter streaming to the next parameter.
 * @param StatementHandle Target ODBC statement handle.
 * @param ValuePtrPtr Output pointer receiving application parameter token.
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API
SQLParamData(c_orm_sqlhstmt_t StatementHandle, c_orm_sqlpointer_t *ValuePtrPtr);

/**
 * @brief Returns the number of rows affected by an UPDATE, INSERT, or DELETE.
 * @param StatementHandle Target ODBC statement handle.
 * @param RowCountPtr Output pointer receiving affected row count.
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API
SQLRowCount(c_orm_sqlhstmt_t StatementHandle, c_orm_sqllen_t *RowCountPtr);

/**
 * @brief Determines whether more result sets are available on a statement.
 * @param StatementHandle Target ODBC statement handle.
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API
SQLMoreResults(c_orm_sqlhstmt_t StatementHandle);

/**
 * @brief Stops processing associated with a statement, closes cursors, or
 * unbinds.
 * @param StatementHandle Target ODBC statement handle.
 * @param Option Operation code (e.g. SQL_CLOSE, SQL_RESET_PARAMS).
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API
SQLFreeStmt(c_orm_sqlhstmt_t StatementHandle, c_orm_sqlusmallint_t Option);

/**
 * @brief Closes an open cursor on a statement handle and discards pending rows.
 * @param StatementHandle Target ODBC statement handle.
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API
SQLCloseCursor(c_orm_sqlhstmt_t StatementHandle);

/**
 * @brief Retrieves ANSI diagnostic record fields for an ODBC handle.
 * @param HandleType Type of handle being inspected.
 * @param Handle Target ODBC handle.
 * @param RecNumber 1-based diagnostic record number.
 * @param SQLState Output 5-character ANSI SQLSTATE buffer.
 * @param NativeErrorPtr Output pointer receiving driver native error code.
 * @param MessageText Output buffer receiving diagnostic message text.
 * @param BufferLength Capacity of MessageText buffer.
 * @param TextLengthPtr Output pointer receiving message length.
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API SQLGetDiagRec(
    c_orm_sqlsmallint_t HandleType, c_orm_sqlhandle_t Handle,
    c_orm_sqlsmallint_t RecNumber, c_orm_sqlchar_t *SQLState,
    c_orm_sqlinteger_t *NativeErrorPtr, c_orm_sqlchar_t *MessageText,
    c_orm_sqlsmallint_t BufferLength, c_orm_sqlsmallint_t *TextLengthPtr);

/**
 * @brief Retrieves UTF-16 diagnostic record fields for an ODBC handle.
 * @param HandleType Type of handle being inspected.
 * @param Handle Target ODBC handle.
 * @param RecNumber 1-based diagnostic record number.
 * @param SQLState Output 5-character UTF-16 SQLSTATE buffer.
 * @param NativeErrorPtr Output pointer receiving driver native error code.
 * @param MessageText Output buffer receiving UTF-16 diagnostic message text.
 * @param BufferLength Capacity of MessageText in characters.
 * @param TextLengthPtr Output pointer receiving message character length.
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API SQLGetDiagRecW(
    c_orm_sqlsmallint_t HandleType, c_orm_sqlhandle_t Handle,
    c_orm_sqlsmallint_t RecNumber, c_orm_sqlwchar_t *SQLState,
    c_orm_sqlinteger_t *NativeErrorPtr, c_orm_sqlwchar_t *MessageText,
    c_orm_sqlsmallint_t BufferLength, c_orm_sqlsmallint_t *TextLengthPtr);

/**
 * @brief Returns general driver and data source information for a connection.
 * @param ConnectionHandle Target ODBC connection handle.
 * @param InfoType Information type code.
 * @param InfoValuePtr Output buffer receiving requested information.
 * @param BufferLength Byte capacity of InfoValuePtr.
 * @param StringLengthPtr Output pointer receiving returned byte count.
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API
SQLGetInfo(c_orm_sqlhdbc_t ConnectionHandle, c_orm_sqlusmallint_t InfoType,
           c_orm_sqlpointer_t InfoValuePtr, c_orm_sqlsmallint_t BufferLength,
           c_orm_sqlsmallint_t *StringLengthPtr);

/**
 * @brief Queries catalog table, view, or schema names into a result set.
 * @param StatementHandle Target ODBC statement handle.
 * @param CatalogName Catalog filter pattern or NULL.
 * @param NameLength1 Length of CatalogName or SQL_NTS.
 * @param SchemaName Schema filter pattern or NULL.
 * @param NameLength2 Length of SchemaName or SQL_NTS.
 * @param TableName Table name filter pattern or NULL.
 * @param NameLength3 Length of TableName or SQL_NTS.
 * @param TableType Table type list filter or NULL.
 * @param NameLength4 Length of TableType or SQL_NTS.
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API
SQLTables(c_orm_sqlhstmt_t StatementHandle, c_orm_sqlchar_t *CatalogName,
          c_orm_sqlsmallint_t NameLength1, c_orm_sqlchar_t *SchemaName,
          c_orm_sqlsmallint_t NameLength2, c_orm_sqlchar_t *TableName,
          c_orm_sqlsmallint_t NameLength3, c_orm_sqlchar_t *TableType,
          c_orm_sqlsmallint_t NameLength4);

/**
 * @brief Queries column metadata for tables matching catalog/schema filters.
 * @param StatementHandle Target ODBC statement handle.
 * @param CatalogName Catalog filter pattern or NULL.
 * @param NameLength1 Length of CatalogName or SQL_NTS.
 * @param SchemaName Schema filter pattern or NULL.
 * @param NameLength2 Length of SchemaName or SQL_NTS.
 * @param TableName Table name filter pattern or NULL.
 * @param NameLength3 Length of TableName or SQL_NTS.
 * @param ColumnName Column name filter pattern or NULL.
 * @param NameLength4 Length of ColumnName or SQL_NTS.
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API
SQLColumns(c_orm_sqlhstmt_t StatementHandle, c_orm_sqlchar_t *CatalogName,
           c_orm_sqlsmallint_t NameLength1, c_orm_sqlchar_t *SchemaName,
           c_orm_sqlsmallint_t NameLength2, c_orm_sqlchar_t *TableName,
           c_orm_sqlsmallint_t NameLength3, c_orm_sqlchar_t *ColumnName,
           c_orm_sqlsmallint_t NameLength4);

/**
 * @brief Queries primary-key column names for a specified table.
 * @param StatementHandle Target ODBC statement handle.
 * @param CatalogName Catalog name or NULL.
 * @param NameLength1 Length of CatalogName or SQL_NTS.
 * @param SchemaName Schema name or NULL.
 * @param NameLength2 Length of SchemaName or SQL_NTS.
 * @param TableName Table name string.
 * @param NameLength3 Length of TableName or SQL_NTS.
 * @return ODBC SQLRETURN status code.
 */
extern c_orm_sqlreturn_t C_ORM_SQL_API
SQLPrimaryKeys(c_orm_sqlhstmt_t StatementHandle, c_orm_sqlchar_t *CatalogName,
               c_orm_sqlsmallint_t NameLength1, c_orm_sqlchar_t *SchemaName,
               c_orm_sqlsmallint_t NameLength2, c_orm_sqlchar_t *TableName,
               c_orm_sqlsmallint_t NameLength3);
#endif

/**
 * @brief Descriptor mapping an ODBC/CLI symbol name to its struct offset in
 * `c_orm_odbc_api_t`.
 */
typedef struct c_orm_sym_desc {
  /** @brief ASCII symbol name exported by `libodbc`/`odbc32`/`libdb2`. */
  const char *name;
  /** @brief Byte offset of function pointer inside `c_orm_odbc_api_t`. */
  size_t offset;
} c_orm_sym_desc_t;

static const c_orm_odbc_api_t *g_odbc_mock_api = NULL;
static const c_orm_odbc_api_t *g_db2_mock_api = NULL;
static int g_db2_mock_is_direct = 0;

static c_orm_odbc_api_t g_odbc_dyn_api;
static int g_odbc_dyn_loaded = 0;
static void *g_odbc_lib_handle = NULL;

static c_orm_odbc_api_t g_db2_dyn_api;
static int g_db2_dyn_loaded = 0;
static void *g_db2_lib_handle = NULL;

#if !defined(C_ORM_NO_THREADS) && !defined(__EMSCRIPTEN__)
#if defined(_WIN32)
static volatile long g_odbc_loader_spinlock = 0;
#else
static pthread_mutex_t g_odbc_loader_mutex = PTHREAD_MUTEX_INITIALIZER;
#endif
#endif

#ifdef C_ORM_TEST_ALLOCATOR
static c_orm_odbc_dlopen_fn g_test_dlopen_fn = NULL;
static c_orm_odbc_sym_fn g_test_dlsym_fn = NULL;
static c_orm_odbc_dlclose_fn g_test_dlclose_fn = NULL;

C_ORM_EXPORT c_orm_error_t c_orm_odbc_set_test_dl_hooks(
    c_orm_odbc_dlopen_fn open_fn, c_orm_odbc_sym_fn sym_fn,
    c_orm_odbc_dlclose_fn close_fn) {
  g_test_dlopen_fn = open_fn;
  g_test_dlsym_fn = sym_fn;
  g_test_dlclose_fn = close_fn;
  return C_ORM_OK;
}
#endif /* C_ORM_TEST_ALLOCATOR */

#if !defined(C_ORM_NO_THREADS) && !defined(__EMSCRIPTEN__)
#if defined(_MSC_VER)
#define LOADER_LOCK()                                                          \
  do {                                                                         \
    while (_InterlockedCompareExchange(&g_odbc_loader_spinlock, 1L, 0L) !=     \
           0L) {                                                               \
    }                                                                          \
  } while (0)
#define LOADER_UNLOCK()                                                        \
  do {                                                                         \
    g_odbc_loader_spinlock = 0;                                                \
  } while (0)
#elif defined(_WIN32)
#define LOADER_LOCK()                                                          \
  do {                                                                         \
    while (__sync_val_compare_and_swap(&g_odbc_loader_spinlock, 0L, 1L) !=     \
           0L) {                                                               \
    }                                                                          \
  } while (0)
#define LOADER_UNLOCK()                                                        \
  do {                                                                         \
    g_odbc_loader_spinlock = 0;                                                \
  } while (0)
#else
#define LOADER_LOCK()                                                          \
  do {                                                                         \
    pthread_mutex_lock(&g_odbc_loader_mutex);                                  \
  } while (0)
#define LOADER_UNLOCK()                                                        \
  do {                                                                         \
    pthread_mutex_unlock(&g_odbc_loader_mutex);                                \
  } while (0)
#endif
#else
#define LOADER_LOCK()                                                          \
  do {                                                                         \
  } while (0)
#define LOADER_UNLOCK()                                                        \
  do {                                                                         \
  } while (0)
#endif

/**
 * @brief Dynamically opens a shared library by UTF-8 path without <windows.h>.
 * @param path Null-terminated UTF-8 library path or soname.
 * @param out_handle Output pointer receiving opaque OS module handle.
 * @return C_ORM_OK on success, or C_ORM_ERROR_DRIVER_LOAD on failure.
 */
static c_orm_error_t os_open_library(const char *path, void **out_handle) {
  void *h = NULL;
  *out_handle = NULL;
#ifdef C_ORM_TEST_ALLOCATOR
  if (g_test_dlopen_fn) {
    return g_test_dlopen_fn(path, out_handle);
  }
#endif
#if defined(_WIN32)
#if defined(UNICODE) || defined(_UNICODE)
  {
    unsigned short wpath[1024];
    size_t units = 0;
    c_orm_error_t urc =
        c_orm_utf8_to_utf16(path, strlen(path), 0, wpath, 1024, &units);
    if (urc != C_ORM_OK) {
      return (urc == C_ORM_ERROR_MEMORY) ? urc : C_ORM_ERROR_DRIVER_LOAD;
    }
    h = LoadLibraryW(wpath);
  }
#else
  h = LoadLibraryA(path);
#endif
#elif !defined(__EMSCRIPTEN__)
  h = dlopen(path, RTLD_LAZY);
#else
  if (!path) {
    return C_ORM_ERROR_DRIVER_LOAD;
  }
#endif
  if (!h) {
    return C_ORM_ERROR_DRIVER_LOAD;
  }
  *out_handle = h;
  return C_ORM_OK;
}

/**
 * @brief Resolves a symbol from an OS library handle.
 * @param handle Opaque OS module handle.
 * @param symbol_name Null-terminated ASCII symbol name.
 * @param out_fn Output pointer receiving resolved function address (or NULL).
 * @return C_ORM_OK on completion.
 */
static c_orm_error_t os_lookup_symbol(void *handle, const char *symbol_name,
                                      void **out_fn) {
#ifdef C_ORM_TEST_ALLOCATOR
  if (g_test_dlsym_fn) {
    return g_test_dlsym_fn(handle, symbol_name, out_fn);
  }
#endif
#if defined(_WIN32)
  *out_fn = GetProcAddress(handle, symbol_name);
#elif !defined(__EMSCRIPTEN__)
  *out_fn = dlsym(handle, symbol_name);
#else
  if (!handle || !symbol_name) {
    *out_fn = NULL;
    return C_ORM_ERROR_NULL_POINTER;
  }
  *out_fn = NULL;
#endif
  return C_ORM_OK;
}

/**
 * @brief Closes an OS shared library handle.
 * @param handle Non-NULL opaque OS module handle.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t os_close_library(void *handle) {
#ifdef C_ORM_TEST_ALLOCATOR
  if (g_test_dlclose_fn) {
    return g_test_dlclose_fn(handle);
  }
#endif
#if defined(_WIN32)
  FreeLibrary(handle);
#elif !defined(__EMSCRIPTEN__)
  dlclose(handle);
#endif
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_odbc_populate_api(c_orm_odbc_sym_fn lookup_fn,
                                                   void *handle,
                                                   c_orm_odbc_api_t *out_api) {
  static const c_orm_sym_desc_t syms[] = {
      {"SQLAllocHandle", offsetof(c_orm_odbc_api_t, SQLAllocHandle)},
      {"SQLFreeHandle", offsetof(c_orm_odbc_api_t, SQLFreeHandle)},
      {"SQLSetEnvAttr", offsetof(c_orm_odbc_api_t, SQLSetEnvAttr)},
      {"SQLSetConnectAttr", offsetof(c_orm_odbc_api_t, SQLSetConnectAttr)},
      {"SQLSetConnectAttrW", offsetof(c_orm_odbc_api_t, SQLSetConnectAttrW)},
      {"SQLGetConnectAttr", offsetof(c_orm_odbc_api_t, SQLGetConnectAttr)},
      {"SQLDriverConnect", offsetof(c_orm_odbc_api_t, SQLDriverConnect)},
      {"SQLDriverConnectW", offsetof(c_orm_odbc_api_t, SQLDriverConnectW)},
      {"SQLConnect", offsetof(c_orm_odbc_api_t, SQLConnect)},
      {"SQLDisconnect", offsetof(c_orm_odbc_api_t, SQLDisconnect)},
      {"SQLEndTran", offsetof(c_orm_odbc_api_t, SQLEndTran)},
      {"SQLSetStmtAttr", offsetof(c_orm_odbc_api_t, SQLSetStmtAttr)},
      {"SQLPrepare", offsetof(c_orm_odbc_api_t, SQLPrepare)},
      {"SQLPrepareW", offsetof(c_orm_odbc_api_t, SQLPrepareW)},
      {"SQLExecute", offsetof(c_orm_odbc_api_t, SQLExecute)},
      {"SQLExecDirect", offsetof(c_orm_odbc_api_t, SQLExecDirect)},
      {"SQLBindParameter", offsetof(c_orm_odbc_api_t, SQLBindParameter)},
      {"SQLNumResultCols", offsetof(c_orm_odbc_api_t, SQLNumResultCols)},
      {"SQLDescribeCol", offsetof(c_orm_odbc_api_t, SQLDescribeCol)},
      {"SQLDescribeColW", offsetof(c_orm_odbc_api_t, SQLDescribeColW)},
      {"SQLColAttribute", offsetof(c_orm_odbc_api_t, SQLColAttribute)},
      {"SQLFetch", offsetof(c_orm_odbc_api_t, SQLFetch)},
      {"SQLGetData", offsetof(c_orm_odbc_api_t, SQLGetData)},
      {"SQLPutData", offsetof(c_orm_odbc_api_t, SQLPutData)},
      {"SQLParamData", offsetof(c_orm_odbc_api_t, SQLParamData)},
      {"SQLRowCount", offsetof(c_orm_odbc_api_t, SQLRowCount)},
      {"SQLMoreResults", offsetof(c_orm_odbc_api_t, SQLMoreResults)},
      {"SQLFreeStmt", offsetof(c_orm_odbc_api_t, SQLFreeStmt)},
      {"SQLCloseCursor", offsetof(c_orm_odbc_api_t, SQLCloseCursor)},
      {"SQLGetDiagRec", offsetof(c_orm_odbc_api_t, SQLGetDiagRec)},
      {"SQLGetDiagRecW", offsetof(c_orm_odbc_api_t, SQLGetDiagRecW)},
      {"SQLGetInfo", offsetof(c_orm_odbc_api_t, SQLGetInfo)},
      {"SQLTables", offsetof(c_orm_odbc_api_t, SQLTables)},
      {"SQLColumns", offsetof(c_orm_odbc_api_t, SQLColumns)},
      {"SQLPrimaryKeys", offsetof(c_orm_odbc_api_t, SQLPrimaryKeys)},
      {"SQLGetLength", offsetof(c_orm_odbc_api_t, SQLGetLength)},
      {"SQLGetSubString", offsetof(c_orm_odbc_api_t, SQLGetSubString)},
      {"SQLGetPosition", offsetof(c_orm_odbc_api_t, SQLGetPosition)}};
  size_t i;
  c_orm_error_t rc;

  if (!lookup_fn || !out_api) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  memset(out_api, 0, sizeof(*out_api));

  for (i = 0; i < sizeof(syms) / sizeof(syms[0]); i++) {
    void *fn_ptr = NULL;
    rc = lookup_fn(handle, syms[i].name, &fn_ptr);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_odbc_populate_api: symbol lookup error");
      memset(out_api, 0, sizeof(*out_api));
      return rc;
    }
    memcpy(((unsigned char *)out_api) + syms[i].offset, &fn_ptr,
           sizeof(fn_ptr));
  }

  if (!out_api->SQLAllocHandle || !out_api->SQLFreeHandle ||
      !out_api->SQLSetEnvAttr || !out_api->SQLDisconnect ||
      !out_api->SQLEndTran || !out_api->SQLExecute ||
      !out_api->SQLBindParameter || !out_api->SQLNumResultCols ||
      !out_api->SQLFetch || !out_api->SQLGetData || !out_api->SQLFreeStmt ||
      (!out_api->SQLDriverConnect && !out_api->SQLDriverConnectW &&
       !out_api->SQLConnect) ||
      (!out_api->SQLPrepare && !out_api->SQLPrepareW) ||
      (!out_api->SQLGetDiagRec && !out_api->SQLGetDiagRecW)) {
    memset(out_api, 0, sizeof(*out_api));
    return C_ORM_ERROR_DRIVER_LOAD;
  }

  return C_ORM_OK;
}

/**
 * @brief Closes a dynamic library handle during error cleanup or loader reset.
 * @param h Open dynamic library handle.
 * @return C_ORM_OK on success, or OS close error code.
 */
static c_orm_error_t loader_lib_cleanup_free(void *h) {
  return os_close_library(h);
}

/**
 * @brief Reads an optional environment variable into `buf`.
 * @param buf Destination buffer.
 * @param cap Buffer capacity in bytes.
 * @param name Environment variable name.
 * @return 1 if present and non-empty, 0 otherwise.
 */
static int loader_calc_getenv_opt(char *buf, size_t cap, const char *name) {
  buf[0] = '\0';
  if (C_ORM_GETENV(buf, cap, name) != C_ORM_OK) {
    buf[0] = '\0';
    return 0;
  }
  return (buf[0] != '\0') ? 1 : 0;
}

/**
 * @brief Attempts to open a candidate shared library and populate out_api.
 * @param candidate Library path or soname.
 * @param out_handle Output pointer receiving opened handle on success.
 * @param out_api Output dispatch table populated on success.
 * @return C_ORM_OK on success, or C_ORM_ERROR_DRIVER_LOAD on failure.
 */
static c_orm_error_t try_load_candidate(const char *candidate,
                                        void **out_handle,
                                        c_orm_odbc_api_t *out_api) {
  void *h = NULL;
  c_orm_error_t rc;

  rc = os_open_library(candidate, &h);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = c_orm_odbc_populate_api(os_lookup_symbol, h, out_api);
  if (rc != C_ORM_OK) {
    loader_lib_cleanup_free(h);
    return rc;
  }
  *out_handle = h;
  return C_ORM_OK;
}

/**
 * @brief Probes a single library candidate and returns 1 on successful load.
 * @param candidate Library filename or path.
 * @param out_handle Output handle pointer on success.
 * @param out_api Output populated API dispatch table.
 * @return 1 if loaded and populated, 0 otherwise.
 */
static int loader_calc_try_candidate(const char *candidate, void **out_handle,
                                     c_orm_odbc_api_t *out_api) {
  return (try_load_candidate(candidate, out_handle, out_api) == C_ORM_OK) ? 1
                                                                          : 0;
}

C_ORM_EXPORT c_orm_error_t
c_orm_odbc_set_mock_api(const c_orm_odbc_api_t *mock_api) {
  LOADER_LOCK();
  g_odbc_mock_api = mock_api;
  LOADER_UNLOCK();
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t
c_orm_db2_set_mock_api(const c_orm_odbc_api_t *mock_api, int is_direct_cli) {
  LOADER_LOCK();
  g_db2_mock_api = mock_api;
  g_db2_mock_is_direct = is_direct_cli ? 1 : 0;
  LOADER_UNLOCK();
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_odbc_reset_loader(void) {
  LOADER_LOCK();
  g_odbc_mock_api = NULL;
  g_db2_mock_api = NULL;
  g_db2_mock_is_direct = 0;
  if (g_odbc_lib_handle) {
    void *oh = g_odbc_lib_handle;
    g_odbc_lib_handle = NULL;
    loader_lib_cleanup_free(oh);
  }
  memset(&g_odbc_dyn_api, 0, sizeof(g_odbc_dyn_api));
  g_odbc_dyn_loaded = 0;
  if (g_db2_lib_handle) {
    void *dh = g_db2_lib_handle;
    g_db2_lib_handle = NULL;
    loader_lib_cleanup_free(dh);
  }
  memset(&g_db2_dyn_api, 0, sizeof(g_db2_dyn_api));
  g_db2_dyn_loaded = 0;
  LOADER_UNLOCK();
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t
c_orm_odbc_load_driver_manager(const c_orm_odbc_api_t **out_api) {
  c_orm_error_t rc;
  char env_lib[512];
  size_t i;
  static const char *const candidates[] = {
#if defined(_WIN32)
      "odbc32.dll",
#elif defined(__APPLE__)
      "libodbc.2.dylib",
      "libodbc.dylib",
      "/opt/homebrew/lib/libodbc.dylib",
      "/usr/local/lib/libodbc.dylib",
      "libiodbc.2.dylib",
      "libiodbc.dylib",
#else
      "libodbc.so.2",
      "libodbc.so.1",
      "libodbc.so",
      "cygodbc-2.dll",
      "libiodbc.so.2",
#endif
      NULL};

  if (!out_api) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  *out_api = NULL;

  LOADER_LOCK();

  if (g_odbc_mock_api) {
    *out_api = g_odbc_mock_api;
    LOADER_UNLOCK();
    return C_ORM_OK;
  }

  if (g_odbc_dyn_loaded) {
    *out_api = &g_odbc_dyn_api;
    LOADER_UNLOCK();
    return C_ORM_OK;
  }

#if defined(C_ORM_LINK_ODBC)
  g_odbc_dyn_api.SQLAllocHandle = SQLAllocHandle;
  g_odbc_dyn_api.SQLFreeHandle = SQLFreeHandle;
  g_odbc_dyn_api.SQLSetEnvAttr = SQLSetEnvAttr;
  g_odbc_dyn_api.SQLSetConnectAttr = SQLSetConnectAttr;
  g_odbc_dyn_api.SQLSetConnectAttrW = SQLSetConnectAttrW;
  g_odbc_dyn_api.SQLGetConnectAttr = SQLGetConnectAttr;
  g_odbc_dyn_api.SQLDriverConnect = SQLDriverConnect;
  g_odbc_dyn_api.SQLDriverConnectW = SQLDriverConnectW;
  g_odbc_dyn_api.SQLConnect = SQLConnect;
  g_odbc_dyn_api.SQLDisconnect = SQLDisconnect;
  g_odbc_dyn_api.SQLEndTran = SQLEndTran;
  g_odbc_dyn_api.SQLSetStmtAttr = SQLSetStmtAttr;
  g_odbc_dyn_api.SQLPrepare = SQLPrepare;
  g_odbc_dyn_api.SQLPrepareW = SQLPrepareW;
  g_odbc_dyn_api.SQLExecute = SQLExecute;
  g_odbc_dyn_api.SQLExecDirect = SQLExecDirect;
  g_odbc_dyn_api.SQLBindParameter = SQLBindParameter;
  g_odbc_dyn_api.SQLNumResultCols = SQLNumResultCols;
  g_odbc_dyn_api.SQLDescribeCol = SQLDescribeCol;
  g_odbc_dyn_api.SQLDescribeColW = SQLDescribeColW;
  g_odbc_dyn_api.SQLColAttribute = SQLColAttribute;
  g_odbc_dyn_api.SQLFetch = SQLFetch;
  g_odbc_dyn_api.SQLGetData = SQLGetData;
  g_odbc_dyn_api.SQLPutData = SQLPutData;
  g_odbc_dyn_api.SQLParamData = SQLParamData;
  g_odbc_dyn_api.SQLRowCount = SQLRowCount;
  g_odbc_dyn_api.SQLMoreResults = SQLMoreResults;
  g_odbc_dyn_api.SQLFreeStmt = SQLFreeStmt;
  g_odbc_dyn_api.SQLCloseCursor = SQLCloseCursor;
  g_odbc_dyn_api.SQLGetDiagRec = SQLGetDiagRec;
  g_odbc_dyn_api.SQLGetDiagRecW = SQLGetDiagRecW;
  g_odbc_dyn_api.SQLGetInfo = SQLGetInfo;
  g_odbc_dyn_api.SQLTables = SQLTables;
  g_odbc_dyn_api.SQLColumns = SQLColumns;
  g_odbc_dyn_api.SQLPrimaryKeys = SQLPrimaryKeys;
  g_odbc_dyn_loaded = 1;
  *out_api = &g_odbc_dyn_api;
  LOADER_UNLOCK();
  return C_ORM_OK;
#endif

  if (loader_calc_getenv_opt(env_lib, sizeof(env_lib), "C_ORM_ODBC_LIB")) {
    rc = try_load_candidate(env_lib, &g_odbc_lib_handle, &g_odbc_dyn_api);
    if (rc != C_ORM_OK) {
      LOADER_UNLOCK();
      return rc;
    }
    g_odbc_dyn_loaded = 1;
    *out_api = &g_odbc_dyn_api;
    LOADER_UNLOCK();
    return C_ORM_OK;
  }

  for (i = 0; candidates[i] != NULL; i++) {
    if (loader_calc_try_candidate(candidates[i], &g_odbc_lib_handle,
                                  &g_odbc_dyn_api)) {
      g_odbc_dyn_loaded = 1;
      *out_api = &g_odbc_dyn_api;
      LOADER_UNLOCK();
      return C_ORM_OK;
    }
  }

  LOADER_UNLOCK();
  return C_ORM_ERROR_DRIVER_LOAD;
}

C_ORM_EXPORT c_orm_error_t c_orm_db2_load_cli(const c_orm_odbc_api_t **out_api,
                                              int *out_is_direct_db2_cli) {
  c_orm_error_t rc;
  char env_lib[512];
  char db2_home[512];
  char home_candidate[1024];
  size_t i;
  static const char *const db2_candidates[] = {
#if defined(_WIN32)
      "db2cli64.dll", "db2app64.dll", "db2cli.dll",
#elif defined(__APPLE__)
      "libdb2.dylib",
#else
      "libdb2.so.1", "libdb2.so",
#endif
      NULL};

  if (!out_api) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  *out_api = NULL;
  if (out_is_direct_db2_cli) {
    *out_is_direct_db2_cli = 0;
  }

  LOADER_LOCK();

  if (g_db2_mock_api) {
    *out_api = g_db2_mock_api;
    if (out_is_direct_db2_cli) {
      *out_is_direct_db2_cli = g_db2_mock_is_direct;
    }
    LOADER_UNLOCK();
    return C_ORM_OK;
  }

  if (g_db2_dyn_loaded) {
    *out_api = &g_db2_dyn_api;
    if (out_is_direct_db2_cli) {
      *out_is_direct_db2_cli = 1;
    }
    LOADER_UNLOCK();
    return C_ORM_OK;
  }

  if (loader_calc_getenv_opt(env_lib, sizeof(env_lib), "C_ORM_DB2_LIB")) {
    rc = try_load_candidate(env_lib, &g_db2_lib_handle, &g_db2_dyn_api);
    if (rc != C_ORM_OK) {
      LOADER_UNLOCK();
      return rc;
    }
    g_db2_dyn_loaded = 1;
    *out_api = &g_db2_dyn_api;
    if (out_is_direct_db2_cli) {
      *out_is_direct_db2_cli = 1;
    }
    LOADER_UNLOCK();
    return C_ORM_OK;
  }

  if (loader_calc_getenv_opt(db2_home, sizeof(db2_home), "IBM_DB_HOME")) {
#if defined(_WIN32)
    rc = C_ORM_SPRINTF(home_candidate, sizeof(home_candidate),
                       "%s/bin/db2cli64.dll", db2_home);
#elif defined(__APPLE__)
    rc = C_ORM_SPRINTF(home_candidate, sizeof(home_candidate),
                       "%s/lib/libdb2.dylib", db2_home);
#else
    rc = C_ORM_SPRINTF(home_candidate, sizeof(home_candidate),
                       "%s/lib64/libdb2.so", db2_home);
#endif
    if (rc != C_ORM_OK) {
      LOADER_UNLOCK();
      return rc;
    }
    if (loader_calc_try_candidate(home_candidate, &g_db2_lib_handle,
                                  &g_db2_dyn_api)) {
      g_db2_dyn_loaded = 1;
      *out_api = &g_db2_dyn_api;
      if (out_is_direct_db2_cli) {
        *out_is_direct_db2_cli = 1;
      }
      LOADER_UNLOCK();
      return C_ORM_OK;
    }
  }

  for (i = 0; db2_candidates[i] != NULL; i++) {
    if (loader_calc_try_candidate(db2_candidates[i], &g_db2_lib_handle,
                                  &g_db2_dyn_api)) {
      g_db2_dyn_loaded = 1;
      *out_api = &g_db2_dyn_api;
      if (out_is_direct_db2_cli) {
        *out_is_direct_db2_cli = 1;
      }
      LOADER_UNLOCK();
      return C_ORM_OK;
    }
  }

  LOADER_UNLOCK();
  return c_orm_odbc_load_driver_manager(out_api);
}
