/**
 * @file c_orm_oauth2.c
 * @brief Implementation of OAuth 2.0 and User schemas support.
 */

/* clang-format off */
#include "c_orm_oauth2.h"
#include "c_orm_api.h"
#include "c_orm_sqlite.h"
#include "c_orm_postgres.h"
#include "c_orm_mysql.h"
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#if defined(_MSC_VER)
#pragma comment(lib, "Crypt32.lib")

typedef struct _CRYPTOAPI_BLOB {
  unsigned long cbData;
  unsigned char *pbData;
} DATA_BLOB;

__declspec(dllimport) int __stdcall CryptProtectData(
  DATA_BLOB *pDataIn,
  const wchar_t *szDataDescr,
  DATA_BLOB *pOptionalEntropy,
  void *pvReserved,
  void *pPromptStruct,
  unsigned long dwFlags,
  DATA_BLOB *pDataOut
);

__declspec(dllimport) int __stdcall CryptUnprotectData(
  DATA_BLOB *pDataIn,
  const wchar_t **ppszDataDescr,
  DATA_BLOB *pOptionalEntropy,
  void *pvReserved,
  void *pPromptStruct,
  unsigned long dwFlags,
  DATA_BLOB *pDataOut
);

__declspec(dllimport) void * __stdcall LocalFree(void *hMem);

#define C_ORM_CRYPTPROTECT_UI_FORBIDDEN 0x1
#endif
/* clang-format on */

typedef struct {
  const char *key;
  char **str_out;
  int32_t *int_out;
} json_field_t;

static void parse_flat_json(const char *json, json_field_t *fields,
                            size_t num_fields) {
  const char *p = json;
  int in_string = 0;
  int escaping = 0;
  char current_key[256];
  size_t key_len = 0;
  int expecting_val = 0;

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
            if (!*p)
              break;
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
              if (!*p)
                break;
            }
            val_len++;
            p++;
          }
          val_str = (char *)malloc(val_len + 1);
          if (val_str) {
            const char *v = val_start;
            size_t i = 0;
            while (v < p) {
              if (*v == '\\')
                v++;
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
            if (val_str)
              free(val_str);
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
        while (*p && (*p == '-' || (*p >= '0' && *p <= '9')))
          p++;
        expecting_val = 0;
        current_key[0] = '\0';
        continue; /* already advanced p */
      }
    }
    p++;
  }
}

