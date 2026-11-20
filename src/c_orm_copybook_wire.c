/**
 * @file c_orm_copybook_wire.c
 * @brief Binary mainframe wire record serializer/deserializer (COMP-3, COMP,
 * COMP-5, Zoned Decimal, EBCDIC, UTF-16 GRAPHIC, DCLGEN VARCHAR, ODO) and
 * level-88 condition evaluator.
 */

/* clang-format off */
#include "c_orm_copybook.h"
#include "c_orm_db.h"
#include "c_orm_log.h"
#include "c_orm_safe_crt.h"
#include <stdlib.h>
#include <string.h>
/* clang-format on */

/**
 * @brief Internal neutral value carrier used while marshaling fields between
 * C89 structs / `cdd_c_abstract_struct_t` and COBOL wire buffers.
 */
typedef enum cpy_val_tag {
  /** @brief Unspecified / SQL NULL / use COBOL default. */
  CPY_VAL_EMPTY = 0,
  /** @brief Signed 64-bit integer. */
  CPY_VAL_INT64 = 1,
  /** @brief Double-precision floating point. */
  CPY_VAL_DOUBLE = 2,
  /** @brief Exact base-10 fixed-point decimal. */
  CPY_VAL_DECIMAL = 3,
  /** @brief UTF-8 string view. */
  CPY_VAL_STRING = 4,
  /** @brief Raw byte slice view. */
  CPY_VAL_BYTES = 5
} cpy_val_tag_t;

/**
 * @brief Neutral value payload for wire encoding.
 */
typedef struct cpy_wire_val {
  /** @brief Active tag discriminator. */
  cpy_val_tag_t tag;
  /** @brief Integer payload. */
  c_orm_int64_t i64;
  /** @brief Floating-point payload. */
  double dbl;
  /** @brief Fixed-point decimal payload. */
  c_orm_decimal_t dec;
  /** @brief Null-terminated UTF-8 string pointer. */
  const char *str;
  /** @brief Raw binary pointer. */
  const unsigned char *bytes;
  /** @brief Byte length for `CPY_VAL_BYTES`. */
  size_t bytes_len;
} cpy_wire_val_t;

/**
 * @brief Folds an ASCII lowercase byte to uppercase.
 * @param c Input byte.
 * @param out_c Output uppercase byte.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t wire_char_to_upper(unsigned char c, unsigned char *out_c) {
  *out_c = (c >= 'a' && c <= 'z') ? (unsigned char)(c - ('a' - 'A')) : c;
  return C_ORM_OK;
}

/**
 * @brief Case-insensitive identifier match treating `-` and `_` identically.
 * @param a First identifier.
 * @param b Second identifier.
 * @param out_eq Output flag set to 1 if matched, else 0.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t wire_ident_eq(const char *a, const char *b, int *out_eq) {
  size_t i = 0;
  *out_eq = 0;
  if (!a) {
    return C_ORM_OK;
  }
  while (a[i] != '\0' && b[i] != '\0') {
    unsigned char ca = 0;
    unsigned char cb = 0;
    wire_char_to_upper((unsigned char)a[i], &ca);
    wire_char_to_upper((unsigned char)b[i], &cb);
    if (ca == '-') {
      ca = '_';
    }
    if (cb == '-') {
      cb = '_';
    }
    if (ca != cb) {
      return C_ORM_OK;
    }
    i++;
  }
  if (a[i] == '\0' && b[i] == '\0') {
    *out_eq = 1;
  }
  return C_ORM_OK;
}

/**
 * @brief Locates a matching `c_orm_column_meta_t` in `meta` for COBOL field
 * `f`.
 * @param meta Table metadata (may be NULL).
 * @param f COBOL field node.
 * @param out_col Output matched column descriptor, or NULL if unmapped.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t wire_find_col_meta(const c_orm_table_meta_t *meta,
                                        const c_orm_cobol_field_t *f,
                                        const c_orm_column_meta_t **out_col) {
  size_t i;
  *out_col = NULL;
  if (!meta->columns) {
    return C_ORM_OK;
  }
  for (i = 0; i < meta->num_columns; ++i) {
    int eq1 = 0;
    int eq2 = 0;
    wire_ident_eq(meta->columns[i].name, f->c_name, &eq1);
    wire_ident_eq(meta->columns[i].name, f->cobol_name, &eq2);
    if (eq1 || eq2) {
      *out_col = &meta->columns[i];
      return C_ORM_OK;
    }
  }
  return C_ORM_OK;
}

/**
 * @brief Locates a matching `cdd_c_variant_t` in `abs_struct` for COBOL field
 * `f`.
 * @param abs_struct Abstract struct dictionary.
 * @param f COBOL field node.
 * @param out_var Output matched variant pointer, or NULL if absent.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t
wire_find_abs_val(const cdd_c_abstract_struct_t *abs_struct,
                  const c_orm_cobol_field_t *f,
                  const cdd_c_variant_t **out_var) {
  size_t i;
  *out_var = NULL;
  if (!abs_struct->kvs) {
    return C_ORM_OK;
  }
  for (i = 0; i < abs_struct->count; ++i) {
    int eq1 = 0;
    int eq2 = 0;
    wire_ident_eq(abs_struct->kvs[i].key, f->c_name, &eq1);
    wire_ident_eq(abs_struct->kvs[i].key, f->cobol_name, &eq2);
    if (eq1 || eq2) {
      *out_var = &abs_struct->kvs[i].value;
      return C_ORM_OK;
    }
  }
  return C_ORM_OK;
}

/**
 * @brief Checks whether `f` or any child inside `f` is actively mapped by the
 * caller's `meta` or `abs_struct`.
 * @param f Subtree root field.
 * @param meta Optional C struct metadata.
 * @param abs_struct Optional abstract struct.
 * @param out_mapped Output flag set to 1 if any field in subtree is mapped.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t wire_subtree_is_mapped(
    const c_orm_cobol_field_t *f, const c_orm_table_meta_t *meta,
    const cdd_c_abstract_struct_t *abs_struct, int *out_mapped) {
  const c_orm_column_meta_t *col = NULL;
  const cdd_c_variant_t *var = NULL;
  size_t i;
  *out_mapped = 0;
  if (meta) {
    wire_find_col_meta(meta, f, &col);
    if (col) {
      *out_mapped = 1;
      return C_ORM_OK;
    }
  }
  if (abs_struct) {
    wire_find_abs_val(abs_struct, f, &var);
    if (var) {
      *out_mapped = 1;
      return C_ORM_OK;
    }
  }
  for (i = 0; i < f->num_children; ++i) {
    wire_subtree_is_mapped(&f->children[i], meta, abs_struct, out_mapped);
    if (*out_mapped) {
      return C_ORM_OK;
    }
  }
  return C_ORM_OK;
}

/**
 * @brief Reads a neutral `cpy_wire_val_t` from a C89 struct slot described by
 * `col`.
 * @param c_struct Source C struct pointer.
 * @param col Column metadata descriptor.
 * @param out_val Output populated neutral value.
 * @return C_ORM_OK on success, or error code on invalid decimal string.
 */
static c_orm_error_t wire_read_struct_col(const void *c_struct,
                                          const c_orm_column_meta_t *col,
                                          cpy_wire_val_t *out_val) {
  const unsigned char *ptr = (const unsigned char *)c_struct + col->offset;
  memset(out_val, 0, sizeof(*out_val));

  switch (col->type) {
  case C_ORM_TYPE_INT32:
    if (col->is_nullable) {
      const int32_t *p = NULL;
      memcpy(&p, ptr, sizeof(p));
      if (!p) {
        return C_ORM_OK;
      }
      out_val->i64 = (c_orm_int64_t)(*p);
    } else {
      int32_t v32 = 0;
      memcpy(&v32, ptr, sizeof(v32));
      out_val->i64 = (c_orm_int64_t)v32;
    }
    out_val->tag = CPY_VAL_INT64;
    return C_ORM_OK;

  case C_ORM_TYPE_INT64:
    if (col->is_nullable) {
      const c_orm_int64_t *p = NULL;
      memcpy(&p, ptr, sizeof(p));
      if (!p) {
        return C_ORM_OK;
      }
      out_val->i64 = *p;
    } else {
      c_orm_int64_t v64 = 0;
      memcpy(&v64, ptr, sizeof(v64));
      out_val->i64 = v64;
    }
    out_val->tag = CPY_VAL_INT64;
    return C_ORM_OK;

  case C_ORM_TYPE_BOOL:
    if (col->is_nullable) {
      const unsigned char *p = NULL;
      memcpy(&p, ptr, sizeof(p));
      if (!p) {
        return C_ORM_OK;
      }
      out_val->i64 = (*p != 0) ? 1 : 0;
    } else {
      out_val->i64 = (*ptr != 0) ? 1 : 0;
    }
    out_val->tag = CPY_VAL_INT64;
    return C_ORM_OK;

  case C_ORM_TYPE_FLOAT:
    if (col->is_nullable) {
      const float *p = NULL;
      memcpy(&p, ptr, sizeof(p));
      if (!p) {
        return C_ORM_OK;
      }
      out_val->dbl = (double)(*p);
    } else {
      float fv = 0.0f;
      memcpy(&fv, ptr, sizeof(fv));
      out_val->dbl = (double)fv;
    }
    out_val->tag = CPY_VAL_DOUBLE;
    return C_ORM_OK;

  case C_ORM_TYPE_DOUBLE:
    if (col->is_nullable) {
      const double *p = NULL;
      memcpy(&p, ptr, sizeof(p));
      if (!p) {
        return C_ORM_OK;
      }
      out_val->dbl = *p;
    } else {
      double dv = 0.0;
      memcpy(&dv, ptr, sizeof(dv));
      out_val->dbl = dv;
    }
    out_val->tag = CPY_VAL_DOUBLE;
    return C_ORM_OK;

  case C_ORM_TYPE_DECIMAL:
  case C_ORM_TYPE_DECFLOAT:
    if (col->is_nullable) {
      const c_orm_decimal_t *p = NULL;
      memcpy(&p, ptr, sizeof(p));
      if (!p) {
        return C_ORM_OK;
      }
      out_val->dec = *p;
    } else {
      memcpy(&out_val->dec, ptr, sizeof(out_val->dec));
    }
    out_val->tag = CPY_VAL_DECIMAL;
    return C_ORM_OK;

  case C_ORM_TYPE_CHAR_FIXED:
    if (col->fixed_length > 0) {
      out_val->str = (const char *)ptr;
      out_val->tag = CPY_VAL_STRING;
      return C_ORM_OK;
    }
    /* Fallthrough for pointer CHAR_FIXED */
    memcpy(&out_val->str, ptr, sizeof(out_val->str));
    if (out_val->str) {
      out_val->tag = CPY_VAL_STRING;
    }
    return C_ORM_OK;

  case C_ORM_TYPE_STRING:
  case C_ORM_TYPE_DATE:
  case C_ORM_TYPE_TIME:
  case C_ORM_TYPE_TIMESTAMP:
  case C_ORM_TYPE_GRAPHIC:
  case C_ORM_TYPE_VARGRAPHIC:
    memcpy(&out_val->str, ptr, sizeof(out_val->str));
    if (out_val->str) {
      out_val->tag = CPY_VAL_STRING;
    }
    return C_ORM_OK;

  case C_ORM_TYPE_BINARY_FIXED:
    if (col->fixed_length > 0) {
      out_val->bytes = ptr;
      out_val->bytes_len = col->fixed_length;
      out_val->tag = CPY_VAL_BYTES;
      return C_ORM_OK;
    }
    {
      c_orm_blob_t b;
      memcpy(&b, ptr, sizeof(b));
      if (b.data) {
        out_val->bytes = (const unsigned char *)b.data;
        out_val->bytes_len = b.size;
        out_val->tag = CPY_VAL_BYTES;
      }
    }
    return C_ORM_OK;

  case C_ORM_TYPE_BLOB:
  case C_ORM_TYPE_VARBINARY: {
    c_orm_blob_t b;
    memcpy(&b, ptr, sizeof(b));
    if (b.data) {
      out_val->bytes = (const unsigned char *)b.data;
      out_val->bytes_len = b.size;
      out_val->tag = CPY_VAL_BYTES;
    }
    return C_ORM_OK;
  }

  default:
    return C_ORM_OK;
  }
}

