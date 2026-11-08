#ifndef USER_CALLBACK_CONFIG_H
#define USER_CALLBACK_CONFIG_H

#include "app_callback.h"
#include "app_default_handlers.h"
#include "app_entry_point.h"
#include "app_prf_types.h"
#include "user_app.h"

/* Callbacks not listed are NULL. */
static const struct app_callbacks user_app_callbacks = {
    .app_on_connection                  = user_app_on_connection,
    .app_on_disconnect                  = user_app_on_disconnect,
    .app_on_adv_undirect_complete       = user_app_on_adv_undirect_complete,
    .app_on_set_dev_config_complete     = user_app_on_set_dev_config_complete,
    .app_on_db_init_complete            = default_app_on_db_init_complete,
    .app_on_generate_static_random_addr = user_app_generate_static_addr,
};

#define app_process_catch_rest_cb       user_catch_rest_hndl

static const struct arch_main_loop_callbacks user_app_main_loop_callbacks = {
    .app_on_init            = default_app_on_init,
};

/* Called by default_app_on_db_init_complete() and, after a disconnect, by user_app_on_disconnect(). */
static const struct default_app_operations user_default_app_operations = {
    .default_operation_adv = user_app_adv_start,
};

static const struct prf_func_callbacks user_prf_funcs[] =
{
    {TASK_ID_INVALID,    NULL, NULL}   // DO NOT MOVE. Must always be last
};

#endif
