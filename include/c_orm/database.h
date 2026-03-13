/**
 * @file database.h
 * @brief Intermediate Representation (IR) mapping for relational databases.
 *
 * Defines the AST structures needed for parsing C structs into Database
 * schemas.
 */

#ifndef C_CDD_DATABASE_H
#define C_CDD_DATABASE_H

#ifdef __cplusplus
extern "C" {
#endif

/* clang-format off */
#include <stddef.h>
/* clang-format on */

/**
 * @brief Represents the data type of a column.
 */
enum DatabaseColumnType {
  DB_COL_TYPE_UNKNOWN, /**< Unknown type. */
  DB_COL_TYPE_INTEGER, /**< Integer type. */
  DB_COL_TYPE_VARCHAR, /**< Variable length character string. */
  DB_COL_TYPE_TEXT,    /**< Text type. */
  DB_COL_TYPE_REAL,    /**< Floating point type. */
  DB_COL_TYPE_BLOB,    /**< Binary large object. */
  DB_COL_TYPE_BOOLEAN, /**< Boolean type. */
  DB_COL_TYPE_DATE,    /**< Date type. */
  DB_COL_TYPE_DATETIME /**< Date and time type. */
};

/**
 * @brief Represents the type of a foreign key constraint action.
 */
enum DatabaseForeignKeyAction {
  DB_FK_ACTION_NONE = 0,    /**< No action. */
  DB_FK_ACTION_CASCADE,     /**< Cascade the action. */
  DB_FK_ACTION_SET_NULL,    /**< Set the column to NULL. */
  DB_FK_ACTION_SET_DEFAULT, /**< Set the column to its default value. */
  DB_FK_ACTION_RESTRICT     /**< Restrict the action. */
};

/**
 * @brief Represents a single column in a database table.
 */
struct DatabaseColumn {
  char *name;                   /**< Name of the column. */
  enum DatabaseColumnType type; /**< Data type of the column. */
  int is_primary_key;           /**< 1 if primary key, 0 otherwise. */
  int is_nullable;              /**< 1 if nullable, 0 otherwise. */
  int is_unique;                /**< 1 if unique, 0 otherwise. */
  char *default_value;          /**< Default value as string, or NULL. */
  char *foreign_key_table;      /**< Table this column references, or NULL. */
  char *foreign_key_column;     /**< Column this column references, or NULL. */
  enum DatabaseForeignKeyAction
      on_delete; /**< Action on deletion of referenced row. */
  enum DatabaseForeignKeyAction
      on_update; /**< Action on update of referenced row. */
};

/**
 * @brief Represents a database table.
 */
struct DatabaseTable {
  char *name;                     /**< Name of the table. */
  struct DatabaseColumn *columns; /**< Array of columns. */
  size_t n_columns;               /**< Number of columns. */
};

/**
 * @brief Represents a complete database schema.
 */
struct DatabaseSchema {
  char *name;                   /**< Name of the schema/database. */
  struct DatabaseTable *tables; /**< Array of tables. */
  size_t n_tables;              /**< Number of tables. */
};

/**
 * @brief Initialize a DatabaseSchema.
 * @param schema The schema to initialize.
 */
void db_schema_init(struct DatabaseSchema *schema);

/**
 * @brief Free resources associated with a DatabaseSchema.
 * @param schema The schema to free.
 */
void db_schema_free(struct DatabaseSchema *schema);

#if defined(__cplusplus)
}
#endif /* __cplusplus */

#endif /* C_CDD_DATABASE_H */
