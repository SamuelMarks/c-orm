/**
 * @file c_orm_decimal.c
 * @brief Pure C89 lossless fixed-point decimal arithmetic and COBOL/ODBC wire
 * marshaling.
 */

/* clang-format off */
#include "c_orm_decimal.h"
#include "c_orm_log.h"
#include <string.h>
/* clang-format on */

/**
 * @brief Validates structural invariants of a c_orm_decimal_t instance.
 * @param dec Decimal structure to validate.
 * @return C_ORM_OK if valid, C_ORM_ERROR_NULL_POINTER, or
 * C_ORM_ERROR_VALIDATION.
 */
static c_orm_error_t dec_validate(const c_orm_decimal_t *dec) {
  size_t i;
  if (dec->sign > 1 || dec->precision < 1 ||
      dec->precision > C_ORM_DECIMAL_MAX_DIGITS ||
      dec->scale > C_ORM_DECIMAL_MAX_DIGITS) {
    LOG_DEBUG("dec_validate: invalid header fields");
    return C_ORM_ERROR_VALIDATION;
  }
  for (i = 0; i < C_ORM_DECIMAL_MAX_DIGITS; ++i) {
    if (dec->digits[i] > 9) {
      LOG_DEBUG("dec_validate: invalid digit > 9");
      return C_ORM_ERROR_VALIDATION;
    }
  }
  return C_ORM_OK;
}

/**
 * @brief Recomputes canonical precision and clears negative-zero sign.
 * @param dec Decimal structure to normalize in-place.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t dec_normalize(c_orm_decimal_t *dec) {
  int top = -1;
  int i;
  unsigned char min_prec;
  for (i = C_ORM_DECIMAL_MAX_DIGITS - 1; i >= 0; --i) {
    if (dec->digits[i] != 0) {
      top = i;
      break;
    }
  }
  if (top < 0) {
    dec->sign = 0;
    dec->precision = (unsigned char)(dec->scale > 0 ? dec->scale : 1);
    return C_ORM_OK;
  }
  min_prec = (unsigned char)(dec->scale > 0 ? dec->scale : 1);
  if ((unsigned char)(top + 1) > min_prec) {
    dec->precision = (unsigned char)(top + 1);
  } else {
    dec->precision = min_prec;
  }
  return C_ORM_OK;
}

/**
 * @brief Shifts or rounds an extended little-endian digit buffer into a
 * c_orm_decimal_t.
 * @param buf Little-endian base-10 digits (0..9).
 * @param len Number of valid digits in buf.
 * @param sign Sign flag (0 or 1).
 * @param cur_scale Current fractional scale of buf.
 * @param target_scale Desired fractional scale (0..38).
 * @param extra_sticky Non-zero if non-zero digits were already discarded below
 * buf[0].
 * @param mode Rounding mode to apply when cur_scale > target_scale.
 * @param out_dec Output decimal receiving rounded result.
 * @return C_ORM_OK on success, C_ORM_ERROR_DECIMAL_INEXACT,
 * C_ORM_ERROR_DECIMAL_OVERFLOW, or C_ORM_ERROR_VALIDATION.
 */
static c_orm_error_t dec_round_from_buf(const unsigned char *buf, size_t len,
                                        unsigned char sign, size_t cur_scale,
                                        unsigned char target_scale,
                                        int extra_sticky,
                                        c_orm_round_mode_t mode,
                                        c_orm_decimal_t *out_dec) {
  c_orm_decimal_t res;
  size_t i;
  if ((int)mode < (int)C_ORM_ROUND_TRUNCATE ||
      (int)mode > (int)C_ORM_ROUND_ERROR_IF_INEXACT) {
    return C_ORM_ERROR_VALIDATION;
  }
  memset(&res, 0, sizeof(res));
  res.sign = sign ? 1 : 0;
  res.scale = target_scale;

  if (cur_scale <= (size_t)target_scale) {
    size_t shift = (size_t)target_scale - cur_scale;
    for (i = 0; i < len; ++i) {
      if (buf[i] != 0) {
        if (i + shift >= C_ORM_DECIMAL_MAX_DIGITS) {
          LOG_DEBUG("dec_round_from_buf: upward shift overflow");
          return C_ORM_ERROR_DECIMAL_OVERFLOW;
        }
        res.digits[i + shift] = buf[i];
      }
    }
  } else {
    size_t drop = cur_scale - (size_t)target_scale;
    unsigned char mid = buf[drop - 1];
    int sticky = extra_sticky ? 1 : 0;
    unsigned int inc = 0;

    for (i = 0; i + 1 < drop; ++i) {
      if (buf[i] != 0) {
        sticky = 1;
        break;
      }
    }
    if (mode == C_ORM_ROUND_ERROR_IF_INEXACT && (mid != 0 || sticky)) {
      return C_ORM_ERROR_DECIMAL_INEXACT;
    }
    if (mode == C_ORM_ROUND_HALF_UP) {
      if (mid >= 5) {
        inc = 1;
      }
    } else if (mode == C_ORM_ROUND_HALF_EVEN) {
      unsigned char lsb = (drop < len) ? buf[drop] : 0;
      if (mid > 5 || (mid == 5 && (sticky || (lsb & 1U) != 0U))) {
        inc = 1;
      }
    }

    for (i = drop; i < len; ++i) {
      if (buf[i] != 0) {
        if (i - drop >= C_ORM_DECIMAL_MAX_DIGITS) {
          LOG_DEBUG("dec_round_from_buf: mantissa overflow");
          return C_ORM_ERROR_DECIMAL_OVERFLOW;
        }
        res.digits[i - drop] = buf[i];
      }
    }

    if (inc != 0) {
      unsigned int carry = 1;
      for (i = 0; i < C_ORM_DECIMAL_MAX_DIGITS && carry != 0; ++i) {
        unsigned int sum = (unsigned int)res.digits[i] + carry;
        res.digits[i] = (unsigned char)(sum % 10U);
        carry = sum / 10U;
      }
      if (carry != 0) {
        LOG_DEBUG("dec_round_from_buf: carry overflow");
        return C_ORM_ERROR_DECIMAL_OVERFLOW;
      }
    }
  }

  *out_dec = res;
  return dec_normalize(out_dec);
}

