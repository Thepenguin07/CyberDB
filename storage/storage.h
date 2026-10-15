#ifndef STORAGE_H
#define STORAGE_H
/* Module 1 - Storage Engine: the database file is an array of fixed-size pages. */
#define PAGE_SIZE 4096

int  storage_open(const char *filename);              /* open or create the file   */
int  storage_read_page(int page_id, char *buf);       /* 0 ok, -1 failure/out of range */
int  storage_write_page(int page_id, const char *buf);/* 0 ok, -1 failure          */
int  storage_alloc_page(void);                        /* append a zeroed page, return its id */
int  storage_page_count(void);                        /* number of pages in the file */
int  storage_sync(void);                              /* force data to the disk (fsync) */
void storage_close(void);
#endif
