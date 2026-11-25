/**
 * @file test_decimal_cobol.c
 * @brief Exhaustive unit and end-to-end ORM tests for c_orm_decimal_t,
 * SQL_NUMERIC_STRUCT, COBOL COMP-3, Zoned Decimal, COMP/COMP-5, and ORM CRUD.
 */

/* clang-format off */
#include "c_orm_api.h"
#include "c_orm_decimal.h"
#include "c_orm_inline_macros.h"
#include "c_orm_memory.h"
#include "c_orm_sqlite.h"
#include "c_orm_struct.h"
#include "abstract_struct.h"
#include "cdd_c_orm_meta.h"
#define GREATEST_USE_LONGJMP 0
#include <greatest.h>
#include "test_assert_zero_branch.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

/**
 * @brief Test struct exercising C_ORM_TYPE_DECIMAL, C_ORM_TYPE_CHAR_FIXED, and
 * C_ORM_TYPE_BINARY_FIXED across SQLite and Memory drivers.
 */
struct LedgerEntry {
  /** @brief Decimal account key. */
  c_orm_decimal_t acct_id;
  /** @brief Fixed-width account code. */
  char code[9];
  /** @brief Fixed-point ledger balance DECIMAL(15, 2). */
  c_orm_decimal_t balance;
  /** @brief Optional fee DECIMAL(9, 4). */
  c_orm_decimal_t *fee;
  /** @brief Fixed 4-byte binary audit tag (CHAR(4) FOR BIT DATA). */
  unsigned char tag[4];
};

/** @brief Array container for LedgerEntry Hydration. */
struct LedgerEntry_Array {
  /** @brief Elements buffer. */
  struct LedgerEntry *data;
  /** @brief Element count. */
  size_t length;
  /** @brief Allocated capacity. */
  size_t capacity;
};

/** @brief Column descriptors for LedgerEntry. */
static const c_orm_column_meta_t g_ledger_cols[] = {
    C_ORM_DEFINE_COLUMN_EX("acct_id", C_ORM_TYPE_DECIMAL,
                           offsetof(struct LedgerEntry, acct_id), true, false,
                           NULL, false, false, 12, 0, 0, 0),
    C_ORM_COL_CHAR_FIXED(LedgerEntry, code, 8, 37, false),
    C_ORM_COL_DECIMAL(LedgerEntry, balance, 15, 2, false),
    C_ORM_DEFINE_COLUMN_EX("fee", C_ORM_TYPE_DECIMAL,
                           offsetof(struct LedgerEntry, fee), false, true, NULL,
                           false, false, 9, 4, 0, 0),
    C_ORM_DEFINE_COLUMN_EX("tag", C_ORM_TYPE_BINARY_FIXED,
                           offsetof(struct LedgerEntry, tag), false, false,
                           NULL, false, false, 0, 0, 4, 65535)};

/** @brief Table metadata for LedgerEntry. */
static const c_orm_table_meta_t g_ledger_meta = {
    "ledger_entries",
    g_ledger_cols,
    5,
    sizeof(struct LedgerEntry),
    "SELECT acct_id, code, balance, fee, tag FROM ledger_entries",
    "SELECT acct_id, code, balance, fee, tag FROM ledger_entries WHERE acct_id "
    "= ?",
    "INSERT INTO ledger_entries (acct_id, code, balance, fee, tag) VALUES (?, "
    "?, ?, ?, ?)",
    "UPDATE ledger_entries SET acct_id = ?, code = ?, balance = ?, fee = ?, "
    "tag = ? WHERE acct_id = ?",
    "DELETE FROM ledger_entries WHERE acct_id = ?",
    "SELECT acct_id, code, balance, fee, tag FROM ledger_entries WHERE acct_id "
    "= ?",
    false,
    false,
    0,
    0,
    {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
    NULL,
    0};

/**
 * @brief Verifies all newly added c_orm_error_t string representations.
 * @return Greatest test result enum.
 */
TEST test_new_error_strings(void) {
  const char *s = NULL;
  ASSERT_EQ(C_ORM_OK, c_orm_error_to_string(C_ORM_ERROR_CONSTRAINT, &s));
  ASSERT_STR_EQ("C_ORM_ERROR_CONSTRAINT", s);
  ASSERT_EQ(C_ORM_OK, c_orm_error_to_string(C_ORM_ERROR_DRIVER_LOAD, &s));
  ASSERT_STR_EQ("C_ORM_ERROR_DRIVER_LOAD", s);
  ASSERT_EQ(C_ORM_OK, c_orm_error_to_string(C_ORM_ERROR_ODBC_DIAG, &s));
  ASSERT_STR_EQ("C_ORM_ERROR_ODBC_DIAG", s);
  ASSERT_EQ(C_ORM_OK, c_orm_error_to_string(C_ORM_ERROR_TIMEOUT, &s));
  ASSERT_STR_EQ("C_ORM_ERROR_TIMEOUT", s);
  ASSERT_EQ(C_ORM_OK, c_orm_error_to_string(C_ORM_ERROR_DEADLOCK, &s));
  ASSERT_STR_EQ("C_ORM_ERROR_DEADLOCK", s);
  ASSERT_EQ(C_ORM_OK, c_orm_error_to_string(C_ORM_ERROR_TRUNCATION, &s));
  ASSERT_STR_EQ("C_ORM_ERROR_TRUNCATION", s);
  ASSERT_EQ(C_ORM_OK, c_orm_error_to_string(C_ORM_ERROR_DECIMAL_SYNTAX, &s));
  ASSERT_STR_EQ("C_ORM_ERROR_DECIMAL_SYNTAX", s);
  ASSERT_EQ(C_ORM_OK, c_orm_error_to_string(C_ORM_ERROR_DECIMAL_OVERFLOW, &s));
  ASSERT_STR_EQ("C_ORM_ERROR_DECIMAL_OVERFLOW", s);
  ASSERT_EQ(C_ORM_OK, c_orm_error_to_string(C_ORM_ERROR_DECIMAL_INEXACT, &s));
  ASSERT_STR_EQ("C_ORM_ERROR_DECIMAL_INEXACT", s);
  ASSERT_EQ(C_ORM_OK, c_orm_error_to_string(C_ORM_ERROR_CODEPAGE, &s));
  ASSERT_STR_EQ("C_ORM_ERROR_CODEPAGE", s);
  ASSERT_EQ(C_ORM_OK, c_orm_error_to_string(C_ORM_ERROR_COPYBOOK_SYNTAX, &s));
  ASSERT_STR_EQ("C_ORM_ERROR_COPYBOOK_SYNTAX", s);
  PASS();
}

/**
 * @brief Tests decimal parsing, formatting, int64 conversions, and validation.
 * @return Greatest test result enum.
 */
TEST test_decimal_parse_format_int64(void) {
  c_orm_decimal_t d;
  c_orm_decimal_t bad;
  char buf[64];
  size_t len = 0;
  c_orm_int64_t iv = 0;
  int is_z = 0;

  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_decimal_zero(0, NULL));
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW, c_orm_decimal_zero(39, &d));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_zero(2, &d));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_to_str(&d, buf, sizeof(buf), &len));
  ASSERT_STR_EQ("0.00", buf);
  ASSERT_EQ(4U, len);

  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_decimal_from_str(NULL, &d));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_decimal_from_str("1", NULL));
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_SYNTAX, c_orm_decimal_from_str("   ", &d));
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_SYNTAX, c_orm_decimal_from_str(".", &d));
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_SYNTAX, c_orm_decimal_from_str("12.3.4", &d));
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_SYNTAX, c_orm_decimal_from_str("12e+", &d));
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_SYNTAX, c_orm_decimal_from_str("12abc", &d));
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW, c_orm_decimal_from_str("1e38", &d));
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW, c_orm_decimal_from_str("0e-39", &d));
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW, c_orm_decimal_from_str("1e-39", &d));
  ASSERT_EQ(
      C_ORM_ERROR_DECIMAL_OVERFLOW,
      c_orm_decimal_from_str("100000000000000000000000000000000000000", &d));

  /* Leading whitespace, sign, leading zeros, scientific notation */
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("  -00012345.6700  ", &d));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_to_str(&d, buf, sizeof(buf), NULL));
  ASSERT_STR_EQ("-12345.6700", buf);

  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("  +1.25E+3 ", &d));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_to_str(&d, buf, sizeof(buf), NULL));
  ASSERT_STR_EQ("1250", buf);

  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("-0.000", &d));
  ASSERT_EQ(0, (int)d.sign);
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_is_zero(&d, &is_z));
  ASSERT_EQ(1, is_z);
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_to_str(&d, buf, sizeof(buf), NULL));
  ASSERT_STR_EQ("0.000", buf);

  /* Small fractional value < 1 */
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("0.0075", &d));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_to_str(&d, buf, sizeof(buf), NULL));
  ASSERT_STR_EQ("0.0075", buf);
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW, c_orm_decimal_to_str(&d, buf, 0, NULL));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW, c_orm_decimal_to_str(&d, buf, 6, NULL));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_decimal_to_str(NULL, buf, 16, NULL));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_decimal_to_str(&d, NULL, 16, NULL));

  /* Invalid struct validation */
  bad = d;
  bad.precision = 0;
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_decimal_to_str(&bad, buf, 32, NULL));
  bad = d;
  bad.digits[0] = 12;
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_decimal_to_str(&bad, buf, 32, NULL));

  /* Int64 extreme boundaries including INT64_MIN */
  {
    c_orm_int64_t i64_min =
        -((c_orm_int64_t)(((c_orm_uint64_t)1 << 63) - 1U)) - (c_orm_int64_t)1;
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_decimal_from_int64(0, NULL));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_int64(i64_min, &d));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_to_str(&d, buf, sizeof(buf), NULL));
    ASSERT_STR_EQ("-9223372036854775808", buf);
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_to_int64(&d, C_ORM_ROUND_TRUNCATE, &iv));
    ASSERT_EQ(i64_min, iv);
  }

  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("9223372036854775808", &d));
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
            c_orm_decimal_to_int64(&d, C_ORM_ROUND_TRUNCATE, &iv));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("99.5", &d));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_to_int64(&d, C_ORM_ROUND_HALF_UP, &iv));
  ASSERT_EQ((c_orm_int64_t)100, iv);
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_INEXACT,
            c_orm_decimal_to_int64(&d, C_ORM_ROUND_ERROR_IF_INEXACT, &iv));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_decimal_to_int64(NULL, C_ORM_ROUND_TRUNCATE, &iv));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_decimal_to_int64(&d, C_ORM_ROUND_TRUNCATE, NULL));
  PASS();
}

