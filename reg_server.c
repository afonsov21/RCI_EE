#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

// Definições
#define REG_UDP_PORT 59000
#define MAX_BUFFER_SIZE 512
#define MAX_NODES_PER_NET 100 // Número máximo de nós por rede
#define MAX_NETS 10           // Número máximo de redes

// Estrutura para representar um nó registado
typedef struct
{
    char ip[16]; // Considerando IPv4 "XXX.XXX.XXX.XXX\0"
    int tcp_port;
    int is_valid; // 1 se o slot está em uso, 0 caso contrário
} NodeInfo;

// Estrutura para uma rede e seus nós
typedef struct
{
    int net_id;
    NodeInfo nodes[MAX_NODES_PER_NET];
    int node_count;
    int is_valid; // 1 se esta rede está em uso
} Network;

// Array de redes
Network networks[MAX_NETS];

// Função para inicializar as estruturas de dados das redes
void init_networks()
{
    for (int i = 0; i < MAX_NETS; i++)
    {
        networks[i].is_valid = 0;
        networks[i].net_id = -1;
        networks[i].node_count = 0;
        for (int j = 0; j < MAX_NODES_PER_NET; j++)
        {
            networks[i].nodes[j].is_valid = 0;
        }
    }
}

// Função para encontrar ou criar uma rede
Network *find_or_create_network(int net_id)
{
    // Tenta encontrar
    for (int i = 0; i < MAX_NETS; i++)
    {
        if (networks[i].is_valid && networks[i].net_id == net_id)
        {
            return &networks[i];
        }
    }
    // Se não encontrou, tenta criar num slot vazio
    for (int i = 0; i < MAX_NETS; i++)
    {
        if (!networks[i].is_valid)
        {
            networks[i].is_valid = 1;
            networks[i].net_id = net_id;
            networks[i].node_count = 0;
            printf("Created new network: %03d\n", net_id);
            return &networks[i];
        }
    }
    fprintf(stderr, "Error: No space for new network %d\n", net_id);
    return NULL; // Sem espaço para mais redes
}

// Função para adicionar um nó a uma rede
int add_node_to_network(Network *net, const char *ip, int tcp_port)
{
    if (net->node_count >= MAX_NODES_PER_NET)
    {
        fprintf(stderr, "Error: Network %03d is full. Cannot add node %s:%d\n", net->net_id, ip, tcp_port);
        return -1; // Rede cheia
    }

    // Verificar se o nó já existe
    for (int i = 0; i < MAX_NODES_PER_NET; i++)
    {
        if (net->nodes[i].is_valid && strcmp(net->nodes[i].ip, ip) == 0 && net->nodes[i].tcp_port == tcp_port)
        {
            printf("Node %s:%d already exists in net %03d.\n", ip, tcp_port, net->net_id);
            return 0; // Nó já existe
        }
    }

    // Adicionar num slot vazio
    for (int i = 0; i < MAX_NODES_PER_NET; i++)
    {
        if (!net->nodes[i].is_valid)
        {
            strcpy(net->nodes[i].ip, ip);
            net->nodes[i].tcp_port = tcp_port;
            net->nodes[i].is_valid = 1;
            net->node_count++;
            printf("Added node %s:%d to net %03d. Total nodes: %d\n", ip, tcp_port, net->net_id, net->node_count);
            return 1; // Sucesso
        }
    }
    return -1; // Não deveria chegar aqui se a contagem estiver correta
}

// Função para remover um nó de uma rede
int remove_node_from_network(Network *net, const char *ip, int tcp_port)
{
    if (!net)
        return 0;

    for (int i = 0; i < MAX_NODES_PER_NET; i++)
    {
        if (net->nodes[i].is_valid && strcmp(net->nodes[i].ip, ip) == 0 && net->nodes[i].tcp_port == tcp_port)
        {
            net->nodes[i].is_valid = 0; // Marcar como inválido
            net->node_count--;
            printf("Removed node %s:%d from net %03d. Remaining nodes: %d\n", ip, tcp_port, net->net_id, net->node_count);
            // Se a rede ficar vazia, podemos marcá-la como inválida também
            if (net->node_count == 0)
            {
                net->is_valid = 0;
                printf("Network %03d is now empty and removed.\n", net->net_id);
            }
            return 1; // Sucesso
        }
    }
    return 0; // Nó não encontrado
}

void handle_reg_message(int sock_fd, struct sockaddr_in *client_addr, socklen_t client_len, const char *message)
{
    int net_id;
    char ip_str[16];
    int tcp_port;
    char response[MAX_BUFFER_SIZE];

    if (sscanf(message, "REG %d %s %d", &net_id, ip_str, &tcp_port) == 3)
    {
        Network *net = find_or_create_network(net_id);
        if (net && add_node_to_network(net, ip_str, tcp_port) != -1)
        {
            strcpy(response, "OKREG");
            printf("REG: Node %s:%d registered in net %03d.\n", ip_str, tcp_port, net_id);
        }
        else
        {
            strcpy(response, "ERROR: Could not register node");
            fprintf(stderr, "REG: Failed to register node %s:%d in net %03d\n", ip_str, tcp_port, net_id);
        }
        sendto(sock_fd, response, strlen(response), 0, (struct sockaddr *)client_addr, client_len);
    }
    else
    {
        fprintf(stderr, "Malformed REG message: %s\n", message);
    }
}

