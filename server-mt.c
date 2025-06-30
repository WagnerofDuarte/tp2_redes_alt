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
#define NICKNAME_LEN 13
#define BETTING_TIME 10
#define MULTIPLIER_INCREMENT 0.01f
#define MULTIPLIER_DELAY_US 100000

static const char MSG_START[] = "start";
static const char MSG_CLOSED[] = "closed";
static const char MSG_MULTIPLIER[] = "multiplier";
static const char MSG_EXPLODE[] = "explode";
static const char MSG_PROFIT[] = "profit";
static const char MSG_PAYOUT[] = "payout";
static const char MSG_BET[] = "bet";
static const char MSG_CASHOUT[] = "cashout";
static const char MSG_BYE[] = "bye";

struct player
{
    int id;
    int sock;
    char nickname[NICKNAME_LEN];
    float bet_value;
    float current_profit;
    int has_bet;
    int has_cashed_out;
};

static struct player players[MAX_PLAYERS];
static int time_remaining = BETTING_TIME;
static int num_players = 0;
static pthread_mutex_t players_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t game_state_mutex = PTHREAD_MUTEX_INITIALIZER;
static float current_multiplier = 1.00f;
static float explosion_multiplier = 0.0f;
static float house_profit = 0.0f;
static int game_active = 0;
static int betting_open = 0;
static int round_id = 0;

void usage(int argc, char **argv)
{
    printf("usage: %s <v4|v6> <server port>\nexample: %s v4 51511\n", argv[0], argv[0]);
    exit(EXIT_FAILURE);
}

void log_server_event(const char *event_type, int player_id, float multiplier, float me, int N, float V, float bet, float payout, float player_profit, float house_profit)
{
    printf("event=%s | id=%s", event_type, (player_id != -1) ? "" : "*");
    if (player_id != -1) {
        printf("%d", player_id);
    }

    if (strcmp(event_type, "start") == 0)
    {
        printf(" | N=%.0f");
    }
    else if (strcmp(event_type, "closed") == 0)
    {
        printf(" | N=%.0f", " | V=%.2f");
    }
    else if (strcmp(event_type, "multiplier") == 0)
    {
        printf(" | m=%.2f");
    }
    else if (strcmp(event_type, "explode") == 0)
    {
        printf(" | m=%.2f");
    }
    else if (strcmp(event_type, "bet") == 0)
    {
        printf(" | bet=%.2f", " | N=%.0f", " | V=%.2f");
    }
    else if (strcmp(event_type, "cashout") == 0)
    {
        printf(" | m=%.2f");
    }
    else if (strcmp(event_type, "payout") == 0)
    {
        printf(" | payout=%.2f");
    }
    else if (strcmp(event_type, "profit") == 0)
    {
        printf("player_profit=%.2f");
    }
    printf("\n");
}

static inline struct aviator_msg create_msg(const char *type, int player_id, float value, float player_profit, float house_profit)
{
    struct aviator_msg msg;
    msg.player_id = player_id;
    msg.value = value;
    strcpy(msg.type, type);
    msg.player_profit = player_profit;
    msg.house_profit = house_profit;
    return msg;
}

void send_message_to_client(int sock, struct aviator_msg msg)
{
    if (send(sock, &msg, sizeof(struct aviator_msg), 0) != sizeof(struct aviator_msg))
    {
        logexit("send");
    }
}

void broadcast_message(struct aviator_msg msg)
{
    pthread_mutex_lock(&players_mutex);
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (players[i].sock != 0)
        {
            send_message_to_client(players[i].sock, msg);
        }
    }
    pthread_mutex_unlock(&players_mutex);
}

float calculate_explosion_multiplier()
{
    int N = 0;
    float V = 0.0f;

    pthread_mutex_lock(&players_mutex);
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        if (players[i].has_bet)
        {
            N++;
            V += players[i].bet_value;
        }
    }
    pthread_mutex_unlock(&players_mutex);

    return (N == 0) ? 1.00f : sqrtf(1 + N + 0.01 * V);
}

