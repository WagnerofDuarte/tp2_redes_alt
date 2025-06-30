#include "common.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define BUFSZ 1024
#define NICKNAME_MAX_LEN 13

static const char MSG_START[] = "start";
static const char MSG_CLOSED[] = "closed";
static const char MSG_MULTIPLIER[] = "multiplier";
static const char MSG_PAYOUT[] = "payout";
static const char MSG_EXPLODE[] = "explode";
static const char MSG_PROFIT[] = "profit";
static const char MSG_BYE[] = "bye";
static const char MSG_BET[] = "bet";
static const char MSG_CASHOUT[] = "cashout";
static const char CMD_QUIT[] = "Q";
static const char CMD_CASHOUT[] = "C";

int is_float(const char *s)
{
    char *endptr;
    strtof(s, &endptr);
    return !(endptr == s || *endptr != '\0');
}

void usage(int argc, char **argv)
{
    printf("Error: Invalid number of arguments\n");
    exit(EXIT_FAILURE);
}

static inline struct aviator_msg create_client_msg(const char *type, float value, float player_profit)
{
    struct aviator_msg msg;
    msg.player_id = 0;
    msg.value = value;
    strcpy(msg.type, type);
    msg.player_profit = player_profit;
    msg.house_profit = 0.0f;
    return msg;
}

static void handle_start_msg(struct aviator_msg msg, float *player_profit, const char *nickname)
{
    printf("Rodada aberta! Digite o valor da aposta ou digite [Q] para sair (%.0f segundos restantes):\n", msg.value);
}

static void handle_closed_msg(struct aviator_msg msg, float *player_profit, const char *nickname)
{
    printf("Apostas encerradas! Não é mais possível apostar nesta rodada.\n");
}

static void handle_multiplier_msg(struct aviator_msg msg, float *player_profit, const char *nickname)
{
    printf("Multiplicador atual: %.2fx\n", msg.value);
}

static void handle_payout_msg(struct aviator_msg msg, float *player_profit, const char *nickname)
{
    printf("Você sacou em %.2fx e ganhou R$ %.2f!\n", msg.value, msg.player_profit);
    *player_profit = msg.player_profit;
    printf("Profit atual: R$ %.2f\n", *player_profit);
}

static void handle_explode_msg(struct aviator_msg msg, float *player_profit, const char *nickname)
{
    printf("Aviãozinho explodiu em: %.2fx\n", msg.value);
}

static void handle_profit_msg(struct aviator_msg msg, float *player_profit, const char *nickname)
{
    if (msg.player_id != -1)
    {
        if (msg.player_profit < *player_profit)
        {
            printf("Você perdeu R$ %.2f. Tente novamente na próxima rodada! Aviãozinho tá pagando :)\n",
                   *player_profit - msg.player_profit);
        }
        *player_profit = msg.player_profit;
        printf("Profit atual: R$ %.2f\n", *player_profit);
    }
    else
    {
        printf("Profit da casa: R$ %.2f\n", msg.house_profit);
    }
}

static void handle_bye_msg(struct aviator_msg msg, float *player_profit, const char *nickname)
{
    if (msg.player_id == -1)
    {
        printf("O servidor caiu, mas sua esperança pode continuar de pé. Até breve!\n");
    }
    else
    {
        printf("Aposte com responsabilidade. A plataforma é nova e tá com horário bugado. Volte logo, %s.\n", nickname);
    }
}

void handle_server_message(struct aviator_msg msg, float *player_current_profit, const char *nickname)
{
    static const struct
    {
        const char *type;
        void (*handler)(struct aviator_msg, float *, const char *);
    } handlers[] = {
        {MSG_START, handle_start_msg},
        {MSG_CLOSED, handle_closed_msg},
        {MSG_MULTIPLIER, handle_multiplier_msg},
        {MSG_PAYOUT, handle_payout_msg},
        {MSG_EXPLODE, handle_explode_msg},
        {MSG_PROFIT, handle_profit_msg},
        {MSG_BYE, handle_bye_msg}};

    for (size_t i = 0; i < sizeof(handlers) / sizeof(handlers[0]); i++)
    {
        if (strcmp(msg.type, handlers[i].type) == 0)
        {
            handlers[i].handler(msg, player_current_profit, nickname);
            return;
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 5 || strcmp(argv[3], "-nick") != 0)
    {
        usage(argc, argv);
    }

    const char *nickname = argv[4];
    if (strlen(nickname) > NICKNAME_MAX_LEN)
    {
        printf("Error: Nickname too long (max %d)\n", NICKNAME_MAX_LEN);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_storage storage;
    if (0 != addrparse(argv[1], argv[2], &storage))
    {
        usage(argc, argv);
    }

    int s = socket(storage.ss_family, SOCK_STREAM, 0);
    if (s == -1)
    {
        logexit("socket");
    }

    struct sockaddr *addr = (struct sockaddr *)(&storage);
    if (0 != connect(s, addr, sizeof(storage)))
    {
        logexit("connect");
    }

    printf("connected to %s\n", argv[1]);

    if (send(s, nickname, strlen(nickname) + 1, 0) == -1)
    {
        logexit("send nickname");
    }

    float player_current_profit = 0.0f;

    fd_set master;
    FD_ZERO(&master);
    FD_SET(STDIN_FILENO, &master);
    FD_SET(s, &master);

    while (1)
    {
        fd_set read_fds = master;
        if (select(s + 1, &read_fds, NULL, NULL, NULL) == -1)
        {
            logexit("select");
        }

        if (FD_ISSET(s, &read_fds))
        {
            struct aviator_msg server_msg;
            ssize_t count = recv(s, &server_msg, sizeof(struct aviator_msg), 0);
            if (count == 0)
            {
                printf("O servidor caiu, mas sua esperança pode continuar de pé. Até breve!\n");
                break;
            }
            else if (count == -1)
            {
                logexit("recv");
            }
            handle_server_message(server_msg, &player_current_profit, nickname);
        }

        if (FD_ISSET(STDIN_FILENO, &read_fds))
        {
            char input_buf[BUFSZ];
            if (fgets(input_buf, BUFSZ, stdin) == NULL)
            {
                break;
            }
            input_buf[strcspn(input_buf, "\n")] = 0;

            struct aviator_msg client_msg;

            if (strcmp(input_buf, CMD_QUIT) == 0)
            {
                client_msg = create_client_msg(MSG_BYE, 0.0f, player_current_profit);
                send(s, &client_msg, sizeof(struct aviator_msg), 0);
                printf("Aposte com responsabilidade. A plataforma é nova e tá com horário bugado. Volte logo, %s.\n", nickname);
                break;
            }
            else if (strcmp(input_buf, CMD_CASHOUT) == 0)
            {
                client_msg = create_client_msg(MSG_CASHOUT, 0.0f, player_current_profit);
                send(s, &client_msg, sizeof(struct aviator_msg), 0);
            }
            else if (is_float(input_buf))
            {
                float bet_value = atof(input_buf);
                if (bet_value <= 0)
                {
                    printf("Error: Invalid bet value\n");
                }
                else
                {
                    client_msg = create_client_msg(MSG_BET, bet_value, player_current_profit);
                    send(s, &client_msg, sizeof(struct aviator_msg), 0);
                    printf("Aposta recebida: R$ %.2f\n", bet_value);
                }
            }
            else
            {
                printf("Error: Invalid command\n");
            }
        }
    }

    close(s);
    exit(EXIT_SUCCESS);
}