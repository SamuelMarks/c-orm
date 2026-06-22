# c-orm Relationships Guide

The `c-orm` relationships engine brings fully typed declarative bindings over standard ANSI C structs. Utilizing `cdd-c` code generation capabilities, developers can describe native mappings dynamically without sacrificing binary safety.

## Overview
c-orm supports 5 primary relation types mapping seamlessly between database paradigms and C memory:
- **`C_ORM_BELONGS_TO`**: Represents a `1:1` reverse association utilizing a local foreign key mapping to the parent target's PK.
- **`C_ORM_HAS_ONE`**: The counterpart forward-direction `1:1` mapping, where the target table points to your local primary key.
- **`C_ORM_HAS_MANY`**: Represents a `1:N` array grouping directly mapped via a proxy struct `Generic_Array`.
- **`C_ORM_MANY_TO_MANY`**: Represents an `N:M` association routing through an intermediate relational join table holding respective foreign keys.
- **`C_ORM_HAS_MANY_THROUGH`**: Resolves targets via pivot tables directly mapping intermediate associations cleanly.

## Lazy vs Eager Loading Trade-offs
### Lazy Loading
Lazy loading defers relationship SQL execution until strictly necessary at runtime.
- **Pros**: Low baseline memory allocation, significantly lighter starting query payloads. Safe on cyclic definitions.
- **Cons**: Severe risk of the N+1 query problem. Iterating through 1,000 records lazy loading relations equates to 1,001 distinct sequential DB queries overhead.

**Usage**:
```c
c_orm_lazy_load(db, &User_meta, &user, "posts");
```

### Eager Loading
Eager loading attempts to retrieve relational mappings instantly upon hydrating the primary object.
- **Pros**: Bypasses the N+1 query penalty utilizing intelligent batching mechanisms (`WHERE IN (...)`) resulting in exactly 2 queries regardless of list size (one for the parent batch, one for the child batch).
- **Cons**: Slightly higher burst memory overhead aggregating all elements upfront. Deep nesting `a -> b -> c` requires careful parsing.

**Usage**:
```c
c_orm_find_all_with_relation(db, &User_meta, "posts", &users_array);
```

## Solving N+1 Problems
Using `c_orm_find_all_with_relations` intelligently scales. Always utilize eager loading over `c_orm_lazy_load` natively inside a loop:

**Bad**:
```c
c_orm_find_all(db, &User_meta, &users);
for (i = 0; i < users.length; i++) {
   /* Triggers a blocking SQL call PER USER */
   c_orm_lazy_load(db, &User_meta, &users.data[i], "posts");
}
```

**Good**:
```c
const char *paths[] = {"posts"};
/* Pre-fetches completely with batch processing internally */
c_orm_find_all_with_relations(db, &User_meta, paths, 1, &users);
```

## Cascading Behaviors & Warnings
Relations enforce recursive rules explicitly. Utilizing `C_ORM_CASCADE_DELETE` instructs the ORM to trigger deep recursive cleanup on mapped child datasets synchronously BEFORE destroying the parent item (avoiding native Database FK violation errors for incompatible dialects).
- **`C_ORM_CASCADE_NONE`**: Fails immediately if DB enforces structural safety, OR leaves orphaned pointers.
- **`C_ORM_CASCADE_SET_NULL`**: Clears dependent columns instead of wiping out the child records.
- **`C_ORM_CASCADE_DELETE`**: Drastically destructive if improperly structured.

> **Warning**: Ensure structural logic avoids recursive infinite loops. Self-referencing trees (nodes pointing to nodes) mapped to cascade deletes can recursively scrub an entire database graph.

## Many-to-Many Join Tables Diagram
Many-to-Many relationships utilize an invisible structural third table linking entities transparently.

```text
[ User Table ]          [ user_roles Join Table ]          [ Role Table ]
+----+---------+        +---------+---------+              +----+--------+
| id | name    |        | user_id | role_id |              | id | title  |
+----+---------+        +---------+---------+              +----+--------+
| 1  | Alice   | -----> | 1       | 10      | -----------> | 10 | Admin  |
| 2  | Bob     | -----> | 2       | 10      |        /---> | 11 | Editor |
+----+---------+        | 2       | 11      | ------/      +----+--------+
                        +---------+---------+
```
`c_orm` utilizes `C_ORM_MANY_TO_MANY(..., "user_roles", "user_id", "role_id")` cleanly handling insertions across `c_orm_attach(...)` and dynamic sync deletions via `c_orm_sync(...)`.
