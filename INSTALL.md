# Installation Guide

## Prerequisites

This FaaS platform requires:
- **GCC** (C compiler)
- **SQLite3** (database + development libraries)
- **pthread** (usually included with GCC)

## Quick Install (Ubuntu/Debian/WSL)

```bash
# 1. Make install script executable
chmod +x install_deps.sh

# 2. Run installation (requires sudo)
./install_deps.sh

# 3. Fix line endings if needed (from Windows)
sed -i 's/\r$//' *.sh

# 4. Make all scripts executable
chmod +x start.sh stop.sh

# 5. Start the platform
./start.sh
```

## Manual Installation

If the automatic script doesn't work:

```bash
sudo apt-get update
sudo apt-get install -y build-essential sqlite3 libsqlite3-dev
```

## Troubleshooting

### Error: `/bin/bash^M: bad interpreter`
Your files have Windows line endings. Fix with:
```bash
sed -i 's/\r$//' *.sh
# or
dos2unix *.sh
```

### Error: `sqlite3: command not found`
Install SQLite3:
```bash
sudo apt-get install sqlite3 libsqlite3-dev
```

### Error: `gcc: command not found`
Install build tools:
```bash
sudo apt-get install build-essential
```

## Testing

Once started, you should see:
```
[CHECK] ✓ All prerequisites found
[BUILD] ✓ Compilation successful
[DB] ✓ Database initialized with 2 routes
[START] Launching workers...
[START] Launching load balancer...
```

## Stopping

Press `Ctrl+C` or in another terminal:
```bash
./stop.sh
```

