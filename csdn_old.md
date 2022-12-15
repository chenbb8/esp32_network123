这个文档对应的是V1.0.0之前的版本。

# 创建工程
通过<[新建esp32的vscode工程的三种方式](https://blog.csdn.net/chenbb8/article/details/128134802)>中的<通过"ESP-IDF:New Project"创建新工程>这个方法，创建一个以esp-idf/examples/wifi/scan为模板的工程esp32_network123，这也是本文中的主工程模板
接着再通过<通过vscode的"ESP-IDF:Show Examples Project"页面中新建工程>这个方法，通过SDK中的以下几个例子：

```
esp-idf/examples/wifi/scan
esp-idf/examples/wifi/getting_started/station
esp-idf/examples/protocols/sockets/udp_client
esp-idf/examples/peripherals/uart/uart_echo
```

为模板分别创建几个参考的工程。
# 扫描AP
在vscode下打开工程esp32_network123工程，并在main文件夹上右键，选中"New File"，新建两个文件：app_main.c和wifi.c。并将原本自带的scan.c文件删除。

> 注：这种通过vscode来新建和删除文件的行为，会通过插件，自动的修改对应的CMakeLists.txt文件，省却了一些麻烦的操作。但有时候可能会因为git的文件冲突，而自动在CMakeLists.txt文件加入一些git生成的额外源文件。

其中在app_main.c内添加app_main()用于调用各种模块：

```c
#define WIFI_TASK_STACK_SIZE  (1024*16)
extern void wifi_task(void *arg);
void app_main(void)
{
    xTaskCreate(wifi_task, "wifi_task", WIFI_TASK_STACK_SIZE, NULL, 10, NULL);
}
```
而wifi.c则参考原有的scan.c，在wifi.c内实现的wifi_task()任务用来初始化WIFI，并配置成sta模式

```c
void wifi_task(void *arg)
{
...
    // Initialize Wi-Fi as sta
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    wifi_scan();
    while(1) {
        vTaskDelay( 5000/portTICK_PERIOD_MS );
        ESP_LOGI(TAG, "delay");
    }
}
```
上面的代码中调用的wifi_scan()调用后直接触发扫描，并将结果打印出来：

```cpp
static void wifi_scan(void)
{
    uint16_t number = DEFAULT_SCAN_LIST_SIZE;
    memset(ap_info, 0, sizeof(ap_info));

    esp_wifi_scan_start(NULL, true);
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_info));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    ESP_LOGI(TAG, "Total APs scanned = %u", ap_count);
    for (int i = 0; (i < DEFAULT_SCAN_LIST_SIZE) && (i < ap_count); i++) {
        wifi_print_scan(ap_info,i);//按照需求的格式打印AP列表
    }
}
```
# 连接AP
前面wifi_task()中已经配置esp32为station，这里只需要在wifi_task()中插入几行代码：

```c
void wifi_task(void *arg)
{
...
    s_wifi_event_group = xEventGroupCreate();/*!新增代码1*/
    // Initialize Wi-Fi as sta
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.nvs_enable = 0;/*!新增代码2*/
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

/*!新增代码3 start*/
    //注册相关事件的回调处理函数
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
/*!新增代码3 end*/
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    wifi_scan();
    wifi_station((uint8_t*)"casa", (uint8_t*)"12345678");/*!新增代码4*/
...
}
```
上述代码中：
- 新增代码1：初始化了事件组s_wifi_event_group
- 新增代码2：我上传的代码没有附带menuconfig，而新生成的menuconfig默认通过的CONFIG_ESP32_WIFI_NVS_ENABLED选项，使能了自动连接AP的功能。所以这里禁用这个自动连接功能。
- 新增代码3：用于配置在event_handler()中接受的event，这里是接受WIFI_EVENT和IP_EVENT两种类型。
- 新增代码4：调用wifi_station()连接对应的AP，参数是我本地测试用的手机热点。
<br>
ESP32 Wi-Fi 编程模型如下图所示：
![在这里插入图片描述](https://img-blog.csdnimg.cn/86d1371c91d742baa404ad6f32737772.png#pic_center)
上面提到的event_handler()就是Event task调用的callback，它当前的实现如下：

```c
static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_STA_START_BIT);//通知：sta初始化结束（暂时没用到）
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);//通知：连接AP失败
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);//通知：连接AP成功
    }
}
```
在上面的代码中，分别通过WIFI_EVENT和IP_EVENT事件获取了连接AP的状态，并通过事件组s_wifi_event_group通知对应的任务。
```c
void wifi_sta_connect(uint8_t *ssid, uint8_t *password)
{ 
    wifi_config_t wifi_config = {
       .sta = {
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
       },
    };
    strncpy ((char*)wifi_config.sta.ssid, (char*)ssid, sizeof(wifi_config.sta.ssid));
    strncpy ((char*)wifi_config.sta.password, (char*)password, sizeof(wifi_config.sta.ssid));
    if (wifi_get_authmode(&wifi_config.sta.threshold.authmode, ssid) != false)
    {
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
        ESP_LOGI(TAG, "wifi_init_sta finished.");
        s_retry_num = 0;//ap重连次数清零
        esp_wifi_connect();//触发首次连接AP

        /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
        * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                pdFALSE,
                pdFALSE,
                portMAX_DELAY);

        /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
        * happened. */
        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                    ssid, password);
            ESP_LOGI(TAG, "[ATPN] OK");
        } else if (bits & WIFI_FAIL_BIT) {
            ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                    ssid, password);
            ESP_LOGI(TAG, "[ATPN] ERROR:3");//连接AP失败
        } else {
            ESP_LOGE(TAG, "UNEXPECTED EVENT");
            ESP_LOGI(TAG, "[ATPN] ERROR:3");//连接AP失败
        }
    }
    else
    {
        ESP_LOGI(TAG, "[ATPN] ERROR:5");//无ap信息
    }
}
```
上面的代码主要是通过调用wifi_get_authmode()，从之前wifi_scan()存入到ap_info[]中的数据，获取路由器的加密方式。
接着调用esp_wifi_connect()进行首次连接ap。
然后通过事件组s_wifi_event_group接受WIFI_CONNECTED_BIT或者WIFI_FAIL_BIT事件，以判断连接是否成功。
# TCP CLIENT
 新建app_socket_client.c用于存放tcp client相关代码。
 app_socket_client.c对外提供接口app_socket_client_connect()。
 在前面提到的wifi_task()中，连接上ap后，加上一行代码用于调用tcp client功能

```c
 app_socket_client_connect((uint8_t*)"192.168.2.140", (uint8_t*)"3333");
```
而app_socket_client_connect()的相关实现如下：

```c
#define INVALID_SOCKET          (-1)
static volatile int sock = INVALID_SOCKET;
static volatile struct sockaddr_in dest_addr;
/**
* @brief socket的重连
* @param[in] paddr:服务器相关信息
* @retval true:成功 false:失败
*/
static bool app_socket_client_reconnect(const struct sockaddr_in *paddr)
{
...
    if (sock != INVALID_SOCKET)
    {
        close(sock);
        sock = INVALID_SOCKET;
    }
    sock =  socket(AF_INET, SOCK_STREAM, 0);
    int err = connect(sock, (struct sockaddr *)paddr, sizeof(paddr));

    ESP_LOGI(TAG, "sock=%d,addr=%x,port=%x", sock, (int)paddr->sin_addr.s_addr, paddr->sin_port);
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to reconnect: errno %d", errno);
        close(sock);
        sock = INVALID_SOCKET;
        return false;
    }
    ESP_LOGI(TAG, "Successfully connected");
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
    cline_send_queue = xQueueCreate(CLIENT_QUQUE_NUMB, sizeof(client_send_q_t));

    xTaskCreate(app_socket_client_send_task, "app_socket_client_send_task", 4*1024UL, NULL, 11, NULL);
    xTaskCreate(app_socket_client_recv_task, "app_socket_client_recv_task", 4*1024UL, NULL, 12, NULL);

    return true;
}
```
为了方便展示，本demo中实现的socket tcp client所使用的recv()/send()是默认的阻塞模式，因此app_socket_client_connect()在成功连接server后，还需要创建两个任务app_socket_client_recv_task和app_socket_client_send_task。前者阻塞接收server发来的数据；后者等待队列client_send_queue中的数据，有则发送：

```c
#define CLIENT_QUQUE_NUMB       5
#define CLIENT_QUQUE_DATA_SIZE  512
typedef struct {
    uint16_t len;
    uint8_t  data[CLIENT_QUQUE_DATA_SIZE];
} client_send_q_t;
static volatile QueueHandle_t client_send_queue;
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
```

# 未完待续。有空的时候就一边撸代码，一边写笔记。
