/**
 * ============================================================================
 * RECEIVER - Validator/simulator stub (ZMQ REP)
 * ============================================================================
 *
 * Binds a ZMQ REP socket and receives transaction batches from the generator.
 * Stores accepted transactions in the mempool. When the mempool hits the
 * dispatch threshold, elects one winner node per shard and dispatches
 * transactions via the shard assigner.
 *
 * Run first, then start the generator with --connect tcp://localhost:5557
 *
 * ============================================================================
 */

#include "transaction.h"
#include "common.h"
#include "mempool.h"
#include "node.h"
#include "shard.h"
#include "election.h"
#include "blockchain.pb-c.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <zmq.h>

#define SUBMIT_BATCH_PB_PREFIX "SUBMIT_BATCH_PB:"  /* 16 bytes */
#define DEFAULT_BIND       "tcp://*:5557"
#define RECV_BUF_SIZE      (2 * 1024 * 1024)
#define DEFAULT_BLOCK_SIZE 256
#define SHARD_BASE_PORT    5560
#define MAX_NODES          256

static unsigned int  g_sleep_ms   = 0;
static int           g_verify     = 0;
static int           g_num_shards = 48;
static int           g_block_size = DEFAULT_BLOCK_SIZE;
static Mempool*      g_mempool    = NULL;
static Node*         g_nodes[MAX_NODES];
static ShardAssigner* g_assigner  = NULL;

static void setup_nodes(void) {
    for (int i = 0; i < g_num_shards; i++) {
        char addr[64];
        uint64_t stake = 100 + (uint64_t)(rand() % 900);
        snprintf(addr, sizeof(addr), "tcp://localhost:%d", SHARD_BASE_PORT + i);
        g_nodes[i] = node_create((uint32_t)i, stake, addr);
        node_print(g_nodes[i]);
    }
}

static void run_election_and_dispatch(uint32_t block_size) {
    uint64_t round_start_ms = get_current_time_ms();

    for (int i = 0; i < g_num_shards; i++) {
        g_nodes[i]->shard_id = -1;
        g_nodes[i]->role     = NODE_ROLE_VALIDATOR;
        g_nodes[i]->state    = NODE_STATE_IDLE;
    }

    Node** node_ptrs = (Node**)safe_malloc((size_t)g_num_shards * sizeof(Node*));
    for (int i = 0; i < g_num_shards; i++)
        node_ptrs[i] = g_nodes[i];

    Node** winners = elect_winners(node_ptrs, (uint32_t)g_num_shards, (uint32_t)g_num_shards);
    free(node_ptrs);
    if (!winners) return;

    election_print_results(winners, (uint32_t)g_num_shards);
    election_free_results(winners);

    shard_assigner_dispatch(g_assigner, g_mempool, round_start_ms, block_size);
}

static Transaction* pb_to_tx(const Blockchain__Transaction* pt) {
    Transaction* tx = (Transaction*)safe_malloc(sizeof(Transaction));
    memset(tx, 0, sizeof(Transaction));
    tx->nonce        = pt->nonce;
    tx->expiry_block = pt->expiry_block;
    if (pt->source_address.data && pt->source_address.len >= 20)
        memcpy(tx->source_address, pt->source_address.data, 20);
    if (pt->dest_address.data && pt->dest_address.len >= 20)
        memcpy(tx->dest_address, pt->dest_address.data, 20);
    tx->value = pt->value;
    tx->fee   = pt->fee;
    if (pt->signature.data && pt->signature.len >= 64)
        memcpy(tx->signature, pt->signature.data, 64);
    else if (pt->signature.data && pt->signature.len > 0)
        memcpy(tx->signature, pt->signature.data, pt->signature.len);
    return tx;
}

