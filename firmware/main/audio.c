#include "sdkconfig.h"
#include "audio.h"
#include <stdatomic.h>
#include <string.h>
#include <stdlib.h>
#include "bsp/esp-bsp.h"
#include "esp_codec_dev.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "usb/uac_host.h"

static const char *TAG="audio";
static SemaphoreHandle_t s_lock;
static QueueHandle_t s_devices,s_removed;
static pcm_ring_t s_ring;
static audio_status_t s_status={.volume=CONFIG_CAPTURE_AUDIO_VOLUME};
static atomic_bool s_disconnected, s_transfer_error;
static esp_codec_dev_handle_t s_speaker;
typedef struct {uint8_t addr,iface;} device_event_t;

void audio_status(audio_status_t *out)
{
    if (!s_lock) { *out=s_status; return; }
    xSemaphoreTake(s_lock,portMAX_DELAY);*out=s_status;xSemaphoreGive(s_lock);
}
bool audio_control(unsigned volume,bool muted)
{
    if (!s_lock || volume>100) return false;
    xSemaphoreTake(s_lock,portMAX_DELAY);
    if(!s_status.enabled) {xSemaphoreGive(s_lock);return false;}
    s_status.volume=volume;s_status.muted=muted;
    xSemaphoreGive(s_lock);return true;
}
bool audio_copy(pcm_cursor_t *cursor,uint8_t *out,size_t capacity,pcm_info_t *info,bool network)
{
    if (!s_lock) return false;
    xSemaphoreTake(s_lock,portMAX_DELAY);
    bool ready=s_status.connected && s_status.last_audio_us &&
        esp_timer_get_time()-s_status.last_audio_us<100000 &&
        pcm_ring_read(&s_ring,cursor,out,capacity,3,info);
    if(ready) {
        if(network) s_status.network_skipped+=info->skipped;
        else s_status.speaker_skipped+=info->skipped;
    }
    xSemaphoreGive(s_lock);return ready;
}
static void set_connected(bool connected)
{
    xSemaphoreTake(s_lock,portMAX_DELAY);
    s_status.connected=connected;s_status.last_audio_us=0;pcm_ring_reset(&s_ring);
    xSemaphoreGive(s_lock);
}
static void error_count(void)
{
    xSemaphoreTake(s_lock,portMAX_DELAY);++s_status.errors;xSemaphoreGive(s_lock);
}
static void device_event(uac_host_device_handle_t handle,uac_host_device_event_t event,void *arg)
{
    /* The application owns close; callbacks never read, play or close a handle. */
    if(event==UAC_HOST_DRIVER_EVENT_DISCONNECTED) xQueueSend(s_removed,&handle,0);
    if(event==UAC_HOST_DEVICE_EVENT_TRANSFER_ERROR) atomic_store(&s_transfer_error,true);
}
static void driver_event(uint8_t addr,uint8_t iface,uac_host_driver_event_t event,void *arg)
{
    if(event!=UAC_HOST_DRIVER_EVENT_RX_CONNECTED) return;
    device_event_t candidate={addr,iface};xQueueSend(s_devices,&candidate,0);
}
static bool is_capture_card(const uac_host_dev_info_t *info)
{
    return info->PID==0x2109 && (info->VID==0x345f || info->VID==0x534d) &&
        (!CONFIG_CAPTURE_VID || CONFIG_CAPTURE_VID==info->VID) &&
        (!CONFIG_CAPTURE_PID || CONFIG_CAPTURE_PID==info->PID);
}
static esp_err_t open_audio(const device_event_t *candidate,uac_host_device_handle_t *handle)
{
    uac_host_device_config_t config={.addr=candidate ? candidate->addr : 0,
        .iface_num=candidate ? candidate->iface : 3,.buffer_size=PCM_BLOCK_BYTES*2,
        .buffer_threshold=PCM_BLOCK_BYTES,.callback=device_event};
    esp_err_t err;
    if(candidate) err=uac_host_device_open(&config,handle);
    else {
        /* Cover devices enumerated before driver installation. MS2109 AS is interface 3. */
        err=uac_host_device_open_with_vid_pid(0x345f,0x2109,&config,handle);
        if(err==ESP_ERR_NOT_FOUND) err=uac_host_device_open_with_vid_pid(0x534d,0x2109,&config,handle);
    }
    if(err!=ESP_OK) return err;
    uac_host_dev_info_t info;
    err=uac_host_get_device_info(*handle,&info);
    if(err==ESP_OK && !is_capture_card(&info)) err=ESP_ERR_NOT_SUPPORTED;
    if(err==ESP_OK) {
        ESP_LOGI(TAG,"MS2109 audio address=%u interface=%u VID:PID=%04x:%04x",info.addr,info.iface_num,info.VID,info.PID);
        uac_host_stream_config_t stream={.channels=PCM_CHANNELS,.bit_resolution=PCM_BITS,.sample_freq=PCM_RATE};
        err=uac_host_device_start(*handle,&stream);
    }
    if(err!=ESP_OK) {
        esp_err_t close=uac_host_device_close(*handle);
        if(close==ESP_OK) *handle=NULL;
        else atomic_store(&s_transfer_error,true);
    }
    return err;
}
static void usb_audio_task(void *arg)
{
    uint8_t *buffer=arg;
    uac_host_device_handle_t handle=NULL;
    size_t filled=0;
    int64_t last_data=0,last_retry=0;
    while(true) {
        uac_host_device_handle_t removed;
        while(xQueueReceive(s_removed,&removed,0)==pdPASS) {
            if(!handle || handle==removed) {handle=removed;atomic_store(&s_disconnected,true);}
            else uac_host_device_close(removed);
        }
        if(handle && (atomic_load(&s_disconnected) || atomic_exchange(&s_transfer_error,false) ||
                esp_timer_get_time()-last_data>1000000)) {
            set_connected(false);
            ESP_LOGW(TAG,"Audio disconnected/stalled; reopening independently of video");
            /* Only this task closes; driver callback asks the user to delete on removal. */
            esp_err_t err=uac_host_device_close(handle);
            if(err!=ESP_OK && err!=ESP_ERR_INVALID_ARG) { error_count();vTaskDelay(pdMS_TO_TICKS(100));continue; }
            /* Close can defer deletion until the driver's disconnect event. Keep
             * that handle in the removal queue so the owner completes deletion. */
            handle=NULL;filled=0;last_retry=0;
        }
        if(!handle) {
            device_event_t candidate;
            bool have=xQueueReceive(s_devices,&candidate,0)==pdPASS;
            if(have || esp_timer_get_time()-last_retry>2000000) {
                last_retry=esp_timer_get_time();
                atomic_store(&s_disconnected,false);atomic_store(&s_transfer_error,false);
                esp_err_t err=open_audio(have ? &candidate : NULL,&handle);
                if(err==ESP_OK) {
                    set_connected(true);last_data=esp_timer_get_time();
                    ESP_LOGI(TAG,"HDMI audio started: 48000 Hz, stereo PCM16; 20 ms blocks");
                } else if(err!=ESP_ERR_NOT_FOUND) {
                    error_count();ESP_LOGW(TAG,"Audio open: %s",esp_err_to_name(err));
                }
            }
            vTaskDelay(pdMS_TO_TICKS(10));continue;
        }
        uint32_t received=0;
        esp_err_t err=uac_host_device_read(handle,buffer+filled,PCM_BLOCK_BYTES-filled,&received,pdMS_TO_TICKS(5));
        if(err==ESP_OK && received) {
            if(received%4) {filled=0;error_count();continue;}
            last_data=esp_timer_get_time();filled+=received;
            if(filled==PCM_BLOCK_BYTES) {
                xSemaphoreTake(s_lock,portMAX_DELAY);
                pcm_ring_push(&s_ring,buffer,filled,last_data);
                ++s_status.packets;s_status.last_audio_us=last_data;
                xSemaphoreGive(s_lock);filled=0;
            }
        } else if(err!=ESP_OK && err!=ESP_ERR_TIMEOUT) {
            /* Read errors may reflect removal; close in the next owner iteration. */
            atomic_store(&s_transfer_error,true);error_count();
        }
    }
}
static void speaker_task(void *arg)
{
    uint8_t *buffer=arg;
    s_speaker=bsp_audio_codec_speaker_init();
    if(!s_speaker) {ESP_LOGW(TAG,"Speaker codec unavailable; browser audio continues");free(buffer);vTaskDelete(NULL);return;}
    esp_codec_dev_sample_info_t format={.sample_rate=PCM_RATE,.bits_per_sample=PCM_BITS,.channel=PCM_CHANNELS};
    int err=esp_codec_dev_open(s_speaker,&format);
    if(err!=ESP_CODEC_DEV_OK) {ESP_LOGW(TAG,"Speaker codec open failed: %d",err);free(buffer);vTaskDelete(NULL);return;}
    xSemaphoreTake(s_lock,portMAX_DELAY);s_status.speaker_ready=true;xSemaphoreGive(s_lock);
    pcm_cursor_t cursor={0};unsigned volume=101,failures=0;bool mute=false;
    while(true) {
        audio_status_t status;audio_status(&status);
        if(volume!=status.volume || mute!=status.muted) {
            int gain=esp_codec_dev_set_out_vol(s_speaker,status.volume);
            int muted=esp_codec_dev_set_out_mute(s_speaker,status.muted);
            if(gain==ESP_CODEC_DEV_OK && muted==ESP_CODEC_DEV_OK) {volume=status.volume;mute=status.muted;}
            else {
                error_count();
                if(++failures==1 || failures%100==0) ESP_LOGW(TAG,"Speaker gain/mute failed: %d/%d",gain,muted);
            }
        }
        pcm_info_t info;
        if(!audio_copy(&cursor,buffer,PCM_BLOCK_BYTES,&info,false)) memset(buffer,0,PCM_BLOCK_BYTES);
        /* The board speaker gets both HDMI channels; browser audio remains stereo. */
        pcm_stereo_to_dual_mono(buffer,PCM_BLOCK_BYTES);
        err=esp_codec_dev_write(s_speaker,buffer,PCM_BLOCK_BYTES);
        if(err!=ESP_CODEC_DEV_OK) {
            error_count();
            if(++failures==1 || failures%100==0) ESP_LOGW(TAG,"Speaker I2S write failed: %d",err);
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}
esp_err_t audio_init(void)
{
#if !CONFIG_CAPTURE_AUDIO
    return ESP_OK;
#else
    s_lock=xSemaphoreCreateMutex();s_devices=xQueueCreate(4,sizeof(device_event_t));
    s_removed=xQueueCreate(4,sizeof(uac_host_device_handle_t));
    uint8_t *storage=heap_caps_malloc(PCM_BLOCK_BYTES*PCM_RING_SLOTS,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    uint8_t *input=heap_caps_malloc(PCM_BLOCK_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!s_lock || !s_devices || !s_removed || !storage || !input) return ESP_ERR_NO_MEM;
    pcm_ring_init(&s_ring,storage);
    uac_host_driver_config_t driver={.create_background_task=true,.task_priority=10,.stack_size=6144,
        .core_id=tskNO_AFFINITY,.callback=driver_event};
    esp_err_t err=uac_host_install(&driver);if(err!=ESP_OK) return err;
    if(xTaskCreate(usb_audio_task,"hdmi_audio",6144,input,8,NULL)!=pdPASS) return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_lock,portMAX_DELAY);s_status.enabled=true;xSemaphoreGive(s_lock);
#if CONFIG_CAPTURE_AUDIO_SPEAKER
    uint8_t *output=heap_caps_malloc(PCM_BLOCK_BYTES,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(output && xTaskCreate(speaker_task,"speaker",6144,output,5,NULL)!=pdPASS) free(output);
#endif
    return ESP_OK;
#endif
}
