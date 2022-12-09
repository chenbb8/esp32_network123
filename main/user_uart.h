#ifndef _USER_UART_H_
#define _USER_UART_H_

#include <stdarg.h>

void user_uart_init(void);
void user_uart_send(uint8_t *data, uint16_t len);
void user_uart_print(char * format, ...);

#endif
