/*
 * SPDX-FileCopyrightText: 2021-2023 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 * Adapted captive DNS lifecycle/parser for MS2109 firmware.
 */
#include "dns_server.h"
#include "dns_packet.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"

struct dns_server_handle {
    atomic_bool started;
    SemaphoreHandle_t done;
    int socket;
    dns_entry_pair_t entry;
};

static void dns_server_task(void *arg)
{
    dns_server_handle_t handle = arg;
    while (atomic_load(&handle->started)) {
        uint8_t query[256], reply[272];
        struct sockaddr_in source;
        socklen_t source_size = sizeof(source);
        int size = recvfrom(handle->socket, query, sizeof(query), 0,
                            (struct sockaddr *)&source, &source_size);
        if (size < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
            break;
        }
        esp_ip4_addr_t ip = handle->entry.ip;
        if (handle->entry.if_key) {
            esp_netif_t *netif = esp_netif_get_handle_from_ifkey(handle->entry.if_key);
            esp_netif_ip_info_t info;
            if (!netif || esp_netif_get_ip_info(netif, &info) != ESP_OK) continue;
            ip = info.ip;
        }
        size_t len = captive_dns_reply(query, size, (const uint8_t *)&ip.addr, reply, sizeof(reply));
        if (len) sendto(handle->socket, reply, len, 0, (struct sockaddr *)&source, source_size);
    }
    close(handle->socket);
    /* No access to handle after signalling. stop_dns_server owns its lifetime. */
    xSemaphoreGive(handle->done);
    vTaskDelete(NULL);
}

dns_server_handle_t start_dns_server(dns_server_config_t *config)
{
    if (!config || config->num_of_entries != 1 || !config->item[0].name ||
        strcmp(config->item[0].name, "*")) return NULL;
    dns_server_handle_t handle = calloc(1, sizeof(*handle));
    if (!handle) return NULL;
    handle->done = xSemaphoreCreateBinary();
    handle->socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    struct timeval timeout = {.tv_usec = 250000};
    struct sockaddr_in local = {.sin_family = AF_INET, .sin_port = htons(53),
                                .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (!handle->done || handle->socket < 0 ||
        setsockopt(handle->socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
        setsockopt(handle->socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0 ||
        bind(handle->socket, (struct sockaddr *)&local, sizeof(local)) < 0) goto fail;
    handle->entry = config->item[0];
    atomic_store(&handle->started, true);
    if (xTaskCreate(dns_server_task, "captive_dns", 4096, handle, 5, NULL) != pdPASS) goto fail;
    return handle;
fail:
    if (handle->socket >= 0) close(handle->socket);
    if (handle->done) vSemaphoreDelete(handle->done);
    free(handle);
    return NULL;
}

void stop_dns_server(dns_server_handle_t handle)
{
    if (!handle) return;
    atomic_store(&handle->started, false);
    xSemaphoreTake(handle->done, portMAX_DELAY);
    vSemaphoreDelete(handle->done);
    free(handle);
}
