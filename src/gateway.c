// gateway.c
// Unified HTTP Gateway + Load Balancer + Config Loader

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "http_handler.h"
#include "config_loader.h"
#include "kv.h"
#include "metrics.h"
#include "faas_compiler.h"
#include "fd_passing.h"
#include "worker_protocol.h"

#define NUM_WORKERS 4
#define HTTP_PORT 8080

const char *WORKER_SOCKS[NUM_WORKERS] = {
    "/tmp/faas_worker_0.sock",
    "/tmp/faas_worker_1.sock",
    "/tmp/faas_worker_2.sock",
    "/tmp/faas_worker_3.sock"
};

// External functions from metrics_collector.c
extern int start_metrics_collector(pthread_t* thread_out);
extern void print_all_metrics(void);
extern int get_worker_metrics(int worker_id, worker_metrics_t* out);

// External function from kv_sqlite_sync.c
extern int kv_sync_init(
    kv_t* kv,
    const char* db_path,
    const char* table,
    int interval,
    pthread_rwlock_t* lock,
    volatile int* running,
    pthread_t* thread_out
);

// Global state
static volatile int g_running = 1;
static kv_t* g_kv = NULL;
static pthread_rwlock_t g_kv_lock;

// -------------------------------------------------------------------
// Signal handler
// -------------------------------------------------------------------
void signal_handler(int sig) {
    (void)sig;
    printf("\n[GATEWAY] Shutdown requested...\n");
    g_running = 0;
}

// -------------------------------------------------------------------
// Unescape JSON string (basic: \" and \\n)
// -------------------------------------------------------------------
void unescape_json_string(char* str) {
    if (!str) return;
    
    char* read = str;
    char* write = str;
    
    while (*read) {
        if (*read == '\\' && *(read + 1)) {
            read++; // Skip backslash
            switch (*read) {
                case 'n':  *write++ = '\n'; break;
                case 't':  *write++ = '\t'; break;
                case 'r':  *write++ = '\r'; break;
                case '"':  *write++ = '"';  break;
                case '\\': *write++ = '\\'; break;
                default:   *write++ = *read; break;
            }
            read++;
        } else {
            *write++ = *read++;
        }
    }
    *write = '\0';
}