/**
 * @brief Converts a `cdd_c_variant_t` into a neutral `cpy_wire_val_t`.
 * @param var Source variant.
 * @param out_val Output neutral value.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t wire_read_variant(const cdd_c_variant_t *var,
                                       cpy_wire_val_t *out_val) {
  memset(out_val, 0, sizeof(*out_val));
  switch (var->type) {
  case CDD_C_VARIANT_TYPE_INT:
    out_val->tag = CPY_VAL_INT64;
    out_val->i64 = var->value.i_val;
    break;
  case CDD_C_VARIANT_TYPE_FLOAT:
    out_val->tag = CPY_VAL_DOUBLE;
    out_val->dbl = var->value.f_val;
    break;
  case CDD_C_VARIANT_TYPE_DECIMAL:
    out_val->tag = CPY_VAL_DECIMAL;
    out_val->dec = var->value.d_val;
    break;
  case CDD_C_VARIANT_TYPE_STRING:
    if (var->value.s_val) {
      out_val->tag = CPY_VAL_STRING;
      out_val->str = var->value.s_val;
    }
    break;
  case CDD_C_VARIANT_TYPE_BLOB:
    if (var->value.b_val.data) {
      out_val->tag = CPY_VAL_BYTES;
      out_val->bytes = var->value.b_val.data;
      out_val->bytes_len = var->value.b_val.size;
    }
    break;
  default:
    break;
  }
  return C_ORM_OK;
}

/**
 * @brief Coerces a `cpy_wire_val_t` (or field `VALUE` default) into a
 * `c_orm_decimal_t`.
 * @param f COBOL field descriptor.
 * @param val Neutral input value.
 * @param out_dec Output decimal value.
 * @return C_ORM_OK on success, or decimal error code.
 */
static c_orm_error_t wire_val_to_decimal(const c_orm_cobol_field_t *f,
                                         const cpy_wire_val_t *val,
                                         c_orm_decimal_t *out_dec) {
  char buf[64];
  c_orm_error_t rc;
  switch (val->tag) {
  case CPY_VAL_DECIMAL:
    *out_dec = val->dec;
    return C_ORM_OK;
  case CPY_VAL_INT64:
    return c_orm_decimal_from_int64(val->i64, out_dec);
  case CPY_VAL_DOUBLE:
    rc = C_ORM_SPRINTF(buf, sizeof(buf), "%.12g", val->dbl);
    if (rc != C_ORM_OK) {
      return rc;
    }
    return c_orm_decimal_from_str(buf, out_dec);
  case CPY_VAL_STRING:
    return c_orm_decimal_from_str(val->str, out_dec);
  default:
    if (f->value_kind == C_ORM_COBOL_VALUE_NUMERIC) {
      return c_orm_decimal_from_str(f->default_value, out_dec);
    }
    return c_orm_decimal_zero(f->frac_digits, out_dec);
  }
}

/**
 * @brief Coerces a `cpy_wire_val_t` into an unscaled `c_orm_int64_t` (scaling
 * fractional digits by 10^frac_digits when `f->frac_digits > 0`).
 * @param f COBOL field descriptor.
 * @param val Neutral input value.
 * @param out_i64 Output unscaled 64-bit integer.
 * @return C_ORM_OK on success, or decimal overflow/syntax error.
 */
static c_orm_error_t wire_val_to_unscaled_int64(const c_orm_cobol_field_t *f,
                                                const cpy_wire_val_t *val,
                                                c_orm_int64_t *out_i64) {
  c_orm_decimal_t dec;
  c_orm_decimal_t scaled;
  unsigned char k;
  c_orm_error_t rc;

  if (f->frac_digits == 0 && val->tag == CPY_VAL_INT64) {
    *out_i64 = val->i64;
    return C_ORM_OK;
  }
  rc = wire_val_to_decimal(f, val, &dec);
  if (rc != C_ORM_OK) {
    return rc;
  }
  rc = c_orm_decimal_rescale(&dec, f->total_digits, f->frac_digits,
                             C_ORM_ROUND_TRUNCATE, &scaled);
  if (rc != C_ORM_OK) {
    return rc;
  }
  /* Shift decimal point right by frac_digits (total_digits <= 18) */
  if (scaled.scale > 0) {
    c_orm_decimal_t ten;
    c_orm_decimal_from_int64(10, &ten);
    for (k = 0; k < f->frac_digits; ++k) {
      c_orm_decimal_t next_d;
      c_orm_decimal_mul(&scaled, &ten, 0, C_ORM_ROUND_TRUNCATE, &next_d);
      scaled = next_d;
    }
  }
  return c_orm_decimal_to_int64(&scaled, C_ORM_ROUND_TRUNCATE, out_i64);
}

/**
 * @brief Determines whether host byte order differs from requested wire
 * endianness `big_endian_comp`.
 * @param big_endian_comp Non-zero when wire format is big-endian.
 * @param out_swap Output flag set to 1 when bytes must be reversed, else 0.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t wire_need_byte_swap(int big_endian_comp, int *out_swap) {
  unsigned short probe = 0x0102U;
  const unsigned char *p = (const unsigned char *)&probe;
  *out_swap = ((big_endian_comp != 0) != (p[0] == 0x01U)) ? 1 : 0;
  return C_ORM_OK;
}

/**
 * @brief Encodes a single elementary COBOL item at `dst`
 * (`f->element_byte_size` bytes).
 * @param f Elementary COBOL field descriptor.
 * @param val Neutral input value.
 * @param cp Target wire codepage.
 * @param big_endian_comp Non-zero for big-endian `COMP` / `GRAPHIC` / floats.
 * @param dst Destination wire slice of `f->element_byte_size` bytes.
 * @return C_ORM_OK on success, or specific error code.
 */
