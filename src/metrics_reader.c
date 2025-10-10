// metrics_reader.c
// Read actual system metrics from /proc with high precision

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "metrics_reader.h"

// Static variables for CPU calculation
static unsigned long long last_total_time = 0;
static unsigned long long last_timestamp_ms = 0;

// Static variables for I/O calculation
static unsigned long long last_read_bytes = 0;
static unsigned long long last_write_bytes = 0;
static unsigned long long last_io_timestamp_ms = 0;

// -------------------------------------------------------------------
// Get current timestamp in milliseconds (high precision)
// -------------------------------------------------------------------
static unsigned long long get_timestamp_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
}

// -------------------------------------------------------------------
// Get CPU usage for current process (percentage)
// -------------------------------------------------------------------
float get_process_cpu_usage(void) {
    FILE* fp = fopen("/proc/self/stat", "r");
    if (!fp) {
        return 0.0f;
    }
    
    // Parse /proc/self/stat
    // Robust format: skip all fields until utime/stime
    char buffer[512];
    if (!fgets(buffer, sizeof(buffer), fp)) {
        fclose(fp);
        return 0.0f;
    }
    fclose(fp);
    
    // Robust parsing with sscanf
    int pid;
    unsigned long long utime = 0, stime = 0;
    
    // Format: pid (comm) state ppid pgrp session tty_nr tpgid flags minflt cminflt majflt cmajflt utime stime
    // Skip first 13 fields to get to utime (14th) and stime (15th)
    char *p = buffer;
    
    // Read pid
    sscanf(p, "%d", &pid);
    
    // Skip to after (comm) - find last ')' to handle names with spaces/parens
    p = strrchr(buffer, ')');
    if (!p) {
        return 0.0f;
    }
    p += 2;  // Skip ') '
    
    // Now read: state ppid pgrp session tty_nr tpgid flags minflt cminflt majflt cmajflt utime stime
    unsigned long skip1, skip2, skip3, skip4, skip5, skip6, skip7, skip8, skip9, skip10;
    char state;
    
    sscanf(p, "%c %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %llu %llu",
           &state, &skip1, &skip2, &skip3, &skip4, &skip5, &skip6, 
           &skip7, &skip8, &skip9, &skip10, &utime, &stime);
    
    unsigned long long total_time = utime + stime;
    unsigned long long current_timestamp = get_timestamp_ms();
    
    // Calculate CPU percentage with millisecond precision
    float cpu_percent = 0.0f;
    if (last_timestamp_ms > 0) {
        unsigned long long time_delta = total_time - last_total_time;
        unsigned long long real_delta_ms = current_timestamp - last_timestamp_ms;
        
        if (real_delta_ms > 0) {
            long ticks_per_sec = sysconf(_SC_CLK_TCK);
            // Convert: (clock_ticks * 1000ms) / (ticks_per_sec * ms_elapsed) = % CPU
            cpu_percent = (100.0f * time_delta * 1000.0f) / (ticks_per_sec * real_delta_ms);
        }
    }
    
    last_total_time = total_time;
    last_timestamp_ms = current_timestamp;
    
    return cpu_percent;
}

// -------------------------------------------------------------------
// Get memory usage for current process (MB)
// -------------------------------------------------------------------
float get_process_memory_mb(void) {
    FILE* fp = fopen("/proc/self/status", "r");
    if (!fp) {
        return 0.0f;
    }
    
    char line[256];
    unsigned long vm_rss_kb = 0;
    
    // Look for VmRSS line (Resident Set Size)
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "VmRSS:", 6) == 0) {
            sscanf(line + 6, "%lu", &vm_rss_kb);
            break;
        }
    }
    
    fclose(fp);
    
    // Convert KB to MB
    return vm_rss_kb / 1024.0f;
}

// -------------------------------------------------------------------
// Get I/O rate for current process (KB/s)
// -------------------------------------------------------------------
float get_process_io_rate(void) {
    FILE* fp = fopen("/proc/self/io", "r");
    if (!fp) {
        // /proc/self/io requires CAP_SYS_PTRACE or same user
        // Return 0 if not accessible
        return 0.0f;
    }
    
    char line[256];
    unsigned long long read_bytes = 0, write_bytes = 0;
    
    // Parse /proc/self/io
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "read_bytes:", 11) == 0) {
            sscanf(line + 11, "%llu", &read_bytes);
        } else if (strncmp(line, "write_bytes:", 12) == 0) {
            sscanf(line + 12, "%llu", &write_bytes);
        }
    }
    
    fclose(fp);
    
    unsigned long long current_timestamp = get_timestamp_ms();
    
    // Calculate I/O rate with millisecond precision
    float io_rate_kbs = 0.0f;
    if (last_io_timestamp_ms > 0) {
        unsigned long long time_delta_ms = current_timestamp - last_io_timestamp_ms;
        if (time_delta_ms > 0) {
            // Calculate bytes delta (read + write)
            unsigned long long bytes_delta = 
                (read_bytes - last_read_bytes) + (write_bytes - last_write_bytes);
            
            // Convert to KB/s with overflow protection
            // bytes_delta / 1024 = KB, then / (ms / 1000) = KB/s
            io_rate_kbs = ((double)bytes_delta / 1024.0) / ((double)time_delta_ms / 1000.0);
        }
    }
    
    last_read_bytes = read_bytes;
    last_write_bytes = write_bytes;
    last_io_timestamp_ms = current_timestamp;
    
    return io_rate_kbs;
}