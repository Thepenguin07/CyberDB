#ifndef EXECUTOR_H
#define EXECUTOR_H
#include <stdio.h>
/* Module 3 - SQL Parser + Query Executor (the "front door" of the engine). */

int  engine_open(const char *db_path);   /* open storage + log, run crash recovery, init catalog */
void engine_close(void);                 /* roll back open txn, checkpoint, close files */

/* Runs ONE statement. Always fills out_buf with the protocol reply:
 *   OK \n row \n row ... END \n      or      ERR message \n END \n
 * Returns 0 on success, -1 on error. */
int  execute_sql(const char *sql, char *out_buf, int out_size);

void print_tokens(const char *sql, FILE *out);   /* demo: show the tokenizer output */
#endif
