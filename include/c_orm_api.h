#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file c_orm_api.h
 * @brief High-level API for c-orm: find, insert, update, delete.
 */

#ifndef C_ORM_API_H
#define C_ORM_API_H

/* clang-format off */
#include "c_orm_db.h"
#include "c_orm_meta.h"
#include "abstract_struct.h"
/* clang-format on */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @brief Implement runtime validation wrapping cdd-c dynamic validation rules
 * (Steps 154-156).
 *
 * @param meta Table metadata.
 * @param obj Struct instance to validate.
 * @return C_ORM_OK on success, C_ORM_ERROR_VALIDATION if validation fails.
 */
C_ORM_EXPORT c_orm_error_t c_orm_validate(const c_orm_table_meta_t *meta,
                                          const void *obj);

/**
 * @brief Find a single record by its primary key, and eager-load a specific
 * relationship via SQL JOIN.
 *
 * @param db Database connection.
 * @param meta Table metadata for the parent struct.
 * @param id_val Primary key value of the parent struct.
 * @param relation_name The name of the relation field to eager load.
 * @param out_struct Pointer to the parent struct to hydrate.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_find_with_relation_int32(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, int32_t id_val,
    const char *relation_name, void *out_struct);

/**
 * @brief Find a single record by its primary key, and eager-load multiple
 * nested relationships.
 *
 * @param db Database connection.
 * @param meta Table metadata for the parent struct.
 * @param id_val Primary key value of the parent struct.
 * @param relation_paths An array of dot-separated relationship paths (e.g.
 * "posts.comments").
 * @param num_paths Number of paths in the array.
 * @param out_struct Pointer to the parent struct to hydrate.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_find_with_relations_int32(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, int32_t id_val,
    const char **relation_paths, size_t num_paths, void *out_struct);

/**
 * @brief Find all records, and eager-load a specific relationship via SQL JOIN.
 * Deduplicates parent records.
 *
 * @param db Database connection.
 * @param meta Table metadata for the parent struct.
 * @param relation_name The name of the relation field to eager load.
 * @param out_array Pointer to an array of parent structs to hydrate.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_find_all_with_relation(c_orm_db_t *db, const c_orm_table_meta_t *meta,
                             const char *relation_name, void *out_array);

/**
 * @brief Find all records, and eager-load multiple nested relationships.
 *
 * @param db Database connection.
 * @param meta Table metadata for the parent struct.
 * @param relation_paths An array of dot-separated relationship paths (e.g.
 * "posts.comments").
 * @param num_paths Number of paths in the array.
 * @param out_array Pointer to an array of parent structs to hydrate.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_find_all_with_relations(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, const char **relation_paths,
    size_t num_paths, void *out_array);

/**
 * @brief Find a single record by its primary key.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param id_val Primary key value to search for (assumes string/int is passed
 * appropriately, but currently expects int32 for basic testing, we can pass as
 * void*). For safety we will pass as int32_t for now.
 * @param out_struct Pointer to an already allocated struct to hydrate.
 * @return C_ORM_OK on success, C_ORM_ERROR_NOT_FOUND if no row.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_find_by_id_int32(c_orm_db_t *db, const c_orm_table_meta_t *meta,
                       int32_t id_val, void *out_struct);

/**
 * @brief Find a single record by a composite primary key.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param num_keys Number of primary key components.
 * @param key_values Array of CddCVariant structures representing the key
 * values.
 * @param out_struct Pointer to an already allocated struct to hydrate.
 * @return C_ORM_OK on success, C_ORM_ERROR_NOT_FOUND if no row.
 */
C_ORM_EXPORT c_orm_error_t c_orm_find_by_composite_key(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, size_t num_keys,
    const struct CddCVariant *key_values, void *out_struct);

/**
 * @brief Update a record by a composite primary key.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param num_keys Number of primary key components.
 * @param key_values Array of CddCVariant structures representing the key
 * values.
 * @param in_struct Pointer to the struct containing the updated data.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_update_by_composite_key(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, size_t num_keys,
    const struct CddCVariant *key_values, const void *in_struct);

/**
 * @brief Delete a record by a composite primary key.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param num_keys Number of primary key components.
 * @param key_values Array of CddCVariant structures representing the key
 * values.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_delete_by_composite_key(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, size_t num_keys,
    const struct CddCVariant *key_values);

/**
 * @brief Find all records.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param out_array Pointer to the generic Array struct. Data will be allocated
 * dynamically.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_find_all(c_orm_db_t *db,
                                          const c_orm_table_meta_t *meta,
                                          void *out_array);

/**
 * @brief Insert a new record into the database.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param in_struct Pointer to the struct containing data to insert.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_insert(c_orm_db_t *db,
                                        const c_orm_table_meta_t *meta,
                                        const void *in_struct);

/**
 * @brief Insert an array of records into the database in bulk.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param in_array Pointer to an array of structs containing data to insert.
 * @param num_items The total number of structs in the array.
 * @param chunk_size The number of structs to process per SQL query. 0 to
 * auto-calculate.
 * @return C_ORM_OK on success.
 */
