#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sqlite3.h>
#include "faas_compiler.h"



// faas_compiler.c
// FaaS compiler library
// - Compiles functions from /tmp/progfile to WASM
// - Supports multiple languages (C, C++, Rust, Python, Go)
// - Inserts compiled function into SQLite database
// - Can be called as standalone program or as library function

#define SCAN_DIR "/tmp/progfile"
#define OUT_BASE "/opt/functions"
#define DB_DIR "/var/lib/faas_db"
#define BUF_SIZE 4096
#define BUFFER_SIZE 4096
#define PORT 8080

// No longer needed - UUID is passed from gateway

// simple file reader
char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long sz = ftell(f);
    rewind(f);
    char *buf = malloc(sz + 1);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, sz, f) != (size_t)sz) { free(buf); fclose(f); return NULL; }
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

// very small JSON extractor: finds "field" : "value" and returns a malloc'd string
char *extract_json_field(const char *json, const char *field) {
    if (!json || !field) return NULL;
    char *pos = strstr(json, field);
    if (!pos) return NULL;
    pos = strchr(pos, ':');
    if (!pos) return NULL;
    pos++;
    while (*pos && (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r')) pos++;
    if (*pos == '"') {
        pos++;
        const char *end = pos;
        while (*end && *end != '"') end++;
        size_t len = end - pos;
        char *out = malloc(len + 1);
        if (!out) return NULL;
        memcpy(out, pos, len);
        out[len] = '\0';
        return out;
    } else {
        // handle non-quoted value (not expected usually)
        const char *end = pos;
        while (*end && *end != ',' && *end != '}' && *end != '\n') end++;
        while (end > pos && (*(end-1) == ' ' || *(end-1) == '\t')) end--;
        size_t len = end - pos;
        char *out = malloc(len + 1);
        if (!out) return NULL;
        memcpy(out, pos, len);
        out[len] = '\0';
        return out;
    }
}

// find files for a specific UUID inside SCAN_DIR
int find_files_by_uuid(const char* uuid, char *json_path, size_t jn, char *code_path, size_t cn) {
    DIR *d = opendir(SCAN_DIR);
    if (!d) return -1;
    struct dirent *ent;
    json_path[0] = '\0';
    code_path[0] = '\0';
    
    // Build expected descriptor filename: uuid_descriptor.json
    char expected_desc[512];
    snprintf(expected_desc, sizeof(expected_desc), "%s_descriptor.json", uuid);
    
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_type == DT_DIR) continue;
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        
        // Check if this file belongs to our UUID
        if (strncmp(ent->d_name, uuid, strlen(uuid)) != 0) continue;
        
        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", SCAN_DIR, ent->d_name);
        
        // Is it the descriptor?
        if (strcmp(ent->d_name, expected_desc) == 0) {
            strncpy(json_path, full, jn-1);
            json_path[jn-1] = '\0';
        } 
        // Is it the code file? (starts with UUID but not the descriptor)
        else {
            strncpy(code_path, full, cn-1);
            code_path[cn-1] = '\0';
        }
        
        if (json_path[0] && code_path[0]) break;
    }
    closedir(d);
    if (json_path[0] == '\0' || code_path[0] == '\0') return -1;
    return 0;
}

// Legacy wrapper for backward compatibility
int find_files(char *json_path, size_t jn, char *code_path, size_t cn) {
    DIR *d = opendir(SCAN_DIR);
    if (!d) return -1;
    struct dirent *ent;
    json_path[0] = '\0';
    code_path[0] = '\0';
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_type == DT_DIR) continue;
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        char full[1024];
        snprintf(full, sizeof(full), "%s/%s", SCAN_DIR, ent->d_name);
        const char *ext = strrchr(ent->d_name, '.');
        if (ext && strcmp(ext, ".json") == 0) {
            if (json_path[0] == '\0') strncpy(json_path, full, jn-1);
        } else {
            if (code_path[0] == '\0') strncpy(code_path, full, cn-1);
        }
        if (json_path[0] && code_path[0]) break;
    }
    closedir(d);
    if (json_path[0] == '\0' || code_path[0] == '\0') return -1;
    return 0;
}

