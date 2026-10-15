#include <stdio.h>
#include <string.h>
#include "sessions.h"

#define MAXS 64

typedef struct { int pc, user, active; time_t start; } Session;
static Session list[MAXS];
static char path[512] = "sessions.dat";
static int rate = 2;

static void save(void) {
    char tmp[600];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    for (int i = 0; i < MAXS; i++)
        if (list[i].active) fprintf(f, "%d %d %ld\n", list[i].pc, list[i].user, (long)list[i].start);
    fclose(f);
    rename(tmp, path);                       /* atomic replace: never a half-written file */
}

int sessions_init(const char *file) {
    snprintf(path, sizeof path, "%s", file);
    memset(list, 0, sizeof list);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    int pc, user, i = 0; long start;
    while (i < MAXS && fscanf(f, "%d %d %ld", &pc, &user, &start) == 3) {
        list[i].pc = pc; list[i].user = user; list[i].start = (time_t)start; list[i].active = 1; i++;
    }
    fclose(f);
    return i;
}

void session_set_rate(int r) { if (r > 0) rate = r; }

int elapsed_minutes(time_t start, time_t now) {
    long secs = (long)(now - start);
    if (secs < 0) secs = 0;
    int m = (int)((secs + 59) / 60);         /* round UP: 61 seconds bills 2 minutes */
    return m < 1 ? 1 : m;                    /* minimum 1 minute */
}

int session_start_at(int pc, int user, time_t start) {
    int freeslot = -1;
    for (int i = 0; i < MAXS; i++) {
        if (list[i].active && list[i].pc == pc)     return -1;   /* no double-booked PC */
        if (list[i].active && list[i].user == user) return -2;  /* one PC per user */
        if (!list[i].active && freeslot < 0) freeslot = i;
    }
    if (freeslot < 0) return -3;
    list[freeslot].pc = pc; list[freeslot].user = user;
    list[freeslot].start = start; list[freeslot].active = 1;
    save();
    return 0;
}

int session_start(int pc, int user) { return session_start_at(pc, user, time(NULL)); }

int session_stop(int pc, int *minutes, int *bill) {
    for (int i = 0; i < MAXS; i++)
        if (list[i].active && list[i].pc == pc) {
            *minutes = elapsed_minutes(list[i].start, time(NULL));
            *bill = *minutes * rate;
            list[i].active = 0;
            save();
            return 0;
        }
    return -1;
}

int session_count(void) {
    int n = 0;
    for (int i = 0; i < MAXS; i++) if (list[i].active) n++;
    return n;
}

void session_print_all(FILE *out) {
    time_t now = time(NULL);
    for (int i = 0; i < MAXS; i++)
        if (list[i].active) {
            int m = elapsed_minutes(list[i].start, now);
            fprintf(out, "%d|%d|%ld|%d|%d\n", list[i].pc, list[i].user, (long)list[i].start, m, m * rate);
        }
}

int session_write_status(const char *json_path, int pid, long uptime, long hits, long misses, long commits) {
    char tmp[600];
    snprintf(tmp, sizeof tmp, "%s.tmp", json_path);
    FILE *f = fopen(tmp, "w");
    if (!f) return -1;
    fprintf(f, "{\n  \"generated_at\": %ld,\n  \"rate_per_minute\": %d,\n", (long)time(NULL), rate);
    fprintf(f, "  \"engine\": {\"pid\": %d, \"uptime_seconds\": %ld, \"buffer_hits\": %ld, "
               "\"buffer_misses\": %ld, \"txn_commits\": %ld},\n", pid, uptime, hits, misses, commits);
    fprintf(f, "  \"sessions\": [");
    int first = 1;
    for (int i = 0; i < MAXS; i++)
        if (list[i].active) {
            fprintf(f, "%s\n    {\"pc\": %d, \"user\": %d, \"start\": %ld}", first ? "" : ",",
                    list[i].pc, list[i].user, (long)list[i].start);
            first = 0;
        }
    fprintf(f, "%s]\n}\n", first ? "" : "\n  ");
    fclose(f);
    return rename(tmp, json_path);
}
