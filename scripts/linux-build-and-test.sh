#!/usr/bin/env bash
# Build and quick-test parallel_tx_simulator on a native Linux machine.
#
# This script will check for and, when possible, install required packages on
# Debian/Ubuntu-like systems:
#   - build-essential (or gcc + make)
#   - libssl-dev
#   - libprotobuf-c-dev
#   - libzmq3-dev
#
# On non-Debian systems it will just print a warning and continue.
#
# Usage:
#   ./scripts/linux-build-and-test.sh
#
# What it does:
#   1. Runs `make clean && make`
#   2. Starts the receiver in the background
#   3. Runs a small generator workload against it
#   4. Prints throughput and confirms the code is working

set -euo pipefail

cd "$(dirname "$0")/.."

echo "== Checking build tools and libraries =="

NEED_APT_INSTALL=0

need_pkg() {
  dpkg -s "$1" >/dev/null 2>&1 || NEED_APT_INSTALL=1
}

if command -v dpkg >/dev/null 2>&1 && command -v apt-get >/dev/null 2>&1; then
  # Check for required Debian/Ubuntu packages
  need_pkg build-essential
  need_pkg libssl-dev
  need_pkg libprotobuf-c-dev
  need_pkg libzmq3-dev

  if [ "$NEED_APT_INSTALL" -eq 1 ]; then
    echo "Some required packages are missing. Installing with sudo apt-get..."
    sudo apt-get update
    sudo apt-get install -y build-essential libssl-dev libprotobuf-c-dev libzmq3-dev
  else
    echo "All required Debian/Ubuntu packages are already installed."
  fi
else
  echo "Warning: This does not appear to be a Debian/Ubuntu system (no dpkg/apt-get)."
  echo "Please make sure the following are installed manually before proceeding:"
  echo "  - gcc, make (build tools)"
  echo "  - OpenSSL development headers"
  echo "  - protobuf-c development headers"
  echo "  - ZeroMQ development headers"
fi

echo ""
echo "== Building (make clean && make)..."
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
  echo "!! Generator exited with code $GEN_EXIT. Please check the output above."
  exit "$GEN_EXIT"
fi

echo ""
echo "== Done. Code is working on this Linux environment."

