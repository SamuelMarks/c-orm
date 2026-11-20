/**
 * @file c_orm_decimal.h
 * @brief Lossless fixed-point decimal arithmetic and COBOL/ODBC wire formats.
 */

#ifndef C_ORM_DECIMAL_H
#define C_ORM_DECIMAL_H

/* clang-format off */
#include "c_orm_meta.h"
#include "c_orm_safe_crt.h"
#include <stddef.h>
/* clang-format on */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** @brief Maximum supported base-10 decimal digits (ANSI/ODBC max 38, Db2 31).
 */
#define C_ORM_DECIMAL_MAX_DIGITS 38

/**
 * @brief Rounding modes for fixed-point decimal rescaling and division.
 */
typedef enum {
  C_ORM_ROUND_TRUNCATE = 0,  /**< Truncate toward zero (COBOL default) */
  C_ORM_ROUND_HALF_UP = 1,   /**< Round half away from zero (COBOL ROUNDED) */
  C_ORM_ROUND_HALF_EVEN = 2, /**< Banker's rounding (Db2 ROUND_HALF_EVEN) */
  C_ORM_ROUND_ERROR_IF_INEXACT = 3 /**< Fail with C_ORM_ERROR_DECIMAL_INEXACT */
} c_orm_round_mode_t;

/**
 * @brief Stack-allocated exact base-10 fixed-point decimal value.
 * @var sign 0 for positive or zero, 1 for negative.
 * @var precision Total number of represented digits (1..38).
 * @var scale Number of fractional digits after the radix point (0..38).
 * @var digits Little-endian base-10 digits (0..9), digits[0] is 10^(-scale).
 */
typedef struct c_orm_decimal {
  /** @brief 0 for non-negative, 1 for negative (zero always has sign == 0). */
  unsigned char sign;
  /** @brief Active precision in digits (1..38). */
  unsigned char precision;
  /** @brief Number of fractional digits (0..38). */
  unsigned char scale;
  /** @brief Unpacked base-10 digits (0..9), least-significant at index 0. */
  unsigned char digits[C_ORM_DECIMAL_MAX_DIGITS];
} c_orm_decimal_t;

/**
 * @brief Initializes a decimal value to zero at the requested scale.
 * @param scale Desired fractional scale (0..38).
 * @param out_dec Output pointer receiving the initialized zero decimal.
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER, or
 * C_ORM_ERROR_DECIMAL_OVERFLOW if scale > 38.
 */
C_ORM_EXPORT c_orm_error_t c_orm_decimal_zero(unsigned char scale,
                                              c_orm_decimal_t *out_dec);

/**
 * @brief Parses a base-10 string (including optional sign and exponent) into
 * an exact c_orm_decimal_t.
 * @param str Null-terminated ASCII decimal or scientific string.
 * @param out_dec Output pointer receiving the parsed decimal.
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER,
 * C_ORM_ERROR_DECIMAL_SYNTAX, or C_ORM_ERROR_DECIMAL_OVERFLOW.
 */
C_ORM_EXPORT c_orm_error_t c_orm_decimal_from_str(const char *str,
                                                  c_orm_decimal_t *out_dec);

/**
 * @brief Formats a c_orm_decimal_t into a canonical null-terminated string.
 * @param dec Source decimal value.
 * @param out_buf Destination character buffer.
 * @param buf_size Capacity of out_buf in bytes (including NUL terminator).
 * @param out_len Optional output pointer receiving string length (excluding
 * NUL), or NULL.
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER,
 * C_ORM_ERROR_VALIDATION, or C_ORM_ERROR_OVERFLOW if buf_size is too small.
 */
C_ORM_EXPORT c_orm_error_t c_orm_decimal_to_str(const c_orm_decimal_t *dec,
                                                char *out_buf, size_t buf_size,
                                                size_t *out_len);

