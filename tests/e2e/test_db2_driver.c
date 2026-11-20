/**
 * @file test_db2_driver.c
 * @brief Exhaustive unit, mock-CLI, WLM, LOB locator, DECFLOAT, GRAPHIC, z/OS
 * timestamp, and SQLCODE tests for `c_orm_db2.c`.
 */

/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_db2.h"
#include "c_orm_decimal.h"
#include "c_orm_ebcdic.h"
#include "c_orm_odbc_abi.h"
#include "c_orm_safe_crt.h"
#include "c_orm_sqlite.h"
#include "c_orm_string_builder.h"
#include "greatest.h"
#include <stdlib.h>
#include <string.h>
/* clang-format on */

/**
 * @brief Scripted state for the mock IBM Db2 Call Level Interface (CLI).
 */
typedef struct mock_db2_state {
  /** @brief Captured CLI connection string. */
  char conn_str[1024];
  /** @brief Captured WLM userid (`1281`). */
  char wlm_userid[128];
  /** @brief Captured WLM workstation (`1282`). */
  char wlm_wrkstn[128];
  /** @brief Captured WLM application name (`1283`). */
  char wlm_applname[128];
  /** @brief Captured WLM accounting string (`1284`). */
  char wlm_acctstr[256];
  /** @brief Captured `SQL_ATTR_CURRENT_SCHEMA` (`1254`). */
  char attr_schema[128];
  /** @brief Non-zero to reject `SQL_ATTR_CURRENT_SCHEMA` and test SQL SET. */
  int reject_schema_attr;
  /** @brief Captured last SQL statement prepared or executed directly. */
  char last_sql[512];
  /** @brief Captured isolation bitmask. */
  unsigned long isolation_mask;
  /** @brief Result mode: 0=DML, 1=IDENTITY_VAL_LOCAL, 2=IDENTITY NULL,
   * 3=Db2 extended types (DECFLOAT, GRAPHIC, FOR BIT DATA, z/OS TIMESTAMP),
   * 4=DECFLOAT Infinity/NaN. */
  int mode;
  /** @brief Current 1-based row cursor. */
  int cur_row;
  /** @brief Simulated Db2 SQLCODE for diagnostics. */
  c_orm_sqlinteger_t sqlcode;
  /** @brief Simulated SQLSTATE for diagnostics. */
  char sqlstate[6];
  /** @brief Force failure on specific CLI operation. */
  const char *fail_op;
  /** @brief Captured bound locator value in `FREE LOCATOR ?`. */
  c_orm_sqlinteger_t freed_locator;
  /** @brief Captured bound wide GRAPHIC text converted back to UTF-8. */
  char bound_graphic_utf8[128];
} mock_db2_state_t;

static mock_db2_state_t g_db2;
static int g_db2_env = 11;
static int g_db2_dbc = 22;
static int g_db2_stmt = 33;

/**
 * @brief Resets `g_db2` mock state.
 * @return C_ORM_OK on completion.
 */
static c_orm_error_t reset_db2_mock(void) {
  memset(&g_db2, 0, sizeof(g_db2));
  (void)C_ORM_STRCPY(g_db2.sqlstate, sizeof(g_db2.sqlstate), "HY000");
  g_db2.sqlcode = -1;
  return C_ORM_OK;
}

