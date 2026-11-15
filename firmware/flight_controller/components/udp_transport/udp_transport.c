#include "udp_transport.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "command_protocol.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "wifi_manager.h"

#define UDP_PORT 5005
#define COMMAND_MAX_CHARS 95

// One UDP datagram is one ASCII command, with an optional trailing CR/LF.
static void handle_datagram(const uint8_t *data, size_t length,
                            char *reply, size_t reply_size)
{
    if (length == 0) {
        snprintf(reply, reply_size, "ERR EMPTY");
        return;
    }
    // No datagram may contain more than 95 command characters and CR/LF.
    while (length && (data[length - 1] == '\r' || data[length - 1] == '\n')) {
        --length;
    }
    if (length == 0) {
        snprintf(reply, reply_size, "ERR EMPTY");
        return;
    }
    if (length > COMMAND_MAX_CHARS) {
        snprintf(reply, reply_size, "ERR LINE_TOO_LONG");
        return;
    }
    for (size_t i = 0; i < length; ++i) {
        if (data[i] < 32 || data[i] > 126) {
            snprintf(reply, reply_size, "ERR BAD_CHARACTER");
            return;
        }
    }

    char command[COMMAND_MAX_CHARS + 1];
    memcpy(command, data, length);
    command[length] = '\0';
    command_protocol_handle(command, reply, reply_size);
}

void udp_transport_run(void *arg)
{
    (void)arg;
    uint8_t received[512];
    char reply[128];
    char packet[sizeof(reply) + 2];

    for (;;) {
        wifi_manager_wait_connected(portMAX_DELAY);
        uint32_t epoch = wifi_manager_epoch();
        int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
        if (fd < 0) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        struct timeval timeout = {.tv_sec = 0, .tv_usec = 500000};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

        struct sockaddr_in local = {0};
        local.sin_family = AF_INET;
        local.sin_port = htons(UDP_PORT);
        local.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(fd, (struct sockaddr *)&local, sizeof(local)) != 0) {
            close(fd);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // Disconnects or IP changes invalidate the socket. Recreate it.
        while (wifi_manager_connected() && wifi_manager_epoch() == epoch) {
            struct sockaddr_in peer = {0};
            socklen_t peer_len = sizeof(peer);
            int n = recvfrom(fd, received, sizeof(received), 0,
                             (struct sockaddr *)&peer, &peer_len);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                    continue;
                }
                break;
            }

            handle_datagram(received, (size_t)n, reply, sizeof(reply));
            int size = snprintf(packet, sizeof(packet), "%s\n", reply);
            if (size > 0 && (size_t)size < sizeof(packet)) {
                (void)sendto(fd, packet, (size_t)size, 0,
                             (struct sockaddr *)&peer, peer_len);
            }
        }
        close(fd);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
