# CyberDB — SQL Parser + Query Executor prototype

This is an educational C database engine for the cybercafe-management project. It uses a custom page file, a 64-frame buffer pool, a write-ahead log, and a small persistent session manager. It does **not** use MySQL.

## Build (Linux / WSL)

```sh
make
./cyberdb cyberdb.db
```

The source uses POSIX `fsync`, `unistd.h`, and pthreads. For Windows, the easiest route is to build and run it inside WSL. Native MinGW builds may need POSIX-compatible pthread and sync support.

## Commands

```sql
CREATE TABLE users (id INT, name VARCHAR(50), age INT);
INSERT INTO users VALUES (1, 'Anjali Chand', 20);
INSERT INTO users (name, id, age) VALUES ('Rohan', 2, 21);
SELECT * FROM users;
SELECT name, age FROM users WHERE age >= 20 ORDER BY age DESC;
SELECT COUNT(*) FROM users;
SELECT SUM(age), AVG(age) FROM users;
UPDATE users SET age = 22 WHERE id = 2;
DELETE FROM users WHERE id = 1;
BEGIN;
INSERT INTO users VALUES (3, 'Neha', 20);
ROLLBACK;
COMMIT;
```

The parser currently supports a deliberately small SQL subset. `CREATE TABLE` stores column names; type tokens are accepted for familiar syntax but values are stored as strings. `WHERE` supports one predicate, and `SELECT` supports column lists, `*`, `COUNT`, `SUM`, `AVG`, and one `ORDER BY` column. It does not support joins, NULL semantics, constraints, indexes, multiple predicates, or full SQL type checking.

Engine commands:

- `TOKENS SELECT * FROM users;`
- `SESSION START <pc_id> <user_id>`
- `SESSION STOP <pc_id>`
- `SESSION LIST`
- `STATUS`
- `WALDUMP`
- `EXIT` / `QUIT`

Optional environment variables: `CYBERDB_STATUS` (status JSON path), `CYBERDB_SESSIONS` (session persistence path), `CYBERDB_RATE` (billing units per minute; default 2), `CYBERDB_VERBOSE=1` (buffer hit/miss diagnostics on stderr), and `CYBERDB_CRASH=before_commit|after_commit` (WAL crash-test hook).

## Current design limitations

- The buffer manager has 64 frames and uses no-steal behavior for dirty transaction pages. A transaction that dirties too many distinct pages can exhaust the pool.
- Only one transaction may be active at a time. This is not a concurrent multi-transaction DBMS.
- The page structures are written as native C structs, so this prototype is intended for the same compiler/platform/ABI across runs; a production engine should use an explicit portable on-disk encoding.
- The session manager persists sessions separately in a text file; it is not yet stored as SQL rows in the custom database.
- This is a teaching prototype, not production-grade storage or recovery software. Back up your database files before experimenting with crash hooks.
