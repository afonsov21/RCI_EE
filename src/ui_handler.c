#include "ui_handler.h"
#include "ndn_node.h"
#include "registration_protocol.h"
#include "topology_protocol.h"
#include "ndn_protocol.h" // Incluir o novo cabeçalho para as funções NDN
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <unistd.h> // Para usleep

// Implementação da função print_help()
void print_help()
{
    printf("Comandos disponíveis:\n");
    printf("  join (j) <net>        - Entrada do nó na rede\n");
    printf("  direct join (dj) <connectIP> <connectTCP> - Entrada direta na rede\n");
    printf("  create (c) <name>     - Criação de um objeto com nome\n");
    printf("  delete (dl) <name>    - Remoção do objeto com nome\n");
    printf("  retrieve (r) <name>   - Pesquisa do objeto com nome\n");
    printf("  show topology (st)    - Visualização dos vizinhos\n");
    printf("  show names (sn)       - Visualização dos nomes de objetos guardados\n");
    printf("  show interest table (si) - Visualização da tabela de interesses pendentes\n");
    printf("  leave (l)             - Saída do nó da rede\n");
    printf("  exit (x)              - Fecho da aplicação\n");
    printf("  help                  - Mostra esta ajuda\n");
}

void handle_user_command(char *command_line)
{
    char cmd[50];
    char arg1[101];
    int arg3;

    NDNNode *node = get_current_ndn_node();

    if (sscanf(command_line, "%s", cmd) == 1)
    {
        if (strcmp(cmd, "help") == 0)
        {
            print_help(); // Chamada para a função agora implementada
        }
        else if (strcmp(cmd, "join") == 0 || strcmp(cmd, "j") == 0)
        {
            int num_scanned = sscanf(command_line, "%*s %s", arg1);
            if (num_scanned == 1)
            {
                int net_id = atoi(arg1);
                if (net_id < 0 || net_id > 999)
                {
                    printf("Erro: ID de rede inválido. Deve ser entre 000 e 999.\n");
                    return;
                }
                if (node->current_net_id != -1 && node->current_net_id != net_id)
                {
                    printf("Erro: Nó já está na rede %03d. Saia antes de entrar em outra.\n", node->current_net_id);
                }
                else if (node->current_net_id == net_id)
                {
                    printf("Nó já está na rede %03d.\n", net_id);
                }
                else
                {
                    printf("Comando: join (rede: %03d)\n", net_id);
                    node->current_net_id = net_id;
                    send_nodes_request_message(node, net_id);
                }
            }
            else
            {
                printf("Uso: join (j) <net>\n");
            }
        }
        else if (strcmp(cmd, "direct") == 0)
        {
            char sub_cmd[50];
            int num_scanned = sscanf(command_line, "%*s %s %s %d", sub_cmd, arg1, &arg3);
            if (num_scanned == 3 && (strcmp(sub_cmd, "join") == 0 || strcmp(sub_cmd, "dj") == 0))
            {
                char *connect_ip = arg1;
                int connect_tcp = arg3;

                if (strcmp(connect_ip, "0.0.0.0") == 0 && connect_tcp == 0)
                {
                    printf("Comando: direct join (criando nova rede com este nó).\n");
                    if (node->current_net_id == -1)
                    {
                        node->current_net_id = 0; // Por omissão, o primeiro nó cria a rede 000
                        printf("Nó se registrando como primeiro na rede %03d.\n", node->current_net_id);
                        send_reg_message(node, node->current_net_id);
                    }
                    else
                    {
                        printf("Nó já está na rede %03d. Não é possível criar nova rede com 0.0.0.0.\n", node->current_net_id);
                    }
                }
                else
                {
                    printf("Comando: direct join (conectando a %s:%d).\n", connect_ip, connect_tcp);
                    int connected_sd = connect_to_node(node, connect_ip, connect_tcp);
                    if (connected_sd != -1)
                    {
                        printf("Conexão direta bem sucedida. Lembre-se de usar 'join <net>' para se registrar nesta rede.\n");
                    }
                    else
                    {
                        printf("Falha na conexão direta com %s:%d.\n", connect_ip, connect_tcp);
                    }
                }
            }
            else
            {
                printf("Uso: direct join (dj) <connectIP> <connectTCP>\n");
            }
        }
        else if (strcmp(cmd, "create") == 0 || strcmp(cmd, "c") == 0)
        {
            int num_scanned = sscanf(command_line, "%*s %s", arg1);
            if (num_scanned == 1)
            {
                char *name = arg1;
                create_local_object(node, name); // CHAMA FUNÇÃO NDN
            }
            else
            {
                printf("Uso: create (c) <name>\n");
            }
        }
        else if (strcmp(cmd, "delete") == 0 || strcmp(cmd, "dl") == 0)
        {
            int num_scanned = sscanf(command_line, "%*s %s", arg1);
            if (num_scanned == 1)
            {
                char *name = arg1;
                delete_local_object(node, name); // CHAMA FUNÇÃO NDN
            }
            else
            {
                printf("Uso: delete (dl) <name>\n");
            }
        }
        else if (strcmp(cmd, "retrieve") == 0 || strcmp(cmd, "r") == 0)
        {
            int num_scanned = sscanf(command_line, "%*s %s", arg1);
            if (num_scanned == 1)
            {
                char *name = arg1;
                initiate_retrieve(node, name); // CHAMA FUNÇÃO NDN
            }
            else
            {
                printf("Uso: retrieve (r) <name>\n");
            }
        }
        else if (strcmp(cmd, "show") == 0)
        {
            char sub_cmd[50];
            int num_scanned = sscanf(command_line, "%*s %s", sub_cmd);
            if (num_scanned == 1)
            {
                if (strcmp(sub_cmd, "topology") == 0 || strcmp(sub_cmd, "st") == 0)
                {
                    printf("Comando: show topology\n");
                    printf("  Nó atual: %s:%d\n", node->ip, node->tcp_port);
                    printf("  Rede atual: %s\n", (node->current_net_id != -1) ? "Registrado na rede " : "Não registrado em rede");
                    if (node->current_net_id != -1)
                    {
                        printf("  ID da Rede: %03d\n", node->current_net_id);
                    }
                    Neighbor *external = get_external_neighbor(node);
                    printf("  Vizinho Externo: %s:%d\n", external ? external->ip : "Nenhum", external ? external->tcp_port : 0);
                    printf("  Vizinhos Internos:\n");
                    int internal_count = 0;
                    for (int i = 0; i < MAX_NEIGHBORS; ++i)
                    {
                        if (node->neighbors[i].is_valid &&
                            (node->neighbors[i].type == NEIGHBOR_TYPE_INTERNAL || node->neighbors[i].type == NEIGHBOR_TYPE_EXTERNAL_AND_INTERNAL))
                        {
                            printf("    - %s:%d (SD: %d)\n", node->neighbors[i].ip, node->neighbors[i].tcp_port, node->neighbors[i].socket_sd);
                            internal_count++;
                        }
                    }
                    if (internal_count == 0)
                    {
                        printf("    (Nenhum)\n");
                    }
                }
                else if (strcmp(sub_cmd, "names") == 0 || strcmp(sub_cmd, "sn") == 0)
                {
                    printf("Comando: show names\n");
                    show_local_objects(node); // CHAMA FUNÇÃO NDN
                }
                else if (strcmp(sub_cmd, "interest") == 0)
                {
                    char next_arg[50];
                    sscanf(command_line, "%*s %*s %s", next_arg);
                    if (strcmp(next_arg, "table") == 0 || strcmp(next_arg, "si") == 0)
                    {
                        printf("Comando: show interest table\n");
                        show_interest_table(node); // CHAMA FUNÇÃO NDN
                    }
                    else
                    {
                        printf("Comando desconhecido: %s\n", command_line);
                    }
                }
                else
                {
                    printf("Comando desconhecido: %s\n", command_line);
                }
            }
            else
            {
                printf("Uso: show <topology|names|interest table> (st|sn|si)\n");
            }
        }
        else if (strcmp(cmd, "leave") == 0 || strcmp(cmd, "l") == 0)
        {
            if (node->current_net_id != -1)
            {
                printf("Comando: leave (rede: %03d)\n", node->current_net_id);

                node->is_leaving = 1;                       // Marcar que o nó está a sair
                node->internal_neighbors_to_disconnect = 0; // Resetar contador

                // Enviar mensagens LEAVE para todos os vizinhos internos
                for (int i = 0; i < MAX_NEIGHBORS; i++)
                {
                    if (node->neighbors[i].is_valid &&
                        (node->neighbors[i].type == NEIGHBOR_TYPE_INTERNAL || node->neighbors[i].type == NEIGHBOR_TYPE_EXTERNAL_AND_INTERNAL))
                    {
                        send_leave_message(node->neighbors[i].socket_sd, node);
                        node->internal_neighbors_to_disconnect++; // Contar quantos vizinhos internos precisam desconectar
                    }
                }

                send_unreg_message(node, node->current_net_id);
                node->current_net_id = -1;

                if (node->internal_neighbors_to_disconnect == 0)
                {
                    printf("Nó não tem vizinhos internos. Saída imediata da rede.\n");
                }
                else
                {
                    printf("Nó iniciando processo de saída. Aguardando desconexão de %d vizinhos internos.\n", node->internal_neighbors_to_disconnect);
                }
            }
            else
            {
                printf("Nó não está atualmente em nenhuma rede para sair.\n");
            }
        }
        else if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "x") == 0)
        {
            // A lógica de UNREG e fechamento de sockets está no ndn_node_cleanup()
        }
        else
        {
            printf("Comando desconhecido: %s\n", command_line);
        }
    }
    else
    {
        printf("Comando vazio ou inválido.\n");
    }
}