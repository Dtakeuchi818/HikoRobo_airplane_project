#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "command_protocol.h"

static void check(const char *command, const char *expected)
{
    char response[128];
    command_protocol_handle(command, response, sizeof(response));
    if (strcmp(response, expected) != 0) {
        fprintf(stderr, "%s -> %s (expected %s)\n", command, response, expected);
        assert(0);
    }
}

int main(void)
{
    check("PING", "OK PONG");
    check("GET_RATE", "OK RATE 100");
    check("SET_RATE 250", "OK RATE 250");
    check("GET_STATUS", "OK STATUS READY RATE=250");
    check("SET_RATE 0", "ERR RATE_RANGE");
    check("SET_RATE foo", "ERR BAD_ARGUMENT");
    check("HELLO", "ERR UNKNOWN_COMMAND");
    check("SET_RATE 100", "OK RATE 100");
    puts("PASS: command_protocol native unit tests");
    return 0;
}