/**
 * @brief Converts a signed 64-bit integer into an exact scale-0 decimal.
 * @param val Signed 64-bit integer value.
 * @param out_dec Output pointer receiving the decimal representation.
 * @return C_ORM_OK on success, or C_ORM_ERROR_NULL_POINTER.
 */
C_ORM_EXPORT c_orm_error_t c_orm_decimal_from_int64(c_orm_int64_t val,
                                                    c_orm_decimal_t *out_dec);

/**
 * @brief Converts a decimal value to a signed 64-bit integer using the
 * specified rounding mode.
 * @param dec Source decimal value.
 * @param mode Rounding mode applied to fractional digits.
 * @param out_val Output pointer receiving the 64-bit integer.
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER,
 * C_ORM_ERROR_DECIMAL_INEXACT, or C_ORM_ERROR_DECIMAL_OVERFLOW.
 */
C_ORM_EXPORT c_orm_error_t c_orm_decimal_to_int64(const c_orm_decimal_t *dec,
                                                  c_orm_round_mode_t mode,
                                                  c_orm_int64_t *out_val);

/**
 * @brief Rescales a decimal to a target precision and scale, applying rounding
 * and verifying Db2/SQL DECIMAL(p, s) bounds.
 * @param dec Source decimal value.
 * @param target_precision Maximum allowed total digits (1..38).
 * @param target_scale Target fractional digits (0..target_precision).
 * @param mode Rounding mode when reducing scale.
 * @param out_dec Output pointer receiving the rescaled decimal (may alias dec).
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER,
 * C_ORM_ERROR_VALIDATION, C_ORM_ERROR_DECIMAL_INEXACT, or
 * C_ORM_ERROR_DECIMAL_OVERFLOW.
 */
C_ORM_EXPORT c_orm_error_t c_orm_decimal_rescale(const c_orm_decimal_t *dec,
                                                 unsigned char target_precision,
                                                 unsigned char target_scale,
                                                 c_orm_round_mode_t mode,
                                                 c_orm_decimal_t *out_dec);

/**
 * @brief Checks whether a decimal value equals zero.
 * @param dec Decimal value to inspect.
 * @param out_is_zero Output pointer set to 1 if zero, 0 otherwise.
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER, or
 * C_ORM_ERROR_VALIDATION.
 */
C_ORM_EXPORT c_orm_error_t c_orm_decimal_is_zero(const c_orm_decimal_t *dec,
                                                 int *out_is_zero);

/**
 * @brief Negates a decimal value (zero remains unsigned positive zero).
 * @param dec Source decimal value.
 * @param out_dec Output pointer receiving negated decimal (may alias dec).
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER, or
 * C_ORM_ERROR_VALIDATION.
 */
C_ORM_EXPORT c_orm_error_t c_orm_decimal_negate(const c_orm_decimal_t *dec,
                                                c_orm_decimal_t *out_dec);

/**
 * @brief Numerically compares two decimal values regardless of trailing scale
 * zeros (e.g., 12.3 == 12.30).
 * @param a Left-hand decimal value.
 * @param b Right-hand decimal value.
 * @param out_cmp Output pointer set to -1 if a < b, 0 if a == b, or 1 if a > b.
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER, or
 * C_ORM_ERROR_VALIDATION.
 */
C_ORM_EXPORT c_orm_error_t c_orm_decimal_cmp(const c_orm_decimal_t *a,
                                             const c_orm_decimal_t *b,
                                             int *out_cmp);

/**
 * @brief Computes exact fixed-point sum (a + b).
 * @param a Left addend.
 * @param b Right addend.
 * @param out_res Output pointer receiving a + b (may alias a or b).
 * @return C_ORM_OK on success, or C_ORM_ERROR_DECIMAL_OVERFLOW if > 38 digits.
 */
C_ORM_EXPORT c_orm_error_t c_orm_decimal_add(const c_orm_decimal_t *a,
                                             const c_orm_decimal_t *b,
                                             c_orm_decimal_t *out_res);

