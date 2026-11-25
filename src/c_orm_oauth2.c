/**
 * @file c_orm_oauth2.c
 * @brief Implementation of OAuth 2.0 and User schemas support.
 */

/* clang-format off */
#include "c_orm_safe_crt.h"
#include "c_orm_oauth2.h"
#include "c_orm_api.h"
#include "c_orm_uuid.h"
#include "c_orm_sqlite.h"
#include "c_orm_postgres.h"
#include "c_orm_mysql.h"
#include "c_orm_log.h"
#include "c_cdd/json_dom.h"
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
#elif !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
/* clang-format on */

/**
 * @brief Constant-time string equality check to prevent timing side-channels.
 * @param a First null-terminated string.
 * @param b Second null-terminated string.
 * @return 1 if equal, 0 otherwise.
 */
static int oauth2_calc_constant_time_eq(const char *a, const char *b) {
  size_t len_a = strlen(a);
  size_t len_b = strlen(b);
  size_t i;
  unsigned char diff = (unsigned char)(len_a != len_b);
  for (i = 0; i < len_a; ++i) {
    unsigned char cb = (i < len_b) ? (unsigned char)b[i] : 0U;
    diff |= (unsigned char)((unsigned char)a[i] ^ cb);
  }
  return (diff == 0U) ? 1 : 0;
}

/**
 * @brief Frees a `JSON_Value` tree and returns `C_ORM_OK`.
 * @param val Root JSON value to free (safe if NULL).
 * @return `C_ORM_OK`.
 */
static c_orm_error_t oauth2_json_value_free(JSON_Value *val) {
  int st = (int)cdd_json_value_free(val);
  return (c_orm_error_t)((int)C_ORM_OK + (st * 0));
}

/**
 * @brief Releases heap-allocated string fields on a parsed OAuth2 token.
 * @param tok Token struct whose string fields should be freed and cleared.
 * @return C_ORM_OK.
 */
static c_orm_error_t oauth2_token_strings_free(c_orm_oauth2_token_t *tok) {
  c_orm_system_free(tok->access_token);
  c_orm_system_free(tok->refresh_token);
  c_orm_system_free(tok->token_type);
  tok->access_token = NULL;
  tok->refresh_token = NULL;
  tok->token_type = NULL;
  return C_ORM_OK;
}

/**
 * @brief Reads an optional JSON numeric field as int32_t.
 * @param obj The JSON object.
 * @param key The key name to look up.
 * @param out_i32 Output pointer receiving the truncated integer when present.
 * @return 1 if present, 0 otherwise.
 */
static int oauth2_calc_copy_json_int32_field(const JSON_Object *obj,
                                             const char *key,
                                             int32_t *out_i32) {
  double val = 0.0;
  if (cdd_json_object_get_number(obj, key, &val) != CDD_C_SUCCESS) {
    return 0;
  }
  *out_i32 = (int32_t)val;
  return 1;
}

/**
 * @brief Duplicates a JSON string field into a newly allocated buffer.
 * @param obj The JSON object.
 * @param key The key name to look up.
 * @param out_str Output pointer receiving the duplicated string.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on OOM.
 */
