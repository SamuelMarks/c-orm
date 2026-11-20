/**
 * @file c_orm_copybook.h
 * @brief Bidirectional COBOL Copybook (.cpy) AST, lexer/parser, binary
 * mainframe wire serializer (COMP-3, COMP, Zoned, EBCDIC, ODO, DCLGEN), and
 * C89 / Db2 SQL DDL code generation.
 */

#ifndef C_ORM_COPYBOOK_H
#define C_ORM_COPYBOOK_H

/* clang-format off */
#include "abstract_struct.h"
#include "c_orm_c_to_sql.h"
#include "c_orm_decimal.h"
#include "c_orm_ebcdic.h"
#include "c_orm_meta.h"
#include <stddef.h>
#include <stdio.h>
/* clang-format on */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @brief Source column layout mode for COBOL Copybook (.cpy) files.
 */
typedef enum c_orm_cobol_format {
  /** @brief Automatically detect fixed (cols 1-80) vs free format. */
  C_ORM_COBOL_FORMAT_AUTO = 0,
  /** @brief Traditional 80-column fixed format (seq 1-6, ind 7, area A/B 8-72).
   */
  C_ORM_COBOL_FORMAT_FIXED = 1,
  /** @brief Modern COBOL free format (`>>SOURCE FORMAT FREE`). */
  C_ORM_COBOL_FORMAT_FREE = 2
} c_orm_cobol_format_t;

/**
 * @brief COBOL `USAGE` storage representation clauses.
 */
typedef enum c_orm_cobol_usage {
  /** @brief Character or zoned-decimal display (`USAGE DISPLAY`). */
  C_ORM_COBOL_USAGE_DISPLAY = 0,
  /** @brief Mainframe binary integer (`COMP` / `COMP-4` / `BINARY`). */
  C_ORM_COBOL_USAGE_COMP = 1,
  /** @brief Native host-endian binary integer (`COMP-5`). */
  C_ORM_COBOL_USAGE_COMP_5 = 2,
  /** @brief Single-precision 4-byte floating point (`COMP-1`). */
  C_ORM_COBOL_USAGE_COMP_1 = 3,
  /** @brief Double-precision 8-byte floating point (`COMP-2`). */
  C_ORM_COBOL_USAGE_COMP_2 = 4,
  /** @brief BCD packed decimal (`COMP-3` / `PACKED-DECIMAL`). */
  C_ORM_COBOL_USAGE_COMP_3 = 5,
  /** @brief DBCS / UTF-16 national characters (`DISPLAY-1` / `NATIONAL`). */
  C_ORM_COBOL_USAGE_DISPLAY_1 = 6
} c_orm_cobol_usage_t;

/**
 * @brief Semantic category of an expanded COBOL `PICTURE` (`PIC`) string.
 */
typedef enum c_orm_cobol_pic_category {
  /** @brief Group item or float (`COMP-1`/`COMP-2`) without a `PIC` clause. */
  C_ORM_COBOL_PIC_NONE = 0,
  /** @brief Alphabetic item (`PIC A(n)`). */
  C_ORM_COBOL_PIC_ALPHABETIC = 1,
  /** @brief Alphanumeric item (`PIC X(n)`). */
  C_ORM_COBOL_PIC_ALPHANUMERIC = 2,
  /** @brief Operational numeric item (`PIC [S]9(n)[V9(m)][P(k)]`). */
  C_ORM_COBOL_PIC_NUMERIC = 3,
  /** @brief Edited numeric display item (`PIC Z, $, ., -, CR, DB`, etc.). */
  C_ORM_COBOL_PIC_NUMERIC_EDITED = 4,
  /** @brief Edited alphanumeric item (`PIC X` combined with `B`, `0`, `/`). */
  C_ORM_COBOL_PIC_ALPHANUMERIC_EDITED = 5,
  /** @brief Double-byte graphic / national character item (`PIC G(n)`/`N(n)`).
   */
  C_ORM_COBOL_PIC_NATIONAL = 6
} c_orm_cobol_pic_category_t;

/**
 * @brief Kind of COBOL `VALUE` default literal or figurative constant.
 */
