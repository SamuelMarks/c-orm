# c-orm Architecture

The `c-orm` project is designed as an abstraction layer bridging C applications and relational databases (SQLite, PostgreSQL, MySQL). It achieves this without imposing heavy dependencies, without requiring dynamic memory allocations where stack allocation is viable, and crucially, without sacrificing C89 compatibility.

This document outlines the core structural components, threading models, dialect abstraction logic, and build-time code generation strategies that make `c-orm` work securely and performantly.

## Core Components

### 1. Connection Management (`c_orm.c`)
The root object of any application interacting with the database is `c_orm_db_t`, an opaque struct representing a database connection.
- **Dialect Switcher:** The `c_orm_dialect_t` enumeration determines which underlying C driver (e.g., `sqlite3`, `libpq`, `libmysqlclient`) is invoked during `c_orm_connect()`. The API exposes a unified interface, mapping generic calls (like `c_orm_execute`) to driver-specific APIs via an internal virtual method table (vtable).
- **Thread Safety:** Every connection has an internal `c_orm_mutex_t`. This is a thin wrapper over `CRITICAL_SECTION` on Windows, or `pthread_mutex_t` on POSIX systems. Mutexes are dynamically initialized upon connection creation and cleanly destroyed upon teardown.
- **Asynchronous Queue:** The `c_orm_async_job` linked list allows queries to be queued. This is polled via an event-loop mechanism (`c_orm_poll_async`), preventing blocking on long transactions.

### 2. Connection Pool (`c_orm_pool_t`)
To prevent connection exhaustion and handle high-throughput gracefully, `c-orm` provides an integrated connection pool.
- **Capacity:** Initialized with a hard-limit maximum capacity to cap memory footprint and database strain.
- **State Tracking:** Uses an internal array combined with an `in_use` boolean map to track active connections.
- **Synchronization:** The pool is fully synchronized via a global pool mutex. Threads safely `acquire` and `release` connections without race conditions. If no connections are available, the pool immediately returns an exhaustion error code, allowing the host application to gracefully retry or fail rather than indefinitely block.

### 3. Query Builder (`c_orm_query_t`)
A fluent interface for constructing safe SQL strings dynamically, completely eliminating manual string concatenation.
- **Safety First:** Mitigates SQL injection vectors by ensuring structural integrity before execution.
- **Clause Management:** Exposes fluent APIs for `select`, `where`, `order_by`, `limit`, and `offset`.
- **Dynamic Formatting:** Handles integer and float formatting safely using bounds-checked implementations of `snprintf` or `sprintf_s`.

### 4. Identity Map and L1 Cache
To increase performance when querying relational data repetitively:
- **L1 Cache:** Intercepts duplicate read-only SELECT queries within a transaction block, serving them from in-memory buffers instead of hitting the database driver.
- **Identity Map:** Ensures that if a row with `id=5` is fetched multiple times in a session, all pointers resolve to the exact same memory struct instance, preserving data consistency across object mutations.

### 5. Code Generator / AST (`c_orm_codegen.c` wrapping `cdd-c`)
The library isn't just a runtime ORM; it's a development toolchain.
- **Code Emitters:** By wrapping the `cdd-c` native code generation API, `c-orm` provides a robust mechanism via `c_orm_codegen_generate` to parse SQL schemas and output:
  - C Struct Headers mapped precisely to the underlying table columns.
  - `cdd-c` metadata arrays defining offsets, types, and constraints for the fallback engine.
  - C CRUD Boilerplate Code to inject/fetch these structs directly without manual mapping logic.

## Cross-Platform and Compilation Considerations

### Avoidance of `<windows.h>`
To reduce binary bloat and severely cut down compilation times on MSVC, `c-orm` bypasses the standard, monolithic Windows API header. 
- It relies directly on `<windef.h>` and `<winbase.h>`.
- To satisfy internal Microsoft headers, target architecture macros (`_X86_`, `_AMD64_`, `_ARM64_`) are manually defined based on compiler preprocessor definitions.
- This technique drastically improves compilation speed and avoids namespace pollution from legacy Win32 macros.

### MSVC Safe CRT Integration
On MSVC compilers (`_MSC_VER`), standard string manipulation functions (`strcpy`, `sprintf`) trigger `C4996` deprecation warnings. The architecture conditionally replaces these with their Microsoft Safe CRT variants (`strcpy_s`, `sprintf_s`, `fopen_s`) natively.
- Buffer bounds are actively checked at runtime.
- For POSIX targets, standard C89 compliant string functions are used, maintaining full portability without littering the source code with `#ifdef` blocks via internal macro shims.

### Strict Error Handling and Return Codes
All standard operational functions (except destructors and allocation primitives) return standard `int` exit codes:
- `C_ORM_OK` (`0`) indicates success.
- Negative values (`C_ORM_ERR_INVALID_ARG`, `C_ORM_ERR_POOL_EXHAUSTED`, `C_ORM_ERR_DB_ENGINE`) indicate specific failure states.
- By convention, returning `int` and passing output structs by double-pointer reference (`**out`) forces developers to actively acknowledge and handle error states, promoting rigorous fault tolerance.

## Memory Management

### Defensive Allocations
The ORM dynamically allocates memory using strictly controlled `malloc` / `calloc` / `free` mechanisms. 
- Every struct that is allocated dynamically provides a corresponding `_free` or `_destroy` method (e.g., `c_orm_query_destroy`).
- Large datasets or BLOB streaming APIs utilize memory-mapped abstractions (if supported by the host OS) to stream gigabytes of data natively without saturating the heap.

### Fallback Abstract Hydration
When specific ahead-of-time (AOT) structures are not available (e.g., raw ad-hoc `COUNT(*)` aggregations), `c-orm` utilizes a fallback system routing data into a linked list or array of `cdd_c_abstract_struct_t`. This allows developers to read arbitrary result sets generically, behaving somewhat like a dynamically typed dictionary within a statically typed language.