/**
 * @brief Computes exact fixed-point difference (a - b).
 * @param a Minuend.
 * @param b Subtrahend.
 * @param out_res Output pointer receiving a - b (may alias a or b).
 * @return C_ORM_OK on success, or C_ORM_ERROR_DECIMAL_OVERFLOW if > 38 digits.
 */
C_ORM_EXPORT c_orm_error_t c_orm_decimal_sub(const c_orm_decimal_t *a,
                                             const c_orm_decimal_t *b,
                                             c_orm_decimal_t *out_res);

/**
 * @brief Computes fixed-point product (a * b) bounded by max_scale.
 * @param a Multiplicand.
 * @param b Multiplier.
 * @param max_scale Maximum fractional scale to retain (0..38).
 * @param mode Rounding mode applied if (a->scale + b->scale) > max_scale.
 * @param out_res Output pointer receiving the product (may alias a or b).
 * @return C_ORM_OK on success, C_ORM_ERROR_DECIMAL_INEXACT, or
 * C_ORM_ERROR_DECIMAL_OVERFLOW.
 */
C_ORM_EXPORT c_orm_error_t c_orm_decimal_mul(const c_orm_decimal_t *a,
                                             const c_orm_decimal_t *b,
                                             unsigned char max_scale,
                                             c_orm_round_mode_t mode,
                                             c_orm_decimal_t *out_res);

/**
 * @brief Computes fixed-point quotient (a / b) rounded to result_scale.
 * @param a Dividend.
 * @param b Divisor.
 * @param result_scale Desired fractional scale of the quotient (0..38).
 * @param mode Rounding mode applied to the remainder.
 * @param out_res Output pointer receiving the quotient (may alias a or b).
 * @return C_ORM_OK on success, C_ORM_ERROR_DECIMAL_OVERFLOW on divide-by-zero
 * or > 38 digits, or C_ORM_ERROR_DECIMAL_INEXACT.
 */
C_ORM_EXPORT c_orm_error_t c_orm_decimal_div(const c_orm_decimal_t *a,
                                             const c_orm_decimal_t *b,
                                             unsigned char result_scale,
                                             c_orm_round_mode_t mode,
                                             c_orm_decimal_t *out_res);

/**
 * @brief Marshals a c_orm_decimal_t into ODBC SQL_NUMERIC_STRUCT 16-byte
 * little-endian mantissa fields.
 * @param dec Source decimal value.
 * @param out_val16 16-byte output buffer receiving little-endian unscaled int.
 * @param out_precision Output pointer receiving precision (1..38).
 * @param out_scale Output pointer receiving scale (0..38).
 * @param out_odbc_sign Output pointer receiving ODBC sign (1 = positive,
 * 0 = negative).
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER, or
 * C_ORM_ERROR_DECIMAL_OVERFLOW if mantissa exceeds 128 bits.
 */
C_ORM_EXPORT c_orm_error_t c_orm_decimal_to_odbc_numeric(
    const c_orm_decimal_t *dec, unsigned char out_val16[16],
    unsigned char *out_precision, signed char *out_scale,
    unsigned char *out_odbc_sign);

/**
 * @brief Unmarshals ODBC SQL_NUMERIC_STRUCT fields into a c_orm_decimal_t.
 * @param val16 16-byte little-endian unsigned mantissa from SQL_NUMERIC_STRUCT.
 * @param precision Reported ODBC precision (0..38).
 * @param scale Reported ODBC scale (0..38; negative scales rejected).
 * @param odbc_sign ODBC sign flag (1 = positive, 0 = negative).
 * @param out_dec Output pointer receiving the decoded decimal.
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER,
 * C_ORM_ERROR_VALIDATION, or C_ORM_ERROR_DECIMAL_OVERFLOW if > 38 digits.
 */
C_ORM_EXPORT c_orm_error_t c_orm_decimal_from_odbc_numeric(
    const unsigned char val16[16], unsigned char precision, signed char scale,
    unsigned char odbc_sign, c_orm_decimal_t *out_dec);

