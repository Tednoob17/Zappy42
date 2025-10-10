// metrics_smoother.c
// EMA smoothing and normalization for metrics

#include "metrics_smoother.h"
#include "scheduler_config.h"

// -------------------------------------------------------------------
// Initialize smoother
// -------------------------------------------------------------------
void metrics_smoother_init(metrics_smoother_t* smoother) {
    if (!smoother) return;
    
    smoother->cpu_smoothed = 0.0f;
    smoother->mem_smoothed = 0.0f;
    smoother->io_smoothed = 0.0f;
    smoother->initialized = 0;
}

// -------------------------------------------------------------------
// Normalize value to 0-100 range
// -------------------------------------------------------------------
static float normalize(float value, float max_value) {
    if (max_value <= 0.0f) return 0.0f;
    float normalized = (value / max_value) * 100.0f;
    if (normalized < 0.0f) normalized = 0.0f;
    if (normalized > 100.0f) normalized = 100.0f;
    return normalized;
}

// -------------------------------------------------------------------
// Apply EMA smoothing
// -------------------------------------------------------------------
static float apply_ema(float old_value, float new_value, int initialized) {
    if (!initialized) {
        return new_value;  // First value, no smoothing
    }
    return EMA_FACTOR * old_value + (1.0f - EMA_FACTOR) * new_value;
}

// -------------------------------------------------------------------
// Update metrics with EMA smoothing and normalization
// -------------------------------------------------------------------
void metrics_smoother_update(
    metrics_smoother_t* smoother,
    float cpu_raw,
    float mem_raw_mb,
    float io_raw,
    float* cpu_out,
    float* mem_out,
    float* io_out
) {
    if (!smoother || !cpu_out || !mem_out || !io_out) return;
    
    // Normalize raw values
    float cpu_norm = normalize(cpu_raw, MAX_CPU_PERCENT);
    float mem_norm = normalize(mem_raw_mb, MAX_MEM_MB);
    float io_norm = normalize(io_raw, MAX_IO_RATE);
    
    // Apply EMA smoothing
    smoother->cpu_smoothed = apply_ema(smoother->cpu_smoothed, cpu_norm, smoother->initialized);
    smoother->mem_smoothed = apply_ema(smoother->mem_smoothed, mem_norm, smoother->initialized);
    smoother->io_smoothed = apply_ema(smoother->io_smoothed, io_norm, smoother->initialized);
    
    smoother->initialized = 1;
    
    // Output smoothed values
    *cpu_out = smoother->cpu_smoothed;
    *mem_out = smoother->mem_smoothed;
    *io_out = smoother->io_smoothed;
}

// -------------------------------------------------------------------
// Calculate load score: alpha*cpu + beta*mem + gamma*io
// Lower score = less loaded = better choice
// -------------------------------------------------------------------
float calculate_load_score(float cpu_norm, float mem_norm, float io_norm) {
    return ALPHA * cpu_norm + BETA * mem_norm + GAMMA * io_norm;
}
