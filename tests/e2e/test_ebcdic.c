/**
 * @file test_ebcdic.c
 * @brief Exhaustive unit tests for IBM EBCDIC codepages (IBM037, IBM1047,
 * IBM500, IBM1140), COBOL fixed-width string formatting, and UTF-16 GRAPHIC.
 */

/* clang-format off */
#include "c_orm_ebcdic.h"
#define GREATEST_USE_LONGJMP 0
#include <greatest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* clang-format on */

/**
 * @brief Tests CCSID <-> codepage enum resolution and error paths.
 * @return Greatest test result enum.
 */
TEST test_ebcdic_ccsid_mapping(void) {
  c_orm_codepage_t cp;
  unsigned short ccsid = 0;

  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_codepage_from_ccsid(37, NULL));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_codepage_to_ccsid(C_ORM_CP_IBM037, NULL));
  ASSERT_EQ(C_ORM_ERROR_CODEPAGE, c_orm_codepage_from_ccsid(9999, &cp));
  ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
            c_orm_codepage_to_ccsid((c_orm_codepage_t)99, &ccsid));

  ASSERT_EQ(C_ORM_OK, c_orm_codepage_from_ccsid(0, &cp));
  ASSERT_EQ(C_ORM_CP_UTF8, cp);
  ASSERT_EQ(C_ORM_OK, c_orm_codepage_from_ccsid(1208, &cp));
  ASSERT_EQ(C_ORM_CP_UTF8, cp);
  ASSERT_EQ(C_ORM_OK, c_orm_codepage_from_ccsid(367, &cp));
  ASSERT_EQ(C_ORM_CP_ASCII, cp);
  ASSERT_EQ(C_ORM_OK, c_orm_codepage_from_ccsid(819, &cp));
  ASSERT_EQ(C_ORM_CP_ASCII, cp);
  ASSERT_EQ(C_ORM_OK, c_orm_codepage_from_ccsid(37, &cp));
  ASSERT_EQ(C_ORM_CP_IBM037, cp);
  ASSERT_EQ(C_ORM_OK, c_orm_codepage_from_ccsid(1047, &cp));
  ASSERT_EQ(C_ORM_CP_IBM1047, cp);
  ASSERT_EQ(C_ORM_OK, c_orm_codepage_from_ccsid(500, &cp));
  ASSERT_EQ(C_ORM_CP_IBM500, cp);
  ASSERT_EQ(C_ORM_OK, c_orm_codepage_from_ccsid(1140, &cp));
  ASSERT_EQ(C_ORM_CP_IBM1140, cp);

  ASSERT_EQ(C_ORM_OK, c_orm_codepage_to_ccsid(C_ORM_CP_UTF8, &ccsid));
  ASSERT_EQ(1208U, (unsigned int)ccsid);
  ASSERT_EQ(C_ORM_OK, c_orm_codepage_to_ccsid(C_ORM_CP_ASCII, &ccsid));
  ASSERT_EQ(819U, (unsigned int)ccsid);
  ASSERT_EQ(C_ORM_OK, c_orm_codepage_to_ccsid(C_ORM_CP_IBM037, &ccsid));
  ASSERT_EQ(37U, (unsigned int)ccsid);
  ASSERT_EQ(C_ORM_OK, c_orm_codepage_to_ccsid(C_ORM_CP_IBM1047, &ccsid));
  ASSERT_EQ(1047U, (unsigned int)ccsid);
  ASSERT_EQ(C_ORM_OK, c_orm_codepage_to_ccsid(C_ORM_CP_IBM500, &ccsid));
  ASSERT_EQ(500U, (unsigned int)ccsid);
  ASSERT_EQ(C_ORM_OK, c_orm_codepage_to_ccsid(C_ORM_CP_IBM1140, &ccsid));
  ASSERT_EQ(1140U, (unsigned int)ccsid);
  PASS();
}

