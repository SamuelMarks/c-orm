c-orm
=====
[![License](https://img.shields.io/badge/license-Apache--2.0%20OR%20MIT-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![Doc Coverage](https://img.shields.io/badge/docs-100%25-brightgreen.svg)](#)
[![Test Coverage](https://img.shields.io/badge/coverage-100%25-brightgreen.svg)](#)

An abstract, cross-platform Object Relational Mapper for C. Designed to be lightweight and compatible with SQLite and PostgreSQL.

It provides primitives analogous to Python's SQLAlchemy/Alembic or Rust's Diesel.

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

## Features
- Dynamic Dialect Handling (`c_orm_dialect_t`)
- CRUD Code Generation
- Abstracted Schema Migrations

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
