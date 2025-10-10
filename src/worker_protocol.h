#ifndef WORKER_PROTOCOL_H
#define WORKER_PROTOCOL_H

#include <stddef.h>

#define MAX_RUNTIME_LEN 32
#define MAX_MODULE_PATH 256
#define MAX_HANDLER_LEN 128
#define MAX_BODY_LEN 4096

// Message sent from gateway to worker (with client FD)
typedef struct {
    char runtime[MAX_RUNTIME_LEN];
    char module[MAX_MODULE_PATH];
    char handler[MAX_HANDLER_LEN];
    char body[MAX_BODY_LEN];
    size_t body_len;
} worker_request_t;

#endif

