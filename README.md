# FaaS Platform - Function as a Service

A lightweight, high-performance Function-as-a-Service platform with:
- 🚀 **Smart load balancing** (CPU+MEM+IO score-based)
- ⚡ **Real-time config sync** (SQLite → RAM)
- 📊 **Live worker metrics** (500ms granularity)
- 🔧 **Zero-downtime updates** (modify routes without restart)

## Architecture

```
HTTP Client (curl, browser, etc.)
       ↓
   Port 8080
       ↓
┌──────────────────────────────────────┐
│         GATEWAY Process              │
│  ┌────────────────────────────────┐ │
│  │ HTTP Server (multi-threaded)   │ │
│  │ Config Loader (KV-based)       │ │
│  │ Smart Scheduler (score-based)  │ │
│  └────────────────────────────────┘ │
│  ┌────────────────────────────────┐ │
│  │ Background Threads:            │ │
│  │  - KV Sync (SQLite→RAM)        │ │
│  │  - Metrics Collector           │ │
│  └────────────────────────────────┘ │
└──────────────┬───────────────────────┘
               │ Unix sockets
       ┌───────┼───────┐
       ↓       ↓       ↓
   Worker 0  Worker 1  ...
```

## Quick Start

### Option 1: Using shell scripts (recommended)
```bash
# Fix line endings (if on WSL/Windows)
sed -i 's/\r$//' *.sh

# Make scripts executable
chmod +x *.sh

# Install dependencies (Ubuntu/Debian/WSL)
./install_deps.sh

# Build and start
./start.sh
```

### Option 2: Using Makefile
```bash
# Build all
make

# Initialize database and start
make start 2> /dev/null

# Stop
make stop

# Clean
make clean
```

## Testing

### Quick Test Script
```bash
chmod +x test.sh
./test.sh
```

### Manual Tests

#### PHP Functions (work immediately!)
```bash
# Test POST /api/hello (PHP)
curl -X POST http://localhost:8080/api/hello \
  -H "Content-Type: application/json" \
  -d '{"name":"Alice"}'

# Test GET /api/info (PHP)
curl http://localhost:8080/api/info
```

#### WASM Functions (require wasmer)
```bash
# Test POST /resize (WASM)
curl -X POST http://localhost:8080/resize \
  -H "Content-Type: application/json" \
  -d '{"image":"base64data...","width":800}'

# Test GET /ping (WASM)
curl http://localhost:8080/ping
```

### Add new function dynamically
```bash
# Add function config
sqlite3 faas_meta.db << EOF
INSERT INTO functions (k, v, updated) VALUES (
  'POST:/api/convert',
  '{"name":"imageConverter","runtime":"wasm","module":"/opt/functions/convert.wasm","handler":"convert","memory":512,"timeout":30}',
  strftime('%s','now')
);
EOF

# Wait <5 seconds, it's automatically synced!

# Test new route
curl -X POST http://localhost:8080/api/convert \
  -d '{"format":"png"}'
```

## Project Structure

```
.
├── build/                    # Build artifacts (auto-generated)
│   ├── *.o                  # Object files
│   ├── worker               # Worker binary
│   └── gateway              # Gateway binary
│
├── src/                     # Source code directory
│   ├── gateway.c            # Main: HTTP + Load Balancer
│   ├── worker.c             # Function executor
│   ├── http_handler.c/h     # HTTP parsing & responses
│   ├── config_loader.c/h    # Route lookup
│   ├── kv.c/h               # In-memory KV store
│   ├── kv_sqlite_sync.c     # SQLite → KV sync
│   ├── metrics_collector.c  # Collect worker metrics
│   ├── metrics_reader.c/h   # Read /proc metrics
│   ├── metrics_smoother.c/h # EMA smoothing
│   ├── metrics.h            # Metrics structures
│   └── scheduler_config.h   # Score weights (α,β,γ)
│
├── archive/                 # Old/obsolete files (git-ignored)
│   ├── load_balancer.c.old  # Old standalone LB
│   └── main.c.old           # Old KV test
│
├── Configuration
│   ├── init.sql             # Database schema & sample data
│   └── faas_meta.db         # SQLite database (runtime)
│
├── Build & Scripts
│   ├── Makefile             # Build system
│   ├── start.sh             # Build & start
│   ├── stop.sh              # Stop services
│   ├── clean.sh             # Clean artifacts
│   ├── reorganize.sh        # Move files to src/
│   └── install_deps.sh      # Install dependencies
│
└── Documentation
    ├── README.md            # This file
    ├── ARCHITECTURE.md      # Architecture details
    ├── GATEWAY.md           # Gateway docs
    ├── INSTALL.md           # Installation guide
    └── FILES.md             # File reference
```

## Configuration

### Scheduler Tuning (`src/scheduler_config.h`)

```c
// Score weights
#define ALPHA 0.5f   // CPU weight (50%)
#define BETA  0.3f   // Memory weight (30%)
#define GAMMA 0.2f   // I/O weight (20%)

// Smoothing factor
#define EMA_FACTOR 0.7f  // 70% old, 30% new
```

### Worker Count (`src/gateway.c`)

```c
#define NUM_WORKERS 4  // Default: 4 workers
```

To change the number of workers:
1. Edit `src/gateway.c` - change `NUM_WORKERS`
2. Add/remove socket paths in `WORKER_SOCKS[]`
3. Edit `start.sh` - add/remove worker launch commands

### HTTP Port (`src/gateway.c`)

```c
#define HTTP_PORT 8080  // Change if needed
```

## Monitoring

Watch live:
```bash
# Watch database
watch -n 1 'sqlite3 faas_meta.db "SELECT k,substr(v,1,60) FROM functions;"'

# Watch logs
# Gateway and workers output to stdout
```

## Key Features

### 1. Smart Load Balancing
Workers send metrics every 500ms:
```
Score = 0.5×CPU + 0.3×MEM + 0.2×IO
```
Gateway always selects worker with **lowest score**.

### 2. Zero-Downtime Updates
Modify routes in SQLite:
```sql
UPDATE functions 
SET v='{"module":"/new/path.wasm",...}', 
    updated=strftime('%s','now') 
WHERE k='POST:/resize';
```
Changes sync within 5 seconds automatically!

### 3. Multi-threaded HTTP
One thread per HTTP request = high concurrency.

### 4. Process Isolation
Each function executes in isolated process (fork/exec).

## Performance

| Metric | Value |
|--------|-------|
| HTTP Throughput | ~10,000 req/s |
| Latency (routing) | <1µs (direct call) |
| Latency (worker) | ~50µs (Unix socket) |
| Config sync | <5s (polling) |
| Metrics refresh | 500ms (push) |

## Requirements

- **OS**: Linux (WSL on Windows)
- **Compiler**: GCC with pthread support
- **Database**: SQLite3 + libsqlite3-dev
- **Runtimes**:
  - PHP CLI (for PHP functions) - Usually pre-installed
  - Wasmer (for WASM functions) - Optional

## Troubleshooting

### Port already in use
```bash
# Kill process using port 8080
sudo lsof -ti:8080 | xargs kill -9
```

### Workers not responding
```bash
./stop.sh
./start.sh
```

### Database issues
```bash
rm -f faas_meta.db
sqlite3 faas_meta.db < init.sql
```

### Line ending issues (Windows)
```bash
sed -i 's/\r$//' *.sh
```

## Development

### Build only
```bash
make
```

### Rebuild from scratch
```bash
make clean
make
```

### Debug mode
```bash
# Edit Makefile, change:
CFLAGS = -Wall -Wextra -pthread -O2 -g -DDEBUG
```

## License

Educational project for distributed systems learning.

