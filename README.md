# CyberDB
A database engine written from scratch in C, powering a cybercafe management system written in Java.

## Team Multitaskers
| Module | Owner | Folder |
|---|---|---|
| 1 Storage + Buffer Pool | Gourvansh | `storage/`, `buffer/` |
| 2 B+ Tree Indexing | Atharv | `index/` |
| 3 SQL Parser + Executor | Abhinav | `parser/` |
| 4 Transactions + WAL | Aman | `transactions/` |
| 5 Sessions + Monitoring | Shaurya | `sessions/` |
| 6 Application Layer (Java) | Shifa | `app/` |

## Build and test (Linux / macOS)
    make            # builds ./cyberdb
    make test       # unit tests for all 5 C modules + crash-recovery integration tests

## Run the engine by hand
    ./cyberdb cyberdb.db < seed.sql     # create tables and sample data
    ./cyberdb cyberdb.db                # then type SQL, e.g.  SELECT * FROM users;

## Run the Java app on the real engine
    cd app && javac -d out $(find src -name "*.java")
    java -cp out app.Main engine ../cyberdb ../cyberdb.db      # (omit arguments for mock data)
Test users: Shifa (ADMIN), Gourvansh (STAFF), Priyanshu (CUSTOMER), Shifa (ADMIN); password `1234`.

Docs: `docs/CONTRACTS.md` (interfaces), `docs/MODULE_GUIDE.md` (what each module does, demos, viva questions).
