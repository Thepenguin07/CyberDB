#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdarg.h>
#include <limits.h>
#include <errno.h>
#include "executor.h"
#include "../storage/storage.h"
#include "../buffer/buffer.h"
#include "../index/btree.h"
#include "../transactions/txn.h"

/* ================= catalog (lives in page 0) ================= */
#define MAX_TABLES 8
#define MAX_COLS   8
#define NAME_LEN   32
#define TEXT_LEN   80            /* TEXT columns: up to 79 characters (SHA-256 hex = 64 fits) */
#define T_INT  0
#define T_TEXT 1
#define CAT_MAGIC 0x43594244

typedef struct { char name[NAME_LEN]; int type; } Col;
typedef struct {
    int  used;
    char name[NAME_LEN];
    int  ncols;
    Col  cols[MAX_COLS];
    int  row_size;
    int  heap_first;             /* first page of the table's row pages (linked list) */
    int  index_root;             /* root page of the B+ tree on column 0 */
} Table;
typedef struct { int magic; Table t[MAX_TABLES]; } Catalog;
typedef char catalog_fits[(sizeof(Catalog) <= PAGE_SIZE) ? 1 : -1];

typedef struct { int page, slot; } Rid;

/* ================= engine state ================= */
static int  in_txn = 0, cur_txn = 0;
static long n_statements = 0, n_index_lookups = 0, n_full_scans = 0;
static char errmsg[256];

#define USER_ERR  1              /* nothing was changed */
#define FATAL_ERR 2              /* transaction must be rolled back */

static int fail(int code, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vsnprintf(errmsg, sizeof errmsg, fmt, ap);
    va_end(ap);
    return code;
}

/* ================= output buffer ================= */
typedef struct { char *buf; int size, len; } Out;
static void out_add(Out *o, const char *s) {
    int n = (int)strlen(s);
    if (o->len + n >= o->size) { o->len = o->size; return; }   /* len==size marks overflow */
    memcpy(o->buf + o->len, s, n + 1);
    o->len += n;
}
static int out_overflow(const Out *o) { return o->len >= o->size; }

/* ================= tokenizer ================= */
#define MAX_TOK 256
#define TOK_LEN 128
typedef struct { char s[TOK_LEN]; int quoted; } Tok;

static int tokenize(const char *s, Tok *t, int max) {
    int n = 0;
    while (*s) {
        if (isspace((unsigned char)*s)) { s++; continue; }
        if (n >= max) { fail(USER_ERR, "statement too long"); return -1; }
        int k = 0;
        t[n].quoted = 0;
        if (*s == '\'') {                                  /* 'quoted text' */
            s++;
            while (*s && *s != '\'') { if (k < TOK_LEN - 1) t[n].s[k++] = *s; s++; }
            if (*s != '\'') { fail(USER_ERR, "unterminated string"); return -1; }
            s++;
            t[n].quoted = 1;
        } else if (strchr("(),;=*", *s)) {                 /* single-character symbols */
            t[n].s[k++] = *s++;
        } else {                                           /* words and numbers */
            while (*s && !isspace((unsigned char)*s) && !strchr("(),;='", *s)) {
                if (k < TOK_LEN - 1) t[n].s[k++] = *s;
                s++;
            }
        }
        t[n].s[k] = 0;
        n++;
    }
    return n;
}

void print_tokens(const char *sql, FILE *out) {
    Tok t[MAX_TOK];
    int n = tokenize(sql, t, MAX_TOK);
    if (n < 0) { fprintf(out, "ERR %s\n", errmsg); return; }
    for (int i = 0; i < n; i++)
        fprintf(out, "[%d] %-10s %s\n", i, t[i].quoted ? "STRING" : "WORD/SYMBOL", t[i].s);
}

/* ================= parser helpers ================= */
typedef struct { Tok *t; int n, pos; } Parser;

