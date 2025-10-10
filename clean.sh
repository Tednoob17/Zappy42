#!/bin/bash

echo "[CLEAN] Cleaning build artifacts..."

# Remove build directory
rm -rf build/

# Remove database
rm -f faas_meta.db

# Remove sockets
rm -f /tmp/faas_worker_*.sock
rm -f /tmp/faas_lb_metrics.sock

echo "[CLEAN] ✓ All build artifacts and runtime files removed"

