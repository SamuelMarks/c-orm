# C-ORM Migrations Architecture

## 1. `_c_orm_migrations` Table Schema
To track applied migrations, we will use a table named `_c_orm_migrations`.
Schema:
```sql
CREATE TABLE IF NOT EXISTS _c_orm_migrations (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    version VARCHAR(255) NOT NULL UNIQUE,
    name VARCHAR(255) NOT NULL,
    applied_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
```
- `id`: Sequential primary key.
- `version`: A timestamp-based string (e.g., `20260330120000`) to avoid merge conflicts and allow concurrent development, rather than sequential numbers.
- `name`: A descriptive name of the migration (e.g., `create_users_table`).
- `applied_at`: When the migration was successfully executed.

## 2. Migration Versioning System
We will use **timestamps** (format `YYYYMMDDHHMMSS`) as versions. This minimizes conflicts when multiple developers create migrations simultaneously.

## 3. UP and DOWN Migration Script Format
Migrations will be stored in a specified directory as SQL files or managed via C arrays of strings.
Format:
- `YYYYMMDDHHMMSS_name.up.sql`
- `YYYYMMDDHHMMSS_name.down.sql`

## 4. Pure SQL vs C-based Migrations
We will support **Pure SQL** migrations for maximum compatibility and flexibility, but allow C-based migrations for complex data transformations that cannot be easily done in pure SQL. Pure SQL is the default.

## 5. CLI Tool Draft
The `c-orm-cli` tool will have commands:
- `init`: Setup the migrations directory.
- `create <name>`: Generate empty `UP` and `DOWN` scripts.
- `migrate`: Apply pending migrations.
- `rollback [steps]`: Rollback migrations.
- `status`: Show applied and pending migrations.

## 6. Schema State Hashing for Drift Detection
Drift detection will be done by maintaining a hash of the current struct metadata and comparing it against the hash of the last successfully mapped metadata (potentially stored in another table `_c_orm_schema_state`), or by querying the DB's information schema and diffing it with the C structures directly.

## 7. Lock Mechanism for Concurrent Migrations
- SQLite: Relies on SQLite's built-in file locks / WAL mode. For migrations, we use `BEGIN EXCLUSIVE TRANSACTION`.
- PostgreSQL (if added later): Advisory locks.

## 8. Dry-run Functionality
A `dry_run` flag in the migration API will prevent `EXECUTE` and instead print the generated/parsed SQL to a provided output stream (or logger).

## 9. Transaction Wrapping (DDL Transactions)
SQLite supports DDL inside transactions. We will wrap every UP or DOWN migration in `BEGIN TRANSACTION; ... COMMIT;`. If an error occurs, `ROLLBACK;` is issued.

## 10. Callback Hooks
Migration API will support `pre_migrate` and `post_migrate` function pointers in the `c_orm_migration_options` struct to allow custom application logic (e.g., seeding data, cache invalidation).
