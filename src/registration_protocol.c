#include "registration_protocol.h"
#include "topology_protocol.h" // Necessário para connect_to_node
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h> // Para srand, rand

// Para a lógica de escolher um nó aleatoriamente
static int nodes_count = 0;
static char nodes_ip_list[MAX_NODES_PER_NET][MAX_IP_LEN]; // MAX_NODES_PER_NET deve ser definido ou usar um tamanho fixo
static int nodes_port_list[MAX_NODES_PER_NET];            // MAX_NODES_PER_NET deve ser definido ou usar um tamanho fixo

void send_reg_message(NDNNode *node, int net_id)
{
    char message[MAX_UDP_MSG_LEN];
    snprintf(message, sizeof(message), "REG %03d %s %d", net_id, node->ip, node->tcp_port);

    ssize_t bytes_sent = sendto(node->udp_reg_sd, message, strlen(message), 0,
                                (struct sockaddr *)&node->reg_server_addr, sizeof(node->reg_server_addr));
    if (bytes_sent == -1)
    {
        perror("Erro ao enviar mensagem REG UDP");
    }
}

void send_unreg_message(NDNNode *node, int net_id)
{
    char message[MAX_UDP_MSG_LEN];
    snprintf(message, sizeof(message), "UNREG %03d %s %d", net_id, node->ip, node->tcp_port);

    ssize_t bytes_sent = sendto(node->udp_reg_sd, message, strlen(message), 0,
                                (struct sockaddr *)&node->reg_server_addr, sizeof(node->reg_server_addr));
    if (bytes_sent == -1)
    {
        perror("Erro ao enviar mensagem UNREG UDP");
    }
}

void send_nodes_request_message(NDNNode *node, int net_id)
{
    char message[MAX_UDP_MSG_LEN];
    snprintf(message, sizeof(message), "NODES %03d", net_id);

    ssize_t bytes_sent = sendto(node->udp_reg_sd, message, strlen(message), 0,
                                (struct sockaddr *)&node->reg_server_addr, sizeof(node->reg_server_addr));
    if (bytes_sent == -1)
    {
        perror("Erro ao enviar mensagem NODES UDP");
    }
}

void process_udp_registration_message(NDNNode *node, const char *message)
{

    char cmd[20];
    int net_id;

    if (sscanf(message, "%s %d", cmd, &net_id) >= 1)
    {
        if (strcmp(cmd, "OKREG") == 0)
        {
            printf("Servidor de registo confirmou o registo para rede %03d.\n", node->current_net_id);
        }
        else if (strcmp(cmd, "OKUNREG") == 0)
        {
            printf("Servidor de registo confirmou a remoção do registo para rede %03d.\n", net_id);
        }
        else if (strcmp(cmd, "NODESLIST") == 0)
        {
            printf("Lista de Nós recebida. Rede ID: %03d\n", net_id);
            // Resetar a lista de nós temporária
            nodes_count = 0;

            char *line_start = strchr(message, '\n');
            if (line_start)
            {
                line_start++; // Pular o '\n'
                char ip_str[MAX_IP_LEN];
                int port_num;
                while (nodes_count < MAX_NODES_PER_NET && sscanf(line_start, "%s %d", ip_str, &port_num) == 2)
                {
                    // Ignorar o próprio nó na lista
                    if (!(strcmp(ip_str, node->ip) == 0 && port_num == node->tcp_port))
                    {
                        strncpy(nodes_ip_list[nodes_count], ip_str, MAX_IP_LEN - 1);
                        nodes_ip_list[nodes_count][MAX_IP_LEN - 1] = '\0';
                        nodes_port_list[nodes_count] = port_num;
                        nodes_count++;
                    }
                    line_start = strchr(line_start, '\n');
                    if (line_start)
                        line_start++;
                    else
                        break;
                }
            }

            if (nodes_count == 0)
            {
                printf("  Lista de nós na rede %03d está vazia. Este nó é o primeiro da rede.\n", net_id);
                send_reg_message(node, net_id);
            }
            else // nodes_count > 0
            {
                printf("  Total de %d outros nós na rede %03d. Escolhendo um para conectar...\n", nodes_count, net_id);

                srand(time(NULL)); // Inicializar o gerador de números aleatórios
                int random_idx = rand() % nodes_count;

                char *target_ip = nodes_ip_list[random_idx];
                int target_port = nodes_port_list[random_idx];

                printf("  Tentando conectar a %s:%d para se juntar à rede %03d.\n", target_ip, target_port, net_id);

                // Determina se este é o cenário de 2 nós (apenas um outro nó na lista)
                // O is_two_node_network_scenario é usado aqui para decidir o tipo de vizinho.
                int is_two_node_network_scenario = (nodes_count == 1);

                int connected_sd = connect_to_node(node, target_ip, target_port);
                if (connected_sd != -1)
                {
                    printf("  Conectado ao nó %s:%d. Registrando-se na rede %03d.\n", target_ip, target_port, net_id);

                    // Se este é o cenário de 2 nós, classifique o vizinho recém-conectado como EXTERNAL_AND_INTERNAL
                    if (is_two_node_network_scenario)
                    {
                        Neighbor *just_connected_neighbor = find_neighbor_by_sd(node, connected_sd);
                        if (just_connected_neighbor)
                        {
                            just_connected_neighbor->type = NEIGHBOR_TYPE_EXTERNAL_AND_INTERNAL;
                            printf("    Nó conectado %s:%d classificado como EXTERNAL_AND_INTERNAL (cenário 2 nós, iniciador).\n", target_ip, target_port);
                        }
                    }
                    send_reg_message(node, net_id);
                }
                else
                {
                    fprintf(stderr, "  Falha ao conectar ao nó %s:%d. Tentando outro ou falhando o join.\n", target_ip, target_port);
                    node->current_net_id = -1;
                }
            }
        }
        else
        {
            printf("Mensagem UDP desconhecida ou mal formatada: %s\n", message);
        }
    }
    else
    {
        printf("Mensagem UDP incompleta ou mal formatada: %s\n", message);
    }
}