#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file c_orm_oauth2.c
 * @brief Implementation of OAuth 2.0 and User schemas support.
 */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_oauth2.h"
#include "c_orm_api.h"
#include "c_orm_sqlite.h"
#include "c_orm_postgres.h"
#include "c_orm_mysql.h"
#include "c_orm_log.h"
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#if defined(_MSC_VER)

typedef struct _CRYPTOAPI_BLOB {
  unsigned long cbData;
  unsigned char *pbData;
} DATA_BLOB;

__declspec(dllimport) void * __stdcall LoadLibraryA(const char *lpLibFileName);
typedef int(__stdcall *FARPROC_t)(void);
__declspec(dllimport) FARPROC_t __stdcall GetProcAddress(void *hModule, const char *lpProcName);
__declspec(dllimport) int __stdcall FreeLibrary(void *hLibModule);

typedef int(__stdcall *CryptProtectData_t)(DATA_BLOB *, const wchar_t *, DATA_BLOB *, void *, void *, unsigned long, DATA_BLOB *);
typedef int(__stdcall *CryptUnprotectData_t)(DATA_BLOB *, const wchar_t **, DATA_BLOB *, void *, void *, unsigned long, DATA_BLOB *);

__declspec(dllimport) void * __stdcall LocalFree(void *hMem);

#define C_ORM_CRYPTPROTECT_UI_FORBIDDEN 0x1
#endif
/* clang-format on */

/**
 * @brief Internal JSON field structure for token parsing.
 */
typedef struct {
  const char *key;
  char **str_out;
  int32_t *int_out;
} json_field_t;

/**
 * @brief Parses a flat JSON string into an array of fields.
 * @param json The JSON string to parse.
 * @param fields The array of fields to populate.
 * @param num_fields The number of fields in the array.
 */
static void parse_flat_json(const char *json, json_field_t *fields,
                            size_t num_fields) {
  const char *p = json;
  int in_string = 0;
  int escaping = 0;
  char current_key[256];
  size_t key_len = 0;
  int expecting_val = 0;

  LOG_DEBUG("parse_flat_json: entered");
  current_key[0] = '\0';

  while (*p) {
    if (escaping) {
      escaping = 0;
      p++;
      continue;
    }
    if (*p == '\\') {
      escaping = 1;
      p++;
      continue;
    }

    if (*p == '"') {
      in_string = !in_string;
      if (in_string && !expecting_val) {
        /* Start of key */
        p++;
        key_len = 0;
        while (*p && *p != '"' && key_len < sizeof(current_key) - 1) {
          if (*p == '\\') {
            p++;
            if (!*p) {
              break;
            }
          }
          current_key[key_len++] = *p++;
        }
        current_key[key_len] = '\0';
        if (*p == '"') {
          in_string = 0;
        }
      } else if (in_string && expecting_val) {
        /* Start of string value */
        p++;
        {
          const char *val_start = p;
          size_t val_len = 0;
          char *val_str;
          while (*p && *p != '"') {
            if (*p == '\\') {
              p++;
              if (!*p) {
                break;
              }
            }
            val_len++;
            p++;
          }
          val_str = (char *)C_ORM_MALLOC(val_len + 1);
          if (val_str) {
            const char *v = val_start;
            size_t i = 0;
            while (v < p) {
              if (*v == '\\') {
                v++;
                if (v >= p) {
                  break;
                }
              }
              val_str[i++] = *v++;
            }
            val_str[i] = '\0';

            {
              size_t f;
              for (f = 0; f < num_fields; ++f) {
                if (strcmp(fields[f].key, current_key) == 0 &&
                    fields[f].str_out) {
                  *fields[f].str_out = val_str;
                  val_str = NULL;
                  break;
                }
              }
            }
            if (val_str) {
              C_ORM_FREE(val_str);
            }
          } else {
            LOG_DEBUG("parse_flat_json: OOM");
          }
          if (*p == '"') {
            in_string = 0;
          }
          expecting_val = 0;
          current_key[0] = '\0';
        }
      }
    } else if (!in_string) {
      if (*p == ':') {
        expecting_val = 1;
      } else if (*p == ',' || *p == '}') {
        expecting_val = 0;
        current_key[0] = '\0';
      } else if (expecting_val && (*p == '-' || (*p >= '0' && *p <= '9'))) {
        /* Start of number */
        int32_t val = (int32_t)atoi(p);
        size_t f;
        for (f = 0; f < num_fields; ++f) {
          if (strcmp(fields[f].key, current_key) == 0 && fields[f].int_out) {
            *fields[f].int_out = val;
            break;
          }
        }
        while (*p && (*p == '-' || (*p >= '0' && *p <= '9'))) {
          p++;
        }
        expecting_val = 0;
        current_key[0] = '\0';
        continue; /* already advanced p */
      }
    }
    if (!*p) {
      break;
    }
    p++;
  }
  LOG_DEBUG("parse_flat_json: exiting");
}

