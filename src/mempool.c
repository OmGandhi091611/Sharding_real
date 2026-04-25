#include "mempool.h"
#include "common.h"
#include <string.h>

Mempool* mempool_create(uint32_t capacity) {
    if (capacity == 0 || capacity > MEMPOOL_MAX_SIZE)
        capacity = MEMPOOL_MAX_SIZE;
    Mempool* pool    = (Mempool*)safe_malloc(sizeof(Mempool));
    pool->txs        = (Transaction**)safe_malloc(capacity * sizeof(Transaction*));
    pool->pubkeys    = (uint8_t (*)[32])safe_malloc((size_t)capacity * 32);
    pool->count      = 0;
    pool->capacity   = capacity;
    pthread_mutex_init(&pool->lock, NULL);
    return pool;
}

void mempool_destroy(Mempool* pool) {
    if (!pool) return;
    pthread_mutex_lock(&pool->lock);
    for (uint32_t i = 0; i < pool->count; i++)
        transaction_destroy(pool->txs[i]);
    free(pool->txs);
    free(pool->pubkeys);
    pthread_mutex_unlock(&pool->lock);
    pthread_mutex_destroy(&pool->lock);
    free(pool);
}

int mempool_add(Mempool* pool, Transaction* tx, const uint8_t pubkey[32]) {
    pthread_mutex_lock(&pool->lock);
    if (pool->count >= pool->capacity) {
        pthread_mutex_unlock(&pool->lock);
        return 0;
    }
    pool->txs[pool->count] = tx;
    if (pubkey)
        memcpy(pool->pubkeys[pool->count], pubkey, 32);
    else
        memset(pool->pubkeys[pool->count], 0, 32);
    pool->count++;
    pthread_mutex_unlock(&pool->lock);
    return 1;
}

uint32_t mempool_drain(Mempool* pool, Transaction** out, uint8_t (*pubkeys_out)[32], uint32_t max) {
    pthread_mutex_lock(&pool->lock);
    uint32_t n = pool->count < max ? pool->count : max;
    memcpy(out, pool->txs, n * sizeof(Transaction*));
    if (pubkeys_out)
        memcpy(pubkeys_out, pool->pubkeys, (size_t)n * 32);
    pool->count = 0;
    pthread_mutex_unlock(&pool->lock);
    return n;
}

uint32_t mempool_size(Mempool* pool) {
    pthread_mutex_lock(&pool->lock);
    uint32_t n = pool->count;
    pthread_mutex_unlock(&pool->lock);
    return n;
}
