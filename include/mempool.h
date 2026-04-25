#ifndef MEMPOOL_H
#define MEMPOOL_H

#include "transaction.h"
#include <stdint.h>
#include <pthread.h>

#define MEMPOOL_MAX_SIZE 65536

typedef struct {
    Transaction**   txs;
    uint8_t       (*pubkeys)[32]; // parallel Ed25519 pubkeys, one per TX
    uint32_t        count;
    uint32_t        capacity;
    pthread_mutex_t lock;
} Mempool;

Mempool* mempool_create(uint32_t capacity);
void     mempool_destroy(Mempool* pool);
int      mempool_add(Mempool* pool, Transaction* tx, const uint8_t pubkey[32]);
uint32_t mempool_drain(Mempool* pool, Transaction** out, uint8_t (*pubkeys_out)[32], uint32_t max);
uint32_t mempool_size(Mempool* pool);

#endif // MEMPOOL_H
