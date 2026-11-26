# Usage Guide

This guide covers the fundamental workflows for using `c-orm`. Every operational function in `c-orm` conforms to strict ISO C89 and returns a `c_orm_error_t` status enum (`C_ORM_OK` == `0` on success), passing outputs through pointer parameters so errors can be percolated assertively.

## 1. Connecting to a Database

Use `c_orm_connect_url` to dispatch any supported connection URI (`sqlite://`, `postgres://`, `mysql://`, `db2://`, `odbc://`, or `memory://`), or call a backend-specific connector such as `c_orm_sqlite_connect`.

```c
/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_sqlite.h"
#include <stdio.h>
/* clang-format on */

int main(void) {
  c_orm_db_t *db = NULL;
  c_orm_error_t rc;

  rc = c_orm_connect_url("sqlite://my_database.db", &db);
  if (rc != C_ORM_OK) {
    printf("Failed to connect (rc=%d)
", (int)rc);
    return 1;
  }

  printf("Connected successfully.
");
  rc = db->vtable->disconnect(db);
  if (rc != C_ORM_OK) {
    return 1;
  }
  return 0;
}
```

## 2. Executing Queries

Execute unparameterized DDL/DML statements with `c_orm_execute_raw`, or bind parameters safely through prepared statements (`c_orm_prepare_cached` / `db->vtable->prepare`).

### Raw Statements
```c
c_orm_error_t rc;

rc = c_orm_execute_raw(
    db, "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, age INTEGER)");
if (rc == C_ORM_OK) {
  rc = c_orm_execute_raw(db, "INSERT INTO users (id, name, age) VALUES (1, 'Alice', 30)");
}
```

### Parameterized Queries
Binding parameters protects against SQL injection and leverages the LRU statement cache when enabled (`c_orm_enable_statement_caching`).

```c
c_orm_query_t *stmt = NULL;
c_orm_error_t rc;
c_orm_error_t fin_rc;
int has_row = 0;

rc = c_orm_prepare_cached(db, "UPDATE users SET name = ? WHERE id = ?", &stmt);
if (rc == C_ORM_OK) {
  rc = db->vtable->bind_string(stmt, 1, "Bob");
  if (rc == C_ORM_OK) {
    rc = db->vtable->bind_int32(stmt, 2, 1);
  }
  if (rc == C_ORM_OK) {
    rc = db->vtable->step(stmt, &has_row);
  }
  fin_rc = c_orm_finalize_cached(db, stmt);
  if (rc == C_ORM_OK) {
    rc = fin_rc;
  }
}
```

## 3. Fluent AST Query Builder

Use `c_orm_ast_query_t` (`c_orm_ast.h`) to construct parameterized `SELECT`, `INSERT`, `UPDATE`, `DELETE`, `JOIN`, CTE, and window queries with arena-managed memory.

```c
/* clang-format off */
#include "c_orm_ast.h"
#include <stdio.h>
/* clang-format on */

c_orm_ast_query_t *q = NULL;
c_orm_ast_node_t *cond = NULL;
c_orm_query_params_t params;
char *sql_string = NULL;
c_orm_error_t rc;

rc = c_orm_query_new(&q);
if (rc == C_ORM_OK) {
  rc = q->select_(q, "id, name");
  if (rc == C_ORM_OK)
    rc = q->from(q, "users");
  if (rc == C_ORM_OK)
    rc = C_ORM_GTE_NUM(q, "age", 18, &cond);
  if (rc == C_ORM_OK)
    rc = q->where(q, cond);
  if (rc == C_ORM_OK)
    rc = q->order_by(q, "name", 1); /* 1 = DESC, 0 = ASC */
  if (rc == C_ORM_OK)
    rc = q->limit(q, 10);

  /* Render dialect-specific SQL and extracted bind parameters */
  if (rc == C_ORM_OK) {
    rc = c_orm_query_to_sql(q, C_ORM_DIALECT_SQLITE, &sql_string, &params);
    if (rc == C_ORM_OK) {
      c_orm_error_t free_rc;
      printf("Generated SQL: %s\n", sql_string);
      free_rc = c_orm_system_free(sql_string);
      if (rc == C_ORM_OK)
        rc = free_rc;
      free_rc = c_orm_query_params_cleanup(&params);
      if (rc == C_ORM_OK)
        rc = free_rc;
    }
  }

  {
    c_orm_error_t qfree_rc = c_orm_query_free(q);
    if (rc == C_ORM_OK)
      rc = qfree_rc;
  }
}
```

## 4. Abstract Struct Hydration and Dynamic Queries