/**
 * @brief Tests exact decimal arithmetic (add, sub, mul, div, cmp, negate,
 * rescale) and all four financial rounding modes.
 * @return Greatest test result enum.
 */
TEST test_decimal_arithmetic_and_rounding(void) {
  c_orm_decimal_t a;
  c_orm_decimal_t b;
  c_orm_decimal_t r;
  char buf[64];
  int cmp = 0;

  /* Classic IEEE-754 trap: 0.1 + 0.2 == 0.30 exactly */
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("0.1", &a));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("0.20", &b));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_add(&a, &b, &r));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_to_str(&r, buf, sizeof(buf), NULL));
  ASSERT_STR_EQ("0.30", buf);

  /* Comparison across scales and signs */
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("0.3", &a));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_cmp(&a, &r, &cmp));
  ASSERT_EQ(0, cmp);

  ASSERT_EQ(C_ORM_OK, c_orm_decimal_negate(&r, &b));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_cmp(&b, &a, &cmp));
  ASSERT_EQ(-1, cmp);
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_cmp(&a, &b, &cmp));
  ASSERT_EQ(1, cmp);

  /* Subtraction cancelling to zero and crossing zero */
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_sub(&a, &r, &b));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_to_str(&b, buf, sizeof(buf), NULL));
  ASSERT_STR_EQ("0.00", buf);

  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("10.00", &a));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("25.75", &b));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_sub(&a, &b, &r));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_to_str(&r, buf, sizeof(buf), NULL));
  ASSERT_STR_EQ("-15.75", buf);
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_sub(&b, &a, &r));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_to_str(&r, buf, sizeof(buf), NULL));
  ASSERT_STR_EQ("15.75", buf);

  /* Negative comparison */
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("-100.5", &a));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("-20.0", &b));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_cmp(&a, &b, &cmp));
  ASSERT_EQ(-1, cmp);

  /* Multiplication & Banker's vs Half-Up vs Truncate rounding */
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("2.25", &a));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("0.1", &b));
  /* 2.25 * 0.1 = 0.225 -> scale 2: trunc=0.22, half_up=0.23, half_even=0.22 */
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_mul(&a, &b, 2, C_ORM_ROUND_TRUNCATE, &r));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_to_str(&r, buf, sizeof(buf), NULL));
  ASSERT_STR_EQ("0.22", buf);
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_mul(&a, &b, 2, C_ORM_ROUND_HALF_UP, &r));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_to_str(&r, buf, sizeof(buf), NULL));
  ASSERT_STR_EQ("0.23", buf);
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_mul(&a, &b, 2, C_ORM_ROUND_HALF_EVEN, &r));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_to_str(&r, buf, sizeof(buf), NULL));
  ASSERT_STR_EQ("0.22", buf);

  /* 2.35 * 0.1 = 0.235 -> half_even rounds odd 3 up to even 4 -> 0.24 */
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("2.35", &a));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_mul(&a, &b, 2, C_ORM_ROUND_HALF_EVEN, &r));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_to_str(&r, buf, sizeof(buf), NULL));
  ASSERT_STR_EQ("0.24", buf);

  /* Carry propagation all the way across 999.995 -> 1000.00 */
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("999.995", &a));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_rescale(&a, 6, 2, C_ORM_ROUND_HALF_UP, &r));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_to_str(&r, buf, sizeof(buf), NULL));
  ASSERT_STR_EQ("1000.00", buf);
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
            c_orm_decimal_rescale(&a, 5, 2, C_ORM_ROUND_HALF_UP, &r));

  /* Division: 1 / 3 at scale 8, 10 / 8 exact, divide by zero */
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("1", &a));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("3", &b));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_div(&a, &b, 6, C_ORM_ROUND_HALF_UP, &r));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_to_str(&r, buf, sizeof(buf), NULL));
  ASSERT_STR_EQ("0.333333", buf);

  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("-2", &a));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_div(&a, &b, 4, C_ORM_ROUND_HALF_UP, &r));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_to_str(&r, buf, sizeof(buf), NULL));
  ASSERT_STR_EQ("-0.6667", buf);
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_INEXACT,
            c_orm_decimal_div(&a, &b, 4, C_ORM_ROUND_ERROR_IF_INEXACT, &r));

  ASSERT_EQ(C_ORM_OK, c_orm_decimal_zero(0, &b));
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
            c_orm_decimal_div(&a, &b, 2, C_ORM_ROUND_HALF_UP, &r));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_div(&b, &a, 2, C_ORM_ROUND_HALF_UP, &r));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_to_str(&r, buf, sizeof(buf), NULL));
  ASSERT_STR_EQ("0.00", buf);
  PASS();
}

/**
 * @brief Tests ODBC SQL_NUMERIC_STRUCT, COBOL COMP-3, Zoned Decimal, and
 * COMP/COMP-5 wire conversions.
 * @return Greatest test result enum.
 */