/**
 * @brief Mock Db2 SQLAllocHandle.
 * @param HandleType Handle type.
 * @param InputHandle Parent handle.
 * @param OutputHandlePtr Output handle pointer.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLDb2_AllocHandle(
    c_orm_sqlsmallint_t HandleType, c_orm_sqlhandle_t InputHandle,
    c_orm_sqlhandle_t *OutputHandlePtr) {
  (void)InputHandle;
  if (g_db2.fail_op && strcmp(g_db2.fail_op, "AllocStmt") == 0 &&
      HandleType == C_ORM_SQL_HANDLE_STMT) {
    return C_ORM_SQL_ERROR;
  }
  if (HandleType == C_ORM_SQL_HANDLE_ENV) {
    *OutputHandlePtr = &g_db2_env;
  } else if (HandleType == C_ORM_SQL_HANDLE_DBC) {
    *OutputHandlePtr = &g_db2_dbc;
  } else {
    *OutputHandlePtr = &g_db2_stmt;
  }
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock Db2 SQLFreeHandle.
 * @param HandleType Handle type.
 * @param Handle Target handle.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API
SQLDb2_FreeHandle(c_orm_sqlsmallint_t HandleType, c_orm_sqlhandle_t Handle) {
  (void)HandleType;
  (void)Handle;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock Db2 SQLSetEnvAttr.
 * @param EnvironmentHandle Environment handle.
 * @param Attribute Attribute code.
 * @param ValuePtr Attribute value pointer.
 * @param StringLength Value byte length.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLDb2_SetEnvAttr(
    c_orm_sqlhenv_t EnvironmentHandle, c_orm_sqlinteger_t Attribute,
    c_orm_sqlpointer_t ValuePtr, c_orm_sqlinteger_t StringLength) {
  (void)EnvironmentHandle;
  (void)Attribute;
  (void)ValuePtr;
  (void)StringLength;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock Db2 SQLSetConnectAttr capturing WLM and schema attributes.
 * @param ConnectionHandle Connection handle.
 * @param Attribute Attribute code.
 * @param ValuePtr Attribute value pointer.
 * @param StringLength Value byte length.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLDb2_SetConnectAttr(
    c_orm_sqlhdbc_t ConnectionHandle, c_orm_sqlinteger_t Attribute,
    c_orm_sqlpointer_t ValuePtr, c_orm_sqlinteger_t StringLength) {
  (void)ConnectionHandle;
  (void)StringLength;
  if (g_db2.fail_op && strcmp(g_db2.fail_op, "SetWlm") == 0 &&
      Attribute == C_ORM_SQL_ATTR_INFO_USERID) {
    return C_ORM_SQL_ERROR;
  }
  if (g_db2.fail_op && strcmp(g_db2.fail_op, "SetWlmWrk") == 0 &&
      Attribute == C_ORM_SQL_ATTR_INFO_WRKSTNNAME) {
    return C_ORM_SQL_ERROR;
  }
  if (g_db2.fail_op && strcmp(g_db2.fail_op, "SetWlmApp") == 0 &&
      Attribute == C_ORM_SQL_ATTR_INFO_APPLNAME) {
    return C_ORM_SQL_ERROR;
  }
  if (g_db2.fail_op && strcmp(g_db2.fail_op, "SetWlmAcc") == 0 &&
      Attribute == C_ORM_SQL_ATTR_INFO_ACCTSTR) {
    return C_ORM_SQL_ERROR;
  }
  if (g_db2.fail_op && strcmp(g_db2.fail_op, "SetIso") == 0 &&
      Attribute == C_ORM_SQL_ATTR_TXN_ISOLATION) {
    return C_ORM_SQL_ERROR;
  }
  if (Attribute == C_ORM_SQL_ATTR_INFO_USERID && ValuePtr) {
    (void)C_ORM_STRCPY(g_db2.wlm_userid, sizeof(g_db2.wlm_userid),
                       (const char *)ValuePtr);
  } else if (Attribute == C_ORM_SQL_ATTR_INFO_WRKSTNNAME && ValuePtr) {
    (void)C_ORM_STRCPY(g_db2.wlm_wrkstn, sizeof(g_db2.wlm_wrkstn),
                       (const char *)ValuePtr);
  } else if (Attribute == C_ORM_SQL_ATTR_INFO_APPLNAME && ValuePtr) {
    (void)C_ORM_STRCPY(g_db2.wlm_applname, sizeof(g_db2.wlm_applname),
                       (const char *)ValuePtr);
  } else if (Attribute == C_ORM_SQL_ATTR_INFO_ACCTSTR && ValuePtr) {
    (void)C_ORM_STRCPY(g_db2.wlm_acctstr, sizeof(g_db2.wlm_acctstr),
                       (const char *)ValuePtr);
  } else if (Attribute == C_ORM_SQL_ATTR_CURRENT_SCHEMA) {
    if (g_db2.reject_schema_attr) {
      return C_ORM_SQL_ERROR;
    }
    if (ValuePtr) {
      (void)C_ORM_STRCPY(g_db2.attr_schema, sizeof(g_db2.attr_schema),
                         (const char *)ValuePtr);
    }
  } else if (Attribute == C_ORM_SQL_ATTR_TXN_ISOLATION) {
    g_db2.isolation_mask = (unsigned long)(size_t)ValuePtr;
  }
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock Db2 SQLDriverConnect.
 * @param ConnectionHandle Connection handle.
 * @param WindowHandle Window handle.
 * @param InConnectionString Input CLI connection string.
 * @param StringLength1 Input length.
 * @param OutConnectionString Output buffer.
 * @param BufferLength Output buffer capacity.
 * @param StringLength2Ptr Output written length.
 * @param DriverCompletion Prompt mode.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLDb2_DriverConnect(
    c_orm_sqlhdbc_t ConnectionHandle, c_orm_sqlhwnd_t WindowHandle,
    c_orm_sqlchar_t *InConnectionString, c_orm_sqlsmallint_t StringLength1,
    c_orm_sqlchar_t *OutConnectionString, c_orm_sqlsmallint_t BufferLength,
    c_orm_sqlsmallint_t *StringLength2Ptr,
    c_orm_sqlusmallint_t DriverCompletion) {
  (void)ConnectionHandle;
  (void)WindowHandle;
  (void)StringLength1;
  (void)OutConnectionString;
  (void)BufferLength;
  (void)StringLength2Ptr;
  (void)DriverCompletion;
  if (g_db2.fail_op && strcmp(g_db2.fail_op, "DriverConnect") == 0) {
    return C_ORM_SQL_ERROR;
  }
  if (InConnectionString) {
    (void)C_ORM_STRCPY(g_db2.conn_str, sizeof(g_db2.conn_str),
                       (const char *)InConnectionString);
  }
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock Db2 SQLDisconnect.
 * @param ConnectionHandle Connection handle.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API
SQLDb2_Disconnect(c_orm_sqlhdbc_t ConnectionHandle) {
  (void)ConnectionHandle;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock Db2 SQLEndTran.
 * @param HandleType Handle type.
 * @param Handle Target handle.
 * @param CompletionType Commit or rollback action.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API
SQLDb2_EndTran(c_orm_sqlsmallint_t HandleType, c_orm_sqlhandle_t Handle,
               c_orm_sqlsmallint_t CompletionType) {
  (void)HandleType;
  (void)Handle;
  (void)CompletionType;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock Db2 SQLPrepare.
 * @param StatementHandle Statement handle.
 * @param StatementText SQL text.
 * @param TextLength SQL byte length.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API
SQLDb2_Prepare(c_orm_sqlhstmt_t StatementHandle, c_orm_sqlchar_t *StatementText,
               c_orm_sqlinteger_t TextLength) {
  (void)StatementHandle;
  (void)TextLength;
  if (g_db2.fail_op && strcmp(g_db2.fail_op, "Prepare") == 0) {
    return C_ORM_SQL_ERROR;
  }
  if (StatementText) {
    (void)C_ORM_STRCPY(g_db2.last_sql, sizeof(g_db2.last_sql),
                       (const char *)StatementText);
    if (strstr(g_db2.last_sql, "IDENTITY_VAL_LOCAL") != NULL &&
        g_db2.mode != 2 && g_db2.mode < 5) {
      g_db2.mode = 1;
    }
  }
  g_db2.cur_row = 0;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock Db2 SQLExecDirect.
 * @param StatementHandle Statement handle.
 * @param StatementText SQL text.
 * @param TextLength SQL byte length.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLDb2_ExecDirect(
    c_orm_sqlhstmt_t StatementHandle, c_orm_sqlchar_t *StatementText,
    c_orm_sqlinteger_t TextLength) {
  (void)StatementHandle;
  (void)TextLength;
  if (g_db2.fail_op && strcmp(g_db2.fail_op, "FreeLocator") == 0) {
    return C_ORM_SQL_ERROR;
  }
  if (StatementText) {
    (void)C_ORM_STRCPY(g_db2.last_sql, sizeof(g_db2.last_sql),
                       (const char *)StatementText);
  }
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock Db2 SQLExecute.
 * @param StatementHandle Statement handle.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API
SQLDb2_Execute(c_orm_sqlhstmt_t StatementHandle) {
  (void)StatementHandle;
  if (g_db2.fail_op && strcmp(g_db2.fail_op, "Execute") == 0) {
    return C_ORM_SQL_ERROR;
  }
  g_db2.cur_row = 0;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock Db2 SQLBindParameter.
 * @param StatementHandle Statement handle.
 * @param ParameterNumber 1-based parameter index.
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
static c_orm_sqlreturn_t C_ORM_SQL_API SQLDb2_BindParameter(
    c_orm_sqlhstmt_t StatementHandle, c_orm_sqlusmallint_t ParameterNumber,
    c_orm_sqlsmallint_t InputOutputType, c_orm_sqlsmallint_t ValueType,
    c_orm_sqlsmallint_t ParameterType, c_orm_sqlulen_t ColumnSize,
    c_orm_sqlsmallint_t DecimalDigits, c_orm_sqlpointer_t ParameterValuePtr,
    c_orm_sqllen_t BufferLength, c_orm_sqllen_t *StrLen_or_IndPtr) {
  (void)StatementHandle;
  (void)ParameterNumber;
  (void)InputOutputType;
  (void)ColumnSize;
  (void)DecimalDigits;
  (void)BufferLength;
  if (g_db2.fail_op && strcmp(g_db2.fail_op, "Bind") == 0) {
    return C_ORM_SQL_ERROR;
  }
  if ((ValueType == C_ORM_SQL_C_BLOB_LOCATOR ||
       ValueType == C_ORM_SQL_C_CLOB_LOCATOR ||
       ValueType == C_ORM_SQL_C_DBCLOB_LOCATOR) &&
      ParameterValuePtr) {
    g_db2.freed_locator = *(c_orm_sqlinteger_t *)ParameterValuePtr;
  } else if (ValueType == C_ORM_SQL_C_WCHAR &&
             ParameterType == C_ORM_SQL_VARGRAPHIC && ParameterValuePtr &&
             StrLen_or_IndPtr && *StrLen_or_IndPtr > 0) {
    size_t units = (size_t)(*StrLen_or_IndPtr) / sizeof(c_orm_sqlwchar_t);
    (void)c_orm_utf16_to_utf8((const c_orm_sqlwchar_t *)ParameterValuePtr,
                              units, 0, g_db2.bound_graphic_utf8,
                              sizeof(g_db2.bound_graphic_utf8), NULL);
  }
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock Db2 SQLNumResultCols.
 * @param StatementHandle Statement handle.
 * @param ColumnCountPtr Output column count.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLDb2_NumResultCols(
    c_orm_sqlhstmt_t StatementHandle, c_orm_sqlsmallint_t *ColumnCountPtr) {
  (void)StatementHandle;
  if (g_db2.mode == 1 || g_db2.mode == 2 || g_db2.mode == 4 ||
      g_db2.mode >= 5) {
    *ColumnCountPtr = 1;
  } else if (g_db2.mode == 3) {
    *ColumnCountPtr = 4;
  } else {
    *ColumnCountPtr = 0;
  }
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock Db2 SQLDescribeCol.
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
static c_orm_sqlreturn_t C_ORM_SQL_API SQLDb2_DescribeCol(
    c_orm_sqlhstmt_t StatementHandle, c_orm_sqlusmallint_t ColumnNumber,
    c_orm_sqlchar_t *ColumnName, c_orm_sqlsmallint_t BufferLength,
    c_orm_sqlsmallint_t *NameLengthPtr, c_orm_sqlsmallint_t *DataTypePtr,
    c_orm_sqlulen_t *ColumnSizePtr, c_orm_sqlsmallint_t *DecimalDigitsPtr,
    c_orm_sqlsmallint_t *NullablePtr) {
  static const char *const names3[4] = {"decflt", "graphic_col", "bit_data",
                                        "zos_ts"};
  static const c_orm_sqlsmallint_t types3[4] = {
      C_ORM_SQL_DECFLOAT, C_ORM_SQL_GRAPHIC, C_ORM_SQL_BINARY,
      C_ORM_SQL_TYPE_TIMESTAMP};

  (void)StatementHandle;
  *ColumnSizePtr = 34;
  *DecimalDigitsPtr = 0;
  *NullablePtr = 1;

  if (g_db2.mode == 3 && ColumnNumber >= 1 && ColumnNumber <= 4) {
    (void)C_ORM_STRCPY((char *)ColumnName, (size_t)BufferLength,
                       names3[ColumnNumber - 1]);
    *NameLengthPtr = (c_orm_sqlsmallint_t)strlen((const char *)ColumnName);
    *DataTypePtr = types3[ColumnNumber - 1];
  } else if (g_db2.mode == 4) {
    (void)C_ORM_STRCPY((char *)ColumnName, (size_t)BufferLength, "df_special");
    *NameLengthPtr = 10;
    *DataTypePtr = C_ORM_SQL_DECFLOAT;
  } else {
    (void)C_ORM_STRCPY((char *)ColumnName, (size_t)BufferLength, "id_val");
    *NameLengthPtr = 6;
    *DataTypePtr = C_ORM_SQL_DECIMAL;
  }
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock Db2 SQLFetch.
 * @param StatementHandle Statement handle.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API
SQLDb2_Fetch(c_orm_sqlhstmt_t StatementHandle) {
  int max_rows = (g_db2.mode == 4) ? 3 : ((g_db2.mode == 5) ? 0 : 1);
  (void)StatementHandle;
  if (g_db2.cur_row >= max_rows) {
    return C_ORM_SQL_NO_DATA;
  }
  g_db2.cur_row++;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock Db2 SQLGetData.
 * @param StatementHandle Statement handle.
 * @param Col_or_Param_Num 1-based column index.
 * @param TargetType Requested C target type.
 * @param TargetValuePtr Destination buffer.
 * @param BufferLength Buffer capacity in bytes.
 * @param StrLen_or_IndPtr Output byte length or NULL indicator.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLDb2_GetData(
    c_orm_sqlhstmt_t StatementHandle, c_orm_sqlusmallint_t Col_or_Param_Num,
    c_orm_sqlsmallint_t TargetType, c_orm_sqlpointer_t TargetValuePtr,
    c_orm_sqllen_t BufferLength, c_orm_sqllen_t *StrLen_or_IndPtr) {
  (void)StatementHandle;
  if (g_db2.mode == 6) {
    if (TargetType == C_ORM_SQL_C_NUMERIC) {
      return C_ORM_SQL_ERROR;
    }
    (void)C_ORM_STRCPY((char *)TargetValuePtr, (size_t)BufferLength,
                       "9999999999999999999999999999999");
    *StrLen_or_IndPtr = 31;
    return C_ORM_SQL_SUCCESS;
  }
  if (g_db2.mode == 7) {
    if (TargetType == C_ORM_SQL_C_NUMERIC) {
      return C_ORM_SQL_ERROR;
    }
    (void)C_ORM_STRCPY((char *)TargetValuePtr, (size_t)BufferLength,
                       "NOT_A_DECIMAL");
    *StrLen_or_IndPtr = 13;
    return C_ORM_SQL_SUCCESS;
  }
  if (g_db2.mode == 8) {
    return C_ORM_SQL_ERROR;
  }
  if (g_db2.mode == 1) {
    if (TargetType == C_ORM_SQL_C_NUMERIC) {
      return C_ORM_SQL_ERROR;
    }
    (void)C_ORM_STRCPY((char *)TargetValuePtr, (size_t)BufferLength,
                       "987654321012");
    *StrLen_or_IndPtr = 12;
    return C_ORM_SQL_SUCCESS;
  }
  if (g_db2.mode == 2) {
    *StrLen_or_IndPtr = C_ORM_SQL_NULL_DATA;
    return C_ORM_SQL_SUCCESS;
  }
  if (g_db2.mode == 3) {
    if (Col_or_Param_Num == 1) {
      /* DECFLOAT(34) scientific string */
      (void)C_ORM_STRCPY((char *)TargetValuePtr, (size_t)BufferLength,
                         "1.25E+4");
      *StrLen_or_IndPtr = 7;
      return C_ORM_SQL_SUCCESS;
    }
    if (Col_or_Param_Num == 2) {
      /* GRAPHIC UTF-16 column ("IBM_DB2_漢字") */
      size_t units = 0;
      (void)c_orm_utf8_to_utf16("IBM_DB2_\xE6\xBC\xA2\xE5\xAD\x97", 14, 0,
                                (c_orm_sqlwchar_t *)TargetValuePtr,
                                (size_t)BufferLength / 2U, &units);
      *StrLen_or_IndPtr = (c_orm_sqllen_t)(units * sizeof(c_orm_sqlwchar_t));
      return C_ORM_SQL_SUCCESS;
    }
    if (Col_or_Param_Num == 3) {
      /* CHAR(4) FOR BIT DATA with embedded NUL */
      static const unsigned char raw_bit[4] = {0xC1, 0x00, 0xF0, 0x40};
      memcpy(TargetValuePtr, raw_bit, 4);
      *StrLen_or_IndPtr = 4;
      return C_ORM_SQL_SUCCESS;
    }
    if (Col_or_Param_Num == 4) {
      /* Db2 z/OS dash-dot timestamp */
      (void)C_ORM_STRCPY((char *)TargetValuePtr, (size_t)BufferLength,
                         "2026-11-17-14.35.59.123456");
      *StrLen_or_IndPtr = 26;
      return C_ORM_SQL_SUCCESS;
    }
  }
  if (g_db2.mode == 4) {
    const char *sp = (g_db2.cur_row == 1)
                         ? "Infinity"
                         : ((g_db2.cur_row == 2) ? "-Infinity" : "sNaN");
    (void)C_ORM_STRCPY((char *)TargetValuePtr, (size_t)BufferLength, sp);
    *StrLen_or_IndPtr = (c_orm_sqllen_t)strlen(sp);
    return C_ORM_SQL_SUCCESS;
  }
  return C_ORM_SQL_NO_DATA;
}