When executing ad-hoc projections, `GROUP BY` aggregations, or untyped queries without a static C model, hydrate rows into `struct CddCAbstractStructArray` (`abstract_struct.h`).

```c
/* clang-format off */
#include "c_orm_api.h"
#include "abstract_struct.h"
/* clang-format on */

struct CddCAbstractStructArray abstract_arr;
c_orm_error_t rc;

rc = c_orm_find_all_abstract(
    db, "SELECT age, COUNT(id) AS total FROM users GROUP BY age",
    &abstract_arr);
if (rc == C_ORM_OK) {
  size_t i;
  for (i = 0; i < abstract_arr.length; ++i) {
    struct CddCAbstractStruct *row = &abstract_arr.data[i];
    (void)row;
  }
  rc = c_orm_abstract_free(&abstract_arr);
}
```

## 5. Connection Pooling

For multi-threaded services, `c_orm_pool_t` manages pre-warmed connections, maximum capacity bounds, and acquisition wait timeouts.

```c
c_orm_pool_t *pool = NULL;
c_orm_db_t *session = NULL;
c_orm_error_t rc;
c_orm_error_t cleanup_rc;

/* min_conns=2, max_conns=8, timeout_ms=500 */
rc = c_orm_pool_init("sqlite://my_database.db", 2, 8, 500, &pool);
if (rc == C_ORM_OK) {
  rc = c_orm_pool_acquire(pool, &session);
  if (rc == C_ORM_OK) {
    rc = c_orm_execute_raw(session, "SELECT 1");
    cleanup_rc = c_orm_pool_release(pool, session);
    if (rc == C_ORM_OK) {
      rc = cleanup_rc;
    }
  }
  cleanup_rc = c_orm_pool_destroy(pool);
  if (rc == C_ORM_OK) {
    rc = cleanup_rc;
  }
}
```

## 6. Code Generation & CLI (`c_orm_codegen.h` & `c-orm-cli`)

Generate strict C89 structs, array helpers, and `c_orm_table_meta_t` descriptors programmatically or via `c-orm-cli`:

```c
/* clang-format off */
#include "c_orm_codegen.h"
/* clang-format on */

c_orm_error_t rc = c_orm_codegen_generate("schema.sql", "src/models");
```

Or from the command line:
```sh
c-orm-cli sql2c schema.sql src/models
c-orm-cli cpy2c ACCTREC.cpy src/models
c-orm-cli cpy2sql ACCTREC.cpy schema_db2.sql
c-orm-cli sql2cpy schema.sql ACCTREC.cpy
c-orm-cli migrate --db sqlite://app.db --dir ./migrations
```

## 7. Asynchronous Callback Wrappers (`c-orm-async`)

Link against `c-orm-async` to use completion-callback CRUD entry points (`c_orm_insert_async`, `c_orm_find_all_async`). When built with `C_ORM_THREADING=OFF` or without an external worker loop attached, operations execute synchronously inline and invoke `cb` immediately.

```c
static c_orm_error_t on_insert_done(c_orm_error_t status, void *ctx) {
  (void)ctx;
  printf("Async insert completed with status: %d
", (int)status);
  return status;
}

/* Dispatches insert and triggers on_insert_done */
rc = c_orm_insert_async(db, &Users_meta, &user, on_insert_done, NULL);
```

## 8. Spatial Data (`C_ORM_TYPE_POINT` & `C_ORM_TYPE_POLYGON`)

Columns mapped with `C_ORM_TYPE_POINT` (`c_orm_point_t`) and `C_ORM_TYPE_POLYGON` (`c_orm_polygon_t`) serialize and hydrate `POINT(x y)` / `POLYGON((...))` WKT representations automatically through standard CRUD calls. Release dynamically allocated polygon vertices with `c_orm_free_columns(&Locations_meta, &loc)`.

## 9. Transactions & Savepoints

Ensure atomicity across multiple operations with explicit transactions and nested savepoints:

```c
c_orm_error_t rc;

rc = c_orm_transaction_begin(db);
if (rc == C_ORM_OK) {
  rc = c_orm_savepoint_create(db, "transfer_sp");
  if (rc == C_ORM_OK) {
    rc = c_orm_execute_raw(
        db, "UPDATE accounts SET balance = balance - 100 WHERE id = 1");
  }
  if (rc == C_ORM_OK) {
    rc = c_orm_execute_raw(
        db, "UPDATE accounts SET balance = balance + 100 WHERE id = 2");
  }
  if (rc == C_ORM_OK) {
    rc = c_orm_savepoint_release(db, "transfer_sp");
    if (rc == C_ORM_OK) {
      rc = c_orm_transaction_commit(db);
    }
  } else {
    c_orm_error_t rb_rc = c_orm_savepoint_rollback(db, "transfer_sp");
    c_orm_error_t tx_rc = c_orm_transaction_rollback(db);
    if (rb_rc != C_ORM_OK && tx_rc != C_ORM_OK) {
      rc = rb_rc;
    }
  }
}
```

