#include "ndn_protocol.h"
#include "topology_protocol.h" // Para enviar mensagens a vizinhos
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>   // Para time() em srand() e last_access_time
#include <unistd.h> // Para write() em sockets, STDIN_FILENO

// Helper function: Inicializa a tabela de interesses pendentes
void init_pending_interests(NDNNode *node)
{
    for (int i = 0; i < MAX_PENDING_INTERESTS; i++)
    {
        node->pending_interests[i].is_valid = 0;
        node->pending_interests[i].num_active_interfaces = 0;
        for (int j = 0; j < MAX_INTEREST_INTERFACES; j++)
        {
            node->pending_interests[i].interfaces[j].is_valid = 0;
            node->pending_interests[i].interfaces[j].sd = -1;
            node->pending_interests[i].interfaces[j].state = INTERFACE_STATE_NONE;
        }
    }
    node->num_pending_interests = 0;
}

// Helper function: Inicializa os objetos locais
void init_local_objects(NDNNode *node)
{
    for (int i = 0; i < MAX_LOCAL_OBJECTS; i++)
    {
        node->local_objects[i].is_valid = 0;
    }
    node->num_local_objects = 0; // Inicializa o contador
}

// Helper function: Inicializa a cache
void init_cache(NDNNode *node)
{
    for (int i = 0; i < MAX_CACHE_OBJECTS; i++)
    {
        node->object_cache[i].is_valid = 0;
        node->object_cache[i].last_access_time = 0; // Inicializar também o timestamp
    }
    node->num_cached_objects = 0; // Inicializa o contador
}

// Funções de gestão de objetos locais
void create_local_object(NDNNode *node, const char *name)
{
    if (strlen(name) > MAX_OBJECT_NAME_LEN)
    {
        printf("Erro: Nome do objeto '%s' excede o tamanho máximo de %d caracteres.\n", name, MAX_OBJECT_NAME_LEN);
        return;
    }
    for (int i = 0; i < MAX_LOCAL_OBJECTS; i++)
    {
        if (node->local_objects[i].is_valid && strcmp(node->local_objects[i].name, name) == 0)
        {
            printf("Objeto '%s' já existe localmente.\n", name);
            return;
        }
    }
    for (int i = 0; i < MAX_LOCAL_OBJECTS; i++)
    {
        if (!node->local_objects[i].is_valid)
        {
            strncpy(node->local_objects[i].name, name, MAX_OBJECT_NAME_LEN);
            node->local_objects[i].name[MAX_OBJECT_NAME_LEN] = '\0';
            node->local_objects[i].is_valid = 1;
            node->num_local_objects++;
            printf("Objeto '%s' criado localmente.\n", name);
            return;
        }
    }
    printf("Erro: Limite de objetos locais atingido (%d).\n", MAX_LOCAL_OBJECTS);
}

void delete_local_object(NDNNode *node, const char *name)
{
    for (int i = 0; i < MAX_LOCAL_OBJECTS; i++)
    {
        if (node->local_objects[i].is_valid && strcmp(node->local_objects[i].name, name) == 0)
        {
            node->local_objects[i].is_valid = 0;
            node->num_local_objects--;
            printf("Objeto '%s' removido localmente.\n", name);
            return;
        }
    }
    printf("Objeto '%s' não encontrado localmente.\n", name);
}

int has_local_object(NDNNode *node, const char *name)
{
    for (int i = 0; i < MAX_LOCAL_OBJECTS; i++)
    {
        if (node->local_objects[i].is_valid && strcmp(node->local_objects[i].name, name) == 0)
        {
            return 1;
        }
    }
    return 0;
}

