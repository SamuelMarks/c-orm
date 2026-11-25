/**
 * @file cobol_db2_ledger.c
 * @brief End-to-end financial ledger example demonstrating COBOL Copybook
 * parsing, IBM037 EBCDIC + COMP-3 mainframe wire unpacking/repacking, lossless
 * fixed-point decimal postings, level-88 domain validation, and Db2/ODBC/SQLite
 * persistence via c-orm.
 */

/* clang-format off */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

#include "c_orm_api.h"
#include "c_orm_copybook.h"
#include "c_orm_db.h"
#include "c_orm_decimal.h"
#include "c_orm_ebcdic.h"
#include "c_orm_inline_macros.h"
#include "c_orm_memory.h"
#include "c_orm_safe_crt.h"
#include "c_orm_struct.h"
/* clang-format on */

/**
 * @brief Typed C89 ORM model mapped to the `ACCT-REC` COBOL Copybook and SQL
 * `ACCT_REC` table.
 */
typedef struct AcctLedger {
  /** @brief `ACCT-ID PIC S9(9) USAGE COMP` primary key. */
  int32_t acct_id;
  /** @brief `ACCT-HOLDER PIC X(12)` space-padded EBCDIC holder name. */
  char acct_holder[13];
  /** @brief `ACCT-BAL PIC S9(9)V99 USAGE COMP-3` exact ledger balance. */
  c_orm_decimal_t acct_bal;
  /** @brief `ACCT-STATUS PIC X(1)` status code (`'A'`, `'F'`, `'C'`). */
  char acct_status[2];
  /** @brief `ACCT-REF-CODE PIC X(8)` branch/reference code. */
  char acct_ref_code[9];
} AcctLedger;

static const c_orm_column_meta_t AcctLedger_cols[] = {
    C_ORM_DEFINE_COLUMN_EX("acct_id", C_ORM_TYPE_INT32,
                           offsetof(struct AcctLedger, acct_id), true, false,
                           NULL, false, false, 9, 0, 0, 0),
    C_ORM_COL_CHAR_FIXED(AcctLedger, acct_holder, 12, 37, false),
    C_ORM_COL_DECIMAL(AcctLedger, acct_bal, 11, 2, false),
    C_ORM_COL_CHAR_FIXED(AcctLedger, acct_status, 1, 37, false),
    C_ORM_COL_CHAR_FIXED(AcctLedger, acct_ref_code, 8, 37, false)};

static const c_orm_table_meta_t AcctLedger_meta = {
    "ACCT_REC",
    AcctLedger_cols,
    5,
    sizeof(struct AcctLedger),
    "SELECT acct_id, acct_holder, acct_bal, acct_status, acct_ref_code FROM "
    "ACCT_REC",
    "SELECT acct_id, acct_holder, acct_bal, acct_status, acct_ref_code FROM "
    "ACCT_REC WHERE acct_id = ?",
    "INSERT INTO ACCT_REC (acct_id, acct_holder, acct_bal, acct_status, "
    "acct_ref_code) VALUES (?, ?, ?, ?, ?)",
    "UPDATE ACCT_REC SET acct_holder = ?, acct_bal = ?, acct_status = ?, "
    "acct_ref_code = ? WHERE acct_id = ?",
    "DELETE FROM ACCT_REC WHERE acct_id = ?",
    NULL,
    false,
    false,
    0,
    0,
    {0},
    NULL,
    0};

/** @brief Default 31-byte IBM037 EBCDIC + COMP + COMP-3 mainframe record. */
static const unsigned char g_default_inbound_wire[31] = {
    0x00, 0x00, 0x03, 0xE9, 0xD9, 0xD6, 0xE3, 0xC8, 0xE2, 0xC3, 0xC8,
    0xC9, 0xD3, 0xC4, 0x40, 0x40, 0x00, 0x01, 0x50, 0x00, 0x07, 0x5C,
    0xC1, 0xC2, 0xD9, 0xF0, 0xF1, 0xF0, 0xF0, 0xF4, 0xF2};

/**
 * @brief Assembles the default fixed-format `ACCT-REC` COBOL Copybook text.
 * @param out_buf Destination character buffer.
 * @param buf_cap Capacity of `out_buf` in bytes.
 * @return C_ORM_OK on success, or Safe CRT error code.
 */
