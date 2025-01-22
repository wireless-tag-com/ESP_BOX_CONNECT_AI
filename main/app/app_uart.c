#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_log.h"

#define UART_TXD (38)
#define UART_RXD (39)
#define UART_RTS (UART_PIN_NO_CHANGE)
#define UART_CTS (UART_PIN_NO_CHANGE)

#define UART_PORT_NUM (1)
#define CHAT_UART_PORT_NUM (0)
#define UART_BAUD_RATE (115200)

#define BUF_SIZE 1024

static const char *TAG = "APP_UART";

// 发送UART数据
int app_uart_send(const void *data, size_t length)
{
    return uart_write_bytes(UART_PORT_NUM, (const char *)data, length);
}

// 读取UART数据
int app_uart_read(void *data, size_t length)
{
    return uart_read_bytes(UART_PORT_NUM, data, length, 20 / portTICK_PERIOD_MS);
}

// 初始化UART
void app_uart_init()
{
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    int intr_alloc_flags = 0;

    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, intr_alloc_flags));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, UART_TXD, UART_RXD, UART_RTS, UART_CTS));

    ESP_LOGI(TAG, "Uart init finsh");
}
