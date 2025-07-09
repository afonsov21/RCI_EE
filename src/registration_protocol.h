#ifndef REGISTRATION_PROTOCOL_H
#define REGISTRATION_PROTOCOL_H

#include "ndn_node.h" // Para acessar a estrutura NDNNode

#define MAX_NODES_PER_NET 100

// Funções para enviar mensagens ao servidor de registo
void send_reg_message(NDNNode *node, int net_id);
void send_unreg_message(NDNNode *node, int net_id);
void send_nodes_request_message(NDNNode *node, int net_id);

// Função para processar mensagens recebidas do servidor de registo
void process_udp_registration_message(NDNNode *node, const char *message);

#endif // REGISTRATION_PROTOCOL_H