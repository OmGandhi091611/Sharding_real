/**
 * Shard Worker
 *
 * Shard 0 = Leader:
 *   - Binds PUB socket (port 5570) to broadcast ROUND_START to all followers
 *   - Binds PULL socket (port 5571) to collect ShardSummary from followers
 *   - Receives DispatchHeader from assigner, waits, then broadcasts round start
 *   - Processes own txs, collects follower ShardSummaries, assembles and prints Block
 *
 * Shards 1..N = Followers:
 *   - Subscribe to leader PUB (port 5570) waiting for ROUND_START
 *   - Drain and process buffered txs, computing fees and merkle root
 *   - PUSH ShardSummary to leader (port 5571)
 *
 * Run: ./build/shard_worker <shard_id> <port> [--num-shards N]
 */

#include "transaction.h"
#include "common.h"
#include "block.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <zmq.h>

#define BASE_TX_PORT       5560
#define LEADER_WAIT_MS     20    /* wait after dispatch header before broadcasting */
#define SUB_CONNECT_WAIT_PER_FOLLOWER_MS 50  /* 50ms per follower for SUB socket connect */

/* PUB and result ports are placed after all shard TX ports to avoid conflicts:
 * TX ports: BASE_TX_PORT .. BASE_TX_PORT + num_shards - 1
 * PUB port: BASE_TX_PORT + num_shards
 * Result port: BASE_TX_PORT + num_shards + 1
 */
#define LEADER_PUB_PORT(n)    (BASE_TX_PORT + (n))
#define LEADER_RESULT_PORT(n) (BASE_TX_PORT + (n) + 1)

static unsigned int g_proc_us = 1000;  /* simulated processing time per tx (microseconds) */

static double now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

/*
 * Read exactly the transactions for this round from pull_sock.
 * The assigner sends a RoundEnd marker after all txs — we use its tx_count
 * to know when to stop, so we never accidentally drain next-round txs.
 */
static ShardSummary process_transactions(void* pull_sock, uint32_t shard_id) {
    ShardSummary summary;
    memset(&summary, 0, sizeof(ShardSummary));
    summary.shard_id = shard_id;

    uint8_t running_hash[32];
    memset(running_hash, 0, 32);

    /* First receive the RoundEnd marker to know how many txs to expect */
    RoundEnd end;
    uint32_t expected = 0;
    while (1) {
        uint8_t buf[sizeof(Transaction)];
        int size = zmq_recv(pull_sock, buf, sizeof(buf), 0);
        if (size == (int)sizeof(RoundEnd)) {
            memcpy(&end, buf, sizeof(RoundEnd));
            expected = end.tx_count;
            break;
        }
        /* A tx arrived before the RoundEnd — process it now */
        if (size == (int)sizeof(Transaction)) {
            Transaction tx;
            memcpy(&tx, buf, sizeof(Transaction));
            if (g_proc_us > 0) usleep(g_proc_us);
            summary.tx_count++;
            summary.total_fees += tx.fee;
            uint8_t tx_hash[TX_HASH_SIZE];
            transaction_compute_hash(&tx, tx_hash);
            uint8_t combined[32 + TX_HASH_SIZE];
            memcpy(combined, running_hash, 32);
            memcpy(combined + 32, tx_hash, TX_HASH_SIZE);
            sha256(combined, sizeof(combined), running_hash);
        }
    }

    /* Drain remaining txs up to expected count */
    while (summary.tx_count < expected) {
        Transaction tx;
        int size = zmq_recv(pull_sock, &tx, sizeof(Transaction), 0);
        if (size != (int)sizeof(Transaction)) continue;
        if (g_proc_us > 0) usleep(g_proc_us);
        summary.tx_count++;
        summary.total_fees += tx.fee;
        uint8_t tx_hash[TX_HASH_SIZE];
        transaction_compute_hash(&tx, tx_hash);
        uint8_t combined[32 + TX_HASH_SIZE];
        memcpy(combined, running_hash, 32);
        memcpy(combined + 32, tx_hash, TX_HASH_SIZE);
        sha256(combined, sizeof(combined), running_hash);
    }

    memcpy(summary.merkle_root, running_hash, 32);
    return summary;
}

