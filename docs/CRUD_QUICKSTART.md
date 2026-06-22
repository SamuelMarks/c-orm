# c-orm CRUD Quickstart

This guide shows you how to rapidly implement Create, Read, Update, and Delete operations using `c-orm`.

## Setup

Assuming you have already generated your structs and metadata via `cdd-c` (see `TUTORIAL.md`), setting up your CRUD workflow involves:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c_orm_sqlite.h"
#include "c_orm_api.h"
#include "out_dir/Models.h" /* cdd-c generated */

c_orm_db_t *db = NULL;
if (c_orm_sqlite_connect("app.db", &db) != C_ORM_OK) {
    /* Handle error */
}
```

## Create (Insert)

To insert a new record, allocate your generated struct, populate its fields, and call `c_orm_insert`. Note that for nullable columns you pass pointers.

```c
struct Users user;
int32_t age = 28;

memset(&user, 0, sizeof(user));
user.id = 1;
user.username = "johndoe";
user.email = "john@example.com";
user.age = &age;

/* Returns C_ORM_OK on success */
c_orm_insert(db, &Users_meta, &user);
```

## Read (Select)

`c-orm` provides direct lookups by Primary Key, as well as fetching all records into an array structure.

### By Primary Key
```c
struct Users fetched_user;
memset(&fetched_user, 0, sizeof(fetched_user));

if (c_orm_find_by_id_int32(db, &Users_meta, 1, &fetched_user) == C_ORM_OK) {
    printf("Found: %s\n", fetched_user.username);

    /* Important: Free dynamic strings and nullable pointers allocated during fetch */
    Users_free(&fetched_user);
}
```

### Fetch All
```c
struct Users_Array all_users;
memset(&all_users, 0, sizeof(all_users));

if (c_orm_find_all(db, &Users_meta, &all_users) == C_ORM_OK) {
    for (size_t i = 0; i < all_users.size; i++) {
        printf("User %d: %s\n", all_users.data[i].id, all_users.data[i].username);
    }
    Users_Array_free(&all_users);
}
```

## Update

Updates require you to supply a struct containing the updated values, including the Primary Key to identify the row.

```c
struct Users update_user;
int32_t new_age = 29;

memset(&update_user, 0, sizeof(update_user));
update_user.id = 1; /* Identifies the row */
update_user.username = "johndoe_updated";
update_user.email = "john@example.com";
update_user.age = &new_age;

if (c_orm_update(db, &Users_meta, &update_user) == C_ORM_OK) {
    printf("User updated successfully.\n");
}
```

## Delete

Deletion can be done by providing the Primary Key directly.

```c
if (c_orm_delete_by_id_int32(db, &Users_meta, 1) == C_ORM_OK) {
    printf("User deleted successfully.\n");
}
```
