#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "project_common.h"

#define ECHO_TEST_TXD (4)
#define ECHO_TEST_RXD (5)
#define ECHO_TEST_RTS (UART_PIN_NO_CHANGE)
#define ECHO_TEST_CTS (UART_PIN_NO_CHANGE)

#define ECHO_UART_PORT_NUM      (2)
#define ECHO_UART_BAUD_RATE     (115200)
#define ECHO_TASK_STACK_SIZE    (4*1024UL)

static const char *TAG = "UART_TASK";
#define BUF_SIZE (1024)

//user uart模块的任务
static void user_uart_task(void *arg)
{
    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    uart_config_t uart_config = {
        .baud_rate = ECHO_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    int intr_alloc_flags = 0;

    ESP_ERROR_CHECK(uart_driver_install(ECHO_UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, intr_alloc_flags));
    ESP_ERROR_CHECK(uart_param_config(ECHO_UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(ECHO_UART_PORT_NUM, ECHO_TEST_TXD, ECHO_TEST_RXD, ECHO_TEST_RTS, ECHO_TEST_CTS));

    // Configure a temporary buffer for the incoming data
    uint8_t *data = (uint8_t *) malloc(BUF_SIZE);

    //注册socket模块的回调函数
    app_socket_regist_cb(at_cmd_is_in_pass_mode, at_cmd_set_pass_mode);

    while (1) {
        // Read data from the UART
        int len = uart_read_bytes(ECHO_UART_PORT_NUM, data, (BUF_SIZE - 1), 20 / portTICK_PERIOD_MS);
        if (len) {
            data[len] = '\0';
            ESP_LOGI(TAG, "Recv str: %s", (char *) data);
        }
        if (len > 0) {
            if (!at_cmd_is_in_pass_mode()) {//AT mode
                user_uart_send(data, len);//echo
                at_cmd_recv_hand((char*)data, len);
            }
            else {//透传
                // 通过socket client发送数据到server
                app_socket_client_send_data(data, len);
            }
        }
    }
}
//user uart模块的初始化
void user_uart_init(void)
{
    xTaskCreate(user_uart_task, "user_uart_task", ECHO_TASK_STACK_SIZE, NULL, 10, NULL);
}
//通过user uart将普通数据发送出去
void user_uart_send(uint8_t *data, uint16_t len)
{
    uart_write_bytes(ECHO_UART_PORT_NUM, (const char *) data, len);
}
//通过user uart将格式化字符发送出去
void user_uart_print(char * format, ...)
{
    char buffer[256];
    int len;
    va_list args;
    va_start(args, format);
    len = vsnprintf(buffer, 255, format, args);
    if (len >= 0) {
        user_uart_send((uint8_t*)buffer, len);
    }
    va_end (args);
}
