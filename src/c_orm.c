#include "c_orm/c_orm.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

static void c_orm_mutex_init(c_orm_mutex_t* mutex);
static void c_orm_mutex_destroy(c_orm_mutex_t* mutex);
static void c_orm_mutex_lock(c_orm_mutex_t* mutex);
static void c_orm_mutex_unlock(c_orm_mutex_t* mutex);
static int strdup_safe(const char* s, char** out);

/**
 * @brief Initialize a mutex.
 * @param mutex Pointer to the mutex to initialize.
 */
static void c_orm_mutex_init(c_orm_mutex_t* mutex) {
#if defined(_WIN32)
    InitializeCriticalSection(mutex);
#else
    pthread_mutex_init(mutex, NULL);
#endif
}

static void c_orm_mutex_destroy(c_orm_mutex_t* mutex) {
#if defined(_WIN32)
    DeleteCriticalSection(mutex);
#else
    pthread_mutex_destroy(mutex);
#endif
}

static void c_orm_mutex_lock(c_orm_mutex_t* mutex) {
#if defined(_WIN32)
    EnterCriticalSection(mutex);
#else
    pthread_mutex_lock(mutex);
#endif
}

static void c_orm_mutex_unlock(c_orm_mutex_t* mutex) {
#if defined(_WIN32)
    LeaveCriticalSection(mutex);
#else
    pthread_mutex_unlock(mutex);
#endif
}

/* Simulation structures for Async I/O */
struct c_orm_async_job {
    char* query;
    c_orm_async_cb_t cb;
    void* user_data;
    struct c_orm_async_job* next;
};

struct c_orm_db {
    c_orm_dialect_t dialect;
    void* native_conn;
    c_orm_log_cb_t logger;
    void* logger_user_data;
    int simulated_migration_version;
    c_orm_mutex_t mutex;

    /* Async simulation queue */
    struct c_orm_async_job* async_queue_head;
    struct c_orm_async_job* async_queue_tail;
};

struct c_orm_pool {
    c_orm_db_t** connections;
    int* in_use;
    size_t pool_size;
    c_orm_dialect_t dialect;
    char* conn_string;
    c_orm_mutex_t mutex;
};

struct c_orm_query {
    c_orm_db_t* db;
    char* table_name;
    char* select_columns;
    char* where_condition;
    char* order_by;
    int has_limit;
    size_t limit;
};

static int strdup_safe(const char* s, char** out) {
    if (!s || !out) return -1;
    *out = malloc(strlen(s) + 1);
    if (!*out) return -1;
#if defined(_MSC_VER)
    strcpy_s(*out, strlen(s) + 1, s);
#else
    strcpy(*out, s);
#endif
    return 0;
}

/* Stub connections for now to satisfy link dependencies.
   Full implementation will require libpq, sqlite3, and mysql headers/libs.
*/
int c_orm_connect(c_orm_db_t** db_out, c_orm_dialect_t dialect, const char* conn_string) {
    c_orm_db_t* db;

    if (!db_out || !conn_string) {
        return -1;
    }

    /* Validate dialect compiled in before allocating */
    if (dialect == C_ORM_DIALECT_SQLITE) {
#ifndef C_ORM_HAVE_SQLITE
        return -4;
#endif
    } else if (dialect == C_ORM_DIALECT_POSTGRES) {
#ifndef C_ORM_HAVE_POSTGRES
        return -4;
#endif
    } else if (dialect == C_ORM_DIALECT_MYSQL) {
#ifndef C_ORM_HAVE_MYSQL
        return -4;
#endif
    } else {
        return -1; /* Unknown dialect */
    }

    db = calloc(1, sizeof(c_orm_db_t));
    if (!db) {
        return -1;
    }

    db->dialect = dialect;
    db->logger = NULL;
    db->logger_user_data = NULL;
    db->simulated_migration_version = 0; /* Starts at 0 initially */
    db->async_queue_head = NULL;
    db->async_queue_tail = NULL;
    c_orm_mutex_init(&db->mutex);

    if (dialect == C_ORM_DIALECT_SQLITE) {
#ifdef C_ORM_HAVE_SQLITE
        /* sqlite3_open(conn_string, (sqlite3**)&db->native_conn); */
#endif
    } else if (dialect == C_ORM_DIALECT_POSTGRES) {
#ifdef C_ORM_HAVE_POSTGRES
        /* db->native_conn = PQconnectdb(conn_string); */
#endif
    } else if (dialect == C_ORM_DIALECT_MYSQL) {
#ifdef C_ORM_HAVE_MYSQL
        /* mysql_real_connect(db->native_conn, ...); */
#endif
    }

    *db_out = db;
    return 0;
}

