#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include "txn.h"
#include "../storage/storage.h"
#include "../buffer/buffer.h"

#define REC_MAGIC  0x57414C31u      /* "WAL1" */
#define T_WRITE    1
#define T_COMMIT   2
#define T_ABORT    3

typedef struct { uint32_t magic, type, txn, page, len, csum; } RecHdr;

static FILE *wal = NULL;
static char wal_path[512] = "cyberdb.wal";
static int next_id = 1;
static long commits = 0;

static uint32_t fnv(uint32_t h, const void *p, size_t n) {
    const unsigned char *s = p;
    while (n--) { h ^= *s++; h *= 16777619u; }
    return h;
}
static uint32_t checksum(const RecHdr *h, const char *payload) {
    uint32_t c = 2166136261u;
    c = fnv(c, &h->type, 4); c = fnv(c, &h->txn, 4);
    c = fnv(c, &h->page, 4); c = fnv(c, &h->len, 4);
    if (h->len) c = fnv(c, payload, h->len);
    return c;
}

void txn_init(const char *path) {
    snprintf(wal_path, sizeof wal_path, "%s", path);
    if (wal) { fclose(wal); wal = NULL; }
}
static int open_wal(void) {
    if (!wal) wal = fopen(wal_path, "ab");
    return wal ? 0 : -1;
}
static int flush_log(void) {                 /* the durability point of WAL */
    if (fflush(wal) != 0) return -1;
    return fsync(fileno(wal));
}

static int append(uint32_t type, int txn, int page, const char *payload, uint32_t len) {
    if (open_wal() != 0) return -1;
    RecHdr h = { REC_MAGIC, type, (uint32_t)txn, (uint32_t)page, len, 0 };
    h.csum = checksum(&h, payload);
    if (fwrite(&h, sizeof h, 1, wal) != 1) return -1;
    if (len && fwrite(payload, 1, len, wal) != len) return -1;
    return 0;
}

int txn_begin(void) {
    int id = next_id++;
    buffer_txn_begin();                       /* pool starts remembering before-images */
    return id;
}

int txn_log_write(int txn, int page, const char *old_data, const char *new_data) {
    char payload[2 * PAGE_SIZE];
    memcpy(payload, old_data, PAGE_SIZE);
    memcpy(payload + PAGE_SIZE, new_data, PAGE_SIZE);
    return append(T_WRITE, txn, page, payload, sizeof payload);
}

int txn_commit(int txn) {
    int ids[1024];
    int n = buffer_txn_pages(ids, 1024);
    for (int i = 0; i < n; i++) {             /* 1. log every changed page */
        const char *before = buffer_before_image(ids[i]);
        const char *after  = buffer_page_data(ids[i]);
        if (!before || !after || txn_log_write(txn, ids[i], before, after) != 0) return -1;
    }
    const char *crash = getenv("CYBERDB_CRASH");  /* test hooks: simulate a power cut */
    if (crash && strcmp(crash, "before_commit") == 0) { flush_log(); _exit(9); }
    if (append(T_COMMIT, txn, 0, NULL, 0) != 0) return -1;
    if (flush_log() != 0) return -1;          /* 2. COMMIT is now durable */
    if (crash && strcmp(crash, "after_commit") == 0) _exit(9);
    buffer_txn_end(1);                        /* 3. only now write pages to the db file */
    commits++;
    return 0;
}

int txn_rollback(int txn) {
    buffer_txn_end(0);                        /* restore before-images, nothing was logged yet */
    if (append(T_ABORT, txn, 0, NULL, 0) == 0) flush_log();
    return 0;
}

long txn_commit_count(void) { return commits; }

/* ---------- reading the log (recovery and dump) ---------- */
typedef struct { long off; uint32_t type, txn, page; } Rec;