static c_orm_error_t build_ledger_copybook_source(char *out_buf,
                                                  size_t buf_cap) {
  static const char *part1 =
      "000100* FINANCIAL ACCOUNT LEDGER COPYBOOK                               "
      "LED00001\n"
      "000200 01  ACCT-REC.                                                    "
      "LED00002\n"
      "000300     05 ACCT-ID           PIC S9(9) USAGE COMP.                   "
      "LED00003\n"
      "000400     05 ACCT-HOLDER       PIC X(12).                              "
      "LED00004\n"
      "000500     05 ACCT-BAL          PIC S9(9)V99 USAGE COMP-3.              "
      "LED00005\n";
  static const char *part2 =
      "000600     05 ACCT-STATUS       PIC X(1).                               "
      "LED00006\n"
      "000700        88 ACTIVE-ACCT    VALUE 'A'.                              "
      "LED00007\n"
      "000800        88 FROZEN-ACCT    VALUE 'F'.                              "
      "LED00008\n"
      "000900        88 CLOSED-ACCT    VALUE 'C'.                              "
      "LED00009\n";
  static const char *part3 =
      "001000     05 ACCT-REF-CODE     PIC X(8).                               "
      "LED00010\n"
      "001100     05 ACCT-REF-PARTS REDEFINES ACCT-REF-CODE.                   "
      "LED00011\n"
      "001200        10 ACCT-BRANCH    PIC X(4).                               "
      "LED00012\n"
      "001300        10 ACCT-SEQ-NUM   PIC 9(4).                               "
      "LED00013\n";

  return C_ORM_SPRINTF(out_buf, buf_cap, "%s%s%s", part1, part2, part3);
}

/**
 * @brief Executes the COBOL Copybook + `COMP-3`/`IBM037` + ORM ledger workflow
 * on an open `c_orm_db_t` connection with configurable inputs.
 * @param db Active database handle.
 * @param db_url Connection URL hint for SQL DDL dialect selection.
 * @param cpy_src COBOL Copybook source text.
 * @param wire Inbound mainframe wire byte buffer.
 * @param wire_len Length of `wire` in bytes.
 * @param cond_name Level-88 condition name to evaluate.
 * @param credit_str Credit decimal string to post.
 * @param debit_str Debit decimal string to post.
 * @param expected_bal Expected final balance decimal pointer.
 * @param fetch_id Account ID to query after insertion.
 * @param pack_cap Outbound wire buffer capacity (`<= 64`).
 * @param out_bal_str Output buffer receiving formatted decimal balance string.
 * @param bal_str_cap Capacity of `out_bal_str` in bytes.
 * @return C_ORM_OK on success, or percolated `c_orm_error_t` on failure.
 */
