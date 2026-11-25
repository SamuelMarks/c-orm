/**
 * @file c_orm_ebcdic.c
 * @brief Pure C89 EBCDIC codepage tables (IBM037, IBM1047, IBM500, IBM1140),
 * COBOL fixed-width string padding, and UTF-8 <-> UTF-16 GRAPHIC transcoders.
 */

/* clang-format off */
#include "c_orm_ebcdic.h"
#include "c_orm_log.h"
#include <string.h>
/* clang-format on */

/**
 * @brief IBM CCSID 37 (US/Canada z/OS MVS & Db2) EBCDIC-to-Unicode (ISO-8859-1)
 * bijection table.
 */
static const unsigned short g_ibm037_to_uni[256] = {
    0x00, 0x01, 0x02, 0x03, 0x9C, 0x09, 0x86, 0x7F, 0x97, 0x8D, 0x8E, 0x0B,
    0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x9D, 0x85, 0x08, 0x87,
    0x18, 0x19, 0x92, 0x8F, 0x1C, 0x1D, 0x1E, 0x1F, 0x80, 0x81, 0x82, 0x83,
    0x84, 0x0A, 0x17, 0x1B, 0x88, 0x89, 0x8A, 0x8B, 0x8C, 0x05, 0x06, 0x07,
    0x90, 0x91, 0x16, 0x93, 0x94, 0x95, 0x96, 0x04, 0x98, 0x99, 0x9A, 0x9B,
    0x14, 0x15, 0x9E, 0x1A, 0x20, 0xA0, 0xE2, 0xE4, 0xE0, 0xE1, 0xE3, 0xE5,
    0xE7, 0xF1, 0xA2, 0x2E, 0x3C, 0x28, 0x2B, 0x7C, 0x26, 0xE9, 0xEA, 0xEB,
    0xE8, 0xED, 0xEE, 0xEF, 0xEC, 0xDF, 0x21, 0x24, 0x2A, 0x29, 0x3B, 0xAC,
    0x2D, 0x2F, 0xC2, 0xC4, 0xC0, 0xC1, 0xC3, 0xC5, 0xC7, 0xD1, 0xA6, 0x2C,
    0x25, 0x5F, 0x3E, 0x3F, 0xF8, 0xC9, 0xCA, 0xCB, 0xC8, 0xCD, 0xCE, 0xCF,
    0xCC, 0x60, 0x3A, 0x23, 0x40, 0x27, 0x3D, 0x22, 0xD8, 0x61, 0x62, 0x63,
    0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0xAB, 0xBB, 0xF0, 0xFD, 0xFE, 0xB1,
    0xB0, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F, 0x70, 0x71, 0x72, 0xAA, 0xBA,
    0xE6, 0xB8, 0xC6, 0xA4, 0xB5, 0x7E, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
    0x79, 0x7A, 0xA1, 0xBF, 0xD0, 0xDD, 0xDE, 0xAE, 0x5E, 0xA3, 0xA5, 0xB7,
    0xA9, 0xA7, 0xB6, 0xBC, 0xBD, 0xBE, 0x5B, 0x5D, 0xAF, 0xA8, 0xB4, 0xD7,
    0x7B, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0xAD, 0xF4,
    0xF6, 0xF2, 0xF3, 0xF5, 0x7D, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F, 0x50,
    0x51, 0x52, 0xB9, 0xFB, 0xFC, 0xF9, 0xFA, 0xFF, 0x5C, 0xF7, 0x53, 0x54,
    0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0xB2, 0xD4, 0xD6, 0xD2, 0xD3, 0xD5,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0xB3, 0xDB,
    0xDC, 0xD9, 0xDA, 0x9F};

/**
 * @brief Computes the Unicode scalar value for a byte in a validated EBCDIC
 * codepage (`IBM037`, `IBM1047`, `IBM500`, or `IBM1140`).
 * @param cp Validated EBCDIC codepage enum.
 * @param eb Byte to decode.
 * @return Mapped Unicode scalar value.
 */