typedef enum {
  C_ORM_ON_CONFLICT_FAIL = 0,
  C_ORM_ON_CONFLICT_DO_NOTHING = 1,
  C_ORM_ON_CONFLICT_DO_UPDATE = 2
} c_orm_on_conflict_t;

typedef void (*c_orm_batch_progress_cb)(size_t processed, size_t total,
                                        void *ctx);

/**
 * @brief Insert an array of records into the database in bulk with advanced
 * options.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param in_array Pointer to an array of structs containing data to insert.
 * @param num_items The total number of structs in the array.
 * @param chunk_size The number of structs to process per SQL query. 0 to
 * auto-calculate.
 * @param conflict_policy Conflict resolution policy (e.g. UPSERT).
 * @param progress_cb Callback for progress reporting. Can be NULL.
 * @param progress_ctx Context passed to progress callback.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_insert_batch_ext(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, const void *in_array,
    size_t num_items, size_t chunk_size, c_orm_on_conflict_t conflict_policy,
    c_orm_batch_progress_cb progress_cb, void *progress_ctx);

C_ORM_EXPORT c_orm_error_t c_orm_insert_batch(c_orm_db_t *db,
                                              const c_orm_table_meta_t *meta,
                                              const void *in_array,
                                              size_t num_items,
                                              size_t chunk_size);

struct c_orm_iterator;

/**
 * @brief Initialize an iterator to fetch a large number of rows in batches.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param sql The custom query or NULL to find all.
 * @param chunk_size The maximum number of structs to fetch per next() call.
 * @param out_iter Pointer to receive the initialized iterator.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_find_batch_init(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, const char *sql,
    size_t chunk_size, struct c_orm_iterator **out_iter);

/**
 * @brief Fetch the next chunk of rows into the provided array.
 *
 * @param iter The active iterator.
 * @param out_array Pointer to a pre-allocated array of structs capable of
 * holding chunk_size items.
 * @param out_num_fetched Pointer to receive the number of rows actually fetched
 * (0 if EOF).
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_iterator_next(struct c_orm_iterator *iter,
                                               void *out_array,
                                               size_t *out_num_fetched);

/**
 * @brief Close and free the iterator.
 *
 * @param iter The iterator to free.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_iterator_close(struct c_orm_iterator *iter);

/**
 * @brief Update an array of records in the database in bulk.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param in_array Pointer to an array of structs containing data to update.
 * @param num_items The total number of structs in the array.
 * @param chunk_size The number of structs to process per SQL query. 0 to
 * auto-calculate.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_update_batch(c_orm_db_t *db,
                                              const c_orm_table_meta_t *meta,
                                              const void *in_array,
                                              size_t num_items,
                                              size_t chunk_size);

/**
 * @brief Delete records matching the PKs in the given array in bulk.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param in_array Pointer to an array of structs containing the PKs to delete.
 * @param num_items The total number of structs in the array.
 * @param chunk_size The number of structs to process per SQL query. 0 to
 * auto-calculate.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_delete_batch(c_orm_db_t *db,
                                              const c_orm_table_meta_t *meta,
                                              const void *in_array,
                                              size_t num_items,
                                              size_t chunk_size);
/**
 * @brief Save a record to the database via insert or update depending on PK
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_save(c_orm_db_t *db,
                                      const c_orm_table_meta_t *meta,
                                      const void *in_struct);
/**
 * @brief Update an existing record in the database by its primary key.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param in_struct Pointer to the struct containing the updated data (and PK).
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_update(c_orm_db_t *db,
                                        const c_orm_table_meta_t *meta,
                                        const void *in_struct);

/**
 * @brief Delete a record from the database using a struct instance.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param in_struct Pointer to the struct containing data to delete.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_delete(c_orm_db_t *db,
                                        const c_orm_table_meta_t *meta,
                                        const void *in_struct);

/**
 * @brief Delete a record from the database by its primary key.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param id_val Primary key value to delete.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_delete_by_id_int32(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, int32_t id_val);

/**
 * @brief Delete a record from the database by its string primary key.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param id_val Primary key string to delete.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_delete_by_id_string(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, const char *id_val);

/**
 * @brief Partially update an object in the database.
 * @param db Database handle.
 * @param meta Table metadata.
 * @param obj Object containing updated values and ID.
 * @param fields Array of column names to update.
 * @param num_fields Number of columns in fields array.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_update_partial(c_orm_db_t *db,
                                                const c_orm_table_meta_t *meta,
                                                const void *obj,
                                                const char **fields,
                                                size_t num_fields);

/**
 * @brief Check if an object exists by INT32 ID.
 * @param db Database handle.
 * @param meta Table metadata.
 * @param id The ID.
 * @param out_exists Output boolean (1 = true, 0 = false).
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_exists_int32(c_orm_db_t *db,
                                              const c_orm_table_meta_t *meta,
                                              int32_t id, int *out_exists);

/**
 * @brief Check if an object exists by STRING ID.
 * @param db Database handle.
 * @param meta Table metadata.
 * @param id The ID.
 * @param out_exists Output boolean.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_exists_string(c_orm_db_t *db,
                                               const c_orm_table_meta_t *meta,
                                               const char *id, int *out_exists);

/**
 * @brief Find all objects, paginated.
 * @param db Database handle.
 * @param meta Table metadata.
 * @param limit Max objects to return.
 * @param offset Number of objects to skip.
 * @param out_array Output array.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_find_all_paginated(c_orm_db_t *db, const c_orm_table_meta_t *meta,
                         void *out_array, size_t limit, size_t offset);

/**
 * @brief Delete all objects from a table.
 * @param db Database handle.
 * @param meta Table metadata.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_delete_all(c_orm_db_t *db,
                                            const c_orm_table_meta_t *meta);

/**
 * @brief Find a single record by its string primary key.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param id_val Primary key value to search for.
 * @param out_struct Pointer to an already allocated struct to hydrate.
 * @return C_ORM_OK on success, C_ORM_ERROR_NOT_FOUND if no row.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_find_by_id_string(c_orm_db_t *db, const c_orm_table_meta_t *meta,
                        const char *id_val, void *out_struct);

/**
 * @brief Find a single row by string primary key and apply row-level locking.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param id_val Primary key value.
 * @param out_struct Output struct.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_find_for_update_by_id_string(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, const char *id_val,
    void *out_struct);

/**
 * @brief Implement pessimistic locking APIs (SELECT ... FOR UPDATE) for integer
 * PK.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param id_val Primary key value.
 * @param out_struct Output struct.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_find_for_update_by_id_int32(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, int32_t id_val,
    void *out_struct);

/**
 * @brief Update an existing record in the database utilizing optimistic locking
 * via version column.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param version_column_name Name of the column tracking the version.
 * @param in_struct Pointer to the struct containing the updated data (and PK).
 * @return C_ORM_OK on success, C_ORM_ERROR_NOT_FOUND on version mismatch.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_update_optimistic(c_orm_db_t *db, const c_orm_table_meta_t *meta,
                        const char *version_column_name, const void *in_struct);
/**
 * @brief Find a single record by a specific string column.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param column_name The column to filter by.
 * @param value The value to search for.
 * @param out_struct Pointer to an already allocated struct to hydrate.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_find_one_by_string(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, const char *column_name,
    const char *value, void *out_struct);

/**
 * @brief Hydrate an array from an already prepared and optionally bound query.
 *
 * @param db Database connection.
 * @param query Prepared and optionally bound query.
 * @param meta Table metadata.
 * @param out_array Pointer to the generic Array struct.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_hydrate_all(c_orm_db_t *db,
                                             c_orm_query_t *query,
                                             const c_orm_table_meta_t *meta,
                                             void *out_array);

/**
 * @brief Hydrate a struct from a row in a query, starting at a specific column
 index.
  *
  * @param db Database connection.
  * @param query Prepared and optionally bound query.
  * @param meta Table metadata.
  * @param out_struct Pointer to an already allocated struct to hydrate.
  * @param start_col The column index to start reading from.
  * @return C_ORM_OK on success.
  */
