# COBOL Copybook Interoperability, `COMP-3` & `EBCDIC` Wire Serialization

`c-orm` includes a zero-dependency, pure ISO C89 COBOL Copybook (`.cpy`) lexer/parser, mainframe binary record serializer (`COMP-3`, Zoned Decimal, Big-Endian `COMP`, `EBCDIC`, `GRAPHIC`, `OCCURS DEPENDING ON`), and bidirectional Copybook <-> C89 / SQL DDL / IBM Db2 `DCLGEN` code generator.

---

## 1. Supported COBOL Copybook Language Features (`include/c_orm_copybook.h`)

- **Source Formats (`c_orm_cobol_format_t`)**:
  - **Fixed 80-Column Format (`C_ORM_COBOL_FORMAT_FIXED`)**: Strips sequence area (cols 1–6) and identification area (cols 73–80), honors indicator column 7 (`*` and `/` comments, `D` debug lines, `-` unclosed literal continuations).
  - **Free Format (`C_ORM_COBOL_FORMAT_FREE`)**: Supports `*>` inline comments and unrestricted line width.
  - **Auto-Detection (`C_ORM_COBOL_FORMAT_AUTO`)**: Detects fixed vs. free format automatically and honors inline `>>SOURCE FORMAT FREE` and `>>SOURCE FORMAT FIXED` compiler directives.
- **Parameterized Copybooks (`REPLACING`)**:
  - Supports inline `REPLACING ==:TAG:== BY ==ACCT==.` directives as well as programmatic `c_orm_cobol_replace_pair_t` arrays and CLI `--replacing OLD=NEW` flags.
- **Data Hierarchy & Clauses**:
  - Levels `01`–`49` (nested elementary and group items), standalone `77` items, `66 RENAMES ... [THRU ...]`, and `88` condition names (single values, value lists, and `VALUE low THRU high` ranges).
  - `PIC` / `PICTURE` clauses: `X`, `A`, `9`, `S`, `V` (implied decimal), `P` (scaling positions), `G` / `N` (DBCS / National UTF-16), and numeric/alphanumeric edited pictures.
  - `USAGE` clauses: `DISPLAY` (Zoned Decimal / alphanumeric), `COMP` / `COMP-4` / `BINARY` (2/4/8-byte big-endian binary), `COMP-5` (native-endian binary), `COMP-3` / `PACKED-DECIMAL` (BCD nybbles), `COMP-1` (`float`), `COMP-2` (`double`), and `DISPLAY-1` / `NATIONAL` (UTF-16).
  - `SIGN IS LEADING / TRAILING [SEPARATE CHARACTER]`, `SYNCHRONIZED`, `JUSTIFIED RIGHT`, `BLANK WHEN ZERO`, and `VALUE` literals (`ZEROS`, `SPACES`, `LOW-VALUES`, `HIGH-VALUES`, strings, numbers, hex `X'...'`).
  - `REDEFINES` (overlapping memory offsets mapped to C89 `union`s).
  - `OCCURS n TIMES` and variable-length `OCCURS min TO max TIMES DEPENDING ON <field>` (ODO arrays).
- **IBM Db2 `DCLGEN` Integration**:
  - Recognizes `EXEC SQL DECLARE <schema>.<table> TABLE (...) END-EXEC.` blocks to extract SQL `PRIMARY KEY`, `NOT NULL`, and `FOR BIT DATA` metadata.
  - Automatically collapses Db2 `49`-level length-prefixed host groups (`49 COL-LEN PIC S9(4) USAGE COMP.` + `49 COL-TEXT PIC X(n).` / `PIC G(n).`) into `VARCHAR(n)` / `VARGRAPHIC(n)` columns!

---

## 2. Lossless Mainframe Numeric & EBCDIC Wire Formats

