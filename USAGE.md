# Usage Guide

This guide covers the fundamental workflows for using `c-orm`. All functions in `c-orm` (except for basic initializers and destructors) return an `int` representing a status code, where `C_ORM_OK` (`0`) indicates success.

## 1. Connecting to a Database

Use `c_orm_connect` to instantiate a connection handle. You must specify the dialect and the connection string.

```c
#include <c_orm/c_orm.h>
#include <stdio.h>

int main() {
    c_orm_db_t* db = NULL;
    int res = c_orm_connect(&db, C_ORM_DIALECT_SQLITE, "my_database.db");

    if (res != C_ORM_OK) {
        printf("Failed to connect: %d\n", res);
        return 1;
    }

    printf("Connected successfully.\n");
    c_orm_disconnect(db);
    return 0;
}
```

## 2. Executing Queries

You can execute raw queries or parameterized queries.

### Raw Queries
```c
int res = c_orm_execute(db, "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT)");
if (res == C_ORM_OK) {
    res = c_orm_execute(db, "INSERT INTO users (name) VALUES ('Alice')");
}
```

### Parameterized Queries
Binding parameters protects against SQL injection.

```c
c_orm_param_t params[2];

params[0].type = C_ORM_PARAM_INTEGER;
params[0].value.int_val = 1;

params[1].type = C_ORM_PARAM_TEXT;
params[1].value.text_val = "Bob";

int res = c_orm_execute_params(db, "UPDATE users SET name = ? WHERE id = ?", params, 2);
if (res != C_ORM_OK) {
    printf("Parameter update failed.\n");
}
```

## 3. Fluent Query Builder

Instead of writing raw SQL, use the query builder for `SELECT` queries.

```c
c_orm_query_t* q = NULL;
char* sql_string = NULL;

int res = c_orm_query_create(&q, db, "users");
if (res == C_ORM_OK) {
    c_orm_query_select(q, "id, name");
    c_orm_query_where(q, "age >= 18");
    c_orm_query_order_by(q, "name DESC");
    c_orm_query_limit(q, 10);

    /* Render SQL (for debugging or manual execution) */
    res = c_orm_query_build(q, &sql_string);
    if (res == C_ORM_OK) {
        printf("Generated Query: %s\n", sql_string);
        free(sql_string);
    }

    /* Or execute it directly */
    res = c_orm_query_execute(q);

    c_orm_query_destroy(q);
}
```

## 4. Abstract Struct Hydration and Dynamic Queries

`c-orm` utilizes the `cdd-c` library to automatically generate specific struct mappings. However, when executing complex aggregations (e.g., `COUNT`, `SUM`), dynamic SQL queries, or retrieving raw untyped metadata, `c-orm` elegantly falls back to a dynamic key-value store called `cdd_c_abstract_struct_t`.

```c
#include <c_orm/c_orm_abstract.h>

struct CddCAbstractStructArray abstract_arr;
const char* query = "SELECT is_active, COUNT(id) as total FROM users GROUP BY is_active";

int res = c_orm_find_all_abstract(db, query, &abstract_arr);
if (res == C_ORM_OK) {
    /* Iterate results dynamically mapped to abstract variant fields */
    size_t i;
    for (i = 0; i < abstract_arr.length; i++) {
        struct CddCAbstractStruct* row = &abstract_arr.data[i];
        /* Do something with row */
    }
    c_orm_abstract_free(&abstract_arr);
}
```

## 5. Connection Pooling

For multi-threaded environments (like web servers), use a connection pool to recycle database connections safely.

```c
c_orm_pool_t* pool = NULL;
c_orm_db_t* db_conn = NULL;

/* Create a pool of 5 connections */
int res = c_orm_pool_create(&pool, C_ORM_DIALECT_POSTGRES, "user=postgres password=secret", 5);

if (res == C_ORM_OK) {
    /* Thread 1: Acquire and use */
    if (c_orm_pool_acquire(pool, &db_conn) == C_ORM_OK) {
        c_orm_execute(db_conn, "SELECT 1");
        c_orm_pool_release(pool, db_conn);
    }
    c_orm_pool_destroy(pool);
}
```

## 6. Schema Codegen

`c-orm` uses the integrated `cdd-c` native code generation API to create C structural definitions and boilerplate CRUD functions directly from a schema.

```c
#include <c_orm/c_orm_codegen.h>

/* Generate models from a SQL schema file */
int res = c_orm_codegen_generate("schema.sql", "output_dir");
if (res == C_ORM_OK) {
    printf("Models generated successfully.\n");
}
```

## 7. Asynchronous Execution

`c-orm` provides an asynchronous execution mechanism allowing queries to be queued and completed via event-loop polling. This makes `c-orm` perfectly suited for asynchronous runtime integrations (e.g. `epoll`, `libuv`).

```c
void my_async_callback(int status, void* user_data) {
    if (status == C_ORM_OK) {
        printf("Async query completed successfully.\n");
    }
}

/* Queue the query */
int res = c_orm_execute_async(db, "UPDATE users SET age = age + 1", my_async_callback, NULL);

if (res == C_ORM_OK) {
    /* Inside your event loop, poll to process the queue */
    while (event_loop_is_running()) {
        c_orm_poll_async(db);
        sleep_or_wait_for_io();
    }
}
```

## 8. Spatial Types Support

`c-orm` natively maps spatial objects (`C_ORM_TYPE_POINT`, `C_ORM_TYPE_POLYGON`) directly to native memory or fallback abstract raw binary (WKB) via `c_orm_point_t` and `c_orm_polygon_t`.

```c
c_orm_point_t point;
point.x = 40.7128;
point.y = -74.0060;

int res = c_orm_insert_point(db, "locations", "coordinates", &point);
if (res == C_ORM_OK) {
    printf("Point inserted successfully.\n");
}
```

## 9. Transactions

You can explicitly control the flow of database transactions.

```c
int res = c_orm_transaction_begin(db);

if (res == C_ORM_OK) {
    int update1 = c_orm_execute(db, "UPDATE accounts SET balance = balance - 100 WHERE id = 1");
    int update2 = c_orm_execute(db, "UPDATE accounts SET balance = balance + 100 WHERE id = 2");

    if (update1 == C_ORM_OK && update2 == C_ORM_OK) {
        c_orm_transaction_commit(db);
    } else {
        c_orm_transaction_rollback(db);
    }
}
```

## 10. CRUD Operations (Mapped Structs)

Once `c-orm` has generated the specific C structs and mapping definitions via `cdd-c`, you can perform strictly typed CRUD operations seamlessly.

```c
#include "generated_models.h"

struct User new_user;
new_user.id = 1;
/* Using Safe CRT or strncpy */
strncpy(new_user.name, "Charlie", sizeof(new_user.name) - 1);
new_user.name[sizeof(new_user.name) - 1] = '\0';
new_user.age = 25;

/* Create (Insert) */
int res = c_orm_insert_user(db, &new_user);
if (res == C_ORM_OK) {
    printf("User inserted.\n");
}

/* Read (Select single) */
struct User fetched_user;
res = c_orm_get_user(db, 1, &fetched_user);
if (res == C_ORM_OK) {
    printf("Fetched user: %s, Age: %d\n", fetched_user.name, fetched_user.age);
}

/* Update */
fetched_user.age = 26;
res = c_orm_update_user(db, &fetched_user);

/* Delete */
res = c_orm_delete_user(db, 1);
```
