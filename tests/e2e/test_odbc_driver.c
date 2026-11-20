/**
 * @file test_odbc_driver.c
 * @brief Exhaustive unit, mock-ABI, streaming LOB, batch, and OOM tests for
 * `c_orm_odbc_loader.c` and `c_orm_odbc.c`.
 */

/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_decimal.h"
#include "c_orm_ebcdic.h"
#include "c_orm_odbc.h"
#include "c_orm_odbc_abi.h"
#include "c_orm_safe_crt.h"
#include "c_orm_sqlite.h"
#include "greatest.h"
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32) || defined(_WIN64)
/**
 * @brief Win32 Sleep forward declaration (zero `<windows.h>`).
 * @param dwMilliseconds Duration in milliseconds.
 */
__declspec(dllimport) void __stdcall Sleep(unsigned long dwMilliseconds);
#else
#include <unistd.h>
#endif
/* clang-format on */

/**
 * @brief Scripted state for the deterministic in-memory mock ODBC 3.52 driver.
 */
typedef struct mock_odbc_state {
  /** @brief Captured connection string from SQLDriverConnect(W)/SQLConnect. */
  char last_conn_str[1024];
  /** @brief Captured prepared SQL text. */
  char last_sql[1024];
  /** @brief Number of SQLExecute calls. */
  int exec_count;
  /** @brief Number of SQLEndTran COMMIT calls. */
  int commit_count;
  /** @brief Number of SQLEndTran ROLLBACK calls. */
  int rollback_count;
  /** @brief Last autocommit attribute set (1 or 0). */
  unsigned long autocommit_mode;
  /** @brief Last isolation bitmask set. */
  unsigned long isolation_mask;
  /** @brief Last login timeout set. */
  unsigned long login_timeout;
  /** @brief Last batch PARAMSET_SIZE set. */
  size_t batch_paramset_size;
  /** @brief Reject SQL_C_NUMERIC bind/getdata to test ODBC 2.x fallback. */
  int reject_c_numeric;
  /** @brief Force failure on specific API name (or empty). */
  const char *fail_on_op;
  /** @brief SQLSTATE to emit on next SQLGetDiagRec(W). */
  char diag_sqlstate[6];
  /** @brief Native error code to emit on next SQLGetDiagRec(W). */
  c_orm_sqlinteger_t diag_native;
  /** @brief Primary diagnostic message. */
  const char *diag_msg1;
  /** @brief Optional second diagnostic message for multi-record trace test. */
  const char *diag_msg2;
  /** @brief Result mode: 0=DML(0 cols), 1=standard rowset, 2=large LOBs,
   * 3=tables catalog, 4=identity. */
  int result_mode;
  /** @brief Total rows to return in SQLFetch. */
  int total_rows;
  /** @brief Current 1-based fetched row index. */
  int cur_row;
  /** @brief Streaming byte offset inside column for multi-chunk SQLGetData. */
  size_t stream_offset[8];
  /** @brief Captured bound parameter count. */
  int bound_param_max;
  /** @brief Captured bound C type per 1-based param. */
  c_orm_sqlsmallint_t bound_c_type[16];
  /** @brief Captured bound SQL type per 1-based param. */
  c_orm_sqlsmallint_t bound_sql_type[16];
  /** @brief Captured bound value pointer per 1-based param. */
  c_orm_sqlpointer_t bound_val_ptr[16];
  /** @brief Captured bound indicator pointer per 1-based param. */
  c_orm_sqllen_t *bound_ind_ptr[16];
  /** @brief Custom SQL column type for single-col mode 6. */
  c_orm_sqlsmallint_t custom_col_type;
  /** @brief Custom string payload for single-col mode 6. */
  const char *custom_str;
  /** @brief Fail after N SQLGetData calls when > 0. */
  int getdata_fail_after;
  /** @brief Emit 0 length on DescribeColW / GetDiagRecW to test NUL scan. */
  int zero_wide_len;
  /** @brief Emit >2048 bytes of multi-record diagnostics. */
  int flood_diag_recs;
  /** @brief Leave batch processed == 0 so row_count fallback is used. */
  int batch_zero_proc;
  /** @brief Sleep 2ms in SQLExecute to trigger slow_query_threshold_ms. */
  int sleep_exec;
  /** @brief Return SQL_NO_DATA on Nth SQLGetData call when > 0. */
  int getdata_nodata_after;
} mock_odbc_state_t;

static mock_odbc_state_t g_mock;
static int g_dummy_env = 101;
static int g_dummy_dbc = 202;
static int g_dummy_stmt = 303;
static char g_large_clob[70000];
static unsigned char g_large_blob[68000];

/**
 * @brief Resets `g_mock` and deterministic large CLOB/BLOB test payloads.
 * @return C_ORM_OK on completion.
 */
static c_orm_error_t reset_mock_state(void) {
  size_t i;
  memset(&g_mock, 0, sizeof(g_mock));
  g_mock.autocommit_mode = 1UL;
  (void)C_ORM_STRCPY(g_mock.diag_sqlstate, sizeof(g_mock.diag_sqlstate),
                     "HY000");
  g_mock.diag_native = -1;
  g_mock.diag_msg1 = "Mock ODBC error";
  g_mock.diag_msg2 = NULL;
  for (i = 0; i < sizeof(g_large_clob) - 1U; i++) {
    g_large_clob[i] = (char)('A' + (i % 26U));
  }
  g_large_clob[sizeof(g_large_clob) - 1U] = '\0';
  for (i = 0; i < sizeof(g_large_blob); i++) {
    g_large_blob[i] = (unsigned char)(i & 0xFFU);
  }
  return C_ORM_OK;
}

/**
 * @brief Checks if `op` matches `g_mock.fail_on_op`.
 * @param op Operation name.
 * @param out_fail Output set to 1 if `op` should fail, 0 otherwise.
 * @return C_ORM_OK on completion.
 */
static c_orm_error_t should_fail(const char *op, int *out_fail) {
  *out_fail = (g_mock.fail_on_op && strcmp(g_mock.fail_on_op, op) == 0) ? 1 : 0;
  return C_ORM_OK;
}

