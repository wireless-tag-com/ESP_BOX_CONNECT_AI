/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_check.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "audio_player.h"
#include "cJSON.h"
#include "bsp/esp-bsp.h"
#include "bsp_board.h"
#include "app_sr.h"
#include "app_audio.h"
#include "app_wifi.h"
#include "app_uart.h"
#include "app_ui_ctrl.h"


#include "esp_peripherals.h"
#include "board.h"

#define AUDIO_PLAY_FINAL_BIT BIT0
static size_t _http_data_len = 0;

static char *TAG = "app_main";

static QueueHandle_t mp3_data_queue = NULL;
static EventGroupHandle_t audio_play_event_group = NULL;
static SemaphoreHandle_t audio_semaphore;
static char *player_data = NULL;

typedef enum
{
    WIFI_CONNECT_CMD = 0,
    WIFI_STATE_CMD,
    CHATGPT_RESPONSE_CMD,
} uart_cmd_t;

typedef struct
{
    char header[3]; /*!< Always "ID3" */
    char ver;       /*!< Version, equals to3 if ID3V2.3 */
    char revision;  /*!< Revision, should be 0 */
    char flag;      /*!< Flag byte, use Bit[7..5] only */
    char size[4];   /*!< TAG size */
} mp3_header_t;

typedef struct
{
    char *mp3_data;
    size_t mp3_len;
} mp3_data_t;

// HTTP事件处理函数
esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static char *data = NULL;   // 用于存储接收到的数据
    static int data_len = 0;    // 数据长度

    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");  // 处理HTTP错误事件
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");   // 处理HTTP连接成功事件
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");    // 处理HTTP头部发送事件
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);    // 处理HTTP头部接收事件
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA (%d +)%d", data_len, evt->data_len);  // 处理HTTP数据接收事件
        ESP_LOGI(TAG, "Raw Response: data length: (%d +)%d: %.*s", data_len, evt->data_len, evt->data_len, (char *)evt->data);  // 打印接收到的原始数据

        // 重新分配内存以存储接收到的数据
        data = heap_caps_realloc(data, data_len + evt->data_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (data == NULL)
        {
            ESP_LOGE(TAG, "data realloc failed");   // 内存重新分配失败
            free(data);     // 释放内存
            data = NULL;
            assert(0);      // 断言失败

            break;
        }
        // 将接收到的数据复制到data中
        memcpy(data + data_len, (char *)evt->data, evt->data_len);
        data_len += evt->data_len;      // 更新数据长度
        data[data_len] = '\0';          // 添加字符串结束符
        // printf("%s\r\n", data);      // 打印接收到的数据
        break;

    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");      // 处理HTTP请求完成事件
        esp_http_client_set_user_data(evt->client, data);   // 将接收到的数据设置为用户数据
        _http_data_len = data_len;      // 更新全局数据长度
        break;

    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");   // 处理HTTP断开连接事件
        printf("---%d---\r\n", data_len);       // 打印数据长度
        if (data != NULL)
        {
            free(data);     // 释放内存
            data = NULL;
            data_len = 0;   // 重置数据长度
        }

        break;

    case HTTP_EVENT_REDIRECT:
        ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");   // 处理HTTP重定向事件
        break;
    }
    return ESP_OK;
}

// 检查文件是否为MP3文件
static bool app_is_mp3(FILE *fp)
{
    bool is_mp3_file = false;

    fseek(fp, 0, SEEK_SET);     // 将文件指针移动到文件开头

    // see https://en.wikipedia.org/wiki/List_of_file_signatures
    uint8_t magic[3];
    if (sizeof(magic) == fread(magic, 1, sizeof(magic), fp))
    {
        // 检查文件头是否为MP3文件标识
        if ((magic[0] == 0xFF) &&
            (magic[1] == 0xFB))
        {
            is_mp3_file = true;
        }
        else if ((magic[0] == 0xFF) &&
                 (magic[1] == 0xF3))
        {
            is_mp3_file = true;
        }
        else if ((magic[0] == 0xFF) &&
                 (magic[1] == 0xF2))
        {
            is_mp3_file = true;
        }
        else if ((magic[0] == 0x49) &&
                 (magic[1] == 0x44) &&
                 (magic[2] == 0x33)) /* 'ID3' */
        {
            fseek(fp, 0, SEEK_SET);     // 将文件指针移动到文件开头

            // 获取ID3头部
            mp3_header_t tag;
            if (sizeof(mp3_header_t) == fread(&tag, 1, sizeof(mp3_header_t), fp))
            {
                if (memcmp("ID3", (const void *)&tag, sizeof(tag.header)) == 0)
                {
                    is_mp3_file = true;
                }
            }
        }
    }
    // 将文件指针移动回文件开头以避免解码时丢失帧
    fseek(fp, 0, SEEK_SET);

    return is_mp3_file;
}

