#include <stdatomic.h>
#include <stdlib.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "capture.h"
#include "config_store.h"
#include "display.h"
#include "provisioning.h"
#include "video_store.h"
#include "web_server.h"
#include "wifi_mgr.h"

static bridge_config_t s_config;
static atomic_bool s_setup_requested;
void network_request_setup(void) { atomic_store(&s_setup_requested, true); }

static void network_task(void *arg)
{
    ESP_ERROR_CHECK(provisioning_apply_or_start(&s_config));
    int64_t disconnected_at = esp_timer_get_time();
    bool web_running = false;
    while (true) {
        bool setup = atomic_exchange(&s_setup_requested, false);
        bool portal = provisioning_is_active();
        bool connected = wifi_mgr_is_connected();
        if (connected) disconnected_at = esp_timer_get_time();
        if (!portal && (setup || (!connected && esp_timer_get_time() - disconnected_at > 30000000))) {
            if (web_running) { web_server_stop(); web_running = false; }
            ESP_ERROR_CHECK(provisioning_start(&s_config));
        } else if (!portal && connected && !web_running) {
            esp_err_t err = web_server_start();
            web_running = err == ESP_OK;
            if (err != ESP_OK) ESP_LOGW("network", "HTTP start: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    config_load(&s_config);
    ESP_ERROR_CHECK(video_store_init());
    ESP_ERROR_CHECK(wifi_mgr_init(&s_config));
    ESP_ERROR_CHECK(provisioning_init());
    ESP_ERROR_CHECK(display_init());
    ESP_ERROR_CHECK(capture_init());
    if (xTaskCreate(network_task, "network", 8192, NULL, 5, NULL) != pdPASS) abort();
}