/**
 * @brief Checks if an OAuth2 token is valid based on its expiration.
 * @param token The token to check.
 * @param current_time The current time timestamp.
 * @param out_is_valid Pointer to store the validity status.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_oauth2_is_token_valid(const c_orm_oauth2_token_t *token,
                            int64_t current_time, int *out_is_valid) {
  LOG_DEBUG("c_orm_oauth2_is_token_valid: entered");
  if (!token || !out_is_valid) {
    LOG_DEBUG("c_orm_oauth2_is_token_valid: validation error");
    return C_ORM_ERROR_VALIDATION;
  }

  if ((token->created_at + token->expires_in) > current_time) {
    *out_is_valid = 1;
  } else {
    *out_is_valid = 0;
  }

  LOG_DEBUG("c_orm_oauth2_is_token_valid: exiting");
  return C_ORM_OK;
}

/**
 * @brief Parses an OAuth2 token from a JSON string.
 * @param json The JSON string.
 * @param out_token Pointer to store the parsed token.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_oauth2_token_parse_json(
    const char *json, c_orm_oauth2_token_t *out_token) {
  json_field_t fields[4];

  LOG_DEBUG("c_orm_oauth2_token_parse_json: entered");

  if (!json || !out_token) {
    LOG_DEBUG("c_orm_oauth2_token_parse_json: validation error");
    return C_ORM_ERROR_VALIDATION;
  }

  out_token->access_token = NULL;
  out_token->refresh_token = NULL;
  out_token->token_type = NULL;
  out_token->expires_in = 0;
  out_token->created_at = 0;

  fields[0].key = "access_token";
  fields[0].str_out = &out_token->access_token;
  fields[0].int_out = NULL;

  fields[1].key = "refresh_token";
  fields[1].str_out = &out_token->refresh_token;
  fields[1].int_out = NULL;

  fields[2].key = "token_type";
  fields[2].str_out = &out_token->token_type;
  fields[2].int_out = NULL;

  fields[3].key = "expires_in";
  fields[3].str_out = NULL;
  fields[3].int_out = &out_token->expires_in;

  parse_flat_json(json, fields, 4);

  LOG_DEBUG("c_orm_oauth2_token_parse_json: exiting");
  return C_ORM_OK;
}

/**
 * @brief Encrypts a plain text token.
 * @param plain_token The plain text token.
 * @param out_encrypted_token Pointer to store the encrypted token.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_oauth2_encrypt_token(
    const char *plain_token, char **out_encrypted_token) {
  LOG_DEBUG("c_orm_oauth2_encrypt_token: entered");

  if (!plain_token || !out_encrypted_token) {
    LOG_DEBUG("c_orm_oauth2_encrypt_token: validation error");
    return C_ORM_ERROR_VALIDATION;
  }

#if defined(_MSC_VER)
  {
    DATA_BLOB in_blob;
    DATA_BLOB out_blob;
    size_t i;
    char *hex_str;

    in_blob.pbData = (unsigned char *)plain_token;
    in_blob.cbData = (unsigned long)strlen(plain_token);

    {
      void *hCrypt32 = LoadLibraryA("crypt32.dll");
      if (hCrypt32) {
        CryptProtectData_t pCryptProtectData =
            (CryptProtectData_t)GetProcAddress(hCrypt32, "CryptProtectData");
        if (pCryptProtectData &&
            pCryptProtectData(&in_blob, L"c_orm_token", NULL, NULL, NULL,
                              C_ORM_CRYPTPROTECT_UI_FORBIDDEN, &out_blob)) {
          hex_str = (char *)C_ORM_MALLOC((out_blob.cbData * 2) + 1);
          if (!hex_str) {
            LOG_DEBUG("c_orm_oauth2_encrypt_token: OOM");
            LocalFree(out_blob.pbData);
            return C_ORM_ERROR_VALIDATION;
          }
          for (i = 0; i < out_blob.cbData; ++i) {
            C_ORM_SPRINTF(&hex_str[i * 2], 3, "%02x", out_blob.pbData[i]);
          }
          hex_str[out_blob.cbData * 2] = '\0';
          LocalFree(out_blob.pbData);
          *out_encrypted_token = hex_str;
          FreeLibrary(hCrypt32);
          LOG_DEBUG("c_orm_oauth2_encrypt_token: exiting");
          return C_ORM_OK;
        }
        FreeLibrary(hCrypt32);
      }
    }
    LOG_DEBUG("c_orm_oauth2_encrypt_token: unknown error");
    return C_ORM_ERROR_UNKNOWN;
  }
#else
  {
    size_t len = strlen(plain_token);
    char *hex_str = (char *)C_ORM_MALLOC((len * 2) + 1);
    size_t i;
    if (!hex_str) {
      LOG_DEBUG("c_orm_oauth2_encrypt_token: OOM");
      return C_ORM_ERROR_VALIDATION;
    }
    for (i = 0; i < len; ++i) {
      unsigned char c = (unsigned char)(plain_token[i] ^ 0x42);
      C_ORM_SPRINTF(&hex_str[i * 2], 3, "%02x", c);
    }
    hex_str[len * 2] = '\0';
    *out_encrypted_token = hex_str;
    LOG_DEBUG("c_orm_oauth2_encrypt_token: exiting");
    return C_ORM_OK;
  }
#endif
}

/**
 * @brief Converts a hex character to a byte.
 * @param c The hex character.
 * @return The byte value.
 */
static c_orm_error_t hex_to_byte(char c, unsigned char *out) {
  if (c >= '0' && c <= '9') {
    *out = (unsigned char)(c - '0');
    return C_ORM_OK;
  }
  if (c >= 'a' && c <= 'f') {
    *out = (unsigned char)(c - 'a' + 10);
    return C_ORM_OK;
  }
  if (c >= 'A' && c <= 'F') {
    *out = (unsigned char)(c - 'A' + 10);
    return C_ORM_OK;
  }
  *out = 0;
  return C_ORM_OK;
}

