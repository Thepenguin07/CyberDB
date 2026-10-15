#include <string.h>
#include "btree.h"
#include "../storage/storage.h"
#include "../buffer/buffer.h"

/* Maximum keys per node. Small values (e.g. -DBT_CAP=4) make splits easy to observe in tests. */
#ifndef BT_CAP
#define BT_CAP 250
#endif

/* One node = one page.
 * leaf:     keys[i] -> row (a[i] = row page, b[i] = row slot), next = next leaf (range scans)
 * internal: n keys and n+1 children a[0..n]; child a[i] holds keys < keys[i] (and >= keys[i-1]) */
typedef struct {
    int is_leaf, n, next;
    int keys[BT_CAP + 1];          /* +1 slot so a node can overflow briefly before it splits */
    int a[BT_CAP + 2];
    int b[BT_CAP + 1];
} Node;

typedef char node_fits_in_page[(sizeof(Node) <= PAGE_SIZE) ? 1 : -1];

/* first index with keys[i] >= key */
static int lower_bound(const Node *nd, int key) {
    int lo = 0, hi = nd->n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (nd->keys[mid] < key) lo = mid + 1; else hi = mid;
    }
    return lo;
}
/* first index with keys[i] > key (which child to descend into) */
static int upper_bound(const Node *nd, int key) {
    int lo = 0, hi = nd->n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (nd->keys[mid] <= key) lo = mid + 1; else hi = mid;
    }
    return lo;
}

int btree_create(void) {
    int id = storage_alloc_page();
    if (id < 0) return -1;
    Node *nd = (Node *)buffer_get_page(id);
    if (!nd) return -1;
    nd->is_leaf = 1; nd->n = 0; nd->next = -1;
    buffer_mark_dirty(id);
    buffer_unpin(id);
    return id;
}

/* Recursive insert. Returns 0 = done, 1 = node split (promoted key + new right page returned),
 * -1 = error, -2 = duplicate key. */
static int ins(int pid, int key, int rp, int rs, int *pkey, int *pnew) {
    Node *nd = (Node *)buffer_get_page(pid);
    if (!nd) return -1;

    if (nd->is_leaf) {
        int i = lower_bound(nd, key);
        if (i < nd->n && nd->keys[i] == key) { buffer_unpin(pid); return -2; }
        for (int j = nd->n; j > i; j--) {
            nd->keys[j] = nd->keys[j - 1]; nd->a[j] = nd->a[j - 1]; nd->b[j] = nd->b[j - 1];
        }
        nd->keys[i] = key; nd->a[i] = rp; nd->b[i] = rs; nd->n++;
        buffer_mark_dirty(pid);
        if (nd->n <= BT_CAP) { buffer_unpin(pid); return 0; }

        /* leaf split: right half moves to a new leaf, the separator is its first key */
        int newid = storage_alloc_page();
        Node *r = newid < 0 ? NULL : (Node *)buffer_get_page(newid);
        if (!r) { buffer_unpin(pid); return -1; }
        int mid = nd->n / 2;
        r->is_leaf = 1; r->n = nd->n - mid; r->next = nd->next;
        for (int j = 0; j < r->n; j++) {
            r->keys[j] = nd->keys[mid + j]; r->a[j] = nd->a[mid + j]; r->b[j] = nd->b[mid + j];
        }
        nd->n = mid; nd->next = newid;
        *pkey = r->keys[0]; *pnew = newid;
        buffer_mark_dirty(pid); buffer_mark_dirty(newid);
        buffer_unpin(pid); buffer_unpin(newid);
        return 1;
    }

    int i = upper_bound(nd, key);
    int child = nd->a[i];
    buffer_unpin(pid);                                  /* do not hold pins while recursing */
    int ck = 0, cn = 0;
    int rc = ins(child, key, rp, rs, &ck, &cn);
    if (rc != 1) return rc;

    nd = (Node *)buffer_get_page(pid);                  /* child split: insert separator here */
    if (!nd) return -1;
    for (int j = nd->n; j > i; j--) nd->keys[j] = nd->keys[j - 1];
    for (int j = nd->n + 1; j > i + 1; j--) nd->a[j] = nd->a[j - 1];
    nd->keys[i] = ck; nd->a[i + 1] = cn; nd->n++;
    buffer_mark_dirty(pid);
    if (nd->n <= BT_CAP) { buffer_unpin(pid); return 0; }

    /* internal split: the middle key moves UP (it is not kept in either half) */
    int newid = storage_alloc_page();
    Node *r = newid < 0 ? NULL : (Node *)buffer_get_page(newid);
    if (!r) { buffer_unpin(pid); return -1; }
    int mid = nd->n / 2;
    r->is_leaf = 0; r->next = -1; r->n = nd->n - mid - 1;
    for (int j = 0; j < r->n; j++) r->keys[j] = nd->keys[mid + 1 + j];
    for (int j = 0; j <= r->n; j++) r->a[j] = nd->a[mid + 1 + j];
    *pkey = nd->keys[mid]; *pnew = newid;
    nd->n = mid;
    buffer_mark_dirty(pid); buffer_mark_dirty(newid);
    buffer_unpin(pid); buffer_unpin(newid);
    return 1;
}

