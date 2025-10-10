#ifndef CONFIG_LOADER_H
#define CONFIG_LOADER_H

#include <pthread.h>

// Forward declaration
typedef struct kv kv_t;

struct FunctionDescriptor {
    char method[16];        // HTTP method (GET, POST, etc.)
    char uri[128];          // URI path
    char name[64];          // Function name
    char runtime[32];       // Runtime (wasm, python, etc.)
    char module[256];       // Module path
    char handler[128];      // Handler function name
    int memory;             // Memory limit (MB)
    int timeout;            // Timeout (seconds)
};

// Initialize config loader with KV store reference
void config_loader_init(kv_t* kv, pthread_rwlock_t* lock);

// Find function by HTTP method and URI
// Returns 0 on success, -1 if not found
// Result is written to 'out' parameter
int find_function(const char *method, const char *uri, struct FunctionDescriptor *out);

#endif