/**
 * @brief Mock SQLAllocHandle.
 * @param HandleType Handle type identifier.
 * @param InputHandle Parent handle.
 * @param OutputHandlePtr Output handle pointer.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLMock_AllocHandle(
    c_orm_sqlsmallint_t HandleType, c_orm_sqlhandle_t InputHandle,
    c_orm_sqlhandle_t *OutputHandlePtr) {
  int fail = 0;
  (void)InputHandle;
  if (HandleType == C_ORM_SQL_HANDLE_ENV) {
    if (should_fail("AllocEnv", &fail) != C_ORM_OK) {
      fail = 1;
    }
    if (fail) {
      return C_ORM_SQL_ERROR;
    }
    *OutputHandlePtr = &g_dummy_env;
  } else if (HandleType == C_ORM_SQL_HANDLE_DBC) {
    if (should_fail("AllocDbc", &fail) != C_ORM_OK) {
      fail = 1;
    }
    if (fail) {
      return C_ORM_SQL_ERROR;
    }
    *OutputHandlePtr = &g_dummy_dbc;
  } else {
    if (should_fail("AllocStmt", &fail) != C_ORM_OK) {
      fail = 1;
    }
    if (fail) {
      return C_ORM_SQL_ERROR;
    }
    *OutputHandlePtr = &g_dummy_stmt;
  }
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLFreeHandle.
 * @param HandleType Handle type identifier.
 * @param Handle Target handle to free.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API
SQLMock_FreeHandle(c_orm_sqlsmallint_t HandleType, c_orm_sqlhandle_t Handle) {
  (void)HandleType;
  (void)Handle;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLSetEnvAttr.
 * @param EnvironmentHandle Environment handle.
 * @param Attribute Attribute code.
 * @param ValuePtr Attribute value pointer.
 * @param StringLength Value byte length.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLMock_SetEnvAttr(
    c_orm_sqlhenv_t EnvironmentHandle, c_orm_sqlinteger_t Attribute,
    c_orm_sqlpointer_t ValuePtr, c_orm_sqlinteger_t StringLength) {
  int fail = 0;
  (void)EnvironmentHandle;
  (void)Attribute;
  (void)ValuePtr;
  (void)StringLength;
  if (should_fail("SetEnvAttr", &fail) != C_ORM_OK) {
    fail = 1;
  }
  return fail ? C_ORM_SQL_ERROR : C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLSetConnectAttr.
 * @param ConnectionHandle Connection handle.
 * @param Attribute Attribute code.
 * @param ValuePtr Attribute value pointer.
 * @param StringLength Value byte length.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLMock_SetConnectAttr(
    c_orm_sqlhdbc_t ConnectionHandle, c_orm_sqlinteger_t Attribute,
    c_orm_sqlpointer_t ValuePtr, c_orm_sqlinteger_t StringLength) {
  int fail = 0;
  (void)ConnectionHandle;
  (void)StringLength;
  if (should_fail("SetConnectAttr", &fail) != C_ORM_OK) {
    fail = 1;
  }
  if (fail) {
    return C_ORM_SQL_ERROR;
  }
  if (Attribute == C_ORM_SQL_ATTR_AUTOCOMMIT) {
    g_mock.autocommit_mode = (unsigned long)(size_t)ValuePtr;
  } else if (Attribute == C_ORM_SQL_ATTR_TXN_ISOLATION) {
    g_mock.isolation_mask = (unsigned long)(size_t)ValuePtr;
  } else if (Attribute == C_ORM_SQL_ATTR_LOGIN_TIMEOUT) {
    g_mock.login_timeout = (unsigned long)(size_t)ValuePtr;
  }
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLDriverConnect (ANSI/UTF-8).
 * @param ConnectionHandle Connection handle.
 * @param WindowHandle Window handle.
 * @param InConnectionString Input connection string.
 * @param StringLength1 Input length.
 * @param OutConnectionString Output buffer.
 * @param BufferLength Output buffer capacity.
 * @param StringLength2Ptr Output written length.
 * @param DriverCompletion Prompt mode.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLMock_DriverConnect(
    c_orm_sqlhdbc_t ConnectionHandle, c_orm_sqlhwnd_t WindowHandle,
    c_orm_sqlchar_t *InConnectionString, c_orm_sqlsmallint_t StringLength1,
    c_orm_sqlchar_t *OutConnectionString, c_orm_sqlsmallint_t BufferLength,
    c_orm_sqlsmallint_t *StringLength2Ptr,
    c_orm_sqlusmallint_t DriverCompletion) {
  int fail = 0;
  (void)ConnectionHandle;
  (void)WindowHandle;
  (void)StringLength1;
  (void)OutConnectionString;
  (void)BufferLength;
  (void)StringLength2Ptr;
  (void)DriverCompletion;
  if (should_fail("DriverConnect", &fail) != C_ORM_OK) {
    fail = 1;
  }
  if (fail) {
    return C_ORM_SQL_ERROR;
  }
  if (InConnectionString) {
    (void)C_ORM_STRCPY(g_mock.last_conn_str, sizeof(g_mock.last_conn_str),
                       (const char *)InConnectionString);
  }
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLDriverConnectW (UTF-16).
 * @param ConnectionHandle Connection handle.
 * @param WindowHandle Window handle.
 * @param InConnectionString Input wide connection string.
 * @param StringLength1 Input code unit length.
 * @param OutConnectionString Output wide buffer.
 * @param BufferLength Output buffer capacity.
 * @param StringLength2Ptr Output written length.
 * @param DriverCompletion Prompt mode.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLMock_DriverConnectW(
    c_orm_sqlhdbc_t ConnectionHandle, c_orm_sqlhwnd_t WindowHandle,
    c_orm_sqlwchar_t *InConnectionString, c_orm_sqlsmallint_t StringLength1,
    c_orm_sqlwchar_t *OutConnectionString, c_orm_sqlsmallint_t BufferLength,
    c_orm_sqlsmallint_t *StringLength2Ptr,
    c_orm_sqlusmallint_t DriverCompletion) {
  int fail = 0;
  size_t units = 0;
  (void)ConnectionHandle;
  (void)WindowHandle;
  (void)OutConnectionString;
  (void)BufferLength;
  (void)StringLength2Ptr;
  (void)DriverCompletion;
  if (should_fail("DriverConnectW", &fail) != C_ORM_OK) {
    fail = 1;
  }
  if (fail) {
    return C_ORM_SQL_ERROR;
  }
  if (InConnectionString) {
    if (StringLength1 > 0) {
      units = (size_t)StringLength1;
    } else {
      while (InConnectionString[units] != 0) {
        units++;
      }
    }
    (void)c_orm_utf16_to_utf8(InConnectionString, units, 0,
                              g_mock.last_conn_str,
                              sizeof(g_mock.last_conn_str), NULL);
  }
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLConnect fallback.
 * @param ConnectionHandle Connection handle.
 * @param ServerName DSN name.
 * @param NameLength1 DSN length.
 * @param UserName User ID.
 * @param NameLength2 User length.
 * @param Authentication Password.
 * @param NameLength3 Password length.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLMock_Connect(
    c_orm_sqlhdbc_t ConnectionHandle, c_orm_sqlchar_t *ServerName,
    c_orm_sqlsmallint_t NameLength1, c_orm_sqlchar_t *UserName,
    c_orm_sqlsmallint_t NameLength2, c_orm_sqlchar_t *Authentication,
    c_orm_sqlsmallint_t NameLength3) {
  (void)ConnectionHandle;
  (void)NameLength1;
  (void)UserName;
  (void)NameLength2;
  (void)Authentication;
  (void)NameLength3;
  if (ServerName) {
    (void)C_ORM_STRCPY(g_mock.last_conn_str, sizeof(g_mock.last_conn_str),
                       (const char *)ServerName);
  }
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLDisconnect.
 * @param ConnectionHandle Connection handle.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API
SQLMock_Disconnect(c_orm_sqlhdbc_t ConnectionHandle) {
  (void)ConnectionHandle;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLEndTran.
 * @param HandleType Handle type.
 * @param Handle Target handle.
 * @param CompletionType Commit or rollback action.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API
SQLMock_EndTran(c_orm_sqlsmallint_t HandleType, c_orm_sqlhandle_t Handle,
                c_orm_sqlsmallint_t CompletionType) {
  int fail = 0;
  (void)HandleType;
  (void)Handle;
  if (should_fail("EndTran", &fail) != C_ORM_OK) {
    fail = 1;
  }
  if (fail) {
    return C_ORM_SQL_ERROR;
  }
  if (CompletionType == C_ORM_SQL_COMMIT) {
    g_mock.commit_count++;
  } else {
    g_mock.rollback_count++;
  }
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLSetStmtAttr.
 * @param StatementHandle Statement handle.
 * @param Attribute Attribute code.
 * @param ValuePtr Value pointer.
 * @param StringLength Value byte length.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLMock_SetStmtAttr(
    c_orm_sqlhstmt_t StatementHandle, c_orm_sqlinteger_t Attribute,
    c_orm_sqlpointer_t ValuePtr, c_orm_sqlinteger_t StringLength) {
  int fail = 0;
  (void)StatementHandle;
  (void)StringLength;
  if (should_fail("SetStmtAttr", &fail) != C_ORM_OK) {
    fail = 1;
  }
  if (fail) {
    return C_ORM_SQL_ERROR;
  }
  if (Attribute == C_ORM_SQL_ATTR_PARAMSET_SIZE) {
    g_mock.batch_paramset_size = (size_t)ValuePtr;
  } else if (Attribute == C_ORM_SQL_ATTR_PARAMS_PROCESSED_PTR && ValuePtr) {
    *(c_orm_sqlulen_t *)ValuePtr =
        g_mock.batch_zero_proc ? 0U
                               : (c_orm_sqlulen_t)g_mock.batch_paramset_size;
  }
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLPrepare (ANSI/UTF-8).
 * @param StatementHandle Statement handle.
 * @param StatementText SQL text.
 * @param TextLength SQL byte length.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API
SQLMock_Prepare(c_orm_sqlhstmt_t StatementHandle,
                c_orm_sqlchar_t *StatementText, c_orm_sqlinteger_t TextLength) {
  int fail = 0;
  (void)StatementHandle;
  (void)TextLength;
  if (should_fail("Prepare", &fail) != C_ORM_OK) {
    fail = 1;
  }
  if (fail) {
    return C_ORM_SQL_ERROR;
  }
  if (StatementText) {
    (void)C_ORM_STRCPY(g_mock.last_sql, sizeof(g_mock.last_sql),
                       (const char *)StatementText);
    if (strstr(g_mock.last_sql, "@@IDENTITY") != NULL) {
      g_mock.result_mode = 4;
      g_mock.total_rows = 1;
    }
  }
  g_mock.cur_row = 0;
  memset(g_mock.stream_offset, 0, sizeof(g_mock.stream_offset));
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLPrepareW (UTF-16).
 * @param StatementHandle Statement handle.
 * @param StatementText Wide SQL text.
 * @param TextLength Wide unit length.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLMock_PrepareW(
    c_orm_sqlhstmt_t StatementHandle, c_orm_sqlwchar_t *StatementText,
    c_orm_sqlinteger_t TextLength) {
  int fail = 0;
  size_t units = (TextLength > 0) ? (size_t)TextLength : 0;
  (void)StatementHandle;
  if (should_fail("PrepareW", &fail) != C_ORM_OK) {
    fail = 1;
  }
  if (fail) {
    return C_ORM_SQL_ERROR;
  }
  if (StatementText) {
    if (units == 0) {
      while (StatementText[units] != 0) {
        units++;
      }
    }
    (void)c_orm_utf16_to_utf8(StatementText, units, 0, g_mock.last_sql,
                              sizeof(g_mock.last_sql), NULL);
    if (strstr(g_mock.last_sql, "@@IDENTITY") != NULL) {
      g_mock.result_mode = 4;
      g_mock.total_rows = 1;
    }
  }
  g_mock.cur_row = 0;
  memset(g_mock.stream_offset, 0, sizeof(g_mock.stream_offset));
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLExecute.
 * @param StatementHandle Statement handle.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API
SQLMock_Execute(c_orm_sqlhstmt_t StatementHandle) {
  int fail = 0;
  int nodata = 0;
  (void)StatementHandle;
  if (g_mock.sleep_exec) {
#if defined(_WIN32) || defined(_WIN64)
    Sleep(5);
#else
    usleep(2500);
#endif
  }
  if (should_fail("Execute", &fail) != C_ORM_OK) {
    fail = 1;
  }
  if (fail) {
    return C_ORM_SQL_ERROR;
  }
  if (should_fail("ExecuteNoData", &nodata) != C_ORM_OK) {
    nodata = 0;
  }
  g_mock.exec_count++;
  g_mock.cur_row = 0;
  memset(g_mock.stream_offset, 0, sizeof(g_mock.stream_offset));
  return nodata ? C_ORM_SQL_NO_DATA : C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLBindParameter.
 * @param StatementHandle Statement handle.
 * @param ParameterNumber 1-based parameter number.
 * @param InputOutputType Direction indicator.
 * @param ValueType C value type.
 * @param ParameterType SQL parameter type.
 * @param ColumnSize Precision/size.
 * @param DecimalDigits Scale.
 * @param ParameterValuePtr Bound data buffer.
 * @param BufferLength Buffer capacity.
 * @param StrLen_or_IndPtr Bound length/NULL indicator pointer.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLMock_BindParameter(
    c_orm_sqlhstmt_t StatementHandle, c_orm_sqlusmallint_t ParameterNumber,
    c_orm_sqlsmallint_t InputOutputType, c_orm_sqlsmallint_t ValueType,
    c_orm_sqlsmallint_t ParameterType, c_orm_sqlulen_t ColumnSize,
    c_orm_sqlsmallint_t DecimalDigits, c_orm_sqlpointer_t ParameterValuePtr,
    c_orm_sqllen_t BufferLength, c_orm_sqllen_t *StrLen_or_IndPtr) {
  int fail = 0;
  (void)StatementHandle;
  (void)InputOutputType;
  (void)ColumnSize;
  (void)DecimalDigits;
  (void)BufferLength;
  if (ValueType == C_ORM_SQL_C_NUMERIC && g_mock.reject_c_numeric) {
    return C_ORM_SQL_ERROR;
  }
  if (should_fail("BindParameter", &fail) != C_ORM_OK) {
    fail = 1;
  }
  if (fail) {
    return C_ORM_SQL_ERROR;
  }
  if (ParameterNumber >= 1 && ParameterNumber < 16) {
    if ((int)ParameterNumber > g_mock.bound_param_max) {
      g_mock.bound_param_max = (int)ParameterNumber;
    }
    g_mock.bound_c_type[ParameterNumber] = ValueType;
    g_mock.bound_sql_type[ParameterNumber] = ParameterType;
    g_mock.bound_val_ptr[ParameterNumber] = ParameterValuePtr;
    g_mock.bound_ind_ptr[ParameterNumber] = StrLen_or_IndPtr;
  }
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLNumResultCols.
 * @param StatementHandle Statement handle.
 * @param ColumnCountPtr Output column count.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLMock_NumResultCols(
    c_orm_sqlhstmt_t StatementHandle, c_orm_sqlsmallint_t *ColumnCountPtr) {
  int fail = 0;
  (void)StatementHandle;
  if (should_fail("NumResultCols", &fail) != C_ORM_OK) {
    fail = 1;
  }
  if (fail) {
    return C_ORM_SQL_ERROR;
  }
  if (g_mock.result_mode == 1) {
    *ColumnCountPtr = 7;
  } else if (g_mock.result_mode == 2) {
    *ColumnCountPtr = 3;
  } else if (g_mock.result_mode == 4 || g_mock.result_mode >= 6) {
    *ColumnCountPtr = 1;
  } else {
    *ColumnCountPtr = 0;
  }
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLDescribeCol (ANSI).
 * @param StatementHandle Statement handle.
 * @param ColumnNumber 1-based column index.
 * @param ColumnName Output column name buffer.
 * @param BufferLength Name buffer capacity.
 * @param NameLengthPtr Output name length.
 * @param DataTypePtr Output SQL data type.
 * @param ColumnSizePtr Output column precision.
 * @param DecimalDigitsPtr Output scale.
 * @param NullablePtr Output nullability.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLMock_DescribeCol(
    c_orm_sqlhstmt_t StatementHandle, c_orm_sqlusmallint_t ColumnNumber,
    c_orm_sqlchar_t *ColumnName, c_orm_sqlsmallint_t BufferLength,
    c_orm_sqlsmallint_t *NameLengthPtr, c_orm_sqlsmallint_t *DataTypePtr,
    c_orm_sqlulen_t *ColumnSizePtr, c_orm_sqlsmallint_t *DecimalDigitsPtr,
    c_orm_sqlsmallint_t *NullablePtr) {
  static const char *const m1_names[7] = {
      "id", "big_id", "ratio", "title", "amount", "raw_bin", "opt_null"};
  static const c_orm_sqlsmallint_t m1_types[7] = {
      C_ORM_SQL_INTEGER, C_ORM_SQL_BIGINT,  C_ORM_SQL_DOUBLE,
      C_ORM_SQL_VARCHAR, C_ORM_SQL_DECIMAL, C_ORM_SQL_VARBINARY,
      C_ORM_SQL_VARCHAR};
  static const char *const m2_names[3] = {"huge_clob", "huge_blob", "wide_col"};
  static const c_orm_sqlsmallint_t m2_types[3] = {
      C_ORM_SQL_CLOB, C_ORM_SQL_BLOB, C_ORM_SQL_WVARCHAR};
  int fail = 0;

  (void)StatementHandle;
  if (should_fail("DescribeCol", &fail) != C_ORM_OK) {
    fail = 1;
  }
  if (fail) {
    return C_ORM_SQL_ERROR;
  }
  *ColumnSizePtr = 255;
  *DecimalDigitsPtr = 2;
  *NullablePtr = 1;

  if (g_mock.result_mode == 1 && ColumnNumber >= 1 && ColumnNumber <= 7) {
    (void)C_ORM_STRCPY((char *)ColumnName, (size_t)BufferLength,
                       m1_names[ColumnNumber - 1]);
    *NameLengthPtr = (c_orm_sqlsmallint_t)strlen((const char *)ColumnName);
    *DataTypePtr = m1_types[ColumnNumber - 1];
  } else if (g_mock.result_mode == 2 && ColumnNumber >= 1 &&
             ColumnNumber <= 3) {
    (void)C_ORM_STRCPY((char *)ColumnName, (size_t)BufferLength,
                       m2_names[ColumnNumber - 1]);
    *NameLengthPtr = (c_orm_sqlsmallint_t)strlen((const char *)ColumnName);
    *DataTypePtr = m2_types[ColumnNumber - 1];
  } else if (g_mock.result_mode >= 6) {
    (void)C_ORM_STRCPY((char *)ColumnName, (size_t)BufferLength, "c0");
    *NameLengthPtr = 2;
    *DataTypePtr = g_mock.custom_col_type;
  } else {
    (void)C_ORM_STRCPY((char *)ColumnName, (size_t)BufferLength, "ident");
    *NameLengthPtr = 5;
    *DataTypePtr = C_ORM_SQL_BIGINT;
  }
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLDescribeColW (UTF-16).
 * @param StatementHandle Statement handle.
 * @param ColumnNumber 1-based column index.
 * @param ColumnName Output wide column name buffer.
 * @param BufferLength Capacity in code units.
 * @param NameLengthPtr Output name code unit length.
 * @param DataTypePtr Output SQL data type.
 * @param ColumnSizePtr Output column precision.
 * @param DecimalDigitsPtr Output scale.
 * @param NullablePtr Output nullability.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLMock_DescribeColW(
    c_orm_sqlhstmt_t StatementHandle, c_orm_sqlusmallint_t ColumnNumber,
    c_orm_sqlwchar_t *ColumnName, c_orm_sqlsmallint_t BufferLength,
    c_orm_sqlsmallint_t *NameLengthPtr, c_orm_sqlsmallint_t *DataTypePtr,
    c_orm_sqlulen_t *ColumnSizePtr, c_orm_sqlsmallint_t *DecimalDigitsPtr,
    c_orm_sqlsmallint_t *NullablePtr) {
  c_orm_sqlchar_t aname[128];
  c_orm_sqlsmallint_t alen = 0;
  size_t units = 0;
  int fail = 0;
  if (should_fail("DescribeColW", &fail) != C_ORM_OK) {
    fail = 1;
  }
  if (fail) {
    return C_ORM_SQL_ERROR;
  }
  (void)SQLMock_DescribeCol(
      StatementHandle, ColumnNumber, aname, (c_orm_sqlsmallint_t)sizeof(aname),
      &alen, DataTypePtr, ColumnSizePtr, DecimalDigitsPtr, NullablePtr);
  (void)c_orm_utf8_to_utf16((const char *)aname, (size_t)alen, 0, ColumnName,
                            (size_t)BufferLength, &units);
  *NameLengthPtr = g_mock.zero_wide_len ? 0 : (c_orm_sqlsmallint_t)units;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLFetch.
 * @param StatementHandle Statement handle.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API
SQLMock_Fetch(c_orm_sqlhstmt_t StatementHandle) {
  int fail = 0;
  (void)StatementHandle;
  if (should_fail("Fetch", &fail) != C_ORM_OK) {
    fail = 1;
  }
  if (fail) {
    return C_ORM_SQL_ERROR;
  }
  memset(g_mock.stream_offset, 0, sizeof(g_mock.stream_offset));
  if (g_mock.cur_row >= g_mock.total_rows) {
    return C_ORM_SQL_NO_DATA;
  }
  g_mock.cur_row++;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLGetData supporting inline, binary numeric, and multi-chunk
 * LOBs.
 * @param StatementHandle Statement handle.
 * @param Col_or_Param_Num 1-based column index.
 * @param TargetType Requested C target type.
 * @param TargetValuePtr Destination buffer.
 * @param BufferLength Destination capacity in bytes.
 * @param StrLen_or_IndPtr Output byte length or NULL indicator.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLMock_GetData(
    c_orm_sqlhstmt_t StatementHandle, c_orm_sqlusmallint_t Col_or_Param_Num,
    c_orm_sqlsmallint_t TargetType, c_orm_sqlpointer_t TargetValuePtr,
    c_orm_sqllen_t BufferLength, c_orm_sqllen_t *StrLen_or_IndPtr) {
  int fail = 0;
  (void)StatementHandle;
  if (should_fail("GetData", &fail) != C_ORM_OK) {
    fail = 1;
  }
  if (fail) {
    return C_ORM_SQL_ERROR;
  }
  if (g_mock.getdata_fail_after > 0) {
    g_mock.getdata_fail_after--;
    if (g_mock.getdata_fail_after == 0) {
      return C_ORM_SQL_ERROR;
    }
  }
  if (g_mock.getdata_nodata_after > 0) {
    g_mock.getdata_nodata_after--;
    if (g_mock.getdata_nodata_after == 0) {
      return C_ORM_SQL_NO_DATA;
    }
  }

  /* Catalog SQLColumns mode (mode 5) */
  if (g_mock.result_mode == 5) {
    if (g_mock.cur_row == 1) {
      /* Row 1: NULL column name -> skipped */
      *StrLen_or_IndPtr = C_ORM_SQL_NULL_DATA;
      return C_ORM_SQL_SUCCESS;
    }
    if (Col_or_Param_Num == 4 && g_mock.cur_row == 10) {
      /* Empty cname -> skipped */
      ((char *)TargetValuePtr)[0] = '\0';
      *StrLen_or_IndPtr = 0;
      return C_ORM_SQL_SUCCESS;
    }
    if (Col_or_Param_Num == 4 && g_mock.cur_row == 11) {
      /* SQL_ERROR on cname -> skipped */
      return C_ORM_SQL_ERROR;
    }
    if (Col_or_Param_Num == 4) {
      char cn[32];
      (void)C_ORM_SPRINTF(cn, sizeof(cn), "COL_%02d", g_mock.cur_row);
      (void)C_ORM_STRCPY((char *)TargetValuePtr, (size_t)BufferLength, cn);
      *StrLen_or_IndPtr = (c_orm_sqllen_t)strlen(cn);
      return C_ORM_SQL_SUCCESS;
    }
    if (Col_or_Param_Num == 6) {
      if (g_mock.cur_row == 2) {
        *StrLen_or_IndPtr = C_ORM_SQL_NULL_DATA;
      } else if (g_mock.cur_row == 3) {
        ((char *)TargetValuePtr)[0] = '\0';
        *StrLen_or_IndPtr = 0;
      } else {
        (void)C_ORM_STRCPY((char *)TargetValuePtr, (size_t)BufferLength,
                           "DECIMAL");
        *StrLen_or_IndPtr = 7;
      }
      return C_ORM_SQL_SUCCESS;
    }
    if (Col_or_Param_Num == 7) {
      if (g_mock.cur_row == 2) {
        *StrLen_or_IndPtr = C_ORM_SQL_NULL_DATA;
      } else {
        *(c_orm_sqlinteger_t *)TargetValuePtr = 18;
        *StrLen_or_IndPtr = 4;
      }
      return C_ORM_SQL_SUCCESS;
    }
    if (Col_or_Param_Num == 9) {
      if (g_mock.cur_row == 2) {
        *StrLen_or_IndPtr = C_ORM_SQL_NULL_DATA;
      } else {
        *(c_orm_sqlsmallint_t *)TargetValuePtr = 2;
        *StrLen_or_IndPtr = 2;
      }
      return C_ORM_SQL_SUCCESS;
    }
    if (Col_or_Param_Num == 18) {
      const char *ns = (g_mock.cur_row % 2 == 0) ? "NO" : "YES";
      (void)C_ORM_STRCPY((char *)TargetValuePtr, (size_t)BufferLength, ns);
      *StrLen_or_IndPtr = (c_orm_sqllen_t)strlen(ns);
      return C_ORM_SQL_SUCCESS;
    }
  }

  /* Mode 6: custom single-column string / NULL / SQL_NUMERIC edge cases */
  if (g_mock.result_mode == 6) {
    if (!g_mock.custom_str) {
      *StrLen_or_IndPtr = C_ORM_SQL_NULL_DATA;
      return C_ORM_SQL_SUCCESS;
    }
    if (TargetType == C_ORM_SQL_C_NUMERIC) {
      c_orm_sql_numeric_struct_t *ns =
          (c_orm_sql_numeric_struct_t *)TargetValuePtr;
      if (strcmp(g_mock.custom_str, "NUM_NULL") == 0) {
        *StrLen_or_IndPtr = C_ORM_SQL_NULL_DATA;
        return C_ORM_SQL_SUCCESS;
      }
      if (strcmp(g_mock.custom_str, "NUM_BAD_PREC") == 0) {
        ns->precision = 99;
        ns->scale = -1;
        ns->sign = 1;
        *StrLen_or_IndPtr = (c_orm_sqllen_t)sizeof(*ns);
        return C_ORM_SQL_SUCCESS;
      }
      if (strcmp(g_mock.custom_str, "NUM_BIG") == 0) {
        c_orm_decimal_t d;
        c_orm_decimal_from_str("999999999999999999999999999999", &d);
        c_orm_decimal_to_odbc_numeric(&d, ns->val, &ns->precision, &ns->scale,
                                      &ns->sign);
        ns->scale = -2;
        *StrLen_or_IndPtr = (c_orm_sqllen_t)sizeof(*ns);
        return C_ORM_SQL_SUCCESS;
      }
      return C_ORM_SQL_ERROR;
    }
    {
      size_t sl = strlen(g_mock.custom_str);
      size_t off = g_mock.stream_offset[1];
      size_t rem = sl - off;
      size_t cap_c = (size_t)BufferLength - 1U;
      if (off >= sl) {
        return C_ORM_SQL_NO_DATA;
      }
      if (rem > cap_c) {
        memcpy(TargetValuePtr, g_mock.custom_str + off, cap_c);
        ((char *)TargetValuePtr)[cap_c] = '\0';
        g_mock.stream_offset[1] += cap_c;
        *StrLen_or_IndPtr = (c_orm_sqllen_t)rem;
        return C_ORM_SQL_SUCCESS_WITH_INFO;
      }
      memcpy(TargetValuePtr, g_mock.custom_str + off, rem + 1U);
      g_mock.stream_offset[1] = sl;
      *StrLen_or_IndPtr = (c_orm_sqllen_t)rem;
      return C_ORM_SQL_SUCCESS;
    }
  }

  /* Mode 7: wide inline >42 3-byte BMP chars (utf8_cap > 128) & surrogates */
  if (g_mock.result_mode == 7) {
    c_orm_sqlwchar_t *wdst = (c_orm_sqlwchar_t *)TargetValuePtr;
    if (!g_mock.custom_str) {
      *StrLen_or_IndPtr = C_ORM_SQL_NULL_DATA;
      return C_ORM_SQL_SUCCESS;
    }
    if (strcmp(g_mock.custom_str, "WIDE_SHORT") == 0) {
      wdst[0] = 'H';
      wdst[1] = 'i';
      wdst[2] = 0;
      *StrLen_or_IndPtr = 4;
      return C_ORM_SQL_SUCCESS;
    }
    if (strcmp(g_mock.custom_str, "WIDE_BMP50") == 0) {
      size_t k;
      for (k = 0; k < 50; k++) {
        wdst[k] = 0x20ACU; /* Euro sign -> 3 UTF-8 bytes each = 150 > 128 */
      }
      wdst[50] = 0;
      *StrLen_or_IndPtr = 100;
      return C_ORM_SQL_SUCCESS;
    }
    if (strcmp(g_mock.custom_str, "WIDE_BAD_SURR") == 0) {
      wdst[0] = 0xD800U; /* Unpaired high surrogate */
      wdst[1] = 0;
      *StrLen_or_IndPtr = 2;
      return C_ORM_SQL_SUCCESS;
    }
    if (strcmp(g_mock.custom_str, "WIDE_STREAM_NO_TOTAL") == 0 ||
        strcmp(g_mock.custom_str, "WIDE_STREAM_UNK") == 0) {
      /* 300 wide units with small positive ind vs SQL_NO_TOTAL */
      size_t total_u = 300;
      size_t off_u = g_mock.stream_offset[1];
      size_t cap_u = ((size_t)BufferLength / sizeof(c_orm_sqlwchar_t)) - 1U;
      size_t rem_u;
      size_t k;
      if (off_u >= total_u) {
        return C_ORM_SQL_NO_DATA;
      }
      rem_u = total_u - off_u;
      if (rem_u > cap_u) {
        for (k = 0; k < cap_u; k++) {
          wdst[k] = 'W';
        }
        wdst[cap_u] = 0;
        g_mock.stream_offset[1] += cap_u;
        *StrLen_or_IndPtr = (off_u == 0 && strcmp(g_mock.custom_str,
                                                  "WIDE_STREAM_NO_TOTAL") == 0)
                                ? 140
                                : C_ORM_SQL_NO_TOTAL;
        return C_ORM_SQL_SUCCESS_WITH_INFO;
      }
      for (k = 0; k < rem_u; k++) {
        wdst[k] = (g_mock.zero_wide_len && k == rem_u - 1U) ? 0xD800U : 'W';
      }
      wdst[rem_u] = 0;
      g_mock.stream_offset[1] = total_u;
      *StrLen_or_IndPtr = (c_orm_sqllen_t)(rem_u * sizeof(c_orm_sqlwchar_t));
      return C_ORM_SQL_SUCCESS;
    }
  }

  /* Catalog SQLTables mode */
  if (g_mock.result_mode == 3) {
    char tbl[32];
    if (g_mock.cur_row == 1 && g_mock.zero_wide_len) {
      *StrLen_or_IndPtr = C_ORM_SQL_NULL_DATA;
      return C_ORM_SQL_SUCCESS;
    }
    if (g_mock.cur_row == 2 && g_mock.zero_wide_len) {
      ((char *)TargetValuePtr)[0] = '\0';
      *StrLen_or_IndPtr = 0;
      return C_ORM_SQL_SUCCESS;
    }
    if (g_mock.cur_row == 3 && g_mock.zero_wide_len) {
      return C_ORM_SQL_ERROR;
    }
    (void)C_ORM_SPRINTF(tbl, sizeof(tbl), "TBL_%02d", g_mock.cur_row);
    (void)C_ORM_STRCPY((char *)TargetValuePtr, (size_t)BufferLength, tbl);
    *StrLen_or_IndPtr = (c_orm_sqllen_t)strlen(tbl);
    return C_ORM_SQL_SUCCESS;
  }

  /* @@IDENTITY mode */
  if (g_mock.result_mode == 4) {
    if (g_mock.custom_str && strcmp(g_mock.custom_str, "ID_NULL") == 0) {
      *StrLen_or_IndPtr = C_ORM_SQL_NULL_DATA;
      return C_ORM_SQL_SUCCESS;
    }
    if (g_mock.custom_str && strcmp(g_mock.custom_str, "ID_BAD") == 0) {
      (void)C_ORM_STRCPY((char *)TargetValuePtr, (size_t)BufferLength, "abc");
      *StrLen_or_IndPtr = 3;
      return C_ORM_SQL_SUCCESS;
    }
    (void)C_ORM_STRCPY((char *)TargetValuePtr, (size_t)BufferLength, "777");
    *StrLen_or_IndPtr = 3;
    return C_ORM_SQL_SUCCESS;
  }

  /* Standard 7-column typed rowset */
  if (g_mock.result_mode == 1) {
    if (Col_or_Param_Num == 1) {
      const char *v = (g_mock.cur_row == 1) ? "42" : "3000000000";
      (void)C_ORM_STRCPY((char *)TargetValuePtr, (size_t)BufferLength, v);
      *StrLen_or_IndPtr = (c_orm_sqllen_t)strlen(v);
      return C_ORM_SQL_SUCCESS;
    }
    if (Col_or_Param_Num == 2) {
      const char *v =
          (g_mock.cur_row == 1) ? "-9007199254740993" : "not_an_int";
      (void)C_ORM_STRCPY((char *)TargetValuePtr, (size_t)BufferLength, v);
      *StrLen_or_IndPtr = (c_orm_sqllen_t)strlen(v);
      return C_ORM_SQL_SUCCESS;
    }
    if (Col_or_Param_Num == 3) {
      const char *v = (g_mock.cur_row == 1) ? "3.14159" : "bad_dbl";
      (void)C_ORM_STRCPY((char *)TargetValuePtr, (size_t)BufferLength, v);
      *StrLen_or_IndPtr = (c_orm_sqllen_t)strlen(v);
      return C_ORM_SQL_SUCCESS;
    }
    if (Col_or_Param_Num == 4) {
      (void)C_ORM_STRCPY((char *)TargetValuePtr, (size_t)BufferLength,
                         "CobolMainframe");
      *StrLen_or_IndPtr = 14;
      return C_ORM_SQL_SUCCESS;
    }
    if (Col_or_Param_Num == 5) {
      if (TargetType == C_ORM_SQL_C_NUMERIC) {
        c_orm_decimal_t d;
        c_orm_sql_numeric_struct_t *ns =
            (c_orm_sql_numeric_struct_t *)TargetValuePtr;
        if (g_mock.reject_c_numeric) {
          return C_ORM_SQL_ERROR;
        }
        if (c_orm_decimal_from_str("-123456789.75", &d) != C_ORM_OK ||
            c_orm_decimal_to_odbc_numeric(&d, ns->val, &ns->precision,
                                          &ns->scale, &ns->sign) != C_ORM_OK) {
          return C_ORM_SQL_ERROR;
        }
        *StrLen_or_IndPtr = (c_orm_sqllen_t)sizeof(*ns);
        return C_ORM_SQL_SUCCESS;
      }
      (void)C_ORM_STRCPY((char *)TargetValuePtr, (size_t)BufferLength,
                         "-123456789.75");
      *StrLen_or_IndPtr = 13;
      return C_ORM_SQL_SUCCESS;
    }
    if (Col_or_Param_Num == 6) {
      static const unsigned char raw[5] = {0xDE, 0x00, 0xAD, 0xBE, 0xEF};
      memcpy(TargetValuePtr, raw, sizeof(raw));
      *StrLen_or_IndPtr = 5;
      return C_ORM_SQL_SUCCESS;
    }
    if (Col_or_Param_Num == 7) {
      *StrLen_or_IndPtr = C_ORM_SQL_NULL_DATA;
      return C_ORM_SQL_SUCCESS;
    }
  }

  /* Mode 2: >64KB multi-chunk streaming CLOB, BLOB (SQL_NO_TOTAL), and WCHAR */
  if (g_mock.result_mode == 2) {
    if (Col_or_Param_Num == 1) {
      size_t total = sizeof(g_large_clob) - 1U;
      size_t off = g_mock.stream_offset[1];
      size_t rem = total - off;
      size_t cap_chars = (size_t)BufferLength - 1U;
      if (off >= total) {
        return C_ORM_SQL_NO_DATA;
      }
      if (rem > cap_chars) {
        memcpy(TargetValuePtr, g_large_clob + off, cap_chars);
        ((char *)TargetValuePtr)[cap_chars] = '\0';
        g_mock.stream_offset[1] += cap_chars;
        *StrLen_or_IndPtr = (c_orm_sqllen_t)rem;
        return C_ORM_SQL_SUCCESS_WITH_INFO;
      }
      memcpy(TargetValuePtr, g_large_clob + off, rem + 1U);
      g_mock.stream_offset[1] = total;
      *StrLen_or_IndPtr = (c_orm_sqllen_t)rem;
      return C_ORM_SQL_SUCCESS;
    }
    if (Col_or_Param_Num == 2) {
      size_t total = sizeof(g_large_blob);
      size_t off = g_mock.stream_offset[2];
      size_t rem = total - off;
      size_t cap_b = (size_t)BufferLength;
      if (off >= total) {
        return C_ORM_SQL_NO_DATA;
      }
      if (rem > cap_b) {
        memcpy(TargetValuePtr, g_large_blob + off, cap_b);
        g_mock.stream_offset[2] += cap_b;
        *StrLen_or_IndPtr = C_ORM_SQL_NO_TOTAL;
        return C_ORM_SQL_SUCCESS_WITH_INFO;
      }
      memcpy(TargetValuePtr, g_large_blob + off, rem);
      g_mock.stream_offset[2] = total;
      *StrLen_or_IndPtr = (c_orm_sqllen_t)rem;
      return C_ORM_SQL_SUCCESS;
    }
    if (Col_or_Param_Num == 3) {
      /* Multi-chunk UTF-16 WCHAR stream (120 units > 64-unit inline buffer) */
      c_orm_sqlwchar_t full_w[121];
      size_t total_u = 120;
      size_t off_u = g_mock.stream_offset[3];
      size_t rem_u;
      size_t cap_u = ((size_t)BufferLength / sizeof(c_orm_sqlwchar_t)) - 1U;
      size_t k;
      for (k = 0; k < total_u; k++) {
        full_w[k] = (c_orm_sqlwchar_t)('Z' - (k % 20U));
      }
      full_w[total_u] = 0;
      if (off_u >= total_u) {
        return C_ORM_SQL_NO_DATA;
      }
      rem_u = total_u - off_u;
      if (rem_u > cap_u) {
        memcpy(TargetValuePtr, full_w + off_u,
               cap_u * sizeof(c_orm_sqlwchar_t));
        ((c_orm_sqlwchar_t *)TargetValuePtr)[cap_u] = 0;
        g_mock.stream_offset[3] += cap_u;
        *StrLen_or_IndPtr = (c_orm_sqllen_t)(rem_u * sizeof(c_orm_sqlwchar_t));
        return C_ORM_SQL_SUCCESS_WITH_INFO;
      }
      memcpy(TargetValuePtr, full_w + off_u,
             (rem_u + 1U) * sizeof(c_orm_sqlwchar_t));
      g_mock.stream_offset[3] = total_u;
      *StrLen_or_IndPtr = (c_orm_sqllen_t)(rem_u * sizeof(c_orm_sqlwchar_t));
      return C_ORM_SQL_SUCCESS;
    }
  }

  return C_ORM_SQL_NO_DATA;
}

