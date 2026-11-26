# Error Handling in c-orm

`c-orm` uses the `c_orm_error_t` enumeration (defined in `include/c_orm_meta.h`) across **all** API functions and callbacks. Every non-void, non-math function returns `c_orm_error_t` and passes output values via pointer parameters.

## Complete `c_orm_error_t` Variants

* `C_ORM_OK` (`0`): Operation succeeded.
* `C_ORM_ERROR_MEMORY`: Heap or arena allocation (`malloc` / `calloc` / `realloc`) failed.
* `C_ORM_ERROR_CONNECTION`: Database connection failed or dropped (`SQLSTATE 08xxx`).
* `C_ORM_ERROR_SQL`: SQL syntax or prepare/DDL error (`SQLSTATE 42xxx`).
* `C_ORM_ERROR_BIND`: Parameter binding failed.
* `C_ORM_ERROR_STEP`: Statement execution or cursor fetch failed.
* `C_ORM_ERROR_TYPE_MISMATCH`: Mismatch between database wire type and C metadata column type.
* `C_ORM_ERROR_NOT_FOUND`: No matching row (`SQL_NO_DATA` / Db2 `+100`), column, or condition found.
* `C_ORM_ERROR_NOT_IMPLEMENTED`: Capability is not supported by the active driver or build configuration.
* `C_ORM_ERROR_UNKNOWN`: Unclassified fallback error.
* `C_ORM_ERROR_EXPIRED`: Token, cache entry, or session expired.
* `C_ORM_ERROR_VALIDATION`: Domain, nullability, precision/scale, or level-`88` validation failed.
* `C_ORM_ERROR_RECURSION`: Maximum relation or parser recursion depth exceeded.
* `C_ORM_ERROR_READ_ONLY`: Attempted mutation on a read-only view or handle.
* `C_ORM_ERROR_NULL_POINTER`: Required pointer argument was `NULL`.
* `C_ORM_ERROR_IO`: Filesystem open, read, or write failure.
* `C_ORM_ERROR_PARSE`: Malformed JSON, URL, or schema input.
* `C_ORM_ERROR_OVERFLOW`: Destination buffer capacity or integer range exceeded.
* `C_ORM_ERROR_CYCLE`: Cyclic graph detected in relationships or migrations.
* `C_ORM_ERROR_BUSY`: Database or lock resource is busy.
* `C_ORM_ERROR_CRYPTO`: Cryptographic or random number generator failure.
* `C_ORM_ERROR_CONSTRAINT`: Primary key, unique, foreign-key, or `CHECK` constraint violation (`SQLSTATE 23xxx`, Db2 `SQLCODE -803`).
* `C_ORM_ERROR_DRIVER_LOAD`: Dynamic client library (`odbc32`, `libodbc`, `libdb2`) or required symbol failed to load.
* `C_ORM_ERROR_ODBC_DIAG`: Underlying ODBC / Db2 CLI call returned `SQL_ERROR` or `SQL_INVALID_HANDLE` (inspect via `c_orm_get_last_error_message` / `c_orm_odbc_get_sqlstate`).
* `C_ORM_ERROR_TIMEOUT`: Login or query timeout (`SQLSTATE HYT00` / `HYT01`, Db2 `-913`).
* `C_ORM_ERROR_DEADLOCK`: Serialization failure or deadlock victim (`SQLSTATE 40001`, Db2 `SQL0911N` / `-911`).
* `C_ORM_ERROR_TRUNCATION`: Disallowed string, binary, or fractional right-truncation (`SQLSTATE 01004` / `22001`, Db2 `-302` / `-433`).
* `C_ORM_ERROR_DECIMAL_SYNTAX`: Malformed decimal string or invalid `COMP-3` BCD/sign nybble.
* `C_ORM_ERROR_DECIMAL_OVERFLOW`: Precision exceeded (`> 38` digits or column `DECIMAL(p,s)` bounds) or division by zero (`SQLSTATE 22003` / `22012`).
* `C_ORM_ERROR_DECIMAL_INEXACT`: Non-zero fractional digits would be discarded under `C_ORM_ROUND_ERROR_IF_INEXACT`.
* `C_ORM_ERROR_CODEPAGE`: Unmappable character or invalid UTF-8 / UTF-16 / EBCDIC byte sequence.
* `C_ORM_ERROR_COPYBOOK_SYNTAX`: Lexer or parser syntax error in COBOL Copybook (`.cpy`) source.

## Human-Readable Error Descriptions

Use `c_orm_error_to_string` (plus `c_orm_get_last_error_message` on a `c_orm_db_t *` handle) for diagnostics:

```c
const char *err_name = NULL;
c_orm_error_t rc = c_orm_insert(db, &Users_meta, &user);

if (rc != C_ORM_OK) {
    c_orm_error_t str_rc = c_orm_error_to_string(rc, &err_name);
    printf("Insert failed: %s (rc=%d, str_rc=%d)\n",
           (str_rc == C_ORM_OK && err_name) ? err_name : "unknown", (int)rc,
           (int)str_rc);
}
```