/**
 * @brief Mock Db2 SQLFreeStmt.
 * @param StatementHandle Statement handle.
 * @param Option Reset/close option.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API
SQLDb2_FreeStmt(c_orm_sqlhstmt_t StatementHandle, c_orm_sqlusmallint_t Option) {
  (void)StatementHandle;
  (void)Option;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock Db2 SQLGetDiagRec.
 * @param HandleType Handle type.
 * @param Handle Target handle.
 * @param RecNumber 1-based record index.
 * @param SQLState Output 5-char SQLSTATE buffer.
 * @param NativeErrorPtr Output Db2 SQLCODE.
 * @param MessageText Output message buffer.
 * @param BufferLength Buffer capacity.
 * @param TextLengthPtr Output message length.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLDb2_GetDiagRec(
    c_orm_sqlsmallint_t HandleType, c_orm_sqlhandle_t Handle,
    c_orm_sqlsmallint_t RecNumber, c_orm_sqlchar_t *SQLState,
    c_orm_sqlinteger_t *NativeErrorPtr, c_orm_sqlchar_t *MessageText,
    c_orm_sqlsmallint_t BufferLength, c_orm_sqlsmallint_t *TextLengthPtr) {
  (void)HandleType;
  (void)Handle;
  if (RecNumber == 1) {
    (void)C_ORM_STRCPY((char *)SQLState, 6, g_db2.sqlstate);
    *NativeErrorPtr = g_db2.sqlcode;
    (void)C_ORM_SPRINTF(
        (char *)MessageText, (size_t)BufferLength,
        "[IBM][CLI Driver][DB2/LINUXX8664] SQL%04ldN",
        (long)(g_db2.sqlcode < 0 ? -g_db2.sqlcode : g_db2.sqlcode));
    *TextLengthPtr = (c_orm_sqlsmallint_t)strlen((const char *)MessageText);
    return C_ORM_SQL_SUCCESS;
  }
  return C_ORM_SQL_NO_DATA;
}

/**
 * @brief Mock Db2 SQLGetLength for LOB locators.
 * @param StatementHandle Statement handle.
 * @param LocatorCType Locator C type.
 * @param Locator Locator token.
 * @param StringLength Output total LOB length.
 * @param IndicatorValue Output NULL indicator.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLDb2_GetLength(
    c_orm_sqlhstmt_t StatementHandle, c_orm_sqlsmallint_t LocatorCType,
    c_orm_sqlinteger_t Locator, c_orm_sqlinteger_t *StringLength,
    c_orm_sqlinteger_t *IndicatorValue) {
  (void)StatementHandle;
  (void)LocatorCType;
  if (g_db2.fail_op && strcmp(g_db2.fail_op, "GetLength") == 0) {
    return C_ORM_SQL_ERROR;
  }
  if (Locator == 999) {
    *StringLength = 0;
    *IndicatorValue = C_ORM_SQL_NULL_DATA;
  } else if (Locator == 998) {
    *StringLength = -5;
    *IndicatorValue = 0;
  } else {
    *StringLength = 1048576;
    *IndicatorValue = 0;
  }
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Mock Db2 SQLGetSubString for LOB locators.
 * @param StatementHandle Statement handle.
 * @param LocatorCType Locator C type.
 * @param SourceLocator Source locator token.
 * @param FromPosition 1-based start offset.
 * @param ForLength Requested unit count.
 * @param TargetCType Target C type.
 * @param DataPtr Destination buffer.
 * @param BufferLength Destination capacity.
 * @param StringLength Output bytes/units written.
 * @param IndicatorValue Output NULL indicator.
 * @return SQLRETURN status.
 */
static c_orm_sqlreturn_t C_ORM_SQL_API SQLDb2_GetSubString(
    c_orm_sqlhstmt_t StatementHandle, c_orm_sqlsmallint_t LocatorCType,
    c_orm_sqlinteger_t SourceLocator, c_orm_sqluinteger_t FromPosition,
    c_orm_sqluinteger_t ForLength, c_orm_sqlsmallint_t TargetCType,
    c_orm_sqlpointer_t DataPtr, c_orm_sqlinteger_t BufferLength,
    c_orm_sqlinteger_t *StringLength, c_orm_sqlinteger_t *IndicatorValue) {
  static const char *const payload = "DB2_ZOS_LOB_SLICE_PAYLOAD";
  size_t plen = strlen(payload);
  size_t start0 = (FromPosition > 0) ? (size_t)(FromPosition - 1U) : 0U;
  size_t copy_n = 0;

  (void)StatementHandle;
  (void)LocatorCType;
  (void)BufferLength;
  if (g_db2.fail_op && strcmp(g_db2.fail_op, "GetSubString") == 0) {
    return C_ORM_SQL_ERROR;
  }
  if (SourceLocator == 999) {
    *StringLength = 0;
    *IndicatorValue = C_ORM_SQL_NULL_DATA;
    return C_ORM_SQL_SUCCESS;
  }
  if (start0 < plen) {
    copy_n = plen - start0;
    if (copy_n > (size_t)ForLength) {
      copy_n = (size_t)ForLength;
    }
    memcpy(DataPtr, payload + start0, copy_n);
    if (TargetCType == C_ORM_SQL_C_CHAR) {
      ((char *)DataPtr)[copy_n] = '\0';
    }
  }
  *StringLength = (c_orm_sqlinteger_t)copy_n;
  *IndicatorValue = 0;
  return C_ORM_SQL_SUCCESS;
}

