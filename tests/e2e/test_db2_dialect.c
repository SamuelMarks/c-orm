/**
 * @file test_db2_dialect.c
 * @brief Exhaustive test suite for IBM Db2 and Generic ODBC SQL dialects across
 * AST rendering, fluent queries, dynamic query builders, MERGE INTO upserts,
 * FINAL/OLD TABLE returning, DDL parsing, SQL-to-C/C-to-SQL codegen, catalog
 * introspection (`SYSCAT.COLUMNS`, `SYSIBM.SYSCOLUMNS`, `SQLColumns`), and
 * schema migrations.
 */

/* clang-format off */
#include "abstract_struct.h"
#include "c_orm_api.h"
#include "c_orm_ast.h"
#include "c_orm_c_to_sql.h"
#include "c_orm_db2.h"
#include "c_orm_decimal.h"
#include "c_orm_inline_macros.h"
#include "c_orm_memory.h"
#include "c_orm_migrations.h"
#include "c_orm_odbc.h"
#include "c_orm_odbc_abi.h"
#include "c_orm_query_builder.h"
#include "c_orm_safe_crt.h"
#include "c_orm_sql.h"
#include "c_orm_sql_to_c.h"
#include "c_orm_sqlite.h"
#include "c_orm_string_builder.h"
#include "c_orm_struct.h"
#include "greatest.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

/**
 * @brief Test model for Db2/ODBC CRUD, MERGE upsert, and FINAL TABLE insert
 * hydration.
 * @var id Primary key identity column.
 * @var code Account code string.
 * @var amount Exact fixed-point balance DECIMAL(15, 2).
 */
typedef struct DialectLedger {
  /** @brief Primary key identity column. */
  int32_t id;
  /** @brief Account code string. */
  char *code;
  /** @brief Exact fixed-point balance DECIMAL(15, 2). */
  c_orm_decimal_t amount;
} DialectLedger;

#define DQ "\042"

static const c_orm_column_meta_t DialectLedger_cols[] = {
    C_ORM_DEFINE_COLUMN_EX("id", C_ORM_TYPE_INT32,
                           offsetof(struct DialectLedger, id), true, false,
                           NULL, false, false, 0, 0, 0, 0),
    C_ORM_DEFINE_COLUMN_EX("code", C_ORM_TYPE_STRING,
                           offsetof(struct DialectLedger, code), false, false,
                           NULL, false, false, 0, 0, 0, 0),
    C_ORM_COL_DECIMAL(DialectLedger, amount, 15, 2, false)};

static const c_orm_table_meta_t DialectLedger_meta = {
    "LEDGER",
    DialectLedger_cols,
    3,
    sizeof(struct DialectLedger),
    "SELECT id, code, amount FROM LEDGER",
    "SELECT id, code, amount FROM LEDGER WHERE id = ?",
    "INSERT INTO LEDGER (id, code, amount) VALUES (?, ?, ?)",
    "UPDATE LEDGER SET code = ?, amount = ? WHERE id = ?",
    "DELETE FROM LEDGER WHERE id = ?",
    NULL,
    false,
    false,
    0,
    0,
    {0},
    NULL,
    0};

/**
 * @brief Scripted state for dialect mock vtable and ODBC/Db2 CLI catalog mock.
 * @var last_sql Last prepared or executed SQL text.
 * @var prev_sql Previously prepared SQL text.
 * @var bound_dec Last decimal bound via `bind_decimal`.
 * @var bound_str Last string bound via `bind_string`.
 * @var bind_dec_calls Number of `bind_decimal` invocations.
 * @var cli_begins Number of `SQL_AUTOCOMMIT_OFF` calls on CLI mock.
 * @var cli_commits Number of `SQLEndTran(SQL_COMMIT)` calls on CLI mock.
 * @var final_table_id ID returned from `FINAL TABLE` row (`0` to test
 * fallback).
 * @var identity_val_id ID returned from `get_last_insert_rowid`.
 * @var catalog_mode `0` = LUW `SYSCAT.COLUMNS`, `1` = z/OS `SYSIBM.SYSCOLUMNS`,
 * `2` = fallback to `SQLColumns`.
 * @var step_row Current 1-based row cursor.
 */
typedef struct dialect_mock_state {
  /** @brief Last prepared or executed SQL text. */
  char last_sql[1024];
  /** @brief Previously prepared SQL text. */
  char prev_sql[1024];
  /** @brief Last decimal bound via `bind_decimal`. */
  c_orm_decimal_t bound_dec;
  /** @brief Last string bound via `bind_string`. */
  char bound_str[128];
  /** @brief Number of `bind_decimal` invocations. */
  int bind_dec_calls;
  /** @brief Number of `SQL_AUTOCOMMIT_OFF` calls on CLI mock. */
  int cli_begins;
  /** @brief Number of `SQLEndTran(SQL_COMMIT)` calls on CLI mock. */
  int cli_commits;
  /** @brief ID returned from `FINAL TABLE` row (`0` to test fallback). */
  int64_t final_table_id;
  /** @brief ID returned from `get_last_insert_rowid`. */
  int64_t identity_val_id;
  /** @brief Catalog mode (`0`=LUW, `1`=z/OS, `2`=SQLColumns). */
  int catalog_mode;
  /** @brief Current 1-based row cursor. */
  int step_row;
} dialect_mock_state_t;

static dialect_mock_state_t g_dm;
static int g_dummy_stmt = 77;
static int g_cli_env = 101;
static int g_cli_dbc = 102;
static int g_cli_stmt = 103;
static int g_cli_col_row = 0;

/**
 * @brief Resets `g_dm` mock state.
 * @return C_ORM_OK.
 */
static c_orm_error_t reset_dialect_mock(void) {
  memset(&g_dm, 0, sizeof(g_dm));
  g_cli_col_row = 0;
  return C_ORM_OK;
}

/**
 * @brief Mock vtable prepare callback.
 * @param db Database connection.
 * @param sql SQL string.
 * @param out_query Output statement pointer.
 * @return C_ORM_OK or C_ORM_ERROR_SQL.
 */
static c_orm_error_t dm_prepare(c_orm_db_t *db, const char *sql,
                                c_orm_query_t **out_query) {
  (void)db;
  (void)C_ORM_STRCPY(g_dm.prev_sql, sizeof(g_dm.prev_sql), g_dm.last_sql);
  (void)C_ORM_STRCPY(g_dm.last_sql, sizeof(g_dm.last_sql), sql ? sql : "");
  g_dm.step_row = 0;
  if (strstr(g_dm.last_sql, "SYSCAT.COLUMNS") != NULL &&
      g_dm.catalog_mode != 0) {
    return C_ORM_ERROR_SQL;
  }
  if (strstr(g_dm.last_sql, "SYSIBM.SYSCOLUMNS") != NULL &&
      g_dm.catalog_mode != 1) {
    return C_ORM_ERROR_SQL;
  }
  *out_query = (c_orm_query_t *)&g_dummy_stmt;
  return C_ORM_OK;
}

/**
 * @brief Mock vtable step callback.
 * @param query Statement handle.
 * @param out_has_row Output row indicator.
 * @return C_ORM_OK.
 */
static c_orm_error_t dm_step(c_orm_query_t *query, int *out_has_row) {
  (void)query;
  g_dm.step_row++;
  if (strstr(g_dm.last_sql, "FINAL TABLE") != NULL && g_dm.final_table_id > 0 &&
      g_dm.step_row == 1) {
    *out_has_row = 1;
    return C_ORM_OK;
  }
  if ((strstr(g_dm.last_sql, "SYSCAT.COLUMNS") != NULL ||
       strstr(g_dm.last_sql, "SYSIBM.SYSCOLUMNS") != NULL) &&
      g_dm.step_row <= 2) {
    *out_has_row = 1;
    return C_ORM_OK;
  }
  *out_has_row = 0;
  return C_ORM_OK;
}

/**
 * @brief Mock vtable reset callback.
 * @param query Statement handle.
 * @return C_ORM_OK.
 */
static c_orm_error_t dm_reset(c_orm_query_t *query) {
  (void)query;
  g_dm.step_row = 0;
  return C_ORM_OK;
}

/**
 * @brief Mock vtable finalize callback.
 * @param query Statement handle.
 * @return C_ORM_OK.
 */
static c_orm_error_t dm_finalize(c_orm_query_t *query) {
  (void)query;
  return C_ORM_OK;
}

/**
 * @brief Mock vtable bind_int32 callback.
 * @param query Statement handle.
 * @param index 1-based parameter index.
 * @param val Bound value.
 * @return C_ORM_OK.
 */
static c_orm_error_t dm_bind_int32(c_orm_query_t *query, int index,
                                   int32_t val) {
  (void)query;
  (void)index;
  (void)val;
  return C_ORM_OK;
}

/**
 * @brief Mock vtable bind_int64 callback.
 * @param query Statement handle.
 * @param index 1-based parameter index.
 * @param val Bound value.
 * @return C_ORM_OK.
 */
static c_orm_error_t dm_bind_int64(c_orm_query_t *query, int index,
                                   int64_t val) {
  (void)query;
  (void)index;
  (void)val;
  return C_ORM_OK;
}

/**
 * @brief Mock vtable bind_double callback.
 * @param query Statement handle.
 * @param index 1-based parameter index.
 * @param val Bound value.
 * @return C_ORM_OK.
 */
static c_orm_error_t dm_bind_double(c_orm_query_t *query, int index,
                                    double val) {
  (void)query;
  (void)index;
  (void)val;
  return C_ORM_OK;
}

/**
 * @brief Mock vtable bind_string callback.
 * @param query Statement handle.
 * @param index 1-based parameter index.
 * @param val Bound string.
 * @return C_ORM_OK.
 */
static c_orm_error_t dm_bind_string(c_orm_query_t *query, int index,
                                    const char *val) {
  (void)query;
  (void)index;
  (void)C_ORM_STRCPY(g_dm.bound_str, sizeof(g_dm.bound_str), val ? val : "");
  return C_ORM_OK;
}

/**
 * @brief Mock vtable bind_null callback.
 * @param query Statement handle.
 * @param index 1-based parameter index.
 * @return C_ORM_OK.
 */
static c_orm_error_t dm_bind_null(c_orm_query_t *query, int index) {
  (void)query;
  (void)index;
  return C_ORM_OK;
}

/**
 * @brief Mock vtable bind_decimal callback.
 * @param query Statement handle.
 * @param index 1-based parameter index.
 * @param val Bound decimal pointer.
 * @return C_ORM_OK.
 */
static c_orm_error_t dm_bind_decimal(c_orm_query_t *query, int index,
                                     const c_orm_decimal_t *val) {
  (void)query;
  (void)index;
  if (val) {
    g_dm.bound_dec = *val;
  }
  g_dm.bind_dec_calls++;
  return C_ORM_OK;
}

/**
 * @brief Mock vtable get_int32 callback.
 * @param query Statement handle.
 * @param index 0-based column index.
 * @param out_val Output integer.
 * @return C_ORM_OK.
 */
