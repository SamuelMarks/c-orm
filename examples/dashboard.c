/**
 * @file dashboard.c
 * @brief Demonstrates dynamic abstract fallback router API in c-orm.
 */

/* clang-format off */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c_orm_api.h"
#include "c_orm_sqlite.h"
/* clang-format on */

/**
 * @brief Execute dashboard aggregation queries and print metric results.
 *
 * @param db Database handle.
 * @param sql SQL statement to prepare and execute.
 * @return C_ORM_OK on success or error code.
 */
c_orm_error_t run_dashboard_ops(c_orm_db_t *db, const char *sql);

/**
 * @brief Run the complete dashboard example connecting to a database path.
 *
 * @param db_path Database path or :memory:.
 * @return C_ORM_OK on success or error code.
 */
c_orm_error_t run_dashboard_example(const char *db_path);

/**
 * @brief Execute dashboard aggregation queries and print metric results.
 *
 * @param db Database handle.
 * @param sql SQL statement to prepare and execute.
 * @return C_ORM_OK on success or error code.
 */
c_orm_error_t run_dashboard_ops(c_orm_db_t *db, const char *sql) {
  c_orm_error_t rc;
  c_orm_query_t *query;
  int has_row;
  int i;
  static const char *stmts[] = {
      "CREATE TABLE events (event_name TEXT, metric INTEGER)",
      "INSERT INTO events VALUES ('click', 5)",
      "INSERT INTO events VALUES ('click', 2)",
      "INSERT INTO events VALUES ('impression', 10)", NULL};

  query = NULL;
  has_row = 0;
  if (!db)
    return C_ORM_ERROR_MEMORY;
  if (!sql)
    return C_ORM_ERROR_MEMORY;

  for (i = 0; stmts[i] != NULL; ++i) {
    rc = c_orm_execute_raw(db, stmts[i]);
    if (rc != C_ORM_OK)
      return rc;
  }

  rc = db->vtable->prepare(db, sql, &query);
  if (rc != C_ORM_OK)
    return rc;

  for (;;) {
    const char *event_name = NULL;
    int32_t total = 0;

    rc = db->vtable->step(query, &has_row);
    if (rc != C_ORM_OK) {
      db->vtable->finalize(query);
      return rc;
    }
    if (!has_row)
      break;

    rc = db->vtable->get_string(query, 0, &event_name);
    if (rc != C_ORM_OK) {
      db->vtable->finalize(query);
      return rc;
    }
    rc = db->vtable->get_int32(query, 1, &total);
    if (rc != C_ORM_OK) {
      db->vtable->finalize(query);
      return rc;
    }

    printf("Event Metric Output: %s -> %d\n", event_name, total);
  }

  return db->vtable->finalize(query);
}

/**
 * @brief Run the complete dashboard example connecting to a database path.
 *
 * @param db_path Database path or :memory:.
 * @return C_ORM_OK on success or error code.
 */
c_orm_error_t run_dashboard_example(const char *db_path) {
  c_orm_error_t rc;
  c_orm_db_t *db;
  const char *sql = "SELECT event_name, SUM(metric) as total_metric "
                    "FROM events GROUP BY event_name";

  db = NULL;
  if (!db_path)
    return C_ORM_ERROR_MEMORY;

  printf("Starting Dashboard Analytics Engine...\n");

  rc = c_orm_sqlite_connect(db_path, &db);
  if (rc != C_ORM_OK)
    return rc;

  rc = run_dashboard_ops(db, sql);
  db->vtable->disconnect(db);

  return rc;
}

/** @brief Mock failure mode for dashboard driver callbacks. */
static int g_dash_fail_mode = 0;
/** @brief Non-zero when the prepared query is the SELECT aggregation query. */
static int g_dash_is_select = 0;

/**
 * @brief Mock prepare callback returning a non-null sentinel query handle.
 * @param db Database connection.
 * @param sql SQL text.
 * @param out_q Output query handle.
 * @return C_ORM_OK.
 */
static c_orm_error_t dash_mock_prepare(c_orm_db_t *db, const char *sql,
                                       c_orm_query_t **out_q) {
  (void)db;
  g_dash_is_select = (strncmp(sql, "SELECT", 6) == 0);
  *out_q = (c_orm_query_t *)&g_dash_fail_mode;
  return C_ORM_OK;
}