/**
 * @brief Populates mock IBM Db2 CLI dispatch table.
 * @param out_api Output dispatch table.
 * @return C_ORM_OK on completion.
 */
static c_orm_error_t build_mock_db2_api(c_orm_odbc_api_t *out_api) {
  memset(out_api, 0, sizeof(*out_api));
  out_api->SQLAllocHandle = SQLDb2_AllocHandle;
  out_api->SQLFreeHandle = SQLDb2_FreeHandle;
  out_api->SQLSetEnvAttr = SQLDb2_SetEnvAttr;
  out_api->SQLSetConnectAttr = SQLDb2_SetConnectAttr;
  out_api->SQLDriverConnect = SQLDb2_DriverConnect;
  out_api->SQLDisconnect = SQLDb2_Disconnect;
  out_api->SQLEndTran = SQLDb2_EndTran;
  out_api->SQLPrepare = SQLDb2_Prepare;
  out_api->SQLExecDirect = SQLDb2_ExecDirect;
  out_api->SQLExecute = SQLDb2_Execute;
  out_api->SQLBindParameter = SQLDb2_BindParameter;
  out_api->SQLNumResultCols = SQLDb2_NumResultCols;
  out_api->SQLDescribeCol = SQLDb2_DescribeCol;
  out_api->SQLFetch = SQLDb2_Fetch;
  out_api->SQLGetData = SQLDb2_GetData;
  out_api->SQLFreeStmt = SQLDb2_FreeStmt;
  out_api->SQLGetDiagRec = SQLDb2_GetDiagRec;
  out_api->SQLGetLength = SQLDb2_GetLength;
  out_api->SQLGetSubString = SQLDb2_GetSubString;
  return C_ORM_OK;
}

/**
 * @brief Sets an environment variable portably without `<windows.h>`.
 * @param key Variable name.
 * @param val Variable value (or `""` to clear).
 * @return C_ORM_OK on completion.
 */
static c_orm_error_t set_db2_env(const char *key, const char *val) {
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

/** @brief Verifies `c_orm_db2_load_cli`, `c_orm_db2_build_conn_string`, and
 * z/OS timestamp normalization. */
TEST test_db2_loader_urls_and_timestamps(void) {
  c_orm_odbc_api_t api;
  const c_orm_odbc_api_t *loaded = NULL;
  int is_direct = -1;
  char conn[1024];
  char ts[64];
  c_orm_db2_url_opts_t opts;

  /* Loader branches: NULL, C_ORM_DB2_LIB error, IBM_DB_HOME fallback to ODBC */
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_db2_load_cli(NULL, &is_direct));
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_reset_loader());
  (void)set_db2_env("C_ORM_DB2_LIB", "/nonexistent_libdb2_999.so");
  ASSERT_EQ(C_ORM_ERROR_DRIVER_LOAD, c_orm_db2_load_cli(&loaded, &is_direct));
  (void)set_db2_env("C_ORM_DB2_LIB", "");

  (void)build_mock_db2_api(&api);
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_set_mock_api(&api));
  (void)set_db2_env("IBM_DB_HOME", "/nonexistent_ibm_sqllib");
  ASSERT_EQ(C_ORM_OK, c_orm_db2_load_cli(&loaded, &is_direct));
  ASSERT_EQ(0, is_direct); /* Fell back to ODBC Driver Manager mock! */
  (void)set_db2_env("IBM_DB_HOME", "");

  ASSERT_EQ(C_ORM_OK, c_orm_db2_set_mock_api(&api, 1));
  ASSERT_EQ(C_ORM_OK, c_orm_db2_load_cli(&loaded, &is_direct));
  ASSERT_EQ(1, is_direct);
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_reset_loader());

  /* TCP/IP URL parsing (direct CLI vs ODBC Driver Manager) */
  ASSERT_EQ(
      C_ORM_OK,
      c_orm_db2_build_conn_string(
          "db2://db2inst1:p%3Bss@db2.corp.local:50000/SAMPLE?schema=PROD"
          "&isolation=RS&ssl=true&userid=u42&wrkstn=batch01&applname=ledger"
          "&acctstr=DEPT99&timeout=30&autocommit=off&ConnectTimeout=5",
          1, conn, sizeof(conn), &opts));
  ASSERT_STR_EQ("DATABASE=SAMPLE;HOSTNAME=db2.corp.local;PORT=50000;"
                "PROTOCOL=TCPIP;UID=db2inst1;PWD={p;ss};Security=SSL;"
                "CurrentSchema=PROD;ConnectTimeout=5;",
                conn);
  ASSERT_STR_EQ("PROD", opts.schema);
  ASSERT_EQ(1, opts.has_isolation);
  ASSERT_EQ(C_ORM_DB2_ISOLATION_RS, opts.isolation);
  ASSERT_STR_EQ("u42", opts.info_userid);
  ASSERT_STR_EQ("batch01", opts.info_wrkstn);
  ASSERT_STR_EQ("ledger", opts.info_applname);
  ASSERT_STR_EQ("DEPT99", opts.info_acctstr);
  ASSERT_EQ(30, opts.login_timeout);
  ASSERT_EQ(0, opts.autocommit);

  /* Driver Manager mode injects DRIVER={{IBM DB2 ODBC DRIVER}} */
  ASSERT_EQ(C_ORM_OK,
            c_orm_db2_build_conn_string("db2://db2.local/SAMPLE?isolation=UR",
                                        0, conn, sizeof(conn), &opts));
  ASSERT(strstr(conn, "DRIVER=") != NULL);
  ASSERT(strstr(conn, "PORT=50000;") != NULL);
  ASSERT_EQ(C_ORM_DB2_ISOLATION_UR, opts.isolation);

  /* Cataloged z/OS DSN subsystem alias */
  ASSERT_EQ(C_ORM_OK,
            c_orm_db2_build_conn_string(
                "db2://DB2Z_SUBSYS?uid=zosusr&pwd=zospwd&isolation=RR", 1, conn,
                sizeof(conn), &opts));
  ASSERT_STR_EQ("DSN=DB2Z_SUBSYS;UID=zosusr;PWD=zospwd;", conn);
  ASSERT_EQ(C_ORM_DB2_ISOLATION_RR, opts.isolation);

  /* Raw DATABASE= string and error cases */
  ASSERT_EQ(C_ORM_OK,
            c_orm_db2_build_conn_string("DATABASE=SAMPLE;HOSTNAME=localhost;",
                                        1, conn, sizeof(conn), &opts));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_db2_build_conn_string(NULL, 1, conn, sizeof(conn), NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_build_conn_string("db2://", 1, conn, sizeof(conn), NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_build_conn_string("db2://host:50000", 1, conn,
                                        sizeof(conn), NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_build_conn_string("db2://host/DB?isolation=BAD", 1, conn,
                                        sizeof(conn), NULL));

  /* z/OS & LUW timestamp normalization */
  ASSERT_EQ(C_ORM_OK, c_orm_db2_normalize_timestamp(
                          "2026-11-17-14.35.59.123456789012", ts, sizeof(ts)));
  ASSERT_STR_EQ("2026-11-17 14:35:59.123456789012", ts);
  ASSERT_EQ(C_ORM_OK, c_orm_db2_normalize_timestamp(" 2026-11-17-08.05.01 ", ts,
                                                    sizeof(ts)));
  ASSERT_STR_EQ("2026-11-17 08:05:01", ts);
  ASSERT_EQ(C_ORM_OK, c_orm_db2_normalize_timestamp("2026-11-17T08:05:01.500",
                                                    ts, sizeof(ts)));
  ASSERT_STR_EQ("2026-11-17 08:05:01.500", ts);
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_db2_normalize_timestamp(NULL, ts, sizeof(ts)));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_db2_normalize_timestamp("2026-11-17-08.05.01", ts, 10));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_normalize_timestamp("short", ts, sizeof(ts)));
  ASSERT_EQ(
      C_ORM_ERROR_VALIDATION,
      c_orm_db2_normalize_timestamp("2026-11-17-08.05.01.", ts, sizeof(ts)));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_db2_normalize_timestamp(
                                        "2026/11/17-08.05.01", ts, sizeof(ts)));
  PASS();
}

