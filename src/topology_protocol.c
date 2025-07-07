#include "topology_protocol.h"
#include "registration_protocol.h"
#include "ndn_protocol.h" // Necessário para process_ndn_message
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

// Funções de gestão de vizinhos (interno ao módulo)

/**
 * @brief Adiciona um novo vizinho à lista do nó.
 *
 * @param node Ponteiro para a estrutura NDNNode.
 * @param ip IP do vizinho.
 * @param port Porto TCP do vizinho.
 * @param sd Socket descriptor da conexão com o vizinho.
 * @param type Tipo de vizinho (EXTERNAL, INTERNAL, PENDING_INCOMING).
 * @return Índice do vizinho na lista, ou -1 se o limite foi atingido.
 */
int add_neighbor(NDNNode *node, const char *ip, int port, int sd, NeighborType type)
{
    if (node->num_active_neighbors >= MAX_NEIGHBORS)
    {
        fprintf(stderr, "Erro: Máximo de vizinhos atingido para o nó %s:%d. Não pode adicionar %s:%d.\n", node->ip, node->tcp_port, ip, port);
        close(sd); // Fecha o socket se não puder adicionar
        return -1;
    }

    // Procura por um slot vazio na lista de vizinhos
    for (int i = 0; i < MAX_NEIGHBORS; i++)
    {
        if (!node->neighbors[i].is_valid)
        {
            strncpy(node->neighbors[i].ip, ip, sizeof(node->neighbors[i].ip) - 1);
            node->neighbors[i].ip[sizeof(node->neighbors[i].ip) - 1] = '\0';
            node->neighbors[i].tcp_port = port;
            node->neighbors[i].socket_sd = sd;
            node->neighbors[i].type = type;
            node->neighbors[i].is_valid = 1;
            node->neighbors[i].recv_buffer_pos = 0;                                            // Inicializar a posição do buffer
            memset(node->neighbors[i].recv_buffer, 0, sizeof(node->neighbors[i].recv_buffer)); // Limpar o buffer

            node->num_active_neighbors++;
            printf("Vizinho adicionado: %s:%d (Tipo: %s, SD: %d). Total: %d\n",
                   ip, port,
                   (type == NEIGHBOR_TYPE_EXTERNAL ? "Externo" : (type == NEIGHBOR_TYPE_INTERNAL ? "Interno" : "Pendente")),
                   sd, node->num_active_neighbors);
            return i; // Retorna o índice do vizinho
        }
    }
    return -1; // Não encontrou slot vazio (não deveria acontecer se a contagem estiver certa)
}

/**
 * @brief Remove um vizinho da lista do nó pelo seu socket descriptor.
 *
 * @param node Ponteiro para a estrutura NDNNode.
 * @param sd Socket descriptor do vizinho a remover.
 */
void remove_neighbor(NDNNode *node, int sd)
{
    for (int i = 0; i < MAX_NEIGHBORS; i++)
    {
        if (node->neighbors[i].is_valid && node->neighbors[i].socket_sd == sd)
        {
            printf("Removendo vizinho SD: %d (%s:%d).\n", sd, node->neighbors[i].ip, node->neighbors[i].tcp_port);
            close(sd); // Fechar o socket do vizinho
            node->neighbors[i].is_valid = 0;
            node->neighbors[i].socket_sd = -1;                                                 // Invalidar SD
            node->neighbors[i].type = NEIGHBOR_TYPE_NONE;                                      // Resetar tipo
            node->neighbors[i].recv_buffer_pos = 0;                                            // Limpar a posição do buffer
            memset(node->neighbors[i].recv_buffer, 0, sizeof(node->neighbors[i].recv_buffer)); // Limpar o buffer

            node->num_active_neighbors--;
            printf("Vizinho removido. Total: %d\n", node->num_active_neighbors);
            return;
        }
    }
    printf("Vizinho com SD %d não encontrado para remoção.\n", sd);
}

/**
 * @brief Encontra um vizinho na lista pelo seu socket descriptor.
 *
 * @param node Ponteiro para a estrutura NDNNode.
 * @param sd Socket descriptor do vizinho.
 * @return Ponteiro para a estrutura Neighbor, ou NULL se não encontrado.
 */