/**
 * @brief Decrypts an encrypted token.
 * @param encrypted_token The encrypted token.
 * @param out_plain_token Pointer to store the decrypted token.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_oauth2_decrypt_token(
    const char *encrypted_token, char **out_plain_token) {
  LOG_DEBUG("c_orm_oauth2_decrypt_token: entered");

  if (!encrypted_token || !out_plain_token) {
    LOG_DEBUG("c_orm_oauth2_decrypt_token: validation error");
    return C_ORM_ERROR_VALIDATION;
  }

#if defined(_MSC_VER)
  {
    DATA_BLOB in_blob;
    DATA_BLOB out_blob;
    size_t hex_len = strlen(encrypted_token);
    size_t bin_len = hex_len / 2;
    unsigned char *bin_data;
    size_t i;

    bin_data = (unsigned char *)C_ORM_MALLOC(bin_len);
    if (!bin_data) {
      LOG_DEBUG("c_orm_oauth2_decrypt_token: OOM");
      return C_ORM_ERROR_VALIDATION;
    }

    for (i = 0; i < bin_len; ++i) {
      {
        unsigned char b1, b2;
        hex_to_byte(encrypted_token[i * 2], &b1);
        hex_to_byte(encrypted_token[i * 2 + 1], &b2);
        bin_data[i] = (unsigned char)((b1 << 4) | b2);
      }
    }

    in_blob.pbData = bin_data;
    in_blob.cbData = (unsigned long)bin_len;

    {
      void *hCrypt32 = LoadLibraryA("crypt32.dll");
      if (hCrypt32) {
        CryptUnprotectData_t pCryptUnprotectData =
            (CryptUnprotectData_t)GetProcAddress(hCrypt32,
                                                 "CryptUnprotectData");
        if (pCryptUnprotectData &&
            pCryptUnprotectData(&in_blob, NULL, NULL, NULL, NULL,
                                C_ORM_CRYPTPROTECT_UI_FORBIDDEN, &out_blob)) {
          char *plain = (char *)C_ORM_MALLOC(out_blob.cbData + 1);
          if (!plain) {
            LOG_DEBUG("c_orm_oauth2_decrypt_token: OOM");
            LocalFree(out_blob.pbData);
            C_ORM_FREE(bin_data);
            return C_ORM_ERROR_VALIDATION;
          }
          memcpy(plain, out_blob.pbData, out_blob.cbData);
          plain[out_blob.cbData] = '\0';
          LocalFree(out_blob.pbData);
          C_ORM_FREE(bin_data);
          *out_plain_token = plain;
          FreeLibrary(hCrypt32);
          LOG_DEBUG("c_orm_oauth2_decrypt_token: exiting");
          return C_ORM_OK;
        }
        FreeLibrary(hCrypt32);
      }
    }
    C_ORM_FREE(bin_data);
    LOG_DEBUG("c_orm_oauth2_decrypt_token: unknown error");
    return C_ORM_ERROR_UNKNOWN;
  }
#else
  {
    size_t hex_len = strlen(encrypted_token);
    size_t bin_len = hex_len / 2;
    char *plain = (char *)C_ORM_MALLOC(bin_len + 1);
    size_t i;
    if (!plain) {
      LOG_DEBUG("c_orm_oauth2_decrypt_token: OOM");
      return C_ORM_ERROR_VALIDATION;
    }
    for (i = 0; i < bin_len; ++i) {
      unsigned char b1, b2, c;
      hex_to_byte(encrypted_token[i * 2], &b1);
      hex_to_byte(encrypted_token[i * 2 + 1], &b2);
      c = (unsigned char)((b1 << 4) | b2);
      plain[i] = (char)(c ^ 0x42);
    }
    plain[bin_len] = '\0';
    *out_plain_token = plain;
    LOG_DEBUG("c_orm_oauth2_decrypt_token: exiting");
    return C_ORM_OK;
  }
#endif
}

/**
 * @brief Stores an OAuth2 token securely on disk.
 * @param token The token to store.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_store_token_secure(const c_orm_oauth2_token_t *token) {
  char *encrypted_access = NULL;
  char *encrypted_refresh = NULL;
  c_orm_error_t rc;
  FILE *f;

  LOG_DEBUG("c_orm_store_token_secure: entered");

  if (!token) {
    LOG_DEBUG("c_orm_store_token_secure: validation error");
    return C_ORM_ERROR_VALIDATION;
  }

  rc = c_orm_oauth2_encrypt_token(
      token->access_token ? token->access_token : "", &encrypted_access);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_store_token_secure: encrypt access error");
    return rc;
  }

  rc = c_orm_oauth2_encrypt_token(
      token->refresh_token ? token->refresh_token : "", &encrypted_refresh);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_store_token_secure: encrypt refresh error");
    C_ORM_FREE(encrypted_access);
    return rc;
  }

#if defined(_MSC_VER)
  if (fopen_s(&f, "c_orm_token.dat", "w") != 0) {
    f = NULL;
  }
#else
  C_ORM_FOPEN(&f, "c_orm_token.dat", "w");
#endif

  if (!f) {
    LOG_DEBUG("c_orm_store_token_secure: file open error");
    C_ORM_FREE(encrypted_access);
    C_ORM_FREE(encrypted_refresh);
    return C_ORM_ERROR_UNKNOWN;
  }

  fprintf(f, "%s\n%s\n", encrypted_access, encrypted_refresh);
  fclose(f);

  C_ORM_FREE(encrypted_access);
  C_ORM_FREE(encrypted_refresh);

  LOG_DEBUG("c_orm_store_token_secure: exiting");
  return C_ORM_OK;
}

/**
 * @brief Gets the current timestamp.
 * @param out_timestamp Pointer to store the timestamp.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_oauth2_get_current_timestamp(int64_t *out_timestamp) {
  LOG_DEBUG("c_orm_oauth2_get_current_timestamp: entered");
  if (!out_timestamp) {
    LOG_DEBUG("c_orm_oauth2_get_current_timestamp: validation error");
    return C_ORM_ERROR_VALIDATION;
  }
  *out_timestamp = (int64_t)time(NULL);
  LOG_DEBUG("c_orm_oauth2_get_current_timestamp: exiting");
  return C_ORM_OK;
}

/**
 * @brief Calculates the expiration timestamp for a token.
 * @param current_timestamp The current timestamp.
 * @param expires_in The token's expiration duration.
 * @param out_expiration Pointer to store the expiration timestamp.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_oauth2_calculate_expiration(
    int64_t current_timestamp, int32_t expires_in, int64_t *out_expiration) {
  LOG_DEBUG("c_orm_oauth2_calculate_expiration: entered");
  if (!out_expiration) {
    LOG_DEBUG("c_orm_oauth2_calculate_expiration: validation error");
    return C_ORM_ERROR_VALIDATION;
  }
  *out_expiration = current_timestamp + (int64_t)expires_in;
  LOG_DEBUG("c_orm_oauth2_calculate_expiration: exiting");
  return C_ORM_OK;
}

/**
 * @brief Creates necessary tables for OAuth2 support.
 * @param db The database connection.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_oauth2_create_tables(c_orm_db_t *db) {
  const c_orm_driver_vtable_t *sqlite_vt = NULL;
  const c_orm_driver_vtable_t *pg_vt = NULL;
  const c_orm_driver_vtable_t *my_vt = NULL;
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_oauth2_create_tables: entered");

  if (!db || !db->vtable) {
    LOG_DEBUG("c_orm_oauth2_create_tables: validation error");
    return C_ORM_ERROR_VALIDATION;
  }

  sqlite_vt = NULL;
  c_orm_sqlite_get_vtable(&sqlite_vt);

  pg_vt = NULL;
  c_orm_postgres_get_vtable(&pg_vt);

  my_vt = NULL;
  c_orm_mysql_get_vtable(&my_vt);

  if (sqlite_vt && db->vtable == sqlite_vt) {
    rc = c_orm_execute_raw(db, "CREATE TABLE IF NOT EXISTS users ("
                               "id TEXT PRIMARY KEY, "
                               "username TEXT UNIQUE, "
                               "password_hash TEXT, "
                               "salt TEXT);");
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_oauth2_create_tables: users table creation error");
      return rc;
    }
    rc = c_orm_execute_raw(db, "CREATE TABLE IF NOT EXISTS tokens ("
                               "access_token TEXT PRIMARY KEY, "
                               "refresh_token TEXT, "
                               "token_type TEXT, "
                               "expires_in INTEGER, "
                               "created_at INTEGER, "
                               "user_id TEXT, "
                               "scopes TEXT, "
                               "FOREIGN KEY(user_id) REFERENCES users(id));");
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_oauth2_create_tables: tokens table creation error");
      return rc;
    }
    rc = c_orm_execute_raw(db, "CREATE TABLE IF NOT EXISTS clients ("
                               "id TEXT PRIMARY KEY, "
                               "client_secret TEXT, "
                               "redirect_uris TEXT, "
                               "scopes TEXT, "
                               "grant_types TEXT);");
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_oauth2_create_tables: clients table creation error");
      return rc;
    }
    rc = c_orm_execute_raw(db, "CREATE TABLE IF NOT EXISTS auth_codes ("
                               "code TEXT PRIMARY KEY, "
                               "client_id TEXT, "
                               "redirect_uri TEXT, "
                               "user_id TEXT, "
                               "expires_at INTEGER, "
                               "scopes TEXT);");
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_oauth2_create_tables: auth_codes table creation error");
      return rc;
    }
    LOG_DEBUG("c_orm_oauth2_create_tables: exiting");
    return C_ORM_OK;
  } else {
    rc = c_orm_execute_raw(db, "CREATE TABLE IF NOT EXISTS users ("
                               "id VARCHAR(255) PRIMARY KEY, "
                               "username VARCHAR(255) UNIQUE, "
                               "password_hash VARCHAR(255), "
                               "salt VARCHAR(255));");
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_oauth2_create_tables: users table creation error");
      return rc;
    }
    rc = c_orm_execute_raw(db, "CREATE TABLE IF NOT EXISTS tokens ("
                               "access_token VARCHAR(255) PRIMARY KEY, "
                               "refresh_token VARCHAR(255), "
                               "token_type VARCHAR(255), "
                               "expires_in INT, "
                               "created_at BIGINT, "
                               "user_id VARCHAR(255), "
                               "scopes VARCHAR(255), "
                               "FOREIGN KEY(user_id) REFERENCES users(id));");
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_oauth2_create_tables: tokens table creation error");
      return rc;
    }
    rc = c_orm_execute_raw(db, "CREATE TABLE IF NOT EXISTS clients ("
                               "id VARCHAR(255) PRIMARY KEY, "
                               "client_secret VARCHAR(255), "
                               "redirect_uris VARCHAR(255), "
                               "scopes VARCHAR(255), "
                               "grant_types VARCHAR(255));");
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_oauth2_create_tables: clients table creation error");
      return rc;
    }
    rc = c_orm_execute_raw(db, "CREATE TABLE IF NOT EXISTS auth_codes ("
                               "code VARCHAR(255) PRIMARY KEY, "
                               "client_id VARCHAR(255), "
                               "redirect_uri VARCHAR(255), "
                               "user_id VARCHAR(255), "
                               "expires_at BIGINT, "
                               "scopes VARCHAR(255));");
    if (rc != C_ORM_OK) {
      LOG_DEBUG("c_orm_oauth2_create_tables: auth_codes table creation error");
      return rc;
    }
    LOG_DEBUG("c_orm_oauth2_create_tables: exiting");
    return C_ORM_OK;
  }
}

/**
 * @brief User table columns.
 */