TEST test_decimal_odbc_and_cobol_wire(void) {
  c_orm_decimal_t d;
  c_orm_decimal_t back;
  unsigned char val16[16];
  unsigned char prec = 0;
  signed char scl = 0;
  unsigned char osign = 0;
  unsigned char wire[64];
  size_t written = 0;
  int cmp = 0;
  c_orm_int64_t iv = 0;

  /* 38-digit max precision round-trip through 128-bit SQL_NUMERIC_STRUCT */
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str(
                          "-999999999999999999999999999999999999.99", &d));
  ASSERT_EQ(C_ORM_OK,
            c_orm_decimal_to_odbc_numeric(&d, val16, &prec, &scl, &osign));
  ASSERT_EQ(38, (int)prec);
  ASSERT_EQ(2, (int)scl);
  ASSERT_EQ(0, (int)osign);
  ASSERT_EQ(C_ORM_OK,
            c_orm_decimal_from_odbc_numeric(val16, prec, scl, osign, &back));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_cmp(&d, &back, &cmp));
  ASSERT_EQ(0, cmp);

  /* All-0xFF 128-bit value (~3.4028e38) exceeds 38 base-10 digits */
  memset(val16, 0xFF, sizeof(val16));
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
            c_orm_decimal_from_odbc_numeric(val16, 38, 0, 1, &back));

  /* COBOL COMP-3: PIC S9(7)V99 COMP-3 for -12345.67 -> 00 12 34 56 7D */
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("-12345.67", &d));
  ASSERT_EQ(C_ORM_OK,
            c_orm_decimal_to_comp3(&d, 7, 2, 1, wire, sizeof(wire), &written));
  ASSERT_EQ(5U, written);
  ASSERT_EQ(0x00, (int)wire[0]);
  ASSERT_EQ(0x12, (int)wire[1]);
  ASSERT_EQ(0x34, (int)wire[2]);
  ASSERT_EQ(0x56, (int)wire[3]);
  ASSERT_EQ(0x7D, (int)wire[4]);

  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_comp3(wire, 5, 2, &back));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_cmp(&d, &back, &cmp));
  ASSERT_EQ(0, cmp);

  /* Unsigned PIC 9(4) COMP-3 for 4321 -> 04 32 1F; negative rejected */
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
            c_orm_decimal_to_comp3(&d, 7, 2, 0, wire, sizeof(wire), NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("4321", &d));
  ASSERT_EQ(C_ORM_OK,
            c_orm_decimal_to_comp3(&d, 4, 0, 0, wire, sizeof(wire), &written));
  ASSERT_EQ(3U, written);
  ASSERT_EQ(0x04, (int)wire[0]);
  ASSERT_EQ(0x32, (int)wire[1]);
  ASSERT_EQ(0x1F, (int)wire[2]);
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_comp3(wire, 3, 0, &back));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_cmp(&d, &back, &cmp));
  ASSERT_EQ(0, cmp);

  /* Alternate z/Architecture sign nybbles 0x0A (+) and 0x0B (-), invalid BCD */
  wire[0] = 0x05;
  wire[1] = 0x0B;
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_comp3(wire, 2, 1, &back));
  ASSERT_EQ(1, (int)back.sign);
  wire[0] = 0xA5; /* invalid digit nybble 0x0A */
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_SYNTAX,
            c_orm_decimal_from_comp3(wire, 2, 1, &back));
  wire[0] = 0x05;
  wire[1] = 0x53; /* invalid sign nybble 0x03 */
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_SYNTAX,
            c_orm_decimal_from_comp3(wire, 2, 1, &back));

  /* COBOL Zoned Decimal: EBCDIC trailing overpunch -123.45 -> F1 F2 F3 F4 D5 */
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("-123.45", &d));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_to_zoned(&d, 3, 2, 1, 0, 0, 1, wire,
                                             sizeof(wire), &written));
  ASSERT_EQ(5U, written);
  ASSERT_EQ(0xF1, (int)wire[0]);
  ASSERT_EQ(0xF2, (int)wire[1]);
  ASSERT_EQ(0xF3, (int)wire[2]);
  ASSERT_EQ(0xF4, (int)wire[3]);
  ASSERT_EQ(0xD5, (int)wire[4]);
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_zoned(wire, 5, 2, 1, 0, 0, 1, &back));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_cmp(&d, &back, &cmp));
  ASSERT_EQ(0, cmp);

  /* ASCII trailing overpunch +120 -> "12{" and -125 -> "12N" */
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("120", &d));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_to_zoned(&d, 3, 0, 1, 0, 0, 0, wire,
                                             sizeof(wire), &written));
  ASSERT_EQ(0, memcmp(wire, "12{", 3));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_zoned(wire, 3, 0, 1, 0, 0, 0, &back));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_cmp(&d, &back, &cmp));
  ASSERT_EQ(0, cmp);

  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("-125", &d));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_to_zoned(&d, 3, 0, 1, 0, 0, 0, wire,
                                             sizeof(wire), &written));
  ASSERT_EQ(0, memcmp(wire, "12N", 3));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_zoned(wire, 3, 0, 1, 0, 0, 0, &back));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_cmp(&d, &back, &cmp));
  ASSERT_EQ(0, cmp);

  /* Leading separate sign in EBCDIC (-0042 -> 60 F0 F0 F4 F2) & ASCII */
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("-42", &d));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_to_zoned(&d, 4, 0, 1, 1, 1, 1, wire,
                                             sizeof(wire), &written));
  ASSERT_EQ(5U, written);
  ASSERT_EQ(0x60, (int)wire[0]);
  ASSERT_EQ(0xF0, (int)wire[1]);
  ASSERT_EQ(0xF2, (int)wire[4]);
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_zoned(wire, 5, 0, 1, 1, 1, 1, &back));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_cmp(&d, &back, &cmp));
  ASSERT_EQ(0, cmp);

  /* Trailing separate sign in ASCII (+07 -> "07+") */
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("7", &d));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_to_zoned(&d, 2, 0, 1, 0, 1, 0, wire,
                                             sizeof(wire), &written));
  ASSERT_EQ(0, memcmp(wire, "07+", 3));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_zoned(wire, 3, 0, 1, 0, 1, 0, &back));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_cmp(&d, &back, &cmp));
  ASSERT_EQ(0, cmp);

  /* COBOL COMP (big-endian) & COMP-5 (native) 2, 4, 8 byte round-trips */
  ASSERT_EQ(C_ORM_OK, c_orm_cobol_comp_byte_len(4, &written));
  ASSERT_EQ(2U, written);
  ASSERT_EQ(C_ORM_OK, c_orm_cobol_comp_byte_len(9, &written));
  ASSERT_EQ(4U, written);
  ASSERT_EQ(C_ORM_OK, c_orm_cobol_comp_byte_len(18, &written));
  ASSERT_EQ(8U, written);
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_cobol_comp_byte_len(0, &written));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_cobol_comp_byte_len(19, &written));

  ASSERT_EQ(C_ORM_OK,
            c_orm_int64_to_cobol_comp(-12345, 2, 1, wire, sizeof(wire)));
  ASSERT_EQ(0xCF, (int)wire[0]);
  ASSERT_EQ(0xC7, (int)wire[1]);
  ASSERT_EQ(C_ORM_OK, c_orm_int64_from_cobol_comp(wire, 2, 1, 1, &iv));
  ASSERT_EQ((c_orm_int64_t)-12345, iv);

  ASSERT_EQ(C_ORM_OK, c_orm_int64_to_cobol_comp(-(c_orm_int64_t)123456789L, 4,
                                                0, wire, sizeof(wire)));
  ASSERT_EQ(C_ORM_OK, c_orm_int64_from_cobol_comp(wire, 4, 1, 0, &iv));
  ASSERT_EQ(-(c_orm_int64_t)123456789L, iv);

  {
    c_orm_int64_t i64_min =
        -((c_orm_int64_t)(((c_orm_uint64_t)1 << 63) - 1U)) - (c_orm_int64_t)1;
    ASSERT_EQ(C_ORM_OK,
              c_orm_int64_to_cobol_comp(i64_min, 8, 1, wire, sizeof(wire)));
    ASSERT_EQ(0x80, (int)wire[0]);
    ASSERT_EQ(C_ORM_OK, c_orm_int64_from_cobol_comp(wire, 8, 1, 1, &iv));
    ASSERT_EQ(i64_min, iv);
    ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
              c_orm_int64_from_cobol_comp(wire, 8, 0, 1, &iv));
  }
  PASS();
}

