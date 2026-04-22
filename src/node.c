 #include "node.h"
  #include "common.h"
  #include <stdio.h>
  #include <string.h>
                                                                                                            
  Node* node_create(uint32_t id, uint64_t stake, const char* zmq_addr) {
      Node* n = (Node*)safe_malloc(sizeof(Node));                                                           
      n->id       = id;
      n->stake    = stake;                                                                                  
      n->shard_id = -1;
      n->role     = NODE_ROLE_VALIDATOR;                                                                    
      n->state    = NODE_STATE_IDLE;
      safe_strcpy(n->zmq_addr, zmq_addr, sizeof(n->zmq_addr));
      return n;
  }

  void node_destroy(Node* node) {
      if (node) free(node);
  }                                                                                                         
   
  void node_assign_shard(Node* node, int shard_id) {                                                        
      node->shard_id = shard_id;
      node->role     = NODE_ROLE_WINNER;
  }

  void node_print(const Node* node) {
      printf("Node[%u] stake=%lu shard=%d role=%s state=%s addr=%s\n",
          node->id, node->stake, node->shard_id,                                                            
          node->role  == NODE_ROLE_WINNER    ? "WINNER"     : "VALIDATOR",
          node->state == NODE_STATE_PROCESSING ? "PROCESSING" : "IDLE",                                     
          node->zmq_addr);
  }