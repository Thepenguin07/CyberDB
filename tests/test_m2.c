/* Module 2 tests: B+ tree (compiled with BT_CAP=4 so splits happen constantly) */
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include "../storage/storage.h"
#include "../buffer/buffer.h"
#include "../index/btree.h"

#define N 3000

int main(void) {
    remove("m2.db");
    storage_open("m2.db");
    int root = btree_create();
    assert(root >= 0 && btree_height(root) == 1);

    /* insert N distinct keys in scrambled order */
    for (int i = 0; i < N; i++) {
        int key = (int)(((long)i * 7919) % N);             /* 7919 is prime: a permutation of 0..N-1 */
        assert(btree_insert(&root, key, key + 100, key % 50) == 0);
    }
    printf("PASS inserted %d keys, tree height = %d\n", N, btree_height(root));
    assert(btree_height(root) >= 4);

    int pg, sl;
    for (int k = 0; k < N; k++) {
        assert(btree_search(root, k, &pg, &sl) == 0);
        assert(pg == k + 100 && sl == k % 50);
    }
    assert(btree_search(root, N + 5, &pg, &sl) == -1 && btree_search(root, -1, &pg, &sl) == -1);
    printf("PASS every key found with the right (page, slot); missing keys rejected\n");

    assert(btree_insert(&root, 10, 1, 1) == -2);
    printf("PASS duplicate key rejected\n");

    for (int k = 0; k < N; k += 2) assert(btree_delete(root, k) == 0);
    for (int k = 0; k < N; k++) assert((btree_search(root, k, &pg, &sl) == 0) == (k % 2 == 1));
    assert(btree_delete(root, 0) == -1);
    printf("PASS deleted all even keys; odd keys remain\n");
    /* reinsert a deleted key and verify it can be found again */
    assert(btree_insert(&root, 0, 999, 7) == 0);
    assert(btree_search(root, 0, &pg, &sl) == 0);
    assert(pg == 999 && sl == 7);

printf("PASS deleted key can be reinserted\n");

    /* persistence: flush, close, reopen, search again */
    buffer_flush_all(); storage_close();
    storage_open("m2.db");
    for (int k = 1; k < N; k += 2) assert(btree_search(root, k, &pg, &sl) == 0);
    printf("PASS index survives close + reopen\n");

    /* tiny tree printout for the demo */
    remove("m2.db"); storage_close(); storage_open("m2.db");
    root = btree_create();
    for (int k = 1; k <= 12; k++) btree_insert(&root, k * 10, k, 0);
    printf("\nTree with keys 10..120 (max 4 keys per node):\n");
    btree_print(root, stdout);

    buffer_flush_all(); storage_close(); remove("m2.db");
    printf("\nMODULE 2: ALL TESTS PASSED\n");
    return 0;
}
