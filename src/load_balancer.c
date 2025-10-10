// load_balancer.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

#include "config_loader.h"
#include "kv.h"
#include "metrics.h"

// External functions from metrics_collector.c
extern int start_metrics_collector(pthread_t* thread_out);
extern void print_all_metrics(void);
extern int get_worker_metrics(int worker_id, worker_metrics_t* out);

#define NUM_WORKERS 2
const char *WORKER_SOCKS[NUM_WORKERS] = {
    "/tmp/faas_worker_0.sock",
    "/tmp/faas_worker_1.sock"
};

static volatile int running = 1;

// Declaration of sync initialization function (from kv_sqlite_sync.c)
extern int kv_sync_init(
    kv_t* kv,
    const char* db_path,
    const char* table,
    int interval,
    pthread_rwlock_t* lock,
    volatile int* running,
    pthread_t* thread_out
);

void signal_handler(int sig) {
    (void)sig;
    printf("\n[LB] Shutdown requested...\n");
    running = 0;
}

// Select worker with lowest load score (smart scheduling)
int select_worker() {
    static int fallback_counter = 0;  // For round-robin fallback
    int best_worker = -1;
    float best_score = 1000000.0f;  
    
    // Try to find worker with best score
    for (int i = 0; i < NUM_WORKERS; i++) {
        worker_metrics_t m;
        if (get_worker_metrics(i, &m) == 0 && m.timestamp > 0) {
            if (m.score < best_score) {
                best_score = m.score;
                best_worker = i;
            }
        }
    }
    
    // Fallback: if no metrics available, use round-robin
    if (best_worker == -1) {
        best_worker = fallback_counter % NUM_WORKERS;
        fallback_counter++;
        printf("[LB] Selected Worker #%d (no metrics, using round-robin)\n", best_worker);
    } else {
        printf("[LB] Selected Worker #%d (score: %.2f)\n", best_worker, best_score);
    }
    
    return best_worker;
}

void send_to_worker(int idx, const char *module, const char *handler) {
    int fd;
    struct sockaddr_un addr;
    char buffer[256] = {0};
    char message[256];
    snprintf(message, sizeof(message), "%s %s", module, handler);

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, WORKER_SOCKS[idx], sizeof(addr.sun_path)-1);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("[LB] connect");
        close(fd);
        return;
    }

    write(fd, message, strlen(message));
    read(fd, buffer, sizeof(buffer)-1);
    printf("[LB] Worker %d → %s\n", idx, buffer);
    close(fd);
}

int main() {
    signal(SIGINT, signal_handler);
    
    printf("[LB] Starting load balancer...\n");

    // Create KV store and initialize synchronization
    kv_t* kv = kv_new(1024);
    if (!kv) {
        fprintf(stderr, "[LB] KV creation error\n");
        return 1;
    }

    pthread_rwlock_t kv_lock;
    pthread_rwlock_init(&kv_lock, NULL);

    pthread_t sync_thread;
    printf("[LB] Starting KV synchronization...\n");
    if (kv_sync_init(kv, "faas_meta.db", "functions", 5, &kv_lock, &running, &sync_thread) != 0) {
        fprintf(stderr, "[LB] Sync initialization error\n");
        kv_free(kv);
        pthread_rwlock_destroy(&kv_lock);
        return 1;
    }

    // Initialize config loader with KV
    config_loader_init(kv, &kv_lock);

    // Start metrics collector
    pthread_t metrics_thread;
    printf("[LB] Starting metrics collector...\n");
    if (start_metrics_collector(&metrics_thread) != 0) {
        fprintf(stderr, "[LB] Warning: Metrics collector failed to start\n");
    }

    // Wait a bit for initial sync to complete
    sleep(2);

    printf("[LB] ✓ Load balancer ready!\n\n");

    // Simulation of HTTP requests
    const char *requests[][2] = {
        {"POST", "/resize"},
        {"GET",  "/ping"},
        {"POST", "/resize"},
        {"POST", "/resize"}
    };

    for (int i = 0; i < 4 && running; i++) {
        const char *method = requests[i][0];
        const char *uri = requests[i][1];
        printf("\n[LB] ═══ Request #%d: %s %s ═══\n", i+1, method, uri);

        struct FunctionDescriptor fn;
        if (find_function(method, uri, &fn) != 0) {
            printf("[LB] No function found for %s %s\n", method, uri);
            sleep(1);
            continue;
        }

        printf("[LB] Found: %s (runtime: %s, mem: %dMB, timeout: %ds)\n", 
               fn.name, fn.runtime, fn.memory, fn.timeout);
        printf("[LB] Routing to: %s -> %s()\n", fn.module, fn.handler);
        
        int w = select_worker();
        send_to_worker(w, fn.module, fn.handler);
        sleep(1);
    }

    // Display final metrics
    sleep(1);
    print_all_metrics();

    // Cleanup
    printf("\n[LB] Shutting down...\n");
    running = 0;
    pthread_join(sync_thread, NULL);
    kv_free(kv);
    pthread_rwlock_destroy(&kv_lock);
    printf("[LB] Goodbye!\n");

    return 0;
}