/**
 * @brief Mock SQLFreeStmt.
 * @param StatementHandle Statement handle.
 * @param Option Reset/close option.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLMock_FreeStmt(
    c_orm_sqlhstmt_t StatementHandle, c_orm_sqlusmallint_t Option) {
  (void)StatementHandle;
  (void)Option;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLGetDiagRec (ANSI).
 * @param HandleType Handle type.
 * @param Handle Target handle.
 * @param RecNumber 1-based diagnostic record number.
 * @param SQLState Output 5-char SQLSTATE buffer.
 * @param NativeErrorPtr Output native error code.
 * @param MessageText Output diagnostic message buffer.
 * @param BufferLength Message buffer capacity.
 * @param TextLengthPtr Output message length.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLMock_GetDiagRec(
    c_orm_sqlsmallint_t HandleType, c_orm_sqlhandle_t Handle,
    c_orm_sqlsmallint_t RecNumber, c_orm_sqlchar_t *SQLState,
    c_orm_sqlinteger_t *NativeErrorPtr, c_orm_sqlchar_t *MessageText,
    c_orm_sqlsmallint_t BufferLength, c_orm_sqlsmallint_t *TextLengthPtr) {
  (void)HandleType;
  (void)Handle;
  if (g_mock.flood_diag_recs && RecNumber >= 1 && RecNumber <= 20) {
    (void)C_ORM_STRCPY((char *)SQLState, 6, "HY000");
    *NativeErrorPtr = -1;
    memset(MessageText, 'D', 200);
    MessageText[200] = '\0';
    *TextLengthPtr = g_mock.zero_wide_len ? -1 : 200;
    return C_ORM_SQL_SUCCESS;
  }
  if (RecNumber == 1 && g_mock.diag_msg1) {
    (void)C_ORM_STRCPY((char *)SQLState, 6, g_mock.diag_sqlstate);
    *NativeErrorPtr = g_mock.diag_native;
    (void)C_ORM_STRCPY((char *)MessageText, (size_t)BufferLength,
                       g_mock.diag_msg1);
    *TextLengthPtr =
        g_mock.zero_wide_len
            ? -1
            : (c_orm_sqlsmallint_t)strlen((const char *)MessageText);
    return C_ORM_SQL_SUCCESS;
  }
  if (RecNumber == 2 && g_mock.diag_msg2) {
    (void)C_ORM_STRCPY((char *)SQLState, 6, g_mock.diag_sqlstate);
    *NativeErrorPtr = g_mock.diag_native;
    (void)C_ORM_STRCPY((char *)MessageText, (size_t)BufferLength,
                       g_mock.diag_msg2);
    *TextLengthPtr = (c_orm_sqlsmallint_t)strlen((const char *)MessageText);
    return C_ORM_SQL_SUCCESS;
  }
  return C_ORM_SQL_NO_DATA;
}

/**
 * @brief Mock SQLGetDiagRecW (UTF-16).
 * @param HandleType Handle type.
 * @param Handle Target handle.
 * @param RecNumber 1-based diagnostic record number.
 * @param SQLState Output wide 5-char SQLSTATE buffer.
 * @param NativeErrorPtr Output native error code.
 * @param MessageText Output wide diagnostic message buffer.
 * @param BufferLength Capacity in code units.
 * @param TextLengthPtr Output message unit length.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLMock_GetDiagRecW(
    c_orm_sqlsmallint_t HandleType, c_orm_sqlhandle_t Handle,
    c_orm_sqlsmallint_t RecNumber, c_orm_sqlwchar_t *SQLState,
    c_orm_sqlinteger_t *NativeErrorPtr, c_orm_sqlwchar_t *MessageText,
    c_orm_sqlsmallint_t BufferLength, c_orm_sqlsmallint_t *TextLengthPtr) {
  c_orm_sqlchar_t astate[8];
  c_orm_sqlchar_t amsg[256];
  c_orm_sqlsmallint_t alen = 0;
  size_t su = 0;
  size_t mu = 0;
  c_orm_sqlreturn_t r =
      SQLMock_GetDiagRec(HandleType, Handle, RecNumber, astate, NativeErrorPtr,
                         amsg, (c_orm_sqlsmallint_t)sizeof(amsg), &alen);
  if (!C_ORM_SQL_SUCCEEDED(r)) {
    return r;
  }
  (void)c_orm_utf8_to_utf16((const char *)astate, strlen((const char *)astate),
                            0, SQLState, 6, &su);
  (void)c_orm_utf8_to_utf16((const char *)amsg, strlen((const char *)amsg), 0,
                            MessageText, (size_t)BufferLength, &mu);
  *TextLengthPtr = g_mock.zero_wide_len ? 0 : (c_orm_sqlsmallint_t)mu;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLColumns.
 * @param StatementHandle Statement handle.
 * @param CatalogName Catalog pattern.
 * @param NameLength1 Catalog length.
 * @param SchemaName Schema pattern.
 * @param NameLength2 Schema length.
 * @param TableName Table pattern.
 * @param NameLength3 Table length.
 * @param ColumnName Column pattern.
 * @param NameLength4 Column length.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API
SQLMock_Columns(c_orm_sqlhstmt_t StatementHandle, c_orm_sqlchar_t *CatalogName,
                c_orm_sqlsmallint_t NameLength1, c_orm_sqlchar_t *SchemaName,
                c_orm_sqlsmallint_t NameLength2, c_orm_sqlchar_t *TableName,
                c_orm_sqlsmallint_t NameLength3, c_orm_sqlchar_t *ColumnName,
                c_orm_sqlsmallint_t NameLength4) {
  int fail = 0;
  (void)StatementHandle;
  (void)CatalogName;
  (void)NameLength1;
  (void)SchemaName;
  (void)NameLength2;
  (void)TableName;
  (void)NameLength3;
  (void)ColumnName;
  (void)NameLength4;
  if (should_fail("Columns", &fail) != C_ORM_OK) {
    fail = 1;
  }
  if (fail) {
    return C_ORM_SQL_ERROR;
  }
  g_mock.result_mode = 5;
  g_mock.cur_row = 0;
  g_mock.total_rows = 13;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock SQLTables.
 * @param StatementHandle Statement handle.
 * @param CatalogName Catalog pattern.
 * @param NameLength1 Catalog length.
 * @param SchemaName Schema pattern.
 * @param NameLength2 Schema length.
 * @param TableName Table pattern.
 * @param NameLength3 Table length.
 * @param TableType Type filter.
 * @param NameLength4 Type length.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API
SQLMock_Tables(c_orm_sqlhstmt_t StatementHandle, c_orm_sqlchar_t *CatalogName,
               c_orm_sqlsmallint_t NameLength1, c_orm_sqlchar_t *SchemaName,
               c_orm_sqlsmallint_t NameLength2, c_orm_sqlchar_t *TableName,
               c_orm_sqlsmallint_t NameLength3, c_orm_sqlchar_t *TableType,
               c_orm_sqlsmallint_t NameLength4) {
  int fail = 0;
  (void)StatementHandle;
  (void)CatalogName;
  (void)NameLength1;
  (void)SchemaName;
  (void)NameLength2;
  (void)TableName;
  (void)NameLength3;
  (void)TableType;
  (void)NameLength4;
  if (should_fail("Tables", &fail) != C_ORM_OK) {
    fail = 1;
  }
  if (fail) {
    return C_ORM_SQL_ERROR;
  }
  g_mock.result_mode = 3;
  g_mock.cur_row = 0;
  g_mock.total_rows = 10;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Populates a complete mock `c_orm_odbc_api_t` dispatch table.
 * @param out_api Output dispatch table.
 * @return C_ORM_OK on completion.
 */
static c_orm_error_t build_mock_odbc_api(c_orm_odbc_api_t *out_api) {
  memset(out_api, 0, sizeof(*out_api));
  out_api->SQLAllocHandle = SQLMock_AllocHandle;
  out_api->SQLFreeHandle = SQLMock_FreeHandle;
  out_api->SQLSetEnvAttr = SQLMock_SetEnvAttr;
  out_api->SQLSetConnectAttr = SQLMock_SetConnectAttr;
  out_api->SQLDriverConnect = SQLMock_DriverConnect;
  out_api->SQLDriverConnectW = SQLMock_DriverConnectW;
  out_api->SQLConnect = SQLMock_Connect;
  out_api->SQLDisconnect = SQLMock_Disconnect;
  out_api->SQLEndTran = SQLMock_EndTran;
  out_api->SQLSetStmtAttr = SQLMock_SetStmtAttr;
  out_api->SQLPrepare = SQLMock_Prepare;
  out_api->SQLPrepareW = SQLMock_PrepareW;
  out_api->SQLExecute = SQLMock_Execute;
  out_api->SQLBindParameter = SQLMock_BindParameter;
  out_api->SQLNumResultCols = SQLMock_NumResultCols;
  out_api->SQLDescribeCol = SQLMock_DescribeCol;
  out_api->SQLDescribeColW = SQLMock_DescribeColW;
  out_api->SQLFetch = SQLMock_Fetch;
  out_api->SQLGetData = SQLMock_GetData;
  out_api->SQLFreeStmt = SQLMock_FreeStmt;
  out_api->SQLGetDiagRec = SQLMock_GetDiagRec;
  out_api->SQLGetDiagRecW = SQLMock_GetDiagRecW;
  out_api->SQLTables = SQLMock_Tables;
  out_api->SQLColumns = SQLMock_Columns;
  return C_ORM_OK;
}

/**
 * @brief Symbol lookup callback used to test `c_orm_odbc_populate_api`.
 * @param handle Mode selector (`(int *)handle`).
 * @param symbol_name Requested ODBC symbol name.
 * @param out_fn Output function pointer.
 * @return C_ORM_OK or simulated error.
 */
static c_orm_error_t mock_sym_resolver(void *handle, const char *symbol_name,
                                       void **out_fn) {
  int mode = *(int *)handle;
  if (mode == 2) {
    return C_ORM_ERROR_DRIVER_LOAD;
  }
  if (mode == 1 && strcmp(symbol_name, "SQLFetch") == 0) {
    *out_fn = NULL;
    return C_ORM_OK;
  }
  *out_fn = (void *)(size_t)SQLMock_AllocHandle;
  return C_ORM_OK;
}

/**
 * @brief Slow-query logger callback for testing telemetry.
 * @param msg Formatted slow-query log message.
 * @param user_data Counter pointer.
 * @return C_ORM_OK on completion.
 */
static c_orm_error_t slow_log_cb(const char *msg, void *user_data) {
  int *cnt = (int *)user_data;
  if (msg && strstr(msg, "SLOW QUERY") != NULL) {
    (*cnt)++;
  }
  return C_ORM_OK;
}

static int g_oom_countdown = -1;

/**
 * @brief Fault-injecting malloc wrapper for OOM path verification.
 * @param size Requested allocation size.
 * @param out_ptr Output allocated pointer.
 * @return C_ORM_OK or C_ORM_ERROR_MEMORY.
 */
static c_orm_error_t oom_malloc(size_t size, void **out_ptr) {
  if (g_oom_countdown == 0) {
    *out_ptr = NULL;
    return C_ORM_ERROR_MEMORY;
  }
  if (g_oom_countdown > 0) {
    g_oom_countdown--;
  }
  *out_ptr = malloc(size > 0 ? size : 1);
  return *out_ptr ? C_ORM_OK : C_ORM_ERROR_MEMORY;
}