typedef enum c_orm_cobol_value_kind {
  /** @brief No `VALUE` clause specified. */
  C_ORM_COBOL_VALUE_NONE = 0,
  /** @brief Quoted character/hex string literal. */
  C_ORM_COBOL_VALUE_STRING = 1,
  /** @brief Signed or unsigned numeric literal. */
  C_ORM_COBOL_VALUE_NUMERIC = 2,
  /** @brief Figurative constant `ZERO` / `ZEROS` / `ZEROES`. */
  C_ORM_COBOL_VALUE_ZEROS = 3,
  /** @brief Figurative constant `SPACE` / `SPACES`. */
  C_ORM_COBOL_VALUE_SPACES = 4,
  /** @brief Figurative constant `LOW-VALUE` / `LOW-VALUES` (`0x00`). */
  C_ORM_COBOL_VALUE_LOW_VALUES = 5,
  /** @brief Figurative constant `HIGH-VALUE` / `HIGH-VALUES` (`0xFF`). */
  C_ORM_COBOL_VALUE_HIGH_VALUES = 6
} c_orm_cobol_value_kind_t;

/**
 * @brief `COPY ... REPLACING ==from== BY ==to==` pseudo-text substitution pair.
 * @var from_text Source token or pseudo-text pattern (with or without `==`).
 * @var to_text Replacement token or pseudo-text string (with or without `==`).
 */
typedef struct c_orm_cobol_replace_pair {
  /** @brief Source token or pseudo-text pattern (e.g. `":PFX:"` or
   * `"==:PFX:=="`). */
  const char *from_text;
  /** @brief Replacement text (e.g. `"ACCT"` or `"==ACCT=="`). */
  const char *to_text;
} c_orm_cobol_replace_pair_t;

/**
 * @brief Single literal or `THRU` / `THROUGH` range entry on a level-`88`
 * condition item.
 * @var is_range Non-zero when `THRU` / `THROUGH` `high_val` is active.
 * @var is_numeric Non-zero when bounds are numeric literals.
 * @var low_val Single literal value or inclusive lower bound.
 * @var high_val Inclusive upper bound when `is_range != 0`, else NULL.
 */
typedef struct c_orm_cobol_value_range {
  /** @brief Non-zero when `THRU` / `THROUGH` upper bound is present. */
  int is_range;
  /** @brief Non-zero if literal is numeric rather than character. */
  int is_numeric;
  /** @brief Single literal value or lower bound string. */
  char *low_val;
  /** @brief Upper bound string for `THRU` ranges, or NULL. */
  char *high_val;
} c_orm_cobol_value_range_t;

/**
 * @brief Level-`88` COBOL condition-name AST node attached to a parent field.
 * @var cobol_name Original COBOL condition name (e.g. `"ACCT-OPEN"`).
 * @var c_name Normalized C snake_case identifier (e.g. `"acct_open"`).
 * @var values Array of single values or `THRU` ranges.
 * @var num_values Number of entries in `values`.
 */
typedef struct c_orm_cobol_condition {
  /** @brief Original COBOL condition name. */
  char *cobol_name;
  /** @brief Normalized C snake_case identifier. */
  char *c_name;
  /** @brief Array of value/range predicates. */
  c_orm_cobol_value_range_t *values;
  /** @brief Count of entries in `values`. */
  size_t num_values;
} c_orm_cobol_condition_t;

/**
 * @brief Generic C89 container layout for variable-length `OCCURS DEPENDING ON`
 * array fields in generated C models.
 * @var data Heap pointer to `length` contiguous element values.
 * @var length Active element count (`min_occurs .. max_occurs`).
 * @var capacity Allocated element capacity.
 */
typedef struct c_orm_cobol_odo_array {
  /** @brief Heap pointer to contiguous element instances. */
  void *data;
  /** @brief Active occurrence count. */
  size_t length;
  /** @brief Allocated element capacity. */
  size_t capacity;
} c_orm_cobol_odo_array_t;

/**
 * @brief AST node representing a COBOL group, elementary, or `66` `RENAMES`
 * field within a Copybook record.
 */