static c_orm_error_t copy_json_string_field(const JSON_Object *obj,
                                            const char *key, char **out_str) {
  const char *val = NULL;
  if (cdd_json_object_get_string(obj, key, &val) != CDD_C_SUCCESS) {
    return C_ORM_OK;
  }
  {
    size_t len = strlen(val);
    char *copy = NULL;
    c_orm_error_t rc = c_orm_system_malloc(len + 1, (void **)&copy);
    if (rc != C_ORM_OK) {
      LOG_DEBUG("copy_json_string_field: OOM");
      return rc;
    }
    memcpy(copy, val, len + 1);
    *out_str = copy;
  }
  return C_ORM_OK;
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
 * @return C_ORM_OK on success, C_ORM_ERROR_PARSE on malformed JSON, or
 * C_ORM_ERROR_MEMORY on OOM.
 */
C_ORM_EXPORT c_orm_error_t c_orm_oauth2_token_parse_json(
    const char *json, c_orm_oauth2_token_t *out_token) {
  JSON_Value *root_val = NULL;
  JSON_Object *obj = NULL;
  cdd_c_error_t jrc;
  c_orm_error_t rc = C_ORM_OK;

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

  jrc = cdd_json_parse_string(json, &root_val);
  if (jrc != CDD_C_SUCCESS) {
    return (jrc == CDD_C_ERROR_MEMORY) ? C_ORM_ERROR_MEMORY : C_ORM_ERROR_PARSE;
  }

  if (root_val->type != JSONObject) {
    oauth2_json_value_free(root_val);
    return C_ORM_ERROR_PARSE;
  }
  obj = root_val->object_val;

  rc = copy_json_string_field(obj, "access_token", &out_token->access_token);
  if (rc != C_ORM_OK) {
    goto token_parse_err;
  }
  rc = copy_json_string_field(obj, "refresh_token", &out_token->refresh_token);
  if (rc != C_ORM_OK) {
    goto token_parse_err;
  }
  rc = copy_json_string_field(obj, "token_type", &out_token->token_type);
  if (rc != C_ORM_OK) {
    goto token_parse_err;
  }
  oauth2_calc_copy_json_int32_field(obj, "expires_in", &out_token->expires_in);

  oauth2_json_value_free(root_val);
  LOG_DEBUG("c_orm_oauth2_token_parse_json: exiting");
  return C_ORM_OK;

token_parse_err:;
  oauth2_token_strings_free(out_token);
  oauth2_json_value_free(root_val);
  return rc;
}

/**
 * @brief Streaming SHA-256 state (NIST FIPS 180-4).
 */
typedef struct c_orm_sha256_ctx {
  /** @brief Intermediate hash words H0..H7. */
  uint32_t state[8];
  /** @brief Total message bytes processed. */
  uint64_t total_bytes;
  /** @brief Partial 64-byte block buffer. */
  unsigned char buf[64];
  /** @brief Bytes currently buffered in `buf`. */
  size_t buf_len;
} c_orm_sha256_ctx_t;

#define C_ORM_ROTR32(x, n)                                                     \
  (((uint32_t)(x) >> (n)) | ((uint32_t)(x) << (32 - (n))))
#define C_ORM_ROTL32(x, n)                                                     \
  (((uint32_t)(x) << (n)) | ((uint32_t)(x) >> (32 - (n))))

/**
 * @brief Compresses a single 64-byte block into the SHA-256 state.
 * @param ctx SHA-256 context.
 * @param block 64-byte input block.
 * @return 1 on completion.
 */
static int oauth2_calc_sha256_compress(c_orm_sha256_ctx_t *ctx,
                                       const unsigned char block[64]) {
  static const uint32_t k[64] = {
      0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL, 0x3956c25bUL,
      0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL, 0xd807aa98UL, 0x12835b01UL,
      0x243185beUL, 0x550c7dc3UL, 0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL,
      0xc19bf174UL, 0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL,
      0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL, 0x983e5152UL,
      0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL, 0xc6e00bf3UL, 0xd5a79147UL,
      0x06ca6351UL, 0x14292967UL, 0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL,
      0x53380d13UL, 0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
      0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL, 0xd192e819UL,
      0xd6990624UL, 0xf40e3585UL, 0x106aa070UL, 0x19a4c116UL, 0x1e376c08UL,
      0x2748774cUL, 0x34b0bcb5UL, 0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL,
      0x682e6ff3UL, 0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL,
      0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL};
  uint32_t w[64];
  uint32_t a, b, c, d, e, f, g, h;
  size_t i;

  for (i = 0; i < 16; ++i) {
    w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) |
           ((uint32_t)block[i * 4 + 2] << 8) | (uint32_t)block[i * 4 + 3];
  }
  for (i = 16; i < 64; ++i) {
    uint32_t s0 = C_ORM_ROTR32(w[i - 15], 7) ^ C_ORM_ROTR32(w[i - 15], 18) ^
                  (w[i - 15] >> 3);
    uint32_t s1 = C_ORM_ROTR32(w[i - 2], 17) ^ C_ORM_ROTR32(w[i - 2], 19) ^
                  (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }

  a = ctx->state[0];
  b = ctx->state[1];
  c = ctx->state[2];
  d = ctx->state[3];
  e = ctx->state[4];
  f = ctx->state[5];
  g = ctx->state[6];
  h = ctx->state[7];

  for (i = 0; i < 64; ++i) {
    uint32_t s1 =
        C_ORM_ROTR32(e, 6) ^ C_ORM_ROTR32(e, 11) ^ C_ORM_ROTR32(e, 25);
    uint32_t ch = (e & f) ^ ((~e) & g);
    uint32_t temp1 = h + s1 + ch + k[i] + w[i];
    uint32_t s0 =
        C_ORM_ROTR32(a, 2) ^ C_ORM_ROTR32(a, 13) ^ C_ORM_ROTR32(a, 22);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t temp2 = s0 + maj;

    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }

  ctx->state[0] += a;
  ctx->state[1] += b;
  ctx->state[2] += c;
  ctx->state[3] += d;
  ctx->state[4] += e;
  ctx->state[5] += f;
  ctx->state[6] += g;
  ctx->state[7] += h;
  return 1;
}

/**
 * @brief Initializes a SHA-256 context.
 * @param ctx Context to initialize.
 * @return 1 on completion.
 */
static int oauth2_calc_sha256_init(c_orm_sha256_ctx_t *ctx) {
  ctx->state[0] = 0x6a09e667UL;
  ctx->state[1] = 0xbb67ae85UL;
  ctx->state[2] = 0x3c6ef372UL;
  ctx->state[3] = 0xa54ff53aUL;
  ctx->state[4] = 0x510e527fUL;
  ctx->state[5] = 0x9b05688cUL;
  ctx->state[6] = 0x1f83d9abUL;
  ctx->state[7] = 0x5be0cd19UL;
  ctx->total_bytes = 0;
  ctx->buf_len = 0;
  return 1;
}

/**
 * @brief Feeds input bytes into a SHA-256 context.
 * @param ctx SHA-256 context.
 * @param data Input byte pointer.
 * @param len Number of input bytes.
 * @return 1 on completion.
 */
static int oauth2_calc_sha256_update(c_orm_sha256_ctx_t *ctx,
                                     const unsigned char *data, size_t len) {
  size_t idx = 0;
  if (len == 0) {
    return 1;
  }
  ctx->total_bytes += (uint64_t)len;
  if (ctx->buf_len > 0) {
    size_t take = 64 - ctx->buf_len;
    if (take > len) {
      take = len;
    }
    memcpy(ctx->buf + ctx->buf_len, data, take);
    ctx->buf_len += take;
    idx += take;
    if (ctx->buf_len == 64) {
      oauth2_calc_sha256_compress(ctx, ctx->buf);
      ctx->buf_len = 0;
    }
  }
  while (idx + 64 <= len) {
    oauth2_calc_sha256_compress(ctx, data + idx);
    idx += 64;
  }
  if (idx < len) {
    ctx->buf_len = len - idx;
    memcpy(ctx->buf, data + idx, ctx->buf_len);
  }
  return 1;
}

/**
 * @brief Finalizes a SHA-256 digest.
 * @param ctx SHA-256 context.
 * @param out_digest 32-byte output digest buffer.
 * @return 1 on completion.
 */
static int oauth2_calc_sha256_final(c_orm_sha256_ctx_t *ctx,
                                    unsigned char out_digest[32]) {
  uint64_t bit_len = ctx->total_bytes * 8;
  size_t i;

  ctx->buf[ctx->buf_len++] = 0x80U;
  if (ctx->buf_len > 56) {
    while (ctx->buf_len < 64) {
      ctx->buf[ctx->buf_len++] = 0;
    }
    oauth2_calc_sha256_compress(ctx, ctx->buf);
    ctx->buf_len = 0;
  }
  while (ctx->buf_len < 56) {
    ctx->buf[ctx->buf_len++] = 0;
  }
  for (i = 0; i < 8; ++i) {
    ctx->buf[56 + i] = (unsigned char)((bit_len >> ((7 - i) * 8)) & 0xFFU);
  }
  oauth2_calc_sha256_compress(ctx, ctx->buf);
  for (i = 0; i < 8; ++i) {
    out_digest[i * 4] = (unsigned char)((ctx->state[i] >> 24) & 0xFFU);
    out_digest[i * 4 + 1] = (unsigned char)((ctx->state[i] >> 16) & 0xFFU);
    out_digest[i * 4 + 2] = (unsigned char)((ctx->state[i] >> 8) & 0xFFU);
    out_digest[i * 4 + 3] = (unsigned char)(ctx->state[i] & 0xFFU);
  }
  memset(ctx, 0, sizeof(*ctx));
  return 1;
}

/**
 * @brief Computes RFC 2104 HMAC-SHA256 over two concatenated buffers.
 * @param key Secret key bytes.
 * @param key_len Secret key length.
 * @param msg1 First message segment.
 * @param msg1_len First message segment length.
 * @param msg2 Optional second message segment.
 * @param msg2_len Second message segment length.
 * @param out_mac 32-byte MAC output buffer.
 * @return 1 on completion.
 */
static int oauth2_calc_hmac_sha256_2(const unsigned char *key, size_t key_len,
                                     const unsigned char *msg1, size_t msg1_len,
                                     const unsigned char *msg2, size_t msg2_len,
                                     unsigned char out_mac[32]) {
  unsigned char k_pad[64];
  unsigned char ipad[64];
  unsigned char opad[64];
  unsigned char inner[32];
  c_orm_sha256_ctx_t ctx;
  size_t i;

  memset(k_pad, 0, sizeof(k_pad));
  if (key_len > 64) {
    oauth2_calc_sha256_init(&ctx);
    oauth2_calc_sha256_update(&ctx, key, key_len);
    oauth2_calc_sha256_final(&ctx, k_pad);
  } else if (key_len > 0) {
    memcpy(k_pad, key, key_len);
  }

  for (i = 0; i < 64; ++i) {
    ipad[i] = (unsigned char)(k_pad[i] ^ 0x36U);
    opad[i] = (unsigned char)(k_pad[i] ^ 0x5cU);
  }

  oauth2_calc_sha256_init(&ctx);
  oauth2_calc_sha256_update(&ctx, ipad, 64);
  oauth2_calc_sha256_update(&ctx, msg1, msg1_len);
  if (msg2_len > 0) {
    oauth2_calc_sha256_update(&ctx, msg2, msg2_len);
  }
  oauth2_calc_sha256_final(&ctx, inner);

  oauth2_calc_sha256_init(&ctx);
  oauth2_calc_sha256_update(&ctx, opad, 64);
  oauth2_calc_sha256_update(&ctx, inner, 32);
  oauth2_calc_sha256_final(&ctx, out_mac);

  memset(k_pad, 0, sizeof(k_pad));
  memset(inner, 0, sizeof(inner));
  return 1;
}

/**
 * @brief Computes PBKDF2-HMAC-SHA256 (RFC 2898) into a 32-byte derived key.
 * @param password Password bytes.
 * @param password_len Password byte length.
 * @param salt Salt bytes.
 * @param salt_len Salt byte length.
 * @param iterations Iteration count.
 * @param out_dk 32-byte derived key output.
 * @return 1 on completion.
 */
static int oauth2_calc_pbkdf2_hmac_sha256(const unsigned char *password,
                                          size_t password_len,
                                          const unsigned char *salt,
                                          size_t salt_len,
                                          unsigned int iterations,
                                          unsigned char out_dk[32]) {
  unsigned char be_idx[4] = {0, 0, 0, 1};
  unsigned char u[32];
  unsigned char u_next[32];
  unsigned int iter;
  size_t k;

  oauth2_calc_hmac_sha256_2(password, password_len, salt, salt_len, be_idx, 4,
                            u);
  memcpy(out_dk, u, 32);

  for (iter = 1; iter < iterations; ++iter) {
    oauth2_calc_hmac_sha256_2(password, password_len, u, 32, NULL, 0, u_next);
    memcpy(u, u_next, 32);
    for (k = 0; k < 32; ++k) {
      out_dk[k] ^= u[k];
    }
  }
  memset(u, 0, sizeof(u));
  memset(u_next, 0, sizeof(u_next));
  return 1;
}

/**
 * @brief Derives a 64-char hex PBKDF2-HMAC-SHA256 digest on the stack.
 * @param password Plaintext password string.
 * @param salt Salt string (may be NULL).
 * @param out_hex 65-byte output buffer receiving lowercase hex digest.
 * @return 1 on completion.
 */
static int oauth2_calc_pbkdf2_password_hex(const char *password,
                                           const char *salt, char out_hex[65]) {
  static const char hex_chars[] = "0123456789abcdef";
  unsigned char dk[32];
  const char *s = salt ? salt : "";
  size_t i;

  oauth2_calc_pbkdf2_hmac_sha256((const unsigned char *)password,
                                 strlen(password), (const unsigned char *)s,
                                 strlen(s), 10000U, dk);
  for (i = 0; i < 32; ++i) {
    out_hex[i * 2] = hex_chars[(dk[i] >> 4) & 0x0FU];
    out_hex[i * 2 + 1] = hex_chars[dk[i] & 0x0FU];
  }
  out_hex[64] = '\0';
  memset(dk, 0, sizeof(dk));
  return 1;
}

C_ORM_EXPORT c_orm_error_t c_orm_user_hash_password(const char *password,
                                                    const char *salt,
                                                    char **out_hash) {
  char hex[65];
  char *buf = NULL;
  c_orm_error_t rc;

  if (!password || !out_hash) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  *out_hash = NULL;
  oauth2_calc_pbkdf2_password_hex(password, salt, hex);
  rc = c_orm_system_malloc(65, (void **)&buf);
  if (rc != C_ORM_OK) {
    return rc;
  }
  memcpy(buf, hex, 65);
  *out_hash = buf;
  return C_ORM_OK;
}

#define C_ORM_CHACHA_QR(a, b, c, d)                                            \
  do {                                                                         \
    (a) += (b);                                                                \
    (d) ^= (a);                                                                \
    (d) = C_ORM_ROTL32((d), 16);                                               \
    (c) += (d);                                                                \
    (b) ^= (c);                                                                \
    (b) = C_ORM_ROTL32((b), 12);                                               \
    (a) += (b);                                                                \
    (d) ^= (a);                                                                \
    (d) = C_ORM_ROTL32((d), 8);                                                \
    (c) += (d);                                                                \
    (b) ^= (c);                                                                \
    (b) = C_ORM_ROTL32((b), 7);                                                \
  } while (0)

/**
 * @brief Loads a little-endian 32-bit unsigned integer from 4 bytes.
 * @param p 4-byte input buffer.
 * @return Loaded 32-bit unsigned integer.
 */
static uint32_t oauth2_calc_load_le32(const unsigned char p[4]) {
  return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

/**
 * @brief Encrypts or decrypts bytes in-place/out-of-place with RFC 8439
 * ChaCha20 (20 rounds, counter starting at 1).
 * @param key 32-byte key.
 * @param nonce 12-byte nonce.
 * @param in_buf Input bytes.
 * @param out_buf Output bytes.
 * @param len Byte length.
 * @return 1 on completion.
 */
static int oauth2_calc_chacha20_xor(const unsigned char key[32],
                                    const unsigned char nonce[12],
                                    const unsigned char *in_buf,
                                    unsigned char *out_buf, size_t len) {
  uint32_t counter = 1U;
  size_t offset = 0;

  while (offset < len) {
    uint32_t st[16];
    uint32_t wk[16];
    unsigned char ks[64];
    size_t block_len = (len - offset > 64) ? 64 : (len - offset);
    size_t i;

    st[0] = 0x61707865UL;
    st[1] = 0x3320646eUL;
    st[2] = 0x79622d32UL;
    st[3] = 0x6b206574UL;
    for (i = 0; i < 8; ++i) {
      st[4 + i] = oauth2_calc_load_le32(key + i * 4);
    }
    st[12] = counter++;
    st[13] = oauth2_calc_load_le32(nonce);
    st[14] = oauth2_calc_load_le32(nonce + 4);
    st[15] = oauth2_calc_load_le32(nonce + 8);

    for (i = 0; i < 16; ++i) {
      wk[i] = st[i];
    }
    for (i = 0; i < 10; ++i) {
      C_ORM_CHACHA_QR(wk[0], wk[4], wk[8], wk[12]);
      C_ORM_CHACHA_QR(wk[1], wk[5], wk[9], wk[13]);
      C_ORM_CHACHA_QR(wk[2], wk[6], wk[10], wk[14]);
      C_ORM_CHACHA_QR(wk[3], wk[7], wk[11], wk[15]);
      C_ORM_CHACHA_QR(wk[0], wk[5], wk[10], wk[15]);
      C_ORM_CHACHA_QR(wk[1], wk[6], wk[11], wk[12]);
      C_ORM_CHACHA_QR(wk[2], wk[7], wk[8], wk[13]);
      C_ORM_CHACHA_QR(wk[3], wk[4], wk[9], wk[14]);
    }
    for (i = 0; i < 16; ++i) {
      uint32_t v = wk[i] + st[i];
      ks[i * 4] = (unsigned char)(v & 0xFFU);
      ks[i * 4 + 1] = (unsigned char)((v >> 8) & 0xFFU);
      ks[i * 4 + 2] = (unsigned char)((v >> 16) & 0xFFU);
      ks[i * 4 + 3] = (unsigned char)((v >> 24) & 0xFFU);
    }
    for (i = 0; i < block_len; ++i) {
      out_buf[offset + i] = (unsigned char)(in_buf[offset + i] ^ ks[i]);
    }
    memset(wk, 0, sizeof(wk));
    memset(ks, 0, sizeof(ks));
    offset += block_len;
  }
  return 1;
}

static unsigned char g_oauth2_master_key[32];
static int g_oauth2_master_key_ready = 0;

C_ORM_EXPORT c_orm_error_t
c_orm_oauth2_set_encryption_key(const unsigned char *key, size_t key_len) {
  if (!key && key_len == 0) {
    memset(g_oauth2_master_key, 0, sizeof(g_oauth2_master_key));
    g_oauth2_master_key_ready = 0;
    return C_ORM_OK;
  }
  if (!key || key_len != 32) {
    return C_ORM_ERROR_VALIDATION;
  }
  memcpy(g_oauth2_master_key, key, 32);
  g_oauth2_master_key_ready = 1;
  return C_ORM_OK;
}

/**
 * @brief Ensures the 256-bit OAuth2 master key is initialized from explicit
 * configuration, `C_ORM_OAUTH2_SECRET`, or OS CSPRNG.
 * @param out_enc_key 32-byte derived encryption subkey.
 * @param out_mac_key 32-byte derived MAC subkey.
 * @return C_ORM_OK on success, or C_ORM_ERROR_CRYPTO on CSPRNG failure.
 */
static c_orm_error_t c_orm_oauth2_get_subkeys(unsigned char out_enc_key[32],
                                              unsigned char out_mac_key[32]) {
  static const unsigned char enc_label[] = "c-orm-oauth2-aead-enc-v1";
  static const unsigned char mac_label[] = "c-orm-oauth2-aead-mac-v1";
  c_orm_error_t rc;

  if (!g_oauth2_master_key_ready) {
    char env_secret[256];
    env_secret[0] = '\0';
    rc = C_ORM_GETENV(env_secret, sizeof(env_secret), "C_ORM_OAUTH2_SECRET");
    if (rc != C_ORM_OK) {
      return rc;
    }
    if (env_secret[0] != '\0') {
      static const unsigned char kdf_salt[] = "c-orm-oauth2-master-salt";
      oauth2_calc_pbkdf2_hmac_sha256(
          (const unsigned char *)env_secret, strlen(env_secret), kdf_salt,
          sizeof(kdf_salt) - 1, 10000U, g_oauth2_master_key);
      memset(env_secret, 0, sizeof(env_secret));
    } else {
      rc = c_orm_random_bytes(g_oauth2_master_key, 32);
      if (rc != C_ORM_OK) {
        return rc;
      }
    }
    g_oauth2_master_key_ready = 1;
  }

  oauth2_calc_hmac_sha256_2(g_oauth2_master_key, 32, enc_label,
                            sizeof(enc_label) - 1, NULL, 0, out_enc_key);
  oauth2_calc_hmac_sha256_2(g_oauth2_master_key, 32, mac_label,
                            sizeof(mac_label) - 1, NULL, 0, out_mac_key);
  return C_ORM_OK;
}

/**
 * @brief Converts a hex character to its 4-bit value.
 * @param c Hex character (`0-9`, `a-f`, `A-F`).
 * @param out Output nibble value.
 * @return C_ORM_OK on valid hex digit, or C_ORM_ERROR_CRYPTO otherwise.
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
  return C_ORM_ERROR_CRYPTO;
}

/**
 * @brief Encrypts a plain text token using Windows DPAPI on MSVC or
 * ChaCha20-HMAC-SHA256 Encrypt-then-MAC AEAD with a random 96-bit OS CSPRNG
 * nonce.
 * @param plain_token The plain text token.
 * @param out_encrypted_token Pointer to store the hex-encoded encrypted token.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_oauth2_encrypt_token(
    const char *plain_token, char **out_encrypted_token) {
  LOG_DEBUG("c_orm_oauth2_encrypt_token: entered");

  if (!plain_token || !out_encrypted_token) {
    LOG_DEBUG("c_orm_oauth2_encrypt_token: validation error");
    return C_ORM_ERROR_VALIDATION;
  }
  *out_encrypted_token = NULL;

#if defined(_MSC_VER)
  {
    DATA_BLOB in_blob;
    DATA_BLOB out_blob;
    size_t i;
    char *hex_str;
    void *hCrypt32;

    in_blob.pbData = (unsigned char *)plain_token;
    in_blob.cbData = (unsigned long)strlen(plain_token);

    hCrypt32 = LoadLibraryA("crypt32.dll");
    if (hCrypt32) {
      CryptProtectData_t pCryptProtectData =
          (CryptProtectData_t)GetProcAddress(hCrypt32, "CryptProtectData");
      if (pCryptProtectData &&
          pCryptProtectData(&in_blob, L"c_orm_token", NULL, NULL, NULL,
                            C_ORM_CRYPTPROTECT_UI_FORBIDDEN, &out_blob)) {
        static const char hex_chars[] = "0123456789abcdef";
        c_orm_error_t mrc;
        hex_str = NULL;
        mrc = c_orm_system_malloc((out_blob.cbData * 2) + 1, (void **)&hex_str);
        if (mrc != C_ORM_OK) {
          LocalFree(out_blob.pbData);
          FreeLibrary(hCrypt32);
          return mrc;
        }
        for (i = 0; i < out_blob.cbData; ++i) {
          hex_str[i * 2] = hex_chars[(out_blob.pbData[i] >> 4) & 0x0FU];
          hex_str[i * 2 + 1] = hex_chars[out_blob.pbData[i] & 0x0FU];
        }
        hex_str[out_blob.cbData * 2] = '\0';
        LocalFree(out_blob.pbData);
        *out_encrypted_token = hex_str;
        FreeLibrary(hCrypt32);
        return C_ORM_OK;
      }
      FreeLibrary(hCrypt32);
    }
    return C_ORM_ERROR_CRYPTO;
  }
#else
  {
    static const char hex_chars[] = "0123456789abcdef";
    size_t pt_len = strlen(plain_token);
    size_t wire_len = 12 + pt_len + 32;
    unsigned char enc_key[32];
    unsigned char mac_key[32];
    unsigned char nonce[12];
    unsigned char tag[32];
    unsigned char *wire = NULL;
    char *hex_str = NULL;
    size_t i;
    c_orm_error_t rc;

    rc = c_orm_oauth2_get_subkeys(enc_key, mac_key);
    if (rc != C_ORM_OK) {
      return rc;
    }
    rc = c_orm_random_bytes(nonce, sizeof(nonce));
    if (rc != C_ORM_OK) {
      memset(enc_key, 0, sizeof(enc_key));
      memset(mac_key, 0, sizeof(mac_key));
      return rc;
    }

    rc = c_orm_system_malloc(wire_len, (void **)&wire);
    if (rc != C_ORM_OK) {
      memset(enc_key, 0, sizeof(enc_key));
      memset(mac_key, 0, sizeof(mac_key));
      return rc;
    }

    memcpy(wire, nonce, 12);
    oauth2_calc_chacha20_xor(enc_key, nonce, (const unsigned char *)plain_token,
                             wire + 12, pt_len);
    oauth2_calc_hmac_sha256_2(mac_key, 32, wire, 12 + pt_len, NULL, 0, tag);
    memcpy(wire + 12 + pt_len, tag, 32);
    memset(enc_key, 0, sizeof(enc_key));
    memset(mac_key, 0, sizeof(mac_key));

    rc = c_orm_system_malloc((wire_len * 2) + 1, (void **)&hex_str);
    if (rc != C_ORM_OK) {
      c_orm_system_free(wire);
      return rc;
    }
    for (i = 0; i < wire_len; ++i) {
      hex_str[i * 2] = hex_chars[(wire[i] >> 4) & 0x0FU];
      hex_str[i * 2 + 1] = hex_chars[wire[i] & 0x0FU];
    }
    hex_str[wire_len * 2] = '\0';
    c_orm_system_free(wire);
    *out_encrypted_token = hex_str;
    return C_ORM_OK;
  }
#endif
}

/**
 * @brief Decrypts and authenticates an encrypted token string.
 * @param encrypted_token Hex-encoded authenticated ciphertext.
 * @param out_plain_token Pointer to store the decrypted plaintext token.
 * @return C_ORM_OK on success, C_ORM_ERROR_VALIDATION on NULL,
 * C_ORM_ERROR_MEMORY on OOM, or C_ORM_ERROR_CRYPTO on malformed hex or
 * authentication tag failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_oauth2_decrypt_token(
    const char *encrypted_token, char **out_plain_token) {
  size_t hex_len;
  size_t bin_len;
  unsigned char *bin_data = NULL;
  size_t i;
  c_orm_error_t mrc;

  LOG_DEBUG("c_orm_oauth2_decrypt_token: entered");

  if (!encrypted_token || !out_plain_token) {
    LOG_DEBUG("c_orm_oauth2_decrypt_token: validation error");
    return C_ORM_ERROR_VALIDATION;
  }
  *out_plain_token = NULL;

  hex_len = strlen(encrypted_token);
  if (hex_len == 0 || (hex_len % 2) != 0) {
    return C_ORM_ERROR_CRYPTO;
  }
  bin_len = hex_len / 2;

#if !defined(_MSC_VER)
  if (bin_len < 44) {
    return C_ORM_ERROR_CRYPTO;
  }
#endif

  mrc = c_orm_system_malloc(bin_len, (void **)&bin_data);
  if (mrc != C_ORM_OK) {
    return mrc;
  }

  for (i = 0; i < bin_len; ++i) {
    unsigned char b1 = 0, b2 = 0;
    c_orm_error_t hrc = hex_to_byte(encrypted_token[i * 2], &b1);
    if (hrc != C_ORM_OK) {
      c_orm_system_free(bin_data);
      return hrc;
    }
    hrc = hex_to_byte(encrypted_token[i * 2 + 1], &b2);
    if (hrc != C_ORM_OK) {
      c_orm_system_free(bin_data);
      return hrc;
    }
    bin_data[i] = (unsigned char)((b1 << 4) | b2);
  }

#if defined(_MSC_VER)
  {
    DATA_BLOB in_blob;
    DATA_BLOB out_blob;
    void *hCrypt32;

    in_blob.pbData = bin_data;
    in_blob.cbData = (unsigned long)bin_len;

    hCrypt32 = LoadLibraryA("crypt32.dll");
    if (hCrypt32) {
      CryptUnprotectData_t pCryptUnprotectData =
          (CryptUnprotectData_t)GetProcAddress(hCrypt32, "CryptUnprotectData");
      if (pCryptUnprotectData &&
          pCryptUnprotectData(&in_blob, NULL, NULL, NULL, NULL,
                              C_ORM_CRYPTPROTECT_UI_FORBIDDEN, &out_blob)) {
        char *plain = NULL;
        mrc = c_orm_system_malloc(out_blob.cbData + 1, (void **)&plain);
        if (mrc != C_ORM_OK) {
          LocalFree(out_blob.pbData);
          FreeLibrary(hCrypt32);
          c_orm_system_free(bin_data);
          return mrc;
        }
        memcpy(plain, out_blob.pbData, out_blob.cbData);
        plain[out_blob.cbData] = '\0';
        LocalFree(out_blob.pbData);
        FreeLibrary(hCrypt32);
        c_orm_system_free(bin_data);
        *out_plain_token = plain;
        return C_ORM_OK;
      }
      FreeLibrary(hCrypt32);
    }
    c_orm_system_free(bin_data);
    return C_ORM_ERROR_CRYPTO;
  }
#else
  {
    size_t ct_len = bin_len - 44;
    unsigned char enc_key[32];
    unsigned char mac_key[32];
    unsigned char expected_tag[32];
    unsigned char diff = 0;
    char *plain = NULL;
    c_orm_error_t rc;

    rc = c_orm_oauth2_get_subkeys(enc_key, mac_key);
    if (rc != C_ORM_OK) {
      c_orm_system_free(bin_data);
      return rc;
    }

    oauth2_calc_hmac_sha256_2(mac_key, 32, bin_data, 12 + ct_len, NULL, 0,
                              expected_tag);
    memset(mac_key, 0, sizeof(mac_key));

    for (i = 0; i < 32; ++i) {
      diff |= (unsigned char)(bin_data[12 + ct_len + i] ^ expected_tag[i]);
    }
    memset(expected_tag, 0, sizeof(expected_tag));
    if (diff != 0) {
      memset(enc_key, 0, sizeof(enc_key));
      c_orm_system_free(bin_data);
      return C_ORM_ERROR_CRYPTO;
    }

    rc = c_orm_system_malloc(ct_len + 1, (void **)&plain);
    if (rc != C_ORM_OK) {
      memset(enc_key, 0, sizeof(enc_key));
      c_orm_system_free(bin_data);
      return rc;
    }

    oauth2_calc_chacha20_xor(enc_key, bin_data, bin_data + 12,
                             (unsigned char *)plain, ct_len);
    memset(enc_key, 0, sizeof(enc_key));
    c_orm_system_free(bin_data);
    plain[ct_len] = '\0';
    *out_plain_token = plain;
    return C_ORM_OK;
  }
#endif
}

C_ORM_EXPORT c_orm_error_t c_orm_store_token_secure_at(
    const c_orm_oauth2_token_t *token, const char *file_path) {
  char *encrypted_access = NULL;
  char *encrypted_refresh = NULL;
  FILE *f = NULL;
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_store_token_secure_at: entered");

  if (!token || !file_path || file_path[0] == '\0') {
    LOG_DEBUG("c_orm_store_token_secure_at: validation error");
    return C_ORM_ERROR_VALIDATION;
  }

  rc = c_orm_oauth2_encrypt_token(
      token->access_token ? token->access_token : "", &encrypted_access);
  if (rc != C_ORM_OK) {
    return rc;
  }

  rc = c_orm_oauth2_encrypt_token(
      token->refresh_token ? token->refresh_token : "", &encrypted_refresh);
  if (rc != C_ORM_OK) {
    c_orm_system_free(encrypted_access);
    return rc;
  }

  {
    int open_ok;
#if !defined(_WIN32) && !defined(_MSC_VER) && !defined(__EMSCRIPTEN__)
    mode_t old_mask = umask(0177);
    open_ok = (C_ORM_FOPEN(&f, file_path, "w") == C_ORM_OK);
    umask(old_mask);
    if (!open_ok) {
      c_orm_system_free(encrypted_access);
      c_orm_system_free(encrypted_refresh);
      return C_ORM_ERROR_IO;
    }
    chmod(file_path, 0600);
#else
    open_ok = (C_ORM_FOPEN(&f, file_path, "w") == C_ORM_OK);
    if (!open_ok) {
      c_orm_system_free(encrypted_access);
      c_orm_system_free(encrypted_refresh);
      return C_ORM_ERROR_IO;
    }
#endif
    fprintf(f, "%s\n%s\n", encrypted_access, encrypted_refresh);
    fclose(f);
  }

  c_orm_system_free(encrypted_access);
  c_orm_system_free(encrypted_refresh);
  return C_ORM_OK;
}

/**
 * @brief Stores an OAuth2 token securely on disk using `C_ORM_TOKEN_FILE` or
 * `"c_orm_token.dat"`.
 * @param token The token to store.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_store_token_secure(const c_orm_oauth2_token_t *token) {
  char path_buf[512];
  c_orm_error_t rc;
  if (!token) {
    return C_ORM_ERROR_VALIDATION;
  }
  path_buf[0] = '\0';
  rc = C_ORM_GETENV(path_buf, sizeof(path_buf), "C_ORM_TOKEN_FILE");
  if (rc != C_ORM_OK) {
    return rc;
  }
  return c_orm_store_token_secure_at(
      token, path_buf[0] != '\0' ? path_buf : "c_orm_token.dat");
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
 * @brief Compares `db->vtable` against `*vt_ptr` after
 * `c_orm_sqlite_get_vtable` completes at the call sequence point.
 * @param st Ignored status code evaluated prior to entering this helper.
 * @param db Validated non-null database handle.
 * @param vt_ptr Pointer holding the fetched SQLite vtable pointer.
 * @return 1 if `db->vtable == *vt_ptr`, 0 otherwise.
 */
static int oauth2_calc_eq_vtable(int st, const c_orm_db_t *db,
                                 const c_orm_driver_vtable_t *const *vt_ptr) {
  return (st * 0) + ((db->vtable == *vt_ptr) ? 1 : 0);
}

/**
 * @brief Checks whether `db` uses the SQLite driver vtable.
 * @param db Validated non-null database handle.
 * @return 1 if `db->vtable` matches SQLite vtable, 0 otherwise.
 */
static int oauth2_calc_is_sqlite_db(const c_orm_db_t *db) {
  const c_orm_driver_vtable_t *sqlite_vt = NULL;
  return oauth2_calc_eq_vtable((int)c_orm_sqlite_get_vtable(&sqlite_vt), db,
                               &sqlite_vt);
}

/**
 * @brief Creates necessary tables for OAuth2 support.
 * @param db The database connection.
 * @return C_ORM_OK on success, or an error code.
 */
C_ORM_EXPORT c_orm_error_t c_orm_oauth2_create_tables(c_orm_db_t *db) {
  c_orm_error_t rc;

  LOG_DEBUG("c_orm_oauth2_create_tables: entered");

  if (!db || !db->vtable) {
    LOG_DEBUG("c_orm_oauth2_create_tables: validation error");
    return C_ORM_ERROR_VALIDATION;
  }

  if (oauth2_calc_is_sqlite_db(db)) {
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
     false, false, 0, 0, 0, 0},
    {"username", C_ORM_TYPE_STRING, offsetof(c_orm_user_t, username), false,
     false, NULL, false, false, 0, 0, 0, 0},
    {"password_hash", C_ORM_TYPE_STRING, offsetof(c_orm_user_t, password_hash),
     false, true, NULL, false, true, 0, 0, 0, 0},
    {"salt", C_ORM_TYPE_STRING, offsetof(c_orm_user_t, salt), false, true, NULL,
     false, false, 0, 0, 0, 0}};

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
     true, 0, 0, 0, 0},
    {"refresh_token", C_ORM_TYPE_STRING,
     offsetof(c_orm_oauth2_token_t, refresh_token), false, true, NULL, false,
     true, 0, 0, 0, 0},
    {"token_type", C_ORM_TYPE_STRING,
     offsetof(c_orm_oauth2_token_t, token_type), false, true, NULL, false,
     false, 0, 0, 0, 0},
    {"expires_in", C_ORM_TYPE_INT32, offsetof(c_orm_oauth2_token_t, expires_in),
     false, false, NULL, false, false, 0, 0, 0, 0},
    {"created_at", C_ORM_TYPE_INT64, offsetof(c_orm_oauth2_token_t, created_at),
     false, false, NULL, false, false, 0, 0, 0, 0},
    {"user_id", C_ORM_TYPE_STRING, offsetof(c_orm_oauth2_token_t, user_id),
     false, true, "users", false, false, 0, 0, 0, 0},
    {"scopes", C_ORM_TYPE_STRING, offsetof(c_orm_oauth2_token_t, scopes), false,
     true, NULL, false, false, 0, 0, 0, 0}};

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
     NULL, false, false, 0, 0, 0, 0},
    {"client_secret", C_ORM_TYPE_STRING,
     offsetof(c_orm_oauth2_client_t, client_secret), false, true, NULL, false,
     true, 0, 0, 0, 0},
    {"redirect_uris", C_ORM_TYPE_STRING,
     offsetof(c_orm_oauth2_client_t, redirect_uris), false, true, NULL, false,
     false, 0, 0, 0, 0},
    {"grant_types", C_ORM_TYPE_STRING,
     offsetof(c_orm_oauth2_client_t, grant_types), false, true, NULL, false,
     false, 0, 0, 0, 0},
    {"scopes", C_ORM_TYPE_STRING, offsetof(c_orm_oauth2_client_t, scopes),
     false, true, NULL, false, false, 0, 0, 0, 0}};

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
     false, NULL, false, true, 0, 0, 0, 0},
    {"client_id", C_ORM_TYPE_STRING,
     offsetof(c_orm_oauth2_auth_code_t, client_id), false, true, NULL, false,
     false, 0, 0, 0, 0},
    {"redirect_uri", C_ORM_TYPE_STRING,
     offsetof(c_orm_oauth2_auth_code_t, redirect_uri), false, true, NULL, false,
     false, 0, 0, 0, 0},
    {"user_id", C_ORM_TYPE_STRING, offsetof(c_orm_oauth2_auth_code_t, user_id),
     false, true, NULL, false, false, 0, 0, 0, 0},
    {"expires_at", C_ORM_TYPE_INT64,
     offsetof(c_orm_oauth2_auth_code_t, expires_at), false, false, NULL, false,
     false, 0, 0, 0, 0},
    {"scopes", C_ORM_TYPE_STRING, offsetof(c_orm_oauth2_auth_code_t, scopes),
     false, true, NULL, false, false, 0, 0, 0, 0}};

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
 * @brief Releases heap-allocated string fields on a `c_orm_user_t`.
 * @param user User struct whose fields should be freed.
 * @return C_ORM_OK.
 */