static c_orm_error_t
run_cobol_ledger_on_db(c_orm_db_t *db, const char *db_url, const char *cpy_src,
                       const unsigned char *wire, size_t wire_len,
                       const char *cond_name, const char *credit_str,
                       const char *debit_str,
                       const c_orm_decimal_t *expected_bal, int32_t fetch_id,
                       size_t pack_cap, char *out_bal_str, size_t bal_str_cap) {
  c_orm_cobol_record_t *rec = NULL;
  char *ddl_sql = NULL;
  AcctLedger inbound;
  AcctLedger persisted;
  c_orm_decimal_t credit;
  c_orm_decimal_t debit;
  c_orm_decimal_t tmp_bal;
  unsigned char outbound_wire[64];
  size_t outbound_len = 0;
  int is_active = 0;
  int cmp = 0;
  c_to_sql_dialect_t ddl_dialect = C_TO_SQL_DIALECT_SQLITE;
  c_orm_error_t rc;

  /* 1. Parse COBOL Copybook */
  rc = c_orm_copybook_parse_string(cpy_src, C_ORM_COBOL_FORMAT_FIXED, NULL, 0,
                                   &rec);
  if (rc != C_ORM_OK) {
    printf("c_orm_copybook_parse_string failed: %d\n", (int)rc);
    return rc;
  }

  /* 2. Unpack IBM037 + COMP-3 mainframe record into C89 struct */
  memset(&inbound, 0, sizeof(inbound));
  rc = c_orm_copybook_unpack_struct(rec, &AcctLedger_meta, wire, wire_len,
                                    C_ORM_CP_IBM037, 1, &inbound);
  if (rc != C_ORM_OK) {
    printf("c_orm_copybook_unpack_struct failed: %d\n", (int)rc);
    goto cleanup_rec;
  }

  rc = c_orm_copybook_validate_88(rec, &AcctLedger_meta, &inbound);
  if (rc != C_ORM_OK) {
    printf("c_orm_copybook_validate_88 failed: %d\n", (int)rc);
    goto cleanup_rec;
  }
  rc = c_orm_copybook_check_condition(rec, &AcctLedger_meta, &inbound,
                                      cond_name, &is_active);
  if (rc != C_ORM_OK) {
    goto cleanup_rec;
  }
  if (!is_active) {
    printf("Expected ACTIVE-ACCT condition to hold\n");
    rc = C_ORM_ERROR_VALIDATION;
    goto cleanup_rec;
  }

  /* 3. Exact base-10 fixed-point postings (+credit - debit) */
  rc = c_orm_decimal_from_str(credit_str, &credit);
  if (rc != C_ORM_OK) {
    printf("Decimal credit parse failed: %d\n", (int)rc);
    goto cleanup_rec;
  }
  rc = c_orm_decimal_from_str(debit_str, &debit);
  if (rc != C_ORM_OK) {
    goto cleanup_rec;
  }
  rc = c_orm_decimal_add(&inbound.acct_bal, &credit, &tmp_bal);
  if (rc != C_ORM_OK) {
    goto cleanup_rec;
  }
  rc = c_orm_decimal_sub(&tmp_bal, &debit, &inbound.acct_bal);
  if (rc != C_ORM_OK) {
    goto cleanup_rec;
  }

  /* 4. Emit dialect DDL, persist & query back via c-orm */
  if (strncmp(db_url, "db2://", 6) == 0) {
    ddl_dialect = C_TO_SQL_DIALECT_DB2;
  } else if (strncmp(db_url, "odbc://", 7) == 0) {
    ddl_dialect = C_TO_SQL_DIALECT_ODBC;
  }

  rc = c_orm_copybook_emit_sql_ddl(rec, ddl_dialect, &ddl_sql);
  if (rc != C_ORM_OK) {
    goto cleanup_rec;
  }
  rc = c_orm_execute_raw(db, ddl_sql);
  if (rc != C_ORM_OK) {
    c_orm_system_free(ddl_sql);
    printf("DDL execution failed: %d\n", (int)rc);
    goto cleanup_rec;
  }
  c_orm_system_free(ddl_sql);

  rc = c_orm_insert(db, &AcctLedger_meta, &inbound);
  if (rc != C_ORM_OK) {
    printf("c_orm_insert failed: %d\n", (int)rc);
    goto cleanup_rec;
  }

  memset(&persisted, 0, sizeof(persisted));
  rc = c_orm_find_by_id_int32(db, &AcctLedger_meta, fetch_id, &persisted);
  if (rc != C_ORM_OK) {
    printf("c_orm_find_by_id_int32 failed: %d\n", (int)rc);
    goto cleanup_rec;
  }

  rc = c_orm_decimal_cmp(&persisted.acct_bal, expected_bal, &cmp);
  if (rc != C_ORM_OK) {
    goto cleanup_rec;
  }
  if (cmp != 0) {
    printf("Persisted balance mismatch (cmp=%d)\n", cmp);
    rc = C_ORM_ERROR_VALIDATION;
    goto cleanup_rec;
  }

  /* 5. Repack updated ledger row into IBM037 EBCDIC + COMP-3 wire record */
  rc = c_orm_copybook_pack_struct(rec, &AcctLedger_meta, &persisted,
                                  C_ORM_CP_IBM037, 1, outbound_wire, pack_cap,
                                  &outbound_len);
  if (rc != C_ORM_OK) {
    printf("c_orm_copybook_pack_struct failed: %d\n", (int)rc);
    goto cleanup_rec;
  }
  c_orm_copybook_record_free(rec);

  rc =
      c_orm_decimal_to_str(&persisted.acct_bal, out_bal_str, bal_str_cap, NULL);
  if (rc != C_ORM_OK) {
    return rc;
  }
  printf("Ledger account %d (%s) updated to %s (%s) [" C_ORM_FMT_SIZE_T
         " IBM037 wire bytes]\n",
         (int)persisted.acct_id, persisted.acct_holder, out_bal_str,
         persisted.acct_ref_code, C_ORM_CAST_SIZE_T(outbound_len));
  return C_ORM_OK;

cleanup_rec:
  c_orm_copybook_record_free(rec);
  return rc;
}

