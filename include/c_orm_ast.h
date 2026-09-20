#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file c_orm_ast.h
 * @brief Dynamic SQL Query Abstract Syntax Tree (AST).
 */

#ifndef C_ORM_AST_H
#define C_ORM_AST_H

/* clang-format off */
#include "c_orm_api.h"
#include <stddef.h>
/* clang-format on */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @brief Node types for the Query AST.
 */
typedef enum {
  C_ORM_AST_NODE_SELECT,
  C_ORM_AST_NODE_FROM,
  C_ORM_AST_NODE_WHERE,
  C_ORM_AST_NODE_JOIN,
  C_ORM_AST_NODE_GROUP_BY,
  C_ORM_AST_NODE_HAVING,
  C_ORM_AST_NODE_ORDER_BY,
  C_ORM_AST_NODE_LIMIT,
  C_ORM_AST_NODE_OFFSET,
  C_ORM_AST_NODE_LITERAL,
  C_ORM_AST_NODE_OPERATOR,
  C_ORM_AST_NODE_RAW,
  C_ORM_AST_NODE_COLUMN,
  C_ORM_AST_NODE_GROUP,
  C_ORM_AST_NODE_SUBQUERY,
  C_ORM_AST_NODE_UNION,
  C_ORM_AST_NODE_WITH,
  C_ORM_AST_NODE_FUNCTION,
  C_ORM_AST_NODE_CAST,
  C_ORM_AST_NODE_BETWEEN,
  C_ORM_AST_NODE_EXISTS,
  C_ORM_AST_NODE_WINDOW
} c_orm_ast_node_type_t;

/**
 * @brief Base structure for all AST nodes.
 * @var type Node discriminant type.
 * @var next Pointer to next sibling AST node.
 */
typedef struct c_orm_ast_node {
  /** @brief Node discriminant type. */
  c_orm_ast_node_type_t type;
  /** @brief Pointer to next sibling AST node. */
  struct c_orm_ast_node *next; /* Sibling linked list */
} c_orm_ast_node_t;

/**
 * @brief Select AST Node
 * @var base Base node metadata.
 * @var columns Comma separated column list or projection string.
 * @var is_distinct Non-zero if DISTINCT modifier applies.
 */
typedef struct c_orm_ast_select {
  /** @brief Base node metadata. */
  c_orm_ast_node_t base;
  /** @brief Comma separated column list or projection string. */
  const char *columns; /* Comma separated or list, keeping it simple as a string
                          for now */
  /** @brief Non-zero if DISTINCT modifier applies. */
  int is_distinct;
} c_orm_ast_select_t;

/**
 * @brief From AST Node
 * @var base Base node metadata.
 * @var table Name of the table.
 * @var alias Optional alias for the table.
 */
typedef struct c_orm_ast_from {
  /** @brief Base node metadata. */
  c_orm_ast_node_t base;
  /** @brief Name of the table. */
  const char *table;
  /** @brief Optional alias for the table. */
  const char *alias;
} c_orm_ast_from_t;

/**
 * @brief Where AST Node
 * @var base Base node metadata.
 * @var condition Root condition expression AST node.
 */
typedef struct c_orm_ast_where {
  /** @brief Base node metadata. */
  c_orm_ast_node_t base;
  /** @brief Root condition expression AST node. */
  struct c_orm_ast_node *condition;
} c_orm_ast_where_t;

/**
 * @brief Join AST Node
 * @var base Base node metadata.
 * @var type_str Join type string (e.g. INNER, LEFT, RIGHT).
 * @var table Target join table name.
 * @var on_condition Join condition expression AST node.
 */
typedef struct c_orm_ast_join {
  /** @brief Base node metadata. */
  c_orm_ast_node_t base;
  /** @brief Join type string. */
  const char *type_str; /* "INNER", "LEFT", "RIGHT" */
  /** @brief Target join table name. */
  const char *table;
  /** @brief Join condition expression AST node. */
  struct c_orm_ast_node *on_condition;
} c_orm_ast_join_t;

