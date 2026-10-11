#pragma once
#include "esp_http_server.h"
/* Install before application initialization, preserving normal UART logs. */
void app_logs_init(void);
/* Available on the LAN server and the setup portal. */
esp_err_t app_logs_register(httpd_handle_t server);
