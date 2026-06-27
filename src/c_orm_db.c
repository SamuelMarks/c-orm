/**
 * @file c_orm_db.c
 * @brief Core interfaces and vtables implementation.
 */

/* clang-format off */
#include "c_orm_db.h"
#include "c_orm_api.h"
#include "c_orm_log.h"
#include <stddef.h>
/* clang-format on */

/**
 * @brief Gets the last error message.
 * @param db Database instance.
 * @param out_message Pointer to a string pointer to store the message.
 * @return Integer error code, 0 on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_get_last_error_message(c_orm_db_t *db, const char **out_message) {
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_get_last_error_message: entry");
  if (!out_message) {
    LOG_DEBUG("c_orm_get_last_error_message: out_message is NULL");
    rc = C_ORM_ERROR_UNKNOWN;
    LOG_DEBUG("c_orm_get_last_error_message: exit");
    return rc;
  }
  if (!db || !db->vtable || !db->vtable->get_last_error) {
    LOG_DEBUG(
        "c_orm_get_last_error_message: missing db or get_last_error pointer");
    *out_message = "Unknown Error (No DB context)";
    rc = C_ORM_OK;
    LOG_DEBUG("c_orm_get_last_error_message: exit");
    return rc;
  }
  rc = db->vtable->get_last_error(db, out_message);
  LOG_DEBUG("c_orm_get_last_error_message: exit");
  return rc;
}

/**
 * @brief Gets the last error trace.
 * @param db Database instance.
 * @param out_trace Pointer to a string pointer to store the trace.
 * @return Integer error code, 0 on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_get_last_error_trace(c_orm_db_t *db,
                                                      const char **out_trace) {
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_get_last_error_trace: entry");
  if (!out_trace) {
    LOG_DEBUG("c_orm_get_last_error_trace: out_trace is NULL");
    rc = C_ORM_ERROR_UNKNOWN;
    LOG_DEBUG("c_orm_get_last_error_trace: exit");
    return rc;
  }
  if (!db || !db->vtable) {
    LOG_DEBUG("c_orm_get_last_error_trace: missing db or vtable");
    *out_trace = "Unknown Error (No DB context or vtable)";
    rc = C_ORM_ERROR_UNKNOWN;
    LOG_DEBUG("c_orm_get_last_error_trace: exit");
    return rc;
  }
  if (!db->vtable->get_last_trace) {
    LOG_DEBUG("c_orm_get_last_error_trace: driver does not support stack trace "
              "reporting");
    *out_trace = "Driver does not support stack trace reporting";
    rc = C_ORM_ERROR_UNKNOWN;
    LOG_DEBUG("c_orm_get_last_error_trace: exit");
    return rc;
  }
  rc = db->vtable->get_last_trace(db, out_trace);
  LOG_DEBUG("c_orm_get_last_error_trace: exit");
  return rc;
}

/**
 * @brief Sets a log callback.
 * @param db Database instance.
 * @param cb Log callback function.
 * @param user_data User data to pass to the callback.
 */
C_ORM_EXPORT void c_orm_set_log_callback(c_orm_db_t *db, c_orm_log_cb cb,
                                         void *user_data) {
  LOG_DEBUG("c_orm_set_log_callback: entry");
  if (db) {
    db->log_cb = cb;
    db->log_user_data = user_data;
  } else {
    LOG_DEBUG("c_orm_set_log_callback: db is NULL");
  }
  LOG_DEBUG("c_orm_set_log_callback: exit");
}

/**
 * @brief Sets the slow query threshold.
 * @param db Database instance.
 * @param threshold_ms Slow query threshold in milliseconds.
 */
C_ORM_EXPORT void c_orm_set_slow_query_threshold(c_orm_db_t *db,
                                                 uint32_t threshold_ms) {
  LOG_DEBUG("c_orm_set_slow_query_threshold: entry");
  if (db) {
    db->slow_query_threshold_ms = threshold_ms;
  } else {
    LOG_DEBUG("c_orm_set_slow_query_threshold: db is NULL");
  }
  LOG_DEBUG("c_orm_set_slow_query_threshold: exit");
}

