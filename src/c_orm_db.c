/**
 * @file c_orm_db.c
 * @brief Core interfaces and vtables implementation.
 */

/* clang-format off */
#include "c_orm_db.h"
#include <stddef.h>
/* clang-format on */

C_ORM_EXPORT int c_orm_get_last_error_message(c_orm_db_t *db,
                                              const char **out_message) {
  if (!out_message)
    return 1;
  if (!db || !db->vtable || !db->vtable->get_last_error) {
    *out_message = "Unknown Error (No DB context)";
    return 0;
  }
  return db->vtable->get_last_error(db, out_message);
}

C_ORM_EXPORT int c_orm_get_last_error_trace(c_orm_db_t *db,
                                            const char **out_trace) {
  if (!out_trace)
    return 1;
  if (!db || !db->vtable) {
    *out_trace = "Unknown Error (No DB context or vtable)";
    return 1;
  }
  if (!db->vtable->get_last_trace) {
    *out_trace = "Driver does not support stack trace reporting";
    return 1;
  }
  return db->vtable->get_last_trace(db, out_trace);
}

C_ORM_EXPORT void c_orm_set_log_callback(c_orm_db_t *db, c_orm_log_cb cb,
                                         void *user_data) {
  if (db) {
    db->log_cb = cb;
    db->log_user_data = user_data;
  }
}

C_ORM_EXPORT void c_orm_set_expire_callback(c_orm_db_t *db, c_orm_expire_cb cb,
                                            void *user_data) {
  if (db) {
    db->expire_cb = cb;
    db->expire_user_data = user_data;
  }
}

C_ORM_EXPORT c_orm_error_t
c_orm_db_attach_identity_map(c_orm_db_t *db, c_orm_identity_map_t *map) {
  if (!db)
    return C_ORM_ERROR_MEMORY;
  db->identity_map = map;
  return C_ORM_OK;
}

C_ORM_EXPORT void c_orm_register_query_interceptor(c_orm_db_t *db,
                                                   c_orm_interceptor_cb hook,
                                                   void *context) {
  if (db) {
    db->query_interceptor = hook;
    db->query_interceptor_ctx = context;
  }
}

C_ORM_EXPORT void
c_orm_register_hydration_interceptor(c_orm_db_t *db, c_orm_interceptor_cb hook,
                                     void *context) {
  if (db) {
    db->hydration_interceptor = hook;
    db->hydration_interceptor_ctx = context;
  }
}

C_ORM_EXPORT void c_orm_register_crypto_hooks(c_orm_db_t *db,
                                              c_orm_crypto_hook_t encrypt_hook,
                                              c_orm_crypto_hook_t decrypt_hook,
                                              void *context) {
  if (db) {
    db->encrypt_hook = encrypt_hook;
    db->decrypt_hook = decrypt_hook;
    db->crypto_context = context;
  }
}

C_ORM_EXPORT void c_orm_set_timezone(c_orm_db_t *db, c_orm_timezone_t tz) {
  if (db) {
    db->timezone = tz;
  }
}
