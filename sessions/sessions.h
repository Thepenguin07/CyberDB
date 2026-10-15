#ifndef SESSIONS_H
#define SESSIONS_H
#include <stdio.h>
#include <time.h>
/* Module 5 - Session Orchestration + Process Monitoring.
 * Tracks which user is on which PC, since when, and what they owe. State is saved to a file
 * so sessions survive an engine restart. The monitor writes status.json for the live dashboard. */

int  sessions_init(const char *file);                       /* load saved sessions */
void session_set_rate(int rupees_per_minute);
int  session_start(int pc_id, int user_id);                 /* 0 ok, -1 PC busy, -2 user already on a PC, -3 full */
int  session_start_at(int pc_id, int user_id, time_t start);/* same, with an explicit start (tests) */
int  session_stop(int pc_id, int *minutes, int *bill);      /* 0 ok, -1 no session on that PC */
int  elapsed_minutes(time_t start, time_t now);             /* rounded UP, minimum 1 */
int  session_count(void);
void session_print_all(FILE *out);                          /* pc|user|start|minutes|bill per line */
int  session_write_status(const char *json_path, int engine_pid, long uptime_s,
                          long buf_hits, long buf_misses, long txn_commits);
#endif
