/* Name: Anjali Chand | Roll No: 09 | Project: SQL Parser and Query Executor */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "tokenizer.h"

static int is_punctuation(char c) {
    return c == '(' || c == ')' || c == ',' || c == ';' || c == '*';
}

int tokenize_sql(const char *sql, char tokens[MAX_TOKENS][TOKEN_LEN], int max_tokens) {
    int i = 0, count = 0;
    if (!sql || !tokens || max_tokens <= 0) return -1;
    memset(tokens, 0, (size_t)max_tokens * TOKEN_LEN);

    while (sql[i] != '\0') {
        int j = 0;
        unsigned char c = (unsigned char)sql[i];
        if (isspace(c)) { i++; continue; }
        if (count >= max_tokens) return -1;

        if (sql[i] == '\'' || sql[i] == '"') {
            char quote = sql[i++];
            while (sql[i] && sql[i] != quote) {
                if (sql[i] == '\\' && sql[i + 1] == quote) i++;
                if (j < TOKEN_LEN - 1) tokens[count][j++] = sql[i];
                i++;
            }
            if (sql[i] != quote) return -1; /* unterminated quoted literal */
            i++;
        } else if (is_punctuation(sql[i])) {
            tokens[count][j++] = sql[i++];
        } else if (strchr("=<>!", sql[i])) {
            tokens[count][j++] = sql[i++];
            if ((tokens[count][0] == '<' && (sql[i] == '=' || sql[i] == '>')) ||
                (tokens[count][0] == '>' && sql[i] == '=') ||
                (tokens[count][0] == '!' && sql[i] == '=')) {
                if (j < TOKEN_LEN - 1) tokens[count][j++] = sql[i];
                i++;
            }
        } else {
            while (sql[i] && !isspace((unsigned char)sql[i]) &&
                   !is_punctuation(sql[i]) && !strchr("=<>!'\"", sql[i])) {
                if (j < TOKEN_LEN - 1) tokens[count][j++] = sql[i];
                i++;
            }
        }
        tokens[count][j] = '\0';
        count++;
    }
    return count;
}

void print_tokens(const char *sql, FILE *out) {
    char tokens[MAX_TOKENS][TOKEN_LEN];
    int n = tokenize_sql(sql, tokens, MAX_TOKENS);
    if (n < 0) { fprintf(out, "ERR tokenization failed\n"); return; }
    for (int i = 0; i < n; i++) fprintf(out, "%d|%s\n", i + 1, tokens[i]);
}
