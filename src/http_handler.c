// http_handler.c
// HTTP parsing and response handling

#define _GNU_SOURCE  // For strcasestr
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>  // For strcasecmp
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>  // For TCP_NODELAY
#include "http_handler.h"

#define BUFFER_SIZE 8192

// -------------------------------------------------------------------
// Parse HTTP request from socket
// -------------------------------------------------------------------
int parse_http_request(int client_fd, http_request_t* req) {
    if (!req) return -1;
    
    memset(req, 0, sizeof(http_request_t));
    
    char buffer[BUFFER_SIZE];
    ssize_t bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes <= 0) {
        return -1;
    }
    buffer[bytes] = '\0';
    printf("[HTTP] Received %zd bytes in first recv()\n", bytes);
    printf("[HTTP] Raw buffer:\n%s\n[END]\n", buffer);
    
    // Parse request line: "METHOD /path HTTP/1.1"
    char* line_end = strstr(buffer, "\r\n");
    if (!line_end) {
        return -1;
    }
    
    // Don't modify buffer yet - we need it intact for header parsing
    char request_line[512];
    size_t req_line_len = line_end - buffer;
    if (req_line_len >= sizeof(request_line)) req_line_len = sizeof(request_line) - 1;
    memcpy(request_line, buffer, req_line_len);
    request_line[req_line_len] = '\0';
    sscanf(request_line, "%15s %511s", req->method, req->uri);
    
    // Extract Content-Length header (search in full buffer)
    int content_length = 0;
    char* cl_header = strcasestr(buffer, "Content-Length:");
    if (cl_header) {
        cl_header += 15; // Skip "Content-Length:"
        while (*cl_header == ' ') cl_header++;
        content_length = atoi(cl_header);
        printf("[HTTP] Content-Length: %d\n", content_length);
    } else {
        printf("[HTTP] No Content-Length header found\n");
    }
    
    // Extract Content-Type header (search in full buffer)
    char* content_type = strcasestr(buffer, "Content-Type:");
    if (content_type) {
        content_type += 13; // Skip "Content-Type:"
        while (*content_type == ' ') content_type++;
        char* end = strstr(content_type, "\r\n");
        if (end) {
            size_t len = end - content_type;
            if (len < sizeof(req->content_type)) {
                strncpy(req->content_type, content_type, len);
                req->content_type[len] = '\0';
            }
        }
    }
    
    // Find body (after "\r\n\r\n")
    char* body_start = strstr(buffer, "\r\n\r\n");
    if (body_start && content_length > 0) {
        body_start += 4; // Skip "\r\n\r\n"
        size_t already_read = bytes - (body_start - buffer);
        
        // Copy what we already have
        if (already_read > 0 && already_read < sizeof(req->body)) {
            memcpy(req->body, body_start, already_read);
        }
        
        // Read remaining body if needed
        size_t remaining = content_length - already_read;
        if (remaining > 0 && (already_read + remaining) < sizeof(req->body)) {
            ssize_t more = recv(client_fd, req->body + already_read, remaining, 0);
            if (more > 0) {
                req->body_len = already_read + more;
            } else {
                req->body_len = already_read;
            }
        } else {
            req->body_len = already_read;
        }
        
        req->body[req->body_len] = '\0';
    }
    
    return 0;
}

// -------------------------------------------------------------------
// Send HTTP response
// -------------------------------------------------------------------
void send_http_response(int client_fd, const http_response_t* resp) {
    if (!resp) return;
    
    char response[BUFFER_SIZE];
    int len = snprintf(response, sizeof(response),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        resp->status_code,
        resp->status_code == 200 ? "OK" :
        resp->status_code == 404 ? "Not Found" :
        resp->status_code == 500 ? "Internal Server Error" : "Unknown",
        resp->content_type[0] ? resp->content_type : "text/plain",
        resp->body_len
    );
    
    send(client_fd, response, len, 0);
    if (resp->body_len > 0) {
        send(client_fd, resp->body, resp->body_len, 0);
    }
}

// -------------------------------------------------------------------
// Convenience: Send 200 OK
// -------------------------------------------------------------------
void send_http_200(int client_fd, const char* body) {
    http_response_t resp = {0};
    resp.status_code = 200;
    strcpy(resp.content_type, "application/json");
    
    if (body) {
        strncpy(resp.body, body, sizeof(resp.body) - 1);
        resp.body_len = strlen(resp.body);
    } else {
        strcpy(resp.body, "{\"status\":\"ok\"}");
        resp.body_len = strlen(resp.body);
    }
    
    send_http_response(client_fd, &resp);
}

