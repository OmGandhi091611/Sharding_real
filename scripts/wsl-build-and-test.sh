#!/usr/bin/env bash
# Build and quick-test parallel_tx_simulator in WSL (Ubuntu/Debian).
# It will check for and install (via apt-get) the required packages unless
# you pass --no-install:
#   - build-essential
#   - libssl-dev
#   - libprotobuf-c-dev
#   - libzmq3-dev
#
# Usage:
#   ./scripts/wsl-build-and-test.sh         # auto-install missing deps
#   ./scripts/wsl-build-and-test.sh --no-install  # skip apt-get entirely

set -euo pipefail
cd "$(dirname "$0")/.."

NEED_APT_INSTALL=0

need_pkg() {
  dpkg -s "$1" >/dev/null 2>&1 || NEED_APT_INSTALL=1
}

if [ "${1-}" != "--no-install" ]; then
  echo "== Checking and installing dependencies in WSL (may need sudo)..."
  need_pkg build-essential
  need_pkg libssl-dev
  need_pkg libprotobuf-c-dev
  need_pkg libzmq3-dev

  if [ "$NEED_APT_INSTALL" -eq 1 ]; then
    sudo apt-get update -qq
    sudo apt-get install -y build-essential libssl-dev libprotobuf-c-dev libzmq3-dev
  else
    echo "All required packages already installed."
  fi
  echo ""
else
  echo "== Skipping dependency install (per --no-install)."
fi

echo "== Building..."
make clean
make

echo ""
echo "== Quick test: receiver (background) + generator..."
./build/receiver --sleep-ms 5 &
RECV_PID=$!
sleep 1

set +e
./build/generator alice bob 1 64 --threads 2 --batch 32 --connect tcp://localhost:5557
GEN_EXIT=$?
set -e

kill "$RECV_PID" 2>/dev/null || true
wait "$RECV_PID" 2>/dev/null || true

if [ "$GEN_EXIT" -ne 0 ]; then
  echo ""
  echo "!! Generator exited with code $GEN_EXIT inside WSL. Please check the output above."
  exit "$GEN_EXIT"
fi

echo ""
echo "== Done. Code is working inside WSL."
