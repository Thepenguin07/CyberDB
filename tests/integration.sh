#!/bin/sh
# End-to-end tests of the real `cyberdb` program, including simulated power cuts.
set -u
ENGINE="$(pwd)/cyberdb"
DIR=$(mktemp -d); cd "$DIR"
fail() { echo "FAIL: $1"; exit 1; }
run() { printf '%s\n' "$@" | "$ENGINE" test.db 2>recovery.log; }

echo "--- 1. normal use through the stdin/stdout protocol"
OUT=$(run "CREATE TABLE pcs (id INT, label TEXT, status TEXT);" \
          "INSERT INTO pcs VALUES (1,'PC-1','FREE');" "INSERT INTO pcs VALUES (2,'PC-2','FREE');" \
          "UPDATE pcs SET status = 'BUSY' WHERE id = 2;" "SELECT * FROM pcs;" "EXIT")
echo "$OUT" | grep -q "2|PC-2|BUSY" || fail "update not visible"
echo "PASS"

echo "--- 2. clean restart keeps the data"
OUT=$(run "SELECT * FROM pcs WHERE id = 2;" "EXIT")
echo "$OUT" | grep -q "2|PC-2|BUSY" || fail "data lost after restart"
echo "PASS"

echo "--- 3. POWER CUT right AFTER commit is logged but BEFORE the db file is written"
CYBERDB_CRASH=after_commit run "INSERT INTO pcs VALUES (3,'PC-3','FREE');" >/dev/null
OUT=$(run "SELECT * FROM pcs WHERE id = 3;" "EXIT")
grep "recovery" recovery.log
echo "$OUT" | grep -q "3|PC-3|FREE" || fail "committed row lost (durability broken)"
echo "PASS: committed transaction survived the crash (redo)"

echo "--- 4. POWER CUT in the middle of a transaction (before COMMIT is logged)"
printf '%s\n' "BEGIN;" "INSERT INTO pcs VALUES (4,'PC-4','FREE');" "COMMIT;" "EXIT" | CYBERDB_CRASH=before_commit "$ENGINE" test.db 2>/dev/null >/dev/null
OUT=$(run "SELECT * FROM pcs;" "EXIT")
grep "recovery" recovery.log
echo "$OUT" | grep -q "4|PC-4" && fail "uncommitted row appeared (atomicity broken)"
echo "$OUT" | grep -q "3|PC-3|FREE" || fail "earlier committed data damaged"
echo "PASS: unfinished transaction left no trace (undo)"

echo "--- 5. a torn/corrupt log tail is ignored"
CYBERDB_CRASH=after_commit run "INSERT INTO pcs VALUES (5,'PC-5','FREE');" >/dev/null
printf 'garbage-bytes' >> test.db.wal
OUT=$(run "SELECT * FROM pcs WHERE id = 5;" "EXIT")
echo "$OUT" | grep -q "5|PC-5|FREE" || fail "valid log prefix not recovered"
echo "PASS"

echo "--- 6. sessions through the protocol + status.json"
OUT=$(CYBERDB_STATUS="$DIR/status.json" run "SESSION START 1 101" "SESSION START 1 102" "SESSION LIST" "SESSION STOP 1" "EXIT")
echo "$OUT" | grep -q "ERR PC is already in use" || fail "double booking allowed"
echo "$OUT" | grep -q "^1|2$" || fail "bill wrong (expected 1 minute|Rs 2)"
[ -f status.json ] || fail "status.json missing"
echo "PASS"

echo "INTEGRATION: ALL TESTS PASSED"