static int at_end(Parser *p) { return p->pos >= p->n; }
static int is_sym(Parser *p, const char *s) {
    return !at_end(p) && !p->t[p->pos].quoted && strcmp(p->t[p->pos].s, s) == 0;
}
static int is_kw(Parser *p, const char *kw) {
    return !at_end(p) && !p->t[p->pos].quoted && strcasecmp(p->t[p->pos].s, kw) == 0;
}
static int accept_sym(Parser *p, const char *s) { if (is_sym(p, s)) { p->pos++; return 1; } return 0; }
static int accept_kw(Parser *p, const char *kw) { if (is_kw(p, kw)) { p->pos++; return 1; } return 0; }
static int expect_kw(Parser *p, const char *kw) {
    if (accept_kw(p, kw)) return 0;
    return fail(USER_ERR, "syntax error: expected %s", kw);
}
static int expect_sym(Parser *p, const char *s) {
    if (accept_sym(p, s)) return 0;
    return fail(USER_ERR, "syntax error: expected '%s'", s);
}
static Tok *next_tok(Parser *p) { return at_end(p) ? NULL : &p->t[p->pos++]; }

static int valid_ident(const char *s) {
    if (!*s || strlen(s) >= NAME_LEN || isdigit((unsigned char)*s)) return 0;
    for (; *s; s++) if (!isalnum((unsigned char)*s) && *s != '_') return 0;
    return 1;
}

/* ================= catalog access ================= */
static int cat_find(const char *name, Table *out, int *idx) {
    Catalog *c = (Catalog *)buffer_get_page(0);
    if (!c) return -1;
    int found = -1;
    for (int i = 0; i < MAX_TABLES; i++)
        if (c->t[i].used && strcasecmp(c->t[i].name, name) == 0) { *out = c->t[i]; *idx = i; found = 0; break; }
    buffer_unpin(0);
    return found;
}
static int cat_save(int idx, const Table *tb) {
    Catalog *c = (Catalog *)buffer_get_page(0);
    if (!c) return -1;
    c->t[idx] = *tb;
    buffer_mark_dirty(0);
    buffer_unpin(0);
    return 0;
}

/* ================= row layout ================= */
static int col_size(int type) { return type == T_INT ? 4 : TEXT_LEN; }
static int col_off(const Table *t, int c) {
    int off = 0;
    for (int i = 0; i < c; i++) off += col_size(t->cols[i].type);
    return off;
}
static void col_str(const Table *t, const char *row, int c, char *out) {   /* out: TEXT_LEN+1 */
    int off = col_off(t, c);
    if (t->cols[c].type == T_INT) { int v; memcpy(&v, row + off, 4); snprintf(out, TEXT_LEN + 1, "%d", v); }
    else { memcpy(out, row + off, TEXT_LEN); out[TEXT_LEN] = 0; }
}
static int col_index(const Table *t, const char *name) {
    for (int i = 0; i < t->ncols; i++) if (strcasecmp(t->cols[i].name, name) == 0) return i;
    return -1;
}

/* Convert a literal into column storage. int_val is set for INT columns. */
static int parse_value(const Table *t, int c, const Tok *v, char *dst) {
    if (t->cols[c].type == T_INT) {
        if (v->quoted) return fail(USER_ERR, "column %s expects a number, got text", t->cols[c].name);
        char *end; errno = 0;
        long x = strtol(v->s, &end, 10);
        if (*v->s == 0 || *end != 0 || x > INT_MAX || x < INT_MIN)
            return fail(USER_ERR, "'%s' is not a valid INT for %s", v->s, t->cols[c].name);
        int iv = (int)x;
        memcpy(dst, &iv, 4);
    } else {
        if (!v->quoted) return fail(USER_ERR, "column %s expects 'text' in quotes", t->cols[c].name);
        if (strlen(v->s) >= TEXT_LEN) return fail(USER_ERR, "text too long for %s (max %d)", t->cols[c].name, TEXT_LEN - 1);
        if (strchr(v->s, '|') || strchr(v->s, '\n')) return fail(USER_ERR, "text may not contain '|' or newline");
        memset(dst, 0, TEXT_LEN);
        memcpy(dst, v->s, strlen(v->s));
    }
    return 0;
}

