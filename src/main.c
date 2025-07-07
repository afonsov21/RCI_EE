#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ndn_node.h"

// Valores por omissão para o servidor de nós
#define DEFAULT_REG_IP "193.136.138.142"
#define DEFAULT_REG_UDP_PORT 59000

int main(int argc, char *argv[])
{
    if (argc < 3 || argc > 5)
    {
        fprintf(stderr, "Uso: %s <IP> <TCP> [regIP] [regUDP]\n", argv[0]);
        fprintf(stderr, "   IP: endereço IP da máquina do nó\n");
        fprintf(stderr, "   TCP: porto TCP de escuta do nó\n");
        fprintf(stderr, "   regIP: IP do servidor de nós (omissão: %s)\n", DEFAULT_REG_IP);
        fprintf(stderr, "   regUDP: porto UDP do servidor de nós (omissão: %d)\n", DEFAULT_REG_UDP_PORT);
        return EXIT_FAILURE;
    }

    char *node_ip = argv[1];
    int node_tcp_port = atoi(argv[2]);

    char *reg_ip = DEFAULT_REG_IP;
    int reg_udp_port = DEFAULT_REG_UDP_PORT;

    if (argc >= 4)
    {
        reg_ip = argv[3];
    }
    if (argc == 5)
    {
        reg_udp_port = atoi(argv[4]);
    }

    printf("Nó NDN iniciado com IP: %s, Porto TCP: %d\n", node_ip, node_tcp_port);
    printf("Servidor de Nós: IP: %s, Porto UDP: %d\n", reg_ip, reg_udp_port);

    ndn_node_init(node_ip, node_tcp_port, reg_ip, reg_udp_port);

    start_ndn_node_loop(); // Este loop bloqueia até o comando 'exit' ou erro

    // A limpeza é chamada dentro do start_ndn_node_loop() antes de sair
    // ou pode ser chamada aqui se o loop sair por outro motivo.
    // ndn_node_cleanup();

    return EXIT_SUCCESS;
}