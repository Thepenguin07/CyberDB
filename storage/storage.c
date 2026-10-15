#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "storage.h"

static FILE *fp = NULL;

int storage_open(const char *filename) {
    if (fp) storage_close();
    fp = fopen(filename, "r+b");            /* existing database */
    if (!fp) fp = fopen(filename, "w+b");   /* otherwise create a new one */
    return fp ? 0 : -1;
}

int storage_page_count(void) {
    if (!fp) return -1;
    if (fseek(fp, 0, SEEK_END) != 0) return -1;
    long size = ftell(fp);
    return (int)(size / PAGE_SIZE);
}

int storage_read_page(int page_id, char *buf) {
    if (!fp || page_id < 0 || page_id >= storage_page_count()) return -1;
    if (fseek(fp, (long)page_id * PAGE_SIZE, SEEK_SET) != 0) return -1;
    return fread(buf, 1, PAGE_SIZE, fp) == PAGE_SIZE ? 0 : -1;
}

int storage_write_page(int page_id, const char *buf) {
    if (!fp || page_id < 0) return -1;
    if (fseek(fp, (long)page_id * PAGE_SIZE, SEEK_SET) != 0) return -1;
    if (fwrite(buf, 1, PAGE_SIZE, fp) != PAGE_SIZE) return -1;
    return fflush(fp) == 0 ? 0 : -1;
}

int storage_alloc_page(void) {
    int id = storage_page_count();
    if (id < 0) return -1;
    char zero[PAGE_SIZE];
    memset(zero, 0, sizeof zero);
    return storage_write_page(id, zero) == 0 ? id : -1;
}

int storage_sync(void) {
    if (!fp) return -1;
    if (fflush(fp) != 0) return -1;
    return fsync(fileno(fp));
}

void storage_close(void) {
    if (fp) { fflush(fp); fclose(fp); }
    fp = NULL;
}
