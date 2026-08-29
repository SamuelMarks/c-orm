#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file hydrate_router.c
 * @brief Implementation of dynamic runtime routing APIs for C-ORM struct
 * hydration.
 *
 * @author Samuel Marks
 */

/* clang-format off */
#include "hydrate_router.h"
#include <stdlib.h>
#include "c_orm_meta.h"
#include <string.h>
/* clang-format on */

#if defined(_MSC_VER)
#define CDD_C_THREAD_LOCAL __declspec(thread)
#elif defined(__GNUC__) || defined(__clang__)
#define CDD_C_THREAD_LOCAL __thread
#else
#define CDD_C_THREAD_LOCAL
#endif

static CDD_C_THREAD_LOCAL char cdd_c_hydrate_error_msg[512] = {0};

c_orm_error_t cdd_c_hydrate_router_get_last_error(const char **out_msg) {
  if (!out_msg)
    return C_ORM_ERROR_UNKNOWN;
  if (cdd_c_hydrate_error_msg[0] == '\0') {
    *out_msg = NULL;
  } else {
    *out_msg = cdd_c_hydrate_error_msg;
  }
  return C_ORM_OK;
}

C_ORM_EXPORT int c_orm_mock_hydrate_router_set_last_error_fail = 0;

c_orm_error_t cdd_c_hydrate_router_set_last_error(const char *msg) {
  if (c_orm_mock_hydrate_router_set_last_error_fail == 1 && msg &&
      strstr(msg, "Invalid"))
    return C_ORM_ERROR_UNKNOWN;
  if (c_orm_mock_hydrate_router_set_last_error_fail == 2 && !msg)
    return C_ORM_ERROR_UNKNOWN;
  if (c_orm_mock_hydrate_router_set_last_error_fail == 3 && msg &&
      strstr(msg, "returned error"))
    return C_ORM_ERROR_UNKNOWN;
  if (c_orm_mock_hydrate_router_set_last_error_fail == 4 && msg &&
      strstr(msg, "not found"))
    return C_ORM_ERROR_UNKNOWN;
  if (!msg) {

    cdd_c_hydrate_error_msg[0] = '\0';
  } else {
#if defined(_MSC_VER)
    strncpy_s(cdd_c_hydrate_error_msg, sizeof(cdd_c_hydrate_error_msg), msg,
              sizeof(cdd_c_hydrate_error_msg) - 1);
#else
    strncpy(cdd_c_hydrate_error_msg, msg, sizeof(cdd_c_hydrate_error_msg) - 1);
#endif

    cdd_c_hydrate_error_msg[sizeof(cdd_c_hydrate_error_msg) - 1] = '\0';
  }
  return C_ORM_OK;
}

c_orm_error_t cdd_c_hydrate_router_init(cdd_c_hydrate_router_t *router) {
  if (!router)
    return C_ORM_ERROR_UNKNOWN;
  router->routes = NULL;
  router->count = 0;
  router->capacity = 0;
  return C_ORM_OK;
}

c_orm_error_t
cdd_c_hydrate_router_register(cdd_c_hydrate_router_t *router,
                              c_orm_uint64_t query_id_hash,
                              const struct cdd_c_meta *struct_meta,
                              cdd_c_specific_hydrator_fn hydrate_fn) {
  cdd_c_hydrate_route_t *new_routes;
  size_t new_cap, i;

  if (!router || !hydrate_fn)
    return C_ORM_ERROR_UNKNOWN;

  for (i = 0; i < router->count; ++i) {
    if (router->routes[i].query_id_hash == query_id_hash) {
      /* Update existing route */
      router->routes[i].struct_meta = struct_meta;
      router->routes[i].hydrate_fn = hydrate_fn;
      return C_ORM_OK;
    }
  }

  if (router->count >= router->capacity) {
    new_cap = router->capacity == 0 ? 8 : router->capacity * 2;
    new_routes = (cdd_c_hydrate_route_t *)C_ORM_REALLOC(
        router->routes, new_cap * sizeof(cdd_c_hydrate_route_t));
    if (!new_routes)
      return C_ORM_ERROR_UNKNOWN;
    router->routes = new_routes;
    router->capacity = new_cap;
  }

  router->routes[router->count].query_id_hash = query_id_hash;
  router->routes[router->count].struct_meta = struct_meta;
  router->routes[router->count].hydrate_fn = hydrate_fn;
  router->count++;

  return C_ORM_OK;
}

c_orm_error_t cdd_c_hydrate_router_dispatch(
    const cdd_c_hydrate_router_t *router, c_orm_uint64_t query_id_hash,
    const cdd_c_abstract_struct_t *row, void *out_struct) {
  size_t i;
  c_orm_error_t rc;

  if (!router || !row || !out_struct) {
    {
      c_orm_error_t _e = cdd_c_hydrate_router_set_last_error(
          "Invalid arguments to router_dispatch");
      if (_e != C_ORM_OK)
        return _e;
    }
    return C_ORM_ERROR_UNKNOWN;
  }

  {
    c_orm_error_t _e = cdd_c_hydrate_router_set_last_error(NULL);
    if (_e != C_ORM_OK)
      return _e;
  } /* Clear previous error */

  for (i = 0; i < router->count; ++i) {
    if (router->routes[i].query_id_hash == query_id_hash) {
      /* Hand execution cleanly off to the codegen handler */
      rc = router->routes[i].hydrate_fn(out_struct, row);
      if (rc != C_ORM_OK) {
        {
          c_orm_error_t _e = cdd_c_hydrate_router_set_last_error(
              "Hydration function returned error");
          if (_e != C_ORM_OK)
            return _e;
        }
      }
      return rc;
    }
  }

  /* Target not found - indicates consumer must utilize fallback
   * cdd_c_abstract_hydrate mechanics */
  {
    c_orm_error_t _e =
        cdd_c_hydrate_router_set_last_error("Route not found for query ID");
    if (_e != C_ORM_OK)
      return _e;
  }
  return C_ORM_ERROR_UNKNOWN;
}

c_orm_error_t cdd_c_hydrate_router_free(cdd_c_hydrate_router_t *router) {
  if (!router)
    return C_ORM_ERROR_UNKNOWN;
  if (router->routes)
    C_ORM_FREE(router->routes);
  router->routes = NULL;
  router->count = 0;
  router->capacity = 0;
  return C_ORM_OK;
}

#if defined(__clang__) || defined(__GNUC__)
#endif
