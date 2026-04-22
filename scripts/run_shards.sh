#!/bin/bash
# Usage: ./scripts/run_shards.sh [--num-shards N] [--proc-us N] [--block-size N]
# Kills any existing receiver/shard workers, starts fresh.
# Leader output (block time + TPS) prints to this terminal.

NUM_SHARDS=4
BASE_PORT=5560
BUILD=./build
PROC_US=1000
BLOCK_SIZE=512
LOG_DIR=./logs

while [[ $# -gt 0 ]]; do
    case $1 in
        --num-shards)  NUM_SHARDS="$2";  shift 2 ;;
        --proc-us)     PROC_US="$2";     shift 2 ;;
        --block-size)  BLOCK_SIZE="$2";  shift 2 ;;
        *) echo "Unknown arg: $1"; shift ;;
    esac
done

echo "Cleaning up any existing processes..."

# Kill ALL receiver and shard_worker processes regardless of port/num-shards
# (handles leftover processes from previous runs with different --num-shards)
# To check manually: ps aux | grep -E "receiver|shard_worker" | grep -v grep
pkill -f "shard_worker" 2>/dev/null
pkill -f "receiver" 2>/dev/null

sleep 0.5

# Create log directory and clean old logs
mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR"/receiver.log "$LOG_DIR"/shard_*.log

# Kill all background processes and delete logs on Ctrl+C or normal exit
cleanup() {
    echo "Shutting down all processes..."
    pkill -f "shard_worker" 2>/dev/null
    pkill -f "receiver" 2>/dev/null
    rm -f "$LOG_DIR"/receiver.log "$LOG_DIR"/shard_*.log
    rmdir "$LOG_DIR" 2>/dev/null
}
trap cleanup EXIT

echo "Starting receiver (block-size=$BLOCK_SIZE, num-shards=$NUM_SHARDS)..."
$BUILD/receiver --num-shards $NUM_SHARDS --block-size $BLOCK_SIZE > "$LOG_DIR/receiver.log" 2>&1 &
sleep 0.5

echo "Starting $NUM_SHARDS shard workers (proc-us=$PROC_US)..."

# Start followers (shards 1..N-1) in background
for i in $(seq 1 $((NUM_SHARDS - 1))); do
    PORT=$((BASE_PORT + i))
    $BUILD/shard_worker $i $PORT --num-shards $NUM_SHARDS --proc-us $PROC_US \
        > "$LOG_DIR/shard_${i}.log" 2>&1 &
done

sleep 0.3

echo ""
echo "All processes started. Run the generator now:"
echo "  ./build/generator alice bob 10 <count> --threads $NUM_SHARDS --batch 64"
echo ""
echo "Receiver log: tail -f $LOG_DIR/receiver.log"
echo "Shard logs:   tail -f $LOG_DIR/shard_1.log  (and shard_2, shard_3, ...)"
echo ""

# Start leader in foreground — block output prints here
$BUILD/shard_worker 0 $BASE_PORT --num-shards $NUM_SHARDS --proc-us $PROC_US