/**
 * @brief Fault-injecting realloc wrapper for OOM path verification.
 * @param ptr Existing allocation pointer.
 * @param size New requested size.
 * @param out_ptr Output reallocated pointer.
 * @return C_ORM_OK or C_ORM_ERROR_MEMORY.
 */
static c_orm_error_t oom_realloc(void *ptr, size_t size, void **out_ptr) {
  if (g_oom_countdown == 0) {
    *out_ptr = NULL;
    return C_ORM_ERROR_MEMORY;
  }
  if (g_oom_countdown > 0) {
    g_oom_countdown--;
  }
  *out_ptr = realloc(ptr, size > 0 ? size : 1);
  return *out_ptr ? C_ORM_OK : C_ORM_ERROR_MEMORY;
}

/**
 * @brief Matching free wrapper for OOM tests.
 * @param ptr Block to free.
 * @return C_ORM_OK on completion.
 */
static c_orm_error_t oom_free(void *ptr) {
  free(ptr);
  return C_ORM_OK;
}

/**
 * @brief Sets an environment variable portably without `<windows.h>`.
 * @param key Variable name.
 * @param val Variable value (or `""` to clear).
 * @return C_ORM_OK on completion.
 */
static c_orm_error_t set_test_env(const char *key, const char *val) {
#if defined(_MSC_VER)
  (void)_putenv_s(key, val ? val : "");
#elif defined(_WIN32)
  char buf[512];
  (void)C_ORM_SPRINTF(buf, sizeof(buf), "%s=%s", key, val ? val : "");
  (void)_putenv(buf);
#else
  if (val && val[0] != '\0') {
    (void)setenv(key, val, 1);
  } else {
    (void)unsetenv(key);
  }
#endif
  return C_ORM_OK;
}

/** @brief Verifies loader symbol resolution, OS dlopen paths, and mock hooks.
 */
TEST test_odbc_loader_and_populate(void) {
  c_orm_odbc_api_t api;
  const c_orm_odbc_api_t *loaded = NULL;
  int mode = 0;

  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_odbc_populate_api(NULL, &mode, &api));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_odbc_populate_api(mock_sym_resolver, &mode, NULL));

  mode = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_populate_api(mock_sym_resolver, &mode, &api));
  ASSERT(api.SQLAllocHandle != NULL);

  mode = 1; /* Missing SQLFetch */
  ASSERT_EQ(C_ORM_ERROR_DRIVER_LOAD,
            c_orm_odbc_populate_api(mock_sym_resolver, &mode, &api));

  mode = 2; /* Callback error */
  ASSERT_EQ(C_ORM_ERROR_DRIVER_LOAD,
            c_orm_odbc_populate_api(mock_sym_resolver, &mode, &api));

  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_odbc_load_driver_manager(NULL));

  ASSERT_EQ(C_ORM_OK, c_orm_odbc_reset_loader());
  (void)set_test_env("C_ORM_ODBC_LIB", "/nonexistent_c_orm_odbc_9999.so");
  ASSERT_EQ(C_ORM_ERROR_DRIVER_LOAD, c_orm_odbc_load_driver_manager(&loaded));

  /* Open real OS library lacking SQL* symbols -> exercises os_open/sym/close */
#if defined(_WIN32)
  (void)set_test_env("C_ORM_ODBC_LIB", "kernel32.dll");
#elif defined(__APPLE__)
  (void)set_test_env("C_ORM_ODBC_LIB", "/usr/lib/libSystem.B.dylib");
#else
  (void)set_test_env("C_ORM_ODBC_LIB", "libc.so.6");
#endif
  ASSERT_EQ(C_ORM_ERROR_DRIVER_LOAD, c_orm_odbc_load_driver_manager(&loaded));
  (void)set_test_env("C_ORM_ODBC_LIB", "");

  (void)build_mock_odbc_api(&api);
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_set_mock_api(&api));
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_load_driver_manager(&loaded));
  ASSERT_EQ(&api, loaded);
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_set_mock_api(NULL));
  PASS();
}

/** @brief Verifies URL percent-decoding, `{...}` brace escaping, and URLs. */
TEST test_odbc_url_and_escaping(void) {
  char buf[512];
  size_t len = 0;
  int timeout = 0;
  int autocommit = 1;

  /* Percent decoding */
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_odbc_percent_decode(NULL, 0, 0, buf, sizeof(buf), NULL));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_odbc_percent_decode("a", 1, 0, NULL, sizeof(buf), NULL));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_odbc_percent_decode("a", 1, 0, buf, 0, NULL));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_odbc_percent_decode("abcd", 4, 0, buf, 3, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_percent_decode("ab%00cd", 7, 0, buf, sizeof(buf), NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_percent_decode("ab%2", 4, 0, buf, sizeof(buf), NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_percent_decode("ab%ZZ", 5, 0, buf, sizeof(buf), NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_percent_decode("PostgreSQL+Unicode%20%2f%3A",
                                                27, 1, buf, sizeof(buf), &len));
  ASSERT_STR_EQ("PostgreSQL Unicode /:", buf);
  ASSERT_EQ(21U, len);

  /* Injection-safe attribute escaping */
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_odbc_escape_conn_value(NULL, buf, sizeof(buf), NULL));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_odbc_escape_conn_value("x", NULL, sizeof(buf), NULL));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_odbc_escape_conn_value("clean", buf, 5, NULL));
  ASSERT_EQ(C_ORM_OK,
            c_orm_odbc_escape_conn_value("clean_val", buf, sizeof(buf), &len));
  ASSERT_STR_EQ("clean_val", buf);
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_escape_conn_value("p@ss;UID=admin;x={a}b", buf,
                                                   sizeof(buf), &len));
  ASSERT_STR_EQ("{p@ss;UID=admin;x={a}}b}", buf);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_odbc_escape_conn_value(
                                        "bad\x0aline", buf, sizeof(buf), NULL));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_odbc_escape_conn_value("a;b}", buf, 5, NULL));

  /* Full URL parsing */
  ASSERT_EQ(
      C_ORM_OK,
      c_orm_odbc_build_conn_string(
          "odbc://usr:p%3Bss%7D1@db.local:5432/fin?driver=PostgreSQL+Unicode"
          "&timeout=15&autocommit=false&encrypt=yes",
          buf, sizeof(buf), &timeout, &autocommit));
  ASSERT_STR_EQ("DRIVER={PostgreSQL Unicode};SERVER=db.local;PORT=5432;"
                "DATABASE=fin;UID=usr;PWD={p;ss}}1};encrypt=yes;",
                buf);
  ASSERT_EQ(15, timeout);
  ASSERT_EQ(0, autocommit);

  /* IPv6 + DSN + boolean variants */
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_build_conn_string(
                          "odbc://alice@[::1]:1433/master?dsn=SqlSrv"
                          "&autocommit=yes&login_timeout=5",
                          buf, sizeof(buf), &timeout, &autocommit));
  ASSERT_EQ(5, timeout);
  ASSERT_EQ(1, autocommit);
  ASSERT(strstr(buf, "DSN=SqlSrv;") != NULL);
  ASSERT(strstr(buf, "SERVER=::1;") != NULL);

  /* Raw DSN and DRIVER strings */
  ASSERT_EQ(C_ORM_OK,
            c_orm_odbc_build_conn_string("odbc://DSN=CorpDsn;UID=u;PWD=p", buf,
                                         sizeof(buf), NULL, NULL));
  ASSERT_STR_EQ("DSN=CorpDsn;UID=u;PWD=p", buf);
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_build_conn_string(
                          "DRIVER={SQLite3};Database=:memory:", buf,
                          sizeof(buf), NULL, NULL));
  ASSERT_STR_EQ("DRIVER={SQLite3};Database=:memory:", buf);

  /* Invalid URL edge cases */
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_odbc_build_conn_string(NULL, buf, sizeof(buf), NULL, NULL));
  ASSERT_EQ(
      C_ORM_ERROR_OVERFLOW,
      c_orm_odbc_build_conn_string("odbc://localhost/db", buf, 1, NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("", buf, sizeof(buf), NULL, NULL));
  ASSERT_EQ(
      C_ORM_ERROR_VALIDATION,
      c_orm_odbc_build_conn_string("odbc://", buf, sizeof(buf), NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("http://localhost/db", buf,
                                         sizeof(buf), NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("odbc://[::1/db", buf, sizeof(buf),
                                         NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("odbc://host:99999/db", buf,
                                         sizeof(buf), NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("odbc://host:/db", buf, sizeof(buf),
                                         NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("odbc://host/db?badpair", buf,
                                         sizeof(buf), NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("odbc://host/db?bad;key=1", buf,
                                         sizeof(buf), NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("odbc://host/db?timeout=-5", buf,
                                         sizeof(buf), NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("odbc://host/db?autocommit=invalid",
                                         buf, sizeof(buf), NULL, NULL));
  PASS();
}

/** @brief Verifies full statement execution, binding, types, and streaming. */
TEST test_odbc_crud_types_and_streaming(void) {
  c_orm_odbc_api_t mock_api;
  c_orm_db_t *db = NULL;
  c_orm_query_t *q = NULL;
  const c_orm_driver_vtable_t *vt = NULL;
  c_orm_decimal_t dec_in;
  c_orm_decimal_t dec_out;
  int has_row = 0;
  int is_null = 0;
  int32_t v32 = 0;
  int64_t v64 = 0;
  double vdbl = 0.0;
  const char *vstr = NULL;
  const void *vblob = NULL;
  size_t bsize = 0;
  int col_cnt = 0;
  const char *col_name = NULL;
  char dec_text[48];
  int slow_cnt = 0;
  static const unsigned char bin_in[4] = {0x01, 0x00, 0xFE, 0xFF};

  (void)reset_mock_state();
  (void)build_mock_odbc_api(&mock_api);
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_set_mock_api(&mock_api));
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_get_vtable(&vt));
  ASSERT(vt != NULL);

  ASSERT_EQ(C_ORM_OK, c_orm_connect_url(
                          "odbc://u:p@localhost:5432/test?timeout=10", &db));
  ASSERT(db != NULL);
  ASSERT_STR_EQ("odbc", db->driver_name);
  ASSERT_EQ(10UL, g_mock.login_timeout);

  /* Prepare + bind all types (including high param index 12 -> realloc) */
  g_mock.result_mode = 1;
  g_mock.total_rows = 2;
  db->slow_query_threshold_ms = 1;
  db->log_cb = slow_log_cb;
  db->log_user_data = &slow_cnt;

  ASSERT_EQ(C_ORM_OK, vt->prepare(db, "SELECT * FROM t WHERE a=?", &q));
  ASSERT_EQ(C_ORM_OK, vt->get_column_count(q, &col_cnt));
  ASSERT_EQ(7, col_cnt);
  ASSERT_EQ(C_ORM_OK, vt->get_column_name(q, 4, &col_name));
  ASSERT_STR_EQ("amount", col_name);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, vt->get_column_name(q, 99, &col_name));

  ASSERT_EQ(C_ORM_OK, vt->bind_int32(q, 1, -42));
  ASSERT_EQ(C_ORM_OK, vt->bind_int64(q, 2, (int64_t)9000000000L));
  ASSERT_EQ(C_ORM_OK, vt->bind_double(q, 3, 2.71828));
  ASSERT_EQ(C_ORM_OK, vt->bind_string(q, 4, "hello_odbc"));
  ASSERT_EQ(C_ORM_OK, vt->bind_string(q, 5, NULL));
  ASSERT_EQ(C_ORM_OK, vt->bind_blob(q, 6, bin_in, sizeof(bin_in)));
  ASSERT_EQ(C_ORM_OK, vt->bind_blob(q, 7, bin_in, 0));
  ASSERT_EQ(C_ORM_OK, vt->bind_blob(q, 8, NULL, 0));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("999999999999.99", &dec_in));
  ASSERT_EQ(C_ORM_OK, vt->bind_decimal(q, 12, &dec_in));
  ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_int32(q, 0, 1));

  /* Fallback decimal binding when driver rejects SQL_C_NUMERIC */
  g_mock.reject_c_numeric = 1;
  ASSERT_EQ(C_ORM_OK, vt->bind_decimal(q, 9, &dec_in));
  ASSERT_EQ(C_ORM_SQL_C_CHAR, g_mock.bound_c_type[9]);
  g_mock.reject_c_numeric = 0;

  /* Row 1: verify out-of-order column access (col 6 -> col 0) works cleanly */
  db->slow_query_threshold_ms = 0;
  ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
  ASSERT_EQ(1, has_row);
  ASSERT_EQ(C_ORM_OK, vt->is_null(q, 6, &is_null));
  ASSERT_EQ(1, is_null);
  ASSERT_EQ(C_ORM_OK, vt->get_string(q, 6, &vstr));
  ASSERT_EQ(NULL, vstr);
  ASSERT_EQ(C_ORM_OK, vt->get_int32(q, 0, &v32));
  ASSERT_EQ(42, v32);
  ASSERT_EQ(C_ORM_OK, vt->get_int64(q, 1, &v64));
  ASSERT_EQ(((int64_t)(-900719925L) * (int64_t)10000000L - (int64_t)4740993L),
            v64);
  ASSERT_EQ(C_ORM_OK, vt->get_double(q, 2, &vdbl));
  ASSERT(vdbl > 3.1415 && vdbl < 3.1416);
  ASSERT_EQ(C_ORM_OK, vt->get_string(q, 3, &vstr));
  ASSERT_STR_EQ("CobolMainframe", vstr);
  ASSERT_EQ(C_ORM_OK, vt->get_decimal(q, 4, &dec_out));
  ASSERT_EQ(C_ORM_OK,
            c_orm_decimal_to_str(&dec_out, dec_text, sizeof(dec_text), NULL));
  ASSERT_STR_EQ("-123456789.75", dec_text);
  ASSERT_EQ(C_ORM_OK, vt->get_blob(q, 5, &vblob, &bsize));
  ASSERT_EQ(5U, bsize);
  ASSERT_EQ(0x00, ((const unsigned char *)vblob)[1]);

  /* Row 2: verify INT32 overflow, invalid INT64/DOUBLE type mismatch, and
   * decimal string fallback */
  g_mock.reject_c_numeric = 1;
  ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
  ASSERT_EQ(1, has_row);
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW, vt->get_int32(q, 0, &v32));
  ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH, vt->get_int64(q, 1, &v64));
  ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH, vt->get_double(q, 2, &vdbl));
  ASSERT_EQ(C_ORM_OK, vt->get_decimal(q, 4, &dec_out));
  ASSERT_EQ(C_ORM_OK, vt->get_int64(q, 4, &v64));
  ASSERT_EQ(-123456789, (int)v64);
  g_mock.reject_c_numeric = 0;

  /* EOF + Reset + Reuse */
  ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
  ASSERT_EQ(0, has_row);
  ASSERT_EQ(C_ORM_OK, vt->reset(q));
  g_mock.result_mode = 0; /* DML 0 cols */
  ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
  ASSERT_EQ(0, has_row);
  ASSERT_EQ(C_ORM_OK, vt->finalize(q));

  /* Mode 2: >64KB multi-chunk CLOB, BLOB (SQL_NO_TOTAL), and WCHAR streaming */
  g_mock.result_mode = 2;
  g_mock.total_rows = 1;
  ASSERT_EQ(C_ORM_OK, vt->prepare(db, "SELECT clob_c, blob_c, w_c FROM l", &q));
  ASSERT_EQ(C_ORM_OK, vt->step(q, &has_row));
  ASSERT_EQ(1, has_row);
  ASSERT_EQ(C_ORM_OK, vt->get_string(q, 0, &vstr));
  ASSERT_EQ(sizeof(g_large_clob) - 1U, strlen(vstr));
  ASSERT_EQ(0, memcmp(vstr, g_large_clob, sizeof(g_large_clob) - 1U));
  ASSERT_EQ(C_ORM_OK, vt->get_blob(q, 1, &vblob, &bsize));
  ASSERT_EQ(sizeof(g_large_blob), bsize);
  ASSERT_EQ(0, memcmp(vblob, g_large_blob, sizeof(g_large_blob)));
  ASSERT_EQ(C_ORM_OK, vt->get_string(q, 2, &vstr));
  ASSERT_EQ(120U, strlen(vstr));
  ASSERT_EQ(C_ORM_OK, vt->finalize(q));

  /* Last insert rowid */
  ASSERT_EQ(C_ORM_OK, vt->get_last_insert_rowid(db, &v64));
  ASSERT_EQ(777, (int)v64);

  ASSERT_EQ(C_ORM_OK, vt->disconnect(db));
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_set_mock_api(NULL));
  PASS();
}

/** @brief Verifies UNICODE wide mode, SQLSTATE mappings, transactions, tables,
 * batch array binding, and OOM resilience. */
