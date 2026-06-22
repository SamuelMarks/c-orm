# Error Handling in c-orm

`c-orm` uses a standardized `c_orm_error_t` enumeration (defined in `c_orm_db.h`) across all API functions. Every non-void function returns this type (or an `int` containing its value).

## Core Error Codes

*   `C_ORM_OK` (0): Success. No error occurred.
*   `C_ORM_ERROR` (1): Generic failure or unmapped driver error.
*   `C_ORM_ALLOC_FAILED`: Memory allocation (e.g., `malloc`, `strdup`) failed.
*   `C_ORM_NOT_FOUND`: No row matched the query (e.g., in `c_orm_find_by_id`).
*   `C_ORM_ALREADY_EXISTS`: Primary key or unique constraint violation during insert/update.
*   `C_ORM_DB_ERROR`: The underlying database driver returned an error.
*   `C_ORM_INVALID_ARGUMENT`: A NULL pointer or malformed argument was passed.
*   `C_ORM_UNSUPPORTED_TYPE`: A struct field type is not supported by the current driver.
*   `C_ORM_BIND_ERROR`: Failed to bind a parameter to a prepared statement.
*   `C_ORM_EXECUTE_ERROR`: Query execution failed.
*   `C_ORM_ROW_LOCK_ERROR`: Failed to acquire a row lock (e.g., `SELECT FOR UPDATE`).
*   `C_ORM_CONCURRENCY_ERROR`: Optimistic lock violation (version mismatch during update).
*   `C_ORM_TYPE_MISMATCH`: Mismatch between database column type and struct field type.

## Best Practices

Always check the return code of `c-orm` operations:

```c
c_orm_error_t err = c_orm_insert(db, &Users_meta, &user);

if (err == C_ORM_ALREADY_EXISTS) {
    printf("User already exists!\n");
} else if (err != C_ORM_OK) {
    printf("Database error occurred: %d\n", err);
}
```