static c_orm_error_t oauth2_user_strings_free(c_orm_user_t *user) {
  c_orm_system_free(user->id);
  c_orm_system_free(user->username);
  c_orm_system_free(user->password_hash);
  c_orm_system_free(user->salt);
  user->id = NULL;
  user->username = NULL;
  user->password_hash = NULL;
  user->salt = NULL;
  return C_ORM_OK;
}

/**
 * @brief Releases heap-allocated string fields on a `c_orm_oauth2_client_t`.
 * @param client Client struct whose fields should be freed.
 * @return C_ORM_OK.
 */
static c_orm_error_t oauth2_client_strings_free(c_orm_oauth2_client_t *client) {
  c_orm_system_free(client->id);
  c_orm_system_free(client->client_secret);
  c_orm_system_free(client->redirect_uris);
  c_orm_system_free(client->grant_types);
  c_orm_system_free(client->scopes);
  client->id = NULL;
  client->client_secret = NULL;
  client->redirect_uris = NULL;
  client->grant_types = NULL;
  client->scopes = NULL;
  return C_ORM_OK;
}

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
    char derived_hex[65];
    oauth2_calc_pbkdf2_password_hex(password, user.salt ? user.salt : "",
                                    derived_hex);
    *out_is_valid =
        oauth2_calc_constant_time_eq(user.password_hash, derived_hex);
    memset(derived_hex, 0, sizeof(derived_hex));
  }

  oauth2_user_strings_free(&user);
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
    if (client_secret) {
      *out_is_valid =
          oauth2_calc_constant_time_eq(client.client_secret, client_secret);
    }
  } else {
    /* Public client (no secret required) */
    *out_is_valid = 1;
  }

  oauth2_client_strings_free(&client);
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
  if (rc != C_ORM_OK) {
    goto save_token_err;
  }

  if (token->refresh_token) {
    rc = db->vtable->bind_string(query, 2, token->refresh_token);
    if (rc != C_ORM_OK) {
      goto save_token_err;
    }
  } else {
    rc = db->vtable->bind_null(query, 2);
    if (rc != C_ORM_OK) {
      goto save_token_err;
    }
  }

  if (token->token_type) {
    rc = db->vtable->bind_string(query, 3, token->token_type);
    if (rc != C_ORM_OK) {
      goto save_token_err;
    }
  } else {
    rc = db->vtable->bind_null(query, 3);
    if (rc != C_ORM_OK) {
      goto save_token_err;
    }
  }

  rc = db->vtable->bind_int32(query, 4, token->expires_in);
  if (rc != C_ORM_OK) {
    goto save_token_err;
  }
  rc = db->vtable->bind_int64(query, 5, token->created_at);
  if (rc != C_ORM_OK) {
    goto save_token_err;
  }

  if (token->user_id) {
    rc = db->vtable->bind_string(query, 6, token->user_id);
    if (rc != C_ORM_OK) {
      goto save_token_err;
    }
  } else {
    rc = db->vtable->bind_null(query, 6);
    if (rc != C_ORM_OK) {
      goto save_token_err;
    }
  }

  if (token->scopes) {
    rc = db->vtable->bind_string(query, 7, token->scopes);
    if (rc != C_ORM_OK) {
      goto save_token_err;
    }
  } else {
    rc = db->vtable->bind_null(query, 7);
    if (rc != C_ORM_OK) {
      goto save_token_err;
    }
  }

  rc = db->vtable->step(query, &has_row);
  if (rc != C_ORM_OK) {
    if (rc != C_ORM_ERROR_NOT_FOUND) {
      goto save_token_err;
    }
  }

  rc = c_orm_finalize_cached(db, query);
  if (rc != C_ORM_OK) {
    return rc;
  }

  LOG_DEBUG("c_orm_oauth2_save_token: exiting");
  return C_ORM_OK;

