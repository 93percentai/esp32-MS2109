#pragma once
#include <stddef.h>
#include <stdint.h>
/* Single-question captive DNS. ip is four bytes in network order. */
size_t captive_dns_reply(const uint8_t *query, size_t size, const uint8_t ip[4],
                         uint8_t *reply, size_t capacity);
