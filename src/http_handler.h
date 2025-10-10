#ifndef HTTP_HANDLER_H
#define HTTP_HANDLER_H

#include <stddef.h>

// HTTP request structure
typedef struct {
    char method[16];           // GET, POST, PUT, DELETE, etc.
    char uri[512];             // /api/resize
    char headers[2048];        // Raw headers
    char content_type[128];    // application/json
    char body[65536];          // Request body
    size_t body_len;           // Body length
} http_request_t;

// HTTP response structure
typedef struct {
    int status_code;           // 200, 404, 500, etc.
    char content_type[128];    // application/json
    char body[131072];         // Response body (128KB for HTML pages)
    size_t body_len;           // Body length
} http_response_t;

// Uploaded file structure (for multipart/form-data)
typedef struct {
    char name[256];            // Field name (e.g., "code", "descriptor")
    char filename[256];        // Original filename (e.g., "function.c")
    char content_type[128];    // MIME type
    char* data;                // File content (malloc'd)
    size_t data_len;           // Content length
} uploaded_file_t;

// Multipart upload result
typedef struct {
    uploaded_file_t files[10]; // Max 10 files
    int file_count;            // Number of files uploaded
} multipart_upload_t;

// Parse HTTP request from socket
int parse_http_request(int client_fd, http_request_t* req);

// Parse multipart/form-data from request body
int parse_multipart_upload(const char* body, size_t body_len, const char* boundary, multipart_upload_t* upload);

// Free multipart upload data
void free_multipart_upload(multipart_upload_t* upload);

// Send HTTP response
void send_http_response(int client_fd, const http_response_t* resp);

// Convenience functions
void send_http_200(int client_fd, const char* body);
void send_http_404(int client_fd);
void send_http_500(int client_fd, const char* error);

// Serve HTML file from pages/ directory
int serve_html_file(int client_fd, const char* filename);

// Create HTTP server socket
int create_http_server(int port);

#endif
