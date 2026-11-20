# c-orm Architecture

The `c-orm` project is an ISO C89 database abstraction, reflection, and code-generation layer bridging C applications, relational databases (SQLite, PostgreSQL, MySQL/MariaDB, IBM Db2, Generic ODBC 3.52, and an ephemeral in-memory SQL engine), and mainframe COBOL Copybook data pipelines. Every non-mathematical function across the codebase returns and percolates a unified `c_orm_error_t` status enum (`C_ORM_OK == 0`), passing outputs via pointer arguments.

This document describes the core runtime modules, driver vtable dispatch, synchronization model, query AST, caching layers, and mainframe wire codecs.

## Core Components

### 1. Connection & Driver VTable Layer (`c_orm_db.h`, `src/c_orm_db.c`, `src/c_orm_api.c`)
The root session handle is `c_orm_db_t`, which pairs a `const c_orm_driver_vtable_t *vtable` with driver-private state, interceptors, cryptographic field hooks, slow-query telemetry (`c_orm_pool_telemetry_t`), timezone offsets, an identity map, and a statement LRU cache.
- **Unified URL Dispatcher:** `c_orm_connect_url()` inspects the URI scheme (`sqlite://`, `postgres://` / `postgresql://`, `mysql://` / `mariadb://`, `db2://`, `odbc://`, `memory://`, or bare file paths) and delegates to the matching driver (`src/c_orm_sqlite.c`, `src/c_orm_postgres.c`, `src/c_orm_mysql.c`, `src/c_orm_db2.c`, `src/c_orm_odbc.c`, `src/c_orm_memory.c`).
- **Dynamic Client Loading:** PostgreSQL, MySQL, ODBC (`src/c_orm_odbc_loader.c`), and IBM Db2 drivers support runtime dynamic loading (`dlopen` / `LoadLibraryA` / `LoadLibraryW`) as well as compile-time linking, allowing portable binaries to run without hard shared-library dependencies at startup.
- **Zero `<windows.h>` Policy:** Core Win32 synchronization, dynamic symbol resolution, high-resolution timers, DPAPI/CNG randomness, and ODBC 3.52 ABI definitions (`include/c_orm_odbc_abi.h`) forward-declare exact `__declspec(dllimport)` signatures rather than including `<windows.h>`, keeping compile times fast and avoiding global namespace pollution.

### 2. Connection Pool & Cross-Platform Mutexes (`src/c_orm_pool.c`, `src/c_orm_cache.c`)
To bound database connection counts across multi-threaded services, `c_orm_pool_t` manages reusable `c_orm_db_t` sessions (`c_orm_pool_init`, `c_orm_pool_acquire`, `c_orm_pool_release`, `c_orm_pool_destroy`).
- **Pre-Warming & Lazy Growth:** `c_orm_pool_init` pre-warms `min_conns` connections eagerly and allocates `max_conns` slot descriptors (`conns` + `in_use` byte flags). Additional connections up to `max_conns` are opened on demand during `c_orm_pool_acquire`.
- **Bounded Wait Timeouts:** When all `max_conns` slots are leased, `c_orm_pool_acquire` releases the pool lock and polls in bounded millisecond slices up to `timeout_ms` before returning `C_ORM_ERROR_TIMEOUT`.
- **Shared Synchronization Primitives:** `c_orm_mutex_create`, `c_orm_mutex_lock`, `c_orm_mutex_unlock`, and `c_orm_mutex_destroy` wrap Win32 `CRITICAL_SECTION` on Windows, `pthread_mutex_t` on POSIX, and deterministic no-op tokens when `C_ORM_THREADING=OFF` (`C_ORM_NO_THREADS`) or `EMSCRIPTEN_NO_THREADS` is active.

### 3. Query AST & Dialect SQL Builders (`include/c_orm_ast.h`, `src/c_orm_query_fluent.c`, `src/c_orm_query_sql.c`, `src/c_orm_query_builder.c`)
`c-orm` separates prepared driver statement handles (`c_orm_query_t` / `c_orm_stmt_t` in `c_orm_db.h`) from SQL construction:
- **Arena-Backed Fluent AST (`c_orm_ast_query_t`):** Allocates AST nodes inside a contiguous bump arena (`src/c_orm_arena.c`) freed in one shot by `c_orm_query_free()`. Supports `SELECT`, `INSERT`, `UPDATE`, `DELETE`, `JOIN`s, `GROUP BY`/`HAVING`, `UNION`, recursive/non-recursive CTEs (`WITH`), window functions (`OVER`), `CAST`, `BETWEEN`, `EXISTS`, `RETURNING` / Db2 `FINAL TABLE`, and Db2 isolation lock clauses (`WITH UR`/`CS`/`RS`/`RR`).
- **Dialect Compiler (`c_orm_query_to_sql`):** Renders AST trees into parameterized SQL (`?` or PostgreSQL `$1..$N`) tailored for `C_ORM_DIALECT_SQLITE`, `C_ORM_DIALECT_POSTGRES`, `C_ORM_DIALECT_MYSQL`, `C_ORM_DIALECT_DB2` (`FETCH FIRST n ROWS ONLY`, `MERGE INTO`), and `C_ORM_DIALECT_ODBC`.
- **Lightweight Builders (`include/c_orm_query_builder.h`):** Imperative `c_orm_select_builder_t`, `c_orm_insert_builder_t`, `c_orm_update_builder_t`, and `c_orm_delete_builder_t` backed by `c_orm_string_builder_t`.

