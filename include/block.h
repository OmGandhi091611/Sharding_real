#ifndef BLOCK_H
#define BLOCK_H

#include <stdint.h>

/* Sent by the shard assigner to the leader shard at the start of each round */
typedef struct {
    uint64_t round_start_ms;
    uint32_t round_number;
    uint32_t num_shards;
} DispatchHeader;

/* Sent by the assigner to the leader after all txs + RoundEnds are dispatched */
typedef struct {
    uint32_t round_number;
    double   dispatch_duration_ms;   /* real measured dispatch time */
} DispatchDone;

/* Sent by the assigner to every shard after all txs are dispatched for a round */
typedef struct {
    uint32_t round_number;
    uint32_t tx_count;   /* how many txs were sent to this shard this round */
} RoundEnd;

/* Summary a follower shard sends to the leader after processing its transactions */
typedef struct {
    uint32_t shard_id;
    uint32_t tx_count;
    uint64_t total_fees;
    uint8_t  merkle_root[32];   /* SHA256 of all tx hashes in this shard */
} ShardSummary;

/* Block assembled by the leader from all shard summaries */
typedef struct {
    uint32_t block_number;
    uint64_t timestamp_ms;      /* when the leader finalized this block */
    uint32_t num_shards;        /* how many shards contributed */
    uint32_t total_tx_count;    /* sum of all shard tx_counts */
    uint64_t total_fees;        /* sum of all shard total_fees */
    uint8_t  merkle_root[32];   /* combined hash of all shard merkle_roots */
    double   processing_latency_ms;  /* parallel tx processing time (ROUND_START → leader done) */
    double   coord_latency_ms;       /* dispatch wait + follower summary collection */
    double   block_time_ms;          /* aggregate: processing + coordination */
    double   tps;                    /* total_tx_count / block_time_ms * 1000 */
} Block;

#endif /* BLOCK_H */