/** @brief Verifies Db2 connect, WLM registers, CURRENT SCHEMA, savepoints,
 * `IDENTITY_VAL_LOCAL()`, LOB locators, DECFLOAT/GRAPHIC, and SQLCODEs. */
TEST test_db2_session_lobs_types_and_sqlcodes(void) {
  c_orm_odbc_api_t api;
  c_orm_db_t *db = NULL;
  c_orm_query_t *q = NULL;
  int64_t last_id = 0;
  int has_row = 0;
  c_orm_decimal_t dec;
  char dec_str[48];
  const char *sval = NULL;
  const void *bval = NULL;
  size_t bsize = 0;
  c_orm_int64_t lob_len = 0;
  char lob_buf[32];
  size_t lob_read = 0;

  (void)reset_db2_mock();
  (void)build_mock_db2_api(&api);
  ASSERT_EQ(C_ORM_OK, c_orm_db2_set_mock_api(&api, 1));

  ASSERT_EQ(C_ORM_OK,
            c_orm_connect_url(
                "db2://inst1:pw@zos.corp:446/PRODDB?schema=FINANCE&isolation=CS"
                "&userid=CICSUSR&wrkstn=TERM01&applname=PAYROLL&acctstr=ACC77",
                &db));
  ASSERT(db != NULL);
  ASSERT_STR_EQ("db2", db->driver_name);
  ASSERT_STR_EQ("CICSUSR", g_db2.wlm_userid);
  ASSERT_STR_EQ("TERM01", g_db2.wlm_wrkstn);
  ASSERT_STR_EQ("PAYROLL", g_db2.wlm_applname);
  ASSERT_STR_EQ("ACC77", g_db2.wlm_acctstr);
  ASSERT_STR_EQ("FINANCE", g_db2.attr_schema);
  ASSERT_EQ(C_ORM_SQL_TXN_READ_COMMITTED, g_db2.isolation_mask);

  /* Fallback SET CURRENT SCHEMA = "..." when attribute 1254 is rejected */
  g_db2.reject_schema_attr = 1;
  ASSERT_EQ(C_ORM_OK, c_orm_db2_set_current_schema(db, "ARCHIVE"));
  ASSERT_STR_EQ("SET CURRENT SCHEMA = \x22"
                "ARCHIVE\x22",
                g_db2.last_sql);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_db2_set_current_schema(db, ""));

  /* Isolation levels */
  ASSERT_EQ(C_ORM_OK, c_orm_db2_set_isolation(db, C_ORM_DB2_ISOLATION_UR));
  ASSERT_EQ(C_ORM_SQL_TXN_READ_UNCOMMITTED, g_db2.isolation_mask);
  ASSERT_EQ(C_ORM_OK, c_orm_db2_set_isolation(db, C_ORM_DB2_ISOLATION_RR));
  ASSERT_EQ(C_ORM_SQL_TXN_SERIALIZABLE, g_db2.isolation_mask);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_set_isolation(db, (c_orm_db2_isolation_t)99));

  /* Db2 savepoints (both direct and via c_orm_savepoint_create) */
  ASSERT_EQ(C_ORM_OK, c_orm_savepoint_create(db, "sp_alpha"));
  ASSERT_STR_EQ("SAVEPOINT \x22sp_alpha\x22 ON ROLLBACK RETAIN CURSORS",
                g_db2.last_sql);
  ASSERT_EQ(C_ORM_OK, c_orm_db2_savepoint_rollback(db, "sp_alpha"));
  ASSERT_STR_EQ("ROLLBACK TO SAVEPOINT \x22sp_alpha\x22", g_db2.last_sql);
  ASSERT_EQ(C_ORM_OK, c_orm_db2_savepoint_release(db, "sp_alpha"));
  ASSERT_STR_EQ("RELEASE SAVEPOINT \x22sp_alpha\x22", g_db2.last_sql);

  /* IDENTITY_VAL_LOCAL() DECIMAL(31,0) -> int64_t and NULL -> 0 */
  g_db2.mode = 1;
  ASSERT_EQ(C_ORM_OK, db->vtable->get_last_insert_rowid(db, &last_id));
  ASSERT_EQ(((int64_t)987654L * (int64_t)1000000L + (int64_t)321012L), last_id);

  g_db2.mode = 2;
  ASSERT_EQ(C_ORM_OK, db->vtable->get_last_insert_rowid(db, &last_id));
  ASSERT_EQ(0, (int)last_id);

  /* Db2 extended wire types: DECFLOAT, GRAPHIC, FOR BIT DATA, z/OS TIMESTAMP */
  g_db2.mode = 3;
  ASSERT_EQ(C_ORM_OK,
            db->vtable->prepare(db, "SELECT * FROM ZOS.LEDGER WHERE G=?", &q));
  ASSERT_EQ(C_ORM_OK,
            c_orm_db2_bind_graphic(q, 1, "DBCS_\xE6\xBC\xA2\xE5\xAD\x97"));
  ASSERT_STR_EQ("DBCS_\xE6\xBC\xA2\xE5\xAD\x97", g_db2.bound_graphic_utf8);
  ASSERT_EQ(C_ORM_OK, db->vtable->step(q, &has_row));
  ASSERT_EQ(1, has_row);

  ASSERT_EQ(C_ORM_OK, db->vtable->get_decimal(q, 0, &dec));
  ASSERT_EQ(C_ORM_OK,
            c_orm_decimal_to_str(&dec, dec_str, sizeof(dec_str), NULL));
  ASSERT_STR_EQ("12500", dec_str);

  ASSERT_EQ(C_ORM_OK, db->vtable->get_string(q, 1, &sval));
  ASSERT_STR_EQ("IBM_DB2_\xE6\xBC\xA2\xE5\xAD\x97", sval);

  ASSERT_EQ(C_ORM_OK, db->vtable->get_blob(q, 2, &bval, &bsize));
  ASSERT_EQ(4U, bsize);
  ASSERT_EQ(0x00, ((const unsigned char *)bval)[1]);
  ASSERT_EQ(0x40, ((const unsigned char *)bval)[3]);

  ASSERT_EQ(C_ORM_OK, db->vtable->get_string(q, 3, &sval));
  ASSERT_STR_EQ("2026-11-17 14:35:59.123456", sval);
  ASSERT_EQ(C_ORM_OK, db->vtable->finalize(q));

  /* DECFLOAT special values Infinity / -Infinity / sNaN rejected on decimal */
  g_db2.mode = 4;
  ASSERT_EQ(C_ORM_OK, db->vtable->prepare(db, "SELECT df FROM T", &q));
  ASSERT_EQ(C_ORM_OK, db->vtable->step(q, &has_row));
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW, db->vtable->get_decimal(q, 0, &dec));
  ASSERT_EQ(C_ORM_OK, db->vtable->step(q, &has_row));
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW, db->vtable->get_decimal(q, 0, &dec));
  ASSERT_EQ(C_ORM_OK, db->vtable->step(q, &has_row));
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_SYNTAX, db->vtable->get_decimal(q, 0, &dec));
  ASSERT_EQ(C_ORM_OK, db->vtable->finalize(q));

  /* LOB Locator streaming (SQLGetLength, SQLGetSubString, FREE LOCATOR) */
  ASSERT_EQ(C_ORM_OK, c_orm_db2_lob_get_length(db, C_ORM_SQL_C_BLOB_LOCATOR,
                                               77U, &lob_len));
  ASSERT_EQ(1048576, (int)lob_len);
  ASSERT_EQ(C_ORM_OK, c_orm_db2_lob_get_length(db, C_ORM_SQL_C_CLOB_LOCATOR,
                                               999U, &lob_len));
  ASSERT_EQ(0, (int)lob_len);

  memset(lob_buf, 0, sizeof(lob_buf));
  ASSERT_EQ(C_ORM_OK, c_orm_db2_lob_read(db, C_ORM_SQL_C_CLOB_LOCATOR, 77U, 9,
                                         lob_buf, 10, &lob_read));
  ASSERT_EQ(9U, lob_read);
  ASSERT_STR_EQ("LOB_SLICE", lob_buf);

  ASSERT_EQ(C_ORM_OK,
            c_orm_db2_lob_free_locator(db, C_ORM_SQL_C_DBCLOB_LOCATOR, 77U));
  ASSERT_EQ(77, (int)g_db2.freed_locator);
  ASSERT_STR_EQ("FREE LOCATOR ?", g_db2.last_sql);

  /* Fallback FREE LOCATOR via SQLPrepare+SQLExecute when SQLExecDirect=NULL */
  api.SQLExecDirect = NULL;
  ASSERT_EQ(C_ORM_OK,
            c_orm_db2_lob_free_locator(db, C_ORM_SQL_C_BLOB_LOCATOR, 88U));
  ASSERT_EQ(88, (int)g_db2.freed_locator);

  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_lob_get_length(db, 1234, 77U, &lob_len));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_lob_read(db, C_ORM_SQL_C_BLOB_LOCATOR, 77U, 0, lob_buf,
                               sizeof(lob_buf), &lob_read));

  /* Db2 native SQLCODE -> c_orm_error_t mappings (-803, -911, -913, -302,
   * -802, +100) */
  {
    static const c_orm_sqlinteger_t codes[6] = {-803, -911, -913,
                                                -302, -802, 100};
    static const c_orm_error_t mapped[6] = {
        C_ORM_ERROR_CONSTRAINT,       C_ORM_ERROR_DEADLOCK,
        C_ORM_ERROR_TIMEOUT,          C_ORM_ERROR_TRUNCATION,
        C_ORM_ERROR_DECIMAL_OVERFLOW, C_ORM_ERROR_NOT_FOUND};
    int k;
    g_db2.fail_op = "Execute";
    (void)C_ORM_STRCPY(g_db2.sqlstate, sizeof(g_db2.sqlstate), "HY000");
    for (k = 0; k < 6; k++) {
      g_db2.sqlcode = codes[k];
      ASSERT_EQ(C_ORM_OK, db->vtable->prepare(db, "UPDATE T SET X=1", &q));
      ASSERT_EQ(mapped[k], db->vtable->step(q, &has_row));
      ASSERT_EQ(C_ORM_OK, db->vtable->finalize(q));
    }
    g_db2.fail_op = NULL;
  }

  ASSERT_EQ(C_ORM_OK, db->vtable->disconnect(db));
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_reset_loader());

  /* Optional live IBM Db2 integration smoke test when C_ORM_TEST_DB2_URL set */
  {
    char live_url[512];
    live_url[0] = '\0';
    if (C_ORM_GETENV(live_url, sizeof(live_url), "C_ORM_TEST_DB2_URL") ==
            C_ORM_OK &&
        live_url[0] != '\0') {
      c_orm_db_t *live_db = NULL;
      ASSERT_EQ(C_ORM_OK, c_orm_db2_connect(live_url, &live_db));
      ASSERT_EQ(C_ORM_OK, live_db->vtable->disconnect(live_db));
    }
  }
  PASS();
}

