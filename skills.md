# Developer Skills Required (skills.md)

This repository requires specific technical skills and engineering discipline to maintain, debug, and extend safely.

## C Programming & Systems Engineering
- **Strict ISO C89 / C90:** Complete fluency in ISO C90 (`-std=c90 -pedantic -Werror`). All variable declarations must appear at the top of lexical scopes, `//` comments and C99 VLAs are prohibited, and headers like `<stdint.h>` or `<stdbool.h>` must be guarded for pre-C99 MSVC toolchains (down to MSVC 2005).
- **Universal Error-Enum Percolation:** Every non-mathematical function must return `c_orm_error_t` (`C_ORM_OK == 0`), pass output values through pointer parameters (`**out`), and percolate non-zero error codes assertively without discarding results.
- **Deterministic Memory Discipline:** Mastery of custom allocator wrappers (`c_orm_system_malloc`, `c_orm_system_calloc`, `c_orm_system_realloc`, `c_orm_system_free`), bump arenas (`c_orm_arena_t`), and deterministic Out-Of-Memory (OOM) fault-injection testing (`c_orm_set_allocators`).
- **Bounds-Checked String & Wire Manipulation:** Safe byte-buffer and string handling via `c_orm_safe_crt.h` (`c_orm_sprintf`, `c_orm_strcpy_s`, `c_orm_strcat_s`) and exact big-endian / BCD binary codecs without buffer overruns or strict-aliasing/alignment violations (`-Wcast-align`).

## Cross-Platform Engineering & Quirks
- **MSVC & Windows Runtime Compatibility:** Proficiency with Microsoft Safe CRT (`_s` routines), `/MT`/`/MTd` vs `/MD`/`/MDd` CRT linkage, `UNICODE` vs `ANSI` (`_MBCS`) builds, `/RTC1`/`/RTCs`/`/RTCu` runtime checks, and cross-testing Windows binaries under Wine (`build_msvc_wine.sh`).
- **Zero-`<windows.h>` Win32 & ODBC ABI:** Never `#include <windows.h>`. Forward-declare exact `__declspec(dllimport) ... __stdcall` Win32 synchronization (`CRITICAL_SECTION`, `Sleep`), dynamic loader (`LoadLibraryA`/`W`, `GetProcAddress`), cryptographic (`BCryptGenRandom`, DPAPI), and ODBC 3.52 (`c_orm_odbc_abi.h`) ABI signatures directly.
- **POSIX, Cygwin & Emscripten Portability:** Familiarity with `pthread_mutex_t`, `dlopen`/`dlsym`, POSIX timers (`nanosleep`), single-threaded builds (`C_ORM_NO_THREADS`), and Emscripten WebAssembly `IDBFS` persistence.

## Build Systems & CI/CD
- **Modern CMake:** Target-scoped properties, `FetchContent_MakeAvailable` tracking `master` for internal `SamuelMarks/*` libraries, Link-Time Optimization (`CMAKE_INTERPROCEDURAL_OPTIMIZATION`), shared DLL symbol export (`C_ORM_EXPORT`), and shared `vcpkg` toolchain integration.
- **Static Analysis & Pre-Commit Gates:** Maintaining zero-warning builds across GCC, Clang, MSVC, and `cppcheck`, alongside `libclang` AST hooks (`scripts/check_enum_returns_clang.py`, `check_no_discard_void_clang.py`, and `clang-format --style=LLVM` with single `/* clang-format off/on */` include guards).

## Database Drivers & Mainframe Data Pipelines
- **Driver VTable Abstraction:** Implementing and maintaining `c_orm_driver_vtable_t` backends across SQLite3, PostgreSQL (`libpq`), MySQL/MariaDB (`libmysqlclient`), Generic ODBC 3.52 (`odbc32` / `unixODBC`), IBM Db2 CLI (`libdb2`, WLM registers, LOB locators), and the built-in ephemeral in-memory SQL engine.
- **SQL Dialect AST & Migrations:** Translating AST queries (`c_orm_ast_query_t`) across dialect-specific syntax (`$1..$N`, `ON CONFLICT`, `ON DUPLICATE KEY`, Db2 `MERGE INTO` / `FINAL TABLE` / `FETCH FIRST` / `WITH UR`), connection pooling (`c_orm_pool_t`), and versioned schema migrations.
- **Mainframe Financial & Copybook Codecs:** Working with 38-digit base-$10^9$ fixed-point `c_orm_decimal_t`, `SQL_NUMERIC_STRUCT`, COBOL `COMP-3` packed decimal, Zoned Decimal overpunch signs, `EBCDIC` (`IBM037`, `IBM1047`, `IBM500`, `IBM1140`), UTF-16 `GRAPHIC`, and bidirectional COBOL Copybook (`.cpy` / `DCLGEN`) parsing and code generation.

## Quality Assurance & Coverage Standards
- **100% Documentation & Core Test Coverage:** Every public/internal function, struct, field, parameter, and return value must carry Doxygen comments (`100%` doc coverage via `scripts/generate_undercovered.py --doc`), and every core library module in `src/` must maintain `100%` line, function, and branch coverage under `gcovr` using real `greatest.h` assertions.
