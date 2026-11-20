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
 * @param credit_str Credit decimal string to post.
 * @param fetch_id Account ID to query after insertion.
 * @param pack_cap Outbound wire buffer capacity (`<= 64`).
 * @return C_ORM_OK on success, or percolated `c_orm_error_t` on failure.
 */
static c_orm_error_t run_cobol_ledger_on_db(c_orm_db_t *db, const char *db_url,
                                            const char *cpy_src,
                                            const unsigned char *wire,
                                            size_t wire_len,
                                            const char *credit_str,
                                            int32_t fetch_id, size_t pack_cap) {
  c_orm_cobol_record_t *rec = NULL;
  char *ddl_sql = NULL;
  AcctLedger inbound;
  AcctLedger persisted;
  c_orm_decimal_t credit;
  c_orm_decimal_t debit;
  c_orm_decimal_t tmp_bal;
  c_orm_decimal_t expected_bal;
  unsigned char outbound_wire[64];
  size_t outbound_len = 0;
  char bal_str[48];
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
    (void)c_orm_copybook_record_free(rec);
    return rc;
  }

  rc = c_orm_copybook_validate_88(rec, &AcctLedger_meta, &inbound);
  if (rc != C_ORM_OK) {
    printf("c_orm_copybook_validate_88 failed: %d\n", (int)rc);
    (void)c_orm_copybook_record_free(rec);
    return rc;
  }
  c_orm_copybook_check_condition(rec, &AcctLedger_meta, &inbound, "ACTIVE-ACCT",
                                 &is_active);
  if (!is_active) {
    printf("Expected ACTIVE-ACCT condition to hold\n");
    (void)c_orm_copybook_record_free(rec);
    return C_ORM_ERROR_VALIDATION;
  }

  /* 3. Exact base-10 fixed-point postings (+credit - 150.10) */
  rc = c_orm_decimal_from_str(credit_str, &credit);
  if (rc != C_ORM_OK) {
    printf("Decimal credit parse failed: %d\n", (int)rc);
    (void)c_orm_copybook_record_free(rec);
    return rc;
  }
  c_orm_decimal_from_str("150.10", &debit);
  c_orm_decimal_add(&inbound.acct_bal, &credit, &tmp_bal);
  c_orm_decimal_sub(&tmp_bal, &debit, &inbound.acct_bal);

  /* 4. Emit dialect DDL, persist & query back via c-orm */
  if (strncmp(db_url, "db2://", 6) == 0) {
    ddl_dialect = C_TO_SQL_DIALECT_DB2;
  } else if (strncmp(db_url, "odbc://", 7) == 0) {
    ddl_dialect = C_TO_SQL_DIALECT_ODBC;
  }

  c_orm_copybook_emit_sql_ddl(rec, ddl_dialect, &ddl_sql);
  rc = c_orm_execute_raw(db, ddl_sql);
  (void)c_orm_system_free(ddl_sql);
  if (rc != C_ORM_OK) {
    printf("DDL execution failed: %d\n", (int)rc);
    (void)c_orm_copybook_record_free(rec);
    return rc;
  }

  rc = c_orm_insert(db, &AcctLedger_meta, &inbound);
  if (rc != C_ORM_OK) {
    printf("c_orm_insert failed: %d\n", (int)rc);
    (void)c_orm_copybook_record_free(rec);
    return rc;
  }

  memset(&persisted, 0, sizeof(persisted));
  rc = c_orm_find_by_id_int32(db, &AcctLedger_meta, fetch_id, &persisted);
  if (rc != C_ORM_OK) {
    printf("c_orm_find_by_id_int32 failed: %d\n", (int)rc);
    (void)c_orm_copybook_record_free(rec);
    return rc;
  }

  c_orm_decimal_from_str("152349.90", &expected_bal);
  c_orm_decimal_cmp(&persisted.acct_bal, &expected_bal, &cmp);
  if (cmp != 0) {
    printf("Persisted balance mismatch (cmp=%d)\n", cmp);
    (void)c_orm_copybook_record_free(rec);
    return C_ORM_ERROR_VALIDATION;
  }

  /* 5. Repack updated ledger row into IBM037 EBCDIC + COMP-3 wire record */
  rc = c_orm_copybook_pack_struct(rec, &AcctLedger_meta, &persisted,
                                  C_ORM_CP_IBM037, 1, outbound_wire, pack_cap,
                                  &outbound_len);
  (void)c_orm_copybook_record_free(rec);
  if (rc != C_ORM_OK) {
    printf("c_orm_copybook_pack_struct failed: %d\n", (int)rc);
    return rc;
  }

  (void)c_orm_decimal_to_str(&persisted.acct_bal, bal_str, sizeof(bal_str),
                             NULL);
  printf("Ledger account %d (%s) updated to %s (%s) [" C_ORM_FMT_SIZE_T
         " IBM037 wire bytes]\n",
         (int)persisted.acct_id, persisted.acct_holder, bal_str,
         persisted.acct_ref_code, C_ORM_CAST_SIZE_T(outbound_len));
  return C_ORM_OK;
}