static const c_orm_column_meta_t user_cols[] = {
    {"id", C_ORM_TYPE_STRING, offsetof(c_orm_user_t, id), true, false, NULL,
     false, false},
    {"username", C_ORM_TYPE_STRING, offsetof(c_orm_user_t, username), false,
     false, NULL, false, false},
    {"password_hash", C_ORM_TYPE_STRING, offsetof(c_orm_user_t, password_hash),
     false, true, NULL, false, true},
    {"salt", C_ORM_TYPE_STRING, offsetof(c_orm_user_t, salt), false, true, NULL,
     false, false}};

/**
 * @brief User table metadata.
 */
C_ORM_EXPORT const c_orm_table_meta_t c_orm_user_meta = {
    "users",
    user_cols,
    4,
    sizeof(c_orm_user_t),
    "SELECT * FROM users",
    "SELECT * FROM users WHERE id = ?",
    "INSERT INTO users (id, username, password_hash, salt) VALUES (?, ?, ?, ?)",
    "UPDATE users SET username = ?, password_hash = ?, salt = ? WHERE id = ?",
    "DELETE FROM users WHERE id = ?",
    "SELECT * FROM users WHERE id = ? FOR UPDATE",
    false,
    false,
    0,
    0,
    {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
    NULL,
    0};

/**
 * @brief Token table columns.
 */
static const c_orm_column_meta_t token_cols[] = {
    {"access_token", C_ORM_TYPE_STRING,
     offsetof(c_orm_oauth2_token_t, access_token), true, false, NULL, false,
     true},
    {"refresh_token", C_ORM_TYPE_STRING,
     offsetof(c_orm_oauth2_token_t, refresh_token), false, true, NULL, false,
     true},
    {"token_type", C_ORM_TYPE_STRING,
     offsetof(c_orm_oauth2_token_t, token_type), false, true, NULL, false,
     false},
    {"expires_in", C_ORM_TYPE_INT32, offsetof(c_orm_oauth2_token_t, expires_in),
     false, false, NULL, false, false},
    {"created_at", C_ORM_TYPE_INT64, offsetof(c_orm_oauth2_token_t, created_at),
     false, false, NULL, false, false},
    {"user_id", C_ORM_TYPE_STRING, offsetof(c_orm_oauth2_token_t, user_id),
     false, true, "users", false, false},
    {"scopes", C_ORM_TYPE_STRING, offsetof(c_orm_oauth2_token_t, scopes), false,
     true, NULL, false, false}};

/**
 * @brief Token table metadata.
 */
C_ORM_EXPORT const c_orm_table_meta_t c_orm_token_meta = {
    "tokens",
    token_cols,
    7,
    sizeof(c_orm_oauth2_token_t),
    "SELECT * FROM tokens",
    "SELECT * FROM tokens WHERE access_token = ?",
    "INSERT INTO tokens (access_token, refresh_token, token_type, expires_in, "
    "created_at, user_id, scopes) VALUES (?, ?, ?, ?, ?, ?, ?)",
    "UPDATE tokens SET refresh_token = ?, token_type = ?, expires_in = ?, "
    "created_at = ?, user_id = ?, scopes = ? WHERE access_token = ?",
    "DELETE FROM tokens WHERE access_token = ?",
    "SELECT * FROM tokens WHERE access_token = ? FOR UPDATE",
    false,
    true,
    offsetof(c_orm_oauth2_token_t, created_at),
    offsetof(c_orm_oauth2_token_t, expires_in),
    {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
    NULL,
    0};

/**
 * @brief Client table columns.
 */
static const c_orm_column_meta_t client_cols[] = {
    {"id", C_ORM_TYPE_STRING, offsetof(c_orm_oauth2_client_t, id), true, false,
     NULL, false, false},
    {"client_secret", C_ORM_TYPE_STRING,
     offsetof(c_orm_oauth2_client_t, client_secret), false, true, NULL, false,
     true},
    {"redirect_uris", C_ORM_TYPE_STRING,
     offsetof(c_orm_oauth2_client_t, redirect_uris), false, true, NULL, false,
     false},
    {"grant_types", C_ORM_TYPE_STRING,
     offsetof(c_orm_oauth2_client_t, grant_types), false, true, NULL, false,
     false},
    {"scopes", C_ORM_TYPE_STRING, offsetof(c_orm_oauth2_client_t, scopes),
     false, true, NULL, false, false}};

/**
 * @brief Client table metadata.
 */
C_ORM_EXPORT const c_orm_table_meta_t c_orm_oauth2_client_meta = {
    "clients",
    client_cols,
    5,
    sizeof(c_orm_oauth2_client_t),
    "SELECT * FROM clients",
    "SELECT * FROM clients WHERE id = ?",
    "INSERT INTO clients (id, client_secret, redirect_uris, grant_types, "
    "scopes) VALUES (?, ?, ?, ?, ?)",
    "UPDATE clients SET client_secret = ?, redirect_uris = ?, grant_types = ?, "
    "scopes = ? WHERE id = ?",
    "DELETE FROM clients WHERE id = ?",
    "SELECT * FROM clients WHERE id = ? FOR UPDATE",
    false,
    false,
    0,
    0,
    {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
    NULL,
    0};

/**
 * @brief Auth code table columns.
 */
static const c_orm_column_meta_t auth_code_cols[] = {
    {"code", C_ORM_TYPE_STRING, offsetof(c_orm_oauth2_auth_code_t, code), true,
     false, NULL, false, true},
    {"client_id", C_ORM_TYPE_STRING,
     offsetof(c_orm_oauth2_auth_code_t, client_id), false, true, NULL, false,
     false},
    {"redirect_uri", C_ORM_TYPE_STRING,
     offsetof(c_orm_oauth2_auth_code_t, redirect_uri), false, true, NULL, false,
     false},
    {"user_id", C_ORM_TYPE_STRING, offsetof(c_orm_oauth2_auth_code_t, user_id),
     false, true, NULL, false, false},
    {"expires_at", C_ORM_TYPE_INT64,
     offsetof(c_orm_oauth2_auth_code_t, expires_at), false, false, NULL, false,
     false},
    {"scopes", C_ORM_TYPE_STRING, offsetof(c_orm_oauth2_auth_code_t, scopes),
     false, true, NULL, false, false}};

/**
 * @brief Auth code table metadata.
 */
C_ORM_EXPORT const c_orm_table_meta_t c_orm_auth_code_meta = {
    "auth_codes",
    auth_code_cols,
    6,
    sizeof(c_orm_oauth2_auth_code_t),
    "SELECT * FROM auth_codes",
    "SELECT * FROM auth_codes WHERE code = ?",
    "INSERT INTO auth_codes (code, client_id, redirect_uri, user_id, "
    "expires_at, scopes) VALUES (?, ?, ?, ?, ?, ?)",
    "UPDATE auth_codes SET client_id = ?, redirect_uri = ?, user_id = ?, "
    "expires_at = ?, scopes = ? WHERE code = ?",
    "DELETE FROM auth_codes WHERE code = ?",
    "SELECT * FROM auth_codes WHERE code = ? FOR UPDATE",
    false,
    false,
    0,
    0,
    {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
    NULL,
    0};

/**
 * @brief Verifies user credentials.
 * @param db The database connection.
 * @param username The username.
 * @param password The password.
 * @param out_is_valid Pointer to store the validity status.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_user_verify_credentials(c_orm_db_t *db,
                                                         const char *username,
                                                         const char *password,
                                                         int *out_is_valid) {
  c_orm_error_t rc;
  c_orm_user_t user;

  LOG_DEBUG("c_orm_user_verify_credentials: entered");

  if (!db || !username || !password || !out_is_valid) {
    LOG_DEBUG("c_orm_user_verify_credentials: validation error");
    return C_ORM_ERROR_VALIDATION;
  }

  *out_is_valid = 0;
  memset(&user, 0, sizeof(user));

  rc = c_orm_find_one_by_string(db, &c_orm_user_meta, "username", username,
                                &user);
  if (rc != C_ORM_OK) {
    if (rc == C_ORM_ERROR_NOT_FOUND) {
      LOG_DEBUG("c_orm_user_verify_credentials: user not found");
      return C_ORM_OK; /* No user found, but query succeeded */
    }
    LOG_DEBUG("c_orm_user_verify_credentials: query error");
    return rc;
  }

  if (user.password_hash) {
    if (strcmp(user.password_hash, password) == 0) {
      *out_is_valid = 1;
    }
  }

  if (user.id) {
    C_ORM_FREE(user.id);
  }
  if (user.username) {
    C_ORM_FREE(user.username);
  }
  if (user.password_hash) {
    C_ORM_FREE(user.password_hash);
  }
  if (user.salt) {
    C_ORM_FREE(user.salt);
  }

  LOG_DEBUG("c_orm_user_verify_credentials: exiting");
  return C_ORM_OK;
}

/**
 * @brief Verifies a client's credentials.
 * @param db The database connection.
 * @param client_id The client ID.
 * @param client_secret The client secret.
 * @param out_is_valid Pointer to store the validity status.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_oauth2_verify_client(c_orm_db_t *db,
                                                      const char *client_id,
                                                      const char *client_secret,
                                                      int *out_is_valid) {
  c_orm_error_t rc;
  c_orm_oauth2_client_t client;

  LOG_DEBUG("c_orm_oauth2_verify_client: entered");

  if (!db || !client_id || !out_is_valid) {
    LOG_DEBUG("c_orm_oauth2_verify_client: validation error");
    return C_ORM_ERROR_VALIDATION;
  }

  *out_is_valid = 0;
  memset(&client, 0, sizeof(client));

  rc = c_orm_find_by_id_string(db, &c_orm_oauth2_client_meta, client_id,
                               &client);
  if (rc != C_ORM_OK) {
    if (rc == C_ORM_ERROR_NOT_FOUND) {
      LOG_DEBUG("c_orm_oauth2_verify_client: client not found");
      return C_ORM_OK;
    }
    LOG_DEBUG("c_orm_oauth2_verify_client: query error");
    return rc;
  }

  if (client.client_secret && strlen(client.client_secret) > 0) {
    if (client_secret && strcmp(client.client_secret, client_secret) == 0) {
      *out_is_valid = 1;
    }
  } else {
    /* Public client (no secret required) */
    *out_is_valid = 1;
  }

  if (client.id) {
    C_ORM_FREE(client.id);
  }
  if (client.client_secret) {
    C_ORM_FREE(client.client_secret);
  }
  if (client.redirect_uris) {
    C_ORM_FREE(client.redirect_uris);
  }
  if (client.grant_types) {
    C_ORM_FREE(client.grant_types);
  }
  if (client.scopes) {
    C_ORM_FREE(client.scopes);
  }

  LOG_DEBUG("c_orm_oauth2_verify_client: exiting");
  return C_ORM_OK;
}