static c_orm_error_t dm_get_int32(c_orm_query_t *query, int index,
                                  int32_t *out_val) {
  (void)query;
  if (index == 2) {
    *out_val = (g_dm.step_row == 1) ? 4 : 15;
  } else if (index == 3) {
    *out_val = (g_dm.step_row == 1) ? 0 : 2;
  } else {
    *out_val = 0;
  }
  return C_ORM_OK;
}

/**
 * @brief Mock vtable get_int64 callback.
 * @param query Statement handle.
 * @param index 0-based column index.
 * @param out_val Output 64-bit integer.
 * @return C_ORM_OK.
 */
static c_orm_error_t dm_get_int64(c_orm_query_t *query, int index,
                                  int64_t *out_val) {
  (void)query;
  (void)index;
  *out_val = g_dm.final_table_id;
  return C_ORM_OK;
}

/**
 * @brief Mock vtable get_string callback.
 * @param query Statement handle.
 * @param index 0-based column index.
 * @param out_val Output string pointer.
 * @return C_ORM_OK.
 */
static c_orm_error_t dm_get_string(c_orm_query_t *query, int index,
                                   const char **out_val) {
  (void)query;
  if (index == 0) {
    *out_val = (g_dm.step_row == 1) ? "ID" : "AMOUNT";
  } else if (index == 1) {
    *out_val = (g_dm.step_row == 1) ? "INTEGER" : "DECIMAL";
  } else {
    *out_val = (g_dm.step_row == 1) ? "N" : "Y";
  }
  return C_ORM_OK;
}

/**
 * @brief Mock vtable get_last_insert_rowid callback.
 * @param db Database connection.
 * @param out_id Output generated identity value.
 * @return C_ORM_OK.
 */
static c_orm_error_t dm_last_rowid(c_orm_db_t *db, int64_t *out_id) {
  (void)db;
  *out_id = g_dm.identity_val_id;
  return C_ORM_OK;
}

/**
 * @brief Populates a `c_orm_driver_vtable_t` for dialect unit tests.
 * @param vt Output vtable structure.
 * @param with_bind_decimal Non-zero to enable `bind_decimal`.
 * @return C_ORM_OK.
 */
static c_orm_error_t init_dialect_mock_vtable(c_orm_driver_vtable_t *vt,
                                              int with_bind_decimal) {
  memset(vt, 0, sizeof(*vt));
  vt->prepare = dm_prepare;
  vt->step = dm_step;
  vt->reset = dm_reset;
  vt->finalize = dm_finalize;
  vt->bind_int32 = dm_bind_int32;
  vt->bind_int64 = dm_bind_int64;
  vt->bind_double = dm_bind_double;
  vt->bind_string = dm_bind_string;
  vt->bind_null = dm_bind_null;
  vt->bind_decimal = with_bind_decimal ? dm_bind_decimal : NULL;
  vt->get_int32 = dm_get_int32;
  vt->get_int64 = dm_get_int64;
  vt->get_string = dm_get_string;
  vt->get_last_insert_rowid = dm_last_rowid;
  return C_ORM_OK;
}

/* --- ODBC / Db2 CLI mock for MERGE batch, migrations & SQLColumns --- */

/**
 * @brief Mock CLI SQLAllocHandle.
 * @param HandleType Handle type.
 * @param InputHandle Parent handle.
 * @param OutputHandlePtr Output handle pointer.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLDial_AllocHandle(
    c_orm_sqlsmallint_t HandleType, c_orm_sqlhandle_t InputHandle,
    c_orm_sqlhandle_t *OutputHandlePtr) {
  (void)InputHandle;
  if (HandleType == C_ORM_SQL_HANDLE_ENV) {
    *OutputHandlePtr = &g_cli_env;
  } else if (HandleType == C_ORM_SQL_HANDLE_DBC) {
    *OutputHandlePtr = &g_cli_dbc;
  } else {
    *OutputHandlePtr = &g_cli_stmt;
  }
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock CLI SQLFreeHandle.
 * @param HandleType Handle type.
 * @param Handle Target handle.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API
SQLDial_FreeHandle(c_orm_sqlsmallint_t HandleType, c_orm_sqlhandle_t Handle) {
  (void)HandleType;
  (void)Handle;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock CLI SQLFreeStmt.
 * @param StatementHandle Statement handle.
 * @param Option Free option.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLDial_FreeStmt(
    c_orm_sqlhstmt_t StatementHandle, c_orm_sqlusmallint_t Option) {
  (void)StatementHandle;
  (void)Option;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock CLI SQLSetEnvAttr.
 * @param EnvironmentHandle Environment handle.
 * @param Attribute Attribute id.
 * @param ValuePtr Value pointer.
 * @param StringLength Value length.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLDial_SetEnvAttr(
    c_orm_sqlhenv_t EnvironmentHandle, c_orm_sqlinteger_t Attribute,
    c_orm_sqlpointer_t ValuePtr, c_orm_sqlinteger_t StringLength) {
  (void)EnvironmentHandle;
  (void)Attribute;
  (void)ValuePtr;
  (void)StringLength;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock CLI SQLSetConnectAttr tracking autocommit transitions.
 * @param ConnectionHandle Connection handle.
 * @param Attribute Attribute id.
 * @param ValuePtr Value pointer.
 * @param StringLength Value length.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLDial_SetConnectAttr(
    c_orm_sqlhdbc_t ConnectionHandle, c_orm_sqlinteger_t Attribute,
    c_orm_sqlpointer_t ValuePtr, c_orm_sqlinteger_t StringLength) {
  (void)ConnectionHandle;
  (void)StringLength;
  if (Attribute == C_ORM_SQL_ATTR_AUTOCOMMIT &&
      (size_t)ValuePtr == (size_t)C_ORM_SQL_AUTOCOMMIT_OFF) {
    g_dm.cli_begins++;
  }
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock CLI SQLDriverConnect.
 * @param ConnectionHandle Connection handle.
 * @param WindowHandle Window handle.
 * @param InConnectionString Input string.
 * @param StringLength1 Input length.
 * @param OutConnectionString Output buffer.
 * @param BufferLength Output buffer size.
 * @param StringLength2Ptr Output length pointer.
 * @param DriverCompletion Completion flag.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLDial_DriverConnect(
    c_orm_sqlhdbc_t ConnectionHandle, c_orm_sqlhwnd_t WindowHandle,
    c_orm_sqlchar_t *InConnectionString, c_orm_sqlsmallint_t StringLength1,
    c_orm_sqlchar_t *OutConnectionString, c_orm_sqlsmallint_t BufferLength,
    c_orm_sqlsmallint_t *StringLength2Ptr,
    c_orm_sqlusmallint_t DriverCompletion) {
  (void)ConnectionHandle;
  (void)WindowHandle;
  (void)InConnectionString;
  (void)StringLength1;
  (void)OutConnectionString;
  (void)BufferLength;
  (void)StringLength2Ptr;
  (void)DriverCompletion;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock CLI SQLDisconnect.
 * @param ConnectionHandle Connection handle.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API
SQLDial_Disconnect(c_orm_sqlhdbc_t ConnectionHandle) {
  (void)ConnectionHandle;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock CLI SQLEndTran tracking commits.
 * @param HandleType Handle type.
 * @param Handle Target handle.
 * @param CompletionType Commit or rollback.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API
SQLDial_EndTran(c_orm_sqlsmallint_t HandleType, c_orm_sqlhandle_t Handle,
                c_orm_sqlsmallint_t CompletionType) {
  (void)HandleType;
  (void)Handle;
  if (CompletionType == C_ORM_SQL_COMMIT) {
    g_dm.cli_commits++;
  }
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock CLI SQLPrepare capturing SQL statements.
 * @param StatementHandle Statement handle.
 * @param StatementText SQL text.
 * @param TextLength SQL length.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API
SQLDial_Prepare(c_orm_sqlhstmt_t StatementHandle,
                c_orm_sqlchar_t *StatementText, c_orm_sqlinteger_t TextLength) {
  (void)StatementHandle;
  (void)TextLength;
  (void)C_ORM_STRCPY(g_dm.prev_sql, sizeof(g_dm.prev_sql), g_dm.last_sql);
  (void)C_ORM_STRCPY(g_dm.last_sql, sizeof(g_dm.last_sql),
                     StatementText ? (const char *)StatementText : "");
  if (strstr(g_dm.last_sql, "WHERE 1 = 0") != NULL ||
      strstr(g_dm.last_sql, "SYSCAT.COLUMNS") != NULL ||
      strstr(g_dm.last_sql, "SYSIBM.SYSCOLUMNS") != NULL) {
    return C_ORM_SQL_ERROR;
  }
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock CLI SQLBindParameter.
 * @param StatementHandle Statement handle.
 * @param ParameterNumber 1-based parameter number.
 * @param InputOutputType Input/output direction.
 * @param ValueType C data type.
 * @param ParameterType SQL data type.
 * @param ColumnSize Column size / precision.
 * @param DecimalDigits Scale.
 * @param ParameterValuePtr Parameter value buffer.
 * @param BufferLength Buffer byte capacity.
 * @param StrLen_or_IndPtr Indicator pointer.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLDial_BindParameter(
    c_orm_sqlhstmt_t StatementHandle, c_orm_sqlusmallint_t ParameterNumber,
    c_orm_sqlsmallint_t InputOutputType, c_orm_sqlsmallint_t ValueType,
    c_orm_sqlsmallint_t ParameterType, c_orm_sqlulen_t ColumnSize,
    c_orm_sqlsmallint_t DecimalDigits, c_orm_sqlpointer_t ParameterValuePtr,
    c_orm_sqllen_t BufferLength, c_orm_sqllen_t *StrLen_or_IndPtr) {
  (void)StatementHandle;
  (void)ParameterNumber;
  (void)InputOutputType;
  (void)ValueType;
  (void)ParameterType;
  (void)ColumnSize;
  (void)DecimalDigits;
  (void)ParameterValuePtr;
  (void)BufferLength;
  (void)StrLen_or_IndPtr;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock CLI SQLExecute.
 * @param StatementHandle Statement handle.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API
SQLDial_Execute(c_orm_sqlhstmt_t StatementHandle) {
  (void)StatementHandle;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock CLI SQLNumResultCols.
 * @param StatementHandle Statement handle.
 * @param ColumnCountPtr Output column count.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLDial_NumResultCols(
    c_orm_sqlhstmt_t StatementHandle, c_orm_sqlsmallint_t *ColumnCountPtr) {
  (void)StatementHandle;
  *ColumnCountPtr = 0;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock CLI SQLColumns returning 2 catalog columns (`ACCT_ID`, `BAL`).
 * @param StatementHandle Statement handle.
 * @param CatalogName Catalog filter.
 * @param NameLength1 Catalog length.
 * @param SchemaName Schema filter.
 * @param NameLength2 Schema length.
 * @param TableName Table filter.
 * @param NameLength3 Table length.
 * @param ColumnName Column filter.
 * @param NameLength4 Column length.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API
SQLDial_Columns(c_orm_sqlhstmt_t StatementHandle, c_orm_sqlchar_t *CatalogName,
                c_orm_sqlsmallint_t NameLength1, c_orm_sqlchar_t *SchemaName,
                c_orm_sqlsmallint_t NameLength2, c_orm_sqlchar_t *TableName,
                c_orm_sqlsmallint_t NameLength3, c_orm_sqlchar_t *ColumnName,
                c_orm_sqlsmallint_t NameLength4) {
  (void)StatementHandle;
  (void)CatalogName;
  (void)NameLength1;
  (void)SchemaName;
  (void)NameLength2;
  (void)TableName;
  (void)NameLength3;
  (void)ColumnName;
  (void)NameLength4;
  g_cli_col_row = 0;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock CLI SQLFetch for `SQLColumns`.
 * @param StatementHandle Statement handle.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API
SQLDial_Fetch(c_orm_sqlhstmt_t StatementHandle) {
  (void)StatementHandle;
  g_cli_col_row++;
  return (g_cli_col_row <= 2) ? C_ORM_SQL_SUCCESS : C_ORM_SQL_NO_DATA;
}

/**
 * @brief Mock CLI SQLGetData for `SQLColumns` columns 4, 6, 7, 9, and 18.
 * @param StatementHandle Statement handle.
 * @param Col_or_Param_Num 1-based column number.
 * @param TargetType Target C type.
 * @param TargetValuePtr Output buffer.
 * @param BufferLength Output buffer size.
 * @param StrLen_or_IndPtr Output indicator.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLDial_GetData(
    c_orm_sqlhstmt_t StatementHandle, c_orm_sqlusmallint_t Col_or_Param_Num,
    c_orm_sqlsmallint_t TargetType, c_orm_sqlpointer_t TargetValuePtr,
    c_orm_sqllen_t BufferLength, c_orm_sqllen_t *StrLen_or_IndPtr) {
  (void)StatementHandle;
  (void)TargetType;
  if (Col_or_Param_Num == 4) {
    const char *s = (g_cli_col_row == 1) ? "ACCT_ID" : "BAL";
    (void)C_ORM_STRCPY((char *)TargetValuePtr, (size_t)BufferLength, s);
    *StrLen_or_IndPtr = (c_orm_sqllen_t)strlen(s);
  } else if (Col_or_Param_Num == 6) {
    const char *s = (g_cli_col_row == 1) ? "BIGINT" : "DECIMAL";
    (void)C_ORM_STRCPY((char *)TargetValuePtr, (size_t)BufferLength, s);
    *StrLen_or_IndPtr = (c_orm_sqllen_t)strlen(s);
  } else if (Col_or_Param_Num == 7) {
    *(c_orm_sqlinteger_t *)TargetValuePtr = (g_cli_col_row == 1) ? 19 : 15;
    *StrLen_or_IndPtr = (c_orm_sqllen_t)sizeof(c_orm_sqlinteger_t);
  } else if (Col_or_Param_Num == 9) {
    *(c_orm_sqlsmallint_t *)TargetValuePtr = (g_cli_col_row == 1) ? 0 : 2;
    *StrLen_or_IndPtr = (c_orm_sqllen_t)sizeof(c_orm_sqlsmallint_t);
  } else if (Col_or_Param_Num == 18) {
    const char *s = (g_cli_col_row == 1) ? "NO" : "YES";
    (void)C_ORM_STRCPY((char *)TargetValuePtr, (size_t)BufferLength, s);
    *StrLen_or_IndPtr = (c_orm_sqllen_t)strlen(s);
  }
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Assembles minimal ODBC CLI dispatch table for dialect tests.
 * @param api Output dispatch table.
 * @return C_ORM_OK.
 */
