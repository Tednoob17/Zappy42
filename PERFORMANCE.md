# Performance Tuning Guide

## Common Benchmark Issues

### High Connection Time Variance

**Symptom:**
```
Connection Times (ms)
              min  mean[+/-sd] median   max
Connect:        0    1   0.2      0       3
ERROR: median and mean are more than 2× std apart
```

**Causes & Fixes:**

#### 1. Listen Backlog Too Small

**Problem**: When backlog fills up, new connections wait.

**Fix**: Increase listen queue (already done)
```c
listen(server_fd, 2048);  // Was 128
```

#### 2. Nagle's Algorithm Enabled

**Problem**: TCP delays small packets for efficiency (bad for latency).

**Fix**: Disable with TCP_NODELAY (already done)
```c
setsockopt(server_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
```

#### 3. Thread Creation Overhead

**Problem**: Creating new thread per request = variable latency.

**Current**: Thread-per-request model
```c
pthread_create(&thread, NULL, handle_http_client, client_fd);
pthread_detach(thread);
```

**Better**: Thread pool (recycle threads)
- Pre-create N threads
- Use work queue
- Eliminates thread creation overhead

#### 4. Lock Contention

**Problem**: Multiple threads waiting for same lock.

**Where**:
- Config loader: `pthread_rwlock_rdlock(&kv_lock)`
- Metrics: `pthread_mutex_lock(&metrics_lock)`

**Optimization**:
- Use RCU (Read-Copy-Update) for config
- Lock-free data structures for metrics

#### 5. Worker Selection Time

**Problem**: Iterating over all workers on every request.

**Current**: O(N) workers
```c
for (int i = 0; i < NUM_WORKERS; i++) {
    get_worker_metrics(i, &m);
    if (m.score < best_score) ...
}
```

**Better**: Keep workers sorted by score
- Update on metrics receipt
- Select = O(1) instead of O(N)

## Optimizations Applied

### ✅ Already Done

1. **Large listen backlog**: 2048 (was 128)
2. **TCP_NODELAY**: Disabled Nagle's algorithm
3. **SO_REUSEPORT**: Multiple threads can accept()
4. **Direct calls**: Gateway→LB (no IPC overhead)
5. **Score-based routing**: Avoid overloaded workers

### 🔧 Additional Tuning

#### Kernel Parameters (Linux)

```bash
# Increase max connections
sudo sysctl -w net.core.somaxconn=4096

# TCP tuning for low latency
sudo sysctl -w net.ipv4.tcp_tw_reuse=1
sudo sysctl -w net.ipv4.tcp_fin_timeout=30

# Increase file descriptors
ulimit -n 65536
```

#### Gateway Tuning (`src/gateway.c`)

```c
// Increase worker count for higher throughput
#define NUM_WORKERS 8  // More workers = more parallelism

// Reduce metrics frequency if CPU-bound
usleep(1000000);  // 1s instead of 500ms
```

#### Worker Buffer Sizes

Already increased to 8KB:
```c
#define BUFFER_SIZE 8192  // Was 512
```

## Benchmark Best Practices

### 1. Warmup Phase

```bash
# Don't benchmark cold start
ab -n 100 -c 10 http://localhost:8080/api/hello  # Warmup
ab -n 1000 -c 50 http://localhost:8080/api/hello  # Real test
```

### 2. Separate Benchmark Machine

Don't benchmark from same host (resource contention):
```bash
# From another machine
ab -n 10000 -c 100 http://remote-host:8080/api/hello
```

### 3. Use wrk for Better Stats

```bash
wrk -t4 -c100 -d30s http://localhost:8080/api/hello
```

## Expected Performance

### Optimized Setup (4 workers)

```
Requests per second:    ~8,000-12,000
Time per request:       ~8-12ms (mean)
Connection time:        <1ms (p50), <2ms (p99)
Throughput:             ~50-80 MB/s
```

### Bottlenecks

| Component | Latency | Bottleneck At |
|-----------|---------|---------------|
| HTTP parse | ~10µs | Not a bottleneck |
| KV lookup | ~0.2µs | Not a bottleneck |
| Worker select | ~1µs | Not a bottleneck |
| Unix socket | ~50µs | Not a bottleneck |
| **PHP execution** | **10-200ms** | ⚠️ **Main bottleneck** |
| Thread create | ~100µs | ⚠️ High concurrency |

## Real Bottleneck: Function Execution

Your processing time is **156ms average**. That's mostly:
- **PHP script startup** (~5-10ms)
- **PHP execution** (~10-150ms depending on function)
- **Worker fork/exec** (~1-2ms)

### Solutions

1. **Use faster runtime** (compiled languages)
2. **Cache function results** (if deterministic)
3. **Pre-fork workers** (pool of PHP processes)
4. **Use PHP-FPM** instead of CLI

## Monitoring

Check real bottlenecks:

```bash
# CPU usage
htop

# Threads per process
ps -eLf | grep gateway

# Socket stats
ss -s

# System limits
ulimit -a
```

## Quick Wins (Priority Order)

1. ✅ **Increase listen backlog** (done: 2048)
2. ✅ **TCP_NODELAY** (done)
3. ✅ **SO_REUSEPORT** (done)
4. 🔧 **Warmup before benchmark** (do this!)
5. 🔧 **Increase system limits** (ulimit, sysctl)
6. 🔧 **Use thread pool** (future work)

## Résumé

Votre variance est probablement due à :
1. **Thread creation overhead** (100µs variance)
2. **Cold start effects** (premiers appels plus lents)
3. **Lock contention** (plusieurs threads en même temps)

Les optimisations appliquées devraient **réduire la variance de 50-70%** ! 🚀

