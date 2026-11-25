/**
 * @file c_orm_sql.h
 * @brief Parses SQL DDL into an AST.
 */

#ifndef C_CDD_SQL_PARSER_H
#define C_CDD_SQL_PARSER_H

/* clang-format off */
#include "c_orm_db.h"
#include <stddef.h>
#include <c_str_span.h>
/* clang-format on */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*
 * Phase 1: AST Node Structs
 */

/**
 * @brief Represents SQL Data Types.
 */
enum SqlDataType {
  SQL_TYPE_UNKNOWN,
  SQL_TYPE_INT,
  SQL_TYPE_BIGINT,
  SQL_TYPE_VARCHAR,
  SQL_TYPE_TEXT,
  SQL_TYPE_CHAR,
  SQL_TYPE_FLOAT,
  SQL_TYPE_DOUBLE,
  SQL_TYPE_DECIMAL,
  SQL_TYPE_BOOLEAN,
  SQL_TYPE_DATE,
  SQL_TYPE_TIMESTAMP,
  SQL_TYPE_BLOB,
  SQL_TYPE_SMALLINT,   /**< 16-bit signed integer (`SMALLINT`) */
  SQL_TYPE_NUMERIC,    /**< Exact fixed-point numeric (`NUMERIC(p, s)`) */
  SQL_TYPE_DECFLOAT,   /**< Db2 IEEE 754r decimal float (`DECFLOAT(16/34)`) */
  SQL_TYPE_GRAPHIC,    /**< Fixed-length DBCS/UTF-16 (`GRAPHIC(n)`) */
  SQL_TYPE_VARGRAPHIC, /**< Variable-length DBCS/UTF-16 (`VARGRAPHIC(n)`) */
  SQL_TYPE_CLOB,       /**< Character Large Object (`CLOB`) */
  SQL_TYPE_DBCLOB,     /**< Double-Byte Character Large Object (`DBCLOB`) */
  SQL_TYPE_BINARY,     /**< Fixed-length binary (`BINARY(n)` / `CHAR FOR BIT
                          DATA`) */
  SQL_TYPE_VARBINARY,  /**< Variable-length binary (`VARBINARY(n)` / `VARCHAR
                          FOR BIT DATA`) */
  SQL_TYPE_TIME        /**< SQL `TIME` (`HH:MM:SS`) */
};

/**
 * @brief Represents a SQL Column Constraint Type.
 */
enum SqlConstraintType {
  SQL_CONSTRAINT_NONE,
  SQL_CONSTRAINT_PRIMARY_KEY,
  SQL_CONSTRAINT_FOREIGN_KEY,
  SQL_CONSTRAINT_UNIQUE,
  SQL_CONSTRAINT_NOT_NULL,
  SQL_CONSTRAINT_DEFAULT
};

/**
 * @brief Represents a single SQL Constraint.
 */
struct sql_constraint_t {
  /** @brief Constraint type discriminator */
  enum SqlConstraintType type;
  char *column_name;      /**< Local column name for table-level FOREIGN KEY */
  char *reference_table;  /**< For FOREIGN KEY */
  char *reference_column; /**< For FOREIGN KEY */
  char *default_value;    /**< For DEFAULT */
  char **columns;         /**< For table-level constraints */
  /** @brief Number of columns in table-level constraint */
  size_t n_columns;
};

/**
 * @brief Represents a single SQL Column definition.
 */
struct sql_column_t {
  /** @brief Column identifier name */
  char *name;
  /** @brief Parsed SQL data type */
  enum SqlDataType type;
  int length; /**< For VARCHAR(n) or DECIMAL(p, s) precision, -1 if unspecified
               */
  int scale;  /**< Fractional scale for DECIMAL(p, s) / NUMERIC(p, s), -1 if
                 unspecified */
  int is_identity;      /**< Non-zero if GENERATED ALWAYS/BY DEFAULT AS IDENTITY
                         */
  int is_for_bit_data;  /**< Non-zero if FOR BIT DATA specified */
  unsigned short ccsid; /**< Parsed CCSID (0 if unspecified, 37=EBCDIC,
                           1208=UNICODE, 367=ASCII, 65535=FOR BIT DATA) */
  /** @brief Array of column constraints */
  struct sql_constraint_t *constraints;
  /** @brief Number of column constraints */
  size_t n_constraints;
};

/**
 * @brief Represents a SQL Table definition.
 */
struct sql_table_t {
  /** @brief Table identifier name */
  char *name;
  /** @brief Array of column definitions */
  struct sql_column_t *columns;
  /** @brief Number of column definitions */
  size_t n_columns;
  struct sql_constraint_t *table_constraints; /**< Table-level constraints */
  /** @brief Number of table-level constraints */
  size_t n_table_constraints;
};