C_ORM_EXPORT c_orm_error_t c_orm_hydrate_row_from(
    c_orm_db_t *db, c_orm_query_t *query, const c_orm_table_meta_t *meta,
    void *out_struct, size_t start_col);

/**
 * @brief Hydrate a single record from a prepared query into the struct.
 *
 * @param db Database connection.
 * @param query Prepared and optionally bound query.
 * @param meta Table metadata.
 * @param out_struct Pointer to an already allocated struct to hydrate.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_hydrate_row(c_orm_db_t *db,
                                             c_orm_query_t *query,
                                             const c_orm_table_meta_t *meta,
                                             void *out_struct);

/**
 * @brief Function to detect and resolve N+1 query scenarios during iteration
 *        by aggregating foreign keys into a single bulk IN clause dynamically.
 *
 * @param db Database connection.
 * @param array Pointer to the array to inspect for unhydrated relations.
 * @param meta The table metadata containing relation offsets.
 * @param target_relation The specific relation index to resolve via bulk IN
 * query.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_resolve_n_plus_one(
    c_orm_db_t *db, void *array, const c_orm_table_meta_t *meta,
    size_t target_relation);

/**
 * @brief Implement row caching in c_orm_identity_map_t during hydration
 *
 * @param db Database connection handling hydration.
 * @param meta Table metadata for the row being cached.
 * @param hydrated_row The raw generated struct pointer just hydrated.
 * @param out_cached_row Returns the pointer to the unique instance to use
 * (aliased).
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_hydrate_cache_row(c_orm_db_t *db, const c_orm_table_meta_t *meta,
                        void *hydrated_row, void **out_cached_row);
/**
 * @brief Execute a raw query string that returns no results.
 */