static unsigned int ebcdic_calc_map_uni(c_orm_codepage_t cp, unsigned char eb) {
  unsigned int u = g_ibm037_to_uni[eb];
  if (cp == C_ORM_CP_IBM1047) {
    switch (eb) {
    case 0x5FU:
      return 0x5EU;
    case 0xADU:
      return 0x5BU;
    case 0xB0U:
      return 0xACU;
    case 0xBAU:
      return 0xDDU;
    case 0xBBU:
      return 0xA8U;
    case 0xBDU:
      return 0x5DU;
    default:
      return u;
    }
  }
  if (cp == C_ORM_CP_IBM500) {
    switch (eb) {
    case 0x4AU:
      return 0x5BU;
    case 0x4FU:
      return 0x21U;
    case 0x5AU:
      return 0x5DU;
    case 0x5FU:
      return 0x5EU;
    case 0xB0U:
      return 0xA2U;
    case 0xBAU:
      return 0xACU;
    case 0xBBU:
      return 0x7CU;
    default:
      return u;
    }
  }
  if (cp == C_ORM_CP_IBM1140 && eb == 0x9FU) {
    return 0x20ACU;
  }
  return u;
}

/**
 * @brief Looks up the Unicode codepoint for a single EBCDIC/ASCII byte.
 * @param cp Single-byte codepage enum.
 * @param eb Byte to decode.
 * @param out_cp_uni Output pointer receiving Unicode scalar value.
 * @return C_ORM_OK on success, or C_ORM_ERROR_CODEPAGE if cp is invalid.
 */
static c_orm_error_t ebcdic_byte_to_uni(c_orm_codepage_t cp, unsigned char eb,
                                        unsigned int *out_cp_uni) {
  if (cp == C_ORM_CP_IBM037 || cp == C_ORM_CP_IBM1047 ||
      cp == C_ORM_CP_IBM500 || cp == C_ORM_CP_IBM1140) {
    *out_cp_uni = ebcdic_calc_map_uni(cp, eb);
    return C_ORM_OK;
  }
  if (cp == C_ORM_CP_ASCII) {
    *out_cp_uni = (unsigned int)eb;
    return C_ORM_OK;
  }
  return C_ORM_ERROR_CODEPAGE;
}

/**
 * @brief Maps a Unicode scalar value into a single byte in target codepage cp.
 * @param cp Target single-byte codepage.
 * @param uni Unicode scalar codepoint.
 * @param out_eb Output pointer receiving encoded byte.
 * @return C_ORM_OK on success, or C_ORM_ERROR_CODEPAGE if unmappable.
 */
static c_orm_error_t uni_to_ebcdic_byte(c_orm_codepage_t cp, unsigned int uni,
                                        unsigned char *out_eb) {
  unsigned int i;
  c_orm_error_t rc;
  if (cp == C_ORM_CP_ASCII) {
    if (uni > 0xFFU) {
      return C_ORM_ERROR_CODEPAGE;
    }
    *out_eb = (unsigned char)uni;
    return C_ORM_OK;
  }
  if (cp == C_ORM_CP_IBM1140 && uni == 0x20ACU) {
    *out_eb = 0x9FU;
    return C_ORM_OK;
  }
  if (uni > 0xFFU || (cp == C_ORM_CP_IBM1140 && uni == 0xA4U)) {
    return C_ORM_ERROR_CODEPAGE;
  }
  for (i = 0; i < 255U; ++i) {
    unsigned int mapped = 0;
    rc = ebcdic_byte_to_uni(cp, (unsigned char)i, &mapped);
    if (rc != C_ORM_OK) {
      return rc;
    }
    if (mapped == uni) {
      *out_eb = (unsigned char)i;
      return C_ORM_OK;
    }
  }
  *out_eb = 255U;
  return C_ORM_OK;
}

/**
 * @brief Decodes a single UTF-8 codepoint starting at src[*io_idx].
 * @param src UTF-8 byte sequence.
 * @param len Total length of src.
 * @param io_idx In/out byte cursor advanced past the decoded sequence.
 * @param out_cp Output pointer receiving Unicode scalar value (0..0x10FFFF).
 * @return C_ORM_OK on success, or C_ORM_ERROR_CODEPAGE on malformed UTF-8.
 */
