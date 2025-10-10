#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "config_loader.h"
#include "kv.h"

// Global KV reference (set during initialization)
static kv_t* g_kv = NULL;
static pthread_rwlock_t* g_lock = NULL;

// -------------------------------------------------------------------
// Initialize config loader with KV store
// -------------------------------------------------------------------
void config_loader_init(kv_t* kv, pthread_rwlock_t* lock) {
    g_kv = kv;
    g_lock = lock;
    printf("[CONFIG] Config loader initialized with KV store\n");
}

// -------------------------------------------------------------------
// Parse JSON value to extract all fields
// Simple parser for: {"name":"...","module":"...","handler":"..."}
// -------------------------------------------------------------------
static int parse_function_json(const char* json, struct FunctionDescriptor* out) {
    if (!json || !out) return -1;
    
    // Initialize output
    memset(out, 0, sizeof(*out));
    
    // Find "module":"..."
    const char* module_start = strstr(json, "\"module\":\"");
    if (module_start) {
        module_start += 10; // skip "module":"
        const char* module_end = strchr(module_start, '"');
        if (module_end) {
            size_t len = module_end - module_start;
            if (len < sizeof(out->module)) {
                strncpy(out->module, module_start, len);
                out->module[len] = '\0';
            }
        }
    }
    
    // Find "handler":"..."
    const char* handler_start = strstr(json, "\"handler\":\"");
    if (handler_start) {
        handler_start += 11; // skip "handler":"
        const char* handler_end = strchr(handler_start, '"');
        if (handler_end) {
            size_t len = handler_end - handler_start;
            if (len < sizeof(out->handler)) {
                strncpy(out->handler, handler_start, len);
                out->handler[len] = '\0';
            }
        }
    }
    
    // Find "name":"..." (optional)
    const char* name_start = strstr(json, "\"name\":\"");
    if (name_start) {
        name_start += 8; // skip "name":"
        const char* name_end = strchr(name_start, '"');
        if (name_end) {
            size_t len = name_end - name_start;
            if (len < sizeof(out->name)) {
                strncpy(out->name, name_start, len);
                out->name[len] = '\0';
            }
        }
    }
    
    // Find "runtime":"..." (optional)
    const char* runtime_start = strstr(json, "\"runtime\":\"");
    if (runtime_start) {
        runtime_start += 11; // skip "runtime":"
        const char* runtime_end = strchr(runtime_start, '"');
        if (runtime_end) {
            size_t len = runtime_end - runtime_start;
            if (len < sizeof(out->runtime)) {
                strncpy(out->runtime, runtime_start, len);
                out->runtime[len] = '\0';
            }
        }
    }
    
    // Find "memory": (optional, integer)
    const char* memory_start = strstr(json, "\"memory\":");
    if (memory_start) {
        memory_start += 9; // skip "memory":
        out->memory = atoi(memory_start);
    }
    
    // Find "timeout": (optional, integer)
    const char* timeout_start = strstr(json, "\"timeout\":");
    if (timeout_start) {
        timeout_start += 10; // skip "timeout":
        out->timeout = atoi(timeout_start);
    }
    
    return (out->module[0] && out->handler[0]) ? 0 : -1;
}

// -------------------------------------------------------------------
// Find function by HTTP method and URI
// Returns 0 on success, -1 if not found
// -------------------------------------------------------------------
int find_function(const char *method, const char *uri, struct FunctionDescriptor *out) {
    if (!g_kv || !g_lock || !method || !uri || !out) {
        fprintf(stderr, "[CONFIG] Config loader not initialized or invalid parameters\n");
        return -1;
    }
    
    // Build route key: "METHOD:URI" (e.g., "POST:/resize")
    char route_key[256];
    snprintf(route_key, sizeof(route_key), "%s:%s", method, uri);
    
    // Single KV lookup - get complete JSON config
    pthread_rwlock_rdlock(g_lock);
    const char* function_json = kv_get(g_kv, route_key);
    pthread_rwlock_unlock(g_lock);
    
    if (!function_json) {
        return -1; // Route not found
    }
    
    // Fill output structure with method and uri
    strncpy(out->method, method, sizeof(out->method) - 1);
    out->method[sizeof(out->method) - 1] = '\0';
    
    strncpy(out->uri, uri, sizeof(out->uri) - 1);
    out->uri[sizeof(out->uri) - 1] = '\0';
    
    // Parse JSON to extract all fields
    if (parse_function_json(function_json, out) != 0) {
        fprintf(stderr, "[CONFIG] Failed to parse function JSON for route '%s'\n", route_key);
        return -1;
    }
    
    return 0;
}
