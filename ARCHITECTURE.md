# c-orm Architecture

The `c-orm` project is designed as an abstraction layer bridging C applications and relational databases (SQLite, PostgreSQL, MySQL) without imposing heavy dependencies or sacrificing C89 compatibility.

## Core Components

### 1. Connection Management (`c_orm.c`)
The root object is `c_orm_db_t`, an opaque struct representing a database connection.
- **Dialect Switcher:** The `c_orm_dialect_t` enumeration determines which underlying C driver (e.g., `sqlite3`, `libpq`, `libmysqlclient`) is invoked during `c_orm_connect()`.
- **Thread Safety:** Every connection has an internal `c_orm_mutex_t` (a thin wrapper over `CRITICAL_SECTION` on Windows or `pthread_mutex_t` on POSIX).
- **Asynchronous Queue:** The `c_orm_async_job` linked list allows queries to be queued and executed via an event-loop polling mechanism (`c_orm_poll_async`), preventing blocking on long transactions.

### 2. Connection Pool (`c_orm_pool_t`)
To prevent connection exhaustion and handle high-throughput, the ORM provides a connection pool.
- Initialized with a maximum capacity.
- Uses an `in_use` array to track active connections.
- Fully synchronized via a global pool mutex to safely `acquire` and `release` connections across threads.

### 3. Query Builder (`c_orm_query_t`)
A fluent interface for constructing safe SQL strings dynamically.
- Eliminates manual string concatenation errors.
- Exposes `select`, `where`, `order_by`, and `limit` clauses.
- Handles integer formatting using the `NUM_FORMAT` macro.

### 4. Code Generator / AST (`db_codegen.c` & `database.c`)
The library isn't just a runtime ORM; it's also a development tool.
- **DatabaseSchema AST:** In-memory C structs (`DatabaseTable`, `DatabaseColumn`) represent a database schema.
- **Code Emitters:** The library can parse basic SQL (`db_codegen_parse_sql`) and output:
  - Cross-dialect SQL `CREATE TABLE` scripts (`db_codegen_sql`).
  - C Struct Headers (`db_codegen_struct_header`).
  - C CRUD Boilerplate Code (`db_codegen_crud_h`, `db_codegen_crud_c`).

## Cross-Platform Considerations

### Avoidance of `<windows.h>`
To reduce binary bloat and compilation times, `c-orm` bypasses the standard Windows API header. Instead, it relies directly on `<windef.h>` and `<winbase.h>`. To satisfy internal Microsoft headers, target architecture macros (`_X86_`, `_AMD64_`, etc.) are manually defined based on compiler detection.

### MSVC Safe CRT
On MSVC compilers (`_MSC_VER`), standard string manipulation functions (`strcpy`, `sprintf`) trigger deprecation warnings. The architecture conditionally replaces these with their bounds-checking variants (`strcpy_s`, `sprintf_s`). 

### Error Handling
All functions (except destructors) return `int` exit codes:
- `0` indicates success.
- Negative values (e.g., `-1`, `-2`, `-4`) indicate specific failure states like invalid arguments, exhausted pools, or unsupported compilation features.
- Pointers are passed by reference (`**out`) rather than returned to maintain strict signature consistency.
