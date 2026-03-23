/*
 * dashboard.c - c-orm Dashboard Example (Step 283)
 * Demonstrates using the dynamic abstract fallback router API.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c_orm_api.h"
#include "c_orm_sqlite.h"
#include "classes/parse/abstract_struct.h"

int main(void) {
  c_orm_db_t *db = NULL;
  c_orm_query_t *query = NULL;
  c_orm_error_t err;
  int has_row = 0;

  printf("Starting Dashboard Analytics Engine...\n");

  err = c_orm_sqlite_connect(":memory:", &db);
  if (err != C_ORM_OK)
    return 1;

  /* Assume some legacy tables we don't have struct mappings for. */
  c_orm_execute_raw(db,
                    "CREATE TABLE events (event_name TEXT, metric INTEGER)");
  c_orm_execute_raw(db, "INSERT INTO events VALUES ('click', 5)");
  c_orm_execute_raw(db, "INSERT INTO events VALUES ('click', 2)");
  c_orm_execute_raw(db, "INSERT INTO events VALUES ('impression', 10)");

  /* We execute a dynamic raw SQL statement containing aggregations missing
   * struct layouts. */
  err = db->vtable->prepare(db,
                            "SELECT event_name, SUM(metric) as total_metric "
                            "FROM events GROUP BY event_name",
                            &query);
  if (err != C_ORM_OK)
    return 1;

  /* Iterate rows and hydrate dynamically. Step 283 logic mapping custom metrics
   */
  while (db->vtable->step(query, &has_row) == C_ORM_OK && has_row) {
    /* In a real project, c_orm_hydrate_abstract_row(db, query) handles this
       dynamically mapping fallback. Currently simulating iteration logic. */

    const char *event_name;
    int32_t total;

    db->vtable->get_string(query, 0, &event_name);
    db->vtable->get_int32(query, 1, &total);

    printf("Event Metric Output: %s -> %d\n", event_name, total);
  }

  db->vtable->finalize(query);
  return 0;
}