typedef struct c_orm_cobol_field {
  /** @brief COBOL level number (`1..49`, `66`, or `77`). */
  int level;
  /** @brief Non-zero if field is `FILLER` or unnamed. */
  int is_filler;
  /** @brief Original uppercase COBOL identifier (e.g. `"CUST-ACCT-BAL"`). */
  char *cobol_name;
  /** @brief Normalized C89 snake_case field name (e.g. `"cust_acct_bal"`). */
  char *c_name;
  /** @brief Normalized SQL uppercase column name (e.g. `"CUST_ACCT_BAL"`). */
  char *sql_name;
  /** @brief Raw `PIC` / `PICTURE` string (e.g. `"S9(9)V99"`), or NULL. */
  char *pic_raw;
  /** @brief Analyzed `PIC` category. */
  c_orm_cobol_pic_category_t pic_category;
  /** @brief Effective `USAGE` representation (including group inheritance). */
  c_orm_cobol_usage_t usage;
  /** @brief Total numeric digit positions (`int_digits + frac_digits`). */
  unsigned char total_digits;
  /** @brief Integer digit positions before implied `V`. */
  unsigned char int_digits;
  /** @brief Fractional digit positions after implied `V`. */
  unsigned char frac_digits;
  /** @brief Net `P` scaling positions (`>0` trailing `P`, `<0` leading `VP`).
   */
  signed char scale_p;
  /** @brief Character or symbol display width. */
  size_t display_length;
  /** @brief Non-zero if `PIC` includes leading `S` sign. */
  int is_signed;
  /** @brief Non-zero if `SIGN IS LEADING` (0 for trailing default). */
  int sign_leading;
  /** @brief Non-zero if `SEPARATE CHARACTER` sign byte is present. */
  int sign_separate;
  /** @brief Non-zero if `SYNC` / `SYNCHRONIZED` specified. */
  int is_synchronized;
  /** @brief Non-zero if `JUSTIFIED RIGHT` specified. */
  int is_justified_right;
  /** @brief Non-zero if `BLANK WHEN ZERO` specified. */
  int is_blank_when_zero;
  /** @brief Default `VALUE` literal kind. */
  c_orm_cobol_value_kind_t value_kind;
  /** @brief Default `VALUE` literal text when present, or NULL. */
  char *default_value;
  /** @brief Target COBOL field name of `REDEFINES` clause, or NULL. */
  char *redefines_name;
  /** @brief Resolved sibling pointer for `REDEFINES`, or NULL. */
  struct c_orm_cobol_field *redefines_target;
  /** @brief Starting target field name for level-`66` `RENAMES`, or NULL. */
  char *renames_from;
  /** @brief Ending target field name for level-`66` `RENAMES ... THRU`, or
   * NULL. */
  char *renames_thru;
  /** @brief Non-zero if an `OCCURS` array clause is present on this field. */
  int has_occurs;
  /** @brief Minimum occurrences (`max_occurs` for fixed `OCCURS N TIMES`). */
  size_t min_occurs;
  /** @brief Maximum occurrences (`>= 1` when `has_occurs != 0`). */
  size_t max_occurs;
  /** @brief Controlling COBOL field name for `OCCURS DEPENDING ON`, or NULL. */
  char *odo_field_name;
  /** @brief Non-zero if group is an IBM Db2 `DCLGEN` `49`-level `VARCHAR` /
   * `VARGRAPHIC` pair (`LEN PIC S9(4) COMP` + `TEXT PIC X/G(n)`). */
  int is_dclgen_varchar;
  /** @brief Non-zero when `is_dclgen_varchar` holds `PIC G(n)`/`N(n)` DBCS. */
  int is_dclgen_vargraphic;
  /** @brief Character capacity `n` of a `DCLGEN` `49`-level text subfield. */
  size_t dclgen_max_chars;
  /** @brief Non-zero if matched to a column in `EXEC SQL DECLARE ... TABLE`. */
  int has_sql_decl;
  /** @brief Non-zero if `EXEC SQL DECLARE` marked column `NOT NULL`. */
  int sql_not_null;
  /** @brief Non-zero if `EXEC SQL DECLARE` marked column `PRIMARY KEY`. */
  int sql_is_pk;
  /** @brief Non-zero if `EXEC SQL DECLARE` marked column `FOR BIT DATA`. */
  int sql_for_bit_data;
  /** @brief Starting 0-based byte offset within the static `01` record. */
  size_t byte_offset;
  /** @brief Wire byte length of a single occurrence of this field/group. */
  size_t element_byte_size;
  /** @brief Total static wire byte length (`element_byte_size * max_occurs`).
   */
  size_t total_byte_size;
  /** @brief Array of child fields for group items, or NULL for elementary. */
  struct c_orm_cobol_field *children;
  /** @brief Number of direct children in `children`. */
  size_t num_children;
  /** @brief Array of level-`88` condition items attached to this field. */
  c_orm_cobol_condition_t *conditions;
  /** @brief Number of level-`88` conditions in `conditions`. */
  size_t num_conditions;
} c_orm_cobol_field_t;

