/**
 * @file c_orm_ebcdic.h
 * @brief EBCDIC mainframe codepage, fixed-width COBOL string, and UTF-16
 * GRAPHIC transcoders.
 */

#ifndef C_ORM_EBCDIC_H
#define C_ORM_EBCDIC_H

/* clang-format off */
#include "c_orm_meta.h"
#include <stddef.h>
/* clang-format on */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @brief Supported character codepages and IBM Coded Character Set IDs (CCSID).
 */
typedef enum {
  C_ORM_CP_UTF8 = 0,  /**< UTF-8 (CCSID 1208) */
  C_ORM_CP_ASCII = 1, /**< 7-bit ASCII / ISO-8859-1 Latin-1 (CCSID 367/819) */
  C_ORM_CP_IBM037 =
      2, /**< US/Canada z/OS MVS & Db2 default EBCDIC (CCSID 37) */
  C_ORM_CP_IBM1047 =
      3, /**< z/OS UNIX System Services C/COBOL EBCDIC (CCSID 1047) */
  C_ORM_CP_IBM500 = 4, /**< International EBCDIC (CCSID 500) */
  C_ORM_CP_IBM1140 =
      5 /**< US/Canada EBCDIC with Euro U+20AC at 0x9F (CCSID 1140) */
} c_orm_codepage_t;

/**
 * @brief Resolves an IBM CCSID integer into a c_orm_codepage_t enum constant.
 * @param ccsid Numeric IBM CCSID (e.g. 37, 367, 500, 819, 1047, 1140, 1208).
 * @param out_cp Output pointer receiving the corresponding codepage enum.
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER, or
 * C_ORM_ERROR_CODEPAGE if unsupported.
 */
C_ORM_EXPORT c_orm_error_t c_orm_codepage_from_ccsid(unsigned short ccsid,
                                                     c_orm_codepage_t *out_cp);

/**
 * @brief Resolves a c_orm_codepage_t enum constant into its canonical IBM
 * CCSID.
 * @param cp Codepage enum constant.
 * @param out_ccsid Output pointer receiving the IBM CCSID (e.g. 37, 1047).
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER, or
 * C_ORM_ERROR_CODEPAGE if invalid.
 */
C_ORM_EXPORT c_orm_error_t c_orm_codepage_to_ccsid(c_orm_codepage_t cp,
                                                   unsigned short *out_ccsid);

/**
 * @brief Transcodes single-byte EBCDIC (or ISO-8859-1/UTF-8) bytes into a
 * null-terminated UTF-8 string.
 * @param cp Source single-byte codepage (IBM037, IBM1047, IBM500, IBM1140,
 * ASCII, or UTF8).
 * @param src Source byte buffer.
 * @param src_len Number of bytes in src.
 * @param out_utf8 Destination UTF-8 buffer (always null-terminated on success).
 * @param out_size Capacity of out_utf8 in bytes (must be >= 1).
 * @param out_written Optional output pointer receiving UTF-8 byte count
 * excluding NUL, or NULL.
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER, C_ORM_ERROR_CODEPAGE,
 * or C_ORM_ERROR_OVERFLOW.
 */
C_ORM_EXPORT c_orm_error_t c_orm_ebcdic_to_utf8(c_orm_codepage_t cp,
                                                const unsigned char *src,
                                                size_t src_len, char *out_utf8,
                                                size_t out_size,
                                                size_t *out_written);

/**
 * @brief Transcodes a UTF-8 string into single-byte EBCDIC (or ISO-8859-1)
 * bytes.
 * @param cp Target codepage (IBM037, IBM1047, IBM500, IBM1140, ASCII, or UTF8).
 * @param src_utf8 Source UTF-8 byte buffer.
 * @param src_len Number of UTF-8 bytes in src_utf8.
 * @param out_ebcdic Destination byte buffer.
 * @param out_size Capacity of out_ebcdic in bytes.
 * @param out_written Optional output pointer receiving encoded byte count, or
 * NULL.
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER, C_ORM_ERROR_CODEPAGE
 * on invalid UTF-8 or unmappable codepoint, or C_ORM_ERROR_OVERFLOW.
 */
C_ORM_EXPORT c_orm_error_t c_orm_utf8_to_ebcdic(
    c_orm_codepage_t cp, const char *src_utf8, size_t src_len,
    unsigned char *out_ebcdic, size_t out_size, size_t *out_written);

/**
 * @brief Converts a buffer in-place from an EBCDIC codepage to 8-bit ISO-8859-1
 * / ASCII (note: Euro U+20AC in IBM1140 returns C_ORM_ERROR_CODEPAGE as it
 * exceeds ISO-8859-1).
 * @param cp Source EBCDIC codepage (IBM037, IBM1047, IBM500, or IBM1140).
 * @param buf Buffer to transcode in-place.
 * @param len Number of bytes in buf.
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER, or
 * C_ORM_ERROR_CODEPAGE.
 */
C_ORM_EXPORT c_orm_error_t c_orm_ebcdic_to_ascii_inplace(c_orm_codepage_t cp,
                                                         unsigned char *buf,
                                                         size_t len);

/**
 * @brief Converts an 8-bit ISO-8859-1 / ASCII buffer in-place to the specified
 * EBCDIC codepage.
 * @param cp Target EBCDIC codepage (IBM037, IBM1047, IBM500, or IBM1140).
 * @param buf Buffer to transcode in-place.
 * @param len Number of bytes in buf.
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER, or
 * C_ORM_ERROR_CODEPAGE.
 */
