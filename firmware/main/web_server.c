#include "sdkconfig.h"
#include "web_server.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "video_store.h"
#include "wifi_mgr.h"
#include "app_logs.h"
#include "audio_http.h"
#include "deadline_send.h"
#include "lwip/sockets.h"
#include "lwip/tcp.h"
#include "esp_log.h"

static httpd_handle_t s_web, s_stream;
static atomic_bool s_stop_stream;
/* Owned by the one synchronous stream-server task; all chunks share a budget. */
static int64_t s_send_deadline;
static int stream_send(httpd_handle_t server, int socket, const char *data, size_t size, int flags)
{
    if (atomic_load(&s_stop_stream)) return HTTPD_SOCK_ERR_FAIL;
    int result = deadline_send(socket, data, size, flags, s_send_deadline);
    return result >= 0 ? result : HTTPD_SOCK_ERR_FAIL;
}
extern const char viewer_start[] asm("_binary_viewer_html_start");

static esp_err_t root(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_sendstr(req, viewer_start);
}

static esp_err_t status(httpd_req_t *req)
{
    video_status_t video;
    video_status(&video);
    wifi_mgr_status_t wifi;
    wifi_mgr_get_status(&wifi);
    int64_t age = video.last_frame_us ? (esp_timer_get_time() - video.last_frame_us) / 1000 : -1;
    char json[1024];
    snprintf(json, sizeof(json), "{\"connected\":%s,\"width\":%u,\"height\":%u,"
        "\"sequence\":%" PRIu32 ",\"jpeg_bytes\":%u,\"dropped\":%" PRIu32
        ",\"errors\":%" PRIu32 ",\"decode_us\":%" PRIu32 ",\"age_ms\":%" PRId64
        ",\"wifi_rssi\":%d,\"display_frames\":%" PRIu32 ",\"display_errors\":%" PRIu32
        ",\"display_sequence\":%" PRIu32 ",\"jpeg_decode_us\":%" PRIu32 ",\"ppa_scale_us\":%" PRIu32
        ",\"present_us\":%" PRIu32 ",\"display_age_us\":%" PRIu32 ",\"stream_frames\":%" PRIu32
        ",\"stream_skipped\":%" PRIu32 ",\"stream_aborts\":%" PRIu32 ",\"stream_send_us\":%" PRIu32
        ",\"stream_active\":%s,\"display_backend\":\"native-jpeg-ppa\"}",
        video.connected ? "true" : "false", video.width, video.height, video.sequence,
        (unsigned)video.jpeg_bytes, video.dropped, video.errors, video.decode_us, age, wifi.rssi,
        video.display_frames, video.display_errors, video.display_sequence, video.jpeg_decode_us, video.scale_us,
        video.present_us, video.display_age_us, video.stream_frames, video.stream_skipped,
        video.stream_aborts, video.stream_send_us, video.stream_active ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, json);
}

static esp_err_t snapshot(httpd_req_t *req)
{
    uint8_t *copy = heap_caps_malloc(VIDEO_MAX_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!copy) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    size_t size; uint32_t sequence;
    esp_err_t err;
    if (video_copy(copy, VIDEO_MAX_BYTES, 0, &size, &sequence)) {
        httpd_resp_set_type(req, "image/jpeg");
        httpd_resp_set_hdr(req, "Cache-Control", "no-store");
        err = httpd_resp_send(req, (char *)copy, size);
    } else {
        httpd_resp_set_status(req, "503 Service Unavailable");
        err = httpd_resp_sendstr(req, "Waiting for a fresh capture frame");
    }
    free(copy);
    return err;
}

static esp_err_t stream(httpd_req_t *req)
{
    uint8_t *copy = heap_caps_malloc(VIDEO_MAX_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!copy) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    int socket = httpd_req_to_sockfd(req), nodelay = 1;
    setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
    if (httpd_sess_set_send_override(s_stream, socket, stream_send) != ESP_OK) {
        free(copy); return ESP_FAIL;
    }
    video_set_stream_active(true);
    httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=ms2109frame");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    uint32_t sequence = 0;
    int64_t last_sent = esp_timer_get_time();
    esp_err_t err = ESP_OK;
    while (wifi_mgr_is_connected() && !atomic_load(&s_stop_stream)) {
        size_t size;
        uint32_t previous = sequence;
        if (!video_copy(copy, VIDEO_MAX_BYTES, sequence, &size, &sequence)) {
            if (esp_timer_get_time() - last_sent > 5000000) break;
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }
        char header[128];
        int n = snprintf(header, sizeof(header), "\r\n--ms2109frame\r\nContent-Type: image/jpeg\r\n"
                         "Content-Length: %u\r\n\r\n", (unsigned)size);
        int64_t send_start = esp_timer_get_time();
        s_send_deadline = send_start + CONFIG_CAPTURE_STREAM_SEND_MS * 1000LL;
        err = httpd_resp_send_chunk(req, header, n);
        if (err == ESP_OK) err = httpd_resp_send_chunk(req, (char *)copy, size);
        video_stream_result(esp_timer_get_time() - send_start,
                            previous && sequence > previous ? sequence - previous - 1 : 0, err == ESP_OK);
        if (err != ESP_OK) {
            ESP_LOGW("stream", "Frame send aborted within %u ms budget; slow link or disconnected client",
                     CONFIG_CAPTURE_STREAM_SEND_MS);
            break;
        }
        last_sent = esp_timer_get_time();
    }
    video_set_stream_active(false);
    free(copy);
    if (err == ESP_OK) {
        s_send_deadline = esp_timer_get_time() + CONFIG_CAPTURE_STREAM_SEND_MS * 1000LL;
        err = httpd_resp_send_chunk(req, NULL, 0);
    }
    return err;
}

void web_server_stop(void)
{
    atomic_store(&s_stop_stream, true);
    audio_http_stop();
    /* stream loop exits on lost STA IP; send timeout bounds teardown. */
    if (s_stream) { httpd_stop(s_stream); s_stream = NULL; }
    if (s_web) { httpd_stop(s_web); s_web = NULL; }
}

esp_err_t web_server_start(void)
{
    if (s_web) return ESP_OK;
    atomic_store(&s_stop_stream, false);
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.stack_size = 6144;
    config.max_open_sockets = 4;
    config.lru_purge_enable = true;
    config.send_wait_timeout = 1;
    esp_err_t err = httpd_start(&s_web, &config);
    if (err != ESP_OK) return err;
    err = app_logs_register(s_web);
    if (err != ESP_OK) goto fail;
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = root},
        {.uri = "/status", .method = HTTP_GET, .handler = status},
        {.uri = "/snapshot.jpg", .method = HTTP_GET, .handler = snapshot},
    };
    for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        err = httpd_register_uri_handler(s_web, &routes[i]);
        if (err != ESP_OK) goto fail;
    }
    /* Separate HTTP task for long-lived stream: status and snapshot stay responsive. */
    config.server_port = 81;
    config.ctrl_port += 1;
    config.max_open_sockets = 1;
    config.lru_purge_enable = false;
    err = httpd_start(&s_stream, &config);
    if (err != ESP_OK) goto fail;
    const httpd_uri_t route = {.uri = "/stream", .method = HTTP_GET, .handler = stream};
    err = httpd_register_uri_handler(s_stream, &route);
    if (err == ESP_OK) err=audio_http_start(s_web);
    if (err == ESP_OK) return ESP_OK;
fail:
    web_server_stop();
    return err;
}
