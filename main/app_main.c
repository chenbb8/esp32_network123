#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define WIFI_TASK_STACK_SIZE  (1024*16)

extern void wifi_task(void *arg);

void app_main(void)
{
    xTaskCreate(wifi_task, "wifi_task", WIFI_TASK_STACK_SIZE, NULL, 10, NULL);
}