/**
 * @brief Top-level parsed COBOL Copybook `01` record AST and optional `DCLGEN`
 * SQL metadata.
 */
typedef struct c_orm_cobol_record {
  /** @brief COBOL `01` record name (e.g. `"ACCT-REC"` or `"DCLACCOUNTS"`). */
  char *record_name;
  /** @brief Generated C89 struct identifier (e.g. `"Acct_rec"`). */
  char *c_struct_name;
  /** @brief SQL table identifier (from `EXEC SQL DECLARE` or record name). */
  char *sql_table_name;
  /** @brief Optional SQL schema qualifier from `EXEC SQL DECLARE`, or NULL. */
  char *sql_schema_name;
  /** @brief Total maximum static byte length of the `01` wire record. */
  size_t total_byte_size;
  /** @brief Non-zero if an `EXEC SQL DECLARE ... TABLE` block was parsed. */
  int has_exec_sql_declare;
  /** @brief Top-level child fields under the `01` record. */
  c_orm_cobol_field_t *fields;
  /** @brief Number of top-level child fields in `fields`. */
  size_t num_fields;
  /** @brief Additional level-`77` standalone items in the copybook, or NULL. */
  c_orm_cobol_field_t *standalone_fields;
  /** @brief Number of level-`77` items in `standalone_fields`. */
  size_t num_standalone_fields;
  /** @brief Secondary `01` records (such as `DCLGEN` `01 IND-...`), or NULL. */
  struct c_orm_cobol_record *secondary_records;
  /** @brief Number of secondary `01` records in `secondary_records`. */
  size_t num_secondary_records;
} c_orm_cobol_record_t;

/**
 * @brief Parses a COBOL Copybook (`.cpy`) string into a layout-resolved
 * `c_orm_cobol_record_t` AST.
 *
 * @param cpy_source Null-terminated COBOL Copybook source text.
 * @param format Fixed, free, or auto-detected column format.
 * @param replacements Optional array of `REPLACING` pairs, or NULL.
 * @param num_replacements Number of pairs in `replacements` (0 if none).
 * @param out_record Output pointer receiving heap-allocated record AST.
 * @return C_ORM_OK on success, C_ORM_ERROR_NULL_POINTER,
 * C_ORM_ERROR_COPYBOOK_SYNTAX on invalid COBOL syntax, or C_ORM_ERROR_MEMORY.
 */
C_ORM_EXPORT c_orm_error_t c_orm_copybook_parse_string(
    const char *cpy_source, c_orm_cobol_format_t format,
    const c_orm_cobol_replace_pair_t *replacements, size_t num_replacements,
    c_orm_cobol_record_t **out_record);

/**
 * @brief Reads and parses a COBOL Copybook (`.cpy`) file from disk.
 *
 * @param file_path Path to the `.cpy` / `.cob` file.
 * @param format Fixed, free, or auto-detected column format.
 * @param replacements Optional array of `REPLACING` pairs, or NULL.
 * @param num_replacements Number of pairs in `replacements` (0 if none).
 * @param out_record Output pointer receiving heap-allocated record AST.
 * @return C_ORM_OK on success, C_ORM_ERROR_IO if unreadable,
 * C_ORM_ERROR_COPYBOOK_SYNTAX on malformed COBOL, or C_ORM_ERROR_MEMORY.
 */
