c-orm
=====
[![License](https://img.shields.io/badge/license-Apache--2.0%20OR%20MIT-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![CI](https://github.com/SamuelMarks/c-orm/actions/workflows/ci.yml/badge.svg)](https://github.com/SamuelMarks/c-orm/actions/workflows/ci.yml)
[![Doc Coverage](https://img.shields.io/badge/docs-100%25-brightgreen.svg)](#)
[![Test Coverage](https://img.shields.io/badge/coverage-100%25-brightgreen.svg)](#)
[![C Standard](https://img.shields.io/badge/C-89-blue.svg)](#)

An abstract, cross-platform Object Relational Mapper for C. Designed to be lightweight, rigorously safe, and compatible with SQLite, PostgreSQL, and MySQL.

`c-orm` provides primitives analogous to Python's SQLAlchemy/Alembic or Rust's Diesel, but implemented in strict C89 to maximize portability across embedded systems, legacy environments, and modern architectures.

## Table of Contents
1. [Key Features](#key-features)
2. [Philosophy](#philosophy)
3. [Feature Status Matrix](#feature-status)
4. [Installation](#installation)
5. [Advanced CMake Options](#advanced-cmake-options)
6. [Supported Compilers & Platforms](#supported-compilers--platforms)
7. [Documentation Links](#documentation)
8. [License & Contributing](#license)

## Key Features

- **Strict C89 Compliance**: Guaranteed to compile on ancient and modern toolchains alike. Zero reliance on C99 VLA (Variable Length Arrays) or inline declarations.
- **Cross-Platform Compatibility**: Unparalleled support for MSVC (from 2005 up to 2026), MinGW, Cygwin, GCC, and Clang.
- **Safe CRT Integration**: Automatically utilizes Microsoft Safe CRT functions (`strcpy_s`, `sprintf_s`, `fopen_s`) on MSVC via internal macros, dynamically preventing buffer overflows without breaking C89 standards on POSIX.
- **Optimized Windows Headers**: Explicitly avoids the bloated `<windows.h>` header. It surgically relies on `<windef.h>` and `<winbase.h>` with hand-crafted architecture guards (`_X86_`, `_AMD64_`, etc.) to significantly reduce binary footprint and compile times.
- **Dynamic Dialect Handling**: Connect to SQLite, PostgreSQL, or MySQL via a single unified API (`c_orm_dialect_t`). Switch out databases without modifying a single line of your application code.
- **Connection Pooling**: Built-in thread-safe database connection pool. Optimizes resource utilization for multi-threaded applications.
- **Fluent Query Builder**: Safely construct SELECT statements without raw string concatenation, natively preventing SQL injection.
- **Abstracted Schema Migrations**: Alembic-style schema migrations and version tracking, integrated via the `cdd-c` library.
- **CRUD Code Generation**: Generate pure C structs and CRUD boilerplates straight from your DB Schema using the integrated `cdd-c` native code generation API.
- **Async Execution Simulation**: Queue-based async execution mechanisms designed for easy integration with event loops (like libuv, epoll, or select).

## Philosophy

`c-orm` operates on the principle that C developers deserve safe, robust, and modern tooling without sacrificing the explicit control and portability that C is famous for. 

By pushing the complexity into the code generation step (AOT) and abstracting the SQL dialects, `c-orm` minimizes runtime overhead. We aggressively avoid dynamic memory allocation where stack allocation suffices, and we provide deterministic failure paths over crashing.

## Feature Status

| Feature | Status | Description |
| :--- | :---: | :--- |
| **Dynamic Hydration & Abstract Routing** | ✅ | Seamless fallback to `cdd_c_abstract_struct_t` for raw queries. |
| **Spatial Types Support** | ✅ | `C_ORM_TYPE_POINT` and `C_ORM_TYPE_POLYGON` mapping to native structs or raw WKB binaries. |
| **AOT Specific Structs Projection** | ✅ | High-performance memory mapping straight from SQL driver buffer to specific struct instances. |
| **Automatic UUID Generation** | ✅ | Auto-generates UUID v4 strings for empty string primary keys. |
| **SQLite Backend** | ✅ | Full support for SQLite (amalgamation natively built or linked dynamically). |
| **PostgreSQL Backend** | ✅ | Full support via native libpq integration. |
| **MySQL Backend** | ✅ | Full support via native mysqlclient integration. |
| **Connection Pooling** | ✅ | Thread-safe connection pool implemented. |
| **Query Builder** | ✅ | Fluent, type-safe API for SELECT, INSERT, UPDATE, DELETE. |
| **Migrations** | ✅ | Supports schema diffing and version tracking using `cdd-c`. |
| **Code Generation** | ✅ | Native struct mapping and AST reflection via `cdd-c`. |
| **Async Driver** | ✅ | Event-loop aware abstractions for non-blocking I/O. |
| **Identity Map** | ✅ | High-performance memory tracking layer resolving active references. |
| **L1 Cache** | ✅ | Thread-safe query caching layer intercepting duplicate queries. |
| **Thread Safety** | ✅ | Completely re-entrant structures mapping sessions natively. |
| **Sharding Engine** | ✅ | Horizontal data partitioning across connections. |
| **Advanced Security** | ✅ | SQL sanitization, Transparent Encryption hooks, Strict type mapping. |
| **BLOB Streaming API** | ✅ | Gigabyte+ native memory-mapped abstractions for PG/SQLite. |
| **ORM Object Mapping** | ✅ | Full automated struct-to-row mapping implemented. |

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
- `C_ORM_LTO`: Enable Link-Time Optimization (IPO) for maximum runtime performance.
- `C_ORM_RUNTIME_CHECKS_RTC1`: Enable MSVC `/RTC1` (equivalent to `/RTCsu`).
- `C_ORM_RUNTIME_CHECKS_RTCS`: Enable MSVC `/RTCs` (Stack Frame runtime checking).
- `C_ORM_RUNTIME_CHECKS_RTCU`: Enable MSVC `/RTCu` (Uninitialized local usage checks).
- `C_ORM_STRICT_WARNINGS`: Enable `-Wall -Wextra -pedantic` (GCC/Clang) or `/W4 /WX` (MSVC).

### CRT & Linking (MSVC Specific)
- `C_ORM_STATIC_CRT`: Use Static CRT (`/MT`, `/MTd`). Recommended for standalone distributions without dependency on vcredist.
- `C_ORM_SHARED_CRT`: Use Shared CRT (`/MD`, `/MDd`).
- `BUILD_SHARED_LIBS`: Build `c-orm` as a dynamic shared library (`.dll` or `.so`) instead of a static library (`.lib` or `.a`).

### Character Sets & Threading
- `C_ORM_UNICODE`: Define `UNICODE` and `_UNICODE` for wide-character Win32 API interactions.
- `C_ORM_ANSI`: Define `_MBCS` for multi-byte character sets.
- `C_ORM_MULTI_THREADED` (Default: ON): Enable internal thread-safety mechanisms (mutexes). Set to `OFF` for bare-metal single-threaded environments to save overhead.

## Supported Compilers & Platforms

`c-orm` uses continuous integration to guarantee compatibility across:
- **Windows:** MSVC 2005, MSVC 2010, MSVC 2015, MSVC 2019, MSVC 2022, MSVC 2026.
- **Linux:** GCC 4.8 through GCC 14. Clang 3.8 through Clang 18.
- **Mac OS:** Apple Clang.
- **Toolchains:** MinGW-w64, Cygwin, MSYS2.

## Documentation
- See [USAGE.md](USAGE.md) for detailed code examples and quickstart workflows.
- See [ARCHITECTURE.md](ARCHITECTURE.md) for internal design details, memory management, and code-generation architecture.
- See [ROADMAP.md](ROADMAP.md) for upcoming features and long-term project goals.
- See [skills.md](skills.md) for contributor requirements and development workflows.

---

## License

Licensed under either of

- Apache License, Version 2.0 ([LICENSE-APACHE](LICENSE-APACHE) or <https://www.apache.org/licenses/LICENSE-2.0>)
- MIT license ([LICENSE-MIT](LICENSE-MIT) or <https://opensource.org/licenses/MIT>)

at your option.

### Contribution

Unless you explicitly state otherwise, any contribution intentionally submitted
for inclusion in the work by you, as defined in the Apache-2.0 license, shall be
dual licensed as above, without any additional terms or conditions. Please refer to [skills.md](skills.md) before submitting Pull Requests.