/* ================= heap (row pages) ================= */
/* page layout: [next_page:int][reserved:int] then slots of [used:1 byte][row bytes] */
static int slots_per_page(const Table *t) { return (PAGE_SIZE - 8) / (1 + t->row_size); }
static char *slot_ptr(char *page, const Table *t, int s) { return page + 8 + s * (1 + t->row_size); }

static int heap_new_page(void) {
    int id = storage_alloc_page();
    if (id < 0) return -1;
    char *p = buffer_get_page(id);
    if (!p) return -1;
    int nx = -1; memcpy(p, &nx, 4);
    buffer_mark_dirty(id); buffer_unpin(id);
    return id;
}

static int put_in_page(const Table *t, int pid, const char *row, Rid *r) {
    char *pg = buffer_get_page(pid);
    if (!pg) return -1;
    int spp = slots_per_page(t);
    for (int s = 0; s < spp; s++) {
        char *sp = slot_ptr(pg, t, s);
        if (!sp[0]) {
            sp[0] = 1; memcpy(sp + 1, row, t->row_size);
            buffer_mark_dirty(pid); buffer_unpin(pid);
            r->page = pid; r->slot = s;
            return 0;
        }
    }
    buffer_unpin(pid);
    return 1;                                            /* page full */
}

static int heap_insert(const Table *t, const char *row, Rid *r) {
    int pid = t->heap_first, last = -1;
    while (pid != -1) {
        int rc = put_in_page(t, pid, row, r);
        if (rc <= 0) return rc;
        char *pg = buffer_get_page(pid);
        if (!pg) return -1;
        int nx; memcpy(&nx, pg, 4);
        buffer_unpin(pid);
        last = pid; pid = nx;
    }
    int np = heap_new_page();                            /* all pages full: chain a new one */
    if (np < 0) return -1;
    char *pg = buffer_get_page(last);
    if (!pg) return -1;
    memcpy(pg, &np, 4);
    buffer_mark_dirty(last); buffer_unpin(last);
    return put_in_page(t, np, row, r);
}

static int heap_read(const Table *t, Rid r, char *row) {
    char *pg = buffer_get_page(r.page);
    if (!pg) return -1;
    char *sp = slot_ptr(pg, t, r.slot);
    int ok = sp[0];
    if (ok) memcpy(row, sp + 1, t->row_size);
    buffer_unpin(r.page);
    return ok ? 0 : -1;
}
static int heap_write(const Table *t, Rid r, const char *row) {
    char *pg = buffer_get_page(r.page);
    if (!pg) return -1;
    memcpy(slot_ptr(pg, t, r.slot) + 1, row, t->row_size);
    buffer_mark_dirty(r.page); buffer_unpin(r.page);
    return 0;
}
static int heap_free(const Table *t, Rid r) {
    char *pg = buffer_get_page(r.page);
    if (!pg) return -1;
    slot_ptr(pg, t, r.slot)[0] = 0;
    buffer_mark_dirty(r.page); buffer_unpin(r.page);
    return 0;
}

/* ================= finding rows (index or full scan) ================= */
typedef struct { Rid *a; int n, cap; } RidList;
static int rl_add(RidList *l, Rid r) {
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        Rid *na = realloc(l->a, l->cap * sizeof(Rid));
        if (!na) return -1;
        l->a = na;
    }
    l->a[l->n++] = r;
    return 0;
}

/* wcol < 0: all rows.  Otherwise rows whose column wcol (as text) equals wval.
 * Uses the B+ tree when the condition is on the first (primary key) column. */