static c_orm_error_t build_sqlcolumns_mock_api(c_orm_odbc_api_t *api) {
  memset(api, 0, sizeof(*api));
  api->SQLAllocHandle = SQLDial_AllocHandle;
  api->SQLFreeHandle = SQLDial_FreeHandle;
  api->SQLFreeStmt = SQLDial_FreeStmt;
  api->SQLSetEnvAttr = SQLDial_SetEnvAttr;
  api->SQLSetConnectAttr = SQLDial_SetConnectAttr;
  api->SQLDriverConnect = SQLDial_DriverConnect;
  api->SQLDisconnect = SQLDial_Disconnect;
  api->SQLEndTran = SQLDial_EndTran;
  api->SQLPrepare = SQLDial_Prepare;
  api->SQLBindParameter = SQLDial_BindParameter;
  api->SQLExecute = SQLDial_Execute;
  api->SQLNumResultCols = SQLDial_NumResultCols;
  api->SQLColumns = SQLDial_Columns;
  api->SQLFetch = SQLDial_Fetch;
  api->SQLGetData = SQLDial_GetData;
  return C_ORM_OK;
}

/**
 * @brief Tests `C_ORM_DIALECT_DB2` and `C_ORM_DIALECT_ODBC` AST SQL rendering,
 * qualified identifier quoting, `SYSIBM.SYSDUMMY1`, pagination, `FINAL TABLE` /
 * `OLD TABLE` DML returning, `WITH UR`/`CS`/`RS`/`RR` + `FOR FETCH ONLY`, and
 * `C_ORM_TYPE_DECIMAL` AST parameter binding.
 * @return GREATEST test result.
 */