C_ORM_EXPORT c_orm_error_t c_orm_copybook_parse_file(
    const char *file_path, c_orm_cobol_format_t format,
    const c_orm_cobol_replace_pair_t *replacements, size_t num_replacements,
    c_orm_cobol_record_t **out_record);

/**
 * @brief Recursively frees a `c_orm_cobol_record_t` AST and all owned strings,
 * child fields, conditions, and secondary records.
 *
 * @param record Record AST pointer to free (safe if NULL).
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_copybook_record_free(c_orm_cobol_record_t *record);

/**
 * @brief Looks up a field inside `rec` by COBOL name, C snake_case name, or
 * SQL column name (case-insensitively).
 *
 * @param rec Parsed Copybook record AST.
 * @param name Field identifier to locate.
 * @param out_field Output pointer receiving matched field pointer.
 * @return C_ORM_OK when found, C_ORM_ERROR_NOT_FOUND, or
 * C_ORM_ERROR_NULL_POINTER.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_copybook_find_field(const c_orm_cobol_record_t *rec, const char *name,
                          const c_orm_cobol_field_t **out_field);

/**
 * @brief Packs a hydrated `c-orm` C89 struct into a binary COBOL mainframe wire
 * record (`COMP-3`, `COMP`, `COMP-5`, Zoned Decimal, `EBCDIC`/`ASCII`, `UTF-16`
 * `GRAPHIC`, `DCLGEN` `49`-level `VARCHAR`, and `OCCURS DEPENDING ON`).
 *
 * @param rec Parsed COBOL Copybook record descriptor.
 * @param meta `c-orm` table metadata describing `c_struct` field offsets/types.
 * @param c_struct Source C struct instance to serialize.
 * @param cp Target wire character codepage (e.g. `C_ORM_CP_IBM037`).
 * @param big_endian_comp Non-zero for mainframe big-endian `COMP` / `GRAPHIC`.
 * @param out_buf Destination byte buffer receiving packed wire record.
 * @param buf_cap Capacity of `out_buf` in bytes.
 * @param out_record_len Optional output pointer receiving actual packed wire
 * byte length (reflecting active `OCCURS DEPENDING ON` counts), or NULL.
 * @return C_ORM_OK on success, or specific `c_orm_error_t` on overflow,
 * truncation, decimal overflow, or codepage error.
 */
C_ORM_EXPORT c_orm_error_t c_orm_copybook_pack_struct(
    const c_orm_cobol_record_t *rec, const c_orm_table_meta_t *meta,
    const void *c_struct, c_orm_codepage_t cp, int big_endian_comp,
    unsigned char *out_buf, size_t buf_cap, size_t *out_record_len);

