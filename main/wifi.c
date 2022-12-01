#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"

#define DEFAULT_SCAN_LIST_SIZE   50
#define DEFAULT_SCAN_TIME_MIN    0
#define DEFAULT_SCAN_TIME_MAX    100

static const char *TAG = "wifi";
wifi_ap_record_t ap_info[DEFAULT_SCAN_LIST_SIZE];
uint16_t ap_count = 0;

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

void wifi_task(void *arg)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

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