TEST test_db2_and_odbc_ast_rendering_and_execution(void) {
  c_orm_string_builder_t *sb = NULL;
  const char *s = NULL;
  c_orm_ast_query_t *q = NULL;
  c_orm_ast_query_t *q_clone = NULL;
  c_orm_ast_node_t *c1 = NULL;
  c_orm_ast_node_t *l1 = NULL;
  c_orm_ast_node_t *e1 = NULL;
  c_orm_decimal_t dec;
  c_orm_query_params_t params;
  char *sql = NULL;
  c_orm_driver_vtable_t vt;
  c_orm_db_t mock_db;
  char dec_str[64];
  char sq = 39;

  /* 1. Qualified identifier quoting edge cases */
  ASSERT_EQ(C_ORM_OK, c_orm_string_builder_init(&sb));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_quote_qualified_identifier(NULL, "a", '"'));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_quote_qualified_identifier(sb, NULL, '"'));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_quote_qualified_identifier(sb, "", '"'));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_quote_qualified_identifier(sb, "a", sq));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_quote_qualified_identifier(sb, ".a", '"'));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_quote_qualified_identifier(sb, "a.", '"'));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_quote_qualified_identifier(sb, "a..b", '"'));
  ASSERT_EQ(C_ORM_OK,
            c_orm_quote_qualified_identifier(sb, "PROD.AC" DQ "CT", '"'));
  ASSERT_EQ(C_ORM_OK, c_orm_string_builder_append(sb, "|"));
  ASSERT_EQ(C_ORM_OK, c_orm_quote_qualified_identifier(sb, "u.*", '"'));
  ASSERT_EQ(C_ORM_OK, c_orm_string_builder_get(sb, &s));
  ASSERT_STR_EQ(DQ "PROD" DQ "." DQ "AC" DQ DQ "CT" DQ "|" DQ "u" DQ ".*", s);
  ASSERT_EQ(C_ORM_OK, c_orm_string_builder_free(sb));

  /* 2. SELECT without FROM -> SYSIBM.SYSDUMMY1 on Db2 only */
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
  ASSERT_EQ(C_ORM_OK, q->select_(q, "1 + 1"));
  ASSERT_EQ(C_ORM_OK, c_orm_query_to_sql(q, C_ORM_DIALECT_DB2, &sql, NULL));
  ASSERT_STR_EQ("SELECT 1 + 1 FROM SYSIBM.SYSDUMMY1", sql);
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(sql));
  ASSERT_EQ(C_ORM_OK, c_orm_query_to_sql(q, C_ORM_DIALECT_ODBC, &sql, NULL));
  ASSERT_STR_EQ("SELECT 1 + 1", sql);
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(sql));
  ASSERT_EQ(C_ORM_OK, c_orm_query_free(q));

  /* 3. Db2 SELECT with qualified identifiers, LIMIT -> FETCH FIRST, FOR FETCH
   * ONLY, WITH UR */
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
  ASSERT_EQ(C_ORM_OK, q->select_(q, "a.id, a.amount"));
  ASSERT_EQ(C_ORM_OK, q->from_alias(q, "PROD.LEDGER", "a"));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("999999999999.75", &dec));
  ASSERT_EQ(C_ORM_OK, q->col(q, "a.amount", &c1));
  ASSERT_EQ(C_ORM_OK, q->lit_decimal(q, &dec, &l1));
  ASSERT_EQ(C_ORM_OK, q->op(q, ">", c1, l1, &e1));
  ASSERT_EQ(C_ORM_OK, q->where(q, e1));
  ASSERT_EQ(C_ORM_OK, q->order_by(q, "a.id", 1));
  ASSERT_EQ(C_ORM_OK, q->limit(q, 25));
  ASSERT_EQ(C_ORM_OK, q->with_lock(q, C_ORM_AST_LOCK_FOR_FETCH_ONLY));
  ASSERT_EQ(C_ORM_OK, q->with_lock(q, C_ORM_AST_LOCK_WITH_UR));
  ASSERT_EQ(C_ORM_OK, c_orm_query_params_init(&params));
  ASSERT_EQ(C_ORM_OK, c_orm_query_to_sql(q, C_ORM_DIALECT_DB2, &sql, &params));
  ASSERT_STR_EQ("SELECT " DQ "a" DQ "." DQ "id" DQ ", " DQ "a" DQ "." DQ
                "amount" DQ " FROM " DQ "PROD" DQ "." DQ "LEDGER" DQ " AS " DQ
                "a" DQ " WHERE " DQ "a" DQ "." DQ "amount" DQ
                " > ? ORDER BY " DQ "a" DQ "." DQ "id" DQ
                " DESC FETCH FIRST 25 ROWS ONLY FOR FETCH ONLY WITH UR",
                sql);
  ASSERT_EQ(1U, params.count);
  ASSERT_EQ(C_ORM_TYPE_DECIMAL, params.params[0].param_type);
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(sql));
  ASSERT_EQ(C_ORM_OK, c_orm_query_params_cleanup(&params));

  /* Execute AST query against mock "db2" and "odbc" connections */
  ASSERT_EQ(C_ORM_OK, reset_dialect_mock());
  ASSERT_EQ(C_ORM_OK, init_dialect_mock_vtable(&vt, 1));
  memset(&mock_db, 0, sizeof(mock_db));
  mock_db.vtable = &vt;
  mock_db.driver_name = "db2";
  ASSERT_EQ(C_ORM_OK, c_orm_query_execute(&mock_db, q));
  ASSERT_EQ(1, g_dm.bind_dec_calls);
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_to_str(&g_dm.bound_dec, dec_str,
                                           sizeof(dec_str), NULL));
  ASSERT_STR_EQ("999999999999.75", dec_str);
  ASSERT(strstr(g_dm.last_sql, "WITH UR") != NULL);

  /* Fallback when bind_decimal is NULL */
  ASSERT_EQ(C_ORM_OK, init_dialect_mock_vtable(&vt, 0));
  mock_db.driver_name = "odbc";
  ASSERT_EQ(C_ORM_OK, c_orm_query_execute(&mock_db, q));
  ASSERT_STR_EQ("999999999999.75", g_dm.bound_str);
  ASSERT(strstr(g_dm.last_sql, "WITH UR") == NULL);
  ASSERT_EQ(C_ORM_OK, c_orm_query_free(q));

  /* 4. LIMIT + OFFSET on Db2 & ODBC + WITH CS / WITH RS / WITH RR */
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
  ASSERT_EQ(C_ORM_OK, q->select_(q, "*"));
  ASSERT_EQ(C_ORM_OK, q->from(q, "ACCOUNTS"));
  ASSERT_EQ(C_ORM_OK, q->limit(q, 10));
  ASSERT_EQ(C_ORM_OK, q->offset(q, 30));
  ASSERT_EQ(C_ORM_OK, q->with_lock(q, C_ORM_AST_LOCK_WITH_RS));
  ASSERT_EQ(C_ORM_OK, c_orm_query_to_sql(q, C_ORM_DIALECT_DB2, &sql, NULL));
  ASSERT_STR_EQ("SELECT * FROM " DQ "ACCOUNTS" DQ
                " OFFSET 30 ROWS FETCH NEXT 10 ROWS ONLY WITH RS",
                sql);
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(sql));
  ASSERT_EQ(C_ORM_OK, c_orm_query_to_sql(q, C_ORM_DIALECT_ODBC, &sql, NULL));
  ASSERT_STR_EQ("SELECT * FROM " DQ "ACCOUNTS" DQ
                " OFFSET 30 ROWS FETCH NEXT 10 ROWS ONLY",
                sql);
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(sql));
  ASSERT_EQ(C_ORM_OK, q->with_lock(q, C_ORM_AST_LOCK_WITH_CS));
  ASSERT_EQ(C_ORM_OK, c_orm_query_to_sql(q, C_ORM_DIALECT_DB2, &sql, NULL));
  ASSERT(strstr(sql, "WITH CS") != NULL);
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(sql));
  ASSERT_EQ(C_ORM_OK, q->with_lock(q, C_ORM_AST_LOCK_WITH_RR));
  ASSERT_EQ(C_ORM_OK, c_orm_query_to_sql(q, C_ORM_DIALECT_DB2, &sql, NULL));
  ASSERT(strstr(sql, "WITH RR") != NULL);
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(sql));
  ASSERT_EQ(C_ORM_OK, c_orm_query_free(q));

  /* 5. Db2 INSERT / UPDATE / DELETE with RETURNING -> FINAL TABLE / OLD TABLE
   * + clone */
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
  ASSERT_EQ(C_ORM_OK, q->insert_into(q, "PROD.LEDGER"));
  ASSERT_EQ(C_ORM_OK, q->lit(q, "ACC-01", 1, &l1));
  ASSERT_EQ(C_ORM_OK, q->set_col(q, "code", l1));
  ASSERT_EQ(C_ORM_OK, q->lit_decimal(q, &dec, &l1));
  ASSERT_EQ(C_ORM_OK, q->set_col(q, "amount", l1));
  ASSERT_EQ(C_ORM_OK, q->returning(q, "id, code"));
  ASSERT_EQ(C_ORM_OK, q->clone(q, &q_clone));
  ASSERT_EQ(C_ORM_OK, c_orm_query_params_init(&params));
  ASSERT_EQ(C_ORM_OK,
            c_orm_query_to_sql(q_clone, C_ORM_DIALECT_DB2, &sql, &params));
  ASSERT_STR_EQ("SELECT " DQ "id" DQ ", " DQ "code" DQ
                " FROM FINAL TABLE (INSERT INTO " DQ "PROD" DQ "." DQ
                "LEDGER" DQ " (" DQ "code" DQ ", " DQ "amount" DQ
                ") VALUES (?, ?))",
                sql);
  ASSERT_EQ(2U, params.count);
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(sql));
  ASSERT_EQ(C_ORM_OK, c_orm_query_params_cleanup(&params));
  ASSERT_EQ(C_ORM_OK, c_orm_query_params_init(&params));
  ASSERT_EQ(C_ORM_OK,
            c_orm_query_to_sql(q_clone, C_ORM_DIALECT_POSTGRES, &sql, &params));
  ASSERT_STR_EQ(
      "INSERT INTO PROD.LEDGER (code, amount) VALUES ($1, $2) RETURNING id, "
      "code",
      sql);
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(sql));
  ASSERT_EQ(C_ORM_OK, c_orm_query_params_cleanup(&params));
  ASSERT_EQ(C_ORM_OK, c_orm_query_free(q_clone));
  ASSERT_EQ(C_ORM_OK, c_orm_query_free(q));

  /* UPDATE -> FINAL TABLE */
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
  ASSERT_EQ(C_ORM_OK, q->update_table(q, "LEDGER"));
  ASSERT_EQ(C_ORM_OK, q->lit_decimal(q, &dec, &l1));
  ASSERT_EQ(C_ORM_OK, q->set_col(q, "amount", l1));
  ASSERT_EQ(C_ORM_OK, q->eq(q, "id", "7", 0, &e1));
  ASSERT_EQ(C_ORM_OK, q->where(q, e1));
  ASSERT_EQ(C_ORM_OK, q->returning(q, "amount"));
  ASSERT_EQ(C_ORM_OK, c_orm_query_params_init(&params));
  ASSERT_EQ(C_ORM_OK, c_orm_query_to_sql(q, C_ORM_DIALECT_DB2, &sql, &params));
  ASSERT_STR_EQ("SELECT " DQ "amount" DQ " FROM FINAL TABLE (UPDATE " DQ
                "LEDGER" DQ " SET " DQ "amount" DQ " = ? WHERE " DQ "id" DQ
                " = ?)",
                sql);
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(sql));
  ASSERT_EQ(C_ORM_OK, c_orm_query_params_cleanup(&params));
  ASSERT_EQ(C_ORM_OK, c_orm_query_free(q));

  /* DELETE -> OLD TABLE */
  ASSERT_EQ(C_ORM_OK, c_orm_query_new(&q));
  ASSERT_EQ(C_ORM_OK, q->delete_from(q, "LEDGER"));
  ASSERT_EQ(C_ORM_OK, q->eq(q, "id", "7", 0, &e1));
  ASSERT_EQ(C_ORM_OK, q->where(q, e1));
  ASSERT_EQ(C_ORM_OK, q->returning(q, "id, code"));
  ASSERT_EQ(C_ORM_OK, c_orm_query_params_init(&params));
  ASSERT_EQ(C_ORM_OK, c_orm_query_to_sql(q, C_ORM_DIALECT_DB2, &sql, &params));
  ASSERT_STR_EQ("SELECT " DQ "id" DQ ", " DQ "code" DQ
                " FROM OLD TABLE (DELETE FROM " DQ "LEDGER" DQ " WHERE " DQ
                "id" DQ " = ?)",
                sql);
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(sql));
  ASSERT_EQ(C_ORM_OK, c_orm_query_params_cleanup(&params));
  ASSERT_EQ(C_ORM_OK, c_orm_query_free(q));

  PASS();
}

/**
 * @brief Tests dynamic query builders (`c_orm_select_builder_set_dialect_kind`,
 * `c_orm_insert_builder_set_dialect_kind`, `MERGE INTO` upserts) and high-level
 * CRUD `c_orm_upsert`, `c_orm_upsert_batch`, and Db2 `FINAL TABLE` auto-PK
 * `c_orm_insert`.
 * @return GREATEST test result.
 */
