#ifndef USER_CONFIG_H
#define USER_CONFIG_H

/* Based on the SDK ble_app_noncon user_config.h, made connectable for the DULT sound service. */
#include "app_user_config.h"
#include "arch_api.h"
#include "app_default_handlers.h"
#include "app_adv_data.h"
#include "co_bt.h"

/* Random static address, derived from the Find My key (user_app.c). */
#define USER_CFG_ADDRESS_MODE       APP_CFG_ADDR_STATIC

#define USER_CFG_CNTL_PRIV_MODE     APP_CFG_CNTL_PRIV_MODE_NETWORK

/* user_app.c enables extended sleep after the SWD window. */
static const sleep_state_t app_default_sleep_mode = ARCH_SLEEP_OFF;

/* Find My advertising interval. */
#define USER_ADV_INTERVAL_MS        (2000)

static const struct advertise_configuration user_adv_conf = {
    .addr_src = APP_CFG_ADDR_SRC(USER_CFG_ADDRESS_MODE),
    .intv_min = MS_TO_BLESLOTS(USER_ADV_INTERVAL_MS),
    .intv_max = MS_TO_BLESLOTS(USER_ADV_INTERVAL_MS),
    .channel_map = ADV_ALL_CHNLS_EN,
    /*
     * Broadcaster mode is non-connectable only. In connectable modes GAPM prepends a 3-byte
     * Flags AD, which leaves 28 bytes; user_app.c then writes the 31-byte Find My payload
     * straight to the link layer.
     */
    .mode = GAP_NON_DISCOVERABLE,
    /* CON_WLST would only accept connections from whitelisted centrals. */
    .adv_filt_policy = ADV_ALLOW_SCAN_ANY_CON_ANY,
    .peer_addr = {0x1, 0x2, 0x3, 0x4, 0x5, 0x6},
    .peer_addr_type = 0,
};

/* Payload is set at run time in user_app_adv_start(). */
#define USER_ADVERTISE_DATA         ""
#define USER_ADVERTISE_DATA_LEN     (sizeof(USER_ADVERTISE_DATA)-1)
#define USER_ADVERTISE_SCAN_RESPONSE_DATA ""
#define USER_ADVERTISE_SCAN_RESPONSE_DATA_LEN (sizeof(USER_ADVERTISE_SCAN_RESPONSE_DATA)-1)

/* Empty: a name would not fit next to the 31-byte Find My payload. */
#define USER_DEVICE_NAME            ""
#define USER_DEVICE_NAME_LEN        (sizeof(USER_DEVICE_NAME)-1)

static const struct gapm_configuration user_gapm_conf = {
    .role = GAP_ROLE_PERIPHERAL,
    .max_mtu = 23,
    .addr_type = APP_CFG_ADDR_TYPE(USER_CFG_ADDRESS_MODE),
    .renew_dur = 15000,
    /* Null address: app_on_generate_static_random_addr supplies it. */
    .addr = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    .irk = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f},
    .att_cfg = 0,
    .gap_start_hdl = 0,
    .gatt_start_hdl = 0,
    .max_mps = 0,
    .max_txoctets = 251,
    .max_txtime = 2120,
};

static const struct connection_param_configuration user_connection_param_conf = {
    .intv_min = MS_TO_DOUBLESLOTS(10),
    .intv_max = MS_TO_DOUBLESLOTS(20),
    .latency = 0,
    .time_out = MS_TO_TIMERUNITS(1250),
    .ce_len_min = MS_TO_DOUBLESLOTS(0),
    .ce_len_max = MS_TO_DOUBLESLOTS(0),
};

static const struct default_handlers_configuration user_default_hnd_conf = {
    .adv_scenario = DEF_ADV_FOREVER,
    .advertise_period = MS_TO_TIMERUNITS(180000),
    .security_request_scenario = DEF_SEC_REQ_NEVER
};

static const struct central_configuration user_central_conf = {
    .code = GAPM_CONNECTION_DIRECT,
    .addr_src = APP_CFG_ADDR_SRC(USER_CFG_ADDRESS_MODE),
    .scan_interval = 0x180,
    .scan_window = 0x160,
    .con_intv_min = 100,
    .con_intv_max = 100,
    .con_latency = 0,
    .superv_to = 0x1F4,
    .ce_len_min = 0,
    .ce_len_max = 0x5,
    .peer_addr_0 = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0},
    .peer_addr_0_type = 0,
    .peer_addr_1 = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0},
    .peer_addr_1_type = 0,
    .peer_addr_2 = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0},
    .peer_addr_2_type = 0,
    .peer_addr_3 = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0},
    .peer_addr_3_type = 0,
    .peer_addr_4 = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0},
    .peer_addr_4_type = 0,
    .peer_addr_5 = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0},
    .peer_addr_5_type = 0,
    .peer_addr_6 = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0},
    .peer_addr_6_type = 0,
    .peer_addr_7 = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0},
    .peer_addr_7_type = 0,
};

static const struct security_configuration user_security_conf = {
    .iocap          = GAP_IO_CAP_NO_INPUT_NO_OUTPUT,
    .oob            = GAP_OOB_AUTH_DATA_NOT_PRESENT,
    .auth           = GAP_AUTH_REQ_NO_MITM_BOND,
    .sec_req        = GAP_NO_SEC,
    .key_size       = KEY_LEN,
    .ikey_dist      = GAP_KDIST_SIGNKEY,
    .rkey_dist      = GAP_KDIST_ENCKEY,
};

#endif
