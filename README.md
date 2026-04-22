# Parallel TX Simulator (OpenMP + ZMQ)

A **C** load generator and receiver for parallel transaction submission over **ZMQ**. The generator creates signed transactions in parallel (OpenMP) and sends them in protobuf batches to a receiver process. The receiver acts as a validator/simulator stub: it accepts batches, optionally verifies each transaction, and replies. The design is ready to be extended into NEAR-style validator nodes.

---

## Table of contents

- [What is happening (high level)](#what-is-happening-high-level)
- [Code layout and what each part does](#code-layout-and-what-each-part-does)
- [Build (Linux)](#build-linux)
- [Run (Linux)](#run-linux)
- [Options reference](#options-reference)
- [Tests](#tests)
- [Glossary: Running on Windows via WSL](#glossary-running-on-windows-via-wsl)

---

## What is happening (high level)

1. **Two processes (ZMQ mode)**  
   - **Receiver** starts first and binds a ZMQ REP socket (default `tcp://*:5557`). It waits for messages.  
   - **Generator** connects to that address and sends **batches of transactions**. Each message is a fixed 16-byte prefix `SUBMIT_BATCH_PB:` followed by a protobuf-encoded `TransactionBatch`.

2. **Inside the generator**  
   - You pass: sender name, recipient name, amount, and total transaction count (plus optional flags).  
   - OpenMP runs multiple threads (default 48). Each thread has its own **wallet** (same logical sender, per-thread for parallelism).  
   - Transactions are created in **batches** (e.g. 64 TXs per batch). For each batch, threads build **Transaction** structs (112 bytes: nonce, expiry, source/dest addresses, value, fee, 48-byte BLS-style signature), then serialize them into a single **TransactionBatch** protobuf and send it over ZMQ.  
   - The generator waits for a short reply (e.g. `OK`) per batch and then continues. At the end it prints total submitted count and throughput (tx/sec).

3. **Inside the receiver**  
   - Receives the prefixed message, strips the prefix, and unpacks the protobuf `TransactionBatch`.  
   - Optionally sleeps N ms per batch (to simulate verification delay) and/or runs **transaction_verify()** on each transaction.  
   - Sends back a simple reply (e.g. `OK`).  
   - This process can later be replaced or extended with real validator logic (mempool, block building, etc.); the wire protocol stays the same.

4. **In-process mode**  
   - If you pass `--in-process` to the generator, it does **not** use ZMQ. Batches are “submitted” via an in-process callback (and optional `--sleep-ms`). No receiver process is needed. Useful for quick single-process tests.

5. **Transaction format**  
   - Fixed 112-byte layout: nonce (8), expiry_block (4), source_address (20), dest_address (20), value (8), fee (4), signature (48).  
   - Addresses are 20-byte binary (e.g. RIPEMD160(SHA256(pubkey))).  
   - Signing uses the wallet’s private key; signatures are suitable for NEAR-style verification.  
   - The **proto** files are used only for serialization (Transaction + TransactionBatch over the wire); there is no blockchain or block structure in this repo.

---

## Code layout and what each part does

| Path | Role |
|------|------|
| **main.c** | **Generator** entry point. Parses CLI (`<from> <to> <amount> <count>` and options), creates per-thread wallets, spawns OpenMP threads, builds batches of transactions, and either sends them over ZMQ (REQ socket per thread) or calls an in-process callback. Measures time and prints throughput. |
| **src/receiver.c** | **Receiver** entry point. Binds a ZMQ REP socket, receives messages starting with `SUBMIT_BATCH_PB:`, unpacks protobuf batches, optionally sleeps and/or verifies each TX, replies `OK`. |
| **src/transaction.c** + **include/transaction.h** | **Transaction** type (112 bytes) and helpers: create/sign (with wallet), create_from_address (no wallet), verify, compute hash (BLAKE3), serialize/deserialize to protobuf or hex. |
| **src/wallet.c** + **include/wallet.h** | **Wallet**: key storage, address derivation (RIPEMD160(SHA256(pubkey))), nonce handling, signing (48-byte BLS-style). `wallet_create_named(name)` used by generator for per-thread senders. |
| **src/common.c** + **include/common.h** | **Common** utilities: SHA256, RIPEMD160, hash160, BLAKE3 wrappers, hex conversion, safe_malloc, logging, etc. |
| **src/blake3.c** + **include/blake3.h** | **BLAKE3** implementation used for transaction hashes (and any other BLAKE3 usage). |
| **proto/blockchain.proto** | **Protobuf** definitions: `Transaction` and `TransactionBatch` only (no blocks). Used for on-wire encoding. |
| **proto/blockchain.pb-c.c/h** | Generated **protobuf-c** code (pack/unpack). Regenerate with `protoc --c_out=./proto` if you change the `.proto` file. |
| **Makefile** | Builds `build/generator` (main.c + transaction + wallet + common + blake3 + proto) and `build/receiver` (receiver.c + transaction + common + blake3 + proto). |
| **build/** | Output directory for object files and binaries: `generator`, `receiver`. |

**Why “blockchain” in proto?**  
The repo only uses transactions and batches; the name comes from the original project. You can ignore it—treat `proto/` as “serialization for Transaction and TransactionBatch”.

---

## Build (Linux)

**Requirements:** `gcc` with OpenMP, OpenSSL, protobuf-c, and ZMQ (libzmq).

**Install dependencies (Debian/Ubuntu):**

```bash
sudo apt-get update
sudo apt-get install -y build-essential libssl-dev libprotobuf-c-dev libzmq3-dev
```

**Build:**

```bash
cd /path/to/shardingsim
make
```

This produces:

- `build/generator` — load generator
- `build/receiver` — validator/simulator stub

**Clean:**

```bash
make clean
```

---

## Run (Linux)

The primary way to run is **ZMQ mode**: two processes, receiver first, then generator.

**1. Start the receiver** (in a terminal):

```bash
./build/receiver
```

Or with options:

```bash
./build/receiver --bind tcp://*:5557 --sleep-ms 2 --verify
```

**2. Run the generator** (in another terminal, same machine or another host):

```bash
./build/generator <from> <to> <amount> <count> [options]
```

Examples:

```bash
# Default: 48 threads, batch 64, connect to tcp://localhost:5557
./build/generator alice bob 1 1000

# Fewer threads, smaller batch
./build/generator alice bob 10 200 --threads 4 --batch 32

# Custom receiver endpoint
./build/generator sender receiver 10 5000 --connect tcp://192.168.1.10:5557 --threads 24
```

**In-process mode (no receiver, single process):**

```bash
./build/generator alice bob 10 100 --in-process --threads 4 --batch 32
```

Optional: `--sleep-ms N` in-process adds N ms delay per batch in the callback.

---

## Options reference

**Generator**

| Option | Default | Description |
|--------|---------|-------------|
| `--connect ADDR` | `tcp://localhost:5557` | ZMQ endpoint of the receiver |
| `--in-process` | off | No ZMQ; use in-process callback only |
| `--threads N` | 48 | Number of OpenMP threads |
| `--batch N` | 64 | Transactions per batch |
| `--sleep-ms N` | 0 | In-process only: sleep N ms per batch |
| `--base-nonce N` | 0 | Starting nonce for the sender |

**Receiver**

| Option | Default | Description |
|--------|---------|-------------|
| `--bind ADDR` | `tcp://*:5557` | Bind address |
| `--sleep-ms N` | 0 | Simulate work: sleep N ms per batch |
| `--verify` | off | Run `transaction_verify()` on each TX |

---

## Tests

**In-process test (no receiver):**

```bash
make test
```

This runs:  
`./build/generator alice bob 10 100 --in-process --threads 4 --batch 32`  
and then prints “Done.”

**ZMQ test (receiver + generator):**

1. Terminal 1:  
   `./build/receiver --sleep-ms 2`
2. Terminal 2:  
   `./build/generator alice bob 10 200 --threads 4 --batch 32`

---

## Glossary: Running on Windows via WSL

This project is intended to be run in a **Linux** environment. On Windows, the recommended way is **WSL (Windows Subsystem for Linux)** so you get `make`, `gcc`, and the same libraries as on Linux.

### Terms

- **WSL** — Windows Subsystem for Linux. Lets you run a Linux shell (e.g. Ubuntu) on Windows.
- **WSL2** — Current default WSL version; use it for best compatibility.
- **Project path in WSL** — Windows path `C:\Users\<you>\...\shardingsim` is under WSL as `/mnt/c/Users/<you>/.../shardingsim`.

### One-time setup in WSL

1. Open a WSL terminal (e.g. “Ubuntu” from Start, or `wsl` from PowerShell).
2. Install build and runtime dependencies:

```bash
sudo apt-get update
sudo apt-get install -y build-essential libssl-dev libprotobuf-c-dev libzmq3-dev
```

### Build and run from WSL

1. Go to the project directory (replace with your actual path):

```bash
cd /mnt/c/Users/sohin/Desktop/Sem4_END_EXCELLENTLY/CS-597/shardingsim
```

2. Build:

```bash
make
```

3. Run receiver and generator in **two WSL terminals** (both in the same project directory):

**Terminal 1 (receiver):**

```bash
./build/receiver --sleep-ms 2
```

**Terminal 2 (generator):**

```bash
./build/generator alice bob 1 500 --threads 4 --batch 64
```

Or run a quick in-process test in one terminal (no receiver):

```bash
./build/generator alice bob 10 100 --in-process --threads 4 --batch 32
```

### Running the provided script from Windows (PowerShell)

You can trigger the WSL build-and-test script from PowerShell so that build and a quick ZMQ test run inside Linux:

```powershell
wsl bash -c "cd /mnt/c/Users/sohin/Desktop/Sem4_END_EXCELLENTLY/CS-597/shardingsim && ./scripts/wsl-build-and-test.sh --no-install"
```

- Use `--no-install` if dependencies are already installed in WSL.  
- Omit `--no-install` to let the script run `sudo apt-get install ...` (you may be prompted for your password).

The script will:

- Run `make clean` and `make`
- Start the receiver in the background
- Run the generator with a small workload
- Print throughput and “Done. Code is working.”

So for this repo, **Linux or WSL** is the supported environment; the “Run (Linux)” and “Build (Linux)” sections apply inside WSL as-is.

---
