# c-orm Roadmap

This document outlines the planned features and architectural direction for `c-orm`. Our primary goal is to provide a safe, ergonomic, and performant Object-Relational Mapping (ORM) experience natively in C.

## 🚀 Near-Term Goals

### Serde/Diesel-Style Struct Mapping
We aim to bring the developer experience of modern ORMs (like Rust's Diesel) and serialization frameworks (like Serde) to C.

#### Fully Typed SQL Table Representations
Tables will be directly represented by fully typed C `struct`s. A database schema should map 1:1 to your C data structures, enabling type-safe database interactions natively.

**Example:**
```sql
CREATE TABLE Organisation (
    id INT PRIMARY KEY,
    name VARCHAR(255) NOT NULL,
    created_at TIMESTAMP
);
```

Maps directly to:
```c
struct Organisation {
    int id;
    char name[255];
    time_t created_at;
};
```

#### Seamless CRUD Operations for Structs
Reading and writing these structs should be as frictionless as possible. We plan to implement macros and code-generation utilities that provide out-of-the-box CRUD operations for your defined structures without writing a single line of serialization code:

* **Create (Insert):** Directly pass a `struct` pointer to be inserted into the database.
* **Read (Select):** Fetch rows directly into a `struct` or an array of `struct`s, automatically handling the field mapping, bounds checking, and data alignment.
* **Update:** Modify a `struct` in memory and persist the changes back to the database.
* **Delete:** Remove records using the struct's primary key mapping.

#### Array and Batch Operations
Support for efficiently reading and writing arrays of structs to handle multiple records in a single query (e.g., batch inserts, bulk reads) bypassing the N+1 problem inherent in simple iterations.

```c
// Example conceptual API
struct Organisation orgs[10];
size_t count = c_orm_find_all_organisations(db, orgs, 10);

struct Organisation new_org = { .id = 1, .name = "Acme Corp" };
c_orm_insert_organisation(db, &new_org);
```

## 🛠️ Medium to Long-Term Goals

### Advanced Query Builder Expansion
* Expand the fluent query builder API (`c_orm_query_t`) to construct complex `JOIN`s, nested `GROUP BY`, and sub-query aggregations without ever falling back to raw SQL strings.

### Advanced Schema Migrations
* Fully implement real schema diffing via `cdd-c` to track, generate, and execute database schema changes over time across all three supported dialects (SQLite, Postgres, MySQL).
* Automatic up/down migration script generation directly from header file diffs.

### Relationship Management
* Built-in automated handling for One-to-One, One-to-Many, and Many-to-Many relationships between mapped structs.
* Implement Eager (via `JOIN`) and Lazy (via proxy hooks) loading strategies.

### Ecosystem and Bindings
* While `c-orm` is built in C, its predictable ABI makes it ideal for language bindings. We intend to officially support:
  * **Lua:** via LuaJIT FFI for embedded application scripting.
  * **Python:** via `cffi` to allow using Python scripts to manage C-based database schemas.
  * **Zig / Nim:** as a high-performance natively-compiled alternative to their standard library ORMs.

### Performance Targets
* Optimize the driver abstractions to hit targets capable of streaming or querying 1,000,000 rows per second on modern hardware, making it suitable for HFT (High-Frequency Trading) data logging or game server backend telemetry.

## 🤝 Community Goals
- Expand the CI/CD pipeline to test across embedded targets (e.g., ARM Cortex-M, ESP32) ensuring no regressions are introduced that would bloat the ROM.
- Standardize the plugin API so community members can write additional dialects (e.g., Oracle, SQL Server) without modifying the core `c-orm` source.