/**
 * @brief Computes the byte length of a COBOL COMP-3 (PACKED-DECIMAL) field.
 * @param total_digits Total PIC 9 digits (integer + fractional, 1..38).
 * @param out_bytes Output pointer receiving `(total_digits / 2) + 1`.
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER, or
 * C_ORM_ERROR_VALIDATION.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_decimal_comp3_byte_len(unsigned char total_digits, size_t *out_bytes);

/**
 * @brief Packs a c_orm_decimal_t into a COBOL COMP-3 (PACKED-DECIMAL) byte
 * buffer.
 * @param dec Source decimal value (truncated/exact-aligned to pic_frac_digits).
 * @param pic_int_digits Number of integer digits before V in PIC (0..38).
 * @param pic_frac_digits Number of fractional digits after V in PIC (0..38).
 * @param is_signed Non-zero for PIC S9 (0x0C/0x0D sign), 0 for unsigned PIC 9
 * (0x0F sign).
 * @param out_buf Destination byte buffer.
 * @param buf_size Capacity of out_buf in bytes.
 * @param out_written Optional output pointer receiving bytes written, or NULL.
 * @return C_ORM_OK on success, C_ORM_ERROR_DECIMAL_OVERFLOW, or
 * C_ORM_ERROR_OVERFLOW if buf_size is insufficient.
 */
C_ORM_EXPORT c_orm_error_t c_orm_decimal_to_comp3(
    const c_orm_decimal_t *dec, unsigned char pic_int_digits,
    unsigned char pic_frac_digits, int is_signed, unsigned char *out_buf,
    size_t buf_size, size_t *out_written);

/**
 * @brief Unpacks a COBOL COMP-3 (PACKED-DECIMAL) byte buffer into a
 * c_orm_decimal_t.
 * @param buf Raw COMP-3 bytes.
 * @param buf_len Length of buf in bytes (1..20).
 * @param pic_frac_digits Implied fractional digits after V (0..38).
 * @param out_dec Output pointer receiving unpacked decimal.
 * @return C_ORM_OK on success, C_ORM_ERROR_DECIMAL_SYNTAX on invalid BCD or
 * sign nybble, or C_ORM_ERROR_DECIMAL_OVERFLOW.
 */
C_ORM_EXPORT c_orm_error_t c_orm_decimal_from_comp3(
    const unsigned char *buf, size_t buf_len, unsigned char pic_frac_digits,
    c_orm_decimal_t *out_dec);

/**
 * @brief Packs a c_orm_decimal_t into a COBOL Zoned-Decimal (USAGE DISPLAY)
 * byte buffer in ASCII or EBCDIC.
 * @param dec Source decimal value.
 * @param pic_int_digits Integer digit count in PIC (0..38).
 * @param pic_frac_digits Fractional digit count in PIC (0..38).
 * @param is_signed Non-zero if PIC S9.
 * @param sign_leading Non-zero if SIGN IS LEADING, 0 for trailing (default).
 * @param sign_separate Non-zero if SEPARATE CHARACTER, 0 for overpunch.
 * @param use_ebcdic Non-zero for EBCDIC (0xF0/0xC0/0xD0), 0 for ASCII.
 * @param out_buf Destination byte buffer.
 * @param buf_size Capacity of out_buf in bytes.
 * @param out_written Optional output pointer receiving bytes written, or NULL.
 * @return C_ORM_OK on success, C_ORM_ERROR_DECIMAL_OVERFLOW, or
 * C_ORM_ERROR_OVERFLOW.
 */
C_ORM_EXPORT c_orm_error_t c_orm_decimal_to_zoned(
    const c_orm_decimal_t *dec, unsigned char pic_int_digits,
    unsigned char pic_frac_digits, int is_signed, int sign_leading,
    int sign_separate, int use_ebcdic, unsigned char *out_buf, size_t buf_size,
    size_t *out_written);

