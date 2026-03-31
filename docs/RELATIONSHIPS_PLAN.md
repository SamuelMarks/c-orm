# C-ORM Relationship Management Plan

This document outlines the API design, paradigms, and logic for Phase 1 of the Relationship Management implementation in `c-orm`.

## Definitions

### Mapping APIs

1.  **One-to-One (`C_ORM_RELATION_ONE_TO_ONE`)**: Defines a "Has One" relationship where the current struct holds the primary key and the target struct holds the foreign key.
2.  **Belongs-To (`C_ORM_RELATION_BELONGS_TO`)**: The inverse of One-to-One/One-to-Many. The current struct holds the foreign key pointing to the target struct's primary key.
3.  **One-to-Many (`C_ORM_RELATION_ONE_TO_MANY`)**: Defines a "Has Many" relationship where the current struct holds the primary key and the target struct holds the foreign key, mapping to an array of target structs.
4.  **Many-to-Many (`C_ORM_RELATION_MANY_TO_MANY`)**: Requires a join table. Both sides act conceptually like "Has Many".

### Foreign Key Resolution Logic

-   **Implicit**: By default, the foreign key column is assumed to be `<target_table_name>_id` for `Belongs-To`, or `<local_table_name>_id` for `Has-One`/`Has-Many`.
-   **Explicit**: The user can provide explicit `foreign_key` and `local_key` values in `c_orm_relation_meta_t`.

### Lazy vs Eager Loading

-   **Lazy Loading**: A relationship pointer is initially `NULL` (or partially initialized via `c_orm_lazy_load_context_t` inside a proxy struct). Calling `c_orm_load_relation` fetches the data only when needed. Ideal for lowering initial memory and query time if relations are often unaccessed.
-   **Eager Loading**: Relations are fetched immediately via `JOIN` or `WHERE IN (...)` during the primary query.
-   **N+1 Mitigations**:
    -   When querying a list of records with a lazy relation, accessing it in a loop causes N queries.
    -   Instead of multiple SELECTs, `c_orm` provides bulk eager loading macros (e.g. `C_ORM_EAGER_LOAD_ALL(db, list, len, meta, rel_idx)`) which aggregates all FKs and issues a single `SELECT * FROM target WHERE id IN (...)` and maps them back in memory.

### Struct Macros Draft

Macros allow defining relationships in C structs for reflection:

```c
#define C_ORM_HAS_ONE(Type, name) Type *name;
#define C_ORM_BELONGS_TO(Type, name) Type *name;
#define C_ORM_HAS_MANY(Type, name) size_t num_##name; Type *name;
#define C_ORM_MANY_TO_MANY(Type, name) size_t num_##name; Type *name;
```

*Note: In `cdd-c` these are typically defined using `#pragma` or specific typedefs for reflection.*

### Cyclic Dependency Resolution

-   Relationships across tables might be cyclic (e.g. User has Profile, Profile has User).
-   To avoid infinite loops during initialization or eager loading, the ORM Identity Map tracks pointers being hydrated. If an object is already in the map (by ID/Table), the existing pointer is assigned rather than re-querying.
-   Lazy loading naturally breaks cycles because pointers start unhydrated.

### Cascading Rules

`c_orm_cascade_rule_t` defines behavior when a parent row is modified:

-   `C_ORM_CASCADE_NONE`: Database default / No action.
-   `C_ORM_CASCADE_DELETE`: Child rows are automatically deleted.
-   `C_ORM_CASCADE_SET_NULL`: Child rows have their foreign keys set to NULL.
-   `C_ORM_CASCADE_RESTRICT`: Database throws an error if dependent child rows exist.

These are translated directly into SQL constraints where supported (`ON DELETE CASCADE`, etc) and emulated in code via ORM lifecycle hooks if necessary.
