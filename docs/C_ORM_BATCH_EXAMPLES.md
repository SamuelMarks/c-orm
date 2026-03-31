# C-ORM Batch Operations Examples

## Memory Lifecycle of Array Results
When using `c_orm_find_all` or `c_orm_hydrate_all`, memory is allocated dynamically.
For `c_orm_iterator_next`, a pre-allocated array must be passed in. The memory is filled sequentially.
String pointers within the structs point to heap allocations and must be deep-freed manually.
In future versions, a `c_orm_arena_reset` function will be provided to bulk-free these allocations instantly.

## Batch Chunking Configuration
Batch chunking is automatically handled. When passing `chunk_size = 0`, the library auto-calculates the safest maximum parameter count limit for the target database:
- SQLite: `SQLITE_MAX_VARIABLE_NUMBER` limits chunk size.
- PostgreSQL: Max 65535 parameters limit chunk size.

## Example: Importing CSV to Database
```c
#include "c_orm.h"
#include <stdio.h>
#include <stdlib.h>

void import_csv(c_orm_db_t *db, const char *filename) {
    FILE *f = fopen(filename, "r");
    char line[256];
    MyStruct items[500];
    size_t count = 0;

    while (fgets(line, sizeof(line), f)) {
        /* Parse line into items[count] */
        count++;
        if (count == 500) {
            c_orm_insert_batch(db, &my_struct_meta, items, 500, 500);
            count = 0;
        }
    }
    if (count > 0) {
        c_orm_insert_batch(db, &my_struct_meta, items, count, count);
    }
    fclose(f);
}
```

## Example: Exporting Table to JSON
```c
#include "c_orm.h"
#include <stdio.h>

void export_json(c_orm_db_t *db) {
    struct c_orm_iterator *iter;
    MyStruct items[100];
    size_t fetched;

    c_orm_find_batch_init(db, &my_struct_meta, NULL, 100, &iter);

    printf("[\n");
    int first = 1;
    while (c_orm_iterator_next(iter, items, &fetched) == C_ORM_OK && fetched > 0) {
        for (size_t i = 0; i < fetched; i++) {
            if (!first) printf(",\n");
            first = 0;
            char *json;
            c_orm_to_json(&my_struct_meta, &items[i], &json);
            printf("  %s", json);
            free(json);
            /* deep free items[i] */
        }
    }
    printf("\n]\n");

    c_orm_iterator_close(iter);
}
```
