#ifndef TOKENIZER_H
#define TOKENIZER_H
#include <stdio.h>

#define TOKEN_LEN 100
#define MAX_TOKENS 256

/* Tokenizes SQL into identifiers, literals, punctuation and operators.
   Quoted strings are returned without their surrounding quotes. */
int tokenize_sql(const char *sql, char tokens[MAX_TOKENS][TOKEN_LEN], int max_tokens);
void print_tokens(const char *sql, FILE *out);

#endif
