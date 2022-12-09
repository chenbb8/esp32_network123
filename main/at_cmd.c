#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_log.h"
#include "project_common.h"

#define CMD_ARGS_VALID_MAX  10/*process_cmd()最多的有效参数数量,参数由一个指向cmd_desc_str的自我描述，外加CMD_ARGS_VALID_MAX个有效参数组成*/
#define CMD_TAIL_MIN        1/*指令默认以\n或者\r或者\r\n为结尾*/

static const char *TAG = "at_cmd";

typedef struct at_cmd_entry {
    const char  *cmd_str;//指令匹配字符串
    uint16_t    cmd_str_len;
    uint8_t     min_args;//指令最少的参数数量
    void (*process_cmd)(int argc, char** argv);//argc:参数数量 argv:指向存放参数字符串的指针数组
    const char  *cmd_desc_str;//指令描述
    const char  *cmd_args_numb_fail;//参数达不到最低要求时候的报错
} at_cmd_entry_t;

static volatile uint32_t is_in_at_mode = true;//默认AT模式
//cmd_desc_str
const char ATWS_DESC[] = "scan ap list\n";
const char ATPN_DESC[] = "connect to ap\n";
const char ATPC_DESC[] = "create sock\n";

//cmd_args_numb_fail
const char *const ATWS_ARGS_FAIL  = NULL;
const char ATPN_ARGS_FAIL[] = "[ATPN] ERROR:1";
const char ATPC_ARGS_FAIL[] = "[ATPC] ERROR:1";

/* cmd 入口数组 */
at_cmd_entry_t at_cmds[] = {
//    cmd_str   cmd_str_len         min_args    process_cmd                 cmd_desc_str    cmd_args_numb_fail
    { "ATWS",   sizeof("ATWS")-1,   0+1,        app_wifi_scan,              ATWS_DESC,      ATWS_ARGS_FAIL},
    { "ATPN=",  sizeof("ATPN=")-1,  2+1,        app_wifi_sta_connect,       ATPN_DESC,      ATPN_ARGS_FAIL},
    { "ATPC=",  sizeof("ATPC=")-1,  3+1,        app_socket_client_connect,  ATPC_DESC,      ATPC_ARGS_FAIL},
};

// 判断是否为透传模式
bool at_cmd_is_in_pass_mode(void)
{
    return !is_in_at_mode;
}
//设置进入透传模式
void at_cmd_set_pass_mode(void)
{
    is_in_at_mode = false;
}
/**
* @brief 将buf中的数据通过','号，区分成不同的段(每个段都认为是一个参数)，并将对应段的地址，填入到args_vector[]中
* @param[out] buf:需要被检测的数据
* @param[in]  len:需要被检测的数据的数量
* @param[out] args_vector:存放检测出来的参数指针
* @param[out] args_valid_numb:检测出的有效参数数量
* @param[in]  args_valid_max:限定最多的有效参数数量
* @retval true:成功 false:失败
*/
static bool at_cmd_set_args(char *buf, uint16_t len, char *args_vector[], 
                            uint8_t *args_valid_numb, uint8_t args_max)
{
    char *ptr_read = buf;
    char *ptr_backup = buf;//指向参数的起始点
    while (ptr_read < (buf+len))
    {
        if (*ptr_read == ',')
        {
            if (ptr_read != ptr_backup)//有参数
            {
                args_vector[*args_valid_numb] = ptr_backup;
                *ptr_read = '\0';
            }
            else {//参数为空
                args_vector[*args_valid_numb] = NULL;
            }
            *args_valid_numb += 1;
            ptr_backup = ptr_read + 1;//指向下一个参数的起始点
            if (*args_valid_numb >= args_max)//参数数量超出，认定为成功
            {
                return true;
            }
        }
        else if ( (*ptr_read == '\r') || (*ptr_read == '\n') )
        {
            if (ptr_read != ptr_backup)//有参数
            {
                args_vector[*args_valid_numb] = ptr_backup;
                *ptr_read = '\0';
            }
            else {//参数为空
                args_vector[*args_valid_numb] = NULL;
            }
            *args_valid_numb += 1;
            return true;
        }
        ptr_read++;
    }
    return false;
}
/**
* @brief at模式下的串口数据处理
* @param[out] buf:需要被处理的数据
* @param[in]  len:需要被处理的数据的数量
*/
void at_cmd_recv_hand(char *buff, uint16_t len)
{
    int cmd_index = 0;
    char *buff_ptr;
    uint16_t len_curr;
    uint8_t args_numb = 0;
    char *args_vector[CMD_ARGS_VALID_MAX+1];//参数由一个指向cmd_desc_str的自我描述，外加CMD_ARGS_VALID_MAX个有效参数组成

    while ( (cmd_index < sizeof(at_cmds)/sizeof(at_cmds[0])) &&
            (at_cmds[cmd_index].cmd_str != NULL) )
    {
        buff_ptr = buff;
        len_curr = len;
        args_numb = 0;//先赋值0代表有效参数
        memset(args_vector, 0, sizeof(args_vector));
        args_vector[0] = (char*)at_cmds[cmd_index].cmd_desc_str;

        if (0 == memcmp(buff, at_cmds[cmd_index].cmd_str, at_cmds[cmd_index].cmd_str_len))
        {
            buff_ptr += at_cmds[cmd_index].cmd_str_len;
            if (len >= (CMD_TAIL_MIN+at_cmds[cmd_index].cmd_str_len))
            {
                len_curr = len - at_cmds[cmd_index].cmd_str_len;
                if (at_cmd_set_args(buff_ptr, len_curr, &args_vector[1],
                        &args_numb, CMD_ARGS_VALID_MAX) == true)
                {
                    args_numb += 1;//有效参数+1个自我描述

                    uint8_t i;
                    ESP_LOGI(TAG, "args_numb=%d", args_numb);
                    for (i = 0; i < args_numb; ++i)
                    {
                        if (args_vector[i] != NULL) {
                            ESP_LOGI(TAG, "args=%s", args_vector[i]);
                        }
                        else {
                            ESP_LOGI(TAG, "args=NULL");
                        }
                    }

                    if (args_numb < at_cmds[cmd_index].min_args) {
                        if (at_cmds[cmd_index].cmd_args_numb_fail != NULL) {
                            USER_UART_INFO("%s", at_cmds[cmd_index].cmd_args_numb_fail);
                        }
                    }
                    else if (at_cmds[cmd_index].process_cmd != NULL) {
                        at_cmds[cmd_index].process_cmd(args_numb, args_vector);
                    }
                    break;
                }
            }
        }
        cmd_index++;
    }
}
