#ifndef _AT_CMD_H_
#define _AT_CMD_H_ 

bool at_cmd_is_in_pass_mode(void);
void at_cmd_set_pass_mode(void);
void at_cmd_recv_hand(char *buff, uint16_t len);

#endif