// Funções de gestão de cache (política LRU simples)
void add_object_to_cache(NDNNode *node, const char *name)
{
    // Primeiro, verifica se já está na cache
    for (int i = 0; i < MAX_CACHE_OBJECTS; i++)
    {
        if (node->object_cache[i].is_valid && strcmp(node->object_cache[i].name, name) == 0)
        {
            node->object_cache[i].last_access_time = time(NULL); // Atualiza timestamp para LRU
            printf("Objeto '%s' já estava na cache. Timestamp atualizado.\n", name);
            return;
        }
    }

    // Procura por um slot vazio
    for (int i = 0; i < MAX_CACHE_OBJECTS; i++)
    {
        if (!node->object_cache[i].is_valid) // Se encontrou um slot vazio
        {
            strncpy(node->object_cache[i].name, name, MAX_OBJECT_NAME_LEN);
            node->object_cache[i].name[MAX_OBJECT_NAME_LEN] = '\0';
            node->object_cache[i].is_valid = 1;
            node->object_cache[i].last_access_time = time(NULL);
            node->num_cached_objects++; // SÓ INCREMENTA QUANDO ADICIONA NOVO
            return;
        }
    }

    // Cache cheia, aplica política LRU: remove o menos recentemente usado
    printf("Cache cheia. Aplicando política LRU para '%s'.\n", name);
    long oldest_time = time(NULL) + 1; // Inicializa com um valor maior para garantir que o primeiro é sempre "oldest"
    int oldest_idx = -1;

    for (int i = 0; i < MAX_CACHE_OBJECTS; i++)
    {
        if (node->object_cache[i].is_valid && node->object_cache[i].last_access_time < oldest_time)
        {
            oldest_time = node->object_cache[i].last_access_time;
            oldest_idx = i;
        }
    }

    if (oldest_idx != -1)
    {
        printf("Removendo '%s' da cache (LRU).\n", node->object_cache[oldest_idx].name);
        strncpy(node->object_cache[oldest_idx].name, name, MAX_OBJECT_NAME_LEN);
        node->object_cache[oldest_idx].name[MAX_OBJECT_NAME_LEN] = '\0';
        node->object_cache[oldest_idx].last_access_time = time(NULL);
        // num_cached_objects NÃO ALTERA AQUI - é uma SUBSTITUIÇÃO
    }
    else
    {
        fprintf(stderr, "Erro lógico: Cache cheia, mas não encontrei LRU válido para remover (todos os timestamps eram 0?).\n");
    }
}

int has_cached_object(NDNNode *node, const char *name)
{
    for (int i = 0; i < MAX_CACHE_OBJECTS; i++)
    {
        if (node->object_cache[i].is_valid && strcmp(node->object_cache[i].name, name) == 0)
        {
            node->object_cache[i].last_access_time = time(NULL); // Atualiza timestamp para LRU
            return 1;
        }
    }
    return 0;
}

// Funções de envio de mensagens NDN
void send_interest_message(int target_sd, unsigned char id, const char *name)
{
    char message[MAX_TCP_MSG_LEN];
    snprintf(message, sizeof(message), "INTEREST %u %s\n", id, name); // %u para unsigned char
    printf("Enviando INTEREST (ID: %u) para SD %d: '%s'", id, target_sd, message);
    if (write(target_sd, message, strlen(message)) == -1)
    {
        perror("Erro ao enviar mensagem INTEREST");
        NDNNode *node = get_current_ndn_node(); // Acessar o nó global
        remove_neighbor(node, target_sd);
    }
}

void send_object_message(int target_sd, unsigned char id, const char *name)
{
    char message[MAX_TCP_MSG_LEN];
    snprintf(message, sizeof(message), "OBJECT %u %s\n", id, name);
    printf("Enviando OBJECT (ID: %u) para SD %d: '%s'", id, target_sd, message);
    if (write(target_sd, message, strlen(message)) == -1)
    {
        perror("Erro ao enviar mensagem OBJECT");
        NDNNode *node = get_current_ndn_node();
        remove_neighbor(node, target_sd);
    }
}