/**
 * @brief Tests end-to-end ORM persistence, validation, PK lookup, composite
 * keys, CddCVariant JSON, and numeric ordering on SQLite and Memory drivers.
 * @return Greatest test result enum.
 */
TEST test_decimal_orm_e2e_sqlite_and_memory(void) {
  c_orm_db_t *db = NULL;
  struct LedgerEntry row1;
  struct LedgerEntry row2;
  struct LedgerEntry fetched;
  c_orm_decimal_t fee_val;
  c_orm_decimal_t search_id;
  struct CddCVariant ck;
  cdd_c_abstract_struct_t astruct;
  cdd_c_prop_meta_t props[1];
  cdd_c_meta_t cmeta;
  char *json_str = NULL;
  int cmp = 0;

  /* 1. SQLite E2E */
  ASSERT_EQ(C_ORM_OK, c_orm_sqlite_connect(":memory:", &db));
  ASSERT_EQ(C_ORM_OK,
            c_orm_execute_raw(
                db, "CREATE TABLE ledger_entries (acct_id TEXT PRIMARY KEY, "
                    "code TEXT NOT NULL, balance TEXT NOT NULL, fee TEXT, "
                    "tag BLOB NOT NULL);"));

  memset(&row1, 0, sizeof(row1));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("100001", &row1.acct_id));
  memcpy(row1.code, "CHK-0001", 9);
  ASSERT_EQ(C_ORM_OK,
            c_orm_decimal_from_str("9876543210123.45", &row1.balance));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("12.3456", &fee_val));
  row1.fee = &fee_val;
  row1.tag[0] = 0xDE;
  row1.tag[1] = 0xAD;
  row1.tag[2] = 0xBE;
  row1.tag[3] = 0xEF;

  ASSERT_EQ(C_ORM_OK, c_orm_validate(&g_ledger_meta, &row1));
  ASSERT_EQ(C_ORM_OK, c_orm_insert(db, &g_ledger_meta, &row1));

  /* Precision overflow validation check (DECIMAL(15, 2) rejects 14 int digits)
   */
  memset(&row2, 0, sizeof(row2));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("100002", &row2.acct_id));
  memcpy(row2.code, "SAV-0002", 9);
  ASSERT_EQ(C_ORM_OK,
            c_orm_decimal_from_str("10000000000000.00", &row2.balance));
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
            c_orm_validate(&g_ledger_meta, &row2));

  /* Valid second row with NULL fee */
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("-450.05", &row2.balance));
  row2.fee = NULL;
  ASSERT_EQ(C_ORM_OK, c_orm_insert(db, &g_ledger_meta, &row2));

  /* Fetch by decimal PK */
  memset(&fetched, 0, sizeof(fetched));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("100001", &search_id));
  ASSERT_EQ(C_ORM_OK,
            c_orm_find_by_id_decimal(db, &g_ledger_meta, &search_id, &fetched));
  ASSERT_STR_EQ("CHK-0001", fetched.code);
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_cmp(&row1.balance, &fetched.balance, &cmp));
  ASSERT_EQ(0, cmp);
  ASSERT(fetched.fee != NULL);
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_cmp(&fee_val, fetched.fee, &cmp));
  ASSERT_EQ(0, cmp);
  ASSERT_EQ(0, memcmp(row1.tag, fetched.tag, 4));
  ASSERT_EQ(C_ORM_OK, c_orm_free_columns(&g_ledger_meta, &fetched));

  /* Composite key lookup & delete using CDD_C_VARIANT_TYPE_DECIMAL */
  ck.type = CDD_C_VARIANT_TYPE_DECIMAL;
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("100002", &ck.value.d_val));
  memset(&fetched, 0, sizeof(fetched));
  ASSERT_EQ(C_ORM_OK,
            c_orm_find_by_composite_key(db, &g_ledger_meta, 1, &ck, &fetched));
  ASSERT(fetched.fee == NULL);
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_cmp(&row2.balance, &fetched.balance, &cmp));
  ASSERT_EQ(0, cmp);
  ASSERT_EQ(C_ORM_OK, c_orm_free_columns(&g_ledger_meta, &fetched));
  ASSERT_EQ(C_ORM_OK,
            c_orm_delete_by_composite_key(db, &g_ledger_meta, 1, &ck));
  ASSERT_EQ(
      C_ORM_ERROR_NOT_FOUND,
      c_orm_find_by_id_decimal(db, &g_ledger_meta, &ck.value.d_val, &fetched));
  ASSERT_EQ(C_ORM_OK, db->vtable->disconnect(db));

  /* 2. Memory Driver numeric comparison & transaction rollback */
  ASSERT_EQ(C_ORM_OK, c_orm_memory_connect("memory://", &db));
  ASSERT_EQ(C_ORM_OK,
            c_orm_execute_raw(
                db, "CREATE TABLE ledger_entries (acct_id, code, balance, fee, "
                    "tag);"));
  row1.fee = NULL;
  ASSERT_EQ(C_ORM_OK, c_orm_insert(db, &g_ledger_meta, &row1));
  memset(&fetched, 0, sizeof(fetched));
  ASSERT_EQ(C_ORM_OK,
            c_orm_find_by_id_decimal(db, &g_ledger_meta, &search_id, &fetched));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_cmp(&row1.balance, &fetched.balance, &cmp));
  ASSERT_EQ(0, cmp);
  ASSERT_EQ(C_ORM_OK, c_orm_free_columns(&g_ledger_meta, &fetched));
  ASSERT_EQ(C_ORM_OK, db->vtable->disconnect(db));

  /* 3. Abstract struct & lossless JSON round-trip */
  memset(props, 0, sizeof(props));
  props[0].name = "balance";
  props[0].type = "C_ORM_TYPE_DECIMAL";
  props[0].offset = offsetof(struct LedgerEntry, balance);
  cmeta.name = "LedgerEntry";
  cmeta.size = sizeof(struct LedgerEntry);
  cmeta.props = props;
  cmeta.num_props = 1;
  cmeta.driver_ctx = NULL;

  ASSERT_EQ(C_ORM_OK, cdd_c_specific_to_abstract(&astruct, &row1, &cmeta));
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_to_json(&astruct, &json_str));
  ASSERT(json_str != NULL);
  ASSERT(strstr(json_str, "9876543210123.45") != NULL);
  memset(&fetched, 0, sizeof(fetched));
  ASSERT_EQ(C_ORM_OK,
            cdd_c_abstract_to_specific(&fetched, &astruct, &cmeta, 1));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_cmp(&row1.balance, &fetched.balance, &cmp));
  ASSERT_EQ(0, cmp);
  c_orm_system_free(json_str);
  ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_free(&astruct));
  PASS();
}

/**
 * @brief Exercises all boundary, invalid-argument, sticky-bit, and overflow
 * branches across c_orm_decimal_t and COBOL wire functions.
 * @return Greatest test result enum.
 */