static int find_rows(const Table *t, int wcol, const char *wval, RidList *out) {
    char row[PAGE_SIZE], tmp[TEXT_LEN + 1];
    if (wcol == 0) {
        n_index_lookups++;
        int pg, sl;
        if (btree_search(t->index_root, atoi(wval), &pg, &sl) == 0) {
            Rid r = { pg, sl };
            if (heap_read(t, r, row) == 0) rl_add(out, r);
        }
        return 0;
    }
    n_full_scans++;
    int spp = slots_per_page(t), pid = t->heap_first;
    while (pid != -1) {
        char *pgp = buffer_get_page(pid);
        if (!pgp) return -1;
        int nx; memcpy(&nx, pgp, 4);
        for (int s = 0; s < spp; s++) {
            char *sp = slot_ptr(pgp, t, s);
            if (!sp[0]) continue;
            if (wcol >= 0) {
                col_str(t, sp + 1, wcol, tmp);
                if (strcmp(tmp, wval) != 0) continue;
            }
            Rid r = { pid, s };
            if (rl_add(out, r) != 0) { buffer_unpin(pid); return -1; }
        }
        buffer_unpin(pid);
        pid = nx;
    }
    return 0;
}

/* optional WHERE col = value; normalises the value to the text form used by col_str */
static int parse_where(Parser *p, const Table *t, int *wcol, char *wval) {
    *wcol = -1;
    if (!accept_kw(p, "WHERE")) return 0;
    Tok *c = next_tok(p);
    if (!c) return fail(USER_ERR, "syntax error: expected column after WHERE");
    int ci = col_index(t, c->s);
    if (ci < 0) return fail(USER_ERR, "unknown column '%s'", c->s);
    if (expect_sym(p, "=") != 0) return USER_ERR;
    Tok *v = next_tok(p);
    if (!v) return fail(USER_ERR, "syntax error: expected a value after =");
    char buf[TEXT_LEN];
    if (parse_value(t, ci, v, buf) != 0) return USER_ERR;
    if (t->cols[ci].type == T_INT) { int x; memcpy(&x, buf, 4); snprintf(wval, TEXT_LEN + 1, "%d", x); }
    else snprintf(wval, TEXT_LEN + 1, "%s", v->s);
    *wcol = ci;
    return 0;
}

/* ================= statements ================= */
static int do_create(Parser *p) {
    if (expect_kw(p, "TABLE")) return USER_ERR;
    Tok *nm = next_tok(p);
    if (!nm || !valid_ident(nm->s)) return fail(USER_ERR, "invalid table name");
    Table tb; int idx;
    if (cat_find(nm->s, &tb, &idx) == 0) return fail(USER_ERR, "table %s already exists", nm->s);
    memset(&tb, 0, sizeof tb);
    snprintf(tb.name, NAME_LEN, "%s", nm->s);
    if (expect_sym(p, "(")) return USER_ERR;
    do {
        Tok *cn = next_tok(p), *ty = next_tok(p);
        if (!cn || !ty || !valid_ident(cn->s)) return fail(USER_ERR, "syntax error in column list");
        if (tb.ncols >= MAX_COLS) return fail(USER_ERR, "too many columns (max %d)", MAX_COLS);
        for (int i = 0; i < tb.ncols; i++)
            if (strcasecmp(tb.cols[i].name, cn->s) == 0) return fail(USER_ERR, "duplicate column %s", cn->s);
        if (strcasecmp(ty->s, "INT") == 0) tb.cols[tb.ncols].type = T_INT;
        else if (strcasecmp(ty->s, "TEXT") == 0) tb.cols[tb.ncols].type = T_TEXT;
        else return fail(USER_ERR, "unknown type %s (use INT or TEXT)", ty->s);
        snprintf(tb.cols[tb.ncols].name, NAME_LEN, "%s", cn->s);
        tb.ncols++;
    } while (accept_sym(p, ","));
    if (expect_sym(p, ")")) return USER_ERR;
    if (tb.cols[0].type != T_INT) return fail(USER_ERR, "first column must be INT (it is the primary key)");

    for (int i = 0; i < tb.ncols; i++) tb.row_size += col_size(tb.cols[i].type);
    Catalog *c = (Catalog *)buffer_get_page(0);
    if (!c) return fail(FATAL_ERR, "buffer pool exhausted");
    int slot = -1;
    for (int i = 0; i < MAX_TABLES; i++) if (!c->t[i].used) { slot = i; break; }
    buffer_unpin(0);
    if (slot < 0) return fail(USER_ERR, "catalog full (max %d tables)", MAX_TABLES);

    tb.used = 1;
    tb.heap_first = heap_new_page();
    tb.index_root = btree_create();
    if (tb.heap_first < 0 || tb.index_root < 0) return fail(FATAL_ERR, "out of space");
    if (cat_save(slot, &tb) != 0) return fail(FATAL_ERR, "cannot save catalog");
    return 0;
}