int ensure_dir_p(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) return 0;
    // try to create recursively
    char tmp[1024];
    char *p = NULL;
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (tmp[len-1] == '/') tmp[len-1] = '\0';
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (stat(tmp, &st) != 0) {
                if (mkdir(tmp, 0755) != 0) return -1;
            }
            *p = '/';
        }
    }
    if (stat(tmp, &st) != 0) {
        if (mkdir(tmp, 0755) != 0) return -1;
    }
    return 0;
}

int run_cmd(const char *cmd) {
    printf("[run] %s\n", cmd);
    int rv = system(cmd);
    if (rv == -1) return -1;
    return WEXITSTATUS(rv);
}



// delete all files in a directory (non-recursive)
int clear_dir(const char *path) {
    DIR *d = opendir(path);
    if (!d) return -1;
    struct dirent *ent;
    char full[1024];
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_type == DT_DIR) continue;
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        snprintf(full, sizeof(full), "%s/%s", path, ent->d_name);
        if (unlink(full) != 0) {
            perror("unlink");
        } else {
            printf("Deleted: %s\n", full);
        }
    }
    closedir(d);
    return 0;
}


// Main compilation function - can be called from gateway or standalone
int compile_function(const char* uuid) {
    if (!uuid) {
        fprintf(stderr, "Error: UUID is required\n");
        return 1;
    }

    char json_path[1024];
    char code_path[1024];
    if (find_files_by_uuid(uuid, json_path, sizeof(json_path), code_path, sizeof(code_path)) != 0) {
        fprintf(stderr, "Error: couldn't find both descriptor and code files for UUID '%s' in %s\n", uuid, SCAN_DIR);
        fprintf(stderr, "Expected: %s/%s_descriptor.json and %s/%s.[ext]\n", SCAN_DIR, uuid, SCAN_DIR, uuid);
        return 2;
    }

    printf("[COMPILER] Found JSON: %s\n", json_path);
    printf("[COMPILER] Found Code: %s\n", code_path);

    char *json = read_file(json_path);
    if (!json) { fprintf(stderr, "Error reading JSON\n"); return 3; }

    char *runtime = extract_json_field(json, "\"runtime\"");
    if (!runtime) {
        fprintf(stderr, "Error: JSON does not contain \"runtime\"\n");
        free(json);
        return 4;
    }

    printf("[COMPILER] runtime -> %s\n", runtime);
    printf("[COMPILER] UUID -> %s\n", uuid);

    char out_dir[1024];
    snprintf(out_dir, sizeof(out_dir), "%s/%s", OUT_BASE, uuid);
    if (ensure_dir_p(out_dir) != 0) {
        fprintf(stderr, "Error: can't create output dir %s (permissions?)\n", out_dir);
        free(json); free(runtime);
        return 5;
    }

    char out_module[2048];  // Larger buffer to avoid truncation warning
    snprintf(out_module, sizeof(out_module), "%s/module.wasm", out_dir);

    // choose compile command
    char cmd[4096];
    int supported = 1;
	if (strcmp(runtime, "c") == 0) {
    	snprintf(cmd, sizeof(cmd), "emcc -O2 %s -o %s --no-entry -s STANDALONE_WASM", code_path, out_module);
	} else if (strcmp(runtime, "cpp") == 0 || strcmp(runtime, "c++") == 0) {
    	snprintf(cmd, sizeof(cmd), "em++ -O2 %s -o %s --no-entry -s STANDALONE_WASM", code_path, out_module);
	} else if (strcmp(runtime, "rust") == 0) {
    	snprintf(cmd, sizeof(cmd), "rustc +stable --target=wasm32-wasi -O -o %s %s", out_module, code_path);
	} else if (strcmp(runtime, "tinygo") == 0 || strcmp(runtime, "go") == 0) {
    	snprintf(cmd, sizeof(cmd), "tinygo build -o %s -target wasi %s", out_module, code_path);
	} else if (strcmp(runtime, "python") == 0) {
    // use micropython-wasm (precompiled interpreter)
    	snprintf(cmd, sizeof(cmd), "py2wasm %s > %s.wasm", code_path, out_module);
	} else if (strcmp(runtime, "php") == 0) {
    // use php-wasm
    	snprintf(cmd, sizeof(cmd), "php-wasm-builder %s -o %s", code_path, out_module);
	} else if (strcmp(runtime, "wasm") == 0) {
    	snprintf(cmd, sizeof(cmd), "cp %s %s", code_path, out_module);
	} else {
    	supported = 0;
	}

    if (!supported) {
        fprintf(stderr, "Unsupported runtime '%s'.\n", runtime);
        fprintf(stderr, "Supported examples: c, cpp, c++, rust, tinygo, go, python, php, wasm\n");
        fprintf(stderr, "You can extend mappings in the source to add other toolchains.\n");
        free(json); free(runtime);
        return 6;
    }

    // run compile command
    int cret = run_cmd(cmd);
    if (cret != 0) {
        fprintf(stderr, "Compilation failed (exit code %d). Check toolchain and source.\n", cret);
        free(json); free(runtime);
        return 7;
    }
    
    // NOTE: Do NOT clean /tmp/progfile here - concurrent uploads would lose their files!
    // Each file uses a unique UUID so no conflicts
	
    // make DB dir and write entry
    if (ensure_dir_p(DB_DIR) != 0) {
        fprintf(stderr, "Warning: can't create DB dir %s. Will still print DB entry to stdout.\n", DB_DIR);
    }
 
    char db_path[1024];
    snprintf(db_path, sizeof(db_path), "%s/%s.json", DB_DIR, uuid);

    // produce DB JSON (keep original runtime for reference)
    char dbjson[2048];
    snprintf(dbjson, sizeof(dbjson), "{\"name\":\"%s\",\"runtime\":\"wasm\",\"module\":\"%s\",\"handler\":\"%s\",\"memory\":128,\"timeout\":5}", uuid, out_module, uuid);

    FILE *dbf = fopen(db_path, "w");
    if (dbf) {
        fprintf(dbf, "%s\n", dbjson);
        fclose(dbf);
        printf("Database entry written to %s\n", db_path);
    } else {
        printf("Could not write DB file to %s (permissions?).\n", db_path);
    }

    printf("Deployment successful. DB entry:\n%s\n", dbjson);
    
    // Also insert into SQLite database (if exists)
    sqlite3* sqldb = NULL;
    if (sqlite3_open("faas_meta.db", &sqldb) == SQLITE_OK) {
        // Extract method from JSON (default to POST if not found)
        char* method = extract_json_field(json, "\"method\"");
        if (!method) method = strdup("POST");
        
        // Generate route key: METHOD:/api/uuid
        char route_key[512];
        snprintf(route_key, sizeof(route_key), "%s:/api/%s", method, uuid);
        
        // Insert into functions table
        char sql[4096];
        snprintf(sql, sizeof(sql),
                 "INSERT OR REPLACE INTO functions (k, v, updated) VALUES ('%s', '%s', strftime('%%s','now'));",
                 route_key, dbjson);
        
        char* err_msg = NULL;
        if (sqlite3_exec(sqldb, sql, NULL, NULL, &err_msg) == SQLITE_OK) {
            printf("✓ Route inserted into SQLite: %s\n", route_key);
        } else {
            fprintf(stderr, "Warning: SQLite insert failed: %s\n", err_msg);
            sqlite3_free(err_msg);
        }
        
        free(method);
        sqlite3_close(sqldb);
    } else {
        printf("Info: faas_meta.db not found, skipping SQL insert (JSON still available)\n");
    }

    free(json);
    free(runtime);
    return 0;
}

// Standalone main (for command-line usage only, not when linked with gateway)
#ifdef FAAS_COMPILER_STANDALONE
int main(int argc, char **argv) {
    srand((unsigned int)time(NULL) ^ getpid());
    
    // Generate UUID if not provided
    char uuid[128];
    if (argc > 1) {
        strncpy(uuid, argv[1], sizeof(uuid) - 1);
        uuid[sizeof(uuid) - 1] = '\0';
    } else {
        // Generate UUID: timestamp + pid + random
        unsigned long t = (unsigned long)time(NULL);
        unsigned long pid = (unsigned long)getpid();
        unsigned long r = (unsigned long)rand();
        snprintf(uuid, sizeof(uuid), "%lu%lu%08lx", t, pid, r);
    }
    
    return compile_function(uuid);
}
#endif
