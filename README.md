c-orm
=====
[![License](https://img.shields.io/badge/license-Apache--2.0%20OR%20MIT-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![CI](https://github.com/SamuelMarks/c-orm/actions/workflows/ci.yml/badge.svg)](https://github.com/SamuelMarks/c-orm/actions/workflows/ci.yml)
[![Doc Coverage](https://img.shields.io/badge/docs-100%25-brightgreen.svg)](#)
[![Test Coverage](https://img.shields.io/badge/coverage-100%25-brightgreen.svg)](#)
[![C Standard](https://img.shields.io/badge/C-89-blue.svg)](#)

An abstract, cross-platform Object Relational Mapper for C. Designed to be lightweight, rigorously safe, and compatible with SQLite, PostgreSQL, and MySQL.

It provides primitives analogous to Python's SQLAlchemy/Alembic or Rust's Diesel, but implemented in strict C89 to maximize portability across embedded systems, legacy environments, and modern architectures.

## Key Features

- **Strict C89 Compliance**: Guaranteed to compile on ancient and modern toolchains alike.
- **Cross-Platform Compatibility**: Supports MSVC (from 2005 up to 2026), MinGW, Cygwin, GCC, and Clang.
- **Safe CRT Integration**: Automatically utilizes Microsoft Safe CRT functions (`strcpy_s`, `sprintf_s`, `fopen_s`) on MSVC via internal macros without breaking C89 standards on POSIX.
- **Optimized Windows Headers**: Explicitly avoids the bloated `<windows.h>` header. It surgically relies on `<windef.h>` and `<winbase.h>` with hand-crafted architecture guards (`_X86_`, `_AMD64_`, etc.) to significantly reduce binary footprint and compile times.
- **Dynamic Dialect Handling**: Connect to SQLite, PostgreSQL, or MySQL via a single unified API (`c_orm_dialect_t`).
- **Connection Pooling**: Built-in thread-safe database connection pool.
- **Fluent Query Builder**: Safely construct SELECT statements without raw string concatenation.
- **Abstracted Schema Migrations**: Alembic-style schema migrations and version tracking.
- **CRUD Code Generation**: Generate pure C structs and CRUD boilerplates straight from your DB Schema AST using the `db_codegen` module.
- **Async Execution Simulation**: Queue-based async execution mechanisms designed for easy integration with event loops.

## Feature Status

| Feature | Status | Description |
| :--- | :---: | :--- |
| SQLite Backend | ✅ | Full support for SQLite. |
| PostgreSQL Backend | ✅ | Full support via native libpq integration. |
| MySQL Backend | ✅ | Full support via native mysqlclient integration. |
| Connection Pooling | ✅ | Thread-safe connection pool implemented. |
| Fluent Query Builder | ✅ | SELECT, WHERE, ORDER BY, LIMIT supported. |
| Parameterized Queries | ✅ | Type-safe binding primitives available. |
| Schema Migrations | 🚧 | Version tracking and up/down stubbed. |
| Code Generation | ✅ | SQL to C struct and CRUD generation functional. |
| Async Execution | ✅ | Queue-based simulation for event loops. |
| ORM Object Mapping | 🚧 | Full automated struct-to-row mapping pending. |

## Installation

This project integrates directly with CMake `FetchContent`:

```cmake
include(FetchContent)
FetchContent_Declare(
    c-orm
    GIT_REPOSITORY https://github.com/SamuelMarks/c-orm.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(c-orm)

# Link your target
target_link_libraries(my_server PRIVATE c-orm)
```

## Advanced CMake Options

`c-orm` exposes an extensive set of CMake options allowing you to precisely control the build output to match your host application's environment. This is especially useful for strict MSVC ABI matching.

### Backends
- `C_ORM_BACKEND_SQLITE` (Default: ON): Enable SQLite backend.
- `C_ORM_BACKEND_POSTGRES` (Default: OFF): Enable PostgreSQL backend.
- `C_ORM_BACKEND_MYSQL` (Default: OFF): Enable MySQL backend.

### Optimization & Diagnostics
- `C_ORM_LTO`: Enable Link-Time Optimization (IPO).
- `C_ORM_RUNTIME_CHECKS_RTC1`: Enable MSVC `/RTC1` (equivalent to `/RTCsu`).
- `C_ORM_RUNTIME_CHECKS_RTCS`: Enable MSVC `/RTCs` (Stack Frame runtime checking).
- `C_ORM_RUNTIME_CHECKS_RTCU`: Enable MSVC `/RTCu` (Uninitialized local usage checks).

### CRT & Linking (MSVC Specific)
- `C_ORM_STATIC_CRT`: Use Static CRT (`/MT`, `/MTd`).
- `C_ORM_SHARED_CRT`: Use Shared CRT (`/MD`, `/MDd`).
- `BUILD_SHARED_LIBS`: Build `c-orm` as a dynamic shared library (DLL) instead of a static library.

### Character Sets & Threading
- `C_ORM_UNICODE`: Define `UNICODE` and `_UNICODE`.
- `C_ORM_ANSI`: Define `_MBCS` for multi-byte character sets.
- `C_ORM_MULTI_THREADED` (Default: ON): Enable internal thread-safety mechanisms (mutexes). Set to `OFF` for bare-metal single-threaded environments to save overhead.

## Documentation
- See [USAGE.md](USAGE.md) for code examples.
- See [ARCHITECTURE.md](ARCHITECTURE.md) for internal design details.

---

## License

Licensed under either of

- Apache License, Version 2.0 ([LICENSE-APACHE](LICENSE-APACHE) or <https://www.apache.org/licenses/LICENSE-2.0>)
- MIT license ([LICENSE-MIT](LICENSE-MIT) or <https://opensource.org/licenses/MIT>)

at your option.

### Contribution

Unless you explicitly state otherwise, any contribution intentionally submitted
for inclusion in the work by you, as defined in the Apache-2.0 license, shall be
dual licensed as above, without any additional terms or conditions.