// -------------------------------------------------------------------
// Validate descriptor JSON structure
// -------------------------------------------------------------------
int validate_descriptor(const char* descriptor, char* error_msg, size_t error_msg_size) {
    if (!descriptor || strlen(descriptor) == 0) {
        snprintf(error_msg, error_msg_size, "Descriptor is empty");
        return -1;
    }
    
    // Check basic JSON structure
    if (descriptor[0] != '{') {
        snprintf(error_msg, error_msg_size, "Descriptor must be a JSON object starting with '{'");
        return -1;
    }
    
    // Check for required field: runtime
    if (!strstr(descriptor, "\"runtime\"")) {
        snprintf(error_msg, error_msg_size, "Missing required field: 'runtime'");
        return -1;
    }
    
    // Validate runtime is supported (flexible: accepts spaces)
    const char* supported_types[] = {
        "c", "cpp", "c++", "python", "rust", "go", "tinygo", "wasm", "php", NULL
    };
    
    // Extract runtime value
    char* runtime_pos = strstr(descriptor, "\"runtime\"");
    if (!runtime_pos) {
        snprintf(error_msg, error_msg_size, "Missing required field: 'runtime'");
        return -1;
    }
    
    // Find the colon and value
    char* colon = strchr(runtime_pos, ':');
    if (!colon) {
        snprintf(error_msg, error_msg_size, "Malformed runtime field");
        return -1;
    }
    colon++;
    while (*colon == ' ' || *colon == '\t' || *colon == '\n') colon++;
    if (*colon != '"') {
        snprintf(error_msg, error_msg_size, "Runtime must be a string");
        return -1;
    }
    colon++; // Skip opening quote
    
    // Extract runtime value
    char runtime_value[64];
    const char* value_end = strchr(colon, '"');
    if (!value_end) {
        snprintf(error_msg, error_msg_size, "Unterminated runtime string");
        return -1;
    }
    size_t value_len = value_end - colon;
    if (value_len >= sizeof(runtime_value)) {
        snprintf(error_msg, error_msg_size, "Runtime value too long");
        return -1;
    }
    memcpy(runtime_value, colon, value_len);
    runtime_value[value_len] = '\0';
    
    // Check if runtime is supported
    int type_found = 0;
    for (int i = 0; supported_types[i] != NULL; i++) {
        if (strcmp(runtime_value, supported_types[i]) == 0) {
            type_found = 1;
            break;
        }
    }
    
    if (!type_found) {
        snprintf(error_msg, error_msg_size, 
                 "Invalid or unsupported runtime '%s'. Supported: c, cpp, c++, python, rust, go, tinygo, wasm, php", 
                 runtime_value);
        return -1;
    }
    
    // Optional: validate memory and timeout are numbers if present
    char* memory_pos = strstr(descriptor, "\"memory\":");
    if (memory_pos) {
        memory_pos += 9; // Skip "memory":
        while (*memory_pos == ' ') memory_pos++;
        if (*memory_pos < '0' || *memory_pos > '9') {
            snprintf(error_msg, error_msg_size, "Field 'memory' must be a number");
            return -1;
        }
    }
    
    char* timeout_pos = strstr(descriptor, "\"timeout\":");
    if (timeout_pos) {
        timeout_pos += 10; // Skip "timeout":
        while (*timeout_pos == ' ') timeout_pos++;
        if (*timeout_pos < '0' || *timeout_pos > '9') {
            snprintf(error_msg, error_msg_size, "Field 'timeout' must be a number");
            return -1;
        }
    }
    
    // Validate method if present
    char* method_pos = strstr(descriptor, "\"method\":\"");
    if (method_pos) {
        method_pos += 10; // Skip "method":"
        if (strncmp(method_pos, "GET", 3) != 0 && 
            strncmp(method_pos, "POST", 4) != 0 &&
            strncmp(method_pos, "PUT", 3) != 0 &&
            strncmp(method_pos, "DELETE", 6) != 0 &&
            strncmp(method_pos, "PATCH", 5) != 0) {
            snprintf(error_msg, error_msg_size, 
                     "Invalid method. Supported: GET, POST, PUT, DELETE, PATCH");
            return -1;
        }
    }
    
    return 0; // Valid
}

// -------------------------------------------------------------------
// Smart worker selection (score-based)
// -------------------------------------------------------------------
int select_worker(void) {
    static int fallback_counter = 0;
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
    
    // Fallback: if no metrics, use round-robin
    if (best_worker == -1) {
        best_worker = fallback_counter % NUM_WORKERS;
        fallback_counter++;
        printf("[GATEWAY] Selected Worker #%d (no metrics, round-robin)\n", best_worker);
    } else {
        printf("[GATEWAY] Selected Worker #%d (score: %.2f)\n", best_worker, best_score);
    }
    
    return best_worker;
}

// -------------------------------------------------------------------
// Send request to worker (with client FD passing)
// -------------------------------------------------------------------
int send_to_worker_with_fd(int idx, int client_fd, const char *runtime, 
                            const char *module, const char *handler, const char *body) {
    int fd;
    struct sockaddr_un addr;
    
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("[GATEWAY] socket");
        return -1;
    }
    
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, WORKER_SOCKS[idx], sizeof(addr.sun_path) - 1);
    
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("[GATEWAY] connect");
        close(fd);
        return -1;
    }
    
    // Prepare request metadata
    worker_request_t req = {0};
    strncpy(req.runtime, runtime, sizeof(req.runtime) - 1);
    strncpy(req.module, module, sizeof(req.module) - 1);
    strncpy(req.handler, handler, sizeof(req.handler) - 1);
    
    if (body) {
        size_t body_len = strlen(body);
        if (body_len < sizeof(req.body)) {
            memcpy(req.body, body, body_len);
            req.body_len = body_len;
        } else {
            memcpy(req.body, body, sizeof(req.body) - 1);
            req.body_len = sizeof(req.body) - 1;
        }
    }
    
    // Send client FD with metadata
    if (sendfd(fd, client_fd, &req, sizeof(req)) != 0) {
        fprintf(stderr, "[GATEWAY] Failed to send FD to worker %d\n", idx);
        close(fd);
        return -1;
    }
    
    printf("[GATEWAY] Sent client FD to worker #%d\n", idx);
    
    close(fd);
    return 0;
}

