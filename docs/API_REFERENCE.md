# CRUD API Reference

These are the primary CRUD functions exposed in `c_orm_api.h`.

## Core Operations

### `c_orm_insert`
```c
c_orm_error_t c_orm_insert(c_orm_db_t *db, const c_orm_table_meta_t *meta, void *struct_ptr);
```
Inserts a populated struct into the database. Pointers for nullable fields must be valid or NULL.

### `c_orm_update`
```c
c_orm_error_t c_orm_update(c_orm_db_t *db, const c_orm_table_meta_t *meta, void *struct_ptr);
```
Updates an existing record. The struct must contain the populated Primary Key. All fields in the struct (including NULLs if represented) will overwrite database values.

### `c_orm_delete_by_id_int32` / `c_orm_delete_by_id_string`
```c
c_orm_error_t c_orm_delete_by_id_int32(c_orm_db_t *db, const c_orm_table_meta_t *meta, int32_t id);
c_orm_error_t c_orm_delete_by_id_string(c_orm_db_t *db, const c_orm_table_meta_t *meta, const char *id);
```
Deletes a single row matching the provided primary key.

### `c_orm_delete`
```c
c_orm_error_t c_orm_delete(c_orm_db_t *db, const c_orm_table_meta_t *meta, void *struct_ptr);
```
Deletes the row identified by the Primary Key present in the passed struct.

## Read Operations

### `c_orm_find_by_id_int32` / `c_orm_find_by_id_string`
```c
c_orm_error_t c_orm_find_by_id_int32(c_orm_db_t *db, const c_orm_table_meta_t *meta, int32_t id, void *out_struct);
c_orm_error_t c_orm_find_by_id_string(c_orm_db_t *db, const c_orm_table_meta_t *meta, const char *id, void *out_struct);
```
Fetches a single row. Allocates inner dynamic strings/pointers in `out_struct`. You must free the struct using its generated cleanup helper (e.g., `Users_free`).

### `c_orm_find_all`
```c
c_orm_error_t c_orm_find_all(c_orm_db_t *db, const c_orm_table_meta_t *meta, void *out_array_struct);
```
Fetches all rows into an array struct (e.g., `struct Users_Array`). Allocates the underlying array and its elements' inner pointers. Must be freed using the generated array cleanup helper.

## Concurrency

### `c_orm_find_for_update_by_id_int32` / `c_orm_find_for_update_by_id_string`
```c
c_orm_error_t c_orm_find_for_update_by_id_int32(c_orm_db_t *db, const c_orm_table_meta_t *meta, int32_t id, void *out_struct);
```
Fetches a single row and applies a write-lock (`SELECT FOR UPDATE`). Must be executed within an active transaction.

### `c_orm_update_optimistic`
```c
c_orm_error_t c_orm_update_optimistic(c_orm_db_t *db, const c_orm_table_meta_t *meta, const char *version_column_name, const void *in_struct);
```
Updates a record only when the database row's `version_column_name` equals `new_version - 1`, returning `C_ORM_ERROR_NOT_FOUND` on version conflict or missing row.

### `c_orm_pool_init` / `c_orm_pool_acquire` / `c_orm_pool_release` / `c_orm_pool_destroy`
```c
c_orm_error_t c_orm_pool_init(const char *url, int min_conns, int max_conns, int timeout_ms, c_orm_pool_t **out_pool);
c_orm_error_t c_orm_pool_acquire(c_orm_pool_t *pool, c_orm_db_t **out_db);
c_orm_error_t c_orm_pool_release(c_orm_pool_t *pool, c_orm_db_t *db);
c_orm_error_t c_orm_pool_destroy(c_orm_pool_t *pool);
```
Thread-safe session connection pool supporting pre-warmed connections, capacity limits, and acquisition timeouts.

## Unified Connections, Upserts & Decimal Lookups

### `c_orm_connect_url`
```c
c_orm_error_t c_orm_connect_url(const char *url, c_orm_db_t **out_db);
```
Dispatches `sqlite://`, `postgres://` / `postgresql://`, `mysql://` / `mariadb://`, `memory://`, `odbc://`, and `db2://` connection URLs to their respective driver implementations.

### `c_orm_upsert` / `c_orm_upsert_batch`
```c
c_orm_error_t c_orm_upsert(c_orm_db_t *db, const c_orm_table_meta_t *meta, const void *in_struct);
c_orm_error_t c_orm_upsert_batch(c_orm_db_t *db, const c_orm_table_meta_t *meta, const void *in_array, size_t count);
```
Performs dialect-aware atomic upsert (`ON CONFLICT DO UPDATE` on SQLite/PostgreSQL, `ON DUPLICATE KEY UPDATE` on MySQL, and `MERGE INTO ... USING (VALUES (...))` on IBM Db2).

### `c_orm_find_one_by_decimal`
```c
c_orm_error_t c_orm_find_one_by_decimal(c_orm_db_t *db, const c_orm_table_meta_t *meta, const char *col_name, const c_orm_decimal_t *val, void *out_struct);
```
Fetches a single record matching an exact fixed-point `c_orm_decimal_t` column value.

## IBM Db2, Generic ODBC & COBOL Copybook Modules

* **Generic ODBC (`c_orm_odbc.h`)**: `c_orm_odbc_connect`, `c_orm_odbc_escape_conn_value`, `c_orm_odbc_get_sqlstate`, `c_orm_odbc_set_isolation`, `c_orm_odbc_execute_batch`.
* **IBM Db2 (`c_orm_db2.h`)**: `c_orm_db2_connect`, `c_orm_db2_set_client_info`, `c_orm_db2_set_current_schema`, `c_orm_db2_set_isolation`, `c_orm_db2_lob_get_length`, `c_orm_db2_lob_read`, `c_orm_db2_lob_free_locator`.
* **Fixed-Point Decimal & EBCDIC (`c_orm_decimal.h`, `c_orm_ebcdic.h`)**: `c_orm_decimal_from_str`, `c_orm_decimal_to_str`, `c_orm_decimal_add`/`sub`/`mul`/`div`/`rescale`, `c_orm_decimal_to_comp3`/`from_comp3`, `c_orm_decimal_to_zoned`/`from_zoned`, `c_orm_ebcdic_to_utf8`, `c_orm_utf8_to_ebcdic`, `c_orm_utf8_to_utf16`, `c_orm_utf16_to_utf8`.
* **COBOL Copybooks (`c_orm_copybook.h`)**: `c_orm_copybook_parse_string`, `c_orm_copybook_parse_file`, `c_orm_copybook_pack_struct`, `c_orm_copybook_unpack_struct`, `c_orm_copybook_validate_88`, `c_orm_copybook_emit_c_header`, `c_orm_copybook_emit_c_source`, `c_orm_copybook_emit_sql_ddl`, `c_orm_sql_to_copybook_emit`.
