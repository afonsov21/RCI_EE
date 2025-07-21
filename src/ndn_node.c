#include "ndn_node.h"
#include "ui_handler.h"
#include "registration_protocol.h"
#include "topology_protocol.h"
#include "ndn_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

static NDNNode current_node;

NDNNode *get_current_ndn_node()
{
    return &current_node;
}

void ndn_node_init(const char *ip, int tcp_port, const char *reg_ip, int reg_udp_port)
{
    strncpy(current_node.ip, ip, sizeof(current_node.ip) - 1);
    current_node.ip[sizeof(current_node.ip) - 1] = '\0';
    current_node.tcp_port = tcp_port;
    strncpy(current_node.reg_ip, reg_ip, sizeof(current_node.reg_ip) - 1);
    current_node.reg_ip[sizeof(current_node.reg_ip) - 1] = '\0';
    current_node.reg_udp_port = reg_udp_port;
    current_node.current_net_id = -1; // Inicializa sem rede

    // Inicializar vizinhos
    current_node.num_active_neighbors = 0;
    for (int i = 0; i < MAX_NEIGHBORS; i++)
    {
        current_node.neighbors[i].is_valid = 0;
        current_node.neighbors[i].socket_sd = -1;
        current_node.neighbors[i].type = NEIGHBOR_TYPE_NONE;
        current_node.neighbors[i].recv_buffer_pos = 0;
        memset(current_node.neighbors[i].recv_buffer, 0, sizeof(current_node.neighbors[i].recv_buffer));
    }

    current_node.is_leaving = 0;                       // Inicializa como não estando a sair
    current_node.internal_neighbors_to_disconnect = 0; // Nenhum para desconectar inicialmente

    // Inicializar estruturas NDN
    init_local_objects(&current_node);     // Chamar a função de inicialização
    init_cache(&current_node);             // Chamar a função de inicialização
    init_pending_interests(&current_node); // Chamar a função de inicialização
    // num_local_objects, num_cached_objects, num_pending_interests são inicializados dentro das respectivas init_* funções

    // 1. Inicializar Socket TCP de Escuta (Servidor TCP)
    current_node.tcp_listen_sd = socket(AF_INET, SOCK_STREAM, 0);
    if (current_node.tcp_listen_sd == -1)
    {
        perror("Erro ao criar socket TCP de escuta");
        exit(EXIT_FAILURE);
    }
    printf("Socket TCP de escuta criado.\n");

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(current_node.tcp_port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Escutar em todas as interfaces

    int optval = 1;
    if (setsockopt(current_node.tcp_listen_sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1)
    {
        perror("Erro em setsockopt SO_REUSEADDR para TCP");
        close(current_node.tcp_listen_sd);
        exit(EXIT_FAILURE);
    }

    if (bind(current_node.tcp_listen_sd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("Erro ao fazer bind do socket TCP");
        close(current_node.tcp_listen_sd);
        exit(EXIT_FAILURE);
    }
    printf("Bind do socket TCP efetuado na porta %d.\n", current_node.tcp_port);

    if (listen(current_node.tcp_listen_sd, 5) == -1)
    {
        perror("Erro ao iniciar listen no socket TCP");
        close(current_node.tcp_listen_sd);
        exit(EXIT_FAILURE);
    }
    printf("Socket TCP a escutar por conexões.\n");

    // 2. Inicializar Socket UDP (Cliente UDP para o servidor de registo)
    current_node.udp_reg_sd = socket(AF_INET, SOCK_DGRAM, 0);
    if (current_node.udp_reg_sd == -1)
    {
        perror("Erro ao criar socket UDP");
        close(current_node.tcp_listen_sd);
        exit(EXIT_FAILURE);
    }
    printf("Socket UDP criado.\n");

    // Configurar o endereço do servidor de registo
    memset(&current_node.reg_server_addr, 0, sizeof(current_node.reg_server_addr));
    current_node.reg_server_addr.sin_family = AF_INET;
    current_node.reg_server_addr.sin_port = htons(current_node.reg_udp_port);
    if (inet_pton(AF_INET, current_node.reg_ip, &current_node.reg_server_addr.sin_addr) <= 0)
    {
        perror("Erro em inet_pton para o IP do servidor de registo");
        close(current_node.tcp_listen_sd);
        close(current_node.udp_reg_sd);
        exit(EXIT_FAILURE);
    }
    printf("Endereço do servidor de registo configurado.\n");

    printf("Nó NDN inicializado.\n");
}

void start_ndn_node_loop()
{
    fd_set read_fds;
    int max_fd;
    NDNNode *node = get_current_ndn_node();
    char temp_read_buffer[MAX_TCP_MSG_LEN]; // Buffer temporário para ler do socket


    while (1)
    {
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(node->tcp_listen_sd, &read_fds);
        FD_SET(node->udp_reg_sd, &read_fds);

        max_fd = STDIN_FILENO;
        if (node->tcp_listen_sd > max_fd)
        {
            max_fd = node->tcp_listen_sd;
        }
        if (node->udp_reg_sd > max_fd)
        {
            max_fd = node->udp_reg_sd;
        }

        // Adicionar sockets de vizinhos ativos ao conjunto de monitoramento
        for (int i = 0; i < MAX_NEIGHBORS; i++)
        {
            if (node->neighbors[i].is_valid && node->neighbors[i].socket_sd != -1)
            {
                FD_SET(node->neighbors[i].socket_sd, &read_fds);
                if (node->neighbors[i].socket_sd > max_fd)
                {
                    max_fd = node->neighbors[i].socket_sd;
                }
            }
        }

        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);

        if (activity < 0)
        {
            if (errno == EINTR)
                continue;
            perror("select error");
            break;
        }

        // 1. Lidar com comandos do utilizador (STDIN)
        if (FD_ISSET(STDIN_FILENO, &read_fds))
        {
            char command_line[256];
            if (fgets(command_line, sizeof(command_line), stdin) != NULL)
            {
                command_line[strcspn(command_line, "\n")] = 0;
                handle_user_command(command_line); // Chamará 'leave' ou 'exit'
                // Se 'exit' for digitado, o loop principal é quebrado aqui.
                if (strcmp(command_line, "exit") == 0 || strcmp(command_line, "x") == 0)
                {
                    printf("Encerrando a aplicação (comando 'exit').\n");
                    break;
                }
            }
        }

        // 2. Lidar com novas conexões TCP
        if (FD_ISSET(node->tcp_listen_sd, &read_fds))
        {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int new_socket_sd = accept(node->tcp_listen_sd, (struct sockaddr *)&client_addr, &client_len);
            if (new_socket_sd == -1)
            {
                perror("Erro ao aceitar conexão TCP");
            }
            else
            {
                char client_ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
                process_incoming_connection(node, new_socket_sd, client_ip, ntohs(client_addr.sin_port));
            }
        }

        // 3. Lidar com dados UDP do servidor de registo
        if (FD_ISSET(node->udp_reg_sd, &read_fds))
        {
            char buffer[MAX_UDP_MSG_LEN];
            struct sockaddr_in sender_addr;
            socklen_t sender_len = sizeof(sender_addr);
            ssize_t bytes_received = recvfrom(node->udp_reg_sd, buffer, sizeof(buffer) - 1, 0,
                                              (struct sockaddr *)&sender_addr, &sender_len);
            if (bytes_received == -1)
            {
                perror("Erro ao receber dados UDP");
            }
            else
            {
                buffer[bytes_received] = '\0';
                process_udp_registration_message(node, buffer);
            }
        }

        // 4. Lidar com dados recebidos de vizinhos TCP existentes (e fechos de conexão)
        for (int i = 0; i < MAX_NEIGHBORS; i++)
        {
            if (node->neighbors[i].is_valid && node->neighbors[i].socket_sd != -1 &&
                FD_ISSET(node->neighbors[i].socket_sd, &read_fds))
            {

                ssize_t bytes_received = read(node->neighbors[i].socket_sd, temp_read_buffer, sizeof(temp_read_buffer));
                if (bytes_received <= 0)
                {
                    // Conexão fechada ou erro
                    if (bytes_received == 0)
                    {
                        
                    }
                    else
                    {
                        perror("Erro ao ler de vizinho TCP");
                    }

                    // Se o nó está a sair e este vizinho é interno, decrementa o contador
                    if (node->is_leaving && (node->neighbors[i].type == NEIGHBOR_TYPE_INTERNAL || node->neighbors[i].type == NEIGHBOR_TYPE_EXTERNAL_AND_INTERNAL))
                    {
                        node->internal_neighbors_to_disconnect--;
                    }

                    remove_neighbor(node, node->neighbors[i].socket_sd); // Remover o vizinho
                }
                else
                {
                    handle_tcp_data_received(node, node->neighbors[i].socket_sd, temp_read_buffer, bytes_received);
                }
            }
        }

        // Se o nó está a sair e todos os vizinhos internos desconectaram, sair do loop
        if (node->is_leaving && node->internal_neighbors_to_disconnect <= 0)
        {
            break;
        }
    }

    ndn_node_cleanup();
}

void ndn_node_cleanup()
{
    NDNNode *node = get_current_ndn_node();
    // A lógica de UNREG ao sair está no ui_handler para 'leave' e 'exit'
    // Se o nó estava a sair por 'leave', já enviou UNREG.
    // Se o nó saiu por 'exit' e ainda está numa rede, deve desregistar.
    if (!node->is_leaving && node->current_net_id != -1)
    { // Só se desregista se não estiver já em processo de saída por 'leave'
        send_unreg_message(node, node->current_net_id);
        usleep(100000); // 100 ms
    }

    // Fechar todos os sockets de vizinhos TCP ativos (sem enviar LEAVE, já foi feito ou não é necessário)
    for (int i = 0; i < MAX_NEIGHBORS; i++)
    {
        if (node->neighbors[i].is_valid && node->neighbors[i].socket_sd != -1)
        {
            close(node->neighbors[i].socket_sd);
        }
    }

    if (node->tcp_listen_sd != -1)
    {
        close(node->tcp_listen_sd);
        printf("Socket TCP de escuta fechado.\n");
    }
    if (node->udp_reg_sd != -1)
    {
        close(node->udp_reg_sd);
        printf("Socket UDP de registo fechado.\n");
    }
    printf("Recursos do nó NDN limpos.\n");
}