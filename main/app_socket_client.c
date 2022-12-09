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
#include "project_common.h"

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

//判断是否为透传模式的回调函数
bool (*app_socket_check_is_pass)(void) = NULL;
//设置进入透传模式的回调函数
void (*app_socket_set_to_pass)(void) = NULL;
static bool app_socket_client_reconnect(const struct sockaddr_in *paddr);

// 通过socket client发送数据到server
void app_socket_client_send_data(uint8_t *data, uint16_t len)
{
    client_send_q_t send_buf;
    send_buf.len = len;
    if (send_buf.len > CLIENT_QUQUE_DATA_SIZE) {
        send_buf.len = CLIENT_QUQUE_DATA_SIZE;
    }
    memcpy(send_buf.data, data, send_buf.len);
    xQueueSend(client_send_queue, (void *) &send_buf, 0);
}

//socket client 接收任务
static void app_socket_client_recv_task(void *arg)
{
    static uint8_t rx_buffer[CLIENT_QUQUE_DATA_SIZE];
    ESP_LOGI(TAG, "app_socket_client_recv_task");
    while (1)
    {
        int len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
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

            // 在透传模式下，接受到的数据传送到串口
            if (app_socket_check_is_pass != NULL)
            {
                if (app_socket_check_is_pass()) {
                    USER_UART_SEND(rx_buffer, len);
                }
            }
        }
    }
}

//socket client 发送任务
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
        USER_UART_INFO("[ATPC] ERROR:3\n");//连接 server失败

        xSemaphoreGive(reconn_metex);
        return false;
    }
    ESP_LOGI(TAG, "Successfully connected");

    xSemaphoreGive(reconn_metex);
    return true;
}

// ATPC指令对应处理函数
void app_socket_client_connect(int argc, char** argv)
{
    if (argv[0] != NULL) {//打印命令描述
        ESP_LOGI(TAG, "%s", argv[0]);
    }
    if ( (argv[1]==NULL)||(argv[2]==NULL)||(argv[3]==NULL) )
    {
        USER_UART_INFO("[ATPC] ERROR:2\n");//参数错误
        return;
    }
    //是否连接上ap了
    if ( !app_wifi_is_connect_ap() ) {
        USER_UART_INFO("[ATPC] ERROR:4\n");//尚未连接AP
        return;
    }

    char *ip_addr=argv[2];
    char *ip_port=argv[3];

    static bool is_inited = false;
    if (is_inited) {//当前版本只允许创建一个client
        ESP_LOGE(TAG, "Socket is inited");
        return;
    }

    //初始化服务器信息，并发起首次连接
    dest_addr.sin_addr.s_addr = inet_addr((char*)ip_addr);
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons((uint16_t)atoi((char*)ip_port));
    if (false == app_socket_client_reconnect((struct sockaddr_in*)&dest_addr)) {
        return;
    }
    is_inited = true;
    ESP_LOGI(TAG, "Successfully connected");
    USER_UART_INFO("[ATPC] OK\n");

    //create queue
    client_send_queue = xQueueCreate(CLIENT_QUQUE_NUMB, sizeof(client_send_q_t));

    //设置进入透传模式
    if (app_socket_set_to_pass != NULL) {
        app_socket_set_to_pass();
    }
    xTaskCreate(app_socket_client_recv_task, "app_socket_client_recv_task", 4*1024UL, NULL, 12, NULL);
    xTaskCreate(app_socket_client_send_task, "app_socket_client_send_task", 4*1024UL, NULL, 11, NULL);

    return;
}

/**
* @brief 注册socket模块的回调函数
* @param[in] check_pass:判断是否为透传模式的回调函数
* @param[in] set_pass:设置进入透传模式的回调函数
*/
void app_socket_regist_cb(bool (*check_pass)(void), void (*set_pass)(void))
{
    app_socket_check_is_pass = check_pass;
    app_socket_set_to_pass   = set_pass;
}
