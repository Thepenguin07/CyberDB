# Module guide: what each person must be able to explain

Every module has: the job, the 3 functions to show, a live demo command, and viva questions.
Build everything first: `make` (engine) and `make test` (all tests).

## Module 1 - Storage + Buffer Pool (Gourvansh) - storage/, buffer/
**Job:** the database file is an array of 4096-byte pages; page n lives at byte n*4096. The buffer
pool keeps 64 pages in RAM so repeated reads skip the disk.
**Show:** `storage_write_page` (fseek + fwrite), `storage_read_page`, `buffer_get_page` (hit/miss, LRU victim).
**Demo:** `make m1` ; or `CYBERDB_VERBOSE=1 ./cyberdb x.db` and run a SELECT to see HIT/MISS lines.
**Viva:** Why fixed-size pages? (offset = id x size, one seek.) What is a pin? (page in use, cannot be
evicted.) What is dirty? (changed in RAM, not yet on disk.) What is LRU? (evict the page unused longest.)
Why no-steal? (pages changed by an unfinished transaction are never evicted, so they never reach the file.)

## Module 2 - B+ Tree Index (Atharv) - index/
**Job:** map a key (user id) to where its row lives (page, slot) so lookups do not scan every row.
Nodes are pages; leaves are chained; nodes split when they exceed BT_CAP keys.
**Show:** `ins` (leaf split vs internal split), `btree_search` (descend with upper_bound), `btree_insert` (root split).
**Demo:** `make m2` prints a real tree. With 250 keys per node, 1 million rows need only 3 levels.
**Viva:** Why B+ and not binary tree? (wide nodes = few disk reads.) Leaf split vs internal split?
(leaf keeps the separator, internal pushes it up.) Delete? (lazy: we remove the entry but do not merge
nodes; merging is a possible extension.) Where does the root id live? (in the catalog, page 0.)

## Module 3 - SQL Parser + Executor (Abhinav) - parser/
**Job:** turn a text line into actions: tokenize, check the statement type, validate, then use the
catalog, heap pages and index. Also the `cyberdb` program (main.c).
**Show:** `tokenize`, `do_insert` (validate -> duplicate check -> heap -> index), `find_rows` (index vs scan).
**Demo:** `./cyberdb x.db` then type `TOKENS SELECT * FROM users WHERE id = 5;` and
`STATS` after a lookup (index_lookups vs full_scans).
**Viva:** What is a token? Why validate before changing anything? (so errors leave no trace.)
How does WHERE id=5 differ from WHERE name='x'? (index vs full scan.) How are rows stored? (fixed-size
slots in linked heap pages, row_size from the schema.)

## Module 4 - Transactions + WAL (Aman) - transactions/
**Job:** all-or-nothing changes and crash safety. At commit: (1) log before+after image of every
changed page, (2) write COMMIT and fsync the log, (3) only then write pages to the db file.
Startup recovery: redo committed transactions, undo unfinished ones.
**Show:** `txn_commit` (the 3 steps), `txn_recover` (redo + undo loops), the checksum in `append`.
**Demo (the best one in the project):** `make integration` runs real simulated power cuts using
`CYBERDB_CRASH=after_commit` and `CYBERDB_CRASH=before_commit`. Also `WALDUMP` shows the log.
**Viva:** Why log first? (a crash between log and data is recoverable; the reverse is not.) What does
fsync do? (forces data from OS cache onto disk.) Why a checksum? (a half-written last record is detected
and ignored.) Limits: one transaction at a time; a transaction can touch at most 1024 pages.

## Module 5 - Sessions + Monitoring (Shaurya) - sessions/
**Job:** who is on which PC, since when, what they owe. Minutes are rounded up (minimum 1), bill =
minutes x rate. No PC and no user can be double-booked. State is saved to `<db>.sessions`.
The monitor thread rewrites `status.json` every 2 s (engine pid, uptime, buffer hit rate, sessions).
**Show:** `elapsed_minutes`, `session_start_at` (the two rejection rules), `session_write_status`.
**Demo:** `CYBERDB_STATUS=sessions/status.json ./cyberdb x.db` in one terminal; in another:
`cd sessions && python3 -m http.server 8000` and open http://localhost:8000/dashboard.html.
Type `SESSION START 1 101` in the engine and watch the dashboard.
**Viva:** Why write the file then rename? (atomic: never a half-written file.) How does the JS dashboard
get live data? (polls status.json every 2 s and recomputes minutes in the browser.)

## Module 6 - Application Layer (Shifa) - app/
Login (SHA-256), role checks, booking grid, billing, `Database` interface. `ProcessDatabase` starts the
engine and speaks the line protocol; swapping it for `MockDatabase` is one line in Main.

## The whole pipeline (everyone must know it)
Java screen -> SQL string -> ProcessDatabase (stdin) -> parser/executor -> B+ tree index + heap pages ->
buffer pool -> (transaction log, then) storage file. Sessions + monitor run beside it.