TEST test_db2_and_odbc_query_builders_and_crud_upserts(void) {
  c_orm_select_builder_t *sb = NULL;
  c_orm_insert_builder_t *ib = NULL;
  c_orm_update_builder_t *ub = NULL;
  char *sql = NULL;
  c_orm_driver_vtable_t vt;
  c_orm_db_t mock_db;
  c_orm_odbc_api_t cli_api;
  c_orm_db_t *db2_cli_db = NULL;
  c_orm_db_t *sq_db = NULL;
  c_orm_db_t *mem_db = NULL;
  DialectLedger rows[2];
  DialectLedger fetched;
  char dec_buf[64];

  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_select_builder_set_dialect_kind(NULL, C_ORM_DIALECT_DB2));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_insert_builder_set_dialect_kind(NULL, C_ORM_DIALECT_DB2));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN,
            c_orm_update_builder_set_dialect_kind(NULL, C_ORM_DIALECT_DB2));
  ASSERT_EQ(C_ORM_ERROR_UNKNOWN, c_orm_insert_builder_set_upsert(NULL, 1));

  /* Select builder Db2 pagination (set dialect after limit/offset) */
  ASSERT_EQ(C_ORM_OK, c_orm_select_builder_init(&DialectLedger_meta, &sb));
  ASSERT_EQ(C_ORM_OK, c_orm_select_where_eq(sb, "code"));
  ASSERT_EQ(C_ORM_OK, c_orm_select_limit(sb, 15));
  ASSERT_EQ(C_ORM_OK,
            c_orm_select_builder_set_dialect_kind(sb, C_ORM_DIALECT_DB2));
  ASSERT_EQ(C_ORM_OK, c_orm_select_builder_compile(sb, &sql));
  ASSERT_STR_EQ("SELECT * FROM LEDGER WHERE code = ? FETCH FIRST 15 ROWS ONLY",
                sql);
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(sql));
  ASSERT_EQ(C_ORM_OK, c_orm_select_offset(sb, 45));
  ASSERT_EQ(C_ORM_OK, c_orm_select_builder_compile(sb, &sql));
  ASSERT_STR_EQ("SELECT * FROM LEDGER WHERE code = ? OFFSET 45 ROWS FETCH NEXT "
                "15 ROWS ONLY",
                sql);
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(sql));
  ASSERT_EQ(C_ORM_OK, c_orm_select_builder_free(sb));

  /* Update builder dialect setter */
  ASSERT_EQ(C_ORM_OK, c_orm_update_builder_init(&DialectLedger_meta, &ub));
  ASSERT_EQ(C_ORM_OK,
            c_orm_update_builder_set_dialect_kind(ub, C_ORM_DIALECT_DB2));
  ASSERT_EQ(C_ORM_OK, c_orm_update_builder_free(ub));

  /* Insert builder MERGE INTO on Db2 + ON CONFLICT / ON DUPLICATE KEY */
  ASSERT_EQ(C_ORM_OK, c_orm_insert_builder_init(&DialectLedger_meta, &ib));
  ASSERT_EQ(C_ORM_OK,
            c_orm_insert_builder_set_dialect_kind(ib, C_ORM_DIALECT_DB2));
  ASSERT_EQ(C_ORM_OK, c_orm_insert_builder_set_upsert(ib, 1));
  ASSERT_EQ(C_ORM_OK, c_orm_insert_builder_compile(ib, &sql));
  ASSERT_STR_EQ("MERGE INTO " DQ "LEDGER" DQ " AS t USING (VALUES (?, ?, ?)) "
                "AS s (" DQ "id" DQ ", " DQ "code" DQ ", " DQ "amount" DQ
                ") ON (t." DQ "id" DQ " = s." DQ "id" DQ
                ") WHEN MATCHED THEN UPDATE SET t." DQ "code" DQ " = s." DQ
                "code" DQ ", t." DQ "amount" DQ " = s." DQ "amount" DQ
                " WHEN NOT MATCHED THEN INSERT (" DQ "id" DQ ", " DQ "code" DQ
                ", " DQ "amount" DQ ") VALUES (s." DQ "id" DQ ", s." DQ
                "code" DQ ", s." DQ "amount" DQ ")",
                sql);
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(sql));

  ASSERT_EQ(C_ORM_OK,
            c_orm_insert_builder_set_dialect_kind(ib, C_ORM_DIALECT_MYSQL));
  ASSERT_EQ(C_ORM_OK, c_orm_insert_builder_compile(ib, &sql));
  ASSERT_STR_EQ("INSERT INTO LEDGER (id, code, amount) VALUES (?, ?, ?) ON "
                "DUPLICATE KEY UPDATE code = VALUES(code), amount = "
                "VALUES(amount)",
                sql);
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(sql));

  ASSERT_EQ(C_ORM_OK,
            c_orm_insert_builder_set_dialect_kind(ib, C_ORM_DIALECT_POSTGRES));
  ASSERT_EQ(C_ORM_OK, c_orm_insert_builder_compile(ib, &sql));
  ASSERT_STR_EQ("INSERT INTO LEDGER (id, code, amount) VALUES ($1, $2, $3) ON "
                "CONFLICT (id) DO UPDATE SET code = EXCLUDED.code, amount = "
                "EXCLUDED.amount",
                sql);
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(sql));
  ASSERT_EQ(C_ORM_OK, c_orm_insert_builder_free(ib));

  /* High-level c_orm_insert auto-PK via FINAL TABLE & IDENTITY_VAL_LOCAL
   * fallback, plus single-row c_orm_upsert on mock "db2" */
  ASSERT_EQ(C_ORM_OK, reset_dialect_mock());
  ASSERT_EQ(C_ORM_OK, init_dialect_mock_vtable(&vt, 1));
  memset(&mock_db, 0, sizeof(mock_db));
  mock_db.vtable = &vt;
  mock_db.driver_name = "db2";

  memset(rows, 0, sizeof(rows));
  rows[0].id = 0;
  rows[0].code = "AUTO-1";
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("125.50", &rows[0].amount));
  g_dm.final_table_id = 42;
  ASSERT_EQ(C_ORM_OK, c_orm_insert(&mock_db, &DialectLedger_meta, &rows[0]));
  ASSERT_STR_EQ("SELECT " DQ "id" DQ " FROM FINAL TABLE (INSERT INTO " DQ
                "LEDGER" DQ " (" DQ "code" DQ ", " DQ "amount" DQ
                ") VALUES (?, ?))",
                g_dm.last_sql);
  ASSERT_EQ(42, rows[0].id);

  /* Fallback to get_last_insert_rowid when FINAL TABLE returns 0 rows */
  rows[0].id = 0;
  g_dm.final_table_id = 0;
  g_dm.identity_val_id = 99;
  ASSERT_EQ(C_ORM_OK, c_orm_insert(&mock_db, &DialectLedger_meta, &rows[0]));
  ASSERT_EQ(99, rows[0].id);

  /* Single-row and multi-row MERGE INTO via c_orm_upsert / c_orm_upsert_batch
   */
  ASSERT_EQ(C_ORM_OK, c_orm_upsert(&mock_db, &DialectLedger_meta, &rows[0]));
  ASSERT(strstr(g_dm.last_sql, "MERGE INTO " DQ "LEDGER" DQ " AS t") != NULL);

  ASSERT_EQ(C_ORM_OK, build_sqlcolumns_mock_api(&cli_api));
  ASSERT_EQ(C_ORM_OK, c_orm_db2_set_mock_api(&cli_api, 1));
  ASSERT_EQ(C_ORM_OK,
            c_orm_db2_connect("db2://u:p@localhost:50000/SAMPLE", &db2_cli_db));
  rows[1].id = 100;
  rows[1].code = "AUTO-2";
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("250.75", &rows[1].amount));
  ASSERT_EQ(C_ORM_OK,
            c_orm_upsert_batch(db2_cli_db, &DialectLedger_meta, rows, 2, 10));
  ASSERT(strstr(g_dm.last_sql, "USING (VALUES (?, ?, ?), (?, ?, ?))") != NULL);
  ASSERT_EQ(C_ORM_OK, db2_cli_db->vtable->disconnect(db2_cli_db));
  ASSERT_EQ(C_ORM_OK, c_orm_db2_set_mock_api(NULL, 0));

  /* Live SQLite and Memory end-to-end c_orm_upsert & c_orm_upsert_batch */
  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_connect(":memory:", &sq_db));
  ASSERT_EQ(C_ORM_OK,
            c_orm_execute_raw(sq_db,
                              "CREATE TABLE LEDGER (id INTEGER PRIMARY KEY, "
                              "code TEXT NOT NULL, amount DECIMAL(15, 2) NOT "
                              "NULL)"));
  rows[0].id = 1;
  rows[0].code = "ACC-INIT";
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("500.25", &rows[0].amount));
  ASSERT_EQ(C_ORM_OK, c_orm_upsert(sq_db, &DialectLedger_meta, &rows[0]));
  rows[0].code = "ACC-UPDATED";
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("875.90", &rows[0].amount));
  rows[1].id = 2;
  rows[1].code = "ACC-SECOND";
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("100.00", &rows[1].amount));
  ASSERT_EQ(C_ORM_OK,
            c_orm_upsert_batch(sq_db, &DialectLedger_meta, rows, 2, 10));
  memset(&fetched, 0, sizeof(fetched));
  ASSERT_EQ(C_ORM_OK,
            c_orm_find_by_id_int32(sq_db, &DialectLedger_meta, 1, &fetched));
  ASSERT_STR_EQ("ACC-UPDATED", fetched.code);
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_to_str(&fetched.amount, dec_buf,
                                           sizeof(dec_buf), NULL));
  ASSERT_STR_EQ("875.90", dec_buf);
  ASSERT_EQ(C_ORM_OK, c_orm_free_columns(&DialectLedger_meta, &fetched));
  ASSERT_EQ(C_ORM_OK, sq_db->vtable->disconnect(sq_db));

  ASSERT_EQ(C_ORM_OK, c_orm_memory_connect(":memory:", &mem_db));
  ASSERT_EQ(C_ORM_OK,
            c_orm_execute_raw(mem_db,
                              "CREATE TABLE LEDGER (id INT PRIMARY KEY, code "
                              "VARCHAR(32), amount DECIMAL(15, 2))"));
  ASSERT_EQ(C_ORM_OK,
            c_orm_upsert_batch(mem_db, &DialectLedger_meta, rows, 2, 10));
  rows[0].code = "MEM-UPD";
  ASSERT_EQ(C_ORM_OK, c_orm_upsert(mem_db, &DialectLedger_meta, &rows[0]));
  memset(&fetched, 0, sizeof(fetched));
  ASSERT_EQ(C_ORM_OK,
            c_orm_find_by_id_int32(mem_db, &DialectLedger_meta, 1, &fetched));
  ASSERT_STR_EQ("MEM-UPD", fetched.code);
  ASSERT_EQ(C_ORM_OK, c_orm_free_columns(&DialectLedger_meta, &fetched));
  ASSERT_EQ(C_ORM_OK, mem_db->vtable->disconnect(mem_db));

  PASS();
}

/**
 * @brief Tests Db2 & ODBC SQL DDL parsing, SQL-to-C lossless `c_orm_decimal_t`
 * codegen, C-to-SQL Db2/ODBC DDL & `ALTER TABLE` diff emission, catalog
 * introspection (`SYSCAT.COLUMNS`, `SYSIBM.SYSCOLUMNS`, `SQLColumns`), and
 * Db2/ODBC schema migrations.
 * @return GREATEST test result.
 */