/**
 * @brief Verifies full 256-byte bijective round-trips for IBM037, IBM1047,
 * IBM500, and IBM1140, plus famous bracket/caret/Euro positions.
 * @return Greatest test result enum.
 */
TEST test_ebcdic_bijections_and_euro(void) {
  c_orm_codepage_t cps[4];
  size_t c;
  unsigned char all256[256];
  unsigned char roundtrip[256];
  char utf8_buf[1024];
  size_t u8_len = 0;
  size_t eb_len = 0;
  size_t i;

  cps[0] = C_ORM_CP_IBM037;
  cps[1] = C_ORM_CP_IBM1047;
  cps[2] = C_ORM_CP_IBM500;
  cps[3] = C_ORM_CP_IBM1140;

  for (i = 0; i < 256U; ++i) {
    all256[i] = (unsigned char)i;
  }

  for (c = 0; c < 4U; ++c) {
    ASSERT_EQ(C_ORM_OK, c_orm_ebcdic_to_utf8(cps[c], all256, 256U, utf8_buf,
                                             sizeof(utf8_buf), &u8_len));
    ASSERT_EQ(C_ORM_OK,
              c_orm_utf8_to_ebcdic(cps[c], utf8_buf, u8_len, roundtrip,
                                   sizeof(roundtrip), &eb_len));
    ASSERT_EQ(256U, eb_len);
    ASSERT_EQ(0, memcmp(all256, roundtrip, 256U));
  }

  /* Verify exact IBM037 vs IBM1047 vs IBM500 bracket & caret positions */
  ASSERT_EQ(C_ORM_OK, c_orm_utf8_to_ebcdic(C_ORM_CP_IBM037, "[]^", 3, roundtrip,
                                           8, &eb_len));
  ASSERT_EQ(0xBA, (int)roundtrip[0]);
  ASSERT_EQ(0xBB, (int)roundtrip[1]);
  ASSERT_EQ(0xB0, (int)roundtrip[2]);

  ASSERT_EQ(C_ORM_OK, c_orm_utf8_to_ebcdic(C_ORM_CP_IBM1047, "[]^", 3,
                                           roundtrip, 8, &eb_len));
  ASSERT_EQ(0xAD, (int)roundtrip[0]);
  ASSERT_EQ(0xBD, (int)roundtrip[1]);
  ASSERT_EQ(0x5F, (int)roundtrip[2]);

  ASSERT_EQ(C_ORM_OK, c_orm_utf8_to_ebcdic(C_ORM_CP_IBM500, "[]^!", 4,
                                           roundtrip, 8, &eb_len));
  ASSERT_EQ(0x4A, (int)roundtrip[0]);
  ASSERT_EQ(0x5A, (int)roundtrip[1]);
  ASSERT_EQ(0x5F, (int)roundtrip[2]);
  ASSERT_EQ(0x4F, (int)roundtrip[3]);

  /* IBM1140 maps U+20AC Euro ("\xE2\x82\xAC") <-> 0x9F; IBM037 rejects it */
  ASSERT_EQ(C_ORM_OK, c_orm_utf8_to_ebcdic(C_ORM_CP_IBM1140, "100\xE2\x82\xAC",
                                           6, roundtrip, 8, &eb_len));
  ASSERT_EQ(4U, eb_len);
  ASSERT_EQ(0xF1, (int)roundtrip[0]);
  ASSERT_EQ(0x9F, (int)roundtrip[3]);
  ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
            c_orm_utf8_to_ebcdic(C_ORM_CP_IBM037, "\xE2\x82\xAC", 3, roundtrip,
                                 8, &eb_len));

  /* Inplace ASCII <-> EBCDIC */
  memcpy(roundtrip, "HELLO 123", 9);
  ASSERT_EQ(C_ORM_OK,
            c_orm_ascii_to_ebcdic_inplace(C_ORM_CP_IBM037, roundtrip, 9));
  ASSERT_EQ(0xC8, (int)roundtrip[0]); /* 'H' */
  ASSERT_EQ(0x40, (int)roundtrip[5]); /* ' ' */
  ASSERT_EQ(0xF1, (int)roundtrip[6]); /* '1' */
  ASSERT_EQ(C_ORM_OK,
            c_orm_ebcdic_to_ascii_inplace(C_ORM_CP_IBM037, roundtrip, 9));
  ASSERT_EQ(0, memcmp(roundtrip, "HELLO 123", 9));

  /* IBM1140 0x9F (Euro > 0xFF) rejected by 8-bit inplace ASCII converter */
  roundtrip[0] = 0x9F;
  ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
            c_orm_ebcdic_to_ascii_inplace(C_ORM_CP_IBM1140, roundtrip, 1));
  PASS();
}

