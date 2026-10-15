#ifndef TXN_H
#define TXN_H
#include <stdio.h>
/* Module 4 - Transactions + Write-Ahead Log.
 *
 * Rule: a transaction's changes reach the database file only AFTER its log records
 * (before + after image of every page it changed, then COMMIT) are safely on disk.
 * The buffer pool never evicts pages a running transaction changed, so uncommitted data
 * never reaches the file. After a crash, txn_recover() replays committed transactions
 * (redo) and restores the before-images of unfinished ones (undo). */

void txn_init(const char *wal_path);   /* choose the log file (call first) */
int  txn_begin(void);                  /* returns a new transaction id */
int  txn_log_write(int txn_id, int page_id, const char *old_data, const char *new_data);
int  txn_commit(int txn_id);           /* 0 ok, -1 failure */
int  txn_rollback(int txn_id);
void txn_recover(void);                /* call at startup, after storage_open */
void txn_checkpoint(void);             /* flush everything and empty the log */
long txn_commit_count(void);
void txn_dump_log(FILE *out);          /* human-readable view of wal for demos */
#endif
