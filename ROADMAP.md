# c-orm Roadmap

This document tracks delivered capabilities and upcoming architectural milestones for `c-orm`. Our goal is a safe, zero-bloat, strictly ISO C89 Object-Relational Mapper with 100% documentation and core test coverage across embedded, server, and mainframe environments.

## ✅ Delivered Milestones

* **Multi-Backend Driver VTable (`c_orm_connect_url`)**: Unified driver interfaces for SQLite (`sqlite://`), PostgreSQL (`postgres://`), MySQL/MariaDB (`mysql://`), IBM Db2 (`db2://`), Generic ODBC 3.52 (`odbc://`), and Ephemeral Memory (`memory://`), supporting both static/shared linking and runtime dynamic loading.
* **IBM Db2 & Generic ODBC Enterprise Support**: Zero-`<windows.h>` C89 ODBC 3.52 + IBM Db2 CLI runtime loader, WLM client accounting registers, `MERGE INTO` upserts, `SELECT ... FROM FINAL TABLE (INSERT ...)`, `WITH UR/CS/RS/RR` isolation clauses, `SQL_ATTR_PARAMSET_SIZE` array batching, and streaming LOB locators.
* **Lossless Financial `DECIMAL` & Mainframe `EBCDIC`**: 38-digit exact fixed-point `c_orm_decimal_t`, `COMP-3` packed decimal, Zoned Decimal, big-endian `COMP`, `IBM037`/`IBM1047`/`IBM500`/`IBM1140` transcoders, and UTF-16 `GRAPHIC`/`VARGRAPHIC`.
* **Bidirectional COBOL Copybook (`.cpy`) & SQL Codegen**: Fixed/free Copybook + `DCLGEN` parser, binary mainframe wire pack/unpack, and `c-orm-cli sql2c` / `cpy2c` / `cpy2sql` / `sql2cpy` code generation.
* **Serde/Diesel-Style Struct Reflection & Batch CRUD**: Compile-time `C_ORM_STRUCT` / `C_ORM_STRUCT_WITH_RELATIONS` X-macros (`include/c_orm_struct.h`), metadata-driven CRUD (`c_orm_insert`, `c_orm_find_by_id_*`, `c_orm_update`, `c_orm_upsert`, `c_orm_delete_*`), and bulk batch operations (`c_orm_insert_batch`, `c_orm_update_batch`, `c_orm_upsert_batch`, `c_orm_delete_batch_int32`).
* **Arena-Backed AST Query Builder**: Parameterized `SELECT`, `INSERT`, `UPDATE`, `DELETE`, `JOIN`s, `GROUP BY`/`HAVING`, subqueries, `UNION`, CTEs (`WITH`), and window functions (`OVER`) rendered cleanly per target SQL dialect.
* **Relationship Graphs & Concurrency Control**: 1:1, 1:N, M:N, `HAS_MANY_THROUGH`, and polymorphic associations with eager (`JOIN`) and lazy loading, cascade rules, pessimistic `SELECT ... FOR UPDATE`, and version-checked optimistic locking (`c_orm_update_optimistic`).
* **Thread-Safe Connection Pooling & Statement LRU Cache**: Pre-warmed, capacity-bounded connection pools (`c_orm_pool_t` in `src/c_orm_pool.c`) with acquisition timeouts alongside per-connection prepared-statement LRU caching (`src/c_orm_cache.c`) and session identity maps.

## 🚀 Near-Term Goals

### Automatic Schema-Diff Migration Generation (`c-orm-cli generate`)
* Wire live schema introspection (`c_orm_migration_fetch_table_schema` / `cdd_c_inspect_schema_odbc`) to `cdd-c` model metadata diffing so `c-orm-cli generate <name>` automatically emits dialect-specific `-- UP` (`ALTER TABLE` / `CREATE TABLE`) and `-- DOWN` SQL scripts across SQLite, PostgreSQL, MySQL, IBM Db2, and Generic ODBC.

### True Background Worker Pool & Event-Loop Dispatch (`c-orm-async`)
* Attach a configurable worker thread pool and event-loop completion adapters (`libuv`, `epoll`, `kqueue`, Win32 IOCP) to `c_orm_set_modality()` and `c-orm-async` (`c_orm_insert_async`, `c_orm_find_all_async`), replacing the current inline synchronous execution fallback when `C_ORM_MODALITY_THREAD_POOL` or `C_ORM_MODALITY_EVENT_LOOP` is configured.
* Dispatch `c_orm_shard_scatter_gather()` concurrently across shard connections when multi-threading is enabled.

### Result-Set L1 Query Cache
* Complement the existing prepared-statement LRU cache (`c_orm_prepare_cached`) and PK identity map (`c_orm_identity_map_t`) with an optional transaction-scoped read-through result cache featuring automatic table-level invalidation on `INSERT`/`UPDATE`/`DELETE`.

## 🛠️ Medium to Long-Term Goals

### Additional Native Dialects & Plugin Registry
* Formalize external `c_orm_driver_vtable_t` URI scheme registration (`c_orm_register_driver`) so third-party dialects (e.g., Oracle OCI, native TDS/SQL Server, DuckDB) can plug into `c_orm_connect_url()` without modifying core `c-orm` sources.

### Language & FFI Bindings
* Leverage `c-orm`'s stable C89 ABI and `c_orm_error_t` conventions to publish official bindings for:
  * **Lua / LuaJIT** via FFI for embedded scripting.
  * **Python** via `cffi` for sharing compiled C models and copybook codecs with tooling scripts.
  * **Zig / Nim** as zero-dependency native C ABI packages.

### Embedded & Minimal-ROM Profiles
* Add CI binary-size gates targeting constrained microcontrollers (ARM Cortex-M, ESP32) with selective feature stripping (`C_ORM_ENABLE_ODBC=OFF`, `C_ORM_ENABLE_DB2=OFF`, `C_ORM_ENABLE_COPYBOOK=OFF`, `C_ORM_THREADING=OFF`).
