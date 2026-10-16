#include <stdio.h>
#include <string.h>
#include "buffer.h"
#include "../storage/storage.h"

#define POOL 64

typedef struct {
    int  page_id, used, dirty, pins;
    int  txn_dirty, has_before;
    unsigned long last_used;
    char data[PAGE_SIZE];
    char before[PAGE_SIZE];
} Frame;

static Frame pool[POOL];
static unsigned long clock_tick = 0;
static long hits = 0, misses = 0;
static int verbose = 0, in_txn = 0;

static int find(int id) {
    for (int i = 0; i < POOL; i++)
        if (pool[i].used && pool[i].page_id == id) return i;
    return -1;
}
/* free frame first, else least-recently-used frame that is not pinned and not txn-dirty */
static int pick_victim(void) {
    int best = -1;
    for (int i = 0; i < POOL; i++) {
        if (!pool[i].used) return i;
        if (pool[i].pins > 0 || pool[i].txn_dirty) continue;
        if (best < 0 || pool[i].last_used < pool[best].last_used) best = i;
    }
    return best;
}

char *buffer_get_page(int id) {
    int i = find(id);
    if (i >= 0) {
        hits++;
        if (verbose) fprintf(stderr, "HIT  page %d\n", id);
    } else {
        misses++;
        if (verbose) fprintf(stderr, "MISS page %d\n", id);
        i = pick_victim();
        if (i < 0) return NULL;                         /* everything is pinned */
        if (pool[i].used && pool[i].dirty)              /* write back the evicted page */
            if (storage_write_page(pool[i].page_id, pool[i].data) != 0) return NULL;
        if (storage_read_page(id, pool[i].data) != 0) { pool[i].used = 0; return NULL; }
        pool[i].page_id = id; pool[i].used = 1; pool[i].dirty = 0;
        pool[i].pins = 0; pool[i].txn_dirty = 0; pool[i].has_before = 0;
    }
    pool[i].pins++;
    pool[i].last_used = ++clock_tick;
    if (in_txn && !pool[i].has_before) {                /* remember how the page looked */
        memcpy(pool[i].before, pool[i].data, PAGE_SIZE);
        pool[i].has_before = 1;
    }
    return pool[i].data;
}

void buffer_mark_dirty(int id) {
    int i = find(id);
    if (i < 0) return;
    pool[i].dirty = 1;
    if (in_txn) pool[i].txn_dirty = 1;
}

void buffer_unpin(int id) {
    int i = find(id);
    if (i >= 0 && pool[i].pins > 0) pool[i].pins--;
}

void buffer_flush_all(void) {
    for (int i = 0; i < POOL; i++)
        if (pool[i].used && pool[i].dirty) {
            storage_write_page(pool[i].page_id, pool[i].data);
            pool[i].dirty = 0;
        }
    storage_sync();
}

void buffer_stats(long *h, long *m) { *h = hits; *m = misses; }
void buffer_set_verbose(int on)     { verbose = on; }

/* ---- transaction hooks ---- */
void buffer_txn_begin(void) { in_txn = 1; }

int buffer_txn_pages(int *ids, int max) {
    int n = 0;
    for (int i = 0; i < POOL && n < max; i++)
        if (pool[i].used && pool[i].txn_dirty) ids[n++] = pool[i].page_id;
    for (int a = 1; a < n; a++) {                       /* insertion sort */
        int v = ids[a], b = a - 1;
        while (b >= 0 && ids[b] > v) { ids[b + 1] = ids[b]; b--; }
        ids[b + 1] = v;
    }
    return n;
}

const char *buffer_before_image(int id) {
    int i = find(id);
    return (i >= 0 && pool[i].has_before) ? pool[i].before : NULL;
}

const char *buffer_page_data(int id) {
    int i = find(id);
    return i >= 0 ? pool[i].data : NULL;
}

void buffer_txn_end(int commit) {
    for (int i = 0; i < POOL; i++) {
        if (pool[i].used && pool[i].txn_dirty && !commit) {
            memcpy(pool[i].data, pool[i].before, PAGE_SIZE);   /* undo in memory */
            pool[i].dirty = 0;
        }
        pool[i].txn_dirty = 0;
        pool[i].has_before = 0;
    }
    in_txn = 0;
    if (commit) buffer_flush_all();                     /* force committed pages to disk */
}
