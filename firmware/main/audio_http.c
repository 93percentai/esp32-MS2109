#include "audio_http.h"
#include "audio.h"
#include "audio_control_form.h"
#include "deadline_send.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/tcp.h"
static httpd_handle_t s_server;
static atomic_bool s_stop;
/* The single synchronous audio HTTP task owns this absolute block deadline. */
static int64_t s_deadline;
extern const char audio_player[] asm("_binary_pcm_player_js_start");
static esp_err_t player(httpd_req_t *req)
{
    httpd_resp_set_type(req,"application/javascript");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,audio_player);
}
static esp_err_t unavailable(httpd_req_t *req,const char *message)
{
    httpd_resp_set_status(req,"503 Service Unavailable");return httpd_resp_sendstr(req,message);
}
static int send_audio(httpd_handle_t server,int socket,const char *data,size_t size,int flags)
{
    if(atomic_load(&s_stop)) return HTTPD_SOCK_ERR_FAIL;
    int result=deadline_send(socket,data,size,flags,s_deadline);
    return result>=0 ? result : HTTPD_SOCK_ERR_FAIL;
}
static esp_err_t status(httpd_req_t *req)
{
    audio_status_t audio;audio_status(&audio);
    int64_t age=audio.last_audio_us ? (esp_timer_get_time()-audio.last_audio_us)/1000 : -1;
    char json[512];
    snprintf(json,sizeof(json),"{\"enabled\":%s,\"connected\":%s,\"speaker_ready\":%s,"
        "\"rate\":48000,\"channels\":2,\"bits\":16,\"volume\":%u,\"mute\":%s,\"age_ms\":%"PRId64
        ",\"packets\":%"PRIu32",\"errors\":%"PRIu32",\"speaker_skipped\":%"PRIu32",\"network_skipped\":%"PRIu32"}",
        audio.enabled ? "true":"false",audio.connected ? "true":"false",audio.speaker_ready ? "true":"false",
        audio.volume,audio.muted ? "true":"false",age,audio.packets,audio.errors,audio.speaker_skipped,audio.network_skipped);
    httpd_resp_set_type(req,"application/json");httpd_resp_set_hdr(req,"Cache-Control","no-store");
    return httpd_resp_sendstr(req,json);
}
static esp_err_t control(httpd_req_t *req)
{
    char body[80];
    if(!req->content_len || req->content_len>=sizeof(body))
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Invalid audio control length");
    size_t received=0;
    while(received<req->content_len) {
        int n=httpd_req_recv(req,body+received,req->content_len-received);
        if(n<=0) return ESP_FAIL;
        received+=n;
    }
    body[received]=0;
    unsigned volume;bool mute;
    if(!audio_parse_control(body,&volume,&mute))
        return httpd_resp_send_err(req,HTTPD_400_BAD_REQUEST,"Use volume=0..100&mute=0 or 1");
    if(!audio_control(volume,mute))
        return unavailable(req,"Audio unavailable");
    return status(req);
}
static esp_err_t stream(httpd_req_t *req)
{
    audio_status_t state;audio_status(&state);
    httpd_resp_set_hdr(req,"Access-Control-Allow-Origin","*");
    if(!state.enabled || !state.connected)
        return unavailable(req,"Waiting for MS2109 HDMI audio");
    uint8_t *buffer=heap_caps_malloc(PCM_BLOCK_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!buffer) return httpd_resp_send_err(req,HTTPD_500_INTERNAL_SERVER_ERROR,"No memory for audio stream");
    int socket=httpd_req_to_sockfd(req),nodelay=1;
    setsockopt(socket,IPPROTO_TCP,TCP_NODELAY,&nodelay,sizeof(nodelay));
    esp_err_t err=httpd_sess_set_send_override(s_server,socket,send_audio);
    if(err!=ESP_OK) {free(buffer);return err;}
    httpd_resp_set_type(req,"application/octet-stream");
    httpd_resp_set_hdr(req,"Cache-Control","no-store");
    httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
    pcm_cursor_t cursor={0};pcm_info_t info;int64_t last_sent=esp_timer_get_time();
    while(!atomic_load(&s_stop)) {
        audio_status(&state);
        if(!state.connected) break;
        if(!audio_copy(&cursor,buffer,PCM_BLOCK_BYTES,&info,true)) {
            if(esp_timer_get_time()-last_sent>1000000) break;
            vTaskDelay(pdMS_TO_TICKS(2));continue;
        }
        uint8_t header[PCM_HEADER_BYTES];pcm_wire_header(header,&info);
        s_deadline=esp_timer_get_time()+100000; /* One shared 100 ms deadline per 20 ms block. */
        err=httpd_resp_send_chunk(req,(char *)header,sizeof(header));
        if(err==ESP_OK) err=httpd_resp_send_chunk(req,(char *)buffer,PCM_BLOCK_BYTES);
        if(err!=ESP_OK) break;
        last_sent=esp_timer_get_time();
    }
    free(buffer);
    if(err==ESP_OK) {
        s_deadline=esp_timer_get_time()+100000;
        err=httpd_resp_send_chunk(req,NULL,0);
    }
    return err;
}
void audio_http_stop(void)
{
    atomic_store(&s_stop,true);
    if(s_server) {httpd_stop(s_server);s_server=NULL;}
}
esp_err_t audio_http_start(httpd_handle_t web)
{
    const httpd_uri_t status_route={.uri="/audio/status",.method=HTTP_GET,.handler=status};
    const httpd_uri_t control_route={.uri="/audio/control",.method=HTTP_POST,.handler=control};
    esp_err_t err=httpd_register_uri_handler(web,&status_route);
    if(err==ESP_OK) err=httpd_register_uri_handler(web,&control_route);
    const httpd_uri_t player_route={.uri="/audio-player.js",.method=HTTP_GET,.handler=player};
    if(err==ESP_OK) err=httpd_register_uri_handler(web,&player_route);
    if(err!=ESP_OK) return err;
    httpd_config_t config=HTTPD_DEFAULT_CONFIG();
    config.server_port=82;config.ctrl_port+=2;config.max_open_sockets=1;config.lru_purge_enable=false;
    config.stack_size=4096;config.send_wait_timeout=1;
    atomic_store(&s_stop,false);
    err=httpd_start(&s_server,&config);
    if(err!=ESP_OK) return err;
    const httpd_uri_t stream_route={.uri="/audio.pcm",.method=HTTP_GET,.handler=stream};
    err=httpd_register_uri_handler(s_server,&stream_route);
    if(err!=ESP_OK) audio_http_stop();
    return err;
}
