#!/bin/bash

echo "[STOP] Stopping all FaaS processes..."

# Kill workers and gateway
pkill -9 worker 2>/dev/null
pkill -9 gateway 2>/dev/null

# Remove Unix sockets
rm -f /tmp/faas_worker_*.sock
rm -f /tmp/faas_lb_metrics.sock

echo "[STOP] ✓ All processes stopped and cleaned up"
