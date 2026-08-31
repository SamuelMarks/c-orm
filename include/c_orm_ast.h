#if defined(__clang__) || defined(__GNUC__)
#endif
/**
 * @file c_orm_ast.h
 * @brief Dynamic SQL Query Abstract Syntax Tree (AST).
 */

#ifndef C_ORM_AST_H
#define C_ORM_AST_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* clang-format off */
#include "c_orm_api.h"
#include <stddef.h>
/* clang-format on */

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
 */
typedef struct c_orm_ast_node {
  c_orm_ast_node_type_t type;
  struct c_orm_ast_node *next; /* Sibling linked list */
} c_orm_ast_node_t;

/**
 * @brief Select AST Node
 */
typedef struct c_orm_ast_select {
  c_orm_ast_node_t base;
  const char *columns; /* Comma separated or list, keeping it simple as a string
                          for now */
  int is_distinct;
} c_orm_ast_select_t;

/**
 * @brief From AST Node
 */
typedef struct c_orm_ast_from {
  c_orm_ast_node_t base;
  const char *table;
  const char *alias;
} c_orm_ast_from_t;

/**
 * @brief Where AST Node
 */
typedef struct c_orm_ast_where {
  c_orm_ast_node_t base;
  struct c_orm_ast_node *condition;
} c_orm_ast_where_t;

/**
 * @brief Join AST Node
 */
typedef struct c_orm_ast_join {
  c_orm_ast_node_t base;
  const char *type_str; /* "INNER", "LEFT", "RIGHT" */
  const char *table;
  struct c_orm_ast_node *on_condition;
} c_orm_ast_join_t;

/**
 * @brief Group By AST Node
 */
typedef struct c_orm_ast_group_by {
  c_orm_ast_node_t base;
  const char *columns;
} c_orm_ast_group_by_t;

/**
 * @brief Having AST Node
 */
typedef struct c_orm_ast_having {
  c_orm_ast_node_t base;
  struct c_orm_ast_node *condition;
} c_orm_ast_having_t;

/**
 * @brief Order By AST Node
 */
typedef struct c_orm_ast_order_by {
  c_orm_ast_node_t base;
  const char *column;
  int is_desc;
} c_orm_ast_order_by_t;

/**
 * @brief Limit AST Node
 */
typedef struct c_orm_ast_limit {
  c_orm_ast_node_t base;
  size_t limit;
} c_orm_ast_limit_t;

/**
 * @brief Offset AST Node
 */
typedef struct c_orm_ast_offset {
  c_orm_ast_node_t base;
  size_t offset;
} c_orm_ast_offset_t;

/**
 * @brief Literal value AST node
 */
typedef struct c_orm_ast_literal {
  c_orm_ast_node_t base;
  const char *value;
  int is_string;
} c_orm_ast_literal_t;

/**
 * @brief Operator AST Node (e.g. '=', '>', 'AND', 'OR')
 */
typedef struct c_orm_ast_operator {
  c_orm_ast_node_t base;
  const char *op;
  struct c_orm_ast_node *left;
  struct c_orm_ast_node *right;
} c_orm_ast_operator_t;

/**
 * @brief Raw SQL fallback node
 */
typedef struct c_orm_ast_raw {
  c_orm_ast_node_t base;
  const char *sql;
} c_orm_ast_raw_t;

/**
 * @brief Column reference AST Node
 */
typedef struct c_orm_ast_column {
  c_orm_ast_node_t base;
  const char *name;
} c_orm_ast_column_t;

/**
 * @brief Group (Parentheses) AST Node
 */
typedef struct c_orm_ast_group {
  c_orm_ast_node_t base;
  struct c_orm_ast_node *expr;
} c_orm_ast_group_t;

/**
 * @brief Subquery AST Node
 */
typedef struct c_orm_ast_subquery {
  c_orm_ast_node_t base;
  struct c_orm_query *query;
  const char *alias;
} c_orm_ast_subquery_t;

/**
 * @brief Union AST Node
 */
typedef struct c_orm_ast_union {
  c_orm_ast_node_t base;
  int is_all;
  struct c_orm_query *query;
} c_orm_ast_union_t;

/**
 * @brief With (CTE) AST Node
 */
typedef struct c_orm_ast_with {
  c_orm_ast_node_t base;
  const char *alias;
  struct c_orm_query *query;
} c_orm_ast_with_t;

/**
 * @brief Function AST Node
 */
typedef struct c_orm_ast_function {
  c_orm_ast_node_t base;
  const char *name;
  const char *args;
  const char *alias;
} c_orm_ast_function_t;

/**
 * @brief Cast AST Node
 */
typedef struct c_orm_ast_cast {
  c_orm_ast_node_t base;
  const char *col;
  const char *type;
} c_orm_ast_cast_t;

/**
 * @brief Between AST Node
 */
typedef struct c_orm_ast_between {
  c_orm_ast_node_t base;
  const char *col;
  const char *low;
  const char *high;
  int is_string;
} c_orm_ast_between_t;

/**
 * @brief Exists AST Node
 */
typedef struct c_orm_ast_exists {
  c_orm_ast_node_t base;
  struct c_orm_query *query;
  int is_not;
} c_orm_ast_exists_t;

/**
 * @brief Window Function AST Node
 */