C_ORM_EXPORT c_orm_error_t c_orm_ascii_to_ebcdic_inplace(c_orm_codepage_t cp,
                                                         unsigned char *buf,
                                                         size_t len);

/**
 * @brief Formats a UTF-8 C string into a fixed-width COBOL PIC X(n) or Db2
 * CHAR(n) field, space-padding with 0x40 (EBCDIC) or 0x20 (ASCII/UTF-8).
 * @param utf8_str Null-terminated UTF-8 input string (treated as "" if NULL).
 * @param field_width Exact fixed byte width of the COBOL/Db2 target field.
 * @param target_cp Target codepage for the fixed-width field.
 * @param allow_truncate Non-zero to truncate excess characters (COBOL MOVE
 * semantics), 0 to return C_ORM_ERROR_TRUNCATION when input exceeds
 * field_width.
 * @param justify_right Non-zero for COBOL JUSTIFIED RIGHT (leading spaces),
 * 0 for standard left-justified trailing spaces.
 * @param out_buf Destination buffer of at least field_width bytes.
 * @param buf_size Capacity of out_buf in bytes.
 * @return C_ORM_OK on success, C_ORM_ERROR_TRUNCATION, C_ORM_ERROR_CODEPAGE,
 * or C_ORM_ERROR_OVERFLOW.
 */
C_ORM_EXPORT c_orm_error_t c_orm_cobol_format_alpha(
    const char *utf8_str, size_t field_width, c_orm_codepage_t target_cp,
    int allow_truncate, int justify_right, unsigned char *out_buf,
    size_t buf_size);

/**
 * @brief Extracts a fixed-width COBOL PIC X(n) or Db2 CHAR(n) field into a
 * null-terminated UTF-8 string, optionally stripping trailing spaces.
 * @param raw_buf Source fixed-width byte field.
 * @param field_width Width of raw_buf in bytes.
 * @param source_cp Source codepage of raw_buf.
 * @param trim_trailing_spaces Non-zero to strip trailing codepage spaces
 * (0x40 in EBCDIC, 0x20 in ASCII/UTF-8).
 * @param out_utf8 Destination null-terminated UTF-8 buffer.
 * @param out_size Capacity of out_utf8 in bytes (must be >= 1).
 * @param out_len Optional output pointer receiving resulting UTF-8 byte length,
 * or NULL.
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER, C_ORM_ERROR_CODEPAGE,
 * or C_ORM_ERROR_OVERFLOW.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_cobol_extract_alpha(const unsigned char *raw_buf, size_t field_width,
                          c_orm_codepage_t source_cp, int trim_trailing_spaces,
                          char *out_utf8, size_t out_size, size_t *out_len);

/**
 * @brief Transcodes UTF-8 text into UTF-16 code units (for Db2 GRAPHIC /
 * VARGRAPHIC CCSID 1200 or ODBC SQLWCHAR wide APIs without <windows.h>).
 * @param utf8 Source UTF-8 byte sequence.
 * @param utf8_len Length of utf8 in bytes.
 * @param big_endian Non-zero to byte-swap each 16-bit unit into big-endian
 * network/z/OS byte order; 0 for host-endian SQLWCHAR.
 * @param out_u16 Destination array of 16-bit code units (null-terminated when
 * out_u16_cap > *out_units; requires at least units + 1 capacity).
 * @param out_u16_cap Capacity of out_u16 in 16-bit units (including NUL unit).
 * @param out_units Optional output pointer receiving number of 16-bit units
 * written (excluding NUL), or NULL.
 * @return C_ORM_OK on success, C_ORM_ERROR_CODEPAGE on malformed UTF-8 or
 * surrogate codepoints, C_ORM_ERROR_OVERFLOW, or C_ORM_ERROR_NULL_POINTER.
 */
C_ORM_EXPORT c_orm_error_t c_orm_utf8_to_utf16(const char *utf8,
                                               size_t utf8_len, int big_endian,
                                               unsigned short *out_u16,
                                               size_t out_u16_cap,
                                               size_t *out_units);

/**
 * @brief Transcodes UTF-16 code units (host-endian SQLWCHAR or big-endian Db2
 * GRAPHIC CCSID 1200) into a null-terminated UTF-8 string.
 * @param u16 Source array of 16-bit UTF-16 code units.
 * @param u16_units Number of 16-bit units to decode.
 * @param big_endian Non-zero if u16 units are stored in big-endian byte order;
 * 0 for host-endian.
 * @param out_utf8 Destination null-terminated UTF-8 buffer.
 * @param out_size Capacity of out_utf8 in bytes (including NUL).
 * @param out_bytes Optional output pointer receiving UTF-8 byte length
 * (excluding NUL), or NULL.
 * @return C_ORM_OK on success, C_ORM_ERROR_CODEPAGE on unpaired surrogates,
 * C_ORM_ERROR_OVERFLOW, or C_ORM_ERROR_NULL_POINTER.
 */
C_ORM_EXPORT c_orm_error_t c_orm_utf16_to_utf8(const unsigned short *u16,
                                               size_t u16_units, int big_endian,
                                               char *out_utf8, size_t out_size,
                                               size_t *out_bytes);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* C_ORM_EBCDIC_H */
