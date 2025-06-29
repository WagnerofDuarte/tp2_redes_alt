#include "common.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <math.h>

#include <sys/socket.h>
#include <sys/types.h>

#define BUFSZ 1024
#define MAX_PLAYERS 10
#define NICKNAME_LEN 13 // Definir o tamanho máximo do nickname aqui

// Estrutura para armazenar dados do jogador
struct player {
    int id;
    int sock;
    char nickname[NICKNAME_LEN];
    float bet_value;
    float current_profit;
    int has_bet;
    int has_cashed_out;
    pthread_mutex_t mutex;
};

// Variáveis globais do jogo
struct player players[MAX_PLAYERS];
int time_remaining = 10; // Tempo de apostas em segundos
int num_players = 0;
pthread_mutex_t players_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t game_state_mutex = PTHREAD_MUTEX_INITIALIZER;

float current_multiplier = 1.00;
float explosion_multiplier = 0.0;
float house_profit = 0.0;
int game_active = 0;
int betting_open = 0;
int round_id = 0;

void usage(int argc, char **argv) {
    printf("usage: %s <v4|v6> <server port>\n", argv[0]);
    printf("example: %s v4 51511\n", argv[0]);
    exit(EXIT_FAILURE);
}

// Função para logar eventos no servidor
void log_server_event(const char *event_type, int player_id, float multiplier, float me, int N, float V, float bet, float payout, float player_profit, float house_profit) {
    printf("event=%s", event_type);
    if (player_id != -1) {
        printf(" | id=%d", player_id);
    } else {
        printf(" | id=*");
    }
    if (multiplier != -1.0) {
        printf(" | m=%.2f", multiplier);
    }
    if (me != -1.0) {
        printf(" | me=%.2f", me);
    }
    if (N != -1) {
        printf(" | N=%d", N);
    }
    if (V != -1.0) {
        printf(" | V=%.2f", V);
    }
    if (bet != -1.0) {
        printf(" | bet=%.2f", bet);
    }
    if (payout != -1.0) {
        printf(" | payout=%.2f", payout);
    }
    if (player_profit != -1.0) {
        printf(" | player_profit=%.2f", player_profit);
    }
    if (house_profit != -1.0) {
        printf(" | house_profit=%.2f", house_profit);
    }
    printf("\n");
}

// Função para enviar mensagem a um cliente
void send_message_to_client(int sock, struct aviator_msg msg) {
    ssize_t count = send(sock, &msg, sizeof(struct aviator_msg), 0);
    if (count != sizeof(struct aviator_msg)) {
        logexit("send");
    }
}

// Função para broadcast de mensagens
void broadcast_message(struct aviator_msg msg) {
    pthread_mutex_lock(&players_mutex);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].sock != 0) {
            send_message_to_client(players[i].sock, msg);
        }
    }
    pthread_mutex_unlock(&players_mutex);
}

// Função para calcular o ponto de explosão
float calculate_explosion_multiplier() {
    int N = 0;
    float V = 0.0;

    pthread_mutex_lock(&players_mutex);
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i].has_bet) {
            N++;
            V += players[i].bet_value;
        }
    }
    pthread_mutex_unlock(&players_mutex);

    if (N == 0) {
        return 1.00; // Se ninguém apostou, explode em 1.00x
    }

    // Fórmula: me = sqrt(V / N + 1.0) * 1.0
    return sqrt(V / N + 1.0);
}

