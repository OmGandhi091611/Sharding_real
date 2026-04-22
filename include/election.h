#ifndef ELECTION_H                                                           
#define ELECTION_H                                                           

#include "node.h"                                                            
#include <stdint.h>                                       

Node** elect_winners(Node** nodes, uint32_t num_nodes, uint32_t num_shards); 
void   election_print_results(Node** winners, uint32_t num_shards);
void   election_free_results(Node** winners);                                
                                                        
#endif // ELECTION_H