/**
 * @brief Tests COBOL PIC X fixed-width padding, right-justification,
 * truncation, and trailing-space extraction.
 * @return Greatest test result enum.
 */
TEST test_cobol_fixed_alpha_helpers(void) {
  unsigned char raw[16];
  char out[32];
  size_t len = 0;

  /* Left-justified EBCDIC space-padded (0x40) */
  ASSERT_EQ(C_ORM_OK, c_orm_cobol_format_alpha("SMITH", 8, C_ORM_CP_IBM037, 0,
                                               0, raw, sizeof(raw)));
  ASSERT_EQ(0xE2, (int)raw[0]); /* 'S' */
  ASSERT_EQ(0x40, (int)raw[5]);
  ASSERT_EQ(0x40, (int)raw[7]);

  ASSERT_EQ(C_ORM_OK, c_orm_cobol_extract_alpha(raw, 8, C_ORM_CP_IBM037, 1, out,
                                                sizeof(out), &len));
  ASSERT_STR_EQ("SMITH", out);
  ASSERT_EQ(5U, len);

  ASSERT_EQ(C_ORM_OK, c_orm_cobol_extract_alpha(raw, 8, C_ORM_CP_IBM037, 0, out,
                                                sizeof(out), &len));
  ASSERT_STR_EQ("SMITH   ", out);
  ASSERT_EQ(8U, len);

  /* Right-justified ASCII space-padded (0x20) & truncation checks */
  ASSERT_EQ(C_ORM_OK, c_orm_cobol_format_alpha("42", 5, C_ORM_CP_ASCII, 0, 1,
                                               raw, sizeof(raw)));
  ASSERT_EQ(0, memcmp(raw, "   42", 5));

  ASSERT_EQ(C_ORM_ERROR_TRUNCATION,
            c_orm_cobol_format_alpha("ABCDEFG", 4, C_ORM_CP_ASCII, 0, 0, raw,
                                     sizeof(raw)));
  ASSERT_EQ(C_ORM_OK, c_orm_cobol_format_alpha("ABCDEFG", 4, C_ORM_CP_ASCII, 1,
                                               0, raw, sizeof(raw)));
  ASSERT_EQ(0, memcmp(raw, "ABCD", 4));
  ASSERT_EQ(C_ORM_OK, c_orm_cobol_format_alpha("ABCDEFG", 4, C_ORM_CP_ASCII, 1,
                                               1, raw, sizeof(raw)));
  ASSERT_EQ(0, memcmp(raw, "DEFG", 4));

  /* Empty / NULL string pads entire field with spaces */
  ASSERT_EQ(C_ORM_OK, c_orm_cobol_format_alpha(NULL, 4, C_ORM_CP_IBM037, 0, 0,
                                               raw, sizeof(raw)));
  ASSERT_EQ(0x40, (int)raw[0]);
  ASSERT_EQ(0x40, (int)raw[3]);
  PASS();
}

/**
 * @brief Tests UTF-8 <-> UTF-16LE / UTF-16BE transcoders including Astral
 * surrogate pairs, malformed UTF-8, and unpaired surrogate rejection.
 * @return Greatest test result enum.
 */