void send_noobject_message(int target_sd, unsigned char id, const char *name)
{
    char message[MAX_TCP_MSG_LEN];
    snprintf(message, sizeof(message), "NOOBJECT %u %s\n", id, name);
    printf("Enviando NOOBJECT (ID: %u) para SD %d: '%s'", id, target_sd, message);
    if (write(target_sd, message, strlen(message)) == -1)
    {
        perror("Erro ao enviar mensagem NOOBJECT");
        NDNNode *node = get_current_ndn_node();
        remove_neighbor(node, target_sd);
    }
}

// Funções de depuração e visualização para NDN
void show_local_objects(NDNNode *node)
{
    printf("Objetos locais (%d):\n", node->num_local_objects);
    if (node->num_local_objects == 0)
    {
        printf("  (Nenhum)\n");
    }
    for (int i = 0; i < MAX_LOCAL_OBJECTS; i++)
    {
        if (node->local_objects[i].is_valid)
        {
            printf("  - %s (Local)\n", node->local_objects[i].name); // Adicionado "(Local)" para clareza
        }
    }
    printf("Objetos em Cache (%d):\n", node->num_cached_objects);
    if (node->num_cached_objects == 0)
    {
        printf("  (Nenhum)\n");
    }
    else
    {
        for (int i = 0; i < MAX_CACHE_OBJECTS; i++)
        {
            if (node->object_cache[i].is_valid)
            {
                printf("  - %s (Cache, Last Access: %ld)\n", node->object_cache[i].name, node->object_cache[i].last_access_time);
            }
        }
    }
}

void show_interest_table(NDNNode *node)
{
    printf("Tabela de Interesses Pendentes (%d):\n", node->num_pending_interests);
    if (node->num_pending_interests == 0)
    {
        printf("  (Nenhum)\n");
        return;
    }
    for (int i = 0; i < MAX_PENDING_INTERESTS; i++)
    {
        if (node->pending_interests[i].is_valid)
        {
            printf("  ID: %u, Nome: %s\n", node->pending_interests[i].interest_id, node->pending_interests[i].object_name);
            printf("    Interfaces:\n");
            int has_waiting = 0;
            for (int j = 0; j < MAX_INTEREST_INTERFACES; j++)
            {
                if (node->pending_interests[i].interfaces[j].is_valid)
                {
                    char *state_str = "UNKNOWN";
                    switch (node->pending_interests[i].interfaces[j].state)
                    {
                    case INTERFACE_STATE_RESPONSE:
                        state_str = "RESPOSTA";
                        break;
                    case INTERFACE_STATE_WAITING:
                        state_str = "ESPERA";
                        has_waiting = 1;
                        break;
                    case INTERFACE_STATE_CLOSED:
                        state_str = "FECHADO";
                        break;
                    default:
                        break;
                    }
                    printf("      SD: %d, Estado: %s\n", node->pending_interests[i].interfaces[j].sd, state_str);
                }
            }
            if (!has_waiting)
            {
                printf("      AVISO: Nenhuma interface em estado 'ESPERA' para este interesse (ID: %u).\n", node->pending_interests[i].interest_id);
            }
        }
    }
}

// --- Lógica principal de obtenção de objetos ---