static void run_leader(void* ctx, int shard_id, int tx_port, int num_shards) {
    char addr[64];

    void* pull_tx = zmq_socket(ctx, ZMQ_PULL);
    snprintf(addr, sizeof(addr), "tcp://*:%d", tx_port);
    if (zmq_bind(pull_tx, addr) != 0) {
        fprintf(stderr, "[shard %d | LEADER] failed to bind tx port: %s\n",
                shard_id, zmq_strerror(zmq_errno()));
        return;
    }

    int pub_port     = LEADER_PUB_PORT(num_shards);
    int results_port = LEADER_RESULT_PORT(num_shards);

    void* pub = zmq_socket(ctx, ZMQ_PUB);
    snprintf(addr, sizeof(addr), "tcp://*:%d", pub_port);
    zmq_bind(pub, addr);

    void* pull_results = zmq_socket(ctx, ZMQ_PULL);
    snprintf(addr, sizeof(addr), "tcp://*:%d", results_port);
    zmq_bind(pull_results, addr);

    /* Wait for all followers to connect their SUB sockets before first publish.
     * 50ms per follower scales safely up to 32+ shards. */
    unsigned int sub_wait_ms = (unsigned int)(num_shards - 1) * SUB_CONNECT_WAIT_PER_FOLLOWER_MS;
    usleep(sub_wait_ms * 1000);

    printf("[shard %d | LEADER] ready — tx_port=%d  pub=%d  results=%d  followers=%d\n",
           shard_id, tx_port, pub_port, results_port, num_shards - 1);

    uint32_t round = 0;

    while (1) {
        /* Block until the assigner sends a DispatchHeader */
        uint8_t buf[sizeof(Transaction)];
        int size = zmq_recv(pull_tx, buf, sizeof(buf), 0);
        if (size < 0) continue;

        double block_start;

        if (size == (int)sizeof(DispatchHeader)) {
            DispatchHeader hdr;
            memcpy(&hdr, buf, sizeof(DispatchHeader));
            block_start = now_ms();
            round       = hdr.round_number;
        } else if (size == (int)sizeof(uint64_t)) {
            /* backwards compat: older assigner sends just round_start_ms */
            block_start = now_ms();
            round++;
        } else {
            block_start = now_ms();
            round++;
        }

        /* Wait for assigner to finish dispatching txs to all shards */
        usleep(LEADER_WAIT_MS * 1000);

        /* Broadcast round start to all followers */
        char msg[64];
        snprintf(msg, sizeof(msg), "ROUND_START:%u", round);
        zmq_send(pub, msg, strlen(msg), 0);
        printf("[shard %d | LEADER] round %u announced\n", shard_id, round);

        /* Process own transactions */
        ShardSummary own = process_transactions(pull_tx, (uint32_t)shard_id);
        printf("[shard %d | LEADER] processed %u txs  fees=%lu\n",
               shard_id, own.tx_count, (unsigned long)own.total_fees);

        /* Collect ShardSummary from every follower */
        ShardSummary* summaries = (ShardSummary*)safe_malloc(
            (size_t)num_shards * sizeof(ShardSummary));
        summaries[0] = own;

        int results_received = 0;
        int followers        = num_shards - 1;

        while (results_received < followers) {
            ShardSummary s;
            int sz = zmq_recv(pull_results, &s, sizeof(ShardSummary), 0);
            if (sz != (int)sizeof(ShardSummary)) continue;
            printf("[shard %d | LEADER] shard %u reported: %u txs  fees=%lu\n",
                   shard_id, s.shard_id, s.tx_count, (unsigned long)s.total_fees);
            summaries[1 + results_received] = s;
            results_received++;
        }

        /* Assemble block from all summaries */
        Block block;
        memset(&block, 0, sizeof(Block));
        block.block_number = round;
        block.timestamp_ms = (uint64_t)now_ms();
        block.num_shards   = (uint32_t)num_shards;

        for (int i = 0; i < num_shards; i++) {
            block.total_tx_count += summaries[i].tx_count;
            block.total_fees     += summaries[i].total_fees;
        }

        /* Combined merkle root: SHA256 of all shard merkle roots concatenated */
        uint8_t* all_roots = (uint8_t*)safe_malloc((size_t)num_shards * 32);
        for (int i = 0; i < num_shards; i++)
            memcpy(all_roots + i * 32, summaries[i].merkle_root, 32);
        sha256(all_roots, (size_t)num_shards * 32, block.merkle_root);
        free(all_roots);
        free(summaries);

        block.block_time_ms = now_ms() - block_start;
        if (block.block_time_ms < 0) block.block_time_ms = 0;
        block.tps = block.block_time_ms > 0
                    ? (block.total_tx_count / block.block_time_ms) * 1000.0
                    : 0;

        char root_hex[65];
        bytes_to_hex_buf(block.merkle_root, 32, root_hex);
        root_hex[64] = '\0';

        printf("\n=== BLOCK %u FINALIZED ===\n",   block.block_number);
        printf("  Shards    : %u\n",                block.num_shards);
        printf("  Total TXs : %u\n",                block.total_tx_count);
        printf("  Total fees: %lu\n",               (unsigned long)block.total_fees);
        printf("  Merkle    : %.16s...\n",           root_hex);
        printf("  Block time: %.2f ms\n",            block.block_time_ms);
        printf("  TPS       : %.2f\n",               block.tps);
        printf("=========================\n\n");
    }

    zmq_close(pull_tx);
    zmq_close(pub);
    zmq_close(pull_results);
}