## 10. Typed Struct Mapping, CRUD & Optimistic Locking

Define models inline with `C_ORM_STRUCT` (`c_orm_struct.h`) or generate them via `c-orm-cli sql2c`, then pass your struct and `c_orm_table_meta_t` descriptor to `c-orm`'s generic CRUD functions:

```c
/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_struct.h"
/* clang-format on */

#define ACCOUNT_FIELDS(X, S) \
  X(S, C_ORM_TYPE_INT32, int32_t, id) \
  X(S, C_ORM_TYPE_STRING, char *, owner) \
  X(S, C_ORM_TYPE_INT32, int32_t, version)

C_ORM_STRUCT(Account, ACCOUNT_FIELDS)
/* Note: mark Account_columns[0].is_pk = true or use sql2c-generated metadata */

/* Create, Read, Optimistic Update, and Delete */
c_orm_error_t run_account_crud(c_orm_db_t *db, const c_orm_table_meta_t *meta) {
  struct Account acct;
  struct Account fetched;
  c_orm_error_t rc;
  c_orm_error_t free_rc;

  acct.id = 1;
  acct.owner = "Charlie";
  acct.version = 1;

  rc = c_orm_insert(db, meta, &acct);
  if (rc != C_ORM_OK)
    return rc;

  memset(&fetched, 0, sizeof(fetched));
  rc = c_orm_find_by_id_int32(db, meta, 1, &fetched);
  if (rc != C_ORM_OK)
    return rc;

  /* Increment version and perform optimistic update (returns C_ORM_ERROR_NOT_FOUND on conflict) */
  fetched.version += 1;
  rc = c_orm_update_optimistic(db, meta, "version", &fetched);
  free_rc = c_orm_free_columns(meta, &fetched);
  if (rc != C_ORM_OK)
    return rc;
  if (free_rc != C_ORM_OK)
    return free_rc;

  return c_orm_delete_by_id_int32(db, meta, 1);
}
```

## 11. Unified URL Connections (`db2://`, `odbc://`, `sqlite://`, `postgres://`, `mysql://`, `memory://`)

Connect to enterprise, embedded, or ephemeral in-memory backends through `c_orm_connect_url`:

```c
/* clang-format off */
#include "c_orm_api.h"
/* clang-format on */

c_orm_db_t *db = NULL;
c_orm_error_t rc;

/* IBM Db2 with schema, isolation, and WLM accounting registers */
rc = c_orm_connect_url(
    "db2://db2inst1:secret@localhost:50000/SAMPLE?schema=FINANCE&isolation=CS&applname=ledger",
    &db);

/* Generic ODBC 3.52 */
rc = c_orm_connect_url(
    "odbc://sa:secret@localhost:1433/AppDB?driver=ODBC+Driver+18+for+SQL+Server",
    &db);
```

## 12. Lossless Fixed-Point `DECIMAL` & COBOL Copybook Wire Marshaling

Perform exact 38-digit base-10 financial arithmetic and marshal directly between C89 structs and IBM037 `EBCDIC` + `COMP-3` mainframe byte records:

```c
/* clang-format off */
#include "c_orm_copybook.h"
#include "c_orm_decimal.h"
#include "c_orm_ebcdic.h"
/* clang-format on */

c_orm_decimal_t balance, credit, updated;
char formatted[64];
c_orm_error_t rc;

rc = c_orm_decimal_from_str("150000.25", &balance);
if (rc == C_ORM_OK)
  rc = c_orm_decimal_from_str("2500.55", &credit);
if (rc == C_ORM_OK)
  rc = c_orm_decimal_add(&balance, &credit, &updated);
if (rc == C_ORM_OK)
  rc = c_orm_decimal_to_str(&updated, formatted, sizeof(formatted), NULL);
/* formatted == "152500.80" */
```

See [docs/TUTORIAL.md](docs/TUTORIAL.md), [docs/CRUD_QUICKSTART.md](docs/CRUD_QUICKSTART.md), [docs/QUERY_BUILDER.md](docs/QUERY_BUILDER.md), [docs/DB2_AND_ODBC.md](docs/DB2_AND_ODBC.md), [docs/COBOL_COPYBOOKS.md](docs/COBOL_COPYBOOKS.md), and [examples/cobol_db2_ledger.c](examples/cobol_db2_ledger.c) for complete walkthroughs.