// 将MP3数据发送到队列
int mp3_data_queue_send(mp3_data_t data)
{
    if (mp3_data_queue == NULL)
    {
        ESP_LOGE(TAG, "mp3_data_queue is not initialized");
        return 0;
    }
    BaseType_t ret_val = xQueueSend(mp3_data_queue, &data, 0);
    if (ret_val != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to send file pointer to queue");
        return 0;
    }
    return 1;
}

// MP3播放任务
static void app_mp3_play_task(void *args)
{
    FILE *fp = NULL;
    mp3_data_t mp3_data;
    while (1)
    {
        // 从队列中接收MP3数据
        if (xQueueReceive(mp3_data_queue, &mp3_data, portMAX_DELAY) == pdPASS)
        {
            // 等待音频播放完成事件
            xEventGroupWaitBits(audio_play_event_group, AUDIO_PLAY_FINAL_BIT, 0, 1, portMAX_DELAY);
            player_data = (char *)heap_caps_calloc(mp3_data.mp3_len, sizeof(char), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (player_data == NULL)
            {
                ESP_LOGE(TAG, "no memory to mp3 player");
                assert(0);
            }
            memset(player_data, 0, mp3_data.mp3_len); // 将 player_data 置为 0
            memcpy(player_data, mp3_data.mp3_data, mp3_data.mp3_len);   // 复制MP3数据到 player_data
            fp = fmemopen(player_data, mp3_data.mp3_len, "rb"); // 打开内存中的MP3数据作为文件
            if (fp)
            {
                if (app_is_mp3(fp))
                {
                    ESP_LOGI(TAG, "it is mp3 data \n");
                    esp_err_t status = audio_player_play(fp);   // 播放MP3数据
                    if (status != ESP_OK)
                    {
                        ESP_LOGE(TAG, "tts mp3 play error \n");
                    }
                }
                else
                {
                    ESP_LOGE(TAG, "no mp3 data \n");
                    break;
                }
            }
            xEventGroupClearBits(audio_play_event_group, AUDIO_PLAY_FINAL_BIT); // 清除音频播放完成事件位
        }
    }
    vTaskDelete(NULL);
}

#define MAX_HTTP_OUTPUT_BUFFER 8192
static int response_len = 0;
static char response_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};

#define POST_URL "http://productID.llm.aiha.cloud/dsse/llm_allInOne/deviceID"
//使用前需将 productID 和 deviceID 替换为实际的值


// HTTP事件处理函数
esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:
        ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        if (esp_http_client_is_chunked_response(evt->client))
        {
            // 将数据追加到响应缓冲区
            if (response_len + evt->data_len < MAX_HTTP_OUTPUT_BUFFER)
            {
                memcpy(response_buffer + response_len, evt->data, evt->data_len);
                response_len += evt->data_len;
            }
            else
            {
                ESP_LOGW(TAG, "Response buffer overflow");
            }
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
        response_buffer[response_len] = 0;  // 添加字符串结束符
        ESP_LOGI(TAG, "HTTP Response: %s", response_buffer);    // 打印HTTP响应
        response_len = 0; // 重置响应长度
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGI(TAG, "HTTP_EVENT_REDIRECT");
        break;
    }
    return ESP_OK;
}

// MP3 URL信息结构体
typedef struct
{
    char *url_mp3;
    void *tts_data;
    size_t output_len;
} mp3_url_info_t;