/**
 * @brief Group By AST Node
 * @var base Base node metadata.
 * @var columns Comma-separated grouping column list.
 */
typedef struct c_orm_ast_group_by {
  /** @brief Base node metadata. */
  c_orm_ast_node_t base;
  /** @brief Comma-separated grouping column list. */
  const char *columns;
} c_orm_ast_group_by_t;

/**
 * @brief Having AST Node
 * @var base Base node metadata.
 * @var condition Having filter condition expression AST node.
 */
typedef struct c_orm_ast_having {
  /** @brief Base node metadata. */
  c_orm_ast_node_t base;
  /** @brief Having filter condition expression AST node. */
  struct c_orm_ast_node *condition;
} c_orm_ast_having_t;

/**
 * @brief Order By AST Node
 * @var base Base node metadata.
 * @var column Sort column expression.
 * @var is_desc Non-zero if descending sort order.
 */
typedef struct c_orm_ast_order_by {
  /** @brief Base node metadata. */
  c_orm_ast_node_t base;
  /** @brief Sort column expression. */
  const char *column;
  /** @brief Non-zero if descending sort order. */
  int is_desc;
} c_orm_ast_order_by_t;

/**
 * @brief Limit AST Node
 * @var base Base node metadata.
 * @var limit Maximum number of records to return.
 */
typedef struct c_orm_ast_limit {
  /** @brief Base node metadata. */
  c_orm_ast_node_t base;
  /** @brief Maximum number of records to return. */
  size_t limit;
} c_orm_ast_limit_t;

/**
 * @brief Offset AST Node
 * @var base Base node metadata.
 * @var offset Number of initial records to skip.
 */
typedef struct c_orm_ast_offset {
  /** @brief Base node metadata. */
  c_orm_ast_node_t base;
  /** @brief Number of initial records to skip. */
  size_t offset;
} c_orm_ast_offset_t;

/**
 * @brief Literal value AST node
 * @var base Base node metadata.
 * @var value String representation of the literal value.
 * @var is_string Non-zero if value is quoted string.
 */
typedef struct c_orm_ast_literal {
  /** @brief Base node metadata. */
  c_orm_ast_node_t base;
  /** @brief String representation of literal value. */
  const char *value;
  /** @brief Non-zero if value is quoted string. */
  int is_string;
} c_orm_ast_literal_t;

/**
 * @brief Operator AST Node (e.g. '=', '>', 'AND', 'OR')
 * @var base Base node metadata.
 * @var op Operator symbol string.
 * @var left Left operand AST node.
 * @var right Right operand AST node.
 */
typedef struct c_orm_ast_operator {
  /** @brief Base node metadata. */
  c_orm_ast_node_t base;
  /** @brief Operator symbol string. */
  const char *op;
  /** @brief Left operand AST node. */
  struct c_orm_ast_node *left;
  /** @brief Right operand AST node. */
  struct c_orm_ast_node *right;
} c_orm_ast_operator_t;

/**
 * @brief Raw SQL fallback node
 * @var base Base node metadata.
 * @var sql Raw SQL expression string.
 */
typedef struct c_orm_ast_raw {
  /** @brief Base node metadata. */
  c_orm_ast_node_t base;
  /** @brief Raw SQL expression string. */
  const char *sql;
} c_orm_ast_raw_t;

/**
 * @brief Column reference AST Node
 * @var base Base node metadata.
 * @var name Column name or qualified identifier.
 */
typedef struct c_orm_ast_column {
  /** @brief Base node metadata. */
  c_orm_ast_node_t base;
  /** @brief Column name or qualified identifier. */
  const char *name;
} c_orm_ast_column_t;

/**
 * @brief Group (Parentheses) AST Node
 * @var base Base node metadata.
 * @var expr Enclosed inner expression AST node.
 */
typedef struct c_orm_ast_group {
  /** @brief Base node metadata. */
  c_orm_ast_node_t base;
  /** @brief Enclosed inner expression AST node. */
  struct c_orm_ast_node *expr;
} c_orm_ast_group_t;

/**
 * @brief Subquery AST Node
 * @var base Base node metadata.
 * @var query Nested subquery handle.
 * @var alias Optional subquery alias name.
 */
