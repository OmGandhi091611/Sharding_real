# Blockchain Sharding — Real Implementation (C + ZMQ + OpenMP)

A fully functional blockchain sharding system written in C. Transactions are generated in parallel (OpenMP), submitted over ZMQ to a receiver that manages the mempool and runs leader election, and then dispatched to independent shard worker processes that verify, hash, and finalize blocks concurrently. The leader shard assembles a combined merkle root and records TPS metrics to `results.csv`.

---

## Architecture overview

```
┌─────────────────────────────────────────────────────┐
│                   Generator (main.c)                │
│   OpenMP threads → sign & batch transactions        │
│   ZMQ REQ → tcp://localhost:5557                    │
└───────────────────────┬─────────────────────────────┘
                        │ protobuf TransactionBatch
                        ▼
┌─────────────────────────────────────────────────────┐
│                  Receiver (receiver.c)              │
│   ZMQ REP socket — accepts batches, fills mempool  │
│   When mempool ≥ block_size → run election          │
│   ShardAssigner → dispatches txs to shard workers  │
└──────┬──────────────────────────────────────────────┘
       │ ZMQ PUSH (TxWithPubkey + RoundEnd per shard)
       ▼
┌─────────────────────────────────────────────────────┐
│            Shard Workers (shard_worker.c)           │
│                                                     │
│  Shard 0 (Leader)          Shards 1..N (Followers) │
│  ├─ PULL from assigner     ├─ PULL from assigner   │
│  ├─ PUB ROUND_START ──────►├─ SUB ROUND_START      │
│  ├─ process own txs        ├─ process own txs       │
│  ├─ PULL ShardSummaries ◄──┤─ PUSH ShardSummary    │
│  └─ assemble block + TPS   └─                      │
└─────────────────────────────────────────────────────┘
```

Each shard worker independently verifies transactions and computes a merkle root (BLAKE3/SHA256). The leader collects all shard summaries, builds a combined block, and writes the result to `results.csv`.

---

## Components

### Binaries (built by `make`)

| Binary | Source | Role |
|--------|--------|------|
| `build/generator` | `main.c` | Creates and sends signed transactions in parallel using OpenMP. Each thread manages its own wallet and ZMQ REQ socket. Sends protobuf-encoded `TransactionBatch` messages to the receiver. |
| `build/receiver` | `src/receiver.c` | Binds a ZMQ REP socket, unpacks transaction batches into the mempool. When the mempool reaches `block_size`, runs leader election and dispatches transactions to shard workers via `ShardAssigner`. |
| `build/shard_worker` | `src/shard_worker.c` | One process per shard. Shard 0 is the **leader**: broadcasts `ROUND_START` to followers via PUB/SUB, collects `ShardSummary` from each follower, assembles the final block, and records metrics. Shards 1..N are **followers**: wait for `ROUND_START`, drain their tx buffer, verify and hash in parallel (OpenMP), then push a `ShardSummary` to the leader. |

### Source modules (`src/` + `include/`)

| Module | Files | Role |
|--------|-------|------|
| **Transaction** | `transaction.c / .h` | 112-byte transaction struct (nonce, expiry, source/dest address, value, fee, 64-byte signature). Create, sign, verify (Ed25519), BLAKE3 hash, protobuf serialization. |
| **Wallet** | `wallet.c / .h` | Key pair storage, 20-byte address derivation (RIPEMD160∘SHA256), nonce tracking, signing. `wallet_create_named(name)` used by the generator for per-thread senders. |
| **Mempool** | `mempool.c / .h` | Fixed-capacity transaction queue. Receiver pushes transactions in; `ShardAssigner` drains them when a block is ready. |
| **ShardAssigner** | `shard_assigner.c / shard.h` | Partitions mempool transactions across shards by address prefix and dispatches them over ZMQ PUSH sockets to the appropriate shard worker ports (`5560..5560+N`). Sends a `DispatchHeader` then individual `TxWithPubkey` structs followed by a `RoundEnd` sentinel. |
| **Election** | `election.c / election.h` | Stake-weighted leader election. Selects one winner node per shard before each dispatch round. |
| **Node** | `node.c / node.h` | Represents a validator node (id, stake, address, role, state). Used by the election module. |
| **Common** | `common.c / common.h` | SHA256, RIPEMD160, BLAKE3 wrappers, hex conversion, `safe_malloc`, `get_current_time_ms`, logging. |
| **BLAKE3** | `blake3.c / blake3.h` | BLAKE3 hash implementation used for transaction hashing. |

### Wire protocol (`proto/`)

