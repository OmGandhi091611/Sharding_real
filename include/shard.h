#ifndef SHARD_H
#define SHARD_H

#include "transaction.h"
#include "mempool.h"
#include <stdint.h>
#include <zmq.h>

#define MAX_SHARDS 64

typedef struct {
    uint32_t  num_shards;
    uint32_t  round_number;
    void*     zmq_ctx;
    void*     push_socks[MAX_SHARDS];
    char      shard_addrs[MAX_SHARDS][64];
    uint64_t  assigned_counts[MAX_SHARDS];
} ShardAssigner;

ShardAssigner* shard_assigner_create(uint32_t num_shards, const char* base_addr, uint16_t base_port);
void           shard_assigner_destroy(ShardAssigner* sa);
void           shard_assigner_dispatch(ShardAssigner* sa, Mempool* pool, uint64_t round_start_ms, uint32_t block_size);
uint32_t       shard_for_tx(const Transaction* tx, uint32_t num_shards);

#endif // SHARD_H
