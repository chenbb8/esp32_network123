#include <string.h>
#include <sys/param.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "lwip/err.h"
#include "lwip/sockets.h"

#include "app_socket_client.h"

#define CLIENT_QUQUE_NUMB       5
#define CLIENT_QUQUE_DATA_SIZE  512
typedef struct {
    uint16_t len;
    uint8_t  data[CLIENT_QUQUE_DATA_SIZE];
} client_send_q_t;

#define INVALID_SOCKET          (-1)
static const char *TAG = "socket_client";
static volatile int sock = INVALID_SOCKET;
static volatile struct sockaddr_in dest_addr;
static volatile QueueHandle_t client_send_queue;

static bool app_socket_client_reconnect(const struct sockaddr_in *paddr);

static void app_socket_client_recv_task(void *arg)
{
    static client_send_q_t rx_buffer;
    ESP_LOGI(TAG, "app_socket_client_recv_task");
    while (1)
    {
        int len = recv(sock, rx_buffer.data, sizeof(rx_buffer.data) - 1, 0);
        // Error occurred during receiving
        if (len <= 0)
        {
            if (len < 0) {
                ESP_LOGE(TAG, "recv failed: errno %d", errno);
            }
            if ( ((errno==ENOTCONN || errno==ECONNRESET)&&(len < 0)) || (len == 0) )
            {
                while (1) {//循环重连
                    if (!app_socket_client_reconnect((struct sockaddr_in*)&dest_addr)) {
                        vTaskDelay( 500/portTICK_PERIOD_MS );
                    }
                    else {
                        break;
                    }
                }
            }
        }
        else {
            ESP_LOGI(TAG, "Received %d bytes from %s:", len, inet_ntoa(dest_addr.sin_addr.s_addr));
            ESP_LOGI(TAG, "%s", rx_buffer.data);
            rx_buffer.len = len;

            //echo test
            if (xQueueSend(client_send_queue, (void *) &rx_buffer, 0) == pdTRUE) {
                //
            }
        }
    }
}

static void app_socket_client_send_task(void *arg)
{
    static client_send_q_t send_buffer;
    ESP_LOGI(TAG, "app_socket_client_send_task");
    while (1)
    {
        //等待需要发送的数据
        if (xQueueReceive(client_send_queue, (void *) &send_buffer, portMAX_DELAY) == pdTRUE)
        {
            if (send_buffer.len > 0)
            {
                int len = send(sock, send_buffer.data, send_buffer.len, 0);

                if (len <= 0)
                {
                    if (len < 0) {
                        ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                    }
                    if ( ((errno==ENOTCONN || errno==ECONNRESET)&&(len < 0)) || (len == 0) )
                    {
                        while (1) {//循环重连
                            if (!app_socket_client_reconnect((struct sockaddr_in*)&dest_addr)) {
                                vTaskDelay( 500/portTICK_PERIOD_MS );
                            }
                            else {
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
}

/**
* @brief socket的重连
* @param[in] paddr:服务器相关信息
* @retval true:成功 false:失败
*/
static bool app_socket_client_reconnect(const struct sockaddr_in *paddr)
{
    static SemaphoreHandle_t reconn_metex = NULL;

    if (paddr == NULL) {
        return false;
    }

    if (reconn_metex == NULL) {
        reconn_metex = xSemaphoreCreateMutex();
        ESP_LOGI(TAG, "create reconn_metex!");
    }
    xSemaphoreTake( reconn_metex, portMAX_DELAY );


    if (sock != INVALID_SOCKET)
    {
        close(sock);
        sock = INVALID_SOCKET;
    }
    sock =  socket(AF_INET, SOCK_STREAM, 0);
    int err = connect(sock, (struct sockaddr *)paddr, sizeof(struct sockaddr));

    ESP_LOGI(TAG, "sock=%d,addr=%x,port=%x", sock, (int)paddr->sin_addr.s_addr, paddr->sin_port);
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to reconnect: errno %d", errno);
        close(sock);
        sock = INVALID_SOCKET;

        xSemaphoreGive(reconn_metex);
        return false;
    }
    ESP_LOGI(TAG, "Successfully connected");

    xSemaphoreGive(reconn_metex);
    return true;
}


bool app_socket_client_connect(uint8_t *ip_addr, uint8_t *ip_port)
{
    static bool is_inited = false;
    if ( (ip_addr==NULL) || (ip_port==NULL) )
    {
        ESP_LOGI(TAG, "[ATPC] ERROR:1");//命令格式错误
        return false;
    }
    if (is_inited) {//当前版本只允许创建一个client
        ESP_LOGE(TAG, "Socket is inited");
        return false;
    }

    //初始化服务器信息，并发起首次连接
    dest_addr.sin_addr.s_addr = inet_addr((char*)ip_addr);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons((uint16_t)atoi((char*)ip_port));
    if (false == app_socket_client_reconnect((struct sockaddr_in*)&dest_addr)) {
        return false;
    }
    is_inited = true;
    ESP_LOGI(TAG, "Successfully connected");
    ESP_LOGI(TAG, "[ATPC] OK");

    //create queue
    client_send_queue = xQueueCreate(CLIENT_QUQUE_NUMB, sizeof(client_send_q_t));

    xTaskCreate(app_socket_client_recv_task, "app_socket_client_recv_task", 4*1024UL, NULL, 12, NULL);
    xTaskCreate(app_socket_client_send_task, "app_socket_client_send_task", 4*1024UL, NULL, 11, NULL);

    return true;
}