void *game_thread(void *arg)
{
    while (num_players == 0)
    {
        continue;
    }
    while (1)
    {
        pthread_mutex_lock(&game_state_mutex);
        game_active = 0;
        betting_open = 1;
        current_multiplier = 1.00f;
        explosion_multiplier = 0.0f;
        round_id++;

        pthread_mutex_lock(&players_mutex);
        for (int i = 0; i < MAX_PLAYERS; i++)
        {
            if (players[i].sock != 0)
            {
                players[i].bet_value = 0.0f;
                players[i].has_bet = 0;
                players[i].has_cashed_out = 0;
            }
        }
        pthread_mutex_unlock(&players_mutex);

        log_server_event("start", -1, -1.0f, -1.0f, num_players, -1.0f, -1.0f, -1.0f, -1.0f, house_profit);
        broadcast_message(create_msg(MSG_START, -1, BETTING_TIME, -1.0f, house_profit));
        pthread_mutex_unlock(&game_state_mutex);

        for (int i = BETTING_TIME; i >= 0; i--)
        {
            sleep(1);
            pthread_mutex_lock(&players_mutex);
            time_remaining = i;
            pthread_mutex_unlock(&players_mutex);
        }
        time_remaining = BETTING_TIME;

        pthread_mutex_lock(&game_state_mutex);
        betting_open = 0;
        explosion_multiplier = calculate_explosion_multiplier();
        game_active = 1;

        log_server_event("closed", -1, -1.0f, explosion_multiplier, -1, -1.0f, -1.0f, -1.0f, -1.0f, house_profit);
        broadcast_message(create_msg(MSG_CLOSED, -1, -1.0f, -1.0f, house_profit));
        pthread_mutex_unlock(&game_state_mutex);

        while (current_multiplier < explosion_multiplier)
        {
            pthread_mutex_lock(&game_state_mutex);
            current_multiplier += MULTIPLIER_INCREMENT;
            if (current_multiplier >= explosion_multiplier)
            {
                current_multiplier = explosion_multiplier;
            }

            struct aviator_msg multiplier_msg = create_msg(MSG_MULTIPLIER, -1, current_multiplier, -1.0f, house_profit);

            pthread_mutex_lock(&players_mutex);
            for (int i = 0; i < MAX_PLAYERS; i++)
            {
                if (players[i].sock != 0 && !players[i].has_cashed_out && players[i].has_bet)
                {
                    send_message_to_client(players[i].sock, multiplier_msg);
                }
            }
            pthread_mutex_unlock(&players_mutex);

            log_server_event("multiplier", -1, current_multiplier, -1.0f, -1, -1.0f, -1.0f, -1.0f, -1.0f, house_profit);
            pthread_mutex_unlock(&game_state_mutex);
            usleep(MULTIPLIER_DELAY_US);
        }

        pthread_mutex_lock(&game_state_mutex);
        game_active = 0;
        log_server_event("explode", -1, explosion_multiplier, -1.0f, -1, -1.0f, -1.0f, -1.0f, -1.0f, house_profit);
        broadcast_message(create_msg(MSG_EXPLODE, -1, explosion_multiplier, -1.0f, house_profit));

        pthread_mutex_lock(&players_mutex);
        for (int i = 0; i < MAX_PLAYERS; i++)
        {
            if (players[i].sock != 0 && players[i].has_bet && !players[i].has_cashed_out)
            {
                players[i].current_profit -= players[i].bet_value;
                house_profit += players[i].bet_value;

                log_server_event("explode", players[i].id, explosion_multiplier, -1.0f, -1, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f);
                log_server_event("profit", players[i].id, -1.0f, -1.0f, -1, -1.0f, -1.0f, -1.0f, players[i].current_profit, -1.0f);

                send_message_to_client(players[i].sock, create_msg(MSG_PROFIT, players[i].id, -1.0f, players[i].current_profit, house_profit));
            }
        }
        pthread_mutex_unlock(&players_mutex);

        log_server_event("profit", -1, -1.0f, -1.0f, -1, -1.0f, -1.0f, -1.0f, -1.0f, house_profit);
        pthread_mutex_unlock(&game_state_mutex);

        sleep(5);
    }
    return NULL;
}