typedef struct c_orm_ast_subquery {
  /** @brief Base node metadata. */
  c_orm_ast_node_t base;
  /** @brief Nested subquery handle. */
  struct c_orm_query *query;
  /** @brief Optional subquery alias name. */
  const char *alias;
} c_orm_ast_subquery_t;

/**
 * @brief Union AST Node
 * @var base Base node metadata.
 * @var is_all Non-zero if UNION ALL.
 * @var query Query handle to combine.
 */
typedef struct c_orm_ast_union {
  /** @brief Base node metadata. */
  c_orm_ast_node_t base;
  /** @brief Non-zero if UNION ALL. */
  int is_all;
  /** @brief Query handle to combine. */
  struct c_orm_query *query;
} c_orm_ast_union_t;

/**
 * @brief With (CTE) AST Node
 * @var base Base node metadata.
 * @var alias CTE alias name.
 * @var query CTE definition query handle.
 */
typedef struct c_orm_ast_with {
  /** @brief Base node metadata. */
  c_orm_ast_node_t base;
  /** @brief CTE alias name. */
  const char *alias;
  /** @brief CTE definition query handle. */
  struct c_orm_query *query;
} c_orm_ast_with_t;

/**
 * @brief Function AST Node
 * @var base Base node metadata.
 * @var name Function name identifier.
 * @var args Comma-separated arguments or expression string.
 * @var alias Optional alias name.
 */
typedef struct c_orm_ast_function {
  /** @brief Base node metadata. */
  c_orm_ast_node_t base;
  /** @brief Function name identifier. */
  const char *name;
  /** @brief Comma-separated arguments or expression string. */
  const char *args;
  /** @brief Optional alias name. */
  const char *alias;
} c_orm_ast_function_t;

/**
 * @brief Cast AST Node
 * @var base Base node metadata.
 * @var col Column name to cast.
 * @var type Target SQL type name.
 */
typedef struct c_orm_ast_cast {
  /** @brief Base node metadata. */
  c_orm_ast_node_t base;
  /** @brief Column name to cast. */
  const char *col;
  /** @brief Target SQL type name. */
  const char *type;
} c_orm_ast_cast_t;

/**
 * @brief Between AST Node
 * @var base Base node metadata.
 * @var col Column identifier.
 * @var low Lower bound value string.
 * @var high Upper bound value string.
 * @var is_string Non-zero if bound values are string literals.
 */
typedef struct c_orm_ast_between {
  /** @brief Base node metadata. */
  c_orm_ast_node_t base;
  /** @brief Column identifier. */
  const char *col;
  /** @brief Lower bound value string. */
  const char *low;
  /** @brief Upper bound value string. */
  const char *high;
  /** @brief Non-zero if bound values are string literals. */
  int is_string;
} c_orm_ast_between_t;

/**
 * @brief Exists AST Node
 * @var base Base node metadata.
 * @var query Subquery handle to test for existence.
 * @var is_not Non-zero if NOT EXISTS.
 */
typedef struct c_orm_ast_exists {
  /** @brief Base node metadata. */
  c_orm_ast_node_t base;
  /** @brief Subquery handle to test for existence. */
  struct c_orm_query *query;
  /** @brief Non-zero if NOT EXISTS. */
  int is_not;
} c_orm_ast_exists_t;

/**
 * @brief Window Function AST Node
 * @var base Base node metadata.
 * @var func_name Window function name.
 * @var partition_by Partition by clause string.
 * @var order_by Order by clause string.
 * @var alias Optional window function alias.
 */
typedef struct c_orm_ast_window {
  /** @brief Base node metadata. */
  c_orm_ast_node_t base;
  /** @brief Window function name. */
  const char *func_name;
  /** @brief Partition by clause string. */
  const char *partition_by;
  /** @brief Order by clause string. */
  const char *order_by;
  /** @brief Optional window function alias. */
  const char *alias;
} c_orm_ast_window_t;

/** @brief Global max recursion depth for the parser/renderer */
C_ORM_EXPORT extern unsigned int cdd_c_sql_parser_max_depth;

