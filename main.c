#define _GNU_SOURCE
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include "libcotp/src/cotp.h"
#include <threads.h>
#include <time.h>
#include <sqlite3.h>
#include <string.h>

// current impl assumes 30 sec counter and SHA1

typedef struct {
    char *name;
    char *key;
} Pair;

#define MAX_KEYS 100
Pair pairs[100] = {};
int pair_count = 0;
sqlite3 *db;
volatile sig_atomic_t running = 1;

void read_all() {
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "SELECT name, key FROM keys", -1, &stmt, 0);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *name = sqlite3_column_text(stmt, 0);
        const unsigned char *key = sqlite3_column_text(stmt, 1);
        char *name_cpy = strdup((const char*)name);
        char *key_cpy = strdup((const char*)key);
        pairs[pair_count++] = (Pair){ name_cpy, key_cpy };
    }

    sqlite3_finalize(stmt);
}

void handle_sigint(int) {
    running = 0;
}

void insert(char *name, char *key) {
    printf("Inserting %s, %s\n", name, key);
    sqlite3_stmt *stmt;

    const char *sql = "INSERT INTO keys (name, key) VALUES (?, ?)";

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK) {
        printf("Prepare failed\n");
        return;
    }

    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, key, -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        printf("Insert failed (Duplicate name?)\n");
    }

    sqlite3_finalize(stmt);
}

void delete(const char *name) {
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, "DELETE FROM keys WHERE name = ?", -1, &stmt, 0);
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void start_tui() {
    cotp_error_t err;
    while (running) {
        time_t now = time(NULL);
        for (int i = 0; i < pair_count; i++) {
            Pair pair = pairs[i];
            char *code = get_totp_at(pair.key, now, 6, 30, COTP_SHA1, &err);
            char *next = get_totp_at(pair.key, now + 30, 6, 30, COTP_SHA1, &err);
            // "\e[2K" erases entire line
            printf("\e[2K%s: %s (next: %s) (%lds)\n", pair.name, code, next, 30 - (now % 30));
        }
        sleep(1);
        if (running) {
            // "\e[%dA" goes up x lines
            printf("\e[%dA", pair_count);
        }
    }
}

void run(int argc, char *argv[]) {
    const char *sql = (
        "CREATE TABLE IF NOT EXISTS keys ("
            "name TEXT PRIMARY KEY,"
            "key TEXT NOT NULL"
        ");"
    );
    int rc = sqlite3_exec(db, sql, 0, 0, NULL);
    if (rc != SQLITE_OK) {
        printf("SQL Error\n");
        return;
    }

    if (argc > 1) {
        if (!strcmp(argv[1], "add")) {
            if (argc < 4) {
                printf("usage: totptui add <name> <key|uri>\n");
                return;
            }
            char *name = argv[2];
            char *key = argv[3];
            cotp_error_t err;
            cotp_otpauth_uri* obj = cotp_otpauth_uri_parse(key, &err);
            if (err == INVALID_USER_INPUT) {
                insert(name, key);
            } else {
                insert(name, obj->secret);
            }
            return;
        }
        if (!strcmp(argv[1], "del")) {
            if (argc < 3) {
                printf("usage: totptui del <name>");
                return;
            }
            char *name = argv[2];
            delete(name);
            return;
        }
        printf(
            "totptui by Samuraien2\n"
            "usage:\n"
            "  totptui add <label> <key>    add key\n"
            "  totptui add <label> <uri>    add key\n"
            "  totptui del <label>          delete key\n"
            "  totptui                      show live tui\n"
        );
        return;
    }
    read_all();
    start_tui();
    return;
}

int main(int argc, char *argv[]) {
    // will run handle_sigint() on signals like Ctrl+C
    struct sigaction sa = {0};
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    
    int rc = sqlite3_open("key.db", &db);
    if (rc) {
        printf("Can't open DB: %s\n", sqlite3_errmsg(db));
        return 1;
    }
    run(argc, argv);
    sqlite3_close(db);
    return 0;
}