TEST test_utf16_graphic_transcoders(void) {
  /* "A" + Euro U+20AC + Grinning Face U+1F600 (D83D DE00) */
  const char *sample = "A\xE2\x82\xAC\xF0\x9F\x98\x80";
  unsigned short u16_host[16];
  unsigned short u16_be[16];
  const unsigned char *be_bytes;
  char back[32];
  size_t units = 0;
  size_t bytes = 0;

  ASSERT_EQ(C_ORM_OK, c_orm_utf8_to_utf16(sample, strlen(sample), 0, u16_host,
                                          16, &units));
  ASSERT_EQ(4U, units);
  ASSERT_EQ(0x0041U, (unsigned int)u16_host[0]);
  ASSERT_EQ(0x20ACU, (unsigned int)u16_host[1]);
  ASSERT_EQ(0xD83DU, (unsigned int)u16_host[2]);
  ASSERT_EQ(0xDE00U, (unsigned int)u16_host[3]);
  ASSERT_EQ(0x0000U, (unsigned int)u16_host[4]);

  ASSERT_EQ(C_ORM_OK, c_orm_utf16_to_utf8(u16_host, units, 0, back,
                                          sizeof(back), &bytes));
  ASSERT_STR_EQ(sample, back);
  ASSERT_EQ(strlen(sample), bytes);

  /* Big-endian Db2 GRAPHIC (CCSID 1200) wire order check */
  ASSERT_EQ(C_ORM_OK,
            c_orm_utf8_to_utf16(sample, strlen(sample), 1, u16_be, 16, &units));
  be_bytes = (const unsigned char *)u16_be;
  ASSERT_EQ(0x00, (int)be_bytes[0]);
  ASSERT_EQ(0x41, (int)be_bytes[1]);
  ASSERT_EQ(0x20, (int)be_bytes[2]);
  ASSERT_EQ(0xAC, (int)be_bytes[3]);
  ASSERT_EQ(C_ORM_OK,
            c_orm_utf16_to_utf8(u16_be, units, 1, back, sizeof(back), &bytes));
  ASSERT_STR_EQ(sample, back);

  /* Malformed UTF-8 & unpaired UTF-16 surrogates */
  ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
            c_orm_utf8_to_utf16("\xC0\xAF", 2, 0, u16_host, 8, NULL));
  ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
            c_orm_utf8_to_utf16("\xED\xA0\x80", 3, 0, u16_host, 8, NULL));
  u16_host[0] = 0xD800U;
  ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
            c_orm_utf16_to_utf8(u16_host, 1, 0, back, sizeof(back), NULL));
  u16_host[0] = 0xDC00U;
  ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
            c_orm_utf16_to_utf8(u16_host, 1, 0, back, sizeof(back), NULL));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_utf8_to_utf16(sample, strlen(sample), 0, u16_host, 4, NULL));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_utf16_to_utf8(u16_be, units, 1, back, 4, NULL));
  PASS();
}

#ifdef C_ORM_TEST_ALLOCATOR
/**
 * @brief Always-failing malloc mock for EBCDIC OOM verification.
 * @param size Requested byte count.
 * @param out_ptr Output pointer set to NULL.
 * @return C_ORM_ERROR_MEMORY always.
 */
static c_orm_error_t ebcdic_fail_malloc(size_t size, void **out_ptr) {
  (void)size;
  if (out_ptr) {
    *out_ptr = NULL;
  }
  return C_ORM_ERROR_MEMORY;
}
#endif

/**
 * @brief Exercises UTF-8 passthrough, multi-byte boundary truncation, buffer
 * overflows, invalid codepages, NULL pointers, and OOM in c_orm_ebcdic.c.
 * @return Greatest test result enum.
 */
