/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_db.h"
/* clang-format on */

C_ORM_EXPORT c_orm_error_t c_orm_set_modality(c_orm_db_t *db,
                                              c_orm_modality_t modality,
                                              void *ctx) {
  if (!db)
    return C_ORM_ERROR_MEMORY;
  db->modality = modality;
  db->modality_ctx = ctx;
  return C_ORM_OK;
}