/**
 * @brief Saves an OAuth2 token to the database.
 * @param db The database connection.
 * @param token The token to save.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_oauth2_save_token(c_orm_db_t *db, const c_orm_oauth2_token_t *token) {
  c_orm_query_t *query;
  c_orm_error_t rc;
  int has_row;

  LOG_DEBUG("c_orm_oauth2_save_token: entered");

  if (!db || !token || !token->access_token) {
    LOG_DEBUG("c_orm_oauth2_save_token: validation error");
    return C_ORM_ERROR_VALIDATION;
  }

  rc = c_orm_prepare_cached(db, c_orm_token_meta.query_insert, &query);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_oauth2_save_token: prepare error");
    return rc;
  }

  rc = db->vtable->bind_string(query, 1, token->access_token);
  if (rc != C_ORM_OK)
    goto out;

  if (token->refresh_token) {
    rc = db->vtable->bind_string(query, 2, token->refresh_token);
    if (rc != C_ORM_OK)
      goto out;
  } else {
    rc = db->vtable->bind_null(query, 2);
    if (rc != C_ORM_OK)
      goto out;
  }

  if (token->token_type) {
    rc = db->vtable->bind_string(query, 3, token->token_type);
    if (rc != C_ORM_OK)
      goto out;
  } else {
    rc = db->vtable->bind_null(query, 3);
    if (rc != C_ORM_OK)
      goto out;
  }

  rc = db->vtable->bind_int32(query, 4, token->expires_in);
  if (rc != C_ORM_OK)
    goto out;

  rc = db->vtable->bind_int64(query, 5, token->created_at);
  if (rc != C_ORM_OK)
    goto out;

  if (token->user_id) {
    rc = db->vtable->bind_string(query, 6, token->user_id);
    if (rc != C_ORM_OK)
      goto out;
  } else {
    rc = db->vtable->bind_null(query, 6);
    if (rc != C_ORM_OK)
      goto out;
  }

  if (token->scopes) {
    rc = db->vtable->bind_string(query, 7, token->scopes);
    if (rc != C_ORM_OK)
      goto out;
  } else {
    rc = db->vtable->bind_null(query, 7);
    if (rc != C_ORM_OK)
      goto out;
  }

  rc = db->vtable->step(query, &has_row);
  if (rc != C_ORM_OK) {
    goto out;
  }

out: {
  c_orm_error_t _fin = c_orm_finalize_cached(db, query);
  if (_fin != C_ORM_OK) {
    return _fin;
  }
  if (rc != C_ORM_OK && rc != C_ORM_ERROR_NOT_FOUND) {
    return rc;
  }
}

  LOG_DEBUG("c_orm_oauth2_save_token: exiting");
  return C_ORM_OK;
}

/**
 * @brief Retrieves a token from the database.
 * @param db The database connection.
 * @param access_token The access token string.
 * @param out_token Pointer to store the retrieved token.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_oauth2_get_token(
    c_orm_db_t *db, const char *access_token, c_orm_oauth2_token_t *out_token) {
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_oauth2_get_token: entered");

  if (!db || !access_token || !out_token) {
    LOG_DEBUG("c_orm_oauth2_get_token: validation error");
    return C_ORM_ERROR_VALIDATION;
  }

  memset(out_token, 0, sizeof(*out_token));
  rc = c_orm_find_by_id_string(db, &c_orm_token_meta, access_token, out_token);
  printf("c_orm_oauth2_get_token: c_orm_find_by_id_string returned %d\n", rc);
  if (rc != C_ORM_OK) {
    return rc;
  }

  LOG_DEBUG("c_orm_oauth2_get_token: exiting");
  return C_ORM_OK;
}

/**
 * @brief Revokes an OAuth2 token.
 * @param db The database connection.
 * @param token_str The access token string to revoke.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_oauth2_revoke_token(c_orm_db_t *db,
                                                     const char *token_str) {
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_oauth2_revoke_token: entered");

  if (!db || !token_str) {
    LOG_DEBUG("c_orm_oauth2_revoke_token: validation error");
    return C_ORM_ERROR_VALIDATION;
  }

  rc = c_orm_delete_by_id_string(db, &c_orm_token_meta, token_str);
  if (rc != C_ORM_OK) {
    return rc;
  }

  LOG_DEBUG("c_orm_oauth2_revoke_token: exiting");
  return C_ORM_OK;
}

/**
 * @brief Validates if the requested scopes are within the granted scopes.
 * @param granted_scopes The granted scopes string.
 * @param requested_scopes The requested scopes string.
 * @param out_is_valid Pointer to store the validation result.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_oauth2_validate_scope(const char *granted_scopes,
                            const char *requested_scopes, int *out_is_valid) {
  LOG_DEBUG("c_orm_oauth2_validate_scope: entered");

  if (!out_is_valid) {
    LOG_DEBUG("c_orm_oauth2_validate_scope: validation error");
    return C_ORM_ERROR_VALIDATION;
  }

  *out_is_valid = 0;

  if (!granted_scopes) {
    LOG_DEBUG("c_orm_oauth2_validate_scope: exiting (no granted scopes)");
    return C_ORM_OK;
  }

  if (!requested_scopes || requested_scopes[0] == '\0') {
    *out_is_valid = 1;
    LOG_DEBUG("c_orm_oauth2_validate_scope: exiting (no requested scopes)");
    return C_ORM_OK;
  }

  {
    const char *p = requested_scopes;
    int valid = 1;

    while (*p) {
      /* Skip leading spaces */
      while (*p == ' ') {
        p++;
      }
      if (*p == '\0') {
        break;
      }

      /* Find end of token */
      {
        const char *start = p;
        size_t len = 0;
        char *tok;

        while (*p && *p != ' ') {
          len++;
          p++;
        }

        tok = (char *)C_ORM_MALLOC(len + 1);
        if (!tok) {
          LOG_DEBUG("c_orm_oauth2_validate_scope: OOM");
          return C_ORM_ERROR_VALIDATION;
        }

        memcpy(tok, start, len);
        tok[len] = '\0';

        if (!strstr(granted_scopes, tok)) {
          valid = 0;
          C_ORM_FREE(tok);
          break;
        }
        C_ORM_FREE(tok);
      }
    }
    *out_is_valid = valid;
  }

  LOG_DEBUG("c_orm_oauth2_validate_scope: exiting");
  return C_ORM_OK;
}

