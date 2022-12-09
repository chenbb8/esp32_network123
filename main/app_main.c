#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "project_common.h"

void app_main(void)
{
    app_wifi_init();
    user_uart_init();
}
