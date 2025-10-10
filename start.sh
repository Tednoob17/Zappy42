#!/bin/bash

set -e  # Exit on error

# ===================================================================
# Check prerequisites
# ===================================================================

echo "[CHECK] Verifying prerequisites..."

# Check for gcc
if ! command -v gcc &> /dev/null; then
    echo "ERROR: gcc is not installed"
    echo "Install with: sudo apt-get install build-essential"
    exit 1
fi

# Check for sqlite3
if ! command -v sqlite3 &> /dev/null; then
    echo "ERROR: sqlite3 is not installed"
    echo "Install with: sudo apt-get install sqlite3 libsqlite3-dev"
    exit 1
fi

# Check for libsqlite3-dev (check if sqlite3.h header exists)
if [ ! -f /usr/include/sqlite3.h ] && [ ! -f /usr/local/include/sqlite3.h ]; then
    echo "ERROR: libsqlite3-dev is not installed (needed for compilation)"
    echo "Install with: sudo apt-get install libsqlite3-dev"
    exit 1
fi

echo "[CHECK] ✓ All prerequisites found"
echo ""

# ===================================================================
# Prepare build directory
# ===================================================================

echo "[BUILD] Preparing build directory..."
mkdir -p build
rm -f build/*.o build/worker build/gateway

# ===================================================================
# Build
# ===================================================================

echo "[BUILD] Compiling workers with real metrics and smart scheduling..."
gcc -c -o build/worker.o src/worker.c -Isrc -lpthread
gcc -c -o build/metrics_reader.o src/metrics_reader.c -Isrc
gcc -c -o build/metrics_smoother.o src/metrics_smoother.c -Isrc
gcc -o build/worker build/worker.o build/metrics_reader.o build/metrics_smoother.o -lpthread

echo "[BUILD] Compiling FaaS compiler library..."
gcc -c -o build/faas_compiler.o src/faas_compiler.c -Isrc -I.

echo "[BUILD] Compiling unified gateway (HTTP + Load Balancer + Compiler)..."
gcc -c -o build/gateway.o src/gateway.c -Isrc -I. -lpthread
gcc -c -o build/http_handler.o src/http_handler.c -Isrc
gcc -c -o build/config_loader.o src/config_loader.c -Isrc
gcc -c -o build/kv.o src/kv.c -Isrc
gcc -c -o build/kv_sqlite_sync.o src/kv_sqlite_sync.c -Isrc
gcc -c -o build/metrics_collector.o src/metrics_collector.c -Isrc
gcc -o build/gateway build/gateway.o build/http_handler.o build/config_loader.o build/kv.o build/kv_sqlite_sync.o build/metrics_collector.o build/faas_compiler.o -lsqlite3 -lpthread

echo "[BUILD] ✓ Compilation successful"
echo "[BUILD] Binaries in: build/"
echo ""

# ===================================================================
# Initialize database
# ===================================================================

echo "[DB] Initializing database..."
if [ -f faas_meta.db ]; then
    echo "[DB] Warning: faas_meta.db already exists, recreating..."
    rm -f faas_meta.db
fi

sqlite3 faas_meta.db < init.sql

# Verify database was created correctly
ROUTE_COUNT=$(sqlite3 faas_meta.db "SELECT COUNT(*) FROM functions;")
echo "[DB] ✓ Database initialized with $ROUTE_COUNT routes"
echo ""

# ===================================================================
# Start services
# ===================================================================

echo "[START] Launching workers with metrics..."
./build/worker /tmp/faas_worker_0.sock 0 &
./build/worker /tmp/faas_worker_1.sock 1 &
./build/worker /tmp/faas_worker_2.sock 2 &
./build/worker /tmp/faas_worker_3.sock 3 &

sleep 1

echo "[START] Launching unified gateway on http://localhost:8080"
echo "Press Ctrl+C to stop"
echo ""
./build/gateway
