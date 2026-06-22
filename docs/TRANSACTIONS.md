# Transactions

`c-orm` provides direct API methods for managing database transactions, ensuring ACID compliance for multi-step operations.

## Transaction API

The API exposes three core functions in `c_orm_api.h`:

*   `c_orm_transaction_begin(c_orm_db_t *db)`: Starts a new transaction.
*   `c_orm_transaction_commit(c_orm_db_t *db)`: Commits the active transaction.
*   `c_orm_transaction_rollback(c_orm_db_t *db)`: Rolls back the active transaction.

## Usage Example

Transactions are essential when transferring funds, updating multiple related tables, or performing bulk inserts.

```c
#include "c_orm_api.h"
#include "out_dir/Models.h"

c_orm_error_t bulk_user_insert(c_orm_db_t *db, struct Users *users_array, size_t count) {
    c_orm_error_t err;

    /* Start the transaction */
    err = c_orm_transaction_begin(db);
    if (err != C_ORM_OK) return err;

    for (size_t i = 0; i < count; i++) {
        err = c_orm_insert(db, &Users_meta, &users_array[i]);
        if (err != C_ORM_OK) {
            /* On any failure, abort and roll back all previous inserts */
            c_orm_transaction_rollback(db);
            return err;
        }
    }

    /* All inserts succeeded, commit the changes */
    return c_orm_transaction_commit(db);
}
```

## Row Locking (SELECT FOR UPDATE)

For concurrent environments, you can lock a row during a transaction to prevent other processes from modifying it until your transaction completes.

```c
c_orm_transaction_begin(db);

struct Users user;
/* Finds the user and applies a write lock */
c_orm_find_for_update_by_id_int32(db, &Users_meta, 1, &user);

/* Make modifications... */
int32_t new_score = 100;
user.score = &new_score;

c_orm_update(db, &Users_meta, &user);

c_orm_transaction_commit(db); /* Lock is released */
Users_free(&user);
```
