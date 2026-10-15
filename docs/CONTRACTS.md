# CyberDB Contracts (final)

Rule: signatures change only after telling the lead (Shifa), who updates this file.
All C functions return 0 on success and -1 on failure unless stated.

## Schema
users(id INT, name TEXT, password_hash TEXT, role TEXT, balance INT)
pcs(id INT, label TEXT, status TEXT)
sessions(id INT, user_id INT, pc_id INT, start_time INT, end_time INT)
bills(id INT, session_id INT, amount INT)
audit_log(id INT, user_id INT, action TEXT, time INT)

* Types: INT (4 bytes) and TEXT (fixed 80 bytes, max 79 characters; a SHA-256 hash is 64).
* First column of every table is an INT primary key and is indexed by the B+ tree.
* TEXT may not contain `|` or newlines. No NULLs, no variable-length rows.

## SQL subset (Module 3)
    CREATE TABLE name (col TYPE, ...);
    INSERT INTO t VALUES (...);
    SELECT * FROM t [WHERE col = value];
    UPDATE t SET col = value [, col = value] [WHERE col = value];
    DELETE FROM t [WHERE col = value];
    BEGIN; COMMIT; ROLLBACK;      (every single statement is also atomic on its own)
Strings use single quotes. WHERE is one `col = value`; WHERE on the first column uses the index.

## Extra engine commands
    SESSION START pc user | SESSION STOP pc (replies minutes|bill) | SESSION LIST
    STATS | WALDUMP | STATUS | TOKENS <sql> | EXIT

## C interfaces
    storage/storage.h   storage_open, storage_read_page, storage_write_page, storage_alloc_page,
                        storage_page_count, storage_sync, storage_close       (PAGE_SIZE 4096)
    buffer/buffer.h     buffer_get_page (pins), buffer_mark_dirty, buffer_unpin, buffer_flush_all,
                        buffer_stats + transaction hooks used only by Module 4
    index/btree.h       int btree_create(void);
                        int btree_insert(int *root_page, int key, int row_page, int row_slot); /* 0, -1, -2 dup */
                        int btree_search(int root_page, int key, int *row_page, int *row_slot);
                        int btree_delete(int root_page, int key);
        CHANGE vs first draft: the tree is identified by its ROOT PAGE (not a table_id) because the
        root moves when it splits; the catalog stores each table's root.
    transactions/txn.h  txn_init(path), txn_begin, txn_log_write, txn_commit, txn_rollback,
                        txn_recover, txn_checkpoint
    parser/executor.h   engine_open, engine_close, execute_sql(sql, out_buf, out_size)
    sessions/sessions.h sessions_init, session_start, session_stop, session_print_all,
                        session_write_status

## Java <-> C protocol
Java starts `cyberdb <dbfile>` and sends ONE line per command on stdin. Reply on stdout:

    OK                                 ERR duplicate primary key 3
    1|Rahul|<hash>|CUSTOMER|500        END
    END

Columns are separated by `|`. The engine flushes stdout after END. Diagnostics (crash-recovery
messages) go to stderr, so they never mix with replies.