// Início de uma pesquisa (chamada do UI)
void initiate_retrieve(NDNNode *node, const char *object_name)
{
    if (node->current_net_id == -1)
    {
        printf("Erro: Nó não está em nenhuma rede. Use 'join' ou 'direct join' primeiro.\n");
        return;
    }

    // 1. Verificar se o nó já tem o objeto localmente
    if (has_local_object(node, object_name))
    {
        printf("Objeto '%s' encontrado localmente. Não é necessária pesquisa.\n", object_name);
        return;
    }

    // 2. Verificar se o objeto está na cache
    if (has_cached_object(node, object_name))
    {
        printf("Objeto '%s' encontrado na cache. Não é necessária pesquisa.\n", object_name);
        return;
    }

    // Se não tiver o objeto, iniciar a pesquisa

    // 3. Escolher um identificador de procura aleatoriamente entre 0 e 255
    srand(time(NULL)); // Inicializa o gerador de números aleatórios
    unsigned char interest_id;
    int id_found = 0;
    for (int attempts = 0; attempts < 256; attempts++)
    { // Tenta encontrar um ID único
        unsigned char potential_id = (unsigned char)(rand() % 256);
        int is_unique = 1;
        for (int i = 0; i < MAX_PENDING_INTERESTS; i++)
        {
            if (node->pending_interests[i].is_valid && node->pending_interests[i].interest_id == potential_id)
            {
                is_unique = 0;
                break;
            }
        }
        if (is_unique)
        {
            interest_id = potential_id;
            id_found = 1;
            break;
        }
    }

    if (!id_found)
    {
        return;
    }

    // 4. Criar a entrada correspondente na tabela de interesses pendentes (PIT)
    int pit_idx = -1;
    for (int i = 0; i < MAX_PENDING_INTERESTS; i++)
    {
        if (!node->pending_interests[i].is_valid)
        {
            pit_idx = i;
            break;
        }
    }
    if (pit_idx == -1)
    {
        printf("Erro: Tabela de Interesses Pendentes cheia. Não é possível iniciar nova pesquisa para '%s'.\n", object_name);
        return;
    }

    PendingInterestEntry *new_interest = &node->pending_interests[pit_idx];
    new_interest->is_valid = 1;
    new_interest->interest_id = interest_id;
    strncpy(new_interest->object_name, object_name, MAX_OBJECT_NAME_LEN);
    new_interest->object_name[MAX_OBJECT_NAME_LEN] = '\0';
    new_interest->num_active_interfaces = 0;

    // A interface que gerou o interesse (o utilizador local) é a interface de RESPOSTA
    if (new_interest->num_active_interfaces < MAX_INTEREST_INTERFACES)
    {
        new_interest->interfaces[new_interest->num_active_interfaces].sd = STDIN_FILENO; // Representa o utilizador
        new_interest->interfaces[new_interest->num_active_interfaces].state = INTERFACE_STATE_RESPONSE;
        new_interest->interfaces[new_interest->num_active_interfaces].is_valid = 1;
        new_interest->num_active_interfaces++;
    }
    else
    {
        node->pending_interests[pit_idx].is_valid = 0; // Invalidar a entrada se não pode ser usada
        return;
    }

    // 5. Enviar uma mensagem de interesse por CADA uma das suas interfaces (vizinhos)
    // Colocando estas interfaces no estado de ESPERA
    int sent_to_any_neighbor = 0;
    for (int i = 0; i < MAX_NEIGHBORS; i++)
    {
        if (node->neighbors[i].is_valid && node->neighbors[i].socket_sd != -1)
        {
            if (new_interest->num_active_interfaces < MAX_INTEREST_INTERFACES)
            {
                send_interest_message(node->neighbors[i].socket_sd, interest_id, object_name);
                new_interest->interfaces[new_interest->num_active_interfaces].sd = node->neighbors[i].socket_sd;
                new_interest->interfaces[new_interest->num_active_interfaces].state = INTERFACE_STATE_WAITING;
                new_interest->interfaces[new_interest->num_active_interfaces].is_valid = 1;
                new_interest->num_active_interfaces++;
                sent_to_any_neighbor = 1;
            }
            else
            {
                break;
            }
        }
    }

    // If no neighbors to send to, and only STDIN is in the PIT, then send NOOBJECT back immediately.
    if (!sent_to_any_neighbor)
    {
        new_interest->is_valid = 0; // Remove from PIT if no interfaces are waiting
        node->num_pending_interests--;
    }
    else
    {
        node->num_pending_interests++;
    }
}