void c_orm_disconnect(c_orm_db_t* db) {
    struct c_orm_async_job* job;
    struct c_orm_async_job* next;

    if (!db) return;

    c_orm_mutex_destroy(&db->mutex);

    job = db->async_queue_head;
    while (job) {
        next = job->next;
        free(job->query);
        free(job);
        job = next;
    }

    if (db->dialect == C_ORM_DIALECT_SQLITE) {
#ifdef C_ORM_HAVE_SQLITE
        /* sqlite3_close(db->native_conn); */
#endif
    } else if (db->dialect == C_ORM_DIALECT_POSTGRES) {
#ifdef C_ORM_HAVE_POSTGRES
        /* PQfinish(db->native_conn); */
#endif
    } else if (db->dialect == C_ORM_DIALECT_MYSQL) {
#ifdef C_ORM_HAVE_MYSQL
        /* mysql_close(db->native_conn); */
#endif
    }
    free(db);
}

int c_orm_lock(c_orm_db_t* db) {
    if (!db) return -1;
    c_orm_mutex_lock(&db->mutex);
    return 0;
}

int c_orm_unlock(c_orm_db_t* db) {
    if (!db) return -1;
    c_orm_mutex_unlock(&db->mutex);
    return 0;
}

int c_orm_migrate(c_orm_db_t* db, const char* migrations_dir) {
    if (!db || !migrations_dir) return -1;

    c_orm_lock(db);

    /*
     * In a real implementation:
     * 1. Create a `_c_orm_migrations` table if it doesn't exist.
     * 2. Read applied versions from it.
     * 3. Iterate through `migrations_dir` finding `up.sql` scripts.
     * 4. Execute those that are newer than `current_version` within a transaction.
     * 5. Record applied version into `_c_orm_migrations`.
     */

    /* Simulation */
    db->simulated_migration_version++;

    c_orm_unlock(db);
    return 0;
}

int c_orm_migrate_rollback(c_orm_db_t* db, const char* migrations_dir) {
    if (!db || !migrations_dir) return -1;

    c_orm_lock(db);

    /*
     * Real implementation would read last version from `_c_orm_migrations`,
     * execute `down.sql`, and delete the record.
     */

    if (db->simulated_migration_version > 0) {
        db->simulated_migration_version--;
    }

    c_orm_unlock(db);
    return 0;
}

int c_orm_migrate_current_version(c_orm_db_t* db, int* current_version) {
    if (!db || !current_version) return -1;

    c_orm_lock(db);
    /* Real impl queries `_c_orm_migrations` */
    *current_version = db->simulated_migration_version;
    c_orm_unlock(db);

    return 0;
}

int c_orm_set_logger(c_orm_db_t* db, c_orm_log_cb_t logger, void* user_data) {
    if (!db) return -1;
    c_orm_lock(db);
    db->logger = logger;
    db->logger_user_data = user_data;
    c_orm_unlock(db);
    return 0;
}

