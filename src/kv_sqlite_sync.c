// kv_sqlite_sync.c
// Optimized polling-based synchronization between SQLite and in-memory KV
// -------------------------------------------------------------------------
// - Loads all data at startup
// - Polls SQLite periodically for changes based on 'updated' timestamp
// - Thread-safe with read/write locks

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sqlite3.h>
#include <time.h>
#include "kv.h"

// -------------------------------------------------------------------
// Sync context structure
// -------------------------------------------------------------------
typedef struct {
    kv_t* kv;
    const char* db_path;
    const char* table;
    int interval;               // Poll interval in seconds
    time_t last_sync;           // Last successful sync timestamp
    pthread_rwlock_t* lock;
    volatile int* running;      // Pointer to running flag
} sync_context_t;

// -------------------------------------------------------------------
// Load all entries modified since last_sync timestamp
// -------------------------------------------------------------------
static time_t kv_refresh_from_sqlite_since(
    kv_t* kv, 
    const char* db_path, 
    const char* table,
    time_t last_sync, 
    pthread_rwlock_t* lock
) {
    sqlite3* db = NULL;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        fprintf(stderr, "[SYNC] DB open error: %s\n", sqlite3_errmsg(db));
        return last_sync;
    }

    char sql[256];
    snprintf(sql, sizeof(sql), "SELECT k, v, updated FROM %s WHERE updated > ?;", table);

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "[SYNC] Prepare error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return last_sync;
    }

    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)last_sync);

    time_t max_ts = last_sync;
    int n_updates = 0;

    // Write lock: block readers during update
    pthread_rwlock_wrlock(lock);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* k = (const char*)sqlite3_column_text(stmt, 0);
        const char* v = (const char*)sqlite3_column_text(stmt, 1);
        time_t ts = sqlite3_column_int64(stmt, 2);
        
        if (k && v) {
            kv_set(kv, k, v);
            n_updates++;
            printf("[SYNC] Updated: %s = %.60s%s\n", k, v, strlen(v) > 60 ? "..." : "");
        }
        
        if (ts > max_ts) {
            max_ts = ts;
        }
    }
    
    pthread_rwlock_unlock(lock);

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    if (n_updates > 0) {
        printf("[SYNC] %d entries updated (timestamp=%ld)\n", n_updates, max_ts);
    }

    return max_ts;
}

// -------------------------------------------------------------------
// Load all initial data from database
// -------------------------------------------------------------------
static int load_initial_data(
    kv_t* kv,
    const char* db_path,
    const char* table,
    pthread_rwlock_t* lock,
    time_t* out_max_timestamp
) {
    sqlite3* db = NULL;
    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        fprintf(stderr, "[SYNC] DB open error: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    char sql[256];
    snprintf(sql, sizeof(sql), "SELECT k, v, updated FROM %s;", table);

    sqlite3_stmt* stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        fprintf(stderr, "[SYNC] Prepare error: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return -1;
    }

    int count = 0;
    time_t max_ts = 0;

    pthread_rwlock_wrlock(lock);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* k = (const char*)sqlite3_column_text(stmt, 0);
        const char* v = (const char*)sqlite3_column_text(stmt, 1);
        time_t ts = sqlite3_column_int64(stmt, 2);
        
        if (k && v) {
            kv_set(kv, k, v);
            count++;
            
            if (ts > max_ts) {
                max_ts = ts;
            }
        }
    }
    
    pthread_rwlock_unlock(lock);

    sqlite3_finalize(stmt);
    sqlite3_close(db);

    printf("[SYNC] %d initial entries loaded (latest timestamp=%ld)\n", count, max_ts);
    
    if (out_max_timestamp) {
        *out_max_timestamp = max_ts;
    }
    
    return 0;
}

// -------------------------------------------------------------------
// Sync worker thread - polls database periodically
// -------------------------------------------------------------------
static void* sync_worker(void* arg) {
    sync_context_t* ctx = (sync_context_t*)arg;
    
    printf("[SYNC] Worker thread started (interval=%ds)\n", ctx->interval);

    while (*ctx->running) {
        sleep(ctx->interval);

        if (!*ctx->running) break;

        printf("[SYNC] Checking for updates since timestamp %ld...\n", ctx->last_sync);

        time_t new_ts = kv_refresh_from_sqlite_since(
            ctx->kv, 
            ctx->db_path, 
            ctx->table, 
            ctx->last_sync, 
            ctx->lock
        );

        if (new_ts > ctx->last_sync) {
            ctx->last_sync = new_ts;
        } else {
            printf("[SYNC] No changes detected\n");
        }
    }

    printf("[SYNC] Worker thread stopping...\n");
    return NULL;
}

// -------------------------------------------------------------------
// Initialize synchronization and start worker thread
// Returns 0 on success, -1 on error
// -------------------------------------------------------------------
int kv_sync_init(
    kv_t* kv,
    const char* db_path,
    const char* table,
    int interval,
    pthread_rwlock_t* lock,
    volatile int* running,
    pthread_t* thread_out
) {
    if (!kv || !db_path || !table || !lock || !running || !thread_out) {
        fprintf(stderr, "[SYNC] Invalid parameters\n");
        return -1;
    }

    // Allocate context (persists for program lifetime)
    sync_context_t* ctx = (sync_context_t*)malloc(sizeof(sync_context_t));
    if (!ctx) {
        fprintf(stderr, "[SYNC] Memory allocation error\n");
        return -1;
    }

    ctx->kv = kv;
    ctx->db_path = db_path;
    ctx->table = table;
    ctx->interval = interval;
    ctx->last_sync = 0;
    ctx->lock = lock;
    ctx->running = running;

    // Load initial data
    if (load_initial_data(kv, db_path, table, lock, &ctx->last_sync) != 0) {
        free(ctx);
        return -1;
    }

    // Start worker thread
    if (pthread_create(thread_out, NULL, sync_worker, ctx) != 0) {
        fprintf(stderr, "[SYNC] Thread creation error\n");
        free(ctx);
        return -1;
    }

    printf("[SYNC] ✓ Polling-based sync initialized on %s.%s\n", db_path, table);
    return 0;
}
