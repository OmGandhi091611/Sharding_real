#include "election.h"                                     
#include "common.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
                                                                            
Node** elect_winners(Node** nodes, uint32_t num_nodes, uint32_t num_shards) {
    if (!nodes || num_nodes == 0 || num_shards == 0 || num_shards >          
num_nodes) {                                                                 
        fprintf(stderr, "elect_winners: need num_nodes >= num_shards\n");
        return NULL;                                                         
    }                                                     

    Node** winners = (Node**)safe_malloc(num_shards * sizeof(Node*));        
    int* used = (int*)safe_malloc(num_nodes * sizeof(int));
    memset(used, 0, num_nodes * sizeof(int));                                
                                                        
    for (uint32_t s = 0; s < num_shards; s++) {                              
        uint64_t total_stake = 0;
        for (uint32_t n = 0; n < num_nodes; n++)                             
            if (!used[n]) total_stake += nodes[n]->stake;                    

        uint64_t pick = total_stake > 0 ? ((uint64_t)rand() % total_stake) : 
0;                                                        
        uint64_t cumulative = 0;                                             
        uint32_t winner_idx = 0;                          
        for (uint32_t n = 0; n < num_nodes; n++) {
            if (used[n]) continue;
            cumulative += nodes[n]->stake;                                   
            if (cumulative > pick) { winner_idx = n; break; }
        }                                                                    
                                                        
        used[winner_idx] = 1;                                                
        node_assign_shard(nodes[winner_idx], (int)s);
        winners[s] = nodes[winner_idx];                                      
    }                                                     

    free(used);
    return winners;
}

void election_print_results(Node** winners, uint32_t num_shards) {           
    printf("Election results:\n");
    for (uint32_t i = 0; i < num_shards; i++)                                
        printf("  shard[%u] -> node[%u]  stake=%lu  addr=%s\n",
                i, winners[i]->id, winners[i]->stake, winners[i]->zmq_addr);  
}                                                                            
                                                                            
void election_free_results(Node** winners) {                                 
    free(winners);                                        
}