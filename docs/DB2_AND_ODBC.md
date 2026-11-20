# IBM Db2 & Generic ODBC Drivers (`db2://` & `odbc://`)

`c-orm` provides first-class ISO C89 drivers for **IBM Db2** (Db2 LUW, Db2 for z/OS, and Db2 for i via IBM Db2 CLI or ODBC) and **Generic ODBC 3.52** (SQL Server, PostgreSQL, Snowflake, Teradata, Oracle, SQLite ODBC, etc.) without pulling in `<windows.h>` or requiring vendor SDK headers at compile time.

---

## 1. Zero-`<windows.h>` C89 ABI Architecture

Standard `<sql.h>` and `<sqltypes.h>` headers on Windows require `#include <windows.h>` before inclusion, bloating translation units and polluting macro namespaces. `c-orm` instead defines a self-contained, binary-exact C89 ABI in `include/c_orm_odbc_abi.h` verified by compile-time `sizeof` assertions across 32-bit ILP32, 64-bit LP64 (Linux/macOS), and 64-bit LLP64 (Windows):

- **Calling convention**: `C_ORM_SQL_API` resolves to `__stdcall` on `_WIN32`/`_WIN64` and empty on POSIX.
- **Runtime Dynamic Loader (`src/c_orm_odbc_loader.c`)**:
  - **Generic ODBC (`c_orm_odbc_load_driver_manager`)**: Checks `C_ORM_ODBC_LIB`, then loads `odbc32.dll` on Windows, `libodbc.so.2` / `libodbc.so` on Linux/Cygwin, or `libodbc.2.dylib` / Homebrew `unixODBC` / `libiodbc.dylib` on macOS.
  - **IBM Db2 CLI (`c_orm_db2_load_cli`)**: Checks `C_ORM_DB2_LIB` and `IBM_DB_HOME` (`db2cli64.dll` / `db2app64.dll` on Windows, `libdb2.so.1` / `libdb2.so` on Linux, `libdb2.dylib` on macOS), and falls back automatically to the ODBC Driver Manager if direct `libdb2` is not installed.
  - **Optional Direct Linkage**: Configure CMake with `-DC_ORM_LINK_ODBC=ON` or `-DC_ORM_LINK_DB2=ON` to link directly against system or `vcpkg` import libraries instead of `dlopen`/`LoadLibrary`.
- **ANSI & UNICODE Support**: Respects `-DC_ORM_CHARSET=UNICODE` (`SQLDriverConnectW`, `SQLPrepareW`, `SQLGetDiagRecW` via internal UTF-8 <-> UTF-16 transcoding) and `-DC_ORM_CHARSET=ANSI` (`SQLDriverConnect`, `SQLPrepare`, `SQLGetDiagRec`).

---

## 2. Connection URL Syntax & Injection Protection

You can connect via unified `c_orm_connect_url`, `c_orm_db2_connect`, or `c_orm_odbc_connect`:

### IBM Db2 (`db2://`)
```c
c_orm_db_t *db = NULL;
c_orm_error_t rc;

/* Direct TCP/IP connection with schema, isolation, SSL, and z/OS WLM registers */
rc = c_orm_connect_url(
    "db2://db2inst1:secret@db2host.internal:50000/SAMPLE"
    "?schema=PROD&isolation=CS&ssl=true"
    "&applname=ledger_svc&wrkstn=batch01&acctstr=DEPT42",
    &db);

/* Cataloged z/OS or LUW DSN alias */
rc = c_orm_db2_connect("db2://DB2Z_SUBSYS?uid=BATCHUSR&pwd=s3cr%3B%7D%7Bet", &db);
```

### Generic ODBC (`odbc://` or Raw DSN/Driver String)
```c
c_orm_db_t *db = NULL;
c_orm_error_t rc;

/* URL syntax */
rc = c_orm_connect_url(
    "odbc://sa:P%40ss%3Bword@sqlhost:1433/Billing"
    "?driver=ODBC+Driver+18+for+SQL+Server&timeout=10&encrypt=yes",
    &db);

/* DSN or raw ODBC connection string */
rc = c_orm_odbc_connect("odbc://DSN=ReportingDSN;UID=analyst;PWD=secret", &db);
```

### Connection-String Injection Prevention
All URL-decoded credentials and query parameters pass through `c_orm_odbc_escape_conn_value`:
- Embedded `%00` NUL bytes and CR/LF control characters are rejected with `C_ORM_ERROR_VALIDATION`.
- Values containing `;`, `{`, `}`, `=`, quotes, or leading/trailing whitespace are automatically wrapped in `{...}` with internal `}` doubled to `}}` per the ODBC 3.x specification so untrusted passwords can never inject extra connection keywords.

---

## 3. IBM Db2 Enterprise Features

### Workload Manager (WLM) Client Accounting Registers
Set z/OS and LUW WLM special registers (`CLIENT USERID`, `CLIENT WRKSTNNAME`, `CLIENT APPLNAME`, `CLIENT ACCTNG`) either via URL query parameters or at runtime:
```c
rc = c_orm_db2_set_client_info(db, "CLERK07", "CICSGW02", "LEDGERPOST", "COSTCTR-900");
```