TEST test_odbc_wide_diag_tx_batch_and_oom(void) {
  c_orm_odbc_api_t mock_api;
  c_orm_db_t *db = NULL;
  c_orm_query_t *q = NULL;
  char sqlstate[6];
  c_orm_sqlinteger_t native_err = 0;
  const char *err_msg = NULL;
  const char *err_trace = NULL;
  char **tables = NULL;
  size_t tcount = 0;
  size_t processed = 0;

  (void)reset_mock_state();
  (void)build_mock_odbc_api(&mock_api);
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_set_mock_api(&mock_api));

  /* Wide connection (prefer_wide = 1) */
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_connect_raw(
                          &mock_api, "DRIVER={WideDrv};SERVER=127.0.0.1;",
                          "odbc", NULL, 5, 1, 1, &db));
  ASSERT_STR_EQ("DRIVER={WideDrv};SERVER=127.0.0.1;", g_mock.last_conn_str);

  /* Wide prepare + wide bind + multi-record wide diagnostics */
  ASSERT_EQ(C_ORM_OK, db->vtable->prepare(db, "INSERT INTO w VALUES (?)", &q));
  ASSERT_STR_EQ("INSERT INTO w VALUES (?)", g_mock.last_sql);
  ASSERT_EQ(C_ORM_OK,
            db->vtable->bind_string(q, 1, "WideText\xF0\x9F\x98\x80"));

  /* Test SQLSTATE mappings via wide SQLGetDiagRecW */
  {
    static const char *const states[7] = {"23505", "40001", "HYT00", "08S01",
                                          "22001", "22012", "42S02"};
    static const c_orm_error_t expected[7] = {
        C_ORM_ERROR_CONSTRAINT, C_ORM_ERROR_DEADLOCK,
        C_ORM_ERROR_TIMEOUT,    C_ORM_ERROR_CONNECTION,
        C_ORM_ERROR_TRUNCATION, C_ORM_ERROR_DECIMAL_OVERFLOW,
        C_ORM_ERROR_SQL};
    int k;
    int has_row = 0;
    g_mock.fail_on_op = "Execute";
    g_mock.diag_msg1 = "Primary failure";
    g_mock.diag_msg2 = "Secondary detail";
    g_mock.diag_native = -803;
    for (k = 0; k < 7; k++) {
      (void)C_ORM_STRCPY(g_mock.diag_sqlstate, sizeof(g_mock.diag_sqlstate),
                         states[k]);
      ASSERT_EQ(expected[k], db->vtable->step(q, &has_row));
      ASSERT_EQ(C_ORM_OK, c_orm_odbc_get_sqlstate(db, sqlstate));
      ASSERT_STR_EQ(states[k], sqlstate);
      ASSERT_EQ(C_ORM_OK, c_orm_odbc_get_native_error(db, &native_err));
      ASSERT_EQ(-803, (int)native_err);
    }
    ASSERT_EQ(C_ORM_OK, db->vtable->get_last_error(db, &err_msg));
    ASSERT(strstr(err_msg, "Primary failure") != NULL);
    ASSERT_EQ(C_ORM_OK, db->vtable->get_last_trace(db, &err_trace));
    ASSERT(strstr(err_trace, "Secondary detail") != NULL);
    g_mock.fail_on_op = NULL;
  }
  ASSERT_EQ(C_ORM_OK, db->vtable->finalize(q));

  /* Native ODBC transactions & isolation routed through c_orm_api.c */
  ASSERT_EQ(C_ORM_OK,
            c_orm_odbc_set_isolation(db, C_ORM_SQL_TXN_REPEATABLE_READ));
  ASSERT_EQ(C_ORM_SQL_TXN_REPEATABLE_READ, g_mock.isolation_mask);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_odbc_set_isolation(db, 0x9999UL));

  ASSERT_EQ(C_ORM_OK, c_orm_transaction_begin(db));
  ASSERT_EQ(0UL, g_mock.autocommit_mode);
  ASSERT_EQ(C_ORM_OK, c_orm_transaction_commit(db));
  ASSERT_EQ(1, g_mock.commit_count);
  ASSERT_EQ(1UL, g_mock.autocommit_mode);

  ASSERT_EQ(C_ORM_OK, c_orm_transaction_begin(db));
  ASSERT_EQ(C_ORM_OK, c_orm_transaction_rollback(db));
  ASSERT_EQ(1, g_mock.rollback_count);

  /* Catalog table listing (>8 tables exercises array doubling) */
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_list_tables(db, "PUBLIC", &tables, &tcount));
  ASSERT_EQ(10U, tcount);
  ASSERT_STR_EQ("TBL_01", tables[0]);
  ASSERT_STR_EQ("TBL_10", tables[9]);
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_free_string_list(tables, tcount));

  /* Column-wise array batch binding */
  {
    int32_t ids[3] = {10, 20, 30};
    int64_t bigs[3] = {100L, 200L, 300L};
    double rates[3] = {1.5, 2.5, 3.5};
    const char *names[3] = {"Alpha", NULL, "GammaLonger"};
    c_orm_decimal_t decs[3];
    c_orm_type_t types[5] = {C_ORM_TYPE_INT32, C_ORM_TYPE_INT64,
                             C_ORM_TYPE_DOUBLE, C_ORM_TYPE_STRING,
                             C_ORM_TYPE_DECIMAL};
    const void *arrays[5];
    (void)c_orm_decimal_from_str("10.50", &decs[0]);
    (void)c_orm_decimal_from_str("20.75", &decs[1]);
    (void)c_orm_decimal_from_str("-5.25", &decs[2]);
    arrays[0] = ids;
    arrays[1] = bigs;
    arrays[2] = rates;
    arrays[3] = names;
    arrays[4] = decs;
    ASSERT_EQ(C_ORM_OK,
              c_orm_odbc_execute_batch(db, "INSERT INTO b VALUES (?,?,?,?,?)",
                                       3, 5, types, arrays, &processed));
    ASSERT_EQ(3U, processed);
    ASSERT_EQ(3U, g_mock.batch_paramset_size);
  }

  ASSERT_EQ(C_ORM_OK, db->vtable->disconnect(db));

  /* SQLConnect fallback when SQLDriverConnect & SQLDriverConnectW are NULL */
  mock_api.SQLDriverConnect = NULL;
  mock_api.SQLDriverConnectW = NULL;
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_connect_raw(&mock_api, "MY_DSN", "odbc", NULL,
                                             0, 1, 0, &db));
  ASSERT_EQ(C_ORM_OK, db->vtable->disconnect(db));

  mock_api.SQLConnect = NULL;
  ASSERT_EQ(
      C_ORM_ERROR_DRIVER_LOAD,
      c_orm_odbc_connect_raw(&mock_api, "MY_DSN", "odbc", NULL, 0, 1, 0, &db));

  /* Explicit ANSI connection & prepare/diag path (prefer_wide = 0) */
  (void)build_mock_odbc_api(&mock_api);
  {
    c_orm_db_t *ansi_db = NULL;
    c_orm_query_t *aq = NULL;
    int a_has = 0;
    ASSERT_EQ(C_ORM_OK, c_orm_odbc_connect_raw(
                            &mock_api, "DRIVER={AnsiDrv};SERVER=127.0.0.1;",
                            "odbc", NULL, 5, 1, 0, &ansi_db));
    g_mock.result_mode = 1;
    g_mock.total_rows = 1;
    ASSERT_EQ(C_ORM_OK, ansi_db->vtable->prepare(
                            ansi_db, "SELECT * FROM a WHERE x=?", &aq));
    ASSERT_EQ(C_ORM_OK, ansi_db->vtable->bind_string(aq, 1, "ansi_str"));
    ASSERT_EQ(C_ORM_OK, ansi_db->vtable->step(aq, &a_has));
    ASSERT_EQ(1, a_has);
    g_mock.fail_on_op = "Fetch";
    (void)C_ORM_STRCPY(g_mock.diag_sqlstate, sizeof(g_mock.diag_sqlstate),
                       "01004");
    ASSERT_EQ(C_ORM_ERROR_TRUNCATION, ansi_db->vtable->step(aq, &a_has));
    g_mock.fail_on_op = NULL;
    ASSERT_EQ(C_ORM_OK, ansi_db->vtable->finalize(aq));
    ASSERT_EQ(C_ORM_OK, ansi_db->vtable->disconnect(ansi_db));
  }

  /* OOM fault injection sweep across connect + prepare + batch */
#ifdef C_ORM_TEST_ALLOCATOR
  (void)build_mock_odbc_api(&mock_api);
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_set_mock_api(&mock_api));
  {
    int step_n;
    for (step_n = 0; step_n < 6; step_n++) {
      c_orm_db_t *oom_db = NULL;
      g_oom_countdown = step_n;
      (void)c_orm_set_allocators(oom_malloc, oom_realloc, oom_free);
      if (c_orm_odbc_connect("odbc://u:p@localhost:5432/db", &oom_db) ==
              C_ORM_OK &&
          oom_db) {
        c_orm_query_t *oq = NULL;
        if (oom_db->vtable->prepare(oom_db, "SELECT 1", &oq) == C_ORM_OK &&
            oq) {
          (void)oom_db->vtable->bind_string(oq, 1, "abc");
          (void)oom_db->vtable->finalize(oq);
        }
        (void)oom_db->vtable->disconnect(oom_db);
      }
      g_oom_countdown = -1;
      (void)c_orm_reset_allocators();
    }
  }
#endif

  ASSERT_EQ(C_ORM_OK, c_orm_odbc_set_mock_api(NULL));

  /* Optional live ODBC smoke test if C_ORM_TEST_ODBC_URL is configured */
  {
    char live_url[512];
    live_url[0] = '\0';
    if (C_ORM_GETENV(live_url, sizeof(live_url), "C_ORM_TEST_ODBC_URL") ==
            C_ORM_OK &&
        live_url[0] != '\0') {
      c_orm_db_t *live_db = NULL;
      ASSERT_EQ(C_ORM_OK, c_orm_odbc_connect(live_url, &live_db));
      ASSERT_EQ(C_ORM_OK, live_db->vtable->disconnect(live_db));
    }
  }
  PASS();
}

static int g_dl_mode = 0;
static int g_dl_closed = 0;
static int g_fake_lib = 77;

/**
 * @brief Selective symbol resolver testing all 14 mandatory symbol groups.
 * @param handle Pointer to group index (`0..20`).
 * @param symbol_name Symbol name.
 * @param out_fn Output function pointer.
 * @return C_ORM_OK.
 */
static c_orm_error_t
selective_sym_resolver(void *handle, const char *symbol_name, void **out_fn) {
  int idx = *(int *)handle;
  static const char *const singles[11] = {
      "SQLAllocHandle", "SQLFreeHandle", "SQLSetEnvAttr",    "SQLDisconnect",
      "SQLEndTran",     "SQLExecute",    "SQLBindParameter", "SQLNumResultCols",
      "SQLFetch",       "SQLGetData",    "SQLFreeStmt"};
  if (idx >= 0 && idx < 11 && strcmp(symbol_name, singles[idx]) == 0) {
    *out_fn = NULL;
    return C_ORM_OK;
  }
  if (idx == 11 && (strcmp(symbol_name, "SQLDriverConnect") == 0 ||
                    strcmp(symbol_name, "SQLDriverConnectW") == 0 ||
                    strcmp(symbol_name, "SQLConnect") == 0)) {
    *out_fn = NULL;
    return C_ORM_OK;
  }
  if (idx == 12 && (strcmp(symbol_name, "SQLDriverConnect") == 0 ||
                    strcmp(symbol_name, "SQLDriverConnectW") == 0)) {
    *out_fn = NULL;
    return C_ORM_OK;
  }
  if (idx == 13 && strcmp(symbol_name, "SQLDriverConnect") == 0) {
    *out_fn = NULL;
    return C_ORM_OK;
  }
  if (idx == 14 && (strcmp(symbol_name, "SQLPrepare") == 0 ||
                    strcmp(symbol_name, "SQLPrepareW") == 0)) {
    *out_fn = NULL;
    return C_ORM_OK;
  }
  if (idx == 15 && strcmp(symbol_name, "SQLPrepare") == 0) {
    *out_fn = NULL;
    return C_ORM_OK;
  }
  if (idx == 16 && (strcmp(symbol_name, "SQLGetDiagRec") == 0 ||
                    strcmp(symbol_name, "SQLGetDiagRecW") == 0)) {
    *out_fn = NULL;
    return C_ORM_OK;
  }
  if (idx == 17 && strcmp(symbol_name, "SQLGetDiagRec") == 0) {
    *out_fn = NULL;
    return C_ORM_OK;
  }
  *out_fn = (void *)(size_t)SQLMock_AllocHandle;
  return C_ORM_OK;
}

#ifdef C_ORM_TEST_ALLOCATOR
/**
 * @brief Mock dlopen hook for runtime loader fallback chain testing.
 * @param path Candidate shared-library path.
 * @param out_handle Output module handle.
 * @return C_ORM_OK or C_ORM_ERROR_DRIVER_LOAD.
 */
static c_orm_error_t test_dlopen_hook(const char *path, void **out_handle) {
  *out_handle = NULL;
  if (g_dl_mode == 1) {
    /* Succeed on any path */
    *out_handle = &g_fake_lib;
    return C_ORM_OK;
  }
  if (g_dl_mode == 2) {
    /* Fail on IBM_DB_HOME & db2 candidates, succeed only on odbc candidate */
    if (strstr(path, "odbc") != NULL) {
      *out_handle = &g_fake_lib;
      return C_ORM_OK;
    }
    return C_ORM_ERROR_DRIVER_LOAD;
  }
  if (g_dl_mode == 3) {
    /* Fail on IBM_DB_HOME, succeed on db2 candidate */
    if (strstr(path, "IBM_HOME_BAD") != NULL) {
      return C_ORM_ERROR_DRIVER_LOAD;
    }
    *out_handle = &g_fake_lib;
    return C_ORM_OK;
  }
  return C_ORM_ERROR_DRIVER_LOAD;
}

/**
 * @brief Mock dlsym hook for runtime loader testing.
 * @param handle Module handle.
 * @param symbol_name Symbol name.
 * @param out_fn Output function pointer.
 * @return C_ORM_OK.
 */
static c_orm_error_t test_dlsym_hook(void *handle, const char *symbol_name,
                                     void **out_fn) {
  (void)handle;
  (void)symbol_name;
  *out_fn = (void *)(size_t)SQLMock_AllocHandle;
  return C_ORM_OK;
}

/**
 * @brief Mock dlclose hook tracking handle releases.
 * @param handle Module handle.
 * @return C_ORM_OK.
 */
static c_orm_error_t test_dlclose_hook(void *handle) {
  if (handle) {
    g_dl_closed++;
  }
  return C_ORM_OK;
}
#endif /* C_ORM_TEST_ALLOCATOR */

/**
 * @brief Succeeding slow-query log callback used to test slow-query logging.
 * @param msg Formatted slow-query log message.
 * @param user_data Optional caller context pointer.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t ok_slow_log_cb(const char *msg, void *user_data) {
  (void)msg;
  (void)user_data;
  return C_ORM_OK;
}

/**
 * @brief Failing slow-query log callback to test error percolation in step().
 * @param msg Log message.
 * @param user_data Unused.
 * @return C_ORM_ERROR_IO.
 */
static c_orm_error_t failing_slow_log_cb(const char *msg, void *user_data) {
  (void)msg;
  (void)user_data;
  return C_ORM_ERROR_IO;
}

/** @brief Exhaustive coverage of `src/c_orm_odbc_loader.c` fallback chains. */
TEST test_odbc_loader_exhaustive(void) {
  c_orm_odbc_api_t api;
  const c_orm_odbc_api_t *loaded = NULL;
  int is_direct = 0;
  int idx;

  /* Every required symbol missing vs alternate W/Connect present */
  for (idx = 0; idx <= 17; idx++) {
    c_orm_error_t rc =
        c_orm_odbc_populate_api(selective_sym_resolver, &idx, &api);
    if (idx == 12 || idx == 13 || idx == 15 || idx == 17) {
      ASSERT_EQ(C_ORM_OK, rc);
    } else {
      ASSERT_EQ(C_ORM_ERROR_DRIVER_LOAD, rc);
    }
  }

  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_db2_load_cli(NULL, &is_direct));

  /* Mock Db2 API with NULL and non-NULL out_is_direct_db2_cli */
  (void)build_mock_odbc_api(&api);
  ASSERT_EQ(C_ORM_OK, c_orm_db2_set_mock_api(&api, 0));
  ASSERT_EQ(C_ORM_OK, c_orm_db2_load_cli(&loaded, &is_direct));
  ASSERT_EQ(0, is_direct);
  ASSERT_EQ(C_ORM_OK, c_orm_db2_load_cli(&loaded, NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_db2_set_mock_api(&api, 1));
  ASSERT_EQ(C_ORM_OK, c_orm_db2_load_cli(&loaded, &is_direct));
  ASSERT_EQ(1, is_direct);
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_reset_loader());

#ifdef C_ORM_TEST_ALLOCATOR
  g_dl_closed = 0;
  ASSERT_EQ(C_ORM_OK,
            c_orm_odbc_set_test_dl_hooks(test_dlopen_hook, test_dlsym_hook,
                                         test_dlclose_hook));

  /* 1. C_ORM_ODBC_LIB success + cached reload + reset closing handle */
  g_dl_mode = 1;
  (void)set_test_env("C_ORM_ODBC_LIB", "/fake/libodbc_custom.so");
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_load_driver_manager(&loaded));
  ASSERT(loaded != NULL);
  /* Second call hits g_odbc_dyn_loaded cache */
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_load_driver_manager(&loaded));
  (void)set_test_env("C_ORM_ODBC_LIB", "");
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_reset_loader());
  ASSERT_EQ(1, g_dl_closed);

  /* 2. Default ODBC candidates loop success (with env var completely unset) */
#if !defined(_WIN32)
  (void)unsetenv("C_ORM_ODBC_LIB");
  (void)unsetenv("C_ORM_DB2_LIB");
  (void)unsetenv("IBM_DB_HOME");
#endif
  g_dl_mode = 1;
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_load_driver_manager(&loaded));
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_reset_loader());

  /* 3. Default ODBC candidates all fail */
  g_dl_mode = 0;
  ASSERT_EQ(C_ORM_ERROR_DRIVER_LOAD, c_orm_odbc_load_driver_manager(&loaded));

  /* 4. C_ORM_DB2_LIB fail & success (both NULL & non-NULL out_is_direct) +
   * cached reload (NULL & non-NULL out_is_direct) */
  g_dl_mode = 0;
  (void)set_test_env("C_ORM_DB2_LIB", "/fake/libdb2_bad.so");
  ASSERT_EQ(C_ORM_ERROR_DRIVER_LOAD, c_orm_db2_load_cli(&loaded, &is_direct));
  g_dl_mode = 1;
  ASSERT_EQ(C_ORM_OK, c_orm_db2_load_cli(&loaded, NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_db2_load_cli(&loaded, NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_db2_load_cli(&loaded, &is_direct));
  ASSERT_EQ(1, is_direct);
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_reset_loader());
  ASSERT_EQ(C_ORM_OK, c_orm_db2_load_cli(&loaded, &is_direct));
  ASSERT_EQ(1, is_direct);
  (void)set_test_env("C_ORM_DB2_LIB", "");
#if !defined(_WIN32)
  (void)unsetenv("C_ORM_DB2_LIB");
#endif
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_reset_loader());

  /* 5. IBM_DB_HOME success (NULL & non-NULL out_is_direct) + Safe CRT fail +
   * candidate fail falling through to db2_candidates */
  (void)set_test_env("IBM_DB_HOME", "/opt/ibm/clidriver");
  g_dl_mode = 1;
  ASSERT_EQ(C_ORM_OK, c_orm_db2_load_cli(&loaded, &is_direct));
  ASSERT_EQ(1, is_direct);
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_reset_loader());
  ASSERT_EQ(C_ORM_OK, c_orm_db2_load_cli(&loaded, NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_reset_loader());

  c_orm_mock_safe_crt_countdown = 1; /* 0=GETENV(DB2_LIB)? No: SPRINTF! */
  /* Sweep countdown 0..4 so C_ORM_SPRINTF failure in IBM_DB_HOME is hit */
  for (idx = 0; idx < 4; idx++) {
    g_dl_mode = 0;
    c_orm_mock_safe_crt_countdown = idx;
    (void)c_orm_db2_load_cli(&loaded, &is_direct);
    c_orm_mock_safe_crt_countdown = -1;
    (void)c_orm_odbc_reset_loader();
  }

  (void)set_test_env("IBM_DB_HOME", "/opt/IBM_HOME_BAD");
  g_dl_mode = 3; /* IBM_DB_HOME fails, db2_candidates succeeds */
  ASSERT_EQ(C_ORM_OK, c_orm_db2_load_cli(&loaded, &is_direct));
  ASSERT_EQ(1, is_direct);
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_reset_loader());
  ASSERT_EQ(C_ORM_OK, c_orm_db2_load_cli(&loaded, NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_reset_loader());
  (void)set_test_env("IBM_DB_HOME", "");

  /* 6. Db2 candidates fail -> falls back to ODBC Driver Manager */
  g_dl_mode = 2;
  ASSERT_EQ(C_ORM_OK, c_orm_db2_load_cli(&loaded, &is_direct));
  ASSERT_EQ(0, is_direct);
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_reset_loader());

  ASSERT_EQ(C_ORM_OK, c_orm_odbc_set_test_dl_hooks(NULL, NULL, NULL));
#endif
  PASS();
}

/** @brief Exhaustive edge-case, error-injection, catalog, and OOM tests for
 * `src/c_orm_odbc.c`. */