C_ORM_EXPORT c_orm_error_t
c_orm_oauth2_is_token_valid(const c_orm_oauth2_token_t *token,
                            int64_t current_time, int *out_is_valid) {
  if (!token || !out_is_valid) {
    return C_ORM_ERROR_MEMORY;
  }

  if ((token->created_at + token->expires_in) > current_time) {
    *out_is_valid = 1;
  } else {
    *out_is_valid = 0;
  }

  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_oauth2_token_parse_json(
    const char *json, c_orm_oauth2_token_t *out_token) {
  json_field_t fields[4];
  if (!json || !out_token) {
    return C_ORM_ERROR_MEMORY;
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

  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_oauth2_encrypt_token(
    const char *plain_token, char **out_encrypted_token) {
  if (!plain_token || !out_encrypted_token)
    return C_ORM_ERROR_MEMORY;

#if defined(_MSC_VER)
  {
    DATA_BLOB in_blob;
    DATA_BLOB out_blob;
    size_t i;
    char *hex_str;

    in_blob.pbData = (unsigned char *)plain_token;
    in_blob.cbData = (unsigned long)strlen(plain_token);

    if (CryptProtectData(&in_blob, L"c_orm_token", NULL, NULL, NULL,
                         C_ORM_CRYPTPROTECT_UI_FORBIDDEN, &out_blob)) {
      hex_str = (char *)malloc((out_blob.cbData * 2) + 1);
      if (!hex_str) {
        LocalFree(out_blob.pbData);
        return C_ORM_ERROR_MEMORY;
      }
      for (i = 0; i < out_blob.cbData; ++i) {
#if defined(_MSC_VER)
        sprintf_s(&hex_str[i * 2], 3, "%02x", out_blob.pbData[i]);
#else
        sprintf(&hex_str[i * 2], "%02x", out_blob.pbData[i]);
#endif
      }
      hex_str[out_blob.cbData * 2] = '\0';
      LocalFree(out_blob.pbData);
      *out_encrypted_token = hex_str;
      return C_ORM_OK;
    }
    return C_ORM_ERROR_UNKNOWN;
  }
#else
  {
    size_t len = strlen(plain_token);
    char *hex_str = (char *)malloc((len * 2) + 1);
    size_t i;
    if (!hex_str)
      return C_ORM_ERROR_MEMORY;
    for (i = 0; i < len; ++i) {
      unsigned char c = (unsigned char)(plain_token[i] ^ 0x42);
      sprintf(&hex_str[i * 2], "%02x", c);
    }
    hex_str[len * 2] = '\0';
    *out_encrypted_token = hex_str;
    return C_ORM_OK;
  }
#endif
}

static unsigned char hex_to_byte(char c) {
  if (c >= '0' && c <= '9')
    return (unsigned char)(c - '0');
  if (c >= 'a' && c <= 'f')
    return (unsigned char)(c - 'a' + 10);
  if (c >= 'A' && c <= 'F')
    return (unsigned char)(c - 'A' + 10);
  return 0;
}

C_ORM_EXPORT c_orm_error_t c_orm_oauth2_decrypt_token(
    const char *encrypted_token, char **out_plain_token) {
  if (!encrypted_token || !out_plain_token)
    return C_ORM_ERROR_MEMORY;

#if defined(_MSC_VER)
  {
    DATA_BLOB in_blob;
    DATA_BLOB out_blob;
    size_t hex_len = strlen(encrypted_token);
    size_t bin_len = hex_len / 2;
    unsigned char *bin_data;
    size_t i;

    bin_data = (unsigned char *)malloc(bin_len);
    if (!bin_data)
      return C_ORM_ERROR_MEMORY;

    for (i = 0; i < bin_len; ++i) {
      bin_data[i] = (unsigned char)((hex_to_byte(encrypted_token[i * 2]) << 4) |
                                    hex_to_byte(encrypted_token[i * 2 + 1]));
    }

    in_blob.pbData = bin_data;
    in_blob.cbData = (unsigned long)bin_len;

    if (CryptUnprotectData(&in_blob, NULL, NULL, NULL, NULL,
                           C_ORM_CRYPTPROTECT_UI_FORBIDDEN, &out_blob)) {
      char *plain = (char *)malloc(out_blob.cbData + 1);
      if (!plain) {
        LocalFree(out_blob.pbData);
        free(bin_data);
        return C_ORM_ERROR_MEMORY;
      }
      memcpy(plain, out_blob.pbData, out_blob.cbData);
      plain[out_blob.cbData] = '\0';
      LocalFree(out_blob.pbData);
      free(bin_data);
      *out_plain_token = plain;
      return C_ORM_OK;
    }
    free(bin_data);
    return C_ORM_ERROR_UNKNOWN;
  }
#else
  {
    size_t hex_len = strlen(encrypted_token);
    size_t bin_len = hex_len / 2;
    char *plain = (char *)malloc(bin_len + 1);
    size_t i;
    if (!plain)
      return C_ORM_ERROR_MEMORY;
    for (i = 0; i < bin_len; ++i) {
      unsigned char c =
          (unsigned char)((hex_to_byte(encrypted_token[i * 2]) << 4) |
                          hex_to_byte(encrypted_token[i * 2 + 1]));
      plain[i] = (char)(c ^ 0x42);
    }
    plain[bin_len] = '\0';
    *out_plain_token = plain;
    return C_ORM_OK;
  }
#endif
}

C_ORM_EXPORT c_orm_error_t
c_orm_store_token_secure(const c_orm_oauth2_token_t *token) {
  char *encrypted_access = NULL;
  char *encrypted_refresh = NULL;
  c_orm_error_t err;
  FILE *f;

  if (!token)
    return C_ORM_ERROR_MEMORY;

  err = c_orm_oauth2_encrypt_token(
      token->access_token ? token->access_token : "", &encrypted_access);
  if (err != C_ORM_OK)
    return err;

  err = c_orm_oauth2_encrypt_token(
      token->refresh_token ? token->refresh_token : "", &encrypted_refresh);
  if (err != C_ORM_OK) {
    free(encrypted_access);
    return err;
  }

#if defined(_MSC_VER)
  if (fopen_s(&f, "c_orm_token.dat", "w") != 0)
    f = NULL;
#else
  f = fopen("c_orm_token.dat", "w");
#endif

  if (!f) {
    free(encrypted_access);
    free(encrypted_refresh);
    return C_ORM_ERROR_UNKNOWN;
  }

  fprintf(f, "%s\n%s\n", encrypted_access, encrypted_refresh);
  fclose(f);

  free(encrypted_access);
  free(encrypted_refresh);

  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t
c_orm_oauth2_get_current_timestamp(int64_t *out_timestamp) {
  if (!out_timestamp)
    return C_ORM_ERROR_MEMORY;
  *out_timestamp = (int64_t)time(NULL);
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_oauth2_calculate_expiration(
    int64_t current_timestamp, int32_t expires_in, int64_t *out_expiration) {
  if (!out_expiration)
    return C_ORM_ERROR_MEMORY;
  *out_expiration = current_timestamp + (int64_t)expires_in;
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_oauth2_create_tables(c_orm_db_t *db) {
  const c_orm_driver_vtable_t *sqlite_vt = NULL;
  const c_orm_driver_vtable_t *pg_vt = NULL;
  const c_orm_driver_vtable_t *my_vt = NULL;
  c_orm_error_t err;

  if (!db)
    return C_ORM_ERROR_MEMORY;

  c_orm_sqlite_get_vtable(&sqlite_vt);
  c_orm_postgres_get_vtable(&pg_vt);
  c_orm_mysql_get_vtable(&my_vt);

  if (db->vtable == sqlite_vt) {
    err = c_orm_execute_raw(db, "CREATE TABLE IF NOT EXISTS users ("
                                "id TEXT PRIMARY KEY, "
                                "username TEXT UNIQUE, "
                                "password_hash TEXT, "
                                "salt TEXT);");
    if (err != C_ORM_OK)
      return err;
    err = c_orm_execute_raw(db, "CREATE TABLE IF NOT EXISTS tokens ("
                                "access_token TEXT PRIMARY KEY, "
                                "refresh_token TEXT, "
                                "token_type TEXT, "
                                "expires_in INTEGER, "
                                "created_at INTEGER, "
                                "user_id TEXT, "
                                "scopes TEXT, "
                                "FOREIGN KEY(user_id) REFERENCES users(id));");
    if (err != C_ORM_OK)
      return err;
    err = c_orm_execute_raw(db, "CREATE TABLE IF NOT EXISTS clients ("
                                "id TEXT PRIMARY KEY, "
                                "client_secret TEXT, "
                                "redirect_uris TEXT, "
                                "scopes TEXT, "
                                "grant_types TEXT);");
    if (err != C_ORM_OK)
      return err;
    err = c_orm_execute_raw(db, "CREATE TABLE IF NOT EXISTS auth_codes ("
                                "code TEXT PRIMARY KEY, "
                                "client_id TEXT, "
                                "redirect_uri TEXT, "
                                "user_id TEXT, "
                                "expires_at INTEGER, "
                                "scopes TEXT);");
    return err;
  } else if (db->vtable == pg_vt || db->vtable == my_vt) {
    err = c_orm_execute_raw(db, "CREATE TABLE IF NOT EXISTS users ("
                                "id VARCHAR(255) PRIMARY KEY, "
                                "username VARCHAR(255) UNIQUE, "
                                "password_hash VARCHAR(255), "
                                "salt VARCHAR(255));");
    if (err != C_ORM_OK)
      return err;
    err = c_orm_execute_raw(db, "CREATE TABLE IF NOT EXISTS tokens ("
                                "access_token VARCHAR(255) PRIMARY KEY, "
                                "refresh_token VARCHAR(255), "
                                "token_type VARCHAR(255), "
                                "expires_in INT, "
                                "created_at BIGINT, "
                                "user_id VARCHAR(255), "
                                "scopes VARCHAR(255), "
                                "FOREIGN KEY(user_id) REFERENCES users(id));");
    if (err != C_ORM_OK)
      return err;
    err = c_orm_execute_raw(db, "CREATE TABLE IF NOT EXISTS clients ("
                                "id VARCHAR(255) PRIMARY KEY, "
                                "client_secret VARCHAR(255), "
                                "redirect_uris VARCHAR(255), "
                                "scopes VARCHAR(255), "
                                "grant_types VARCHAR(255));");
    if (err != C_ORM_OK)
      return err;
    err = c_orm_execute_raw(db, "CREATE TABLE IF NOT EXISTS auth_codes ("
                                "code VARCHAR(255) PRIMARY KEY, "
                                "client_id VARCHAR(255), "
                                "redirect_uri VARCHAR(255), "
                                "user_id VARCHAR(255), "
                                "expires_at BIGINT, "
                                "scopes VARCHAR(255));");
    return err;
  }

  return C_ORM_ERROR_NOT_IMPLEMENTED;
}

static const c_orm_column_meta_t user_cols[] = {
    {"id", C_ORM_TYPE_STRING, offsetof(c_orm_user_t, id), true, false, NULL,
     false, false},
    {"username", C_ORM_TYPE_STRING, offsetof(c_orm_user_t, username), false,
     false, NULL, false, false},
    {"password_hash", C_ORM_TYPE_STRING, offsetof(c_orm_user_t, password_hash),
     false, true, NULL, false, true},
    {"salt", C_ORM_TYPE_STRING, offsetof(c_orm_user_t, salt), false, true, NULL,
     false, false}};
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
    0};

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

static const c_orm_column_meta_t client_cols[] = {
    {"id", C_ORM_TYPE_STRING, offsetof(c_orm_oauth2_client_t, id), true, false,
     NULL, false, false},
    {"client_secret", C_ORM_TYPE_STRING,
     offsetof(c_orm_oauth2_client_t, client_secret), false, false, NULL, false,
     true},
    {"redirect_uris", C_ORM_TYPE_STRING,
     offsetof(c_orm_oauth2_client_t, redirect_uris), false, true, NULL, false,
     false},
    {"grant_types", C_ORM_TYPE_STRING,
     offsetof(c_orm_oauth2_client_t, grant_types), false, true, NULL, false,
     false},
    {"scopes", C_ORM_TYPE_STRING, offsetof(c_orm_oauth2_client_t, scopes),
     false, true, NULL, false, false}};
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
    0,
    0};

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
    0,
    0};

C_ORM_EXPORT c_orm_error_t c_orm_user_verify_credentials(c_orm_db_t *db,
                                                         const char *username,
                                                         const char *password,
                                                         int *out_is_valid) {
  c_orm_error_t err;
  c_orm_user_t user;

  if (!db || !username || !password || !out_is_valid)
    return C_ORM_ERROR_MEMORY;

  *out_is_valid = 0;
  memset(&user, 0, sizeof(user));

  err = c_orm_find_one_by_string(db, &c_orm_user_meta, "username", username,
                                 &user);
  if (err != C_ORM_OK) {
    if (err == C_ORM_ERROR_NOT_FOUND) {
      return C_ORM_OK; /* No user found, but query succeeded */
    }
    return err;
  }

  if (user.password_hash) {
    if (strcmp(user.password_hash, password) == 0) {
      *out_is_valid = 1;
    }
  }

  if (user.id)
    free(user.id);
  if (user.username)
    free(user.username);
  if (user.password_hash)
    free(user.password_hash);
  if (user.salt)
    free(user.salt);

  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_oauth2_verify_client(c_orm_db_t *db,
                                                      const char *client_id,
                                                      const char *client_secret,
                                                      int *out_is_valid) {
  c_orm_error_t err;
  c_orm_oauth2_client_t client;

  if (!db || !client_id || !out_is_valid)
    return C_ORM_ERROR_MEMORY;

  *out_is_valid = 0;
  memset(&client, 0, sizeof(client));

  err = c_orm_find_by_id_string(db, &c_orm_oauth2_client_meta, client_id,
                                &client);
  if (err != C_ORM_OK) {
    if (err == C_ORM_ERROR_NOT_FOUND)
      return C_ORM_OK;
    return err;
  }

  if (client.client_secret && strlen(client.client_secret) > 0) {
    if (client_secret && strcmp(client.client_secret, client_secret) == 0) {
      *out_is_valid = 1;
    }
  } else {
    /* Public client (no secret required) */
    *out_is_valid = 1;
  }

  if (client.id)
    free(client.id);
  if (client.client_secret)
    free(client.client_secret);
  if (client.redirect_uris)
    free(client.redirect_uris);
  if (client.grant_types)
    free(client.grant_types);
  if (client.scopes)
    free(client.scopes);

  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t
c_orm_oauth2_save_token(c_orm_db_t *db, const c_orm_oauth2_token_t *token) {
  c_orm_query_t *query;
  c_orm_error_t err;
  int has_row;

  if (!db || !token || !token->access_token)
    return C_ORM_ERROR_MEMORY;

  err = db->vtable->prepare(db, c_orm_token_meta.query_insert, &query);
  if (err != C_ORM_OK)
    return err;

  db->vtable->bind_string(query, 1, token->access_token);
  if (token->refresh_token)
    db->vtable->bind_string(query, 2, token->refresh_token);
  else
    db->vtable->bind_null(query, 2);
  if (token->token_type)
    db->vtable->bind_string(query, 3, token->token_type);
  else
    db->vtable->bind_null(query, 3);
  db->vtable->bind_int32(query, 4, token->expires_in);
  db->vtable->bind_int64(query, 5, token->created_at);
  if (token->user_id)
    db->vtable->bind_string(query, 6, token->user_id);
  else
    db->vtable->bind_null(query, 6);
  if (token->scopes)
    db->vtable->bind_string(query, 7, token->scopes);
  else
    db->vtable->bind_null(query, 7);

  err = db->vtable->step(query, &has_row);
  db->vtable->finalize(query);

  if (err == C_ORM_ERROR_NOT_FOUND)
    return C_ORM_OK;
  return err;
}

C_ORM_EXPORT c_orm_error_t c_orm_oauth2_get_token(
    c_orm_db_t *db, const char *access_token, c_orm_oauth2_token_t *out_token) {
  if (!db || !access_token || !out_token)
    return C_ORM_ERROR_MEMORY;
  memset(out_token, 0, sizeof(*out_token));
  return c_orm_find_by_id_string(db, &c_orm_token_meta, access_token,
                                 out_token);
}

C_ORM_EXPORT c_orm_error_t c_orm_oauth2_revoke_token(c_orm_db_t *db,
                                                     const char *token_str) {
  if (!db || !token_str)
    return C_ORM_ERROR_MEMORY;
  return c_orm_delete_by_id_string(db, &c_orm_token_meta, token_str);
}

C_ORM_EXPORT c_orm_error_t
c_orm_oauth2_validate_scope(const char *granted_scopes,
                            const char *requested_scopes, int *out_is_valid) {
  if (!out_is_valid)
    return C_ORM_ERROR_MEMORY;
  *out_is_valid = 0;

  if (!granted_scopes)
    return C_ORM_OK;
  if (!requested_scopes || requested_scopes[0] == '\0') {
    *out_is_valid = 1;
    return C_ORM_OK;
  }

  {
    const char *p = requested_scopes;
    int valid = 1;

    while (*p) {
      /* Skip leading spaces */
      while (*p == ' ')
        p++;
      if (*p == '\0')
        break;

      /* Find end of token */
      {
        const char *start = p;
        size_t len = 0;
        char *tok;
        while (*p && *p != ' ') {
          len++;
          p++;
        }

        tok = (char *)malloc(len + 1);
        if (!tok)
          return C_ORM_ERROR_MEMORY;
        memcpy(tok, start, len);
        tok[len] = '\0';

        if (!strstr(granted_scopes, tok)) {
          valid = 0;
          free(tok);
          break;
        }
        free(tok);
      }
    }
    *out_is_valid = valid;
  }

  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_oauth2_save_auth_code(
    c_orm_db_t *db, const c_orm_oauth2_auth_code_t *auth_code) {
  c_orm_query_t *query;
  c_orm_error_t err;
  int has_row;

  if (!db || !auth_code || !auth_code->code)
    return C_ORM_ERROR_MEMORY;

  err = db->vtable->prepare(db, c_orm_auth_code_meta.query_insert, &query);
  if (err != C_ORM_OK)
    return err;

  db->vtable->bind_string(query, 1, auth_code->code);
  if (auth_code->client_id)
    db->vtable->bind_string(query, 2, auth_code->client_id);
  else
    db->vtable->bind_null(query, 2);
  if (auth_code->redirect_uri)
    db->vtable->bind_string(query, 3, auth_code->redirect_uri);
  else
    db->vtable->bind_null(query, 3);
  if (auth_code->user_id)
    db->vtable->bind_string(query, 4, auth_code->user_id);
  else
    db->vtable->bind_null(query, 4);
  db->vtable->bind_int64(query, 5, auth_code->expires_at);
  if (auth_code->scopes)
    db->vtable->bind_string(query, 6, auth_code->scopes);
  else
    db->vtable->bind_null(query, 6);

  err = db->vtable->step(query, &has_row);
  db->vtable->finalize(query);

  if (err == C_ORM_ERROR_NOT_FOUND)
    return C_ORM_OK;
  return err;
}

C_ORM_EXPORT c_orm_error_t c_orm_oauth2_consume_auth_code(
    c_orm_db_t *db, const char *code, c_orm_oauth2_auth_code_t *out_auth_code) {
  c_orm_error_t err;
  if (!db || !code || !out_auth_code)
    return C_ORM_ERROR_MEMORY;

  err = c_orm_transaction_begin(db);
  if (err != C_ORM_OK)
    return err;

  memset(out_auth_code, 0, sizeof(*out_auth_code));
  err = c_orm_find_by_id_string(db, &c_orm_auth_code_meta, code, out_auth_code);

  if (err != C_ORM_OK) {
    c_orm_transaction_rollback(db);
    return err;
  }

  err = c_orm_delete_by_id_string(db, &c_orm_auth_code_meta, code);
  if (err != C_ORM_OK) {
    c_orm_transaction_rollback(db);
    return err;
  }

  c_orm_transaction_commit(db);
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t
c_orm_oauth2_cleanup_expired_tokens(c_orm_db_t *db, int64_t current_time) {
  c_orm_query_t *query;
  c_orm_error_t err;
  int has_row;

  if (!db)
    return C_ORM_ERROR_MEMORY;

  err = db->vtable->prepare(
      db, "DELETE FROM tokens WHERE created_at + expires_in < ?", &query);
  if (err != C_ORM_OK)
    return err;

  db->vtable->bind_int64(query, 1, current_time);
  err = db->vtable->step(query, &has_row);
  db->vtable->finalize(query);

  if (err == C_ORM_ERROR_NOT_FOUND)
    return C_ORM_OK;
  return err;
}