// -------------------------------------------------------------------
// Handle HTTP client request (thread per request)
// -------------------------------------------------------------------
void* handle_http_client(void* arg) {
    int client_fd = *(int*)arg;
    free(arg);
    
    // 1. Parse HTTP request
    http_request_t req;
    if (parse_http_request(client_fd, &req) != 0) {
        send_http_500(client_fd, "Failed to parse request");
        close(client_fd);
        return NULL;
    }
    
    printf("[GATEWAY] HTTP %s %s (body: %zu bytes)\n", 
           req.method, req.uri, req.body_len);
    
    // Special route: GET /upload returns HTML page from file
    if (strcmp(req.method, "GET") == 0 && strcmp(req.uri, "/upload") == 0) {
        serve_html_file(client_fd, "upload.html");
        close(client_fd);
        return NULL;
    }
    
    // Special route: POST /upload saves new function
    if (strcmp(req.method, "POST") == 0 && strcmp(req.uri, "/upload") == 0) {
        printf("[GATEWAY] Function upload requested\n");
        printf("[GATEWAY] Content-Type: %s\n", req.content_type);
        printf("[GATEWAY] Body length: %zu\n", req.body_len);
        
        // Check if multipart/form-data
        if (strstr(req.content_type, "multipart/form-data") == NULL) {
            send_http_500(client_fd, "{\"error\":\"Content-Type must be multipart/form-data\"}");
            close(client_fd);
            return NULL;
        }
        
        // Extract boundary from Content-Type
        char* boundary_start = strstr(req.content_type, "boundary=");
        if (!boundary_start) {
            send_http_500(client_fd, "{\"error\":\"No boundary in Content-Type\"}");
            close(client_fd);
            return NULL;
        }
        boundary_start += 9; // Skip "boundary="
        char boundary[256];
        strncpy(boundary, boundary_start, sizeof(boundary) - 1);
        boundary[sizeof(boundary) - 1] = '\0';
        
        // Parse multipart upload
        multipart_upload_t upload;
        if (parse_multipart_upload(req.body, req.body_len, boundary, &upload) != 0) {
            send_http_500(client_fd, "{\"error\":\"Failed to parse multipart upload\"}");
            close(client_fd);
            return NULL;
        }
        
        // Find code and descriptor files
        uploaded_file_t* code_file = NULL;
        uploaded_file_t* desc_file = NULL;
        
        for (int i = 0; i < upload.file_count; i++) {
            if (strcmp(upload.files[i].name, "code") == 0) {
                code_file = &upload.files[i];
            } else if (strcmp(upload.files[i].name, "descriptor") == 0) {
                desc_file = &upload.files[i];
            }
        }
        
        if (!code_file || !desc_file) {
            free_multipart_upload(&upload);
            send_http_500(client_fd, "{\"error\":\"Missing code or descriptor file\"}");
            close(client_fd);
            return NULL;
        }
        
        printf("[GATEWAY] Code file: %s (%zu bytes)\n", code_file->filename, code_file->data_len);
        printf("[GATEWAY] Descriptor file: %s (%zu bytes)\n", desc_file->filename, desc_file->data_len);
        
        // Use file data directly (already null-terminated)
        char* code = code_file->data;
        char* descriptor = desc_file->data;
        
        // Validate descriptor structure
        char validation_error[512];
        if (validate_descriptor(descriptor, validation_error, sizeof(validation_error)) != 0) {
            printf("[GATEWAY] Invalid descriptor: %s\n", validation_error);
            char error_response[1024];
            snprintf(error_response, sizeof(error_response), 
                     "{\"status\":\"error\",\"message\":\"Invalid descriptor\",\"details\":\"%s\"}", 
                     validation_error);
            send_http_500(client_fd, error_response);
            free_multipart_upload(&upload);
            close(client_fd);
            return NULL;
        }
        
        printf("[GATEWAY] ✓ Descriptor validation passed\n");
        
        // Generate unique ID for this function
        static unsigned int upload_counter = 0;
        char uuid[128];
        snprintf(uuid, sizeof(uuid), "func_%lu_%u_%d", 
                 (unsigned long)time(NULL), upload_counter++, getpid() % 1000);
        
        printf("[GATEWAY] Generated UUID: %s\n", uuid);
        
        // Create /tmp/progfile directory (don't clean - concurrent uploads!)
        system("mkdir -p /tmp/progfile");
        
        // Save code file with UUID as filename (detect extension from descriptor)
        // Extract runtime value from descriptor (already validated)
        char* runtime_value = extract_json_field(descriptor, "\"runtime\"");
        const char* runtime = runtime_value ? runtime_value : "";
        
        char code_file_path[512];
        if (strcmp(runtime, "python") == 0) {
            snprintf(code_file_path, sizeof(code_file_path), "/tmp/progfile/%s.py", uuid);
        } else if (strcmp(runtime, "php") == 0) {
            snprintf(code_file_path, sizeof(code_file_path), "/tmp/progfile/%s.php", uuid);
        } else if (strcmp(runtime, "c") == 0) {
            snprintf(code_file_path, sizeof(code_file_path), "/tmp/progfile/%s.c", uuid);
        } else if (strcmp(runtime, "cpp") == 0 || strcmp(runtime, "c++") == 0) {
            snprintf(code_file_path, sizeof(code_file_path), "/tmp/progfile/%s.cpp", uuid);
        } else if (strcmp(runtime, "rust") == 0) {
            snprintf(code_file_path, sizeof(code_file_path), "/tmp/progfile/%s.rs", uuid);
        } else if (strcmp(runtime, "go") == 0 || strcmp(runtime, "tinygo") == 0) {
            snprintf(code_file_path, sizeof(code_file_path), "/tmp/progfile/%s.go", uuid);
        } else if (strcmp(runtime, "wasm") == 0) {
            snprintf(code_file_path, sizeof(code_file_path), "/tmp/progfile/%s.wasm", uuid);
        } else {
            // Default fallback
            snprintf(code_file_path, sizeof(code_file_path), "/tmp/progfile/%s.txt", uuid);
        }
        
        FILE* code_fp = fopen(code_file_path, "w");
        if (!code_fp) {
            if (runtime_value) free(runtime_value);
            free_multipart_upload(&upload);
            send_http_500(client_fd, "Failed to save code file");
            close(client_fd);
            return NULL;
        }
        fwrite(code, 1, code_file->data_len, code_fp);
        fclose(code_fp);
        
        // Save descriptor as JSON with UUID to avoid concurrent overwrites
        char desc_file_path[512];
        snprintf(desc_file_path, sizeof(desc_file_path), "/tmp/progfile/%s_descriptor.json", uuid);
        FILE* desc_fp = fopen(desc_file_path, "w");
        if (!desc_fp) {
            if (runtime_value) free(runtime_value);
            free_multipart_upload(&upload);
            send_http_500(client_fd, "Failed to save descriptor");
            close(client_fd);
            return NULL;
        }
        fprintf(desc_fp, "%s", descriptor);
        fclose(desc_fp);
        
        printf("[GATEWAY] Saved code to %s\n", code_file_path);
        printf("[GATEWAY] Saved descriptor to %s\n", desc_file_path);
        
        // Call compiler function directly (not via system!)
        printf("[GATEWAY] Compiling function with UUID: %s\n", uuid);
        int compile_result = compile_function(uuid);
        
        if (compile_result != 0) {
            if (runtime_value) free(runtime_value);
            free_multipart_upload(&upload);
            char err[256];
            snprintf(err, sizeof(err), "Compilation failed (error code: %d)", compile_result);
            send_http_500(client_fd, err);
            close(client_fd);
            return NULL;
        }
        
        // Compiler succeeded and auto-inserted into SQLite
        printf("[GATEWAY] ✓ Compilation and SQL insert successful\n");
        printf("[GATEWAY] Function will be available after KV sync (<5s)\n");
        
        // Extract method from descriptor (default to POST if not found)
        char* method_extracted = extract_json_field(descriptor, "\"method\"");
        const char* function_method = method_extracted ? method_extracted : "POST";
        
        // Build function URI
        char function_uri[256];
        snprintf(function_uri, sizeof(function_uri), "/api/%s", uuid);
        
        char response[1024];
        snprintf(response, sizeof(response), 
                 "{\"status\":\"success\",\"message\":\"Function compiled and deployed\",\"uri\":\"%s\",\"method\":\"%s\",\"info\":\"Will be available in <5 seconds\"}", 
                 function_uri, function_method);
        
        if (method_extracted) free(method_extracted);
        if (runtime_value) free(runtime_value);
        
        send_http_200(client_fd, response);
        
        free_multipart_upload(&upload);
        close(client_fd);
        return NULL;
    }
    
    // 2. Find function via config loader (DIRECT CALL - no IPC!)
    struct FunctionDescriptor fn;
    if (find_function(req.method, req.uri, &fn) != 0) {
        printf("[GATEWAY] No function found for %s %s\n", req.method, req.uri);
        send_http_404(client_fd);
        close(client_fd);
        return NULL;
    }
    
    printf("[GATEWAY] Found: %s (runtime: %s, mem: %dMB, timeout: %ds)\n",
           fn.name, fn.runtime, fn.memory, fn.timeout);
    
    // 3. Smart worker selection (DIRECT CALL!)
    int worker_id = select_worker();
    
    // 4. Send client FD to worker (zero-copy, no proxy!)
    if (send_to_worker_with_fd(worker_id, client_fd, fn.runtime, fn.module, fn.handler, req.body) != 0) {
        send_http_500(client_fd, "Worker communication failed");
        close(client_fd);
        return NULL;
    }
    
    printf("[GATEWAY] Client FD delegated to worker #%d - gateway no longer proxies response\n", worker_id);
    
    // Note: Worker will write response directly to client and close the FD
    // Gateway doesn't wait for response or close client_fd (worker owns it now)
    
    return NULL;
}