/**
 * @brief Memory Arena for AST nodes
 */
typedef struct c_orm_arena c_orm_arena_t;

/**
 * @brief Dialect renderer type for SQL generation.
 */
typedef enum {
  C_ORM_DIALECT_SQLITE = 0,
  C_ORM_DIALECT_POSTGRES,
  C_ORM_DIALECT_MYSQL
} c_orm_dialect_t;

/**
 * @brief SQL query parameter.
 * @var value Bound parameter value string.
 * @var is_string Non-zero if parameter is string literal.
 */
typedef struct c_orm_query_param {
  /** @brief Bound parameter value string. */
  const char *value;
  /** @brief Non-zero if parameter is string literal. */
  int is_string;
} c_orm_query_param_t;

/**
 * @brief Collection of SQL query parameters extracted from an AST.
 * @var params Array of parameter descriptors.
 * @var count Number of active parameters.
 * @var capacity Allocated capacity of array.
 */
typedef struct c_orm_query_params {
  /** @brief Array of parameter descriptors. */
  c_orm_query_param_t *params;
  /** @brief Number of active parameters. */
  size_t count;
  /** @brief Allocated capacity of array. */
  size_t capacity;
} c_orm_query_params_t;

/**
 * @brief Initialize a parameter collection.
 * @param params Pointer to parameter collection structure.
 * @return 0 on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_query_params_init(c_orm_query_params_t *params);

/**
 * @brief Free resources in a parameter collection.
 * @param params Pointer to parameter collection structure.
 * @return 0 on success.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_query_params_cleanup(c_orm_query_params_t *params);

/**
 * @brief Add a parameter to the collection.
 * @param params Pointer to parameter collection structure.
 * @param value String value to append.
 * @param is_string Non-zero if value is a string.
 * @return 0 on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_query_params_add(c_orm_query_params_t *params,
                                                  const char *value,
                                                  int is_string);

/**
 * @brief Generate a raw SQL string and its ordered parameters from the AST.
 * @param q The query builder AST.
 * @param dialect The target dialect.
 * @param out_sql Pointer to receive the generated raw SQL string. The caller is
 * responsible for freeing it.
 * @param out_params Pointer to a parameter collection struct. If provided,
 * literal values are extracted into it and replaced with `?` or `$1`
 * respectively.
 * @return 0 on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_query_to_sql(c_orm_query_t *q,
                                              c_orm_dialect_t dialect,
                                              char **out_sql,
                                              c_orm_query_params_t *out_params);

/**
 * @brief Execute a generic AST builder query without expecting a return
 * dataset.
 * @param db Database handle.
 * @param q The query builder.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_query_execute(c_orm_db_t *db,
                                               c_orm_query_t *q);

/**
 * @brief Fetch a single mapped struct instance from an AST query.
 * @param db Database handle.
 * @param q The query builder.
 * @param meta Table metadata structure for the mapping.
 * @param out_struct Output struct pointer (pre-allocated).
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_query_fetch_one(c_orm_db_t *db,
                                                 c_orm_query_t *q,
                                                 const c_orm_table_meta_t *meta,
                                                 void *out_struct);

/**
 * @brief Fetch an array of mapped structs from an AST query.
 * @param db Database handle.
 * @param q The query builder.
 * @param meta Table metadata structure for the mapping.
 * @param out_array Output array container structure from generated models.
 * @return C_ORM_OK on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_query_fetch_all(c_orm_db_t *db,
                                                 c_orm_query_t *q,
                                                 const c_orm_table_meta_t *meta,
                                                 void *out_array);

/**
 * @brief Create a new memory arena.
 * @param out_arena The created arena.
 * @return 0 on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_arena_new(c_orm_arena_t **out_arena);

/**
 * @brief Allocate memory from the arena.
 * @param arena The arena.
 * @param size The size to allocate.
 * @param out_ptr Pointer to receive the allocated memory.
 * @return 0 on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_arena_alloc(c_orm_arena_t *arena, size_t size,
                                             void **out_ptr);

/**
 * @brief Duplicate a string using memory allocated from the arena.
 * @param arena The arena.
 * @param str String to duplicate.
 * @param out_copy Pointer to receive the duplicated string.
 * @return 0 on success, non-zero on failure.
 */
