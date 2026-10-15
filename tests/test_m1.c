/* Module 1 tests: storage engine + buffer pool */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include "../storage/storage.h"
#include "../buffer/buffer.h"

int main(void) {
    remove("m1.db");
    char buf[PAGE_SIZE], out[PAGE_SIZE];

    /* 1. data survives close + reopen */
    assert(storage_open("m1.db") == 0);
    memset(buf, 0, PAGE_SIZE); strcpy(buf, "hello");
    assert(storage_alloc_page() == 0);
    assert(storage_write_page(0, buf) == 0);
    storage_close();
    assert(storage_open("m1.db") == 0);
    assert(storage_read_page(0, out) == 0 && strcmp(out, "hello") == 0);
    printf("PASS persistence: read back '%s' after reopening\n", out);

    /* 2. allocation + bounds */
    assert(storage_alloc_page() == 1 && storage_page_count() == 2);
    assert(storage_read_page(5, out) == -1 && storage_read_page(-1, out) == -1);
    printf("PASS alloc + out-of-range reads rejected\n");

    /* 3. buffer pool hit / miss */
    long h0, m0, h1, m1;
    buffer_stats(&h0, &m0);
    char *p = buffer_get_page(0); assert(p && strcmp(p, "hello") == 0);
    buffer_get_page(0);                                   /* second access = HIT */
    buffer_stats(&h1, &m1);
    assert(m1 - m0 == 1 && h1 - h0 == 1);
    buffer_unpin(0); buffer_unpin(0);
    printf("PASS buffer pool: 1 miss then 1 hit\n");

    /* 4. dirty page reaches disk on flush */
    p = buffer_get_page(0); strcpy(p, "edited"); buffer_mark_dirty(0); buffer_unpin(0);
    buffer_flush_all();
    storage_read_page(0, out); assert(strcmp(out, "edited") == 0);
    printf("PASS dirty page written on flush\n");

    /* 5. eviction: touch 200 pages (pool holds 64), every page must stay correct */
    for (int i = 2; i < 200; i++) assert(storage_alloc_page() == i);
    for (int i = 0; i < 200; i++) {
        p = buffer_get_page(i); assert(p);
        memcpy(p, &i, sizeof i); buffer_mark_dirty(i); buffer_unpin(i);
    }
    for (int i = 0; i < 200; i++) {
        p = buffer_get_page(i); int v; memcpy(&v, p, sizeof v); assert(v == i); buffer_unpin(i);
    }
    printf("PASS eviction: 200 pages through a 64-frame pool, all values intact\n");

    /* 6. pinned pages are never evicted */
    char *keep = buffer_get_page(0);
    for (int i = 1; i < 150; i++) { buffer_get_page(i); buffer_unpin(i); }
    assert(buffer_get_page(0) == keep);
    buffer_unpin(0); buffer_unpin(0);
    printf("PASS pinned page stays in memory\n");

    buffer_flush_all(); storage_close(); remove("m1.db");
    printf("MODULE 1: ALL TESTS PASSED\n");
    return 0;
}