TEST test_db2_and_odbc_ddl_codegen_introspection_and_migrations(void) {
  const char *db2_ddl_1 =
      "CREATE TABLE " DQ "PROD" DQ "." DQ "CORP_LEDGER" DQ " ("
      "  " DQ "ID" DQ " INTEGER GENERATED BY DEFAULT AS IDENTITY (START WITH 1 "
      "INCREMENT BY 1) NOT NULL PRIMARY KEY,"
      "  " DQ "CODE" DQ " CHAR(10) CCSID EBCDIC NOT NULL,"
      "  " DQ "AMOUNT" DQ " DECIMAL(15, 2) WITH DEFAULT 0,"
      "  " DQ "RATE" DQ " NUMERIC(9, 4),"
      "  " DQ "SCORE" DQ " DECFLOAT(34),"
      "  " DQ "NAME_JP" DQ " GRAPHIC(20) CCSID UNICODE,"
      "  " DQ "NOTES_JP" DQ " VARGRAPHIC(100),";
  const char *db2_ddl_2 =
      "  " DQ "RAW_TOKEN" DQ " CHAR(16) FOR BIT DATA,"
      "  " DQ "PAYLOAD" DQ " VARCHAR(64) FOR BIT DATA,"
      "  " DQ "BODY" DQ " CLOB(1024),"
      "  " DQ "DBODY" DQ " DBCLOB(512),"
      "  " DQ "POST_TIME" DQ " TIME WITH DEFAULT CURRENT TIME,"
      "  " DQ "CREATED_AT" DQ " TIMESTAMP(12) WITH DEFAULT CURRENT TIMESTAMP,"
      "  " DQ "TIER" DQ " SMALLINT CCSID 500 FOR SBCS DATA"
      ");";
  c_orm_string_builder_t *ddl_sb = NULL;
  const char *db2_ddl = NULL;
  az_span span;
  struct sql_token_list_t *toks = NULL;
  struct sql_table_t *tbl = NULL;
  struct sql_parse_error_t perr;
  FILE *tmp_fp = NULL;
  char *emitted = NULL;
  size_t nread;
  char *ddl_out = NULL;
  char *up_sql = NULL;
  char *down_sql = NULL;
  char *insp_sql = NULL;
  cdd_c_meta_t *cm1 = NULL;
  cdd_c_prop_meta_t cm2_props[2];
  cdd_c_meta_t cm2_stack;
  c_orm_table_meta_t *roundtrip_tm = NULL;
  cdd_c_meta_diff_t diff;
  c_orm_driver_vtable_t vt;
  c_orm_db_t mock_db;
  c_orm_odbc_api_t cli_api;
  c_orm_db_t *cli_db = NULL;
  c_orm_db_t *db2_db = NULL;
  cdd_c_abstract_struct_array_t abs_schema;
  cdd_c_meta_t *mig_schema = NULL;
  c_orm_odbc_column_info_t *col_list = NULL;
  size_t col_cnt = 0;

  /* 1. Parse full Db2 CREATE TABLE DDL */
  ASSERT_EQ(C_ORM_OK, c_orm_string_builder_init(&ddl_sb));
  ASSERT_EQ(C_ORM_OK, c_orm_string_builder_append(ddl_sb, db2_ddl_1));
  ASSERT_EQ(C_ORM_OK, c_orm_string_builder_append(ddl_sb, db2_ddl_2));
  ASSERT_EQ(C_ORM_OK, c_orm_string_builder_get(ddl_sb, &db2_ddl));
  span = az_span_create_from_str((char *)(size_t)db2_ddl);
  ASSERT_EQ(C_ORM_OK, sql_lex(span, &toks));
  memset(&perr, 0, sizeof(perr));
  ASSERT_EQ(C_ORM_OK, sql_parse_table(toks, &tbl, &perr));
  ASSERT_STR_EQ("CORP_LEDGER", tbl->name);
  ASSERT_EQ(14U, tbl->n_columns);

  ASSERT_EQ(SQL_TYPE_INT, tbl->columns[0].type);
  ASSERT_EQ(1, tbl->columns[0].is_identity);
  ASSERT_EQ(SQL_TYPE_CHAR, tbl->columns[1].type);
  ASSERT_EQ(10, tbl->columns[1].length);
  ASSERT_EQ(37U, tbl->columns[1].ccsid);
  ASSERT_EQ(SQL_TYPE_DECIMAL, tbl->columns[2].type);
  ASSERT_EQ(15, tbl->columns[2].length);
  ASSERT_EQ(2, tbl->columns[2].scale);
  ASSERT_EQ(SQL_TYPE_NUMERIC, tbl->columns[3].type);
  ASSERT_EQ(9, tbl->columns[3].length);
  ASSERT_EQ(4, tbl->columns[3].scale);
  ASSERT_EQ(SQL_TYPE_DECFLOAT, tbl->columns[4].type);
  ASSERT_EQ(34, tbl->columns[4].length);
  ASSERT_EQ(SQL_TYPE_GRAPHIC, tbl->columns[5].type);
  ASSERT_EQ(20, tbl->columns[5].length);
  ASSERT_EQ(1208U, tbl->columns[5].ccsid);
  ASSERT_EQ(SQL_TYPE_VARGRAPHIC, tbl->columns[6].type);
  ASSERT_EQ(SQL_TYPE_BINARY, tbl->columns[7].type);
  ASSERT_EQ(1, tbl->columns[7].is_for_bit_data);
  ASSERT_EQ(65535U, tbl->columns[7].ccsid);
  ASSERT_EQ(SQL_TYPE_VARBINARY, tbl->columns[8].type);
  ASSERT_EQ(1, tbl->columns[8].is_for_bit_data);
  ASSERT_EQ(SQL_TYPE_CLOB, tbl->columns[9].type);
  ASSERT_EQ(SQL_TYPE_DBCLOB, tbl->columns[10].type);
  ASSERT_EQ(SQL_TYPE_TIME, tbl->columns[11].type);
  ASSERT_EQ(SQL_TYPE_TIMESTAMP, tbl->columns[12].type);
  ASSERT_EQ(12, tbl->columns[12].length);
  ASSERT_EQ(SQL_TYPE_SMALLINT, tbl->columns[13].type);
  ASSERT_EQ(500U, tbl->columns[13].ccsid);

  /* 2. Emit C header & source from parsed Db2 AST and verify c_orm_decimal_t
   * and precision/scale/fixed_length/ccsid metadata */
  ASSERT_EQ(C_ORM_OK, c_orm_system_malloc(32768, (void **)&emitted));
  ASSERT_EQ(C_ORM_OK, C_ORM_FOPEN(&tmp_fp, "test_db2_dialect_tmp.out", "w+b"));
  ASSERT(tmp_fp != NULL);
  ASSERT_EQ(C_ORM_OK, sql_to_c_header_emit(tmp_fp, tbl));
  ASSERT_EQ(C_ORM_OK, sql_to_c_source_emit(tmp_fp, tbl, "corp_ledger.h"));
  rewind(tmp_fp);
  nread = fread(emitted, 1, 32767, tmp_fp);
  emitted[nread] = '\0';
  fclose(tmp_fp);
  (void)remove("test_db2_dialect_tmp.out");
  ASSERT(strstr(emitted, "#include " DQ "c_orm_decimal.h" DQ) != NULL);
  ASSERT(strstr(emitted, "c_orm_decimal_t *AMOUNT;") != NULL);
  ASSERT(strstr(emitted, "C_ORM_TYPE_DECIMAL") != NULL);
  ASSERT(strstr(emitted, "C_ORM_TYPE_DECFLOAT") != NULL);
  ASSERT(strstr(emitted, "C_ORM_TYPE_GRAPHIC") != NULL);
  ASSERT(strstr(emitted, "C_ORM_TYPE_VARGRAPHIC") != NULL);
  ASSERT(strstr(emitted, "C_ORM_TYPE_BINARY_FIXED") != NULL);
  ASSERT(strstr(emitted, "C_ORM_TYPE_VARBINARY") != NULL);
  ASSERT(strstr(emitted, "C_ORM_TYPE_TIME") != NULL);
  ASSERT(strstr(emitted, "15, 2, 0, 0") != NULL);
  ASSERT(strstr(emitted, "0, 0, 16, 65535") != NULL);
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(emitted));

  ASSERT_EQ(C_ORM_OK, sql_table_free(tbl));
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(tbl));
  ASSERT_EQ(C_ORM_OK, sql_token_list_free(toks));
  ASSERT_EQ(C_ORM_OK, c_orm_string_builder_free(ddl_sb));

  /* 3. C-to-SQL Db2 & ODBC DDL, diff ALTER TABLE, and introspection queries */
  ASSERT_EQ(C_ORM_OK, c_orm_table_meta_to_sql_create_table(
                          &DialectLedger_meta, C_TO_SQL_DIALECT_DB2, &ddl_out));
  ASSERT(strstr(ddl_out, DQ "id" DQ " INTEGER GENERATED BY DEFAULT AS IDENTITY "
                            "NOT NULL PRIMARY KEY") != NULL);
  ASSERT(strstr(ddl_out, DQ "amount" DQ " DECIMAL(15, 2) NOT NULL") != NULL);
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(ddl_out));

  ASSERT_EQ(C_ORM_OK, c_orm_table_meta_to_cdd_meta(&DialectLedger_meta, &cm1));
  ASSERT_EQ(C_ORM_OK, c_orm_cdd_meta_to_table_meta(cm1, &roundtrip_tm));
  ASSERT_EQ(C_ORM_TYPE_DECIMAL, roundtrip_tm->columns[2].type);
  ASSERT_EQ(C_ORM_OK, c_orm_free_table_meta(roundtrip_tm));

  ASSERT_EQ(C_ORM_OK, cdd_c_meta_to_sql_create_table(cm1, C_TO_SQL_DIALECT_DB2,
                                                     &ddl_out));
  ASSERT(strstr(ddl_out, "DECIMAL(18, 2)") != NULL);
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(ddl_out));

  memset(cm2_props, 0, sizeof(cm2_props));
  cm2_props[0].name = "id";
  cm2_props[0].type = "C_ORM_TYPE_INT32";
  cm2_props[1].name = "code";
  cm2_props[1].type = "C_ORM_TYPE_VARGRAPHIC";
  memset(&cm2_stack, 0, sizeof(cm2_stack));
  cm2_stack.name = "LEDGER";
  cm2_stack.props = cm2_props;
  cm2_stack.num_props = 2;
  memset(&diff, 0, sizeof(diff));
  ASSERT_EQ(C_ORM_OK, cdd_c_meta_diff(cm1, &cm2_stack, &diff));
  ASSERT_EQ(C_ORM_OK,
            cdd_c_meta_diff_to_sql("LEDGER", &diff, C_TO_SQL_DIALECT_DB2,
                                   &up_sql, &down_sql));
  ASSERT(strstr(up_sql, "ALTER TABLE " DQ "LEDGER" DQ " DROP COLUMN " DQ
                        "amount" DQ ";") != NULL);
  ASSERT(strstr(up_sql, "ALTER TABLE " DQ "LEDGER" DQ " ALTER COLUMN " DQ
                        "code" DQ " SET DATA TYPE VARGRAPHIC(255);") != NULL);
  ASSERT(strstr(up_sql,
                "SYSPROC.ADMIN_CMD('REORG TABLE " DQ "LEDGER" DQ "')") != NULL);
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(up_sql));
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(down_sql));
  ASSERT_EQ(C_ORM_OK, cdd_c_meta_diff_free(&diff));

  /* Reverse diff: added column on Db2 & ODBC */
  ASSERT_EQ(C_ORM_OK, cdd_c_meta_diff(&cm2_stack, cm1, &diff));
  ASSERT_EQ(C_ORM_OK,
            cdd_c_meta_diff_to_sql("LEDGER", &diff, C_TO_SQL_DIALECT_DB2,
                                   &up_sql, &down_sql));
  ASSERT(strstr(up_sql, "ALTER TABLE " DQ "LEDGER" DQ " ADD COLUMN " DQ
                        "amount" DQ " DECIMAL(18, 2);") != NULL);
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(up_sql));
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(down_sql));
  ASSERT_EQ(C_ORM_OK,
            cdd_c_meta_diff_to_sql("LEDGER", &diff, C_TO_SQL_DIALECT_ODBC,
                                   &up_sql, &down_sql));
  ASSERT(strstr(up_sql, "ALTER TABLE " DQ "LEDGER" DQ " ALTER COLUMN " DQ
                        "code" DQ) != NULL);
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(up_sql));
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(down_sql));
  ASSERT_EQ(C_ORM_OK, cdd_c_meta_diff_free(&diff));
  ASSERT_EQ(C_ORM_OK, c_orm_free_cdd_meta(cm1));

  ASSERT_EQ(C_ORM_OK, cdd_c_get_schema_inspection_query(C_TO_SQL_DIALECT_DB2,
                                                        "LEDGER", &insp_sql));
  ASSERT(strstr(insp_sql, "SYSCAT.COLUMNS") != NULL);
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(insp_sql));
  ASSERT_EQ(C_ORM_OK,
            cdd_c_get_zos_schema_inspection_query("LEDGER", &insp_sql));
  ASSERT(strstr(insp_sql, "SYSIBM.SYSCOLUMNS") != NULL);
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(insp_sql));
  ASSERT_EQ(C_ORM_OK, cdd_c_get_schema_inspection_query(C_TO_SQL_DIALECT_ODBC,
                                                        "LEDGER", &insp_sql));
  ASSERT(strstr(insp_sql, "INFORMATION_SCHEMA.COLUMNS") != NULL);
  ASSERT_EQ(C_ORM_OK, c_orm_system_free(insp_sql));

  /* 4. Catalog introspection on Db2 (LUW + z/OS) */
  ASSERT_EQ(C_ORM_OK, reset_dialect_mock());
  ASSERT_EQ(C_ORM_OK, init_dialect_mock_vtable(&vt, 1));
  memset(&mock_db, 0, sizeof(mock_db));
  mock_db.vtable = &vt;
  mock_db.driver_name = "db2";

  g_dm.catalog_mode = 0;
  ASSERT_EQ(C_ORM_OK,
            cdd_c_inspect_schema_db2(&mock_db, "LEDGER", &abs_schema));
  ASSERT_EQ(2U, abs_schema.count);
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_array_free(&abs_schema));
  ASSERT_EQ(C_ORM_OK, c_orm_migration_fetch_table_schema(&mock_db, "LEDGER",
                                                         &mig_schema));
  ASSERT_EQ(2U, mig_schema->num_props);
  ASSERT_STR_EQ("ID", mig_schema->props[0].name);
  ASSERT_STR_EQ("DECIMAL", mig_schema->props[1].type);
  ASSERT_EQ(C_ORM_OK, c_orm_migration_free_table_schema(mig_schema));

  g_dm.catalog_mode = 1;
  ASSERT_EQ(C_ORM_OK,
            cdd_c_inspect_schema_db2(&mock_db, "LEDGER", &abs_schema));
  ASSERT_EQ(2U, abs_schema.count);
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_array_free(&abs_schema));
  ASSERT_EQ(C_ORM_OK, c_orm_migration_fetch_table_schema(&mock_db, "LEDGER",
                                                         &mig_schema));
  ASSERT_EQ(2U, mig_schema->num_props);
  ASSERT_EQ(C_ORM_OK, c_orm_migration_free_table_schema(mig_schema));

  /* 5. Real Db2 & ODBC connections via CLI mock for migrations & SQLColumns */
  ASSERT_EQ(C_ORM_OK, reset_dialect_mock());
  ASSERT_EQ(C_ORM_OK, build_sqlcolumns_mock_api(&cli_api));
  ASSERT_EQ(C_ORM_OK, c_orm_db2_set_mock_api(&cli_api, 1));
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_set_mock_api(&cli_api));

  ASSERT_EQ(C_ORM_OK,
            c_orm_db2_connect("db2://u:p@localhost:50000/SAMPLE", &db2_db));
  ASSERT_EQ(C_ORM_OK, c_orm_migration_lock(db2_db));
  ASSERT_EQ(1, g_dm.cli_begins);
  ASSERT_STR_EQ("LOCK TABLE _c_orm_migrations IN EXCLUSIVE MODE",
                g_dm.last_sql);
  ASSERT(strstr(g_dm.prev_sql, "DEFAULT CURRENT TIMESTAMP") != NULL);
  ASSERT_EQ(C_ORM_OK, c_orm_migration_unlock(db2_db));
  ASSERT_EQ(1, g_dm.cli_commits);
  ASSERT_EQ(C_ORM_OK, db2_db->vtable->disconnect(db2_db));

  ASSERT_EQ(C_ORM_OK, c_orm_odbc_connect("odbc://DSN=Test", &cli_db));
  ASSERT_EQ(C_ORM_OK, c_orm_migration_lock(cli_db));
  ASSERT_EQ(2, g_dm.cli_begins);
  ASSERT_EQ(C_ORM_OK, c_orm_migration_unlock(cli_db));
  ASSERT_EQ(2, g_dm.cli_commits);

  ASSERT_EQ(C_ORM_OK, c_orm_odbc_list_columns(cli_db, "PROD", "LEDGER",
                                              &col_list, &col_cnt));
  ASSERT_EQ(2U, col_cnt);
  ASSERT_STR_EQ("ACCT_ID", col_list[0].name);
  ASSERT_EQ(0, col_list[0].is_nullable);
  ASSERT_STR_EQ("BAL", col_list[1].name);
  ASSERT_EQ(15, col_list[1].column_size);
  ASSERT_EQ(2, col_list[1].decimal_digits);
  ASSERT_EQ(1, col_list[1].is_nullable);
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_free_column_list(col_list, col_cnt));

  ASSERT_EQ(C_ORM_OK, cdd_c_inspect_schema_odbc(cli_db, "LEDGER", &abs_schema));
  ASSERT_EQ(2U, abs_schema.count);
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_array_free(&abs_schema));

  ASSERT_EQ(C_ORM_OK,
            c_orm_migration_fetch_table_schema(cli_db, "LEDGER", &mig_schema));
  ASSERT_EQ(2U, mig_schema->num_props);
  ASSERT_STR_EQ("ACCT_ID", mig_schema->props[0].name);
  ASSERT_EQ(C_ORM_OK, c_orm_migration_free_table_schema(mig_schema));

  ASSERT_EQ(C_ORM_OK, cli_db->vtable->disconnect(cli_db));
  ASSERT_EQ(C_ORM_OK, c_orm_db2_set_mock_api(NULL, 0));
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_set_mock_api(NULL));

  PASS();
}