### Fixed-Point Decimal & `COMP-3` / Zoned / `COMP` (`include/c_orm_decimal.h`)
`c_orm_decimal_t` is a stack-allocated exact base-10 value type (`1..38` digits, zero heap allocations, zero IEEE-754 binary float drift):
- **Arithmetic & Rescaling**: `c_orm_decimal_add`, `sub`, `mul`, `div`, `cmp`, and `rescale` supporting `C_ORM_ROUND_TRUNCATE` (COBOL default), `C_ORM_ROUND_HALF_UP` (COBOL `ROUNDED`), `C_ORM_ROUND_HALF_EVEN` (Db2 Banker's rounding), and `C_ORM_ROUND_ERROR_IF_INEXACT`.
- **Packed Decimal (`COMP-3`)**: `c_orm_decimal_to_comp3` / `c_orm_decimal_from_comp3` (`(digits / 2) + 1` bytes; emits `0x0C` positive, `0x0D` negative, `0x0F` unsigned; accepts IBM z/Architecture `0x0A`/`0x0C`/`0x0E`/`0x0F` positive and `0x0B`/`0x0D` negative nybbles).
- **Zoned Decimal (`DISPLAY`)**: `c_orm_decimal_to_zoned` / `c_orm_decimal_from_zoned` for ASCII and EBCDIC leading/trailing overpunched (`{`, `A`–`I`, `}`, `J`–`R`) and separate (`+`/`-`) signs.
- **COBOL Binary (`COMP` / `COMP-5`)**: `c_orm_cobol_comp_byte_len` (`1..4` digits -> 2B, `5..9` -> 4B, `10..18` -> 8B) and `c_orm_int64_to_cobol_comp` / `c_orm_int64_from_cobol_comp`.

### EBCDIC & DBCS `GRAPHIC` Transcoding (`include/c_orm_ebcdic.h`)
Pure C89 bijective lookup tables for mainframe codepages:
- `C_ORM_CP_IBM037` (CCSID 37 — z/OS MVS, OS/400, Db2 for z/OS default)
- `C_ORM_CP_IBM1047` (CCSID 1047 — z/OS USS C/COBOL compiler codepage, preserving `[` `]` `{` `}` `^` `~` swaps vs CP037)
- `C_ORM_CP_IBM500` (CCSID 500 — International EBCDIC)
- `C_ORM_CP_IBM1140` (CCSID 1140 — CP037 + Euro `U+20AC` at `0x9F`)
- Fixed-width `0x40` EBCDIC / `0x20` ASCII padding & trimming (`c_orm_cobol_format_alpha`, `c_orm_cobol_extract_alpha`) and UTF-8 <-> UTF-16BE/LE (`c_orm_utf8_to_utf16`, `c_orm_utf16_to_utf8`).

---

## 3. Binary COBOL Record Pack / Unpack API

Marshal directly between C89 `c-orm` structs (or dynamic `cdd_c_abstract_struct_t` rows) and raw mainframe byte buffers (CICS `COMMAREA`, MQ payloads, sequential datasets):

```c
c_orm_cobol_record_t *rec = NULL;
unsigned char wire_buf[128];
size_t wire_len = 0;
c_orm_error_t rc;

rc = c_orm_copybook_parse_file("ACCTREC.cpy", C_ORM_COBOL_FORMAT_AUTO, NULL, 0, &rec);

/* Unpack raw IBM037 EBCDIC + big-endian COMP + COMP-3 wire bytes into C struct */
rc = c_orm_copybook_unpack_struct(rec, &Acct_rec_meta, raw_mainframe_bytes, 31,
                                  C_ORM_CP_IBM037, 1, &acct);

/* Validate all level-88 domain conditions on the hydrated struct */
rc = c_orm_copybook_validate_88(rec, &Acct_rec_meta, &acct);

/* Repack updated struct back into mainframe wire format */
rc = c_orm_copybook_pack_struct(rec, &Acct_rec_meta, &acct,
                                C_ORM_CP_IBM037, 1,
                                wire_buf, sizeof(wire_buf), &wire_len);

rc = c_orm_copybook_record_free(rec);
```

See [`examples/cobol_db2_ledger.c`](../examples/cobol_db2_ledger.c) for a complete, runnable financial ledger workflow.

---

## 4. Bidirectional CLI Commands (`c-orm-cli`)

### Copybook -> C89 ORM Models (`cpy2c`)
Generates strict ISO C89 `Models.h` and `Models.c` with `c_orm_table_meta_t` reflection tables, `init`/`free`/`deepcopy`, `pack`/`unpack`, and level-`88` predicate/validation helpers:
```sh
c-orm-cli cpy2c ACCT.cpy ./gen_models --format fixed --replacing :PFX:=ACCT
```
*(Note: `c_orm_codegen_generate` and `c-orm-cli sql2c` also auto-detect `.cpy` and `.cob` extensions!)*

### Copybook -> SQL DDL (`cpy2sql`)
Generates `CREATE TABLE` DDL (`db2`, `odbc`, `sqlite`, `postgres`, or `mysql`) including `DECIMAL(p, s)`, `CHAR(n) FOR BIT DATA`, `GRAPHIC(n)`, and `CHECK` constraints synthesized from level-`88` values:
```sh
c-orm-cli cpy2sql ACCT.cpy schema_db2.sql --dialect db2 --replacing :PFX:=ACCT
```

### SQL DDL -> COBOL Copybook / IBM Db2 `DCLGEN` (`sql2cpy`)
Generates 80-column fixed-format (or free-format) COBOL Copybooks, optionally emitting IBM Db2 `EXEC SQL DECLARE ... END-EXEC.`, `01 DCL<TABLE>.` host structures with `49`-level `VARCHAR` groups, and `01 IND-<TABLE>.` null-indicator arrays:
```sh
c-orm-cli sql2cpy schema_db2.sql DCLACCT.cpy --dclgen --format fixed
```
