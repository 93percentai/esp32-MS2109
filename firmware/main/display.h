#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
esp_err_t display_init(void);
/* Wake the independent LCD worker; it copies the newest published JPEG. */
void display_notify(void);
void network_request_setup(void);