/**
 * @brief Gets DB telemetry.
 * @param db Database instance.
 * @param out_telemetry Pointer to telemetry output struct.
 * @return C_ORM_OK on success or memory error.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_get_telemetry(c_orm_db_t *db, c_orm_pool_telemetry_t *out_telemetry) {
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_get_telemetry: entry");
  if (!db) {
    LOG_DEBUG("c_orm_get_telemetry: db is NULL");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_get_telemetry: exit");
    return (c_orm_error_t)rc;
  }
  if (!out_telemetry) {
    LOG_DEBUG("c_orm_get_telemetry: out_telemetry is NULL");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_get_telemetry: exit");
    return (c_orm_error_t)rc;
  }
  *out_telemetry = db->telemetry;
  rc = C_ORM_OK;
  LOG_DEBUG("c_orm_get_telemetry: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Sets an expiration callback.
 * @param db Database instance.
 * @param cb Expire callback function.
 * @param user_data User data for the callback.
 */
C_ORM_EXPORT void c_orm_set_expire_callback(c_orm_db_t *db, c_orm_expire_cb cb,
                                            void *user_data) {
  LOG_DEBUG("c_orm_set_expire_callback: entry");
  if (db) {
    db->expire_cb = cb;
    db->expire_user_data = user_data;
  } else {
    LOG_DEBUG("c_orm_set_expire_callback: db is NULL");
  }
  LOG_DEBUG("c_orm_set_expire_callback: exit");
}

/**
 * @brief Attaches an identity map to a DB instance.
 * @param db Database instance.
 * @param map Pointer to identity map.
 * @return C_ORM_OK on success or memory error.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_db_attach_identity_map(c_orm_db_t *db, c_orm_identity_map_t *map) {
  c_orm_error_t rc;
  LOG_DEBUG("c_orm_db_attach_identity_map: entry");
  if (!db) {
    LOG_DEBUG("c_orm_db_attach_identity_map: db is NULL");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_db_attach_identity_map: exit");
    return (c_orm_error_t)rc;
  }
  db->identity_map = map;
  rc = C_ORM_OK;
  LOG_DEBUG("c_orm_db_attach_identity_map: exit");
  return (c_orm_error_t)rc;
}

/**
 * @brief Registers a query interceptor.
 * @param db Database instance.
 * @param hook Interceptor callback.
 * @param context User context.
 */
C_ORM_EXPORT void c_orm_register_query_interceptor(c_orm_db_t *db,
                                                   c_orm_interceptor_cb hook,
                                                   void *context) {
  LOG_DEBUG("c_orm_register_query_interceptor: entry");
  if (db) {
    db->query_interceptor = hook;
    db->query_interceptor_ctx = context;
  } else {
    LOG_DEBUG("c_orm_register_query_interceptor: db is NULL");
  }
  LOG_DEBUG("c_orm_register_query_interceptor: exit");
}

/**
 * @brief Registers a hydration interceptor.
 * @param db Database instance.
 * @param hook Interceptor callback.
 * @param context User context.
 */
C_ORM_EXPORT void
c_orm_register_hydration_interceptor(c_orm_db_t *db, c_orm_interceptor_cb hook,
                                     void *context) {
  LOG_DEBUG("c_orm_register_hydration_interceptor: entry");
  if (db) {
    db->hydration_interceptor = hook;
    db->hydration_interceptor_ctx = context;
  } else {
    LOG_DEBUG("c_orm_register_hydration_interceptor: db is NULL");
  }
  LOG_DEBUG("c_orm_register_hydration_interceptor: exit");
}

/**
 * @brief Registers cryptographic hooks.
 * @param db Database instance.
 * @param encrypt_hook Hook to encrypt fields.
 * @param decrypt_hook Hook to decrypt fields.
 * @param context User context.
 */
C_ORM_EXPORT void c_orm_register_crypto_hooks(c_orm_db_t *db,
                                              c_orm_crypto_hook_t encrypt_hook,
                                              c_orm_crypto_hook_t decrypt_hook,
                                              void *context) {
  LOG_DEBUG("c_orm_register_crypto_hooks: entry");
  if (db) {
    db->encrypt_hook = encrypt_hook;
    db->decrypt_hook = decrypt_hook;
    db->crypto_context = context;
  } else {
    LOG_DEBUG("c_orm_register_crypto_hooks: db is NULL");
  }
  LOG_DEBUG("c_orm_register_crypto_hooks: exit");
}

/**
 * @brief Sets the database connection timezone.
 * @param db Database instance.
 * @param tz Timezone enum.
 */
C_ORM_EXPORT void c_orm_set_timezone(c_orm_db_t *db, c_orm_timezone_t tz) {
  LOG_DEBUG("c_orm_set_timezone: entry");
  if (db) {
    db->timezone = tz;
  } else {
    LOG_DEBUG("c_orm_set_timezone: db is NULL");
  }
  LOG_DEBUG("c_orm_set_timezone: exit");
}