int btree_insert(int *root, int key, int row_page, int row_slot) {
    int pk = 0, pn = 0;
    int rc = ins(*root, key, row_page, row_slot, &pk, &pn);
    if (rc != 1) return rc;                             /* 0 ok, -1 error, -2 duplicate */

    /* the root itself split: grow the tree by one level */
    int nr = storage_alloc_page();
    Node *nd = nr < 0 ? NULL : (Node *)buffer_get_page(nr);
    if (!nd) return -1;
    nd->is_leaf = 0; nd->next = -1; nd->n = 1;
    nd->keys[0] = pk; nd->a[0] = *root; nd->a[1] = pn;
    buffer_mark_dirty(nr); buffer_unpin(nr);
    *root = nr;
    return 0;
}

/* walk down to the leaf that would contain the key; returns it pinned */
static Node *find_leaf(int root, int key, int *leaf_id) {
    int pid = root;
    for (;;) {
        Node *nd = (Node *)buffer_get_page(pid);
        if (!nd) return NULL;
        if (nd->is_leaf) { *leaf_id = pid; return nd; }
        int next = nd->a[upper_bound(nd, key)];
        buffer_unpin(pid);
        pid = next;
    }
}

int btree_search(int root, int key, int *row_page, int *row_slot) {
    int lid;
    Node *nd = find_leaf(root, key, &lid);
    if (!nd) return -1;
    int i = lower_bound(nd, key), rc = -1;
    if (i < nd->n && nd->keys[i] == key) {
        *row_page = nd->a[i]; *row_slot = nd->b[i]; rc = 0;
    }
    buffer_unpin(lid);
    return rc;
}

int btree_delete(int root, int key) {
    int lid;
    Node *nd = find_leaf(root, key, &lid);
    if (!nd) return -1;
    int i = lower_bound(nd, key);
    if (i >= nd->n || nd->keys[i] != key) { buffer_unpin(lid); return -1; }
    for (int j = i; j < nd->n - 1; j++) {
        nd->keys[j] = nd->keys[j + 1]; nd->a[j] = nd->a[j + 1]; nd->b[j] = nd->b[j + 1];
    }
    nd->n--;
    buffer_mark_dirty(lid);
    buffer_unpin(lid);
    return 0;
}

int btree_height(int root) {
    int h = 1, pid = root;
    for (;;) {
        Node *nd = (Node *)buffer_get_page(pid);
        if (!nd) return -1;
        int leaf = nd->is_leaf, next = nd->a[0];
        buffer_unpin(pid);
        if (leaf) return h;
        pid = next; h++;
    }
}

static void print_node(int pid, int depth, FILE *out) {
    Node *nd = (Node *)buffer_get_page(pid);
    if (!nd) return;
    fprintf(out, "%*s%s page %d keys:", depth * 4, "", nd->is_leaf ? "LEAF    " : "INTERNAL", pid);
    for (int i = 0; i < nd->n; i++) fprintf(out, " %d", nd->keys[i]);
    fprintf(out, "\n");
    int leaf = nd->is_leaf, n = nd->n, kids[BT_CAP + 2];
    if (!leaf) for (int i = 0; i <= n; i++) kids[i] = nd->a[i];
    buffer_unpin(pid);
    if (!leaf) for (int i = 0; i <= n; i++) print_node(kids[i], depth + 1, out);
}

void btree_print(int root, FILE *out) { print_node(root, 0, out); }