static c_orm_error_t wire_encode_elementary(const c_orm_cobol_field_t *f,
                                            const cpy_wire_val_t *val,
                                            c_orm_codepage_t cp,
                                            int big_endian_comp,
                                            unsigned char *dst) {
  c_orm_error_t rc;
  int use_ebcdic = (cp >= C_ORM_CP_IBM037) ? 1 : 0;

  if (f->is_filler) {
    if (f->value_kind == C_ORM_COBOL_VALUE_LOW_VALUES) {
      memset(dst, 0x00, f->element_byte_size);
      return C_ORM_OK;
    }
    if (f->value_kind == C_ORM_COBOL_VALUE_HIGH_VALUES) {
      memset(dst, 0xFF, f->element_byte_size);
      return C_ORM_OK;
    }
    if (f->value_kind == C_ORM_COBOL_VALUE_STRING) {
      return c_orm_cobol_format_alpha(f->default_value, f->element_byte_size,
                                      cp, 1, f->is_justified_right, dst,
                                      f->element_byte_size);
    }
    if (f->value_kind != C_ORM_COBOL_VALUE_ZEROS &&
        f->pic_category != C_ORM_COBOL_PIC_NUMERIC) {
      memset(dst, use_ebcdic ? 0x40 : 0x20, f->element_byte_size);
      return C_ORM_OK;
    }
  }

  switch (f->usage) {
  case C_ORM_COBOL_USAGE_COMP_3: {
    c_orm_decimal_t dec;
    rc = wire_val_to_decimal(f, val, &dec);
    if (rc != C_ORM_OK) {
      return rc;
    }
    return c_orm_decimal_to_comp3(&dec, f->int_digits, f->frac_digits,
                                  f->is_signed, dst, f->element_byte_size,
                                  NULL);
  }

  case C_ORM_COBOL_USAGE_COMP:
  case C_ORM_COBOL_USAGE_COMP_5: {
    c_orm_int64_t iv = 0;
    int is_be = (f->usage == C_ORM_COBOL_USAGE_COMP) ? big_endian_comp : 0;
    rc = wire_val_to_unscaled_int64(f, val, &iv);
    if (rc != C_ORM_OK) {
      return rc;
    }
    if (!f->is_signed && iv < 0) {
      return C_ORM_ERROR_DECIMAL_OVERFLOW;
    }
    return c_orm_int64_to_cobol_comp(iv, f->element_byte_size, is_be, dst,
                                     f->element_byte_size);
  }

  case C_ORM_COBOL_USAGE_COMP_1: {
    float fv = 0.0f;
    int swap = 0;
    unsigned char raw4[4];
    if (val->tag == CPY_VAL_DOUBLE) {
      fv = (float)val->dbl;
    } else if (val->tag == CPY_VAL_INT64) {
      fv = (float)val->i64;
    } else if (val->tag == CPY_VAL_DECIMAL) {
      char sbuf[64];
      c_orm_decimal_to_str(&val->dec, sbuf, sizeof(sbuf), NULL);
      fv = (float)atof(sbuf);
    }
    memcpy(raw4, &fv, 4);
    wire_need_byte_swap(big_endian_comp, &swap);
    if (swap) {
      dst[0] = raw4[3];
      dst[1] = raw4[2];
      dst[2] = raw4[1];
      dst[3] = raw4[0];
    } else {
      memcpy(dst, raw4, 4);
    }
    return C_ORM_OK;
  }

  case C_ORM_COBOL_USAGE_COMP_2: {
    double dv = 0.0;
    int swap = 0;
    unsigned char raw8[8];
    size_t k;
    if (val->tag == CPY_VAL_DOUBLE) {
      dv = val->dbl;
    } else if (val->tag == CPY_VAL_INT64) {
      dv = (double)val->i64;
    } else if (val->tag == CPY_VAL_DECIMAL) {
      char sbuf[64];
      c_orm_decimal_to_str(&val->dec, sbuf, sizeof(sbuf), NULL);
      dv = atof(sbuf);
    }
    memcpy(raw8, &dv, 8);
    wire_need_byte_swap(big_endian_comp, &swap);
    if (swap) {
      for (k = 0; k < 8; ++k) {
        dst[k] = raw8[7 - k];
      }
    } else {
      memcpy(dst, raw8, 8);
    }
    return C_ORM_OK;
  }

  case C_ORM_COBOL_USAGE_DISPLAY_1: {
    const char *s =
        (val->tag == CPY_VAL_STRING)
            ? val->str
            : ((f->value_kind == C_ORM_COBOL_VALUE_STRING) ? f->default_value
                                                           : "");
    size_t cap_units = f->display_length;
    unsigned short *u16 = NULL;
    size_t written_units = 0;
    size_t k;

    rc = c_orm_system_malloc((cap_units + 1) * sizeof(unsigned short),
                             (void **)&u16);
    if (rc != C_ORM_OK) {
      return rc;
    }
    rc = c_orm_utf8_to_utf16(s, strlen(s), big_endian_comp, u16, cap_units + 1,
                             &written_units);
    if (rc == C_ORM_ERROR_OVERFLOW) {
      (void)c_orm_system_free(u16);
      return C_ORM_ERROR_TRUNCATION;
    }
    if (rc != C_ORM_OK) {
      (void)c_orm_system_free(u16);
      return rc;
    }
    if (written_units > 0) {
      memcpy(dst, u16, written_units * 2);
    }
    (void)c_orm_system_free(u16);
    for (k = written_units; k < cap_units; ++k) {
      dst[k * 2] = big_endian_comp ? 0x00U : 0x20U;
      dst[k * 2 + 1] = big_endian_comp ? 0x20U : 0x00U;
    }
    return C_ORM_OK;
  }

  default:
    if (f->pic_category == C_ORM_COBOL_PIC_NUMERIC) {
      c_orm_decimal_t dec;
      int is_z = 0;
      rc = wire_val_to_decimal(f, val, &dec);
      if (rc != C_ORM_OK) {
        return rc;
      }
      if (f->is_blank_when_zero) {
        c_orm_decimal_is_zero(&dec, &is_z);
        if (is_z) {
          memset(dst, use_ebcdic ? 0x40 : 0x20, f->element_byte_size);
          return C_ORM_OK;
        }
      }
      return c_orm_decimal_to_zoned(
          &dec, f->int_digits, f->frac_digits, f->is_signed, f->sign_leading,
          f->sign_separate, use_ebcdic, dst, f->element_byte_size, NULL);
    }
    if (f->sql_for_bit_data || val->tag == CPY_VAL_BYTES) {
      memset(dst, 0, f->element_byte_size);
      if (val->tag == CPY_VAL_BYTES) {
        if (val->bytes_len > f->element_byte_size) {
          return C_ORM_ERROR_TRUNCATION;
        }
        memcpy(dst, val->bytes, val->bytes_len);
      } else if (val->tag == CPY_VAL_STRING) {
        size_t sl = strlen(val->str);
        if (sl > f->element_byte_size) {
          return C_ORM_ERROR_TRUNCATION;
        }
        memcpy(dst, val->str, sl);
      }
      return C_ORM_OK;
    }
    {
      char num_str[64];
      const char *s = "";
      if (val->tag == CPY_VAL_STRING) {
        s = val->str;
      } else if (val->tag == CPY_VAL_DECIMAL) {
        c_orm_decimal_to_str(&val->dec, num_str, sizeof(num_str), NULL);
        s = num_str;
      } else if (val->tag == CPY_VAL_INT64) {
        rc = C_ORM_SPRINTF(num_str, sizeof(num_str), C_ORM_INT64_FORMAT,
                           val->i64);
        if (rc != C_ORM_OK) {
          return rc;
        }
        s = num_str;
      } else if (f->value_kind == C_ORM_COBOL_VALUE_STRING) {
        s = f->default_value;
      }
      return c_orm_cobol_format_alpha(s, f->element_byte_size, cp, 0,
                                      f->is_justified_right, dst,
                                      f->element_byte_size);
    }
  }
}

/**
 * @brief Encodes an IBM Db2 `DCLGEN` `49`-level `VARCHAR` or `VARGRAPHIC` group
 * (`LEN PIC S9(4) COMP` + `TEXT PIC X/G(n)`).
 * @param f Group COBOL field with `is_dclgen_varchar != 0`.
 * @param val Neutral input value.
 * @param cp Target wire codepage.
 * @param big_endian_comp Non-zero for big-endian `COMP` / `GRAPHIC`.
 * @param dst Destination slice at `f->byte_offset`.
 * @return C_ORM_OK on success, or error code.
 */
static c_orm_error_t wire_encode_dclgen_varchar(const c_orm_cobol_field_t *f,
                                                const cpy_wire_val_t *val,
                                                c_orm_codepage_t cp,
                                                int big_endian_comp,
                                                unsigned char *dst) {
  const char *s = (val->tag == CPY_VAL_STRING) ? val->str : "";
  const c_orm_cobol_field_t *len_f = &f->children[0];
  const c_orm_cobol_field_t *txt_f = &f->children[1];
  int len_be = (len_f->usage == C_ORM_COBOL_USAGE_COMP) ? big_endian_comp : 0;
  unsigned char *txt_dst = dst + (txt_f->byte_offset - f->byte_offset);
  c_orm_error_t rc;

  if (f->is_dclgen_vargraphic) {
    unsigned short *u16 = NULL;
    size_t units = 0;
    size_t k;

    rc = c_orm_system_malloc((f->dclgen_max_chars + 1) * sizeof(unsigned short),
                             (void **)&u16);
    if (rc != C_ORM_OK) {
      return rc;
    }
    rc = c_orm_utf8_to_utf16(s, strlen(s), big_endian_comp, u16,
                             f->dclgen_max_chars + 1, &units);
    if (rc == C_ORM_ERROR_OVERFLOW) {
      (void)c_orm_system_free(u16);
      return C_ORM_ERROR_TRUNCATION;
    }
    if (rc != C_ORM_OK) {
      (void)c_orm_system_free(u16);
      return rc;
    }
    if (units > 0) {
      memcpy(txt_dst, u16, units * 2);
    }
    (void)c_orm_system_free(u16);
    for (k = units; k < f->dclgen_max_chars; ++k) {
      txt_dst[k * 2] = big_endian_comp ? 0x00U : 0x20U;
      txt_dst[k * 2 + 1] = big_endian_comp ? 0x20U : 0x00U;
    }
    return c_orm_int64_to_cobol_comp((c_orm_int64_t)units,
                                     len_f->element_byte_size, len_be, dst,
                                     len_f->element_byte_size);
  }

  {
    size_t written = 0;
    unsigned char sp = (cp >= C_ORM_CP_IBM037) ? 0x40U : 0x20U;
    memset(txt_dst, sp, txt_f->element_byte_size);
    rc = c_orm_utf8_to_ebcdic(cp, s, strlen(s), txt_dst,
                              txt_f->element_byte_size, &written);
    if (rc == C_ORM_ERROR_OVERFLOW) {
      return C_ORM_ERROR_TRUNCATION;
    }
    if (rc != C_ORM_OK) {
      return rc;
    }
    return c_orm_int64_to_cobol_comp((c_orm_int64_t)written,
                                     len_f->element_byte_size, len_be, dst,
                                     len_f->element_byte_size);
  }
}

/**
 * @brief Checks if an elementary `OCCURS` item uses `c_orm_decimal_t` in
 * `c_orm_cobol_odo_array_t`.
 * @param f Elementary COBOL field.
 * @param out_is_dec Output flag set to 1 if element type is `c_orm_decimal_t`.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t wire_field_is_decimal_elem(const c_orm_cobol_field_t *f,
                                                int *out_is_dec) {
  *out_is_dec = (f->frac_digits > 0 || f->total_digits > 18 ||
                 f->usage == C_ORM_COBOL_USAGE_COMP_3)
                    ? 1
                    : 0;
  return C_ORM_OK;
}

/**
 * @brief Resolves the active count of an `OCCURS` field from `c_struct` or
 * `abs_struct` when `odo_field_name` is set.
 * @param rec Enclosing record.
 * @param f Field with `has_occurs != 0`.
 * @param meta Optional C struct metadata.
 * @param c_struct Optional C struct pointer.
 * @param abs_struct Optional abstract struct pointer.
 * @param out_count Output validated occurrence count
 * (`min_occurs..max_occurs`).
 * @return C_ORM_OK on success, or C_ORM_ERROR_VALIDATION if ODO out of range.
 */
