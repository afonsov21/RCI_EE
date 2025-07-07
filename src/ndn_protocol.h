#ifndef NDN_PROTOCOL_H
#define NDN_PROTOCOL_H

#include "ndn_node.h" // Para NDNNode e suas estruturas de dados

// Funções para gerir objetos locais
void create_local_object(NDNNode *node, const char *name);
void delete_local_object(NDNNode *node, const char *name);
int has_local_object(NDNNode *node, const char *name); // Verifica se o nó possui o objeto

// Funções para gerir cache
void add_object_to_cache(NDNNode *node, const char *name);
int has_cached_object(NDNNode *node, const char *name); // Verifica se o objeto está na cache

// Funções para iniciar e processar a busca de objetos
void initiate_retrieve(NDNNode *node, const char *object_name);              // Chamada pelo UI (comando retrieve)
void process_ndn_message(NDNNode *node, int client_sd, const char *message); // Chamada pelo topology_protocol

// Funções de envio de mensagens NDN
void send_interest_message(int target_sd, unsigned char id, const char *name);
void send_object_message(int target_sd, unsigned char id, const char *name);
void send_noobject_message(int target_sd, unsigned char id, const char *name);

// Funções de depuração e visualização para NDN
void show_local_objects(NDNNode *node);
void show_interest_table(NDNNode *node);

void init_pending_interests(NDNNode *node);
void init_local_objects(NDNNode *node);
void init_cache(NDNNode *node);

#endif // NDN_PROTOCOL_H