/**
 * @brief Mock step callback returning error in mode 1 or one row for SELECT.
 * @param q Query handle.
 * @param out_has Output row flag.
 * @return C_ORM_OK or C_ORM_ERROR_STEP.
 */
static c_orm_error_t dash_mock_step(c_orm_query_t *q, int *out_has) {
  (void)q;
  if (!g_dash_is_select) {
    *out_has = 0;
    return C_ORM_OK;
  }
  if (g_dash_fail_mode == 1)
    return C_ORM_ERROR_STEP;
  *out_has = 1;
  return C_ORM_OK;
}

/**
 * @brief Mock get_string callback returning error in mode 2 or string
 * otherwise.
 * @param q Query handle.
 * @param idx Column index.
 * @param out_s Output string pointer.
 * @return C_ORM_OK or C_ORM_ERROR_SQL.
 */
static c_orm_error_t dash_mock_get_str(c_orm_query_t *q, int idx,
                                       const char **out_s) {
  (void)q;
  (void)idx;
  if (g_dash_fail_mode == 2)
    return C_ORM_ERROR_SQL;
  *out_s = "click";
  return C_ORM_OK;
}

/**
 * @brief Mock get_int32 callback returning error in mode 3.
 * @param q Query handle.
 * @param idx Column index.
 * @param out_v Output int32 value.
 * @return C_ORM_ERROR_SQL.
 */
static c_orm_error_t dash_mock_get_i32(c_orm_query_t *q, int idx,
                                       int32_t *out_v) {
  (void)q;
  (void)idx;
  (void)out_v;
  return C_ORM_ERROR_SQL;
}

/**
 * @brief Mock finalize callback returning C_ORM_OK.
 * @param q Query handle.
 * @return C_ORM_OK.
 */
static c_orm_error_t dash_mock_finalize(c_orm_query_t *q) {
  (void)q;
  return C_ORM_OK;
}

int main(void);

/**
 * @brief Main entry point for dashboard example.
 *
 * @return 0 on success.
 */
int main(void) {
  c_orm_db_t *db = NULL;
  c_orm_driver_vtable_t mvt;
  c_orm_db_t mdb;
  int failed = 0;
  const char *valid_sql = "SELECT event_name, SUM(metric) as total_metric "
                          "FROM events GROUP BY event_name";

  failed |= (run_dashboard_example(":memory:") != C_ORM_OK);
  failed |= (run_dashboard_example(NULL) == C_ORM_OK);
#ifdef _WIN32
  failed |= (run_dashboard_example("Z:\\invalid_dir\\bad.db") == C_ORM_OK);
#else
  failed |= (run_dashboard_example("/dev/null/invalid_dir/bad.db") == C_ORM_OK);
#endif
  failed |= (run_dashboard_ops(NULL, valid_sql) == C_ORM_OK);

  failed |= (c_orm_sqlite_connect(":memory:", &db) != C_ORM_OK);
  failed |= (run_dashboard_ops(db, NULL) == C_ORM_OK);
  failed |= (c_orm_execute_raw(db, "CREATE TABLE events (id INT)") != C_ORM_OK);
  failed |= (run_dashboard_ops(db, valid_sql) == C_ORM_OK);
  failed |= (db->vtable->disconnect(db) != C_ORM_OK);

  db = NULL;
  failed |= (c_orm_sqlite_connect(":memory:", &db) != C_ORM_OK);
  failed |=
      (run_dashboard_ops(db, "SELECT * FROM invalid_syntax(") == C_ORM_OK);
  failed |= (db->vtable->disconnect(db) != C_ORM_OK);

  memset(&mvt, 0, sizeof(mvt));
  mvt.prepare = dash_mock_prepare;
  mvt.step = dash_mock_step;
  mvt.get_string = dash_mock_get_str;
  mvt.get_int32 = dash_mock_get_i32;
  mvt.finalize = dash_mock_finalize;
  memset(&mdb, 0, sizeof(mdb));
  mdb.vtable = &mvt;
  for (g_dash_fail_mode = 1; g_dash_fail_mode <= 3; g_dash_fail_mode++) {
    failed |= (run_dashboard_ops(&mdb, valid_sql) == C_ORM_OK);
  }

  return failed;
}
