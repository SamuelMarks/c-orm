#if defined(__clang__) || defined(__GNUC__)
#endif
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

c_orm_error_t run_dashboard_ops(c_orm_db_t *db, const char *sql);
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
    const char *event_name;
    int32_t total;

    db->vtable->step(query, &has_row);
    if (!has_row)
      break;

    db->vtable->get_string(query, 0, &event_name);
    db->vtable->get_int32(query, 1, &total);

    printf("Event Metric Output: %s -> %d\n", event_name, total);
  }

  db->vtable->finalize(query);
  return C_ORM_OK;
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

int main(void);

/**
 * @brief Main entry point for dashboard example.
 *
 * @return 0 on success.
 */
int main(void) {
  c_orm_db_t *db;
  const char *valid_sql = "SELECT event_name, SUM(metric) as total_metric "
                          "FROM events GROUP BY event_name";

  run_dashboard_example(":memory:");
  run_dashboard_example(NULL);
#ifdef _WIN32
  run_dashboard_example("Z:\\invalid_dir\\bad.db");
#else
  run_dashboard_example("/dev/null/invalid_dir/bad.db");
#endif
  run_dashboard_ops(NULL, valid_sql);

  db = NULL;
  c_orm_sqlite_connect(":memory:", &db);
  run_dashboard_ops(db, NULL);
  c_orm_execute_raw(db, "CREATE TABLE events (id INT)");
  run_dashboard_ops(db, valid_sql);
  db->vtable->disconnect(db);

  db = NULL;
  c_orm_sqlite_connect(":memory:", &db);
  run_dashboard_ops(db, "SELECT * FROM invalid_syntax(");
  db->vtable->disconnect(db);

  return 0;
}

#if defined(__clang__) || defined(__GNUC__)
#endif