int main(int argc, char* argv[]) {
    const char* bind_addr = DEFAULT_BIND;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--bind") == 0 && i + 1 < argc) {
            bind_addr = argv[++i];
        } else if (strcmp(argv[i], "--sleep-ms") == 0 && i + 1 < argc) {
            g_sleep_ms = (unsigned int)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--verify") == 0) {
            g_verify = 1;
        } else if (strcmp(argv[i], "--num-shards") == 0 && i + 1 < argc) {
            g_num_shards = atoi(argv[++i]);
            if (g_num_shards < 1 || g_num_shards > MAX_NODES) {
                fprintf(stderr, "num-shards must be 1-%d\n", MAX_NODES);
                return 1;
            }
        } else if (strcmp(argv[i], "--block-size") == 0 && i + 1 < argc) {
            g_block_size = atoi(argv[++i]);
            if (g_block_size < 1) {
                fprintf(stderr, "block-size must be >= 1\n");
                return 1;
            }
        }
    }

    printf("\nReceiver (validator/simulator stub)\n");
    printf("  Bind: %s\n", bind_addr);
    printf("  Shards: %d  Nodes: %d  Block size: %d txs\n",
           g_num_shards, g_num_shards, g_block_size);
    if (g_sleep_ms > 0) printf("  Sleep: %u ms per batch (simulated verification)\n", g_sleep_ms);
    if (g_verify) printf("  Verify: run transaction_verify() on each TX\n");
    printf("  Run generator with: --connect tcp://localhost:5557\n\n");

    srand((unsigned int)time(NULL));

    g_mempool  = mempool_create(MEMPOOL_MAX_SIZE);
    g_assigner = shard_assigner_create((uint32_t)g_num_shards, "localhost", SHARD_BASE_PORT);
    setup_nodes();

    void* ctx = zmq_ctx_new();
    void* rep = zmq_socket(ctx, ZMQ_REP);
    if (zmq_bind(rep, bind_addr) != 0) {
        fprintf(stderr, "Failed to bind %s: %s\n", bind_addr, zmq_strerror(zmq_errno()));
        zmq_close(rep);
        zmq_ctx_destroy(ctx);
        return 1;
    }

    char* buf = (char*)safe_malloc(RECV_BUF_SIZE);
    uint64_t total_received = 0;

    while (1) {
        int size = zmq_recv(rep, buf, RECV_BUF_SIZE - 1, 0);
        if (size < 0) {
            fprintf(stderr, "zmq_recv: %s\n", zmq_strerror(zmq_errno()));
            continue;
        }

        if (size > 16 && memcmp(buf, SUBMIT_BATCH_PB_PREFIX, 16) == 0) {
            Blockchain__TransactionBatch* batch = blockchain__transaction_batch__unpack(
                NULL, (size_t)(size - 16), (uint8_t*)(buf + 16));
            int accepted = 0, rejected = 0;
            if (batch) {
                if (g_sleep_ms > 0)
                    usleep((useconds_t)(g_sleep_ms * 1000));
                for (size_t i = 0; i < batch->n_transactions; i++) {
                    Blockchain__Transaction* pt = batch->transactions[i];
                    uint8_t pubkey[32] = {0};
                    if (pt->public_key.data && pt->public_key.len >= 32)
                        memcpy(pubkey, pt->public_key.data, 32);

                    Transaction* tx = pb_to_tx(pt);
                    if (g_verify) {
                        bool valid = !is_zero(pubkey, 32)
                                     ? transaction_verify_ed25519(tx, pubkey)
                                     : transaction_verify(tx);
                        if (!valid) {
                            rejected++;
                            transaction_destroy(tx);
                            continue;
                        }
                    }
                    if (!mempool_add(g_mempool, tx, pubkey))
                        transaction_destroy(tx);
                    else
                        accepted++;
                }
                total_received += (uint64_t)(accepted + rejected);
                blockchain__transaction_batch__free_unpacked(batch, NULL);
            }

            if (mempool_size(g_mempool) >= (uint32_t)g_block_size)
                run_election_and_dispatch((uint32_t)g_block_size);

            char resp[64];
            snprintf(resp, sizeof(resp), "OK:%d|%d", accepted, rejected);
            zmq_send(rep, resp, (size_t)strlen(resp), 0);
        } else {
            zmq_send(rep, "UNKNOWN", 7, 0);
        }
    }

    free(buf);
    zmq_close(rep);
    zmq_ctx_destroy(ctx);
    for (int i = 0; i < g_num_shards; i++) node_destroy(g_nodes[i]);
    shard_assigner_destroy(g_assigner);
    mempool_destroy(g_mempool);
    (void)total_received;
    return 0;
}