Neighbor *find_neighbor_by_sd(NDNNode *node, int sd)
{
    for (int i = 0; i < MAX_NEIGHBORS; i++)
    {
        if (node->neighbors[i].is_valid && node->neighbors[i].socket_sd == sd)
        {
            return &node->neighbors[i];
        }
    }
    return NULL;
}

/**
 * @brief Encontra um vizinho na lista pelo seu endereço IP e porto TCP.
 *
 * @param node Ponteiro para a estrutura NDNNode.
 * @param ip IP do vizinho.
 * @param port Porto TCP do vizinho.
 * @return Ponteiro para a estrutura Neighbor, ou NULL se não encontrado.
 */
Neighbor *find_neighbor_by_addr(NDNNode *node, const char *ip, int port)
{
    for (int i = 0; i < MAX_NEIGHBORS; i++)
    {
        if (node->neighbors[i].is_valid && strcmp(node->neighbors[i].ip, ip) == 0 && node->neighbors[i].tcp_port == port)
        {
            return &node->neighbors[i];
        }
    }
    return NULL;
}

/**
 * @brief Obtém o vizinho externo do nó.
 *
 * @param node Ponteiro para a estrutura NDNNode.
 * @return Ponteiro para a estrutura Neighbor do vizinho externo, ou NULL se não houver.
 */
Neighbor *get_external_neighbor(NDNNode *node)
{
    for (int i = 0; i < MAX_NEIGHBORS; i++)
    {
        if (node->neighbors[i].is_valid && node->neighbors[i].type == NEIGHBOR_TYPE_EXTERNAL)
        {
            return &node->neighbors[i];
        }
    }
    return NULL; // Nenhum vizinho externo encontrado
}

// Funções para conexão

/**
 * @brief Tenta conectar-se a um nó alvo via TCP e adiciona-o como vizinho EXTERNAL.
 *
 * @param node Ponteiro para a estrutura NDNNode.
 * @param target_ip IP do nó alvo.
 * @param target_tcp_port Porto TCP do nó alvo.
 * @return O socket descriptor da conexão estabelecida, ou -1 em caso de erro.
 */