C_ORM_EXPORT c_orm_error_t c_orm_execute_raw(c_orm_db_t *db, const char *sql);

/* Transaction APIs */
C_ORM_EXPORT c_orm_error_t c_orm_transaction_begin(c_orm_db_t *db);
C_ORM_EXPORT c_orm_error_t c_orm_transaction_commit(c_orm_db_t *db);
C_ORM_EXPORT c_orm_error_t c_orm_transaction_rollback(c_orm_db_t *db);

/**
 * @brief Create a savepoint within an active transaction.
 *
 * @param db Database connection.
 * @param savepoint_name Name of the savepoint.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_savepoint_create(c_orm_db_t *db,
                                                  const char *savepoint_name);

/**
 * @brief Rollback to a specific savepoint.
 *
 * @param db Database connection.
 * @param savepoint_name Name of the savepoint.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_savepoint_rollback(c_orm_db_t *db,
                                                    const char *savepoint_name);

/**
 * @brief Release a savepoint.
 *
 * @param db Database connection.
 * @param savepoint_name Name of the savepoint.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_savepoint_release(c_orm_db_t *db,
                                                   const char *savepoint_name);

/**
 * @brief Registers default hooks on the given table metadata to automatically
 * set 'updated_at' to the current timestamp on update, and 'created_at' on
 * insert.
 *
 * @param meta Table metadata to modify (must be mutable before first use).
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_register_timestamp_hooks(c_orm_table_meta_t *meta);

/**
 * @brief Registers a soft-delete hook on the given table metadata.
 * Instead of deleting the row, it will update 'deleted_at' to the current
 * timestamp.
 *
 * @param meta Table metadata to modify.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_register_soft_delete_hook(c_orm_table_meta_t *meta);

/**
 * @brief Implement validation logic for recursive relationship definitions
 * across multiple tables.
 *
 * @param tables Array of table metadata pointers.
 * @param num_tables Number of tables in the array.
 * @return C_ORM_OK on success, C_ORM_ERROR_RECURSION if a cyclic dependency is
 * detected.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_validate_relations(const c_orm_table_meta_t **tables, size_t num_tables);

struct sql_table_t;

/**
 * @brief Implement c_orm integration layer to parse FOREIGN KEY constraints
 * into c_orm_relation_meta_t via cdd-c AST.
 *
 * @param sql_table Parsed table AST from cdd-c.
 * @param out_relations Pointer to array of relation metadata to populate.
 * @param out_num_relations Number of relations found and populated.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_build_relation_meta(
    const struct sql_table_t *sql_table, c_orm_relation_meta_t **out_relations,
    size_t *out_num_relations);

struct CddCAbstractStructArray;
struct CddCAbstractStruct;
struct CddCVariant;

/**
 * @brief Implement dynamic struct field accessor wrapping cdd-c reflection
 * (c_orm_get_field_value)
 *
 * @param meta Table metadata.
 * @param obj Struct instance.
 * @param field_name Name of the field.
 * @param out_variant The parsed value from the struct.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_get_field_value(const c_orm_table_meta_t *meta, const void *obj,
                      const char *field_name, struct CddCVariant *out_variant);

/**
 * @brief Implement dynamic struct field mutator wrapping cdd-c reflection
 * (c_orm_set_field_value)
 *
 * @param meta Table metadata.
 * @param obj Struct instance.
 * @param field_name Name of the field.
 * @param in_variant The parsed value to apply to the struct.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_set_field_value(
    const c_orm_table_meta_t *meta, void *obj, const char *field_name,
    const struct CddCVariant *in_variant);

/**
 * @brief Implement fallback routing to cdd_c_abstract_struct_t if specific
 * struct is absent
 *
 * @param db Database connection.
 * @param query Compiled query object.
 * @param out_array Pointer to cdd_c_abstract_struct_array_t to populate.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_hydrate_abstract_all(c_orm_db_t *db, c_orm_query_t *query,
                           struct CddCAbstractStructArray *out_array);

/**
 * @brief Map custom SQL to an existing specific struct array.
 *
 * @param db Database connection.
 * @param sql Raw SQL query.
 * @param meta Table metadata.
 * @param out_array Pointer to generic struct array block.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_select_raw(c_orm_db_t *db, const char *sql,
                                            const c_orm_table_meta_t *meta,
                                            void *out_array);

/**
 * @brief Execute query and return abstract dynamic rows.
 *
 * @param db Database connection.
 * @param sql Raw SQL query.
 * @param out_array Pointer to cdd_c_abstract_struct_array_t to populate.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_find_all_abstract(
    c_orm_db_t *db, const char *sql, struct CddCAbstractStructArray *out_array);

/**
 * @brief Free an abstract array generated by find_all_abstract.
 *
 * @param arr The array.
 */