TEST test_ebcdic_exhaustive_errors_and_oom(void) {
  /* 1-byte "A", 2-byte U+00A2 "\xC2\xA2", 3-byte U+20AC "\xE2\x82\xAC",
   * 4-byte U+1F600 "\xF0\x9F\x98\x80" */
  const char *multi = "A\xC2\xA2\xE2\x82\xAC\xF0\x9F\x98\x80";
  unsigned char raw[32];
  char out[32];
  unsigned short u16[16];
  size_t n = 0;

  /* UTF-8 passthrough in ebcdic_to_utf8 & utf8_to_ebcdic + 1/2/3/4B overflows
   */
  ASSERT_EQ(C_ORM_OK,
            c_orm_ebcdic_to_utf8(C_ORM_CP_UTF8, (const unsigned char *)multi,
                                 strlen(multi), out, sizeof(out), &n));
  ASSERT_STR_EQ(multi, out);
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_ebcdic_to_utf8(C_ORM_CP_UTF8, (const unsigned char *)"A", 1,
                                 out, 1, NULL));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_ebcdic_to_utf8(C_ORM_CP_UTF8,
                                 (const unsigned char *)"\xC2\xA2", 2, out, 2,
                                 NULL));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_ebcdic_to_utf8(C_ORM_CP_UTF8,
                                 (const unsigned char *)"\xE2\x82\xAC", 3, out,
                                 3, NULL));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_ebcdic_to_utf8(C_ORM_CP_UTF8,
                                 (const unsigned char *)"\xF0\x9F\x98\x80", 4,
                                 out, 4, NULL));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_ebcdic_to_utf8(C_ORM_CP_IBM037, raw, 0, out, 0, NULL));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_ebcdic_to_utf8(C_ORM_CP_IBM037, NULL, 2, out, 8, NULL));
  ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
            c_orm_ebcdic_to_utf8((c_orm_codepage_t)77, raw, 1, out, 8, NULL));

  /* Malformed UTF-8 sequences (truncated 2/3/4B, overlong 3/4B, > 0x10FFFF,
   * 0xFF) */
  ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
            c_orm_ebcdic_to_utf8(C_ORM_CP_UTF8, (const unsigned char *)"\xC2",
                                 1, out, 8, NULL));
  ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
            c_orm_ebcdic_to_utf8(C_ORM_CP_UTF8,
                                 (const unsigned char *)"\xE0\x80\x80", 3, out,
                                 8, NULL));
  ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
            c_orm_ebcdic_to_utf8(C_ORM_CP_UTF8,
                                 (const unsigned char *)"\xE2\x82", 2, out, 8,
                                 NULL));
  ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
            c_orm_ebcdic_to_utf8(C_ORM_CP_UTF8,
                                 (const unsigned char *)"\xF0\x80\x80\x80", 4,
                                 out, 8, NULL));
  ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
            c_orm_ebcdic_to_utf8(C_ORM_CP_UTF8,
                                 (const unsigned char *)"\xF4\x90\x80\x80", 4,
                                 out, 8, NULL));
  ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
            c_orm_ebcdic_to_utf8(C_ORM_CP_UTF8,
                                 (const unsigned char *)"\xF0\x9F\x98", 3, out,
                                 8, NULL));
  ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
            c_orm_ebcdic_to_utf8(C_ORM_CP_UTF8, (const unsigned char *)"\xFF",
                                 1, out, 8, NULL));

  /* utf8_to_ebcdic UTF-8 passthrough, ASCII > 0xFF error, IBM1140 0xA4 error */
  ASSERT_EQ(C_ORM_OK, c_orm_utf8_to_ebcdic(C_ORM_CP_UTF8, multi, strlen(multi),
                                           raw, sizeof(raw), &n));
  ASSERT_EQ(strlen(multi), n);
  ASSERT_EQ(
      C_ORM_ERROR_OVERFLOW,
      c_orm_utf8_to_ebcdic(C_ORM_CP_UTF8, multi, strlen(multi), raw, 3, NULL));
  ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
            c_orm_utf8_to_ebcdic(C_ORM_CP_UTF8, "\xFF", 1, raw, 8, NULL));
  ASSERT_EQ(
      C_ORM_ERROR_CODEPAGE,
      c_orm_utf8_to_ebcdic(C_ORM_CP_ASCII, "\xE2\x82\xAC", 3, raw, 8, NULL));
  ASSERT_EQ(
      C_ORM_ERROR_CODEPAGE,
      c_orm_utf8_to_ebcdic(C_ORM_CP_IBM1140, "\xC2\xA4", 2, raw, 8, NULL));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_utf8_to_ebcdic(C_ORM_CP_ASCII, "ABC", 3, raw, 2, NULL));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_utf8_to_ebcdic(C_ORM_CP_ASCII, NULL, 2, raw, 8, NULL));
  ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
            c_orm_utf8_to_ebcdic((c_orm_codepage_t)77, "A", 1, raw, 8, NULL));

  /* Inplace NULL/invalid codepage & IBM1140 0xA4 error */
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_ebcdic_to_ascii_inplace(C_ORM_CP_IBM037, NULL, 2));
  ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
            c_orm_ebcdic_to_ascii_inplace(C_ORM_CP_UTF8, raw, 1));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_ascii_to_ebcdic_inplace(C_ORM_CP_IBM037, NULL, 2));
  ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
            c_orm_ascii_to_ebcdic_inplace(C_ORM_CP_UTF8, raw, 1));
  raw[0] = 0xA4U;
  ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
            c_orm_ascii_to_ebcdic_inplace(C_ORM_CP_IBM1140, raw, 1));

  /* COBOL alpha UTF-8 multi-byte safe truncation (left & right justified) */
  ASSERT_EQ(C_ORM_OK,
            c_orm_cobol_format_alpha("\xE2\x82\xAC\xE2\x82\xAC", 4,
                                     C_ORM_CP_UTF8, 1, 0, raw, sizeof(raw)));
  ASSERT_EQ(0x20, (int)raw[3]);
  ASSERT_EQ(C_ORM_OK,
            c_orm_cobol_format_alpha("\xE2\x82\xAC\xE2\x82\xAC", 4,
                                     C_ORM_CP_UTF8, 1, 1, raw, sizeof(raw)));
  ASSERT_EQ(0x20, (int)raw[0]);
  ASSERT_EQ(C_ORM_OK,
            c_orm_cobol_format_alpha("ABC", 0, C_ORM_CP_ASCII, 1, 0, NULL, 0));
  ASSERT_EQ(C_ORM_ERROR_TRUNCATION,
            c_orm_cobol_format_alpha("ABC", 0, C_ORM_CP_ASCII, 0, 0, NULL, 0));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_cobol_format_alpha("A", 4, C_ORM_CP_ASCII, 0, 0, NULL, 8));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_cobol_format_alpha("A", 4, C_ORM_CP_ASCII, 0, 0, raw, 2));
  ASSERT_EQ(
      C_ORM_ERROR_CODEPAGE,
      c_orm_cobol_format_alpha("A", 4, (c_orm_codepage_t)77, 0, 0, raw, 8));
  ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
            c_orm_cobol_format_alpha("\xFF", 4, C_ORM_CP_IBM037, 0, 0, raw, 8));

  ASSERT_EQ(
      C_ORM_ERROR_NULL_POINTER,
      c_orm_cobol_extract_alpha(NULL, 4, C_ORM_CP_IBM037, 1, out, 8, NULL));
  ASSERT_EQ(
      C_ORM_ERROR_CODEPAGE,
      c_orm_cobol_extract_alpha(raw, 4, (c_orm_codepage_t)77, 1, out, 8, NULL));

  /* UTF-16 NULL, zero-cap, and unpaired high surrogate followed by non-low */
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_utf8_to_utf16(NULL, 2, 0, u16, 8, NULL));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW, c_orm_utf8_to_utf16("A", 1, 0, u16, 0, NULL));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_utf8_to_utf16("AB", 2, 0, u16, 2, NULL));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_utf16_to_utf8(NULL, 2, 0, out, 8, NULL));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW, c_orm_utf16_to_utf8(u16, 1, 0, out, 0, NULL));
  u16[0] = 0xD800U;
  u16[1] = 0x0041U;
  ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
            c_orm_utf16_to_utf8(u16, 2, 0, out, sizeof(out), NULL));

