/**
 * @file test_copybook.c
 * @brief Exhaustive test suite for COBOL Copybook (.cpy) fixed/free lexing,
 * AST layout, DCLGEN parsing, IBM037 EBCDIC + COMP-3/COMP/Zoned/ODO wire
 * serialization, level-88 condition validation, bidirectional C89/SQL/Copybook
 * codegen, CLI subcommands, and OOM fault injection.
 */

/* clang-format off */
#include "abstract_struct.h"
#include "c_orm_api.h"
#include "c_orm_codegen.h"
#include "c_orm_copybook.h"
#include "c_orm_decimal.h"
#include "c_orm_ebcdic.h"
#include "c_orm_inline_macros.h"
#include "c_orm_safe_crt.h"
#include "c_orm_sql.h"
#include "c_orm_sqlite.h"
#include "c_orm_struct.h"
#include "greatest.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32) || defined(_WIN64)
#include <direct.h>
#define CPY_MKDIR(d) _mkdir(d)
#define CPY_RMDIR(d) _rmdir(d)
#else
#include <sys/stat.h>
#include <unistd.h>
#define CPY_MKDIR(d) mkdir(d, 0755)
#define CPY_RMDIR(d) rmdir(d)
#endif
#ifdef C_ORM_CLI_EXECUTABLE
#define CPY_CLI_BIN C_ORM_CLI_EXECUTABLE
#elif defined(_WIN32)
#define CPY_CLI_BIN "c-orm-cli.exe"
#else
#define CPY_CLI_BIN "./bin/c-orm-cli"
#endif

