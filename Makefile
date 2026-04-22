# Parallel TX Simulator - generator (OpenMP + ZMQ) and receiver (ZMQ REP)
# Requires: OpenSSL, protobuf-c, OpenMP, ZMQ (libzmq)

CC       = gcc
CFLAGS   = -Wall -Wextra -O2 -g -I./include -I./proto -fopenmp
LDFLAGS  = -lssl -lcrypto -lm -lprotobuf-c -lzmq -lpthread -fopenmp

BUILD_DIR = build
SRC_DIR   = src
PROTO_DIR = proto

# Generator: main.c + wallet + transaction + common + blake3 + proto
GEN_OBJS = $(BUILD_DIR)/main.o \
           $(BUILD_DIR)/transaction.o \
           $(BUILD_DIR)/wallet.o \
           $(BUILD_DIR)/common.o \
           $(BUILD_DIR)/blake3.o \
           $(BUILD_DIR)/node.o \
           $(BUILD_DIR)/blockchain.pb-c.o
GENERATOR = $(BUILD_DIR)/generator

# Receiver: receiver.c + transaction + common + blake3 + proto (no wallet)
REC_OBJS = $(BUILD_DIR)/receiver.o \
           $(BUILD_DIR)/transaction.o \
           $(BUILD_DIR)/common.o \
           $(BUILD_DIR)/blake3.o \
           $(BUILD_DIR)/node.o \
           $(BUILD_DIR)/mempool.o \
           $(BUILD_DIR)/shard_assigner.o \
           $(BUILD_DIR)/election.o \
           $(BUILD_DIR)/blockchain.pb-c.o
RECEIVER = $(BUILD_DIR)/receiver

# Shard worker: one process per shard
SW_OBJS = $(BUILD_DIR)/shard_worker.o \
          $(BUILD_DIR)/transaction.o \
          $(BUILD_DIR)/common.o \
          $(BUILD_DIR)/blake3.o \
          $(BUILD_DIR)/blockchain.pb-c.o
SHARD_WORKER = $(BUILD_DIR)/shard_worker

.PHONY: all clean run test

all: $(BUILD_DIR) $(GENERATOR) $(RECEIVER) $(SHARD_WORKER)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/main.o: main.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/transaction.o: $(SRC_DIR)/transaction.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD_DIR)/wallet.o: $(SRC_DIR)/wallet.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD_DIR)/common.o: $(SRC_DIR)/common.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD_DIR)/blake3.o: $(SRC_DIR)/blake3.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/blockchain.pb-c.o: $(PROTO_DIR)/blockchain.pb-c.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/node.o: $(SRC_DIR)/node.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/mempool.o: $(SRC_DIR)/mempool.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/shard_assigner.o: $(SRC_DIR)/shard_assigner.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/election.o: $(SRC_DIR)/election.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/shard_worker.o: $(SRC_DIR)/shard_worker.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/receiver.o: $(SRC_DIR)/receiver.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(GENERATOR): $(GEN_OBJS)
	$(CC) $(CFLAGS) -o $@ $(GEN_OBJS) $(LDFLAGS)
	@echo "  generator: ./$(GENERATOR) <from> <to> <amount> <count> [--connect tcp://localhost:5557]"

$(RECEIVER): $(REC_OBJS)
	$(CC) $(CFLAGS) -o $@ $(REC_OBJS) $(LDFLAGS)
	@echo "  receiver:  ./$(RECEIVER) [--bind tcp://*:5557] [--sleep-ms N] [--verify]"
	@echo ""
	@echo "Run receiver first, then generator (default connect: tcp://localhost:5557)"
	@echo ""

$(SHARD_WORKER): $(SW_OBJS)
	$(CC) $(CFLAGS) -o $@ $(SW_OBJS) $(LDFLAGS)
	@echo "  shard_worker: ./$(SHARD_WORKER) <shard_id> <port>"

clean:
	rm -rf $(BUILD_DIR)

run: all
	@echo "Start receiver in another terminal: ./$(RECEIVER)"
	@echo "Then run: ./$(GENERATOR) sender receiver 1 1000 --threads 48 --batch 64"
	./$(GENERATOR) sender receiver 1 1000 --threads 48 --batch 64

test: all
	@echo "Test (ZMQ): start receiver with: ./$(RECEIVER) --sleep-ms 2"
	@echo "Then in another terminal: ./$(GENERATOR) alice bob 10 200 --threads 4 --batch 32"
	@echo ""
	@echo "Test (in-process, no ZMQ): ./$(GENERATOR) alice bob 10 100 --in-process --threads 4 --batch 32"
	./$(GENERATOR) alice bob 10 100 --in-process --threads 4 --batch 32
	@echo "Done."
