# Makefile for FaaS Gateway Platform

CC = gcc
CFLAGS = -Wall -Wextra -pthread -O2 -Isrc
LDFLAGS = -lpthread
SQLITE_LDFLAGS = -lsqlite3

BUILD_DIR = build
SRC_DIR = src

# Source files
WORKER_SRCS = worker.c metrics_reader.c metrics_smoother.c fd_passing.c http_handler.c
GATEWAY_SRCS = gateway.c http_handler.c config_loader.c kv.c kv_sqlite_sync.c metrics_collector.c fd_passing.c
COMPILER_SRC = faas_compiler.c

# Object files
WORKER_OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(WORKER_SRCS))
GATEWAY_OBJS = $(patsubst %.c,$(BUILD_DIR)/%.o,$(GATEWAY_SRCS))

# Targets
WORKER_BIN = $(BUILD_DIR)/worker
GATEWAY_BIN = $(BUILD_DIR)/gateway

.PHONY: all clean init start stop help

all: $(BUILD_DIR) $(WORKER_BIN) $(GATEWAY_BIN)
	@echo ""
	@echo "✓ Build complete!"
	@echo "  Binaries: $(BUILD_DIR)/"

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# Worker binary
$(WORKER_BIN): $(WORKER_OBJS)
	@echo "[LINK] Building worker..."
	$(CC) -o $@ $^ $(LDFLAGS)

# Gateway binary (with integrated compiler)
$(GATEWAY_BIN): $(GATEWAY_OBJS) $(BUILD_DIR)/faas_compiler.o
	@echo "[LINK] Building gateway with compiler..."
	$(CC) -o $@ $^ $(LDFLAGS) $(SQLITE_LDFLAGS)

# Compiler object
$(BUILD_DIR)/faas_compiler.o: $(SRC_DIR)/$(COMPILER_SRC)
	@echo "[CC] $<"
	@$(CC) $(CFLAGS) -I. -c -o $@ $<

# Compile .c to .o
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@echo "[CC] $<"
	@$(CC) $(CFLAGS) -c -o $@ $<

# Initialize database
init:
	@echo "[INIT] Creating database..."
	@rm -f faas_meta.db
	@sqlite3 faas_meta.db < init.sql
	@echo "✓ Database initialized"

# Start services
start: all init
	@echo ""
	@echo "[START] Launching workers..."
	@$(WORKER_BIN) /tmp/faas_worker_0.sock 0 &
	@$(WORKER_BIN) /tmp/faas_worker_1.sock 1 &
	@$(WORKER_BIN) /tmp/faas_worker_2.sock 2 &
	@$(WORKER_BIN) /tmp/faas_worker_3.sock 3 &
	@sleep 1
	@echo "[START] Launching gateway on http://localhost:8080"
	@echo "Press Ctrl+C to stop"
	@echo ""
	@$(GATEWAY_BIN)

# Stop services
stop:
	@echo "[STOP] Stopping all processes..."
	@pkill -9 worker 2>/dev/null || true
	@pkill -9 gateway 2>/dev/null || true
	@rm -f /tmp/faas_worker_*.sock
	@rm -f /tmp/faas_lb_metrics.sock
	@echo "✓ Stopped"

# Clean build artifacts
clean:
	@echo "[CLEAN] Removing build artifacts..."
	@rm -rf $(BUILD_DIR)
	@rm -f faas_meta.db
	@rm -f /tmp/faas_worker_*.sock
	@rm -f /tmp/faas_lb_metrics.sock
	@echo "✓ Clean complete"

# Help
help:
	@echo "FaaS Gateway Build System"
	@echo ""
	@echo "Targets:"
	@echo "  make          - Build all binaries"
	@echo "  make init     - Initialize database"
	@echo "  make start    - Build, init DB, and start services"
	@echo "  make stop     - Stop all services"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make help     - Show this help"