/**
 * @brief Executes the full COBOL Copybook + `COMP-3`/`IBM037` + ORM ledger
 * posting workflow against `db_url`.
 * @param db_url Connection URL (`sqlite://:memory:`, `db2://...`,
 * `odbc://...`).
 * @return C_ORM_OK on success, or percolated `c_orm_error_t` on failure.
 */
c_orm_error_t run_cobol_ledger_workflow(const char *db_url);

c_orm_error_t run_cobol_ledger_workflow(const char *db_url) {
  char ledger_cpy[1200];
  c_orm_db_t *db = NULL;
  c_orm_error_t rc;

  if (!db_url) {
    return C_ORM_ERROR_NULL_POINTER;
  }

  (void)build_ledger_copybook_source(ledger_cpy, sizeof(ledger_cpy));

  rc = c_orm_connect_url(db_url, &db);
  if (rc != C_ORM_OK) {
    printf("c_orm_connect_url failed: %d\n", (int)rc);
    return rc;
  }

  rc = run_cobol_ledger_on_db(db, db_url, ledger_cpy, g_default_inbound_wire,
                              sizeof(g_default_inbound_wire), "2499.25", 1001,
                              64);
  db->vtable->disconnect(db);
  return rc;
}

/**
 * @brief Resolves `C_ORM_EXAMPLE_DB_URL` (defaulting to `sqlite://:memory:`)
 * and runs the ledger workflow.
 * @return C_ORM_OK on success, or percolated `c_orm_error_t` on failure.
 */
static c_orm_error_t run_cobol_ledger_from_env(void) {
  char env_url[512];
  const char *url = "sqlite://:memory:";

  env_url[0] = '\0';
  (void)C_ORM_GETENV(env_url, sizeof(env_url), "C_ORM_EXAMPLE_DB_URL");
  if (env_url[0] != '\0') {
    url = env_url;
  }
  return run_cobol_ledger_workflow(url);
}

/**
 * @brief Main entry point for the COBOL + Db2/ODBC/SQLite ledger example.
 * @return 0 on success, non-zero error code on failure.
 */
int main(void);

