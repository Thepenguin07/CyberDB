/* Module 5 tests: sessions + billing + monitor output */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include "../sessions/sessions.h"

int main(void) {
    remove("m5.dat");
    sessions_init("m5.dat");
    session_set_rate(2);

    assert(elapsed_minutes(100, 100) == 1);          /* minimum 1 minute */
    assert(elapsed_minutes(100, 160) == 1);          /* exactly 60 s */
    assert(elapsed_minutes(100, 161) == 2);          /* 61 s rounds up */
    assert(elapsed_minutes(0, 125 * 60) == 125);
    printf("PASS elapsed-minutes rounding\n");

    time_t now = time(NULL);
    assert(session_start_at(1, 101, now - 125 * 60) == 0);
    assert(session_start_at(2, 102, now - 10 * 60) == 0);
    assert(session_start(3, 103) == 0);
    assert(session_start(1, 104) == -1);             /* PC already busy */
    assert(session_start(4, 101) == -2);             /* user already on a PC */
    assert(session_count() == 3);
    printf("PASS no double-booking of a PC or a user\n");

    printf("\nActive sessions (pc|user|start|minutes|bill):\n");
    session_print_all(stdout);

    int m, b;
    assert(session_stop(1, &m, &b) == 0 && m == 125 && b == 250);
    assert(session_stop(1, &m, &b) == -1);
    assert(session_count() == 2);
    printf("\nPASS stop: PC 1 used %d min -> Rs %d\n", m, b);

    sessions_init("m5.dat");                          /* reload from the file = engine restart */
    assert(session_count() == 2);
    printf("PASS sessions survive a restart\n");

    assert(session_write_status("m5.json", 1234, 60, 90, 10, 5) == 0);
    FILE *f = fopen("m5.json", "r"); char js[2000]; size_t n = fread(js, 1, sizeof js - 1, f); js[n] = 0; fclose(f);
    assert(strstr(js, "\"pid\": 1234") && strstr(js, "\"pc\": 2") && strstr(js, "\"pc\": 3"));
    printf("PASS status.json written for the dashboard\n");

    remove("m5.dat"); remove("m5.json");
    printf("MODULE 5: ALL TESTS PASSED\n");
    return 0;
}
