#!/bin/bash
# Monitor CPU usage of gateway process

if [ -z "$1" ]; then
    echo "Usage: $0 <output_file> [duration_seconds]"
    exit 1
fi

OUTPUT_FILE=$1
DURATION=${2:-60}

echo "Monitoring gateway CPU for ${DURATION} seconds..."

# Find gateway PID
GATEWAY_PID=$(pgrep -f "build/gateway" | head -1)

if [ -z "$GATEWAY_PID" ]; then
    echo "Error: Gateway process not found"
    exit 1
fi

echo "Gateway PID: $GATEWAY_PID"

# Header
echo "timestamp,pid,cpu_percent,user_cpu,sys_cpu,memory_mb,ctx_switches,voluntary_switches,nonvoluntary_switches" > "$OUTPUT_FILE"

# Monitor with pidstat (1 second intervals)
pidstat -u -r -w -p "$GATEWAY_PID" 1 "$DURATION" | \
    awk -v pid="$GATEWAY_PID" '
    /^[0-9]/ && $4 == pid {
        # Format: timestamp,pid,%cpu,%usr,%sys,mem_kb,cswch/s,nvcswch/s
        timestamp = strftime("%Y-%m-%d %H:%M:%S")
        printf "%s,%s,%.2f,%.2f,%.2f,%.2f,%s,%s,%s\n", 
               timestamp, $4, $8, $9, $10, $13/1024, $5, $6, $7
    }' >> "$OUTPUT_FILE"

echo "CPU monitoring saved to $OUTPUT_FILE"

# Calculate average CPU usage
AVG_CPU=$(awk -F, 'NR>1 {sum+=$3; count++} END {if(count>0) printf "%.2f", sum/count}' "$OUTPUT_FILE")
echo "Average Gateway CPU: ${AVG_CPU}%"

