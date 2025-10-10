// worker.c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>      // For strcasecmp
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <pthread.h>
#include <time.h>
#include "metrics.h"
#include "metrics_reader.h"
#include "metrics_smoother.h"
#include "fd_passing.h"
#include "worker_protocol.h"
#include "http_handler.h"

#define BUFFER_SIZE 8192  // Larger buffer for function I/O

// Global worker state
typedef struct {
    int worker_id;
    const char* sock_path;
    uint32_t total_requests;
    uint32_t total_errors;
    int is_busy;
    metrics_smoother_t smoother;
    pthread_mutex_t lock;
} worker_state_t;

static worker_state_t g_state = {0};

// -------------------------------------------------------------------
// Send metrics to load balancer
// -------------------------------------------------------------------
void send_metrics(void) {
    static int metrics_fd = -1;
    
    // Open metrics socket (only once)
    if (metrics_fd < 0) {
        metrics_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
        if (metrics_fd < 0) {
            perror("[Worker] Failed to create metrics socket");
            return;
        }
    }
    
    // Build metrics structure
    worker_metrics_t m = {0};
    m.pid = getpid();
    m.worker_id = g_state.worker_id;
    
    pthread_mutex_lock(&g_state.lock);
    m.requests = g_state.total_requests;
    m.errors = g_state.total_errors;
    int busy = g_state.is_busy;
    
    // Read actual system metrics
    float cpu_raw = get_process_cpu_usage();
    float mem_raw = get_process_memory_mb();
    float io_raw = get_process_io_rate();  // KB/s
    
    // Apply EMA smoothing and normalization
    float cpu_smooth, mem_smooth, io_smooth;
    metrics_smoother_update(&g_state.smoother, cpu_raw, mem_raw, io_raw,
                           &cpu_smooth, &mem_smooth, &io_smooth);
    
    pthread_mutex_unlock(&g_state.lock);
    
    // Fill metrics with smoothed & normalized values
    m.cpu = cpu_smooth;
    m.mem = mem_smooth;
    m.io = io_smooth;
    m.score = calculate_load_score(cpu_smooth, mem_smooth, io_smooth);
    m.timestamp = get_timestamp_ms();
    strncpy(m.status, busy ? "busy" : "idle", sizeof(m.status) - 1);
    
    // Send to load balancer
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, METRICS_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    sendto(metrics_fd, &m, sizeof(m), 0, (struct sockaddr*)&addr, sizeof(addr));
}

// -------------------------------------------------------------------
// Metrics sender thread
// -------------------------------------------------------------------
void* metrics_thread(void* arg) {
    (void)arg;

    // Wait a bit for load balancer to start
    sleep(2);
    
    while (1) {
        send_metrics();
        usleep(500000);  // Send metrics every 500ms
    }
    
    return NULL;
}