static c_orm_error_t utf8_next_codepoint(const unsigned char *src, size_t len,
                                         size_t *io_idx, unsigned int *out_cp) {
  size_t i = *io_idx;
  unsigned char b0 = src[i];
  if (b0 < 0x80U) {
    *io_idx = i + 1U;
    *out_cp = (unsigned int)b0;
    return C_ORM_OK;
  }
  if ((b0 & 0xE0U) == 0xC0U) {
    unsigned int cp;
    if (i + 1U >= len || (src[i + 1U] & 0xC0U) != 0x80U) {
      return C_ORM_ERROR_CODEPAGE;
    }
    cp =
        (((unsigned int)b0 & 0x1FU) << 6) | ((unsigned int)src[i + 1U] & 0x3FU);
    if (cp < 0x80U) {
      return C_ORM_ERROR_CODEPAGE;
    }
    *io_idx = i + 2U;
    *out_cp = cp;
    return C_ORM_OK;
  }
  if ((b0 & 0xF0U) == 0xE0U) {
    unsigned int cp;
    if (i + 2U >= len || (src[i + 1U] & 0xC0U) != 0x80U ||
        (src[i + 2U] & 0xC0U) != 0x80U) {
      return C_ORM_ERROR_CODEPAGE;
    }
    cp = (((unsigned int)b0 & 0x0FU) << 12) |
         (((unsigned int)src[i + 1U] & 0x3FU) << 6) |
         ((unsigned int)src[i + 2U] & 0x3FU);
    if (cp < 0x800U || (cp >= 0xD800U && cp <= 0xDFFFU)) {
      return C_ORM_ERROR_CODEPAGE;
    }
    *io_idx = i + 3U;
    *out_cp = cp;
    return C_ORM_OK;
  }
  if ((b0 & 0xF8U) == 0xF0U) {
    unsigned int cp;
    if (i + 3U >= len || (src[i + 1U] & 0xC0U) != 0x80U ||
        (src[i + 2U] & 0xC0U) != 0x80U || (src[i + 3U] & 0xC0U) != 0x80U) {
      return C_ORM_ERROR_CODEPAGE;
    }
    cp = (((unsigned int)b0 & 0x07U) << 18) |
         (((unsigned int)src[i + 1U] & 0x3FU) << 12) |
         (((unsigned int)src[i + 2U] & 0x3FU) << 6) |
         ((unsigned int)src[i + 3U] & 0x3FU);
    if (cp < 0x10000U || cp > 0x10FFFFU) {
      return C_ORM_ERROR_CODEPAGE;
    }
    *io_idx = i + 4U;
    *out_cp = cp;
    return C_ORM_OK;
  }
  return C_ORM_ERROR_CODEPAGE;
}

/**
 * @brief Appends a Unicode scalar value (0..0x10FFFF) encoded as UTF-8 into
 * out_buf.
 * @param cp Unicode scalar value (already validated non-surrogate <= 0x10FFFF).
 * @param out_buf Destination UTF-8 buffer.
 * @param cap Capacity of out_buf in bytes (reserving 1 byte for NUL).
 * @param io_pos In/out write offset.
 * @return C_ORM_OK on success, or C_ORM_ERROR_OVERFLOW if buffer is full.
 */
