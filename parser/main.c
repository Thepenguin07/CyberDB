/* The `cyberdb` program: reads one command per line on stdin, replies on stdout.
 * Java talks to this through ProcessBuilder (see app/src/app/db/ProcessDatabase.java).
 *
 *   SQL statements          CREATE / INSERT / SELECT / UPDATE / DELETE / BEGIN / COMMIT / ROLLBACK
 *   SESSION START pc user   begin a timed session on a PC
 *   SESSION STOP pc         end it, replies  minutes|bill
 *   SESSION LIST            pc|user|start|minutes|bill
 *   STATS, WALDUMP, STATUS, TOKENS <sql>, EXIT
 * Diagnostics go to stderr so they never corrupt the reply protocol. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include "executor.h"
#include "../buffer/buffer.h"
#include "../transactions/txn.h"
#include "../sessions/sessions.h"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static const char *status_path = "status.json";
static time_t started;

static void write_status(void) {
    long h, m; buffer_stats(&h, &m);
    session_write_status(status_path, (int)getpid(), (long)(time(NULL) - started), h, m, txn_commit_count());
}

/* process monitor: refreshes status.json every 2 seconds for the live dashboard */
static void *monitor(void *arg) {
    (void)arg;
    for (;;) {
        sleep(2);
        pthread_mutex_lock(&lock);
        write_status();
        pthread_mutex_unlock(&lock);
    }
    return NULL;
}

static void handle_session(char *args, char *out, int size) {
    int a, b, minutes, bill;
    if (sscanf(args, "START %d %d", &a, &b) == 2) {
        int rc = session_start(a, b);
        if (rc == 0) snprintf(out, size, "OK\nEND\n");
        else snprintf(out, size, "ERR %s\nEND\n", rc == -1 ? "PC is already in use" :
                      rc == -2 ? "user already has a session" : "session table full");
    } else if (sscanf(args, "STOP %d", &a) == 1) {
        if (session_stop(a, &minutes, &bill) == 0) snprintf(out, size, "OK\n%d|%d\nEND\n", minutes, bill);
        else snprintf(out, size, "ERR no session on that PC\nEND\n");
    } else if (strncmp(args, "LIST", 4) == 0) {
        FILE *tmp = tmpfile();
        char rows[8192] = "";
        if (tmp) { session_print_all(tmp); rewind(tmp); size_t n = fread(rows, 1, sizeof rows - 1, tmp); rows[n] = 0; fclose(tmp); }
        snprintf(out, size, "OK\n%sEND\n", rows);
    } else {
        snprintf(out, size, "ERR usage: SESSION START pc user | STOP pc | LIST\nEND\n");
    }
    write_status();
}

int main(int argc, char **argv) {
    const char *db = argc > 1 ? argv[1] : "cyberdb.db";
    if (getenv("CYBERDB_STATUS")) status_path = getenv("CYBERDB_STATUS");
    if (getenv("CYBERDB_VERBOSE")) buffer_set_verbose(1);
    started = time(NULL);

    if (engine_open(db) != 0) { fprintf(stderr, "cannot open database %s\n", db); return 1; }
    char sess[600]; snprintf(sess, sizeof sess, "%s.sessions", db);
    sessions_init(sess);
    if (getenv("CYBERDB_RATE")) session_set_rate(atoi(getenv("CYBERDB_RATE")));
    write_status();
    pthread_t th; pthread_create(&th, NULL, monitor, NULL); pthread_detach(th);
    fprintf(stderr, "[cyberdb] ready (db=%s)\n", db);

    char line[4096], *out = malloc(1 << 20);
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!*line) continue;
        if (strcasecmp(line, "EXIT") == 0 || strcasecmp(line, "QUIT") == 0) break;

        pthread_mutex_lock(&lock);
        if (strncasecmp(line, "TOKENS ", 7) == 0)      { print_tokens(line + 7, stdout); }
        else if (strncasecmp(line, "SESSION ", 8) == 0) { handle_session(line + 8, out, 1 << 20); fputs(out, stdout); }
        else if (strcasecmp(line, "WALDUMP") == 0)     { printf("OK\n"); txn_dump_log(stdout); printf("END\n"); }
        else if (strcasecmp(line, "STATUS") == 0)      { write_status(); printf("OK\nEND\n"); }
        else                                           { execute_sql(line, out, 1 << 20); fputs(out, stdout); }
        fflush(stdout);                                   /* Java blocks until it sees this */
        pthread_mutex_unlock(&lock);
    }
    pthread_mutex_lock(&lock);
    engine_close();
    pthread_mutex_unlock(&lock);
    free(out);
    return 0;
}
