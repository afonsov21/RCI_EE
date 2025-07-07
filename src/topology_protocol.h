#ifndef TOPOLOGY_PROTOCOL_H
#define TOPOLOGY_PROTOCOL_H

#include "ndn_node.h"

// Funções para gerir vizinhos
int add_neighbor(NDNNode *node, const char *ip, int port, int sd, NeighborType type);
void remove_neighbor(NDNNode *node, int sd);
Neighbor *find_neighbor_by_sd(NDNNode *node, int sd);
Neighbor *find_neighbor_by_addr(NDNNode *node, const char *ip, int port);
Neighbor *get_external_neighbor(NDNNode *node);

// Funções para conexão
int connect_to_node(NDNNode *node, const char *target_ip, int target_tcp_port);
void process_incoming_connection(NDNNode *node, int new_socket_sd, const char *client_ip, int client_port);

// Funções para mensagens de topologia
void send_entry_message(int target_sd, NDNNode *node);
void send_leave_message(int target_sd, NDNNode *node);

// Nova função para processar dados brutos recebidos e extrair mensagens completas
void handle_tcp_data_received(NDNNode *node, int client_sd, char *data, ssize_t len);

// Função interna para processar uma mensagem TCP completa (não chamada diretamente do loop)
void process_complete_tcp_message(NDNNode *node, int client_sd, const char *message);

#endif // TOPOLOGY_PROTOCOL_H