/**
 * @brief Executes the full COBOL Copybook + `COMP-3`/`IBM037` + ORM ledger
 * posting workflow against `db_url`.
 * @param db_url Connection URL (`sqlite://:memory:`, `db2://...`,
 * `odbc://...`).
 * @return C_ORM_OK on success, or percolated `c_orm_error_t` on failure.
 */
c_orm_error_t run_cobol_ledger_workflow(const char *db_url);

/**
 * @brief Initializes the constant expected decimal balance `152349.90`.
 * @param out_dec Output decimal value.
 * @return Status code cast to int.
 */
static int ledger_calc_init_expected_bal(c_orm_decimal_t *out_dec) {
  return (int)c_orm_decimal_from_str("152349.90", out_dec);
}

/**
 * @brief Disconnects `db` during error cleanup.
 * @param db Database connection handle.
 * @return C_ORM_OK on success, or driver disconnect error code.
 */
static c_orm_error_t ledger_conn_cleanup_free(c_orm_db_t *db) {
  return db->vtable->disconnect(db);
}

c_orm_error_t run_cobol_ledger_workflow(const char *db_url) {
  char ledger_cpy[1200];
  char bal_str[48];
  c_orm_decimal_t expected_bal;
  c_orm_db_t *db = NULL;
  c_orm_error_t rc;
  int st;

  if (!db_url) {
    return C_ORM_ERROR_NULL_POINTER;
  }

  rc = build_ledger_copybook_source(ledger_cpy, sizeof(ledger_cpy));
  if (rc != C_ORM_OK) {
    return rc;
  }

  st = ledger_calc_init_expected_bal(&expected_bal);

  rc = c_orm_connect_url(db_url + (st * 0), &db);
  if (rc != C_ORM_OK) {
    printf("c_orm_connect_url failed: %d\n", (int)rc);
    return rc;
  }

  rc = run_cobol_ledger_on_db(db, db_url, ledger_cpy, g_default_inbound_wire,
                              sizeof(g_default_inbound_wire), "ACTIVE-ACCT",
                              "2499.25", "150.10", &expected_bal, 1001, 64,
                              bal_str, sizeof(bal_str));
  if (rc != C_ORM_OK) {
    ledger_conn_cleanup_free(db);
    return rc;
  }
  return db->vtable->disconnect(db);
}

/**
 * @brief Resolves `C_ORM_EXAMPLE_DB_URL` (defaulting to `sqlite://:memory:`)
 * and runs the ledger workflow.
 * @return C_ORM_OK on success, or percolated `c_orm_error_t` on failure.
 */
static c_orm_error_t run_cobol_ledger_from_env(void) {
  char env_url[512];
  const char *url = "sqlite://:memory:";
  c_orm_error_t rc;

  env_url[0] = '\0';
  rc = C_ORM_GETENV(env_url, sizeof(env_url), "C_ORM_EXAMPLE_DB_URL");
  if (rc != C_ORM_OK) {
    return rc;
  }
  if (env_url[0] != '\0') {
    url = env_url;
  }
  return run_cobol_ledger_workflow(url);
}

static int g_ledger_oom_cd = -1;

/**
 * @brief Countdown OOM allocator wrapper for testing allocation failures in the
 * ledger example.
 * @param size Allocation size in bytes.
 * @param out_ptr Output pointer receiving allocated memory.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on countdown trigger.
 */
static c_orm_error_t ledger_mock_malloc(size_t size, void **out_ptr) {
  if (g_ledger_oom_cd >= 0) {
    if (g_ledger_oom_cd == 0) {
      g_ledger_oom_cd = -1;
      *out_ptr = NULL;
      return C_ORM_ERROR_MEMORY;
    }
    g_ledger_oom_cd--;
  }
  return c_orm_default_malloc(size, out_ptr);
}

/**
 * @brief Countdown OOM realloc wrapper for testing reallocation failures in the
 * ledger example.
 * @param ptr Existing allocation pointer.
 * @param size New allocation size in bytes.
 * @param out_ptr Output pointer receiving reallocated memory.
 * @return C_ORM_OK on success, or C_ORM_ERROR_MEMORY on countdown trigger.
 */
static c_orm_error_t ledger_mock_realloc(void *ptr, size_t size,
                                         void **out_ptr) {
  if (g_ledger_oom_cd >= 0) {
    if (g_ledger_oom_cd == 0) {
      g_ledger_oom_cd = -1;
      *out_ptr = NULL;
      return C_ORM_ERROR_MEMORY;
    }
    g_ledger_oom_cd--;
  }
  return c_orm_default_realloc(ptr, size, out_ptr);
}

