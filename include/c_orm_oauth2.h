/**
 * @file c_orm_oauth2.h
 * @brief OAuth 2.0 and User schema definitions for c-orm.
 */

#ifndef C_ORM_OAUTH2_H
#define C_ORM_OAUTH2_H
/* clang-format off */
#include "c_orm_db.h"
/* clang-format on */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Represents an OAuth 2.0 token response/storage.
 */
typedef struct {
  char *access_token;
  char *refresh_token;
  char *token_type;
  int32_t expires_in; /* seconds */
  int64_t created_at; /* UNIX timestamp */
} c_orm_oauth2_token_t;

/**
 * @brief Represents a server-side User.
 */
typedef struct {
  char *id; /* UUID or opaque string */
  char *username;
  char *password_hash;
  char *salt;
} c_orm_user_t;

/**
 * @brief Represents a client-side User (logged-in entity).
 */
typedef struct {
  char *id;
  char *username;
} c_orm_client_user_t;

/**
 * @brief Checks if a token is valid, possibly triggering logic if it is
 * expired.
 *
 * @param token The token structure to check.
 * @param current_time UNIX timestamp of current time.
 * @param out_is_valid Pointer to receive boolean validity.
 * @return C_ORM_OK on success.
 */
c_orm_error_t c_orm_oauth2_is_token_valid(const c_orm_oauth2_token_t *token,
                                          int64_t current_time,
                                          int *out_is_valid);

/**
 * @brief Encrypts a plain token string for secure storage.
 *
 * @param plain_token The plaintext token.
 * @param out_encrypted_token Pointer to receive the allocated encrypted string.
 * @return C_ORM_OK on success.
 */
c_orm_error_t c_orm_oauth2_encrypt_token(const char *plain_token,
                                         char **out_encrypted_token);

/**
 * @brief Decrypts an encrypted token string.
 *
 * @param encrypted_token The encrypted token string.
 * @param out_plain_token Pointer to receive the allocated plaintext string.
 * @return C_ORM_OK on success.
 */
c_orm_error_t c_orm_oauth2_decrypt_token(const char *encrypted_token,
                                         char **out_plain_token);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* C_ORM_OAUTH2_H */