C_ORM_EXPORT void c_orm_abstract_free(struct CddCAbstractStructArray *arr);

/**
 * @brief Implement c_orm_to_json serializer handling specific structs (Step
 * 147).
 *
 * @param meta Table metadata.
 * @param obj Struct instance.
 * @param out_json Pointer to receive the allocated JSON string.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_to_json(const c_orm_table_meta_t *meta,
                                         const void *obj, char **out_json);

/**
 * @brief Implement c_orm_from_json deserializer for specific structs (Step
 * 148).
 *
 * @param meta Table metadata.
 * @param json The JSON string.
 * @param out_obj Pointer to the uninitialized/allocated target struct.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_from_json(const c_orm_table_meta_t *meta,
                                           const char *json, void *out_obj);

/**
 * @brief Implement c_orm_to_dict (hashmap) representation (Step 152).
 *
 * @param meta Table metadata.
 * @param obj Struct instance.
 * @param out_dict Pointer to the target abstract struct dictionary.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_to_dict(const c_orm_table_meta_t *meta,
                                         const void *obj,
                                         struct CddCAbstractStruct *out_dict);

/**
 * @brief Implement c_orm_from_dict (Step 153).
 *
 * @param meta Table metadata.
 * @param in_dict The source abstract struct dictionary.
 * @param out_obj Pointer to the target struct.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_from_dict(const c_orm_table_meta_t *meta,
                const struct CddCAbstractStruct *in_dict, void *out_obj);

/**
 * @brief Implement mapping from abstract struct to JSON for dynamic use cases.
 *
 * @param astruct The abstract struct.
 * @param out_json Pointer to receive the allocated JSON string.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_abstract_to_json(
    const struct CddCAbstractStruct *astruct, char **out_json);

/**
 * @brief Implement mapping from JSON back to abstract struct for dynamic use
 * cases.
 *
 * @param json The JSON string.
 * @param out_astruct Pointer to the initialized abstract struct.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_abstract_from_json(
    const char *json, struct CddCAbstractStruct *out_astruct);

struct cdd_c_meta;

/**
 * @brief Implement deep free logic utilizing cdd-c nested struct traversals.
 *
 * @param meta The reflection metadata representing the struct type.
 * @param obj The structure to recursively free. Does not free `obj` itself,
 * only its allocations.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_deep_free(const struct cdd_c_meta *meta,
                                           void *obj);

/**
 * @brief Implement deep copy logic utilizing cdd-c nested struct traversals.
 *
 * @param meta The reflection metadata representing the struct type.
 * @param dest The destination structure (must be pre-allocated).
 * @param src The source structure to copy from.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_deep_copy(const struct cdd_c_meta *meta,
                                           void *dest, const void *src);

/**
 * @brief Initialize an identity map.
 * @param map Pointer to the identity map.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_identity_map_init(c_orm_identity_map_t *map);

/**
 * @brief Opaque connection pool handle.
 */
typedef struct c_orm_pool c_orm_pool_t;

/**
 * @brief Initializes a thread-safe connection pool with wait timeouts and retry
 * logic.
 *
 * @param url Connection string.
 * @param min_conns Minimum number of connections to keep alive.
 * @param max_conns Maximum number of connections.
 * @param timeout_ms Milliseconds to wait before failing to acquire a
 * connection.
 * @param out_pool Pointer to receive the initialized pool.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_pool_init(const char *url, int min_conns,
                                           int max_conns, int timeout_ms,
                                           c_orm_pool_t **out_pool);

/**
 * @brief Acquire a session-based connection from the pool.
 *
 * @param pool The connection pool.
 * @param out_db Pointer to receive the acquired db connection session.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_pool_acquire(c_orm_pool_t *pool,
                                              c_orm_db_t **out_db);

/**
 * @brief Safely return a session-based connection to the pool.
 *
 * @param pool The connection pool.
 * @param db The connection to release.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_pool_release(c_orm_pool_t *pool,
                                              c_orm_db_t *db);

/**
 * @brief Destroy the connection pool.
 *
 * @param pool The pool to destroy.
 */
C_ORM_EXPORT void c_orm_pool_destroy(c_orm_pool_t *pool);

/**
 * @brief Interceptor hooks for plugin architecture (Steps 194-199).
 *
 * Provides a standard callback interface injected before and after SQL queries.
 */

/**
 * @brief Register a query interceptor plugin (Step 195).
 *
 * Useful for SQL query logging (Step 197) and profiling (Step 198).
 *
 * @param db Database connection.
 * @param hook The interceptor callback.
 * @param context Opaque user data for the plugin.
 */
