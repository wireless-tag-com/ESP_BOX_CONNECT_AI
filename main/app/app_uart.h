#pragma once

#include <stdint.h>

void app_uart_init();
int app_uart_send(const void *data, uint32_t length);
int app_uart_read(void *data, uint32_t length);