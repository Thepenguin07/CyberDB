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
**Job:** Ensures ACID atomicity and durability for multi-statement operations and guarantees that a sudden crash or power cut never corrupts the database.
When a transaction begins (`txn_begin`), the buffer pool starts capturing before-images for every page modified.
When `txn_commit` is called:
1. **Write-Ahead Logging**: For every modified page, both the before-image and after-image are written into the WAL file (`.wal`) as `T_WRITE` records with an FNV-1a checksum.
2. **Durability Barrier**: A `T_COMMIT` record is appended to the log, and `flush_log()` invokes `fsync()` on the WAL file. Once this succeeds, the transaction is irrevocably committed.
3. **Flushing to Disk**: Only after the WAL is flushed to physical disk does `buffer_txn_end(1)` allow the dirty pages to be written into the database file.
If `txn_rollback` is invoked, `buffer_txn_end(0)` restores the cached memory pages from their before-images, logs a `T_ABORT` record, and avoids writing anything to the database file.
During crash recovery (`txn_recover`):
- The engine scans the WAL sequentially from the beginning, validating record headers (`REC_MAGIC == 0x57414C31`) and payload checksums. Any corrupt or torn record tail (e.g. from an interrupted write) is safely stopped at and ignored.
- **REDO Pass**: All `T_WRITE` records belonging to committed transactions (`state == 1`) are replayed forward in chronological order, writing their after-images to storage.
- **UNDO Pass**: Any transactions that were in-flight without a recorded commit (`state == 0`) are rolled back in reverse order, restoring their before-images to storage.
- Once consistent, the database storage is synced and a fresh, empty WAL is initialized.
- Checkpointing (`txn_checkpoint`): Flushes all dirty memory pages directly into the database file via `buffer_flush_all()` and truncates the WAL file, preventing unbounded log growth.
**Show:**
- `txn_commit`: The strict 3-phase sequence (log changed pages -> append COMMIT + fsync -> persist pages to db file).
- `txn_recover`: The two-pass algorithm (forward REDO of committed transactions, reverse UNDO of uncommitted ones).
- `append` and `checksum`: FNV-1a checksum calculation protecting record headers and payload from partial/torn writes.
**Demo:**
- `make m4`: Runs the Module 4 unit tests verifying commit persistence, in-memory rollback, crash REDO replay, crash UNDO rollback, and WAL checkpointing.
- `make integration`: Runs end-to-end integration tests with simulated crashes (`CYBERDB_CRASH=after_commit` testing REDO, and `CYBERDB_CRASH=before_commit` testing UNDO).
- Run `WALDUMP` in `cyberdb` to inspect the exact logged sequence of `WRITE`, `COMMIT`, and `ABORT` entries.
**Viva:**
- *Why Write-Ahead Logging (WAL)?* Because append-only log writes are fast and sequential. By forcing the log entry to non-volatile disk before updating the database pages, we ensure sufficient recovery information exists if the system crashes midway.
- *Why record both before and after images?* The after-image enables REDO recovery (if power is lost after commit but before database pages reach disk). The before-image enables UNDO recovery (if uncommitted dirty pages reached disk during a crash).
- *What does `fsync()` do?* It flushes the operating system page cache to non-volatile disk hardware, ensuring the commit marker cannot be lost if power fails.
- *How are torn writes detected?* Every record contains an FNV-1a checksum over its header and data payload; if a crash occurs mid-write, `read_log()` detects the checksum mismatch or size mismatch and safely halts without parsing corrupt bytes.
- *What is the transaction capacity limit?* The transaction buffer records up to 1024 modified pages per transaction, and CyberDB runs transactions with single-threaded isolation.

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