/**
 * @brief Exhaustive verification of `c_orm_connect_url`, Db2 `MERGE INTO`
 * batch edge cases, `FINAL TABLE` auto-PK edge cases, `c_orm_upsert` /
 * `c_orm_upsert_batch` across drivers, Db2 savepoints, and `DECIMAL` PK /
 * composite-key operations.
 * @return GREATEST test result.
 */
TEST test_db2_dialect_crud_batch_and_connect_exhaustive(void) {
  c_orm_odbc_api_t cli_api;
  c_orm_db_t *db = NULL;
  c_orm_driver_vtable_t vt;
  c_orm_db_t mock_db;
  DialectLedger rows[2];
  DialectLedger out_row;
  c_orm_column_meta_t comp_cols[4];
  c_orm_table_meta_t comp_meta;
  c_orm_decimal_t dec_pk;
  struct CddCVariant key_vars[2];
  int step;

  /* 1. c_orm_connect_url across all supported schemes */
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_connect_url(NULL, &db));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_connect_url("sqlite://:memory:", NULL));

  ASSERT_EQ(C_ORM_OK, build_sqlcolumns_mock_api(&cli_api));
  ASSERT_EQ(C_ORM_OK, c_orm_db2_set_mock_api(&cli_api, 1));
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_set_mock_api(&cli_api));

  ASSERT_EQ(C_ORM_OK, c_orm_connect_url("memory://", &db));
  ASSERT_EQ(C_ORM_OK, db->vtable->disconnect(db));
  ASSERT_EQ(C_ORM_OK, c_orm_connect_url("sqlite://:memory:", &db));
  ASSERT_EQ(C_ORM_OK, db->vtable->disconnect(db));
  ASSERT_EQ(C_ORM_OK, c_orm_connect_url(":memory:", &db));
  ASSERT_EQ(C_ORM_OK, db->vtable->disconnect(db));
  ASSERT_EQ(C_ORM_OK, c_orm_connect_url("odbc://DSN=Test", &db));
  memset(rows, 0, sizeof(rows));
  rows[0].id = 1;
  rows[0].code = "A1";
  (void)c_orm_decimal_from_str("10.50", &rows[0].amount);
  ASSERT_EQ(C_ORM_OK,
            c_orm_insert_batch_ext(db, &DialectLedger_meta, rows, 1, 1,
                                   C_ORM_ON_CONFLICT_DO_UPDATE, NULL, NULL));
  ASSERT_EQ(C_ORM_OK, db->vtable->disconnect(db));
  ASSERT_EQ(C_ORM_OK, c_orm_connect_url("ODBC://DSN=Test", &db));
  ASSERT_EQ(C_ORM_OK, db->vtable->disconnect(db));
  ASSERT_EQ(C_ORM_OK, c_orm_connect_url("DRIVER={Mock};SERVER=s", &db));
  ASSERT_EQ(C_ORM_OK, db->vtable->disconnect(db));
  ASSERT_EQ(C_ORM_OK, c_orm_connect_url("driver={Mock};SERVER=s", &db));
  ASSERT_EQ(C_ORM_OK, db->vtable->disconnect(db));
  ASSERT_EQ(C_ORM_OK, c_orm_connect_url("DSN=MockDsn", &db));
  ASSERT_EQ(C_ORM_OK, db->vtable->disconnect(db));
  ASSERT_EQ(C_ORM_OK, c_orm_connect_url("dsn=MockDsn", &db));
  ASSERT_EQ(C_ORM_OK, db->vtable->disconnect(db));
  ASSERT_EQ(C_ORM_OK, c_orm_connect_url("db2://u:p@localhost:50000/DB", &db));
  ASSERT_EQ(C_ORM_OK, db->vtable->disconnect(db));
  ASSERT_EQ(C_ORM_OK, c_orm_connect_url("DB2://u:p@localhost:50000/DB", &db));
  /* Keep `db` (`db2_cli_db`) open for MERGE INTO batch & CLI transactions! */
  {
    c_orm_db_t *tmp_db = NULL;
    (void)c_orm_connect_url("postgres://invalid_host_0/db", &tmp_db);
    (void)c_orm_connect_url("postgresql://invalid_host_0/db", &tmp_db);
    (void)c_orm_connect_url("mysql://invalid_host_0/db", &tmp_db);
    (void)c_orm_connect_url("mariadb://invalid_host_0/db", &tmp_db);
#ifdef _WIN32
    ASSERT_NEQ(C_ORM_OK, c_orm_connect_url("Z:\\bad_dir\\bad.db", &tmp_db));
#else
    ASSERT_NEQ(C_ORM_OK,
               c_orm_connect_url("/dev/null/bad_dir/bad.db", &tmp_db));
#endif
  }

  /* 2. Db2 MERGE INTO batch with composite PK, 2 non-PK cols, DO_UPDATE,
   * DO_NOTHING, non_pk == 0, pk_count == 0, and string_builder OOM sweeps */
  ASSERT_EQ(C_ORM_OK, reset_dialect_mock());
  ASSERT_EQ(C_ORM_OK, init_dialect_mock_vtable(&vt, 1));
  memset(&mock_db, 0, sizeof(mock_db));
  mock_db.vtable = &vt;
  mock_db.driver_name = "db2";

  memset(rows, 0, sizeof(rows));
  rows[0].id = 1;
  rows[0].code = "A1";
  (void)c_orm_decimal_from_str("10.50", &rows[0].amount);
  rows[1].id = 2;
  rows[1].code = "A2";
  (void)c_orm_decimal_from_str("20.75", &rows[1].amount);

  memset(comp_cols, 0, sizeof(comp_cols));
  comp_cols[0] = DialectLedger_cols[0]; /* PK 1: id */
  comp_cols[1] = DialectLedger_cols[1]; /* PK 2: code */
  comp_cols[1].is_pk = true;
  comp_cols[2] = DialectLedger_cols[2]; /* non-PK 1: amount */
  comp_cols[3] = DialectLedger_cols[1]; /* non-PK 2: alias */
  comp_cols[3].name = "note";
  comp_cols[3].is_pk = false;

  memset(&comp_meta, 0, sizeof(comp_meta));
  comp_meta.name = "PROD.LEDGER";
  comp_meta.columns = comp_cols;
  comp_meta.num_columns = 4;
  comp_meta.struct_size = sizeof(DialectLedger);
  comp_meta.query_insert = "INSERT INTO LEDGER VALUES (?, ?, ?, ?)";

  ASSERT_EQ(C_ORM_OK,
            c_orm_insert_batch_ext(db, &comp_meta, rows, 2, 2,
                                   C_ORM_ON_CONFLICT_DO_UPDATE, NULL, NULL));
  ASSERT(strstr(g_dm.last_sql, "MERGE INTO " DQ "PROD" DQ "." DQ "LEDGER" DQ) !=
         NULL);
  ASSERT(strstr(g_dm.last_sql, "AND t." DQ "code" DQ " = s." DQ "code" DQ) !=
         NULL);
  ASSERT(strstr(g_dm.last_sql, "WHEN MATCHED THEN UPDATE SET") != NULL);

  ASSERT_EQ(C_ORM_OK,
            c_orm_insert_batch_ext(db, &comp_meta, rows, 2, 2,
                                   C_ORM_ON_CONFLICT_DO_NOTHING, NULL, NULL));
  ASSERT(strstr(g_dm.last_sql, "WHEN MATCHED THEN") == NULL);
  ASSERT(strstr(g_dm.last_sql, "WHEN NOT MATCHED THEN INSERT") != NULL);

