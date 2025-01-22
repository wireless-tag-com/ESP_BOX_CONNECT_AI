/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_check.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "app_wifi.h"
#include "esp_timer.h"

#define EXAMPLE_ESP_MAXIMUM_RETRY CONFIG_ESP_MAXIMUM_RETRY

static EventGroupHandle_t s_wifi_event_group;

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define portTICK_RATE_MS 10
#define SSID_SIZE 32
#define PASSWORD_SIZE 64

static const char *TAG = "wifi station";
static int s_retry_num = 0;
static bool s_reconnect = true;
static bool s_connectting = false;
static esp_timer_handle_t s_recon_timer = NULL;
static bool wifi_connected = false;
static QueueHandle_t wifi_event_queue = NULL;
static char s_wifi_ssid[SSID_SIZE];
static char s_wifi_password[PASSWORD_SIZE];

static scan_info_t scan_info_result = {
    .scan_done = WIFI_SCAN_IDLE,
    .ap_count = 0,
};

static void (*__wifi_event)(net_event_t) = NULL;

// 检查WiFi是否已连接
WiFi_Connect_Status wifi_connected_already(void)
{
    WiFi_Connect_Status status;
    if (true == wifi_connected)
    {
        status = WIFI_STATUS_CONNECTED_OK;
    }
    else
    {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY)
        {
            status = WIFI_STATUS_CONNECTING;
        }
        else
        {
            status = WIFI_STATUS_CONNECTED_FAILED;
        }
    }
    return status;
}

// 发送网络事件
static esp_err_t send_network_event(net_event_t event)
{
    net_event_t eventOut = event;
    BaseType_t ret_val = xQueueSend(wifi_event_queue, &eventOut, 0);

    ESP_RETURN_ON_FALSE(pdPASS == ret_val, ESP_ERR_INVALID_STATE,
                        TAG, "The last event has not been processed yet");
    return ESP_OK;
}

// 连接WiFi
void app_connect_wifi(char *ssid, char *password)
{
    if (strlen(ssid) > SSID_SIZE || strlen(password) > PASSWORD_SIZE)
    {
        ESP_LOGE(TAG, "ssid or password too long");
        return;
    }

    if (s_connectting)
    {
        ESP_LOGW(TAG, "WiFi connectting , Please do not send frequently ");
        send_network_event(NET_EVENT_CONNECTING);
        return;
    }

    if (esp_timer_is_active(s_recon_timer))
        ESP_ERROR_CHECK(esp_timer_stop(s_recon_timer));

    memset(s_wifi_ssid, 0, sizeof(s_wifi_ssid));
    memset(s_wifi_password, 0, sizeof(s_wifi_password));
    memcpy(s_wifi_ssid, ssid, strlen(ssid));
    memcpy(s_wifi_password, password, strlen(password));
    send_network_event(NET_EVENT_RECONNECT);
}

// 重新连接WiFi
static void wifi_reconnect_sta()
{
    int bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, 0, 1, 0);

    if (bits & WIFI_CONNECTED_BIT)
    {
        s_reconnect = false;
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        ESP_ERROR_CHECK(esp_wifi_disconnect());
        xEventGroupWaitBits(s_wifi_event_group, WIFI_FAIL_BIT, 0, 1, portTICK_RATE_MS);
    }

    wifi_config_t wifi_config = {0};
    memcpy(wifi_config.sta.ssid, s_wifi_ssid, strlen(s_wifi_ssid));
    memcpy(wifi_config.sta.password, s_wifi_password, strlen(s_wifi_password));

    s_connectting = true;
    s_reconnect = true;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    esp_wifi_connect();

    ESP_LOGI(TAG, "wifi_connect_sta finished.%s, %s",
             wifi_config.sta.ssid, wifi_config.sta.password);

    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, 0, 1, 5000 / portTICK_RATE_MS);
}

// 扫描WiFi
static void wifi_scan(void)
{
    uint16_t number = DEFAULT_SCAN_LIST_SIZE;
    wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];
    uint16_t ap_count = 0;
    memset(ap_info, 0, sizeof(ap_info));

    app_wifi_state_set(WIFI_SCAN_BUSY);

    esp_err_t ret = esp_wifi_scan_start(NULL, true);
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_info));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    ESP_LOGI(TAG, "Total APs scanned = %u, ret:%d", ap_count, ret);

    for (int i = 0; (i < DEFAULT_SCAN_LIST_SIZE) && (i < ap_count); i++)
    {
        ESP_LOGI(TAG, "SSID \t\t%s", ap_info[i].ssid);
    }

    if (ap_count && (ESP_OK == ret))
    {
        scan_info_result.ap_count = (ap_count < DEFAULT_SCAN_LIST_SIZE) ? ap_count : DEFAULT_SCAN_LIST_SIZE;
        memcpy(&scan_info_result.ap_info[0], &ap_info[0], sizeof(wifi_ap_record_t) * scan_info_result.ap_count);
    }
    else
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
        ESP_LOGI(TAG, "failed return");
    }
    app_wifi_state_set(WIFI_SCAN_RENEW);
}