C_ORM_EXPORT c_orm_error_t c_orm_arena_strdup(c_orm_arena_t *arena,
                                              const char *str,
                                              const char **out_copy);

/**
 * @brief Free all memory in the arena and the arena itself.
 * @param arena The arena.
 */
C_ORM_EXPORT void c_orm_arena_free(c_orm_arena_t *arena);

/* Forward declaration */
#ifndef C_ORM_QUERY_T_DEFINED
#define C_ORM_QUERY_T_DEFINED
typedef struct c_orm_query c_orm_query_t;
#endif

/**
 * @brief Fluent Query builder interface.
 * @var arena Memory arena for query AST allocations.
 * @var ast_head Head node pointer of AST list.
 * @var error Error status flag.
 * @var select_ Sets SELECT projection columns.
 * @var from Sets FROM table source.
 * @var where Adds root WHERE condition.
 * @var and_where Adds condition with AND.
 * @var or_where Adds condition with OR.
 * @var order_by Adds ORDER BY clause.
 * @var limit Adds LIMIT clause.
 * @var offset Adds OFFSET clause.
 * @var clone Clones query builder instance.
 * @var join Adds JOIN clause.
 * @var left_join Adds LEFT JOIN clause.
 * @var right_join Adds RIGHT JOIN clause.
 * @var group_by Adds GROUP BY clause.
 * @var having Adds HAVING condition.
 * @var with Adds WITH (CTE) clause.
 * @var union_ Adds UNION clause.
 * @var distinct Enables DISTINCT modifier.
 * @var from_alias Sets FROM table with alias.
 * @var eager_load Eagerly loads specified relationship.
 * @var group Wraps expression in parentheses.
 * @var subquery Constructs subquery AST node.
 * @var func Constructs SQL function AST node.
 * @var cast_ Constructs CAST AST node.
 * @var is_null Constructs IS NULL AST node.
 * @var between Constructs BETWEEN AST node.
 * @var exists Constructs EXISTS AST node.
 * @var window Constructs WINDOW function AST node.
 * @var raw Constructs RAW SQL AST node.
 * @var eq Constructs equality operator AST node.
 * @var neq Constructs inequality operator AST node.
 * @var gt Constructs greater-than operator AST node.
 * @var lt Constructs less-than operator AST node.
 * @var like Constructs LIKE operator AST node.
 * @var in Constructs IN operator AST node.
 * @var op Constructs binary operator AST node.
 * @var lit Constructs literal value AST node.
 * @var col Constructs column reference AST node.
 */
struct c_orm_query {
  /** @brief Memory arena for query AST allocations. */
  c_orm_arena_t *arena;
  /** @brief Head node pointer of AST list. */
  c_orm_ast_node_t *ast_head;
  /** @brief Error status flag. */
  int error;

  /* Fluent methods */
  /** @brief Sets SELECT projection columns. */
  c_orm_query_t *(*select_)(c_orm_query_t *q, const char *columns);
  /** @brief Sets FROM table source. */
  c_orm_query_t *(*from)(c_orm_query_t *q, const char *table);
  /** @brief Adds root WHERE condition. */
  c_orm_query_t *(*where)(c_orm_query_t *q, c_orm_ast_node_t *condition);
  /** @brief Adds condition with AND. */
  c_orm_query_t *(*and_where)(c_orm_query_t *q, c_orm_ast_node_t *condition);
  /** @brief Adds condition with OR. */
  c_orm_query_t *(*or_where)(c_orm_query_t *q, c_orm_ast_node_t *condition);
  /** @brief Adds ORDER BY clause. */
  c_orm_query_t *(*order_by)(c_orm_query_t *q, const char *column, int is_desc);
  /** @brief Adds LIMIT clause. */
  c_orm_query_t *(*limit)(c_orm_query_t *q, size_t n);
  /** @brief Adds OFFSET clause. */
  c_orm_query_t *(*offset)(c_orm_query_t *q, size_t n);

