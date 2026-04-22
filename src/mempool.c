#include "mempool.h"
#include "common.h"
#include <string.h>
                                                                                                        
Mempool* mempool_create(uint32_t capacity) {
    if (capacity == 0 || capacity > MEMPOOL_MAX_SIZE)                                                     
        capacity = MEMPOOL_MAX_SIZE;                                                                      
    Mempool* pool = (Mempool*)safe_malloc(sizeof(Mempool));
    pool->txs      = (Transaction**)safe_malloc(capacity * sizeof(Transaction*));                         
    pool->count    = 0;                                                                                   
    pool->capacity = capacity;
    pthread_mutex_init(&pool->lock, NULL);                                                                
    return pool;                                          
}                                                                                                         

void mempool_destroy(Mempool* pool) {                                                                     
    if (!pool) return;                                    
    pthread_mutex_lock(&pool->lock);
    for (uint32_t i = 0; i < pool->count; i++)
        transaction_destroy(pool->txs[i]);                                                                
    free(pool->txs);
    pthread_mutex_unlock(&pool->lock);                                                                    
    pthread_mutex_destroy(&pool->lock);                   
    free(pool);                                                                                           
}                                                         

int mempool_add(Mempool* pool, Transaction* tx) {                                                         
    pthread_mutex_lock(&pool->lock);
    if (pool->count >= pool->capacity) {                                                                  
        pthread_mutex_unlock(&pool->lock);                
        return 0;
    }
    pool->txs[pool->count++] = tx;                                                                        
    pthread_mutex_unlock(&pool->lock);
    return 1;                                                                                             
}                                                         

uint32_t mempool_drain(Mempool* pool, Transaction** out, uint32_t max) {                                  
    pthread_mutex_lock(&pool->lock);
    uint32_t n = pool->count < max ? pool->count : max;                                                   
    memcpy(out, pool->txs, n * sizeof(Transaction*));                                                     
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