int c_orm_execute(c_orm_db_t* db, const char* query) {
    clock_t start_time, end_time;
    double duration;

    if (!db || !query) return -1;

    c_orm_lock(db);

    start_time = clock();

    /* Execute via sqlite3_exec, PQexec, or mysql_query */

    end_time = clock();

    if (db->logger) {
        duration = ((double)(end_time - start_time)) / CLOCKS_PER_SEC * 1000.0;
        db->logger(query, duration, db->logger_user_data);
    }

    c_orm_unlock(db);
    return 0;
}

int c_orm_execute_async(c_orm_db_t* db, const char* query, c_orm_async_cb_t cb, void* user_data) {        
    struct c_orm_async_job* job;
    if (!db || !query || !cb) return -1;

    job = calloc(1, sizeof(struct c_orm_async_job));
    if (!job) return -1;

    if (strdup_safe(query, &job->query) != 0) {
        free(job);
        return -1;
    }

    job->cb = cb;
    job->user_data = user_data;

    c_orm_lock(db);

    if (!db->async_queue_head) {
        db->async_queue_head = job;
        db->async_queue_tail = job;
    } else {
        db->async_queue_tail->next = job;
        db->async_queue_tail = job;
    }

    c_orm_unlock(db);
    return 0;
}

int c_orm_poll_async(c_orm_db_t* db) {
    struct c_orm_async_job* job;
    int exec_res;

    if (!db) return -1;

    c_orm_lock(db);
    job = db->async_queue_head;
    if (job) {
        db->async_queue_head = job->next;
        if (!db->async_queue_head) {
            db->async_queue_tail = NULL;
        }
    }
    c_orm_unlock(db);

    if (job) {
        /* Run it locally to simulate async completion */
        exec_res = c_orm_execute(db, job->query);
        job->cb(exec_res, job->user_data);

        free(job->query);
        free(job);
        return 1; /* Returned 1 job */
    }

    return 0; /* Queue empty */
}


int c_orm_execute_params(c_orm_db_t* db, const char* query, const c_orm_param_t* params, size_t param_count) {
    clock_t start_time, end_time;
    double duration;

    if (!db || !query) return -1;
    if (param_count > 0 && !params) return -1;

    c_orm_lock(db);

    start_time = clock();

    /*
     * Stub execution for parameterized queries.
     * Full implementation will require:
     * - SQLite: sqlite3_prepare_v2, sqlite3_bind_*, sqlite3_step
     * - PostgreSQL: PQexecParams
     * - MySQL: mysql_stmt_prepare, mysql_stmt_bind_param, mysql_stmt_execute
     */

    /* Basic validation loop just to exercise the params for test coverage */
    if (params) {
        size_t i;
        for (i = 0; i < param_count; i++) {
            switch (params[i].type) {
                case C_ORM_PARAM_INTEGER:
                    /* valid */
                    break;
                case C_ORM_PARAM_REAL:
                    /* valid */
                    break;
                case C_ORM_PARAM_TEXT:
                    if (!params[i].value.text_val) {
                        c_orm_unlock(db);
                        return -2; /* Invalid param text */
                    }
                    break;
                case C_ORM_PARAM_BLOB:
                    if (!params[i].value.blob_val.data && params[i].value.blob_val.size > 0) {
                        c_orm_unlock(db);
                        return -2; /* Invalid param blob */
                    }
                    break;
                case C_ORM_PARAM_NULL:
                    /* valid */
                    break;
                default:
                    c_orm_unlock(db);
                    return -3; /* Unknown type */
            }
        }
    }

    end_time = clock();

    if (db->logger) {
        duration = ((double)(end_time - start_time)) / CLOCKS_PER_SEC * 1000.0;
        db->logger(query, duration, db->logger_user_data);
    }

    c_orm_unlock(db);
    return 0;
}

int c_orm_transaction_begin(c_orm_db_t* db) {
    if (!db) return -1;
    return c_orm_execute(db, "BEGIN TRANSACTION");
}

