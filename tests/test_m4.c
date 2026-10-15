/* Module 4 tests: transactions + WAL at the page level */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../storage/storage.h"
#include "../buffer/buffer.h"
#include "../transactions/txn.h"

static void put(int page, const char *text) {
    char *p = buffer_get_page(page);
    strcpy(p, text); buffer_mark_dirty(page); buffer_unpin(page);
}
static int is(int page, const char *text) {
    char *p = buffer_get_page(page); int r = strcmp(p, text) == 0; buffer_unpin(page); return r;
}

int main(void) {
    remove("m4.db"); remove("m4.wal");
    storage_open("m4.db"); txn_init("m4.wal");
    storage_alloc_page(); storage_alloc_page();

    int t = txn_begin();
    put(0, "balance=500"); put(1, "pc1=FREE");
    assert(txn_commit(t) == 0);
    char disk[PAGE_SIZE]; storage_read_page(0, disk);
    assert(strcmp(disk, "balance=500") == 0);
    printf("PASS commit: change is in the database file\n");

    t = txn_begin();
    put(0, "balance=9999");
    assert(is(0, "balance=9999"));
    storage_read_page(0, disk); assert(strcmp(disk, "balance=500") == 0);   /* not on disk yet */
    txn_rollback(t);
    assert(is(0, "balance=500"));
    printf("PASS rollback: uncommitted change never reached disk and memory is restored\n");

    t = txn_begin(); put(1, "pc1=BUSY"); txn_commit(t);

    printf("\nWAL contents:\n");
    txn_dump_log(stdout);

    /* log is a valid redo source: wipe the data file pages, recover, data returns */
    buffer_flush_all(); storage_close();
    storage_open("m4.db");
    char zero[PAGE_SIZE] = {0};
    storage_write_page(0, zero); storage_write_page(1, zero);        /* simulate lost pages */
    txn_recover();
    storage_read_page(0, disk); assert(strcmp(disk, "balance=500") == 0);
    storage_read_page(1, disk); assert(strcmp(disk, "pc1=BUSY") == 0);
    printf("PASS recovery: committed transactions replayed from the log\n");

    /* test recovery undo: uncommitted transaction rolled back via before-image */
    char old_p0[PAGE_SIZE] = {0};
    char new_p0[PAGE_SIZE] = {0};
    strcpy(old_p0, "balance=500");
    strcpy(new_p0, "balance=12345");
    int uncommitted_t = txn_begin();
    assert(txn_log_write(uncommitted_t, 0, old_p0, new_p0) == 0);
    txn_init("m4.wal");  /* flushes and closes open WAL stream to simulate crash on disk */
    storage_write_page(0, new_p0);
    storage_sync();
    storage_close();

    storage_open("m4.db");
    txn_recover();
    storage_read_page(0, disk);
    assert(strcmp(disk, "balance=500") == 0);
    printf("PASS recovery: uncommitted transaction undone using before-image\n");

    /* test checkpoint: flushes dirty buffer pool pages and empties the WAL log */
    t = txn_begin();
    put(0, "balance=750");
    assert(txn_commit(t) == 0);
    txn_checkpoint();
    FILE *fwal = fopen("m4.wal", "rb");
    assert(fwal != NULL);
    fseek(fwal, 0, SEEK_END);
    assert(ftell(fwal) == 0);
    fclose(fwal);
    storage_read_page(0, disk);
    assert(strcmp(disk, "balance=750") == 0);
    printf("PASS checkpoint: buffer flushed to disk and WAL truncated\n");

    storage_close(); remove("m4.db"); remove("m4.wal");
    printf("MODULE 4: ALL TESTS PASSED\n");
    return 0;
}