// Thread do jogo principal
void *game_thread(void *arg) {
    while (1) {
        // 1. Coleta de apostas
        pthread_mutex_lock(&game_state_mutex);
        game_active = 0;
        betting_open = 1;
        current_multiplier = 1.00;
        explosion_multiplier = 0.0;
        round_id++;

        // Resetar estado dos jogadores para a nova rodada
        pthread_mutex_lock(&players_mutex);
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (players[i].sock != 0) {
                players[i].bet_value = 0.0;
                players[i].has_bet = 0;
                players[i].has_cashed_out = 0;
            }
        }
        pthread_mutex_unlock(&players_mutex);

        log_server_event("start", -1, -1.0, -1.0, num_players, -1.0, -1.0, -1.0, -1.0, house_profit);

        struct aviator_msg start_msg;
        start_msg.player_id = -1;
        start_msg.value = 10.0; // 10 segundos
        // strcpy(start_msg.type, "start");
        start_msg.player_profit = -1.0;
        start_msg.house_profit = house_profit;
        broadcast_message(start_msg);

        pthread_mutex_unlock(&game_state_mutex);

        for (int i = 10; i >= 0; i--) {
            sleep(1);
            // Enviar tempo restante para clientes que se conectarem durante a janela de apostas
            pthread_mutex_lock(&players_mutex);
            time_remaining = i;
            // for (int j = 0; j < MAX_PLAYERS; j++) {
            //     if (players[j].sock != 0 && !players[j].has_bet) { // Apenas para quem não apostou ainda
            //         struct aviator_msg timer_msg;
            //         timer_msg.player_id = -1;
            //         timer_msg.value = (float)i;
            //         strcpy(timer_msg.type, "start");
            //         timer_msg.player_profit = -1.0;
            //         timer_msg.house_profit = house_profit;
            //         send_message_to_client(players[j].sock, timer_msg);
            //     }
            // }
            pthread_mutex_unlock(&players_mutex);
        }
        time_remaining = 10;

        // 2. Cálculo do ponto de explosão
        pthread_mutex_lock(&game_state_mutex);
        betting_open = 0;
        explosion_multiplier = calculate_explosion_multiplier();
        game_active = 1;

        log_server_event("closed", -1, -1.0, explosion_multiplier, -1, -1.0, -1.0, -1.0, -1.0, house_profit);

        struct aviator_msg closed_msg;
        closed_msg.player_id = -1;
        closed_msg.value = -1.0;
        strcpy(closed_msg.type, "closed");
        closed_msg.player_profit = -1.0;
        closed_msg.house_profit = house_profit;
        broadcast_message(closed_msg);
        pthread_mutex_unlock(&game_state_mutex);

        // 3. Decolagem (Broadcast de multiplicador)
        while (current_multiplier < explosion_multiplier) {
            pthread_mutex_lock(&game_state_mutex);
            current_multiplier += 0.01; // Incremento de 0.01x a cada 100ms
            if (current_multiplier >= explosion_multiplier) {
                current_multiplier = explosion_multiplier; // Garante que não ultrapasse
            }

            struct aviator_msg multiplier_msg;
            multiplier_msg.player_id = -1;
            multiplier_msg.value = current_multiplier;
            strcpy(multiplier_msg.type, "multiplier");
            multiplier_msg.player_profit = -1.0;
            multiplier_msg.house_profit = house_profit;
            // broadcast_message(multiplier_msg);
            pthread_mutex_lock(&players_mutex);
            for (int i = 0; i < MAX_PLAYERS; i++) {
                if (players[i].sock != 0 && !players[i].has_cashed_out && players[i].has_bet) {
                    send_message_to_client(players[i].sock, multiplier_msg);
                }
            }
            pthread_mutex_unlock(&players_mutex);

            log_server_event("multiplier", -1, current_multiplier, -1.0, -1, -1.0, -1.0, -1.0, -1.0, house_profit);
            pthread_mutex_unlock(&game_state_mutex);
            usleep(100000); // 100 ms
        }

        // 5. Explosão
        pthread_mutex_lock(&game_state_mutex);
        game_active = 0;
        log_server_event("explode", -1, explosion_multiplier, -1.0, -1, -1.0, -1.0, -1.0, -1.0, house_profit);

        struct aviator_msg explode_msg;
        explode_msg.player_id = -1;
        explode_msg.value = explosion_multiplier;
        strcpy(explode_msg.type, "explode");
        explode_msg.player_profit = -1.0;
        explode_msg.house_profit = house_profit;
        broadcast_message(explode_msg);

        pthread_mutex_lock(&players_mutex);
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (players[i].sock != 0 && players[i].has_bet && !players[i].has_cashed_out) {
                // Jogadores que não sacaram perdem a aposta
                players[i].current_profit -= players[i].bet_value;
                house_profit += players[i].bet_value;

                log_server_event("explode", players[i].id, explosion_multiplier, -1.0, -1, -1.0, -1.0, -1.0, -1.0, -1.0);
                log_server_event("profit", players[i].id, -1.0, -1.0, -1, -1.0, -1.0, -1.0, players[i].current_profit, -1.0);

                struct aviator_msg player_profit_msg;
                player_profit_msg.player_id = players[i].id;
                player_profit_msg.value = -1.0;
                strcpy(player_profit_msg.type, "profit");
                player_profit_msg.player_profit = players[i].current_profit;
                player_profit_msg.house_profit = house_profit;
                // player_profit_msg.nickname = players[i].nickname; // Removido
                send_message_to_client(players[i].sock, player_profit_msg);
            }
        }
        pthread_mutex_unlock(&players_mutex);

        log_server_event("profit", -1, -1.0, -1.0, -1, -1.0, -1.0, -1.0, -1.0, house_profit);

        pthread_mutex_unlock(&game_state_mutex);

        sleep(5); // Pequena pausa antes de iniciar a próxima rodada
    }
    return NULL;
}

