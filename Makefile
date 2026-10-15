CC     = gcc
CFLAGS = -Wall -Wextra -Wno-format-truncation -O1 -g
ENGINE = storage/storage.c buffer/buffer.c index/btree.c transactions/txn.c parser/executor.c sessions/sessions.c

all: cyberdb

cyberdb: $(ENGINE) parser/main.c
	$(CC) $(CFLAGS) $(ENGINE) parser/main.c -pthread -o cyberdb

# unit tests, one per module
m1: storage/storage.c buffer/buffer.c tests/test_m1.c
	$(CC) $(CFLAGS) $^ -o m1test && ./m1test
m2: storage/storage.c buffer/buffer.c index/btree.c tests/test_m2.c
	$(CC) $(CFLAGS) -DBT_CAP=4 $^ -o m2test && ./m2test
m3: $(ENGINE) tests/test_m3.c
	$(CC) $(CFLAGS) $^ -pthread -o m3test && ./m3test
m4: storage/storage.c buffer/buffer.c transactions/txn.c tests/test_m4.c
	$(CC) $(CFLAGS) $^ -o m4test && ./m4test
m5: sessions/sessions.c tests/test_m5.c
	$(CC) $(CFLAGS) $^ -o m5test && ./m5test
integration: cyberdb
	bash tests/integration.sh

test: m1 m2 m3 m4 m5 integration

clean:
	rm -f cyberdb m1test m2test m3test m4test m5test *.db *.wal *.sessions status.json
