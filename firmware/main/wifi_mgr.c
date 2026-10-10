/* Adapted from esp32-ethernetviawifi's Wi-Fi manager; see THIRD_PARTY.md. */
#include "wifi_mgr.h"
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define STA_OK BIT0
#define STA_FAIL BIT1
static const char *TAG = "wifi";
static SemaphoreHandle_t s_lock;
static EventGroupHandle_t s_events;
static esp_netif_t *s_sta;
static wifi_mgr_status_t s_status;
static atomic_bool s_suppressed, s_has_ip;
static uint8_t s_mac[6];

static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    bool retry = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (id == WIFI_EVENT_STA_CONNECTED) {
        wifi_event_sta_connected_t *event = data;
        memset(s_status.ssid, 0, sizeof(s_status.ssid));
        memcpy(s_status.ssid, event->ssid, event->ssid_len <= 32 ? event->ssid_len : 32);
        s_status.channel = event->channel;
        memcpy(s_status.bssid, event->bssid, 6);
        if (!atomic_load(&s_suppressed)) s_status.state = WIFI_MGR_CONNECTING;
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = data;
        atomic_store(&s_has_ip, false);
        s_status.rssi = 0;
        bool auth = event->reason == WIFI_REASON_AUTH_FAIL ||
                    event->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                    event->reason == WIFI_REASON_HANDSHAKE_TIMEOUT;
        if (!atomic_load(&s_suppressed)) {
            s_status.state = auth ? WIFI_MGR_BAD_AUTH : WIFI_MGR_DISCONNECTED;
            retry = !auth && s_status.ssid[0];
        }
        xEventGroupSetBits(s_events, STA_FAIL);
        ESP_LOGW(TAG, "Disconnected: reason=%u", event->reason);
    }
    xSemaphoreGive(s_lock);
    if (retry) esp_wifi_connect();
}

static void on_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == IP_EVENT_STA_GOT_IP) {
        atomic_store(&s_has_ip, true);
        xSemaphoreTake(s_lock, portMAX_DELAY);
        if (!atomic_load(&s_suppressed)) s_status.state = WIFI_MGR_CONNECTED;
        xSemaphoreGive(s_lock);
        xEventGroupSetBits(s_events, STA_OK);
        ip_event_got_ip_t *event = data;
        ESP_LOGI(TAG, "Browser: http://" IPSTR "/", IP2STR(&event->ip_info.ip));
    } else if (id == IP_EVENT_STA_LOST_IP) atomic_store(&s_has_ip, false);
}

esp_err_t wifi_mgr_init(bridge_config_t *cfg)
{
    s_lock = xSemaphoreCreateMutex();
    s_events = xEventGroupCreate();
    if (!s_lock || !s_events) return ESP_ERR_NO_MEM;
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta = esp_netif_create_default_wifi_sta();
    if (!s_sta) return ESP_ERR_NO_MEM;
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, on_ip, NULL));
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, s_mac));
    memcpy(s_status.mac, s_mac, 6);
    return ESP_OK;
}

static esp_err_t connect_to(const char *ssid, const char *password)
{
    wifi_config_t config = {0};
    size_t n = strlen(ssid);
    if (!n || n > 32 || strlen(password) > 63) return ESP_ERR_INVALID_ARG;
    /* Wi-Fi ssid field is 32 raw bytes, so support a full 32-byte SSID. */
    memcpy(config.sta.ssid, ssid, n);
    memcpy(config.sta.password, password, strlen(password));
    config.sta.threshold.authmode = password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    config.sta.pmf_cfg.capable = true;
    config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (err != ESP_OK) return err;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(s_status.ssid, sizeof(s_status.ssid), "%s", ssid);
    xSemaphoreGive(s_lock);
    return esp_wifi_connect();
}

esp_err_t wifi_mgr_apply(const bridge_config_t *cfg)
{
    atomic_store(&s_suppressed, true);
    atomic_store(&s_has_ip, false);
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(150));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    const char *ssid = config_active_ssid(cfg);
    wifi_mgr_set_state(ssid[0] ? WIFI_MGR_CONNECTING : WIFI_MGR_IDLE);
    atomic_store(&s_suppressed, false);
    return ssid[0] ? connect_to(ssid, config_active_pass(cfg)) : ESP_OK;
}

esp_err_t wifi_mgr_test_connect(const char *ssid, const char *password, int timeout_ms)
{
    atomic_store(&s_has_ip, false);
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(150));
    xEventGroupClearBits(s_events, STA_OK | STA_FAIL);
    esp_err_t err = connect_to(ssid, password ? password : "");
    if (err != ESP_OK) return err;
    EventBits_t bits = xEventGroupWaitBits(s_events, STA_OK | STA_FAIL,
                                          pdTRUE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    if ((bits & STA_OK) && atomic_load(&s_has_ip)) return ESP_OK;
    esp_wifi_disconnect();
    return bits & STA_FAIL ? ESP_FAIL : ESP_ERR_TIMEOUT;
}

int wifi_mgr_scan(wifi_scan_result_t *out, int max_out, bool reassociate)
{
    if (!out || max_out <= 0) return 0;
    wifi_mgr_set_state(WIFI_MGR_SCANNING);
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(150));
    wifi_scan_config_t config = {.show_hidden = true};
    if (esp_wifi_scan_start(&config, true) != ESP_OK) return 0;
    wifi_ap_record_t records[24];
    uint16_t count = 24;
    if (esp_wifi_scan_get_ap_records(&count, records) != ESP_OK) return 0;
    int n = 0;
    for (unsigned i = 0; i < count && n < max_out; ++i) {
        if (!records[i].ssid[0]) continue; /* Hidden SSIDs use the custom input. */
        bool duplicate = false;
        for (int j = 0; j < n; ++j)
            if (!strcmp(out[j].ssid, (char *)records[i].ssid)) duplicate = true;
        if (duplicate) continue;
        snprintf(out[n].ssid, sizeof(out[n].ssid), "%s", records[i].ssid);
        out[n].rssi = records[i].rssi;
        out[n].authmode = records[i].authmode;
        memcpy(out[n].bssid, records[i].bssid, 6);
        ++n;
    }
    return n;
}

void wifi_mgr_get_status(wifi_mgr_status_t *out)
{
    wifi_ap_record_t ap;
    bool info = esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_status;
    if (info) out->rssi = ap.rssi;
    xSemaphoreGive(s_lock);
}
bool wifi_mgr_is_connected(void) { return atomic_load(&s_has_ip); }
const uint8_t *wifi_mgr_sta_mac(void) { return s_mac; }
void wifi_mgr_set_suppress_bridge(bool suppress) { atomic_store(&s_suppressed, suppress); }
void wifi_mgr_set_state(wifi_mgr_state_t state)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.state = state;
    xSemaphoreGive(s_lock);
}
void wifi_mgr_get_ip(char *out, size_t capacity)
{
    esp_netif_ip_info_t ip;
    if (atomic_load(&s_has_ip) && esp_netif_get_ip_info(s_sta, &ip) == ESP_OK)
        snprintf(out, capacity, IPSTR, IP2STR(&ip.ip));
    else snprintf(out, capacity, "Connecting...");
}