  /* Query Cloning */
  /** @brief Clones query builder instance. */
  c_orm_error_t (*clone)(c_orm_query_t *q, c_orm_query_t **out_q);

  /* Advanced Query Features (Phase 3) */
  /** @brief Adds JOIN clause. */
  c_orm_query_t *(*join)(c_orm_query_t *q, const char *table,
                         const char *type_str, c_orm_ast_node_t *on_condition);
  /** @brief Adds LEFT JOIN clause. */
  c_orm_query_t *(*left_join)(c_orm_query_t *q, const char *table,
                              c_orm_ast_node_t *on_condition);
  /** @brief Adds RIGHT JOIN clause. */
  c_orm_query_t *(*right_join)(c_orm_query_t *q, const char *table,
                               c_orm_ast_node_t *on_condition);
  /** @brief Adds GROUP BY clause. */
  c_orm_query_t *(*group_by)(c_orm_query_t *q, const char *columns);
  /** @brief Adds HAVING condition. */
  c_orm_query_t *(*having)(c_orm_query_t *q, c_orm_ast_node_t *condition);
  /** @brief Adds WITH (CTE) clause. */
  c_orm_query_t *(*with)(c_orm_query_t *q, const char *alias,
                         c_orm_query_t *subquery);
  /** @brief Adds UNION clause. */
  c_orm_query_t *(*union_)(c_orm_query_t *q, c_orm_query_t *other, int is_all);
  /** @brief Enables DISTINCT modifier. */
  c_orm_query_t *(*distinct)(c_orm_query_t *q);
  /** @brief Sets FROM table with alias. */
  c_orm_query_t *(*from_alias)(c_orm_query_t *q, const char *table,
                               const char *alias);
  /** @brief Eagerly loads specified relationship. */
  c_orm_query_t *(*eager_load)(c_orm_query_t *q, const c_orm_table_meta_t *meta,
                               const char *relation_name);

  /* AST Node Builders */
  /** @brief Wraps expression in parentheses. */
  c_orm_ast_node_t *(*group)(c_orm_query_t *q, c_orm_ast_node_t *expr);
  /** @brief Constructs subquery AST node. */
  c_orm_ast_node_t *(*subquery)(c_orm_query_t *q, c_orm_query_t *subq,
                                const char *alias);
  /** @brief Constructs SQL function AST node. */
  c_orm_ast_node_t *(*func)(c_orm_query_t *q, const char *name,
                            const char *args, const char *alias);
  /** @brief Constructs CAST AST node. */
  c_orm_ast_node_t *(*cast_)(c_orm_query_t *q, const char *col,
                             const char *type);
  /** @brief Constructs IS NULL AST node. */
  c_orm_ast_node_t *(*is_null)(c_orm_query_t *q, const char *col, int is_not);
  /** @brief Constructs BETWEEN AST node. */
  c_orm_ast_node_t *(*between)(c_orm_query_t *q, const char *col,
                               const char *low, const char *high,
                               int is_string);
  /** @brief Constructs EXISTS AST node. */
  c_orm_ast_node_t *(*exists)(c_orm_query_t *q, c_orm_query_t *subq,
                              int is_not);
  /** @brief Constructs WINDOW function AST node. */
  c_orm_ast_node_t *(*window)(c_orm_query_t *q, const char *func_name,
                              const char *partition_by, const char *order_by,
                              const char *alias);
  /** @brief Constructs RAW SQL AST node. */
  c_orm_ast_node_t *(*raw)(c_orm_query_t *q, const char *sql);
  /** @brief Constructs equality operator AST node. */
  c_orm_ast_node_t *(*eq)(c_orm_query_t *q, const char *col, const char *val,
                          int is_string);
  /** @brief Constructs inequality operator AST node. */
  c_orm_ast_node_t *(*neq)(c_orm_query_t *q, const char *col, const char *val,
                           int is_string);
  /** @brief Constructs greater-than operator AST node. */
  c_orm_ast_node_t *(*gt)(c_orm_query_t *q, const char *col, const char *val,
                          int is_string);
  /** @brief Constructs less-than operator AST node. */
  c_orm_ast_node_t *(*lt)(c_orm_query_t *q, const char *col, const char *val,
                          int is_string);
  /** @brief Constructs LIKE operator AST node. */
  c_orm_ast_node_t *(*like)(c_orm_query_t *q, const char *col, const char *val);
  /** @brief Constructs IN operator AST node. */
  c_orm_ast_node_t *(*in)(c_orm_query_t *q, const char *col,
                          const char *val_list);
  /** @brief Constructs binary operator AST node. */
  c_orm_ast_node_t *(*op)(c_orm_query_t *q, const char *op,
                          c_orm_ast_node_t *left, c_orm_ast_node_t *right);
  /** @brief Constructs literal value AST node. */
  c_orm_ast_node_t *(*lit)(c_orm_query_t *q, const char *val, int is_string);
  /** @brief Constructs column reference AST node. */
  c_orm_ast_node_t *(*col)(c_orm_query_t *q, const char *name);
};