static c_orm_error_t wire_resolve_occurs_count(
    const c_orm_cobol_record_t *rec, const c_orm_cobol_field_t *f,
    const c_orm_table_meta_t *meta, const void *c_struct,
    const cdd_c_abstract_struct_t *abs_struct, size_t *out_count) {
  const c_orm_cobol_field_t *odo_f = NULL;
  cpy_wire_val_t v;
  c_orm_int64_t cnt = 0;
  c_orm_error_t rc;

  if (!f->has_occurs) {
    *out_count = 1;
    return C_ORM_OK;
  }
  if (!f->odo_field_name) {
    *out_count = f->max_occurs;
    return C_ORM_OK;
  }
  rc = c_orm_copybook_find_field(rec, f->odo_field_name, &odo_f);
  if (rc != C_ORM_OK) {
    return C_ORM_ERROR_VALIDATION;
  }
  memset(&v, 0, sizeof(v));
  if (c_struct) {
    const c_orm_column_meta_t *col = NULL;
    wire_find_col_meta(meta, odo_f, &col);
    if (col) {
      wire_read_struct_col(c_struct, col, &v);
    }
  } else {
    const cdd_c_variant_t *var = NULL;
    wire_find_abs_val(abs_struct, odo_f, &var);
    if (var) {
      wire_read_variant(var, &v);
    }
  }
  rc = wire_val_to_unscaled_int64(odo_f, &v, &cnt);
  if (rc != C_ORM_OK || cnt < (c_orm_int64_t)f->min_occurs ||
      cnt > (c_orm_int64_t)f->max_occurs) {
    return C_ORM_ERROR_VALIDATION;
  }
  *out_count = (size_t)cnt;
  return C_ORM_OK;
}

/**
 * @brief Computes effective wire record byte length accounting for any
 * variable-length `OCCURS DEPENDING ON` array.
 * @param rec Parsed Copybook record.
 * @param meta Optional C struct metadata.
 * @param c_struct Optional C struct pointer.
 * @param abs_struct Optional abstract struct pointer.
 * @param out_len Output effective wire byte length.
 * @return C_ORM_OK on success, or C_ORM_ERROR_VALIDATION if ODO out of bounds.
 */
static c_orm_error_t wire_compute_active_record_len(
    const c_orm_cobol_record_t *rec, const c_orm_table_meta_t *meta,
    const void *c_struct, const cdd_c_abstract_struct_t *abs_struct,
    size_t *out_len) {
  size_t total = rec->total_byte_size;
  size_t i;
  c_orm_error_t rc;
  for (i = 0; i < rec->num_fields; ++i) {
    const c_orm_cobol_field_t *f = &rec->fields[i];
    if (f->has_occurs && f->odo_field_name && !f->redefines_target) {
      size_t active_n = 0;
      rc = wire_resolve_occurs_count(rec, f, meta, c_struct, abs_struct,
                                     &active_n);
      if (rc != C_ORM_OK) {
        return rc;
      }
      total -= (f->max_occurs - active_n) * f->element_byte_size;
    }
  }
  *out_len = total;
  return C_ORM_OK;
}

/**
 * @brief Recursively packs sibling COBOL fields into `out_buf`.
 * @param rec Top-level record AST.
 * @param fields Array of sibling fields.
 * @param count Number of sibling fields.
 * @param base_shift Byte offset shift applied when inside an `OCCURS` group.
 * @param meta Optional C struct metadata.
 * @param c_struct Optional C struct source.
 * @param abs_struct Optional abstract struct source.
 * @param cp Target wire codepage.
 * @param big_endian_comp Non-zero for big-endian `COMP` / `GRAPHIC`.
 * @param out_buf Destination wire buffer.
 * @return C_ORM_OK on success, or error code.
 */
