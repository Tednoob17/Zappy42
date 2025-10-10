#ifndef METRICS_H
#define METRICS_H

#include <stdint.h>
#include <sys/types.h>

// Metrics structure sent by workers to load balancer
typedef struct {
    pid_t pid;           // Worker PID
    int worker_id;       // Worker ID (0, 1, etc.)
    float cpu;           // CPU usage (%) - normalized 0-100
    float mem;           // Memory usage (MB) - normalized 0-100
    float io;            // I/O usage (%) - normalized 0-100
    float score;         // Load score (lower = better) = alpha*cpu + beta*mem + gamma*io
    uint32_t requests;   // Total requests handled
    uint32_t errors;     // Total errors
    uint64_t timestamp;  // Timestamp (milliseconds)
    char status[32];     // Status: "idle", "busy", "overloaded"
} worker_metrics_t;

// Metrics socket path
#define METRICS_SOCKET_PATH "/tmp/faas_lb_metrics.sock"

// Get current timestamp in milliseconds
static inline uint64_t get_timestamp_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

#endif