void *client_handler_thread(void *data) {
    struct player *p = (struct player *)data;
    struct aviator_msg received_msg;
    char nickname_buffer[NICKNAME_LEN]; // Buffer para receber o nickname

    // Receber o nickname do cliente como uma string separada
    ssize_t count = recv(p->sock, nickname_buffer, NICKNAME_LEN, 0);
    if (count == 0) {
        log_server_event("bye", p->id, -1.0, -1.0, -1, -1.0, -1.0, -1.0, -1.0, -1.0);
        close(p->sock);
        pthread_mutex_lock(&players_mutex);
        p->sock = 0;
        num_players--;
        pthread_mutex_unlock(&players_mutex);
        pthread_exit(EXIT_SUCCESS);
    } else if (count == -1) {
        logexit("recv");
    }
    nickname_buffer[count] = '\0'; // Null-terminate the received string
    strcpy(p->nickname, nickname_buffer);
    printf("[log] Player %d nickname: %s connected.\n", p->id, p->nickname);

    // Enviar estado atual do jogo para o cliente recém-conectado
    pthread_mutex_lock(&game_state_mutex);
    if (betting_open) {
        struct aviator_msg start_msg;
        start_msg.player_id = -1;
        start_msg.value = time_remaining;// 10.0; // Placeholder, cliente vai calcular o tempo restante
        strcpy(start_msg.type, "start");
        start_msg.player_profit = p->current_profit;
        start_msg.house_profit = house_profit;
        // strcpy(start_msg.nickname, p->nickname); // Removido
        send_message_to_client(p->sock, start_msg);
    } else if (game_active) {
        struct aviator_msg closed_msg;
        closed_msg.player_id = -1;
        closed_msg.value = -1.0;
        strcpy(closed_msg.type, "closed");
        closed_msg.player_profit = p->current_profit;
        closed_msg.house_profit = house_profit;
        // strcpy(closed_msg.nickname, p->nickname); // Removido
        send_message_to_client(p->sock, closed_msg);

        struct aviator_msg multiplier_msg;
        multiplier_msg.player_id = -1;
        multiplier_msg.value = current_multiplier;
        strcpy(multiplier_msg.type, "multiplier");
        multiplier_msg.player_profit = p->current_profit;
        multiplier_msg.house_profit = house_profit;
        // strcpy(multiplier_msg.nickname, p->nickname); // Removido
        send_message_to_client(p->sock, multiplier_msg);
    } else { // Jogo inativo, aguardando nova rodada
        struct aviator_msg closed_msg;
        closed_msg.player_id = -1;
        closed_msg.value = -1.0;
        strcpy(closed_msg.type, "closed");
        closed_msg.player_profit = p->current_profit;
        closed_msg.house_profit = house_profit;
        // strcpy(closed_msg.nickname, p->nickname); // Removido
        send_message_to_client(p->sock, closed_msg);
    }
    pthread_mutex_unlock(&game_state_mutex);

    while (1) {
        count = recv(p->sock, &received_msg, sizeof(struct aviator_msg), 0);
        if (count == 0) {
            // Conexão encerrada pelo cliente
            log_server_event("bye", p->id, -1.0, -1.0, -1, -1.0, -1.0, -1.0, -1.0, -1.0);
            printf("[log] Player %d nickname: %s disconnected.\n", p->id, p->nickname);
            break;
        } else if (count == -1) {
            logexit("recv");
        }

        if (strcmp(received_msg.type, "bet") == 0) {
            pthread_mutex_lock(&game_state_mutex);
            if (betting_open && !p->has_bet) {
                p->bet_value = received_msg.value;
                p->has_bet = 1;
                log_server_event("bet", p->id, -1.0, -1.0, -1, -1.0, p->bet_value, -1.0, -1.0, -1.0);
            } else {
                // Enviar mensagem de erro ou ignorar aposta
                // Por simplicidade, vamos ignorar por enquanto
            }
            pthread_mutex_unlock(&game_state_mutex);
        } else if (strcmp(received_msg.type, "cashout") == 0) {
            pthread_mutex_lock(&game_state_mutex);
            if (game_active && p->has_bet && !p->has_cashed_out) {
                float winnings = p->bet_value * current_multiplier;
                p->current_profit += (winnings - p->bet_value);
                house_profit -= (winnings - p->bet_value);
                p->has_cashed_out = 1;

                log_server_event("cashout", p->id, current_multiplier, -1.0, -1, -1.0, -1.0, -1.0, -1.0, -1.0);
                log_server_event("payout", p->id, -1.0, -1.0, -1, -1.0, -1.0, winnings, -1.0, -1.0);
                log_server_event("profit", p->id, -1.0, -1.0, -1, -1.0, -1.0, -1.0, p->current_profit, -1.0);

                struct aviator_msg payout_msg;
                payout_msg.player_id = p->id;
                payout_msg.value = current_multiplier;
                strcpy(payout_msg.type, "payout");
                payout_msg.player_profit = p->current_profit;
                payout_msg.house_profit = house_profit;
                // strcpy(payout_msg.nickname, p->nickname); // Removido
                send_message_to_client(p->sock, payout_msg);
            } else {
                // Enviar mensagem de erro ou ignorar cashout
            }
            pthread_mutex_unlock(&game_state_mutex);
        } else if (strcmp(received_msg.type, "bye") == 0) {
            log_server_event("bye", p->id, -1.0, -1.0, -1, -1.0, -1.0, -1.0, -1.0, -1.0);
            printf("[log] Player %d nickname: %s disconnected.\n", p->id, p->nickname);
            break;
        }
    }

    close(p->sock);
    pthread_mutex_lock(&players_mutex);
    p->sock = 0; // Marcar slot como livre
    num_players--;
    pthread_mutex_unlock(&players_mutex);

    pthread_exit(EXIT_SUCCESS);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        usage(argc, argv);
    }

    struct sockaddr_storage storage;
    if (0 != server_sockaddr_init(argv[1], argv[2], &storage)) {
        usage(argc, argv);
    }

    int s;
    s = socket(storage.ss_family, SOCK_STREAM, 0);
    if (s == -1) {
        logexit("socket");
    }

    int enable = 1;
    if (0 != setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int))) {
        logexit("setsockopt");
    }

    struct sockaddr *addr = (struct sockaddr *)(&storage);
    if (0 != bind(s, addr, sizeof(storage))) {
        logexit("bind");
    }

    if (0 != listen(s, MAX_PLAYERS)) {
        logexit("listen");
    }

    char addrstr[BUFSZ];
    addrtostr(addr, addrstr, BUFSZ);
    printf("bound to %s, waiting connections\n", addrstr); // REMOVER

    // Iniciar thread do jogo
    pthread_t game_tid;
    pthread_create(&game_tid, NULL, game_thread, NULL);

    int player_id_counter = 0;

    while (1) {
        struct sockaddr_storage cstorage;
        struct sockaddr *caddr = (struct sockaddr *)(&cstorage);
        socklen_t caddrlen = sizeof(cstorage);

        int csock = accept(s, caddr, &caddrlen);
        if (csock == -1) {
            logexit("accept");
        }

        pthread_mutex_lock(&players_mutex);
        if (num_players >= MAX_PLAYERS) {
            printf("[log] Max players reached. Connection rejected.\n");
            close(csock);
            pthread_mutex_unlock(&players_mutex);
            continue;
        }

        // Encontrar um slot livre para o novo jogador
        int player_idx = -1;
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (players[i].sock == 0) {
                player_idx = i;
                break;
            }
        }

        if (player_idx == -1) { // Não deveria acontecer se num_players < MAX_PLAYERS
            printf("[log] No free player slot found. Connection rejected.\n");
            close(csock);
            pthread_mutex_unlock(&players_mutex);
            continue;
        }

        player_id_counter++;
        players[player_idx].id = player_id_counter;
        players[player_idx].sock = csock;
        players[player_idx].current_profit = 0.0;
        players[player_idx].has_bet = 0;
        players[player_idx].has_cashed_out = 0;
        pthread_mutex_init(&players[player_idx].mutex, NULL);
        num_players++;

        char caddrstr[BUFSZ];
        addrtostr(caddr, caddrstr, BUFSZ);
        printf("[log] connection from %s, assigned player_id: %d\n", caddrstr, players[player_idx].id);

        pthread_t tid;
        pthread_create(&tid, NULL, client_handler_thread, &players[player_idx]);
        pthread_detach(tid); // Não precisamos esperar por esta thread

        pthread_mutex_unlock(&players_mutex);
    }

    pthread_join(game_tid, NULL); // Esperar a thread do jogo terminar (nunca)
    exit(EXIT_SUCCESS);
}