TEST test_odbc_exhaustive_branches_and_oom(void) {
  c_orm_odbc_api_t mock_api;
  const c_orm_driver_vtable_t *vt = NULL;
  c_orm_db_t *db = NULL;
  c_orm_db_t *sq_db = NULL;
  c_orm_query_t *q = NULL;
  char buf[2048];
  char big[1600];
  size_t len = 0;
  int timeout = 0;
  int autocommit = 1;
  int step_i;

  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_odbc_get_vtable(NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_get_vtable(&vt));
  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_connect(":memory:", &sq_db));

  /* 1. Percent decode & escape edge cases */
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_percent_decode("a\0b", 3, 0, buf, sizeof(buf), NULL));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_odbc_percent_decode("%41", 3, 0, buf, 1, NULL));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_odbc_percent_decode("+", 1, 1, buf, 1, NULL));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_odbc_percent_decode("a", 1, 0, buf, 1, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_percent_decode("%!0", 3, 0, buf, sizeof(buf), NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_percent_decode("%:0", 3, 0, buf, sizeof(buf), NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_percent_decode("%[0", 3, 0, buf, sizeof(buf), NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_percent_decode("%g0", 3, 0, buf, sizeof(buf), NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_percent_decode("%2g", 3, 0, buf, sizeof(buf), NULL));
  ASSERT_EQ(C_ORM_OK,
            c_orm_odbc_percent_decode("a+b%2B", 6, 0, buf, sizeof(buf), NULL));
  ASSERT_STR_EQ("a+b+", buf);

  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_odbc_escape_conn_value("x", buf, 0, NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_escape_conn_value("", buf, sizeof(buf), &len));
  ASSERT_EQ(0U, len);
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_escape_conn_value("", buf, sizeof(buf), NULL));
  ASSERT_EQ(C_ORM_OK,
            c_orm_odbc_escape_conn_value(" lead", buf, sizeof(buf), NULL));
  ASSERT_STR_EQ("{ lead}", buf);
  ASSERT_EQ(C_ORM_OK,
            c_orm_odbc_escape_conn_value("trail ", buf, sizeof(buf), NULL));
  ASSERT_STR_EQ("{trail }", buf);
  ASSERT_EQ(C_ORM_OK,
            c_orm_odbc_escape_conn_value("a\tb=1", buf, sizeof(buf), NULL));
  ASSERT_EQ(C_ORM_OK,
            c_orm_odbc_escape_conn_value("a\"b", buf, sizeof(buf), NULL));
  ASSERT_EQ(C_ORM_OK,
            c_orm_odbc_escape_conn_value("a'b", buf, sizeof(buf), NULL));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_odbc_escape_conn_value(";", buf, 1, NULL));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_odbc_escape_conn_value("}", buf, 4, NULL));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_odbc_escape_conn_value(";a", buf, 3, NULL));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_odbc_escape_conn_value(";a", buf, 4, NULL));

  /* 2. URL parser & append_conn_kv exhaustive branches */
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("odbc", buf, sizeof(buf), NULL, NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_build_conn_string("odbc://h?dsn=D", buf,
                                                   sizeof(buf), NULL, NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_build_conn_string("odbc://h/", buf,
                                                   sizeof(buf), NULL, NULL));
  ASSERT_EQ(C_ORM_OK,
            c_orm_odbc_build_conn_string("odbc://h/d?my-key.1=v&driver=a%09b",
                                         buf, sizeof(buf), NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_odbc_build_conn_string("odbc://h/d", NULL, 10, NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("DSN=bad\x01", buf, sizeof(buf), NULL,
                                         NULL));
  ASSERT_EQ(C_ORM_OK,
            c_orm_odbc_build_conn_string("SERVER=127.0.0.1;\tDatabase=x", buf,
                                         sizeof(buf), &timeout, &autocommit));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_odbc_build_conn_string("DSN=TooLong", buf, 5, NULL, NULL));
  ASSERT_EQ(
      C_ORM_ERROR_VALIDATION,
      c_orm_odbc_build_conn_string("odbc:///", buf, sizeof(buf), NULL, NULL));
  ASSERT_EQ(C_ORM_OK,
            c_orm_odbc_build_conn_string("odbc:///mydb?driver={PreBraced}", buf,
                                         sizeof(buf), NULL, NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_build_conn_string(
                          "odbc://:onlypass@[::1]/mydb?autocommit=on&", buf,
                          sizeof(buf), NULL, &autocommit));
  ASSERT_EQ(1, autocommit);
  ASSERT_EQ(C_ORM_OK,
            c_orm_odbc_build_conn_string("odbc://h/d?autocommit=0", buf,
                                         sizeof(buf), NULL, &autocommit));
  ASSERT_EQ(0, autocommit);
  ASSERT_EQ(C_ORM_OK,
            c_orm_odbc_build_conn_string("odbc://h/d?autocommit=no", buf,
                                         sizeof(buf), NULL, &autocommit));
  ASSERT_EQ(0, autocommit);
  ASSERT_EQ(C_ORM_OK,
            c_orm_odbc_build_conn_string("odbc://h/d?autocommit=off", buf,
                                         sizeof(buf), NULL, &autocommit));
  ASSERT_EQ(0, autocommit);
  ASSERT_EQ(C_ORM_OK,
            c_orm_odbc_build_conn_string("odbc://h/d?autocommit=true", buf,
                                         sizeof(buf), NULL, &autocommit));
  ASSERT_EQ(1, autocommit);
  ASSERT_EQ(C_ORM_OK,
            c_orm_odbc_build_conn_string("odbc://h/d?autocommit=1", buf,
                                         sizeof(buf), NULL, &autocommit));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("odbc://[::1]bad/d", buf, sizeof(buf),
                                         NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("odbc://[::1]:/d", buf, sizeof(buf),
                                         NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("odbc://[::1]:abc/d", buf, sizeof(buf),
                                         NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("odbc://[::1]:12x/d", buf, sizeof(buf),
                                         NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("odbc://[::1]:0/d", buf, sizeof(buf),
                                         NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("odbc://[%GG]/d", buf, sizeof(buf),
                                         NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("odbc://[::1]:%GG/d", buf, sizeof(buf),
                                         NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("odbc://u%GG:p@h/d", buf, sizeof(buf),
                                         NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("odbc://u:p%GG@h/d", buf, sizeof(buf),
                                         NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("odbc://u%GG@h/d", buf, sizeof(buf),
                                         NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("odbc://h%GG/d", buf, sizeof(buf),
                                         NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("odbc://h:%GG/d", buf, sizeof(buf),
                                         NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("odbc://h/d%GG", buf, sizeof(buf),
                                         NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("odbc://h/d?=1", buf, sizeof(buf),
                                         NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("odbc://h/d?k%GG=1", buf, sizeof(buf),
                                         NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("odbc://h/d?k=v%GG", buf, sizeof(buf),
                                         NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("odbc://h/d?k\x01=1", buf, sizeof(buf),
                                         NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("odbc://h/d?k%3D=1", buf, sizeof(buf),
                                         NULL, NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_build_conn_string("odbc://h/d?k==1", buf,
                                                   sizeof(buf), NULL, NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_build_conn_string("odbc://h/d?", buf,
                                                   sizeof(buf), NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("odbc://h/d?timeout=abc", buf,
                                         sizeof(buf), NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("odbc://h/d?timeout=10x", buf,
                                         sizeof(buf), NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("odbc://h/d?timeout=99999", buf,
                                         sizeof(buf), NULL, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_odbc_build_conn_string("odbc://h/d?driver=bad%01drv", buf,
                                         sizeof(buf), NULL, NULL));

  /* force_braces overflows (qval cap is 512, escaped cap is 512) */
  memcpy(big, "odbc://h/d?driver=", 18);
  memset(big + 18, '}', 260);
  big[278] = '\0';
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_odbc_build_conn_string(big, buf, sizeof(buf), NULL, NULL));
  memset(big + 18, 'a', 511); /* w reaches 511 -> w+1 >= 512 inside loop */
  big[529] = '\0';
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_odbc_build_conn_string(big, buf, sizeof(buf), NULL, NULL));
  memset(big + 18, 'a', 510); /* w reaches 511 after loop -> closing '}' ovf */
  big[528] = '\0';
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_odbc_build_conn_string(big, buf, sizeof(buf), NULL, NULL));
  memcpy(big, "odbc://h/d?extra=", 17);
  memset(big + 17, '}', 260);
  big[277] = '\0';
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_odbc_build_conn_string(big, buf, sizeof(buf), NULL, NULL));

  /* extra_kv[1024] overflow + small buf_size sweep */
  {
    int p = 11;
    memcpy(big, "odbc://h/d?", 11);
    for (step_i = 0; step_i < 11; step_i++) {
      int k;
      big[p++] = 'k';
      big[p++] = (char)('a' + step_i);
      big[p++] = '=';
      for (k = 0; k < 95; k++) {
        big[p++] = 'x';
      }
      if (step_i < 10) {
        big[p++] = '&';
      }
    }
    big[p] = '\0';
    ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
              c_orm_odbc_build_conn_string(big, buf, sizeof(buf), NULL, NULL));
  }
  for (len = 2; len < 140; len += 4) {
    (void)c_orm_odbc_build_conn_string(
        "odbc://u:p@host:5432/db?driver=Drv&dsn=Dsn&k=v", buf, len, NULL, NULL);
  }

  /* 3. Handle/diag introspection & connect error paths */
  (void)reset_mock_state();
  (void)build_mock_odbc_api(&mock_api);
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_set_mock_api(&mock_api));

  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_odbc_connect(NULL, &db));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_odbc_connect("odbc://h/d", NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_odbc_connect("odbc://", &db));
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_set_mock_api(NULL));
  (void)set_test_env("C_ORM_ODBC_LIB", "/nonexistent_odbc_999.so");
  ASSERT_EQ(C_ORM_ERROR_DRIVER_LOAD, c_orm_odbc_connect("odbc://h/d", &db));
  (void)set_test_env("C_ORM_ODBC_LIB", "");
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_set_mock_api(&mock_api));

  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_odbc_connect_raw(NULL, "DSN=x", "odbc", NULL, 0, 1, 0, &db));
  ASSERT_EQ(
      C_ORM_ERROR_NULL_POINTER,
      c_orm_odbc_connect_raw(&mock_api, NULL, "odbc", NULL, 0, 1, 0, &db));
  ASSERT_EQ(
      C_ORM_ERROR_NULL_POINTER,
      c_orm_odbc_connect_raw(&mock_api, "DSN=x", NULL, NULL, 0, 1, 0, &db));
  ASSERT_EQ(
      C_ORM_ERROR_NULL_POINTER,
      c_orm_odbc_connect_raw(&mock_api, "DSN=x", "odbc", NULL, 0, 1, 0, NULL));

  {
    static const char *const conn_fails[4] = {"AllocEnv", "SetEnvAttr",
                                              "AllocDbc", "DriverConnect"};
    for (step_i = 0; step_i < 4; step_i++) {
      g_mock.fail_on_op = conn_fails[step_i];
      ASSERT_NEQ(C_ORM_OK, c_orm_odbc_connect_raw(&mock_api, "DSN=x", "odbc",
                                                  NULL, 0, 0, 0, &db));
    }
    g_mock.fail_on_op = "DriverConnectW";
    ASSERT_NEQ(C_ORM_OK, c_orm_odbc_connect_raw(&mock_api, "DSN=x", "odbc",
                                                NULL, 0, 1, 1, &db));
    g_mock.fail_on_op = NULL;
    /* Invalid UTF-8 in wide connect_raw */
    ASSERT_NEQ(C_ORM_OK, c_orm_odbc_connect_raw(&mock_api, "DSN=\xFF", "odbc",
                                                NULL, 0, 1, 1, &db));
  }

  /* Connect with SQLSetConnectAttr == NULL (autocommit=1 commit & rollback) */
  mock_api.SQLSetConnectAttr = NULL;
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_connect_raw(&mock_api, "DSN=x", "odbc", NULL,
                                             5, 1, 0, &db));
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED,
            c_orm_odbc_set_isolation(db, C_ORM_SQL_TXN_SERIALIZABLE));
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED, c_orm_odbc_begin_transaction(db));
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_commit(db));
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_rollback(db));
  ASSERT_EQ(C_ORM_OK, vt->disconnect(db));
  ASSERT_EQ(C_ORM_OK, vt->disconnect(NULL));
  {
    c_orm_db_t *fake_db = NULL;
    int bad_magic = 42;
    ASSERT_EQ(C_ORM_OK,
              c_orm_system_calloc(1, sizeof(c_orm_db_t), (void **)&fake_db));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_odbc_get_handles(fake_db, NULL, NULL, NULL));
    ASSERT_EQ(C_ORM_OK, vt->disconnect(fake_db));
    ASSERT_EQ(C_ORM_OK,
              c_orm_system_calloc(1, sizeof(c_orm_db_t), (void **)&fake_db));
    fake_db->driver_data = &bad_magic;
    ASSERT_EQ(C_ORM_OK, vt->disconnect(fake_db));
  }
  (void)build_mock_odbc_api(&mock_api);

  /* prefer_wide = 1 when W functions are NULL -> falls back to ANSI */
  mock_api.SQLDriverConnectW = NULL;
  mock_api.SQLPrepareW = NULL;
  mock_api.SQLDescribeColW = NULL;
  mock_api.SQLGetDiagRecW = NULL;
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_connect_raw(&mock_api, "DSN=x", "odbc", NULL,
                                             0, 1, 1, &db));
  ASSERT_EQ(C_ORM_OK, vt->prepare(db, "SELECT 1", &q));
  {
    int hr = 0;
    g_mock.result_mode = 6;
    g_mock.custom_col_type = C_ORM_SQL_VARCHAR;
    g_mock.custom_str = "x";
    g_mock.total_rows = 1;
    ASSERT_EQ(C_ORM_OK, vt->step(q, &hr));
    (void)c_orm_odbc_harvest_diag(db, C_ORM_SQL_HANDLE_DBC, &g_dummy_dbc, "op",
                                  C_ORM_ERROR_SQL);
  }
  ASSERT_EQ(C_ORM_OK, vt->finalize(q));
  ASSERT_EQ(C_ORM_OK, vt->disconnect(db));
  (void)build_mock_odbc_api(&mock_api);

  /* Non-ODBC & NULL handle checks on introspection & harvest_diag */
  {
    void *henv_out = NULL;
    void *hdbc_out = NULL;
    char st_out[6];
    c_orm_sqlinteger_t nat_out = 0;
    const char *msg_out = NULL;

    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_odbc_get_handles(NULL, NULL, NULL, NULL));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_odbc_get_handles(sq_db, NULL, &henv_out, &hdbc_out));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_odbc_get_sqlstate(NULL, st_out));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_odbc_get_sqlstate(sq_db, NULL));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_odbc_get_sqlstate(sq_db, st_out));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_odbc_get_native_error(NULL, &nat_out));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_odbc_get_native_error(sq_db, NULL));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_odbc_get_native_error(sq_db, &nat_out));
    ASSERT_EQ(C_ORM_ERROR_SQL,
              c_orm_odbc_harvest_diag(NULL, C_ORM_SQL_HANDLE_DBC, NULL, "op",
                                      C_ORM_ERROR_SQL));
    ASSERT_EQ(C_ORM_ERROR_SQL,
              c_orm_odbc_harvest_diag(sq_db, C_ORM_SQL_HANDLE_DBC, NULL, "op",
                                      C_ORM_ERROR_SQL));

    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, vt->get_last_error(NULL, &msg_out));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, vt->get_last_error(sq_db, NULL));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, vt->get_last_error(sq_db, &msg_out));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, vt->get_last_trace(NULL, &msg_out));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, vt->get_last_trace(sq_db, NULL));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, vt->get_last_trace(sq_db, &msg_out));
  }

  /* 4. Harvest diag: Db2 native codes, missing GetDiagRec, empty rec, >2KB */
  {
    const c_orm_odbc_api_t *api_out = NULL;
    void *henv_out = NULL;
    void *hdbc_out = NULL;
    static const c_orm_sqlinteger_t db2_nats[11] = {
        100, -803, -802, -911, -913, -302, -433, -407, -530, -532, -545};
    static const char *const extra_states[9] = {"02000", "HYT01", "57033",
                                                "57014", "22003", "22007",
                                                "22018", "42000", "99999"};
    ASSERT_EQ(C_ORM_OK, c_orm_odbc_connect_raw(&mock_api, "DSN=x", "db2", NULL,
                                               0, 1, 1, &db));
    ASSERT_EQ(C_ORM_OK,
              c_orm_odbc_get_handles(db, &api_out, &henv_out, &hdbc_out));
    ASSERT(api_out != NULL && henv_out != NULL && hdbc_out != NULL);
    ASSERT_EQ(C_ORM_OK, c_orm_odbc_get_handles(db, NULL, NULL, NULL));
    ASSERT_EQ(C_ORM_ERROR_SQL,
              c_orm_odbc_harvest_diag(db, C_ORM_SQL_HANDLE_DBC, NULL, "op",
                                      C_ORM_ERROR_SQL));
    ASSERT_EQ(C_ORM_ERROR_SQL,
              c_orm_odbc_harvest_diag(db, C_ORM_SQL_HANDLE_DBC, NULL, NULL,
                                      C_ORM_ERROR_SQL));

    (void)C_ORM_STRCPY(g_mock.diag_sqlstate, sizeof(g_mock.diag_sqlstate),
                       "99999");
    for (step_i = 0; step_i < 11; step_i++) {
      g_mock.diag_native = db2_nats[step_i];
      (void)c_orm_odbc_harvest_diag(db, C_ORM_SQL_HANDLE_DBC, hdbc_out, "op",
                                    C_ORM_ERROR_SQL);
    }
    g_mock.diag_native = 0;
    for (step_i = 0; step_i < 9; step_i++) {
      (void)C_ORM_STRCPY(g_mock.diag_sqlstate, sizeof(g_mock.diag_sqlstate),
                         extra_states[step_i]);
      (void)c_orm_odbc_harvest_diag(db, C_ORM_SQL_HANDLE_DBC, hdbc_out, "op",
                                    C_ORM_ERROR_SQL);
    }

    /* Short state != 5, zero_wide_len, flood_diag_recs > 2048 bytes */
    (void)C_ORM_STRCPY(g_mock.diag_sqlstate, sizeof(g_mock.diag_sqlstate),
                       "AB");
    g_mock.zero_wide_len = 1;
    (void)c_orm_odbc_harvest_diag(db, C_ORM_SQL_HANDLE_DBC, hdbc_out, "op",
                                  C_ORM_ERROR_SQL);
    g_mock.flood_diag_recs = 1;
    (void)c_orm_odbc_harvest_diag(db, C_ORM_SQL_HANDLE_DBC, hdbc_out, "op",
                                  C_ORM_ERROR_SQL);
    g_mock.flood_diag_recs = 0;
    g_mock.zero_wide_len = 0;

    /* Empty diag records (rec_num == 1 returns NO_DATA) & NULL op */
    g_mock.diag_msg1 = NULL;
    (void)c_orm_odbc_harvest_diag(db, C_ORM_SQL_HANDLE_DBC, hdbc_out, NULL,
                                  C_ORM_ERROR_SQL);
    ASSERT_EQ(C_ORM_OK, vt->disconnect(db));

    /* ANSI flood_diag_recs + zero_wide_len (tlen < 0) + no GetDiagRec at all */
    g_mock.diag_msg1 = "err";
    ASSERT_EQ(C_ORM_OK, c_orm_odbc_connect_raw(&mock_api, "DSN=x", "odbc", NULL,
                                               0, 1, 0, &db));
    g_mock.zero_wide_len = 1;
    g_mock.flood_diag_recs = 1;
    (void)c_orm_odbc_harvest_diag(db, C_ORM_SQL_HANDLE_DBC, &g_dummy_dbc, "op",
                                  C_ORM_ERROR_SQL);
    g_mock.flood_diag_recs = 0;
    g_mock.zero_wide_len = 0;
    ASSERT_EQ(C_ORM_OK, vt->disconnect(db));

    mock_api.SQLGetDiagRec = NULL;
    mock_api.SQLGetDiagRecW = NULL;
    ASSERT_EQ(C_ORM_OK, c_orm_odbc_connect_raw(&mock_api, "DSN=x", "odbc", NULL,
                                               0, 1, 0, &db));
    (void)c_orm_odbc_harvest_diag(db, C_ORM_SQL_HANDLE_DBC, &g_dummy_dbc, NULL,
                                  C_ORM_ERROR_SQL);
    (void)c_orm_odbc_harvest_diag(db, C_ORM_SQL_HANDLE_DBC, &g_dummy_dbc, "op",
                                  C_ORM_ERROR_SQL);
    ASSERT_EQ(C_ORM_OK, vt->disconnect(db));
    (void)build_mock_odbc_api(&mock_api);
  }

  /* 5. Prepare, bind_*, step, streaming, getters, transactions */
  (void)reset_mock_state();
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_connect_raw(&mock_api, "DSN=x", "db2", NULL, 0,
                                             0, 1, &db));
  /* NULL/invalid args on vtable methods */
  {
    int has_r = 0;
    int nul_f = 0;
    int32_t i32 = 0;
    int64_t i64 = 0;
    double dbl = 0.0;
    const char *s = NULL;
    const void *bl = NULL;
    size_t bsz = 0;
    c_orm_decimal_t dec;
    int cc = 0;

    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, vt->prepare(NULL, "SELECT 1", &q));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, vt->prepare(db, NULL, &q));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, vt->prepare(db, "SELECT 1", NULL));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, vt->prepare(sq_db, "SELECT 1", &q));

    g_mock.fail_on_op = "AllocStmt";
    ASSERT_NEQ(C_ORM_OK, vt->prepare(db, "SELECT 1", &q));
    g_mock.fail_on_op = "PrepareW";
    ASSERT_NEQ(C_ORM_OK, vt->prepare(db, "SELECT 1", &q));
    g_mock.fail_on_op = NULL;
    ASSERT_NEQ(C_ORM_OK, vt->prepare(db, "SELECT \xFF", &q));

    ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_int32(NULL, 1, 1));
    ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_int64(NULL, 1, 1));
    ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_int64(NULL, 0, 1));
    ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_double(NULL, 1, 1.0));
    ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_null(NULL, 1));
    ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_string(NULL, 1, "a"));
    ASSERT_EQ(C_ORM_ERROR_BIND,
              c_orm_odbc_bind_wide_param(NULL, 1, "a", C_ORM_SQL_WVARCHAR));
    ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_blob(NULL, 1, "a", 1));
    ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_decimal(NULL, 1, NULL));

    ASSERT_EQ(C_ORM_ERROR_STEP, vt->step(NULL, &has_r));
    ASSERT_EQ(C_ORM_ERROR_STEP, vt->step((c_orm_query_t *)db, NULL));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, vt->is_null(NULL, 0, &nul_f));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, vt->get_int64(NULL, 0, &i64));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, vt->get_int32(NULL, 0, NULL));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, vt->get_int32(NULL, 0, &i32));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, vt->get_double(NULL, 0, &dbl));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, vt->get_decimal(NULL, 0, &dec));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, vt->get_string(NULL, 0, &s));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, vt->get_blob(NULL, 0, &bl, &bsz));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, vt->get_blob(NULL, 0, NULL, &bsz));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, vt->reset(NULL));
    ASSERT_EQ(C_ORM_OK, vt->finalize(NULL));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, vt->get_last_insert_rowid(NULL, &i64));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, vt->get_last_insert_rowid(db, NULL));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, vt->get_column_count(NULL, &cc));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, vt->get_column_name(NULL, 0, &s));

    /* Wide prepare + bind edge cases (>4000 chars, empty "", rebind slot,
     * invalid UTF-8, c_orm_odbc_bind_wide_param, BindParameter failures) */
    ASSERT_EQ(C_ORM_OK, vt->prepare(db, "SELECT 1", &q));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, vt->is_null(q, 0, NULL));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, vt->get_int64(q, 0, NULL));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, vt->get_double(q, 0, NULL));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, vt->get_decimal(q, 0, NULL));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, vt->get_string(q, 0, NULL));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, vt->get_blob(q, 0, NULL, &bsz));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, vt->get_blob(q, 0, &bl, NULL));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, vt->get_column_count(q, NULL));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, vt->get_column_name(q, 0, NULL));
    ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_int32(q, 40000, 1));
    ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_int64(q, 0, 1));
    ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_double(q, 0, 1.0));
    ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_null(q, 0));
    ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_string(q, 0, "a"));
    ASSERT_EQ(C_ORM_ERROR_BIND,
              c_orm_odbc_bind_wide_param(q, 0, "a", C_ORM_SQL_WVARCHAR));
    ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_blob(q, 0, "a", 1));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, vt->bind_blob(q, 1, NULL, 5));
    ASSERT_EQ(C_ORM_OK, vt->bind_decimal(q, 1, NULL));
    ASSERT_EQ(C_ORM_OK,
              c_orm_odbc_bind_wide_param(q, 1, NULL, C_ORM_SQL_WVARCHAR));
    ASSERT_EQ(C_ORM_OK,
              c_orm_odbc_bind_wide_param(q, 1, "", C_ORM_SQL_WVARCHAR));
    ASSERT_EQ(C_ORM_OK,
              c_orm_odbc_bind_wide_param(q, 1, "rebind", C_ORM_SQL_WVARCHAR));
    ASSERT_NEQ(C_ORM_OK,
               c_orm_odbc_bind_wide_param(q, 1, "\xFF", C_ORM_SQL_WVARCHAR));
    ASSERT_EQ(C_ORM_OK, vt->bind_string(q, 1, ""));
    ASSERT_NEQ(C_ORM_OK, vt->bind_string(q, 1, "\xFF"));
    {
      char *long_s = (char *)malloc(8100);
      memset(long_s, 'X', 8050);
      long_s[8050] = '\0';
      ASSERT_EQ(C_ORM_OK, vt->bind_string(q, 1, long_s));     /* WLONGVARCHAR */
      ASSERT_EQ(C_ORM_OK, vt->bind_blob(q, 2, long_s, 8050)); /* LONGVARBIN */
      free(long_s);
    }

    /* Bad decimal precision & SQLBindParameter failure across all bind_* */
    memset(&dec, 0, sizeof(dec));
    dec.precision = 39;
    ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_decimal(q, 0, &dec));
    ASSERT_NEQ(C_ORM_OK, vt->bind_decimal(q, 1, &dec));
    (void)c_orm_decimal_from_str("12.34", &dec);

    g_mock.fail_on_op = "BindParameter";
    ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_int32(q, 1, 1));
    ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_int64(q, 1, 1));
    ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_double(q, 1, 1.0));
    ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_null(q, 1));
    ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_string(q, 1, "x"));
    ASSERT_EQ(C_ORM_ERROR_BIND,
              c_orm_odbc_bind_wide_param(q, 1, "x", C_ORM_SQL_WVARCHAR));
    ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_blob(q, 1, "x", 1));
    ASSERT_EQ(C_ORM_ERROR_BIND, vt->bind_decimal(q, 1, &dec));
    g_mock.fail_on_op = NULL;

    /* Unpositioned getters before step -> C_ORM_ERROR_VALIDATION */
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, vt->is_null(q, 0, &nul_f));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, vt->get_int64(q, 0, &i64));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, vt->get_double(q, 0, &dbl));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, vt->get_decimal(q, 0, &dec));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, vt->get_string(q, 0, &s));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, vt->get_blob(q, 0, &bl, &bsz));

    /* ExecuteNoData, failing slow_log_cb, NumResultCols error, DescribeColW
     * zero_wide_len */
    g_mock.fail_on_op = "ExecuteNoData";
    ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
    ASSERT_EQ(0, has_r);
    ASSERT_EQ(C_ORM_OK, vt->reset(q));

    /* Slow-query logging (sleep_exec = 1 -> >= 1ms): NULL cb, ok cb, fail cb */
    db->slow_query_threshold_ms = 1;
    g_mock.sleep_exec = 1;
    g_mock.result_mode = 0;
    db->log_cb = NULL;
    ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
    ASSERT_EQ(C_ORM_OK, vt->reset(q));
    db->log_cb = ok_slow_log_cb;
    ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
    ASSERT_EQ(C_ORM_OK, vt->reset(q));
    db->log_cb = failing_slow_log_cb;
    ASSERT_EQ(C_ORM_ERROR_IO, vt->step(q, &has_r));
    ASSERT_EQ(C_ORM_OK, vt->reset(q));
    g_mock.sleep_exec = 0;

    g_mock.fail_on_op = "NumResultCols";
    ASSERT_NEQ(C_ORM_OK, vt->get_column_count(q, &cc));
    ASSERT_NEQ(C_ORM_OK, vt->get_column_name(q, 0, &s));
    ASSERT_NEQ(C_ORM_OK, vt->step(q, &has_r));
    db->slow_query_threshold_ms = 0;
    db->log_cb = NULL;
    g_mock.fail_on_op = NULL;

    /* get_column_name before step (!stmt->executed) + DescribeColW failure */
    g_mock.result_mode = 6;
    g_mock.custom_col_type = C_ORM_SQL_TYPE_TIMESTAMP;
    g_mock.custom_str = "2026-11-17-14.30.00.123456";
    g_mock.total_rows = 1;
    g_mock.fail_on_op = "DescribeColW";
    ASSERT_EQ(C_ORM_OK, vt->get_column_name(q, 0, &s));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, vt->get_column_name(q, -1, &s));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, vt->get_column_name(q, 9, &s));
    g_mock.fail_on_op = NULL;
    ASSERT_EQ(C_ORM_OK, vt->reset(q));
    g_mock.zero_wide_len = 1;
    ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
    g_mock.zero_wide_len = 0;
    ASSERT_EQ(C_ORM_OK, vt->get_column_count(q, &cc));
    ASSERT_EQ(C_ORM_OK, vt->get_column_name(q, 0, &s));
    ASSERT_STR_EQ("c0", s);
    ASSERT_EQ(C_ORM_OK, vt->get_string(q, 0, &s));
    ASSERT_STR_EQ("2026-11-17 14:30:00.123456", s);

    /* Grow cols from 1 -> 7 in mode 1 */
    ASSERT_EQ(C_ORM_OK, vt->reset(q));
    g_mock.result_mode = 1;
    g_mock.total_rows = 1;
    ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
    /* Test col->has_dec int64 truncation on col 4 (-123456789.75) */
    ASSERT_EQ(C_ORM_OK, vt->get_int64(q, 4, &i64));
    ASSERT_EQ(-123456789, (int)i64);
    /* Test NULL getters on col 6 (opt_null) */
    ASSERT_EQ(C_ORM_OK, vt->get_int64(q, 6, &i64));
    ASSERT_EQ(0, (int)i64);
    ASSERT_EQ(C_ORM_OK, vt->get_double(q, 6, &dbl));
    ASSERT_EQ(C_ORM_OK, vt->get_decimal(q, 6, &dec));
    ASSERT_EQ(C_ORM_OK, vt->get_blob(q, 6, &bl, &bsz));
    ASSERT_EQ(NULL, bl);
    ASSERT_EQ(C_ORM_OK, vt->finalize(q));

    /* Mode 6 single-col edge cases: SQL_NUMERIC NULL/bad/overflow, DECFLOAT
     * Infinity, trailing whitespace, >128B Db2 timestamp, stream chunk error */
    {
      char ts_big[160];
      memset(ts_big, '0', sizeof(ts_big) - 1U);
      memcpy(ts_big, "2026-11-17-14.30.00.", 20);
      ts_big[sizeof(ts_big) - 1U] = '\0';

      g_mock.result_mode = 6;
      g_mock.custom_col_type = C_ORM_SQL_TYPE_TIMESTAMP;
      g_mock.custom_str = ts_big;
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->prepare(db, "SELECT ts", &q));
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_EQ(C_ORM_ERROR_VALIDATION, vt->get_string(q, -1, &s));
      ASSERT_EQ(C_ORM_ERROR_VALIDATION, vt->get_blob(q, 99, &bl, &bsz));
      ASSERT_EQ(C_ORM_OK, vt->get_string(q, 0, &s));
      ASSERT_EQ(':', s[13]);

      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.custom_col_type = C_ORM_SQL_TYPE_TIMESTAMP;
      g_mock.custom_str = NULL; /* NULL Db2 timestamp */
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_EQ(C_ORM_OK, vt->is_null(q, 0, &nul_f));

      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.custom_str = "short"; /* < 19 chars Db2 timestamp */
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_EQ(C_ORM_OK, vt->get_string(q, 0, &s));

      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.custom_col_type = C_ORM_SQL_TYPE_TIMESTAMP;
      g_mock.custom_str = "2026/11/17 14:30:00"; /* non-Db2 dash/dot format */
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_EQ(C_ORM_OK, vt->get_string(q, 0, &s));

      /* Whitespace & decimal-string fallbacks in get_int64 / get_double /
       * get_decimal */
      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.custom_col_type = C_ORM_SQL_VARCHAR;
      g_mock.custom_str = "99999999999999999999e-15";
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_EQ(C_ORM_OK, vt->get_int64(q, 0, &i64));
      ASSERT_EQ(99999, (int)i64);

      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.custom_str = "-3000000000";
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_EQ(C_ORM_ERROR_OVERFLOW, vt->get_int32(q, 0, &i32));

      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.custom_str = "+Infinity";
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW, vt->get_decimal(q, 0, &dec));

      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.custom_col_type = C_ORM_SQL_NUMERIC;
      g_mock.custom_str = "NUM_NULL";
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_EQ(C_ORM_OK, vt->is_null(q, 0, &nul_f));
      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.custom_col_type = C_ORM_SQL_VARCHAR;
      g_mock.custom_str = "abc";
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH, vt->get_int32(q, 0, &i32));

      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.custom_str = ".5e25";
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH, vt->get_int64(q, 0, &i64));

      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.custom_str = "12abc";
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH, vt->get_int64(q, 0, &i64));

      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.custom_str = "1e25";
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH, vt->get_int64(q, 0, &i64));

      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.custom_col_type = C_ORM_SQL_VARCHAR;
      g_mock.custom_str = "42 \t";
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_EQ(C_ORM_OK, vt->get_int32(q, 0, &i32));
      ASSERT_EQ(42, i32);

      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.custom_str = "+99.50";
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_EQ(C_ORM_OK, vt->get_int64(q, 0, &i64));
      ASSERT_EQ(99, (int)i64);

      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.custom_str = ".75"; /* strtoint64 fails, decimal_from_str works */
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_EQ(C_ORM_OK, vt->get_int64(q, 0, &i64));
      ASSERT_EQ(0, (int)i64);

      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.custom_str = "3.25 \t";
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_EQ(C_ORM_OK, vt->get_double(q, 0, &dbl));

      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.custom_str = "3.25bad";
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_EQ(C_ORM_ERROR_TYPE_MISMATCH, vt->get_double(q, 0, &dbl));

      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.custom_str = " \t-Infinity";
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW, vt->get_decimal(q, 0, &dec));

      /* SQL_NUMERIC NULL, bad precision, and int64 overflow */
      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.custom_col_type = C_ORM_SQL_DECIMAL;
      g_mock.custom_str = "NUM_NULL";
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_EQ(C_ORM_OK, vt->is_null(q, 0, &nul_f));
      ASSERT_EQ(1, nul_f);

      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.custom_str = "NUM_BAD_PREC";
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_NEQ(C_ORM_OK, vt->get_decimal(q, 0, &dec));

      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.custom_str = "NUM_BIG";
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW, vt->get_int64(q, 0, &i64));

      /* Multi-chunk SQLGetData failure on chunk 2 */
      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.custom_col_type = C_ORM_SQL_CLOB;
      g_mock.custom_str = g_large_clob;
      g_mock.total_rows = 1;
      g_mock.getdata_fail_after = 2;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_NEQ(C_ORM_OK, vt->get_string(q, 0, &s));
      g_mock.getdata_fail_after = 1;
      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_NEQ(C_ORM_OK, vt->get_string(q, 0, &s));
      g_mock.getdata_fail_after = 0;

      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.getdata_nodata_after = 2; /* SQL_NO_DATA on chunk 2 of CLOB */
      g_mock.custom_col_type = C_ORM_SQL_CLOB;
      g_mock.custom_str = g_large_clob;
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_EQ(C_ORM_OK, vt->get_string(q, 0, &s));
      g_mock.getdata_nodata_after = 0;

      /* Mode 7: wide NULL, short inline, 50-Euro inline heap, bad surrogate,
       * 300-unit NO_TOTAL stream + chunk error + stream bad surrogate */
      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.result_mode = 7;
      g_mock.custom_col_type = C_ORM_SQL_WVARCHAR;
      g_mock.custom_str = NULL;
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_EQ(C_ORM_OK, vt->is_null(q, 0, &nul_f));
      ASSERT_EQ(1, nul_f);

      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.custom_str = "WIDE_SHORT";
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_EQ(C_ORM_OK, vt->get_string(q, 0, &s));
      ASSERT_STR_EQ("Hi", s);

      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.custom_str = "WIDE_BMP50";
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_EQ(C_ORM_OK, vt->get_string(q, 0, &s));
      ASSERT_EQ(150U, strlen(s));

      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.custom_str = "WIDE_BAD_SURR";
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_NEQ(C_ORM_OK, vt->get_string(q, 0, &s));

      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.custom_str = "WIDE_STREAM_UNK";
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_EQ(C_ORM_OK, vt->get_string(q, 0, &s));
      ASSERT_EQ(300U, strlen(s));

      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.custom_str = "WIDE_STREAM_NO_TOTAL";
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_EQ(C_ORM_OK, vt->get_string(q, 0, &s));
      ASSERT_EQ(300U, strlen(s));

      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.zero_wide_len = 1; /* injects 0xD800 in last wide chunk */
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_NEQ(C_ORM_OK, vt->get_string(q, 0, &s));
      g_mock.zero_wide_len = 0;

      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.getdata_fail_after = 1;
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_NEQ(C_ORM_OK, vt->get_string(q, 0, &s));

      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.getdata_fail_after = 2;
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_NEQ(C_ORM_OK, vt->get_string(q, 0, &s));
      g_mock.getdata_fail_after = 0;

      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.getdata_nodata_after = 2; /* SQL_NO_DATA on chunk 2 of wide */
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &has_r));
      ASSERT_EQ(C_ORM_OK, vt->get_string(q, 0, &s));
      g_mock.getdata_nodata_after = 0;

      ASSERT_EQ(C_ORM_OK, vt->finalize(q));
    }

    /* last_insert_rowid when Prepare, Execute, Fetch(0 rows), or GetData fails
     */
    g_mock.fail_on_op = "PrepareW";
    ASSERT_EQ(C_ORM_OK, vt->get_last_insert_rowid(db, &i64));
    g_mock.fail_on_op = "Execute";
    ASSERT_EQ(C_ORM_OK, vt->get_last_insert_rowid(db, &i64));
    g_mock.fail_on_op = "ExecuteNoData";
    ASSERT_EQ(C_ORM_OK, vt->get_last_insert_rowid(db, &i64));
    g_mock.fail_on_op = "GetData";
    ASSERT_EQ(C_ORM_OK, vt->get_last_insert_rowid(db, &i64));
    g_mock.fail_on_op = NULL;
    g_mock.custom_str = "ID_NULL";
    ASSERT_EQ(C_ORM_OK, vt->get_last_insert_rowid(db, &i64));
    g_mock.custom_str = "ID_BAD";
    ASSERT_EQ(C_ORM_OK, vt->get_last_insert_rowid(db, &i64));
    g_mock.custom_str = NULL;

    /* Transactions: non-ODBC db, errors, and disconnect while in_tx == 1 */
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_odbc_set_isolation(sq_db, C_ORM_SQL_TXN_READ_COMMITTED));
    ASSERT_EQ(C_ORM_OK,
              c_orm_odbc_set_isolation(db, C_ORM_SQL_TXN_READ_UNCOMMITTED));
    ASSERT_EQ(C_ORM_OK,
              c_orm_odbc_set_isolation(db, C_ORM_SQL_TXN_READ_COMMITTED));
    ASSERT_EQ(C_ORM_OK,
              c_orm_odbc_set_isolation(db, C_ORM_SQL_TXN_SERIALIZABLE));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_odbc_begin_transaction(sq_db));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_odbc_commit(sq_db));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_odbc_rollback(sq_db));

    g_mock.fail_on_op = "SetConnectAttr";
    ASSERT_NEQ(C_ORM_OK,
               c_orm_odbc_set_isolation(db, C_ORM_SQL_TXN_SERIALIZABLE));
    ASSERT_NEQ(C_ORM_OK, c_orm_odbc_begin_transaction(db));
    g_mock.fail_on_op = NULL;

    ASSERT_EQ(C_ORM_OK, c_orm_odbc_begin_transaction(db));
    g_mock.fail_on_op = "EndTran";
    ASSERT_NEQ(C_ORM_OK, c_orm_odbc_commit(db));
    ASSERT_NEQ(C_ORM_OK, c_orm_odbc_rollback(db));
    g_mock.fail_on_op = NULL;
    /* base_autocommit == 0 on db -> commit & rollback don't re-enable AC */
    ASSERT_EQ(C_ORM_OK, c_orm_odbc_commit(db));
    ASSERT_EQ(C_ORM_OK, c_orm_odbc_begin_transaction(db));
    ASSERT_EQ(C_ORM_OK, c_orm_odbc_rollback(db));
    /* Leave in_tx = 1 so disconnect exercises SQLEndTran(ROLLBACK) */
    ASSERT_EQ(C_ORM_OK, c_orm_odbc_begin_transaction(db));
    ASSERT_EQ(C_ORM_OK, vt->disconnect(db));
  }

  /* 6. ANSI prepare/bind >8000, catalog list_tables & list_columns, batch,
   * and full OOM sweeps */
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_connect_raw(&mock_api, "DSN=x", "odbc", NULL,
                                             0, 1, 0, &db));
  {
    char *long_a = (char *)malloc(8100);
    memset(long_a, 'A', 8050);
    long_a[8050] = '\0';
    g_mock.fail_on_op = "Prepare";
    ASSERT_NEQ(C_ORM_OK, vt->prepare(db, "SELECT 1", &q));
    g_mock.fail_on_op = NULL;
    ASSERT_EQ(C_ORM_OK, vt->prepare(db, "SELECT 1", &q));
    ASSERT_EQ(C_ORM_OK, vt->bind_string(q, 1, ""));
    ASSERT_EQ(C_ORM_OK, vt->bind_string(q, 1, long_a)); /* LONGVARCHAR */
    free(long_a);
    {
      int hr = 0;
      const char *ts_s = NULL;
      g_mock.result_mode = 6;
      g_mock.custom_col_type = C_ORM_SQL_TYPE_TIMESTAMP;
      g_mock.custom_str = "2026-11-17-14.30.00.000000";
      g_mock.total_rows = 1;
      g_mock.fail_on_op = "DescribeCol";
      ASSERT_EQ(C_ORM_OK, vt->step(q, &hr));
      g_mock.fail_on_op = NULL;
      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &hr));
      ASSERT_EQ(C_ORM_OK, vt->get_string(q, 0, &ts_s)); /* driver=="odbc" */
      ASSERT_EQ(C_ORM_OK, vt->reset(q));
      mock_api.SQLDescribeCol = NULL;
      mock_api.SQLDescribeColW = NULL;
      g_mock.total_rows = 1;
      ASSERT_EQ(C_ORM_OK, vt->step(q, &hr));
      mock_api.SQLDescribeCol = SQLMock_DescribeCol;
      mock_api.SQLDescribeColW = SQLMock_DescribeColW;
    }
    ASSERT_EQ(C_ORM_OK, vt->finalize(q));
  }

  /* Catalog list_tables & list_columns */
  {
    char **tbls = NULL;
    c_orm_odbc_column_info_t *cols = NULL;
    size_t cnt = 0;

    g_mock.zero_wide_len = 1; /* injects NULL, "", and SQL_ERROR in mode 3 */
    ASSERT_EQ(C_ORM_OK, c_orm_odbc_list_tables(db, NULL, &tbls, &cnt));
    ASSERT_EQ(7U, cnt);
    ASSERT_EQ(C_ORM_OK, c_orm_odbc_free_string_list(tbls, cnt));
    g_mock.zero_wide_len = 0;

    ASSERT_EQ(C_ORM_OK, c_orm_odbc_free_string_list(NULL, 0));
    ASSERT_EQ(C_ORM_OK, c_orm_odbc_free_column_list(NULL, 0));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_odbc_list_tables(db, NULL, NULL, &cnt));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_odbc_list_tables(db, NULL, &tbls, NULL));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_odbc_list_tables(sq_db, NULL, &tbls, &cnt));

    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_odbc_list_columns(NULL, NULL, "T", &cols, &cnt));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_odbc_list_columns(db, NULL, NULL, &cols, &cnt));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_odbc_list_columns(db, NULL, "T", NULL, &cnt));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_odbc_list_columns(db, NULL, "T", &cols, NULL));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_odbc_list_columns(db, NULL, "", &cols, &cnt));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_odbc_list_columns(sq_db, NULL, "T", &cols, &cnt));

    mock_api.SQLTables = NULL;
    mock_api.SQLColumns = NULL;
    ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED,
              c_orm_odbc_list_tables(db, NULL, &tbls, &cnt));
    ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED,
              c_orm_odbc_list_columns(db, NULL, "T", &cols, &cnt));
    mock_api.SQLTables = SQLMock_Tables;
    mock_api.SQLColumns = SQLMock_Columns;

    g_mock.fail_on_op = "AllocStmt";
    ASSERT_NEQ(C_ORM_OK, c_orm_odbc_list_tables(db, NULL, &tbls, &cnt));
    ASSERT_NEQ(C_ORM_OK, c_orm_odbc_list_columns(db, NULL, "T", &cols, &cnt));
    g_mock.fail_on_op = "Tables";
    ASSERT_NEQ(C_ORM_OK, c_orm_odbc_list_tables(db, NULL, &tbls, &cnt));
    g_mock.fail_on_op = "Columns";
    ASSERT_NEQ(C_ORM_OK, c_orm_odbc_list_columns(db, "S", "T", &cols, &cnt));
    g_mock.fail_on_op = "Fetch";
    ASSERT_NEQ(C_ORM_OK, c_orm_odbc_list_tables(db, NULL, &tbls, &cnt));
    ASSERT_NEQ(C_ORM_OK, c_orm_odbc_list_columns(db, NULL, "T", &cols, &cnt));
    g_mock.fail_on_op = NULL;

    ASSERT_EQ(C_ORM_OK, c_orm_odbc_list_tables(db, NULL, &tbls, &cnt));
    ASSERT_EQ(10U, cnt);
    ASSERT_EQ(C_ORM_OK, c_orm_odbc_free_string_list(tbls, cnt));

    ASSERT_EQ(C_ORM_OK, c_orm_odbc_list_columns(db, "SCH", "T", &cols, &cnt));
    ASSERT_EQ(10U, cnt); /* row 1 NULL cname skipped, rows 2..11 = 10 cols */
    ASSERT_STR_EQ("VARCHAR", cols[0].type_name);
    ASSERT_EQ(0, cols[0].is_nullable);
    ASSERT_EQ(C_ORM_OK, c_orm_odbc_free_column_list(cols, cnt));
  }

  /* Batch edge cases & error injection */
  {
    int32_t ids[2] = {1, 2};
    c_orm_decimal_t bad_decs[2];
    c_orm_type_t t_i32[1] = {C_ORM_TYPE_INT32};
    c_orm_type_t t_bad[1] = {C_ORM_TYPE_BLOB};
    c_orm_type_t t_dec[1] = {C_ORM_TYPE_DECIMAL};
    const void *arr_ok[1];
    const void *arr_null[1] = {NULL};
    const void *arr_dec[1];
    size_t proc = 0;

    arr_ok[0] = ids;
    memset(bad_decs, 0, sizeof(bad_decs));
    bad_decs[0].precision = 39;
    arr_dec[0] = bad_decs;

    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_odbc_execute_batch(NULL, "SQL", 2, 1, t_i32, arr_ok, NULL));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_odbc_execute_batch(db, NULL, 2, 1, t_i32, arr_ok, NULL));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_odbc_execute_batch(db, "SQL", 2, 1, NULL, arr_ok, NULL));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_odbc_execute_batch(db, "SQL", 2, 1, t_i32, NULL, NULL));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_odbc_execute_batch(db, "SQL", 0, 1, t_i32, arr_ok, NULL));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_odbc_execute_batch(db, "SQL", 2, 0, t_i32, arr_ok, NULL));
    ASSERT_EQ(
        C_ORM_ERROR_VALIDATION,
        c_orm_odbc_execute_batch(sq_db, "SQL", 2, 1, t_i32, arr_ok, &proc));

    mock_api.SQLSetStmtAttr = NULL;
    ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED,
              c_orm_odbc_execute_batch(db, "SQL", 2, 1, t_i32, arr_ok, &proc));
    mock_api.SQLSetStmtAttr = SQLMock_SetStmtAttr;

    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_odbc_execute_batch(db, "SQL", 2, 1, t_i32, arr_null, NULL));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_odbc_execute_batch(db, "SQL", 2, 1, t_bad, arr_ok, NULL));
    ASSERT_NEQ(C_ORM_OK,
               c_orm_odbc_execute_batch(db, "SQL", 2, 1, t_dec, arr_dec, NULL));

    g_mock.batch_zero_proc = 1;
    ASSERT_EQ(C_ORM_OK,
              c_orm_odbc_execute_batch(db, "SQL", 2, 1, t_i32, arr_ok, &proc));
    ASSERT_EQ(2U, proc);
    g_mock.batch_zero_proc = 0;

    {
      static const char *const bfails[5] = {
          "AllocStmt", "Prepare", "SetStmtAttr", "BindParameter", "Execute"};
      for (step_i = 0; step_i < 5; step_i++) {
        g_mock.fail_on_op = bfails[step_i];
        ASSERT_NEQ(C_ORM_OK, c_orm_odbc_execute_batch(db, "SQL", 2, 1, t_i32,
                                                      arr_ok, NULL));
      }
      g_mock.fail_on_op = NULL;
    }