typedef struct c_orm_ast_window {
  c_orm_ast_node_t base;
  const char *func_name;
  const char *partition_by;
  const char *order_by;
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
 */
typedef struct c_orm_query_param {
  const char *value;
  int is_string;
} c_orm_query_param_t;

/**
 * @brief Collection of SQL query parameters extracted from an AST.
 */
typedef struct c_orm_query_params {
  c_orm_query_param_t *params;
  size_t count;
  size_t capacity;
} c_orm_query_params_t;

/**
 * @brief Initialize a parameter collection.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_query_params_init(c_orm_query_params_t *params);

/**
 * @brief Free resources in a parameter collection.
 */
C_ORM_EXPORT c_orm_error_t
c_orm_query_params_cleanup(c_orm_query_params_t *params);

/**
 * @brief Add a parameter to the collection.
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
 */
struct c_orm_query {
  c_orm_arena_t *arena;
  c_orm_ast_node_t *ast_head;
  int error;

  /* Fluent methods */
  c_orm_query_t *(*select_)(c_orm_query_t *q, const char *columns);
  c_orm_query_t *(*from)(c_orm_query_t *q, const char *table);
  c_orm_query_t *(*where)(c_orm_query_t *q, c_orm_ast_node_t *condition);
  c_orm_query_t *(*and_where)(c_orm_query_t *q, c_orm_ast_node_t *condition);
  c_orm_query_t *(*or_where)(c_orm_query_t *q, c_orm_ast_node_t *condition);
  c_orm_query_t *(*order_by)(c_orm_query_t *q, const char *column, int is_desc);
  c_orm_query_t *(*limit)(c_orm_query_t *q, size_t n);
  c_orm_query_t *(*offset)(c_orm_query_t *q, size_t n);

  /* Query Cloning */
  c_orm_error_t (*clone)(c_orm_query_t *q, c_orm_query_t **out_q);

  /* Advanced Query Features (Phase 3) */
  c_orm_query_t *(*join)(c_orm_query_t *q, const char *table,
                         const char *type_str, c_orm_ast_node_t *on_condition);
  c_orm_query_t *(*left_join)(c_orm_query_t *q, const char *table,
                              c_orm_ast_node_t *on_condition);
  c_orm_query_t *(*right_join)(c_orm_query_t *q, const char *table,
                               c_orm_ast_node_t *on_condition);
  c_orm_query_t *(*group_by)(c_orm_query_t *q, const char *columns);
  c_orm_query_t *(*having)(c_orm_query_t *q, c_orm_ast_node_t *condition);
  c_orm_query_t *(*with)(c_orm_query_t *q, const char *alias,
                         c_orm_query_t *subquery);
  c_orm_query_t *(*union_)(c_orm_query_t *q, c_orm_query_t *other, int is_all);
  c_orm_query_t *(*distinct)(c_orm_query_t *q);
  c_orm_query_t *(*from_alias)(c_orm_query_t *q, const char *table,
                               const char *alias);
  c_orm_query_t *(*eager_load)(c_orm_query_t *q, const c_orm_table_meta_t *meta,
                               const char *relation_name);

  /* AST Node Builders */
  c_orm_ast_node_t *(*group)(c_orm_query_t *q, c_orm_ast_node_t *expr);
  c_orm_ast_node_t *(*subquery)(c_orm_query_t *q, c_orm_query_t *subq,
                                const char *alias);
  c_orm_ast_node_t *(*func)(c_orm_query_t *q, const char *name,
                            const char *args, const char *alias);
  c_orm_ast_node_t *(*cast_)(c_orm_query_t *q, const char *col,
                             const char *type);
  c_orm_ast_node_t *(*is_null)(c_orm_query_t *q, const char *col, int is_not);
  c_orm_ast_node_t *(*between)(c_orm_query_t *q, const char *col,
                               const char *low, const char *high,
                               int is_string);
  c_orm_ast_node_t *(*exists)(c_orm_query_t *q, c_orm_query_t *subq,
                              int is_not);
  c_orm_ast_node_t *(*window)(c_orm_query_t *q, const char *func_name,
                              const char *partition_by, const char *order_by,
                              const char *alias);
  c_orm_ast_node_t *(*raw)(c_orm_query_t *q, const char *sql);
  c_orm_ast_node_t *(*eq)(c_orm_query_t *q, const char *col, const char *val,
                          int is_string);
  c_orm_ast_node_t *(*neq)(c_orm_query_t *q, const char *col, const char *val,
                           int is_string);
  c_orm_ast_node_t *(*gt)(c_orm_query_t *q, const char *col, const char *val,
                          int is_string);
  c_orm_ast_node_t *(*lt)(c_orm_query_t *q, const char *col, const char *val,
                          int is_string);
  c_orm_ast_node_t *(*like)(c_orm_query_t *q, const char *col, const char *val);
  c_orm_ast_node_t *(*in)(c_orm_query_t *q, const char *col,
                          const char *val_list);
  c_orm_ast_node_t *(*op)(c_orm_query_t *q, const char *op,
                          c_orm_ast_node_t *left, c_orm_ast_node_t *right);
  c_orm_ast_node_t *(*lit)(c_orm_query_t *q, const char *val, int is_string);
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

#if defined(__cplusplus)
}
#endif /* __cplusplus */
#endif /* C_ORM_AST_H */

#if defined(__clang__) || defined(__GNUC__)
#endif
