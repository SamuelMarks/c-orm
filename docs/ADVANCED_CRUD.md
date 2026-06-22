# Advanced CRUD: Soft-Deletes & Timestamps

## Soft-Delete

`c-orm` provides mechanisms to implement soft-deletes via metadata or custom queries, though it typically relies on schema definitions (like a `deleted_at` timestamp or `is_deleted` boolean) and query building.

Currently, if you wish to "soft delete", you should perform an update operation setting your soft-delete flag, rather than calling `c_orm_delete_by_id`.

```c
/* Soft-delete example */
struct Users user;
memset(&user, 0, sizeof(user));
user.id = 1;
/* Using an integer flag for simplicity */
int32_t deleted = 1;
user.is_deleted = &deleted;

/* Update the row instead of deleting it */
c_orm_update(db, &Users_meta, &user);
```

When querying, use the Query Builder to filter out soft-deleted records:

```c
c_orm_query_builder_t *qb = c_orm_query_builder_new(db, &Users_meta);
c_orm_query_builder_where(qb, "is_deleted = 0");

/* Execute and hydrate... */
```

## Optimistic Concurrency Control

To prevent lost updates, `c-orm` supports optimistic locking using a version column via `c_orm_update_optimistic`. Your struct must have a version column (typically integer) that you increment on read and submit on update. If the database version doesn't match the submitted previous version, it returns `C_ORM_CONCURRENCY_ERROR`.

```c
struct Users user;
c_orm_find_by_id_int32(db, &Users_meta, 1, &user);

/* user.version is 1 */
int32_t new_version = (*user.version) + 1;
user.version = &new_version;

/* The optimistic update will add WHERE id = 1 AND version = 1 */
c_orm_error_t err = c_orm_update_optimistic(db, &Users_meta, &user);

if (err == C_ORM_CONCURRENCY_ERROR) {
    printf("Another process modified this row!\n");
}
```

## Automatic Timestamps

To automatically update `updated_at` timestamps, it's highly recommended to utilize database triggers (e.g., `BEFORE UPDATE` triggers in SQLite/Postgres) to maintain pure C struct simplicity. However, you can also assign the timestamp string manually before calling `c_orm_update`.
