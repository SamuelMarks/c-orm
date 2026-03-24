/**
 * @file c_orm_api.h
 * @brief High-level API for c-orm: find, insert, update, delete.
 */

#ifndef C_ORM_API_H
#define C_ORM_API_H

/* clang-format off */
#include "c_orm_db.h"
#include "c_orm_meta.h"
/* clang-format on */

#ifdef __cplusplus
extern "C" {
#endif

/* Fallback definitions for CddCVariant, removed from upstream cdd-c */
#define CDD_C_VARIANT_TYPE_NULL 0
#define CDD_C_VARIANT_TYPE_INT 1
#define CDD_C_VARIANT_TYPE_FLOAT 2
#define CDD_C_VARIANT_TYPE_STRING 3
#define CDD_C_VARIANT_TYPE_BLOB 4

struct CddCVariant {
  int type;
  union {
    int64_t i_val;
    double f_val;
    char *s_val;
    struct {
      unsigned char *data;
      size_t size;
    } b_val;
  } value;
};

struct CddCAbstractStruct;
struct CddCAbstractStructArray;
struct CddCVariant;

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
 * @brief Save a record to the database via insert or update depending on PK
 * presence.
 *
 * @param db Database connection.
 * @param meta Table metadata.
 * @param in_struct Pointer to the struct containing data to save.
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

struct c_orm_meta;

/**
 * @brief Implement deep free logic utilizing cdd-c nested struct traversals.
 *
 * @param meta The reflection metadata representing the struct type.
 * @param obj The structure to recursively free. Does not free `obj` itself,
 * only its allocations.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_deep_free(const struct c_orm_meta *meta,
                                           void *obj);

/**
 * @brief Implement deep copy logic utilizing cdd-c nested struct traversals.
 *
 * @param meta The reflection metadata representing the struct type.
 * @param dest The destination structure (must be pre-allocated).
 * @param src The source structure to copy from.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_deep_copy(const struct c_orm_meta *meta,
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
C_ORM_EXPORT void c_orm_register_query_interceptor(c_orm_db_t *db,
                                                   c_orm_interceptor_cb hook,
                                                   void *context);

/**
 * @brief Register a hydration interceptor plugin (Step 196).
 *
 * @param db Database connection.
 * @param hook The interceptor callback.
 * @param context Opaque user data for the plugin.
 */
C_ORM_EXPORT void
c_orm_register_hydration_interceptor(c_orm_db_t *db, c_orm_interceptor_cb hook,
                                     void *context);

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
                                                unsigned long long query_hash,
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

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* C_ORM_API_H */