int connect_to_node(NDNNode *node, const char *target_ip, int target_tcp_port)
{
    // Primeiro, verifica se já é vizinho (pode ser interno ou pendente)
    Neighbor *existing_neighbor = find_neighbor_by_addr(node, target_ip, target_tcp_port);
    if (existing_neighbor && existing_neighbor->is_valid)
    {
        printf("Já conectado a %s:%d. Reutilizando conexão existente (SD: %d).\n", target_ip, target_tcp_port, existing_neighbor->socket_sd);
        // Se já conectado, não precisa fazer um novo 'connect'.
        // A lógica de ENTRY/LEAVE cuidará do tipo (EXTERNAL/INTERNAL) se necessário.
        return existing_neighbor->socket_sd;
    }

    int client_sd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_sd == -1)
    {
        perror("Erro ao criar socket cliente TCP");
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(target_tcp_port);
    if (inet_pton(AF_INET, target_ip, &server_addr.sin_addr) <= 0)
    {
        perror("Erro em inet_pton para IP alvo");
        close(client_sd);
        return -1;
    }

    printf("Tentando conectar a %s:%d...\n", target_ip, target_tcp_port);
    if (connect(client_sd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        if (errno == ECONNREFUSED)
        {
            fprintf(stderr, "Conexão recusada por %s:%d. Nó pode não estar ativo ou porta errada.\n", target_ip, target_tcp_port);
        }
        else
        {
            perror("Erro ao conectar ao nó alvo");
        }
        close(client_sd);
        return -1;
    }

    printf("Conexão estabelecida com %s:%d (SD: %d).\n", target_ip, target_tcp_port, client_sd);
    // Adicionar o nó como vizinho EXTERNAL e enviar mensagem de entrada
    // O porto e IP aqui são os do TARGET, que é o que queremos armazenar.
    int neighbor_idx = add_neighbor(node, target_ip, target_tcp_port, client_sd, NEIGHBOR_TYPE_EXTERNAL);
    if (neighbor_idx != -1)
    {
        send_entry_message(client_sd, node); // Envia ENTRY para o nó conectado
        return client_sd;
    }
    else
    {
        close(client_sd);
        return -1;
    }
}

/**
 * @brief Processa uma nova conexão TCP de entrada (chamada por accept no ndn_node.c).
 * Adiciona o vizinho como PENDING_INCOMING.
 *
 * @param node Ponteiro para a estrutura NDNNode.
 * @param new_socket_sd O socket descriptor da nova conexão aceita.
 * @param client_ip O IP de origem da conexão (temporário).
 * @param client_port O porto de origem da conexão (temporário).
 */
void process_incoming_connection(NDNNode *node, int new_socket_sd, const char *client_ip, int client_port)
{
    printf("Processando nova conexão de entrada de %s:%d (SD: %d).\n", client_ip, client_port, new_socket_sd);

    // Adicionar como vizinho PENDING_INCOMING com IP/porta temporários (do socket de origem).
    // O IP e porta reais do nó serão obtidos da mensagem ENTRY.
    // Primeiro, verificar se já temos uma conexão ACEITA (PENDING_INCOMING) para este SD.
    // ou se já é um vizinho (INTERNAL/EXTERNAL) - isso pode indicar um problema de race condition
    // ou dupla conexão, onde um lado já se conectou.
    Neighbor *existing_sd_neighbor = find_neighbor_by_sd(node, new_socket_sd);
    if (existing_sd_neighbor && existing_sd_neighbor->is_valid)
    {
        fprintf(stderr, "Aviso: Nova conexão aceita (SD: %d) já existe como vizinho. Pode ser reabertura ou erro.\n", new_socket_sd);
        close(new_socket_sd); // Fechar a duplicata
        return;
    }

    // Adicionar o vizinho com o tipo PENDING_INCOMING. IP/Porta serão atualizados pela ENTRY.
    add_neighbor(node, client_ip, client_port, new_socket_sd, NEIGHBOR_TYPE_PENDING_INCOMING);
}

// Funções de envio de mensagens de topologia

/**
 * @brief Envia uma mensagem ENTRY para um vizinho.
 *
 * @param target_sd Socket descriptor do vizinho alvo.
 * @param node Ponteiro para a estrutura NDNNode (remetente).
 */
void send_entry_message(int target_sd, NDNNode *node)
{
    char message[MAX_TCP_MSG_LEN];
    snprintf(message, sizeof(message), "ENTRY %s %d\n", node->ip, node->tcp_port);
    printf("Enviando ENTRY message para SD %d: '%s'", target_sd, message);
    if (write(target_sd, message, strlen(message)) == -1)
    {
        perror("Erro ao enviar mensagem ENTRY");
        remove_neighbor(node, target_sd);
    }
}

/**
 * @brief Envia uma mensagem LEAVE para um vizinho.
 *
 * @param target_sd Socket descriptor do vizinho alvo.
 * @param node Ponteiro para a estrutura NDNNode (remetente).
 */
void send_leave_message(int target_sd, NDNNode *node)
{
    char message[MAX_TCP_MSG_LEN];
    Neighbor *external = get_external_neighbor(node);
    if (external)
    {
        snprintf(message, sizeof(message), "LEAVE %s %d\n", external->ip, external->tcp_port);
    }
    else
    {
        snprintf(message, sizeof(message), "LEAVE %s %d\n", node->ip, node->tcp_port);
    }

    printf("Enviando LEAVE message para SD %d: '%s'", target_sd, message);
    if (write(target_sd, message, strlen(message)) == -1)
    {
        perror("Erro ao enviar mensagem LEAVE");
        remove_neighbor(node, target_sd);
    }
}

// Funções para lidar com buffers de receção e parsing de mensagens TCP

/**
 * @brief Processa dados TCP brutos recebidos de um socket de vizinho.
 * Acumula no buffer de receção e extrai mensagens completas.
 *
 * @param node Ponteiro para a estrutura NDNNode.
 * @param client_sd Socket descriptor de onde os dados foram recebidos.
 * @param data Dados brutos recebidos.
 * @param len Tamanho dos dados brutos.
 */
void handle_tcp_data_received(NDNNode *node, int client_sd, char *data, ssize_t len)
{
    Neighbor *neighbor = find_neighbor_by_sd(node, client_sd);
    if (!neighbor)
    {
        fprintf(stderr, "Erro: Dados recebidos de SD %d, mas vizinho não encontrado.\n", client_sd);
        return;
    }

    if (neighbor->recv_buffer_pos + len >= MAX_TCP_RECV_BUFFER_SIZE)
    {
        fprintf(stderr, "Erro: Buffer de receção de vizinho SD %d cheio. Descartando dados.\n", client_sd);
        neighbor->recv_buffer_pos = 0; // Resetar para evitar estouro contínuo
        return;
    }
    memcpy(neighbor->recv_buffer + neighbor->recv_buffer_pos, data, len);
    neighbor->recv_buffer_pos += len;
    neighbor->recv_buffer[neighbor->recv_buffer_pos] = '\0';

    char *msg_start = neighbor->recv_buffer;
    char *msg_end;

    while ((msg_end = strchr(msg_start, '\n')) != NULL)
    {
        *msg_end = '\0';

        if (strlen(msg_start) > 0)
        {
            process_complete_tcp_message(node, client_sd, msg_start);
        }

        msg_start = msg_end + 1;
    }

    if (msg_start != neighbor->recv_buffer)
    {
        int remaining_len = strlen(msg_start);
        memmove(neighbor->recv_buffer, msg_start, remaining_len + 1);
        neighbor->recv_buffer_pos = remaining_len;
    }
}

/**
 * @brief Processa uma mensagem TCP completa (já extraída do buffer).
 * Encaminha para o handler de topologia ou handler NDN.
 *
 * @param node Ponteiro para a estrutura NDNNode.
 * @param client_sd Socket descriptor de onde a mensagem foi recebida.
 * @param message Mensagem completa (string null-terminated).
 */
void process_complete_tcp_message(NDNNode *node, int client_sd, const char *message)
{
    printf("Mensagem TCP completa de SD %d: '%s'\n", client_sd, message);

    char cmd[20];
    if (sscanf(message, "%s", cmd) == 1)
    {
        // Mensagens de Topologia
        if (strcmp(cmd, "ENTRY") == 0 || strcmp(cmd, "LEAVE") == 0)
        {
            char ip_str[MAX_IP_LEN];
            int tcp_port;
            if (sscanf(message, "%*s %s %d", ip_str, &tcp_port) == 2)
            {
                if (strcmp(cmd, "ENTRY") == 0)
                {
                    printf("  -> Processando ENTRY de %s:%d\n", ip_str, tcp_port);

                    Neighbor *neighbor_conn = find_neighbor_by_sd(node, client_sd);
                    if (neighbor_conn && neighbor_conn->is_valid)
                    {
                        // Se já existia (como PENDING_INCOMING), atualiza os dados e o tipo
                        if (neighbor_conn->type == NEIGHBOR_TYPE_PENDING_INCOMING)
                        {
                            printf("    Atualizando vizinho pendente SD %d para %s:%d (de ENTRY).\n", client_sd, ip_str, tcp_port);
                            strncpy(neighbor_conn->ip, ip_str, sizeof(neighbor_conn->ip) - 1);
                            neighbor_conn->ip[sizeof(neighbor_conn->ip) - 1] = '\0';
                            neighbor_conn->tcp_port = tcp_port;
                        }

                        // Define o tipo final como INTERNAL (ou EXTERNAL se promovido)
                        neighbor_conn->type = NEIGHBOR_TYPE_INTERNAL;

                        // Adicionalmente, se o nó não tiver vizinho externo, estabelece este como externo.
                        if (!get_external_neighbor(node))
                        {
                            printf("    Nó não tinha vizinho externo. %s:%d (SD: %d) set como EXTERNAL.\n", ip_str, tcp_port, client_sd);
                            neighbor_conn->type = NEIGHBOR_TYPE_EXTERNAL;
                        }
                    }
                    else
                    {
                        // Isso não deveria acontecer se process_incoming_connection sempre adicionar um placeholder.
                        // Mas, se acontecer, adiciona como vizinho interno
                        int idx = add_neighbor(node, ip_str, tcp_port, client_sd, NEIGHBOR_TYPE_INTERNAL);
                        if (idx != -1 && !get_external_neighbor(node))
                        {
                            node->neighbors[idx].type = NEIGHBOR_TYPE_EXTERNAL;
                            printf("    Nó não tinha vizinho externo. %s:%d (SD: %d) set como EXTERNAL.\n", ip_str, tcp_port, client_sd);
                        }
                    }
                }
                else if (strcmp(cmd, "LEAVE") == 0)
                {
                    printf("  -> Processando LEAVE de vizinho (SD: %d). Vizinho externo deles: %s:%d\n", client_sd, ip_str, tcp_port);

                    // Remover o vizinho que enviou LEAVE
                    Neighbor *old_external = get_external_neighbor(node);
                    // Se o remetente do LEAVE era o vizinho externo deste nó, então o removemos.
                    if (old_external && old_external->socket_sd == client_sd)
                    {
                        printf("  Meu vizinho externo %s:%d (SD: %d) saiu. Removido.\n", old_external->ip, old_external->tcp_port, client_sd);
                        remove_neighbor(node, client_sd);
                    }
                    else
                    { // O vizinho que saiu era interno
                        printf("  Vizinho interno %s:%d (SD: %d) saiu. Removido.\n", ip_str, tcp_port, client_sd);
                        remove_neighbor(node, client_sd);
                    }

                    // Analisa o identificador contido na mensagem LEAVE (vizinho externo do REMETENTE do LEAVE)
                    // Se não for o próprio nó local, tentar conectar a ele como novo vizinho externo.
                    if (strcmp(ip_str, node->ip) != 0 || tcp_port != node->tcp_port)
                    {
                        printf("  O vizinho externo do nó que saiu era %s:%d. Tentando conectar a ele como novo externo...\n", ip_str, tcp_port);
                        Neighbor *potential_new_external = find_neighbor_by_addr(node, ip_str, tcp_port);
                        if (potential_new_external)
                        {
                            if (potential_new_external->type == NEIGHBOR_TYPE_INTERNAL)
                            {
                                printf("  Vizinho externo do nó que saiu (%s:%d) já é meu vizinho interno. Promovendo a externo.\n", ip_str, tcp_port);
                                potential_new_external->type = NEIGHBOR_TYPE_EXTERNAL;
                            } // Se já é externo, nenhuma ação
                        }
                        else
                        {
                            // Se não estava conectado, tenta iniciar uma nova conexão TCP e adiciona como EXTERNAL
                            int new_sd = connect_to_node(node, ip_str, tcp_port);
                            if (new_sd != -1)
                            {
                                printf("  Conectado com sucesso a %s:%d como novo vizinho externo.\n", ip_str, tcp_port);
                            }
                            else
                            {
                                printf("  Falha ao conectar a %s:%d para ser novo vizinho externo.\n", ip_str, tcp_port);
                            }
                        }
                    }
                    else
                    {
                        // O vizinho externo do nó que saiu era o próprio nó local.
                        // Promover um vizinho interno existente a externo.
                        printf("  O vizinho externo do nó que saiu era EU. Promovendo um vizinho interno a externo.\n");
                        Neighbor *promoted_neighbor = NULL;
                        for (int i = 0; i < MAX_NEIGHBORS; i++)
                        {
                            if (node->neighbors[i].is_valid && node->neighbors[i].type == NEIGHBOR_TYPE_INTERNAL)
                            {
                                promoted_neighbor = &node->neighbors[i];
                                break;
                            }
                        }
                        if (promoted_neighbor)
                        {
                            promoted_neighbor->type = NEIGHBOR_TYPE_EXTERNAL;
                            printf("  Nó %s:%d (SD: %d) promovido a vizinho externo.\n", promoted_neighbor->ip, promoted_neighbor->tcp_port, promoted_neighbor->socket_sd);
                        }
                        else
                        {
                            printf("  Nenhum vizinho interno para promover a externo. Nó agora está isolado ou é o único.\n");
                        }
                    }
                }
            }
            else
            {
                fprintf(stderr, "Erro de parsing na mensagem TCP de topologia: %s\n", message);
            }
        }
        // Mensagens NDN (INTEREST, OBJECT, NOOBJECT)
        else if (strcmp(cmd, "INTEREST") == 0 || strcmp(cmd, "OBJECT") == 0 || strcmp(cmd, "NOOBJECT") == 0)
        {
            process_ndn_message(node, client_sd, message); // Encaminha para o módulo NDN
        }
        else
        {
            fprintf(stderr, "Tipo de mensagem TCP desconhecido: '%s' (de SD %d)\n", cmd, client_sd);
        }
    }
    else
    {
        fprintf(stderr, "Mensagem TCP recebida vazia ou mal formatada para comando: '%s' (de SD %d)\n", message, client_sd);
    }
}