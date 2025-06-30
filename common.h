#pragma once

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define STR_LEN 11

struct aviator_msg
{
    int32_t player_id;
    float value;
    char type[STR_LEN];
    float player_profit;
    float house_profit;
};

void logexit(const char *msg);

int addrparse(const char *addrstr, const char *portstr, struct sockaddr_storage *storage);

void addrtostr(const struct sockaddr *addr, char *str, size_t strsize);

int server_sockaddr_init(const char *proto, const char *portstr, struct sockaddr_storage *storage);