/**
 * @brief Unpacks a raw binary COBOL mainframe wire record into a typed `c-orm`
 * C89 struct.
 *
 * @param rec Parsed COBOL Copybook record descriptor.
 * @param meta `c-orm` table metadata describing `out_c_struct` offsets/types.
 * @param raw_buf Source raw COBOL record bytes.
 * @param raw_len Length of `raw_buf` in bytes.
 * @param cp Source wire character codepage (e.g. `C_ORM_CP_IBM037`).
 * @param big_endian_comp Non-zero for mainframe big-endian `COMP` / `GRAPHIC`.
 * @param out_c_struct Destination C struct instance to populate.
 * @return C_ORM_OK on success, or specific `c_orm_error_t` on truncated buffer,
 * invalid BCD/zoned digits, codepage error, or allocation failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_copybook_unpack_struct(
    const c_orm_cobol_record_t *rec, const c_orm_table_meta_t *meta,
    const unsigned char *raw_buf, size_t raw_len, c_orm_codepage_t cp,
    int big_endian_comp, void *out_c_struct);

/**
 * @brief Packs a dynamic `cdd_c_abstract_struct_t` dictionary into a binary
 * COBOL mainframe wire record.
 *
 * @param rec Parsed COBOL Copybook record descriptor.
 * @param abs_struct Source abstract row dictionary.
 * @param cp Target wire character codepage.
 * @param big_endian_comp Non-zero for mainframe big-endian `COMP` / `GRAPHIC`.
 * @param out_buf Destination byte buffer.
 * @param buf_cap Capacity of `out_buf` in bytes.
 * @param out_record_len Optional output pointer receiving packed byte length.
 * @return C_ORM_OK on success, or error code on failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_copybook_pack_abstract(
    const c_orm_cobol_record_t *rec, const cdd_c_abstract_struct_t *abs_struct,
    c_orm_codepage_t cp, int big_endian_comp, unsigned char *out_buf,
    size_t buf_cap, size_t *out_record_len);

/**
 * @brief Unpacks a raw binary COBOL mainframe wire record into a dynamic
 * `cdd_c_abstract_struct_t` dictionary.
 *
 * @param rec Parsed COBOL Copybook record descriptor.
 * @param raw_buf Source raw COBOL record bytes.
 * @param raw_len Length of `raw_buf` in bytes.
 * @param cp Source wire character codepage.
 * @param big_endian_comp Non-zero for mainframe big-endian `COMP` / `GRAPHIC`.
 * @param out_abs_struct Destination initialized abstract row dictionary.
 * @return C_ORM_OK on success, or error code on failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_copybook_unpack_abstract(
    const c_orm_cobol_record_t *rec, const unsigned char *raw_buf,
    size_t raw_len, c_orm_codepage_t cp, int big_endian_comp,
    cdd_c_abstract_struct_t *out_abs_struct);

/**
 * @brief Validates that every mapped field in `c_struct` with attached level-
 * `88` condition items satisfies at least one of its declared `88` value or
 * `THRU` range domains.
 *
 * @param rec Parsed COBOL Copybook record descriptor.
 * @param meta `c-orm` table metadata for `c_struct`.
 * @param c_struct Hydrated C struct to validate.
 * @return C_ORM_OK if all `88`-constrained fields are valid,
 * C_ORM_ERROR_VALIDATION if any field violates its `88` domain, or
 * C_ORM_ERROR_NULL_POINTER.
 */
C_ORM_EXPORT c_orm_error_t c_orm_copybook_validate_88(
    const c_orm_cobol_record_t *rec, const c_orm_table_meta_t *meta,
    const void *c_struct);

/**
 * @brief Evaluates a specific named level-`88` condition item against a
 * hydrated C struct.
 *
 * @param rec Parsed COBOL Copybook record descriptor.
 * @param meta `c-orm` table metadata for `c_struct`.
 * @param c_struct Hydrated C struct to inspect.
 * @param condition_name COBOL or C name of the level-`88` condition.
 * @param out_matched Output pointer set to 1 if condition holds, 0 otherwise.
 * @return C_ORM_OK on success, C_ORM_ERROR_NOT_FOUND if condition does not
 * exist, or C_ORM_ERROR_NULL_POINTER.
 */
C_ORM_EXPORT c_orm_error_t c_orm_copybook_check_condition(
    const c_orm_cobol_record_t *rec, const c_orm_table_meta_t *meta,
    const void *c_struct, const char *condition_name, int *out_matched);

