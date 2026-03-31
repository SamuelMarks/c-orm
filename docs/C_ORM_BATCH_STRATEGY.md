# C_ORM Batch Strategy & Limitations

## SQLite Limitations on Max Parameters
SQLite limits the number of host parameters in a single SQL statement. By default, `SQLITE_MAX_VARIABLE_NUMBER` is 999 in older versions, and 32766 in newer versions (>= 3.32.0). 
When chunking bulk inserts, we must ensure `num_rows * num_columns <= SQLITE_MAX_VARIABLE_NUMBER`.
A safe default chunk size for SQLite is 500 rows, assuming less than 64 columns.

## Postgres Limitations on Bulk Inserts
PostgreSQL has a maximum of 65535 parameters in a single prepared statement (16-bit parameter index limit).
Chunking must ensure `num_rows * num_columns <= 65535`. A safe default chunk size is 1000 rows.
Postgres also provides the `COPY` command which is vastly superior for massive bulk inserts, avoiding the parameter limits entirely and dramatically reducing parsing overhead.

## Multiple-VALUES Insert Syntax
Standard SQL allows:
`INSERT INTO table (c1, c2) VALUES (?, ?), (?, ?), (?, ?);`
This works in SQLite, PostgreSQL, and MySQL. It requires dynamically generating the query string to append `(, )` the required number of times and then binding an array of parameters.

## Postgres COPY Command
`COPY table (c1, c2) FROM STDIN BINARY;` or `COPY ... FROM STDIN (FORMAT csv);`
This is the fastest method for Postgres. In `c-orm`, we could stream structs into CSV or Binary representation over the `libpq` connection. 

## C Array Handling APIs
- **Input (Inserts/Updates):** Accept `const void* array` and `size_t count`. Use pointer arithmetic `((const char*)array) + (i * meta->struct_size)` to iterate.
- **Output (Reads):** Provide an iterator API that fills a pre-allocated array chunk-by-chunk.

## Memory Pool Strategy for Large Batch Reads
For massive `SELECT` statements (e.g., 1 million rows), allocating all structs at once causes huge memory spikes.
Strategy:
- **Chunked Cursors:** Use SQL cursors (`DECLARE cursor`) or pagination (`LIMIT/OFFSET` or Keyset Pagination).
- **Pre-allocated Array Buffers:** `c_orm_iterator_next` will accept a pre-allocated array of a fixed size. The user iterates by repeatedly calling it, which fills up to `batch_size` items, reusing the same struct memory.
- **String/Blob Memory Pooling:** Dynamic strings attached to the struct must be allocated from an arena/memory pool tied to the batch array. When the batch array is reused for the next chunk, the memory pool is cleared via `c_orm_arena_reset()`, eliminating individual `free()` calls.

## Error Handling Behavior
For inserts/updates:
- By default, wrap the batch in a transaction `c_orm_transaction_begin()`. Fail the entire batch if any chunk fails (Atomicity).
- Support `ON CONFLICT DO NOTHING` for idempotent partial successes.

## Batch Chunking Logic
- `c_orm_insert_batch` will accept an array of structs, `total_count`, and `chunk_size`.
- If `chunk_size` is 0, c-orm auto-calculates it based on `MAX_PARAMETERS / num_columns`.
- Internally loop: `for (i = 0; i < total_count; i += chunk_size)` and execute the multiple-VALUES insert or specific bulk API.
