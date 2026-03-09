# Usage Guide

This guide covers the fundamental workflows for using `c-orm`.

## 1. Connecting to a Database

Use `c_orm_connect` to instantiate a connection handle. You must specify the dialect and the connection string.

```c
#include <c_orm/c_orm.h>
#include <stdio.h>

int main() {
    c_orm_db_t* db = NULL;
    int res = c_orm_connect(&db, C_ORM_DIALECT_SQLITE, "my_database.db");
    
    if (res != 0) {
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
c_orm_execute(db, "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT)");
c_orm_execute(db, "INSERT INTO users (name) VALUES ('Alice')");
```

### Parameterized Queries
Binding parameters protects against SQL injection.

```c
c_orm_param_t params[2];

params[0].type = C_ORM_PARAM_INTEGER;
params[0].value.int_val = 1;

params[1].type = C_ORM_PARAM_TEXT;
params[1].value.text_val = "Bob";

c_orm_execute_params(db, "UPDATE users SET name = ? WHERE id = ?", params, 2);
```

## 3. Fluent Query Builder

Instead of writing raw SQL, use the query builder for `SELECT` queries.

```c
c_orm_query_t* q = NULL;
char* sql_string = NULL;

c_orm_query_create(&q, db, "users");
c_orm_query_select(q, "id, name");
c_orm_query_where(q, "age >= 18");
c_orm_query_order_by(q, "name DESC");
c_orm_query_limit(q, 10);

/* Render SQL (for debugging or manual execution) */
c_orm_query_build(q, &sql_string);
printf("Generated Query: %s\n", sql_string);
free(sql_string);

/* Or execute it directly */
c_orm_query_execute(q);

c_orm_query_destroy(q);
```

## 4. Connection Pooling

For multi-threaded environments (like web servers), use a connection pool to recycle database connections.

```c
c_orm_pool_t* pool = NULL;
c_orm_db_t* db_conn = NULL;

/* Create a pool of 5 connections */
c_orm_pool_create(&pool, C_ORM_DIALECT_POSTGRES, "user=postgres password=secret", 5);

/* Thread 1: Acquire and use */
if (c_orm_pool_acquire(pool, &db_conn) == 0) {
    c_orm_execute(db_conn, "SELECT 1");
    c_orm_pool_release(pool, db_conn);
}

c_orm_pool_destroy(pool);
```

## 5. Schema Codegen

`c-orm` can generate C structural definitions and boilerplate CRUD functions directly from an AST.

```c
#include <c_orm/db_codegen.h>

/* Assuming `schema` is a populated `struct DatabaseSchema` */
FILE* f_sql = fopen("init.sql", "w");
db_codegen_sql(&schema, f_sql, "postgres");
fclose(f_sql);

FILE* f_hdr = fopen("models.h", "w");
db_codegen_struct_header(&schema, f_hdr, "MODELS_H");
fclose(f_hdr);
```

## 6. Asynchronous Execution

`c-orm` provides an asynchronous execution mechanism allowing queries to be queued and completed via event-loop polling.

```c
void my_async_callback(int status, void* user_data) {
    if (status == 0) {
        printf("Async query completed successfully.\n");
    }
}

/* Queue the query */
c_orm_execute_async(db, "UPDATE users SET age = age + 1", my_async_callback, NULL);

/* Inside your event loop, poll to process the queue */
c_orm_poll_async(db);
```
