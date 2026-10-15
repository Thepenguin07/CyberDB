/* Name: Anjali Chand | Roll No: 09 | Project: SQL Parser and Query Executor */
#include <string.h>
#include <ctype.h>
#include "parser.h"

static int eq(const char *a, const char *b) {
    while (*a && *b) {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return 0;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

static int reserved(const char *s) {
    const char *words[] = {"SELECT", "FROM", "WHERE", "ORDER", "BY", "ASC", "DESC",
                           "COUNT", "SUM", "AVG", "AND", "OR", "GROUP", "HAVING", NULL};
    for (int i = 0; words[i]; i++) if (eq(s, words[i])) return 1;
    return 0;
}

static int identifier(const char *s) {
    if (!s || !(isalpha((unsigned char)s[0]) || s[0] == '_')) return 0;
    for (int i = 1; s[i]; i++)
        if (!(isalnum((unsigned char)s[i]) || s[i] == '_')) return 0;
    return !reserved(s);
}

static int at(char tokens[][TOKEN_LEN], int n, int i, const char *s) {
    return i < n && eq(tokens[i], s);
}

static int copy_token(char *dst, const char *src) {
    size_t len = strlen(src);
    if (len >= TOKEN_LEN) return 0;
    memcpy(dst, src, len + 1);
    return 1;
}

static int parse_expr(char tokens[][TOKEN_LEN], int n, int *i, SelectExpr *expr) {
    memset(expr, 0, sizeof *expr);
    if (at(tokens, n, *i, "COUNT") || at(tokens, n, *i, "SUM") || at(tokens, n, *i, "AVG")) {
        if (at(tokens, n, *i, "COUNT")) expr->type = EXPR_COUNT;
        else if (at(tokens, n, *i, "SUM")) expr->type = EXPR_SUM;
        else expr->type = EXPR_AVG;
        (*i)++;
        if (!at(tokens, n, *i, "(")) return 0;
        (*i)++;
        if (at(tokens, n, *i, "*")) {
            expr->star_argument = 1;
            strcpy(expr->column, "*");
            (*i)++;
        } else {
            if (*i >= n || !identifier(tokens[*i]) || !copy_token(expr->column, tokens[*i])) return 0;
            (*i)++;
        }
        if (!at(tokens, n, *i, ")")) return 0;
        (*i)++;
        return 1;
    }
    if (*i >= n || !identifier(tokens[*i]) || !copy_token(expr->column, tokens[*i])) return 0;
    expr->type = EXPR_COLUMN;
    (*i)++;
    return 1;
}

static int parse_where(char tokens[][TOKEN_LEN], int n, int *i, WhereClause *where) {
    if (!at(tokens, n, *i, "WHERE")) return 1;
    (*i)++;
    where->enabled = 1;
    if (*i >= n || !identifier(tokens[*i]) || !copy_token(where->column, tokens[*i])) return 0;
    (*i)++;
    if (*i >= n || (strcmp(tokens[*i], "=") && strcmp(tokens[*i], "!=") &&
        strcmp(tokens[*i], "<>") && strcmp(tokens[*i], "<") && strcmp(tokens[*i], ">") &&
        strcmp(tokens[*i], "<=") && strcmp(tokens[*i], ">="))) return 0;
    strcpy(where->op, tokens[*i]);
    (*i)++;
    if (*i >= n || at(tokens, n, *i, ";") || at(tokens, n, *i, "ORDER")) return 0;
    return copy_token(where->value, tokens[(*i)++]);
}

int parse_query(char tokens[][TOKEN_LEN], int token_count, SelectQuery *query) {
    int i = 0;
    if (!tokens || !query || token_count < 4) return -1;
    memset(query, 0, sizeof *query);
    if (!at(tokens, token_count, i, "SELECT")) return -1;
    i++;
    if (at(tokens, token_count, i, "*")) {
        query->select_all = 1;
        i++;
    } else {
        for (;;) {
            if (query->expression_count >= MAX_COLUMNS ||
                !parse_expr(tokens, token_count, &i, &query->expressions[query->expression_count])) return -1;
            query->expression_count++;
            if (!at(tokens, token_count, i, ",")) break;
            i++;
        }
    }
    if (!at(tokens, token_count, i, "FROM")) return -1;
    i++;
    if (i >= token_count || !identifier(tokens[i]) || !copy_token(query->table, tokens[i])) return -1;
    i++;
    if (!parse_where(tokens, token_count, &i, &query->where)) return -1;
    if (at(tokens, token_count, i, "ORDER")) {
        i++;
        if (!at(tokens, token_count, i, "BY")) return -1;
        i++;
        if (i >= token_count || !identifier(tokens[i]) || !copy_token(query->order.column, tokens[i])) return -1;
        query->order.enabled = 1;
        i++;
        if (at(tokens, token_count, i, "DESC")) { query->order.descending = 1; i++; }
        else if (at(tokens, token_count, i, "ASC")) i++;
    }
    if (at(tokens, token_count, i, ";")) i++;
    return i == token_count ? 0 : -1;
}
