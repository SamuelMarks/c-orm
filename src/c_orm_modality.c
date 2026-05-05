/**
 * @file c_orm_modality.c
 * @brief Modality implementation.
 */

/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_db.h"
#include "c_orm_log.h"
/* clang-format on */

/**
 * @brief Sets the modality of the DB.
 */
C_ORM_EXPORT c_orm_error_t c_orm_set_modality(c_orm_db_t *db,
                                              c_orm_modality_t modality,
                                              void *ctx) {
  int rc;
  LOG_DEBUG("c_orm_set_modality: entry");

  if (!db) {
    LOG_DEBUG("c_orm_set_modality: OOM/db null");
    rc = C_ORM_ERROR_MEMORY;
    LOG_DEBUG("c_orm_set_modality: exit");
    return (c_orm_error_t)rc;
  }
  db->modality = modality;
  db->modality_ctx = ctx;

  rc = C_ORM_OK;
  LOG_DEBUG("c_orm_set_modality: exit");
  return (c_orm_error_t)rc;
}
