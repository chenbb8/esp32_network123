#ifndef _PROJECT_COMMON_H_
#define _PROJECT_COMMON_H_

#include "user_uart.h"
#include "app_wifi.h"
#include "app_socket_client.h"
#include "at_cmd.h"

#if 0
//测试用
#define USER_UART_SEND(DATA,NUMB)
#define USER_UART_INFO(...) ESP_LOGI(TAG, __VA_ARGS__)
#else
//正常用
#define USER_UART_SEND(DATA,NUMB) user_uart_send((DATA),(NUMB))
#define USER_UART_INFO(...) user_uart_print(__VA_ARGS__)
#endif

#endif
