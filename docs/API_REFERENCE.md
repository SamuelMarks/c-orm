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
c_orm_error_t c_orm_update_optimistic(c_orm_db_t *db, const c_orm_table_meta_t *meta, void *struct_ptr);
```
Issues an update utilizing a version column to prevent concurrent overwrites. Returns `C_ORM_CONCURRENCY_ERROR` if the version has drifted.
