# Query Builder Tutorial

The `c-orm` query builder provides a fluent, AST-based API for generating complex SQL queries dynamically while protecting against injection vulnerabilities through automatic parameter tracking and string escaping.

## Core Concepts

The builder operates across three phases:
1. **AST Generation**: Creating a structural representation using fluent method chains (`q->select_(...)`, `q->where(...)`).
2. **Translation**: Converting the AST into dialect-specific raw SQL and extracting dynamic values into a parameterized array (`c_orm_query_to_sql`).
3. **Execution**: Running the query via the target driver (`c_orm_query_execute`, `c_orm_query_fetch_all`).

### Initializing a Query

All fluent operations begin with `c_orm_query_t`:

```c
c_orm_query_t *q;
c_orm_query_new(&q);
// ... build query ...
c_orm_query_free(q);
```

Memory allocations during the build phase are governed by an internal `c_orm_arena_t` which is automatically destroyed during `c_orm_query_free`.

### Basic Selects and Conditions

Use standard mapping functions to build out queries:

```c
q->select_(q, "id, username, email")
 ->from(q, "users")
 ->order_by(q, "created_at", 1) // 1 = DESC, 0 = ASC
 ->limit(q, 10);
```

### Supported Operators and Functions

We provide syntactic macros to eliminate verbosity:

- **Comparisons**: 
  - `C_ORM_EQ(col, val)` / `C_ORM_EQ_NUM(col, val)`
  - `C_ORM_NEQ(col, val)` / `C_ORM_NEQ_NUM(col, val)`
  - `C_ORM_GT(col, val)` / `C_ORM_GT_NUM(col, val)`
  - `C_ORM_LT(col, val)` / `C_ORM_LT_NUM(col, val)`
- **Advanced Filtering**:
  - `C_ORM_LIKE(col, val)`
  - `C_ORM_IN(col, val_list)`
  - `C_ORM_IS_NULL(col)`
  - `C_ORM_IS_NOT_NULL(col)`
  - `C_ORM_BETWEEN(col, low, high, is_string)`
- **Aggregation**:
  - `C_ORM_COUNT(col, alias)`
  - `C_ORM_SUM(col, alias)`
  - `C_ORM_AVG(col, alias)`
- **Math/Date Functions**:
  - `C_ORM_NOW(alias)`
  - `C_ORM_DATE_ADD(args, alias)`
- **Subqueries**:
  - `C_ORM_EXISTS(sub_query_ptr)`
  - `C_ORM_NOT_EXISTS(sub_query_ptr)`

### Condition Chains

Conditions must be registered utilizing the `WHERE` clauses. 

```c
C_ORM_WHERE(C_ORM_EQ_NUM("age", "18"));
C_ORM_AND_WHERE(C_ORM_LIKE("username", "admin%"));
C_ORM_OR_WHERE(C_ORM_IS_NULL("deleted_at"));
```

*(Note: Under the hood `C_ORM_WHERE` translates to `q->where(q, ...)`)*

## Complex Query Examples (JOINs + Grouping)

### Joining Tables

The query builder naturally handles `INNER`, `LEFT`, and `RIGHT` joins.

```c
c_orm_query_t *q;
c_orm_query_new(&q);

q->select_(q, "users.username, profiles.avatar_url")
 ->from(q, "users")
 ->left_join(q, "profiles", C_ORM_EQ("users.id", "profiles.user_id"));

// Executes cleanly
c_orm_query_execute(db, q);
c_orm_query_free(q);
```

### Grouping and Aggregation

```c
c_orm_query_t *q;
c_orm_query_new(&q);

q->select_(q, "department, COUNT(id) AS emp_count")
 ->from(q, "employees")
 ->group_by(q, "department")
 ->having(q, C_ORM_GT_NUM("COUNT(id)", "5"));

c_orm_query_free(q);
```

## AST Memory Management

When `c_orm_query_new` is invoked, a `c_orm_arena_t` block is initialized.
- **Constant Time Allocations**: All AST nodes (e.g. `c_orm_ast_where_t`, `c_orm_ast_limit_t`) are carved from contiguous blocks within the arena preventing fragmentation.
- **Deep Cloning**: When invoking `q->clone(q, &cloned_q)`, a completely new arena is mapped, duplicating the literal graph safely.
- **Teardown**: Executing `c_orm_query_free` purges the entire arena uniformly, rendering garbage collection completely `O(1)`.

## Dialect Specific Documentation Limits

The `c_orm_query_to_sql` step generates specific SQL bound parameters based on target dialects:
- **SQLite / MySQL**: Produces positional `?` variables.
- **PostgreSQL**: Iteratively produces `$1, $2, $3` variables seamlessly.

**Limits:**
- Dialects currently extract parameter strings natively. Non-string numerics (`C_ORM_EQ_NUM`) append directly to AST strings without parameterized extractions for execution speed, assuming sanitized input logic in the API bounds.
- Window functions (`OVER`) and CTEs (`WITH`) may fail if targeted against unsupported or older dialets (e.g. SQLite < 3.25). Validate target DB constraints before chaining.

## Integrating Query Builder with Relationship Management

Relationship Management dynamically interacts with the Query Builder for lazy and eager loading models automatically resolving N+1 situations. 
When `c_orm_load_relation` determines a related structure is unloaded, it formulates an internal AST querying the explicit foreign key:

```c
// Behind the scenes of c_orm_load_relation:
q->select_(q, "*")
 ->from(q, rel->target_table)
 ->where(q, q->eq(q, rel->foreign_key, local_primary_key_val, 1));
```
This is seamlessly passed to the `c_orm_query_fetch_all` array generator if `ONE_TO_MANY`, or `c_orm_query_fetch_one` for explicit `ONE_TO_ONE` linkages preventing the need for manual SQL generation across complex relation chains.