// -------------------------------------------------------------------
// Main worker function
// -------------------------------------------------------------------
int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <socket_path> [worker_id]\n", argv[0]);
        return 1;
    }
    
    const char *sock_path = argv[1];
    int worker_id = (argc > 2) ? atoi(argv[2]) : 0;
    
    // Initialize worker state
    g_state.worker_id = worker_id;
    g_state.sock_path = sock_path;
    g_state.total_requests = 0;
    g_state.total_errors = 0;
    g_state.is_busy = 0;
    metrics_smoother_init(&g_state.smoother);
    pthread_mutex_init(&g_state.lock, NULL);
    
    // Create request handling socket
    int fd, client;
    struct sockaddr_un addr;

    unlink(sock_path);
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind");
        exit(1);
    }
    listen(fd, 4);
    printf("[Worker #%d] Ready on %s (PID=%d)\n", worker_id, sock_path, getpid());
    
    // Start metrics sender thread
    pthread_t metrics_tid;
    pthread_create(&metrics_tid, NULL, metrics_thread, NULL);
    pthread_detach(metrics_tid);

    while (1) {
        client = accept(fd, NULL, NULL);
        if (client < 0) continue;

        // Mark as busy
        pthread_mutex_lock(&g_state.lock);
        g_state.is_busy = 1;
        g_state.total_requests++;
        pthread_mutex_unlock(&g_state.lock);

        // Receive client FD and request metadata
        worker_request_t req;
        ssize_t received_len;
        int client_fd = recvfd(client, &req, sizeof(req), &received_len);
        
        if (client_fd < 0 || received_len != sizeof(req)) {
            fprintf(stderr, "[Worker #%d] Failed to receive client FD\n", worker_id);
            
            pthread_mutex_lock(&g_state.lock);
            g_state.total_errors++;
            g_state.is_busy = 0;
            pthread_mutex_unlock(&g_state.lock);
            
            close(client);
            continue;
        }
        
        printf("[Worker #%d] Received client FD=%d, runtime=%s, module=%s\n", 
               worker_id, client_fd, req.runtime, req.module);
        
        // Close gateway connection (we have the client FD now)
        close(client);
        
        char runtime[MAX_RUNTIME_LEN];
        char module_path[MAX_MODULE_PATH];
        char handler[MAX_HANDLER_LEN];
        
        memcpy(runtime, req.runtime, sizeof(runtime));
        memcpy(module_path, req.module, sizeof(module_path));
        memcpy(handler, req.handler, sizeof(handler));
        
        char request_body[4096] = {0};
        if (req.body_len > 0 && req.body_len < sizeof(request_body)) {
            memcpy(request_body, req.body, req.body_len);
            request_body[req.body_len] = '\0';
        }

        // Runtime detection from descriptor (not extension)
        int is_php = (strcasecmp(runtime, "php") == 0);
        int is_wasm = (strcasecmp(runtime, "wasm") == 0);
        
        printf("[Worker #%d] Executing: %s [Runtime: %s]\n", 
               worker_id, module_path, runtime);
        
        // Create pipe to capture function output
        int pipefd[2];
        if (pipe(pipefd) == -1) {
            perror("[Worker] pipe");
            send_http_500(client_fd, "{\"error\":\"pipe creation failed\"}");
            close(client_fd);
            
            pthread_mutex_lock(&g_state.lock);
            g_state.total_errors++;
            g_state.is_busy = 0;
            pthread_mutex_unlock(&g_state.lock);
            continue;
        }
        
        // Create stdin pipe for passing body
        int stdin_pipe[2];
        if (pipe(stdin_pipe) == -1) {
            perror("[Worker] stdin pipe");
            close(pipefd[0]);
            close(pipefd[1]);
            send_http_500(client_fd, "{\"error\":\"stdin pipe creation failed\"}");
            close(client_fd);
            
            pthread_mutex_lock(&g_state.lock);
            g_state.total_errors++;
            g_state.is_busy = 0;
            pthread_mutex_unlock(&g_state.lock);
            continue;
        }
        
        pid_t pid = fork();
        if (pid == 0) {
            // Child process: setup I/O redirection and execute
            
            // Redirect stdin from pipe (to receive body)
            close(stdin_pipe[1]);  // Close write end
            dup2(stdin_pipe[0], STDIN_FILENO);
            close(stdin_pipe[0]);
            
            // Redirect stdout/stderr to output pipe
            close(pipefd[0]);  // Close read end
            dup2(pipefd[1], STDOUT_FILENO);
            dup2(pipefd[1], STDERR_FILENO);
            close(pipefd[1]);
            
            // Execute based on runtime
            if (is_php) {
                // Execute PHP script (reads from stdin)
                execlp("php", "php", module_path, NULL);
                perror("[Worker] execlp php");
            } else if (is_wasm) {
                // Execute WASM with wasmer (WASI standard)
                // Body will be available on stdin
                execlp("wasmer", "wasmer", "run", module_path, NULL);
                perror("[Worker] execlp wasmer");
            } else {
                fprintf(stderr, "{\"error\":\"Unknown runtime: %s\"}", runtime);
            }
            _exit(127);
        } else if (pid > 0) {
            // Parent: write body to stdin pipe, then read output
            close(stdin_pipe[0]);  // Close read end of stdin pipe
            close(pipefd[1]);  // Close write end of output pipe
            
            // Write request body to child's stdin
            if (request_body[0]) {
                ssize_t written = write(stdin_pipe[1], request_body, strlen(request_body));
                if (written < 0) {
                    perror("[Worker] write to stdin_pipe");
                }
            }
            close(stdin_pipe[1]);  // Close stdin pipe (EOF for child)
            
            char function_output[8192] = {0};
            ssize_t total_read = 0;
            ssize_t n;
            
            // Read all output from function
            while ((n = read(pipefd[0], function_output + total_read, 
                            sizeof(function_output) - total_read - 1)) > 0) {
                total_read += n;
                if (total_read >= (ssize_t)(sizeof(function_output) - 1)) {
                    break;  // Buffer full
                }
            }
            
            close(pipefd[0]);
            
            // Wait for child to complete
            int status;
            waitpid(pid, &status, 0);
            
            int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            printf("[Worker #%d] Child exited: code=%d, output_bytes=%zd\n", 
                   worker_id, exit_code, total_read);
            
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0 && total_read > 0) {
                // Success: send HTTP 200 response to client
                function_output[total_read] = '\0';
                
                // If output looks like JSON, send as-is, otherwise wrap it
                if (function_output[0] == '{' || function_output[0] == '[') {
                    send_http_200(client_fd, function_output);
                } else {
                    char wrapped[8192];
                    snprintf(wrapped, sizeof(wrapped), "{\"result\":\"%s\"}", function_output);
                    send_http_200(client_fd, wrapped);
                }
                
                printf("[Worker #%d] Sent HTTP 200 to client (%zd bytes): %.100s%s\n", 
                       worker_id, total_read, function_output, total_read > 100 ? "..." : "");
            } else {
                // Error: send HTTP 500 error to client
                char error_msg[512];
                snprintf(error_msg, sizeof(error_msg), 
                        "{\"error\":\"Function failed\",\"exit_code\":%d,\"output_bytes\":%zd,\"output\":\"%.200s\"}", 
                        exit_code, total_read, total_read > 0 ? function_output : "");
                send_http_500(client_fd, error_msg);
                printf("[Worker #%d] Sent HTTP 500 to client: exit_code=%d, output=%zd bytes\n", 
                       worker_id, exit_code, total_read);
            }
        } else {
            // Fork failed
            close(stdin_pipe[0]);
            close(stdin_pipe[1]);
            close(pipefd[0]);
            close(pipefd[1]);
            perror("[Worker] fork");
            send_http_500(client_fd, "{\"error\":\"fork failed\"}");
        }
        
        // Mark as idle
        pthread_mutex_lock(&g_state.lock);
        g_state.is_busy = 0;
        pthread_mutex_unlock(&g_state.lock);
        
        // Close client FD (we own it now)
        close(client_fd);
    }
    
    pthread_mutex_destroy(&g_state.lock);
    return 0;
}