### Active Schema & Isolation Levels
```c
/* Applies SQL_ATTR_CURRENT_SCHEMA / SET CURRENT SCHEMA = "FINANCE" */
rc = c_orm_db2_set_current_schema(db, "FINANCE");

/* UR (Uncommitted Read), CS (Cursor Stability), RS (Read Stability), RR (Repeatable Read) */
rc = c_orm_db2_set_isolation(db, C_ORM_DB2_ISOLATION_RS);
```

### Native CLI Transactions & Db2 Savepoints
- `c_orm_transaction_begin`, `c_orm_transaction_commit`, and `c_orm_transaction_rollback` invoke `SQLSetConnectAttr(SQL_ATTR_AUTOCOMMIT, SQL_AUTOCOMMIT_OFF)` and `SQLEndTran(SQL_COMMIT / SQL_ROLLBACK)` directly instead of emitting invalid `BEGIN;` SQL.
- `c_orm_savepoint_create` (and `c_orm_db2_savepoint_create`) automatically emits `SAVEPOINT "<name>" ON ROLLBACK RETAIN CURSORS` as required by IBM Db2.

### Db2 SQL Dialect Support (`C_ORM_DIALECT_DB2`)
- **Pagination**: Emits `FETCH FIRST n ROWS ONLY` and `OFFSET m ROWS FETCH NEXT n ROWS ONLY`.
- **No-`FROM` Queries**: Appends `FROM SYSIBM.SYSDUMMY1`.
- **DML `RETURNING` Clauses**: Compiles AST `RETURNING` nodes into Db2 data-change-table references:
  - `SELECT ... FROM FINAL TABLE (INSERT INTO ...)`
  - `SELECT ... FROM FINAL TABLE (UPDATE ...)`
  - `SELECT ... FROM OLD TABLE (DELETE FROM ...)`
- **Upserts (`c_orm_upsert`)**: Emits standards-compliant Db2 `MERGE INTO ... USING (VALUES (...)) ... WHEN MATCHED THEN UPDATE ... WHEN NOT MATCHED THEN INSERT ...`.
- **Lock Clauses**: Fluent AST queries support `WITH UR`, `WITH CS`, `WITH RS`, `WITH RR`, and `FOR FETCH ONLY`.

### Lossless `DECIMAL(p, s)`, `DECFLOAT`, `GRAPHIC`, `FOR BIT DATA`, & `LOB` Locators
- **`c_orm_decimal_t` (`C_ORM_TYPE_DECIMAL` / `C_ORM_TYPE_DECFLOAT`)**: Up to 38 base-10 digits bound via native 128-bit `SQL_C_NUMERIC` (`c_orm_sql_numeric_struct_t`) with automatic string fallback for legacy ODBC drivers.
- **`GRAPHIC` / `VARGRAPHIC` / `DBCLOB`**: Bound and fetched in UTF-16 (`SQL_C_WCHAR`) and transcoded losslessly to/from UTF-8 via `c_orm_utf16_to_utf8` / `c_orm_utf8_to_utf16`.
- **`CHAR(n) FOR BIT DATA` / `VARCHAR(n) FOR BIT DATA`**: Bound and hydrated via `SQL_C_BINARY` (`C_ORM_TYPE_BINARY_FIXED` / `C_ORM_TYPE_VARBINARY`, `ccsid = 65535`) with zero codepage conversion.
- **Db2 LOB Locators**: Stream slices of multi-gigabyte mainframe `BLOB`/`CLOB`/`DBCLOB` columns without materializing the full payload in heap memory:
  ```c
  c_orm_int64_t total_len = 0;
  size_t bytes_read = 0;
  unsigned char chunk[4096];

  rc = c_orm_db2_lob_get_length(db, C_ORM_SQL_C_BLOB_LOCATOR, locator, &total_len);
  rc = c_orm_db2_lob_read(db, C_ORM_SQL_C_BLOB_LOCATOR, locator, 1,
                          chunk, sizeof(chunk), &bytes_read);
  rc = c_orm_db2_lob_free_locator(db, C_ORM_SQL_C_BLOB_LOCATOR, locator);
  ```

---

## 4. Environment & Driver Setup

- **Windows**: `odbc32.dll` is built into Windows; install the IBM Db2 Data Server Driver or database vendor ODBC driver.
- **Linux (Ubuntu/Debian/Alpine)**: Install `unixodbc` (plus `libsqlite3-odbc`, `psqlodbc`, or IBM `dsdriver` with `IBM_DB_HOME` pointing to the driver root). Override library path anytime via `C_ORM_ODBC_LIB=/path/to/libodbc.so` or `C_ORM_DB2_LIB=/path/to/libdb2.so`.
- **macOS**: `brew install unixodbc` (auto-detected at `/opt/homebrew/lib/libodbc.dylib` and `/usr/local/lib/libodbc.dylib`).
- **Live Integration Tests**: Set `C_ORM_TEST_ODBC_URL` or `C_ORM_TEST_DB2_URL` when running `ctest` to activate live database integration suites alongside the built-in mock ABI test suites.