/**
 * @brief Main entry point for the COBOL + Db2/ODBC/SQLite ledger example.
 * @return 0 on success, non-zero error code on failure.
 */
int main(void);

int main(void) {
  char ledger_cpy[1200];
  char bal_str[48];
  unsigned char bad_wire[31];
  c_orm_decimal_t expected_bal;
  c_orm_db_t *db = NULL;
  int failed = 0;

#if defined(_MSC_VER) && defined(_DEBUG)
  _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
  _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
  _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif

  /* 1. Default run + C_ORM_EXAMPLE_DB_URL override + invalid env URL error */
  failed |= (C_ORM_UNSETENV("C_ORM_EXAMPLE_DB_URL") != C_ORM_OK);
  failed |= (run_cobol_ledger_from_env() != C_ORM_OK);
  failed |=
      (C_ORM_SETENV("C_ORM_EXAMPLE_DB_URL", "sqlite://:memory:") != C_ORM_OK);
  failed |= (run_cobol_ledger_from_env() != C_ORM_OK);
  failed |=
      (C_ORM_SETENV("C_ORM_EXAMPLE_DB_URL", "unsupported://x") != C_ORM_OK);
  failed |= (run_cobol_ledger_from_env() == C_ORM_OK);
  failed |= (C_ORM_UNSETENV("C_ORM_EXAMPLE_DB_URL") != C_ORM_OK);

#ifdef C_ORM_TEST_ALLOCATOR
  c_orm_mock_safe_crt_countdown = 0;
  failed |= (run_cobol_ledger_from_env() == C_ORM_OK);
  c_orm_mock_safe_crt_countdown = 0;
  failed |= (run_cobol_ledger_workflow("sqlite://:memory:") == C_ORM_OK);
#endif

  /* 2. URL validation, connection & pre-existing table workflow failure */
  failed |= (run_cobol_ledger_workflow(NULL) == C_ORM_OK);
  failed |= (run_cobol_ledger_workflow("unsupported://x") == C_ORM_OK);
  remove("ledger_collision.db");
  failed |=
      (run_cobol_ledger_workflow("sqlite://ledger_collision.db") != C_ORM_OK);
  /* Second run against same SQLite file fails DDL since ACCT_REC exists */
  failed |=
      (run_cobol_ledger_workflow("sqlite://ledger_collision.db") == C_ORM_OK);
  remove("ledger_collision.db");

  failed |= (build_ledger_copybook_source(ledger_cpy, sizeof(ledger_cpy)) !=
             C_ORM_OK);
  failed |= (c_orm_decimal_from_str("152349.90", &expected_bal) != C_ORM_OK);

  /* 3. Copybook syntax, truncated wire, invalid 88 ('Z'), unknown 88, frozen 88
   * ('F'), malformed credit/debit, add/sub overflow, DDL OOM/exec failure,
   * DECIMAL(11,2) overflow, missing ID, expected cmp/mismatch, small pack
   * buffer, and small decimal format buffer */
  failed |= (c_orm_connect_url("sqlite://:memory:", &db) != C_ORM_OK);
  failed |=
      (run_cobol_ledger_on_db(
           db, "sqlite://:memory:", "000100 01 BAD PIC ???.\n",
           g_default_inbound_wire, 31, "ACTIVE-ACCT", "2499.25", "150.10",
           &expected_bal, 1001, 64, bal_str, sizeof(bal_str)) == C_ORM_OK);
  failed |= (run_cobol_ledger_on_db(db, "sqlite://:memory:", ledger_cpy,
                                    g_default_inbound_wire, 4, "ACTIVE-ACCT",
                                    "2499.25", "150.10", &expected_bal, 1001,
                                    64, bal_str, sizeof(bal_str)) == C_ORM_OK);

  memcpy(bad_wire, g_default_inbound_wire, sizeof(bad_wire));
  bad_wire[22] = 0xE9; /* 'Z' in IBM037 -> violates level-88 domain */
  failed |=
      (run_cobol_ledger_on_db(db, "sqlite://:memory:", ledger_cpy, bad_wire, 31,
                              "ACTIVE-ACCT", "2499.25", "150.10", &expected_bal,
                              1001, 64, bal_str, sizeof(bal_str)) == C_ORM_OK);

  failed |= (run_cobol_ledger_on_db(db, "sqlite://:memory:", ledger_cpy,
                                    g_default_inbound_wire, 31, "NO-SUCH-COND",
                                    "2499.25", "150.10", &expected_bal, 1001,
                                    64, bal_str, sizeof(bal_str)) == C_ORM_OK);

  bad_wire[22] = 0xC6; /* 'F' in IBM037 -> valid 88, but !ACTIVE-ACCT */
  failed |=
      (run_cobol_ledger_on_db(db, "sqlite://:memory:", ledger_cpy, bad_wire, 31,
                              "ACTIVE-ACCT", "2499.25", "150.10", &expected_bal,
                              1001, 64, bal_str, sizeof(bal_str)) == C_ORM_OK);

  failed |= (run_cobol_ledger_on_db(
                 db, "sqlite://:memory:", ledger_cpy, g_default_inbound_wire,
                 31, "ACTIVE-ACCT", "NOT_DECIMAL", "150.10", &expected_bal,
                 1001, 64, bal_str, sizeof(bal_str)) == C_ORM_OK);
  failed |= (run_cobol_ledger_on_db(
                 db, "sqlite://:memory:", ledger_cpy, g_default_inbound_wire,
                 31, "ACTIVE-ACCT", "2499.25", "NOT_DECIMAL", &expected_bal,
                 1001, 64, bal_str, sizeof(bal_str)) == C_ORM_OK);
  failed |= (run_cobol_ledger_on_db(db, "sqlite://:memory:", ledger_cpy,
                                    g_default_inbound_wire, 31, "ACTIVE-ACCT",
                                    "99999999999999999999999999999999999999",
                                    "150.10", &expected_bal, 1001, 64, bal_str,
                                    sizeof(bal_str)) == C_ORM_OK);
  failed |=
      (run_cobol_ledger_on_db(
           db, "sqlite://:memory:", ledger_cpy, g_default_inbound_wire, 31,
           "ACTIVE-ACCT", "2499.25", "-99999999999999999999999999999999999999",
           &expected_bal, 1001, 64, bal_str, sizeof(bal_str)) == C_ORM_OK);

  /* OOM during c_orm_copybook_emit_sql_ddl */
  {
    int cd;
    failed |= (c_orm_set_allocators(ledger_mock_malloc, ledger_mock_realloc,
                                    c_orm_default_free) != C_ORM_OK);
    g_ledger_oom_cd = -1;
    failed |= (run_cobol_ledger_from_env() != C_ORM_OK);
    failed |=
        (C_ORM_SETENV("C_ORM_EXAMPLE_DB_URL", "sqlite://:memory:") != C_ORM_OK);
    c_orm_mock_safe_crt_countdown = 0;
    failed |= (run_cobol_ledger_from_env() == C_ORM_OK);
    c_orm_mock_safe_crt_countdown = -1;
    failed |= (C_ORM_UNSETENV("C_ORM_EXAMPLE_DB_URL") != C_ORM_OK);
    g_ledger_oom_cd = 9999;
    failed |=
        (run_cobol_ledger_on_db(
             db, "sqlite://:memory:", "000100 01 BAD PIC ???.\n",
             g_default_inbound_wire, 31, "ACTIVE-ACCT", "2499.25", "150.10",
             &expected_bal, 1001, 64, bal_str, sizeof(bal_str)) == C_ORM_OK);
    for (cd = 0;; ++cd) {
      c_orm_error_t oom_rc;
      c_orm_db_t *oom_db = NULL;
      g_ledger_oom_cd = -1;
      failed |= (c_orm_connect_url("sqlite://:memory:", &oom_db) != C_ORM_OK);
      g_ledger_oom_cd = cd;
      oom_rc = run_cobol_ledger_on_db(oom_db, "sqlite://:memory:", ledger_cpy,
                                      g_default_inbound_wire, 31, "ACTIVE-ACCT",
                                      "2499.25", "150.10", &expected_bal, 1001,
                                      64, bal_str, sizeof(bal_str));
      g_ledger_oom_cd = -1;
      failed |= (oom_db->vtable->disconnect(oom_db) != C_ORM_OK);
      if (oom_rc == C_ORM_OK) {
        break;
      }
    }
    g_ledger_oom_cd = -1;
    failed |= (c_orm_reset_allocators() != C_ORM_OK);
  }

  /* DECIMAL(11,2) overflow on insert (credit 99999999999.99 -> 12 digits) */
  {
    c_orm_db_t *db_ovf = NULL;
    failed |= (c_orm_connect_url("sqlite://:memory:", &db_ovf) != C_ORM_OK);
    failed |=
        (run_cobol_ledger_on_db(db_ovf, "sqlite://:memory:", ledger_cpy,
                                g_default_inbound_wire, 31, "ACTIVE-ACCT",
                                "99999999999.99", "150.10", &expected_bal, 1001,
                                64, bal_str, sizeof(bal_str)) == C_ORM_OK);
    /* Table ACCT_REC now exists on db_ovf -> second DDL run fails */
    failed |= (run_cobol_ledger_on_db(
                   db_ovf, "db2://mock/DB", ledger_cpy, g_default_inbound_wire,
                   31, "ACTIVE-ACCT", "2499.25", "150.10", &expected_bal, 1001,
                   64, bal_str, sizeof(bal_str)) == C_ORM_OK);
    failed |=
        (run_cobol_ledger_on_db(db_ovf, "odbc://DSN=Mock", ledger_cpy,
                                g_default_inbound_wire, 31, "ACTIVE-ACCT",
                                "2499.25", "150.10", &expected_bal, 1001, 64,
                                bal_str, sizeof(bal_str)) == C_ORM_OK);
    failed |= (db_ovf->vtable->disconnect(db_ovf) != C_ORM_OK);
  }
  failed |= (db->vtable->disconnect(db) != C_ORM_OK);

  /* Missing fetch_id */
  db = NULL;
  failed |= (c_orm_connect_url("sqlite://:memory:", &db) != C_ORM_OK);
  failed |= (run_cobol_ledger_on_db(db, "sqlite://:memory:", ledger_cpy,
                                    g_default_inbound_wire, 31, "ACTIVE-ACCT",
                                    "2499.25", "150.10", &expected_bal, 9999,
                                    64, bal_str, sizeof(bal_str)) == C_ORM_OK);
  failed |= (db->vtable->disconnect(db) != C_ORM_OK);

  /* Decimal cmp NULL error */
  db = NULL;
  failed |= (c_orm_connect_url("sqlite://:memory:", &db) != C_ORM_OK);
  failed |= (run_cobol_ledger_on_db(db, "sqlite://:memory:", ledger_cpy,
                                    g_default_inbound_wire, 31, "ACTIVE-ACCT",
                                    "2499.25", "150.10", NULL, 1001, 64,
                                    bal_str, sizeof(bal_str)) == C_ORM_OK);
  failed |= (db->vtable->disconnect(db) != C_ORM_OK);

  /* Balance mismatch */
  db = NULL;
  failed |= (c_orm_connect_url("sqlite://:memory:", &db) != C_ORM_OK);
  failed |= (run_cobol_ledger_on_db(db, "sqlite://:memory:", ledger_cpy,
                                    g_default_inbound_wire, 31, "ACTIVE-ACCT",
                                    "10.00", "150.10", &expected_bal, 1001, 64,
                                    bal_str, sizeof(bal_str)) == C_ORM_OK);
  failed |= (db->vtable->disconnect(db) != C_ORM_OK);

  /* Small outbound wire buffer */
  db = NULL;
  failed |= (c_orm_connect_url("sqlite://:memory:", &db) != C_ORM_OK);
  failed |= (run_cobol_ledger_on_db(db, "sqlite://:memory:", ledger_cpy,
                                    g_default_inbound_wire, 31, "ACTIVE-ACCT",
                                    "2499.25", "150.10", &expected_bal, 1001, 8,
                                    bal_str, sizeof(bal_str)) == C_ORM_OK);
  failed |= (db->vtable->disconnect(db) != C_ORM_OK);

  /* Small decimal format buffer */
  db = NULL;
  failed |= (c_orm_connect_url("sqlite://:memory:", &db) != C_ORM_OK);
  failed |= (run_cobol_ledger_on_db(db, "sqlite://:memory:", ledger_cpy,
                                    g_default_inbound_wire, 31, "ACTIVE-ACCT",
                                    "2499.25", "150.10", &expected_bal, 1001,
                                    64, bal_str, 2) == C_ORM_OK);
  failed |= (db->vtable->disconnect(db) != C_ORM_OK);

  return failed;
}
