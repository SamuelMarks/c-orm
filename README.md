c-orm
=====
[![License](https://img.shields.io/badge/license-Apache--2.0%20OR%20MIT-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![Doc Coverage](https://img.shields.io/badge/docs-99%25-brightgreen.svg)](#)
[![Test Coverage](https://img.shields.io/badge/coverage-100%25-brightgreen.svg)](#)
[![CI](https://github.com/SamuelMarks/c-orm/actions/workflows/ci.yml/badge.svg)](https://github.com/SamuelMarks/c-orm/actions/workflows/ci.yml)
[![C Standard](https://img.shields.io/badge/C-89-blue.svg)](#)

An abstract, cross-platform Object Relational Mapper for C. Designed to be lightweight, rigorously safe, and compatible with SQLite (`sqlite://`), PostgreSQL (`postgres://`), MySQL/MariaDB (`mysql://`), IBM Db2 (`db2://`), Generic ODBC 3.52 (`odbc://`), and an ephemeral in-memory SQL engine (`memory://`), with first-class COBOL Copybook (`.cpy`), `COMP-3`, and `EBCDIC` mainframe interoperability.

`c-orm` provides primitives analogous to Python's SQLAlchemy/Alembic or Rust's Diesel, implemented in strict ISO C89 (`-std=c90 -pedantic`) with universal `c_orm_error_t` error-enum percolation across embedded systems, legacy environments, mainframes, and modern servers.

## Table of Contents
1. [Key Features](#key-features)
2. [Philosophy](#philosophy)
3. [Feature Status Matrix](#feature-status)
4. [Installation](#installation)
5. [WebAssembly / Emscripten](#webassembly--emscripten)
6. [Advanced CMake Options](#advanced-cmake-options)
7. [Supported Compilers & Platforms](#supported-compilers--platforms)
8. [Documentation](#documentation)
9. [License](#license)

## Key Features

- **Strict ISO C89 Compliance**: Compiles cleanly under `-std=c90 -Wall -Wextra -pedantic -Werror` and MSVC `/W4 /WX` with zero C99 VLAs, mixed declarations, or `//` comments.
- **Universal Error Percolation**: Every operational function returns `c_orm_error_t` (`C_ORM_OK == 0`) and writes outputs via pointer arguments.
- **Cross-Platform Compatibility**: Supports MSVC (2005 through 2026), MinGW, Cygwin, GCC, Clang, and Emscripten across Windows, macOS, Linux, and WebAssembly.
- **Safe CRT & Zero `<windows.h>`**: Uses bounded Safe CRT wrappers (`strcpy_s`, `sprintf_s`, `fopen_s`) on MSVC and forward-declares exact `__declspec(dllimport)` Win32/ODBC 3.52 symbols without pulling in `<windows.h>`.
- **Unified Multi-Backend Dispatch**: Connect to SQLite, PostgreSQL, MySQL/MariaDB, IBM Db2, Generic ODBC, or Ephemeral Memory via `c_orm_connect_url()` without changing application CRUD logic.
- **Thread-Safe Connection Pooling & Statement LRU Cache**: Bounded connection pools (`c_orm_pool_t`) with wait timeouts alongside per-connection prepared-statement LRU caching (`c_orm_prepare_cached`) and PK identity maps.
- **Arena-Backed AST Query Builder**: Construct parameterized `SELECT`, `INSERT`, `UPDATE`, `DELETE`, `UPSERT`/`MERGE`, `JOIN`, CTE (`WITH`), and window queries across all SQL dialects (`c_orm_ast.h`).
- **Relationships, Concurrency & Crypto Hooks**: 1:1, 1:N, M:N, and polymorphic associations with eager/lazy loading, pessimistic (`FOR UPDATE`) and optimistic (`c_orm_update_optimistic`) locking, OAuth2 models (`c_orm_oauth2.h`), and field-level encryption hooks.
- **Bidirectional SQL & COBOL Copybook Codegen (`c-orm-cli`)**: Translate between SQL DDL, C89 structs/metadata, and COBOL `.cpy` / `DCLGEN` copybooks (`sql2c`, `cpy2c`, `cpy2sql`, `sql2cpy`) with lossless 38-digit `c_orm_decimal_t`, `COMP-3`, and `EBCDIC` wire serialization.

## Philosophy

`c-orm` operates on four principles:
1. **Zero Hidden Magic**: Every allocation routes through inspectable allocator hooks (`c_orm_system_malloc` / `c_orm_set_allocators`), and every SQL query, bind parameter, and error code is directly observable.
2. **Compile Everywhere**: Strict C89 source compatibility ensures builds succeed on decades-old enterprise/embedded compilers and bleeding-edge toolchains alike.
3. **Pay Only for What You Enable**: Database backends can be linked statically, loaded dynamically at runtime (`dlopen`/`LoadLibrary`), or compiled out completely.
4. **100% Verified**: Maintained with 100% Doxygen documentation coverage across all headers/sources and 100% line, function, and branch test coverage across all core library modules (`src/`).

## Feature Status

| Component | Status | Notes |
| :--- | :---: | :--- |
| **SQLite Driver (`sqlite://`)** | ✅ | Linked amalgamation/system SQLite3 or dynamic loading; incremental BLOB streaming. |
| **PostgreSQL Driver (`postgres://`)** | ✅ | Linked or runtime-loaded `libpq`; `$1..$N` query rewriter and Large Object streaming. |
| **MySQL / MariaDB Driver (`mysql://`)** | ✅ | Linked or runtime-loaded `libmysqlclient` with SSL and Unix socket URL parsing. |
| **IBM Db2 Driver (`db2://`)** | ✅ | Zero-`<windows.h>` Db2 CLI/ODBC driver, WLM client registers, LOB locators, `MERGE INTO`, `FINAL TABLE`, `WITH UR/CS/RS/RR`. |
| **Generic ODBC Driver (`odbc://`)** | ✅ | Full ODBC 3.52 ANSI & UTF-16 (`SQLWCHAR`) driver, `SQLGetData` streaming, `SQL_ATTR_PARAMSET_SIZE` array batching. |
| **Ephemeral Memory Driver (`memory://`)** | ✅ | Built-in SQL engine with multi-column `WHERE`, `ORDER BY`, `LIMIT`/`OFFSET`, and snapshot transactions. |
| **Lossless `DECIMAL` & `EBCDIC`** | ✅ | Exact 38-digit `c_orm_decimal_t`, `COMP-3`, Zoned Decimal, `IBM037`/`1047`/`500`/`1140`, UTF-16 `GRAPHIC`. |
| **COBOL Copybooks (`.cpy` / `DCLGEN`)** | ✅ | Fixed/free parser (`REDEFINES`, `OCCURS DEPENDING ON`, `88`-levels) + `cpy2c` / `cpy2sql` / `sql2cpy` CLI & binary wire pack/unpack. |
| **AST & Fluent Query Builder** | ✅ | `SELECT`/`INSERT`/`UPDATE`/`DELETE`/`UPSERT`, `JOIN`s, `GROUP BY`/`HAVING`, CTEs, `UNION`, subqueries, window functions. |
| **Relationships & Concurrency** | ✅ | 1:1, 1:N, M:N, `HAS_MANY_THROUGH`, polymorphic, cascades, `FOR UPDATE`, and `c_orm_update_optimistic`. |
| **Connection Pooling (`c_orm_pool_t`)** | ✅ | Thread-safe pool (`c_orm_pool_init`/`acquire`/`release`/`destroy`) with pre-warming and millisecond wait timeouts. |
| **Statement LRU Cache & Identity Map** | ✅ | Thread-safe prepared-statement LRU cache (`c_orm_prepare_cached`) and session PK identity map. |
| **Schema Migrations (`c-orm-cli`)** | ✅ | Versioned UP/DOWN SQL runner, SHA-256 checksums, locks, and live schema introspection (`generate` auto-diff is planned). |
| **Async Callback API (`c-orm-async`)** | ⚠️ | Callback wrappers (`c_orm_insert_async`, `c_orm_find_all_async`) with inline synchronous fallback; worker-thread dispatch planned. |
| **WebAssembly / Emscripten** | ✅ | Compiles to WASM with SQLite and `IDBFS` filesystem persistence hook (`c_orm_wasm_init_fs`). |

## Installation

### CMake `FetchContent`
```cmake
include(FetchContent)
FetchContent_Declare(
    c-orm
    GIT_REPOSITORY https://github.com/SamuelMarks/c-orm.git
    GIT_TAG master
)
FetchContent_MakeAvailable(c-orm)

target_link_libraries(your_app PRIVATE c-orm)
```

### Building Locally
```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## WebAssembly / Emscripten

1. Configure and build using `emcmake cmake -S . -B build_wasm -DC_ORM_THREADING=OFF`.
2. Mount Emscripten's `IDBFS` filesystem (e.g., `/data`) and synchronize via `c_orm_wasm_init_fs`:
   ```c
   typedef c_orm_error_t (*c_orm_wasm_sync_cb_t)(c_orm_error_t status);
   c_orm_error_t c_orm_wasm_init_fs(c_orm_wasm_sync_cb_t callback);
   ```

## Advanced CMake Options

### Database Backends
- `C_ORM_SQLITE` (`LINKED` [Default] | `DYNAMIC` | `OFF`): Link SQLite3 directly, load dynamically at runtime, or disable.
- `C_ORM_POSTGRESQL` (`OFF` [Default] | `LINKED` | `DYNAMIC`): Configure PostgreSQL `libpq` backend.
- `C_ORM_MYSQL` (`OFF` [Default] | `LINKED` | `DYNAMIC`): Configure MySQL/MariaDB client backend.
- `C_ORM_ENABLE_ODBC` (Default: `ON`): Enable Generic ODBC 3.52 driver (`odbc://`).
- `C_ORM_ENABLE_DB2` (Default: `ON`): Enable IBM Db2 CLI/ODBC driver (`db2://`).
- `C_ORM_ENABLE_COPYBOOK` (Default: `ON`): Enable COBOL Copybook parser, wire marshaling, and codegen.
- `C_ORM_LINK_ODBC` (Default: `OFF`): Link directly against system/vcpkg ODBC (`odbc32` / `ODBC::ODBC`) instead of runtime `dlopen`/`LoadLibrary`.
- `C_ORM_LINK_DB2` (Default: `OFF`): Link directly against IBM Db2 CLI (`libdb2` via `IBM_DB_HOME`) instead of runtime loading.

### Compiler, Runtime & Build Targets
- `BUILD_SHARED_LIBS` (Default: `OFF`): Build shared libraries (`.dll` / `.so` / `.dylib`) instead of static archives.
- `C_ORM_CRT_LINKAGE` (`""` | `static` | `dynamic`): Select MSVC `/MT`/`/MTd` vs `/MD`/`/MDd` CRT linkage.
- `C_ORM_CHARSET` (`UNICODE` [Default] | `ANSI`): Toggle Windows `UNICODE`/`_UNICODE` vs `_MBCS` character sets.
- `C_ORM_THREADING` (Default: `ON`): Enable OS mutexes (`CRITICAL_SECTION` / `pthread_mutex_t`); `OFF` defines `C_ORM_NO_THREADS`.
- `C_ORM_LTO` (Default: `OFF`): Enable Interprocedural / Link-Time Optimization (`CMAKE_INTERPROCEDURAL_OPTIMIZATION`).
- `C_ORM_MSVC_RTC` (`OFF` [Default] | `1` | `s` | `u`): Enable MSVC runtime checks (`/RTC1`, `/RTCs`, `/RTCu`).
- `C_ORM_BUILD_TESTS` (Default: `ON`): Build test suites and enable `C_ORM_TEST_ALLOCATOR` fault-injection hooks.
- `C_ORM_BUILD_EXAMPLES` (Default: `ON`): Build example applications (`examples/`).
- `C_ORM_BUILD_BENCHMARKS` (Default: `OFF`): Build benchmark suite (`tests/benchmarks/`).
- `C_ORM_FORCE_FETCH_MASTER` (Default: `OFF`): Force fetching latest `master` of `SamuelMarks/*` dependencies via `FetchContent`.

## Supported Compilers & Platforms

- **Compilers**: GCC, Clang / AppleClang, MSVC (Visual Studio 2005 through 2026), MinGW-w64, Cygwin, Emscripten (`emcc`).
- **Operating Systems**: Linux, macOS, Windows (x86, x64, ARM64), FreeBSD, WebAssembly.

## Documentation

- [Usage Guide](USAGE.md) & [Architecture Overview](ARCHITECTURE.md)
- [End-to-End Tutorial](docs/TUTORIAL.md) & [CRUD Quickstart](docs/CRUD_QUICKSTART.md)
- [Advanced CRUD & Optimistic Locking](docs/ADVANCED_CRUD.md) & [API Reference](docs/API_REFERENCE.md)
- [AST & Fluent Query Builder](docs/QUERY_BUILDER.md) & [SQL Dialect Support](docs/SQL_SUPPORT.md)
- [Relationships & Cascades](docs/RELATIONSHIPS.md) & [Batch Operations](docs/C_ORM_BATCH_EXAMPLES.md)
- [Transactions & Savepoints](docs/TRANSACTIONS.md) & [Migrations Guide](docs/MIGRATIONS_GUIDE.md)
- [IBM Db2 & Generic ODBC Guide](docs/DB2_AND_ODBC.md) & [COBOL Copybooks & Decimal Guide](docs/COBOL_COPYBOOKS.md)
- [Error Codes (`c_orm_error_t`)](docs/ERRORS.md) & [Memory Management](docs/MEMORY.md)
- [Roadmap](ROADMAP.md) & [Coverage Report](UNDERCOVERED.md)

## License

Licensed under either of

- Apache License, Version 2.0 ([LICENSE-APACHE](LICENSE-APACHE) or <https://www.apache.org/licenses/LICENSE-2.0>)
- MIT license ([LICENSE-MIT](LICENSE-MIT) or <https://opensource.org/licenses/MIT>)

at your option.

### Contribution

Unless you explicitly state otherwise, any contribution intentionally submitted
for inclusion in the work by you, as defined in the Apache-2.0 license, shall be
dual licensed as above, without any additional terms or conditions. Please refer to [skills.md](skills.md) before submitting Pull Requests.