// -------------------------------------------------------------------
// Convenience: Send 404 Not Found
// -------------------------------------------------------------------
void send_http_404(int client_fd) {
    http_response_t resp = {0};
    resp.status_code = 404;
    strcpy(resp.content_type, "application/json");
    strcpy(resp.body, "{\"error\":\"Function not found\"}");
    resp.body_len = strlen(resp.body);
    send_http_response(client_fd, &resp);
}

// -------------------------------------------------------------------
// Convenience: Send 500 Internal Server Error
// -------------------------------------------------------------------
void send_http_500(int client_fd, const char* error) {
    http_response_t resp = {0};
    resp.status_code = 500;
    strcpy(resp.content_type, "application/json");
    
    if (error) {
        snprintf(resp.body, sizeof(resp.body), "{\"error\":\"%s\"}", error);
    } else {
        strcpy(resp.body, "{\"error\":\"Internal server error\"}");
    }
    resp.body_len = strlen(resp.body);
    
    send_http_response(client_fd, &resp);
}

// -------------------------------------------------------------------
// Serve HTML file from pages/ directory
// -------------------------------------------------------------------
int serve_html_file(int client_fd, const char* filename) {
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "pages/%s", filename);
    
    FILE* fp = fopen(filepath, "r");
    if (!fp) {
        fprintf(stderr, "[HTTP] Failed to open %s\n", filepath);
        send_http_404(client_fd);
        return -1;
    }
    
    // Read entire file
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    if (file_size <= 0 || file_size > 1024 * 1024) {  // Max 1MB
        fprintf(stderr, "[HTTP] File size invalid: %ld bytes\n", file_size);
        fclose(fp);
        send_http_500(client_fd, "File too large or empty");
        return -1;
    }
    
    char* content = malloc(file_size + 1);
    if (!content) {
        fclose(fp);
        send_http_500(client_fd, "Memory allocation failed");
        return -1;
    }
    
    size_t read_size = fread(content, 1, file_size, fp);
    fclose(fp);
    
    if ((long)read_size != file_size) {
        free(content);
        send_http_500(client_fd, "File read error");
        return -1;
    }
    
    content[file_size] = '\0';
    
    // Send HTML response
    http_response_t resp = {0};
    resp.status_code = 200;
    strcpy(resp.content_type, "text/html; charset=utf-8");
    strncpy(resp.body, content, sizeof(resp.body) - 1);
    resp.body_len = strlen(resp.body);
    
    send_http_response(client_fd, &resp);
    
    free(content);
    printf("[HTTP] Served %s (%zu bytes)\n", filepath, resp.body_len);
    return 0;
}

// -------------------------------------------------------------------
// Create HTTP server socket
// -------------------------------------------------------------------
int create_http_server(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return -1;
    }
    
    // Socket options for performance
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));  // Multiple threads can accept()
    
    // Disable Nagle's algorithm for lower latency
    setsockopt(server_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server_fd);
        return -1;
    }
    
    // Large backlog for high-concurrency scenarios
    if (listen(server_fd, 2048) < 0) {
        perror("listen");
        close(server_fd);
        return -1;
    }
    
    return server_fd;
}