void handle_unreg_message(int sock_fd, struct sockaddr_in *client_addr, socklen_t client_len, const char *message)
{
    int net_id;
    char ip_str[16];
    int tcp_port;
    char response[MAX_BUFFER_SIZE];

    if (sscanf(message, "UNREG %d %s %d", &net_id, ip_str, &tcp_port) == 3)
    {
        Network *net = find_or_create_network(net_id); // find_or_create para garantir que a rede existe
        if (net && remove_node_from_network(net, ip_str, tcp_port))
        {
            strcpy(response, "OKUNREG");
            printf("UNREG: Node %s:%d unregistered from net %03d.\n", ip_str, tcp_port, net_id);
        }
        else
        {
            strcpy(response, "ERROR: Could not unregister node or node not found");
            fprintf(stderr, "UNREG: Failed to unregister node %s:%d from net %03d\n", ip_str, tcp_port, net_id);
        }
        sendto(sock_fd, response, strlen(response), 0, (struct sockaddr *)client_addr, client_len);
    }
    else
    {
        fprintf(stderr, "Malformed UNREG message: %s\n", message);
    }
}

void handle_nodes_request(int sock_fd, struct sockaddr_in *client_addr, socklen_t client_len, const char *message)
{
    int net_id;
    char response[MAX_BUFFER_SIZE];
    char temp_buffer[MAX_BUFFER_SIZE];
    int offset = 0;

    if (sscanf(message, "NODES %d", &net_id) == 1)
    {
        // Inicia a resposta com o cabeçalho
        offset += snprintf(response + offset, sizeof(response) - offset, "NODESLIST %03d\n", net_id);

        Network *net = find_or_create_network(net_id);
        if (net)
        {
            for (int i = 0; i < MAX_NODES_PER_NET; i++)
            {
                if (net->nodes[i].is_valid)
                {
                    offset += snprintf(response + offset, sizeof(response) - offset, "%s %d\n",
                                       net->nodes[i].ip, net->nodes[i].tcp_port);
                    if (offset >= MAX_BUFFER_SIZE - 1)
                    { // Prevenção de overflow
                        fprintf(stderr, "Warning: NODESLIST response truncated due to buffer size limit.\n");
                        break;
                    }
                }
            }
        }
        // Garante que a string está terminada em null, mesmo se snprintf não o fizer no último char
        response[offset] = '\0';
        sendto(sock_fd, response, strlen(response), 0, (struct sockaddr *)client_addr, client_len);
        printf("NODES: Sent list for net %03d to %s:%d\n", net_id, inet_ntoa(client_addr->sin_addr), ntohs(client_addr->sin_port));
    }
    else
    {
        fprintf(stderr, "Malformed NODES request: %s\n", message);
    }
}

int main()
{
    int sock_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char buffer[MAX_BUFFER_SIZE];

    init_networks(); // Inicializa as estruturas de dados das redes

    // Criar socket UDP
    sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_fd < 0)
    {
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    // Configurar endereço do servidor
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // Escutar em todas as interfaces
    server_addr.sin_port = htons(REG_UDP_PORT);

    // Bind do socket ao endereço e porta
    if (bind(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Error binding socket");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }

    printf("Registration server listening on port %d\n", REG_UDP_PORT);

    while (1)
    {
        ssize_t bytes_received = recvfrom(sock_fd, buffer, MAX_BUFFER_SIZE - 1, 0,
                                          (struct sockaddr *)&client_addr, &client_len);
        if (bytes_received < 0)
        {
            perror("Error receiving data");
            continue;
        }
        buffer[bytes_received] = '\0'; // Null-terminate the received data

        printf("Received: '%s' from %s:%d\n", buffer, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        // Processar a mensagem
        if (strncmp(buffer, "REG", 3) == 0)
        {
            handle_reg_message(sock_fd, &client_addr, client_len, buffer);
        }
        else if (strncmp(buffer, "UNREG", 5) == 0)
        {
            handle_unreg_message(sock_fd, &client_addr, client_len, buffer);
        }
        else if (strncmp(buffer, "NODES", 5) == 0)
        {
            handle_nodes_request(sock_fd, &client_addr, client_len, buffer);
        }
        else
        {
            fprintf(stderr, "Unknown message type: %s\n", buffer);
            char response[] = "ERROR: Unknown command";
            sendto(sock_fd, response, strlen(response), 0, (struct sockaddr *)&client_addr, client_len);
        }
    }

    close(sock_fd);
    return 0;
}