// 发送音频请求
void audio_request(mp3_url_info_t *result)
{
    esp_http_client_config_t config = {
        .url = result->url_mp3,
        .method = HTTP_METHOD_GET,
        .event_handler = _http_event_handler,
        .buffer_size = 128000,
        .timeout_ms = 60000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "speech POST request failed: %s\n", esp_err_to_name(err));
    }
    else
    {
        int status_code = esp_http_client_get_status_code(client);
        int64_t content_length = esp_http_client_get_content_length(client);
        if (status_code == 200)
        {
            ESP_LOGI(TAG, "speech POST Status = %d, content_length = %lld",
                     status_code,
                     content_length);
            void *user_data = NULL;
            esp_http_client_get_user_data(client, &user_data);
            result->tts_data = heap_caps_calloc(sizeof(char), _http_data_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (result->tts_data == NULL)
            {
                ESP_LOGE(TAG, "Failed to allocate tts data");
                assert(0);
            }

            memcpy(result->tts_data, user_data, _http_data_len);
            result->output_len = _http_data_len;
            printf("===%d===\r\n", _http_data_len);
            printf("===%d===\r\n", result->output_len);
        }
        else
        {
            ESP_LOGW(TAG, "speech POST Status = %d, content_length = %lld",
                     status_code,
                     content_length);
        }
    }
    esp_http_client_cleanup(client);
}

// 播放MP3音频
void audio_play_mp3(char *url_mp3)
{
    esp_err_t ret = ESP_OK;
    FILE *fp = NULL;
    mp3_url_info_t *result = (mp3_url_info_t *)malloc(sizeof(mp3_url_info_t));
    if (result == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for result");
        return;
    }
    result->url_mp3 = url_mp3;

    if (xSemaphoreTake(audio_semaphore, portMAX_DELAY) == pdTRUE)
    {
        audio_request(result);

        if (result->tts_data == NULL)
        {
            ret = ESP_ERR_INVALID_RESPONSE;
            fp = fopen("/spiffs/tts_failed.mp3", "r");
            if (fp)
            {
                audio_player_play(fp);
                fclose(fp);
            }
            free(result);
            xSemaphoreGive(audio_semaphore);
            return;
        }
        mp3_data_t data = {
            .mp3_data = result->tts_data,
            .mp3_len = result->output_len,
        };
        ESP_LOGW(TAG, "audio_play_mp3: %d", data.mp3_len);
        mp3_data_queue_send(data);
        free(result);
        xSemaphoreGive(audio_semaphore);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to take semaphore");
        free(result);
    }
}

// 处理HTTP响应
void handle_http_response(char *response)
{
    // ESP_LOGI(TAG, "HTTP Response: %s", response); // 打印完整的响应内容
    char *data_start = response;
    char text_content[4096] = {0};
    char url_content[2048] = {0};
    size_t text_len = 0;
    size_t url_len = 0;
    while ((data_start = strstr(data_start, "data:")) != NULL)
    {
        data_start += 5;
        char *data_end = strstr(data_start, "\n");
        if (data_end == NULL)
        {
            data_end = data_start + strlen(data_start);
        }
        else
        {
            *data_end = '\0';
        }

        //解析数据
        cJSON *json = cJSON_Parse(data_start);
        if (json == NULL)
        {
            ESP_LOGE(TAG, "json parse error: %s", cJSON_GetErrorPtr());
            ui_ctrl_show_panel(UI_CTRL_PANEL_SLEEP, 10000);
            return;
        }

        cJSON *content = cJSON_GetObjectItem(json, "content");
        cJSON *url = cJSON_GetObjectItem(json, "url");

        if (content && cJSON_IsString(content) && content->valuestring[0] != '\0')
        {
            // 拼接文本内容
            text_len += snprintf(text_content + text_len, sizeof(text_content) - text_len, "%s", content->valuestring);
        }

        if (url && cJSON_IsString(url) && url->valuestring[0] != '\0')
        {
            // 播放音频
            // ESP_LOGI(TAG, "Playing audio from URL: %s", url->valuestring);
            url_len += snprintf(url_content + url_len, sizeof(url_content) - url_len, "%s", url->valuestring);
        }
        cJSON_Delete(json);

        if (data_end != data_start + strlen(data_start))
        {
            *data_end = '\n'; // 恢复 '\n'
        }
        data_start = data_end + 1;
        // 打印所有文本内容
        // ESP_LOGI(TAG, "Response Text: %s", text_content);
    }
    // 打印文本内容
    if (text_len > 0 && url_len > 0)
    {
        ESP_LOGI(TAG, "Response Text: %s", text_content);
        ESP_LOGI(TAG, "Response mp3_url: %s", url_content);
        ui_ctrl_label_show_text(UI_CTRL_LABEL_REPLY_CONTENT, text_content);
        ui_ctrl_show_panel(UI_CTRL_PANEL_REPLY, 0);
        char *ptr;
        char *start = url_content;
        while ((ptr = strstr(start, "https://")) != NULL)
        {
            char *end = strstr(ptr, ".mp3");
            if (end != NULL)
            {
                // 计算子字符串长度并提取出来
                int len = end - ptr + 4; // +4是因为".mp3"长度为4
                char mp3_link[len + 1];
                strncpy(mp3_link, ptr, len);
                mp3_link[len] = '\0';
                ESP_LOGI(TAG, "mp3 link: %s\n", mp3_link);
                audio_play_mp3(mp3_link);
                vTaskDelay(300 / portTICK_PERIOD_MS);
                start = end + 4; // 移动起始位置，继续查找下一个
            }
            else
            {
                break;
            }
        }
    }
}

