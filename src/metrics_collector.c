// metrics_collector.c
// Load balancer side: collects metrics from workers

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include "metrics.h"

// Metrics storage for each worker
static worker_metrics_t worker_metrics[30];  // Support up to 30 workers
static pthread_mutex_t metrics_lock = PTHREAD_MUTEX_INITIALIZER;

// -------------------------------------------------------------------
// Update metrics for a worker
// -------------------------------------------------------------------
void update_worker_metrics(const worker_metrics_t* m) {
    if (!m || m->worker_id < 0 || m->worker_id >= 30) {
        return;
    }
    
    pthread_mutex_lock(&metrics_lock);
    memcpy(&worker_metrics[m->worker_id], m, sizeof(worker_metrics_t));
    pthread_mutex_unlock(&metrics_lock);
    
    // printf("[METRICS] Worker #%d: Score=%.2f (CPU=%.1f%%, Mem=%.1f%%, IO=%.1f%%), Reqs=%u, Status=%s\n",
    //        m->worker_id, m->score, m->cpu, m->mem, m->io, m->requests, m->status);
}

// -------------------------------------------------------------------
// Get metrics for a specific worker
// -------------------------------------------------------------------
int get_worker_metrics(int worker_id, worker_metrics_t* out) {
    if (worker_id < 0 || worker_id >= 30 || !out) {
        return -1;
    }
    
    pthread_mutex_lock(&metrics_lock);
    memcpy(out, &worker_metrics[worker_id], sizeof(worker_metrics_t));
    pthread_mutex_unlock(&metrics_lock);
    
    return 0;
}

// -------------------------------------------------------------------
// Metrics collector thread (runs in load balancer)
// -------------------------------------------------------------------
void* metrics_collector_thread(void* arg) {
    (void)arg;
    
    // Create UNIX datagram socket
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("[METRICS] socket");
        return NULL;
    }
    
    // Bind to metrics socket
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, METRICS_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    unlink(METRICS_SOCKET_PATH);  // Remove old socket if exists
    
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("[METRICS] bind");
        close(fd);
        return NULL;
    }
    
    printf("[METRICS] Collector listening on %s\n", METRICS_SOCKET_PATH);
    
    // Receive metrics from workers
    worker_metrics_t m;
    while (1) {
        ssize_t n = recv(fd, &m, sizeof(m), 0);
        if (n == sizeof(m)) {
            update_worker_metrics(&m);
        } else if (n < 0) {
            perror("[METRICS] recv");
            break;
        }
    }
    
    close(fd);
    unlink(METRICS_SOCKET_PATH);
    return NULL;
}

// -------------------------------------------------------------------
// Start metrics collector
// -------------------------------------------------------------------
int start_metrics_collector(pthread_t* thread_out) {
    if (pthread_create(thread_out, NULL, metrics_collector_thread, NULL) != 0) {
        fprintf(stderr, "[METRICS] Failed to start collector thread\n");
        return -1;
    }
    return 0;
}

// -------------------------------------------------------------------
// Display all worker metrics (for debugging)
// -------------------------------------------------------------------
void print_all_metrics(void) {
    pthread_mutex_lock(&metrics_lock);
    
    printf("\n[METRICS] ═══ Worker Status ═══\n");
    for (int i = 0; i < 10; i++) {
        if (worker_metrics[i].timestamp > 0) {
            printf("  Worker #%d: Score=%.2f (CPU=%.1f%%, Mem=%.1f%%, IO=%.1f%%), Reqs=%u, Status=%s\n",
                   i, worker_metrics[i].score, worker_metrics[i].cpu, worker_metrics[i].mem,
                   worker_metrics[i].io, worker_metrics[i].requests, worker_metrics[i].status);
        }
    }
    printf("\n");
    
    pthread_mutex_unlock(&metrics_lock);
}