static c_orm_error_t utf8_emit_codepoint(unsigned int cp, char *out_buf,
                                         size_t cap, size_t *io_pos) {
  size_t pos = *io_pos;
  if (cp < 0x80U) {
    if (pos + 1U >= cap) {
      return C_ORM_ERROR_OVERFLOW;
    }
    out_buf[pos++] = (char)cp;
  } else if (cp < 0x800U) {
    if (pos + 2U >= cap) {
      return C_ORM_ERROR_OVERFLOW;
    }
    out_buf[pos++] = (char)(0xC0U | (cp >> 6));
    out_buf[pos++] = (char)(0x80U | (cp & 0x3FU));
  } else if (cp < 0x10000U) {
    if (pos + 3U >= cap) {
      return C_ORM_ERROR_OVERFLOW;
    }
    out_buf[pos++] = (char)(0xE0U | (cp >> 12));
    out_buf[pos++] = (char)(0x80U | ((cp >> 6) & 0x3FU));
    out_buf[pos++] = (char)(0x80U | (cp & 0x3FU));
  } else {
    if (pos + 4U >= cap) {
      return C_ORM_ERROR_OVERFLOW;
    }
    out_buf[pos++] = (char)(0xF0U | (cp >> 18));
    out_buf[pos++] = (char)(0x80U | ((cp >> 12) & 0x3FU));
    out_buf[pos++] = (char)(0x80U | ((cp >> 6) & 0x3FU));
    out_buf[pos++] = (char)(0x80U | (cp & 0x3FU));
  }
  *io_pos = pos;
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_codepage_from_ccsid(unsigned short ccsid,
                                                     c_orm_codepage_t *out_cp) {
  if (!out_cp) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  switch (ccsid) {
  case 0U:
  case 1208U:
    *out_cp = C_ORM_CP_UTF8;
    return C_ORM_OK;
  case 367U:
  case 437U:
  case 819U:
    *out_cp = C_ORM_CP_ASCII;
    return C_ORM_OK;
  case 37U:
    *out_cp = C_ORM_CP_IBM037;
    return C_ORM_OK;
  case 1047U:
    *out_cp = C_ORM_CP_IBM1047;
    return C_ORM_OK;
  case 500U:
    *out_cp = C_ORM_CP_IBM500;
    return C_ORM_OK;
  case 1140U:
    *out_cp = C_ORM_CP_IBM1140;
    return C_ORM_OK;
  default:
    return C_ORM_ERROR_CODEPAGE;
  }
}

C_ORM_EXPORT c_orm_error_t c_orm_codepage_to_ccsid(c_orm_codepage_t cp,
                                                   unsigned short *out_ccsid) {
  if (!out_ccsid) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  switch (cp) {
  case C_ORM_CP_UTF8:
    *out_ccsid = 1208U;
    return C_ORM_OK;
  case C_ORM_CP_ASCII:
    *out_ccsid = 819U;
    return C_ORM_OK;
  case C_ORM_CP_IBM037:
    *out_ccsid = 37U;
    return C_ORM_OK;
  case C_ORM_CP_IBM1047:
    *out_ccsid = 1047U;
    return C_ORM_OK;
  case C_ORM_CP_IBM500:
    *out_ccsid = 500U;
    return C_ORM_OK;
  case C_ORM_CP_IBM1140:
    *out_ccsid = 1140U;
    return C_ORM_OK;
  default:
    return C_ORM_ERROR_CODEPAGE;
  }
}

C_ORM_EXPORT c_orm_error_t c_orm_ebcdic_to_utf8(c_orm_codepage_t cp,
                                                const unsigned char *src,
                                                size_t src_len, char *out_utf8,
                                                size_t out_size,
                                                size_t *out_written) {
  c_orm_error_t rc;
  size_t pos = 0;
  size_t i = 0;

  if (!out_utf8) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (!src && src_len > 0) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (out_size == 0) {
    return C_ORM_ERROR_OVERFLOW;
  }

  if (cp == C_ORM_CP_UTF8) {
    while (i < src_len) {
      unsigned int ucp = 0;
      rc = utf8_next_codepoint(src, src_len, &i, &ucp);
      if (rc != C_ORM_OK) {
        return rc;
      }
      rc = utf8_emit_codepoint(ucp, out_utf8, out_size, &pos);
      if (rc != C_ORM_OK) {
        return rc;
      }
    }
  } else {
    if (src_len == 0) {
      unsigned short dummy_ccsid = 0;
      rc = c_orm_codepage_to_ccsid(cp, &dummy_ccsid);
      if (rc != C_ORM_OK) {
        return rc;
      }
    }
    for (i = 0; i < src_len; ++i) {
      unsigned int ucp = 0;
      rc = ebcdic_byte_to_uni(cp, src[i], &ucp);
      if (rc != C_ORM_OK) {
        return rc;
      }
      rc = utf8_emit_codepoint(ucp, out_utf8, out_size, &pos);
      if (rc != C_ORM_OK) {
        return rc;
      }
    }
  }

  out_utf8[pos] = '\0';
  if (out_written) {
    *out_written = pos;
  }
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_utf8_to_ebcdic(
    c_orm_codepage_t cp, const char *src_utf8, size_t src_len,
    unsigned char *out_ebcdic, size_t out_size, size_t *out_written) {
  c_orm_error_t rc;
  size_t i = 0;
  size_t pos = 0;
  const unsigned char *u8 = (const unsigned char *)src_utf8;

  if (src_len > 0 && (!src_utf8 || !out_ebcdic)) {
    return C_ORM_ERROR_NULL_POINTER;
  }

  if (cp == C_ORM_CP_UTF8) {
    while (i < src_len) {
      size_t prev = i;
      unsigned int ucp = 0;
      size_t seq_len;
      rc = utf8_next_codepoint(u8, src_len, &i, &ucp);
      if (rc != C_ORM_OK) {
        return rc;
      }
      seq_len = i - prev;
      if (pos + seq_len > out_size) {
        return C_ORM_ERROR_OVERFLOW;
      }
      memcpy(out_ebcdic + pos, u8 + prev, seq_len);
      pos += seq_len;
    }
  } else {
    if (src_len == 0) {
      unsigned short dummy_ccsid = 0;
      rc = c_orm_codepage_to_ccsid(cp, &dummy_ccsid);
      if (rc != C_ORM_OK) {
        return rc;
      }
    }
    while (i < src_len) {
      unsigned int ucp = 0;
      unsigned char eb = 0;
      rc = utf8_next_codepoint(u8, src_len, &i, &ucp);
      if (rc != C_ORM_OK) {
        return rc;
      }
      rc = uni_to_ebcdic_byte(cp, ucp, &eb);
      if (rc != C_ORM_OK) {
        return rc;
      }
      if (pos >= out_size) {
        return C_ORM_ERROR_OVERFLOW;
      }
      out_ebcdic[pos++] = eb;
    }
  }

  if (out_written) {
    *out_written = pos;
  }
  return C_ORM_OK;
}

/**
 * @brief Converts a pre-validated EBCDIC byte to single-byte ASCII.
 * @param cp Pre-validated EBCDIC codepage.
 * @param eb Pre-validated EBCDIC byte.
 * @return Converted ASCII byte.
 */
static unsigned char ebcdic_calc_byte_to_ascii_valid(c_orm_codepage_t cp,
                                                     unsigned char eb) {
  return (unsigned char)ebcdic_calc_map_uni(cp, eb);
}

/**
 * @brief Converts a pre-validated ASCII byte to single-byte EBCDIC.
 * @param cp Pre-validated EBCDIC codepage.
 * @param ascii Pre-validated ASCII byte.
 * @return Converted EBCDIC byte.
 */
static unsigned char ebcdic_calc_ascii_to_byte_valid(c_orm_codepage_t cp,
                                                     unsigned char ascii) {
  unsigned int i;
  for (i = 0; i < 255U; ++i) {
    if (ebcdic_calc_map_uni(cp, (unsigned char)i) == (unsigned int)ascii) {
      return (unsigned char)i;
    }
  }
  return 255U;
}

C_ORM_EXPORT c_orm_error_t c_orm_ebcdic_to_ascii_inplace(c_orm_codepage_t cp,
                                                         unsigned char *buf,
                                                         size_t len) {
  size_t i;
  if (!buf && len > 0) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (cp != C_ORM_CP_IBM037 && cp != C_ORM_CP_IBM1047 &&
      cp != C_ORM_CP_IBM500 && cp != C_ORM_CP_IBM1140) {
    return C_ORM_ERROR_CODEPAGE;
  }
  for (i = 0; i < len; ++i) {
    if (cp == C_ORM_CP_IBM1140 && buf[i] == 0x9FU) {
      return C_ORM_ERROR_CODEPAGE;
    }
  }
  for (i = 0; i < len; ++i) {
    buf[i] = ebcdic_calc_byte_to_ascii_valid(cp, buf[i]);
  }
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_ascii_to_ebcdic_inplace(c_orm_codepage_t cp,
                                                         unsigned char *buf,
                                                         size_t len) {
  size_t i;
  if (!buf && len > 0) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (cp != C_ORM_CP_IBM037 && cp != C_ORM_CP_IBM1047 &&
      cp != C_ORM_CP_IBM500 && cp != C_ORM_CP_IBM1140) {
    return C_ORM_ERROR_CODEPAGE;
  }
  for (i = 0; i < len; ++i) {
    if (cp == C_ORM_CP_IBM1140 && buf[i] == 0xA4U) {
      return C_ORM_ERROR_CODEPAGE;
    }
  }
  for (i = 0; i < len; ++i) {
    buf[i] = ebcdic_calc_ascii_to_byte_valid(cp, buf[i]);
  }
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_cobol_format_alpha(
    const char *utf8_str, size_t field_width, c_orm_codepage_t target_cp,
    int allow_truncate, int justify_right, unsigned char *out_buf,
    size_t buf_size) {
  c_orm_error_t rc;
  const char *s = utf8_str ? utf8_str : "";
  size_t slen = strlen(s);
  unsigned char pad_byte;
  unsigned short dummy_ccsid = 0;
  size_t encoded_len = 0;
  unsigned char *work = NULL;

  if (!out_buf && field_width > 0) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (buf_size < field_width) {
    return C_ORM_ERROR_OVERFLOW;
  }
  rc = c_orm_codepage_to_ccsid(target_cp, &dummy_ccsid);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (field_width == 0) {
    if (slen > 0 && !allow_truncate) {
      return C_ORM_ERROR_TRUNCATION;
    }
    return C_ORM_OK;
  }

  pad_byte = (target_cp == C_ORM_CP_UTF8 || target_cp == C_ORM_CP_ASCII)
                 ? 0x20U
                 : 0x40U;
  memset(out_buf, (int)pad_byte, field_width);

  if (slen == 0) {
    return C_ORM_OK;
  }

  rc = c_orm_system_malloc(slen + 1U, (void **)&work);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = c_orm_utf8_to_ebcdic(target_cp, s, slen, work, slen, &encoded_len);
  if (rc != C_ORM_OK) {
    c_orm_system_free(work);
    return rc;
  }

  if (encoded_len > field_width) {
    if (!allow_truncate) {
      c_orm_system_free(work);
      return C_ORM_ERROR_TRUNCATION;
    }
    if (justify_right) {
      size_t start = encoded_len - field_width;
      if (target_cp == C_ORM_CP_UTF8) {
        while (start < encoded_len && (work[start] & 0xC0U) == 0x80U) {
          ++start;
        }
        memcpy(out_buf + (field_width - (encoded_len - start)), work + start,
               encoded_len - start);
      } else {
        memcpy(out_buf, work + start, field_width);
      }
    } else {
      size_t copy_len = field_width;
      if (target_cp == C_ORM_CP_UTF8) {
        while (copy_len > 0 && (work[copy_len] & 0xC0U) == 0x80U) {
          --copy_len;
        }
      }
      memcpy(out_buf, work, copy_len);
    }
  } else if (justify_right) {
    memcpy(out_buf + (field_width - encoded_len), work, encoded_len);
  } else {
    memcpy(out_buf, work, encoded_len);
  }

  c_orm_system_free(work);
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t
c_orm_cobol_extract_alpha(const unsigned char *raw_buf, size_t field_width,
                          c_orm_codepage_t source_cp, int trim_trailing_spaces,
                          char *out_utf8, size_t out_size, size_t *out_len) {
  c_orm_error_t rc;
  unsigned short dummy_ccsid = 0;
  unsigned char pad_byte;
  size_t eff_len = field_width;

  if (!out_utf8) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (!raw_buf && field_width > 0) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  rc = c_orm_codepage_to_ccsid(source_cp, &dummy_ccsid);
  if (rc != C_ORM_OK) {
    return rc;
  }
  pad_byte = (source_cp == C_ORM_CP_UTF8 || source_cp == C_ORM_CP_ASCII)
                 ? 0x20U
                 : 0x40U;
  if (trim_trailing_spaces) {
    while (eff_len > 0 && raw_buf[eff_len - 1U] == pad_byte) {
      --eff_len;
    }
  }
  return c_orm_ebcdic_to_utf8(source_cp, raw_buf, eff_len, out_utf8, out_size,
                              out_len);
}

/**
 * @brief Writes a 16-bit code unit into dst in host or big-endian wire order.
 * @param dst Destination 16-bit slot.
 * @param unit 16-bit code unit value.
 * @param big_endian Non-zero for big-endian wire representation.
 * @return Number of 16-bit code units written (1).
 */
static size_t ebcdic_calc_u16_write_slot(unsigned short *dst,
                                         unsigned short unit, int big_endian) {
  if (big_endian) {
    unsigned char *wb = (unsigned char *)dst;
    wb[0] = (unsigned char)(unit >> 8);
    wb[1] = (unsigned char)(unit & 0xFFU);
  } else {
    *dst = unit;
  }
  return 1U;
}

/**
 * @brief Reads a 16-bit code unit from src in host or big-endian wire order.
 * @param src Source 16-bit slot.
 * @param big_endian Non-zero if src is stored in big-endian wire order.
 * @return Host-endian 16-bit code unit.
 */
static unsigned short ebcdic_calc_u16_read_slot(const unsigned short *src,
                                                int big_endian) {
  if (big_endian) {
    const unsigned char *rb = (const unsigned char *)src;
    return (unsigned short)(((unsigned int)rb[0] << 8) | (unsigned int)rb[1]);
  }
  return *src;
}

C_ORM_EXPORT c_orm_error_t c_orm_utf8_to_utf16(const char *utf8,
                                               size_t utf8_len, int big_endian,
                                               unsigned short *out_u16,
                                               size_t out_u16_cap,
                                               size_t *out_units) {
  c_orm_error_t rc = C_ORM_OK;
  size_t i = 0;
  size_t upos = 0;
  const unsigned char *src = (const unsigned char *)utf8;

  if (!out_u16) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (!utf8 && utf8_len > 0) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (out_u16_cap == 0) {
    return C_ORM_ERROR_OVERFLOW;
  }

  while (i < utf8_len) {
    unsigned int cp = 0;
    rc = utf8_next_codepoint(src, utf8_len, &i, &cp);
    if (rc != C_ORM_OK) {
      return rc;
    }
    if (cp <= 0xFFFFU) {
      if (upos + 1U >= out_u16_cap) {
        return C_ORM_ERROR_OVERFLOW;
      }
      upos += ebcdic_calc_u16_write_slot(&out_u16[upos], (unsigned short)cp,
                                         big_endian);
    } else {
      unsigned int sub = cp - 0x10000U;
      unsigned short hi = (unsigned short)(0xD800U + (sub >> 10));
      unsigned short lo = (unsigned short)(0xDC00U + (sub & 0x3FFU));
      if (upos + 2U >= out_u16_cap) {
        return C_ORM_ERROR_OVERFLOW;
      }
      upos += ebcdic_calc_u16_write_slot(&out_u16[upos], hi, big_endian);
      upos += ebcdic_calc_u16_write_slot(&out_u16[upos], lo, big_endian);
    }
  }

  out_u16[upos] = 0U;
  if (out_units) {
    *out_units = upos;
  }
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_utf16_to_utf8(const unsigned short *u16,
                                               size_t u16_units, int big_endian,
                                               char *out_utf8, size_t out_size,
                                               size_t *out_bytes) {
  c_orm_error_t rc = C_ORM_OK;
  size_t i = 0;
  size_t pos = 0;

  if (!out_utf8) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (!u16 && u16_units > 0) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (out_size == 0) {
    return C_ORM_ERROR_OVERFLOW;
  }

  while (i < u16_units) {
    unsigned short w0 = ebcdic_calc_u16_read_slot(&u16[i++], big_endian);
    unsigned int cp;
    if (w0 >= 0xD800U && w0 <= 0xDBFFU) {
      unsigned short w1;
      if (i >= u16_units) {
        return C_ORM_ERROR_CODEPAGE;
      }
      w1 = ebcdic_calc_u16_read_slot(&u16[i++], big_endian);
      if (w1 < 0xDC00U || w1 > 0xDFFFU) {
        return C_ORM_ERROR_CODEPAGE;
      }
      cp = 0x10000U + ((((unsigned int)w0 - 0xD800U) << 10) |
                       ((unsigned int)w1 - 0xDC00U));
    } else if (w0 >= 0xDC00U && w0 <= 0xDFFFU) {
      return C_ORM_ERROR_CODEPAGE;
    } else {
      cp = (unsigned int)w0;
    }
    rc = utf8_emit_codepoint(cp, out_utf8, out_size, &pos);
    if (rc != C_ORM_OK) {
      return rc;
    }
  }

  out_utf8[pos] = '\0';
  if (out_bytes) {
    *out_bytes = pos;
  }
  return rc;
}