// -------------------------------------------------------------------
// Parse multipart/form-data
// -------------------------------------------------------------------
int parse_multipart_upload(const char* body, size_t body_len, const char* boundary, multipart_upload_t* upload) {
    if (!body || !boundary || !upload || body_len == 0) {
        return -1;
    }
    
    memset(upload, 0, sizeof(multipart_upload_t));
    
    // Build boundary markers
    char start_boundary[512];
    char end_boundary[512];
    snprintf(start_boundary, sizeof(start_boundary), "--%s", boundary);
    snprintf(end_boundary, sizeof(end_boundary), "--%s--", boundary);
    
    printf("[HTTP] Parsing multipart with boundary: [%s]\n", boundary);
    printf("[HTTP] Body length: %zu bytes\n", body_len);
    
    const char* pos = body;
    const char* end = body + body_len;
    
    // Skip to first boundary
    const char* first_boundary = strstr(pos, start_boundary);
    if (!first_boundary) {
        printf("[HTTP] No start boundary found\n");
        return -1;
    }
    pos = first_boundary + strlen(start_boundary);
    
    // Parse each part
    while (pos < end && upload->file_count < 10) {
        // Skip CRLF after boundary
        if (*pos == '\r') pos++;
        if (*pos == '\n') pos++;
        
        // Check for end boundary
        if (strncmp(pos, end_boundary, strlen(end_boundary)) == 0) {
            break;
        }
        
        uploaded_file_t* file = &upload->files[upload->file_count];
        
        // Parse headers (Content-Disposition, Content-Type, etc.)
        const char* headers_end = strstr(pos, "\r\n\r\n");
        if (!headers_end) {
            printf("[HTTP] No headers end found for part %d\n", upload->file_count);
            break;
        }
        
        // Extract field name from Content-Disposition
        const char* disp = strstr(pos, "Content-Disposition:");
        if (disp && disp < headers_end) {
            const char* name_start = strstr(disp, "name=\"");
            if (name_start && name_start < headers_end) {
                name_start += 6; // Skip 'name="'
                const char* name_end = strchr(name_start, '"');
                if (name_end && name_end < headers_end) {
                    size_t name_len = name_end - name_start;
                    if (name_len < sizeof(file->name)) {
                        memcpy(file->name, name_start, name_len);
                        file->name[name_len] = '\0';
                    }
                }
            }
            
            // Extract filename if present
            const char* filename_start = strstr(disp, "filename=\"");
            if (filename_start && filename_start < headers_end) {
                filename_start += 10; // Skip 'filename="'
                const char* filename_end = strchr(filename_start, '"');
                if (filename_end && filename_end < headers_end) {
                    size_t filename_len = filename_end - filename_start;
                    if (filename_len < sizeof(file->filename)) {
                        memcpy(file->filename, filename_start, filename_len);
                        file->filename[filename_len] = '\0';
                    }
                }
            }
        }
        
        // Extract Content-Type if present
        const char* ct = strstr(pos, "Content-Type:");
        if (ct && ct < headers_end) {
            ct += 13; // Skip "Content-Type:"
            while (*ct == ' ') ct++;
            const char* ct_end = strstr(ct, "\r\n");
            if (ct_end && ct_end < headers_end) {
                size_t ct_len = ct_end - ct;
                if (ct_len < sizeof(file->content_type)) {
                    memcpy(file->content_type, ct, ct_len);
                    file->content_type[ct_len] = '\0';
                }
            }
        }
        
        // Data starts after headers
        const char* data_start = headers_end + 4; // Skip "\r\n\r\n"
        
        // Find next boundary
        const char* next_boundary = strstr(data_start, start_boundary);
        if (!next_boundary) {
            // Try end boundary
            next_boundary = strstr(data_start, end_boundary);
            if (!next_boundary) {
                printf("[HTTP] No next boundary found for part %d\n", upload->file_count);
                break;
            }
        }
        
        // Data ends before boundary (remove trailing CRLF)
        const char* data_end = next_boundary;
        if (data_end > data_start + 2 && *(data_end-2) == '\r' && *(data_end-1) == '\n') {
            data_end -= 2;
        }
        
        file->data_len = data_end - data_start;
        file->data = malloc(file->data_len + 1);
        if (!file->data) {
            printf("[HTTP] Memory allocation failed for part %d\n", upload->file_count);
            return -1;
        }
        
        memcpy(file->data, data_start, file->data_len);
        file->data[file->data_len] = '\0';
        
        printf("[HTTP] Parsed part %d: name=[%s] filename=[%s] size=%zu\n",
               upload->file_count, file->name, file->filename, file->data_len);
        
        upload->file_count++;
        pos = next_boundary;
    }
    
    printf("[HTTP] Total parts parsed: %d\n", upload->file_count);
    return 0;
}

// -------------------------------------------------------------------
// Free multipart upload data
// -------------------------------------------------------------------
void free_multipart_upload(multipart_upload_t* upload) {
    if (!upload) return;
    
    for (int i = 0; i < upload->file_count; i++) {
        if (upload->files[i].data) {
            free(upload->files[i].data);
            upload->files[i].data = NULL;
        }
    }
    upload->file_count = 0;
}