| File | Role |
|------|------|
| `blockchain.proto` | Defines `Transaction` and `TransactionBatch` protobuf messages used between generator and receiver. |
| `blockchain.pb-c.c/h` | Auto-generated protobuf-c pack/unpack code. Regenerate with `protoc --c_out=./proto blockchain.proto` if the `.proto` changes. |

### Scripts

| Script | Role |
|--------|------|
| `scripts/run_shards.sh` | Orchestration script. Kills any stale processes, starts the receiver and all follower shard workers in background, then starts the leader (shard 0) in the foreground. Logs go to `./logs/`. |
| `scripts/linux-build-and-test.sh` | Full build + smoke test on Linux. |
| `scripts/wsl-build-and-test.sh` | Same for Windows WSL. |

### Data and analysis

| File | Role |
|------|------|
| `results.csv` | Written by shard 0 (leader) on exit. Columns: `num_transactions, block_size, block_time_ms, tps, num_shards`. Each row is one experiment run averaged over all blocks. |
| `simulation_results.csv` | Results from the accompanying blockchain simulator (separate project). Columns differ: uses `shards`, `block size`, `tps`, and includes mining time in the block time. |
| `plot_results.py` | Reads both CSVs and produces `tps_vs_shards_comparison.png` — a 3×3 grid comparing Real vs Simulation TPS for each block size. |

---

## Build

**Requirements:** `gcc` with OpenMP, OpenSSL, protobuf-c, libzmq.

**Install dependencies (Debian/Ubuntu / WSL):**

```bash
sudo apt-get update
sudo apt-get install -y build-essential libssl-dev libprotobuf-c-dev libzmq3-dev
```

**Build all three binaries:**

```bash
make
```

Produces `build/generator`, `build/receiver`, `build/shard_worker`.

```bash
make clean   # remove build/
```

---

## Run

### Quickstart — 4 shards

**Terminal 1:** start shards (receiver + shard workers)

```bash
./scripts/run_shards.sh --num-shards 4 --block-size 1024
```

**Terminal 2:** run the generator

```bash
./build/generator alice bob 10 1048576 --threads 4 --batch 64
```

The leader (shard 0) prints a block summary after each block and writes a CSV row to `results.csv` on exit (Ctrl+C).

---

### Manual setup

If you want to control each process individually:

**Step 1 — Start the receiver:**

```bash
./build/receiver --num-shards 4 --block-size 1024
```

**Step 2 — Start follower shard workers** (one terminal each, or background):

```bash
./build/shard_worker 1 5561 --num-shards 4
./build/shard_worker 2 5562 --num-shards 4
./build/shard_worker 3 5563 --num-shards 4
```

**Step 3 — Start the leader shard worker** (shard 0, foreground):

```bash
./build/shard_worker 0 5560 --num-shards 4 --block-size 1024
```

**Step 4 — Run the generator:**

```bash
./build/generator alice bob 10 1048576 --threads 4 --batch 64
```

Press Ctrl+C on shard 0 to flush the CSV summary.

---

### Options reference

**Generator (`build/generator <from> <to> <amount> <count> [options]`)**

| Option | Default | Description |
|--------|---------|-------------|
| `--connect ADDR` | `tcp://localhost:5557` | Receiver ZMQ endpoint |
| `--in-process` | off | Skip ZMQ; use in-process callback (no receiver needed) |
| `--threads N` | 48 | OpenMP threads (one ZMQ socket per thread) |
| `--batch N` | 64 | Transactions per protobuf batch (max 256) |
| `--sleep-ms N` | 0 | In-process only: artificial delay per batch |
| `--base-nonce N` | 0 | Starting nonce for the sender wallet |

**Receiver (`build/receiver [options]`)**

| Option | Default | Description |
|--------|---------|-------------|
| `--bind ADDR` | `tcp://*:5557` | ZMQ bind address |
| `--num-shards N` | 48 | Number of shards (must match shard workers) |
| `--block-size N` | 256 | Transactions per block before dispatch |
| `--sleep-ms N` | 0 | Artificial verification delay per batch |
| `--verify` | off | Run `transaction_verify()` on each TX |

**Shard worker (`build/shard_worker <shard_id> <port> [options]`)**

| Option | Default | Description |
|--------|---------|-------------|
| `--num-shards N` | 4 | Total number of shards (must match across all workers and receiver) |
| `--par-threads N` | 4 | OpenMP threads for parallel tx verification inside each shard |
| `--block-size N` | 0 | Used by leader for CSV logging |

**run_shards.sh**