#undef ASSERT_EQ_FMT
#define ASSERT_EQ_FMT(exp, got, fmt)                                           \
  do {                                                                         \
    greatest_info.assertions++;                                                \
    if (!((exp) == (got))) {                                                   \
      fprintf(stderr, "\nFAIL %s:%d: %s != %s\n", __FILE__, __LINE__, #exp,    \
              #got);                                                           \
      return GREATEST_TEST_RES_FAIL;                                           \
    }                                                                          \
  } while ((void)0, 0)
#undef ASSERT_EQ
#define ASSERT_EQ(exp, got)                                                    \
  do {                                                                         \
    greatest_info.assertions++;                                                \
    if (!((exp) == (got))) {                                                   \
      fprintf(stderr, "\nFAIL %s:%d: %s != %s\n", __FILE__, __LINE__, #exp,    \
              #got);                                                           \
      return GREATEST_TEST_RES_FAIL;                                           \
    }                                                                          \
  } while ((void)0, 0)
#undef ASSERT
#define ASSERT(cond)                                                           \
  do {                                                                         \
    greatest_info.assertions++;                                                \
    if (!((cond) != 0)) {                                                      \
      fprintf(stderr, "\nFAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
      return GREATEST_TEST_RES_FAIL;                                           \
    }                                                                          \
  } while ((void)0, 0)
#undef ASSERT_STR_EQ
#define ASSERT_STR_EQ(exp, got)                                                \
  do {                                                                         \
    greatest_info.assertions++;                                                \
    if (strcmp((exp), (got)) != 0) {                                           \
      fprintf(stderr, "\nFAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__,  \
              (exp), (got));                                                   \
      return GREATEST_TEST_RES_FAIL;                                           \
    }                                                                          \
  } while ((void)0, 0)
/* clang-format on */

/**
 * @brief External CLI runner entry point compiled in `test_cli.c`.
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return C_ORM_OK on success, or error code.
 */
extern c_orm_error_t c_orm_cli_run(int argc, char **argv);

/**
 * @brief Financial mainframe account record C89 struct for wire pack/unpack
 * tests.
 */
typedef struct TestAcctWire {
  /** @brief `ACCT-ID PIC S9(9) COMP` (4 bytes BE). */
  int32_t acct_id;
  /** @brief `ACCT-NAME PIC X(10)` (10 bytes EBCDIC). */
  char acct_name[11];
  /** @brief `ACCT-BAL PIC S9(7)V99 COMP-3` (5 bytes BCD). */
  c_orm_decimal_t acct_bal;
  /** @brief `ACCT-LIMIT PIC S9(7)V99 SIGN LEADING SEPARATE` (10 bytes Zoned).
   */
  c_orm_decimal_t acct_limit;
  /** @brief `ACCT-STATUS PIC X(1)` (`'A'`, `'F'`, `'C'`). */
  char acct_status[2];
  /** @brief `ACCT-MONTH PIC 99` (`01` THRU `12`). */
  int32_t acct_month;
} TestAcctWire;

static const c_orm_column_meta_t TestAcctWire_cols[] = {
    C_ORM_DEFINE_COLUMN_EX("acct_id", C_ORM_TYPE_INT32,
                           offsetof(struct TestAcctWire, acct_id), true, false,
                           NULL, false, false, 9, 0, 0, 0),
    C_ORM_COL_CHAR_FIXED(TestAcctWire, acct_name, 10, 37, false),
    C_ORM_COL_DECIMAL(TestAcctWire, acct_bal, 9, 2, false),
    C_ORM_COL_DECIMAL(TestAcctWire, acct_limit, 9, 2, false),
    C_ORM_COL_CHAR_FIXED(TestAcctWire, acct_status, 1, 37, false),
    C_ORM_DEFINE_COLUMN_EX("acct_month", C_ORM_TYPE_INT32,
                           offsetof(struct TestAcctWire, acct_month), false,
                           false, NULL, false, false, 2, 0, 0, 0)};

static const c_orm_table_meta_t TestAcctWire_meta = {
    "ACCT_REC",
    TestAcctWire_cols,
    6,
    sizeof(struct TestAcctWire),
    "SELECT * FROM ACCT_REC",
    "SELECT * FROM ACCT_REC WHERE acct_id = ?",
    "INSERT INTO ACCT_REC VALUES (?, ?, ?, ?, ?, ?)",
    "UPDATE ACCT_REC SET acct_name = ?, acct_bal = ?, acct_limit = ?, "
    "acct_status = ?, acct_month = ? WHERE acct_id = ?",
    "DELETE FROM ACCT_REC WHERE acct_id = ?",
    NULL,
    false,
    false,
    0,
    0,
    {0},
    NULL,
    0};

/**
 * @brief Complex wire struct covering DCLGEN `49`-level `VARCHAR`/`VARGRAPHIC`,
 * `COMP-1`, `COMP-2`, `COMP-5`, `GRAPHIC`, `FOR BIT DATA`, `BLANK WHEN ZERO`,
 * and `OCCURS DEPENDING ON`.
 */
typedef struct TestComplexWire {
  /** @brief `CUST-SEQ PIC S9(18) COMP-5`. */
  c_orm_int64_t cust_seq;
  /** @brief `CUST-RATE USAGE COMP-1`. */
  float cust_rate;
  /** @brief `CUST-SCORE USAGE COMP-2`. */
  double cust_score;
  /** @brief `CUST-NOTE` (`49` `VARCHAR(20)`). */
  char *cust_note;
  /** @brief `CUST-GNOTE` (`49` `VARGRAPHIC(8)`). */
  char *cust_gnote;
  /** @brief `CUST-KANJI PIC G(4) USAGE DISPLAY-1`. */
  char *cust_kanji;
  /** @brief `CUST-RAW PIC X(4)` (`FOR BIT DATA`). */
  unsigned char cust_raw[4];
  /** @brief `CUST-BWZ PIC 9(5) BLANK WHEN ZERO`. */
  c_orm_decimal_t cust_bwz;
  /** @brief `ITEM-CNT PIC S9(4) COMP`. */
  int32_t item_cnt;
  /** @brief `ITEM-AMT PIC S9(5)V99 COMP-3 OCCURS 1 TO 5 DEPENDING ON ITEM-CNT`.
   */
  c_orm_cobol_odo_array_t item_amt;
} TestComplexWire;

static const c_orm_column_meta_t TestComplexWire_cols[] = {
    C_ORM_DEFINE_COLUMN_EX("cust_seq", C_ORM_TYPE_INT64,
                           offsetof(struct TestComplexWire, cust_seq), true,
                           false, NULL, false, false, 18, 0, 0, 0),
    C_ORM_DEFINE_COLUMN_EX("cust_rate", C_ORM_TYPE_FLOAT,
                           offsetof(struct TestComplexWire, cust_rate), false,
                           false, NULL, false, false, 0, 0, 0, 0),
    C_ORM_DEFINE_COLUMN_EX("cust_score", C_ORM_TYPE_DOUBLE,
                           offsetof(struct TestComplexWire, cust_score), false,
                           false, NULL, false, false, 0, 0, 0, 0),
    C_ORM_DEFINE_COLUMN_EX("cust_note", C_ORM_TYPE_STRING,
                           offsetof(struct TestComplexWire, cust_note), false,
                           false, NULL, false, false, 20, 0, 0, 0),
    C_ORM_DEFINE_COLUMN_EX("cust_gnote", C_ORM_TYPE_VARGRAPHIC,
                           offsetof(struct TestComplexWire, cust_gnote), false,
                           false, NULL, false, false, 8, 0, 0, 1200),
    C_ORM_DEFINE_COLUMN_EX("cust_kanji", C_ORM_TYPE_GRAPHIC,
                           offsetof(struct TestComplexWire, cust_kanji), false,
                           false, NULL, false, false, 4, 0, 0, 1200),
    C_ORM_DEFINE_COLUMN_EX("cust_raw", C_ORM_TYPE_BINARY_FIXED,
                           offsetof(struct TestComplexWire, cust_raw), false,
                           false, NULL, false, false, 0, 0, 4, 65535),
    C_ORM_COL_DECIMAL(TestComplexWire, cust_bwz, 5, 0, false),
    C_ORM_DEFINE_COLUMN_EX("item_cnt", C_ORM_TYPE_INT32,
                           offsetof(struct TestComplexWire, item_cnt), false,
                           false, NULL, false, false, 4, 0, 0, 0),
    C_ORM_DEFINE_COLUMN_EX("item_amt", C_ORM_TYPE_ARRAY,
                           offsetof(struct TestComplexWire, item_amt), false,
                           false, NULL, false, false, 7, 2, 0, 0)};

static const c_orm_table_meta_t TestComplexWire_meta = {
    "CUSTOMER_LEDGER",
    TestComplexWire_cols,
    10,
    sizeof(struct TestComplexWire),
    "SELECT * FROM CUSTOMER_LEDGER",
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    false,
    false,
    0,
    0,
    {0},
    NULL,
    0};

static int g_cpy_oom_countdown = -1;

/**
 * @brief Deterministic OOM-injecting `malloc` callback for Copybook tests.
 * @param size Allocation size in bytes.
 * @param out_ptr Output pointer.
 * @return C_ORM_OK or C_ORM_ERROR_MEMORY.
 */
static c_orm_error_t cpy_oom_malloc(size_t size, void **out_ptr) {
  if (g_cpy_oom_countdown >= 0) {
    if (g_cpy_oom_countdown == 0) {
      *out_ptr = NULL;
      return C_ORM_ERROR_MEMORY;
    }
    g_cpy_oom_countdown--;
  }
  *out_ptr = malloc(size > 0 ? size : 1);
  return (*out_ptr != NULL) ? C_ORM_OK : C_ORM_ERROR_MEMORY;
}

/**
 * @brief Deterministic OOM-injecting `realloc` callback for Copybook tests.
 * @param ptr Existing allocation or NULL.
 * @param size Requested new size in bytes.
 * @param out_ptr Output pointer.
 * @return C_ORM_OK or C_ORM_ERROR_MEMORY.
 */
static c_orm_error_t cpy_oom_realloc(void *ptr, size_t size, void **out_ptr) {
  if (g_cpy_oom_countdown >= 0) {
    if (g_cpy_oom_countdown == 0) {
      *out_ptr = NULL;
      return C_ORM_ERROR_MEMORY;
    }
    g_cpy_oom_countdown--;
  }
  *out_ptr = realloc(ptr, size > 0 ? size : 1);
  return (*out_ptr != NULL) ? C_ORM_OK : C_ORM_ERROR_MEMORY;
}

/**
 * @brief Matching `free` callback for Copybook OOM tests.
 * @param ptr Pointer to release.
 * @return C_ORM_OK.
 */
static c_orm_error_t cpy_oom_free(void *ptr) {
  free(ptr);
  return C_ORM_OK;
}

/**
 * @brief Writes a UTF-8 string to `path` for CLI and file-based parser tests.
 * @param path Destination file path.
 * @param text File contents to write.
 * @return C_ORM_OK on success, or C_ORM_ERROR_IO on failure.
 */
static c_orm_error_t cpy_test_write_file(const char *path, const char *text) {
  FILE *fp = NULL;
  c_orm_error_t rc = C_ORM_FOPEN(&fp, path, "wb");
  if (rc != C_ORM_OK || !fp) {
    return C_ORM_ERROR_IO;
  }
  fputs(text, fp);
  fclose(fp);
  return C_ORM_OK;
}

/**
 * @brief Reads an entire file at `path` into a heap string.
 * @param path Source file path.
 * @param out_str Output heap string (caller frees via `c_orm_system_free`).
 * @return C_ORM_OK on success, or error code.
 */
static c_orm_error_t cpy_test_read_file(const char *path, char **out_str) {
  FILE *fp = NULL;
  long sz;
  char *buf = NULL;
  size_t n;
  c_orm_error_t rc = C_ORM_FOPEN(&fp, path, "rb");
  if (rc != C_ORM_OK || !fp) {
    return C_ORM_ERROR_IO;
  }
  fseek(fp, 0, SEEK_END);
  sz = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  if (sz < 0) {
    fclose(fp);
    return C_ORM_ERROR_IO;
  }
  rc = c_orm_system_malloc((size_t)sz + 1, (void **)&buf);
  if (rc != C_ORM_OK) {
    fclose(fp);
    return rc;
  }
  n = fread(buf, 1, (size_t)sz, fp);
  fclose(fp);
  buf[n] = '\0';
  *out_str = buf;
  return C_ORM_OK;
}

/**
 * @brief Concatenates an array of C90-safe short line strings into `out_buf`
 * (avoiding >509-byte string literals under strict ISO C90 `-pedantic`).
 * @param lines Array of null-terminated line strings.
 * @param count Number of lines in `lines`.
 * @param out_buf Destination buffer.
 * @param out_cap Capacity of `out_buf` in bytes.
 * @return C_ORM_OK on success, or C_ORM_ERROR_OVERFLOW if `out_buf` is full.
 */
static c_orm_error_t cpy_join_lines(const char *const *lines, size_t count,
                                    char *out_buf, size_t out_cap) {
  size_t i;
  size_t pos = 0;
  out_buf[0] = '\0';
  for (i = 0; i < count; ++i) {
    size_t l = strlen(lines[i]);
    if (pos + l + 1 > out_cap) {
      return C_ORM_ERROR_OVERFLOW;
    }
    memcpy(out_buf + pos, lines[i], l);
    pos += l;
    out_buf[pos] = '\0';
  }
  return C_ORM_OK;
}

/**
 * @brief Tests fixed-format 80-column COBOL Copybook parsing including sequence
 * areas, `*`/`/`/`D` indicators, `-` literal continuations, cols 73-80
 * identification area, `REPLACING`, nested groups, `77`, `66 RENAMES`, `88`,
 * `REDEFINES`, `OCCURS`, and `DCLGEN` `EXEC SQL DECLARE` + `49`-level fields.
 * @return GREATEST test result.
 */
TEST test_copybook_parse_fixed_and_dclgen(void) {
  static const char *fixed_lines[] = {
      "000100* ENTERPRISE MAINFRAME ACCOUNT COPYBOOK                           "
      "ACCT0001\n",
      "000200/ PAGE EJECT COMMENT                                              "
      "ACCT0002\n",
      "000300D     DISPLAY 'DEBUG LINE IGNORED'                                "
      "ACCT0003\n",
      "000400      EXEC SQL DECLARE PROD.ACCT_MASTER TABLE                     "
      "ACCT0004\n",
      "000500      ( ACCT_ID INTEGER NOT NULL PRIMARY KEY,                     "
      "ACCT0005\n",
      "000600        ACCT_BAL DECIMAL(9, 2) NOT NULL,                          "
      "ACCT0006\n",
      "000700        RAW_TOKEN CHAR(8) FOR BIT DATA NOT NULL                   "
      "ACCT0007\n",
      "000800      ) END-EXEC.                                                 "
      "ACCT0008\n",
      "000900 77  STANDALONE-VER PIC 9(4) VALUE 12.                            "
      "ACCT0009\n",
      "001000 01  :PFX:-MASTER-REC.                                            "
      "ACCT0010\n",
      "001100     05 :PFX:-ID           PIC S9(9) USAGE COMP.                  "
      "ACCT0011\n",
      "001200     05 :PFX:-HOLDER.                                             "
      "ACCT0012\n",
      "001300        49 :PFX:-HOLDER-LEN  PIC S9(4) USAGE COMP.                "
      "ACCT0013\n",
      "001400        49 :PFX:-HOLDER-TEXT PIC X(30).                           "
      "ACCT0014\n",
      "001500     05 :PFX:-BAL          PIC S9(7)V99 USAGE COMP-3.             "
      "ACCT0015\n",
      "001600     05 :PFX:-STATUS       PIC X(1) VALUE 'A'.                    "
      "ACCT0016\n",
      "001700        88 :PFX:-ACTIVE    VALUE 'A'.                             "
      "ACCT0017\n",
      "001800        88 :PFX:-CLOSED    VALUE 'C', 'X'.                        "
      "ACCT0018\n",
      "001900     05 :PFX:-REGION-CODE  PIC 99 VALUE 01.                       "
      "ACCT0019\n",
      "002000        88 :PFX:-VALID-REG VALUE 01 THRU 12.                      "
      "ACCT0020\n",
      "002100     05 :PFX:-RAW-TOKEN    PIC X(8) VALUE X'0102030405060708'.    "
      "ACCT0021\n",
      "002300     05 :PFX:-TOKEN-RED REDEFINES :PFX:-RAW-TOKEN.                "
      "ACCT0023\n",
      "002400        10 :PFX:-TOK-HI    PIC S9(9) COMP.                        "
      "ACCT0024\n",
      "002500        10 :PFX:-TOK-LO    PIC S9(9) COMP.                        "
      "ACCT0025\n",
      "002600     05 :PFX:-NOTE         PIC X(12) VALUE 'HELLO                 "
      "ACCT0026\n",
      "002700-       'WORLD'.                                                  "
      "ACCT0027\n",
      "002800     05 FILLER             PIC X(3) VALUE SPACES.                 "
      "ACCT0028\n",
      "002900     05 :PFX:-TAGS         PIC X(4) OCCURS 3 TIMES.               "
      "ACCT0029\n",
      "003000 66  :PFX:-HDR-SPAN RENAMES :PFX:-ID THRU :PFX:-BAL.              "
      "ACCT0030\n"};
  char fixed_cpy[3072];
  c_orm_cobol_replace_pair_t rep;
  c_orm_cobol_record_t *rec = NULL;
  const c_orm_cobol_field_t *f_id = NULL;
  const c_orm_cobol_field_t *f_holder = NULL;
  const c_orm_cobol_field_t *f_bal = NULL;
  const c_orm_cobol_field_t *f_raw = NULL;
  const c_orm_cobol_field_t *f_red = NULL;
  const c_orm_cobol_field_t *f_span = NULL;
  const c_orm_cobol_field_t *f_ver = NULL;
  c_orm_error_t rc;

  rc = cpy_join_lines(fixed_lines, sizeof(fixed_lines) / sizeof(fixed_lines[0]),
                      fixed_cpy, sizeof(fixed_cpy));
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");

  rep.from_text = "==:PFX:==";
  rep.to_text = "==ACCT==";

  rc = c_orm_copybook_parse_string(fixed_cpy, C_ORM_COBOL_FORMAT_AUTO, &rep, 1,
                                   &rec);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  ASSERT(rec != NULL);
  ASSERT_STR_EQ("ACCT-MASTER-REC", rec->record_name);
  ASSERT_STR_EQ("Acct_master_rec", rec->c_struct_name);
  ASSERT_STR_EQ("ACCT_MASTER", rec->sql_table_name);
  ASSERT_EQ_FMT(1, rec->has_exec_sql_declare, "%d");
  ASSERT_STR_EQ("PROD", rec->sql_schema_name);
  ASSERT_EQ_FMT(1, (int)rec->num_standalone_fields, "%d");

  rc = c_orm_copybook_find_field(rec, "STANDALONE-VER", &f_ver);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  ASSERT_EQ_FMT(77, f_ver->level, "%d");

  rc = c_orm_copybook_find_field(rec, "ACCT-ID", &f_id);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  ASSERT_EQ_FMT(0, (int)f_id->byte_offset, "%d");
  ASSERT_EQ_FMT(4, (int)f_id->element_byte_size, "%d");
  ASSERT_EQ_FMT(1, f_id->sql_is_pk, "%d");
  ASSERT_EQ_FMT(1, f_id->sql_not_null, "%d");

  rc = c_orm_copybook_find_field(rec, "ACCT-HOLDER", &f_holder);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  ASSERT_EQ_FMT(1, f_holder->is_dclgen_varchar, "%d");
  ASSERT_EQ_FMT(30, (int)f_holder->dclgen_max_chars, "%d");
  ASSERT_EQ_FMT(32, (int)f_holder->total_byte_size, "%d");

  rc = c_orm_copybook_find_field(rec, "ACCT-BAL", &f_bal);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  ASSERT_EQ_FMT(36, (int)f_bal->byte_offset, "%d");
  ASSERT_EQ_FMT(5, (int)f_bal->element_byte_size, "%d");
  ASSERT_EQ_FMT(9, (int)f_bal->total_digits, "%d");
  ASSERT_EQ_FMT(2, (int)f_bal->frac_digits, "%d");

  rc = c_orm_copybook_find_field(rec, "ACCT-RAW-TOKEN", &f_raw);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  ASSERT_EQ_FMT(1, f_raw->sql_for_bit_data, "%d");
  ASSERT_EQ_FMT(8, (int)f_raw->total_byte_size, "%d");

  rc = c_orm_copybook_find_field(rec, "ACCT-TOKEN-RED", &f_red);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  ASSERT(f_red->redefines_target == f_raw);
  ASSERT_EQ_FMT((int)f_raw->byte_offset, (int)f_red->byte_offset, "%d");
  ASSERT_EQ_FMT(8, (int)f_red->total_byte_size, "%d");

  rc = c_orm_copybook_find_field(rec, "ACCT-HDR-SPAN", &f_span);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  ASSERT_EQ_FMT(66, f_span->level, "%d");
  ASSERT_EQ_FMT(0, (int)f_span->byte_offset, "%d");
  ASSERT_EQ_FMT(41, (int)f_span->total_byte_size, "%d");

  /* 4 (ID) + 32 (HOLDER) + 5 (BAL) + 1 (STATUS) + 2 (REG) + 8 (RAW/RED) + 12
   * (NOTE) + 3 (FILLER) + 12 (TAGS[3]) = 79 bytes */
  ASSERT_EQ_FMT(79, (int)rec->total_byte_size, "%d");

  rc = c_orm_copybook_record_free(rec);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  PASS();
}

/**
 * @brief Tests free-format COBOL parsing with inline `REPLACING ==:TAG:== BY
 * ==LOAN==.`, scaling `P`, edited pictures, `SYNCHRONIZED`, and secondary `01`
 * records.
 * @return GREATEST test result.
 */
TEST test_copybook_parse_free_and_clauses(void) {
  static const char *free_lines[] = {
      ">>SOURCE FORMAT FREE\n",
      "REPLACING ==:TAG:== BY ==LOAN==.\n",
      "01 :TAG:-REC. *> inline comment\n",
      "   05 :TAG:-FLAG PIC X VALUE 'Y' JUSTIFIED RIGHT.\n",
      "   05 :TAG:-SYNC PIC S9(4) COMP SYNCHRONIZED.\n",
      "   05 :TAG:-SCALED PIC 999PP.\n",
      "   05 :TAG:-SMALL  PIC VP99.\n",
      "   05 :TAG:-EDITED PIC $$$,$$9.99.\n",
      "01 :TAG:-TRAILER.\n",
      "   05 :TAG:-CNT PIC 9(5).\n"};
  char free_cpy[1024];
  c_orm_cobol_record_t *rec = NULL;
  const c_orm_cobol_field_t *f_sync = NULL;
  const c_orm_cobol_field_t *f_scaled = NULL;
  const c_orm_cobol_field_t *f_small = NULL;
  const c_orm_cobol_field_t *f_edited = NULL;
  c_orm_error_t rc;

  rc = cpy_join_lines(free_lines, sizeof(free_lines) / sizeof(free_lines[0]),
                      free_cpy, sizeof(free_cpy));
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");

  rc = c_orm_copybook_parse_string(free_cpy, C_ORM_COBOL_FORMAT_AUTO, NULL, 0,
                                   &rec);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  ASSERT_STR_EQ("LOAN-REC", rec->record_name);
  ASSERT_EQ_FMT(1, (int)rec->num_secondary_records, "%d");
  ASSERT_STR_EQ("LOAN-TRAILER", rec->secondary_records[0].record_name);

  rc = c_orm_copybook_find_field(rec, "LOAN-SYNC", &f_sync);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  /* Synchronized 2-byte COMP after 1-byte X aligns to offset 2 */
  ASSERT_EQ_FMT(2, (int)f_sync->byte_offset, "%d");

  rc = c_orm_copybook_find_field(rec, "LOAN-SCALED", &f_scaled);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  ASSERT_EQ_FMT(2, (int)f_scaled->scale_p, "%d");

  rc = c_orm_copybook_find_field(rec, "LOAN-SMALL", &f_small);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  ASSERT_EQ_FMT(2, (int)f_small->frac_digits, "%d");
  ASSERT_EQ_FMT(-1, (int)f_small->scale_p, "%d");

  rc = c_orm_copybook_find_field(rec, "LOAN-EDITED", &f_edited);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  ASSERT_EQ_FMT((int)C_ORM_COBOL_PIC_NUMERIC_EDITED,
                (int)f_edited->pic_category, "%d");
  ASSERT_EQ_FMT(10, (int)f_edited->display_length, "%d");

  rc = c_orm_copybook_record_free(rec);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  PASS();
}

/**
 * @brief Verifies byte-for-byte IBM037 EBCDIC + big-endian `COMP` + `COMP-3` +
 * `SIGN LEADING SEPARATE` mainframe wire serialization and level-88 condition
 * checks against known z/OS COBOL byte vectors.
 * @return GREATEST test result.
 */
TEST test_copybook_wire_ebcdic_comp3_vectors(void) {
  static const char *wire_lines[] = {
      ">>SOURCE FORMAT FREE\n",
      "01 ACCT-REC.\n",
      "   05 ACCT-ID     PIC S9(9) USAGE COMP.\n",
      "   05 ACCT-NAME   PIC X(10).\n",
      "   05 ACCT-BAL    PIC S9(7)V99 USAGE COMP-3.\n",
      "   05 ACCT-LIMIT  PIC S9(7)V99 SIGN LEADING SEPARATE.\n",
      "   05 ACCT-STATUS PIC X(1).\n",
      "      88 ACTIVE-ACCT VALUE 'A'.\n",
      "      88 FROZEN-ACCT VALUE 'F'.\n",
      "      88 CLOSED-ACCT VALUE 'C'.\n",
      "   05 ACCT-MONTH  PIC 99.\n",
      "      88 VALID-MONTH VALUE 1 THRU 12.\n"};
  char wire_cpy[1024];

  /* Expected 32-byte IBM037 mainframe record:
   * [0..3]   ACCT-ID = 42 -> 0x00 0x00 0x00 0x2A
   * [4..13]  ACCT-NAME = "ALICE" + 5 EBCDIC spaces -> C1 D3 C9 C3 C5 40..40
   * [14..18] ACCT-BAL = -12345.67 -> 00 12 34 56 7D
   * [19..28] ACCT-LIMIT = +50000.25 -> '+' (0x4E) "005000025" (F0 F0 F5..)
   * [29]     ACCT-STATUS = 'A' -> 0xC1
   * [30..31] ACCT-MONTH = 11 -> F1 F1
   */
  static const unsigned char expected_ibm037[32] = {
      0x00, 0x00, 0x00, 0x2A, 0xC1, 0xD3, 0xC9, 0xC3, 0xC5, 0x40, 0x40,
      0x40, 0x40, 0x40, 0x00, 0x12, 0x34, 0x56, 0x7D, 0x4E, 0xF0, 0xF0,
      0xF5, 0xF0, 0xF0, 0xF0, 0xF0, 0xF2, 0xF5, 0xC1, 0xF1, 0xF1};

  c_orm_cobol_record_t *rec = NULL;
  TestAcctWire src;
  TestAcctWire dst;
  cdd_c_abstract_struct_t abs;
  unsigned char wire[64];
  unsigned char wire_abs[64];
  size_t packed_len = 0;
  char bal_str[32];
  char lim_str[32];
  int matched = 0;
  c_orm_error_t rc;

  rc = cpy_join_lines(wire_lines, sizeof(wire_lines) / sizeof(wire_lines[0]),
                      wire_cpy, sizeof(wire_cpy));
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");

  rc = c_orm_copybook_parse_string(wire_cpy, C_ORM_COBOL_FORMAT_FREE, NULL, 0,
                                   &rec);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  ASSERT_EQ_FMT(32, (int)rec->total_byte_size, "%d");

  memset(&src, 0, sizeof(src));
  src.acct_id = 42;
  rc = C_ORM_STRCPY(src.acct_name, sizeof(src.acct_name), "ALICE");
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  rc = c_orm_decimal_from_str("-12345.67", &src.acct_bal);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  rc = c_orm_decimal_from_str("50000.25", &src.acct_limit);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  rc = C_ORM_STRCPY(src.acct_status, sizeof(src.acct_status), "A");
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  src.acct_month = 11;

  rc =
      c_orm_copybook_pack_struct(rec, &TestAcctWire_meta, &src, C_ORM_CP_IBM037,
                                 1, wire, sizeof(wire), &packed_len);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  ASSERT_EQ_FMT(32, (int)packed_len, "%d");
  ASSERT_EQ_FMT(0, memcmp(wire, expected_ibm037, 32), "%d");

  memset(&dst, 0, sizeof(dst));
  rc = c_orm_copybook_unpack_struct(rec, &TestAcctWire_meta, wire, packed_len,
                                    C_ORM_CP_IBM037, 1, &dst);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  ASSERT_EQ_FMT(42, (int)dst.acct_id, "%d");
  ASSERT_STR_EQ("ALICE", dst.acct_name);
  rc = c_orm_decimal_to_str(&dst.acct_bal, bal_str, sizeof(bal_str), NULL);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  ASSERT_STR_EQ("-12345.67", bal_str);
  rc = c_orm_decimal_to_str(&dst.acct_limit, lim_str, sizeof(lim_str), NULL);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  ASSERT_STR_EQ("50000.25", lim_str);
  ASSERT_STR_EQ("A", dst.acct_status);
  ASSERT_EQ_FMT(11, (int)dst.acct_month, "%d");

  /* Abstract struct round-trip on identical IBM037 wire bytes */
  rc = cdd_c_abstract_struct_init(&abs);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  rc = c_orm_copybook_unpack_abstract(rec, wire, packed_len, C_ORM_CP_IBM037, 1,
                                      &abs);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  rc = c_orm_copybook_pack_abstract(rec, &abs, C_ORM_CP_IBM037, 1, wire_abs,
                                    sizeof(wire_abs), &packed_len);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  ASSERT_EQ_FMT(0, memcmp(wire_abs, expected_ibm037, 32), "%d");
  (void)cdd_c_abstract_struct_free(&abs);

  /* Level-88 condition checks and domain validation */
  rc = c_orm_copybook_validate_88(rec, &TestAcctWire_meta, &dst);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  rc = c_orm_copybook_check_condition(rec, &TestAcctWire_meta, &dst,
                                      "ACTIVE-ACCT", &matched);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  ASSERT_EQ_FMT(1, matched, "%d");
  rc = c_orm_copybook_check_condition(rec, &TestAcctWire_meta, &dst,
                                      "CLOSED-ACCT", &matched);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  ASSERT_EQ_FMT(0, matched, "%d");
  rc = c_orm_copybook_check_condition(rec, &TestAcctWire_meta, &dst,
                                      "NO-SUCH-COND", &matched);
  ASSERT_EQ_FMT(C_ORM_ERROR_NOT_FOUND, rc, "%d");

  /* Invalid status or month fails validate_88 */
  dst.acct_status[0] = 'Z';
  rc = c_orm_copybook_validate_88(rec, &TestAcctWire_meta, &dst);
  ASSERT_EQ_FMT(C_ORM_ERROR_VALIDATION, rc, "%d");
  dst.acct_status[0] = 'F';
  dst.acct_month = 13;
  rc = c_orm_copybook_validate_88(rec, &TestAcctWire_meta, &dst);
  ASSERT_EQ_FMT(C_ORM_ERROR_VALIDATION, rc, "%d");

  rc = c_orm_copybook_record_free(rec);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  PASS();
}

/**
 * @brief Tests complex wire marshaling across DCLGEN `49`-level `VARCHAR` &
 * `VARGRAPHIC`, `COMP-1`, `COMP-2`, `COMP-5`, `GRAPHIC`, `FOR BIT DATA`, `BLANK
 * WHEN ZERO`, and `OCCURS DEPENDING ON`.
 * @return GREATEST test result.
 */
TEST test_copybook_wire_complex_odo_and_dclgen(void) {
  static const char *complex_lines[] = {
      ">>SOURCE FORMAT FREE\n",
      "EXEC SQL DECLARE CUSTOMER_LEDGER TABLE (\n",
      "  CUST_SEQ BIGINT NOT NULL PRIMARY KEY,\n",
      "  CUST_RAW CHAR(4) FOR BIT DATA NOT NULL\n",
      ") END-EXEC.\n",
      "01 CUSTOMER-LEDGER.\n",
      "   05 CUST-SEQ   PIC S9(18) USAGE COMP-5.\n",
      "   05 CUST-RATE  USAGE COMP-1.\n",
      "   05 CUST-SCORE USAGE COMP-2.\n",
      "   05 CUST-NOTE.\n",
      "      49 CUST-NOTE-LEN  PIC S9(4) USAGE COMP.\n",
      "      49 CUST-NOTE-TEXT PIC X(20).\n",
      "   05 CUST-GNOTE.\n",
      "      49 CUST-GNOTE-LEN  PIC S9(4) USAGE COMP.\n",
      "      49 CUST-GNOTE-TEXT PIC G(8) USAGE DISPLAY-1.\n",
      "   05 CUST-KANJI PIC G(4) USAGE DISPLAY-1.\n",
      "   05 CUST-RAW   PIC X(4).\n",
      "   05 CUST-BWZ   PIC 9(5) BLANK WHEN ZERO.\n",
      "   05 ITEM-CNT   PIC S9(4) USAGE COMP.\n",
      "   05 ITEM-AMT   PIC S9(5)V99 USAGE COMP-3\n",
      "      OCCURS 1 TO 5 TIMES DEPENDING ON ITEM-CNT.\n"};
  char complex_cpy[2048];

  c_orm_cobol_record_t *rec = NULL;
  TestComplexWire src;
  TestComplexWire dst;
  c_orm_decimal_t items_in[3];
  unsigned char wire[256];
  size_t wire_len = 0;
  char dec_buf[32];
  int is_z = 0;
  c_orm_error_t rc;

  rc = cpy_join_lines(complex_lines,
                      sizeof(complex_lines) / sizeof(complex_lines[0]),
                      complex_cpy, sizeof(complex_cpy));
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");

  rc = c_orm_copybook_parse_string(complex_cpy, C_ORM_COBOL_FORMAT_FREE, NULL,
                                   0, &rec);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");

  memset(&src, 0, sizeof(src));
  src.cust_seq =
      (c_orm_int64_t)98765 * (c_orm_int64_t)100000 + (c_orm_int64_t)43210;
  src.cust_rate = 3.5f;
  src.cust_score = -42.125;
  src.cust_note = "MAINFRAME WIRE";
  src.cust_gnote = "DB2UTF16";
  src.cust_kanji = "ABCD";
  src.cust_raw[0] = 0xDE;
  src.cust_raw[1] = 0xAD;
  src.cust_raw[2] = 0xBE;
  src.cust_raw[3] = 0xEF;
  rc = c_orm_decimal_zero(0, &src.cust_bwz);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  src.item_cnt = 3;
  rc = c_orm_decimal_from_str("10.50", &items_in[0]);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  rc = c_orm_decimal_from_str("-250.75", &items_in[1]);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  rc = c_orm_decimal_from_str("999.99", &items_in[2]);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  src.item_amt.data = items_in;
  src.item_amt.length = 3;
  src.item_amt.capacity = 3;

  rc = c_orm_copybook_pack_struct(rec, &TestComplexWire_meta, &src,
                                  C_ORM_CP_IBM037, 1, wire, sizeof(wire),
                                  &wire_len);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  /* Max size includes 5 * 4 = 20 ODO bytes; active ODO count=3 trims 2 * 4 = 8
   * trailing bytes! */
  ASSERT_EQ_FMT((int)(rec->total_byte_size - 8), (int)wire_len, "%d");

  memset(&dst, 0, sizeof(dst));
  rc = c_orm_copybook_unpack_struct(rec, &TestComplexWire_meta, wire, wire_len,
                                    C_ORM_CP_IBM037, 1, &dst);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  ASSERT_EQ(src.cust_seq, dst.cust_seq);
  ASSERT(dst.cust_rate > 3.49f && dst.cust_rate < 3.51f);
  ASSERT(dst.cust_score < -42.12 && dst.cust_score > -42.13);
  ASSERT_STR_EQ("MAINFRAME WIRE", dst.cust_note);
  ASSERT_STR_EQ("DB2UTF16", dst.cust_gnote);
  ASSERT_STR_EQ("ABCD", dst.cust_kanji);
  ASSERT_EQ_FMT(0xDE, (int)dst.cust_raw[0], "%d");
  ASSERT_EQ_FMT(0xEF, (int)dst.cust_raw[3], "%d");
  rc = c_orm_decimal_is_zero(&dst.cust_bwz, &is_z);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  ASSERT_EQ_FMT(1, is_z, "%d");
  ASSERT_EQ_FMT(3, (int)dst.item_cnt, "%d");
  ASSERT_EQ_FMT(3, (int)dst.item_amt.length, "%d");

  rc = c_orm_decimal_to_str(&((c_orm_decimal_t *)dst.item_amt.data)[1], dec_buf,
                            sizeof(dec_buf), NULL);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  ASSERT_STR_EQ("-250.75", dec_buf);

  /* ODO out-of-range triggers C_ORM_ERROR_VALIDATION */
  src.item_cnt = 9;
  rc = c_orm_copybook_pack_struct(rec, &TestComplexWire_meta, &src,
                                  C_ORM_CP_IBM037, 1, wire, sizeof(wire),
                                  &wire_len);
  ASSERT_EQ_FMT(C_ORM_ERROR_VALIDATION, rc, "%d");

  (void)c_orm_system_free(dst.cust_note);
  (void)c_orm_system_free(dst.cust_gnote);
  (void)c_orm_system_free(dst.cust_kanji);
  (void)c_orm_system_free(dst.item_amt.data);
  rc = c_orm_copybook_record_free(rec);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  PASS();
}

/**
 * @brief Tests bidirectional Copybook <-> SQL DDL / DCLGEN / C89 codegen and
 * CLI subcommands (`cpy2c`, `cpy2sql`, `sql2cpy`, `sql2c` on `.cpy`).
 * @return GREATEST test result.
 */
TEST test_copybook_bidirectional_codegen_and_cli(void) {
  static const char *cpy_in_lines[] = {
      "000100 01  :TAG:-LEDGER.                                                "
      "00010000\n",
      "000200     05 :TAG:-ID      PIC S9(9) COMP.                             "
      "00020000\n",
      "000300     05 :TAG:-CODE    PIC X(8).                                   "
      "00030000\n",
      "000400     05 :TAG:-BAL     PIC S9(11)V99 COMP-3.                       "
      "00040000\n",
      "000500     05 :TAG:-STATUS  PIC X(1).                                   "
      "00050000\n",
      "000600        88 :TAG:-OPEN VALUE 'O', 'P'.                             "
      "00060000\n"};
  char cpy_in[1024];

  c_orm_cobol_replace_pair_t rep;
  c_orm_cobol_record_t *rec = NULL;
  c_orm_cobol_record_t *reparsed = NULL;
  char *db2_ddl = NULL;
  char *sqlite_ddl = NULL;
  char *dclgen_cpy = NULL;
  char *free_cpy = NULL;
  char *gen_h = NULL;
  char *gen_c = NULL;
  struct sql_table_t *tbls = NULL;
  size_t n_tbls = 0;
  c_orm_db_t *mem_db = NULL;
  c_orm_error_t rc;

  rc = cpy_join_lines(cpy_in_lines,
                      sizeof(cpy_in_lines) / sizeof(cpy_in_lines[0]), cpy_in,
                      sizeof(cpy_in));
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");

  rep.from_text = ":TAG:";
  rep.to_text = "GL";

  rc = c_orm_copybook_parse_string(cpy_in, C_ORM_COBOL_FORMAT_FIXED, &rep, 1,
                                   &rec);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");

  /* 1. Copybook -> Db2 DDL & SQLite DDL -> re-parse with parse_sql_ddl */
  rc = c_orm_copybook_emit_sql_ddl(rec, C_TO_SQL_DIALECT_DB2, &db2_ddl);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  ASSERT(strstr(db2_ddl, "DECIMAL(13, 2)") != NULL);
  ASSERT(strstr(db2_ddl, "CHECK (GL_STATUS = 'O' OR GL_STATUS = 'P')") != NULL);

  rc = parse_sql_ddl(db2_ddl, &tbls, &n_tbls);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  ASSERT_EQ_FMT(1, (int)n_tbls, "%d");
  (void)sql_table_free(&tbls[0]);
  (void)c_orm_system_free(tbls);
  tbls = NULL;

  rc = c_orm_copybook_emit_sql_ddl(rec, C_TO_SQL_DIALECT_SQLITE, &sqlite_ddl);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  rc = c_orm_sqlite_connect(":memory:", &mem_db);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  rc = c_orm_execute_raw(mem_db, sqlite_ddl);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  rc = mem_db->vtable->disconnect(mem_db);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");

  /* 2. SQL DDL -> Fixed-format IBM Db2 DCLGEN & Free-format Copybook ->
   * re-parse */
  rc = c_orm_sql_to_copybook_emit(db2_ddl, C_ORM_COBOL_FORMAT_FIXED, 1,
                                  &dclgen_cpy);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  ASSERT(strstr(dclgen_cpy, "EXEC SQL DECLARE GL_LEDGER TABLE") != NULL);
  ASSERT(strstr(dclgen_cpy, "01  DCLGL-LEDGER.") != NULL);
  ASSERT(strstr(dclgen_cpy, "01  IND-GL-LEDGER.") != NULL);

  rc = c_orm_copybook_parse_string(dclgen_cpy, C_ORM_COBOL_FORMAT_FIXED, NULL,
                                   0, &reparsed);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  ASSERT_EQ_FMT(1, reparsed->has_exec_sql_declare, "%d");
  rc = c_orm_copybook_record_free(reparsed);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  reparsed = NULL;

  rc = c_orm_meta_to_copybook_emit(&TestComplexWire_meta,
                                   C_ORM_COBOL_FORMAT_FREE, 1, &free_cpy);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  rc = c_orm_copybook_parse_string(free_cpy, C_ORM_COBOL_FORMAT_FREE, NULL, 0,
                                   &reparsed);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  rc = c_orm_copybook_record_free(reparsed);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");

  /* 3. CLI cpy2c, cpy2sql, sql2cpy, and sql2c (.cpy auto-detect) */
  rc = cpy_test_write_file("test_tmp_ledger.cpy", cpy_in);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");

  {
    char *argv_cpy2c[] = {"c-orm-cli",    "cpy2c",    "test_tmp_ledger.cpy",
                          "test_cpy_out", "--format", "fixed",
                          "--replacing",  ":TAG:=GL"};
    rc = c_orm_cli_run(8, argv_cpy2c);
    ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  }

  rc = cpy_test_read_file("test_cpy_out/Models.h", &gen_h);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  rc = cpy_test_read_file("test_cpy_out/Models.c", &gen_c);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  ASSERT(strstr(gen_h, "struct Gl_ledger {") != NULL);
  ASSERT(strstr(gen_h, "Gl_ledger_is_gl_open") != NULL);
  ASSERT(strstr(gen_c, "const c_orm_table_meta_t Gl_ledger_meta") != NULL);

  {
    char *argv_cpy2sql[] = {
        "c-orm-cli", "cpy2sql", "test_tmp_ledger.cpy", "test_tmp_out.sql",
        "--dialect", "db2",     "--replacing",         ":TAG:=GL"};
    rc = c_orm_cli_run(8, argv_cpy2sql);
    ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  }
  {
    char *argv_sql2cpy[] = {"c-orm-cli",
                            "sql2cpy",
                            "test_tmp_out.sql",
                            "test_tmp_out.cpy",
                            "--dclgen",
                            "--format",
                            "fixed"};
    rc = c_orm_cli_run(7, argv_sql2cpy);
    ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  }
  /* Auto-detect .cpy via c_orm_codegen_generate */
  rc = c_orm_codegen_generate("test_tmp_out.cpy", "test_cpy_out");
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");

  remove("test_tmp_ledger.cpy");
  remove("test_tmp_out.sql");
  remove("test_tmp_out.cpy");
  remove("test_cpy_out/Models.h");
  remove("test_cpy_out/Models.c");

  (void)c_orm_system_free(gen_h);
  (void)c_orm_system_free(gen_c);
  (void)c_orm_system_free(db2_ddl);
  (void)c_orm_system_free(sqlite_ddl);
  (void)c_orm_system_free(dclgen_cpy);
  (void)c_orm_system_free(free_cpy);
  rc = c_orm_copybook_record_free(rec);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  PASS();
}

/**
 * @brief Tests malformed COBOL Copybook syntax rejection, null guards, and
 * deterministic OOM fault injection across parser and codegen paths.
 * @return GREATEST test result.
 */
TEST test_copybook_errors_and_oom(void) {
  static const char *bad_cases[] = {
      "",
      "00 BAD-LEVEL PIC X(4).",
      "50 BAD-LEVEL PIC X(4).",
      "88 ORPHAN-COND VALUE 'A'.",
      "01 REC.\n 05 FLD PIC X(2).\n 88 BAD-COND.",
      "01 REC.\n 05 FLD PIC ().",
      "01 REC.\n 05 FLD PIC 9(0).",
      "01 REC.\n 05 FLD PIC 9(40).",
      "01 REC.\n 05 A PIC X(4).\n 05 B REDEFINES NO-SUCH PIC X(4).",
      "01 REC.\n 05 A PIC X(4) OCCURS 1 TO 5 DEPENDING ON NO-SUCH.",
      "01 REC.\n 05 A PIC X(4).\n 66 R RENAMES NO-SUCH.",
      "01 REC.\n 05 A PIC X(4) VALUE 'UNCLOSED.",
      "01 REC.\n 05 A PIC X(2) VALUE X'0G'.",
      "EXEC SQL DECLARE T TABLE (ID INT)",
      ">>SOURCE FORMAT BOGUS\n01 REC.\n 05 A PIC X."};

  c_orm_cobol_record_t *rec = NULL;
  size_t i;
  int step;
  c_orm_error_t rc;

  /* Null argument guards */
  ASSERT_EQ_FMT(
      C_ORM_ERROR_NULL_POINTER,
      c_orm_copybook_parse_string(NULL, C_ORM_COBOL_FORMAT_AUTO, NULL, 0, &rec),
      "%d");
  ASSERT_EQ_FMT(
      C_ORM_ERROR_NULL_POINTER,
      c_orm_copybook_parse_file(NULL, C_ORM_COBOL_FORMAT_AUTO, NULL, 0, &rec),
      "%d");
  ASSERT_EQ_FMT(C_ORM_ERROR_IO,
                c_orm_copybook_parse_file("nonexistent_copybook_404.cpy",
                                          C_ORM_COBOL_FORMAT_AUTO, NULL, 0,
                                          &rec),
                "%d");

  for (i = 0; i < sizeof(bad_cases) / sizeof(bad_cases[0]); ++i) {
    rec = NULL;
    rc = c_orm_copybook_parse_string(bad_cases[i], C_ORM_COBOL_FORMAT_FREE,
                                     NULL, 0, &rec);
    ASSERT_EQ_FMT(C_ORM_ERROR_COPYBOOK_SYNTAX, rc, "%d");
    ASSERT(rec == NULL);
  }

  /* Deterministic OOM sweep across Copybook parser + SQL emitter */
  rc = c_orm_set_allocators(cpy_oom_malloc, cpy_oom_realloc, cpy_oom_free);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");

  for (step = 0; step < 80; ++step) {
    char *ddl = NULL;
    rec = NULL;
    g_cpy_oom_countdown = step;
    rc = c_orm_copybook_parse_string("01 OOM-REC.\n"
                                     "   05 OOM-ID PIC S9(9) COMP.\n"
                                     "   05 OOM-BAL PIC S9(7)V99 COMP-3.\n"
                                     "   05 OOM-ST PIC X(1).\n"
                                     "      88 OOM-OK VALUE 'Y'.\n",
                                     C_ORM_COBOL_FORMAT_FREE, NULL, 0, &rec);
    if (rc == C_ORM_OK && rec != NULL) {
      rc = c_orm_copybook_emit_sql_ddl(rec, C_TO_SQL_DIALECT_DB2, &ddl);
      if (rc == C_ORM_OK && ddl != NULL) {
        (void)c_orm_system_free(ddl);
        (void)c_orm_copybook_record_free(rec);
        break;
      }
      (void)c_orm_copybook_record_free(rec);
    }
    if (rc != C_ORM_ERROR_MEMORY) {
      g_cpy_oom_countdown = -1;
      (void)c_orm_reset_allocators();
      ASSERT_EQ_FMT(C_ORM_ERROR_MEMORY, rc, "%d");
    }
  }

  g_cpy_oom_countdown = -1;
  rc = c_orm_reset_allocators();
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  PASS();
}

/**
 * @brief Exhaustively exercises COBOL Copybook format detection, inline/API
 * `REPLACING`, `EXEC SQL` variations, all `PIC`/`USAGE`/`OCCURS`/`RENAMES`/
 * `88`/`77`/elementary-`01` clauses, syntax error branches, Safe CRT failures,
 * and OOM fault-injection sweeps across `c_orm_copybook.c`.
 */
TEST test_copybook_parser_exhaustive_and_oom(void) {
  static const char *const extra_bad_free[] = {
      "",
      ". .",
      "01 EMPTY-REC.",
      "99 BAD-LVL PIC X.",
      "05 ORPHAN-FLD PIC X.",
      "88 ORPHAN-COND VALUE 'A'.",
      "01",
      "01 PIC X(2).",
      "66 PIC X(2).",
      "77 PIC X(2).",
      "01 R.\n 05 A PIC.",
      "01 R.\n 05 A PIC .",
      "01 R.\n 05 A PIC",
      "9999999 BAD-BIG PIC X.",
      "01 R.\n 05 ( ).",
      "01 R.\n 05 A PIC X VALUE ALL .",
      "01 R.\n 05 G1.\n 10 G2.\n 15 BAD.",
      "01 R.\n 05 BAD-SYNC PIC X COMP SYNC.",
      "01 R.\n 05 G1.\n 10 BAD-REDEF REDEFINES NOPE PIC X.",
      "01 R.\n 05 A PIC (3)X.",
      "01 R.\n 05 A PIC X(0).",
      "01 R.\n 05 A PIC X(99999).",
      "01 R.\n 05 A PIC X().",
      "01 R.\n 05 A PIC X(2.",
      "01 R.\n 05 A PIC S(2)9.",
      "01 R.\n 05 A PIC V(2)9.",
      "01 R.\n 05 A PIC CR(2).",
      "01 R.\n 05 A PIC 9S9.",
      "01 R.\n 05 A PIC SX.",
      "01 R.\n 05 A PIC SA.",
      "01 R.\n 05 A PIC VX.",
      "01 R.\n 05 A PIC VA.",
      "01 R.\n 05 A PIC 9V9V9.",
      "01 R.\n 05 A PIC V.",
      "01 R.\n 05 A PIC P.",
      "01 R.\n 05 A PIC GX.",
      "01 R.\n 05 A PIC XG.",
      "01 R.\n 05 A PIC S$99.",
      "01 R.\n 05 A PIC 9(39).",
      "01 R.\n 05 A PIC Q.",
      "01 R.\n 05 A PIC X(2) X'0'.",
      "01 R.\n 05 A PIC X(2) X''.",
      "01 R.\n 05 A PIC X(2) X'0/'.",
      "01 R.\n 05 A PIC X(2) X'0:'.",
      "01 R.\n 05 A PIC X(2) X'G0'.",
      "01 R.\n 05 A PIC C.",
      "01 R.\n 05 A PIC D.",
      "01 R.\n 05 A PIC DA.",
      "01 R.\n 05 A PIC X(2A).",
      "01 R.\n 05 A PIC X(1)V.",
      "01 R.\n 05 A PIC A(1)V.",
      "01 R.\n 05 A PIC G(1)V.",
      "01 R.\n 05 A PIC X OCCURS /2.",
      "01 R.\n 05 A PIC X OCCURS 0 TO 0.",
      "01 R.\n 05 A PIC X.\n 66 RN RENAMES A THRU MISSING.",
      "01 R.\n 66 RN RENAMES A",
      "01 R.\n 05 A PIC X .:",
      "01 R.\n 05 A PIC X ./",
      "01 R.\n 05 A.:",
      "01 R.\n 05 X",
      "01 R.\n 05 A PIC X(2) X'0A.",
      "01 R.\n 05 A PIC X(2) 'UNCLOSED",
      "REPLACING ==UNCLOSED",
      "REPLACING ==A== NOPE ==B==\n01 R.\n 05 A PIC X.",
      "REPLACING ==A== BY NOPE\n01 R.\n 05 A PIC X.",
      "REPLACING ==A== BY ==UNCLOSED",
      "EXEC SQL DECLARE S.T TABLE (ID INT) END-EXEC\nEXEC SQL SELECT 1",
      "01 R.\n 66 RN BAD.",
      "01 R.\n 66 RN RENAMES.",
      "01 R.\n 66 RN RENAMES A THRU.",
      "01 R.\n 66 RN RENAMES A THRU B EXTRA.",
      "01 R.\n 05 B PIC X.\n 05 A PIC X.\n 66 RN RENAMES A THROUGH B.",
      "01 R.\n 05 A PIC X.\n 88.",
      "01 R.\n 05 A PIC X.\n 88 C BAD.",
      "01 R.\n 05 A PIC X.\n 88 C VALUE.",
      "01 R.\n 05 A PIC X.\n 88 C VALUE ALL",
      "01 R.\n 05 A PIC X.\n 88 C VALUE .",
      "01 R.\n 05 A PIC X.\n 88 C VALUE BADWORD.",
      "01 R.\n 05 A PIC X.\n 88 C VALUE 'A' THRU",
      "01 R.\n 05 A PIC X.\n 88 C VALUE 'A' THRU BADWORD.",
      "01 R.\n 05 A PIC X.\n 88 C VALUE 'A'",
      "01 R.\n 05 A 'STR' PIC X.",
      "01 R.\n 05 A REDEFINES.",
      "01 R.\n 05 B PIC X.\n 05 A REDEFINES B REDEFINES B PIC X.",
      "01 R.\n 05 FILLER PIC X.\n 05 A REDEFINES FILLER PIC X.",
      "01 R.\n 05 A PIC X PIC X.",
      "01 R.\n 05 A PIC S9 SIGN.",
      "01 R.\n 05 A PIC S9 SIGN IS.",
      "01 R.\n 05 A PIC S9 SIGN IS MIDDLE.",
      "01 R.\n 05 A PIC 9 BLANK.",
      "01 R.\n 05 A PIC 9 BLANK WHEN SPACES.",
      "01 R.\n 05 A PIC X VALUE",
      "01 R.\n 05 A PIC X VALUE 'A' VALUE 'B'.",
      "01 R.\n 05 A PIC X VALUE BADWORD.",
      "01 R.\n 05 A PIC X OCCURS.",
      "01 R.\n 05 A PIC X OCCURS ABC.",
      "01 R.\n 05 A PIC X OCCURS 0.",
      "01 R.\n 05 A PIC X OCCURS 1 TO.",
      "01 R.\n 05 A PIC X OCCURS 5 TO 2.",
      "01 R.\n 05 A PIC X OCCURS 1 TO ABC.",
      "01 R.\n 05 A PIC X OCCURS 2 DEPENDING.",
      "01 R.\n 05 N PIC X.\n 05 A PIC X OCCURS 1 TO 2 DEPENDING ON N.",
      "01 R.\n 05 A PIC X UNKNOWN-CLAUSE.",
      "01 R.\n 05 A PIC X",
      "01 R.\n 05 GRP PIC X.\n 10 CH PIC X.",
      "01 R.\n 05 ELEM-NO-PIC.",
      "01 R.\n 05 A PIC X COMP.",
      "01 R.\n 05 A PIC 9(19) COMP.",
      "01 R.\n 05 A PIC X COMP-3.",
      "77 BAD-77.",
      "77 BAD-77 PIC X COMP.",
      "01 R.\n 05 A PIC X.\n01 SEC-BAD.\n 05 B.",
      "01 R.\n 05 A PIC X.\n01 SEC-BAD2.\n 05 B PIC X COMP."};

  static const char *const rich_lines[] = {
      "\r\n\t  *> Leading comment and CRLF\r\n",
      "REPLACING ==:P:== BY ==acct== ==:Z:== BY ==10== .\n",
      "EXEC SQL INCLUDE SQLCA END-EXEC.\n",
      "EXEC SQL DECLARE \"PROD\".\"ACCOUNTS\" TABLE (\n",
      "  ID INTEGER NOT NULL,\n",
      "  INT_V CHAR(3),\n",
      "  GRP_C1 SMALLINT DEFAULT 0,\n",
      "  RAW_TOK CHAR(4) FOR BIT DATA,\n",
      "  UNM INT,\n",
      "  CONSTRAINT UQ_RAW UNIQUE (RAW_TOK),\n",
      "  CONSTRAINT PK_ACCTS PRIMARY KEY (ID)\n",
      ") END-EXEC.\n",
      "EXEC SQL DECLARE IGNORED_SECOND TABLE (X INT) END-EXEC\n",
      "77 ws-standalone pic s9(4) computational value -1.\n",
      "   88 ws-neg values are -9 thru -1, zero, zeros, zeroes.\n",
      "77 ws-second-77 pic x(2) values are \"ok\".\n",
      "01 :P:-rec.\n",
      "   05 :P:-id is pic is s9(9) usage is comp-4 sync left value +0.\n",
      "   05 :P:-xxx pic 9.\n",
      "   05 executing pic x.\n",
      "   05 exec-1 pic x.\n",
      "   05 exec_1 pic x.\n",
      "   05 -lead pic x.\n",
      "   05 exec pic x(2) value x\"0a\".\n",
      "   05 1st-col picture x(2) justified right value all 'a''b'; .\n",
      "   05 int pic a(3), just value space.\n",
      "   05 raw-tok pic x(4) value x'0aF91b2C'.\n",
      "   05 nat-auto pic n(2) value spaces.\n",
      "   05 nat-exp pic g(2) usage display-1.\n",
      "   05 nat-kw pic g(2) usage national.\n",
      "   05 nat-disp usage display pic g(2).\n",
      "   05 disp-exp pic x(2) usage display value low-value.\n",
      "   05 disp-lv pic x(2) value low-values.\n",
      "   05 disp-hv1 pic x(2) value high-value.\n",
      "   05 disp-hv2 pic x(2) value high-values.\n",
      "   05 bin-u pic 9(4) binary sync right.\n",
      "   05 c5-u pic s9(10) computational-5 sync.\n",
      "   05 f1-u comp-1 sync.\n",
      "   05 f1-b computational-1.\n",
      "   05 f2-u comp-2 sync.\n",
      "   05 f2-b computational-2.\n",
      "   05 c4-full pic s9(4) computational-4.\n",
      "   05 x-fld pic x.\n",
      "   05 grp-sync sync.\n",
      "      10 gs-1 pic x.\n",
      "   05 arr2 pic x occurs 2 indexed ix2 ascending arr2.\n",
      "   05 arr3 pic x occurs 2 indexed .\n",
      "   05 arr4 pic x occurs 2 ascending .\n",
      "   05 p3-a pic s9(5)v99 computational-3 value .5.\n",
      "   05 p3-b pic s9(5)v99 packed-decimal.\n",
      "   05 p-post pic 9v9p.\n",
      "   05 al-mix pic a(2)9(2).\n",
      "   05 s-tr pic s9(3) sign is trailing separate character.\n",
      "   05 s-ld pic s9(3) sign leading.\n",
      "   05 bwz1 pic 9(3) blank when zero.\n",
      "   05 bwz2 pic 9(3) blank zeros.\n",
      "   05 bwz3 pic 9(3) blank when zeroes.\n",
      "   05 ed-cr pic $**,**9.99cr.\n",
      "   05 ed-db pic +++,++9.99db.\n",
      "   05 ed-zz pic z(2)x.\n",
      "   05 g49-a.\n",
      "      49 g49-a1 pic 9(4) comp.\n",
      "      10 g49-a2 pic x(2).\n",
      "   05 g49-b.\n",
      "      49 g49-b1 pic x(2).\n",
      "      49 g49-b2 pic x(2).\n",
      "   05 g49-c.\n",
      "      49 g49-c1 pic 9v9 comp.\n",
      "      49 g49-c2 pic x(2).\n",
      "   05 g49-d.\n",
      "      49 g49-d1 pic 9(6) comp.\n",
      "      49 g49-d2 pic x(2).\n",
      "   05 g49-e.\n",
      "      49 g49-e1 pic s9(4) comp.\n",
      "      49 g49-e2 pic 9(2).\n",
      "   05 st-all pic x(2).\n",
      "      88 st-all-x value all 'X'.\n",
      "   05 ed-al pic xx/xx/00b*-\n",
      "      *> inline comment\n",
      "      .\n",
      "   05 sync-disp pic x(2) synchronized.\n",
      "   05 filler pic x(1).\n",
      "   05 pic x(2).\n",
      "   05 small-base pic x(2).\n",
      "   05 big-redef redefines small-base pic x(6).\n",
      "   05 grp-comp usage comp.\n",
      "      10 grp-c1 pic s9(4).\n",
      "      10 grp-c2 pic s9(8) sync.\n",
      "   05 vg-fld.\n",
      "      49 vg-len pic s9(4) comp-5.\n",
      "      49 vg-txt pic g(:Z:).\n",
      "   05 arr-cnt pic 9(2) comp.\n",
      "   05 arr-el pic x(2) occurs 1 to 3 depending arr-cnt\n",
      "      ascending key is arr-el descending key arr-el indexed by ix1.\n",
      "   05 st-fl pic x(2).\n",
      "      88 st-ok value 'AA' through 'ZZ' '00'.\n",
      "   66 rn-single renames small-base.\n",
      "   66 rn-span renames small-base through big-redef.\n",
      "01 solo-elem pic x(5).\n",
      "01 solo-f1 comp-1.\n",
      "01 solo-f2 comp-2.\n"};
  char rich_free[4096];

  const char *fixed_edge = "      * space-prefixed fixed comment\r\n"
                           "      / space-prefixed page eject\r\n"
                           "000100D debug line\r\n"
                           "000110d lowercase debug line\r\n"
                           "000120 >>SOURCE FORMAT FREE\r\n"
                           "01 FX-REC.\r\n"
                           ">>SOURCE FORMAT FIXED\r\n"
                           "short\r\n"
                           "000200     05 FX-STR PIC X(12) VALUE 'HELLO ''     "
                           "                         9999\r\n"
                           "000210-    'WORLD'.                                "
                           "                         9999\r\n"
                           "000220     05 FX-NUM PIC 9\r\n"
                           "000230-    (4) VALUE 12.\r\n"
                           "000235       \r\n"
                           "000240-      \r\n";

  const char *fixed_bad_ind = "000100X    01 BAD-IND.\n";
  const char *rep_non_pseudo = "REPLACING OTHER\n";
  const char *exec_Quoted_Fail =
      "EXEC SQL DECLARE \"S\".\"T\" TABLE (INVALID SYNTAX @@@) END-EXEC.\n"
      "01 R.\n 05 A PIC X.\n";
  const char *exec_String_Esc =
      "01 R.\n 05 A PIC X(6) VALUE 'A''B'.\n"
      "EXEC SQL DECLARE S.T TABLE (A CHAR(6) PRIMARY KEY) END-EXEC.\n";

  c_orm_cobol_record_t *rec = NULL;
  const c_orm_cobol_field_t *f = NULL;
  c_orm_cobol_replace_pair_t reps[2];
  FILE *tmp_fp = NULL;
  size_t i;
  int step;
  c_orm_error_t rc;

  /* 1. Null / bad arguments on find_field, record_free, replacements */
  ASSERT_EQ_FMT(C_ORM_OK, c_orm_copybook_record_free(NULL), "%d");
  ASSERT_EQ_FMT(C_ORM_ERROR_NULL_POINTER,
                c_orm_copybook_find_field(NULL, "A", &f), "%d");
  ASSERT_EQ_FMT(C_ORM_ERROR_NULL_POINTER,
                c_orm_copybook_parse_string("01 R.\n05 A PIC X.",
                                            C_ORM_COBOL_FORMAT_FREE, NULL, 1,
                                            &rec),
                "%d");
  reps[0].from_text = NULL;
  reps[0].to_text = "X";
  ASSERT_EQ_FMT(C_ORM_ERROR_NULL_POINTER,
                c_orm_copybook_parse_string("01 R.\n05 A PIC X.",
                                            C_ORM_COBOL_FORMAT_FREE, reps, 1,
                                            &rec),
                "%d");
  reps[0].from_text = "X";
  reps[0].to_text = NULL;
  ASSERT_EQ_FMT(C_ORM_ERROR_NULL_POINTER,
                c_orm_copybook_parse_string("01 R.\n05 A PIC X.",
                                            C_ORM_COBOL_FORMAT_FREE, reps, 1,
                                            &rec),
                "%d");

  /* 2. Rich free-format + EXEC SQL + REPLACING + 77 + secondary 01 */
  ASSERT_EQ_FMT(C_ORM_OK,
                cpy_join_lines(rich_lines,
                               sizeof(rich_lines) / sizeof(rich_lines[0]),
                               rich_free, sizeof(rich_free)),
                "%d");
  reps[0].from_text = "";
  reps[0].to_text = "IGNORED";
  reps[1].from_text = " \t == \t :UNUSED: \t == \t ";
  reps[1].to_text = " \t == \t VAL \t == \t ";
  rc = c_orm_copybook_parse_string(rich_free, C_ORM_COBOL_FORMAT_AUTO, reps, 2,
                                   &rec);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  ASSERT(rec != NULL);
  ASSERT_STR_EQ("PROD", rec->sql_schema_name);
  ASSERT_STR_EQ("ACCOUNTS", rec->sql_table_name);
  ASSERT_EQ_FMT(3, (int)rec->num_secondary_records, "%d");

  ASSERT_EQ_FMT(C_ORM_ERROR_NULL_POINTER,
                c_orm_copybook_find_field(rec, NULL, &f), "%d");
  ASSERT_EQ_FMT(C_ORM_ERROR_NULL_POINTER,
                c_orm_copybook_find_field(rec, "ID", NULL), "%d");
  ASSERT_EQ_FMT(C_ORM_OK, c_orm_copybook_find_field(rec, "ws_standalone", &f),
                "%d");
  ASSERT_EQ_FMT(C_ORM_OK, c_orm_copybook_find_field(rec, "grp_c1", &f), "%d");
  ASSERT_EQ_FMT(C_ORM_OK, c_orm_copybook_find_field(rec, "f_1st_col", &f),
                "%d");
  ASSERT_EQ_FMT(C_ORM_OK, c_orm_copybook_find_field(rec, "int_v", &f), "%d");
  ASSERT_EQ_FMT(C_ORM_OK, c_orm_copybook_find_field(rec, "ACCT_ID", &f), "%d");
  ASSERT_EQ_FMT(1, f->sql_is_pk, "%d");
  ASSERT_EQ_FMT(C_ORM_OK, c_orm_copybook_find_field(rec, "VG-FLD", &f), "%d");
  ASSERT_EQ_FMT(1, f->is_dclgen_varchar, "%d");
  ASSERT_EQ_FMT(1, f->is_dclgen_vargraphic, "%d");
  ASSERT_EQ_FMT(C_ORM_ERROR_NOT_FOUND,
                c_orm_copybook_find_field(rec, "~bad", &f), "%d");
  ASSERT_EQ_FMT(C_ORM_ERROR_NOT_FOUND,
                c_orm_copybook_find_field(rec, "ACCT", &f), "%d");
  ASSERT_EQ_FMT(C_ORM_OK, c_orm_copybook_record_free(rec), "%d");

  /* Pseudotext edge forms (==   ==, ==AB, long needle > src_len) */
  {
    c_orm_cobol_replace_pair_t edge_reps[3];
    edge_reps[0].from_text = "==   ==";
    edge_reps[0].to_text = "X";
    edge_reps[1].from_text = "==AB";
    edge_reps[1].to_text = "Y";
    edge_reps[2].from_text = "VERY_LONG_NEEDLE_EXCEEDING_COPYBOOK_LENGTH";
    edge_reps[2].to_text = "Z";
    rec = NULL;
    ASSERT_EQ_FMT(C_ORM_OK,
                  c_orm_copybook_parse_string("01 R.\n 05 A PIC X.",
                                              C_ORM_COBOL_FORMAT_FREE,
                                              edge_reps, 3, &rec),
                  "%d");
    ASSERT_EQ_FMT(C_ORM_OK, c_orm_copybook_record_free(rec), "%d");
  }

  /* Format detection, inline REPLACING without trailing dot, EXEC SQL edge
   * forms */
  {
    static const char *const auto_cases[] = {
        "\n01 R.\n 05 A PIC *.\n*",
        "01 R.",
        ">>SOURCE FORMAT FREE",
        ">>SOURCE FORMAT FIXED",
        "000100X",
        "      X",
        "000999 >>SOURCE FORMAT FREE\n01 R.\n 05 A PIC X.",
        "/00100     01 R.\n000200     05 A PIC X.",
        "000100     01 R.\n000200     05 A PIC X.\nshort",
        "000100     01 R.\n000200     05 A PIC X.\n000300*eof",
        "000100     01 R.\n000200     05 A PIC X(6) VALUE 'AB\n000210-      "
        "\n000220-    'CD'.\n",
        "000100     01 R.\n000200     05 A PIC X(6) VALUE 'AB\n000210-    "
        "CD'.\n",
        "000100     01 R.\n000200     05 A PIC X(6) VALUE 'AB\n000210     "
        "CD'.\n",
        "REPLACING",
        "REPLACING ==A==",
        "REPLACING ==A== BY",
        "REPLACING ==:K:== BY ==V==01 :K:-R.\n 05 A PIC X.",
        "01 R.\n 05 A PIC X. EXEC",
        "01 R.\n 05 A PIC X. EXEC SQL SELECT 1 END-EXEC",
        "EXEC SQL DECLARE TEND-EXEC\n01 R.\n 05 A PIC X.",
        "EXEC SQL DECLARE (A INT) END-EXEC\n01 R.\n 05 A PIC X.",
        "EXEC SQL DECLARE \"S\"X.\"T\"X TABLE (A INT) END-EXEC\n01 R.\n 05 A "
        "PIC X.",
        "EXEC SQL DECLARE S.T TABLE X END-EXEC\n01 R.\n 05 A PIC X.",
        "EXEC SQL DECLARE CUR CURSOR FOR SELECT 1 END-EXEC\n01 R.\n 05 A PIC "
        "X.",
        "EXEC SQL DECLARE T TABLE END-EXEC\n01 R.\n 05 A PIC X.",
        "EXEC SQL DECLARE T(A CHAR(1)) END-EXEC\n01 R.\n 05 A PIC X."};
    size_t ai;
    for (ai = 0; ai < sizeof(auto_cases) / sizeof(auto_cases[0]); ++ai) {
      rec = NULL;
      rc = c_orm_copybook_parse_string(auto_cases[ai], C_ORM_COBOL_FORMAT_AUTO,
                                       NULL, 0, &rec);
      if (rc == C_ORM_OK && rec != NULL) {
        (void)c_orm_copybook_record_free(rec);
      }
    }
  }

  /* Empty string in AUTO format + >128-byte free-format line + fixed indicators
   */
  rec = NULL;
  ASSERT_EQ_FMT(
      C_ORM_ERROR_COPYBOOK_SYNTAX,
      c_orm_copybook_parse_string("", C_ORM_COBOL_FORMAT_AUTO, NULL, 0, &rec),
      "%d");
  rec = NULL;
  ASSERT_EQ_FMT(C_ORM_ERROR_COPYBOOK_SYNTAX,
                c_orm_copybook_parse_string(
                    "singleline", C_ORM_COBOL_FORMAT_AUTO, NULL, 0, &rec),
                "%d");
  rec = NULL;
  ASSERT_EQ_FMT(
      C_ORM_OK,
      c_orm_copybook_parse_string(
          "01 LONG-LINE-REC.                                                  "
          "                                                            "
          "   05 LL-F PIC X(2).",
          C_ORM_COBOL_FORMAT_AUTO, NULL, 0, &rec),
      "%d");
  ASSERT_EQ_FMT(C_ORM_OK, c_orm_copybook_record_free(rec), "%d");
  rec = NULL;
  ASSERT_EQ_FMT(
      C_ORM_OK,
      c_orm_copybook_parse_string(
          "/00100*comment\n000200     01 D-REC.\n000300     05 A PIC X.",
          C_ORM_COBOL_FORMAT_FIXED, NULL, 0, &rec),
      "%d");
  ASSERT_EQ_FMT(C_ORM_OK, c_orm_copybook_record_free(rec), "%d");
  rec = NULL;
  ASSERT_EQ_FMT(C_ORM_OK,
                c_orm_copybook_parse_string(
                    "000100     01 D-REC.\n000200     05 A PIC X.",
                    C_ORM_COBOL_FORMAT_AUTO, NULL, 0, &rec),
                "%d");
  ASSERT_EQ_FMT(C_ORM_OK, c_orm_copybook_record_free(rec), "%d");

  /* 3. Fixed-format continuation, directives, short lines, space-comments */
  rec = NULL;
  rc = c_orm_copybook_parse_string(fixed_edge, C_ORM_COBOL_FORMAT_AUTO, NULL, 0,
                                   &rec);
  ASSERT_EQ_FMT(C_ORM_OK, rc, "%d");
  ASSERT_EQ_FMT(C_ORM_OK, c_orm_copybook_record_free(rec), "%d");

  rec = NULL;
  ASSERT_EQ_FMT(C_ORM_ERROR_COPYBOOK_SYNTAX,
                c_orm_copybook_parse_string(
                    fixed_bad_ind, C_ORM_COBOL_FORMAT_FIXED, NULL, 0, &rec),
                "%d");
  ASSERT_EQ_FMT(C_ORM_ERROR_COPYBOOK_SYNTAX,
                c_orm_copybook_parse_string(
                    rep_non_pseudo, C_ORM_COBOL_FORMAT_FREE, NULL, 0, &rec),
                "%d");

  rec = NULL;
  ASSERT_EQ_FMT(C_ORM_OK,
                c_orm_copybook_parse_string(
                    exec_Quoted_Fail, C_ORM_COBOL_FORMAT_FREE, NULL, 0, &rec),
                "%d");
  ASSERT_EQ_FMT(C_ORM_OK, c_orm_copybook_record_free(rec), "%d");

  rec = NULL;
  ASSERT_EQ_FMT(C_ORM_OK,
                c_orm_copybook_parse_string(
                    exec_String_Esc, C_ORM_COBOL_FORMAT_FREE, NULL, 0, &rec),
                "%d");
  ASSERT_EQ_FMT(C_ORM_OK, c_orm_copybook_record_free(rec), "%d");

  /* Standalone 77-only copybook */
  rec = NULL;
  ASSERT_EQ_FMT(C_ORM_OK,
                c_orm_copybook_parse_string("77 ONLY-77 PIC 9(4) COMP.",
                                            C_ORM_COBOL_FORMAT_FREE, NULL, 0,
                                            &rec),
                "%d");
  ASSERT_STR_EQ("STANDALONE-REC", rec->record_name);
  ASSERT_EQ_FMT(C_ORM_OK, c_orm_copybook_record_free(rec), "%d");

  /* EXEC SQL DECLARE followed by COBOL syntax error (cleanup frees sql_tbl) */
  rec = NULL;
  ASSERT_EQ_FMT(C_ORM_ERROR_COPYBOOK_SYNTAX,
                c_orm_copybook_parse_string(
                    "EXEC SQL DECLARE S.T TABLE (ID INT) END-EXEC.\n01 BAD.",
                    C_ORM_COBOL_FORMAT_FREE, NULL, 0, &rec),
                "%d");

  /* Safe CRT failure on FILLER sprintf */
  c_orm_mock_safe_crt_countdown = 0;
  rec = NULL;
  ASSERT_NEQ(C_ORM_OK, c_orm_copybook_parse_string("01 R.\n 05 FILLER PIC X.",
                                                   C_ORM_COBOL_FORMAT_FREE,
                                                   NULL, 0, &rec));
  c_orm_mock_safe_crt_countdown = -1;

  /* 4. Table of malformed copybook syntax cases */
  for (i = 0; i < sizeof(extra_bad_free) / sizeof(extra_bad_free[0]); ++i) {
    rec = NULL;
    rc = c_orm_copybook_parse_string(extra_bad_free[i], C_ORM_COBOL_FORMAT_FREE,
                                     NULL, 0, &rec);
    if (rc != C_ORM_ERROR_COPYBOOK_SYNTAX) {
      fprintf(stderr, "Case %d ('%s') got rc=%d\n", (int)i, extra_bad_free[i],
              (int)rc);
    }
    ASSERT_EQ_FMT(C_ORM_ERROR_COPYBOOK_SYNTAX, rc, "%d");
  }

  /* 5. Multi-chunk file parse + OOM sweep across rich_free, fixed_edge,
   * 77-only, and c_orm_copybook_parse_file */
  ASSERT_EQ_FMT(C_ORM_ERROR_NULL_POINTER,
                c_orm_copybook_parse_file("tmp.cpy", C_ORM_COBOL_FORMAT_AUTO,
                                          NULL, 0, NULL),
                "%d");
  ASSERT_EQ_FMT(C_ORM_OK, C_ORM_FOPEN(&tmp_fp, "test_cpy_large_tmp.cpy", "wb"),
                "%d");
  fwrite(rich_free, 1, strlen(rich_free), tmp_fp);
  fclose(tmp_fp);

  ASSERT_EQ_FMT(
      C_ORM_OK,
      c_orm_set_allocators(cpy_oom_malloc, cpy_oom_realloc, cpy_oom_free),
      "%d");

  for (step = 0; step < 500; ++step) {
    rec = NULL;
    g_cpy_oom_countdown = step;
    rc = c_orm_copybook_parse_file("test_cpy_large_tmp.cpy",
                                   C_ORM_COBOL_FORMAT_AUTO, reps, 2, &rec);
    if (rc == C_ORM_OK && rec != NULL) {
      (void)c_orm_copybook_record_free(rec);
      break;
    }
    ASSERT_EQ_FMT(C_ORM_ERROR_MEMORY, rc, "%d");
  }

  for (step = 0; step < 30; ++step) {
    rec = NULL;
    g_cpy_oom_countdown = step;
    rc = c_orm_copybook_parse_string(
        "01 LONG-LINE-REC.                                                    "
        "                                                              "
        " 05 LL-F PIC X(2).",
        C_ORM_COBOL_FORMAT_FREE, NULL, 0, &rec);
    if (rc == C_ORM_OK && rec != NULL) {
      (void)c_orm_copybook_record_free(rec);
      break;
    }
    ASSERT_EQ_FMT(C_ORM_ERROR_MEMORY, rc, "%d");
  }

  for (step = 0; step < 30; ++step) {
    rec = NULL;
    g_cpy_oom_countdown = step;
    rc = c_orm_copybook_parse_string("77 ONLY-77 PIC 9(4) COMP.",
                                     C_ORM_COBOL_FORMAT_FREE, NULL, 0, &rec);
    if (rc == C_ORM_OK && rec != NULL) {
      (void)c_orm_copybook_record_free(rec);
      break;
    }
    ASSERT_EQ_FMT(C_ORM_ERROR_MEMORY, rc, "%d");
  }

  for (step = 0; step < 40; ++step) {
    rec = NULL;
    g_cpy_oom_countdown = step;
    rc = c_orm_copybook_parse_string(fixed_edge, C_ORM_COBOL_FORMAT_FIXED, NULL,
                                     0, &rec);
    if (rc == C_ORM_OK && rec != NULL) {
      (void)c_orm_copybook_record_free(rec);
      break;
    }
    ASSERT_EQ_FMT(C_ORM_ERROR_MEMORY, rc, "%d");
  }

  /* Compact OOM sweeps for REPLACING, fixed-line sb growth, EXEC SQL quotes,
   * and 66/REDEFINES/ODO/implicit-FILLER/elementary-01/secondary-01 */
  {
    static const char *const oom_snippets[] = {
        "01 :X:-REC.\n 05 :X:-A PIC X.",
        ("000100     01 AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n"
         "000200-    BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB.\n"
         "000300     05 C PIC X.\n"),
        ("000100     01 "
         "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA.\n"
         "000200     05 BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB PIC X.\n"),
        ("01 AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA PIC X VALUE "
         "'B'.\n"),
        ("01 AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA PIC X VALUE "
         "'B''C'.\n"),
        ("01 R.\n 05 PIC X.\n 05 A PIC 9.\n 05 B REDEFINES A PIC 9.\n"
         " 05 C PIC X OCCURS 1 TO 2 DEPENDING A.\n 66 RN RENAMES A THRU B.\n"
         "01 SOLO PIC X.\n01 SEC.\n 05 D PIC X.")};
    c_orm_cobol_replace_pair_t long_rep;
    size_t si;
    long_rep.from_text = ":X:";
    long_rep.to_text =
        "LLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLLL";
    for (si = 0; si < sizeof(oom_snippets) / sizeof(oom_snippets[0]); ++si) {
      for (step = 0; step < 120; ++step) {
        rec = NULL;
        g_cpy_oom_countdown = step;
        rc = c_orm_copybook_parse_string(
            oom_snippets[si], C_ORM_COBOL_FORMAT_AUTO,
            (si == 0) ? &long_rep : NULL, (si == 0) ? 1 : 0, &rec);
        if (rc == C_ORM_OK && rec != NULL) {
          (void)c_orm_copybook_record_free(rec);
          break;
        }
        ASSERT_EQ_FMT(C_ORM_ERROR_MEMORY, rc, "%d");
      }
    }
  }

  g_cpy_oom_countdown = -1;
  ASSERT_EQ_FMT(C_ORM_OK, c_orm_reset_allocators(), "%d");
  remove("test_cpy_large_tmp.cpy");
  PASS();
}

/**
 * @brief Exhaustive C89 host struct covering nullable pointers, heap strings/
 * blobs, fixed buffers, booleans, floats, decimals, and ODO arrays.
 */
typedef struct TestWireExhaust {
  /** @brief Nullable int32 pointer. */
  int32_t *p_i32;
  /** @brief Nullable int64 pointer. */
  c_orm_int64_t *p_i64;
  /** @brief Nullable bool pointer. */
  unsigned char *p_b;
  /** @brief Non-null bool byte. */
  unsigned char b_val;
  /** @brief Nullable float pointer. */
  float *p_f32;
  /** @brief Nullable double pointer. */
  double *p_f64;
  /** @brief Nullable decimal pointer. */
  c_orm_decimal_t *p_dec;
  /** @brief Heap string pointer (`C_ORM_TYPE_STRING`). */
  char *s_heap;
  /** @brief Heap string via `CHAR_FIXED` with `fixed_length == 0`. */
  char *s_cf0;
  /** @brief Fixed char buffer (`CHAR_FIXED` with `fixed_length == 4`). */
  char s_cf4[5];
  /** @brief Heap blob (`C_ORM_TYPE_BLOB`). */
  c_orm_blob_t bl_heap;
  /** @brief Heap blob via `BINARY_FIXED` with `fixed_length == 0`. */
  c_orm_blob_t bl_bf0;
  /** @brief Fixed binary buffer (`BINARY_FIXED` with `fixed_length == 4`). */
  unsigned char bl_bf4[4];
  /** @brief Non-null int32 scalar. */
  int32_t v_i32;
  /** @brief Non-null int64 scalar. */
  c_orm_int64_t v_i64;
  /** @brief Non-null float scalar. */
  float v_f32;
  /** @brief Non-null double scalar. */
  double v_f64;
  /** @brief Non-null decimal scalar. */
  c_orm_decimal_t v_dec;
  /** @brief ODO count (`0..3`). */
  int32_t odo_cnt;
  /** @brief ODO array of `int32_t`. */
  c_orm_cobol_odo_array_t arr_i32;
  /** @brief ODO array of `c_orm_int64_t`. */
  c_orm_cobol_odo_array_t arr_i64;
  /** @brief ODO array of `c_orm_decimal_t` (fractional non-COMP-3). */
  c_orm_cobol_odo_array_t arr_dec_z;
  /** @brief ODO array of `c_orm_decimal_t` (>18 digits). */
  c_orm_cobol_odo_array_t arr_dec_big;
  /** @brief ODO array of `char *`. */
  c_orm_cobol_odo_array_t arr_str;
  /** @brief Indexed OCCURS element 1. */
  int32_t idx_1;
  /** @brief Indexed OCCURS element 2. */
  int32_t idx_2;
  /** @brief Unknown-type placeholder. */
  int32_t unk_val;
} TestWireExhaust;

/**
 * @brief Exhaustively exercises `c_orm_copybook_wire.c` across nullable/heap
 * column types, cross-type coercions, `FILLER`, `REDEFINES`, `66`, `OCCURS`/
 * `ODO` arrays, `COMP-1`/`COMP-2`/`GRAPHIC`/`VARGRAPHIC` endianness, abstract
 * structs, level-`88` evaluation, Safe CRT errors, and OOM sweeps.
 */
TEST test_copybook_wire_exhaustive_and_oom(void) {
  static const char *const wire_lines[] = {
      "EXEC SQL DECLARE T TABLE (\n",
      "  F_BIT CHAR(4) FOR BIT DATA,\n",
      "  F_BLOB CHAR(4) FOR BIT DATA\n",
      ") END-EXEC.\n",
      "01 W-REC.\n",
      "   05 FILLER PIC X(2) VALUE LOW-VALUES.\n",
      "   05 FILLER PIC X(2) VALUE HIGH-VALUES.\n",
      "   05 FILLER PIC X(2) VALUE 'XY'.\n",
      "   05 FILLER PIC 9(2) VALUE ZEROS.\n",
      "   05 FILLER PIC X(2).\n",
      "   05 P-I32 PIC S9(4) COMP VALUE 12.\n",
      "   05 P-I64 PIC S9(12) COMP.\n",
      "   05 P-B PIC 9.\n",
      "   05 B-VAL PIC X.\n",
      "   05 P-F32 COMP-1.\n",
      "   05 P-F64 COMP-2.\n",
      "   05 P-DEC PIC S9(4)V99 COMP VALUE 12.34.\n",
      "   05 S-HEAP.\n",
      "      49 S-HEAP-LEN PIC S9(4) COMP-5.\n",
      "      49 S-HEAP-TXT PIC X(8).\n",
      "   05 S-CF0.\n",
      "      49 S-CF0-LEN PIC S9(4) COMP-5.\n",
      "      49 S-CF0-TXT PIC G(4) USAGE DISPLAY-1.\n",
      "   05 S-CF4 PIC X(4) VALUE 'DEF4'.\n",
      "   05 F-BIT PIC X(4).\n",
      "   05 F-BLOB PIC X(4).\n",
      "   05 V-GR PIC G(4) USAGE DISPLAY-1 VALUE 'AB'.\n",
      "   05 V-BWZ PIC 9(4) BLANK WHEN ZERO.\n",
      "   05 V-UNSIGNED PIC 9(4) COMP.\n",
      "   05 V-LONGDEC PIC 9(20).\n",
      "   05 BASE-GRP.\n",
      "      10 BG-X PIC X(4).\n",
      "   05 REDEF-GRP REDEFINES BASE-GRP.\n",
      "      10 RG-Y PIC X(4).\n",
      "   66 RN-W RENAMES P-I32.\n",
      "   05 GRP-OCC OCCURS 2 TIMES.\n",
      "      10 GO-A PIC 9(2).\n",
      "   05 IDX PIC 9(2) OCCURS 2 TIMES.\n",
      "   05 SCALAR-OCC PIC 9(2) OCCURS 2 TIMES.\n",
      "   05 COND-GRP.\n",
      "      10 C-STR PIC X(4).\n",
      "         88 C-STR-RNG VALUE 'BA' THRU 'DZ' 'OK'.\n",
      "      10 C-NUM PIC S9(3).\n",
      "         88 C-NUM-EX VALUE 42, 100 THRU 'BAD', 1 THRU 5.\n",
      "      10 C-UNMAPPED PIC X(2).\n",
      "         88 C-UNM-OK VALUE 'AA'.\n",
      "   05 ARR-I32 PIC S9(4) COMP OCCURS 2 TIMES.\n",
      "   05 ARR-I64 PIC S9(12) COMP OCCURS 2 TIMES.\n",
      "   05 ARR-DEC-Z PIC S9(3)V99 OCCURS 2 TIMES.\n",
      "   05 ARR-DEC-BIG PIC 9(19) OCCURS 2 TIMES.\n",
      "   05 ODO-CNT PIC S9(4) COMP.\n",
      "   05 ARR-STR PIC X(3) OCCURS 0 TO 2 DEPENDING ON ODO-CNT.\n"};
  char wire_cpy[4096];
  c_orm_cobol_record_t *rec = NULL;
  unsigned char raw[512];
  size_t raw_len = 0;
  int step;
  c_orm_error_t rc;

  ASSERT_EQ_FMT(C_ORM_OK,
                cpy_join_lines(wire_lines,
                               sizeof(wire_lines) / sizeof(wire_lines[0]),
                               wire_cpy, sizeof(wire_cpy)),
                "%d");
  ASSERT_EQ_FMT(C_ORM_OK,
                c_orm_copybook_parse_string(wire_cpy, C_ORM_COBOL_FORMAT_FREE,
                                            NULL, 0, &rec),
                "%d");

  /* 1. NULL argument guards across all 6 public wire functions */
  {
    c_orm_table_meta_t dummy_m;
    TestWireExhaust dummy_s;
    cdd_c_abstract_struct_t dummy_a;
    int mflag = 0;
    memset(&dummy_m, 0, sizeof(dummy_m));
    memset(&dummy_s, 0, sizeof(dummy_s));
    memset(&dummy_a, 0, sizeof(dummy_a));

    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_pack_struct(NULL, &dummy_m, &dummy_s,
                                         C_ORM_CP_UTF8, 0, raw, sizeof(raw),
                                         NULL));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_pack_struct(rec, NULL, &dummy_s, C_ORM_CP_UTF8, 0,
                                         raw, sizeof(raw), NULL));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_pack_struct(rec, &dummy_m, NULL, C_ORM_CP_UTF8, 0,
                                         raw, sizeof(raw), NULL));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_pack_struct(rec, &dummy_m, &dummy_s, C_ORM_CP_UTF8,
                                         0, NULL, sizeof(raw), NULL));

    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_pack_abstract(NULL, &dummy_a, C_ORM_CP_UTF8, 0,
                                           raw, sizeof(raw), NULL));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_pack_abstract(rec, NULL, C_ORM_CP_UTF8, 0, raw,
                                           sizeof(raw), NULL));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_pack_abstract(rec, &dummy_a, C_ORM_CP_UTF8, 0,
                                           NULL, sizeof(raw), NULL));

    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_unpack_struct(NULL, &dummy_m, raw, sizeof(raw),
                                           C_ORM_CP_UTF8, 0, &dummy_s));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_unpack_struct(rec, NULL, raw, sizeof(raw),
                                           C_ORM_CP_UTF8, 0, &dummy_s));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_unpack_struct(rec, &dummy_m, NULL, sizeof(raw),
                                           C_ORM_CP_UTF8, 0, &dummy_s));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_unpack_struct(rec, &dummy_m, raw, sizeof(raw),
                                           C_ORM_CP_UTF8, 0, NULL));

    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_unpack_abstract(NULL, raw, sizeof(raw),
                                             C_ORM_CP_UTF8, 0, &dummy_a));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_unpack_abstract(rec, NULL, sizeof(raw),
                                             C_ORM_CP_UTF8, 0, &dummy_a));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_unpack_abstract(rec, raw, sizeof(raw),
                                             C_ORM_CP_UTF8, 0, NULL));

    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_validate_88(NULL, &dummy_m, &dummy_s));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_validate_88(rec, NULL, &dummy_s));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_validate_88(rec, &dummy_m, NULL));

    ASSERT_EQ(
        C_ORM_ERROR_NULL_POINTER,
        c_orm_copybook_check_condition(NULL, &dummy_m, &dummy_s, "X", &mflag));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_check_condition(rec, NULL, &dummy_s, "X", &mflag));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_check_condition(rec, &dummy_m, NULL, "X", &mflag));
    ASSERT_EQ(
        C_ORM_ERROR_NULL_POINTER,
        c_orm_copybook_check_condition(rec, &dummy_m, &dummy_s, NULL, &mflag));
    ASSERT_EQ(
        C_ORM_ERROR_NULL_POINTER,
        c_orm_copybook_check_condition(rec, &dummy_m, &dummy_s, "X", NULL));

    /* Empty meta (columns == NULL) pack & unpack with odo_cnt == 0 */
    ASSERT_EQ(C_ORM_OK, c_orm_copybook_pack_struct(rec, &dummy_m, &dummy_s,
                                                   C_ORM_CP_IBM037, 0, raw,
                                                   sizeof(raw), NULL));
    ASSERT_EQ(C_ORM_OK, c_orm_copybook_unpack_struct(
                            rec, &dummy_m, raw, rec->total_byte_size,
                            C_ORM_CP_IBM037, 0, &dummy_s));
  }

  /* 2. Full nullable + heap + ODO array + native/BE pack & unpack round-trips
   */
  {
    int32_t v32 = 42;
    c_orm_int64_t v64 = ((c_orm_int64_t)12345 * 1000000) + 678901;
    unsigned char vb0 = 0;
    unsigned char vb1 = 1;
    float vf = 2.5f;
    double vd = -8.25;
    c_orm_decimal_t vdec;
    int32_t i32_elems[2] = {-5, 99};
    c_orm_int64_t i64_elems[2];
    c_orm_decimal_t decz_elems[2];
    c_orm_decimal_t decb_elems[2];
    const char *str_elems[2] = {"A1", NULL};
    unsigned char b4_bytes[4] = {0x01, 0x00, 0xFE, 0x7F};
    TestWireExhaust src_s;
    TestWireExhaust dst_s;
    c_orm_column_meta_t cols[26];
    c_orm_table_meta_t meta;
    size_t k;

    i64_elems[0] = (c_orm_int64_t)10000 * 1000000;
    i64_elems[1] = -((c_orm_int64_t)20000 * 1000000);

    (void)c_orm_decimal_from_str("-34.56", &vdec);
    (void)c_orm_decimal_from_str("12.34", &decz_elems[0]);
    (void)c_orm_decimal_from_str("-5.60", &decz_elems[1]);
    (void)c_orm_decimal_from_str("1234567890123456789", &decb_elems[0]);
    (void)c_orm_decimal_from_str("9876543210987654321", &decb_elems[1]);

    memset(cols, 0, sizeof(cols));
    /* col 0 intentionally has NULL name to cover wire_ident_eq(!a) */
    cols[0].name = NULL;
    cols[0].type = (c_orm_type_t)99;
    cols[0].offset = offsetof(TestWireExhaust, unk_val);
    cols[1].name = "~unmatched";
    cols[1].type = (c_orm_type_t)99;
    cols[1].offset = offsetof(TestWireExhaust, unk_val);
    cols[2].name = "P-I32";
    cols[2].type = C_ORM_TYPE_INT32;
    cols[2].is_nullable = true;
    cols[2].offset = offsetof(TestWireExhaust, p_i32);
    cols[3].name = "p_i64";
    cols[3].type = C_ORM_TYPE_INT64;
    cols[3].is_nullable = true;
    cols[3].offset = offsetof(TestWireExhaust, p_i64);
    cols[4].name = "p_b";
    cols[4].type = C_ORM_TYPE_BOOL;
    cols[4].is_nullable = true;
    cols[4].offset = offsetof(TestWireExhaust, p_b);
    cols[5].name = "b_val";
    cols[5].type = C_ORM_TYPE_BOOL;
    cols[5].offset = offsetof(TestWireExhaust, b_val);
    cols[6].name = "p_f32";
    cols[6].type = C_ORM_TYPE_FLOAT;
    cols[6].is_nullable = true;
    cols[6].offset = offsetof(TestWireExhaust, p_f32);
    cols[7].name = "p_f64";
    cols[7].type = C_ORM_TYPE_DOUBLE;
    cols[7].is_nullable = true;
    cols[7].offset = offsetof(TestWireExhaust, p_f64);
    cols[8].name = "p_dec";
    cols[8].type = C_ORM_TYPE_DECIMAL;
    cols[8].is_nullable = true;
    cols[8].offset = offsetof(TestWireExhaust, p_dec);
    cols[9].name = "s_heap";
    cols[9].type = C_ORM_TYPE_STRING;
    cols[9].offset = offsetof(TestWireExhaust, s_heap);
    cols[10].name = "s_cf0";
    cols[10].type = C_ORM_TYPE_CHAR_FIXED;
    cols[10].fixed_length = 0;
    cols[10].offset = offsetof(TestWireExhaust, s_cf0);
    cols[11].name = "s_cf4";
    cols[11].type = C_ORM_TYPE_CHAR_FIXED;
    cols[11].fixed_length = 4;
    cols[11].offset = offsetof(TestWireExhaust, s_cf4);
    cols[12].name = "f_bit";
    cols[12].type = C_ORM_TYPE_BINARY_FIXED;
    cols[12].fixed_length = 0;
    cols[12].offset = offsetof(TestWireExhaust, bl_bf0);
    cols[13].name = "f_blob";
    cols[13].type = C_ORM_TYPE_BLOB;
    cols[13].offset = offsetof(TestWireExhaust, bl_heap);
    cols[14].name = "v_gr";
    cols[14].type = C_ORM_TYPE_STRING;
    cols[14].offset = offsetof(TestWireExhaust, s_cf0);
    cols[15].name = "v_bwz";
    cols[15].type = C_ORM_TYPE_INT32;
    cols[15].offset = offsetof(TestWireExhaust, v_i32);
    cols[16].name = "v_unsigned";
    cols[16].type = C_ORM_TYPE_INT64;
    cols[16].offset = offsetof(TestWireExhaust, v_i64);
    cols[17].name = "rg_y";
    cols[17].type = C_ORM_TYPE_CHAR_FIXED;
    cols[17].fixed_length = 4;
    cols[17].offset = offsetof(TestWireExhaust, s_cf4);
    cols[18].name = "idx_1";
    cols[18].type = C_ORM_TYPE_INT32;
    cols[18].offset = offsetof(TestWireExhaust, idx_1);
    cols[19].name = "idx_2";
    cols[19].type = C_ORM_TYPE_INT32;
    cols[19].offset = offsetof(TestWireExhaust, idx_2);
    cols[20].name = "scalar_occ";
    cols[20].type = C_ORM_TYPE_INT32;
    cols[20].offset = offsetof(TestWireExhaust, v_i32);
    cols[21].name = "odo_cnt";
    cols[21].type = C_ORM_TYPE_INT32;
    cols[21].offset = offsetof(TestWireExhaust, odo_cnt);
    cols[22].name = "arr_i32";
    cols[22].type = C_ORM_TYPE_ARRAY;
    cols[22].offset = offsetof(TestWireExhaust, arr_i32);
    cols[23].name = "arr_i64";
    cols[23].type = C_ORM_TYPE_ARRAY;
    cols[23].offset = offsetof(TestWireExhaust, arr_i64);
    cols[24].name = "arr_dec_z";
    cols[24].type = C_ORM_TYPE_ARRAY;
    cols[24].offset = offsetof(TestWireExhaust, arr_dec_z);
    cols[25].name = "arr_str";
    cols[25].type = C_ORM_TYPE_ARRAY;
    cols[25].offset = offsetof(TestWireExhaust, arr_str);

    memset(&meta, 0, sizeof(meta));
    meta.name = "W_REC";
    meta.columns = cols;
    meta.num_columns = 26;
    meta.struct_size = sizeof(TestWireExhaust);

    /* 2a. Pack with ALL nullable pointers == NULL and odo_cnt == 0 */
    memset(&src_s, 0, sizeof(src_s));
    ASSERT_EQ(C_ORM_OK,
              c_orm_copybook_pack_struct(rec, &meta, &src_s, C_ORM_CP_UTF8, 0,
                                         raw, sizeof(raw), &raw_len));
    memset(&dst_s, 0, sizeof(dst_s));
    ASSERT_EQ(C_ORM_OK, c_orm_copybook_unpack_struct(rec, &meta, raw, raw_len,
                                                     C_ORM_CP_UTF8, 0, &dst_s));

    /* 2b. Pack with ALL nullable pointers populated, odo_cnt == 2, native & BE
     */
    src_s.p_i32 = &v32;
    src_s.p_i64 = &v64;
    src_s.p_b = &vb1;
    src_s.b_val = 1;
    src_s.p_f32 = &vf;
    src_s.p_f64 = &vd;
    src_s.p_dec = &vdec;
    src_s.s_heap = "HELLO";
    src_s.s_cf0 = "G";
    memcpy(src_s.s_cf4, "REDF", 5);
    src_s.bl_bf0.data = b4_bytes;
    src_s.bl_bf0.size = 4;
    src_s.bl_heap.data = b4_bytes;
    src_s.bl_heap.size = 4;
    src_s.v_i32 = 15;
    src_s.v_i64 = 77;
    src_s.idx_1 = 11;
    src_s.idx_2 = 22;
    src_s.odo_cnt = 2;
    src_s.arr_i32.data = i32_elems;
    src_s.arr_i32.length = 2;
    src_s.arr_i64.data = i64_elems;
    src_s.arr_i64.length = 2;
    src_s.arr_dec_z.data = decz_elems;
    src_s.arr_dec_z.length = 2;
    src_s.arr_str.data = (void *)str_elems;
    src_s.arr_str.length = 2;

    for (step = 0; step <= 1; ++step) {
      ASSERT_EQ(C_ORM_OK,
                c_orm_copybook_pack_struct(rec, &meta, &src_s, C_ORM_CP_UTF8,
                                           step, raw, sizeof(raw), &raw_len));
      /* Unpack TWICE into dst_s so both hp==NULL and hp!=NULL (overwrite/free)
       * paths execute! */
      ASSERT_EQ(C_ORM_OK,
                c_orm_copybook_unpack_struct(rec, &meta, raw, raw_len,
                                             C_ORM_CP_UTF8, step, &dst_s));
      ASSERT_EQ(C_ORM_OK,
                c_orm_copybook_unpack_struct(rec, &meta, raw, raw_len,
                                             C_ORM_CP_UTF8, step, &dst_s));
      for (k = 0; k < dst_s.arr_str.length; ++k) {
        (void)c_orm_system_free(((char **)dst_s.arr_str.data)[k]);
        ((char **)dst_s.arr_str.data)[k] = NULL;
      }
    }

    /* Also exercise vb0 == 0 and b_val == 0 */
    src_s.p_b = &vb0;
    src_s.b_val = 0;
    ASSERT_EQ(C_ORM_OK,
              c_orm_copybook_pack_struct(rec, &meta, &src_s, C_ORM_CP_UTF8, 0,
                                         raw, sizeof(raw), &raw_len));
    ASSERT_EQ(C_ORM_OK, c_orm_copybook_unpack_struct(rec, &meta, raw, raw_len,
                                                     C_ORM_CP_UTF8, 0, &dst_s));
    for (k = 0; k < dst_s.arr_str.length; ++k) {
      (void)c_orm_system_free(((char **)dst_s.arr_str.data)[k]);
      ((char **)dst_s.arr_str.data)[k] = NULL;
    }

    /* Unsigned COMP negative overflow */
    src_s.v_i64 = -1;
    ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
              c_orm_copybook_pack_struct(rec, &meta, &src_s, C_ORM_CP_UTF8, 0,
                                         raw, sizeof(raw), &raw_len));
    src_s.v_i64 = 77;

    /* ODO out of range (< min or > max) */
    src_s.odo_cnt = -1;
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_copybook_pack_struct(rec, &meta, &src_s, C_ORM_CP_UTF8, 0,
                                         raw, sizeof(raw), &raw_len));
    src_s.odo_cnt = 99;
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_copybook_pack_struct(rec, &meta, &src_s, C_ORM_CP_UTF8, 0,
                                         raw, sizeof(raw), &raw_len));
    src_s.odo_cnt = 1;

    /* Safe CRT sweeps across pack_struct and unpack_struct */
    ASSERT_EQ(C_ORM_OK,
              c_orm_copybook_pack_struct(rec, &meta, &src_s, C_ORM_CP_UTF8, 0,
                                         raw, sizeof(raw), &raw_len));
    for (step = 0; step < 6; ++step) {
      c_orm_mock_safe_crt_countdown = step;
      (void)c_orm_copybook_pack_struct(rec, &meta, &src_s, C_ORM_CP_UTF8, 0,
                                       raw, sizeof(raw), &raw_len);
      c_orm_mock_safe_crt_countdown = -1;
    }
    for (step = 0; step < 6; ++step) {
      c_orm_mock_safe_crt_countdown = step;
      (void)c_orm_copybook_unpack_struct(rec, &meta, raw, raw_len,
                                         C_ORM_CP_UTF8, 0, &dst_s);
      c_orm_mock_safe_crt_countdown = -1;
    }

    /* Cleanup dst_s heap pointers */
    (void)c_orm_system_free(dst_s.p_i32);
    (void)c_orm_system_free(dst_s.p_i64);
    (void)c_orm_system_free(dst_s.p_b);
    (void)c_orm_system_free(dst_s.p_f32);
    (void)c_orm_system_free(dst_s.p_f64);
    (void)c_orm_system_free(dst_s.p_dec);
    (void)c_orm_system_free(dst_s.s_heap);
    (void)c_orm_system_free(dst_s.s_cf0);
    (void)c_orm_system_free(dst_s.bl_bf0.data);
    (void)c_orm_system_free(dst_s.bl_heap.data);
    (void)c_orm_system_free(dst_s.arr_i32.data);
    (void)c_orm_system_free(dst_s.arr_i64.data);
    (void)c_orm_system_free(dst_s.arr_dec_z.data);
    if (dst_s.arr_str.data) {
      for (k = 0; k < dst_s.arr_str.length; ++k) {
        (void)c_orm_system_free(((char **)dst_s.arr_str.data)[k]);
      }
      (void)c_orm_system_free(dst_s.arr_str.data);
    }
  }

  /* 3. Cross-type coercions, truncation/codepage errors, and >18-digit ODO */
  {
    static const char *const coerce_cpy =
        "EXEC SQL DECLARE TC TABLE (B4 CHAR(4) FOR BIT DATA) END-EXEC.\n"
        "01 C-REC.\n"
        "   05 C-P3 PIC S9(5)V99 COMP-3.\n"
        "   05 C-I4 PIC S9(4) COMP.\n"
        "   05 C-F1 COMP-1.\n"
        "   05 C-F2 COMP-2.\n"
        "   05 C-X6 PIC X(6).\n"
        "   05 C-G2 PIC G(2) USAGE DISPLAY-1.\n"
        "   05 B4 PIC X(4).\n"
        "   05 VG.\n"
        "      49 VG-L PIC S9(4) COMP.\n"
        "      49 VG-T PIC G(2) USAGE DISPLAY-1.\n"
        "   05 VC.\n"
        "      49 VC-L PIC S9(4) COMP.\n"
        "      49 VC-T PIC X(3).\n"
        "   05 BIG-ARR PIC 9(19) OCCURS 1 TIMES.\n";
    c_orm_cobol_record_t *crec = NULL;
    c_orm_column_meta_t ccols[10];
    c_orm_table_meta_t cmeta;
    TestWireExhaust cs;
    c_orm_decimal_t big_one;
    unsigned char craw[128];
    size_t clen = 0;

    ASSERT_EQ(C_ORM_OK,
              c_orm_copybook_parse_string(coerce_cpy, C_ORM_COBOL_FORMAT_FREE,
                                          NULL, 0, &crec));
    (void)c_orm_decimal_from_str("1234567890123456789", &big_one);

    /* Pack DOUBLE->"C-P3", STRING->"C-I4", INT->"C-F1", DECIMAL->"C-F2",
     * DECIMAL->"C-X6", STRING->"B4", >18-digit ODO->"BIG-ARR" */
    memset(ccols, 0, sizeof(ccols));
    memset(&cs, 0, sizeof(cs));
    cs.v_f64 = 12.5;
    cs.s_heap = "25";
    cs.v_i32 = 7;
    (void)c_orm_decimal_from_str("3.25", &cs.v_dec);
    memcpy(cs.s_cf4, "ABCD", 5);
    cs.arr_dec_big.data = &big_one;
    cs.arr_dec_big.length = 1;

    ccols[0].name = "c_p3";
    ccols[0].type = C_ORM_TYPE_DOUBLE;
    ccols[0].offset = offsetof(TestWireExhaust, v_f64);
    ccols[1].name = "c_i4";
    ccols[1].type = C_ORM_TYPE_STRING;
    ccols[1].offset = offsetof(TestWireExhaust, s_heap);
    ccols[2].name = "c_f1";
    ccols[2].type = C_ORM_TYPE_INT32;
    ccols[2].offset = offsetof(TestWireExhaust, v_i32);
    ccols[3].name = "c_f2";
    ccols[3].type = C_ORM_TYPE_DECIMAL;
    ccols[3].offset = offsetof(TestWireExhaust, v_dec);
    ccols[4].name = "c_x6";
    ccols[4].type = C_ORM_TYPE_DECIMAL;
    ccols[4].offset = offsetof(TestWireExhaust, v_dec);
    ccols[5].name = "b4";
    ccols[5].type = C_ORM_TYPE_STRING;
    ccols[5].offset = offsetof(TestWireExhaust, s_heap);
    ccols[6].name = "big_arr";
    ccols[6].type = C_ORM_TYPE_ARRAY;
    ccols[6].offset = offsetof(TestWireExhaust, arr_dec_big);

    memset(&cmeta, 0, sizeof(cmeta));
    cmeta.name = "C_REC";
    cmeta.columns = ccols;
    cmeta.num_columns = 7;
    cmeta.struct_size = sizeof(cs);

    ASSERT_EQ(C_ORM_OK,
              c_orm_copybook_pack_struct(crec, &cmeta, &cs, C_ORM_CP_UTF8, 1,
                                         craw, sizeof(craw), &clen));

    /* Also pack DECIMAL->COMP-1, INT->COMP-2, INT->C-X6 */
    ccols[2].type = C_ORM_TYPE_DECIMAL;
    ccols[2].offset = offsetof(TestWireExhaust, v_dec);
    ccols[3].type = C_ORM_TYPE_INT32;
    ccols[3].offset = offsetof(TestWireExhaust, v_i32);
    ccols[4].type = C_ORM_TYPE_INT32;
    ccols[4].offset = offsetof(TestWireExhaust, v_i32);
    ASSERT_EQ(C_ORM_OK,
              c_orm_copybook_pack_struct(crec, &cmeta, &cs, C_ORM_CP_UTF8, 1,
                                         craw, sizeof(craw), &clen));

    /* Safe CRT fail on DOUBLE->decimal & INT->C-X6 sprintf */
    c_orm_mock_safe_crt_countdown = 0;
    ASSERT_NEQ(C_ORM_OK,
               c_orm_copybook_pack_struct(crec, &cmeta, &cs, C_ORM_CP_UTF8, 1,
                                          craw, sizeof(craw), &clen));
    c_orm_mock_safe_crt_countdown = 1;
    ASSERT_NEQ(C_ORM_OK,
               c_orm_copybook_pack_struct(crec, &cmeta, &cs, C_ORM_CP_UTF8, 1,
                                          craw, sizeof(craw), &clen));
    c_orm_mock_safe_crt_countdown = -1;
    ASSERT_EQ(C_ORM_OK,
              c_orm_copybook_pack_struct(crec, &cmeta, &cs, C_ORM_CP_UTF8, 1,
                                         craw, sizeof(craw), &clen));

    /* Unpack cross-type coercions:
     * DECIMAL(C-P3) -> INT32 / INT64 / CHAR_FIXED(2) / STRING
     * INT(C-I4) -> FLOAT / DOUBLE / DECIMAL / CHAR_FIXED(4) / STRING / BOOL
     * STRING(C-X6) -> DECIMAL / BINARY_FIXED(4) / BLOB / BOOL('Y','1','T','N')
     */
    memset(&cs, 0, sizeof(cs));
    ccols[0].type = C_ORM_TYPE_INT32;
    ccols[0].offset = offsetof(TestWireExhaust, v_i32);
    ccols[1].type = C_ORM_TYPE_FLOAT;
    ccols[1].offset = offsetof(TestWireExhaust, v_f32);
    ccols[4].type = C_ORM_TYPE_DECIMAL;
    ccols[4].offset = offsetof(TestWireExhaust, v_dec);
    ccols[5].type = C_ORM_TYPE_BINARY_FIXED;
    ccols[5].fixed_length = 4;
    ccols[5].offset = offsetof(TestWireExhaust, bl_bf4);
    ASSERT_EQ(C_ORM_OK, c_orm_copybook_unpack_struct(crec, &cmeta, craw, clen,
                                                     C_ORM_CP_UTF8, 1, &cs));
    (void)c_orm_system_free(cs.arr_dec_big.data);
    cs.arr_dec_big.data = NULL;

    ccols[0].type = C_ORM_TYPE_INT64;
    ccols[0].offset = offsetof(TestWireExhaust, v_i64);
    ccols[1].type = C_ORM_TYPE_DOUBLE;
    ccols[1].offset = offsetof(TestWireExhaust, v_f64);
    ccols[4].type = C_ORM_TYPE_BINARY_FIXED;
    ccols[4].fixed_length = 2;
    ccols[4].offset = offsetof(TestWireExhaust, bl_bf4);
    ASSERT_EQ(C_ORM_OK, c_orm_copybook_unpack_struct(crec, &cmeta, craw, clen,
                                                     C_ORM_CP_UTF8, 1, &cs));
    (void)c_orm_system_free(cs.arr_dec_big.data);
    cs.arr_dec_big.data = NULL;

    ccols[0].type = C_ORM_TYPE_CHAR_FIXED;
    ccols[0].fixed_length = 2;
    ccols[0].offset = offsetof(TestWireExhaust, s_cf4);
    ccols[1].type = C_ORM_TYPE_DECIMAL;
    ccols[1].offset = offsetof(TestWireExhaust, v_dec);
    ccols[4].type = C_ORM_TYPE_BLOB;
    ccols[4].offset = offsetof(TestWireExhaust, bl_heap);
    ASSERT_EQ(C_ORM_OK, c_orm_copybook_unpack_struct(crec, &cmeta, craw, clen,
                                                     C_ORM_CP_UTF8, 1, &cs));
    (void)c_orm_system_free(cs.bl_heap.data);
    cs.bl_heap.data = NULL;
    (void)c_orm_system_free(cs.arr_dec_big.data);
    cs.arr_dec_big.data = NULL;

    ccols[0].type = C_ORM_TYPE_STRING;
    ccols[0].offset = offsetof(TestWireExhaust, s_heap);
    ccols[1].type = C_ORM_TYPE_CHAR_FIXED;
    ccols[1].fixed_length = 4;
    ccols[1].offset = offsetof(TestWireExhaust, s_cf4);
    ccols[4].type = C_ORM_TYPE_BOOL;
    ccols[4].offset = offsetof(TestWireExhaust, b_val);
    ASSERT_EQ(C_ORM_OK, c_orm_copybook_unpack_struct(crec, &cmeta, craw, clen,
                                                     C_ORM_CP_UTF8, 1, &cs));
    (void)c_orm_system_free(cs.s_heap);
    cs.s_heap = NULL;
    (void)c_orm_system_free(cs.arr_dec_big.data);
    cs.arr_dec_big.data = NULL;

    ccols[1].type = C_ORM_TYPE_STRING;
    ccols[1].offset = offsetof(TestWireExhaust, s_heap);
    ASSERT_EQ(C_ORM_OK, c_orm_copybook_unpack_struct(crec, &cmeta, craw, clen,
                                                     C_ORM_CP_UTF8, 1, &cs));
    c_orm_mock_safe_crt_countdown = 0;
    (void)c_orm_copybook_unpack_struct(crec, &cmeta, craw, clen, C_ORM_CP_UTF8,
                                       1, &cs);
    c_orm_mock_safe_crt_countdown = -1;
    (void)c_orm_system_free(cs.s_heap);
    cs.s_heap = NULL;
    (void)c_orm_system_free(cs.arr_dec_big.data);
    cs.arr_dec_big.data = NULL;

    ccols[1].type = C_ORM_TYPE_CHAR_FIXED;
    ccols[1].fixed_length = 4;
    ccols[1].offset = offsetof(TestWireExhaust, s_cf4);
    c_orm_mock_safe_crt_countdown = 0;
    (void)c_orm_copybook_unpack_struct(crec, &cmeta, craw, clen, C_ORM_CP_UTF8,
                                       1, &cs);
    c_orm_mock_safe_crt_countdown = -1;
    (void)c_orm_system_free(cs.arr_dec_big.data);
    cs.arr_dec_big.data = NULL;

    /* Truncation & invalid codepage / BCD / DCLGEN length errors */
    ccols[0].type = C_ORM_TYPE_DOUBLE;
    ccols[0].offset = offsetof(TestWireExhaust, v_f64);
    ccols[1].type = C_ORM_TYPE_INT32;
    ccols[1].offset = offsetof(TestWireExhaust, v_i32);
    ccols[4].type = C_ORM_TYPE_INT32;
    ccols[4].offset = offsetof(TestWireExhaust, v_i32);
    ccols[5].name = "b4";
    ccols[5].type = C_ORM_TYPE_STRING;
    ccols[5].offset = offsetof(TestWireExhaust, s_heap);
    cs.s_heap = "TOOLONG_FOR_BIT";
    ASSERT_EQ(C_ORM_ERROR_TRUNCATION,
              c_orm_copybook_pack_struct(crec, &cmeta, &cs, C_ORM_CP_UTF8, 1,
                                         craw, sizeof(craw), &clen));
    cs.bl_heap.data = (unsigned char *)"123456";
    cs.bl_heap.size = 6;
    ccols[5].type = C_ORM_TYPE_BLOB;
    ccols[5].offset = offsetof(TestWireExhaust, bl_heap);
    ASSERT_EQ(C_ORM_ERROR_TRUNCATION,
              c_orm_copybook_pack_struct(crec, &cmeta, &cs, C_ORM_CP_UTF8, 1,
                                         craw, sizeof(craw), &clen));

    ccols[5].name = "c_g2";
    ccols[5].type = C_ORM_TYPE_STRING;
    ccols[5].offset = offsetof(TestWireExhaust, s_heap);
    cs.s_heap = "TOO_LONG_GRAPHIC";
    ASSERT_EQ(C_ORM_ERROR_TRUNCATION,
              c_orm_copybook_pack_struct(crec, &cmeta, &cs, C_ORM_CP_UTF8, 1,
                                         craw, sizeof(craw), &clen));
    cs.s_heap = "\xFF\xFF";
    ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
              c_orm_copybook_pack_struct(crec, &cmeta, &cs, C_ORM_CP_UTF8, 1,
                                         craw, sizeof(craw), &clen));

    ccols[5].name = "vg";
    cs.s_heap = "TOO_LONG_VG";
    ASSERT_EQ(C_ORM_ERROR_TRUNCATION,
              c_orm_copybook_pack_struct(crec, &cmeta, &cs, C_ORM_CP_UTF8, 1,
                                         craw, sizeof(craw), &clen));
    cs.s_heap = "\xFF";
    ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
              c_orm_copybook_pack_struct(crec, &cmeta, &cs, C_ORM_CP_UTF8, 1,
                                         craw, sizeof(craw), &clen));

    ccols[5].name = "vc";
    cs.s_heap = "TOO_LONG_VC";
    ASSERT_EQ(C_ORM_ERROR_TRUNCATION,
              c_orm_copybook_pack_struct(crec, &cmeta, &cs, C_ORM_CP_IBM037, 1,
                                         craw, sizeof(craw), &clen));
    cs.s_heap = "\xFF";
    ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
              c_orm_copybook_pack_struct(crec, &cmeta, &cs, C_ORM_CP_IBM037, 1,
                                         craw, sizeof(craw), &clen));
    cs.s_heap = NULL;

    /* Corrupt DCLGEN VG length to negative and > max in craw */
    ASSERT_EQ(C_ORM_OK,
              c_orm_copybook_pack_struct(crec, &cmeta, &cs, C_ORM_CP_UTF8, 1,
                                         craw, sizeof(craw), &clen));
    /* Corrupt UTF-16 in C-G2 (offset 24: 4+2+4+8+6 = 24) to lone surrogate
     * 0xD800 */
    craw[24] = 0xD8;
    craw[25] = 0x00;
    ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
              c_orm_copybook_unpack_struct(crec, &cmeta, craw, clen,
                                           C_ORM_CP_UTF8, 1, &cs));
    craw[24] = 0x00;
    craw[25] = 0x20;
    /* Corrupt VG-L (offset 32: 24 + 4 + 4 = 32) to 99 */
    craw[32] = 0x00;
    craw[33] = 99;
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_copybook_unpack_struct(crec, &cmeta, craw, clen,
                                           C_ORM_CP_UTF8, 1, &cs));
    craw[32] = 0xFF;
    craw[33] = 0xFF;
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_copybook_unpack_struct(crec, &cmeta, craw, clen,
                                           C_ORM_CP_UTF8, 1, &cs));

    ASSERT_EQ(C_ORM_OK, c_orm_copybook_record_free(crec));
  }

  /* 4. Abstract struct pack & unpack + OOM sweeps */
  {
    cdd_c_abstract_struct_t abs_in;
    cdd_c_abstract_struct_t abs_out;
    cdd_c_variant_t v;
    unsigned char bdata[2] = {0xAA, 0xBB};

    ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_init(&abs_in));
    v.type = CDD_C_VARIANT_TYPE_INT;
    v.value.i_val = 1;
    ASSERT_EQ(C_ORM_OK, cdd_c_abstract_set(&abs_in, "odo_cnt", &v));
    v.type = CDD_C_VARIANT_TYPE_FLOAT;
    v.value.f_val = 1.5;
    ASSERT_EQ(C_ORM_OK, cdd_c_abstract_set(&abs_in, "p_f32", &v));
    v.type = CDD_C_VARIANT_TYPE_DECIMAL;
    (void)c_orm_decimal_from_str("9.99", &v.value.d_val);
    ASSERT_EQ(C_ORM_OK, cdd_c_abstract_set(&abs_in, "p_dec", &v));
    v.type = CDD_C_VARIANT_TYPE_STRING;
    v.value.s_val = "HI";
    ASSERT_EQ(C_ORM_OK, cdd_c_abstract_set(&abs_in, "s_heap", &v));
    ASSERT_EQ(C_ORM_OK, cdd_c_abstract_set(&abs_in, "rg_y", &v));
    ASSERT_EQ(C_ORM_OK, cdd_c_abstract_set(&abs_in, "arr_str", &v));
    v.value.s_val = NULL;
    ASSERT_EQ(C_ORM_OK, cdd_c_abstract_set(&abs_in, "s_cf0", &v));
    v.type = CDD_C_VARIANT_TYPE_BLOB;
    v.value.b_val.data = bdata;
    v.value.b_val.size = 2;
    ASSERT_EQ(C_ORM_OK, cdd_c_abstract_set(&abs_in, "f_bit", &v));
    v.value.b_val.data = NULL;
    v.value.b_val.size = 0;
    ASSERT_EQ(C_ORM_OK, cdd_c_abstract_set(&abs_in, "f_blob", &v));
    v.type = CDD_C_VARIANT_TYPE_NULL;
    ASSERT_EQ(C_ORM_OK, cdd_c_abstract_set(&abs_in, "p_i64", &v));
    v.type = CDD_C_VARIANT_TYPE_INT;
    v.value.i_val = 7;
    ASSERT_EQ(C_ORM_OK, cdd_c_abstract_set(&abs_in, "idx_1", &v));

    ASSERT_EQ(C_ORM_ERROR_OVERFLOW,
              c_orm_copybook_pack_abstract(rec, &abs_in, C_ORM_CP_UTF8, 1, raw,
                                           4, NULL));
    ASSERT_EQ(C_ORM_OK,
              c_orm_copybook_pack_abstract(rec, &abs_in, C_ORM_CP_UTF8, 1, raw,
                                           sizeof(raw), &raw_len));

    ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_init(&abs_out));
    ASSERT_EQ(C_ORM_OK, c_orm_copybook_unpack_abstract(
                            rec, raw, raw_len, C_ORM_CP_UTF8, 1, &abs_out));
    ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_free(&abs_out));

    /* OOM sweep on unpack_abstract */
    ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(cpy_oom_malloc, cpy_oom_realloc,
                                             cpy_oom_free));
    for (step = 0; step < 120; ++step) {
      memset(&abs_out, 0, sizeof(abs_out));
      g_cpy_oom_countdown = step;
      rc = c_orm_copybook_unpack_abstract(rec, raw, raw_len, C_ORM_CP_UTF8, 1,
                                          &abs_out);
      g_cpy_oom_countdown = -1;
      (void)cdd_c_abstract_struct_free(&abs_out);
      if (rc == C_ORM_OK) {
        break;
      }
      ASSERT_NEQ(C_ORM_OK, rc);
    }
    ASSERT_EQ(C_ORM_OK, c_orm_reset_allocators());
    ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_free(&abs_in));
  }

  /* 5. Level-88 condition validation & check_condition edge cases */
  {
    TestWireExhaust cond_s;
    c_orm_column_meta_t cond_cols[2];
    c_orm_table_meta_t cond_m;
    int matched = 0;

    memset(&cond_s, 0, sizeof(cond_s));
    memset(cond_cols, 0, sizeof(cond_cols));
    cond_cols[0].name = "c_str";
    cond_cols[0].type = C_ORM_TYPE_STRING;
    cond_cols[0].is_nullable = true;
    cond_cols[0].offset = offsetof(TestWireExhaust, s_heap);
    cond_cols[1].name = "c_num";
    cond_cols[1].type = C_ORM_TYPE_STRING;
    cond_cols[1].offset = offsetof(TestWireExhaust, s_cf0);

    memset(&cond_m, 0, sizeof(cond_m));
    cond_m.name = "W_REC";
    cond_m.columns = cond_cols;
    cond_m.num_columns = 2;
    cond_m.struct_size = sizeof(cond_s);

    /* Nullable empty C-STR + valid exact C-NUM "42" */
    cond_s.s_heap = NULL;
    cond_s.s_cf0 = "42";
    ASSERT_EQ(C_ORM_OK, c_orm_copybook_validate_88(rec, &cond_m, &cond_s));

    /* String range 'BA' THRU 'DZ' with trailing spaces & shorter/longer strings
     */
    cond_s.s_heap = "BC  ";
    cond_s.s_cf0 = "3";
    ASSERT_EQ(C_ORM_OK, c_orm_copybook_validate_88(rec, &cond_m, &cond_s));
    cond_s.s_heap = "B";
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_copybook_validate_88(rec, &cond_m, &cond_s));
    cond_s.s_heap = "DZZZ";
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_copybook_validate_88(rec, &cond_m, &cond_s));
    cond_s.s_heap = "OK";
    cond_s.s_cf0 = "NOT_A_NUM";
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_copybook_validate_88(rec, &cond_m, &cond_s));

    /* check_condition: c_name match, unmapped field error, not found */
    cond_s.s_cf0 = "42";
    ASSERT_EQ(C_ORM_OK, c_orm_copybook_check_condition(rec, &cond_m, &cond_s,
                                                       "c_num_ex", &matched));
    ASSERT_EQ(1, matched);
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_copybook_check_condition(rec, &cond_m, &cond_s, "c_unm_ok",
                                             &matched));
    ASSERT_EQ(C_ORM_ERROR_NOT_FOUND,
              c_orm_copybook_check_condition(rec, &cond_m, &cond_s, "NO_SUCH",
                                             &matched));
  }

  /* 6. Remaining edge cases + pack_struct/unpack_struct OOM sweeps */
  {
    static const char *const edge_w_lines[] = {
        "EXEC SQL DECLARE TE TABLE (FB CHAR(2) FOR BIT DATA) END-EXEC.\n",
        "01 E-REC.\n",
        "   05 FILLER PIC 9(2).\n",
        "   05 SP-FLD PIC X(3).\n",
        "      88 VOID VALUE '   '.\n",
        "   05 INT PIC X(4).\n",
        "      88 INT-OK VALUE 'AB  '.\n",
        "      88 NUM88 VALUE 5.\n",
        "   05 E-F1 COMP-1.\n",
        "   05 E-P3 PIC S9(3)V99 COMP-3.\n",
        "   05 E-C2 PIC S9(2)V99 COMP.\n",
        "   05 E-Z4 PIC 9(4).\n",
        "      88 BADLOW VALUE 'BAD' 10 THRU 20.\n",
        "   05 E-BIG20 PIC 9(20).\n",
        "   05 E-G2 PIC G(2) USAGE DISPLAY-1.\n",
        "   05 E-VG.\n",
        "      49 E-VGL PIC S9(4) COMP.\n",
        "      49 E-VGT PIC G(2) USAGE DISPLAY-1.\n",
        "   05 E-VC.\n",
        "      49 E-VCL PIC S9(4) COMP.\n",
        "      49 E-VCT PIC X(4).\n",
        "   05 FB PIC X(2).\n",
        "   05 E-BASE PIC X(2).\n",
        "   05 E-RED REDEFINES E-BASE PIC X(2).\n",
        "   05 E-CNT PIC 9(2) COMP.\n",
        "   05 E-ARR PIC X(2) OCCURS 1 TO 2 DEPENDING ON E-CNT.\n",
        "   05 E-RODO REDEFINES E-ARR PIC X(2)\n",
        "      OCCURS 1 TO 2 DEPENDING ON E-CNT.\n",
        "   05 E-GRP.\n",
        "      10 E-SUB PIC S9(2) COMP.\n"};
    char edge_w_cpy[2048];
    c_orm_cobol_record_t *erec = NULL;
    c_orm_column_meta_t ecols[14];
    c_orm_table_meta_t emeta;
    TestWireExhaust es;
    TestWireExhaust ed;
    cdd_c_abstract_struct_t empty_abs;
    unsigned char eraw[128];
    size_t elen = 0;
    const char *one_str[1] = {"A"};
    int mflag = 0;
    size_t k;

    ASSERT_EQ(C_ORM_OK,
              cpy_join_lines(edge_w_lines,
                             sizeof(edge_w_lines) / sizeof(edge_w_lines[0]),
                             edge_w_cpy, sizeof(edge_w_cpy)));
    ASSERT_EQ(C_ORM_OK,
              c_orm_copybook_parse_string(edge_w_cpy, C_ORM_COBOL_FORMAT_FREE,
                                          NULL, 0, &erec));

    /* Empty abstract struct (kvs == NULL) & out_record_len == NULL */
    memset(&empty_abs, 0, sizeof(empty_abs));
    {
      cdd_c_abstract_struct_t a2;
      cdd_c_variant_t vi;
      /* With kvs == NULL: odo_cnt defaults to 0 < min_occurs(1) -> VALIDATION!
       */
      ASSERT_EQ(C_ORM_ERROR_VALIDATION,
                c_orm_copybook_pack_abstract(erec, &empty_abs, C_ORM_CP_UTF8, 1,
                                             eraw, sizeof(eraw), NULL));
      ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_init(&a2));
      vi.type = CDD_C_VARIANT_TYPE_INT;
      vi.value.i_val = 1;
      ASSERT_EQ(C_ORM_OK, cdd_c_abstract_set(&a2, "E-CNT", &vi));
      vi.type = CDD_C_VARIANT_TYPE_STRING;
      vi.value.s_val = "AB";
      ASSERT_EQ(C_ORM_OK, cdd_c_abstract_set(&a2, "INT", &vi));
      ASSERT_EQ(C_ORM_OK,
                c_orm_copybook_pack_abstract(erec, &a2, C_ORM_CP_UTF8, 1, eraw,
                                             sizeof(eraw), NULL));
      /* Safe CRT failure on pack_abstract & unpack_abstract OCCURS (while eraw
       * has E-CNT=1!) */
      c_orm_mock_safe_crt_countdown = 0;
      ASSERT_NEQ(C_ORM_OK,
                 c_orm_copybook_pack_abstract(erec, &a2, C_ORM_CP_UTF8, 1, eraw,
                                              sizeof(eraw), NULL));
      c_orm_mock_safe_crt_countdown = -1;

      ASSERT_EQ(C_ORM_OK,
                c_orm_copybook_pack_abstract(erec, &a2, C_ORM_CP_UTF8, 1, eraw,
                                             sizeof(eraw), NULL));
      {
        cdd_c_abstract_struct_t a3;
        memset(&a3, 0, sizeof(a3));
        c_orm_mock_safe_crt_countdown = 0;
        ASSERT_NEQ(C_ORM_OK,
                   c_orm_copybook_unpack_abstract(erec, eraw, sizeof(eraw),
                                                  C_ORM_CP_UTF8, 1, &a3));
        c_orm_mock_safe_crt_countdown = -1;
        (void)cdd_c_abstract_struct_free(&a3);
      }

      /* Invalid string in E-C2 via pack_abstract -> L1125-1126 */
      vi.value.s_val = "BAD";
      ASSERT_EQ(C_ORM_OK, cdd_c_abstract_set(&a2, "e_c2", &vi));
      ASSERT_NEQ(C_ORM_OK,
                 c_orm_copybook_pack_abstract(erec, &a2, C_ORM_CP_UTF8, 1, eraw,
                                              sizeof(eraw), NULL));
      ASSERT_EQ(C_ORM_OK, cdd_c_abstract_struct_free(&a2));
    }

    /* Invalid string -> COMP/COMP-3/zoned/nested-group & rescale overflow */
    memset(ecols, 0, sizeof(ecols));
    memset(&es, 0, sizeof(es));
    es.odo_cnt = 2;
    es.arr_str.data = (void *)one_str;
    es.arr_str.length = 1; /* length(1) < occ_count(2) covers L1008[2]! */
    ecols[0].name = "e_cnt";
    ecols[0].type = C_ORM_TYPE_INT32;
    ecols[0].offset = offsetof(TestWireExhaust, odo_cnt);
    ecols[1].name = "e_arr";
    ecols[1].type = C_ORM_TYPE_ARRAY;
    ecols[1].offset = offsetof(TestWireExhaust, arr_str);
    ecols[2].name = "INT"; /* cobol_name match when c_name is int_v */
    ecols[2].type = C_ORM_TYPE_STRING;
    ecols[2].offset = offsetof(TestWireExhaust, s_heap);
    ecols[3].name = "e_base";
    ecols[3].type = (c_orm_type_t)99; /* unknown type 99 read & write! */
    ecols[3].offset = offsetof(TestWireExhaust, unk_val);
    ecols[4].name = "e_red"; /* both base & redef mapped -> L933[2]! */
    ecols[4].type = C_ORM_TYPE_CHAR_FIXED;
    ecols[4].fixed_length = 2;
    ecols[4].offset = offsetof(TestWireExhaust, s_cf4);

    memset(&emeta, 0, sizeof(emeta));
    emeta.name = "E_REC";
    emeta.columns = ecols;
    emeta.num_columns = 5;
    emeta.struct_size = sizeof(es);

    es.s_heap = "   "; /* all spaces for wire_cobol_str_cmp */
    ecols[2].name = "sp_fld";
    ASSERT_EQ(C_ORM_OK, c_orm_copybook_check_condition(erec, &emeta, &es,
                                                       "void_v", &mflag));
    ecols[2].name = "INT";
    ASSERT_EQ(C_ORM_OK, c_orm_copybook_check_condition(erec, &emeta, &es,
                                                       "INT-OK", &mflag));
    /* Invalid numeric string in ODO field e_cnt -> L853[1] */
    ecols[0].type = C_ORM_TYPE_STRING;
    ecols[0].offset = offsetof(TestWireExhaust, s_heap);
    es.s_heap = "BAD";
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_copybook_pack_struct(erec, &emeta, &es, C_ORM_CP_UTF8, 1,
                                         eraw, sizeof(eraw), &elen));
    ecols[0].type = C_ORM_TYPE_INT32;
    ecols[0].offset = offsetof(TestWireExhaust, odo_cnt);
    /* BLOB into non-FOR-BIT-DATA INT -> L675[2] */
    es.bl_heap.data = (unsigned char *)"XY";
    es.bl_heap.size = 2;
    ecols[2].type = C_ORM_TYPE_BLOB;
    ecols[2].offset = offsetof(TestWireExhaust, bl_heap);
    ASSERT_EQ(C_ORM_OK,
              c_orm_copybook_pack_struct(erec, &emeta, &es, C_ORM_CP_UTF8, 1,
                                         eraw, sizeof(eraw), &elen));
    ecols[2].type = C_ORM_TYPE_STRING;
    ecols[2].offset = offsetof(TestWireExhaust, s_heap);
    es.s_heap = "5";
    ASSERT_EQ(C_ORM_OK, c_orm_copybook_check_condition(erec, &emeta, &es,
                                                       "NUM88", &mflag));
    ASSERT_EQ(1, mflag);
    /* Non-nullable empty CPY_VAL_EMPTY on validate_88 -> L2054[2] */
    es.s_heap = NULL;
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_copybook_validate_88(erec, &emeta, &es));
    /* BADLOW ('BAD' skipped, 5 < 10 -> cmp_lo < 0 at L2001[1]) */
    ecols[2].name = "e_z4";
    es.s_heap = "5";
    ASSERT_EQ(C_ORM_ERROR_VALIDATION,
              c_orm_copybook_validate_88(erec, &emeta, &es));

    /* Pack failures on E-P3, E-C2 (syntax & rescale overflow), E-Z4, E-SUB */
    es.s_heap = "BAD";
    ecols[2].name = "e_p3";
    ASSERT_NEQ(C_ORM_OK,
               c_orm_copybook_pack_struct(erec, &emeta, &es, C_ORM_CP_UTF8, 1,
                                          eraw, sizeof(eraw), &elen));
    ecols[2].name = "e_c2";
    ASSERT_NEQ(C_ORM_OK,
               c_orm_copybook_pack_struct(erec, &emeta, &es, C_ORM_CP_UTF8, 1,
                                          eraw, sizeof(eraw), &elen));
    es.s_heap = "999999.99";
    ASSERT_NEQ(C_ORM_OK,
               c_orm_copybook_pack_struct(erec, &emeta, &es, C_ORM_CP_UTF8, 1,
                                          eraw, sizeof(eraw), &elen));
    es.s_heap = "BAD";
    ecols[2].name = "e_z4";
    ASSERT_NEQ(C_ORM_OK,
               c_orm_copybook_pack_struct(erec, &emeta, &es, C_ORM_CP_UTF8, 1,
                                          eraw, sizeof(eraw), &elen));
    ecols[2].name = "e_sub";
    ASSERT_NEQ(C_ORM_OK,
               c_orm_copybook_pack_struct(erec, &emeta, &es, C_ORM_CP_UTF8, 1,
                                          eraw, sizeof(eraw), &elen));

    /* Valid pack of E-BIG20 = 99999999999999999999 (> INT64_MAX), INT = "Y" */
    ecols[2].name = "e_big20";
    es.s_heap = "99999999999999999999";
    ecols[5].name = "INT";
    ecols[5].type = C_ORM_TYPE_STRING;
    ecols[5].offset = offsetof(TestWireExhaust, s_cf0);
    es.s_cf0 = "Y";
    ecols[6].name = "e_g2";
    ecols[6].type = C_ORM_TYPE_STRING;
    ecols[6].offset = offsetof(TestWireExhaust, s_cf0);
    ecols[7].name = "e_vg";
    ecols[7].type = C_ORM_TYPE_STRING;
    ecols[7].offset = offsetof(TestWireExhaust, s_cf0);
    ecols[8].name = "e_vc";
    ecols[8].type = C_ORM_TYPE_STRING;
    ecols[8].offset = offsetof(TestWireExhaust, s_cf0);
    ecols[9].name = "fb";
    ecols[9].type = C_ORM_TYPE_STRING;
    ecols[9].offset = offsetof(TestWireExhaust, s_cf0);
    emeta.num_columns = 10;
    ASSERT_EQ(C_ORM_OK,
              c_orm_copybook_pack_struct(erec, &emeta, &es, C_ORM_CP_UTF8, 1,
                                         eraw, sizeof(eraw), &elen));

    /* OOM sweep on pack_struct (hits DISPLAY_1 & VARGRAPHIC u16 mallocs!) */
    ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(cpy_oom_malloc, cpy_oom_realloc,
                                             cpy_oom_free));
    for (step = 0; step < 20; ++step) {
      g_cpy_oom_countdown = step;
      rc = c_orm_copybook_pack_struct(erec, &emeta, &es, C_ORM_CP_UTF8, 1, eraw,
                                      sizeof(eraw), &elen);
      g_cpy_oom_countdown = -1;
      if (rc == C_ORM_OK) {
        break;
      }
      ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
    }
    ASSERT_EQ(C_ORM_OK, c_orm_reset_allocators());

    /* Unpack 20-digit decimal into INT32 & INT64 -> DECIMAL_OVERFLOW */
    memset(&ed, 0, sizeof(ed));
    ecols[2].type = C_ORM_TYPE_INT32;
    ecols[2].offset = offsetof(TestWireExhaust, v_i32);
    ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
              c_orm_copybook_unpack_struct(erec, &emeta, eraw, elen,
                                           C_ORM_CP_UTF8, 1, &ed));
    ecols[2].type = C_ORM_TYPE_INT64;
    ecols[2].offset = offsetof(TestWireExhaust, v_i64);
    ASSERT_EQ(C_ORM_ERROR_DECIMAL_OVERFLOW,
              c_orm_copybook_unpack_struct(erec, &emeta, eraw, elen,
                                           C_ORM_CP_UTF8, 1, &ed));

    /* Unpack non-numeric INT="Y" into DECIMAL -> DECIMAL_SYNTAX */
    ecols[2].type = C_ORM_TYPE_DECIMAL;
    ecols[2].offset = offsetof(TestWireExhaust, v_dec);
    ecols[5].type = C_ORM_TYPE_DECIMAL;
    ecols[5].offset = offsetof(TestWireExhaust, v_dec);
    ASSERT_EQ(C_ORM_ERROR_DECIMAL_SYNTAX,
              c_orm_copybook_unpack_struct(erec, &emeta, eraw, elen,
                                           C_ORM_CP_UTF8, 1, &ed));

    /* Unpack BOOL variants ('Y', COMP-3 decimal) & short BINARY_FIXED(4) */
    ecols[5].type = C_ORM_TYPE_BOOL;
    ecols[5].offset = offsetof(TestWireExhaust, b_val);
    ecols[9].type = C_ORM_TYPE_BINARY_FIXED;
    ecols[9].fixed_length = 4; /* 2 < 4! */
    ecols[9].offset = offsetof(TestWireExhaust, bl_bf4);
    ecols[10].name = "e_p3";
    ecols[10].type = C_ORM_TYPE_BOOL;
    ecols[10].offset = offsetof(TestWireExhaust, b_val);
    ecols[11].name = "e_red";
    ecols[11].type = C_ORM_TYPE_BINARY_FIXED;
    ecols[11].fixed_length = 4; /* 2 < 4 on STRING! */
    ecols[11].offset = offsetof(TestWireExhaust, bl_bf4);
    emeta.num_columns = 12;
    ASSERT_EQ(C_ORM_OK, c_orm_copybook_unpack_struct(erec, &emeta, eraw, elen,
                                                     C_ORM_CP_UTF8, 1, &ed));
    for (k = 0; k < ed.arr_str.length; ++k) {
      (void)c_orm_system_free(((char **)ed.arr_str.data)[k]);
    }
    (void)c_orm_system_free(ed.arr_str.data);
    (void)c_orm_system_free(ed.s_cf0);

    /* Unpack COMP-1 (FLOAT variant) into INT64, CHAR_FIXED, STRING,
     * BINARY_FIXED, BLOB, plus STRING(4)->BINARY_FIXED(1) */
    {
      c_orm_column_meta_t fcols[3];
      c_orm_table_meta_t fmeta;
      static const c_orm_type_t ftypes[5] = {
          C_ORM_TYPE_INT64, C_ORM_TYPE_CHAR_FIXED, C_ORM_TYPE_STRING,
          C_ORM_TYPE_BINARY_FIXED, C_ORM_TYPE_BLOB};
      size_t fi;
      memset(fcols, 0, sizeof(fcols));
      memset(&fmeta, 0, sizeof(fmeta));
      fmeta.name = "E_REC";
      fmeta.columns = fcols;
      fmeta.num_columns = 3;
      fmeta.struct_size = sizeof(ed);
      fcols[1].name = "INT";
      fcols[1].type = C_ORM_TYPE_BINARY_FIXED;
      fcols[1].fixed_length = 1; /* sl(1) >= fixed_length(1) -> L1642[1]! */
      fcols[1].offset = offsetof(TestWireExhaust, bl_bf4);
      fcols[2].name = "e_cnt";
      fcols[2].type = C_ORM_TYPE_INT32;
      fcols[2].offset = offsetof(TestWireExhaust, odo_cnt);
      for (fi = 0; fi < 5; ++fi) {
        memset(&ed, 0, sizeof(ed));
        fcols[0].name = "e_f1";
        fcols[0].type = ftypes[fi];
        fcols[0].fixed_length = (ftypes[fi] == C_ORM_TYPE_CHAR_FIXED ||
                                 ftypes[fi] == C_ORM_TYPE_BINARY_FIXED)
                                    ? 2
                                    : 0;
        fcols[0].offset =
            (ftypes[fi] == C_ORM_TYPE_INT64)
                ? offsetof(TestWireExhaust, v_i64)
                : ((ftypes[fi] == C_ORM_TYPE_CHAR_FIXED)
                       ? offsetof(TestWireExhaust, s_cf4)
                       : ((ftypes[fi] == C_ORM_TYPE_STRING)
                              ? offsetof(TestWireExhaust, s_heap)
                              : ((ftypes[fi] == C_ORM_TYPE_BLOB)
                                     ? offsetof(TestWireExhaust, bl_heap)
                                     : offsetof(TestWireExhaust, bl_bf4))));
        ASSERT_EQ(C_ORM_OK,
                  c_orm_copybook_unpack_struct(erec, &fmeta, eraw, elen,
                                               C_ORM_CP_UTF8, 1, &ed));
        (void)c_orm_system_free(ed.s_heap);
        (void)c_orm_system_free(ed.bl_heap.data);
      }
    }

    /* Corrupt zoned E-Z4 (offset 2+3+4+4+3+2 = 18) and E-VGT (18+4+20+4+2 = 48)
     */
    eraw[18] = 0xFF;
    memset(&ed, 0, sizeof(ed));
    ASSERT_NEQ(C_ORM_OK, c_orm_copybook_unpack_struct(erec, &emeta, eraw, elen,
                                                      C_ORM_CP_UTF8, 1, &ed));
    eraw[18] = '0';

    eraw[48] = 0xD8;
    eraw[49] = 0x00;
    memset(&ed, 0, sizeof(ed));
    ASSERT_EQ(C_ORM_ERROR_CODEPAGE,
              c_orm_copybook_unpack_struct(erec, &emeta, eraw, elen,
                                           C_ORM_CP_UTF8, 1, &ed));
    eraw[48] = 0x00;
    eraw[49] = 'Y';

    /* OOM sweep across full nullable + heap + ODO unpack_struct */
    {
      c_orm_column_meta_t ocols[12];
      c_orm_table_meta_t ometa;
      memset(ocols, 0, sizeof(ocols));
      ocols[0].name = "e_cnt";
      ocols[0].type = C_ORM_TYPE_INT32;
      ocols[0].is_nullable = true;
      ocols[0].offset = offsetof(TestWireExhaust, p_i32);
      ocols[1].name = "e_c2";
      ocols[1].type = C_ORM_TYPE_DECIMAL;
      ocols[1].is_nullable = true;
      ocols[1].offset = offsetof(TestWireExhaust, p_dec);
      ocols[2].name = "e_z4";
      ocols[2].type = C_ORM_TYPE_INT64;
      ocols[2].is_nullable = true;
      ocols[2].offset = offsetof(TestWireExhaust, p_i64);
      ocols[3].name = "INT";
      ocols[3].type = C_ORM_TYPE_BOOL;
      ocols[3].is_nullable = true;
      ocols[3].offset = offsetof(TestWireExhaust, p_b);
      ocols[4].name = "e_p3";
      ocols[4].type = C_ORM_TYPE_FLOAT;
      ocols[4].is_nullable = true;
      ocols[4].offset = offsetof(TestWireExhaust, p_f32);
      ocols[5].name = "e_sub";
      ocols[5].type = C_ORM_TYPE_DOUBLE;
      ocols[5].is_nullable = true;
      ocols[5].offset = offsetof(TestWireExhaust, p_f64);
      ocols[6].name = "e_g2";
      ocols[6].type = C_ORM_TYPE_STRING;
      ocols[6].offset = offsetof(TestWireExhaust, s_heap);
      ocols[7].name = "e_vg";
      ocols[7].type = C_ORM_TYPE_STRING;
      ocols[7].offset = offsetof(TestWireExhaust, s_cf0);
      ocols[8].name = "e_vc";
      ocols[8].type = C_ORM_TYPE_BLOB;
      ocols[8].offset = offsetof(TestWireExhaust, bl_bf0);
      ocols[9].name = "fb";
      ocols[9].type = C_ORM_TYPE_BLOB;
      ocols[9].offset = offsetof(TestWireExhaust, bl_heap);
      ocols[10].name = "e_arr";
      ocols[10].type = C_ORM_TYPE_ARRAY;
      ocols[10].offset = offsetof(TestWireExhaust, arr_str);

      memset(&ometa, 0, sizeof(ometa));
      ometa.name = "E_REC";
      ometa.columns = ocols;
      ometa.num_columns = 11;
      ometa.struct_size = sizeof(ed);

      ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(cpy_oom_malloc, cpy_oom_realloc,
                                               cpy_oom_free));
      for (step = 0; step < 50; ++step) {
        memset(&ed, 0, sizeof(ed));
        g_cpy_oom_countdown = step;
        rc = c_orm_copybook_unpack_struct(erec, &ometa, eraw, elen,
                                          C_ORM_CP_UTF8, 1, &ed);
        g_cpy_oom_countdown = -1;
        (void)c_orm_system_free(ed.p_i32);
        (void)c_orm_system_free(ed.p_i64);
        (void)c_orm_system_free(ed.p_b);
        (void)c_orm_system_free(ed.p_f32);
        (void)c_orm_system_free(ed.p_f64);
        (void)c_orm_system_free(ed.p_dec);
        (void)c_orm_system_free(ed.s_heap);
        (void)c_orm_system_free(ed.s_cf0);
        (void)c_orm_system_free(ed.bl_bf0.data);
        (void)c_orm_system_free(ed.bl_heap.data);
        if (ed.arr_str.data) {
          for (k = 0; k < ed.arr_str.length; ++k) {
            (void)c_orm_system_free(((char **)ed.arr_str.data)[k]);
          }
          (void)c_orm_system_free(ed.arr_str.data);
        }
        if (rc == C_ORM_OK) {
          break;
        }
        ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
      }
      ASSERT_EQ(C_ORM_OK, c_orm_reset_allocators());
    }

    /* Synthetic unresolved ODO name & nested ODO error percolation */
    {
      c_orm_cobol_field_t bad_odo_f;
      c_orm_cobol_field_t bad_grp_f;
      c_orm_cobol_record_t bad_rec;
      memset(&bad_odo_f, 0, sizeof(bad_odo_f));
      bad_odo_f.level = 5;
      bad_odo_f.cobol_name = "X";
      bad_odo_f.c_name = "x";
      bad_odo_f.sql_name = "X";
      bad_odo_f.has_occurs = 1;
      bad_odo_f.min_occurs = 1;
      bad_odo_f.max_occurs = 2;
      bad_odo_f.odo_field_name = "MISSING_ODO";
      bad_odo_f.element_byte_size = 1;
      bad_odo_f.total_byte_size = 2;

      memset(&bad_rec, 0, sizeof(bad_rec));
      bad_rec.fields = &bad_odo_f;
      bad_rec.num_fields = 1;
      bad_rec.total_byte_size = 2;
      ASSERT_EQ(C_ORM_ERROR_VALIDATION,
                c_orm_copybook_pack_struct(&bad_rec, &emeta, &es, C_ORM_CP_UTF8,
                                           1, eraw, sizeof(eraw), &elen));
      ASSERT_EQ(C_ORM_ERROR_VALIDATION,
                c_orm_copybook_unpack_struct(&bad_rec, &emeta, eraw, 2,
                                             C_ORM_CP_UTF8, 1, &ed));

      /* Nested group containing bad ODO hits L940 & L1739 inside child group */
      memset(&bad_grp_f, 0, sizeof(bad_grp_f));
      bad_grp_f.level = 5;
      bad_grp_f.cobol_name = "G";
      bad_grp_f.c_name = "g";
      bad_grp_f.sql_name = "G";
      bad_grp_f.children = &bad_odo_f;
      bad_grp_f.num_children = 1;
      bad_grp_f.element_byte_size = 2;
      bad_grp_f.total_byte_size = 2;
      bad_rec.fields = &bad_grp_f;
      ASSERT_EQ(C_ORM_ERROR_VALIDATION,
                c_orm_copybook_pack_struct(&bad_rec, &emeta, &es, C_ORM_CP_UTF8,
                                           1, eraw, sizeof(eraw), &elen));
      ASSERT_EQ(C_ORM_ERROR_VALIDATION,
                c_orm_copybook_unpack_struct(&bad_rec, &emeta, eraw, 2,
                                             C_ORM_CP_UTF8, 1, &ed));
    }

    ASSERT_EQ(C_ORM_OK, c_orm_copybook_record_free(erec));
  }

  ASSERT_EQ_FMT(C_ORM_OK, c_orm_copybook_record_free(rec), "%d");
  PASS();
}

