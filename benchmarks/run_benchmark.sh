#!/bin/bash
# Main benchmark script - compares PROXY vs ZERO-COPY architectures

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
RESULTS_DIR="$SCRIPT_DIR/results"

# Configuration
DURATION=60           # Duration of each test in seconds
CONNECTIONS=100       # Number of concurrent connections
RPS_TARGET=1000       # Target requests per second
TEST_URL="http://localhost:8080/api/hello"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}╔════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║     FaaS Gateway CPU Benchmark Suite              ║${NC}"
echo -e "${BLUE}║     Proxy vs Zero-Copy Architecture               ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════╝${NC}"
echo ""

# Check prerequisites
echo -e "${YELLOW}[1/6] Checking prerequisites...${NC}"
for cmd in pidstat wrk gcc make sqlite3; do
    if ! command -v $cmd &> /dev/null; then
        echo -e "${RED}Error: $cmd is not installed${NC}"
        exit 1
    fi
done
echo -e "${GREEN}✓ All prerequisites met${NC}"
echo ""

# Create results directory
mkdir -p "$RESULTS_DIR"

# Function to wait for service
wait_for_service() {
    local url=$1
    local max_wait=30
    local waited=0
    
    echo -n "Waiting for service to be ready..."
    while ! curl -s "$url" > /dev/null 2>&1; do
        sleep 1
        waited=$((waited + 1))
        if [ $waited -ge $max_wait ]; then
            echo -e "${RED}Service failed to start${NC}"
            return 1
        fi
    done
    echo -e "${GREEN} ready!${NC}"
    sleep 2  # Extra settling time
}

# Function to stop services
stop_services() {
    echo "Stopping services..."
    pkill -9 gateway 2>/dev/null || true
    pkill -9 worker 2>/dev/null || true
    sleep 2
}

# Function to run test
run_test() {
    local mode=$1
    local output_prefix=$2
    
    echo -e "${BLUE}═══════════════════════════════════════════════════${NC}"
    echo -e "${BLUE}Testing: $mode mode${NC}"
    echo -e "${BLUE}═══════════════════════════════════════════════════${NC}"
    
    # Start CPU monitoring in background
    echo "Starting CPU monitor..."
    bash "$SCRIPT_DIR/monitor_cpu.sh" "$RESULTS_DIR/${output_prefix}_cpu.csv" $DURATION &
    MONITOR_PID=$!
    
    sleep 3  # Let monitor stabilize
    
    # Run load test with wrk
    echo "Starting load test (${DURATION}s, ${CONNECTIONS} connections)..."
    wrk -t4 -c${CONNECTIONS} -d${DURATION}s --latency "$TEST_URL" \
        > "$RESULTS_DIR/${output_prefix}_wrk.txt" 2>&1
    
    # Wait for monitor to complete
    wait $MONITOR_PID 2>/dev/null || true
    
    echo -e "${GREEN}✓ Test completed${NC}"
    echo ""
}

# ============================================
# TEST 1: PROXY MODE (current implementation)
# ============================================

echo -e "${YELLOW}[2/6] Building PROXY mode...${NC}"
cd "$PROJECT_DIR"

# Create proxy version by temporarily modifying code
# (We'll keep the zero-copy version and just document the proxy results)
# For now, we need to create a branch or backup

echo -e "${YELLOW}Note: Testing current ZERO-COPY implementation first${NC}"
echo -e "${YELLOW}You'll need to manually test PROXY mode by reverting to pre-FD-passing code${NC}"
echo ""

# ============================================
# TEST 2: ZERO-COPY MODE
# ============================================

echo -e "${YELLOW}[3/6] Building ZERO-COPY mode...${NC}"
make clean > /dev/null 2>&1
make > /dev/null 2>&1
echo -e "${GREEN}✓ Build complete${NC}"
echo ""

echo -e "${YELLOW}[4/6] Starting ZERO-COPY services...${NC}"
stop_services
make init > /dev/null 2>&1

# Start workers
./build/worker /tmp/faas_worker_0.sock 0 > /dev/null 2>&1 &
./build/worker /tmp/faas_worker_1.sock 1 > /dev/null 2>&1 &
./build/worker /tmp/faas_worker_2.sock 2 > /dev/null 2>&1 &
./build/worker /tmp/faas_worker_3.sock 3 > /dev/null 2>&1 &
sleep 2

# Start gateway
./build/gateway > /dev/null 2>&1 &
GATEWAY_PID=$!

wait_for_service "http://localhost:8080/upload"

echo -e "${GREEN}✓ Services started (Gateway PID: $GATEWAY_PID)${NC}"
echo ""

# Deploy test function if needed
if [ -f "examples/hello.c" ] && [ -f "examples/descriptor.json" ]; then
    echo "Deploying test function..."
    curl -s -X POST http://localhost:8080/upload \
         -F "code=@examples/hello.c" \
         -F "descriptor=@examples/descriptor.json" > /dev/null 2>&1
    sleep 5  # Wait for compilation and sync
fi

echo -e "${YELLOW}[5/6] Running ZERO-COPY benchmark...${NC}"
run_test "ZERO-COPY" "zerocopy"

# ============================================
# CLEANUP
# ============================================

echo -e "${YELLOW}[6/6] Cleaning up...${NC}"
stop_services
echo -e "${GREEN}✓ Cleanup complete${NC}"
echo ""

# ============================================
# RESULTS SUMMARY
# ============================================

echo -e "${BLUE}╔════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║                BENCHMARK RESULTS                   ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════╝${NC}"
echo ""

# Parse results
if [ -f "$RESULTS_DIR/zerocopy_cpu.csv" ]; then
    echo -e "${GREEN}ZERO-COPY Results:${NC}"
    echo "─────────────────────────────────────────────────────"
    
    # CPU stats
    AVG_CPU=$(awk -F, 'NR>1 {sum+=$3; count++} END {printf "%.2f", sum/count}' "$RESULTS_DIR/zerocopy_cpu.csv")
    AVG_USER=$(awk -F, 'NR>1 {sum+=$4; count++} END {printf "%.2f", sum/count}' "$RESULTS_DIR/zerocopy_cpu.csv")
    AVG_SYS=$(awk -F, 'NR>1 {sum+=$5; count++} END {printf "%.2f", sum/count}' "$RESULTS_DIR/zerocopy_cpu.csv")
    
    echo "  Gateway CPU Usage:"
    echo "    Average Total: ${AVG_CPU}%"
    echo "    Average User:  ${AVG_USER}%"
    echo "    Average Sys:   ${AVG_SYS}%"
    echo ""
    
    # Latency stats from wrk
    if [ -f "$RESULTS_DIR/zerocopy_wrk.txt" ]; then
        echo "  Latency & Throughput:"
        grep -A4 "Latency" "$RESULTS_DIR/zerocopy_wrk.txt" | head -5
        grep "Requests/sec" "$RESULTS_DIR/zerocopy_wrk.txt"
    fi
    echo ""
fi

echo -e "${YELLOW}Results saved to: $RESULTS_DIR/${NC}"
echo ""
echo -e "${BLUE}To compare with PROXY mode:${NC}"
echo "1. Checkout the code before FD-passing changes"
echo "2. Run this script again"
echo "3. Compare results in $RESULTS_DIR/"
echo ""
echo -e "${GREEN}Benchmark complete!${NC}"