static int g_db2_oom_cd = -1;

/**
 * @brief Mock malloc for Db2 OOM testing.
 * @param sz Requested byte size.
 * @param out_ptr Output pointer.
 * @return C_ORM_OK or C_ORM_ERROR_MEMORY.
 */
static c_orm_error_t db2_oom_malloc(size_t sz, void **out_ptr) {
  if (g_db2_oom_cd >= 0) {
    if (g_db2_oom_cd == 0) {
      *out_ptr = NULL;
      return C_ORM_ERROR_MEMORY;
    }
    g_db2_oom_cd--;
  }
  *out_ptr = malloc(sz);
  return *out_ptr ? C_ORM_OK : C_ORM_ERROR_MEMORY;
}

/**
 * @brief Exhaustive validation, overflow, Safe CRT, OOM, WLM, schema,
 * identity, savepoint, LOB, and timestamp branch tests for `src/c_orm_db2.c`.
 * @return GREATEST test result.
 */
TEST test_db2_exhaustive_errors_and_oom(void) {
  char conn[2048];
  char ts[64];
  char big_url[700];
  c_orm_db2_url_opts_t opts;
  c_orm_odbc_api_t api;
  c_orm_db_t *db = NULL;
  c_orm_db_t *sq_db = NULL;
  const c_orm_driver_vtable_t *vt = NULL;
  int64_t last_id = 0;
  c_orm_int64_t lob_len = 0;
  char lob_buf[32];
  size_t lob_read = 0;
  size_t sz;
  int step;
  c_orm_malloc_fn_t old_malloc = c_orm_malloc;

  /* 1. URL builder edge cases, percent-decode errors, buffer overflows, Safe
   * CRT */
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_db2_build_conn_string("db2://h/D", 1, NULL, 64, NULL));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_db2_build_conn_string("db2://h/D", 1, conn, 1, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_build_conn_string("", 1, conn, sizeof(conn), NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_build_conn_string("db2", 1, conn, sizeof(conn), NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_build_conn_string("plain_no_scheme", 1, conn,
                                        sizeof(conn), NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_db2_build_conn_string("DSN=MYDB2;\tUID=a;", 1, conn,
                                                  sizeof(conn), NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_db2_build_conn_string("DRIVER={Db2};", 1, conn,
                                                  sizeof(conn), &opts));
  ASSERT_EQ(C_ORM_OK, c_orm_db2_build_conn_string("CUSTOM=1;", 1, conn,
                                                  sizeof(conn), NULL));
  ASSERT_EQ(
      C_ORM_ERROR_VALIDATION,
      c_orm_db2_build_conn_string("DSN=BAD\x01;", 1, conn, sizeof(conn), NULL));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_db2_build_conn_string("DSN=TOO_LONG;", 1, conn, 5, NULL));

  /* Auth, host, port, path, query variations & invalid %XX */
  ASSERT_EQ(C_ORM_OK, c_orm_db2_build_conn_string("db2://onlyuser@host/DB?", 1,
                                                  conn, sizeof(conn), NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_db2_build_conn_string("db2://:onlypass@host/DB", 1,
                                                  conn, sizeof(conn), NULL));
  ASSERT_EQ(C_ORM_OK,
            c_orm_db2_build_conn_string("db2:///SAMPLE?driver=CustomDrv", 1,
                                        conn, sizeof(conn), NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_db2_build_conn_string("db2://host/", 1, conn,
                                                  sizeof(conn), NULL));
  ASSERT_EQ(
      C_ORM_ERROR_VALIDATION,
      c_orm_db2_build_conn_string("db2:///", 1, conn, sizeof(conn), NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_build_conn_string("db2://u%GG:p@h/D", 1, conn,
                                        sizeof(conn), NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_build_conn_string("db2://u:p%GG@h/D", 1, conn,
                                        sizeof(conn), NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_build_conn_string("db2://u%GG@h/D", 1, conn, sizeof(conn),
                                        NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_build_conn_string("db2://h%GG:50000/D", 1, conn,
                                        sizeof(conn), NULL));
  ASSERT_EQ(
      C_ORM_ERROR_VALIDATION,
      c_orm_db2_build_conn_string("db2://h:/D", 1, conn, sizeof(conn), NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_build_conn_string("db2://h:%GG/D", 1, conn, sizeof(conn),
                                        NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_build_conn_string("db2://h:abc/D", 1, conn, sizeof(conn),
                                        NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_build_conn_string("db2://h:12x/D", 1, conn, sizeof(conn),
                                        NULL));
  ASSERT_EQ(
      C_ORM_ERROR_VALIDATION,
      c_orm_db2_build_conn_string("db2://h:0/D", 1, conn, sizeof(conn), NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_build_conn_string("db2://h:70000/D", 1, conn,
                                        sizeof(conn), NULL));
  ASSERT_EQ(
      C_ORM_ERROR_VALIDATION,
      c_orm_db2_build_conn_string("db2://h%GG/D", 1, conn, sizeof(conn), NULL));
  ASSERT_EQ(
      C_ORM_ERROR_VALIDATION,
      c_orm_db2_build_conn_string("db2://h/D%GG", 1, conn, sizeof(conn), NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_build_conn_string("db2://h/D?noeq", 1, conn, sizeof(conn),
                                        NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_build_conn_string("db2://h/D?=val", 1, conn, sizeof(conn),
                                        NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_build_conn_string("db2://h/D?k%GG=v", 1, conn,
                                        sizeof(conn), NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_build_conn_string("db2://h/D?k=v%GG", 1, conn,
                                        sizeof(conn), NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_build_conn_string("db2://h/D?ssl=maybe", 1, conn,
                                        sizeof(conn), NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_build_conn_string("db2://h/D?autocommit=maybe", 1, conn,
                                        sizeof(conn), NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_build_conn_string("db2://h/D?timeout=abc", 1, conn,
                                        sizeof(conn), NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_build_conn_string("db2://h/D?timeout=10x", 1, conn,
                                        sizeof(conn), NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_build_conn_string("db2://h/D?timeout=-1", 1, conn,
                                        sizeof(conn), NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_build_conn_string("db2://h/D?login_timeout=99999", 1,
                                        conn, sizeof(conn), NULL));

  /* Alternate query keys & boolean variants */
  {
    const char *full_q =
        "db2://u:p@host:50000/DB?currentschema=S&isolation=CS&ssl=yes"
        "&client_userid=cu&wrkstnname=wn&applname=ap&acctstr=ac"
        "&login_timeout=5&autocommit=on&driver=Drv&extra=1";
    ASSERT_EQ(C_ORM_OK, c_orm_db2_build_conn_string(full_q, 0, conn,
                                                    sizeof(conn), &opts));
    ASSERT_EQ(C_ORM_OK,
              c_orm_db2_build_conn_string("db2://h/D?ssl=1&autocommit=0", 1,
                                          conn, sizeof(conn), &opts));
    ASSERT_EQ(C_ORM_OK,
              c_orm_db2_build_conn_string("db2://h/D?ssl=0&autocommit=false", 1,
                                          conn, sizeof(conn), &opts));
    ASSERT_EQ(C_ORM_OK, c_orm_db2_build_conn_string("db2://h/D?ssl=no&", 1,
                                                    conn, sizeof(conn), &opts));

    /* Sweep small buf_size from 2..130 to hit every db2_append_kv overflow */
    for (sz = 2; sz < 135; sz += 5) {
      (void)c_orm_db2_build_conn_string(full_q, 0, conn, sz, NULL);
    }
    ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
              c_orm_db2_build_conn_string("db2://SUBSYS", 1, conn, 6, NULL));

#ifdef C_ORM_TEST_ALLOCATOR
    for (step = 0; step < 12; ++step) {
      c_orm_mock_safe_crt_countdown = step;
      if (c_orm_db2_build_conn_string(
              "db2://h/D?schema=S&userid=u&wrkstn=w&applname=a&acctstr=c"
              "&uid=x&pwd=y&driver=d",
              1, conn, sizeof(conn), &opts) == C_ORM_OK) {
        c_orm_mock_safe_crt_countdown = -1;
        break;
      }
      c_orm_mock_safe_crt_countdown = -1;
    }
#endif
  }

  /* >500-char query value triggers esc[512] overflow in db2_append_kv */
  memcpy(big_url, "db2://h/D?schema=", 17);
  memset(big_url + 17, 'A', 505);
  big_url[522] = '\0';
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_db2_build_conn_string(big_url, 1, conn, sizeof(conn), NULL));

  memcpy(big_url, "db2://h/D?extra=", 16);
  memset(big_url + 16, '}', 300); /* doubled '}}' -> 602 > esc[512] */
  big_url[316] = '\0';
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_db2_build_conn_string(big_url, 1, conn, sizeof(conn), NULL));
  {
    char multi_extra[1400];
    int p = 10;
    memcpy(multi_extra, "db2://h/D?", 10);
    for (step = 0; step < 10; ++step) {
      int k;
      multi_extra[p++] = 'k';
      multi_extra[p++] = (char)('a' + step);
      multi_extra[p++] = '=';
      for (k = 0; k < 95; ++k) {
        multi_extra[p++] = 'x';
      }
      multi_extra[p++] = '&';
    }
    memcpy(multi_extra + p, "schema=OVERFLOW_SCHEMA_LONG_NAME_12345", 39);
    ASSERT_EQ(
        C_ORM_ERROR_OVERFLOW,
        c_orm_db2_build_conn_string(multi_extra, 1, conn, sizeof(conn), NULL));
    memcpy(multi_extra + p, "kz=OVERFLOW_EXTRA_LONG_VALUE_123456789", 39);
    ASSERT_EQ(
        C_ORM_ERROR_OVERFLOW,
        c_orm_db2_build_conn_string(multi_extra, 1, conn, sizeof(conn), NULL));
  }

  /* 2. Connect, WLM, schema, isolation, vtable error branches */
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_db2_get_vtable(NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_db2_get_vtable(&vt));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_db2_connect(NULL, &db));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_db2_connect("db2://h/D", NULL));

  ASSERT_EQ(C_ORM_OK, c_orm_odbc_reset_loader());
  (void)set_db2_env("C_ORM_DB2_LIB", "/nonexistent_libdb2_999.so");
  ASSERT_EQ(C_ORM_ERROR_DRIVER_LOAD, c_orm_db2_connect("db2://h/D", &db));
  (void)set_db2_env("C_ORM_DB2_LIB", "");

  (void)reset_db2_mock();
  (void)build_mock_db2_api(&api);
  ASSERT_EQ(C_ORM_OK, c_orm_db2_set_mock_api(&api, 1));

  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_db2_connect("db2://", &db));

  g_db2.fail_op = "DriverConnect";
  ASSERT_NEQ(C_ORM_OK, c_orm_db2_connect("db2://h/D", &db));

  g_db2.fail_op = "SetWlm";
  ASSERT_NEQ(C_ORM_OK, c_orm_db2_connect("db2://h/D?userid=u", &db));
  g_db2.fail_op = "SetWlmWrk";
  ASSERT_NEQ(C_ORM_OK, c_orm_db2_connect("db2://h/D?wrkstn=w", &db));
  g_db2.fail_op = "SetWlmApp";
  ASSERT_NEQ(C_ORM_OK, c_orm_db2_connect("db2://h/D?applname=a", &db));
  g_db2.fail_op = "SetWlmAcc";
  ASSERT_NEQ(C_ORM_OK, c_orm_db2_connect("db2://h/D?acctstr=c", &db));

  g_db2.reject_schema_attr = 1;
  g_db2.fail_op = "Prepare";
  ASSERT_NEQ(C_ORM_OK, c_orm_db2_connect("db2://h/D?schema=S", &db));
  g_db2.reject_schema_attr = 0;

  g_db2.fail_op = "SetIso";
  ASSERT_NEQ(C_ORM_OK, c_orm_db2_connect("db2://h/D?isolation=CS", &db));
  g_db2.fail_op = NULL;

  /* Valid connect + non-ODBC SQLite handle rejection */
  ASSERT_EQ(C_ORM_OK, c_orm_db2_connect("db2://h/D", &db));
  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_connect(":memory:", &sq_db));

  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_set_client_info(sq_db, "u", "w", "a", "c"));
  ASSERT_EQ(C_ORM_OK, c_orm_db2_set_client_info(db, NULL, "", NULL, ""));
#ifdef C_ORM_TEST_ALLOCATOR
  c_orm_mock_safe_crt_countdown = 0;
  ASSERT_NEQ(C_ORM_OK, c_orm_db2_set_client_info(db, "u", NULL, NULL, NULL));
  c_orm_mock_safe_crt_countdown = -1;
#endif
  api.SQLSetConnectAttr = NULL;
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED,
            c_orm_db2_set_client_info(db, "u", NULL, NULL, NULL));

  /* set_current_schema edge cases */
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_db2_set_current_schema(NULL, "S"));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_db2_set_current_schema(db, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_db2_set_current_schema(sq_db, "S"));
  ASSERT_EQ(C_ORM_OK, c_orm_db2_set_current_schema(db, "NO_ATTR_SCHEMA"));
#ifdef C_ORM_TEST_ALLOCATOR
  c_orm_mock_string_builder_append_countdown = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_db2_set_current_schema(db, "S"));
  c_orm_mock_string_builder_append_countdown = -1;
  c_orm_mock_string_builder_get_fail = 1;
  ASSERT_NEQ(C_ORM_OK, c_orm_db2_set_current_schema(db, "S"));
  c_orm_mock_string_builder_get_fail = 0;
#endif
  c_orm_set_allocators(db2_oom_malloc, c_orm_realloc, c_orm_free);
  g_db2_oom_cd = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_db2_set_current_schema(db, "S"));
  g_db2_oom_cd = -1;
  c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);

  api.SQLSetConnectAttr = SQLDb2_SetConnectAttr;
  ASSERT_EQ(C_ORM_OK, c_orm_db2_set_isolation(db, C_ORM_DB2_ISOLATION_RS));
#ifdef C_ORM_TEST_ALLOCATOR
  g_db2.reject_schema_attr = 0;
  c_orm_mock_safe_crt_countdown = 0;
  ASSERT_EQ(C_ORM_OK, c_orm_db2_set_current_schema(db, "FALLBACK_S"));
  c_orm_mock_safe_crt_countdown = -1;
#endif

  /* 3. IDENTITY_VAL_LOCAL() error & boundary paths */
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            db->vtable->get_last_insert_rowid(NULL, &last_id));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            db->vtable->get_last_insert_rowid(db, NULL));
  g_db2.fail_op = "Prepare";
  ASSERT_NEQ(C_ORM_OK, db->vtable->get_last_insert_rowid(db, &last_id));
  g_db2.fail_op = "Execute";
  ASSERT_NEQ(C_ORM_OK, db->vtable->get_last_insert_rowid(db, &last_id));
  g_db2.fail_op = NULL;
  g_db2.mode = 5; /* 0 rows */
  ASSERT_EQ(C_ORM_OK, db->vtable->get_last_insert_rowid(db, &last_id));
  g_db2.mode = 6; /* > INT64_MAX overflow */
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
            db->vtable->get_last_insert_rowid(db, &last_id));
  g_db2.mode = 7; /* get_decimal error after is_null succeeds */
  ASSERT_NEQ(C_ORM_OK, db->vtable->get_last_insert_rowid(db, &last_id));
  g_db2.mode = 8; /* is_null error */
  ASSERT_NEQ(C_ORM_OK, db->vtable->get_last_insert_rowid(db, &last_id));
  g_db2.mode = 0;

  /* 4. Savepoint error & OOM paths */
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_db2_savepoint_create(NULL, "sp"));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_db2_savepoint_create(db, NULL));
  c_orm_set_allocators(db2_oom_malloc, c_orm_realloc, c_orm_free);
  g_db2_oom_cd = 0;
  ASSERT_EQ(C_ORM_ERROR_MEMORY, c_orm_db2_savepoint_create(db, "sp"));
  g_db2_oom_cd = -1;
  c_orm_set_allocators(old_malloc, c_orm_realloc, c_orm_free);
#ifdef C_ORM_TEST_ALLOCATOR
  for (step = 0; step < 6; ++step) {
    c_orm_mock_string_builder_append_countdown = step;
    if (c_orm_db2_savepoint_create(db, "sp") == C_ORM_OK) {
      c_orm_mock_string_builder_append_countdown = -1;
      break;
    }
    c_orm_mock_string_builder_append_countdown = -1;
  }
  c_orm_mock_string_builder_get_fail = 1;
  ASSERT_NEQ(C_ORM_OK, c_orm_db2_savepoint_create(db, "sp"));
  c_orm_mock_string_builder_get_fail = 0;
#endif

  /* 5. LOB Locator error & type paths */
  ASSERT_EQ(
      C_ORM_ERROR_NULL_POINTER,
      c_orm_db2_lob_get_length(NULL, C_ORM_SQL_C_BLOB_LOCATOR, 1U, &lob_len));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_db2_lob_get_length(db, C_ORM_SQL_C_BLOB_LOCATOR, 1U, NULL));
  ASSERT_EQ(
      C_ORM_ERROR_VALIDATION,
      c_orm_db2_lob_get_length(sq_db, C_ORM_SQL_C_BLOB_LOCATOR, 1U, &lob_len));
  ASSERT_EQ(C_ORM_OK, c_orm_db2_lob_get_length(
                          db, C_ORM_SQL_C_DBCLOB_LOCATOR_CLI, 998U, &lob_len));
  ASSERT_EQ(0, (int)lob_len);
  g_db2.fail_op = "AllocStmt";
  ASSERT_NEQ(C_ORM_OK, c_orm_db2_lob_get_length(db, C_ORM_SQL_C_BLOB_LOCATOR,
                                                1U, &lob_len));
  g_db2.fail_op = "GetLength";
  ASSERT_NEQ(C_ORM_OK, c_orm_db2_lob_get_length(db, C_ORM_SQL_C_BLOB_LOCATOR,
                                                1U, &lob_len));
  g_db2.fail_op = NULL;
  api.SQLGetLength = NULL;
  ASSERT_EQ(
      C_ORM_ERROR_NOT_IMPLEMENTED,
      c_orm_db2_lob_get_length(db, C_ORM_SQL_C_BLOB_LOCATOR, 1U, &lob_len));
  api.SQLGetLength = SQLDb2_GetLength;

  /* lob_read */
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_db2_lob_read(NULL, C_ORM_SQL_C_BLOB_LOCATOR, 1U, 1, lob_buf,
                               8, &lob_read));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_db2_lob_read(db, C_ORM_SQL_C_BLOB_LOCATOR, 1U, 1, NULL, 8,
                               &lob_read));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_db2_lob_read(db, C_ORM_SQL_C_BLOB_LOCATOR, 1U, 1, lob_buf, 8,
                               NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_lob_read(db, C_ORM_SQL_C_BLOB_LOCATOR, 1U, 1, lob_buf, 0,
                               &lob_read));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_lob_read(db, 9999, 1U, 1, lob_buf, 8, &lob_read));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_lob_read(sq_db, C_ORM_SQL_C_BLOB_LOCATOR, 1U, 1, lob_buf,
                               8, &lob_read));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_db2_lob_read(db, C_ORM_SQL_C_CLOB_LOCATOR, 1U, 1, lob_buf, 1,
                               &lob_read));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_db2_lob_read(db, C_ORM_SQL_C_DBCLOB_LOCATOR, 1U, 1, lob_buf,
                               2, &lob_read));
  ASSERT_EQ(C_ORM_OK, c_orm_db2_lob_read(db, C_ORM_SQL_C_BLOB_LOCATOR, 1U, 1,
                                         lob_buf, 8, &lob_read));
  ASSERT_EQ(8U, lob_read);
  ASSERT_EQ(C_ORM_OK, c_orm_db2_lob_read(db, C_ORM_SQL_C_DBCLOB_LOCATOR, 999U,
                                         1, lob_buf, 16, &lob_read));
  ASSERT_EQ(0U, lob_read);
  ASSERT_EQ(C_ORM_OK, c_orm_db2_lob_read(db, C_ORM_SQL_C_BLOB_LOCATOR, 1U, 500,
                                         lob_buf, 8, &lob_read));
  ASSERT_EQ(0U, lob_read);
  g_db2.fail_op = "AllocStmt";
  ASSERT_NEQ(C_ORM_OK, c_orm_db2_lob_read(db, C_ORM_SQL_C_BLOB_LOCATOR, 1U, 1,
                                          lob_buf, 8, &lob_read));
  g_db2.fail_op = "GetSubString";
  ASSERT_NEQ(C_ORM_OK, c_orm_db2_lob_read(db, C_ORM_SQL_C_BLOB_LOCATOR, 1U, 1,
                                          lob_buf, 8, &lob_read));
  g_db2.fail_op = NULL;
  api.SQLGetSubString = NULL;
  ASSERT_EQ(C_ORM_ERROR_NOT_IMPLEMENTED,
            c_orm_db2_lob_read(db, C_ORM_SQL_C_BLOB_LOCATOR, 1U, 1, lob_buf, 8,
                               &lob_read));
  api.SQLGetSubString = SQLDb2_GetSubString;

  /* lob_free_locator */
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_db2_lob_free_locator(NULL, C_ORM_SQL_C_BLOB_LOCATOR, 1U));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_db2_lob_free_locator(db, 9999, 1U));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_lob_free_locator(sq_db, C_ORM_SQL_C_BLOB_LOCATOR, 1U));
  g_db2.fail_op = "AllocStmt";
  ASSERT_NEQ(C_ORM_OK,
             c_orm_db2_lob_free_locator(db, C_ORM_SQL_C_BLOB_LOCATOR, 1U));
  g_db2.fail_op = "Bind";
  ASSERT_EQ(C_ORM_ERROR_BIND,
            c_orm_db2_lob_free_locator(db, C_ORM_SQL_C_BLOB_LOCATOR, 1U));
  g_db2.fail_op = "FreeLocator";
  ASSERT_NEQ(C_ORM_OK,
             c_orm_db2_lob_free_locator(db, C_ORM_SQL_C_BLOB_LOCATOR, 1U));
  api.SQLExecDirect = NULL;
  g_db2.fail_op = "Prepare";
  ASSERT_NEQ(C_ORM_OK,
             c_orm_db2_lob_free_locator(db, C_ORM_SQL_C_BLOB_LOCATOR, 1U));
  g_db2.fail_op = "Execute";
  ASSERT_NEQ(C_ORM_OK,
             c_orm_db2_lob_free_locator(db, C_ORM_SQL_C_BLOB_LOCATOR, 1U));
  g_db2.fail_op = NULL;

  ASSERT_EQ(C_ORM_OK, sq_db->vtable->disconnect(sq_db));
  ASSERT_EQ(C_ORM_OK, db->vtable->disconnect(db));
  ASSERT_EQ(C_ORM_OK, c_orm_odbc_reset_loader());

  /* 6. Timestamp normalization remaining validation branches */
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_db2_normalize_timestamp("2026-11-17-08.05.01", NULL, 32));
  ASSERT_EQ(C_ORM_OK, c_orm_db2_normalize_timestamp("\t2026-11-17 08:05:01\t",
                                                    ts, sizeof(ts)));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_normalize_timestamp("   \t   ", ts, sizeof(ts)));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_db2_normalize_timestamp("2026-11-17-08.05.01.12345678901234",
                                          ts, sizeof(ts)));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_db2_normalize_timestamp(
                                        "202!-11-17-08.05.01", ts, sizeof(ts)));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_db2_normalize_timestamp(
                                        "202X-11-17-08.05.01", ts, sizeof(ts)));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_db2_normalize_timestamp(
                                        "2026-11/17-08.05.01", ts, sizeof(ts)));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_db2_normalize_timestamp(
                                        "2026-11-17_08.05.01", ts, sizeof(ts)));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_db2_normalize_timestamp(
                                        "2026-11-17-08.05:01", ts, sizeof(ts)));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_db2_normalize_timestamp(
                                        "2026-11-17-08:05.01", ts, sizeof(ts)));
  ASSERT_EQ(
      C_ORM_ERROR_VALIDATION,
      c_orm_db2_normalize_timestamp("2026-11-17-08.05.01,5", ts, sizeof(ts)));
  ASSERT_EQ(
      C_ORM_ERROR_VALIDATION,
      c_orm_db2_normalize_timestamp("2026-11-17-08.05.01.5X", ts, sizeof(ts)));
  PASS();
}

/**
 * @brief Test suite entry point for IBM Db2 CLI/ODBC driver.
 * @param db2_driver_suite Suite runner function name.
 */
SUITE(db2_driver_suite) {
  RUN_TEST(test_db2_loader_urls_and_timestamps);
  RUN_TEST(test_db2_session_lobs_types_and_sqlcodes);
  RUN_TEST(test_db2_exhaustive_errors_and_oom);
}
