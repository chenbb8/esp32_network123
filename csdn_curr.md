esp32作为一个热门芯片，网上的文章很多，sdk里的例子和官网的教程也是比较详细。不过作为新玩家，还是要先做一些小改动才更容易入门。所以这里就综合几个example代码，写一个简单的透传demo。
==作为一个练手的demo，很多参数的校验被省略了，并且也不会刻意的去纠正用户的错误操作。为了简洁，本文中贴出来的代码中删减了诸如参数检测、共享资源保护之类的代码==
# 环境
操作系统:ubuntu 20.04
虚拟机：VMare Workstation 16
IDE：vscode 1.73.1
vscode插件：Espressif IDF v1.5.1
board：淘宝上的ESP32-S3-DevKitC-1兼容板
外置串口板子：淘宝上的cp2102 6合1串口模块
串口终端/tcp server：sscom5
# 需求
通过外部的串口模块，连接ESP32-S3-DevKitC-1开发板上的IO4和5，与电脑进行通讯。
而串口指令，参考一下某透传模块的文档，提取并简化成了以下几条(==每条指令后应该加上换行符，比如"\n"、"\r\n"，否则将不识别==)：

- 扫描AP

指令 | ATWS
---|---
响应| AP : &lt;num>,&lt;ssid>,&lt;chl>,&lt;sec>,&lt;rssi>,&lt;bssid> <br>[ATWS] OK 

- 连接到 AP

指令 | ATPN=&lt;ssid>,&lt;pwd>
---|---
响应 | 成功 <br>[ATPN] OK <br>失败 <br>[ATPN] ERROR:<error_code>
error_code | 1: 命令格式错误<br>2: 参数错误<br>3: 连接 AP失败<br>4: dhcp 超时<br>5：无ap信息

- 建立socket

指令 | ATPC=&lt;mode>,&lt;Remote Addr>,&lt;Remote Port>
---|---
响应 | 成功 <br>[ATPC] OK <br>失败 <br>[ATPC]:<error_code>
参数 | mode: <br> &emsp;0:TCP
error_code | 1: 命令格式错误<br>2: 参数错误<br>3: 连接 server失败<br>4: 尚未连接AP
注意|连接成功后直接进入透传模式


