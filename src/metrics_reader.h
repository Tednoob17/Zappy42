#ifndef METRICS_READER_H
#define METRICS_READER_H

#include <stdint.h>

// Read actual system metrics for current process
float get_process_cpu_usage(void);
float get_process_memory_mb(void);
float get_process_io_rate(void);  // Returns I/O rate in KB/s

#endif