static int read_log(Rec **out, int *count) {
    FILE *f = fopen(wal_path, "rb");
    *out = NULL; *count = 0;
    if (!f) return 0;
    int cap = 64; Rec *recs = malloc(cap * sizeof(Rec));
    char *payload = malloc(2 * PAGE_SIZE);
    for (;;) {
        long off = ftell(f);
        RecHdr h;
        if (fread(&h, sizeof h, 1, f) != 1 || h.magic != REC_MAGIC || h.len > 2 * PAGE_SIZE) break;
        if (h.len && fread(payload, 1, h.len, f) != h.len) break;      /* torn tail */
        if (checksum(&h, payload) != h.csum) break;                    /* corrupt tail */
        if (*count == cap) { cap *= 2; recs = realloc(recs, cap * sizeof(Rec)); }
        recs[(*count)++] = (Rec){ off, h.type, h.txn, h.page };
    }
    free(payload); fclose(f);
    *out = recs;
    return 0;
}

static int load_images(long off, char *old_img, char *new_img) {
    FILE *f = fopen(wal_path, "rb");
    if (!f) return -1;
    fseek(f, off + (long)sizeof(RecHdr), SEEK_SET);
    int ok = fread(old_img, 1, PAGE_SIZE, f) == PAGE_SIZE && fread(new_img, 1, PAGE_SIZE, f) == PAGE_SIZE;
    fclose(f);
    return ok ? 0 : -1;
}

void txn_recover(void) {
    Rec *recs; int n;
    read_log(&recs, &n);
    if (n == 0) { free(recs); fprintf(stderr, "[recovery] log empty, nothing to do\n"); return; }

    int maxid = 0;
    for (int i = 0; i < n; i++) if ((int)recs[i].txn > maxid) maxid = recs[i].txn;
    char *state = calloc(maxid + 1, 1);                 /* 0 open, 1 committed, 2 aborted */
    for (int i = 0; i < n; i++) {
        if (recs[i].type == T_COMMIT) state[recs[i].txn] = 1;
        if (recs[i].type == T_ABORT)  state[recs[i].txn] = 2;
    }

    char oldi[PAGE_SIZE], newi[PAGE_SIZE];
    int redone = 0, undone = 0;
    for (int i = 0; i < n; i++)                          /* REDO committed work, in log order */
        if (recs[i].type == T_WRITE && state[recs[i].txn] == 1 &&
            load_images(recs[i].off, oldi, newi) == 0) {
            storage_write_page(recs[i].page, newi); redone++;
        }
    for (int i = n - 1; i >= 0; i--)                     /* UNDO unfinished work, newest first */
        if (recs[i].type == T_WRITE && state[recs[i].txn] == 0 &&
            load_images(recs[i].off, oldi, newi) == 0) {
            storage_write_page(recs[i].page, oldi); undone++;
        }
    storage_sync();
    for (int t = 1; t <= maxid; t++) {
        if (state[t] == 1) fprintf(stderr, "[recovery] txn %d committed -> redone\n", t);
        if (state[t] == 0) fprintf(stderr, "[recovery] txn %d never committed -> undone\n", t);
    }
    fprintf(stderr, "[recovery] %d page(s) redone, %d page(s) undone\n", redone, undone);
    free(state); free(recs);

    if (wal) { fclose(wal); wal = NULL; }
    FILE *f = fopen(wal_path, "wb"); if (f) fclose(f);   /* database is consistent: start a fresh log */
    next_id = maxid + 1;
}

void txn_checkpoint(void) {
    buffer_flush_all();
    if (wal) { fclose(wal); wal = NULL; }
    FILE *f = fopen(wal_path, "wb"); if (f) fclose(f);
}

void txn_dump_log(FILE *out) {
    Rec *recs; int n;
    read_log(&recs, &n);
    for (int i = 0; i < n; i++) {
        const char *name = recs[i].type == T_WRITE ? "WRITE " : recs[i].type == T_COMMIT ? "COMMIT" : "ABORT ";
        if (recs[i].type == T_WRITE)
            fprintf(out, "%d|%s|txn=%u|page=%u\n", i, name, recs[i].txn, recs[i].page);
        else
            fprintf(out, "%d|%s|txn=%u\n", i, name, recs[i].txn);
    }
    free(recs);
}
