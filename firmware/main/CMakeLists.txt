idf_component_register(SRCS "app_main.c" "capture.c" "display.c" "video_store.c"
    "web_server.c" "deadline_send.c" "app_logs.c" "log_buffer.c" "form_codec.c" "wifi_mgr.c" "provisioning.c" "config_store.c"
    INCLUDE_DIRS "."
    REQUIRES esp_psram esp_driver_jpeg esp_driver_ppa esp_mm esp_wifi esp_netif esp_event nvs_flash
             esp_http_server esp_timer dns_server usb_host_uvc
    EMBED_TXTFILES "viewer.html" "logs.html")