C_ORM_EXPORT c_orm_error_t c_orm_register_query_interceptor(
    c_orm_db_t *db, c_orm_interceptor_cb hook, void *context);

/**
 * @brief Register a hydration interceptor plugin (Step 196).
 *
 * @param db Database connection.
 * @param hook The interceptor callback.
 * @param context Opaque user data for the plugin.
 */
C_ORM_EXPORT c_orm_error_t c_orm_register_hydration_interceptor(
    c_orm_db_t *db, c_orm_interceptor_cb hook, void *context);

/**
 * @brief Async execution wrappers for libuv integration (Steps 207-210).
 *
 * Asynchronous insertion stub simulating an event loop queue.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param in_struct Pointer to data.
 * @param cb Callback function executed upon completion.
 * @param ctx User context for callback.
 * @return C_ORM_OK if queued successfully.
 */
C_ORM_EXPORT c_orm_error_t c_orm_insert_async(c_orm_db_t *db,
                                              const c_orm_table_meta_t *meta,
                                              const void *in_struct,
                                              void (*cb)(c_orm_error_t, void *),
                                              void *ctx);

/**
 * @brief Asynchronous fetch all stub (Step 209).
 */
C_ORM_EXPORT c_orm_error_t c_orm_find_all_async(
    c_orm_db_t *db, const c_orm_table_meta_t *meta, void *out_array,
    void (*cb)(c_orm_error_t, void *), void *ctx);

/**
 * @brief Hydrate a row, leveraging the cdd-c hydrate_router.
 *
 * Checks the db's hydrate_router for a specific compiled struct mapping
 * based on the query, and routes to it. Falls back to abstract struct
 * if no specific mapping is available.
 *
 * @param db Database connection.
 * @param query Compiled query object.
 * @param query_hash The query's hash for routing.
 * @param out_struct Pointer to allocated output struct or abstract struct.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_hydrate_routed(c_orm_db_t *db,
                                                c_orm_query_t *query,
                                                c_orm_uint64_t query_hash,
                                                void *out_struct);

/**
 * @brief Free an identity map.
 * @param map Pointer to the identity map.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_identity_map_free(c_orm_identity_map_t *map);

/**
 * @brief Attach an identity map to a database connection or session for
 * caching.
 * @param db Pointer to the database connection.
 * @param map Pointer to the identity map.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_db_attach_identity_map(c_orm_db_t *db, c_orm_identity_map_t *map);

/**
 * @brief Add or retrieve an object from the identity map by integer PK.
 * @param map Pointer to the identity map.
 * @param table Pointer to the table metadata.
 * @param pk_int The integer primary key.
 * @param object_ptr The object to store (if not found).
 * @param out_object Pointer to receive the cached object.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_identity_map_get_or_set_int(
    c_orm_identity_map_t *map, const c_orm_table_meta_t *table, int32_t pk_int,
    void *object_ptr, void **out_object);

/**
 * @brief Add or retrieve an object from the identity map by string PK.
 * @param map Pointer to the identity map.
 * @param table Pointer to the table metadata.
 * @param pk_str The string primary key.
 * @param object_ptr The object to store (if not found).
 * @param out_object Pointer to receive the cached object.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_identity_map_get_or_set_str(
    c_orm_identity_map_t *map, const c_orm_table_meta_t *table,
    const char *pk_str, void *object_ptr, void **out_object);

/**
 * @brief Manually trigger lazy loading for a specific relation.
 *
 * @param db Database connection.
 * @param obj The object containing the lazy load context.
 * @param meta Metadata for the table containing the relation.
 * @param target_relation Index of the relation to load.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_load_relation(c_orm_db_t *db, void *obj,
                                               const c_orm_table_meta_t *meta,
                                               size_t target_relation);

/**
 * @brief Manually trigger paginated lazy loading for a specific relation.
 *
 * @param db Database connection.
 * @param obj The object containing the lazy load context.
 * @param meta Metadata for the table containing the relation.
 * @param target_relation Index of the relation to load.
 * @param limit Maximum rows to return (0 for unlimited).
 * @param offset Number of rows to skip.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_load_relation_ext(
    c_orm_db_t *db, void *obj, const c_orm_table_meta_t *meta,
    size_t target_relation, size_t limit, size_t offset);

/**
 * @brief Proxy macro to automatically lazy load a specific struct relationship
 * seamlessly.
 *
 * Evaluates whether the specific relation logic has been fully loaded,
 * requesting the native backend to query it immediately if uninitialized.
 *
 * @param DB Database connection instance.
 * @param OBJ Pointer to the struct being referenced.
 * @param META Pointer to table metadata `c_orm_table_meta_t` defining the
 * table.
 * @param REL_INDEX Numeric index mapping the relation inside `META`.
 * @param PTR_VAR Target specific pointer inside the struct mapping to the
 * relation.
 */
