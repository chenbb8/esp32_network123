#ifndef _APP_SOCKET_CLIENT_H_
#define _APP_SOCKET_CLIENT_H_

void app_socket_client_send_data(uint8_t *data, uint16_t len);
void app_socket_client_connect(int argc, char** argv);
void app_socket_regist_cb(bool (*check_pass)(void), void (*set_pass)(void));

#endif