#ifdef C_ORM_TEST_ALLOCATOR
    /* Exhaustive OOM sweeps across list_tables, list_columns, batch,
     * streaming, wide connect/prepare/bind, and SQLConnect */
    (void)c_orm_set_allocators(oom_malloc, oom_realloc, oom_free);
    {
      c_orm_query_t *aq = NULL;
      g_oom_countdown = -1;
      ASSERT_EQ(C_ORM_OK, vt->prepare(db, "SELECT 1", &aq));
      g_oom_countdown = 1; /* ensure_param_slot succeeds, c_orm_strdup OOMs */
      ASSERT_EQ(C_ORM_ERROR_MEMORY, vt->bind_string(aq, 1, "ansi_oom"));
      g_oom_countdown = -1;
      ASSERT_EQ(C_ORM_OK, vt->finalize(aq));
    }
    for (step_i = 0; step_i < 30; step_i++) {
      char **ot = NULL;
      size_t oc = 0;
      g_oom_countdown = step_i;
      if (c_orm_odbc_list_tables(db, "SCH", &ot, &oc) == C_ORM_OK) {
        g_oom_countdown = -1;
        (void)c_orm_odbc_free_string_list(ot, oc);
        break;
      }
    }
    for (step_i = 0; step_i < 40; step_i++) {
      c_orm_odbc_column_info_t *ocols = NULL;
      size_t oc = 0;
      g_oom_countdown = step_i;
      if (c_orm_odbc_list_columns(db, NULL, "T", &ocols, &oc) == C_ORM_OK) {
        g_oom_countdown = -1;
        (void)c_orm_odbc_free_column_list(ocols, oc);
        break;
      }
    }
    {
      int32_t b_i32[2] = {1, 2};
      int64_t b_i64[2] = {10, 20};
      double b_dbl[2] = {1.0, 2.0};
      const char *b_str[2] = {"a", "b"};
      c_orm_decimal_t b_dec[2];
      c_orm_type_t b_types[5] = {C_ORM_TYPE_INT32, C_ORM_TYPE_INT64,
                                 C_ORM_TYPE_DOUBLE, C_ORM_TYPE_STRING,
                                 C_ORM_TYPE_DECIMAL};
      const void *b_arrs[5];
      (void)c_orm_decimal_from_str("1.5", &b_dec[0]);
      (void)c_orm_decimal_from_str("2.5", &b_dec[1]);
      b_arrs[0] = b_i32;
      b_arrs[1] = b_i64;
      b_arrs[2] = b_dbl;
      b_arrs[3] = b_str;
      b_arrs[4] = b_dec;
      for (step_i = 0; step_i < 25; step_i++) {
        g_oom_countdown = step_i;
        if (c_orm_odbc_execute_batch(db, "INSERT INTO x VALUES (?,?,?,?,?)", 2,
                                     5, b_types, b_arrs, NULL) == C_ORM_OK) {
          break;
        }
      }
    }
    /* OOM on streaming mode 2 + mode 7 wide stream + wide connect/prepare */
    for (step_i = 0; step_i < 25; step_i++) {
      c_orm_db_t *wdb = NULL;
      g_oom_countdown = step_i;
      if (c_orm_odbc_connect_raw(&mock_api, "DSN=w", "odbc", NULL, 0, 1, 1,
                                 &wdb) == C_ORM_OK &&
          wdb) {
        c_orm_query_t *wq = NULL;
        if (vt->prepare(wdb, "SELECT 1", &wq) == C_ORM_OK && wq) {
          int hr = 0;
          const char *ws = NULL;
          const void *wb = NULL;
          size_t wbs = 0;
          (void)vt->bind_string(wq, 1, "wide");
          (void)c_orm_odbc_bind_wide_param(wq, 2, "wide2", C_ORM_SQL_WVARCHAR);
          (void)vt->bind_blob(wq, 3, "b", 1);
          (void)vt->bind_int64(wq, 12, 99);
          (void)vt->bind_int32(wq, 20, 5);
          g_mock.result_mode = 2;
          g_mock.total_rows = 1;
          if (vt->step(wq, &hr) == C_ORM_OK && hr) {
            (void)vt->get_string(wq, 0, &ws);
            (void)vt->get_blob(wq, 1, &wb, &wbs);
            (void)vt->get_string(wq, 2, &ws);
          }
          (void)vt->finalize(wq);
        }
        (void)vt->disconnect(wdb);
      }
    }
    for (step_i = 0; step_i < 15; step_i++) {
      c_orm_query_t *wq = NULL;
      g_oom_countdown = step_i;
      g_mock.result_mode = 7;
      g_mock.custom_col_type = C_ORM_SQL_WVARCHAR;
      g_mock.custom_str = "WIDE_STREAM_NO_TOTAL";
      g_mock.total_rows = 1;
      if (vt->prepare(db, "SELECT w", &wq) == C_ORM_OK && wq) {
        int hr = 0;
        const char *ws = NULL;
        if (vt->step(wq, &hr) == C_ORM_OK && hr) {
          (void)vt->get_string(wq, 0, &ws);
        }
        (void)vt->finalize(wq);
      }
    }
    for (step_i = 0; step_i < 6; step_i++) {
      c_orm_query_t *wq = NULL;
      g_oom_countdown = step_i;
      g_mock.result_mode = 7;
      g_mock.custom_col_type = C_ORM_SQL_WVARCHAR;
      g_mock.custom_str = "WIDE_BMP50";
      g_mock.total_rows = 1;
      if (vt->prepare(db, "SELECT w", &wq) == C_ORM_OK && wq) {
        int hr = 0;
        const char *ws = NULL;
        if (vt->step(wq, &hr) == C_ORM_OK && hr) {
          (void)vt->get_string(wq, 0, &ws);
        }
        (void)vt->finalize(wq);
      }
    }
    /* SQLConnect OOM */
    mock_api.SQLDriverConnect = NULL;
    mock_api.SQLDriverConnectW = NULL;
    for (step_i = 0; step_i < 5; step_i++) {
      c_orm_db_t *cdb = NULL;
      g_oom_countdown = step_i;
      if (c_orm_odbc_connect_raw(&mock_api, "MY_DSN", "odbc", NULL, 0, 1, 0,
                                 &cdb) == C_ORM_OK &&
          cdb) {
        (void)vt->disconnect(cdb);
      }
    }
    g_oom_countdown = -1;
    (void)c_orm_reset_allocators();
    (void)build_mock_odbc_api(&mock_api);
#endif
  }

  ASSERT_EQ(C_ORM_OK, vt->disconnect(db));
  ASSERT_EQ(C_ORM_OK, sq_db->vtable->disconnect(sq_db));
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_set_mock_api(NULL));
  PASS();
}

/**
 * @brief Test suite entry point for Generic ODBC driver & loader.
 * @param odbc_driver_suite Suite runner function name.
 */
SUITE(odbc_driver_suite) {
  RUN_TEST(test_odbc_loader_and_populate);
  RUN_TEST(test_odbc_loader_exhaustive);
  RUN_TEST(test_odbc_url_and_escaping);
  RUN_TEST(test_odbc_crud_types_and_streaming);
  RUN_TEST(test_odbc_wide_diag_tx_batch_and_oom);
  RUN_TEST(test_odbc_exhaustive_branches_and_oom);
}