/**
 * @brief Initialize a new query builder.
 * @param out_query Output query builder.
 * @return 0 on success.
 */
C_ORM_EXPORT c_orm_error_t c_orm_query_new(c_orm_query_t **out_query);

/**
 * @brief Free a query builder.
 * @param query Query builder.
 */
C_ORM_EXPORT void c_orm_query_free(c_orm_query_t *query);

/*
 * Macro wrappers for fluent API.
 * These assume 'q' is an existing c_orm_query_t* variable in the current scope.
 */
#define C_ORM_WHERE(cond) q->where(q, (cond))
#define C_ORM_AND_WHERE(cond) q->and_where(q, (cond))
#define C_ORM_OR_WHERE(cond) q->or_where(q, (cond))
#define C_ORM_EQ(col, val) q->eq(q, (col), (val), 1)
#define C_ORM_EQ_NUM(col, val) q->eq(q, (col), (val), 0)
#define C_ORM_NEQ(col, val) q->neq(q, (col), (val), 1)
#define C_ORM_NEQ_NUM(col, val) q->neq(q, (col), (val), 0)
#define C_ORM_GT(col, val) q->gt(q, (col), (val), 1)
#define C_ORM_GT_NUM(col, val) q->gt(q, (col), (val), 0)
#define C_ORM_LT(col, val) q->lt(q, (col), (val), 1)
#define C_ORM_LT_NUM(col, val) q->lt(q, (col), (val), 0)
#define C_ORM_LIKE(col, val) q->like(q, (col), (val))
#define C_ORM_IN(col, val_list) q->in(q, (col), (val_list))
#define C_ORM_RAW(sql) q->raw(q, (sql))
#define C_ORM_IS_NULL(col) q->is_null(q, (col), 0)
#define C_ORM_IS_NOT_NULL(col) q->is_null(q, (col), 1)
#define C_ORM_BETWEEN(col, low, high, is_string)                               \
  q->between(q, (col), (low), (high), (is_string))
#define C_ORM_EXISTS(subq) q->exists(q, (subq), 0)
#define C_ORM_NOT_EXISTS(subq) q->exists(q, (subq), 1)

#define C_ORM_COUNT(col, alias) q->func(q, "COUNT", (col), (alias))
#define C_ORM_SUM(col, alias) q->func(q, "SUM", (col), (alias))
#define C_ORM_AVG(col, alias) q->func(q, "AVG", (col), (alias))
#define C_ORM_CONCAT(args, alias) q->func(q, "CONCAT", (args), (alias))
#define C_ORM_SUBSTR(args, alias) q->func(q, "SUBSTR", (args), (alias))
#define C_ORM_NOW(alias) q->func(q, "NOW", "", (alias))
#define C_ORM_DATE_ADD(args, alias) q->func(q, "DATE_ADD", (args), (alias))

#define C_ORM_CAST(col, type) q->cast_(q, (col), (type))

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* C_ORM_AST_H */

#if defined(__clang__) || defined(__GNUC__)
#endif
