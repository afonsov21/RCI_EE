#ifndef NDN_NODE_H
#define NDN_NODE_H

#include <sys/select.h>
#include <netinet/in.h>

// Constantes para mensagens UDP e TCP
#define MAX_UDP_MSG_LEN 512
#define MAX_TCP_MSG_LEN 512 // Tamanho máximo de uma mensagem TCP completa
#define MAX_IP_LEN 16
#define MAX_PORT_LEN 6 // Ex: "65535\0"

// Definições para tipos de vizinhos
typedef enum
{
    NEIGHBOR_TYPE_EXTERNAL,
    NEIGHBOR_TYPE_INTERNAL,
    NEIGHBOR_TYPE_PENDING_INCOMING, // NOVO TIPO
    NEIGHBOR_TYPE_NONE              // Para vizinhos não classificados ou slots vazios
} NeighborType;

#define MAX_TCP_RECV_BUFFER_SIZE (MAX_TCP_MSG_LEN * 2) // Buffer para receber mensagens fragmentadas

// Estrutura para representar um vizinho
typedef struct
{
    char ip[MAX_IP_LEN];
    int tcp_port;
    int socket_sd;     // Socket descriptor para esta conexão TCP
    NeighborType type; // EXTERNAL ou INTERNAL
    int is_valid;      // 1 se o slot está em uso, 0 caso contrário

    // Buffer de receção para este socket específico
    char recv_buffer[MAX_TCP_RECV_BUFFER_SIZE];
    int recv_buffer_pos; // Posição atual de escrita no buffer
} Neighbor;

#define MAX_NEIGHBORS 10        // Número máximo de vizinhos que um nó pode ter
#define MAX_OBJECT_NAME_LEN 100 // Máximo de 100 caracteres para o nome do objeto [cite: 128]

// --- Novas estruturas para a NDN ---

// Para objetos criados localmente pelo utilizador
#define MAX_LOCAL_OBJECTS 20 // Exemplo: um limite para objetos que o nó possui
typedef struct
{
    char name[MAX_OBJECT_NAME_LEN + 1]; // Nome do objeto, +1 para '\0'
    int is_valid;                       // 1 se o slot está em uso, 0 caso contrário
} LocalObject;

// Para a cache de objetos
#define MAX_CACHE_OBJECTS 10 // A cache terá um tamanho máximo de 5 objetos [cite: 86]
typedef struct
{
    char name[MAX_OBJECT_NAME_LEN + 1]; // Nome do objeto
    // TODO: Adicionar um timestamp para política de gestão (LRU ou FIFO, por exemplo)
    int is_valid;          // 1 se o slot está em uso, 0 caso contrário
    long last_access_time; // Para política LRU (simplificada, apenas para exemplo)
} CachedObject;

// Para a Tabela de Interesses Pendentes (PIT - Pending Interest Table)
#define MAX_PENDING_INTERESTS 50   // Limite para interesses pendentes
#define MAX_INTEREST_INTERFACES 10 // Limite de interfaces por interesse (pode ser MAX_NEIGHBORS + 1 (stdin))

// Estados de uma interface para um interesse [cite: 72, 73, 76]
typedef enum
{
    INTERFACE_STATE_RESPONSE, // Por onde a mensagem de objeto/não-objeto deverá ser reencaminhada [cite: 72]
    INTERFACE_STATE_WAITING,  // Por onde o nó encaminhou ou reencaminhou uma mensagem de interesse [cite: 75]
    INTERFACE_STATE_CLOSED,   // Por onde o nó recebeu uma mensagem de não-objeto [cite: 76]
    INTERFACE_STATE_NONE      // Slot vazio ou não usado
} InterestInterfaceState;

typedef struct
{
    int sd; // Socket descriptor da interface, ou STDIN_FILENO para o utilizador
    InterestInterfaceState state;
    int is_valid; // 1 se este slot está em uso
} InterestInterface;

typedef struct
{
    unsigned char interest_id;                 // Identificador de procura (0-255) [cite: 80]
    char object_name[MAX_OBJECT_NAME_LEN + 1]; // Nome do objeto procurado [cite: 71]
    InterestInterface interfaces[MAX_INTEREST_INTERFACES];
    int num_active_interfaces; // Contagem de interfaces para este interesse
    int is_valid;              // 1 se esta entrada está em uso
} PendingInterestEntry;

// Estrutura principal do nó
typedef struct
{
    char ip[MAX_IP_LEN]; // Endereço IP próprio do nó
    int tcp_port;        // Porto TCP de escuta próprio do nó
    char reg_ip[MAX_IP_LEN];
    int reg_udp_port;
    struct sockaddr_in reg_server_addr; // Endereço do servidor de registo

    int tcp_listen_sd; // Socket descriptor para o TCP de escuta
    int udp_reg_sd;    // Socket descriptor para o UDP do servidor de registo

    // Informações de rede
    int current_net_id; // -1 se não estiver em nenhuma rede, ou o ID da rede

    // Informações de topologia
    Neighbor neighbors[MAX_NEIGHBORS];
    int num_active_neighbors; // Quantidade de vizinhos atualmente conectados

    int is_leaving;                       // Flag: 1 se o nó está em processo de saída
    int internal_neighbors_to_disconnect; // Contador de vizinhos internos para fechar conexões

    // --- NDN Data Structures ---
    LocalObject local_objects[MAX_LOCAL_OBJECTS];
    int num_local_objects;

    CachedObject object_cache[MAX_CACHE_OBJECTS];
    int num_cached_objects;

    PendingInterestEntry pending_interests[MAX_PENDING_INTERESTS];
    int num_pending_interests;

} NDNNode;

// Obter a instância do nó (para que outras funções possam acessá-la)
NDNNode *get_current_ndn_node();

// Funções de inicialização e gestão do nó
void ndn_node_init(const char *ip, int tcp_port, const char *reg_ip, int reg_udp_port);
void start_ndn_node_loop(); // Loop principal de multiplexagem síncrona
void ndn_node_cleanup();    // Função para fechar sockets e libertar recursos

#endif // NDN_NODE_H