static int do_insert(Parser *p) {
    if (expect_kw(p, "INTO")) return USER_ERR;
    Tok *nm = next_tok(p);
    Table tb; int idx;
    if (!nm || cat_find(nm->s, &tb, &idx) != 0) return fail(USER_ERR, "no such table");
    if (expect_kw(p, "VALUES") || expect_sym(p, "(")) return USER_ERR;
    char row[PAGE_SIZE]; memset(row, 0, sizeof row);
    for (int i = 0; i < tb.ncols; i++) {
        if (i > 0 && expect_sym(p, ",")) return fail(USER_ERR, "expected %d values", tb.ncols);
        Tok *v = next_tok(p);
        if (!v) return fail(USER_ERR, "expected %d values", tb.ncols);
        if (parse_value(&tb, i, v, row + col_off(&tb, i)) != 0) return USER_ERR;
    }
    if (expect_sym(p, ")")) return fail(USER_ERR, "expected %d values", tb.ncols);

    int key; memcpy(&key, row, 4);
    int pg, sl;
    if (btree_search(tb.index_root, key, &pg, &sl) == 0)          /* duplicate check BEFORE any change */
        return fail(USER_ERR, "duplicate primary key %d", key);
    Rid r;
    if (heap_insert(&tb, row, &r) != 0) return fail(FATAL_ERR, "cannot store row");
    int root = tb.index_root;
    if (btree_insert(&root, key, r.page, r.slot) != 0) return fail(FATAL_ERR, "index insert failed");
    if (root != tb.index_root) { tb.index_root = root; if (cat_save(idx, &tb)) return fail(FATAL_ERR, "catalog save failed"); }
    return 0;
}

static int do_select(Parser *p, Out *o) {
    if (expect_sym(p, "*") || expect_kw(p, "FROM")) return USER_ERR;
    Tok *nm = next_tok(p);
    Table tb; int idx;
    if (!nm || cat_find(nm->s, &tb, &idx) != 0) return fail(USER_ERR, "no such table");
    int wcol; char wval[TEXT_LEN + 1];
    if (parse_where(p, &tb, &wcol, wval) != 0) return USER_ERR;
    if (!at_end(p)) return fail(USER_ERR, "unexpected '%s'", p->t[p->pos].s);

    RidList L = {0};
    if (find_rows(&tb, wcol, wval, &L) != 0) { free(L.a); return fail(FATAL_ERR, "read failed"); }
    char row[PAGE_SIZE], cell[TEXT_LEN + 1];
    for (int i = 0; i < L.n; i++) {
        if (heap_read(&tb, L.a[i], row) != 0) continue;
        for (int c = 0; c < tb.ncols; c++) {
            col_str(&tb, row, c, cell);
            if (c) out_add(o, "|");
            out_add(o, cell);
        }
        out_add(o, "\n");
    }
    free(L.a);
    if (out_overflow(o)) return fail(USER_ERR, "result too large for output buffer");
    return 0;
}