static c_orm_error_t wire_pack_fields_rec(
    const c_orm_cobol_record_t *rec, const c_orm_cobol_field_t *fields,
    size_t count, size_t base_shift, const c_orm_table_meta_t *meta,
    const void *c_struct, const cdd_c_abstract_struct_t *abs_struct,
    c_orm_codepage_t cp, int big_endian_comp, unsigned char *out_buf) {
  size_t i;
  c_orm_error_t rc;

  for (i = 0; i < count; ++i) {
    const c_orm_cobol_field_t *f = &fields[i];
    size_t occ_count = 1;
    size_t occ_i;

    if (f->level == 66) {
      continue;
    }
    if (f->redefines_target) {
      int base_mapped = 0;
      int redef_mapped = 0;
      wire_subtree_is_mapped(f->redefines_target, meta, abs_struct,
                             &base_mapped);
      wire_subtree_is_mapped(f, meta, abs_struct, &redef_mapped);
      if (!redef_mapped || base_mapped) {
        continue;
      }
    }

    rc = wire_resolve_occurs_count(rec, f, meta, c_struct, abs_struct,
                                   &occ_count);
    if (rc != C_ORM_OK) {
      return rc;
    }

    for (occ_i = 0; occ_i < occ_count; ++occ_i) {
      size_t elem_off =
          f->byte_offset + base_shift + (occ_i * f->element_byte_size);
      cpy_wire_val_t val;
      memset(&val, 0, sizeof(val));

      if (f->is_dclgen_varchar) {
        if (c_struct) {
          const c_orm_column_meta_t *col = NULL;
          wire_find_col_meta(meta, f, &col);
          if (col) {
            wire_read_struct_col(c_struct, col, &val);
          }
        } else {
          const cdd_c_variant_t *var = NULL;
          wire_find_abs_val(abs_struct, f, &var);
          if (var) {
            wire_read_variant(var, &val);
          }
        }
        rc = wire_encode_dclgen_varchar(f, &val, cp, big_endian_comp,
                                        out_buf + elem_off);
        if (rc != C_ORM_OK) {
          return rc;
        }
        continue;
      }

      if (f->num_children > 0) {
        rc = wire_pack_fields_rec(rec, f->children, f->num_children,
                                  base_shift + (occ_i * f->element_byte_size),
                                  meta, c_struct, abs_struct, cp,
                                  big_endian_comp, out_buf);
        if (rc != C_ORM_OK) {
          return rc;
        }
        continue;
      }

      /* Elementary item (supports scalar or indexed/ODO array column) */
      if (!f->is_filler) {
        if (c_struct) {
          const c_orm_column_meta_t *col = NULL;
          if (f->has_occurs) {
            char idx_name[96];
            c_orm_cobol_field_t probe = *f;
            rc = C_ORM_SPRINTF(idx_name, sizeof(idx_name),
                               "%s_" C_ORM_FMT_SIZE_T, f->c_name,
                               C_ORM_CAST_SIZE_T(occ_i + 1));
            if (rc != C_ORM_OK) {
              return rc;
            }
            probe.c_name = idx_name;
            probe.sql_name = idx_name;
            probe.cobol_name = idx_name;
            wire_find_col_meta(meta, &probe, &col);
            if (!col) {
              wire_find_col_meta(meta, f, &col);
              if (col && col->type == C_ORM_TYPE_ARRAY) {
                c_orm_cobol_odo_array_t arr;
                int is_dec = 0;
                memcpy(&arr, (const unsigned char *)c_struct + col->offset,
                       sizeof(arr));
                wire_field_is_decimal_elem(f, &is_dec);
                if (arr.data && occ_i < arr.length) {
                  if (is_dec) {
                    val.tag = CPY_VAL_DECIMAL;
                    val.dec = ((const c_orm_decimal_t *)arr.data)[occ_i];
                  } else if (f->pic_category == C_ORM_COBOL_PIC_NUMERIC) {
                    val.tag = CPY_VAL_INT64;
                    val.i64 = (f->total_digits > 9)
                                  ? ((const c_orm_int64_t *)arr.data)[occ_i]
                                  : (c_orm_int64_t)((
                                        (const int32_t *)arr.data)[occ_i]);
                  } else {
                    const char *const *sarr = (const char *const *)arr.data;
                    if (sarr[occ_i]) {
                      val.tag = CPY_VAL_STRING;
                      val.str = sarr[occ_i];
                    }
                  }
                }
                col = NULL;
              } else if (occ_i > 0) {
                col = NULL;
              }
            }
          } else {
            wire_find_col_meta(meta, f, &col);
          }
          if (col) {
            wire_read_struct_col(c_struct, col, &val);
          }
        } else {
          const cdd_c_variant_t *var = NULL;
          if (f->has_occurs) {
            char idx_name[96];
            c_orm_cobol_field_t probe = *f;
            rc = C_ORM_SPRINTF(idx_name, sizeof(idx_name),
                               "%s_" C_ORM_FMT_SIZE_T, f->c_name,
                               C_ORM_CAST_SIZE_T(occ_i + 1));
            if (rc != C_ORM_OK) {
              return rc;
            }
            probe.c_name = idx_name;
            probe.sql_name = idx_name;
            probe.cobol_name = idx_name;
            wire_find_abs_val(abs_struct, &probe, &var);
            if (!var && occ_i == 0) {
              wire_find_abs_val(abs_struct, f, &var);
            }
          } else {
            wire_find_abs_val(abs_struct, f, &var);
          }
          if (var) {
            wire_read_variant(var, &val);
          }
        }
      }

      rc = wire_encode_elementary(f, &val, cp, big_endian_comp,
                                  out_buf + elem_off);
      if (rc != C_ORM_OK) {
        return rc;
      }
    }
  }
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_copybook_pack_struct(
    const c_orm_cobol_record_t *rec, const c_orm_table_meta_t *meta,
    const void *c_struct, c_orm_codepage_t cp, int big_endian_comp,
    unsigned char *out_buf, size_t buf_cap, size_t *out_record_len) {
  size_t active_len = 0;
  unsigned char pad = (cp >= C_ORM_CP_IBM037) ? 0x40U : 0x20U;
  c_orm_error_t rc;

  if (!rec || !meta || !c_struct || !out_buf) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  rc = wire_compute_active_record_len(rec, meta, c_struct, NULL, &active_len);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (buf_cap < active_len) {
    return C_ORM_ERROR_OVERFLOW;
  }
  memset(out_buf, pad, active_len);
  rc = wire_pack_fields_rec(rec, rec->fields, rec->num_fields, 0, meta,
                            c_struct, NULL, cp, big_endian_comp, out_buf);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (out_record_len) {
    *out_record_len = active_len;
  }
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_copybook_pack_abstract(
    const c_orm_cobol_record_t *rec, const cdd_c_abstract_struct_t *abs_struct,
    c_orm_codepage_t cp, int big_endian_comp, unsigned char *out_buf,
    size_t buf_cap, size_t *out_record_len) {
  size_t active_len = 0;
  unsigned char pad = (cp >= C_ORM_CP_IBM037) ? 0x40U : 0x20U;
  c_orm_error_t rc;

  if (!rec || !abs_struct || !out_buf) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  rc = wire_compute_active_record_len(rec, NULL, NULL, abs_struct, &active_len);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (buf_cap < active_len) {
    return C_ORM_ERROR_OVERFLOW;
  }
  memset(out_buf, pad, active_len);
  rc = wire_pack_fields_rec(rec, rec->fields, rec->num_fields, 0, NULL, NULL,
                            abs_struct, cp, big_endian_comp, out_buf);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (out_record_len) {
    *out_record_len = active_len;
  }
  return C_ORM_OK;
}

/**
 * @brief Decodes a single elementary wire slice into a `cdd_c_variant_t`
 * (owning any heap string or blob buffer).
 * @param f Elementary COBOL field.
 * @param src Source wire pointer of `f->element_byte_size` bytes.
 * @param cp Source wire codepage.
 * @param big_endian_comp Non-zero for big-endian `COMP` / `GRAPHIC` / floats.
 * @param out_var Output populated variant (caller frees via
 * `cdd_c_variant_free`).
 * @return C_ORM_OK on success, or error code.
 */
static c_orm_error_t wire_decode_elementary_to_variant(
    const c_orm_cobol_field_t *f, const unsigned char *src, c_orm_codepage_t cp,
    int big_endian_comp, cdd_c_variant_t *out_var) {
  c_orm_error_t rc;
  int use_ebcdic = (cp >= C_ORM_CP_IBM037) ? 1 : 0;

  memset(out_var, 0, sizeof(*out_var));

  switch (f->usage) {
  case C_ORM_COBOL_USAGE_COMP_3:
    out_var->type = CDD_C_VARIANT_TYPE_DECIMAL;
    return c_orm_decimal_from_comp3(src, f->element_byte_size, f->frac_digits,
                                    &out_var->value.d_val);

  case C_ORM_COBOL_USAGE_COMP:
  case C_ORM_COBOL_USAGE_COMP_5: {
    c_orm_int64_t iv = 0;
    int is_be = (f->usage == C_ORM_COBOL_USAGE_COMP) ? big_endian_comp : 0;
    c_orm_int64_from_cobol_comp(src, f->element_byte_size, f->is_signed, is_be,
                                &iv);
    if (f->frac_digits > 0) {
      c_orm_decimal_t d;
      c_orm_decimal_t ten;
      unsigned char k;
      c_orm_decimal_from_int64(iv, &d);
      c_orm_decimal_from_int64(10, &ten);
      for (k = 0; k < f->frac_digits; ++k) {
        c_orm_decimal_t next_d;
        c_orm_decimal_div(&d, &ten, (unsigned char)(k + 1U),
                          C_ORM_ROUND_TRUNCATE, &next_d);
        d = next_d;
      }
      out_var->type = CDD_C_VARIANT_TYPE_DECIMAL;
      out_var->value.d_val = d;
    } else {
      out_var->type = CDD_C_VARIANT_TYPE_INT;
      out_var->value.i_val = iv;
    }
    return C_ORM_OK;
  }

  case C_ORM_COBOL_USAGE_COMP_1: {
    float fv = 0.0f;
    int swap = 0;
    unsigned char raw4[4];
    wire_need_byte_swap(big_endian_comp, &swap);
    if (swap) {
      raw4[0] = src[3];
      raw4[1] = src[2];
      raw4[2] = src[1];
      raw4[3] = src[0];
    } else {
      memcpy(raw4, src, 4);
    }
    memcpy(&fv, raw4, 4);
    out_var->type = CDD_C_VARIANT_TYPE_FLOAT;
    out_var->value.f_val = (double)fv;
    return C_ORM_OK;
  }

  case C_ORM_COBOL_USAGE_COMP_2: {
    double dv = 0.0;
    int swap = 0;
    unsigned char raw8[8];
    size_t k;
    wire_need_byte_swap(big_endian_comp, &swap);
    if (swap) {
      for (k = 0; k < 8; ++k) {
        raw8[k] = src[7 - k];
      }
    } else {
      memcpy(raw8, src, 8);
    }
    memcpy(&dv, raw8, 8);
    out_var->type = CDD_C_VARIANT_TYPE_FLOAT;
    out_var->value.f_val = dv;
    return C_ORM_OK;
  }

  case C_ORM_COBOL_USAGE_DISPLAY_1: {
    size_t units = f->display_length;
    unsigned short *u16 = NULL;
    char *utf8 = NULL;
    size_t utf8_cap = units * 4 + 1;
    unsigned char sp0 = big_endian_comp ? 0x00U : 0x20U;
    unsigned char sp1 = big_endian_comp ? 0x20U : 0x00U;

    while (units > 0 && src[(units - 1) * 2] == sp0 &&
           src[(units - 1) * 2 + 1] == sp1) {
      units--;
    }
    rc = c_orm_system_malloc((units + 1) * sizeof(unsigned short),
                             (void **)&u16);
    if (rc != C_ORM_OK) {
      return rc;
    }
    if (units > 0) {
      memcpy(u16, src, units * 2);
    }
    rc = c_orm_system_malloc(utf8_cap, (void **)&utf8);
    if (rc != C_ORM_OK) {
      (void)c_orm_system_free(u16);
      return rc;
    }
    rc = c_orm_utf16_to_utf8(u16, units, big_endian_comp, utf8, utf8_cap, NULL);
    (void)c_orm_system_free(u16);
    if (rc != C_ORM_OK) {
      (void)c_orm_system_free(utf8);
      return rc;
    }
    out_var->type = CDD_C_VARIANT_TYPE_STRING;
    out_var->value.s_val = utf8;
    return C_ORM_OK;
  }

  default:
    if (f->pic_category == C_ORM_COBOL_PIC_NUMERIC) {
      if (f->is_blank_when_zero) {
        size_t bi;
        int all_sp = 1;
        unsigned char sp = use_ebcdic ? 0x40U : 0x20U;
        for (bi = 0; bi < f->element_byte_size; ++bi) {
          if (src[bi] != sp) {
            all_sp = 0;
            break;
          }
        }
        if (all_sp) {
          out_var->type = CDD_C_VARIANT_TYPE_DECIMAL;
          return c_orm_decimal_zero(f->frac_digits, &out_var->value.d_val);
        }
      }
      rc = c_orm_decimal_from_zoned(
          src, f->element_byte_size, f->frac_digits, f->is_signed,
          f->sign_leading, f->sign_separate, use_ebcdic, &out_var->value.d_val);
      if (rc != C_ORM_OK) {
        return rc;
      }
      if (f->frac_digits == 0 && f->total_digits <= 18) {
        c_orm_int64_t iv = 0;
        c_orm_decimal_to_int64(&out_var->value.d_val, C_ORM_ROUND_TRUNCATE,
                               &iv);
        out_var->type = CDD_C_VARIANT_TYPE_INT;
        out_var->value.i_val = iv;
        return C_ORM_OK;
      }
      out_var->type = CDD_C_VARIANT_TYPE_DECIMAL;
      return C_ORM_OK;
    }
    if (f->sql_for_bit_data) {
      unsigned char *bcopy = NULL;
      rc = c_orm_system_malloc(f->element_byte_size, (void **)&bcopy);
      if (rc != C_ORM_OK) {
        return rc;
      }
      memcpy(bcopy, src, f->element_byte_size);
      out_var->type = CDD_C_VARIANT_TYPE_BLOB;
      out_var->value.b_val.data = bcopy;
      out_var->value.b_val.size = f->element_byte_size;
      return C_ORM_OK;
    }
    {
      size_t cap = f->element_byte_size * 3 + 2;
      char *utf8 = NULL;
      rc = c_orm_system_malloc(cap, (void **)&utf8);
      if (rc != C_ORM_OK) {
        return rc;
      }
      c_orm_cobol_extract_alpha(src, f->element_byte_size, cp, 1, utf8, cap,
                                NULL);
      out_var->type = CDD_C_VARIANT_TYPE_STRING;
      out_var->value.s_val = utf8;
      return C_ORM_OK;
    }
  }
}

/**
 * @brief Decodes a `DCLGEN` `49`-level `VARCHAR`/`VARGRAPHIC` group into a
 * string `cdd_c_variant_t`.
 * @param f Group field with `is_dclgen_varchar != 0`.
 * @param src Wire bytes at `f->byte_offset`.
 * @param cp Source wire codepage.
 * @param big_endian_comp Non-zero for big-endian `COMP` / `GRAPHIC`.
 * @param out_var Output populated string variant.
 * @return C_ORM_OK on success, or error code.
 */
static c_orm_error_t wire_decode_dclgen_varchar(const c_orm_cobol_field_t *f,
                                                const unsigned char *src,
                                                c_orm_codepage_t cp,
                                                int big_endian_comp,
                                                cdd_c_variant_t *out_var) {
  const c_orm_cobol_field_t *len_f = &f->children[0];
  const c_orm_cobol_field_t *txt_f = &f->children[1];
  int len_be = (len_f->usage == C_ORM_COBOL_USAGE_COMP) ? big_endian_comp : 0;
  const unsigned char *txt_src = src + (txt_f->byte_offset - f->byte_offset);
  c_orm_int64_t actual_len = 0;
  c_orm_error_t rc;

  memset(out_var, 0, sizeof(*out_var));
  c_orm_int64_from_cobol_comp(src, len_f->element_byte_size, len_f->is_signed,
                              len_be, &actual_len);
  if (actual_len < 0 || (size_t)actual_len > f->dclgen_max_chars) {
    return C_ORM_ERROR_VALIDATION;
  }

  if (f->is_dclgen_vargraphic) {
    size_t units = (size_t)actual_len;
    unsigned short *u16 = NULL;
    char *utf8 = NULL;
    size_t cap = units * 4 + 2;
    rc = c_orm_system_malloc((units + 1) * sizeof(unsigned short),
                             (void **)&u16);
    if (rc != C_ORM_OK) {
      return rc;
    }
    if (units > 0) {
      memcpy(u16, txt_src, units * 2);
    }
    rc = c_orm_system_malloc(cap, (void **)&utf8);
    if (rc != C_ORM_OK) {
      (void)c_orm_system_free(u16);
      return rc;
    }
    rc = c_orm_utf16_to_utf8(u16, units, big_endian_comp, utf8, cap, NULL);
    (void)c_orm_system_free(u16);
    if (rc != C_ORM_OK) {
      (void)c_orm_system_free(utf8);
      return rc;
    }
    out_var->type = CDD_C_VARIANT_TYPE_STRING;
    out_var->value.s_val = utf8;
    return C_ORM_OK;
  }

  {
    size_t bytes_n = (size_t)actual_len;
    size_t cap = bytes_n * 3 + 2;
    char *utf8 = NULL;
    rc = c_orm_system_malloc(cap, (void **)&utf8);
    if (rc != C_ORM_OK) {
      return rc;
    }
    c_orm_ebcdic_to_utf8(cp, txt_src, bytes_n, utf8, cap, NULL);
    out_var->type = CDD_C_VARIANT_TYPE_STRING;
    out_var->value.s_val = utf8;
    return C_ORM_OK;
  }
}

/**
 * @brief Stores a decoded `cdd_c_variant_t` into a target C89 struct column
 * slot (transferring or copying heap buffers cleanly).
 * @param out_c_struct Target C struct instance.
 * @param col Column metadata descriptor.
 * @param var Decoded variant value.
 * @return C_ORM_OK on success, or error code.
 */
static c_orm_error_t wire_write_struct_col(void *out_c_struct,
                                           const c_orm_column_meta_t *col,
                                           const cdd_c_variant_t *var) {
  unsigned char *ptr = (unsigned char *)out_c_struct + col->offset;
  c_orm_error_t rc;

  switch (col->type) {
  case C_ORM_TYPE_INT32: {
    int32_t v32 = 0;
    if (var->type == CDD_C_VARIANT_TYPE_INT) {
      v32 = (int32_t)var->value.i_val;
    } else if (var->type == CDD_C_VARIANT_TYPE_DECIMAL) {
      c_orm_int64_t t64 = 0;
      rc =
          c_orm_decimal_to_int64(&var->value.d_val, C_ORM_ROUND_TRUNCATE, &t64);
      if (rc != C_ORM_OK) {
        return rc;
      }
      v32 = (int32_t)t64;
    }
    if (col->is_nullable) {
      int32_t *hp = NULL;
      memcpy(&hp, ptr, sizeof(hp));
      if (!hp) {
        rc = c_orm_system_malloc(sizeof(int32_t), (void **)&hp);
        if (rc != C_ORM_OK) {
          return rc;
        }
        memcpy(ptr, &hp, sizeof(hp));
      }
      *hp = v32;
    } else {
      memcpy(ptr, &v32, sizeof(v32));
    }
    return C_ORM_OK;
  }

  case C_ORM_TYPE_INT64: {
    c_orm_int64_t v64 = 0;
    if (var->type == CDD_C_VARIANT_TYPE_INT) {
      v64 = var->value.i_val;
    } else if (var->type == CDD_C_VARIANT_TYPE_DECIMAL) {
      rc =
          c_orm_decimal_to_int64(&var->value.d_val, C_ORM_ROUND_TRUNCATE, &v64);
      if (rc != C_ORM_OK) {
        return rc;
      }
    }
    if (col->is_nullable) {
      c_orm_int64_t *hp = NULL;
      memcpy(&hp, ptr, sizeof(hp));
      if (!hp) {
        rc = c_orm_system_malloc(sizeof(c_orm_int64_t), (void **)&hp);
        if (rc != C_ORM_OK) {
          return rc;
        }
        memcpy(ptr, &hp, sizeof(hp));
      }
      *hp = v64;
    } else {
      memcpy(ptr, &v64, sizeof(v64));
    }
    return C_ORM_OK;
  }

  case C_ORM_TYPE_BOOL: {
    unsigned char bv = 0;
    if (var->type == CDD_C_VARIANT_TYPE_INT) {
      bv = (var->value.i_val != 0) ? 1U : 0U;
    } else if (var->type == CDD_C_VARIANT_TYPE_STRING) {
      bv = (strchr("1YT", (unsigned char)var->value.s_val[0]) != NULL &&
            var->value.s_val[0] != '\0')
               ? 1U
               : 0U;
    }
    if (col->is_nullable) {
      unsigned char *hp = NULL;
      memcpy(&hp, ptr, sizeof(hp));
      if (!hp) {
        rc = c_orm_system_malloc(1, (void **)&hp);
        if (rc != C_ORM_OK) {
          return rc;
        }
        memcpy(ptr, &hp, sizeof(hp));
      }
      *hp = bv;
    } else {
      *ptr = bv;
    }
    return C_ORM_OK;
  }

  case C_ORM_TYPE_FLOAT: {
    float fv = (var->type == CDD_C_VARIANT_TYPE_FLOAT)
                   ? (float)var->value.f_val
                   : (float)var->value.i_val;
    if (col->is_nullable) {
      float *hp = NULL;
      memcpy(&hp, ptr, sizeof(hp));
      if (!hp) {
        rc = c_orm_system_malloc(sizeof(float), (void **)&hp);
        if (rc != C_ORM_OK) {
          return rc;
        }
        memcpy(ptr, &hp, sizeof(hp));
      }
      *hp = fv;
    } else {
      memcpy(ptr, &fv, sizeof(fv));
    }
    return C_ORM_OK;
  }

  case C_ORM_TYPE_DOUBLE: {
    double dv = (var->type == CDD_C_VARIANT_TYPE_FLOAT)
                    ? var->value.f_val
                    : (double)var->value.i_val;
    if (col->is_nullable) {
      double *hp = NULL;
      memcpy(&hp, ptr, sizeof(hp));
      if (!hp) {
        rc = c_orm_system_malloc(sizeof(double), (void **)&hp);
        if (rc != C_ORM_OK) {
          return rc;
        }
        memcpy(ptr, &hp, sizeof(hp));
      }
      *hp = dv;
    } else {
      memcpy(ptr, &dv, sizeof(dv));
    }
    return C_ORM_OK;
  }

  case C_ORM_TYPE_DECIMAL:
  case C_ORM_TYPE_DECFLOAT: {
    c_orm_decimal_t d;
    if (var->type == CDD_C_VARIANT_TYPE_DECIMAL) {
      d = var->value.d_val;
    } else if (var->type == CDD_C_VARIANT_TYPE_INT) {
      c_orm_decimal_from_int64(var->value.i_val, &d);
    } else if (var->type == CDD_C_VARIANT_TYPE_STRING) {
      rc = c_orm_decimal_from_str(var->value.s_val, &d);
      if (rc != C_ORM_OK) {
        return rc;
      }
    } else {
      c_orm_decimal_zero(0, &d);
    }
    if (col->is_nullable) {
      c_orm_decimal_t *hp = NULL;
      memcpy(&hp, ptr, sizeof(hp));
      if (!hp) {
        rc = c_orm_system_malloc(sizeof(c_orm_decimal_t), (void **)&hp);
        if (rc != C_ORM_OK) {
          return rc;
        }
        memcpy(ptr, &hp, sizeof(hp));
      }
      *hp = d;
    } else {
      memcpy(ptr, &d, sizeof(d));
    }
    return C_ORM_OK;
  }

  case C_ORM_TYPE_CHAR_FIXED:
    if (col->fixed_length > 0) {
      char num_tmp[64];
      const char *s = "";
      memset(ptr, 0, col->fixed_length + 1);
      if (var->type == CDD_C_VARIANT_TYPE_STRING) {
        s = var->value.s_val;
      } else if (var->type == CDD_C_VARIANT_TYPE_DECIMAL) {
        c_orm_decimal_to_str(&var->value.d_val, num_tmp, sizeof(num_tmp), NULL);
        s = num_tmp;
      } else if (var->type == CDD_C_VARIANT_TYPE_INT) {
        rc = C_ORM_SPRINTF(num_tmp, sizeof(num_tmp), C_ORM_INT64_FORMAT,
                           var->value.i_val);
        if (rc != C_ORM_OK) {
          return rc;
        }
        s = num_tmp;
      }
      {
        size_t sl = strlen(s);
        if (sl > col->fixed_length) {
          sl = col->fixed_length;
        }
        memcpy(ptr, s, sl);
      }
      return C_ORM_OK;
    }
    /* Fallthrough to heap char* */
    /* FALLTHROUGH */
  case C_ORM_TYPE_STRING:
  case C_ORM_TYPE_DATE:
  case C_ORM_TYPE_TIME:
  case C_ORM_TYPE_TIMESTAMP:
  case C_ORM_TYPE_GRAPHIC:
  case C_ORM_TYPE_VARGRAPHIC: {
    char num_tmp[64];
    const char *s = "";
    char *dup = NULL;
    char *prev = NULL;
    if (var->type == CDD_C_VARIANT_TYPE_STRING) {
      s = var->value.s_val;
    } else if (var->type == CDD_C_VARIANT_TYPE_DECIMAL) {
      c_orm_decimal_to_str(&var->value.d_val, num_tmp, sizeof(num_tmp), NULL);
      s = num_tmp;
    } else if (var->type == CDD_C_VARIANT_TYPE_INT) {
      rc = C_ORM_SPRINTF(num_tmp, sizeof(num_tmp), C_ORM_INT64_FORMAT,
                         var->value.i_val);
      if (rc != C_ORM_OK) {
        return rc;
      }
      s = num_tmp;
    }
    rc = c_orm_strdup(s, &dup);
    if (rc != C_ORM_OK) {
      return rc;
    }
    memcpy(&prev, ptr, sizeof(prev));
    if (prev) {
      (void)c_orm_system_free(prev);
    }
    memcpy(ptr, &dup, sizeof(dup));
    return C_ORM_OK;
  }

  case C_ORM_TYPE_BINARY_FIXED:
    if (col->fixed_length > 0) {
      memset(ptr, 0, col->fixed_length);
      if (var->type == CDD_C_VARIANT_TYPE_BLOB) {
        size_t n = (var->value.b_val.size < col->fixed_length)
                       ? var->value.b_val.size
                       : col->fixed_length;
        memcpy(ptr, var->value.b_val.data, n);
      } else if (var->type == CDD_C_VARIANT_TYPE_STRING) {
        size_t sl = strlen(var->value.s_val);
        size_t n = (sl < col->fixed_length) ? sl : col->fixed_length;
        memcpy(ptr, var->value.s_val, n);
      }
      return C_ORM_OK;
    }
    /* FALLTHROUGH */
  case C_ORM_TYPE_BLOB:
  case C_ORM_TYPE_VARBINARY: {
    c_orm_blob_t b;
    const unsigned char *src_b = NULL;
    size_t src_n = 0;
    unsigned char *bdup = NULL;
    memcpy(&b, ptr, sizeof(b));
    if (var->type == CDD_C_VARIANT_TYPE_BLOB) {
      src_b = var->value.b_val.data;
      src_n = var->value.b_val.size;
    } else if (var->type == CDD_C_VARIANT_TYPE_STRING) {
      src_b = (const unsigned char *)var->value.s_val;
      src_n = strlen(var->value.s_val);
    }
    if (b.data) {
      (void)c_orm_system_free(b.data);
      b.data = NULL;
      b.size = 0;
    }
    if (src_n > 0) {
      rc = c_orm_system_malloc(src_n, (void **)&bdup);
      if (rc != C_ORM_OK) {
        memcpy(ptr, &b, sizeof(b));
        return rc;
      }
      memcpy(bdup, src_b, src_n);
      b.data = bdup;
      b.size = src_n;
    }
    memcpy(ptr, &b, sizeof(b));
    return C_ORM_OK;
  }

  default:
    return C_ORM_OK;
  }
}

/**
 * @brief Recursively unpacks wire bytes into `out_c_struct` or
 * `out_abs_struct`.
 * @param rec Top-level record AST.
 * @param fields Array of sibling fields.
 * @param count Number of sibling fields.
 * @param base_shift Byte offset shift inside `OCCURS` groups.
 * @param raw_buf Source raw COBOL record buffer.
 * @param raw_len Length of `raw_buf` in bytes.
 * @param cp Source wire codepage.
 * @param big_endian_comp Non-zero for big-endian `COMP` / `GRAPHIC`.
 * @param meta Optional C struct metadata.
 * @param out_c_struct Optional target C struct.
 * @param out_abs Optional target abstract struct.
 * @return C_ORM_OK on success, or error code.
 */
static c_orm_error_t
wire_unpack_fields_rec(const c_orm_cobol_record_t *rec,
                       const c_orm_cobol_field_t *fields, size_t count,
                       size_t base_shift, const unsigned char *raw_buf,
                       size_t raw_len, c_orm_codepage_t cp, int big_endian_comp,
                       const c_orm_table_meta_t *meta, void *out_c_struct,
                       cdd_c_abstract_struct_t *out_abs) {
  size_t i;
  c_orm_error_t rc;

  for (i = 0; i < count; ++i) {
    const c_orm_cobol_field_t *f = &fields[i];
    size_t occ_count = 1;
    size_t occ_i;

    if (f->level == 66 || f->is_filler) {
      continue;
    }
    if (f->redefines_target && meta) {
      int redef_mapped = 0;
      wire_subtree_is_mapped(f, meta, NULL, &redef_mapped);
      if (!redef_mapped) {
        continue;
      }
    }

    rc = wire_resolve_occurs_count(rec, f, meta, out_c_struct, out_abs,
                                   &occ_count);
    if (rc != C_ORM_OK) {
      return rc;
    }

    /* Pre-allocate c_orm_cobol_odo_array_t if C struct maps ODO/OCCURS to ARRAY
     */
    if (f->has_occurs && f->num_children == 0 && out_c_struct) {
      const c_orm_column_meta_t *arr_col = NULL;
      wire_find_col_meta(meta, f, &arr_col);
      if (arr_col && arr_col->type == C_ORM_TYPE_ARRAY) {
        unsigned char *arr_ptr =
            (unsigned char *)out_c_struct + arr_col->offset;
        c_orm_cobol_odo_array_t arr;
        size_t elem_c_sz = sizeof(char *);
        int is_dec = 0;
        memcpy(&arr, arr_ptr, sizeof(arr));
        wire_field_is_decimal_elem(f, &is_dec);
        if (is_dec) {
          elem_c_sz = sizeof(c_orm_decimal_t);
        } else if (f->pic_category == C_ORM_COBOL_PIC_NUMERIC) {
          elem_c_sz =
              (f->total_digits > 9) ? sizeof(c_orm_int64_t) : sizeof(int32_t);
        }
        if (arr.data) {
          (void)c_orm_system_free(arr.data);
          arr.data = NULL;
        }
        arr.length = 0;
        arr.capacity = occ_count;
        if (occ_count > 0) {
          rc = c_orm_system_calloc(occ_count, elem_c_sz, &arr.data);
          if (rc != C_ORM_OK) {
            memcpy(arr_ptr, &arr, sizeof(arr));
            return rc;
          }
          arr.length = occ_count;
        }
        memcpy(arr_ptr, &arr, sizeof(arr));
      }
    }

    for (occ_i = 0; occ_i < occ_count; ++occ_i) {
      size_t elem_off =
          f->byte_offset + base_shift + (occ_i * f->element_byte_size);
      cdd_c_variant_t var;

      if (elem_off + f->element_byte_size > raw_len) {
        return C_ORM_ERROR_TRUNCATION;
      }

      if (f->is_dclgen_varchar) {
        rc = wire_decode_dclgen_varchar(f, raw_buf + elem_off, cp,
                                        big_endian_comp, &var);
        if (rc != C_ORM_OK) {
          return rc;
        }
        if (out_c_struct) {
          const c_orm_column_meta_t *col = NULL;
          wire_find_col_meta(meta, f, &col);
          if (col) {
            rc = wire_write_struct_col(out_c_struct, col, &var);
            (void)cdd_c_variant_free(&var);
            if (rc != C_ORM_OK) {
              return rc;
            }
          } else {
            (void)cdd_c_variant_free(&var);
          }
        } else {
          rc = cdd_c_abstract_set(out_abs, f->c_name, &var);
          (void)cdd_c_variant_free(&var);
          if (rc != C_ORM_OK) {
            return rc;
          }
        }
        continue;
      }

      if (f->num_children > 0) {
        rc = wire_unpack_fields_rec(rec, f->children, f->num_children,
                                    base_shift + (occ_i * f->element_byte_size),
                                    raw_buf, raw_len, cp, big_endian_comp, meta,
                                    out_c_struct, out_abs);
        if (rc != C_ORM_OK) {
          return rc;
        }
        continue;
      }

      rc = wire_decode_elementary_to_variant(f, raw_buf + elem_off, cp,
                                             big_endian_comp, &var);
      if (rc != C_ORM_OK) {
        return rc;
      }

      if (out_c_struct) {
        const c_orm_column_meta_t *col = NULL;
        if (f->has_occurs) {
          char idx_name[96];
          c_orm_cobol_field_t probe = *f;
          rc = C_ORM_SPRINTF(idx_name, sizeof(idx_name), "%s_" C_ORM_FMT_SIZE_T,
                             f->c_name, C_ORM_CAST_SIZE_T(occ_i + 1));
          if (rc != C_ORM_OK) {
            (void)cdd_c_variant_free(&var);
            return rc;
          }
          probe.c_name = idx_name;
          probe.sql_name = idx_name;
          probe.cobol_name = idx_name;
          wire_find_col_meta(meta, &probe, &col);
          if (!col) {
            wire_find_col_meta(meta, f, &col);
            if (col && col->type == C_ORM_TYPE_ARRAY) {
              c_orm_cobol_odo_array_t arr;
              int is_dec = 0;
              memcpy(&arr, (unsigned char *)out_c_struct + col->offset,
                     sizeof(arr));
              wire_field_is_decimal_elem(f, &is_dec);
              if (is_dec) {
                ((c_orm_decimal_t *)arr.data)[occ_i] = var.value.d_val;
              } else if (f->pic_category == C_ORM_COBOL_PIC_NUMERIC) {
                if (f->total_digits > 9) {
                  ((c_orm_int64_t *)arr.data)[occ_i] = var.value.i_val;
                } else {
                  ((int32_t *)arr.data)[occ_i] = (int32_t)var.value.i_val;
                }
              } else {
                rc = c_orm_strdup(var.value.s_val, &((char **)arr.data)[occ_i]);
                if (rc != C_ORM_OK) {
                  (void)cdd_c_variant_free(&var);
                  return rc;
                }
              }
              col = NULL;
            } else if (occ_i > 0) {
              col = NULL;
            }
          }
        } else {
          wire_find_col_meta(meta, f, &col);
        }
        if (col) {
          rc = wire_write_struct_col(out_c_struct, col, &var);
          (void)cdd_c_variant_free(&var);
          if (rc != C_ORM_OK) {
            return rc;
          }
        } else {
          (void)cdd_c_variant_free(&var);
        }
      } else {
        if (f->has_occurs) {
          char idx_name[96];
          rc = C_ORM_SPRINTF(idx_name, sizeof(idx_name), "%s_" C_ORM_FMT_SIZE_T,
                             f->c_name, C_ORM_CAST_SIZE_T(occ_i + 1));
          if (rc == C_ORM_OK) {
            rc = cdd_c_abstract_set(out_abs, idx_name, &var);
          }
        } else {
          rc = cdd_c_abstract_set(out_abs, f->c_name, &var);
        }
        (void)cdd_c_variant_free(&var);
        if (rc != C_ORM_OK) {
          return rc;
        }
      }
    }
  }
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_copybook_unpack_struct(
    const c_orm_cobol_record_t *rec, const c_orm_table_meta_t *meta,
    const unsigned char *raw_buf, size_t raw_len, c_orm_codepage_t cp,
    int big_endian_comp, void *out_c_struct) {
  if (!rec || !meta || !raw_buf || !out_c_struct) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  return wire_unpack_fields_rec(rec, rec->fields, rec->num_fields, 0, raw_buf,
                                raw_len, cp, big_endian_comp, meta,
                                out_c_struct, NULL);
}

C_ORM_EXPORT c_orm_error_t c_orm_copybook_unpack_abstract(
    const c_orm_cobol_record_t *rec, const unsigned char *raw_buf,
    size_t raw_len, c_orm_codepage_t cp, int big_endian_comp,
    cdd_c_abstract_struct_t *out_abs_struct) {
  if (!rec || !raw_buf || !out_abs_struct) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  return wire_unpack_fields_rec(rec, rec->fields, rec->num_fields, 0, raw_buf,
                                raw_len, cp, big_endian_comp, NULL, NULL,
                                out_abs_struct);
}

/**
 * @brief Compares two strings after ignoring trailing ASCII spaces (COBOL
 * fixed-width character comparison semantics).
 * @param a First string.
 * @param b Second string.
 * @param out_cmp Output `<0`, `0`, or `>0`.
 * @return C_ORM_OK on success.
 */
static c_orm_error_t wire_cobol_str_cmp(const char *a, const char *b,
                                        int *out_cmp) {
  size_t la = strlen(a);
  size_t lb = strlen(b);
  size_t n;
  int r;
  while (la > 0 && a[la - 1] == ' ') {
    la--;
  }
  while (lb > 0 && b[lb - 1] == ' ') {
    lb--;
  }
  n = (la < lb) ? la : lb;
  r = (n > 0) ? memcmp(a, b, n) : 0;
  if (r != 0) {
    *out_cmp = r;
  } else if (la < lb) {
    *out_cmp = -1;
  } else if (la > lb) {
    *out_cmp = 1;
  } else {
    *out_cmp = 0;
  }
  return C_ORM_OK;
}

/**
 * @brief Evaluates a single `c_orm_cobol_condition_t` against `val`.
 * @param f Parent COBOL field.
 * @param cond Level-`88` condition node.
 * @param val Current field value extracted from `c_struct`.
 * @param out_matched Output flag set to 1 if satisfied, else 0.
 * @return C_ORM_OK on success, or error code.
 */
static c_orm_error_t wire_eval_condition(const c_orm_cobol_field_t *f,
                                         const c_orm_cobol_condition_t *cond,
                                         const cpy_wire_val_t *val,
                                         int *out_matched) {
  size_t i;
  *out_matched = 0;
  for (i = 0; i < cond->num_values; ++i) {
    const c_orm_cobol_value_range_t *vr = &cond->values[i];
    if (vr->is_numeric || f->pic_category == C_ORM_COBOL_PIC_NUMERIC) {
      c_orm_decimal_t cur_d;
      c_orm_decimal_t low_d;
      int cmp_lo = 0;
      if (wire_val_to_decimal(f, val, &cur_d) != C_ORM_OK ||
          c_orm_decimal_from_str(vr->low_val, &low_d) != C_ORM_OK) {
        continue;
      }
      c_orm_decimal_cmp(&cur_d, &low_d, &cmp_lo);
      if (vr->is_range) {
        c_orm_decimal_t high_d;
        int cmp_hi = 0;
        if (c_orm_decimal_from_str(vr->high_val, &high_d) == C_ORM_OK) {
          c_orm_decimal_cmp(&cur_d, &high_d, &cmp_hi);
          if (cmp_lo >= 0 && cmp_hi <= 0) {
            *out_matched = 1;
            return C_ORM_OK;
          }
        }
      } else if (cmp_lo == 0) {
        *out_matched = 1;
        return C_ORM_OK;
      }
    } else {
      const char *s = (val->tag == CPY_VAL_STRING) ? val->str : "";
      int cmp_lo = 0;
      wire_cobol_str_cmp(s, vr->low_val, &cmp_lo);
      if (vr->is_range) {
        int cmp_hi = 0;
        wire_cobol_str_cmp(s, vr->high_val, &cmp_hi);
        if (cmp_lo >= 0 && cmp_hi <= 0) {
          *out_matched = 1;
          return C_ORM_OK;
        }
      } else if (cmp_lo == 0) {
        *out_matched = 1;
        return C_ORM_OK;
      }
    }
  }
  return C_ORM_OK;
}

/**
 * @brief Recursively validates level-`88` conditions across `fields`.
 * @param fields Field array.
 * @param count Number of fields.
 * @param meta C struct metadata.
 * @param c_struct Hydrated C struct.
 * @return C_ORM_OK if valid, or C_ORM_ERROR_VALIDATION if violated.
 */
static c_orm_error_t wire_validate_88_rec(const c_orm_cobol_field_t *fields,
                                          size_t count,
                                          const c_orm_table_meta_t *meta,
                                          const void *c_struct) {
  size_t i;
  c_orm_error_t rc;
  for (i = 0; i < count; ++i) {
    const c_orm_cobol_field_t *f = &fields[i];
    if (f->num_conditions > 0) {
      const c_orm_column_meta_t *col = NULL;
      wire_find_col_meta(meta, f, &col);
      if (col) {
        cpy_wire_val_t val;
        size_t ci;
        int any_ok = 0;
        wire_read_struct_col(c_struct, col, &val);
        if (val.tag == CPY_VAL_EMPTY && col->is_nullable) {
          any_ok = 1;
        }
        for (ci = 0; ci < f->num_conditions && !any_ok; ++ci) {
          wire_eval_condition(f, &f->conditions[ci], &val, &any_ok);
        }
        if (!any_ok) {
          return C_ORM_ERROR_VALIDATION;
        }
      }
    }
    if (f->num_children > 0) {
      rc = wire_validate_88_rec(f->children, f->num_children, meta, c_struct);
      if (rc != C_ORM_OK) {
        return rc;
      }
    }
  }
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_copybook_validate_88(
    const c_orm_cobol_record_t *rec, const c_orm_table_meta_t *meta,
    const void *c_struct) {
  if (!rec || !meta || !c_struct) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  return wire_validate_88_rec(rec->fields, rec->num_fields, meta, c_struct);
}

/**
 * @brief Searches `fields` recursively for a named level-`88` condition and
 * evaluates it against `c_struct`.
 * @param fields Field array.
 * @param count Number of fields.
 * @param meta C struct metadata.
 * @param c_struct Hydrated C struct.
 * @param condition_name Condition name to locate.
 * @param out_found Output flag set to 1 when condition was located.
 * @param out_matched Output flag set to 1 if condition holds.
 * @return C_ORM_OK on success, or error code.
 */
static c_orm_error_t wire_check_cond_rec(const c_orm_cobol_field_t *fields,
                                         size_t count,
                                         const c_orm_table_meta_t *meta,
                                         const void *c_struct,
                                         const char *condition_name,
                                         int *out_found, int *out_matched) {
  size_t i;
  c_orm_error_t rc;
  for (i = 0; i < count; ++i) {
    const c_orm_cobol_field_t *f = &fields[i];
    size_t ci;
    for (ci = 0; ci < f->num_conditions; ++ci) {
      int eq1 = 0;
      int eq2 = 0;
      wire_ident_eq(f->conditions[ci].cobol_name, condition_name, &eq1);
      wire_ident_eq(f->conditions[ci].c_name, condition_name, &eq2);
      if (eq1 || eq2) {
        const c_orm_column_meta_t *col = NULL;
        cpy_wire_val_t val;
        *out_found = 1;
        wire_find_col_meta(meta, f, &col);
        if (!col) {
          return C_ORM_ERROR_VALIDATION;
        }
        wire_read_struct_col(c_struct, col, &val);
        return wire_eval_condition(f, &f->conditions[ci], &val, out_matched);
      }
    }
    if (f->num_children > 0) {
      rc = wire_check_cond_rec(f->children, f->num_children, meta, c_struct,
                               condition_name, out_found, out_matched);
      if (rc != C_ORM_OK || *out_found) {
        return rc;
      }
    }
  }
  return C_ORM_OK;
}

C_ORM_EXPORT c_orm_error_t c_orm_copybook_check_condition(
    const c_orm_cobol_record_t *rec, const c_orm_table_meta_t *meta,
    const void *c_struct, const char *condition_name, int *out_matched) {
  int found = 0;
  c_orm_error_t rc;
  if (!rec || !meta || !c_struct || !condition_name || !out_matched) {
    return C_ORM_ERROR_NULL_POINTER;
  }
  *out_matched = 0;
  rc = wire_check_cond_rec(rec->fields, rec->num_fields, meta, c_struct,
                           condition_name, &found, out_matched);
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (!found) {
    return C_ORM_ERROR_NOT_FOUND;
  }
  return C_ORM_OK;
}
