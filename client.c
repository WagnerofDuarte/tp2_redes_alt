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

// Função para validar se a string é um número float
int is_float(const char *s) {
    char *endptr;
    strtof(s, &endptr);
    if (endptr == s || *endptr != '\0') {
        return 0; // Não é um float válido
    }
    return 1;
}

void usage(int argc, char **argv) {
    printf("Error: Invalid number of arguments\n");
    exit(EXIT_FAILURE);
}

void handle_server_message(struct aviator_msg msg, float *player_current_profit, char *nickname) {
    if (strcmp(msg.type, "start") == 0) {
        printf("Rodada aberta! Digite o valor da aposta ou digite [Q] para sair (%.0f segundos restantes):\n", msg.value);
    } else if (strcmp(msg.type, "closed") == 0) {
        printf("Apostas encerradas! Não é mais possível apostar nesta rodada.\n");
    } else if (strcmp(msg.type, "multiplier") == 0) {
        printf("Multiplicador atual: %.2fx\n", msg.value);
    } else if (strcmp(msg.type, "payout") == 0) {
        printf("Você sacou em %.2fx e ganhou R$ %.2f!\n", msg.value, msg.player_profit);
        *player_current_profit = msg.player_profit;
        printf("Profit atual: R$ %.2f\n", *player_current_profit);
    } else if (strcmp(msg.type, "explode") == 0) {
        printf("Aviãozinho explodiu em: %.2fx\n", msg.value);
    } else if (strcmp(msg.type, "profit") == 0) {
        if (msg.player_id != -1) { // Profit individual
            if (msg.player_profit < *player_current_profit) { // Se o profit diminuiu, significa que perdeu
                printf("Você perdeu R$ %.2f. Tente novamente na próxima rodada! Aviãozinho tá pagando :)\n", (*player_current_profit - msg.player_profit));
            }
            *player_current_profit = msg.player_profit;
            printf("Profit atual: R$ %.2f\n", *player_current_profit);
        } else { // Profit da casa
            printf("Profit da casa: R$ %.2f\n", msg.house_profit);
        }
    } else if (strcmp(msg.type, "bye") == 0) {
        if (msg.player_id == -1) { // Servidor encerrou
            printf("O servidor caiu, mas sua esperança pode continuar de pé. Até breve!\n");
        } else { // Cliente encerrou
            printf("Aposte com responsabilidade. A plataforma é nova e tá com horário bugado. Volte logo, %s.\n", nickname);
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 5 || strcmp(argv[3], "-nick") != 0) {
        usage(argc, argv);
    }

    char *nickname = argv[4];
    if (strlen(nickname) > NICKNAME_MAX_LEN) {
        printf("Error: Nickname too long (max %d)\n", NICKNAME_MAX_LEN);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_storage storage;
    if (0 != addrparse(argv[1], argv[2], &storage)) {
        usage(argc, argv);
    }

    int s;
    s = socket(storage.ss_family, SOCK_STREAM, 0);
    if (s == -1) {
        logexit("socket");
    }
    struct sockaddr *addr = (struct sockaddr *)(&storage);
    if (0 != connect(s, addr, sizeof(storage))) {
        logexit("connect");
    }

    printf("connected to %s\n", argv[1]);

    // Enviar nickname para o servidor como uma string separada
    send(s, nickname, strlen(nickname) + 1, 0); // +1 para incluir o null terminator

    float player_current_profit = 0.0;

    fd_set master;
    FD_ZERO(&master);
    FD_SET(STDIN_FILENO, &master);
    FD_SET(s, &master);

    int max_fd = s;

    while (1) {
        fd_set read_fds = master;
        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) == -1) {
            logexit("select");
        }

        if (FD_ISSET(s, &read_fds)) {
            // Dados do servidor
            struct aviator_msg server_msg;
            ssize_t count = recv(s, &server_msg, sizeof(struct aviator_msg), 0);
            if (count == 0) {
                printf("Servidor desconectou.\n");
                break;
            } else if (count == -1) {
                logexit("recv");
            }
            handle_server_message(server_msg, &player_current_profit, nickname);
        }

        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            // Input do usuário
            char input_buf[BUFSZ];
            if (fgets(input_buf, BUFSZ, stdin) == NULL) {
                // EOF ou erro
                break;
            }
            input_buf[strcspn(input_buf, "\n")] = 0; // Remover newline

            struct aviator_msg client_msg;
            client_msg.player_id = 0; // Será preenchido pelo servidor
            client_msg.player_profit = player_current_profit;
            client_msg.house_profit = 0.0;
            // strcpy(client_msg.nickname, nickname); // Removido

            if (strcmp(input_buf, "Q") == 0) {
                strcpy(client_msg.type, "bye");
                client_msg.value = 0.0;
                send(s, &client_msg, sizeof(struct aviator_msg), 0);
                printf("Aposte com responsabilidade. A plataforma é nova e tá com horário bugado. Volte logo, %s.\n", nickname);
                break;
            } else if (strcmp(input_buf, "C") == 0) {
                strcpy(client_msg.type, "cashout");
                client_msg.value = 0.0;
                send(s, &client_msg, sizeof(struct aviator_msg), 0);
            } else if (is_float(input_buf)) {
                float bet_value = atof(input_buf);
                if (bet_value <= 0) {
                    printf("Error: Invalid bet value\n");
                } else {
                    strcpy(client_msg.type, "bet");
                    client_msg.value = bet_value;
                    send(s, &client_msg, sizeof(struct aviator_msg), 0);
                    printf("Aposta recebida: R$ %.2f\n", bet_value);
                }
            } else {
                printf("Error: Invalid command\n");
            }
        }
    }

    close(s);
    exit(EXIT_SUCCESS);
}