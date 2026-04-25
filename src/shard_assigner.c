#include "shard.h"
#include "common.h"
#include "block.h"
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

static double assigner_now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

uint32_t shard_for_tx(const Transaction* tx, uint32_t num_shards) {
    return (uint32_t)(tx->nonce % num_shards);
}

ShardAssigner* shard_assigner_create(uint32_t num_shards, const char* base_addr, uint16_t base_port) {
    if (num_shards == 0 || num_shards > MAX_SHARDS) {
        fprintf(stderr, "shard_assigner: num_shards must be 1-%d\n", MAX_SHARDS);
        return NULL;
    }

    ShardAssigner* sa = (ShardAssigner*)safe_malloc(sizeof(ShardAssigner));
    sa->num_shards = num_shards;
    sa->zmq_ctx    = zmq_ctx_new();
    sa->round_number = 0;
    memset(sa->assigned_counts, 0, sizeof(sa->assigned_counts));

    for (uint32_t i = 0; i < num_shards; i++) {
        snprintf(sa->shard_addrs[i], sizeof(sa->shard_addrs[i]),
                "tcp://%s:%u", base_addr, (unsigned)(base_port + i));
        sa->push_socks[i] = zmq_socket(sa->zmq_ctx, ZMQ_PUSH);
        if (zmq_connect(sa->push_socks[i], sa->shard_addrs[i]) != 0) {
            fprintf(stderr, "shard_assigner: failed to connect to shard %u at %s\n",
                    i, sa->shard_addrs[i]);
        } else {
            printf("ShardAssigner: shard %u -> %s\n", i, sa->shard_addrs[i]);
        }
    }

    /* done_sock: notifies leader of real dispatch duration after each round */
    char done_addr[64];
    unsigned done_port = (unsigned)(base_port + num_shards + 2);
    snprintf(done_addr, sizeof(done_addr), "tcp://%s:%u", base_addr, done_port);
    sa->done_sock = zmq_socket(sa->zmq_ctx, ZMQ_PUSH);
    zmq_connect(sa->done_sock, done_addr);
    printf("ShardAssigner: dispatch-done -> %s\n", done_addr);

    return sa;
}

void shard_assigner_destroy(ShardAssigner* sa) {
    if (!sa) return;
    for (uint32_t i = 0; i < sa->num_shards; i++)
        zmq_close(sa->push_socks[i]);
    zmq_close(sa->done_sock);
    zmq_ctx_destroy(sa->zmq_ctx);
    free(sa);
}

void shard_assigner_dispatch(ShardAssigner* sa, Mempool* pool, uint64_t round_start_ms, uint32_t block_size) {
    /* Reset per-round counts */
    memset(sa->assigned_counts, 0, sizeof(sa->assigned_counts));

    /* Send dispatch header to leader (shard 0) before any transactions */
    sa->round_number++;
    DispatchHeader hdr;
    hdr.round_start_ms = round_start_ms;
    hdr.round_number   = sa->round_number;
    hdr.num_shards     = sa->num_shards;
    zmq_send(sa->push_socks[0], &hdr, sizeof(DispatchHeader), 0);

    double t_dispatch_start = assigner_now_ms();

    /* Round block_size down to nearest multiple of num_shards for even distribution */
    uint32_t exact = (block_size / sa->num_shards) * sa->num_shards;
    if (exact == 0) exact = sa->num_shards;

    Transaction* drain_buf[MEMPOOL_MAX_SIZE];
    uint8_t (*pubkey_buf)[32] = (uint8_t (*)[32])safe_malloc((size_t)MEMPOOL_MAX_SIZE * 32);
    uint32_t count = mempool_drain(pool, drain_buf, pubkey_buf, exact);

    for (uint32_t i = 0; i < count; i++) {
        Transaction* tx = drain_buf[i];
        uint32_t shard_id = shard_for_tx(tx, sa->num_shards);

        TxWithPubkey twp;
        memcpy(&twp.tx, tx, sizeof(Transaction));
        memcpy(twp.pubkey, pubkey_buf[i], 32);
        zmq_send(sa->push_socks[shard_id], &twp, sizeof(TxWithPubkey), 0);
        sa->assigned_counts[shard_id]++;
        transaction_destroy(tx);
    }
    free(pubkey_buf);

    /* Send RoundEnd marker to every shard so workers know exactly when to stop draining */
    for (uint32_t i = 0; i < sa->num_shards; i++) {
        RoundEnd end;
        end.round_number = sa->round_number;
        end.tx_count     = (uint32_t)sa->assigned_counts[i];
        zmq_send(sa->push_socks[i], &end, sizeof(RoundEnd), 0);
    }

    /* Notify leader of real dispatch duration */
    DispatchDone done;
    done.round_number        = sa->round_number;
    done.dispatch_duration_ms = assigner_now_ms() - t_dispatch_start;
    zmq_send(sa->done_sock, &done, sizeof(DispatchDone), 0);

    printf("ShardAssigner: dispatched %u txs across %u shards (%.2f ms)\n",
           count, sa->num_shards, done.dispatch_duration_ms);
    for (uint32_t i = 0; i < sa->num_shards; i++)
        printf("  shard[%u]: %u txs\n", i, (uint32_t)sa->assigned_counts[i]);
}
