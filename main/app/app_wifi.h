/*
 * SPDX-FileCopyrightText: 2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#pragma once
#include <esp_err.h>
#include "esp_wifi.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define DEFAULT_SCAN_LIST_SIZE 20

    typedef enum
    {
        WIFI_SCAN_IDLE,
        WIFI_SCAN_BUSY,
        WIFI_SCAN_RENEW,
        WIFI_SCAN_UPDATE,
    } wifi_scan_status_t;

    typedef struct
    {
        wifi_scan_status_t scan_done;
        wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];
        uint16_t ap_count;
        SemaphoreHandle_t wifi_mux;
    } scan_info_t;

    typedef enum
    {
        NET_EVENT_NONE = 0,
        NET_EVENT_RECONNECT,
        NET_EVENT_SCAN,
        NET_EVENT_POWERON,
        NET_EVENT_CONNECTED,
        NET_EVENT_CONNECTING,
        NET_EVENT_DISCONNECT,
        NET_EVENT_MAX,
    } net_event_t;

    typedef enum
    {
        WIFI_STATUS_CONNECTING,
        WIFI_STATUS_CONNECTED_OK,
        WIFI_STATUS_CONNECTED_FAILED,
    } WiFi_Connect_Status;

    WiFi_Connect_Status wifi_connected_already(void);

    void app_network_start(void (*wifi_event)(net_event_t));
    void app_connect_wifi(char *ssid, char *password);
    bool app_wifi_lock(uint32_t timeout_ms);
    void app_wifi_unlock(void);
    void app_wifi_state_set(wifi_scan_status_t status);

#ifdef __cplusplus
}
#endif
