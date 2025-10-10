#!/bin/bash

# Install dependencies for the FaaS platform on Ubuntu/Debian/WSL

echo "=== Installing FaaS Platform Dependencies ==="
echo ""

# Update package list
echo "[1/3] Updating package list..."
sudo apt-get update

# Install build tools
echo ""
echo "[2/3] Installing build tools (gcc, make, etc.)..."
sudo apt-get install -y build-essential

# Install SQLite3
echo ""
echo "[3/3] Installing SQLite3..."
sudo apt-get install -y sqlite3 libsqlite3-dev

echo ""
echo "=== Installation Complete ==="
echo ""
echo "Installed packages:"
gcc --version | head -n 1
sqlite3 --version

echo ""
echo "You can now run: ./start.sh"

