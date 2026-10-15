#ifndef BUFFER_H
#define BUFFER_H
/* Module 1 - Buffer Pool: caches pages in RAM (LRU replacement, pin counts, dirty flags).
 * Transaction support (used by Module 4): while a transaction is active the pool keeps a
 * "before image" of every page it touches and refuses to evict pages the transaction changed
 * (no-steal), so nothing uncommitted ever reaches the data file. */

char *buffer_get_page(int page_id);     /* load + pin; NULL if impossible */
void  buffer_mark_dirty(int page_id);
void  buffer_unpin(int page_id);
void  buffer_flush_all(void);           /* write every dirty page, then fsync */

void  buffer_stats(long *hits, long *misses);
void  buffer_set_verbose(int on);       /* print HIT/MISS lines to stderr */

/* transaction hooks */
void        buffer_txn_begin(void);
int         buffer_txn_pages(int *ids, int max);   /* pages modified by the txn, ascending */
const char *buffer_before_image(int page_id);
const char *buffer_page_data(int page_id);
void        buffer_txn_end(int commit);            /* commit: flush; rollback: restore images */
#endif