#define C_ORM_LAZY_LOAD(DB, OBJ, META, REL_INDEX, PTR_VAR)                     \
  do {                                                                         \
    if (!(OBJ)->PTR_VAR) {                                                     \
      c_orm_load_relation((DB), (OBJ), (META), (REL_INDEX));                   \
    }                                                                          \
  } while (0)

/**
 * @brief Add support for SQLite specific PRAGMAs via ORM config (Step 200).
 *
 * Executed automatically during connection initialization if provided.
 *
 * @param db Database connection.
 * @param pragma_string Raw PRAGMA statement to execute (e.g. `PRAGMA
 * foreign_keys = ON;`).
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_config_sqlite_pragma(c_orm_db_t *db, const char *pragma_string);

/**
 * @brief Add support for Postgres specific SET statements via ORM config (Step
 * 201).
 *
 * @param db Database connection.
 * @param set_string Raw SET statement (e.g. `SET timezone = 'UTC';`).
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_config_postgres_set(c_orm_db_t *db,
                                                     const char *set_string);

/**
 * @brief Recursively free dynamically allocated memory associated with loaded
 * relationships.
 *
 * Handles cleaning up nested lazy/eager loaded elements ensuring no memory
 * leaks occur. Does not free the root `obj` pointer itself, nor does it free
 * basic string columns.
 *
 * @param meta Metadata for the table containing the relations.
 * @param obj The pointer to the structure.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_free_relations(const c_orm_table_meta_t *meta,
                                                void *obj);

/**
 * @brief Add support for MySQL specific session variables via ORM config (Step
 * 202).
 *
 * @param db Database connection.
 * @param session_var_string Raw session string (e.g. `SET SESSION sql_mode =
 * 'STRICT_ALL_TABLES';`).
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_config_mysql_session(c_orm_db_t *db, const char *session_var_string);

/**
 * @brief Opaque shard manager context.
 */
typedef struct c_orm_shard_manager c_orm_shard_manager_t;

/**
 * @brief Initialize table partitioning helpers and sharding support (Steps 203,
 * 204).
 *
 * @param num_shards Total number of database shards configured.
 * @param out_manager Pointer to receive initialized manager instance.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_shard_manager_init(
    size_t num_shards, c_orm_shard_manager_t **out_manager);

/**
 * @brief Bind a database connection (node) to a shard index.
 *
 * @param manager The shard manager.
 * @param index Shard index (0 to num_shards - 1).
 * @param node Live connection to the specific database instance.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_shard_manager_add_node(
    c_orm_shard_manager_t *manager, size_t index, c_orm_db_t *node);

/**
 * @brief Implement hash-based shard routing algorithm (Step 205).
 *
 * @param manager The shard manager.
 * @param routing_key String key to hash across available shards.
 * @param out_node Pointer to receive the specific db connection to execute
 * against.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_shard_route_hash(c_orm_shard_manager_t *manager, const char *routing_key,
                       c_orm_db_t **out_node);

/**
 * @brief Free resources linked to the shard manager.
 *
 * @param manager The shard manager.
 */
C_ORM_EXPORT void c_orm_shard_manager_free(c_orm_shard_manager_t *manager);

/**
 * @brief Execute a scatter-gather find_all query across all shards in parallel.
 *
 * @param manager The shard manager.
 * @param meta The table metadata to query.
 * @param out_array Pointer to a void* to receive the dynamically allocated
 * combined results array.
 * @param out_count Pointer to receive the total number of combined results.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_scatter_gather_generic(
    c_orm_shard_manager_t *manager, const c_orm_table_meta_t *meta,
    void **out_array, size_t *out_count);

/**
 * @brief Escapes a string to prevent SQL injection vulnerabilities (Steps 241,
 * 242).
 *
 * Designed to sanitize input buffers bound dynamically into abstract struct
 * mapping pipelines when parameterized bindings are unavailable (e.g., dynamic
 * IN clause array construction).
 *
 * @param db Database connection handling dialect-specific escaping rules.
 * @param input Raw string to sanitize.
 * @param output Pre-allocated buffer to store escaped string.
 * @param output_size Size of the pre-allocated output buffer.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY if output buffer is too
 * small.
 */
C_ORM_EXPORT c_orm_error_t c_orm_escape_string(c_orm_db_t *db,
                                               const char *input, char *output,
                                               size_t output_size);