/**
 * @brief Exhaustively exercises `c_orm_copybook_codegen.c` across C89 header/
 * source emission, all 5 SQL DDL dialects, `meta_to_copybook_emit` &
 * `sql_to_copybook_emit` (fixed/free, DCLGEN/plain), Safe CRT & OOM sweeps,
 * and subprocess `c-orm-cli` invocations.
 */
TEST test_copybook_codegen_and_cli_exhaustive(void) {
  static const char *const cg_lines[] = {
      "EXEC SQL DECLARE PROD.CG_TBL TABLE (\n",
      "  F_BIT CHAR(4) FOR BIT DATA,\n",
      "  F_PK2 INT NOT NULL PRIMARY KEY,\n",
      "  F_VC_NULL VARCHAR(300)\n",
      ") END-EXEC.\n",
      "01 CG-REC.\n",
      "   05 FILLER PIC X(2).\n",
      "      88 IGNORED-88 VALUE 'XX'.\n",
      "   05 F-I4 PIC S9(4) COMP.\n",
      "   05 F-PK2 PIC S9(9) COMP.\n",
      "   05 F-I18 PIC S9(18) COMP-5.\n",
      "   05 F-BIG20 PIC 9(20).\n",
      "   05 F-DEC PIC S9(5)V99 COMP-3.\n",
      "   05 F-F1 COMP-1.\n",
      "   05 F-F2 COMP-2.\n",
      "   05 F-BIT PIC X(4).\n",
      "   05 F-BIT-OCC PIC X(2) OCCURS 2 TIMES.\n",
      "   05 F-I-OCC PIC S9(4) COMP OCCURS 2 TIMES.\n",
      "   05 F-GRP-OCC OCCURS 2 TIMES.\n",
      "      10 F-GO1 PIC X(2).\n",
      "   05 F-G10 PIC G(10) USAGE DISPLAY-1.\n",
      "   05 F-G300 PIC G(300) USAGE DISPLAY-1.\n",
      "   05 F-X300 PIC X(300).\n",
      "   05 F-SLS PIC S9(3) SIGN LEADING SEPARATE.\n",
      "   05 F-STS PIC S9(3) SIGN TRAILING SEPARATE.\n",
      "   05 F-SLN PIC S9(3) SIGN LEADING.\n",
      "   05 F-VC-NULL.\n",
      "      49 F-VC-NULL-L PIC S9(4) COMP.\n",
      "      49 F-VC-NULL-T PIC X(300).\n",
      "   05 F-VC-SML.\n",
      "      49 F-VC-SML-L PIC S9(4) COMP.\n",
      "      49 F-VC-SML-T PIC X(20).\n",
      "   05 F-VG-BIG.\n",
      "      49 F-VG-BIG-L PIC S9(4) COMP.\n",
      "      49 F-VG-BIG-T PIC G(300) USAGE DISPLAY-1.\n",
      "   05 F-VG-SML.\n",
      "      49 F-VG-SML-L PIC S9(4) COMP.\n",
      "      49 F-VG-SML-T PIC G(12) USAGE DISPLAY-1.\n",
      "   05 F-NORM-GRP.\n",
      "      10 NG-C1 PIC X(6).\n",
      "         88 NG-STR-COND VALUE 'A''B', 'CA' THRU 'CZ'.\n",
      "      10 NG-C2 PIC S9(4).\n",
      "         88 NG-NUM-COND VALUE 0, 10 THRU 20.\n",
      "   05 F-BASE-G.\n",
      "      10 FILLER PIC X(1).\n",
      "      10 BG-SUBG.\n",
      "         15 BG-LEAF PIC X(1).\n",
      "      10 BG-A PIC X(2).\n",
      "      10 BG-B REDEFINES BG-A PIC 9(2).\n",
      "      10 BG-VC.\n",
      "         49 BG-VCL PIC S9(4) COMP.\n",
      "         49 BG-VCT PIC X(4).\n",
      "   05 F-RED-G REDEFINES F-BASE-G.\n",
      "      10 RG-1 PIC X(10).\n",
      "   05 F-RED-EL REDEFINES F-BASE-G PIC X(10).\n",
      "   05 F-RED-VC REDEFINES F-BASE-G.\n",
      "      49 F-RED-VCL PIC S9(4) COMP.\n",
      "      49 F-RED-VCT PIC X(8).\n",
      "   66 RN-CG RENAMES F-I4.\n",
      "   05 C17 PIC X(1).\n",
      "   05 C18 PIC X(1).\n",
      "   05 C19 PIC X(1).\n",
      "   05 C20 PIC X(1).\n",
      "   05 ODO-N PIC S9(4) COMP.\n",
      "   05 ODO-EL PIC X(2) OCCURS 1 TO 3 DEPENDING ON ODO-N.\n"};
  char cg_cpy[4096];
  c_orm_cobol_record_t *rec = NULL;
  FILE *fp = NULL;
  char *ddl = NULL;
  char *cpy_out = NULL;
  int step;
  c_orm_error_t rc;

  ASSERT_EQ(C_ORM_OK,
            cpy_join_lines(cg_lines, sizeof(cg_lines) / sizeof(cg_lines[0]),
                           cg_cpy, sizeof(cg_cpy)));
  ASSERT_EQ(C_ORM_OK, c_orm_copybook_parse_string(
                          cg_cpy, C_ORM_COBOL_FORMAT_FREE, NULL, 0, &rec));
  /* Mark F_BIT_OCC as FOR BIT DATA to cover inline_fixed_len unsigned char[][]
   */
  {
    size_t fi;
    for (fi = 0; fi < rec->num_fields; ++fi) {
      if (strcmp(rec->fields[fi].c_name, "f_bit_occ") == 0) {
        rec->fields[fi].sql_for_bit_data = 1;
      }
    }
  }

  /* 1. NULL / bad path guards across all codegen functions */
  {
    c_orm_cobol_record_t no_sn = *rec;
    c_orm_table_meta_t bad_m;
    no_sn.c_struct_name = NULL;
    memset(&bad_m, 0, sizeof(bad_m));

    ASSERT_EQ(C_ORM_OK, C_ORM_FOPEN(&fp, "test_cg_null.tmp", "w"));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_emit_c_header_body(NULL, rec));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_emit_c_header_body(fp, NULL));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_emit_c_header_body(fp, &no_sn));

    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_emit_c_header(NULL, rec));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER, c_orm_copybook_emit_c_header(fp, NULL));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_emit_c_header(fp, &no_sn));

    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_emit_c_source(NULL, rec, "Models.h"));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_emit_c_source(fp, NULL, "Models.h"));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_emit_c_source(fp, &no_sn, "Models.h"));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_emit_c_source(fp, rec, NULL));
    fclose(fp);
    remove("test_cg_null.tmp");

    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_generate_c_files(NULL, "test_cg_dir"));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_generate_c_files(&no_sn, "test_cg_dir"));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_generate_c_files(rec, NULL));
    ASSERT_EQ(C_ORM_ERROR_IO,
              c_orm_copybook_generate_c_files(rec, "/dev/null/impossible_dir"));

    /* Models.h opens OK, Models.c is a directory -> C_ORM_ERROR_IO on Models.c!
     */
    (void)CPY_MKDIR("test_cg_dir2");
    (void)CPY_MKDIR("test_cg_dir2/Models.c");
    ASSERT_EQ(C_ORM_ERROR_IO,
              c_orm_copybook_generate_c_files(rec, "test_cg_dir2"));
    remove("test_cg_dir2/Models.h");
    (void)CPY_RMDIR("test_cg_dir2/Models.c");
    (void)CPY_RMDIR("test_cg_dir2");

    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_emit_sql_ddl(NULL, C_TO_SQL_DIALECT_DB2, &ddl));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_copybook_emit_sql_ddl(rec, C_TO_SQL_DIALECT_DB2, NULL));

    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_meta_to_copybook_emit(NULL, C_ORM_COBOL_FORMAT_FREE, 0,
                                          &cpy_out));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_meta_to_copybook_emit(&bad_m, C_ORM_COBOL_FORMAT_FREE, 0,
                                          &cpy_out));
    bad_m.name = "t";
    ASSERT_EQ(
        C_ORM_ERROR_NULL_POINTER,
        c_orm_meta_to_copybook_emit(&bad_m, C_ORM_COBOL_FORMAT_FREE, 0, NULL));

    ASSERT_EQ(
        C_ORM_ERROR_NULL_POINTER,
        c_orm_sql_to_copybook_emit(NULL, C_ORM_COBOL_FORMAT_FREE, 0, &cpy_out));
    ASSERT_EQ(C_ORM_ERROR_NULL_POINTER,
              c_orm_sql_to_copybook_emit("CREATE TABLE T (ID INT);",
                                         C_ORM_COBOL_FORMAT_FREE, 0, NULL));
    ASSERT_EQ(C_ORM_ERROR_SQL,
              c_orm_sql_to_copybook_emit("-- comment only\n",
                                         C_ORM_COBOL_FORMAT_FREE, 0, &cpy_out));
    ASSERT_NEQ(C_ORM_OK, c_orm_sql_to_copybook_emit("CREATE TABLE @@@;",
                                                    C_ORM_COBOL_FORMAT_FREE, 0,
                                                    &cpy_out));
  }

  /* 2. Full C89 codegen + 5-dialect SQL DDL + edge records (77-only, ODO-first,
   * composite-PK) */
  (void)CPY_MKDIR("test_cg_out");
  ASSERT_EQ(C_ORM_OK, c_orm_copybook_generate_c_files(rec, "test_cg_out"));
  remove("test_cg_out/Models.h");
  remove("test_cg_out/Models.c");

  {
    static const c_to_sql_dialect_t dials[5] = {
        C_TO_SQL_DIALECT_DB2, C_TO_SQL_DIALECT_ODBC, C_TO_SQL_DIALECT_SQLITE,
        C_TO_SQL_DIALECT_POSTGRESQL, C_TO_SQL_DIALECT_MYSQL};
    size_t di;
    for (di = 0; di < 5; ++di) {
      ddl = NULL;
      ASSERT_EQ(C_ORM_OK, c_orm_copybook_emit_sql_ddl(rec, dials[di], &ddl));
      (void)c_orm_system_free(ddl);
    }
  }

  /* Edge records: 77-only (num_fields==0 & cols.count==0 & emitted==0),
   * ODO-first (cols.items[0].is_odo_array), composite PK (pk_count==2) */
  {
    c_orm_cobol_record_t *r77 = NULL;
    c_orm_cobol_record_t *rodo = NULL;
    c_orm_cobol_record_t *rpk2 = NULL;

    ASSERT_EQ(C_ORM_OK, c_orm_copybook_parse_string("77 S77 PIC 9(4) COMP.",
                                                    C_ORM_COBOL_FORMAT_FREE,
                                                    NULL, 0, &r77));
    ASSERT_EQ(C_ORM_OK, c_orm_copybook_generate_c_files(r77, "test_cg_out"));
    ddl = NULL;
    ASSERT_EQ(C_ORM_OK,
              c_orm_copybook_emit_sql_ddl(r77, C_TO_SQL_DIALECT_DB2, &ddl));
    (void)c_orm_system_free(ddl);
    ASSERT_EQ(C_ORM_OK, c_orm_copybook_record_free(r77));

    ASSERT_EQ(C_ORM_OK,
              c_orm_copybook_parse_string(
                  "01 ODO-FIRST.\n"
                  "   05 ARR PIC X(2) OCCURS 1 TO 2 DEPENDING ON CNT.\n"
                  "   05 CNT PIC 9(2) COMP.\n",
                  C_ORM_COBOL_FORMAT_FREE, NULL, 0, &rodo));
    ASSERT_EQ(C_ORM_OK, c_orm_copybook_generate_c_files(rodo, "test_cg_out"));
    ASSERT_EQ(C_ORM_OK, c_orm_copybook_record_free(rodo));

    ASSERT_EQ(C_ORM_OK, c_orm_copybook_parse_string(
                            "EXEC SQL DECLARE T2 TABLE (\n"
                            "  K1 INT NOT NULL,\n"
                            "  K2 INT NOT NULL,\n"
                            "  PRIMARY KEY (K1, K2)\n"
                            ") END-EXEC.\n"
                            "01 COMP-PK.\n"
                            "   05 K1 PIC S9(9) COMP.\n"
                            "   05 K2 PIC S9(9) COMP.\n",
                            C_ORM_COBOL_FORMAT_FREE, NULL, 0, &rpk2));
    ASSERT_EQ(C_ORM_OK, c_orm_copybook_generate_c_files(rpk2, "test_cg_out"));
    ASSERT_EQ(C_ORM_OK, c_orm_copybook_record_free(rpk2));
    remove("test_cg_out/Models.h");
    remove("test_cg_out/Models.c");
  }

  /* 3. meta_to_copybook_emit & sql_to_copybook_emit across ALL types, formats,
   * precision/length defaults, and dclgen=0/1 */
  {
    c_orm_column_meta_t mcols[17];
    c_orm_table_meta_t m;
    static const char *const rich_sql = "CREATE TABLE T_ONE (\n"
                                        "  C_SM SMALLINT PRIMARY KEY,\n"
                                        "  C_IN INT NOT NULL,\n"
                                        "  C_BI BIGINT DEFAULT 0,\n"
                                        "  C_FL FLOAT,\n"
                                        "  C_DB DOUBLE,\n"
                                        "  C_D0 DECIMAL,\n"
                                        "  C_DS DECIMAL(4, 4),\n"
                                        "  C_DP DECIMAL(8, 2),\n"
                                        "  C_CH CHAR,\n"
                                        "  C_CB CHAR(4) FOR BIT DATA,\n"
                                        "  C_BN BINARY,\n"
                                        "  C_B8 BINARY(8),\n"
                                        "  C_GR GRAPHIC,\n"
                                        "  C_G4 GRAPHIC(4)\n"
                                        ");\n"
                                        "CREATE TABLE T_TWO (\n"
                                        "  C_VG0 VARGRAPHIC,\n"
                                        "  C_VG1 VARGRAPHIC(20),\n"
                                        "  C_VG2 VARGRAPHIC(400),\n"
                                        "  C_DT DATE,\n"
                                        "  C_TM TIME,\n"
                                        "  C_TS TIMESTAMP,\n"
                                        "  C_VC0 TEXT,\n"
                                        "  C_VC1 VARCHAR(30),\n"
                                        "  C_VC2 VARCHAR(500)\n"
                                        ");\n";

    memset(mcols, 0, sizeof(mcols));
    mcols[0].name = "c-i32~";
    mcols[0].type = C_ORM_TYPE_INT32;
    mcols[0].is_pk = true;
    mcols[1].name = "c_i64";
    mcols[1].type = C_ORM_TYPE_INT64;
    mcols[1].is_nullable = true;
    mcols[2].name = "c_bool";
    mcols[2].type = C_ORM_TYPE_BOOL;
    mcols[3].name = "c_f32";
    mcols[3].type = C_ORM_TYPE_FLOAT;
    mcols[4].name = "c_f64";
    mcols[4].type = C_ORM_TYPE_DOUBLE;
    mcols[5].name = "c_dec_def";
    mcols[5].type = C_ORM_TYPE_DECIMAL; /* precision=0, scale=0 */
    mcols[6].name = "c_dec_eq";
    mcols[6].type = C_ORM_TYPE_DECIMAL;
    mcols[6].precision = 4;
    mcols[6].scale = 4; /* ip == 0 -> SV9(4) */
    mcols[7].name = "c_dec_ip";
    mcols[7].type = C_ORM_TYPE_DECFLOAT;
    mcols[7].precision = 8;
    mcols[7].scale = 2;
    mcols[8].name = "c_cf0";
    mcols[8].type = C_ORM_TYPE_CHAR_FIXED; /* fixed_length == 0 */
    mcols[9].name = "c_bf0";
    mcols[9].type = C_ORM_TYPE_BINARY_FIXED; /* fixed_length == 0 */
    mcols[10].name = "c_gr0";
    mcols[10].type = C_ORM_TYPE_GRAPHIC; /* precision == 0 */
    mcols[11].name = "c_vg0";
    mcols[11].type = C_ORM_TYPE_VARGRAPHIC; /* precision == 0 */
    mcols[12].name = "c_vg8";
    mcols[12].type = C_ORM_TYPE_VARGRAPHIC;
    mcols[12].precision = 8;
    mcols[13].name = "c_dt";
    mcols[13].type = C_ORM_TYPE_DATE;
    mcols[14].name = "c_tm";
    mcols[14].type = C_ORM_TYPE_TIME;
    mcols[15].name = "c_ts";
    mcols[15].type = C_ORM_TYPE_TIMESTAMP;
    mcols[16].name = "c_vc0";
    mcols[16].type = C_ORM_TYPE_STRING; /* precision == 0 */

    memset(&m, 0, sizeof(m));
    m.name = "tbl-meta_x";
    m.columns = mcols;
    m.num_columns = 17;

    for (step = 0; step < 4; ++step) {
      c_orm_cobol_format_t fmt =
          (step & 1) ? C_ORM_COBOL_FORMAT_FREE : C_ORM_COBOL_FORMAT_FIXED;
      int dcl = (step & 2) ? 1 : 0;
      cpy_out = NULL;
      ASSERT_EQ(C_ORM_OK, c_orm_meta_to_copybook_emit(&m, fmt, dcl, &cpy_out));
      (void)c_orm_system_free(cpy_out);

      cpy_out = NULL;
      ASSERT_EQ(C_ORM_OK,
                c_orm_sql_to_copybook_emit(rich_sql, fmt, dcl, &cpy_out));
      (void)c_orm_system_free(cpy_out);
    }

    /* 0-column meta with emit_dclgen = 1 */
    m.num_columns = 0;
    cpy_out = NULL;
    ASSERT_EQ(C_ORM_OK, c_orm_meta_to_copybook_emit(
                            &m, C_ORM_COBOL_FORMAT_FIXED, 1, &cpy_out));
    (void)c_orm_system_free(cpy_out);
    m.num_columns = 17;

    /* Safe CRT sweeps across generate_c_files, emit_sql_ddl, & meta_to_copybook
     */
    for (step = 0; step < 250; ++step) {
      c_orm_mock_safe_crt_countdown = step;
      (void)c_orm_copybook_generate_c_files(rec, "test_cg_out");
      c_orm_mock_safe_crt_countdown = -1;
    }
    for (step = 0; step < 120; ++step) {
      ddl = NULL;
      c_orm_mock_safe_crt_countdown = step;
      (void)c_orm_copybook_emit_sql_ddl(rec, C_TO_SQL_DIALECT_DB2, &ddl);
      c_orm_mock_safe_crt_countdown = -1;
      (void)c_orm_system_free(ddl);

      ddl = NULL;
      c_orm_mock_safe_crt_countdown = step;
      (void)c_orm_copybook_emit_sql_ddl(rec, C_TO_SQL_DIALECT_ODBC, &ddl);
      c_orm_mock_safe_crt_countdown = -1;
      (void)c_orm_system_free(ddl);
    }
    for (step = 0; step < 130; ++step) {
      cpy_out = NULL;
      c_orm_mock_safe_crt_countdown = step;
      (void)c_orm_meta_to_copybook_emit(&m, C_ORM_COBOL_FORMAT_FIXED, 1,
                                        &cpy_out);
      c_orm_mock_safe_crt_countdown = -1;
      (void)c_orm_system_free(cpy_out);
    }
    for (step = 0; step < 60; ++step) {
      cpy_out = NULL;
      c_orm_mock_safe_crt_countdown = step;
      (void)c_orm_meta_to_copybook_emit(&m, C_ORM_COBOL_FORMAT_FREE, 0,
                                        &cpy_out);
      c_orm_mock_safe_crt_countdown = -1;
      (void)c_orm_system_free(cpy_out);
    }

    /* OOM sweeps across generate_c_files (including 1st-col DCLGEN VARCHAR),
     * emit_sql_ddl, meta_to_copybook, & sql_to_copybook */
    {
      c_orm_cobol_record_t *rvc1 = NULL;
      ASSERT_EQ(C_ORM_OK, c_orm_copybook_parse_string(
                              "01 RVC.\n 05 VC.\n  49 VC-L PIC S9(4) COMP.\n"
                              "  49 VC-T PIC X(10).\n",
                              C_ORM_COBOL_FORMAT_FREE, NULL, 0, &rvc1));
      ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(cpy_oom_malloc, cpy_oom_realloc,
                                               cpy_oom_free));
      for (step = 0; step < 10; ++step) {
        ddl = NULL;
        g_cpy_oom_countdown = step;
        rc = c_orm_copybook_emit_sql_ddl(rvc1, C_TO_SQL_DIALECT_DB2, &ddl);
        g_cpy_oom_countdown = -1;
        (void)c_orm_system_free(ddl);
        if (rc == C_ORM_OK) {
          break;
        }
        ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
      }
      ASSERT_EQ(C_ORM_OK, c_orm_reset_allocators());
      ASSERT_EQ(C_ORM_OK, c_orm_copybook_record_free(rvc1));
    }
    {
      c_orm_cobol_record_t *r61 = NULL;
      c_orm_cobol_record_t *rc63 = NULL;
      ASSERT_EQ(C_ORM_OK,
                c_orm_copybook_parse_string(
                    "01 AAAAAAAAAAAAAAAAAA.\n 05 BBBBBBBB PIC X(1).\n",
                    C_ORM_COBOL_FORMAT_FREE, NULL, 0, &r61));
      ASSERT_EQ(C_ORM_OK,
                c_orm_copybook_parse_string(
                    "01 AAAAAAAAAAAAAAAAAAAAA.\n 05 BBBBBBBBBB PIC X(1).\n",
                    C_ORM_COBOL_FORMAT_FREE, NULL, 0, &rc63));
      ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(cpy_oom_malloc, cpy_oom_realloc,
                                               cpy_oom_free));
      for (step = 0; step < 15; ++step) {
        ddl = NULL;
        g_cpy_oom_countdown = step;
        rc = c_orm_copybook_emit_sql_ddl(r61, C_TO_SQL_DIALECT_DB2, &ddl);
        g_cpy_oom_countdown = -1;
        (void)c_orm_system_free(ddl);
        if (rc == C_ORM_OK) {
          break;
        }
        ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
      }
      for (step = 0; step < 15; ++step) {
        g_cpy_oom_countdown = step;
        rc = c_orm_copybook_generate_c_files(rc63, "test_cg_out");
        g_cpy_oom_countdown = -1;
        if (rc == C_ORM_OK) {
          break;
        }
        ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
      }
      ASSERT_EQ(C_ORM_OK, c_orm_reset_allocators());
      ASSERT_EQ(C_ORM_OK, c_orm_copybook_record_free(r61));
      ASSERT_EQ(C_ORM_OK, c_orm_copybook_record_free(rc63));
    }
    ASSERT_EQ(C_ORM_OK, c_orm_set_allocators(cpy_oom_malloc, cpy_oom_realloc,
                                             cpy_oom_free));
    for (step = 0; step < 80; ++step) {
      g_cpy_oom_countdown = step;
      rc = c_orm_copybook_generate_c_files(rec, "test_cg_out");
      g_cpy_oom_countdown = -1;
      if (rc == C_ORM_OK) {
        break;
      }
      ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
    }
    for (step = 0; step < 80; ++step) {
      ddl = NULL;
      g_cpy_oom_countdown = step;
      rc = c_orm_copybook_emit_sql_ddl(rec, C_TO_SQL_DIALECT_DB2, &ddl);
      g_cpy_oom_countdown = -1;
      (void)c_orm_system_free(ddl);
      if (rc == C_ORM_OK) {
        break;
      }
      ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
    }
    for (step = 0; step < 40; ++step) {
      cpy_out = NULL;
      g_cpy_oom_countdown = step;
      rc =
          c_orm_meta_to_copybook_emit(&m, C_ORM_COBOL_FORMAT_FREE, 1, &cpy_out);
      g_cpy_oom_countdown = -1;
      (void)c_orm_system_free(cpy_out);
      if (rc == C_ORM_OK) {
        break;
      }
      ASSERT_EQ(C_ORM_ERROR_MEMORY, rc);
    }
    for (step = 0; step < 260; ++step) {
      cpy_out = NULL;
      g_cpy_oom_countdown = step;
      rc = c_orm_sql_to_copybook_emit(rich_sql, C_ORM_COBOL_FORMAT_FIXED, 1,
                                      &cpy_out);
      g_cpy_oom_countdown = -1;
      (void)c_orm_system_free(cpy_out);
      if (rc == C_ORM_OK) {
        break;
      }
      ASSERT_NEQ(C_ORM_OK, rc);
    }
    ASSERT_EQ(C_ORM_OK, c_orm_reset_allocators());
    remove("test_cg_out/Models.h");
    remove("test_cg_out/Models.c");
    (void)CPY_RMDIR("test_cg_out");
  }

  /* 4. Subprocess CLI tests (cpy2c, cpy2sql, sql2cpy, --db db2://, --db
   * odbc://) */
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__) && !defined(__CYGWIN__)
  {
    char cmd[1024];
    int sys_rc;

    ASSERT_EQ(C_ORM_OK,
              cpy_test_write_file("test_sub_in.cpy",
                                  "01 :T:-REC.\n 05 :T:-ID PIC S9(9) COMP.\n"));
    (void)CPY_MKDIR("test_sub_out");

    ASSERT_EQ(C_ORM_OK,
              C_ORM_SPRINTF(cmd, sizeof(cmd),
                            "\"%s\" cpy2c test_sub_in.cpy test_sub_out "
                            "--format free --replacing :T:=SUB",
                            CPY_CLI_BIN));
    sys_rc = system(cmd);
    ASSERT_EQ(0, sys_rc);

    ASSERT_EQ(C_ORM_OK,
              C_ORM_SPRINTF(cmd, sizeof(cmd),
                            "\"%s\" cpy2sql test_sub_in.cpy test_sub_out.sql "
                            "--dialect db2 --replacing :T:=SUB",
                            CPY_CLI_BIN));
    sys_rc = system(cmd);
    ASSERT_EQ(0, sys_rc);

    ASSERT_EQ(C_ORM_OK,
              C_ORM_SPRINTF(cmd, sizeof(cmd),
                            "\"%s\" sql2cpy test_sub_out.sql test_sub_rt.cpy "
                            "--dclgen --format fixed",
                            CPY_CLI_BIN));
    sys_rc = system(cmd);
    ASSERT_EQ(0, sys_rc);

    /* Subprocess --db db2:// and --db odbc:// error handling */
    ASSERT_EQ(
        C_ORM_OK,
        C_ORM_SPRINTF(cmd, sizeof(cmd),
                      "\"%s\" status --db db2://invalid --dir test_sub_out",
                      CPY_CLI_BIN));
    sys_rc = system(cmd);
    ASSERT_NEQ(0, sys_rc);

    ASSERT_EQ(
        C_ORM_OK,
        C_ORM_SPRINTF(cmd, sizeof(cmd),
                      "\"%s\" status --db odbc://invalid --dir test_sub_out",
                      CPY_CLI_BIN));
    sys_rc = system(cmd);
    ASSERT_NEQ(0, sys_rc);

    remove("test_sub_in.cpy");
    remove("test_sub_out.sql");
    remove("test_sub_rt.cpy");
    remove("test_sub_out/Models.h");
    remove("test_sub_out/Models.c");
    (void)CPY_RMDIR("test_sub_out");
  }
#endif

  ASSERT_EQ(C_ORM_OK, c_orm_copybook_record_free(rec));
  PASS();
}

/**
 * @brief Suite registration for COBOL Copybook parser, wire, and codegen tests.
 * @param copybook_suite Suite runner function name.
 */
SUITE(copybook_suite) {
  RUN_TEST(test_copybook_parse_fixed_and_dclgen);
  RUN_TEST(test_copybook_parse_free_and_clauses);
  RUN_TEST(test_copybook_wire_ebcdic_comp3_vectors);
  RUN_TEST(test_copybook_wire_complex_odo_and_dclgen);
  RUN_TEST(test_copybook_bidirectional_codegen_and_cli);
  RUN_TEST(test_copybook_errors_and_oom);
  RUN_TEST(test_copybook_parser_exhaustive_and_oom);
  RUN_TEST(test_copybook_wire_exhaustive_and_oom);
  RUN_TEST(test_copybook_codegen_and_cli_exhaustive);
}