TEST test_decimal_exhaustive_errors(void) {
  c_orm_decimal_t a;
  c_orm_decimal_t b;
  c_orm_decimal_t bad;
  c_orm_decimal_t out;
  unsigned char wire[64];
  unsigned char val16[16];
  unsigned char prec = 0;
  signed char scl = 0;
  unsigned char osign = 0;
  size_t sz = 0;
  int cmp = 0;
  c_orm_int64_t iv = 0;

  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("12.340001", &a));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("5.6", &b));
  bad = a;
  bad.sign = 5;

  /* Rescale, negate, is_zero, cmp validation & null checks */
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_decimal_rescale(NULL, 10, 2, C_ORM_ROUND_TRUNCATE, &out));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_decimal_rescale(&a, 10, 2, C_ORM_ROUND_TRUNCATE, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_decimal_rescale(&bad, 10, 2, C_ORM_ROUND_TRUNCATE, &out));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_decimal_rescale(&a, 0, 0, C_ORM_ROUND_TRUNCATE, &out));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_decimal_rescale(&a, 5, 6, C_ORM_ROUND_TRUNCATE, &out));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_decimal_rescale(&a, 10, 2, (c_orm_round_mode_t)99, &out));
  /* Upward scale shift overflow (> 38 digits) & sticky HALF_EVEN (12.345001
   * -> 12.35) */
  ASSERT_EQ(C_ORM_OK,
            c_orm_decimal_from_str("123456789012345678901234567890", &out));
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
            c_orm_decimal_rescale(&out, 38, 12, C_ORM_ROUND_TRUNCATE, &out));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("12.345001", &a));
  ASSERT_EQ(C_ORM_OK,
            c_orm_decimal_rescale(&a, 10, 2, C_ORM_ROUND_HALF_EVEN, &out));
  /* Carry overflow at 38 digits: 999...9.9 rounded to scale 0 at 38-digit cap
   */
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str(
                          "99999999999999999999999999999999999999", &a));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("1", &b));
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW, c_orm_decimal_add(&a, &b, &out));

  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_decimal_is_zero(NULL, &cmp));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_decimal_is_zero(&a, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_decimal_is_zero(&bad, &cmp));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_decimal_negate(NULL, &out));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_decimal_negate(&a, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_decimal_negate(&bad, &out));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_decimal_cmp(NULL, &b, &cmp));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_decimal_cmp(&bad, &b, &cmp));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_decimal_cmp(&b, &bad, &cmp));

  /* Add, sub, mul, div error & edge cases */
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_decimal_add(NULL, &b, &out));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_decimal_add(&bad, &b, &out));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_decimal_add(&b, &bad, &out));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_decimal_mul(NULL, &b, 2, C_ORM_ROUND_HALF_UP, &out));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_decimal_mul(&b, &b, 39, C_ORM_ROUND_HALF_UP, &out));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_decimal_mul(&bad, &b, 2, C_ORM_ROUND_HALF_UP, &out));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_decimal_mul(&b, &bad, 2, C_ORM_ROUND_HALF_UP, &out));
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
            c_orm_decimal_mul(&a, &a, 0, C_ORM_ROUND_TRUNCATE, &out));

  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_decimal_div(NULL, &b, 2, C_ORM_ROUND_HALF_UP, &out));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_decimal_div(&b, &b, 39, C_ORM_ROUND_HALF_UP, &out));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_decimal_div(&bad, &b, 2, C_ORM_ROUND_HALF_UP, &out));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_decimal_div(&b, &bad, 2, C_ORM_ROUND_HALF_UP, &out));
  /* Division with negative shift_pow10 (high-scale dividend, result_scale=0) */
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("0.0000123", &b));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("5", &out));
  ASSERT_EQ(C_ORM_OK,
            c_orm_decimal_div(&b, &out, 0, C_ORM_ROUND_TRUNCATE, &out));
  /* Division overflow: 999...9 (38 digits) / 0.01 */
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("0.01", &b));
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
            c_orm_decimal_div(&a, &b, 0, C_ORM_ROUND_TRUNCATE, &out));

  /* ODBC numeric error & 38-nines boundary cases */
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_decimal_to_odbc_numeric(NULL, val16, &prec, &scl, &osign));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_decimal_to_odbc_numeric(&bad, val16, &prec, &scl, &osign));
  ASSERT_EQ(C_ORM_OK,
            c_orm_decimal_to_odbc_numeric(&a, val16, &prec, &scl, &osign));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_odbc_numeric(val16, 38, 0, 1, &out));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_cmp(&a, &out, &cmp));
  ASSERT_EQ(0, cmp);
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_decimal_from_odbc_numeric(NULL, 10, 2, 1, &out));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_decimal_from_odbc_numeric(val16, 10, -1, 1, &out));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_decimal_from_odbc_numeric(val16, 10, 2, 5, &out));

  /* COMP-3 error & boundary cases */
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_decimal_comp3_byte_len(5, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_decimal_comp3_byte_len(0, &sz));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_decimal_comp3_byte_len(39, &sz));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_decimal_to_comp3(NULL, 5, 2, 1, wire, 8, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_decimal_to_comp3(&b, 0, 0, 1, wire, 8, NULL));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_decimal_to_comp3(&b, 5, 2, 1, wire, 2, NULL));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_decimal_from_comp3(NULL, 2, 0, &out));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_decimal_from_comp3(wire, 0, 0, &out));
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
            c_orm_decimal_from_comp3(wire, 21, 0, &out));
  /* 20-byte COMP-3 (39 nybbles) with non-zero leading nybble overflows 38
   * digits */
  memset(wire, 0, 20);
  wire[0] = 0x10;
  wire[19] = 0x0C;
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
            c_orm_decimal_from_comp3(wire, 20, 0, &out));

  /* Zoned Decimal error & ASCII 'p'..'y' / '}' / leading overpunch cases */
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_decimal_to_zoned(NULL, 3, 0, 1, 0, 0, 0, wire, 8, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_decimal_to_zoned(&b, 0, 0, 1, 0, 0, 0, wire, 8, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_decimal_to_zoned(&b, 3, 0, 0, 0, 1, 0, wire, 8, NULL));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_decimal_to_zoned(&b, 3, 0, 1, 0, 0, 0, wire, 2, NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("-20", &b));
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
            c_orm_decimal_to_zoned(&b, 3, 0, 0, 0, 0, 0, wire, 8, NULL));
  /* Negative zero overpunch ("0}" -> -0 -> normalized 0) & leading overpunch */
  ASSERT_EQ(C_ORM_OK,
            c_orm_decimal_to_zoned(&b, 2, 0, 1, 0, 0, 0, wire, 8, NULL));
  ASSERT_EQ(0, memcmp(wire, "2}", 2));
  ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_zoned((const unsigned char *)"12r", 3,
                                               0, 1, 0, 0, 0, &out));
  ASSERT_EQ(1, (int)out.sign);
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_decimal_from_zoned(NULL, 3, 0, 1, 0, 0, 0, &out));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_decimal_from_zoned(wire, 3, 0, 0, 0, 1, 0, &out));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_decimal_from_zoned(wire, 1, 0, 1, 0, 1, 0, &out));
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
            c_orm_decimal_from_zoned(wire, 40, 0, 0, 0, 0, 0, &out));
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_SYNTAX,
            c_orm_decimal_from_zoned((const unsigned char *)"?12", 3, 0, 1, 1,
                                     1, 0, &out));
  wire[0] = 0x7FU;
  wire[1] = 0xF1U;
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_SYNTAX,
            c_orm_decimal_from_zoned(wire, 2, 0, 1, 1, 1, 1, &out));
  wire[0] = 0xFAU; /* invalid EBCDIC low nybble > 9 */
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_SYNTAX,
            c_orm_decimal_from_zoned(wire, 1, 0, 0, 0, 0, 1, &out));
  wire[0] = 0x40U; /* invalid EBCDIC zone 0x40 */
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_SYNTAX,
            c_orm_decimal_from_zoned(wire, 1, 0, 1, 0, 0, 1, &out));
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_SYNTAX,
            c_orm_decimal_from_zoned((const unsigned char *)"12@", 3, 0, 1, 0,
                                     0, 0, &out));

  /* COMP / COMP-5 error & native-endian 2/8-byte cases */
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_cobol_comp_byte_len(4, NULL));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_int64_to_cobol_comp(1, 2, 1, NULL, 4));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_int64_to_cobol_comp(1, 3, 1, wire, 8));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW, c_orm_int64_to_cobol_comp(1, 4, 1, wire, 2));
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
            c_orm_int64_to_cobol_comp(70000, 2, 1, wire, 8));
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
            c_orm_int64_to_cobol_comp(
                (c_orm_int64_t)5000000 * (c_orm_int64_t)1000, 4, 1, wire, 8));
  ASSERT_EQ(C_ORM_OK, c_orm_int64_to_cobol_comp(1234, 2, 0, wire, 8));
  ASSERT_EQ(C_ORM_OK, c_orm_int64_from_cobol_comp(wire, 2, 1, 0, &iv));
  ASSERT_EQ((c_orm_int64_t)1234, iv);
  ASSERT_EQ(C_ORM_OK, c_orm_int64_to_cobol_comp(987654321L, 8, 0, wire, 8));
  ASSERT_EQ(C_ORM_OK, c_orm_int64_from_cobol_comp(wire, 8, 1, 0, &iv));
  ASSERT_EQ((c_orm_int64_t)987654321L, iv);
  ASSERT_EQ(C_ORM_OK, c_orm_int64_from_cobol_comp(wire, 4, 0, 1, &iv));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_int64_from_cobol_comp(NULL, 4, 1, 1, &iv));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_int64_from_cobol_comp(wire, 4, 1, 1, NULL));
  ASSERT_EQ(C_ORM_ERROR_VALIDATION,
            c_orm_int64_from_cobol_comp(wire, 5, 1, 1, &iv));
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
            c_orm_int64_to_cobol_comp(-40000, 2, 1, wire, 8));
  ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
            c_orm_int64_to_cobol_comp(
                -(c_orm_int64_t)5000000 * (c_orm_int64_t)1000, 4, 1, wire, 8));

  /* Additional exhaustive decimal & COBOL wire branches */
  {
    char sbuf[120];
    c_orm_decimal_t d1;
    c_orm_decimal_t d2;

    /* dec_validate header & digit > 9 checks */
    d1 = a;
    d1.sign = 0;
    d1.precision = 0;
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_decimal_is_zero(&d1, &cmp));
    d1.precision = 39;
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_decimal_is_zero(&d1, &cmp));
    d1.precision = 10;
    d1.scale = 39;
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_decimal_is_zero(&d1, &cmp));
    d1.scale = 2;
    d1.digits[0] = 10;
    ASSERT_EQ(C_ORM_ERROR_VALIDATION, c_orm_decimal_is_zero(&d1, &cmp));

    /* dec_round_from_buf: ERROR_IF_INEXACT exact vs mid!=0 vs sticky!=0,
     * HALF_EVEN all cases, drop == len (scale 38 -> 0), mantissa overflow on
     * mul, and 38-nine carry overflow on div */
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_decimal_rescale(&a, 10, 2, (c_orm_round_mode_t)-1, &out));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("1.200", &d1));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_rescale(
                            &d1, 10, 1, C_ORM_ROUND_ERROR_IF_INEXACT, &out));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("1.250", &d1));
    ASSERT_EQ(
        C_ORM_ERROR_DECIMAL_INEXACT,
        c_orm_decimal_rescale(&d1, 10, 1, C_ORM_ROUND_ERROR_IF_INEXACT, &out));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("1.201", &d1));
    ASSERT_EQ(
        C_ORM_ERROR_DECIMAL_INEXACT,
        c_orm_decimal_rescale(&d1, 10, 1, C_ORM_ROUND_ERROR_IF_INEXACT, &out));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("1.24", &d1));
    ASSERT_EQ(C_ORM_OK,
              c_orm_decimal_rescale(&d1, 10, 1, C_ORM_ROUND_HALF_EVEN, &out));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("1.26", &d1));
    ASSERT_EQ(C_ORM_OK,
              c_orm_decimal_rescale(&d1, 10, 1, C_ORM_ROUND_HALF_EVEN, &out));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("1.25", &d1));
    ASSERT_EQ(C_ORM_OK,
              c_orm_decimal_rescale(&d1, 10, 1, C_ORM_ROUND_HALF_EVEN, &out));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("1.35", &d1));
    ASSERT_EQ(C_ORM_OK,
              c_orm_decimal_rescale(&d1, 10, 1, C_ORM_ROUND_HALF_EVEN, &out));

    /* scale 38 -> 0 with HALF_EVEN hits drop == len (lsb = 0) */
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str(
                            "0.50000000000000000000000000000000000000", &d1));
    ASSERT_EQ(C_ORM_OK,
              c_orm_decimal_rescale(&d1, 10, 0, C_ORM_ROUND_HALF_EVEN, &out));

    /* 38-nine carry overflow inside dec_round_from_buf:
     * (10^38 - 2) / (1 - 1.6e-37) = 999...999.6 -> rounds up to 10^38! */
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str(
                            "99999999999999999999999999999999999998", &d1));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str(
                            "0.99999999999999999999999999999999999984", &d2));
    ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
              c_orm_decimal_div(&d1, &d2, 0, C_ORM_ROUND_HALF_UP, &out));

    /* Mantissa overflow when cur_scale > target_scale in mul */
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str(
                            "99999999999999999999999999999999999999", &d1));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("10.00", &d2));
    ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
              c_orm_decimal_mul(&d1, &d2, 1, C_ORM_ROUND_TRUNCATE, &out));

    /* from_str whitespace (\t\r\n), >80 digits, >76 frac digits, huge exp,
     * 0e-50 */
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("\t\r\n +1.5 \t\r\n", &out));
    memset(sbuf, '1', 85);
    sbuf[85] = '\0';
    ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW, c_orm_decimal_from_str(sbuf, &out));
    sbuf[0] = '0';
    sbuf[1] = '.';
    memset(sbuf + 2, '0', 78);
    sbuf[80] = '\0';
    ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW, c_orm_decimal_from_str(sbuf, &out));
    ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
              c_orm_decimal_from_str("1e99999", &out));
    ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
              c_orm_decimal_from_str("0e-50", &out));

    /* to_str NULL/zero buf & scale=38 zero ("0.000...0") & from_int64 NULL */
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_decimal_to_str(&d1, NULL, 10, NULL));
    ASSERT_EQ(C_ORM_ERROR_OVERFLOW, c_orm_decimal_to_str(&d1, sbuf, 0, NULL));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_zero(38, &d1));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_to_str(&d1, sbuf, sizeof(sbuf), NULL));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_decimal_from_int64(0, NULL));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_decimal_to_int64(&d1, C_ORM_ROUND_TRUNCATE, NULL));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_decimal_to_int64(&bad, C_ORM_ROUND_TRUNCATE, &iv));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("10.5", &d2));
    ASSERT_EQ(C_ORM_ERROR_DECIMAL_INEXACT,
              c_orm_decimal_to_int64(&d2, C_ORM_ROUND_ERROR_IF_INEXACT, &iv));

    /* cmp zero/negative combinations & add/sub borrow when aa > bb */
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_decimal_cmp(&d1, NULL, &cmp));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_decimal_cmp(&d1, &d1, NULL));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_zero(0, &d1));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("-5", &d2));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_cmp(&d2, &d1, &cmp));
    ASSERT_EQ(-1, cmp);
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_cmp(&d1, &d2, &cmp));
    ASSERT_EQ(1, cmp);
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("-3", &out));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_cmp(&d2, &out, &cmp));
    ASSERT_EQ(-1, cmp);

    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_decimal_add(&d1, NULL, &out));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_decimal_add(&d1, &d2, NULL));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("10.0", &d1));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("1.5", &d2));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_sub(&d1, &d2, &out));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_sub(&d2, &d1, &out));

    /* mul & div NULL / mode / zero / top_a < drop_right cases */
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_decimal_mul(&d1, NULL, 2, C_ORM_ROUND_HALF_UP, &out));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_decimal_mul(&d1, &d2, 2, C_ORM_ROUND_HALF_UP, NULL));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_decimal_div(&d1, NULL, 2, C_ORM_ROUND_HALF_UP, &out));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_decimal_div(&d1, &d2, 2, C_ORM_ROUND_HALF_UP, NULL));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_decimal_div(&d1, &d2, 2, (c_orm_round_mode_t)-1, &out));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_decimal_div(&d1, &d2, 2, (c_orm_round_mode_t)99, &out));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_zero(0, &d1));
    ASSERT_EQ(C_ORM_OK,
              c_orm_decimal_div(&d1, &d2, 2, C_ORM_ROUND_HALF_UP, &out));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("0.00001", &d1));
    ASSERT_EQ(
        C_ORM_ERROR_DECIMAL_INEXACT,
        c_orm_decimal_div(&d1, &d2, 0, C_ORM_ROUND_ERROR_IF_INEXACT, &out));

    /* ODBC numeric NULL pointers & >38-digit 128-bit overflow (all 0xFF) */
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_decimal_to_odbc_numeric(&d2, NULL, &prec, &scl, &osign));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_decimal_to_odbc_numeric(&d2, val16, NULL, &scl, &osign));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_decimal_to_odbc_numeric(&d2, val16, &prec, NULL, &osign));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_decimal_to_odbc_numeric(&d2, val16, &prec, &scl, NULL));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_decimal_from_odbc_numeric(val16, 10, 2, 1, NULL));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_decimal_from_odbc_numeric(val16, 10, 39, 1, &out));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_decimal_from_odbc_numeric(val16, 39, 2, 1, &out));
    memset(val16, 0xFF, 16);
    ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
              c_orm_decimal_from_odbc_numeric(val16, 38, 0, 1, &out));
    memset(val16, 0, 16);
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_odbc_numeric(val16, 2, 4, 0, &out));
    ASSERT_EQ(4U, (unsigned int)out.precision);
    ASSERT_EQ(0U, (unsigned int)out.sign);
    val16[0] = 5U;
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_odbc_numeric(val16, 10, 0, 0, &out));
    ASSERT_EQ(10U, (unsigned int)out.precision);

    /* COMP-3 rescale overflow, unsigned negative overflow, 20-byte valid 38-dig
     */
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_decimal_to_comp3(&d2, 5, 2, 1, NULL, 8, NULL));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_decimal_to_comp3(&d2, 30, 10, 1, wire, 32, NULL));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("99999", &d1));
    ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
              c_orm_decimal_to_comp3(&d1, 2, 0, 1, wire, 8, NULL));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("-5", &d1));
    ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
              c_orm_decimal_to_comp3(&d1, 3, 0, 0, wire, 8, NULL));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_decimal_from_comp3(wire, 2, 0, NULL));
    ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
              c_orm_decimal_from_comp3(wire, 2, 39, &out));
    ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
              c_orm_decimal_from_comp3(wire, 2, 5, &out));
    memset(wire, 0, 20);
    wire[0] = 0x01;
    wire[19] = 0x0C;
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_comp3(wire, 20, 0, &out));

    /* Zoned Decimal: rescale overflow, positive zero overpunch '{', space
     * separate signs in ASCII & EBCDIC, EBCDIC 0xA0/0xE0/0xB0 zones, ASCII
     * '}' and 'J'..'R' */
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_decimal_to_zoned(&d2, 3, 0, 1, 0, 0, 0, NULL, 8, NULL));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_decimal_to_zoned(&d2, 30, 10, 1, 0, 0, 0, wire, 64, NULL));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("9999", &d1));
    ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
              c_orm_decimal_to_zoned(&d1, 2, 0, 1, 0, 0, 0, wire, 8, NULL));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_zero(0, &d1));
    ASSERT_EQ(C_ORM_OK,
              c_orm_decimal_to_zoned(&d1, 2, 0, 1, 0, 0, 0, wire, 8, NULL));
    ASSERT_EQ(0, memcmp(wire, "0{", 2));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_decimal_from_zoned(wire, 2, 0, 1, 0, 0, 0, NULL));
    ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
              c_orm_decimal_from_zoned(wire, 2, 5, 1, 0, 0, 0, &out));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_zoned((const unsigned char *)" 12",
                                                 3, 0, 1, 1, 1, 0, &out));
    wire[0] = 0x40U;
    wire[1] = 0xF1U;
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_zoned(wire, 2, 0, 1, 1, 1, 1, &out));
    wire[0] = 0xC2U;
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_zoned(wire, 1, 0, 1, 0, 0, 1, &out));
    wire[0] = 0xA2U;
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_zoned(wire, 1, 0, 1, 0, 0, 1, &out));
    wire[0] = 0xE2U;
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_zoned(wire, 1, 0, 1, 0, 0, 1, &out));
    wire[0] = 0xB2U;
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_zoned(wire, 1, 0, 1, 0, 0, 1, &out));
    ASSERT_EQ(1, (int)out.sign);
    ASSERT_EQ(C_ORM_ERROR_DECIMAL_SYNTAX,
              c_orm_decimal_from_zoned((const unsigned char *)"12!", 3, 0, 1, 0,
                                       0, 0, &out));
    ASSERT_EQ(C_ORM_ERROR_DECIMAL_SYNTAX,
              c_orm_decimal_from_zoned((const unsigned char *)"12z", 3, 0, 1, 0,
                                       0, 0, &out));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_zoned((const unsigned char *)"12}",
                                                 3, 0, 1, 0, 0, 0, &out));
    ASSERT_EQ(1, (int)out.sign);
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_zoned((const unsigned char *)"12J",
                                                 3, 0, 1, 0, 0, 0, &out));
    ASSERT_EQ(1, (int)out.sign);
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_zoned((const unsigned char *)"12{",
                                                 3, 0, 1, 0, 0, 0, &out));
    ASSERT_EQ(0, (int)out.sign);

    /* Final branch closures:
     * 1. dec_normalize top < 0 with scale > 0 & negate(0) & cmp(0, 0) & cmp(+5,
     * -3) */
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_zero(0, &d1));
    ASSERT_EQ(C_ORM_OK,
              c_orm_decimal_rescale(&d1, 5, 2, C_ORM_ROUND_TRUNCATE, &out));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_negate(&out, &out));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_cmp(&d1, &out, &cmp));
    ASSERT_EQ(0, cmp);
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("5", &d1));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("-3", &d2));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_cmp(&d1, &d2, &cmp));
    ASSERT_EQ(1, cmp);

    /* 2. 8333...33 (38 digits) * 1.2 = 999...99.6 -> 38-nine carry overflow! */
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str(
                            "83333333333333333333333333333333333333", &d1));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("1.2", &d2));
    ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
              c_orm_decimal_mul(&d1, &d2, 0, C_ORM_ROUND_HALF_UP, &out));

    /* 3. from_str "0.00" & "0e+5", to_str(NULL, ...), rescale precision=39 */
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("0.00", &out));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("0e+5", &out));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_decimal_to_str(NULL, sbuf, 10, NULL));
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_decimal_rescale(&d1, 39, 0, C_ORM_ROUND_TRUNCATE, &out));

    /* 4. div with shift_pow10 < 0 and top_a >= drop_right ("12.3456" / "2" at
     * scale 0) + "1" / "1000" at scale 38 (num_len > 40 with leading zero quot)
     */
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("12.3456", &d1));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("2", &d2));
    ASSERT_EQ(C_ORM_OK,
              c_orm_decimal_div(&d1, &d2, 0, C_ORM_ROUND_TRUNCATE, &out));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("12.0000", &d1));
    ASSERT_EQ(C_ORM_OK,
              c_orm_decimal_div(&d1, &d2, 0, C_ORM_ROUND_TRUNCATE, &out));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("1", &d1));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("1000", &d2));
    ASSERT_EQ(C_ORM_OK,
              c_orm_decimal_div(&d1, &d2, 38, C_ORM_ROUND_TRUNCATE, &out));

    /* 5. Negative to_odbc_numeric & COMP-3 0x0A, 0x0E, 0x0B sign nybbles */
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("-12.34", &d1));
    ASSERT_EQ(C_ORM_OK,
              c_orm_decimal_to_odbc_numeric(&d1, val16, &prec, &scl, &osign));
    ASSERT_EQ(0U, (unsigned int)osign);
    wire[0] = 0x12U;
    wire[1] = 0x3AU;
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_comp3(wire, 2, 0, &out));
    wire[1] = 0x3EU;
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_comp3(wire, 2, 0, &out));
    wire[1] = 0x3BU;
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_comp3(wire, 2, 0, &out));
    ASSERT_EQ(1, (int)out.sign);

    /* 6. Zoned Decimal remaining sign/overpunch/EBCDIC/ASCII/unsigned branches
     */
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("12", &d1));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("-12", &d2));
    /* Unsigned to_zoned & from_zoned (ASCII & EBCDIC) */
    ASSERT_EQ(C_ORM_OK,
              c_orm_decimal_to_zoned(&d1, 3, 0, 0, 0, 0, 0, wire, 8, NULL));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_zoned(wire, 3, 0, 0, 0, 0, 0, &out));
    ASSERT_EQ(C_ORM_OK,
              c_orm_decimal_to_zoned(&d1, 3, 0, 0, 0, 0, 1, wire, 8, NULL));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_zoned(wire, 3, 0, 0, 0, 0, 1, &out));
    /* Trailing separate sign in EBCDIC & ASCII (+ and -) */
    ASSERT_EQ(C_ORM_OK,
              c_orm_decimal_to_zoned(&d1, 2, 0, 1, 0, 1, 1, wire, 8, NULL));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_zoned(wire, 3, 0, 1, 0, 1, 1, &out));
    ASSERT_EQ(C_ORM_OK,
              c_orm_decimal_to_zoned(&d2, 2, 0, 1, 0, 1, 1, wire, 8, NULL));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_zoned(wire, 3, 0, 1, 0, 1, 1, &out));
    ASSERT_EQ(C_ORM_OK,
              c_orm_decimal_to_zoned(&d2, 2, 0, 1, 0, 1, 0, wire, 8, NULL));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_zoned(wire, 3, 0, 1, 0, 1, 0, &out));
    ASSERT_EQ(1, (int)out.sign);
    /* Leading overpunch in ASCII & EBCDIC + non-overpunch slot syntax error */
    ASSERT_EQ(C_ORM_OK,
              c_orm_decimal_to_zoned(&d1, 2, 0, 1, 1, 0, 0, wire, 8, NULL));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_zoned(wire, 2, 0, 1, 1, 0, 0, &out));
    ASSERT_EQ(C_ORM_OK,
              c_orm_decimal_to_zoned(&d2, 2, 0, 1, 1, 0, 1, wire, 8, NULL));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_zoned(wire, 2, 0, 1, 1, 0, 1, &out));
    wire[0] = 0xC1U;
    wire[1] = 0xD2U; /* zone 0xC0 in non-overpunch slot when sign_leading=0 */
    ASSERT_EQ(C_ORM_ERROR_DECIMAL_SYNTAX,
              c_orm_decimal_from_zoned(wire, 2, 0, 1, 0, 0, 1, &out));
    ASSERT_EQ(C_ORM_ERROR_DECIMAL_SYNTAX,
              c_orm_decimal_from_zoned((const unsigned char *)"A2", 2, 0, 1, 0,
                                       0, 0, &out));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_zoned((const unsigned char *)"12I",
                                                 3, 0, 1, 0, 0, 0, &out));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_zoned((const unsigned char *)"12R",
                                                 3, 0, 1, 0, 0, 0, &out));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_zoned((const unsigned char *)"12p",
                                                 3, 0, 1, 0, 0, 0, &out));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_zoned((const unsigned char *)"12y",
                                                 3, 0, 1, 0, 0, 0, &out));

    /* 7. Positive unsigned 8-byte COMP + remaining -0/0/+5/leading-sep cases */
    ASSERT_EQ(C_ORM_OK, c_orm_int64_to_cobol_comp(42, 8, 1, wire, 8));
    ASSERT_EQ(C_ORM_OK, c_orm_int64_from_cobol_comp(wire, 8, 0, 1, &iv));
    ASSERT_EQ((c_orm_int64_t)42, iv);

    ASSERT_EQ(C_ORM_OK, c_orm_decimal_zero(0, &d1));
    d1.sign = 1; /* unnormalized -0 */
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_to_str(&d1, sbuf, sizeof(sbuf), NULL));
    ASSERT_STR_EQ("0", sbuf);
    ASSERT_EQ(C_ORM_OK,
              c_orm_decimal_to_odbc_numeric(&d1, val16, &prec, &scl, &osign));
    ASSERT_EQ(1U, (unsigned int)osign);

    ASSERT_EQ(C_ORM_OK, c_orm_decimal_zero(0, &d1));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("5", &d2));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_cmp(&d2, &d1, &cmp));
    ASSERT_EQ(1, cmp);
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_cmp(&d1, &d2, &cmp));
    ASSERT_EQ(-1, cmp);

    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("12", &d1));
    ASSERT_EQ(C_ORM_OK, c_orm_decimal_from_str("-12", &d2));
    ASSERT_EQ(C_ORM_OK,
              c_orm_decimal_to_zoned(&d1, 2, 0, 1, 1, 1, 0, wire, 8, NULL));
    ASSERT_EQ(C_ORM_OK,
              c_orm_decimal_to_zoned(&d2, 2, 0, 1, 1, 1, 0, wire, 8, NULL));
    ASSERT_EQ(C_ORM_OK,
              c_orm_decimal_to_zoned(&d1, 2, 0, 1, 1, 1, 1, wire, 8, NULL));
    ASSERT_EQ(C_ORM_OK,
              c_orm_decimal_to_zoned(&d2, 2, 0, 1, 1, 1, 1, wire, 8, NULL));
  }
  PASS();
}

/**
 * @brief Suite registration for fixed-point decimal and COBOL numeric wire
 * tests.
 * @param decimal_cobol_suite Suite runner function name.
 */
SUITE(decimal_cobol_suite) {
  RUN_TEST(test_new_error_strings);
  RUN_TEST(test_decimal_parse_format_int64);
  RUN_TEST(test_decimal_arithmetic_and_rounding);
  RUN_TEST(test_decimal_odbc_and_cobol_wire);
  RUN_TEST(test_decimal_orm_e2e_sqlite_and_memory);
  RUN_TEST(test_decimal_exhaustive_errors);
}
