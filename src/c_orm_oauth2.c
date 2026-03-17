/**
 * @file c_orm_oauth2.c
 * @brief Implementation of OAuth 2.0 and User schemas support.
 */

/* clang-format off */
#include "c_orm_oauth2.h"
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
/* clang-format on */

/* Helper to extract a string value from a simple flat JSON object */
static char *extract_json_string(const char *json, const char *key) {
  char search_key[128];
  const char *pos;
  const char *start;
  const char *end;
  char *result;
  size_t len;

#if defined(_MSC_VER)
  sprintf_s(search_key, sizeof(search_key), "\"%s\"", key);
#else
  sprintf(search_key, "\"%s\"", key);
#endif

  pos = strstr(json, search_key);
  if (!pos)
    return NULL;

  pos = strchr(pos + strlen(search_key), ':');
  if (!pos)
    return NULL;

  start = strchr(pos, '"');
  if (!start)
    return NULL;
  start++;

  end = strchr(start, '"');
  if (!end)
    return NULL;

  len = (size_t)(end - start);
  result = (char *)malloc(len + 1);
  if (!result)
    return NULL;

  /* Fallback to strncpy since strncpy_s is MSVC only */
#if defined(_MSC_VER)
  strncpy_s(result, len + 1, start, len);
#else
  strncpy(result, start, len);
  result[len] = '\0';
#endif

  return result;
}

/* Helper to extract an integer value from a simple flat JSON object */
static int32_t extract_json_int(const char *json, const char *key) {
  char search_key[128];
  const char *pos;
  const char *val_start;

#if defined(_MSC_VER)
  sprintf_s(search_key, sizeof(search_key), "\"%s\"", key);
#else
  sprintf(search_key, "\"%s\"", key);
#endif

  pos = strstr(json, search_key);
  if (!pos)
    return 0;

  pos = strchr(pos + strlen(search_key), ':');
  if (!pos)
    return 0;

  pos++;
  /* skip whitespace */
  while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r')
    pos++;

  val_start = pos;
  /* find end of number */
  while (*pos >= '0' && *pos <= '9')
    pos++;

  if (pos > val_start) {
    return (int32_t)atoi(val_start);
  }
  return 0;
}

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

c_orm_error_t c_orm_oauth2_token_parse_json(const char *json,
                                            c_orm_oauth2_token_t *out_token) {
  if (!json || !out_token) {
    return C_ORM_ERROR_MEMORY;
  }

  out_token->access_token = extract_json_string(json, "access_token");
  out_token->refresh_token = extract_json_string(json, "refresh_token");
  out_token->token_type = extract_json_string(json, "token_type");
  out_token->expires_in = extract_json_int(json, "expires_in");
  out_token->created_at = 0; /* Should be set by caller */

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

c_orm_error_t c_orm_store_token_secure(const c_orm_oauth2_token_t *token) {
  if (!token)
    return C_ORM_ERROR_MEMORY;
  /* Implementation depends on platform (e.g. IndexedDB for Web) */
  return C_ORM_ERROR_NOT_IMPLEMENTED;
}