save_token_err:;
  {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK) {
      return fin_rc;
    }
  }
  return rc;
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
      const char *req_start;
      size_t req_len = 0;
      const char *g;
      int matched = 0;

      while (*p == ' ' || *p == '\t') {
        p++;
      }
      if (*p == '\0') {
        break;
      }

      req_start = p;
      while (*p && *p != ' ' && *p != '\t') {
        req_len++;
        p++;
      }

      g = granted_scopes;
      while (*g) {
        const char *g_start;
        size_t g_len = 0;
        while (*g == ' ' || *g == '\t') {
          g++;
        }
        if (*g == '\0') {
          break;
        }
        g_start = g;
        while (*g && *g != ' ' && *g != '\t') {
          g_len++;
          g++;
        }
        if (g_len == req_len && memcmp(g_start, req_start, req_len) == 0) {
          matched = 1;
          break;
        }
      }

      if (!matched) {
        valid = 0;
        break;
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
  if (rc != C_ORM_OK) {
    goto save_code_err;
  }

  if (auth_code->client_id) {
    rc = db->vtable->bind_string(query, 2, auth_code->client_id);
    if (rc != C_ORM_OK) {
      goto save_code_err;
    }
  } else {
    rc = db->vtable->bind_null(query, 2);
    if (rc != C_ORM_OK) {
      goto save_code_err;
    }
  }

  if (auth_code->redirect_uri) {
    rc = db->vtable->bind_string(query, 3, auth_code->redirect_uri);
    if (rc != C_ORM_OK) {
      goto save_code_err;
    }
  } else {
    rc = db->vtable->bind_null(query, 3);
    if (rc != C_ORM_OK) {
      goto save_code_err;
    }
  }

  if (auth_code->user_id) {
    rc = db->vtable->bind_string(query, 4, auth_code->user_id);
    if (rc != C_ORM_OK) {
      goto save_code_err;
    }
  } else {
    rc = db->vtable->bind_null(query, 4);
    if (rc != C_ORM_OK) {
      goto save_code_err;
    }
  }

  rc = db->vtable->bind_int64(query, 5, auth_code->expires_at);
  if (rc != C_ORM_OK) {
    goto save_code_err;
  }

  if (auth_code->scopes) {
    rc = db->vtable->bind_string(query, 6, auth_code->scopes);
    if (rc != C_ORM_OK) {
      goto save_code_err;
    }
  } else {
    rc = db->vtable->bind_null(query, 6);
    if (rc != C_ORM_OK) {
      goto save_code_err;
    }
  }

  rc = db->vtable->step(query, &has_row);
  if (rc != C_ORM_OK) {
    if (rc != C_ORM_ERROR_NOT_FOUND) {
      goto save_code_err;
    }
  }

  rc = c_orm_finalize_cached(db, query);
  if (rc != C_ORM_OK) {
    return rc;
  }

  LOG_DEBUG("c_orm_oauth2_save_auth_code: exiting");
  return C_ORM_OK;

save_code_err:;
  {
    c_orm_error_t fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK) {
      return fin_rc;
    }
  }
  return rc;
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
  c_orm_error_t rb_rc;

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
    rb_rc = c_orm_transaction_rollback(db);
    if (rb_rc != C_ORM_OK)
      return rb_rc;
    return rc;
  }

  rc = c_orm_delete_by_id_string(db, &c_orm_auth_code_meta, code);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_oauth2_consume_auth_code: delete error");
    rb_rc = c_orm_transaction_rollback(db);
    if (rb_rc != C_ORM_OK)
      return rb_rc;
    return rc;
  }

  rc = c_orm_transaction_commit(db);
  if (rc != C_ORM_OK) {
    LOG_DEBUG("c_orm_oauth2_consume_auth_code: commit failed");
    rb_rc = c_orm_transaction_rollback(db);
    if (rb_rc != C_ORM_OK)
      return rb_rc;
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
  c_orm_error_t fin_rc;

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
    fin_rc = c_orm_finalize_cached(db, query);
    if (fin_rc != C_ORM_OK) {
      return fin_rc;
    }
    return rc;
  }
  rc = db->vtable->step(query, &has_row);
  if (rc != C_ORM_OK) {
    if (rc != C_ORM_ERROR_NOT_FOUND) {
      fin_rc = c_orm_finalize_cached(db, query);
      if (fin_rc != C_ORM_OK) {
        return fin_rc;
      }
      return rc;
    }
  }
  fin_rc = c_orm_finalize_cached(db, query);
  if (fin_rc != C_ORM_OK) {
    return fin_rc;
  }

  LOG_DEBUG("c_orm_oauth2_cleanup_expired_tokens: exiting");
  return C_ORM_OK;
}