/*
 * Phase 1: Lexer & Tokenizer
 */

/**
 * @brief Classification of SQL lexical elements.
 */
enum SqlTokenKind {
  SQL_TOKEN_EOF,
  SQL_TOKEN_WHITESPACE,
  SQL_TOKEN_IDENTIFIER,
  SQL_TOKEN_KEYWORD,
  SQL_TOKEN_NUMBER,
  SQL_TOKEN_STRING,
  SQL_TOKEN_LPAREN,
  SQL_TOKEN_RPAREN,
  SQL_TOKEN_COMMA,
  SQL_TOKEN_SEMICOLON,
  SQL_TOKEN_UNKNOWN
};

/**
 * @brief A single SQL lexical unit.
 */
struct sql_token_t {
  /** @brief Token lexical kind */
  enum SqlTokenKind kind;
  /** @brief Pointer to start of token in source buffer */
  const char *start;
  /** @brief Length of token in bytes */
  size_t length;
};

/**
 * @brief A list of SQL tokens.
 */
struct sql_token_list_t {
  /** @brief Dynamic array of SQL tokens */
  struct sql_token_t *tokens;
  /** @brief Number of tokens in array */
  size_t size;
  /** @brief Allocated capacity of tokens array */
  size_t capacity;
};

/**
 * @brief Error information returned by the SQL parser.
 */
struct sql_parse_error_t {
  /** @brief Diagnostic parse error message */
  const char *message;
  /** @brief Offending SQL token pointer, if available */
  const struct sql_token_t *token;
};

/**
 * @brief Lex a SQL string into a token list.
 *
 * @param source The SQL source text.
 * @param out_list Pointer to pointer of list to allocate and populate.
 * @return 0 on success, non-zero on error.
 */
C_ORM_EXPORT c_orm_error_t sql_lex(az_span source,
                                   struct sql_token_list_t **out_list);

/**
 * @brief Configure simulated parser allocation failure for testing.
 *
 * @param count Number of successful allocations before failure.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_parser_set_fail(int count);

/**
 * @brief Parse a SQL CREATE TABLE statement.
 *
 * @param list The token list generated by sql_lex.
 * @param out_table Pointer to pointer to allocate and populate with AST.
 * @param out_error Pointer to populate with error info on failure.
 * @return 0 on success, non-zero on error.
 */
C_ORM_EXPORT c_orm_error_t sql_parse_table(const struct sql_token_list_t *list,
                                           struct sql_table_t **out_table,
                                           struct sql_parse_error_t *out_error);

/**
 * @brief Free a SQL token list.
 *
 * @param list The list to free.
 * @return 0 on success.
 */
C_ORM_EXPORT c_orm_error_t sql_token_list_free(struct sql_token_list_t *list);

/**
 * @brief Free a SQL table AST node's internal allocations.
 *
 * @param table The table to free.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t sql_table_free(struct sql_table_t *table);

#define sql_table_C_ORM_FREE sql_table_free

/**
 * @brief Parse full SQL DDL file returning all tables.
 *
 * @param sql_data SQL source text
 * @param out_tables Pointer to receive array of tables
 * @param out_n_tables Pointer to receive count
 * @return 0 on success
 */
C_ORM_EXPORT c_orm_error_t parse_sql_ddl(const char *sql_data,
                                         struct sql_table_t **out_tables,
                                         size_t *out_n_tables);

/* Forward declaration for query projection to avoid circular include */
struct CddCQueryProjection;

/**
 * @brief Parse a SQL SELECT statement into a query projection.
 *
 * @param list The token list generated by sql_lex.
 * @param out_proj Pointer to pointer to allocate and populate with AST.
 * @param out_error Pointer to populate with error info on failure.
 * @return 0 on success, non-zero on error.
 */
C_ORM_EXPORT c_orm_error_t sql_parse_select(
    const struct sql_token_list_t *list, struct CddCQueryProjection **out_proj,
    struct sql_parse_error_t *out_error);

/**
 * @brief Parse a SQL RETURNING clause into a query projection.
 *
 * @param list The token list generated by sql_lex.
 * @param out_proj Pointer to pointer to allocate and populate with AST.
 * @param out_error Pointer to populate with error info on failure.
 * @return 0 on success, non-zero on error.
 */
C_ORM_EXPORT c_orm_error_t sql_parse_returning(
    const struct sql_token_list_t *list, struct CddCQueryProjection **out_proj,
    struct sql_parse_error_t *out_error);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* C_CDD_SQL_PARSER_H */
