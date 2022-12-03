#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#define DEFAULT_SCAN_LIST_SIZE      50
#define DEFAULT_SCAN_TIME_MIN       0
#define DEFAULT_SCAN_TIME_MAX       100

#define EXAMPLE_ESP_MAXIMUM_RETRY   5

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_STA_START_BIT BIT2

static const char *TAG = "wifi_";
wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];
uint16_t ap_count = 0;
/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;

//打印AP列表
void wifi_print_scan(const wifi_ap_record_t *ap_info, uint8_t print_idx)
{
    char str[128];
    char *pAuthModeStr = "";
    switch (ap_info[print_idx].authmode) {
        case WIFI_AUTH_OPEN:
            pAuthModeStr = "WIFI_AUTH_OPEN";
            break;
        case WIFI_AUTH_OWE:
            pAuthModeStr = "WIFI_AUTH_OWE";
            break;
        case WIFI_AUTH_WEP:
            pAuthModeStr = "WIFI_AUTH_WEP";
            break;
        case WIFI_AUTH_WPA_PSK:
            pAuthModeStr = "WIFI_AUTH_WPA_PSK";
            break;
        case WIFI_AUTH_WPA2_PSK:
            pAuthModeStr = "WIFI_AUTH_WPA2_PSK";
            break;
        case WIFI_AUTH_WPA_WPA2_PSK:
            pAuthModeStr = "WIFI_AUTH_WPA_WPA2_PSK";
            break;
        case WIFI_AUTH_WPA2_ENTERPRISE:
            pAuthModeStr = "WIFI_AUTH_WPA2_ENTERPRISE";
            break;
        case WIFI_AUTH_WPA3_PSK:
            pAuthModeStr = "WIFI_AUTH_WPA3_PSK";
            break;
        case WIFI_AUTH_WPA2_WPA3_PSK:
            pAuthModeStr = "WIFI_AUTH_WPA2_WPA3_PSK";
            break;
        default:
            pAuthModeStr = "WIFI_AUTH_UNKNOWN";
            break;
    }

    snprintf(str, sizeof(str), "AP:%d,%s,%d,%s,%d,%d:%d:%d:%d:%d:%d",
        print_idx,ap_info[print_idx].ssid,ap_info[print_idx].primary,
        pAuthModeStr,ap_info[print_idx].rssi,
        ap_info[print_idx].bssid[0],ap_info[print_idx].bssid[01],ap_info[print_idx].bssid[2],
        ap_info[print_idx].bssid[3],ap_info[print_idx].bssid[4],ap_info[print_idx].bssid[5]
        );
    ESP_LOGI(TAG, "%s",str);
}

/* set scan method */
static void wifi_scan(void)
{
    // wifi_scan_config_t scanConf = {
    //     .ssid = NULL,
    //     .bssid = NULL,
    //     .channel = 0,//0为全频道扫描 1-14为指定信道扫描
    //     .show_hidden = false,
    //     .scan_type = WIFI_SCAN_TYPE_ACTIVE,/*主动模式 */
    //     .scan_time.passive = 500,  /*被动模式的每个通道的扫描时间 */
    //     .scan_time.active.min = DEFAULT_SCAN_TIME_MIN,
    //     .scan_time.active.max = DEFAULT_SCAN_TIME_MAX,
    // };

    uint16_t number = DEFAULT_SCAN_LIST_SIZE;
    memset(ap_info, 0, sizeof(ap_info));

    esp_wifi_scan_start(NULL, true);
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&number, ap_info));
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    ESP_LOGI(TAG, "Total APs scanned = %u", ap_count);
    for (int i = 0; (i < DEFAULT_SCAN_LIST_SIZE) && (i < ap_count); i++) {
        wifi_print_scan(ap_info,i);
    }
}

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

static bool wifi_get_authmode(wifi_auth_mode_t *read_mod, uint8_t * ssid)
{
    uint8_t i;
    if (read_mod != NULL || ssid != NULL) {
        for (i = 0; i < ap_count; ++i)
        {
            if (memcmp(ap_info[i].ssid, ssid, strlen((char*)ssid)) == 0)
            {
                *read_mod = ap_info[i].authmode;
                return true;
            }
        }
    }
    else {
        return false;
    }
    return false;
}

static void wifi_sta_connect(uint8_t *ssid, uint8_t *password)
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
void wifi_task(void *arg)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    s_wifi_event_group = xEventGroupCreate();
    // Initialize Wi-Fi as sta
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.nvs_enable = 0;//禁用menuconfig中的CONFIG_ESP32_WIFI_NVS_ENABLED
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

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

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());


    // EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
    //         WIFI_STA_START_BIT,
    //         pdFALSE,
    //         pdFALSE,
    //         portMAX_DELAY);
    wifi_scan();
    wifi_sta_connect((uint8_t*)"casa", (uint8_t*)"12345678");//用来测试的AP热点：casa
    while(1) {
        vTaskDelay( 5000/portTICK_PERIOD_MS );
        ESP_LOGI(TAG, "delay");
    }
}