/**
 * @brief Saves an authorization code to the database.
 * @param db The database connection.
 * @param auth_code The authorization code to save.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_oauth2_save_auth_code(
    c_orm_db_t *db, const c_orm_oauth2_auth_code_t *auth_code) {
  c_orm_query_t *query;
  c_orm_error_t rc;
  int has_row;

  LOG_DEBUG("c_orm_oauth2_save_auth_code: entered");

  if (!db || !auth_code || !auth_code->code) {
    LOG_DEBUG("c_orm_oauth2_save_auth_code: validation error");
    return C_ORM_ERROR_VALIDATION;
  }

  rc = c_orm_prepare_cached(db, c_orm_auth_code_meta.query_insert, &query);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_oauth2_save_auth_code: prepare error");
    return rc;
  }

  rc = db->vtable->bind_string(query, 1, auth_code->code);
  if (rc != C_ORM_OK)
    goto out;

  if (auth_code->client_id) {
    rc = db->vtable->bind_string(query, 2, auth_code->client_id);
    if (rc != C_ORM_OK)
      goto out;
  } else {
    rc = db->vtable->bind_null(query, 2);
    if (rc != C_ORM_OK)
      goto out;
  }

  if (auth_code->redirect_uri) {
    rc = db->vtable->bind_string(query, 3, auth_code->redirect_uri);
    if (rc != C_ORM_OK)
      goto out;
  } else {
    rc = db->vtable->bind_null(query, 3);
    if (rc != C_ORM_OK)
      goto out;
  }

  if (auth_code->user_id) {
    rc = db->vtable->bind_string(query, 4, auth_code->user_id);
    if (rc != C_ORM_OK)
      goto out;
  } else {
    rc = db->vtable->bind_null(query, 4);
    if (rc != C_ORM_OK)
      goto out;
  }

  rc = db->vtable->bind_int64(query, 5, auth_code->expires_at);
  if (rc != C_ORM_OK)
    goto out;

  if (auth_code->scopes) {
    rc = db->vtable->bind_string(query, 6, auth_code->scopes);
    if (rc != C_ORM_OK)
      goto out;
  } else {
    rc = db->vtable->bind_null(query, 6);
    if (rc != C_ORM_OK)
      goto out;
  }

  rc = db->vtable->step(query, &has_row);
  if (rc != C_ORM_OK) {
    goto out;
  }

out: {
  c_orm_error_t _fin = c_orm_finalize_cached(db, query);
  if (_fin != C_ORM_OK) {
    return _fin;
  }
  if (rc != C_ORM_OK && rc != C_ORM_ERROR_NOT_FOUND) {
    return rc;
  }
}

  LOG_DEBUG("c_orm_oauth2_save_auth_code: exiting");
  return C_ORM_OK;
}

/**
 * @brief Consumes an authorization code from the database.
 * @param db The database connection.
 * @param code The authorization code string.
 * @param out_auth_code Pointer to store the consumed auth code data.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_oauth2_consume_auth_code(
    c_orm_db_t *db, const char *code, c_orm_oauth2_auth_code_t *out_auth_code) {
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_oauth2_consume_auth_code: entered");

  if (!db || !code || !out_auth_code) {
    LOG_DEBUG("c_orm_oauth2_consume_auth_code: validation error");
    return C_ORM_ERROR_VALIDATION;
  }

  rc = c_orm_transaction_begin(db);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_oauth2_consume_auth_code: begin transaction error");
    return rc;
  }

  memset(out_auth_code, 0, sizeof(*out_auth_code));
  rc = c_orm_find_by_id_string(db, &c_orm_auth_code_meta, code, out_auth_code);

  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_oauth2_consume_auth_code: auth code not found");
    {
      c_orm_error_t _rb = c_orm_transaction_rollback(db);
      printf("CONSUME RB: %d\n", _rb);
      if (_rb != C_ORM_OK)
        return _rb;
    }

    return rc;
  }

  rc = c_orm_delete_by_id_string(db, &c_orm_auth_code_meta, code);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_oauth2_consume_auth_code: delete error");
    {
      c_orm_error_t _rb = c_orm_transaction_rollback(db);
      printf("CONSUME RB: %d\n", _rb);
      if (_rb != C_ORM_OK)
        return _rb;
    }

    return rc;
  }

  rc = c_orm_transaction_commit(db);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_oauth2_consume_auth_code: commit failed");
    {
      c_orm_error_t _rb = c_orm_transaction_rollback(db);
      printf("CONSUME RB: %d\n", _rb);
      if (_rb != C_ORM_OK)
        return _rb;
    }

    return rc;
  }

  LOG_DEBUG("c_orm_oauth2_consume_auth_code: exiting");
  return C_ORM_OK;
}

/**
 * @brief Cleans up expired tokens from the database.
 * @param db The database connection.
 * @param current_time The current time timestamp.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_oauth2_cleanup_expired_tokens(c_orm_db_t *db, int64_t current_time) {
  c_orm_query_t *query;
  c_orm_error_t rc;

  int has_row;

  LOG_DEBUG("c_orm_oauth2_cleanup_expired_tokens: entered");

  if (!db) {
    LOG_DEBUG("c_orm_oauth2_cleanup_expired_tokens: validation error");
    return C_ORM_ERROR_VALIDATION;
  }

  rc = c_orm_prepare_cached(
      db, "DELETE FROM tokens WHERE created_at + expires_in < ?", &query);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_oauth2_cleanup_expired_tokens: prepare error");
    return rc;
  }

  rc = db->vtable->bind_int64(query, 1, current_time);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_oauth2_cleanup_expired_tokens: bind error");
    {
      c_orm_error_t tmp_rc = c_orm_finalize_cached(db, query);
      if (tmp_rc != C_ORM_OK)
        return tmp_rc;
      return rc;
    }
  }
  rc = db->vtable->step(query, &has_row);
  if (rc != C_ORM_OK && rc != C_ORM_ERROR_NOT_FOUND) {
    c_orm_error_t err = c_orm_finalize_cached(db, query);
    if (err != C_ORM_OK) {
      return err;
    }
    return rc;
  }
  {
    c_orm_error_t err = c_orm_finalize_cached(db, query);
    if (err != C_ORM_OK) {
      return err;
    }
  }

  LOG_DEBUG("c_orm_oauth2_cleanup_expired_tokens: exiting");
  return C_ORM_OK;
}

#if defined(__clang__) || defined(__GNUC__)
#endif