#ifdef C_ORM_TEST_ALLOCATOR
  c_orm_malloc = ebcdic_fail_malloc;
  ASSERT_EQ(C_ORM_ERROR_MEMORY,
            c_orm_cobol_format_alpha("SMITH", 8, C_ORM_CP_IBM037, 0, 0, raw,
                                     sizeof(raw)));
  ASSERT_EQ(C_ORM_OK, c_orm_reset_allocators());
#endif

  /* Additional branch coverage: ASCII in ebcdic_to_utf8, non-UTF8 overflow,
   * bad UTF-8 continuation bytes, U+FFFD > 0xDFFF, inplace across all 4 EBCDIC
   * codepages, 4B UTF-8 truncation at width 1/2, and UTF-16 NULL/surrogate
   * bounds */
  {
    c_orm_codepage_t cp_tmp;
    ASSERT_EQ(C_ORM_OK, c_orm_codepage_from_ccsid(437, &cp_tmp));
    ASSERT_EQ(C_ORM_CP_ASCII, cp_tmp);
  }
  ASSERT_EQ(C_ORM_OK,
            c_orm_ebcdic_to_utf8(C_ORM_CP_ASCII, (const unsigned char *)"HI", 2,
                                 out, sizeof(out), NULL));
  ASSERT_STR_EQ("HI", out);
  ASSERT_EQ(C_ORM_OK,
            c_orm_ebcdic_to_utf8(C_ORM_CP_IBM037, NULL, 0, out, 8, NULL));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_ebcdic_to_utf8(C_ORM_CP_IBM037, raw, 1, NULL, 8, NULL));
  ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
            c_orm_ebcdic_to_utf8(C_ORM_CP_ASCII, (const unsigned char *)"ABCD",
                                 4, out, 2, NULL));

  /* Malformed continuation bytes in 2B, 3B, 4B UTF-8 + valid U+FFFD (> 0xDFFF)
   */
  ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
            c_orm_ebcdic_to_utf8(C_ORM_CP_UTF8,
                                 (const unsigned char *)"\xC2\x20", 2, out, 8,
                                 NULL));
  ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
            c_orm_ebcdic_to_utf8(C_ORM_CP_UTF8,
                                 (const unsigned char *)"\xE2\x20\x80", 3, out,
                                 8, NULL));
  ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
            c_orm_ebcdic_to_utf8(C_ORM_CP_UTF8,
                                 (const unsigned char *)"\xE2\x82\x20", 3, out,
                                 8, NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_ebcdic_to_utf8(
                          C_ORM_CP_UTF8, (const unsigned char *)"\xEF\xBF\xBD",
                          3, out, 8, NULL));
  ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
            c_orm_ebcdic_to_utf8(C_ORM_CP_UTF8,
                                 (const unsigned char *)"\xF0\x20\x80\x80", 4,
                                 out, 8, NULL));
  ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
            c_orm_ebcdic_to_utf8(C_ORM_CP_UTF8,
                                 (const unsigned char *)"\xF0\x9F\x20\x80", 4,
                                 out, 8, NULL));
  ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
            c_orm_ebcdic_to_utf8(C_ORM_CP_UTF8,
                                 (const unsigned char *)"\xF0\x9F\x98\x20", 4,
                                 out, 8, NULL));

  /* utf8_to_ebcdic NULL with len=0 and out_ebcdic=NULL with len>0 */
  ASSERT_EQ(C_ORM_OK,
            c_orm_utf8_to_ebcdic(C_ORM_CP_ASCII, NULL, 0, NULL, 0, NULL));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_utf8_to_ebcdic(C_ORM_CP_ASCII, "AB", 2, NULL, 8, NULL));

  /* Inplace across all 4 EBCDIC codepages + NULL with len=0 */
  ASSERT_EQ(C_ORM_OK, c_orm_ebcdic_to_ascii_inplace(C_ORM_CP_IBM037, NULL, 0));
  ASSERT_EQ(C_ORM_OK, c_orm_ascii_to_ebcdic_inplace(C_ORM_CP_IBM037, NULL, 0));
  {
    c_orm_codepage_t cps4[4];
    size_t k;
    cps4[0] = C_ORM_CP_IBM037;
    cps4[1] = C_ORM_CP_IBM1047;
    cps4[2] = C_ORM_CP_IBM500;
    cps4[3] = C_ORM_CP_IBM1140;
    for (k = 0; k < 4U; ++k) {
      memcpy(raw, "AZ", 2);
      ASSERT_EQ(C_ORM_OK, c_orm_ascii_to_ebcdic_inplace(cps4[k], raw, 2));
      ASSERT_EQ(C_ORM_OK, c_orm_ebcdic_to_ascii_inplace(cps4[k], raw, 2));
      ASSERT_EQ(0, memcmp(raw, "AZ", 2));
    }
  }

  /* COBOL alpha: empty UTF-8 pad, 4B emoji truncated to width 1 & 2, all-space
   * extract, NULL extract */
  ASSERT_EQ(C_ORM_OK,
            c_orm_cobol_format_alpha("", 4, C_ORM_CP_UTF8, 0, 0, raw, 8));
  ASSERT_EQ(C_ORM_OK,
            c_orm_cobol_format_alpha("", 0, C_ORM_CP_UTF8, 0, 0, NULL, 0));
  ASSERT_EQ(C_ORM_OK, c_orm_cobol_format_alpha("\xF0\x9F\x98\x80", 2,
                                               C_ORM_CP_UTF8, 1, 1, raw, 8));
  ASSERT_EQ(C_ORM_OK, c_orm_cobol_format_alpha("\xF0\x9F\x98\x80", 1,
                                               C_ORM_CP_UTF8, 1, 0, raw, 8));
  ASSERT_EQ(
      C_ORM_ERROR_NULL_POINTER,
      c_orm_cobol_extract_alpha(raw, 4, C_ORM_CP_IBM037, 1, NULL, 8, NULL));
  ASSERT_EQ(C_ORM_OK,
            c_orm_cobol_extract_alpha(NULL, 0, C_ORM_CP_UTF8, 1, out, 8, NULL));
  ASSERT_EQ(C_ORM_OK,
            c_orm_cobol_extract_alpha((const unsigned char *)"    ", 4,
                                      C_ORM_CP_ASCII, 1, out, 8, &n));
  ASSERT_EQ(0U, n);

  /* UTF-16 NULL len=0, out=NULL, w1 > 0xDFFF, and w0 > 0xDFFF */
  ASSERT_EQ(C_ORM_OK, c_orm_utf8_to_utf16(NULL, 0, 0, u16, 4, NULL));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_utf8_to_utf16("A", 1, 0, NULL, 4, NULL));
  ASSERT_EQ(C_ORM_OK, c_orm_utf16_to_utf8(NULL, 0, 0, out, 8, NULL));
  ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
            c_orm_utf16_to_utf8(u16, 1, 0, NULL, 8, NULL));
  u16[0] = 0xD800U;
  u16[1] = 0xE000U;
  ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
            c_orm_utf16_to_utf8(u16, 2, 0, out, sizeof(out), NULL));
  u16[0] = 0xFFFDU;
  ASSERT_EQ(C_ORM_OK, c_orm_utf16_to_utf8(u16, 1, 0, out, sizeof(out), NULL));
  PASS();
}

/**
 * @brief Suite registration for EBCDIC, COBOL alpha, and UTF-16 GRAPHIC tests.
 * @param ebcdic_suite Suite runner function name.
 */
SUITE(ebcdic_suite) {
  RUN_TEST(test_ebcdic_ccsid_mapping);
  RUN_TEST(test_ebcdic_bijections_and_euro);
  RUN_TEST(test_cobol_fixed_alpha_helpers);
  RUN_TEST(test_utf16_graphic_transcoders);
  RUN_TEST(test_ebcdic_exhaustive_errors_and_oom);
}
