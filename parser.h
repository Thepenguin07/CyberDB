#ifndef PARSER_H
#define PARSER_H
#include "tokenizer.h"

#define MAX_COLUMNS 16

typedef enum { EXPR_COLUMN, EXPR_COUNT, EXPR_SUM, EXPR_AVG } ExprType;
typedef struct { ExprType type; char column[TOKEN_LEN]; int star_argument; } SelectExpr;
typedef struct { int enabled; char column[TOKEN_LEN]; char op[3]; char value[TOKEN_LEN]; } WhereClause;
typedef struct { int enabled; char column[TOKEN_LEN]; int descending; } OrderClause;
typedef struct {
    SelectExpr expressions[MAX_COLUMNS];
    int expression_count;
    int select_all;
    char table[TOKEN_LEN];
    WhereClause where;
    OrderClause order;
} SelectQuery;

/* Parses the SELECT subset: columns/aggregates, FROM, optional WHERE and ORDER BY. */
int parse_query(char tokens[][TOKEN_LEN], int token_count, SelectQuery *query);
#endif
