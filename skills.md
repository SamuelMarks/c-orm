# Developer Skills Required (skills.md)

This repository requires specific technical skills and knowledge sets to successfully maintain and extend. 

## C Programming
- **C89/ANSI C:** Complete proficiency in legacy C89 standard. Familiarity with archaic scope declarations, lack of `//` comments, and absence of `stdint.h` guarantees in very old compilers (though `stdint.h` is polyfilled where necessary).
- **Manual Memory Management:** Deep understanding of pointers, double pointers for out-parameters, and preventing memory leaks using strictly `malloc`/`calloc`/`free`.
- **String Manipulation:** Working safely with raw byte buffers and null-terminated strings without higher-level standard libraries.

## Cross-Platform Engineering
- **MSVC Quirks:** Knowledge of Microsoft Visual C++ compiler specifics, including Safe CRT functions (`_s` variants) and legacy C-runtime requirements going back to MSVC 2005.
- **Windows Headers:** Understanding of Windows SDK header hierarchies. Specifically, knowing how to extract primitives (like `CRITICAL_SECTION`) from `<windef.h>` and `<winbase.h>` while bypassing the monolithic `<windows.h>` to optimize binary size.
- **POSIX API:** Familiarity with Linux/macOS standard threading (`pthread_mutex_t`) and POSIX IO.

## Build Systems
- **Advanced CMake:**
  - Modern CMake target properties (`target_compile_definitions`, `target_link_libraries`).
  - Handling MSVC-specific generator expressions (e.g., `MSVC_RUNTIME_LIBRARY` for `/MT` vs `/MD`).
  - Interprocedural Optimization (IPO / LTO).
  - External dependency integration via `FetchContent`.

## Database Systems
- **Dialect Abstracting:** Understanding the C-API interfaces for SQLite3, libpq (PostgreSQL), and MySQL/MariaDB client libraries.
- **SQL Knowledge:** Capability to write cross-compatible ANSI SQL statements and construct Abstract Syntax Trees (AST) representing schema architectures.

## Quality Assurance
- **Unit Testing:** Using minimalistic C testing frameworks (like `greatest.h`).
- **Code Coverage:** Tracing conditional branching to ensure 100% test coverage.
- **Documentation:** Authoring strict Doxygen-compliant documentation for public APIs.
