/**
 * @file c_orm_oauth2.c
 * @brief Implementation of OAuth 2.0 and User schemas support.
 */

/* clang-format off */
#include "c_orm_oauth2.h"
#include <stddef.h>
/* clang-format on */

c_orm_error_t c_orm_oauth2_is_token_valid(const c_orm_oauth2_token_t *token,
                                          int64_t current_time,
                                          int *out_is_valid) {
  if (!token || !out_is_valid) {
    return C_ORM_ERROR_MEMORY;
  }

  /* expires_in is in seconds, created_at is a UNIX timestamp.
     Token is valid if: created_at + expires_in > current_time */
  if ((token->created_at + token->expires_in) > current_time) {
    *out_is_valid = 1;
  } else {
    *out_is_valid = 0;
  }

  return C_ORM_OK;
}

c_orm_error_t c_orm_oauth2_encrypt_token(const char *plain_token,
                                         char **out_encrypted_token) {
  if (!plain_token || !out_encrypted_token)
    return C_ORM_ERROR_MEMORY;
  *out_encrypted_token = NULL;
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}

c_orm_error_t c_orm_oauth2_decrypt_token(const char *encrypted_token,
                                         char **out_plain_token) {
  if (!encrypted_token || !out_plain_token)
    return C_ORM_ERROR_MEMORY;
  *out_plain_token = NULL;
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}