| Flag | Default | Description |
|------|---------|-------------|
| `--num-shards N` | 4 | Number of shards to launch |
| `--block-size N` | 512 | Block size passed to receiver and leader |
| `--par-threads N` | 4 | Parallel threads per shard worker |
| `--verify` | off | Enable transaction signature verification |

---

## Port layout

| Port range | Usage |
|------------|-------|
| `5557` | Receiver ZMQ REP (generator → receiver) |
| `5560 .. 5560+N-1` | Shard worker TX PULL ports (assigner → each shard) |
| `5560+N` | Leader PUB socket (broadcasts `ROUND_START` to followers) |
| `5560+N+1` | Leader PULL socket (collects `ShardSummary` from followers) |
| `5560+N+2` | Leader PULL socket (receives `DispatchDone` from assigner) |

---

## Experiment workload

The benchmark sweeps two independent variables — **block size** and **number of shards** — holding everything else constant, and records average block time and TPS for each combination.

### Parameter space

| Dimension | Values |
|-----------|--------|
| Block size (transactions per block) | 1 024, 2 048, 4 096, 8 192, 16 384, 32 768, 65 536, 131 072, 262 144 |
| Block size (bytes, at 112 B/tx) | 112 KB, 224 KB, 448 KB, 896 KB, 1.75 MB, 3.5 MB, 7 MB, 14 MB, 28 MB |
| Number of shards | 1, 2, 4, 8, 12, 16, 24 |
| Total configurations | 9 × 7 = **63 runs** |

Block sizes double at each step, covering regimes where coordination overhead dominates (small blocks) through regimes where transaction processing dominates (large blocks). The shard counts include the baseline single-shard (no sharding) case and scale up to 24 shards to stress-test coordination cost.

### Real implementation workload (`results.csv`)

Each run submits **1 048 576 transactions (2²⁰ ≈ 1 M)** — the total is rounded down slightly per run so it divides evenly into `num_shards × block_size` blocks. Each transaction is a fixed **112-byte signed struct**:

| Field | Size |
|-------|------|
| Nonce | 8 B |
| Expiry block | 4 B |
| Source address (RIPEMD160∘SHA256) | 20 B |
| Destination address | 20 B |
| Value | 8 B |
| Fee | 4 B |
| Signature (Ed25519-style) | 48 B |
| **Total** | **112 B** |

Generator configuration per run:

- **Threads:** 48 OpenMP threads, each with its own wallet and ZMQ REQ socket
- **Batch size:** 64 transactions per protobuf message
- **Measurement:** wall-clock time from dispatch start to all shard summaries received — **processing overhead only, no PoW mining time**

TPS is computed per block as `total_tx_in_block / block_time_ms × 1000` and averaged across all blocks in the run.

### Simulation workload (`simulation_results.csv`)

The simulation runs the same 63 block size / shard count configurations but models a full network:

| Parameter | Value |
|-----------|-------|
| Nodes | 1 000 |
| Miners | 1 000 |
| Wallets | Matched to block size (1 024 – 10 000) |
| Transactions per block | 1 000 – 26 215 (scales with block size) |
| Blocks generated per run | 500 |
| Target block time (PoW) | 0.2 s |
| Mode | `sharded` (N > 1) or `conventional` (N = 1) |

Because the simulation includes PoW mining time (~0.2 s baseline) in the measured block time, its TPS is significantly lower than the real implementation at small block sizes where mining dominates. At large block sizes the processing time exceeds the mining baseline and the two converge.

---

## Result analysis

After experiments, generate the comparison plot:

```bash
# activate your Python environment first if needed
source environment/bin/activate

python3 plot_results.py
```

Outputs `tps_vs_shards_comparison.png` — a 3×3 grid with one panel per block size, each showing Real vs Simulation TPS as a function of number of shards.

> **Note on the Real vs Simulation gap:** `results.csv` measures only transaction processing overhead (no PoW mining). `simulation_results.csv` includes a ~0.2s PoW mining target in every block time, which dominates at small block sizes and accounts for the large TPS difference there. At large block sizes, processing time grows and the two converge.

---

## In-process test (no ZMQ, no receiver)

```bash
make test
# runs: ./build/generator alice bob 10 100 --in-process --threads 4 --batch 32
```

---

## WSL (Windows)

All steps above work inside WSL2 without modification. From PowerShell you can trigger the build script directly:

```powershell
wsl bash -c "cd /mnt/c/path/to/Sharding_Real && ./scripts/wsl-build-and-test.sh --no-install"
```

Use `--no-install` if dependencies are already installed in WSL.