// 上电连接WiFi
static esp_err_t poweron_connect()
{
    wifi_config_t wifi_cfg = {0};
    if (esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg) != ESP_OK)
    {
        ESP_LOGW(TAG, "esp_wifi_get_config error!");
        return ESP_FAIL;
    }

    if (!strlen((char *)wifi_cfg.sta.ssid))
    {
        ESP_LOGW(TAG, "WIFI config not factory data!");
        return ESP_FAIL;
    }

    s_connectting = true;
    esp_wifi_connect();
    return ESP_OK;
}

// 重新连接定时器回调
static void recon_timer_callback(void *arg)
{
    s_retry_num = 0;
    esp_wifi_connect();
    ESP_LOGI(TAG, "reconnect wifi");
}

// 事件处理器
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        send_network_event(NET_EVENT_POWERON);
        ESP_LOGI(TAG, "start connect to the AP");
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_event_sta_disconnected_t *disconnected = (wifi_event_sta_disconnected_t *)event_data;
        ESP_LOGI(TAG, "disconnected reason %d", disconnected->reason);
        if (s_reconnect && ++s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY)
        {
            esp_wifi_connect();
            ESP_LOGI(TAG, "sta disconnect, retry attempt %d...", s_retry_num);
        }
        else
        {
            if (disconnected->reason == WIFI_REASON_NO_AP_FOUND)
            {

                ESP_ERROR_CHECK(esp_timer_start_once(s_recon_timer, 30 * 1000 * 1000));
            }
            ESP_LOGI(TAG, "sta disconnected");
            s_connectting = false;
            send_network_event(NET_EVENT_DISCONNECT);
        }
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        wifi_connected = false;
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));

        if (esp_timer_is_active(s_recon_timer))
            ESP_ERROR_CHECK(esp_timer_stop(s_recon_timer));

        s_retry_num = 0;
        wifi_connected = true;
        s_connectting = false;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        send_network_event(NET_EVENT_CONNECTED);
    }
}

// 网络任务
static void network_task(void *args)
{
    net_event_t net_event;

    while (1)
    {
        if (pdPASS == xQueueReceive(wifi_event_queue, &net_event, portTICK_RATE_MS / 5))
        {
            switch (net_event)
            {
            case NET_EVENT_RECONNECT:
                ESP_LOGI(TAG, "NET_EVENT_RECONNECT");
                wifi_connected = false;
                s_retry_num = 0;
                wifi_reconnect_sta();
                break;

            case NET_EVENT_SCAN:
                ESP_LOGI(TAG, "NET_EVENT_SCAN");
                wifi_scan();
                break;

            case NET_EVENT_POWERON:
                ESP_LOGI(TAG, "NET_EVENT_POWERON_SCAN");
                wifi_connected = false;
                s_retry_num = 0;
                poweron_connect();
                break;

            case NET_EVENT_CONNECTED:
                __wifi_event(NET_EVENT_CONNECTED);
                break;

            case NET_EVENT_CONNECTING:
                __wifi_event(NET_EVENT_CONNECTING);
                break;

            case NET_EVENT_DISCONNECT:
                __wifi_event(NET_EVENT_DISCONNECT);
                break;

            default:
                break;
            }
        }
    }
    vTaskDelete(NULL);
}

// 初始化WiFi STA模式
static void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "wifi_init_sta finished");
}

// 锁定WiFi
bool app_wifi_lock(uint32_t timeout_ms)
{
    assert(scan_info_result.wifi_mux && "bsp_display_start must be called first");

    const TickType_t timeout_ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTakeRecursive(scan_info_result.wifi_mux, timeout_ticks) == pdTRUE;
}

// 解锁WiFi
void app_wifi_unlock(void)
{
    assert(scan_info_result.wifi_mux && "bsp_display_start must be called first");
    xSemaphoreGiveRecursive(scan_info_result.wifi_mux);
}

// 设置WiFi状态
void app_wifi_state_set(wifi_scan_status_t status)
{
    app_wifi_lock(0);
    scan_info_result.scan_done = status;
    app_wifi_unlock();
}

// 启动网络
void app_network_start(void (*wifi_event)(net_event_t))
{

    if (__wifi_event)
        return;
    __wifi_event = wifi_event;

    BaseType_t ret_val;

    scan_info_result.wifi_mux = xSemaphoreCreateRecursiveMutex();
    ESP_ERROR_CHECK_WITHOUT_ABORT((scan_info_result.wifi_mux) ? ESP_OK : ESP_FAIL);

    wifi_event_queue = xQueueCreate(4, sizeof(net_event_t));
    ESP_ERROR_CHECK_WITHOUT_ABORT((wifi_event_queue) ? ESP_OK : ESP_FAIL);

    wifi_init_sta();

    const esp_timer_create_args_t recon_timer_args = {
        .callback = &recon_timer_callback,
        .name = "reconnect",
    };
    ESP_ERROR_CHECK(esp_timer_create(&recon_timer_args, &s_recon_timer));

    ret_val = xTaskCreatePinnedToCore(network_task, "NetWork Task", 5 * 1024, NULL, 1, NULL, 0);
    ESP_ERROR_CHECK_WITHOUT_ABORT((pdPASS == ret_val) ? ESP_OK : ESP_FAIL);
}
