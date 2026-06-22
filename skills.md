# Developer Skills Required (skills.md)

This repository requires specific technical skills and knowledge sets to successfully maintain, debug, and extend.

## C Programming & Systems Engineering
- **C89/ANSI C:** Complete proficiency in the legacy C89 standard. Familiarity with archaic scope declarations, the lack of `//` comments, and the absence of `stdint.h` guarantees in very old compilers (though `stdint.h` is polyfilled via macros where necessary).
- **Manual Memory Management:** Deep understanding of pointers, double pointers for out-parameters (`**out`), defensive allocation strategies, and preventing memory leaks using strictly `malloc` / `calloc` / `free`.
- **String Manipulation:** Working safely with raw byte buffers and null-terminated strings without higher-level standard libraries, mitigating buffer overflow risks intrinsically.

## Cross-Platform Engineering & Quirks
- **MSVC Quirks:** Knowledge of Microsoft Visual C++ compiler specifics, including Safe CRT functions (`_s` variants) and legacy C-runtime requirements going back to MSVC 2005. You must know how to conditionally map POSIX APIs to their Windows equivalents.
- **Windows Headers:** Understanding of Windows SDK header hierarchies. Specifically, knowing how to extract primitives (like `CRITICAL_SECTION`) from `<windef.h>` and `<winbase.h>` while aggressively bypassing the monolithic `<windows.h>` to optimize binary size and compilation speeds.
- **POSIX API:** Familiarity with Linux/macOS standard threading (`pthread_mutex_t`) and standard POSIX IO abstractions.

## Build Systems & CI/CD
- **Advanced CMake:**
  - Modern CMake target properties (`target_compile_definitions`, `target_link_libraries`, `target_include_directories`).
  - Handling MSVC-specific generator expressions (e.g., `MSVC_RUNTIME_LIBRARY` for selecting `/MT` vs `/MD`).
  - Interprocedural Optimization (IPO / LTO) and Compiler/Linker Flags.
  - External dependency integration via `FetchContent` and modular subdirectories.
- **GitHub Actions:** Reading and maintaining YAML-based CI pipelines to ensure that builds pass simultaneously across GCC, Clang, and multiple generations of MSVC.

## Database Systems
- **Dialect Abstracting:** Understanding the C-API interfaces for the big three: SQLite3 (amalgamations), libpq (PostgreSQL natively), and MySQL/MariaDB client libraries.
- **SQL Knowledge:** Capability to write cross-compatible ANSI SQL statements and construct Abstract Syntax Trees (AST) representing schema architectures dynamically.
- **Transactions and Concurrency:** Understanding transaction isolation levels, locks, and how connections map cleanly to thread pools.

## Quality Assurance & Debugging
- **Unit Testing:** Using minimalistic C testing frameworks (like `greatest.h` or custom macros) to assert invariants and isolate failures.
- **Code Coverage:** Tracing conditional branching meticulously to ensure 100% test coverage across all compilation architectures.
- **Documentation:** Authoring strict Doxygen-compliant documentation for public APIs and maintaining descriptive READMEs and usage examples.
- **Tooling:** Familiarity with `gdb` or `lldb` for tracing segmentation faults, and `valgrind` or ASan (AddressSanitizer) for memory leak verification.
