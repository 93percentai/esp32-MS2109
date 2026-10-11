#include "sdkconfig.h"
#include "app_logs.h"
#include "log_buffer.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static char s_storage[CONFIG_CAPTURE_LOG_KB * 1024];
static log_buffer_t s_buffer;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static vprintf_like_t s_uart = vprintf;
static uint64_t s_truncated;
extern const char logs_page[] asm("_binary_logs_html_start");

static int capture_log(const char *format, va_list args)
{
    char fragment[768];
    va_list copy;
    va_copy(copy, args);
    int size = vsnprintf(fragment, sizeof(fragment), format, copy);
    va_end(copy);
    /* Forward the original arguments; the browser buffer is an additional sink. */
    int result = s_uart(format, args);
    if (size > 0) {
        size_t retained = (size_t)size < sizeof(fragment) ? (size_t)size : sizeof(fragment) - 1;
        portENTER_CRITICAL(&s_lock);
        log_buffer_append(&s_buffer, fragment, retained);
        if ((size_t)size > retained) {
            s_truncated += (size_t)size - retained;
            const char marker[] = " [log fragment truncated]\n";
            log_buffer_append(&s_buffer, marker, sizeof(marker) - 1);
        }
        portEXIT_CRITICAL(&s_lock);
    }
    return result;
}

void app_logs_init(void)
{
    log_buffer_init(&s_buffer, s_storage, sizeof(s_storage));
    s_uart = esp_log_set_vprintf(capture_log);
}

static esp_err_t page(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_sendstr(req, logs_page);
}

static esp_err_t recent(httpd_req_t *req)
{
    char *snapshot = heap_caps_malloc(sizeof(s_storage) + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!snapshot) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory for log snapshot");
    uint64_t overwritten, truncated;
    portENTER_CRITICAL(&s_lock);
    size_t size = log_buffer_copy(&s_buffer, snapshot, sizeof(s_storage) + 1);
    overwritten = s_buffer.overwritten;
    truncated = s_truncated;
    portEXIT_CRITICAL(&s_lock);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    char header[256];
    int n = snprintf(header, sizeof(header),
        "Recent application logs (%u bytes retained; %" PRIu64 " overwritten; %" PRIu64 " truncated).\n"
        "Buffer resets on reboot; ROM/bootloader and raw printf output are not captured.\n\n",
        (unsigned)size, overwritten, truncated);
    esp_err_t err = httpd_resp_send_chunk(req, header, n);
    if (err == ESP_OK && size) err = httpd_resp_send_chunk(req, snapshot, size);
    free(snapshot);
    if (err == ESP_OK) err = httpd_resp_send_chunk(req, NULL, 0);
    return err;
}

esp_err_t app_logs_register(httpd_handle_t server)
{
    const httpd_uri_t routes[] = {
        {.uri = "/logs", .method = HTTP_GET, .handler = page},
        {.uri = "/logs.txt", .method = HTTP_GET, .handler = recent},
    };
    for (unsigned i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        esp_err_t err = httpd_register_uri_handler(server, &routes[i]);
        if (err != ESP_OK) return err;
    }
    return ESP_OK;
}