int main(void) {
  char ledger_cpy[1200];
  unsigned char bad_wire[31];
  c_orm_db_t *db = NULL;
  int failed = 0;

  /* 1. Default run + C_ORM_EXAMPLE_DB_URL override */
  (void)C_ORM_UNSETENV("C_ORM_EXAMPLE_DB_URL");
  failed |= (run_cobol_ledger_from_env() != C_ORM_OK);
  (void)C_ORM_SETENV("C_ORM_EXAMPLE_DB_URL", "sqlite://:memory:");
  failed |= (run_cobol_ledger_from_env() != C_ORM_OK);
  (void)C_ORM_UNSETENV("C_ORM_EXAMPLE_DB_URL");

  /* 2. URL validation & connection failures */
  failed |= (run_cobol_ledger_workflow(NULL) == C_ORM_OK);
  failed |= (run_cobol_ledger_workflow("unsupported://x") == C_ORM_OK);

  (void)build_ledger_copybook_source(ledger_cpy, sizeof(ledger_cpy));

  /* 3. Copybook syntax, truncated wire, invalid 88 ('Z'), frozen 88 ('F'),
   * malformed credit, DECIMAL(11,2) overflow, missing ID, balance mismatch,
   * small pack buffer, DDL failure, and db2:///odbc:// DDL dialect paths */
  failed |= (c_orm_connect_url("sqlite://:memory:", &db) != C_ORM_OK);
  failed |= (run_cobol_ledger_on_db(
                 db, "sqlite://:memory:", "000100 01 BAD PIC ???.\n",
                 g_default_inbound_wire, 31, "2499.25", 1001, 64) == C_ORM_OK);
  failed |= (run_cobol_ledger_on_db(db, "sqlite://:memory:", ledger_cpy,
                                    g_default_inbound_wire, 4, "2499.25", 1001,
                                    64) == C_ORM_OK);

  memcpy(bad_wire, g_default_inbound_wire, sizeof(bad_wire));
  bad_wire[22] = 0xE9; /* 'Z' in IBM037 -> violates level-88 domain */
  failed |=
      (run_cobol_ledger_on_db(db, "sqlite://:memory:", ledger_cpy, bad_wire, 31,
                              "2499.25", 1001, 64) == C_ORM_OK);

  bad_wire[22] = 0xC6; /* 'F' in IBM037 -> valid 88, but !ACTIVE-ACCT */
  failed |=
      (run_cobol_ledger_on_db(db, "sqlite://:memory:", ledger_cpy, bad_wire, 31,
                              "2499.25", 1001, 64) == C_ORM_OK);

  failed |= (run_cobol_ledger_on_db(db, "sqlite://:memory:", ledger_cpy,
                                    g_default_inbound_wire, 31, "NOT_DECIMAL",
                                    1001, 64) == C_ORM_OK);

  /* DECIMAL(11,2) overflow on insert (credit 99999999999.99 -> 12 digits) */
  failed |= (run_cobol_ledger_on_db(db, "sqlite://:memory:", ledger_cpy,
                                    g_default_inbound_wire, 31,
                                    "99999999999.99", 1001, 64) == C_ORM_OK);
  /* Table ACCT_REC now exists on db -> second DDL run fails */
  failed |= (run_cobol_ledger_on_db(db, "db2://mock/DB", ledger_cpy,
                                    g_default_inbound_wire, 31, "2499.25", 1001,
                                    64) == C_ORM_OK);
  failed |= (run_cobol_ledger_on_db(db, "odbc://DSN=Mock", ledger_cpy,
                                    g_default_inbound_wire, 31, "2499.25", 1001,
                                    64) == C_ORM_OK);
  (void)db->vtable->disconnect(db);

  /* Missing fetch_id */
  db = NULL;
  failed |= (c_orm_connect_url("sqlite://:memory:", &db) != C_ORM_OK);
  failed |= (run_cobol_ledger_on_db(db, "sqlite://:memory:", ledger_cpy,
                                    g_default_inbound_wire, 31, "2499.25", 9999,
                                    64) == C_ORM_OK);
  (void)db->vtable->disconnect(db);

  /* Balance mismatch */
  db = NULL;
  failed |= (c_orm_connect_url("sqlite://:memory:", &db) != C_ORM_OK);
  failed |= (run_cobol_ledger_on_db(db, "sqlite://:memory:", ledger_cpy,
                                    g_default_inbound_wire, 31, "10.00", 1001,
                                    64) == C_ORM_OK);
  (void)db->vtable->disconnect(db);

  /* Small outbound wire buffer */
  db = NULL;
  failed |= (c_orm_connect_url("sqlite://:memory:", &db) != C_ORM_OK);
  failed |= (run_cobol_ledger_on_db(db, "sqlite://:memory:", ledger_cpy,
                                    g_default_inbound_wire, 31, "2499.25", 1001,
                                    8) == C_ORM_OK);
  (void)db->vtable->disconnect(db);

  return failed;
}