/**
 * @brief Unpacks a COBOL Zoned-Decimal (USAGE DISPLAY) byte buffer in ASCII or
 * EBCDIC into a c_orm_decimal_t.
 * @param buf Source zoned-decimal byte buffer.
 * @param buf_len Number of bytes in buf.
 * @param pic_frac_digits Implied fractional digits after V (0..38).
 * @param is_signed Non-zero if PIC S9.
 * @param sign_leading Non-zero if SIGN IS LEADING, 0 for trailing.
 * @param sign_separate Non-zero if SEPARATE CHARACTER, 0 for overpunch.
 * @param use_ebcdic Non-zero for EBCDIC, 0 for ASCII.
 * @param out_dec Output pointer receiving unpacked decimal.
 * @return C_ORM_OK on success, C_ORM_ERROR_DECIMAL_SYNTAX, or
 * C_ORM_ERROR_DECIMAL_OVERFLOW.
 */
C_ORM_EXPORT c_orm_error_t c_orm_decimal_from_zoned(
    const unsigned char *buf, size_t buf_len, unsigned char pic_frac_digits,
    int is_signed, int sign_leading, int sign_separate, int use_ebcdic,
    c_orm_decimal_t *out_dec);

/**
 * @brief Computes storage width in bytes (2, 4, or 8) for an IBM Enterprise
 * COBOL USAGE COMP / BINARY field of `pic_digits` (1..18).
 * @param pic_digits Total PIC 9 digits (1..18).
 * @param out_bytes Output pointer receiving 2, 4, or 8.
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER, or
 * C_ORM_ERROR_VALIDATION.
 */
C_ORM_EXPORT c_orm_error_t c_orm_cobol_comp_byte_len(unsigned char pic_digits,
                                                     size_t *out_bytes);

/**
 * @brief Encodes a 64-bit integer into a 2-, 4-, or 8-byte COBOL COMP
 * (big-endian) or COMP-5 (native-endian) binary buffer.
 * @param val Integer value to encode.
 * @param byte_width Storage width in bytes (must be 2, 4, or 8).
 * @param is_big_endian Non-zero for big-endian mainframe COMP, 0 for native
 * endian COMP-5.
 * @param out_buf Destination buffer of at least byte_width bytes.
 * @param buf_size Capacity of out_buf in bytes.
 * @return C_ORM_OK on success, C_ORM_ERROR_DECIMAL_OVERFLOW if val exceeds
 * byte_width range, C_ORM_ERROR_OVERFLOW if buf_size < byte_width, or
 * C_ORM_ERROR_VALIDATION.
 */
C_ORM_EXPORT c_orm_error_t c_orm_int64_to_cobol_comp(c_orm_int64_t val,
                                                     size_t byte_width,
                                                     int is_big_endian,
                                                     unsigned char *out_buf,
                                                     size_t buf_size);

/**
 * @brief Decodes a 2-, 4-, or 8-byte COBOL COMP (big-endian) or COMP-5
 * (native-endian) binary buffer into a signed 64-bit integer.
 * @param buf Source binary buffer of at least byte_width bytes.
 * @param byte_width Storage width in bytes (must be 2, 4, or 8).
 * @param is_signed Non-zero for signed two's-complement, 0 for unsigned.
 * @param is_big_endian Non-zero for big-endian mainframe COMP, 0 for native
 * endian COMP-5.
 * @param out_val Output pointer receiving the decoded integer.
 * @return C_ORM_OK on success, C_ORM_ERROR_DECIMAL_OVERFLOW if unsigned 8-byte
 * exceeds INT64_MAX, C_ORM_ERROR_NULL_POINTER, or C_ORM_ERROR_VALIDATION.
 */
C_ORM_EXPORT c_orm_error_t c_orm_int64_from_cobol_comp(const unsigned char *buf,
                                                       size_t byte_width,
                                                       int is_signed,
                                                       int is_big_endian,
                                                       c_orm_int64_t *out_val);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* C_ORM_DECIMAL_H */