// 启动OpenAI请求
esp_err_t start_openai(uint8_t *audio, int audio_len)
{
    // 创建MP3播放任务
    xTaskCreate(app_mp3_play_task, "app_mp3_play_task", 8192, NULL, 3, NULL);
    ui_ctrl_show_panel(UI_CTRL_PANEL_GET, 0);
    esp_http_client_config_t config = {
        .url = POST_URL,
        .event_handler = http_event_handler,
        .method = HTTP_METHOD_POST,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "multipart/form-data; boundary=----WebKitFormBoundary7MA4YWxkTrZu0gW");

    char *post_data = malloc(audio_len + 512);
    memset(post_data, 0, audio_len + 512);

    memcpy(post_data, "------WebKitFormBoundary7MA4YWxkTrZu0gW\r\nContent-Disposition: form-data; name=\"file\"; filename=\"test.mp3\"\r\nContent-Type: audio/wav\r\n\r\n", 134);
    memcpy(post_data + 134, audio, audio_len);
    memcpy(post_data + 134 + audio_len, "\r\n------WebKitFormBoundary7MA4YWxkTrZu0gW\r\nContent-Disposition: form-data; name=\"format\"\r\n\r\nwav\r\n------WebKitFormBoundary7MA4YWxkTrZu0gW\r\nContent-Disposition: form-data; name=\"convertMp3\"\r\n\r\ntrue\r\n------WebKitFormBoundary7MA4YWxkTrZu0gW--\r\n", 240);

    ESP_LOGI(TAG, "Post data:\n%s", post_data);

    // 设置POST字段
    esp_http_client_set_post_field(client, post_data, 374 + audio_len);

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        ESP_LOGI(TAG, "HTTP POST Status = %d",
                 esp_http_client_get_status_code(client));

        // 处理响应数据
        handle_http_response(response_buffer);
    }
    else
    {
        ESP_LOGE(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
        ui_ctrl_label_show_text(UI_CTRL_LABEL_LISTEN_SPEAK, "tts respone error");
        ui_ctrl_show_panel(UI_CTRL_PANEL_SLEEP, 2000);
    }

    // Cleanup
    // free(audio);
    free(post_data);
    esp_http_client_cleanup(client);
}
// 音频播放完成回调
static void audio_play_finish_cb(void)
{
    ESP_LOGI(TAG, "replay audio end");

    // 设置音频播放完成事件位
    xEventGroupSetBits(audio_play_event_group, AUDIO_PLAY_FINAL_BIT);
    if (ui_ctrl_reply_get_audio_start_flag())
    {
        ui_ctrl_reply_set_audio_end_flag(true);
    }
}

