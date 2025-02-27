#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_memory_utils.h"
#include "esp_heap_caps.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "app_audio.h"

#define MIC_BUFFER_SIZE  300 * 1024

static const char *TAG = "app_mic";

static uint8_t *mic_buffer = NULL;
static size_t bytes_read = 0;
static TaskHandle_t audio_record_task_handle = NULL;
static TaskHandle_t audio_play_task_handle = NULL;

typedef struct {
    // The "RIFF" chunk descriptor
    uint8_t ChunkID[4];
    int32_t ChunkSize;
    uint8_t Format[4];
    // The "fmt" sub-chunk
    uint8_t Subchunk1ID[4];
    int32_t Subchunk1Size;
    int16_t AudioFormat;
    int16_t NumChannels;
    int32_t SampleRate;
    int32_t ByteRate;
    int16_t BlockAlign;
    int16_t BitsPerSample;
    // The "data" sub-chunk
    uint8_t Subchunk2ID[4];
    int32_t Subchunk2Size;
} wav_header_t;

static void write_wav_header(FILE *file, wav_header_t *header) {
    fwrite(header->ChunkID, sizeof(header->ChunkID), 1, file);
    fwrite(&header->ChunkSize, sizeof(header->ChunkSize), 1, file);
    fwrite(header->Format, sizeof(header->Format), 1, file);
    fwrite(header->Subchunk1ID, sizeof(header->Subchunk1ID), 1, file);
    fwrite(&header->Subchunk1Size, sizeof(header->Subchunk1Size), 1, file);
    fwrite(&header->AudioFormat, sizeof(header->AudioFormat), 1, file);
    fwrite(&header->NumChannels, sizeof(header->NumChannels), 1, file);
    fwrite(&header->SampleRate, sizeof(header->SampleRate), 1, file);
    fwrite(&header->ByteRate, sizeof(header->ByteRate), 1, file);
    fwrite(&header->BlockAlign, sizeof(header->BlockAlign), 1, file);
    fwrite(&header->BitsPerSample, sizeof(header->BitsPerSample), 1, file);
    fwrite(header->Subchunk2ID, sizeof(header->Subchunk2ID), 1, file);
    fwrite(&header->Subchunk2Size, sizeof(header->Subchunk2Size), 1, file);
}

static void app_audio_record_task(void *arg)
{   
    while (1) {
        ESP_ERROR_CHECK(bsp_extra_pdm_i2s_read(mic_buffer, MIC_BUFFER_SIZE, &bytes_read, portMAX_DELAY));
        ESP_LOGD(TAG, "Audio recording");
        
        // for (size_t i = 0; i < MIC_BUFFER_SIZE / 100; i++) {
        //     ESP_LOGI(TAG, "mic_buffer[%d]: %d", i, mic_buffer[i]);
        // }

        // memset(mic_buffer, 0, MIC_BUFFER_SIZE);

        // vTaskDelay(2000 / portTICK_PERIOD_MS);

        FILE *file_record_wav = fopen("/sdcard/record.wav", "wb");
        ESP_LOGI(TAG, "outfile:%s",  "/sdcard/record.wav");
        if (file_record_wav == NULL) {
            ESP_LOGE(TAG, "Failed to open file for writing");
            return;
        }

        wav_header_t header;
        memcpy(header.ChunkID, "RIFF", 4);
        header.ChunkSize = 36 + MIC_BUFFER_SIZE;  
        memcpy(header.Format, "WAVE", 4);
        memcpy(header.Subchunk1ID, "fmt ", 4);
        header.Subchunk1Size = 16;  
        header.AudioFormat = 1;     
        header.NumChannels = 2;     
        header.SampleRate = 16000;  
        header.ByteRate = header.SampleRate * header.NumChannels * 16 / 8;
        header.BlockAlign = header.NumChannels * 16 / 8;
        header.BitsPerSample = 16;  
        memcpy(header.Subchunk2ID, "data", 4);
        header.Subchunk2Size = MIC_BUFFER_SIZE;   

        write_wav_header(file_record_wav, &header);

        fwrite(mic_buffer, 1, MIC_BUFFER_SIZE, file_record_wav);

        fclose(file_record_wav);
        ESP_LOGI(TAG, "Audio recording done");
        break;
    }

    vTaskDelete(NULL);
}

// static void app_audio_play_task(void *arg)
// {

//     while(1) {
//         ESP_ERROR_CHECK(bsp_extra_i2s_write(mic_buffer, MIC_BUFFER_SIZE, &bytes_read, 1000));
//         ESP_LOGD(TAG, "Audio playing");
//     }

//     vTaskDelete(NULL);
// }

esp_err_t app_audio_init(void)
{
    mic_buffer = (uint8_t*)heap_caps_calloc(1, MIC_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    assert(mic_buffer != NULL);
    
    memset(mic_buffer, 0, MIC_BUFFER_SIZE);

    ESP_LOGI(TAG, "Audio Task begin");
    xTaskCreatePinnedToCore(app_audio_record_task, "app_audio_record_task", 4096, NULL, 5, &audio_record_task_handle, 1);
    
    // xTaskCreatePinnedToCore(app_audio_play_task, "app_audio_play_task", 4096, NULL, 5, &audio_play_task_handle, 1);

    return ESP_OK;
}

esp_err_t app_audio_deinit(void)
{
    vTaskDelete(audio_record_task_handle);
    // vTaskDelete(audio_play_task_handle);

    heap_caps_free(mic_buffer);

    return ESP_OK;
}