// -------------------------------------------------------------------
// Main
// -------------------------------------------------------------------
int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    signal(SIGINT, signal_handler);
    
    printf("[GATEWAY] ═══ FaaS Gateway Starting ═══\n");
    
    // === Initialize KV store ===
    g_kv = kv_new(1024);
    if (!g_kv) {
        fprintf(stderr, "[GATEWAY] KV creation error\n");
        return 1;
    }
    pthread_rwlock_init(&g_kv_lock, NULL);
    
    // === Start KV sync thread ===
    pthread_t sync_thread;
    printf("[GATEWAY] Starting KV synchronization...\n");
    if (kv_sync_init(g_kv, "faas_meta.db", "functions", 5, 
                     &g_kv_lock, &g_running, &sync_thread) != 0) {
        fprintf(stderr, "[GATEWAY] Sync initialization error\n");
        kv_free(g_kv);
        pthread_rwlock_destroy(&g_kv_lock);
        return 1;
    }
    
    // === Initialize config loader ===
    config_loader_init(g_kv, &g_kv_lock);
    
    // === Start metrics collector ===
    pthread_t metrics_thread;
    printf("[GATEWAY] Starting metrics collector...\n");
    if (start_metrics_collector(&metrics_thread) != 0) {
        fprintf(stderr, "[GATEWAY] Warning: Metrics collector failed to start\n");
    }
    
    // Wait for initial data load
    sleep(2);
    
    // === Create HTTP server ===
    int server_fd = create_http_server(HTTP_PORT);
    if (server_fd < 0) {
        fprintf(stderr, "[GATEWAY] Failed to create HTTP server\n");
        g_running = 0;
        pthread_join(sync_thread, NULL);
        kv_free(g_kv);
        return 1;
    }
    
    printf("[GATEWAY] ✓ HTTP server listening on port %d\n", HTTP_PORT);
    printf("[GATEWAY] ✓ Gateway ready!\n\n");
    
    // === Main loop: Accept HTTP clients ===
    while (g_running) {
        int* client_fd = malloc(sizeof(int));
        if (!client_fd) {
            perror("malloc");
            continue;
        }
        
        *client_fd = accept(server_fd, NULL, NULL);
        if (*client_fd < 0) {
            free(client_fd);
            if (!g_running) break;
            continue;
        }
        
        // Create thread per HTTP request
        pthread_t thread;
        if (pthread_create(&thread, NULL, handle_http_client, client_fd) != 0) {
            perror("pthread_create");
            close(*client_fd);
            free(client_fd);
            continue;
        }
        pthread_detach(thread);
    }
    
    // === Cleanup ===
    printf("\n[GATEWAY] Shutting down...\n");
    close(server_fd);
    pthread_join(sync_thread, NULL);
    
    sleep(1);
    print_all_metrics();
    
    kv_free(g_kv);
    pthread_rwlock_destroy(&g_kv_lock);
    printf("[GATEWAY] Goodbye!\n");
    
    return 0;
}

