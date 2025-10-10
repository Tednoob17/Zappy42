#ifndef METRICS_SMOOTHER_H
#define METRICS_SMOOTHER_H

// Exponential Moving Average smoother for metrics
typedef struct {
    float cpu_smoothed;
    float mem_smoothed;
    float io_smoothed;
    int initialized;
} metrics_smoother_t;

// Initialize smoother
void metrics_smoother_init(metrics_smoother_t* smoother);

// Apply EMA smoothing and normalization
void metrics_smoother_update(
    metrics_smoother_t* smoother,
    float cpu_raw,      // Raw CPU % (0-100)
    float mem_raw_mb,   // Raw memory in MB
    float io_raw,       // Raw I/O (0-100)
    float* cpu_out,     // Normalized & smoothed CPU (0-100)
    float* mem_out,     // Normalized & smoothed mem (0-100)
    float* io_out       // Normalized & smoothed I/O (0-100)
);

// Calculate load score
float calculate_load_score(float cpu_norm, float mem_norm, float io_norm);

#endif
