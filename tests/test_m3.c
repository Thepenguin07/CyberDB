/* Module 3 tests: SQL parser + executor running on the whole engine */
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include "../parser/executor.h"

static char out[1 << 16];
static const char *run(const char *sql) { execute_sql(sql, out, sizeof out); return out; }
#define EXPECT(sql, expected) do { const char *r = run(sql); \
    if (strcmp(r, expected) != 0) { printf("FAIL: %s\n  got:      [%s]\n  expected: [%s]\n", sql, r, expected); exit(1); } } while (0)
#define EXPECT_PREFIX(sql, prefix) do { const char *r = run(sql); \
    if (strncmp(r, prefix, strlen(prefix)) != 0) { printf("FAIL: %s\n  got: [%s]\n", sql, r); exit(1); } } while (0)

int main(void) {
    remove("m3.db"); remove("m3.db.wal");
    assert(engine_open("m3.db") == 0);

    EXPECT("CREATE TABLE users (id INT, name TEXT, role TEXT, balance INT);", "OK\nEND\n");
    EXPECT("INSERT INTO users VALUES (1,'Rahul','CUSTOMER',500);", "OK\nEND\n");
    EXPECT("INSERT INTO users VALUES (2,'Priya','ADMIN',0);", "OK\nEND\n");
    EXPECT("insert into users values (3, 'Amit', 'STAFF', 0)", "OK\nEND\n");      /* case + no ; */
    EXPECT("SELECT * FROM users;", "OK\n1|Rahul|CUSTOMER|500\n2|Priya|ADMIN|0\n3|Amit|STAFF|0\nEND\n");
    EXPECT("SELECT * FROM users WHERE id = 2;", "OK\n2|Priya|ADMIN|0\nEND\n");
    EXPECT("SELECT * FROM users WHERE name = 'Amit';", "OK\n3|Amit|STAFF|0\nEND\n");
    EXPECT("SELECT * FROM users WHERE id = 99;", "OK\nEND\n");
    printf("PASS create / insert / select / where (index + scan)\n");

    EXPECT("UPDATE users SET balance = 450, role = 'VIP' WHERE id = 1;", "OK\nEND\n");
    EXPECT("SELECT * FROM users WHERE id = 1;", "OK\n1|Rahul|VIP|450\nEND\n");
    EXPECT("DELETE FROM users WHERE id = 2;", "OK\nEND\n");
    EXPECT("SELECT * FROM users;", "OK\n1|Rahul|VIP|450\n3|Amit|STAFF|0\nEND\n");
    printf("PASS update (multi-column) / delete\n");

    EXPECT("INSERT INTO users VALUES (1,'Dup','X',0);", "ERR duplicate primary key 1\nEND\n");
    EXPECT_PREFIX("INSERT INTO users VALUES ('a','Dup','X',0);", "ERR column id expects a number");
    EXPECT_PREFIX("INSERT INTO users VALUES (9,'Dup');", "ERR expected 4 values");
    EXPECT_PREFIX("SELECT * FROM nope;", "ERR no such table");
    EXPECT_PREFIX("SELECT * FROM users WHERE zzz = 1;", "ERR unknown column");
    EXPECT_PREFIX("UPDATE users SET id = 5 WHERE id = 1;", "ERR cannot update the primary key");
    EXPECT_PREFIX("DROP TABLE users;", "ERR unsupported");
    EXPECT_PREFIX("INSERT INTO users VALUES (9,'it''s','X',0);", "ERR");
    EXPECT_PREFIX("CREATE TABLE bad (name TEXT);", "ERR first column must be INT");
    EXPECT("SELECT * FROM users WHERE id = 1;", "OK\n1|Rahul|VIP|450\nEND\n");    /* errors changed nothing */
    printf("PASS errors reported cleanly and change nothing\n");

    /* transactions */
    EXPECT("BEGIN;", "OK\nEND\n");
    EXPECT("INSERT INTO users VALUES (10,'Temp','X',1);", "OK\nEND\n");
    EXPECT("UPDATE users SET balance = 0 WHERE id = 1;", "OK\nEND\n");
    EXPECT("SELECT * FROM users WHERE id = 10;", "OK\n10|Temp|X|1\nEND\n");
    EXPECT("ROLLBACK;", "OK\nEND\n");
    EXPECT("SELECT * FROM users WHERE id = 10;", "OK\nEND\n");
    EXPECT("SELECT * FROM users WHERE id = 1;", "OK\n1|Rahul|VIP|450\nEND\n");
    EXPECT("BEGIN;", "OK\nEND\n");
    EXPECT("INSERT INTO users VALUES (11,'Kept','X',1);", "OK\nEND\n");
    EXPECT("COMMIT;", "OK\nEND\n");
    EXPECT("SELECT * FROM users WHERE id = 11;", "OK\n11|Kept|X|1\nEND\n");
    EXPECT_PREFIX("COMMIT;", "ERR no active transaction");
    printf("PASS BEGIN / COMMIT / ROLLBACK\n");

    /* many rows: spans several heap pages and splits the index */
    EXPECT("CREATE TABLE big (id INT, label TEXT);", "OK\nEND\n");
    char sql[200];
    for (int i = 1; i <= 2000; i++) {
        snprintf(sql, sizeof sql, "INSERT INTO big VALUES (%d,'row%d');", i, i);
        EXPECT(sql, "OK\nEND\n");
    }
    EXPECT("SELECT * FROM big WHERE id = 1777;", "OK\n1777|row1777\nEND\n");
    EXPECT("SELECT * FROM big WHERE label = 'row2000';", "OK\n2000|row2000\nEND\n");
    run("SELECT * FROM big;");
    int lines = 0; for (char *c = out; *c; c++) if (*c == '\n') lines++;
    assert(lines == 2002);                                  /* OK + 2000 rows + END */
    printf("PASS 2000 rows across many pages\n");

    /* persistence across a clean restart */
    engine_close();
    assert(engine_open("m3.db") == 0);
    EXPECT("SELECT * FROM big WHERE id = 1500;", "OK\n1500|row1500\nEND\n");
    EXPECT("SELECT * FROM users WHERE id = 11;", "OK\n11|Kept|X|1\nEND\n");
    printf("PASS data survives restart\n");

    engine_close(); remove("m3.db"); remove("m3.db.wal");
    printf("MODULE 3: ALL TESTS PASSED\n");
    return 0;
}
