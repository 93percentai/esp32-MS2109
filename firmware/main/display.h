#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
esp_err_t display_init(void);
/* Called only by the capture worker. Decode and scale outside the LVGL lock. */
esp_err_t display_jpeg(const uint8_t *data, size_t size, unsigned width, unsigned height);
void network_request_setup(void);