static void run_follower(void* ctx, int shard_id, int tx_port, int num_shards) {
    char addr[64];

    void* pull_tx = zmq_socket(ctx, ZMQ_PULL);
    snprintf(addr, sizeof(addr), "tcp://*:%d", tx_port);
    if (zmq_bind(pull_tx, addr) != 0) {
        fprintf(stderr, "[shard %d | FOLLOWER] failed to bind tx port: %s\n",
                shard_id, zmq_strerror(zmq_errno()));
        return;
    }

    void* sub = zmq_socket(ctx, ZMQ_SUB);
    snprintf(addr, sizeof(addr), "tcp://localhost:%d", LEADER_PUB_PORT(num_shards));
    zmq_connect(sub, addr);
    zmq_setsockopt(sub, ZMQ_SUBSCRIBE, "ROUND_START", 11);

    void* push = zmq_socket(ctx, ZMQ_PUSH);
    snprintf(addr, sizeof(addr), "tcp://localhost:%d", LEADER_RESULT_PORT(num_shards));
    zmq_connect(push, addr);

    printf("[shard %d | FOLLOWER] ready — tx_port=%d  subscribed to leader\n",
           shard_id, tx_port);

    while (1) {
        /* Wait for leader to announce round start */
        char msg[64];
        int sz = zmq_recv(sub, msg, sizeof(msg) - 1, 0);
        if (sz < 0) continue;
        msg[sz] = '\0';

        uint32_t round = 0;
        sscanf(msg, "ROUND_START:%u", &round);
        printf("[shard %d | FOLLOWER] round %u — processing...\n", shard_id, round);

        /* Process all buffered txs and build summary */
        ShardSummary summary = process_transactions(pull_tx, (uint32_t)shard_id);
        printf("[shard %d | FOLLOWER] done: %u txs  fees=%lu\n",
               shard_id, summary.tx_count, (unsigned long)summary.total_fees);

        /* Send ShardSummary to leader */
        zmq_send(push, &summary, sizeof(ShardSummary), 0);
    }

    zmq_close(pull_tx);
    zmq_close(sub);
    zmq_close(push);
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <shard_id> <port> [--num-shards N]\n", argv[0]);
        return 1;
    }

    int shard_id   = atoi(argv[1]);
    int port       = atoi(argv[2]);
    int num_shards = 4;

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--num-shards") == 0 && i + 1 < argc)
            num_shards = atoi(argv[++i]);
        else if (strcmp(argv[i], "--proc-us") == 0 && i + 1 < argc)
            g_proc_us = (unsigned int)atoi(argv[++i]);
    }

    void* ctx = zmq_ctx_new();

    if (shard_id == 0)
        run_leader(ctx, shard_id, port, num_shards);
    else
        run_follower(ctx, shard_id, port, num_shards);

    zmq_ctx_destroy(ctx);
    return 0;
}
