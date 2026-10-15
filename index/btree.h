#ifndef BTREE_H
#define BTREE_H
#include <stdio.h>
/* Module 2 - B+ Tree index stored in pages (through the buffer pool).
 * Maps an INT key to a row location (page, slot). Each index is identified by the page
 * id of its root; the root can change when it splits, so insert takes a pointer to it. */

int btree_create(void);                                   /* new empty tree, returns root page */
int btree_insert(int *root_page, int key, int row_page, int row_slot); /* 0 ok, -1 error, -2 duplicate */
int btree_search(int root_page, int key, int *row_page, int *row_slot); /* 0 found, -1 not found */
int btree_delete(int root_page, int key);                 /* 0 ok, -1 not found (lazy: no merging) */
int btree_height(int root_page);                          /* 1 = a single leaf */
void btree_print(int root_page, FILE *out);               /* show the tree level by level */
#endif