### 4. Statement LRU Cache & Identity Map (`src/c_orm_cache.c`, `src/c_orm_api.c`)
- **Prepared-Statement LRU Cache:** `c_orm_enable_statement_caching()` attaches a mutex-protected hash/LRU cache of compiled `c_orm_query_t` handles to a `c_orm_db_t` connection. `c_orm_prepare_cached()` and `c_orm_finalize_cached()` reset and recycle statements across repeated CRUD calls while evicting the least-recently-used unleased entry at capacity.
- **Session Identity Map (`c_orm_identity_map_t`):** Tracks hydrated C struct pointers by table name and primary key within a session so repeated lookups resolve to a consistent in-memory instance, paired with dirty-field tracking and expiration callbacks.

### 5. Reflection Metadata, Concurrency, Relations & Sharding (`include/c_orm_meta.h`, `include/c_orm_struct.h`, `src/c_orm_api.c`)
- **Compile-Time Reflection:** `c_orm_table_meta_t` and `c_orm_column_meta_t` capture `offsetof()` byte offsets, SQL/COBOL types, `DECIMAL(p, s)` precision/scale, EBCDIC CCSIDs, nullability, lifecycle hooks, and relationship graphs (`C_ORM_STRUCT`, `C_ORM_STRUCT_WITH_RELATIONS`, `c_orm_inline_macros.h`).
- **Relationships & Cascades:** Automated hydration for `ONE_TO_ONE`, `BELONGS_TO`, `ONE_TO_MANY`, `MANY_TO_MANY`, `HAS_MANY_THROUGH`, and `POLYMORPHIC` associations via eager `JOIN` loading (`c_orm_find_with_relation_int32`, `c_orm_find_with_relations_int32`) or deferred lazy loading (`c_orm_load_relation`), plus cascading inserts, updates, and deletes.
- **Concurrency & Horizontal Sharding:** Supports pessimistic row locking (`c_orm_find_for_update_by_id_*`), version-checked optimistic concurrency (`c_orm_update_optimistic`), and hash/range shard routing (`c_orm_shard_manager_t`).
- **Async Callback Target (`c-orm-async`, `src/c_orm_async.c`):** Provides `c_orm_insert_async` and `c_orm_find_all_async` completion-callback entry points in a separate library target, falling back to inline synchronous execution when no background worker pool is attached.

### 6. Code Generation, Migrations & Mainframe Interop (`src/c_orm_codegen.c`, `src/c_orm_migrations.c`, `src/c_orm_copybook*.c`, `src/c_orm_cli.c`)
- **Bidirectional Codegen (`c-orm-cli`):** Wraps `cdd-c` alongside `c-orm`'s SQL parser (`src/c_orm_sql.c`), OpenAPI emitter (`src/orm_gen.c`), and COBOL Copybook / Db2 `DCLGEN` parser (`src/c_orm_copybook.c`, `src/c_orm_copybook_codegen.c`) to translate across SQL DDL, C89 model headers/sources, and `.cpy` copybooks (`sql2c`, `cpy2c`, `cpy2sql`, `sql2cpy`).
- **Lossless Mainframe Wire Codecs (`src/c_orm_decimal.c`, `src/c_orm_ebcdic.c`, `src/c_orm_copybook_wire.c`):** Exact 38-digit base-$10^9$ fixed-point `c_orm_decimal_t` arithmetic, `SQL_NUMERIC_STRUCT` conversion, `COMP-3` packed decimal, Zoned Decimal, big-endian `COMP`, `IBM037`/`IBM1047`/`IBM500`/`IBM1140` `EBCDIC` transcoders, and UTF-16LE/BE `GRAPHIC` codecs.
- **Migrations (`src/c_orm_migrations.c`, `src/migration_runner.c`):** Versioned `-- UP` / `-- DOWN` migration tracking with SHA-256 checksums, dry-run execution, pre/post hooks, distributed migration locks, and live database schema introspection (`c_orm_migration_fetch_table_schema`, `cdd_c_inspect_schema_odbc`).

## Memory Management & Safety Discipline
1. **Injectable Allocators (`src/c_orm_alloc.c`):** All heap allocations route through `c_orm_system_malloc`, `c_orm_system_calloc`, `c_orm_system_realloc`, `c_orm_system_free`, and `c_orm_strdup`, enabling deterministic OOM fault injection via `c_orm_set_allocators` under `C_ORM_TEST_ALLOCATOR`.
2. **Safe CRT (`include/c_orm_safe_crt.h`):** Bounds-checked wrappers (`c_orm_sprintf`, `c_orm_strcpy_s`, `c_orm_strcat_s`, `c_orm_fopen`, `c_orm_getenv_s`) automatically leverage Microsoft Safe CRT (`_s`) routines on MSVC while falling back to bounded C89 implementations on POSIX.
3. **Dynamic Fallback (`src/abstract_struct.c`, `src/hydrate_router.c`):** Queries without static struct metadata hydrate into `cdd_c_abstract_struct_t` key-value variant dictionaries (`c_orm_find_all_abstract`, freed via `c_orm_abstract_free`).