/**
 * @brief Emits the C89 struct, `REDEFINES` unions, `OCCURS` arrays, level-`88`
 * predicate prototypes, and `c-orm` metadata declarations for `rec` (without
 * outer `extern "C"` wrapper, suitable for embedding in `Models.h`).
 *
 * @param fp Open writable `FILE` stream.
 * @param rec Parsed Copybook record AST.
 * @return C_ORM_OK on success, or error code on invalid args / I/O failure.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_copybook_emit_c_header_body(FILE *fp, const c_orm_cobol_record_t *rec);

/**
 * @brief Emits a standalone strict ISO C89 header file (`.h`) with 100% Doxygen
 * comments, single `clang-format` include guard, and single `__cplusplus`
 * wrapper for a parsed COBOL Copybook record.
 *
 * @param fp Open writable `FILE` stream.
 * @param rec Parsed Copybook record AST.
 * @return C_ORM_OK on success, or error code on invalid args / I/O failure.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_copybook_emit_c_header(FILE *fp, const c_orm_cobol_record_t *rec);

/**
 * @brief Emits a strict ISO C89 implementation source file (`.c`) containing
 * `init`, `free`, `deepcopy`, `pack`, `unpack`, `validate`, level-`88`
 * predicates, and `c_orm_table_meta_t` descriptors for `rec`.
 *
 * @param fp Open writable `FILE` stream.
 * @param rec Parsed Copybook record AST.
 * @param header_name Header filename to `#include` (e.g. `"Models.h"`).
 * @return C_ORM_OK on success, or error code on invalid args / I/O failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_copybook_emit_c_source(
    FILE *fp, const c_orm_cobol_record_t *rec, const char *header_name);

/**
 * @brief Writes `Models.h` and `Models.c` into `output_dir` for a parsed COBOL
 * Copybook record.
 *
 * @param rec Parsed Copybook record AST.
 * @param output_dir Target directory path.
 * @return C_ORM_OK on success, or error code on I/O or allocation failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_copybook_generate_c_files(
    const c_orm_cobol_record_t *rec, const char *output_dir);

/**
 * @brief Generates a dialect-specific SQL `CREATE TABLE` DDL statement from a
 * parsed COBOL Copybook record, including `DECIMAL(p, s)`, `SMALLINT`/`INTEGER`
 * /`BIGINT`, `CHAR(n)`, `VARCHAR(n)` (`DCLGEN` `49`-level), `GRAPHIC(n)`,
 * `FOR BIT DATA`, and `CHECK` constraints synthesized from level-`88` values.
 *
 * @param rec Parsed Copybook record AST.
 * @param dialect Target SQL DDL dialect (e.g. `C_TO_SQL_DIALECT_DB2`).
 * @param out_sql Output pointer receiving heap-allocated SQL DDL string.
 * @return C_ORM_OK on success, or error code on invalid arguments / OOM.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_copybook_emit_sql_ddl(const c_orm_cobol_record_t *rec,
                            c_to_sql_dialect_t dialect, char **out_sql);

/**
 * @brief Generates a COBOL Copybook (`.cpy`) — optionally formatted as an IBM
 * Db2 `DCLGEN` (`EXEC SQL DECLARE ... TABLE` + `01 DCL<TABLE>` with `49`-level
 * `VARCHAR`/`VARGRAPHIC` pairs + `01 IND-<TABLE>` indicator array) — from a
 * `c_orm_table_meta_t` descriptor.
 *
 * @param meta Source `c-orm` table metadata descriptor.
 * @param format Output COBOL column format (`FIXED` 80-col or `FREE`).
 * @param emit_dclgen Non-zero to emit IBM Db2 `DCLGEN` wrapper structures.
 * @param out_cpy Output pointer receiving heap-allocated `.cpy` source text.
 * @return C_ORM_OK on success, or error code on invalid arguments / OOM.
 */
C_ORM_EXPORT c_orm_error_t c_orm_meta_to_copybook_emit(
    const c_orm_table_meta_t *meta, c_orm_cobol_format_t format,
    int emit_dclgen, char **out_cpy);

/**
 * @brief Generates a COBOL Copybook (`.cpy`) or IBM Db2 `DCLGEN` copybook from
 * one or more SQL `CREATE TABLE` DDL statements.
 *
 * @param sql_ddl Null-terminated SQL `CREATE TABLE` DDL string.
 * @param format Output COBOL column format (`FIXED` 80-col or `FREE`).
 * @param emit_dclgen Non-zero to emit IBM Db2 `DCLGEN` structures.
 * @param out_cpy Output pointer receiving heap-allocated `.cpy` source text.
 * @return C_ORM_OK on success, or error code on SQL parse error / OOM.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_sql_to_copybook_emit(const char *sql_ddl, c_orm_cobol_format_t format,
                           int emit_dclgen, char **out_cpy);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* C_ORM_COPYBOOK_H */
