#!/bin/bash
# Quick CPU benchmark using perf stat (requires root or perf_event_paranoid setting)

GATEWAY_URL="http://localhost:8080/api/hello"
DURATION=30
REQUESTS=10000

echo "🚀 Quick Gateway CPU Benchmark"
echo "================================"
echo ""

# Find gateway PID
GATEWAY_PID=$(pgrep -f "build/gateway" | head -1)

if [ -z "$GATEWAY_PID" ]; then
    echo "❌ Error: Gateway not running"
    echo "   Start it with: make start"
    exit 1
fi

echo "✓ Gateway PID: $GATEWAY_PID"
echo ""

# Check if perf is available
if command -v perf &> /dev/null; then
    echo "📊 Running perf stat for ${DURATION} seconds..."
    echo "   This will measure CPU cycles, instructions, context switches, etc."
    echo ""
    
    # Run load in background
    (
        sleep 2  # Give perf time to start
        echo "   Generating load with wrk..."
        wrk -t4 -c50 -d${DURATION}s "$GATEWAY_URL" > /dev/null 2>&1
    ) &
    
    # Monitor with perf
    sudo perf stat -p $GATEWAY_PID -e cycles,instructions,cache-references,cache-misses,context-switches,cpu-migrations,page-faults -- sleep $DURATION
    
    wait
    
    echo ""
    echo "✅ Benchmark complete!"
    echo ""
    echo "Key metrics to compare:"
    echo "  • CPU cycles: Lower is better (less CPU work)"
    echo "  • Context switches: Lower is better (less overhead)"
    echo "  • Cache misses: Lower is better (better memory efficiency)"
    
else
    echo "⚠️  perf not available, using pidstat instead..."
    echo ""
    
    # Run load in background
    (
        echo "Generating load..."
        wrk -t4 -c50 -d${DURATION}s "$GATEWAY_URL" > /tmp/wrk_quick.txt 2>&1
    ) &
    LOAD_PID=$!
    
    # Monitor CPU
    echo "Monitoring gateway CPU..."
    pidstat -u -p $GATEWAY_PID 1 $DURATION | tee /tmp/pidstat_quick.txt
    
    wait $LOAD_PID
    
    # Summary
    echo ""
    echo "================================"
    echo "RESULTS:"
    echo "================================"
    
    AVG_CPU=$(awk '/%CPU/ {next} /Average/ {print $8}' /tmp/pidstat_quick.txt | tail -1)
    echo "Average Gateway CPU: ${AVG_CPU}%"
    
    grep "Requests/sec" /tmp/wrk_quick.txt
    grep "Latency" /tmp/wrk_quick.txt | head -1
    
    echo ""
fi

echo ""
echo "💡 For detailed comparison, run: ./run_benchmark.sh"