#ifdef C_ORM_TEST_ALLOCATOR
  for (step = 0; step < 160; step++) {
    c_orm_mock_string_builder_append_countdown = step;
    (void)c_orm_insert_batch_ext(db, &comp_meta, rows, 2, 2,
                                 C_ORM_ON_CONFLICT_DO_NOTHING, NULL, NULL);
    c_orm_mock_string_builder_append_countdown = step;
    if (c_orm_insert_batch_ext(db, &comp_meta, rows, 2, 2,
                               C_ORM_ON_CONFLICT_DO_UPDATE, NULL,
                               NULL) == C_ORM_OK) {
      c_orm_mock_string_builder_append_countdown = -1;
      break;
    }
    c_orm_mock_string_builder_append_countdown = -1;
  }
  c_orm_mock_string_builder_get_fail = 1;
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_insert_batch_ext(db, &comp_meta, rows, 2, 2,
                                   C_ORM_ON_CONFLICT_DO_UPDATE, NULL, NULL));
  c_orm_mock_string_builder_get_fail = 0;
#endif

  /* All-PK table (non_pk_count == 0) & no-PK table (pk_count == 0) */
  comp_meta.num_columns = 2;
  ASSERT_EQ(C_ORM_OK,
            c_orm_insert_batch_ext(db, &comp_meta, rows, 1, 1,
                                   C_ORM_ON_CONFLICT_DO_UPDATE, NULL, NULL));
#ifdef C_ORM_TEST_ALLOCATOR
  for (step = 0; step < 100; step++) {
    c_orm_mock_string_builder_append_countdown = step;
    if (c_orm_insert_batch_ext(db, &comp_meta, rows, 1, 1,
                               C_ORM_ON_CONFLICT_DO_UPDATE, NULL,
                               NULL) == C_ORM_OK) {
      c_orm_mock_string_builder_append_countdown = -1;
      break;
    }
    c_orm_mock_string_builder_append_countdown = -1;
  }
#endif
  comp_cols[0].is_pk = false;
  comp_cols[1].is_pk = false;
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_insert_batch_ext(db, &comp_meta, rows, 1, 1,
                                   C_ORM_ON_CONFLICT_DO_UPDATE, NULL, NULL));
  comp_cols[0].is_pk = true;

  /* Transactions & upsert_batch on Db2 CLI connection */
  ASSERT_EQ(C_ORM_OK, c_orm_upsert_batch(db, &DialectLedger_meta, rows, 2, 2));
  ASSERT_EQ(C_ORM_OK, c_orm_transaction_begin(db));
  ASSERT_EQ(C_ORM_OK, c_orm_transaction_commit(db));
  ASSERT_EQ(C_ORM_OK, c_orm_transaction_begin(db));
  ASSERT_EQ(C_ORM_OK, c_orm_transaction_rollback(db));
  ASSERT_EQ(C_ORM_OK, db->vtable->disconnect(db));
  ASSERT_EQ(C_ORM_OK, c_orm_db2_set_mock_api(NULL, 0));
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_set_mock_api(NULL));

  /* 3. Db2 FINAL TABLE auto-PK edge cases (non_pk > 0, non_pk == 0, fallback to
   * IDENTITY_VAL_LOCAL, pk_col_name == NULL, SB OOMs) */
  comp_meta.num_columns = 3;
  rows[0].id = 0;
  g_dm.final_table_id = 55;
  ASSERT_EQ(C_ORM_OK, c_orm_insert(&mock_db, &comp_meta, &rows[0]));
  ASSERT_EQ(55, rows[0].id);

  /* final_table_id == 0 -> falls back to get_last_insert_rowid */
  rows[0].id = 0;
  g_dm.final_table_id = 0;
  g_dm.identity_val_id = 88;
  ASSERT_EQ(C_ORM_OK, c_orm_insert(&mock_db, &comp_meta, &rows[0]));
  ASSERT_EQ(88, rows[0].id);

  /* PK-only auto-PK table (non_pk == 0 -> DEFAULT VALUES) */
  comp_meta.num_columns = 1;
  rows[0].id = 0;
  g_dm.final_table_id = 91;
  ASSERT_EQ(C_ORM_OK, c_orm_insert(&mock_db, &comp_meta, &rows[0]));
  ASSERT_EQ(91, rows[0].id);
  ASSERT(strstr(g_dm.last_sql, " DEFAULT VALUES)") != NULL);

  /* No column has is_pk -> falls through cleanly */
  comp_cols[0].is_pk = false;
  comp_meta.num_columns = 3;
  rows[0].id = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_insert(&mock_db, &comp_meta, &rows[0]));
  comp_cols[0].is_pk = true;

#ifdef C_ORM_TEST_ALLOCATOR
  comp_meta.num_columns = 1;
  for (step = 0; step < 60; step++) {
    rows[0].id = 0;
    c_orm_mock_string_builder_append_countdown = step;
    if (c_orm_insert(&mock_db, &comp_meta, &rows[0]) == C_ORM_OK) {
      c_orm_mock_string_builder_append_countdown = -1;
      break;
    }
    c_orm_mock_string_builder_append_countdown = -1;
  }
  comp_meta.num_columns = 3;
  for (step = 0; step < 90; step++) {
    rows[0].id = 0;
    c_orm_mock_string_builder_append_countdown = step;
    if (c_orm_insert(&mock_db, &comp_meta, &rows[0]) == C_ORM_OK) {
      c_orm_mock_string_builder_append_countdown = -1;
      break;
    }
    c_orm_mock_string_builder_append_countdown = -1;
  }
  rows[0].id = 0;
  c_orm_mock_string_builder_get_fail = 1;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_insert(&mock_db, &comp_meta, &rows[0]));
  c_orm_mock_string_builder_get_fail = 0;
#endif
  rows[0].id = 1;

  /* 4. c_orm_upsert & c_orm_upsert_batch across drivers (memory, db2, odbc,
   * postgres, mysql, NULL driver_name, view error) */
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_upsert(NULL, &DialectLedger_meta, &rows[0]));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_upsert_batch(NULL, &DialectLedger_meta, rows, 1, 1));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_upsert_batch(&mock_db, &DialectLedger_meta, NULL, 2, 1));

  comp_meta.is_view = true;
  ASSERT_EQ(C_ORM_ERROR_READ_ONLY,
            c_orm_upsert(&mock_db, &comp_meta, &rows[0]));
  comp_meta.is_view = false;

  mock_db.driver_name = "db2";
  ASSERT_EQ(C_ORM_OK, c_orm_upsert(&mock_db, &DialectLedger_meta, &rows[0]));
  mock_db.driver_name = "odbc";
  ASSERT_EQ(C_ORM_OK, c_orm_upsert(&mock_db, &DialectLedger_meta, &rows[0]));
  mock_db.driver_name = "postgres";
  ASSERT_EQ(C_ORM_OK, c_orm_upsert(&mock_db, &DialectLedger_meta, &rows[0]));
  mock_db.driver_name = "mysql";
  ASSERT_EQ(C_ORM_OK, c_orm_upsert(&mock_db, &DialectLedger_meta, &rows[0]));
  mock_db.driver_name = NULL;
  ASSERT_EQ(C_ORM_OK, c_orm_upsert(&mock_db, &DialectLedger_meta, &rows[0]));

  /* Memory driver upsert (insert + conflict update) & upsert_batch */
  ASSERT_EQ(C_ORM_OK, c_orm_memory_connect(":memory:", &db));
  ASSERT_EQ(C_ORM_OK,
            c_orm_execute_raw(
                db, "CREATE TABLE LEDGER (id INT PRIMARY KEY, code TEXT, "
                    "amount DECIMAL(15,2))"));
  ASSERT_EQ(C_ORM_OK, c_orm_upsert_batch(db, &DialectLedger_meta, rows, 2, 2));
  rows[0].code = "A1_UPDATED";
  ASSERT_EQ(C_ORM_OK, c_orm_upsert(db, &DialectLedger_meta, &rows[0]));
  ASSERT_EQ(C_ORM_OK, db->vtable->disconnect(db));

  /* 5. Db2 savepoints (`ON ROLLBACK RETAIN CURSORS`) */
  mock_db.driver_name = "db2";
  ASSERT_EQ(C_ORM_OK, c_orm_savepoint_create(&mock_db, "sp1"));
  ASSERT_STR_EQ("SAVEPOINT " DQ "sp1" DQ " ON ROLLBACK RETAIN CURSORS",
                g_dm.last_sql);
  ASSERT_EQ(C_ORM_OK, c_orm_savepoint_rollback(&mock_db, "sp1"));
  ASSERT_EQ(C_ORM_OK, c_orm_savepoint_release(&mock_db, "sp1"));

  /* 6. DECIMAL PK & composite key operations (+ bind_decimal == NULL fallback)
   */
  (void)c_orm_decimal_from_str("42.50", &dec_pk);
  key_vars[0].type = CDD_C_VARIANT_TYPE_INT;
  key_vars[0].value.i_val = 1;
  key_vars[1].type = CDD_C_VARIANT_TYPE_DECIMAL;
  key_vars[1].value.d_val = dec_pk;

  ASSERT_EQ(
      C_ORM_ERROR_NULL_POINTER,
      c_orm_find_by_id_decimal(NULL, &DialectLedger_meta, &dec_pk, &out_row));
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_find_by_id_decimal(&mock_db, &DialectLedger_meta, &dec_pk,
                                     &out_row));
  ASSERT_EQ(C_ORM_OK,
            c_orm_update_by_composite_key(&mock_db, &DialectLedger_meta, 2,
                                          key_vars, &rows[0]));
  ASSERT_EQ(C_ORM_OK, c_orm_delete_by_composite_key(
                          &mock_db, &DialectLedger_meta, 2, key_vars));

  /* bind_decimal == NULL fallback to string */
  vt.bind_decimal = NULL;
  ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
            c_orm_find_by_id_decimal(&mock_db, &DialectLedger_meta, &dec_pk,
                                     &out_row));
  ASSERT_STR_EQ("42.50", g_dm.bound_str);
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED,
            c_orm_update_by_composite_key(&mock_db, &DialectLedger_meta, 2,
                                          key_vars, &rows[0]));
  ASSERT_EQ(C_ORM_OK,
            c_orm_update_by_composite_key(&mock_db, &DialectLedger_meta, 1,
                                          key_vars, &rows[0]));

  PASS();
}

/**
 * @brief Test suite entry point for Db2 and ODBC SQL dialects.
 * @param db2_dialect_suite Suite runner function name.
 */
SUITE(db2_dialect_suite) {
  RUN_TEST(test_db2_and_odbc_ast_rendering_and_execution);
  RUN_TEST(test_db2_and_odbc_query_builders_and_crud_upserts);
  RUN_TEST(test_db2_and_odbc_ddl_codegen_introspection_and_migrations);
  RUN_TEST(test_db2_dialect_crud_batch_and_connect_exhaustive);
}