static int do_update(Parser *p) {
    Tok *nm = next_tok(p);
    Table tb; int idx;
    if (!nm || cat_find(nm->s, &tb, &idx) != 0) return fail(USER_ERR, "no such table");
    if (expect_kw(p, "SET")) return USER_ERR;
    int sc[MAX_COLS]; char sv[MAX_COLS][TEXT_LEN]; int ns = 0;
    do {
        Tok *cn = next_tok(p);
        if (!cn) return fail(USER_ERR, "syntax error in SET");
        int ci = col_index(&tb, cn->s);
        if (ci < 0) return fail(USER_ERR, "unknown column '%s'", cn->s);
        if (ci == 0) return fail(USER_ERR, "cannot update the primary key column");
        if (expect_sym(p, "=")) return USER_ERR;
        Tok *v = next_tok(p);
        if (!v || ns >= MAX_COLS) return fail(USER_ERR, "syntax error in SET");
        if (parse_value(&tb, ci, v, sv[ns]) != 0) return USER_ERR;
        sc[ns++] = ci;
    } while (accept_sym(p, ","));
    int wcol; char wval[TEXT_LEN + 1];
    if (parse_where(p, &tb, &wcol, wval) != 0) return USER_ERR;
    if (!at_end(p)) return fail(USER_ERR, "unexpected '%s'", p->t[p->pos].s);

    RidList L = {0};
    if (find_rows(&tb, wcol, wval, &L) != 0) { free(L.a); return fail(FATAL_ERR, "read failed"); }
    char row[PAGE_SIZE];
    for (int i = 0; i < L.n; i++) {
        if (heap_read(&tb, L.a[i], row) != 0) continue;
        for (int k = 0; k < ns; k++) memcpy(row + col_off(&tb, sc[k]), sv[k], col_size(tb.cols[sc[k]].type));
        if (heap_write(&tb, L.a[i], row) != 0) { free(L.a); return fail(FATAL_ERR, "write failed"); }
    }
    free(L.a);
    return 0;
}

static int do_delete(Parser *p) {
    if (expect_kw(p, "FROM")) return USER_ERR;
    Tok *nm = next_tok(p);
    Table tb; int idx;
    if (!nm || cat_find(nm->s, &tb, &idx) != 0) return fail(USER_ERR, "no such table");
    int wcol; char wval[TEXT_LEN + 1];
    if (parse_where(p, &tb, &wcol, wval) != 0) return USER_ERR;
    if (!at_end(p)) return fail(USER_ERR, "unexpected '%s'", p->t[p->pos].s);

    RidList L = {0};
    if (find_rows(&tb, wcol, wval, &L) != 0) { free(L.a); return fail(FATAL_ERR, "read failed"); }
    char row[PAGE_SIZE];
    for (int i = 0; i < L.n; i++) {
        if (heap_read(&tb, L.a[i], row) != 0) continue;
        int key; memcpy(&key, row, 4);
        btree_delete(tb.index_root, key);
        if (heap_free(&tb, L.a[i]) != 0) { free(L.a); return fail(FATAL_ERR, "write failed"); }
    }
    free(L.a);
    return 0;
}

/* ================= engine lifecycle ================= */
int engine_open(const char *db_path) {
    char wal[600];
    if (storage_open(db_path) != 0) return -1;
    snprintf(wal, sizeof wal, "%s.wal", db_path);
    txn_init(wal);
    txn_recover();                                         /* fix anything a crash left behind */
    if (storage_page_count() == 0) {                       /* brand-new database: write the catalog page */
        storage_alloc_page();
        Catalog *c = (Catalog *)buffer_get_page(0);
        if (!c) return -1;
        memset(c, 0, sizeof(Catalog));
        c->magic = CAT_MAGIC;
        buffer_mark_dirty(0); buffer_unpin(0);
        buffer_flush_all();
    }
    return 0;
}

void engine_close(void) {
    if (in_txn) { txn_rollback(cur_txn); in_txn = 0; }
    txn_checkpoint();
    storage_close();
}

