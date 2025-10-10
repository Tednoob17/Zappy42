#ifndef SCHEDULER_CONFIG_H
#define SCHEDULER_CONFIG_H

// Scheduler weights for load balancing score calculation
// Score = ALPHA * cpu + BETA * mem + GAMMA * io
// Lower score = less loaded = preferred worker

#define ALPHA 0.5f   // CPU weight
#define BETA  0.3f   // Memory weight  
#define GAMMA 0.2f   // I/O weight

// Exponential Moving Average (EMA) smoothing factor
// smooth_value = EMA_FACTOR * old_value + (1 - EMA_FACTOR) * new_value
#define EMA_FACTOR 0.7f

// Normalization constants
#define MAX_CPU_PERCENT 100.0f
#define MAX_MEM_MB 512.0f      // Assume max 512MB per worker
#define MAX_IO_RATE 10000.0f   // Max 10 MB/s (10000 KB/s) per worker

#endif