void *client_handler_thread(void *data)
{
    struct player *p = (struct player *)data;
    struct aviator_msg received_msg;
    char nickname_buffer[NICKNAME_LEN];

    ssize_t count = recv(p->sock, nickname_buffer, NICKNAME_LEN, 0);
    if (count <= 0)
    {
        if (count == 0)
            log_server_event("bye", p->id, -1.0f, -1.0f, -1, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f);
        else
            logexit("recv");

        close(p->sock);
        pthread_mutex_lock(&players_mutex);
        p->sock = 0;
        num_players--;
        pthread_mutex_unlock(&players_mutex);
        pthread_exit(EXIT_SUCCESS);
    }

    nickname_buffer[count] = '\0';
    strcpy(p->nickname, nickname_buffer);
    printf("[log] Player %d nickname: %s connected.\n", p->id, p->nickname);

    pthread_mutex_lock(&game_state_mutex);
    if (betting_open)
    {
        send_message_to_client(p->sock, create_msg(MSG_START, -1, time_remaining, p->current_profit, house_profit));
    }
    else if (game_active)
    {
        send_message_to_client(p->sock, create_msg(MSG_CLOSED, -1, -1.0f, p->current_profit, house_profit));
    }
    else
    {
        send_message_to_client(p->sock, create_msg(MSG_CLOSED, -1, -1.0f, p->current_profit, house_profit));
    }
    pthread_mutex_unlock(&game_state_mutex);

    while (1)
    {
        count = recv(p->sock, &received_msg, sizeof(struct aviator_msg), 0);
        if (count == 0)
        {
            log_server_event("bye", p->id, -1.0f, -1.0f, -1, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f);
            printf("[log] Player %d nickname: %s disconnected.\n", p->id, p->nickname);
            break;
        }
        else if (count == -1)
        {
            logexit("recv");
        }

        if (strcmp(received_msg.type, MSG_BET) == 0)
        {
            pthread_mutex_lock(&game_state_mutex);
            if (betting_open && !p->has_bet)
            {
                p->bet_value = received_msg.value;
                p->has_bet = 1;
                log_server_event("bet", p->id, -1.0f, -1.0f, -1, -1.0f, p->bet_value, -1.0f, -1.0f, -1.0f);
            }
            pthread_mutex_unlock(&game_state_mutex);
        }
        else if (strcmp(received_msg.type, MSG_CASHOUT) == 0)
        {
            pthread_mutex_lock(&game_state_mutex);
            if (game_active && p->has_bet && !p->has_cashed_out)
            {
                float winnings = p->bet_value * current_multiplier;
                p->current_profit += (winnings - p->bet_value);
                house_profit -= (winnings - p->bet_value);
                p->has_cashed_out = 1;

                log_server_event("cashout", p->id, current_multiplier, -1.0f, -1, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f);
                log_server_event("payout", p->id, -1.0f, -1.0f, -1, -1.0f, -1.0f, winnings, -1.0f, -1.0f);
                log_server_event("profit", p->id, -1.0f, -1.0f, -1, -1.0f, -1.0f, -1.0f, p->current_profit, -1.0f);

                send_message_to_client(p->sock, create_msg(MSG_PAYOUT, p->id, current_multiplier, p->current_profit, house_profit));
            }
            pthread_mutex_unlock(&game_state_mutex);
        }
        else if (strcmp(received_msg.type, MSG_BYE) == 0)
        {
            log_server_event("bye", p->id, -1.0f, -1.0f, -1, -1.0f, -1.0f, -1.0f, -1.0f, -1.0f);
            printf("[log] Player %d nickname: %s disconnected.\n", p->id, p->nickname);
            break;
        }
    }

    close(p->sock);
    pthread_mutex_lock(&players_mutex);
    p->sock = 0;
    num_players--;
    pthread_mutex_unlock(&players_mutex);

    pthread_exit(EXIT_SUCCESS);
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        usage(argc, argv);
    }

    struct sockaddr_storage storage;
    if (0 != server_sockaddr_init(argv[1], argv[2], &storage))
    {
        usage(argc, argv);
    }

    int s = socket(storage.ss_family, SOCK_STREAM, 0);
    if (s == -1)
    {
        logexit("socket");
    }

    int enable = 1;
    if (0 != setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)))
    {
        logexit("setsockopt");
    }

    struct sockaddr *addr = (struct sockaddr *)(&storage);
    if (0 != bind(s, addr, sizeof(storage)))
    {
        logexit("bind");
    }

    if (0 != listen(s, MAX_PLAYERS))
    {
        logexit("listen");
    }

    char addrstr[BUFSZ];
    addrtostr(addr, addrstr, BUFSZ);
    printf("bound to %s, waiting connections\n", addrstr);

    pthread_t game_tid;
    pthread_create(&game_tid, NULL, game_thread, NULL);

    int player_id_counter = 0;

    while (1)
    {
        struct sockaddr_storage cstorage;
        struct sockaddr *caddr = (struct sockaddr *)(&cstorage);
        socklen_t caddrlen = sizeof(cstorage);

        int csock = accept(s, caddr, &caddrlen);
        if (csock == -1)
        {
            logexit("accept");
        }

        pthread_mutex_lock(&players_mutex);
        if (num_players >= MAX_PLAYERS)
        {
            printf("[log] Max players reached. Connection rejected.\n");
            close(csock);
            pthread_mutex_unlock(&players_mutex);
            continue;
        }

        int player_idx = -1;
        for (int i = 0; i < MAX_PLAYERS; i++)
        {
            if (players[i].sock == 0)
            {
                player_idx = i;
                break;
            }
        }

        if (player_idx == -1)
        {
            printf("[log] No free player slot found. Connection rejected.\n");
            close(csock);
            pthread_mutex_unlock(&players_mutex);
            continue;
        }

        players[player_idx] = (struct player){
            .id = ++player_id_counter,
            .sock = csock,
            .current_profit = 0.0f,
            .has_bet = 0,
            .has_cashed_out = 0};
        num_players++;

        char caddrstr[BUFSZ];
        addrtostr(caddr, caddrstr, BUFSZ);
        printf("[log] connection from %s, assigned player_id: %d\n", caddrstr, players[player_idx].id);

        pthread_t tid;
        pthread_create(&tid, NULL, client_handler_thread, &players[player_idx]);
        pthread_detach(tid);

        pthread_mutex_unlock(&players_mutex);
    }

    pthread_join(game_tid, NULL);
    exit(EXIT_SUCCESS);
}