# 代码
https://github.com/chenbb8/esp32_network123
代码clone下来后，还需要用到<[新建esp32的vscode工程的三种方式](https://blog.csdn.net/chenbb8/article/details/128134802)>中的<将普通esp-idf工程升级成esp-vscode工程>这个方法，修改一下本地的程序。
并且还需要重新设置串口号/芯片型号。
# 代码结构概述

主要的文件，及其功能如下
```c
|   |--app_main.c            入口app_main()函数所在文件，调用了WIFI和UART模块的初始化函数
|   |--app_socket_client.c   实现tcp socket功能
|   |--app_socket_client.h
|   |--app_wifi.c            WIFI功能
|   |--app_wifi.h
|   |--at_cmd.c              AT指令的解析和执行
|   |--at_cmd.h
|   |--project_common.h      全局包含的头文件
|   |--user_uart.c           UART功能
|   |--user_uart.h
```
> 注：如果文件是通过vscode来新建和删除的，会通过插件，自动的修改对应的CMakeLists.txt文件，省却了一些麻烦的操作。但有时候可能会因为git的文件冲突，而自动在CMakeLists.txt文件加入一些git生成的额外源文件。

而用户的串口数据与tcp client数据之间交互的关系如下
![在这里插入图片描述](https://img-blog.csdnimg.cn/2883510b1e1f42b49c26f2abcd64f9ec.png#pic_center)
接下来跟着文章，一步步的解析整个工程的实现吧。

> 本文其实有好多个版本，从头记录着demo的搭建过程，跟github上master分支上的历史版本一一对应。不过因为感觉代码有点混乱，并且原本一步步搭建的文章写法与v1.0.0代码有出入；因此在新版本中将部分文件名还有一些必要的代码结构进行了修改，并且后面的章节都是基于v1.0.0版本编写的。

# 串口服务
```
参考例子esp-idf/examples/peripherals/uart/uart_echo
```
串口服务对应两个模块：user_uart / at_cmd。
其中user_uart模块下，初始化函数user_uart_init()主要功能就是创建user_uart_task任务，这个任务负责初始化串口，并将接收到的数据转发到at_cmd模块或者app_socket_client模块。
这里只贴出app_socket_client模块初始化部分的代码：

```c
#define ECHO_TEST_TXD (4)
#define ECHO_TEST_RXD (5)
#define ECHO_TEST_RTS (UART_PIN_NO_CHANGE)
#define ECHO_TEST_CTS (UART_PIN_NO_CHANGE)

#define ECHO_UART_PORT_NUM      (2)
#define ECHO_UART_BAUD_RATE     (115200)
#define ECHO_TASK_STACK_SIZE    (4*1024UL)

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
...
}
```
其中app_socket_regist_cb()用于给app_socket_client模块注册回调，以便它可以获取当前是否为透传模式，并且可以在连接上server后自动设置成透传模式。
而at_cmd模块的at_cmd_recv_hand()函数在at模式下会被user_uart_task()任务所调用。
at_cmd_recv_hand()函数的功能是根据at_cmds[]获取需要调用哪个AT指令的处理函数。

```c
typedef struct at_cmd_entry {
    const char  *cmd_str;//指令匹配字符串
    uint16_t    cmd_str_len;
    uint8_t     min_args;//指令最少的参数数量
    void (*process_cmd)(int argc, char** argv);//argc:参数数量 argv:指向存放参数字符串的指针数组
    const char  *cmd_desc_str;//指令描述
    const char  *cmd_args_numb_fail;//参数达不到最低要求时候的报错
} at_cmd_entry_t;
/* cmd 入口数组 */
at_cmd_entry_t at_cmds[] = {
//    cmd_str   cmd_str_len         min_args    process_cmd                 cmd_desc_str    cmd_args_numb_fail
    { "ATWS",   sizeof("ATWS")-1,   0+1,        app_wifi_scan,              ATWS_DESC,      ATWS_ARGS_FAIL},
    { "ATPN=",  sizeof("ATPN=")-1,  2+1,        app_wifi_sta_connect,       ATPN_DESC,      ATPN_ARGS_FAIL},
    { "ATPC=",  sizeof("ATPC=")-1,  3+1,        app_socket_client_connect,  ATPC_DESC,      ATPC_ARGS_FAIL},
};
```
以上AT处理函数的类型，以app_wifi_sta_connect()为例：

```c
void app_wifi_sta_connect(int argc, char** argv)；
```
因此at_cmd_recv_hand()函数还需要将用户输入到串口中的参数，以','符号为界，区分成不同的段，每个段作为一个参数，写入到char** argv对应的数组中。具体参考工程，这里就不详细展开了。
# 扫描AP
```
参考例子esp-idf/examples/wifi/scan
```
通过vscode工程打开app_wifi.c，首先先看初始化
```c
static EventGroupHandle_t s_wifi_event_group;
//WIFI相关功能的初始化
void app_wifi_init(void)
{
...
    s_wifi_event_group = xEventGroupCreate(); /*!新增代码1*/
    // Initialize Wi-Fi as sta
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    //禁用menuconfig中的CONFIG_ESP32_WIFI_NVS_ENABLED
    cfg.nvs_enable = 0; /*!新增代码2*/
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

/*!新增代码3 start*/
    //设置回调函数
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
}
```
这个app_wifi_init()同时服务于==扫描AP==和==连接AP==两个功能，其中有"/*!新增代码 x/"注释部分的代码，属于==连接AP==章节讲解的内容。
在app_wifi.c中，有一个AT指令处理函数，对应着ATWS指令：

```cpp
#define DEFAULT_SCAN_LIST_SIZE      50
wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];
uint16_t ap_count = 0;
/* set scan method */
void app_wifi_scan(int argc, char** argv)
{
...
    uint16_t number = DEFAULT_SCAN_LIST_SIZE;
    memset(ap_info, 0, sizeof(ap_info));

    esp_wifi_scan_start(NULL, true);
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_info));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    ESP_LOGI(TAG, "Total APs scanned = %u", ap_count);
    for (int i = 0; (i < DEFAULT_SCAN_LIST_SIZE) && (i < ap_count); i++) {
        app_wifi_print_scan(ap_info,i);
    }
    USER_UART_INFO("[ATWS] OK\n");
}
```
成功后，用app_wifi_print_scan()函数将结果打印出来；并且会将AP list存放在ap_info中，这在==连接AP==的环节中，还要从里面获取到对应SSID的加密方式。

# 连接AP
```
参考例子esp-idf/examples/wifi/getting_started/station
```
本章节和==扫描AP==章节共用app_wifi_init()函数，参考上章节中的代码，其中：
- 新增代码1：初始化了事件组s_wifi_event_group
- 新增代码2：我上传的代码没有附带menuconfig，而新生成的menuconfig默认通过的CONFIG_ESP32_WIFI_NVS_ENABLED选项，使能了自动连接AP的功能。所以这里禁用这个自动连接功能。
- 新增代码3：用于配置在event_handler()中接受的event，这里是接受WIFI_EVENT和IP_EVENT两种类型。


ESP32 Wi-Fi 编程模型如下图所示：
![在这里插入图片描述](https://img-blog.csdnimg.cn/86d1371c91d742baa404ad6f32737772.png#pic_center)
上面提到的event_handler()就是Event task调用的callback，它当前的实现如下：

```c
// 在Event task内调用的事件处理函数
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
在app_wifi.c中，还有一个AT指令处理函数，对应着ATPN指令：

```c
// ATPN指令对应处理函数
void app_wifi_sta_connect(int argc, char** argv)
{
...
    char *ssid=argv[1], *password=argv[2];
    wifi_config_t wifi_config = {
       .sta = {
            .sae_pwe_h2e = WPA3_SAE_PWE_BOTH,
       },
    };
    strncpy ((char*)wifi_config.sta.ssid, (char*)ssid, sizeof(wifi_config.sta.ssid));
    strncpy ((char*)wifi_config.sta.password, (char*)password, sizeof(wifi_config.sta.password));
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
            USER_UART_INFO("[ATPN] OK\n");
            is_connect_ap = true;//已经连接上AP了
        } else if (bits & WIFI_FAIL_BIT) {
            ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                    ssid, password);
            USER_UART_INFO("[ATPN] ERROR:3\n");//连接AP失败
        } else {
            ESP_LOGE(TAG, "UNEXPECTED EVENT");
            USER_UART_INFO("[ATPN] ERROR:3\n");//连接AP失败
        }
    }
    else
    {
        USER_UART_INFO("[ATPN] ERROR:5\n");//无ap信息
    }
}
```
上面的代码主要是通过调用wifi_get_authmode()，从之前wifi_scan()存入到ap_info[]中的数据，获取路由器的加密方式。
接着调用esp_wifi_connect()进行首次连接ap。
然后通过事件组s_wifi_event_group接受WIFI_CONNECTED_BIT或者WIFI_FAIL_BIT事件，以判断连接是否成功。
# TCP CLIENT
```
参考例子esp-idf/examples/protocols/sockets/udp_client
如果想用基于select机制的非阻塞的recv/send，可以参考例子esp-idf/examples/protocols/sockets/non_blocking
```
本功能对应模块app_socket_client。
在app_socket_client.c中，有一个AT指令处理函数，对应着ATPC指令，它们的相关实现如下：

```c
#define CLIENT_QUQUE_NUMB       5
#define CLIENT_QUQUE_DATA_SIZE  512
typedef struct {
    uint16_t len;
    uint8_t  data[CLIENT_QUQUE_DATA_SIZE];
} client_send_q_t;
#define INVALID_SOCKET          (-1)
static volatile int sock = INVALID_SOCKET;
static volatile struct sockaddr_in dest_addr;
static volatile QueueHandle_t client_send_queue;
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
    int err = connect(sock, (struct sockaddr *)paddr, sizeof(struct sockaddr));

    ESP_LOGI(TAG, "sock=%d,addr=%x,port=%x", sock, (int)paddr->sin_addr.s_addr, paddr->sin_port);
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to reconnect: errno %d", errno);
        close(sock);
        sock = INVALID_SOCKET;
        USER_UART_INFO("[ATPC] ERROR:3\n");//连接 server失败

        return false;
    }
    ESP_LOGI(TAG, "Successfully connected");

    return true;
}

// ATPC指令对应处理函数
void app_socket_client_connect(int argc, char** argv)
{
...
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
```
为了方便展示，本demo中实现的socket tcp client所使用的recv()/send()是默认的阻塞模式，因此app_socket_client_connect()在成功连接server后，还需要创建两个任务app_socket_client_recv_task和app_socket_client_send_task。前者阻塞接收server发来的数据；后者等待队列client_send_queue中的数据，有则发送：

```c
//判断是否为透传模式的回调函数
bool (*app_socket_check_is_pass)(void) = NULL;
//设置进入透传模式的回调函数
void (*app_socket_set_to_pass)(void) = NULL;
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
```
# 测试
为了方便演示，这里串口终端和tcp server使用的工具都是常用的sscom5
测试结果如下：
![在这里插入图片描述](https://img-blog.csdnimg.cn/25e7a6098e8947e2aa6a9c5bd8f5afe2.png#pic_center)