int c_orm_transaction_commit(c_orm_db_t* db) {
    if (!db) return -1;
    return c_orm_execute(db, "COMMIT");
}

int c_orm_transaction_rollback(c_orm_db_t* db) {
    if (!db) return -1;
    return c_orm_execute(db, "ROLLBACK");
}

int c_orm_pool_create(c_orm_pool_t** pool_out, c_orm_dialect_t dialect, const char* conn_string, size_t pool_size) {
    c_orm_pool_t* pool;
    size_t i;

    if (!pool_out || !conn_string || pool_size == 0) {
        return -1;
    }

    pool = calloc(1, sizeof(c_orm_pool_t));
    if (!pool) return -1;

    pool->pool_size = pool_size;
    pool->dialect = dialect;
    c_orm_mutex_init(&pool->mutex);

    pool->conn_string = malloc(strlen(conn_string) + 1);
    if (!pool->conn_string) {
        c_orm_mutex_destroy(&pool->mutex);
        free(pool);
        return -1;
    }
#if defined(_MSC_VER)
    strcpy_s(pool->conn_string, strlen(conn_string) + 1, conn_string);
#else
    strcpy(pool->conn_string, conn_string);
#endif

    pool->connections = calloc(pool_size, sizeof(c_orm_db_t*));
    if (!pool->connections) {
        free(pool->conn_string);
        c_orm_mutex_destroy(&pool->mutex);
        free(pool);
        return -1;
    }

    pool->in_use = calloc(pool_size, sizeof(int));
    if (!pool->in_use) {
        free(pool->connections);
        free(pool->conn_string);
        c_orm_mutex_destroy(&pool->mutex);
        free(pool);
        return -1;
    }

    for (i = 0; i < pool_size; i++) {
        if (c_orm_connect(&pool->connections[i], dialect, conn_string) != 0) {
            c_orm_pool_destroy(pool);
            return -1;
        }
        pool->in_use[i] = 0;
    }

    *pool_out = pool;
    return 0;
}

int c_orm_pool_destroy(c_orm_pool_t* pool) {
    size_t i;
    if (!pool) return -1;

    c_orm_mutex_lock(&pool->mutex);

    if (pool->connections) {
        for (i = 0; i < pool->pool_size; i++) {
            if (pool->connections[i]) {
                c_orm_disconnect(pool->connections[i]);
            }
        }
        free(pool->connections);
    }

    if (pool->in_use) {
        free(pool->in_use);
    }

    if (pool->conn_string) {
        free(pool->conn_string);
    }

    c_orm_mutex_unlock(&pool->mutex);
    c_orm_mutex_destroy(&pool->mutex);

    free(pool);
    return 0;
}

int c_orm_pool_acquire(c_orm_pool_t* pool, c_orm_db_t** db_out) {
    size_t i;

    if (!pool || !db_out) return -1;

    c_orm_mutex_lock(&pool->mutex);

    for (i = 0; i < pool->pool_size; i++) {
        if (pool->in_use[i] == 0) {
            pool->in_use[i] = 1;
            *db_out = pool->connections[i];
            c_orm_mutex_unlock(&pool->mutex);
            return 0;
        }
    }

    c_orm_mutex_unlock(&pool->mutex);

    /* Pool exhausted */
    return -2;
}

int c_orm_pool_release(c_orm_pool_t* pool, c_orm_db_t* db) {
    size_t i;

    if (!pool || !db) return -1;

    c_orm_mutex_lock(&pool->mutex);

    for (i = 0; i < pool->pool_size; i++) {
        if (pool->connections[i] == db) {
            pool->in_use[i] = 0;
            c_orm_mutex_unlock(&pool->mutex);
            return 0;
        }
    }

    c_orm_mutex_unlock(&pool->mutex);

    /* Connection not found in pool */
    return -2;
}