/* ================= top level ================= */
static int reply_err(char *out, int size, const char *msg) {
    snprintf(out, size, "ERR %s\nEND\n", msg);
    return -1;
}

int execute_sql(const char *sql, char *out, int size) {
    Out o = { out, size, 0 };
    out[0] = 0;
    Tok t[MAX_TOK];
    int n = tokenize(sql, t, MAX_TOK);
    if (n < 0) return reply_err(out, size, errmsg);
    while (n > 0 && !t[n - 1].quoted && strcmp(t[n - 1].s, ";") == 0) n--;   /* drop trailing ; */
    if (n == 0) return reply_err(out, size, "empty statement");
    Parser p = { t, n, 1 };
    n_statements++;

    if (strcasecmp(t[0].s, "BEGIN") == 0) {
        if (in_txn) return reply_err(out, size, "a transaction is already active");
        cur_txn = txn_begin(); in_txn = 1;
        snprintf(out, size, "OK\nEND\n"); return 0;
    }
    if (strcasecmp(t[0].s, "COMMIT") == 0) {
        if (!in_txn) return reply_err(out, size, "no active transaction");
        int rc = txn_commit(cur_txn); in_txn = 0;
        if (rc != 0) { txn_rollback(cur_txn); return reply_err(out, size, "commit failed, rolled back"); }
        snprintf(out, size, "OK\nEND\n"); return 0;
    }
    if (strcasecmp(t[0].s, "ROLLBACK") == 0) {
        if (!in_txn) return reply_err(out, size, "no active transaction");
        txn_rollback(cur_txn); in_txn = 0;
        snprintf(out, size, "OK\nEND\n"); return 0;
    }
    if (strcasecmp(t[0].s, "STATS") == 0) {
        long h, m; buffer_stats(&h, &m);
        snprintf(out, size, "OK\nstatements|%ld\nindex_lookups|%ld\nfull_scans|%ld\n"
                 "buffer_hits|%ld\nbuffer_misses|%ld\ntxn_commits|%ld\nEND\n",
                 n_statements, n_index_lookups, n_full_scans, h, m, txn_commit_count());
        return 0;
    }

    int auto_txn = !in_txn;
    if (auto_txn) { cur_txn = txn_begin(); in_txn = 1; }   /* every statement is atomic */
    out_add(&o, "OK\n");
    int rc;
    if      (strcasecmp(t[0].s, "CREATE") == 0) rc = do_create(&p);
    else if (strcasecmp(t[0].s, "INSERT") == 0) rc = do_insert(&p);
    else if (strcasecmp(t[0].s, "SELECT") == 0) rc = do_select(&p, &o);
    else if (strcasecmp(t[0].s, "UPDATE") == 0) rc = do_update(&p);
    else if (strcasecmp(t[0].s, "DELETE") == 0) rc = do_delete(&p);
    else rc = fail(USER_ERR, "unsupported statement '%s'", t[0].s);

    if (rc == 0 && !at_end(&p) && strcasecmp(t[0].s, "SELECT") != 0 && strcasecmp(t[0].s, "UPDATE") != 0 &&
        strcasecmp(t[0].s, "DELETE") != 0)
        rc = fail(USER_ERR, "unexpected '%s' at end of statement", p.t[p.pos].s);

    if (rc == 0) {
        if (auto_txn) {
            int c = txn_commit(cur_txn); in_txn = 0;
            if (c != 0) { txn_rollback(cur_txn); return reply_err(out, size, "commit failed"); }
        }
        out_add(&o, "END\n");
        if (out_overflow(&o)) return reply_err(out, size, "result too large");
        return 0;
    }
    if (auto_txn || rc == FATAL_ERR) {
        txn_rollback(cur_txn); in_txn = 0;
        if (!auto_txn) { char m[300]; snprintf(m, sizeof m, "%s (transaction rolled back)", errmsg); return reply_err(out, size, m); }
    }
    return reply_err(out, size, errmsg);
}