// uart 任务
static void app_uart_task(void *parm)
{
    int data_len = 1024;
    uint8_t *data = (uint8_t *)calloc(1024, sizeof(char));
    while (1)
    {
        // 读取UART数据
        int len = app_uart_read(data, data_len - 1);
        if (len > 0)
        {
            // app_uart_send(data, len);
            ESP_LOGI(TAG, "UART1 %.*s", len, data);
            // 解析JSON数据
            cJSON *root = cJSON_Parse((char *)data);
            if (root)
            {
                cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
                if (cmd && cJSON_IsNumber(cmd))
                {
                    switch (cmd->valueint)
                    {
                    // 接收到WIFI连接命令
                    case WIFI_CONNECT_CMD:
                        ESP_LOGI(TAG, "recive cmd WIFI_CONNECT_CMD");
                        cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
                        cJSON *password = cJSON_GetObjectItem(root, "password");
                        if (ssid && password && cJSON_IsString(ssid) && cJSON_IsString(password))
                        {
                            // 接收到WIFI连接命令
                            app_connect_wifi(ssid->valuestring, password->valuestring);
                        }
                        else
                        {
                            ESP_LOGW(TAG, "CMD error : ssid or password formate error");
                        }
                        break;
                    // 接收到WIFI状态命令
                    case WIFI_STATE_CMD:
                        ESP_LOGI(TAG, "recive cmd WIFI_STATE_CMD");
                        WiFi_Connect_Status status = wifi_connected_already();
                        cJSON *state = cJSON_CreateObject();
                        cJSON_AddNumberToObject(state, "cmd", 1);
                        cJSON_AddNumberToObject(state, "status", status);
                        char *state_data = cJSON_PrintUnformatted(state);
                        // 发送WIFI状态
                        app_uart_send(state_data, strlen(state_data));
                        free(state_data);
                        cJSON_Delete(state);
                        break;
                    // 接收到ChatGPT响应命令
                    case CHATGPT_RESPONSE_CMD:
                        ESP_LOGI(TAG, "recive cmd WIFI_STATE_CMD");
                        break;

                    default:
                        break;
                    }
                }
                cJSON_Delete(root);
            }
            else
            {
                ESP_LOGW(TAG, "CMD error : no json formate");
            }
        }
    }
    vTaskDelete(NULL);
}

// 处理WIFI事件
static void app_wifi_event(net_event_t event)
{
    int status;
    switch (event)
    {
    case NET_EVENT_CONNECTING:
        // 正在连接
        status = 0;
        break;

    case NET_EVENT_CONNECTED:
        // 已连接
        status = 1;
        break;

    case NET_EVENT_DISCONNECT:
        // 已断开连接
        status = 2;
        break;

    default:
        return;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "cmd", 1);
    cJSON_AddNumberToObject(root, "status", status);
    char *data = cJSON_PrintUnformatted(root);
    app_uart_send(data, strlen(data));
    free(data);
    cJSON_Delete(root);
}

void app_main()
{
    // 初始化 NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    //创建一个互斥信号量，用于控制音频播放的同步
    audio_semaphore = xSemaphoreCreateMutex();
    xSemaphoreGive(audio_semaphore);

    //初始化SPIFFS 文件系统、I2C 接口、显示屏、板载硬件、网络和 UI 控制
    bsp_spiffs_mount();
    bsp_i2c_init();
    bsp_display_start();
    bsp_board_init();
    app_network_start(app_wifi_event);
    bsp_display_backlight_on();
    ui_ctrl_init();

    //启动语音识别功能
    ESP_LOGI(TAG, "speech recognition start");
    app_sr_start(false);

    //注册一个回调函数，当音频播放完成时会调用该函数
    audio_register_play_finish_cb(audio_play_finish_cb);

    //初始化 UART 并创建一个任务来处理 UART 通信
    app_uart_init();
    xTaskCreate(app_uart_task, "app_uart_task", 8192, NULL, 3, NULL);

    //创建一个事件组，用于音频播放的同步控制
    audio_play_event_group = xEventGroupCreate();
    xEventGroupSetBits(audio_play_event_group, AUDIO_PLAY_FINAL_BIT);

    //创建一个队列，用于存储 MP3 数据
    mp3_data_queue = xQueueCreate(20, sizeof(mp3_data_t));
    ESP_ERROR_CHECK_WITHOUT_ABORT((mp3_data_queue) ? ESP_OK : ESP_FAIL);
}