C_ORM_EXPORT c_orm_error_t c_orm_decimal_zero(unsigned char scale,
                                              c_orm_decimal_t *out_dec) {
  if (!out_dec) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (scale > C_ORM_DECIMAL_MAX_DIGITS) {
    LOG_DEBUG("c_orm_decimal_zero: scale %u > 38", (unsigned int)scale);
    return C_ORM_ERROR_DECIMAL_OVERFLOW;
  }
  memset(out_dec, 0, sizeof(*out_dec));
  out_dec->scale = scale;
  out_dec->precision = (unsigned char)(scale > 0 ? scale : 1);
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_decimal_from_str(const char *str,
                                                  c_orm_decimal_t *out_dec) {
  const char *p;
  unsigned char sign = 0;
  unsigned char raw_digits[80];
  size_t num_raw = 0;
  size_t frac_digits = 0;
  int seen_dot = 0;
  int seen_digit = 0;
  long exp_val = 0;
  long eff_scale;
  size_t first_nz = 0;
  c_orm_decimal_t res;

  if (!str || !out_dec) {
    return C_ORM_ERROR_NULL_POINTER;
  }

  p = str;
  while (*p == 0x20 || *p == 0x09 || *p == 0x0D || *p == 0x0A) {
    ++p;
  }
  if (*p == '+') {
    ++p;
  } else if (*p == '-') {
    sign = 1;
    ++p;
  }

  while (*p != '\0') {
    if (*p >= '0' && *p <= '9') {
      seen_digit = 1;
      if (num_raw == 0 && *p == '0' && !seen_dot) {
        /* Skip leading integer zeros */
      } else {
        if (num_raw < sizeof(raw_digits)) {
          raw_digits[num_raw++] = (unsigned char)(*p - '0');
        } else {
          return C_ORM_ERROR_DECIMAL_OVERFLOW;
        }
      }
      if (seen_dot) {
        ++frac_digits;
        if (frac_digits > 76) {
          return C_ORM_ERROR_DECIMAL_OVERFLOW;
        }
      }
      ++p;
    } else if (*p == '.' && !seen_dot) {
      seen_dot = 1;
      ++p;
    } else {
      break;
    }
  }

  if (!seen_digit) {
    LOG_DEBUG("c_orm_decimal_from_str: no digits in input");
    return C_ORM_ERROR_DECIMAL_SYNTAX;
  }

  if (*p == 'e' || *p == 'E') {
    int exp_neg = 0;
    int exp_digits = 0;
    ++p;
    if (*p == '+') {
      ++p;
    } else if (*p == '-') {
      exp_neg = 1;
      ++p;
    }
    while (*p >= '0' && *p <= '9') {
      exp_digits = 1;
      if (exp_val <= 1000L) {
        exp_val = exp_val * 10L + (long)(*p - '0');
      }
      ++p;
    }
    if (!exp_digits) {
      return C_ORM_ERROR_DECIMAL_SYNTAX;
    }
    if (exp_neg) {
      exp_val = -exp_val;
    }
  }

  while (*p == 0x20 || *p == 0x09 || *p == 0x0D || *p == 0x0A) {
    ++p;
  }
  if (*p != '\0') {
    LOG_DEBUG("c_orm_decimal_from_str: trailing invalid char");
    return C_ORM_ERROR_DECIMAL_SYNTAX;
  }

  while (first_nz < num_raw && raw_digits[first_nz] == 0) {
    ++first_nz;
  }

  eff_scale = (long)frac_digits - exp_val;
  if (first_nz == num_raw) {
    unsigned char zscale = 0;
    if (eff_scale > 0) {
      if (eff_scale > C_ORM_DECIMAL_MAX_DIGITS) {
        return C_ORM_ERROR_DECIMAL_OVERFLOW;
      }
      zscale = (unsigned char)eff_scale;
    }
    return c_orm_decimal_zero(zscale, out_dec);
  }

  memset(&res, 0, sizeof(res));
  res.sign = sign;

  if (eff_scale < 0) {
    size_t pad_zeros = (size_t)(-eff_scale);
    size_t sig_len = num_raw - first_nz;
    size_t k;
    if (sig_len + pad_zeros > C_ORM_DECIMAL_MAX_DIGITS) {
      return C_ORM_ERROR_DECIMAL_OVERFLOW;
    }
    res.scale = 0;
    for (k = 0; k < sig_len; ++k) {
      res.digits[pad_zeros + k] = raw_digits[num_raw - 1 - k];
    }
  } else {
    size_t sig_len = num_raw - first_nz;
    size_t k;
    if (eff_scale > C_ORM_DECIMAL_MAX_DIGITS ||
        sig_len > C_ORM_DECIMAL_MAX_DIGITS) {
      return C_ORM_ERROR_DECIMAL_OVERFLOW;
    }
    res.scale = (unsigned char)eff_scale;
    for (k = 0; k < sig_len; ++k) {
      res.digits[k] = raw_digits[num_raw - 1 - k];
    }
  }

  *out_dec = res;
  return dec_normalize(out_dec);
}

C_ORM_EXPORT c_orm_error_t c_orm_decimal_to_str(const c_orm_decimal_t *dec,
                                                char *out_buf, size_t buf_size,
                                                size_t *out_len) {
  c_orm_error_t rc;
  int top = -1;
  int i;
  int msb_int;
  size_t pos = 0;
  size_t needed;

  if (!dec || !out_buf) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  rc = dec_validate(dec);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (buf_size == 0) {
    return C_ORM_ERROR_OVERFLOW;
  }

  for (i = C_ORM_DECIMAL_MAX_DIGITS - 1; i >= 0; --i) {
    if (dec->digits[i] != 0) {
      top = i;
      break;
    }
  }

  msb_int = (top >= (int)dec->scale) ? top : (int)dec->scale;
  needed = (size_t)(msb_int - (int)dec->scale + 1) + (size_t)dec->scale +
           (dec->scale > 0 ? 1U : 0U) + ((dec->sign && top >= 0) ? 1U : 0U) +
           1U;
  if (buf_size < needed) {
    return C_ORM_ERROR_OVERFLOW;
  }

  if (dec->sign && top >= 0) {
    out_buf[pos++] = '-';
  }
  for (i = msb_int; i >= (int)dec->scale; --i) {
    unsigned char d =
        (i < C_ORM_DECIMAL_MAX_DIGITS) ? dec->digits[i] : (unsigned char)0;
    out_buf[pos++] = (char)('0' + d);
  }
  if (dec->scale > 0) {
    out_buf[pos++] = '.';
    for (i = (int)dec->scale - 1; i >= 0; --i) {
      out_buf[pos++] = (char)('0' + dec->digits[i]);
    }
  }
  out_buf[pos] = '\0';
  if (out_len) {
    *out_len = pos;
  }
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_decimal_from_int64(c_orm_int64_t val,
                                                    c_orm_decimal_t *out_dec) {
  c_orm_uint64_t mag;
  size_t idx = 0;
  if (!out_dec) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  memset(out_dec, 0, sizeof(*out_dec));
  if (val < 0) {
    out_dec->sign = 1;
    mag = (c_orm_uint64_t)(-(val + 1)) + 1U;
  } else {
    mag = (c_orm_uint64_t)val;
  }
  while (mag > 0) {
    out_dec->digits[idx++] = (unsigned char)(mag % 10U);
    mag /= 10U;
  }
  return dec_normalize(out_dec);
}

C_ORM_EXPORT c_orm_error_t c_orm_decimal_to_int64(const c_orm_decimal_t *dec,
                                                  c_orm_round_mode_t mode,
                                                  c_orm_int64_t *out_val) {
  c_orm_error_t rc;
  c_orm_decimal_t rounded;
  c_orm_uint64_t limit;
  c_orm_uint64_t acc = 0;
  int i;

  if (!dec || !out_val) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  rc = dec_validate(dec);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = dec_round_from_buf(dec->digits, C_ORM_DECIMAL_MAX_DIGITS, dec->sign,
                          dec->scale, 0, 0, mode, &rounded);
  if (rc != C_ORM_OK) {
    return rc;
  }

  limit = rounded.sign ? (((c_orm_uint64_t)1 << 63))
                       : ((((c_orm_uint64_t)1 << 63)) - 1U);
  for (i = C_ORM_DECIMAL_MAX_DIGITS - 1; i >= 0; --i) {
    unsigned char d = rounded.digits[i];
    if (acc > (limit - (c_orm_uint64_t)d) / 10U) {
      return C_ORM_ERROR_DECIMAL_OVERFLOW;
    }
    acc = acc * 10U + (c_orm_uint64_t)d;
  }

  if (rounded.sign) {
    if (acc == ((c_orm_uint64_t)1 << 63)) {
      *out_val =
          -((c_orm_int64_t)(((c_orm_uint64_t)1 << 63) - 1U)) - (c_orm_int64_t)1;
    } else {
      *out_val = -(c_orm_int64_t)acc;
    }
  } else {
    *out_val = (c_orm_int64_t)acc;
  }
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_decimal_rescale(const c_orm_decimal_t *dec,
                                                 unsigned char target_precision,
                                                 unsigned char target_scale,
                                                 c_orm_round_mode_t mode,
                                                 c_orm_decimal_t *out_dec) {
  c_orm_error_t rc;
  c_orm_decimal_t tmp;
  size_t i;

  if (!dec || !out_dec) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  rc = dec_validate(dec);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (target_precision < 1 || target_precision > C_ORM_DECIMAL_MAX_DIGITS ||
      target_scale > target_precision) {
    return C_ORM_ERROR_VALIDATION;
  }
  rc = dec_round_from_buf(dec->digits, C_ORM_DECIMAL_MAX_DIGITS, dec->sign,
                          dec->scale, target_scale, 0, mode, &tmp);
  if (rc != C_ORM_OK) {
    return rc;
  }
  for (i = target_precision; i < C_ORM_DECIMAL_MAX_DIGITS; ++i) {
    if (tmp.digits[i] != 0) {
      return C_ORM_ERROR_DECIMAL_OVERFLOW;
    }
  }
  *out_dec = tmp;
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_decimal_is_zero(const c_orm_decimal_t *dec,
                                                 int *out_is_zero) {
  c_orm_error_t rc;
  size_t i;
  if (!dec || !out_is_zero) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  rc = dec_validate(dec);
  if (rc != C_ORM_OK) {
    return rc;
  }
  for (i = 0; i < C_ORM_DECIMAL_MAX_DIGITS; ++i) {
    if (dec->digits[i] != 0) {
      *out_is_zero = 0;
      return C_ORM_OK;
    }
  }
  *out_is_zero = 1;
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_decimal_negate(const c_orm_decimal_t *dec,
                                                c_orm_decimal_t *out_dec) {
  c_orm_error_t rc;
  int is_z = 0;
  c_orm_decimal_t tmp;
  if (!dec || !out_dec) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  rc = c_orm_decimal_is_zero(dec, &is_z);
  if (rc != C_ORM_OK) {
    return rc;
  }
  tmp = *dec;
  tmp.sign = is_z ? 0 : (unsigned char)(!tmp.sign);
  *out_dec = tmp;
  return dec_normalize(out_dec);
}

/**
 * @brief Compares absolute magnitudes of two validated decimals across
 * different scales.
 * @param a First decimal.
 * @param b Second decimal.
 * @param out_cmp Output pointer receiving -1, 0, or 1.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t dec_cmp_abs(const c_orm_decimal_t *a,
                                 const c_orm_decimal_t *b, int *out_cmp) {
  int max_int = C_ORM_DECIMAL_MAX_DIGITS;
  int p;
  for (p = max_int; p >= -((int)C_ORM_DECIMAL_MAX_DIGITS); --p) {
    int ia = p + (int)a->scale;
    int ib = p + (int)b->scale;
    unsigned char da =
        (ia >= 0 && ia < C_ORM_DECIMAL_MAX_DIGITS) ? a->digits[ia] : 0;
    unsigned char db =
        (ib >= 0 && ib < C_ORM_DECIMAL_MAX_DIGITS) ? b->digits[ib] : 0;
    if (da != db) {
      *out_cmp = (da < db) ? -1 : 1;
      return C_ORM_OK;
    }
  }
  *out_cmp = 0;
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_decimal_cmp(const c_orm_decimal_t *a,
                                             const c_orm_decimal_t *b,
                                             int *out_cmp) {
  c_orm_error_t rc;
  int za = 0;
  int zb = 0;
  int abs_cmp = 0;

  if (!a || !b || !out_cmp) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  rc = c_orm_decimal_is_zero(a, &za);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = c_orm_decimal_is_zero(b, &zb);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (za && zb) {
    *out_cmp = 0;
    return C_ORM_OK;
  }
  if (!za && a->sign && (zb || !b->sign)) {
    *out_cmp = -1;
    return C_ORM_OK;
  }
  if (!zb && b->sign && (za || !a->sign)) {
    *out_cmp = 1;
    return C_ORM_OK;
  }
  rc = dec_cmp_abs(a, b, &abs_cmp);
  if (!za && a->sign) {
    *out_cmp = -abs_cmp;
  } else {
    *out_cmp = abs_cmp;
  }
  return rc;
}

/**
 * @brief Internal signed addition/subtraction with exact scale alignment.
 * @param a Left operand.
 * @param b Right operand.
 * @param flip_b_sign Non-zero to compute a - b instead of a + b.
 * @param out_res Output decimal.
 * @return C_ORM_OK on success, or error code on overflow/validation failure.
 */
static c_orm_error_t dec_add_sub(const c_orm_decimal_t *a,
                                 const c_orm_decimal_t *b, int flip_b_sign,
                                 c_orm_decimal_t *out_res) {
  c_orm_error_t rc;
  unsigned char aa[78];
  unsigned char bb[78];
  unsigned char rr[78];
  unsigned char target_scale;
  unsigned char sa;
  unsigned char sb;
  size_t shift_a;
  size_t shift_b;
  size_t i;
  unsigned char res_sign = 0;

  if (!a || !b || !out_res) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  rc = dec_validate(a);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = dec_validate(b);
  if (rc != C_ORM_OK) {
    return rc;
  }

  target_scale = (a->scale >= b->scale) ? a->scale : b->scale;
  shift_a = (size_t)(target_scale - a->scale);
  shift_b = (size_t)(target_scale - b->scale);
  memset(aa, 0, sizeof(aa));
  memset(bb, 0, sizeof(bb));
  memset(rr, 0, sizeof(rr));

  for (i = 0; i < C_ORM_DECIMAL_MAX_DIGITS; ++i) {
    aa[i + shift_a] = a->digits[i];
    bb[i + shift_b] = b->digits[i];
  }

  sa = a->sign;
  sb = flip_b_sign ? (unsigned char)(!b->sign) : b->sign;

  if (sa == sb) {
    unsigned int carry = 0;
    res_sign = sa;
    for (i = 0; i < 77; ++i) {
      unsigned int s = (unsigned int)aa[i] + (unsigned int)bb[i] + carry;
      rr[i] = (unsigned char)(s % 10U);
      carry = s / 10U;
    }
    rr[77] = (unsigned char)carry;
  } else {
    int cmp = 0;
    int k;
    for (k = 77; k >= 0; --k) {
      if (aa[k] != bb[k]) {
        cmp = (aa[k] < bb[k]) ? -1 : 1;
        break;
      }
    }
    if (cmp == 0) {
      return c_orm_decimal_zero(target_scale, out_res);
    } else if (cmp > 0) {
      int borrow = 0;
      res_sign = sa;
      for (i = 0; i < 78; ++i) {
        int diff = (int)aa[i] - (int)bb[i] - borrow;
        if (diff < 0) {
          diff += 10;
          borrow = 1;
        } else {
          borrow = 0;
        }
        rr[i] = (unsigned char)diff;
      }
    } else {
      int borrow = 0;
      res_sign = sb;
      for (i = 0; i < 78; ++i) {
        int diff = (int)bb[i] - (int)aa[i] - borrow;
        if (diff < 0) {
          diff += 10;
          borrow = 1;
        } else {
          borrow = 0;
        }
        rr[i] = (unsigned char)diff;
      }
    }
  }

  return dec_round_from_buf(rr, 78, res_sign, target_scale, target_scale, 0,
                            C_ORM_ROUND_TRUNCATE, out_res);
}

C_ORM_EXPORT c_orm_error_t c_orm_decimal_add(const c_orm_decimal_t *a,
                                             const c_orm_decimal_t *b,
                                             c_orm_decimal_t *out_res) {
  return dec_add_sub(a, b, 0, out_res);
}

C_ORM_EXPORT c_orm_error_t c_orm_decimal_sub(const c_orm_decimal_t *a,
                                             const c_orm_decimal_t *b,
                                             c_orm_decimal_t *out_res) {
  return dec_add_sub(a, b, 1, out_res);
}

C_ORM_EXPORT c_orm_error_t c_orm_decimal_mul(const c_orm_decimal_t *a,
                                             const c_orm_decimal_t *b,
                                             unsigned char max_scale,
                                             c_orm_round_mode_t mode,
                                             c_orm_decimal_t *out_res) {
  c_orm_error_t rc;
  unsigned char prod[78];
  size_t raw_scale;
  unsigned char target_scale;
  size_t i;
  size_t j;

  if (!a || !b || !out_res) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (max_scale > C_ORM_DECIMAL_MAX_DIGITS) {
    return C_ORM_ERROR_VALIDATION;
  }
  rc = dec_validate(a);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = dec_validate(b);
  if (rc != C_ORM_OK) {
    return rc;
  }

  memset(prod, 0, sizeof(prod));
  for (i = 0; i < C_ORM_DECIMAL_MAX_DIGITS; ++i) {
    unsigned int carry = 0;
    if (a->digits[i] == 0) {
      continue;
    }
    for (j = 0; j < C_ORM_DECIMAL_MAX_DIGITS; ++j) {
      unsigned int p = (unsigned int)prod[i + j] +
                       (unsigned int)a->digits[i] * (unsigned int)b->digits[j] +
                       carry;
      prod[i + j] = (unsigned char)(p % 10U);
      carry = p / 10U;
    }
    prod[i + C_ORM_DECIMAL_MAX_DIGITS] =
        (unsigned char)(prod[i + C_ORM_DECIMAL_MAX_DIGITS] + carry);
  }

  raw_scale = (size_t)a->scale + (size_t)b->scale;
  target_scale =
      (raw_scale > (size_t)max_scale) ? max_scale : (unsigned char)raw_scale;
  return dec_round_from_buf(prod, 78, (unsigned char)(a->sign ^ b->sign),
                            raw_scale, target_scale, 0, mode, out_res);
}

/**
 * @brief Compares two 40-digit little-endian base-10 arrays.
 * @param x First 40-digit little-endian array.
 * @param y Second 40-digit little-endian array.
 * @param out_cmp Output pointer receiving -1 if x < y, 0 if x == y, 1 if x > y.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t limb40_cmp(const unsigned char x[40],
                                const unsigned char y[40], int *out_cmp) {
  int i;
  for (i = 39; i >= 0; --i) {
    if (x[i] != y[i]) {
      *out_cmp = (x[i] < y[i]) ? -1 : 1;
      return C_ORM_OK;
    }
  }
  *out_cmp = 0;
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_decimal_div(const c_orm_decimal_t *a,
                                             const c_orm_decimal_t *b,
                                             unsigned char result_scale,
                                             c_orm_round_mode_t mode,
                                             c_orm_decimal_t *out_res) {
  c_orm_error_t rc;
  int za = 0;
  int zb = 0;
  unsigned char num_be[120];
  size_t num_len = 0;
  unsigned char den[40];
  unsigned char rem[40];
  unsigned char quot_be[120];
  unsigned char quot_le[80];
  long shift_pow10;
  int top_a = 0;
  int i;
  size_t idx;
  int rem_nz = 0;

  if (!a || !b || !out_res) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (result_scale > C_ORM_DECIMAL_MAX_DIGITS ||
      (int)mode < (int)C_ORM_ROUND_TRUNCATE ||
      (int)mode > (int)C_ORM_ROUND_ERROR_IF_INEXACT) {
    return C_ORM_ERROR_VALIDATION;
  }
  rc = c_orm_decimal_is_zero(a, &za);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = c_orm_decimal_is_zero(b, &zb);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (zb) {
    LOG_DEBUG("c_orm_decimal_div: division by zero");
    return C_ORM_ERROR_DECIMAL_OVERFLOW;
  }
  if (za) {
    return c_orm_decimal_zero(result_scale, out_res);
  }

  /* Compute floor( (A * 10^(result_scale + 1 + b->scale - a->scale)) / B ) */
  shift_pow10 = (long)result_scale + 1L + (long)b->scale - (long)a->scale;
  top_a = C_ORM_DECIMAL_MAX_DIGITS - 1;
  while (a->digits[top_a] == 0) {
    --top_a;
  }

  memset(num_be, 0, sizeof(num_be));
  if (shift_pow10 >= 0) {
    for (i = top_a; i >= 0; --i) {
      num_be[num_len++] = a->digits[i];
    }
    for (idx = 0; idx < (size_t)shift_pow10; ++idx) {
      num_be[num_len++] = 0;
    }
  } else {
    size_t drop_right = (size_t)(-shift_pow10);
    for (idx = 0; idx < drop_right && idx < C_ORM_DECIMAL_MAX_DIGITS; ++idx) {
      if (a->digits[idx] != 0) {
        rem_nz = 1;
      }
    }
    if ((size_t)top_a >= drop_right) {
      for (i = top_a; i >= (int)drop_right; --i) {
        num_be[num_len++] = a->digits[i];
      }
    } else {
      num_be[num_len++] = 0;
    }
  }

  memset(den, 0, sizeof(den));
  memcpy(den, b->digits, C_ORM_DECIMAL_MAX_DIGITS);
  memset(rem, 0, sizeof(rem));
  memset(quot_be, 0, sizeof(quot_be));

  for (idx = 0; idx < num_len; ++idx) {
    unsigned char qdigit = 0;
    int d;
    for (i = 39; i > 0; --i) {
      rem[i] = rem[i - 1];
    }
    rem[0] = num_be[idx];

    for (d = 9; d >= 1; --d) {
      unsigned char prod[40];
      unsigned int carry = 0;
      int cmp = 0;
      int k;
      for (k = 0; k < 39; ++k) {
        unsigned int p = (unsigned int)den[k] * (unsigned int)d + carry;
        prod[k] = (unsigned char)(p % 10U);
        carry = p / 10U;
      }
      prod[39] = (unsigned char)carry;
      rc = limb40_cmp(rem, prod, &cmp);
      if (cmp >= 0) {
        int borrow = 0;
        qdigit = (unsigned char)d;
        for (k = 0; k < 40; ++k) {
          int diff = (int)rem[k] - (int)prod[k] - borrow;
          if (diff < 0) {
            diff += 10;
            borrow = 1;
          } else {
            borrow = 0;
          }
          rem[k] = (unsigned char)diff;
        }
        break;
      }
    }
    quot_be[idx] = qdigit;
  }

  for (i = 0; i < 40; ++i) {
    if (rem[i] != 0) {
      rem_nz = 1;
      break;
    }
  }

  /* Check for overflow above 40 little-endian slots */
  memset(quot_le, 0, sizeof(quot_le));
  for (idx = 0; idx < num_len; ++idx) {
    unsigned char d = quot_be[num_len - 1 - idx];
    if (idx >= 40U) {
      if (d != 0) {
        return C_ORM_ERROR_DECIMAL_OVERFLOW;
      }
    } else {
      quot_le[idx] = d;
    }
  }

  return dec_round_from_buf(quot_le, 40, (unsigned char)(a->sign ^ b->sign),
                            (size_t)result_scale + 1U, result_scale, rem_nz,
                            mode, out_res);
}

C_ORM_EXPORT c_orm_error_t c_orm_decimal_to_odbc_numeric(
    const c_orm_decimal_t *dec, unsigned char out_val16[16],
    unsigned char *out_precision, signed char *out_scale,
    unsigned char *out_odbc_sign) {
  c_orm_error_t rc;
  unsigned char acc[16];
  int i;
  size_t b;
  int is_z = 0;

  if (!dec || !out_val16 || !out_precision || !out_scale || !out_odbc_sign) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  rc = c_orm_decimal_is_zero(dec, &is_z);
  if (rc != C_ORM_OK) {
    return rc;
  }

  memset(acc, 0, sizeof(acc));
  for (i = C_ORM_DECIMAL_MAX_DIGITS - 1; i >= 0; --i) {
    unsigned int carry = (unsigned int)dec->digits[i];
    for (b = 0; b < 16; ++b) {
      unsigned int v = (unsigned int)acc[b] * 10U + carry;
      acc[b] = (unsigned char)(v & 0xFFU);
      carry = v >> 8;
    }
  }

  memcpy(out_val16, acc, 16);
  *out_precision = dec->precision;
  *out_scale = (signed char)dec->scale;
  *out_odbc_sign = (is_z || !dec->sign) ? (unsigned char)1 : (unsigned char)0;
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_decimal_from_odbc_numeric(
    const unsigned char val16[16], unsigned char precision, signed char scale,
    unsigned char odbc_sign, c_orm_decimal_t *out_dec) {
  unsigned char work[16];
  c_orm_decimal_t res;
  size_t idx = 0;

  if (!val16 || !out_dec) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (scale < 0 || scale > C_ORM_DECIMAL_MAX_DIGITS ||
      precision > C_ORM_DECIMAL_MAX_DIGITS || odbc_sign > 1) {
    return C_ORM_ERROR_VALIDATION;
  }

  memcpy(work, val16, 16);
  memset(&res, 0, sizeof(res));
  res.scale = (unsigned char)scale;
  res.sign = (odbc_sign == 0) ? (unsigned char)1 : (unsigned char)0;

  for (;;) {
    int any_nz = 0;
    unsigned int rem = 0;
    int b;
    for (b = 15; b >= 0; --b) {
      if (work[b] != 0) {
        any_nz = 1;
        break;
      }
    }
    if (!any_nz) {
      break;
    }
    if (idx >= C_ORM_DECIMAL_MAX_DIGITS) {
      LOG_DEBUG("c_orm_decimal_from_odbc_numeric: > 38 digits");
      return C_ORM_ERROR_DECIMAL_OVERFLOW;
    }
    for (b = 15; b >= 0; --b) {
      unsigned int cur = (rem << 8) | (unsigned int)work[b];
      work[b] = (unsigned char)(cur / 10U);
      rem = cur % 10U;
    }
    res.digits[idx++] = (unsigned char)rem;
  }

  {
    c_orm_error_t nrc = dec_normalize(&res);
    if (precision > res.precision) {
      res.precision = precision;
    }
    *out_dec = res;
    return nrc;
  }
}

C_ORM_EXPORT c_orm_error_t
c_orm_decimal_comp3_byte_len(unsigned char total_digits, size_t *out_bytes) {
  if (!out_bytes) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (total_digits < 1 || total_digits > C_ORM_DECIMAL_MAX_DIGITS) {
    return C_ORM_ERROR_VALIDATION;
  }
  *out_bytes = ((size_t)total_digits / 2U) + 1U;
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_decimal_to_comp3(
    const c_orm_decimal_t *dec, unsigned char pic_int_digits,
    unsigned char pic_frac_digits, int is_signed, unsigned char *out_buf,
    size_t buf_size, size_t *out_written) {
  c_orm_error_t rc;
  unsigned int total_digits;
  size_t byte_len;
  c_orm_decimal_t aligned;
  unsigned char sign_nib;
  size_t d_idx;
  size_t nib_pos;

  if (!dec || !out_buf) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  total_digits = (unsigned int)pic_int_digits + (unsigned int)pic_frac_digits;
  if (total_digits < 1U || total_digits > C_ORM_DECIMAL_MAX_DIGITS) {
    return C_ORM_ERROR_VALIDATION;
  }
  byte_len = ((size_t)total_digits / 2U) + 1U;
  if (buf_size < byte_len) {
    return C_ORM_ERROR_OVERFLOW;
  }

  rc = c_orm_decimal_rescale(dec, (unsigned char)total_digits, pic_frac_digits,
                             C_ORM_ROUND_TRUNCATE, &aligned);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (!is_signed && aligned.sign) {
    return C_ORM_ERROR_DECIMAL_OVERFLOW;
  }

  memset(out_buf, 0, byte_len);
  if (!is_signed) {
    sign_nib = 0x0FU;
  } else {
    sign_nib = aligned.sign ? 0x0DU : 0x0CU;
  }
  out_buf[byte_len - 1] = sign_nib;

  /* Place digits from right to left starting at high nybble of last byte */
  nib_pos = 1;
  for (d_idx = 0; d_idx < (size_t)total_digits; ++d_idx, ++nib_pos) {
    size_t b_idx = byte_len - 1U - (nib_pos / 2U);
    unsigned char d = aligned.digits[d_idx];
    if ((nib_pos & 1U) != 0U) {
      out_buf[b_idx] |= (unsigned char)(d << 4);
    } else {
      out_buf[b_idx] |= d;
    }
  }

  if (out_written) {
    *out_written = byte_len;
  }
  return rc;
}

C_ORM_EXPORT c_orm_error_t c_orm_decimal_from_comp3(
    const unsigned char *buf, size_t buf_len, unsigned char pic_frac_digits,
    c_orm_decimal_t *out_dec) {
  size_t total_nibs;
  unsigned char sign_nib;
  c_orm_decimal_t res;
  size_t nib_pos;

  if (!buf || !out_dec) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (buf_len == 0) {
    return C_ORM_ERROR_VALIDATION;
  }
  total_nibs = buf_len * 2U - 1U;
  if (total_nibs > C_ORM_DECIMAL_MAX_DIGITS + 1U ||
      pic_frac_digits > C_ORM_DECIMAL_MAX_DIGITS ||
      (size_t)pic_frac_digits > total_nibs) {
    return C_ORM_ERROR_DECIMAL_OVERFLOW;
  }

  sign_nib = (unsigned char)(buf[buf_len - 1U] & 0x0FU);
  memset(&res, 0, sizeof(res));
  res.scale = pic_frac_digits;
  if (sign_nib == 0x0AU || sign_nib == 0x0CU || sign_nib == 0x0EU ||
      sign_nib == 0x0FU) {
    res.sign = 0;
  } else if (sign_nib == 0x0BU || sign_nib == 0x0DU) {
    res.sign = 1;
  } else {
    LOG_DEBUG("c_orm_decimal_from_comp3: invalid sign nybble 0x%x",
              (unsigned int)sign_nib);
    return C_ORM_ERROR_DECIMAL_SYNTAX;
  }

  for (nib_pos = 1U; nib_pos <= total_nibs; ++nib_pos) {
    size_t b_idx = buf_len - 1U - (nib_pos / 2U);
    unsigned char d = ((nib_pos & 1U) != 0U)
                          ? (unsigned char)((buf[b_idx] >> 4) & 0x0FU)
                          : (unsigned char)(buf[b_idx] & 0x0FU);
    if (d > 9U) {
      LOG_DEBUG("c_orm_decimal_from_comp3: invalid BCD digit %u",
                (unsigned int)d);
      return C_ORM_ERROR_DECIMAL_SYNTAX;
    }
    if (nib_pos - 1U >= C_ORM_DECIMAL_MAX_DIGITS) {
      if (d != 0U) {
        return C_ORM_ERROR_DECIMAL_OVERFLOW;
      }
    } else {
      res.digits[nib_pos - 1U] = d;
    }
  }

  *out_dec = res;
  return dec_normalize(out_dec);
}

C_ORM_EXPORT c_orm_error_t c_orm_decimal_to_zoned(
    const c_orm_decimal_t *dec, unsigned char pic_int_digits,
    unsigned char pic_frac_digits, int is_signed, int sign_leading,
    int sign_separate, int use_ebcdic, unsigned char *out_buf, size_t buf_size,
    size_t *out_written) {
  c_orm_error_t rc;
  unsigned int total_digits;
  size_t byte_len;
  c_orm_decimal_t aligned;
  int neg;
  size_t digit_offset = 0;
  size_t i;

  if (!dec || !out_buf) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  total_digits = (unsigned int)pic_int_digits + (unsigned int)pic_frac_digits;
  if (total_digits < 1U || total_digits > C_ORM_DECIMAL_MAX_DIGITS) {
    return C_ORM_ERROR_VALIDATION;
  }
  if (!is_signed && sign_separate) {
    return C_ORM_ERROR_VALIDATION;
  }
  byte_len = (size_t)total_digits + ((is_signed && sign_separate) ? 1U : 0U);
  if (buf_size < byte_len) {
    return C_ORM_ERROR_OVERFLOW;
  }

  rc = c_orm_decimal_rescale(dec, (unsigned char)total_digits, pic_frac_digits,
                             C_ORM_ROUND_TRUNCATE, &aligned);
  if (rc != C_ORM_OK) {
    return rc;
  }
  neg = aligned.sign ? 1 : 0;
  if (!is_signed && neg) {
    return C_ORM_ERROR_DECIMAL_OVERFLOW;
  }

  if (is_signed && sign_separate && sign_leading) {
    out_buf[0] =
        use_ebcdic ? (neg ? 0x60U : 0x4EU) : (unsigned char)(neg ? '-' : '+');
    digit_offset = 1U;
  }

  for (i = 0; i < (size_t)total_digits; ++i) {
    unsigned char d = aligned.digits[total_digits - 1U - i];
    out_buf[digit_offset + i] =
        use_ebcdic ? (unsigned char)(0xF0U | d) : (unsigned char)('0' + d);
  }

  if (is_signed && sign_separate && !sign_leading) {
    out_buf[total_digits] =
        use_ebcdic ? (neg ? 0x60U : 0x4EU) : (unsigned char)(neg ? '-' : '+');
  } else if (is_signed && !sign_separate) {
    size_t s_pos = sign_leading ? 0U : ((size_t)total_digits - 1U);
    unsigned char sd = aligned.digits[total_digits - 1U - s_pos];
    if (use_ebcdic) {
      out_buf[s_pos] = (unsigned char)((neg ? 0xD0U : 0xC0U) | sd);
    } else if (neg) {
      out_buf[s_pos] =
          (sd == 0U) ? (unsigned char)'}' : (unsigned char)('J' + (sd - 1U));
    } else {
      out_buf[s_pos] =
          (sd == 0U) ? (unsigned char)'{' : (unsigned char)('A' + (sd - 1U));
    }
  }

  if (out_written) {
    *out_written = byte_len;
  }
  return rc;
}

C_ORM_EXPORT c_orm_error_t c_orm_decimal_from_zoned(
    const unsigned char *buf, size_t buf_len, unsigned char pic_frac_digits,
    int is_signed, int sign_leading, int sign_separate, int use_ebcdic,
    c_orm_decimal_t *out_dec) {
  size_t num_digits;
  size_t d_start = 0;
  c_orm_decimal_t res;
  size_t i;

  if (!buf || !out_dec) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (!is_signed && sign_separate) {
    return C_ORM_ERROR_VALIDATION;
  }
  if (buf_len < ((is_signed && sign_separate) ? 2U : 1U)) {
    return C_ORM_ERROR_VALIDATION;
  }
  num_digits = buf_len - ((is_signed && sign_separate) ? 1U : 0U);
  if (num_digits > C_ORM_DECIMAL_MAX_DIGITS ||
      (size_t)pic_frac_digits > num_digits) {
    return C_ORM_ERROR_DECIMAL_OVERFLOW;
  }

  memset(&res, 0, sizeof(res));
  res.scale = pic_frac_digits;

  if (is_signed && sign_separate) {
    size_t s_idx = sign_leading ? 0U : num_digits;
    unsigned char sb = buf[s_idx];
    d_start = sign_leading ? 1U : 0U;
    if (use_ebcdic) {
      if (sb == 0x4EU || sb == 0x40U) {
        res.sign = 0;
      } else if (sb == 0x60U) {
        res.sign = 1;
      } else {
        return C_ORM_ERROR_DECIMAL_SYNTAX;
      }
    } else {
      if (sb == (unsigned char)'+' || sb == (unsigned char)' ') {
        res.sign = 0;
      } else if (sb == (unsigned char)'-') {
        res.sign = 1;
      } else {
        return C_ORM_ERROR_DECIMAL_SYNTAX;
      }
    }
  }

  for (i = 0; i < num_digits; ++i) {
    unsigned char ch = buf[d_start + i];
    int is_overpunch_slot = (is_signed && !sign_separate &&
                             ((sign_leading && i == 0U) ||
                              (!sign_leading && i + 1U == num_digits)));
    unsigned char d = 0;

    if (use_ebcdic) {
      unsigned char zone = (unsigned char)(ch & 0xF0U);
      unsigned char low = (unsigned char)(ch & 0x0FU);
      if (low > 9U) {
        return C_ORM_ERROR_DECIMAL_SYNTAX;
      }
      if (zone == 0xF0U) {
        d = low;
      } else if (!is_overpunch_slot) {
        return C_ORM_ERROR_DECIMAL_SYNTAX;
      } else if (zone == 0xC0U || zone == 0xA0U || zone == 0xE0U) {
        res.sign = 0;
        d = low;
      } else if (zone == 0xD0U || zone == 0xB0U) {
        res.sign = 1;
        d = low;
      } else {
        return C_ORM_ERROR_DECIMAL_SYNTAX;
      }
    } else {
      if (ch >= (unsigned char)'0' && ch <= (unsigned char)'9') {
        d = (unsigned char)(ch - (unsigned char)'0');
      } else if (!is_overpunch_slot) {
        return C_ORM_ERROR_DECIMAL_SYNTAX;
      } else if (ch == (unsigned char)'{') {
        res.sign = 0;
        d = 0;
      } else if (ch >= (unsigned char)'A' && ch <= (unsigned char)'I') {
        res.sign = 0;
        d = (unsigned char)(1U + (ch - (unsigned char)'A'));
      } else if (ch == (unsigned char)'}') {
        res.sign = 1;
        d = 0;
      } else if (ch >= (unsigned char)'J' && ch <= (unsigned char)'R') {
        res.sign = 1;
        d = (unsigned char)(1U + (ch - (unsigned char)'J'));
      } else if (ch >= (unsigned char)'p' && ch <= (unsigned char)'y') {
        res.sign = 1;
        d = (unsigned char)(ch - (unsigned char)'p');
      } else {
        return C_ORM_ERROR_DECIMAL_SYNTAX;
      }
    }
    res.digits[num_digits - 1U - i] = d;
  }

  *out_dec = res;
  return dec_normalize(out_dec);
}

C_ORM_EXPORT c_orm_error_t c_orm_cobol_comp_byte_len(unsigned char pic_digits,
                                                     size_t *out_bytes) {
  if (!out_bytes) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (pic_digits >= 1 && pic_digits <= 4) {
    *out_bytes = 2U;
  } else if (pic_digits >= 5 && pic_digits <= 9) {
    *out_bytes = 4U;
  } else if (pic_digits >= 10 && pic_digits <= 18) {
    *out_bytes = 8U;
  } else {
    return C_ORM_ERROR_VALIDATION;
  }
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_int64_to_cobol_comp(c_orm_int64_t val,
                                                     size_t byte_width,
                                                     int is_big_endian,
                                                     unsigned char *out_buf,
                                                     size_t buf_size) {
  c_orm_uint64_t uval;
  size_t i;

  if (!out_buf) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (byte_width != 2U && byte_width != 4U && byte_width != 8U) {
    return C_ORM_ERROR_VALIDATION;
  }
  if (buf_size < byte_width) {
    return C_ORM_ERROR_OVERFLOW;
  }

  if (byte_width == 2U) {
    if (val < -(c_orm_int64_t)32768 || val > (c_orm_int64_t)65535) {
      return C_ORM_ERROR_DECIMAL_OVERFLOW;
    }
    if (!is_big_endian) {
      unsigned short v16 = (unsigned short)((c_orm_uint64_t)val & 0xFFFFU);
      memcpy(out_buf, &v16, 2U);
      return C_ORM_OK;
    }
  } else if (byte_width == 4U) {
    c_orm_int64_t i32_min = -((c_orm_int64_t)2147483647) - (c_orm_int64_t)1;
    c_orm_int64_t u32_max =
        ((c_orm_int64_t)2147483647) * (c_orm_int64_t)2 + (c_orm_int64_t)1;
    if (val < i32_min || val > u32_max) {
      return C_ORM_ERROR_DECIMAL_OVERFLOW;
    }
    if (!is_big_endian) {
      uint32_t v32 = (uint32_t)((c_orm_uint64_t)val & 0xFFFFFFFFUL);
      memcpy(out_buf, &v32, 4U);
      return C_ORM_OK;
    }
  } else if (!is_big_endian) {
    memcpy(out_buf, &val, 8U);
    return C_ORM_OK;
  }

  uval = (c_orm_uint64_t)val;
  for (i = 0; i < byte_width; ++i) {
    size_t shift = (byte_width - 1U - i) * 8U;
    out_buf[i] = (unsigned char)((uval >> shift) & 0xFFU);
  }
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_int64_from_cobol_comp(const unsigned char *buf,
                                                       size_t byte_width,
                                                       int is_signed,
                                                       int is_big_endian,
                                                       c_orm_int64_t *out_val) {
  c_orm_uint64_t uval = 0;
  size_t i;

  if (!buf || !out_val) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  if (byte_width != 2U && byte_width != 4U && byte_width != 8U) {
    return C_ORM_ERROR_VALIDATION;
  }

  if (is_big_endian) {
    for (i = 0; i < byte_width; ++i) {
      uval = (uval << 8) | (c_orm_uint64_t)buf[i];
    }
  } else if (byte_width == 2U) {
    unsigned short v16 = 0;
    memcpy(&v16, buf, 2U);
    uval = (c_orm_uint64_t)v16;
  } else if (byte_width == 4U) {
    uint32_t v32 = 0;
    memcpy(&v32, buf, 4U);
    uval = (c_orm_uint64_t)v32;
  } else {
    memcpy(&uval, buf, 8U);
  }

  if (!is_signed) {
    if (byte_width == 8U && (uval & ((c_orm_uint64_t)1 << 63)) != 0U) {
      return C_ORM_ERROR_DECIMAL_OVERFLOW;
    }
    *out_val = (c_orm_int64_t)uval;
  } else if (byte_width == 2U) {
    if ((uval & 0x8000U) != 0U) {
      *out_val = -(c_orm_int64_t)((~uval & 0xFFFFU) + 1U);
    } else {
      *out_val = (c_orm_int64_t)uval;
    }
  } else if (byte_width == 4U) {
    if ((uval & 0x80000000UL) != 0U) {
      *out_val = -(c_orm_int64_t)((~uval & 0xFFFFFFFFUL) + 1U);
    } else {
      *out_val = (c_orm_int64_t)uval;
    }
  } else {
    if ((uval & ((c_orm_uint64_t)1 << 63)) != 0U) {
      c_orm_uint64_t mag_minus_1 = ~uval;
      *out_val = -(c_orm_int64_t)mag_minus_1 - 1;
    } else {
      *out_val = (c_orm_int64_t)uval;
    }
  }
  return C_ORM_OK;
}
