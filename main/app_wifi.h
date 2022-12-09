#ifndef _APP_WIFI_H_
#define _APP_WIFI_H_


void app_wifi_init(void);
bool app_wifi_is_connect_ap(void);
void app_wifi_sta_connect(int argc, char** argv);
void app_wifi_scan(int argc, char** argv);

#endif
