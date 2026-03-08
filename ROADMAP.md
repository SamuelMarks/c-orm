# c-orm Roadmap

This document outlines the planned features and architectural direction for `c-orm`. Our primary goal is to provide a safe, ergonomic, and performant Object-Relational Mapping (ORM) experience in C.

## 🚀 Near-Term Goals

### Serde/Diesel-Style Struct Mapping
We aim to bring the developer experience of modern ORMs (like Rust's Diesel) and serialization frameworks (like Serde) to C. 

#### Fully Typed SQL Table Representations
Tables will be directly represented by fully typed C `struct`s. A database schema should map 1:1 to your C data structures, enabling type-safe database interactions.

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
Reading and writing these structs should be as frictionless as possible. We plan to implement macros or code-generation utilities that provide out-of-the-box CRUD operations for your defined structures:

* **Create (Insert):** Directly pass a `struct` to be inserted into the database.
* **Read (Select):** Fetch rows directly into a `struct` or an array of `struct`s, automatically handling the field mapping.
* **Update:** Modify a `struct` in memory and persist the changes back to the database.
* **Delete:** Remove records using the struct's primary key.

#### Array and Batch Operations
Support for efficiently reading and writing arrays of structs to handle multiple records in a single query (e.g., batch inserts, bulk reads).

```c
// Example conceptual API
struct Organisation orgs[10];
size_t count = c_orm_find_all_organisations(db, orgs, 10);

struct Organisation new_org = { .id = 1, .name = "Acme Corp" };
c_orm_insert_organisation(db, &new_org);
```

## 🛤️ Medium to Long-Term Goals

* **Advanced Code Generation:** Expand the `db_codegen` capabilities to automatically parse SQL schema files or metadata and generate the corresponding C structs and CRUD boilerplate.
* **Query Builder:** A fluent, type-safe (as much as C allows) query builder API to construct complex `WHERE` clauses, `JOIN`s, and aggregations without raw SQL strings.
* **Schema Migrations:** Tools to track, generate, and apply database schema changes over time.
* **Multi-Backend Support:** Abstract the underlying database driver to support SQLite, PostgreSQL, and MySQL seamlessly.
* **Relationship Management:** Built-in handling for One-to-One, One-to-Many, and Many-to-Many relationships between mapped structs.