// Processamento de qualquer mensagem NDN recebida (chamada pelo topology_protocol.c)
void process_ndn_message(NDNNode *node, int client_sd, const char *message)
{
    char cmd[20];
    unsigned int id_uint; // Usar unsigned int para sscanf, depois converter para unsigned char
    char object_name[MAX_OBJECT_NAME_LEN + 1];

    if (sscanf(message, "%s %u %s", cmd, &id_uint, object_name) != 3)
    {
        return;
    }
    unsigned char interest_id = (unsigned char)id_uint;

    if (strcmp(cmd, "INTEREST") == 0)
    {
        printf("Recebida INTEREST (ID: %u, Nome: %s) de SD %d.\n", interest_id, object_name, client_sd);

        // 1. Verificar se o nó tem o objeto
        if (has_local_object(node, object_name))
        {
            printf("  Objeto '%s' encontrado localmente. Respondendo com OBJECT.\n", object_name);
            send_object_message(client_sd, interest_id, object_name);
            return;
        }

        // 2. Verificar se o objeto está na cache
        if (has_cached_object(node, object_name))
        {
            printf("  Objeto '%s' encontrado na cache. Respondendo com OBJECT.\n", object_name);
            send_object_message(client_sd, interest_id, object_name);
            return;
        }

        // 3. Se o nó não tiver o objeto localmente ou na cache
        // Procurar na Tabela de Interesses Pendentes (PIT) se já existe este interesse
        PendingInterestEntry *existing_interest = NULL;
        for (int i = 0; i < MAX_PENDING_INTERESTS; i++)
        {
            if (node->pending_interests[i].is_valid && node->pending_interests[i].interest_id == interest_id &&
                strcmp(node->pending_interests[i].object_name, object_name) == 0)
            {
                existing_interest = &node->pending_interests[i];
                break;
            }
        }

        if (existing_interest)
        {
            // Se o interesse já existe na PIT, adicionar a interface de onde veio
            // para que a resposta seja enviada de volta por ela.
            printf("  Interesse ID %u para '%s' já existe na PIT. Adicionando SD %d como interface de RESPOSTA.\n",
                   interest_id, object_name, client_sd);
            // Verifica se a interface já existe para não duplicar, apenas atualiza o estado
            int interface_found = 0;
            for (int i = 0; i < existing_interest->num_active_interfaces; ++i)
            {
                if (existing_interest->interfaces[i].is_valid && existing_interest->interfaces[i].sd == client_sd)
                {
                    existing_interest->interfaces[i].state = INTERFACE_STATE_RESPONSE;
                    interface_found = 1;
                    break;
                }
            }
            if (!interface_found)
            {
                // Adiciona nova interface de resposta
                if (existing_interest->num_active_interfaces < MAX_INTEREST_INTERFACES)
                {
                    existing_interest->interfaces[existing_interest->num_active_interfaces].sd = client_sd;
                    existing_interest->interfaces[existing_interest->num_active_interfaces].state = INTERFACE_STATE_RESPONSE;
                    existing_interest->interfaces[existing_interest->num_active_interfaces].is_valid = 1;
                    existing_interest->num_active_interfaces++;
                }
                else
                {
                    fprintf(stderr, "Aviso: Limite de interfaces para interesse %u atingido. Não adicionou SD %d.\n", interest_id, client_sd);
                }
            }
        }
        else
        {
            // Se o interesse não existe na PIT, criar uma nova entrada
            printf("  Interesse ID %u para '%s' não existe na PIT. Criando nova entrada e reencaminhando.\n", interest_id, object_name);
            int pit_idx = -1;
            for (int i = 0; i < MAX_PENDING_INTERESTS; i++)
            {
                if (!node->pending_interests[i].is_valid)
                {
                    pit_idx = i;
                    break;
                }
            }
            if (pit_idx == -1)
            {
                // Neste caso, o interesse não pode ser reencaminhado. Poderíamos enviar NOOBJECT de volta.
                send_noobject_message(client_sd, interest_id, object_name);
                return;
            }

            PendingInterestEntry *new_interest = &node->pending_interests[pit_idx];
            new_interest->is_valid = 1;
            new_interest->interest_id = interest_id;
            strncpy(new_interest->object_name, object_name, MAX_OBJECT_NAME_LEN);
            new_interest->object_name[MAX_OBJECT_NAME_LEN] = '\0';
            new_interest->num_active_interfaces = 0;

            // A interface de onde veio a mensagem é a interface de RESPOSTA
            if (new_interest->num_active_interfaces < MAX_INTEREST_INTERFACES)
            {
                new_interest->interfaces[new_interest->num_active_interfaces].sd = client_sd;
                new_interest->interfaces[new_interest->num_active_interfaces].state = INTERFACE_STATE_RESPONSE;
                new_interest->interfaces[new_interest->num_active_interfaces].is_valid = 1;
                new_interest->num_active_interfaces++;
            }
            else
            {
                send_noobject_message(client_sd, interest_id, object_name); // Se não pode adicionar interface de resposta
                node->pending_interests[pit_idx].is_valid = 0;              // Invalidar a entrada se não pode ser usada
                return;
            }
            node->num_pending_interests++;
            existing_interest = new_interest; // Definir para reencaminhar

            // Reencaminhar a mensagem de interesse por todas as outras interfaces (exceto a de entrada)
            // Colocando-as no estado de ESPERA
            if (node->num_active_neighbors == 0)
            { // Se não tem vizinhos para reencaminhar
                send_noobject_message(client_sd, interest_id, object_name);
                // Remover a entrada da PIT se não há espera
                node->pending_interests[pit_idx].is_valid = 0;
                node->num_pending_interests--;
                return;
            }

            for (int i = 0; i < MAX_NEIGHBORS; i++)
            {
                if (node->neighbors[i].is_valid && node->neighbors[i].socket_sd != -1 &&
                    node->neighbors[i].socket_sd != client_sd)
                { // Não reencaminhar pela mesma interface
                    if (existing_interest->num_active_interfaces < MAX_INTEREST_INTERFACES)
                    {
                        send_interest_message(node->neighbors[i].socket_sd, interest_id, object_name);
                        existing_interest->interfaces[existing_interest->num_active_interfaces].sd = node->neighbors[i].socket_sd;
                        existing_interest->interfaces[existing_interest->num_active_interfaces].state = INTERFACE_STATE_WAITING;
                        existing_interest->interfaces[existing_interest->num_active_interfaces].is_valid = 1;
                        existing_interest->num_active_interfaces++;
                    }
                    else
                    {
                        break;
                    }
                }
            }
            // A especificação diz: "Cada entrada na tabela de interesses terá pelo menos uma interface no estado de espera."
            // Se nenhuma interface foi colocada em ESPERA (ex: apenas 1 vizinho e foi a interface de entrada), deve enviar NOOBJECT.
            int has_waiting_interface = 0;
            for (int i = 0; i < existing_interest->num_active_interfaces; ++i)
            {
                if (existing_interest->interfaces[i].is_valid && existing_interest->interfaces[i].state == INTERFACE_STATE_WAITING)
                {
                    has_waiting_interface = 1;
                    break;
                }
            }
            if (!has_waiting_interface)
            {
                send_noobject_message(client_sd, interest_id, object_name);
                existing_interest->is_valid = 0;
                node->num_pending_interests--;
            }
        }
    }
    else if (strcmp(cmd, "OBJECT") == 0)
    {

        // Se o identificador da procura consta da tabela de interesses pendentes
        PendingInterestEntry *pending_interest = NULL;
        for (int i = 0; i < MAX_PENDING_INTERESTS; i++)
        {
            if (node->pending_interests[i].is_valid && node->pending_interests[i].interest_id == interest_id &&
                strcmp(node->pending_interests[i].object_name, object_name) == 0)
            {
                pending_interest = &node->pending_interests[i];
                break;
            }
        }

        if (pending_interest)
        {
            // O objeto é guardado em cache
            add_object_to_cache(node, object_name);

            // A mensagem é reencaminhada pela interface no estado de RESPOSTA
            for (int i = 0; i < MAX_INTEREST_INTERFACES; i++)
            {
                if (pending_interest->interfaces[i].is_valid && pending_interest->interfaces[i].state == INTERFACE_STATE_RESPONSE)
                {
                    int response_sd = pending_interest->interfaces[i].sd;
                    // Se a interface de resposta for STDIN, significa que o usuário local iniciou a pesquisa.
                    if (response_sd == STDIN_FILENO)
                    {
                        printf("  Objeto '%s' (ID %u) entregue ao utilizador local.\n", object_name, interest_id);
                    }
                    else
                    {
                        send_object_message(response_sd, interest_id, object_name);
                    }
                    break; // Supondo apenas uma interface de RESPOSTA por interesse.
                }
            }
            // A entrada correspondente à procura é apagada da tabela de interesses pendentes
            pending_interest->is_valid = 0;
            node->num_pending_interests--;
        }

    }
    else if (strcmp(cmd, "NOOBJECT") == 0)
    {

        // Se o identificador da procura consta da tabela de interesses pendentes
        PendingInterestEntry *pending_interest = NULL;
        for (int i = 0; i < MAX_PENDING_INTERESTS; i++)
        {
            if (node->pending_interests[i].is_valid && node->pending_interests[i].interest_id == interest_id &&
                strcmp(node->pending_interests[i].object_name, object_name) == 0)
            {
                pending_interest = &node->pending_interests[i];
                break;
            }
        }

        if (pending_interest)
        {
            // O estado da interface por onde a mensagem é recebida passa a FECHADO
            int interface_found = 0;
            for (int i = 0; i < MAX_INTEREST_INTERFACES; i++)
            {
                if (pending_interest->interfaces[i].is_valid && pending_interest->interfaces[i].sd == client_sd)
                {
                    pending_interest->interfaces[i].state = INTERFACE_STATE_CLOSED;
                    interface_found = 1;
                    break;
                }
            }
            if (!interface_found)
            {
                fprintf(stderr, "Aviso: NOOBJECT recebido, mas interface SD %d não encontrada na PIT para ID %u, nome %s.\n",
                        client_sd, interest_id, object_name);
                // Pode ser um NOOBJECT de uma interface que não estava em ESPERA, ou já foi tratada.
            }

            // Se, em resultado desta atualização, não houver interfaces no estado de ESPERA
            int has_waiting_interface = 0;
            for (int i = 0; i < MAX_INTEREST_INTERFACES; i++)
            {
                if (pending_interest->interfaces[i].is_valid && pending_interest->interfaces[i].state == INTERFACE_STATE_WAITING)
                {
                    has_waiting_interface = 1;
                    break;
                }
            }

            if (!has_waiting_interface)
            {
                // Então é enviada uma mensagem de não-objeto pela interface no estado de RESPOSTA
                for (int i = 0; i < MAX_INTEREST_INTERFACES; i++)
                {
                    if (pending_interest->interfaces[i].is_valid && pending_interest->interfaces[i].state == INTERFACE_STATE_RESPONSE)
                    {
                        int response_sd = pending_interest->interfaces[i].sd;
                        if (response_sd == STDIN_FILENO)
                        {
                            printf("  Objeto '%s' (ID %u) NÃO ENCONTRADO para o utilizador local.\n", object_name, interest_id);
                        }
                        else
                        {
                            send_noobject_message(response_sd, interest_id, object_name);
                        }
                        break; // Supondo apenas uma interface de RESPOSTA
                    }
                }
                // Apaga a entrada da PIT
                pending_interest->is_valid = 0;
                node->num_pending_interests--;
                printf("  Entrada da PIT para ID %u, nome %s apagada.\n", interest_id, object_name);
            }
        }
        else
        {
            printf("  NOOBJECT (ID: %u, Nome: %s) recebido, mas não há interesse pendente correspondente. Descartado.\n", interest_id, object_name);
        }
    }
    else
    {
        fprintf(stderr, "Tipo de mensagem NDN desconhecido: %s\n", cmd);
    }
}