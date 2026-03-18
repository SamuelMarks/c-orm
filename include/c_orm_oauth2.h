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
  char *user_id;
  char *scopes;
} c_orm_oauth2_token_t;

/**
 * @brief Represents an OAuth 2.0 Client.
 */
typedef struct {
  char *id;
  char *client_secret;
  char *redirect_uris;
  char *grant_types;
  char *scopes;
} c_orm_oauth2_client_t;

/**
 * @brief Represents an Authorization Code in the Authorization Code Flow.
 */
typedef struct {
  char *code;
  char *client_id;
  char *redirect_uri;
  char *user_id;
  int64_t expires_at;
  char *scopes;
} c_orm_oauth2_auth_code_t;

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
 * @brief Represents an active user session linking a user and a token.
 */
typedef struct {
  char *session_id;
  c_orm_user_t *user;
  c_orm_oauth2_token_t *token;
} c_orm_session_t;

/**
 * @brief Table metadata for the users table.
 */
C_ORM_EXPORT extern const c_orm_table_meta_t c_orm_user_meta;

/**
 * @brief Table metadata for the tokens table.
 */
C_ORM_EXPORT extern const c_orm_table_meta_t c_orm_token_meta;

/**
 * @brief Table metadata for the clients table.
 */
C_ORM_EXPORT extern const c_orm_table_meta_t c_orm_oauth2_client_meta;

/**
 * @brief Table metadata for the authorization codes table.
 */
C_ORM_EXPORT extern const c_orm_table_meta_t c_orm_auth_code_meta;

/**
 * @brief Verifies a client's credentials.
 *
 * @param db Database connection.
 * @param client_id The client's ID.
 * @param client_secret The client's secret.
 * @param out_is_valid Pointer to an integer that will be 1 if valid, 0
 * otherwise.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_oauth2_verify_client(c_orm_db_t *db,
                                                      const char *client_id,
                                                      const char *client_secret,
                                                      int *out_is_valid);

/**
 * @brief Saves a newly minted token to the database.
 *
 * @param db Database connection.
 * @param token The token structure.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_oauth2_save_token(c_orm_db_t *db, const c_orm_oauth2_token_t *token);

/**
 * @brief Retrieves a token from the database by access token string.
 *
 * @param db Database connection.
 * @param access_token The access token string.
 * @param out_token Pointer to an uninitialized token structure to populate.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_oauth2_get_token(
    c_orm_db_t *db, const char *access_token, c_orm_oauth2_token_t *out_token);

/**
 * @brief Revokes a token by deleting it or marking it revoked.
 *
 * @param db Database connection.
 * @param token_str The access token string.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_oauth2_revoke_token(c_orm_db_t *db,
                                                     const char *token_str);

/**
 * @brief Validates if requested scopes are a subset of granted scopes.
 *
 * @param granted_scopes A space-separated list of granted scopes.
 * @param requested_scopes A space-separated list of requested scopes.
 * @param out_is_valid Pointer to an integer that will be 1 if valid, 0
 * otherwise.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_oauth2_validate_scope(const char *granted_scopes,
                            const char *requested_scopes, int *out_is_valid);

/**
 * @brief Saves an authorization code to the database.
 *
 * @param db Database connection.
 * @param auth_code The authorization code structure.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_oauth2_save_auth_code(
    c_orm_db_t *db, const c_orm_oauth2_auth_code_t *auth_code);

/**
 * @brief Consumes an authorization code, removing it from the database and
 * populating the output.
 *
 * @param db Database connection.
 * @param code The authorization code string.
 * @param out_auth_code Pointer to an uninitialized auth code structure to
 * populate.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_oauth2_consume_auth_code(
    c_orm_db_t *db, const char *code, c_orm_oauth2_auth_code_t *out_auth_code);

/**
 * @brief Deletes expired tokens from the database.
 *
 * @param db Database connection.
 * @param current_time The current UNIX timestamp.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_oauth2_cleanup_expired_tokens(c_orm_db_t *db, int64_t current_time);

/**
 * @brief Verifies a user's credentials against the database.
 *
 * @param db Database connection.
 * @param username The username to verify.
 * @param password The plaintext password to verify.
 * @param out_is_valid Pointer to an integer that will be 1 if valid, 0
 * otherwise.
 * @return C_ORM_OK on success (even if credentials are invalid).
 */
C_ORM_EXPORT c_orm_error_t c_orm_user_verify_credentials(c_orm_db_t *db,
                                                         const char *username,
                                                         const char *password,
                                                         int *out_is_valid);

/**
 * @brief Checks if a token is valid, possibly triggering logic if it is
 * expired.
 *
 * @param token The token structure to check.
 * @param current_time UNIX timestamp of current time.
 * @param out_is_valid Pointer to receive boolean validity.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_oauth2_is_token_valid(
    const c_orm_oauth2_token_t *token, int64_t current_time, int *out_is_valid);

/**
 * @brief Parses an OAuth 2.0 JSON response into a token structure.
 *
 * @param json The JSON payload string.
 * @param out_token The token structure to populate.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_oauth2_token_parse_json(
    const char *json, c_orm_oauth2_token_t *out_token);

/**
 * @brief Encrypts a plain token string for secure storage.
 *
 * @param plain_token The plaintext token.
 * @param out_encrypted_token Pointer to receive the allocated encrypted string.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_oauth2_encrypt_token(const char *plain_token, char **out_encrypted_token);

/**
 * @brief Decrypts an encrypted token string.
 *
 * @param encrypted_token The encrypted token string.
 * @param out_plain_token Pointer to receive the allocated plaintext string.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_oauth2_decrypt_token(const char *encrypted_token, char **out_plain_token);

/**
 * @brief Securely stores a token using a platform-specific mechanism (e.g.
 * Web).
 *
 * @param token The token structure to store.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_store_token_secure(const c_orm_oauth2_token_t *token);

/**
 * @brief Get the current UNIX timestamp in seconds.
 *
 * @param out_timestamp Pointer to receive the 64-bit timestamp.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_oauth2_get_current_timestamp(int64_t *out_timestamp);

/**
 * @brief Calculate the expiration timestamp given current time and lifetime.
 *
 * @param current_timestamp The start time.
 * @param expires_in The number of seconds until expiration.
 * @param out_expiration Pointer to receive the calculated timestamp.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_oauth2_calculate_expiration(
    int64_t current_timestamp, int32_t expires_in, int64_t *out_expiration);

/**
 * @brief Generate dialect-specific CREATE TABLE statements for OAuth2 models.
 *
 * @param db Database connection.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_oauth2_create_tables(c_orm_db_t *db);

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* C_ORM_OAUTH2_H */
