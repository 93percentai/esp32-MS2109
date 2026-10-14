#pragma once
#include "esp_http_server.h"
esp_err_t audio_http_start(httpd_handle_t web);
void audio_http_stop(void);
