#ifndef NODE_H
#define NODE_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    NODE_ROLE_VALIDATOR,
    NODE_ROLE_WINNER
} NodeRole;

typedef enum {
    NODE_STATE_IDLE,
    NODE_STATE_PROCESSING
} NodeState;

typedef struct {
    uint32_t   id;
    uint64_t   stake;        /* used for winner election weight */
    int        shard_id;     /* -1 if not assigned */
    NodeRole   role;
    NodeState  state;
    char       zmq_addr[64]; /* e.g. "tcp://localhost:556X" */
} Node;                                                                                                   

Node*    node_create(uint32_t id, uint64_t stake, const char* zmq_addr);                                  
void     node_destroy(Node* node);
void     node_assign_shard(Node* node, int shard_id);                                                     
void     node_print(const Node* node);

#endif // NODE_H