int c_orm_query_create(c_orm_query_t** query_out, c_orm_db_t* db, const char* table_name) {
    c_orm_query_t* query;
    if (!query_out || !db || !table_name) return -1;

    query = calloc(1, sizeof(c_orm_query_t));
    if (!query) return -1;

    query->db = db;
    if (strdup_safe(table_name, &query->table_name) != 0) {
        free(query);
        return -1;
    }
    
    *query_out = query;
    return 0;
}

int c_orm_query_select(c_orm_query_t* query, const char* columns) {
    if (!query || !columns) return -1;
    if (query->select_columns) free(query->select_columns);
    if (strdup_safe(columns, &query->select_columns) != 0) return -1;
    return 0;
}

int c_orm_query_where(c_orm_query_t* query, const char* condition) {
    if (!query || !condition) return -1;
    if (query->where_condition) free(query->where_condition);
    if (strdup_safe(condition, &query->where_condition) != 0) return -1;
    return 0;
}

int c_orm_query_order_by(c_orm_query_t* query, const char* order_by) {
    if (!query || !order_by) return -1;
    if (query->order_by) free(query->order_by);
    if (strdup_safe(order_by, &query->order_by) != 0) return -1;
    return 0;
}

int c_orm_query_limit(c_orm_query_t* query, size_t limit) {
    if (!query) return -1;
    query->has_limit = 1;
    query->limit = limit;
    return 0;
}

int c_orm_query_build(c_orm_query_t* query, char** sql_out) {
    size_t size = 128;
    char* sql;

    if (!query || !sql_out) return -1;

    size += query->select_columns ? strlen(query->select_columns) : 1;
    size += strlen(query->table_name);
    if (query->where_condition) size += strlen(query->where_condition) + 8;
    if (query->order_by) size += strlen(query->order_by) + 12;
    if (query->has_limit) size += 32;

    sql = malloc(size);
    if (!sql) return -1;

#if defined(_MSC_VER)
    strcpy_s(sql, size, "SELECT ");
    strcat_s(sql, size, query->select_columns ? query->select_columns : "*");
    strcat_s(sql, size, " FROM ");
    strcat_s(sql, size, query->table_name);
#else
    strcpy(sql, "SELECT ");
    strcat(sql, query->select_columns ? query->select_columns : "*");
    strcat(sql, " FROM ");
    strcat(sql, query->table_name);
#endif

    if (query->where_condition) {
#if defined(_MSC_VER)
        strcat_s(sql, size, " WHERE ");
        strcat_s(sql, size, query->where_condition);
#else
        strcat(sql, " WHERE ");
        strcat(sql, query->where_condition);
#endif
    }

    if (query->order_by) {
#if defined(_MSC_VER)
        strcat_s(sql, size, " ORDER BY ");
        strcat_s(sql, size, query->order_by);
#else
        strcat(sql, " ORDER BY ");
        strcat(sql, query->order_by);
#endif
    }

    if (query->has_limit) {
        char limit_str[32];
#if defined(_MSC_VER)
        sprintf_s(limit_str, sizeof(limit_str), " LIMIT " NUM_FORMAT, (long)query->limit);
        strcat_s(sql, size, limit_str);
#else
        sprintf(limit_str, " LIMIT " NUM_FORMAT, (long)query->limit);
        strcat(sql, limit_str);
#endif
    }

    *sql_out = sql;
    return 0;
}

int c_orm_query_execute(c_orm_query_t* query) {
    char* sql = NULL;
    int res;

    if (!query) return -1;

    res = c_orm_query_build(query, &sql);
    if (res != 0) return res;

    res = c_orm_execute(query->db, sql);
    free(sql);

    return res;
}

int c_orm_query_destroy(c_orm_query_t* query) {
    if (!query) return -1;

    if (query->table_name) free(query->table_name);
    if (query->select_columns) free(query->select_columns);
    if (query->where_condition) free(query->where_condition);
    if (query->order_by) free(query->order_by);

    free(query);
    return 0;
}
