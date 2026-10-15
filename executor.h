/* Name: Anjali Chand | Roll No: 09 | Project: SQL Parser and Query Executor */
#ifndef EXECUTOR_H
#define EXECUTOR_H
#include <stdio.h>
int engine_open(const char *db_path);
void engine_close(void);
/* Executes one statement. Reply format: OK\n[rows...]END\n or ERR ...\nEND\n. */
int execute_sql(const char *sql, char *out_buf, int out_size);
void print_tokens(const char *sql, FILE *out);
#endif