/**
 * @brief Implement prepared statement caching at the session level (Steps 243,
 * 244).
 *
 * Eviction policy implements LRU logic wrapping active `c_orm_query_t`
 * structures locally bound to the `c_orm_db_t` handle.
 *
 * @param db Database connection.
 * @param cache_size Number of statements to cache.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_enable_statement_caching(c_orm_db_t *db,
                                                          size_t cache_size);

/**
 * @brief Disable statement caching and free resources.
 *
 * @param db Database connection.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_disable_statement_caching(c_orm_db_t *db);

/**
 * @brief Performs a lazy load for a specific relationship on a given object.
 *
 * @param db Database connection.
 * @param parent_meta Metadata for the parent table/struct.
 * @param parent_obj Pointer to the parent object containing the relationship
 * proxy.
 * @param relation_name The name of the relationship field to load.
 * @return C_ORM_OK on success, C_ORM_ERROR_NOT_FOUND if the target doesn't
 * exist.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_lazy_load(c_orm_db_t *db, const c_orm_table_meta_t *parent_meta,
                void *parent_obj, const char *relation_name);

/**
 * @brief Performs a paginated lazy load for a specific relationship.
 *
 * Appends LIMIT and OFFSET specifically useful for HasMany and ManyToMany.
 *
 * @param db Database connection.
 * @param parent_meta Metadata for the parent table/struct.
 * @param parent_obj Pointer to the parent object containing the relationship.
 * @param relation_name The name of the relationship field to load.
 * @param limit Maximum number of records to return.
 * @param offset Number of records to skip.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_lazy_load_paginated(
    c_orm_db_t *db, const c_orm_table_meta_t *parent_meta, void *parent_obj,
    const char *relation_name, size_t limit, size_t offset);

/**
 * @brief Attach a child object to a parent object's relationship.
 *
 * Supports One-to-Many and Many-to-Many.
 *
 * @param db Database connection.
 * @param parent_meta Metadata for the parent table/struct.
 * @param parent_obj Pointer to the parent object.
 * @param relation_name The name of the relationship field.
 * @param child_obj Pointer to the child object.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_attach(c_orm_db_t *db,
                                        const c_orm_table_meta_t *parent_meta,
                                        void *parent_obj,
                                        const char *relation_name,
                                        void *child_obj);

/**
 * @brief Detach a child object from a parent object's relationship.
 *
 * Supports One-to-Many and Many-to-Many.
 *
 * @param db Database connection.
 * @param parent_meta Metadata for the parent table/struct.
 * @param parent_obj Pointer to the parent object.
 * @param relation_name The name of the relationship field.
 * @param child_obj Pointer to the child object.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_detach(c_orm_db_t *db,
                                        const c_orm_table_meta_t *parent_meta,
                                        void *parent_obj,
                                        const char *relation_name,
                                        void *child_obj);

/**
 * @brief Sync a parent object's relationship with an array of children.
 *
 * Replaces the entire set of related children for Many-to-Many and One-to-Many.
 *
 * @param db Database connection.
 * @param parent_meta Metadata for the parent table/struct.
 * @param parent_obj Pointer to the parent object.
 * @param relation_name The name of the relationship field.
 * @param children_array Pointer to the contiguous array of children structs.
 * @param num_children The number of children in the array.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_sync(
    c_orm_db_t *db, const c_orm_table_meta_t *parent_meta, void *parent_obj,
    const char *relation_name, void *children_array, size_t num_children);

/**
 * @brief Prepare a statement, potentially pulling from cache.
 *
 * @param db Database connection.
 * @param sql SQL string.
 * @param out_query Pointer to receive the query handle.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_prepare_cached(c_orm_db_t *db, const char *sql,
                                                c_orm_query_t **out_query);

/**
 * @brief Finalize a statement or return it to the cache.
 *
 * @param db Database connection.
 * @param query Query handle.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_finalize_cached(c_orm_db_t *db,
                                                 c_orm_query_t *query);

/**
 * @name Generic CRUD Backend
 * @{
 */

/**
 * @brief Generic dynamically constructed insert.
 */
C_ORM_EXPORT c_orm_error_t c_orm_insert_generic(c_orm_db_t *db,
                                                const c_orm_table_meta_t *meta,
                                                const void *ptr);

/**
 * @brief Generic dynamically constructed get by int32 PK.
 */
C_ORM_EXPORT c_orm_error_t c_orm_get_generic(c_orm_db_t *db,
                                             const c_orm_table_meta_t *meta,
                                             int32_t pk_val, void *out_struct);

/**
 * @brief Generic dynamically constructed find_all with array allocation.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_find_all_generic(c_orm_db_t *db, const c_orm_table_meta_t *meta,
                       void **out_array, size_t *out_count);

/**
 * @brief Generic dynamically constructed get by string PK.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_get_generic_string(c_orm_db_t *db, const c_orm_table_meta_t *meta,
                         const char *pk_val, void *out_struct);

/** @} */

#ifdef __EMSCRIPTEN__
/**
 * @brief Emscripten specific initialization hook to mount and sync IDBFS.
 * @param callback Callback function to invoke when syncfs completes (receives
 * an error code, 0 on success).
 */
C_ORM_EXPORT void c_orm_wasm_init_fs(void (*callback)(int));
#endif

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* C_ORM_API_H */

#if defined(__clang__) || defined(__GNUC__)
#endif
