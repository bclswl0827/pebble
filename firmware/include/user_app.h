#ifndef USER_APP_H
#define USER_APP_H

#include "co_bt.h"
#include "ke_msg.h"

void user_app_on_set_dev_config_complete(void);
void user_app_adv_start(void);
void user_app_generate_static_addr(struct bd_addr *addr);
void user_catch_rest_hndl(ke_msg_id_t const msgid,
                          void const *param,
                          ke_task_id_t const dest_id,
                          ke_task_id_t const src_id);

#endif
