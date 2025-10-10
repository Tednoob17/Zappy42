# FaaS Platform Architecture

## Overview

Unified gateway architecture combining HTTP server, load balancer, and dynamic configuration in a single process.

## Component Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                      GATEWAY PROCESS                             │
│                                                                  │
│  ┌───────────────────────────────────────────────────────────┐ │
│  │                    HTTP Server Layer                       │ │
│  │  - Listens on port 8080                                   │ │
│  │  - Multi-threaded (thread per request)                    │ │
│  │  - Parses HTTP requests                                   │ │
│  │  - Formats HTTP responses                                 │ │
│  └─────────────────────────┬─────────────────────────────────┘ │
│                            │ Direct function call              │
│                            ↓                                   │
│  ┌───────────────────────────────────────────────────────────┐ │
│  │                  Routing & Scheduling Layer                │ │
│  │                                                            │ │
│  │  ┌──────────────────────┐  ┌─────────────────────────┐   │ │
│  │  │  Config Loader       │  │  Smart Scheduler        │   │ │
│  │  │  - Query KV store    │  │  - Score-based         │   │ │
│  │  │  - Parse JSON config │  │  - Metrics-driven      │   │ │
│  │  │  - Find function     │  │  - Select best worker  │   │ │
│  │  └──────────────────────┘  └─────────────────────────┘   │ │
│  │             ↓                          ↓                  │ │
│  │  ┌──────────────────────────────────────────────────┐    │ │
│  │  │           In-Memory KV Store                     │    │ │
│  │  │  - Hash table (1024 buckets)                     │    │ │
│  │  │  - Thread-safe (rwlock)                          │    │ │
│  │  │  - Keys: "METHOD:URI" → JSON configs             │    │ │
│  │  └──────────────────────────────────────────────────┘    │ │
│  └───────────────────────────────────────────────────────────┘ │
│                                                                  │
│  ┌───────────────────────────────────────────────────────────┐ │
│  │                  Background Threads                        │ │
│  │                                                            │ │
│  │  [Thread 1] KV Sync                                       │ │
│  │    - Polls SQLite every 5s                                │ │
│  │    - Loads changed entries (timestamp-based)              │ │
│  │    - Updates KV store                                     │ │
│  │                                                            │ │
│  │  [Thread 2] Metrics Collector                             │ │
│  │    - Listens on DGRAM socket                              │ │
│  │    - Receives worker metrics                              │ │
│  │    - Updates worker state table                           │ │
│  └───────────────────────────────────────────────────────────┘ │
└──────────────────────────────┬───────────────────────────────────┘
                               │ Unix domain sockets (STREAM)
                    ┌──────────┼──────────┐
                    ↓          ↓          ↓
              ┌──────────┐┌──────────┐┌──────────┐
              │Worker #0 ││Worker #1 ││Worker #N │
              │          ││          ││          │
              │ Metrics  ││ Metrics  ││ Metrics  │
              │ Thread   ││ Thread   ││ Thread   │
              │ (500ms)  ││ (500ms)  ││ (500ms)  │
              └──────────┘└──────────┘└──────────┘
                    │          │          │
                    └──────────┴──────────┘
                         │ DGRAM socket
                         ↓
                  /tmp/faas_lb_metrics.sock
```

## Data Flow

### 1. HTTP Request → Response

```
1. Client sends HTTP
   ├─ POST /api/resize
   ├─ Headers: Content-Type, etc.
   └─ Body: {"image":"..."}

2. Gateway receives
   ├─ Thread created
   ├─ parse_http_request()
   └─ Extracts: method, uri, body

3. Route lookup (in-memory)
   ├─ find_function("POST", "/api/resize")
   ├─ KV lookup: "POST:/api/resize"
   └─ Returns: FunctionDescriptor

4. Smart scheduling
   ├─ select_worker()
   ├─ Compare worker scores
   └─ Choose worker with lowest load

5. Worker execution
   ├─ send_to_worker() via Unix socket
   ├─ Worker forks child process
   ├─ Executes WASM with wasmer
   └─ Returns result

6. HTTP response
   ├─ Format JSON response
   ├─ HTTP 200 OK
   └─ Send to client
```

### 2. Config Sync

```
1. Admin updates SQLite
   UPDATE functions SET v='...' WHERE k='POST:/resize'

2. KV Sync thread (every 5s)
   ├─ Query: SELECT * WHERE updated > last_sync
   ├─ Lock KV (write)
   ├─ Update changed entries
   └─ Unlock

3. Config Loader sees new config
   └─ Next request uses updated function
```

### 3. Metrics Flow

```
1. Worker measures
   ├─ Read /proc/self/stat (CPU)
   ├─ Read /proc/self/status (MEM)
   ├─ Read /proc/self/io (I/O)
   └─ Every 500ms

2. Worker calculates
   ├─ Normalize values (0-100)
   ├─ Apply EMA smoothing
   ├─ Calculate score: α×CPU + β×MEM + γ×IO
   └─ Build metrics struct

3. Worker sends
   ├─ sendto() via DGRAM socket
   └─ Non-blocking, fire-and-forget

4. Gateway receives
   ├─ Metrics collector thread
   ├─ recv() from socket
   └─ Update worker state table

5. Gateway uses
   └─ select_worker() reads metrics
       └─ Chooses worker with lowest score
```

## Technologies

- **Language**: C (C99)
- **HTTP**: Raw sockets (no external libs)
- **IPC**: Unix domain sockets (STREAM & DGRAM)
- **Database**: SQLite3
- **Threading**: POSIX threads (pthread)
- **Synchronization**: Read/Write locks, Mutexes
- **Metrics**: `/proc` filesystem

## Key Design Decisions

### 1. Unified Process (Gateway + LB)
**Why**: Eliminates IPC overhead between gateway and load balancer. Direct function calls are ~100x faster than Unix sockets.

### 2. Score-Based Scheduling
**Why**: Better than round-robin. Adapts to actual worker load in real-time.

### 3. In-Memory KV + SQLite Sync
**Why**: 
- Fast reads (O(1) hash table)
- Persistent config (SQLite)
- Best of both worlds

### 4. DGRAM for Metrics
**Why**: Non-blocking, no connection overhead, perfect for high-frequency updates.

### 5. EMA Smoothing
**Why**: Prevents overreaction to temporary spikes, stabilizes scheduling decisions.

## Performance Characteristics

### Latency Breakdown (per request)

```
HTTP parse:            ~10µs
Config lookup (KV):    ~0.2µs  (hash table)
Worker selection:      ~1µs    (compare N scores)
Unix socket to worker: ~50µs   (context switch + IPC)
Worker execution:      varies  (function dependent)
Response format:       ~5µs
                       ─────
Total overhead:        ~66µs (excluding function execution)
```

### Memory Usage

```
Gateway process:       ~10MB (base + KV + threads)
Worker process:        ~5MB each (base + metrics thread)
Total (1 gateway + 2 workers): ~20MB
```

### Scalability

- **Workers**: Linear scaling (tested up to 100)
- **Routes**: O(1) lookup (hash table)
- **Concurrent HTTP**: Limited by thread count (~1000s)
- **Metrics**: O(N) workers (lightweight)

## Extending

### Add Authentication

Edit `gateway.c`:
```c
void* handle_http_client(void* arg) {
    // ... parse request ...
    
    // Check auth header
    if (!check_auth(req.headers)) {
        send_http_401(client_fd);
        return NULL;
    }
    
    // ... continue ...
}
```

### Add Rate Limiting

Use token bucket per client IP.

### Add More Metrics

Edit `metrics.h` and add fields to `worker_metrics_t`.

## See Also

- `GATEWAY.md` - Detailed gateway documentation
- `INSTALL.md` - Installation instructions
- `SCHEDULING.md` - Smart scheduling algorithm (if exists)

