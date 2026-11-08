#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "app.h"
#include "app_default_handlers.h"
#include "app_easy_gap.h"
#include "app_easy_msg_utils.h"
#include "app_easy_timer.h"
#include "app_task.h"
#include "arch.h"
#include "arch_api.h"
#include "battery.h"
#include "custs1_task.h"
#include "findmy_key.h"
#include "gapm_task.h"
#include "gpio.h"
#include "hci.h"
#include "lis2dw12.h"
#include "llm.h"
#include "llm_task.h"
#include "rf_531.h"
#include "user_app.h"
#include "user_custs1_def.h"
#include "user_periph_setup.h"
#include "wkupct_quadec.h"

/* ROM function; only rf_585.h declares it. */
extern void rf_reinit(void);

/* Times are in ms. app_easy_timer() counts 10 ms ticks; round up so no delay becomes 0. */
#define MS_TO_TICKS(ms)         (((ms) + 9) / 10)

#define BOOT_BLINK_COUNT        (10)
#define BOOT_BLINK_HALF_PERIOD_MS (100) /* 100 ms on, 100 ms off */
/* Extended sleep turns the SWD interface off; keep it awake for uploads after a reset. */
#define SWD_WINDOW_MS           (10000)
#define SWD_RECHECK_MS          (1000)
#define SHAKE_CHECK_PERIOD_MS   (100)
#define SHAKE_REQUIRED_MS       (5000)  /* continuous shaking needed */
#define SHAKE_MAX_QUIET_MS      (500)   /* a pause this long restarts the count */
#define LED_ON_TIME_MS          (5000)  /* 0 keeps the LEDs on */
/* OpenTagViewer disconnects right after the write; a new Sound_Start restarts the time. */
#define RING_DURATION_MS        (10000)
/* Lets GAPM finish its own advertising setup before the payload is replaced. */
#define ADV_DATA_DELAY_MS       (50)
/* Delay before restarting advertising that ended without a connection. */
#define ADV_RETRY_MS            (1000)
/* With one connection allowed, a central that stays connected stops advertising. */
#define CONN_TIMEOUT_MS         (30000)
#define MONITOR_PERIOD_MS       (60000)
/* Reset after this many monitor checks in a row without advertising (about 3 min). */
#define ADV_STALL_CHECKS        (3)
#define BATT_CHECK_PERIOD_MS    (3600000)

#define SHAKE_REQUIRED_CHECKS   (SHAKE_REQUIRED_MS / SHAKE_CHECK_PERIOD_MS)
#define SHAKE_MAX_QUIET_CHECKS  (SHAKE_MAX_QUIET_MS / SHAKE_CHECK_PERIOD_MS)
#define RING_STEPS              (RING_DURATION_MS / BOOT_BLINK_HALF_PERIOD_MS)
#define BATT_CHECK_TICKS        (BATT_CHECK_PERIOD_MS / MONITOR_PERIOD_MS)

/* DULT control point opcodes, little-endian on air. */
#define DULT_OP_SOUND_START     (0x0300)
#define DULT_OP_SOUND_STOP      (0x0301)

/*
 * battery_get_lvl() on DA14531 is linear (2.0 V = 0 %, 3.0 V = 100 %), but a CR2032
 * stays near 2.9 V for most of its capacity. Thresholds follow the SDK's DA14585
 * CR2032 curve (3.0 V = 100 %, 2.79 V = 28 %, 2.39 V = 4 %).
 */
#define BATT_MV_THREE_LEDS      (2900)  /* >= 66 %: green + yellow + red */
#define BATT_MV_TWO_LEDS        (2800)  /* >= 33 %: yellow + red; else red only */
#define BATT_MV_LOW             (2600)  /* below: critically low */

/* Battery level in status byte bits 6..7, as OpenTagViewer decodes it. */
#define BATT_STATUS_FULL        (0)     /* >= BATT_MV_THREE_LEDS */
#define BATT_STATUS_MEDIUM      (1)     /* >= BATT_MV_TWO_LEDS */
#define BATT_STATUS_LOW         (2)     /* >= BATT_MV_LOW */
#define BATT_STATUS_CRITICAL    (3)

#define FINDMY_KEY_LEN          (28)
#define FINDMY_ADV_LEN          (31)

static const uint8_t findmy_key[FINDMY_KEY_LEN] = FINDMY_ADV_KEY;

static uint8_t blink_steps          __SECTION_ZERO("retention_mem_area0");
static bool acc_wait_release        __SECTION_ZERO("retention_mem_area0");
static bool shake_active            __SECTION_ZERO("retention_mem_area0");
static bool shake_seen              __SECTION_ZERO("retention_mem_area0");
static uint16_t shake_checks        __SECTION_ZERO("retention_mem_area0");
static uint16_t shake_quiet_checks  __SECTION_ZERO("retention_mem_area0");
static timer_hnd led_timer          __SECTION_ZERO("retention_mem_area0");
static uint16_t ring_steps          __SECTION_ZERO("retention_mem_area0");
static timer_hnd ring_timer         __SECTION_ZERO("retention_mem_area0");
static timer_hnd adv_timer          __SECTION_ZERO("retention_mem_area0");
static timer_hnd conn_timer         __SECTION_ZERO("retention_mem_area0");
static uint8_t conn_idx             __SECTION_ZERO("retention_mem_area0");
static uint8_t batt_status          __SECTION_ZERO("retention_mem_area0");
static uint8_t adv_stall_checks     __SECTION_ZERO("retention_mem_area0");
static uint8_t batt_check_ticks     __SECTION_ZERO("retention_mem_area0");

static void adv_data_refresh(void);

static void boot_blink_cb(void)
{
    blink_steps++;
    leds_set((blink_steps & 1) ? 0 : LED_ALL);
    if (blink_steps < BOOT_BLINK_COUNT * 2 - 1) {
        app_easy_timer(MS_TO_TICKS(BOOT_BLINK_HALF_PERIOD_MS), boot_blink_cb);
    }
}

static void swd_window_cb(void)
{
    /* arch_goto_sleep() busy-waits with interrupts off while a debugger is attached. */
    if (GetBits16(SYS_STAT_REG, DBG_IS_UP)) {
        app_easy_timer(MS_TO_TICKS(SWD_RECHECK_MS), swd_window_cb);
        return;
    }
    arch_set_extended_sleep(false);
}

static void led_off_cb(void)
{
    led_timer = EASY_TIMER_INVALID_TIMER;
    leds_set(0);
}

static void ring_cb(void)
{
    ring_steps++;
    if (ring_steps >= RING_STEPS) {
        ring_timer = EASY_TIMER_INVALID_TIMER;
        leds_set(0);
        return;
    }
    leds_set((ring_steps & 1) ? 0 : LED_ALL);
    ring_timer = app_easy_timer(MS_TO_TICKS(BOOT_BLINK_HALF_PERIOD_MS), ring_cb);
}

static void ring_stop(void)
{
    if (ring_timer == EASY_TIMER_INVALID_TIMER) {
        return;
    }
    app_easy_timer_cancel(ring_timer);
    ring_timer = EASY_TIMER_INVALID_TIMER;
    leds_set(0);
}

/* "Play sound": blink all LEDs like the boot blink for RING_DURATION_MS. */
static void ring_start(void)
{
    if (led_timer != EASY_TIMER_INVALID_TIMER) {
        app_easy_timer_cancel(led_timer);
        led_timer = EASY_TIMER_INVALID_TIMER;
    }
    ring_stop();
    ring_steps = 0;
    leds_set(LED_ALL);
    ring_timer = app_easy_timer(MS_TO_TICKS(BOOT_BLINK_HALF_PERIOD_MS), ring_cb);
}

/* Call with the LEDs off so their current does not lower the cell voltage. */
static void battery_measure(void)
{
    uint16_t voltage = battery_get_voltage(BATT_CR2032);
    uint8_t status;

    if (voltage >= BATT_MV_THREE_LEDS) {
        status = BATT_STATUS_FULL;
    } else if (voltage >= BATT_MV_TWO_LEDS) {
        status = BATT_STATUS_MEDIUM;
    } else if (voltage >= BATT_MV_LOW) {
        status = BATT_STATUS_LOW;
    } else {
        status = BATT_STATUS_CRITICAL;
    }

    if (status != batt_status) {
        batt_status = status;
        adv_data_refresh();
    }
}

/* LED_1 green, LED_2 yellow, LED_3 red: a bar that grows from red to green. */
static uint8_t battery_led_mask(void)
{
    battery_measure();

    if (batt_status == BATT_STATUS_FULL) {
        return LED_ALL;
    }
    if (batt_status == BATT_STATUS_MEDIUM) {
        return LED_2 | LED_3;
    }
    return LED_3;
}

static void shake_detected(void)
{
    /* Ringing owns the LEDs; led_off_cb() would cut it short. */
    if (ring_timer != EASY_TIMER_INVALID_TIMER) {
        return;
    }
    /* Measure with the LEDs off so their current does not lower the cell voltage. */
    leds_set(0);
    leds_set(battery_led_mask());
    if (LED_ON_TIME_MS > 0) {
        if (led_timer != EASY_TIMER_INVALID_TIMER) {
            app_easy_timer_cancel(led_timer);
        }
        led_timer = app_easy_timer(MS_TO_TICKS(LED_ON_TIME_MS), led_off_cb);
    }
}

static void shake_check_cb(void)
{
    if (shake_seen || GPIO_GetPinStatus(ACC_PORT, ACC_INT1_PIN)) {
        shake_seen = false;
        shake_quiet_checks = 0;
    } else {
        shake_quiet_checks++;
    }
    shake_checks++;

    if (shake_quiet_checks >= SHAKE_MAX_QUIET_CHECKS) {
        shake_active = false;
    } else if (shake_checks >= SHAKE_REQUIRED_CHECKS) {
        shake_active = false;
        shake_detected();
    } else {
        app_easy_timer(MS_TO_TICKS(SHAKE_CHECK_PERIOD_MS), shake_check_cb);
    }
}

static void shake_event(void)
{
    shake_seen = true;
    if (!shake_active) {
        shake_active = true;
        shake_checks = 0;
        shake_quiet_checks = 0;
        app_easy_timer(MS_TO_TICKS(SHAKE_CHECK_PERIOD_MS), shake_check_cb);
    }
}

static void acc_event_cb(void);

/*
 * Wait for INT1 to go high, or for the current pulse to end first if it is
 * still high, so that every motion pulse is counted once.
 */
static void acc_arm(void)
{
    /* app_easy_msg callbacks are one-shot: the SDK clears the entry before calling it. */
    app_easy_wakeup_set(acc_event_cb);
    acc_wait_release = GPIO_GetPinStatus(ACC_PORT, ACC_INT1_PIN);
    wkupct_enable_irq(WKUPCT_PIN_SELECT(ACC_PORT, ACC_INT1_PIN),
                      WKUPCT_PIN_POLARITY(ACC_PORT, ACC_INT1_PIN,
                                          acc_wait_release ? WKUPCT_PIN_POLARITY_LOW : WKUPCT_PIN_POLARITY_HIGH),
                      1,
                      0);
}

/* Runs in the kernel context, posted by acc_int_isr(). */
static void acc_event_cb(void)
{
    if (!acc_wait_release) {
        shake_event();
    }
    acc_arm();
}

/* WKUP_QUADEC interrupt; it may also wake the system from extended sleep. */
static void acc_int_isr(void)
{
    arch_ble_force_wakeup();
    app_easy_wakeup();
}

/*
 * Last-resort recovery: none of the known paths should leave the radio silent
 * for minutes, so a reset (seen as the boot blink) beats staying invisible.
 */
static void monitor_cb(void)
{
    if (ke_state_get(TASK_LLM) == LLM_ADVERTISING) {
        adv_stall_checks = 0;
    } else if (++adv_stall_checks >= ADV_STALL_CHECKS) {
        platform_reset(RESET_NO_ERROR);
    }

    /* Postponed while the LEDs or a connection load the cell. */
    if (batt_check_ticks < BATT_CHECK_TICKS) {
        batt_check_ticks++;
    }
    if (batt_check_ticks >= BATT_CHECK_TICKS && leds_get() == 0 &&
        ke_state_get(TASK_APP) != APP_CONNECTED) {
        batt_check_ticks = 0;
        battery_measure();
    }

    app_easy_timer(MS_TO_TICKS(MONITOR_PERIOD_MS), monitor_cb);
}

/*
 * Runs after GAPM_RESET and GAPM_SET_DEV_CONFIG. The reset calls rwip_reset(),
 * which flushes every kernel timer and message, so nothing that depends on
 * app_easy_timer() or app_easy_wakeup() may start in app_on_init.
 */
void user_app_on_set_dev_config_complete(void)
{
    bool acc_ok;

    /* The ROM may leave SWD off after OTP boot; SWCLK=P0_2, SWDIO=P0_10 on this board. */
    SetBits16(SYS_CTRL_REG, DEBUGGER_ENABLE, SWD_DATA_AT_P0_10);

    /* Before the LEDs turn on; advertising has not started, so this only sets batt_status. */
    battery_measure();

    /* LEDs on during the check; they stay on if it fails or the I2C bus hangs. */
    leds_set(LED_ALL);

    acc_ok = lis2dw12_init_wakeup();
    if (acc_ok) {
        wkupct_register_callback(acc_int_isr);
        acc_arm();

        blink_steps = 0;
        app_easy_timer(MS_TO_TICKS(BOOT_BLINK_HALF_PERIOD_MS), boot_blink_cb);
    }

    app_easy_timer(MS_TO_TICKS(SWD_WINDOW_MS), swd_window_cb);
    app_easy_timer(MS_TO_TICKS(MONITOR_PERIOD_MS), monitor_cb);

    default_app_on_set_dev_config_complete();
}

static void adv_timer_cancel(void)
{
    if (adv_timer != EASY_TIMER_INVALID_TIMER) {
        app_easy_timer_cancel(adv_timer);
        adv_timer = EASY_TIMER_INVALID_TIMER;
    }
}

/*
 * OpenHaystack payload: Apple manufacturer data, Offline Finding type 0x12,
 * status (battery level), key bytes 6..27, then the two top bits of key byte 0.
 * Key bytes 0..5 are carried in the random static address.
 *
 * GAPM prepends a Flags AD to connectable advertising data, which leaves no
 * room for these 31 bytes, so they go straight to the link layer once GAPM
 * has set up advertising. The LLM either copies the data at once, or keeps
 * the message until the current advertising event ends and frees it then;
 * only the first case leaves the message to be freed here.
 */
static void adv_data_set_cb(void)
{
    struct hci_le_set_adv_data_cmd *cmd;
    uint8_t *data;

    adv_timer = EASY_TIMER_INVALID_TIMER;

    /* Connected or stopped: the next user_app_adv_start() schedules this again. */
    if (ke_state_get(TASK_APP) != APP_CONNECTABLE) {
        return;
    }
    /* The LLM tracks one pending update; never stack a second one on it. */
    if (ke_state_get(TASK_LLM) != LLM_ADVERTISING ||
        llm_le_env.advertising_params->adv_data_req != NULL) {
        adv_timer = app_easy_timer(MS_TO_TICKS(ADV_DATA_DELAY_MS), adv_data_set_cb);
        return;
    }

    cmd = KE_MSG_ALLOC(HCI_COMMAND, TASK_LLM, HCI_LE_SET_ADV_DATA_CMD_OPCODE, hci_le_set_adv_data_cmd);
    data = cmd->data.data;

    data[0] = 0x1E;
    data[1] = 0xFF;
    data[2] = 0x4C;
    data[3] = 0x00;
    data[4] = 0x12;
    data[5] = 0x19;
    data[6] = batt_status << 6;
    memcpy(&data[7], &findmy_key[6], 22);
    data[29] = findmy_key[0] >> 6;
    data[30] = 0x00;
    cmd->adv_data_len = FINDMY_ADV_LEN;

    llm_set_adv_data(cmd);
    if (llm_le_env.advertising_params == NULL ||
        llm_le_env.advertising_params->adv_data_req != ke_param2msg(cmd)) {
        ke_msg_free(ke_param2msg(cmd));
    }
}

/* Pushes a new status byte while advertising; a pending adv_timer picks it up anyway. */
static void adv_data_refresh(void)
{
    if (ke_state_get(TASK_APP) == APP_CONNECTABLE && adv_timer == EASY_TIMER_INVALID_TIMER) {
        adv_data_set_cb();
    }
}

/*
 * Connectable undirected advertising (ADV_IND) so phones can reach the DULT
 * service. GAPM advertises only its Flags AD until adv_data_set_cb() runs.
 */
void user_app_adv_start(void)
{
    /* rf_pa_pwr_set() only stores the level; rf_reinit() loads the matching OTP trims into the radio. */
    rf_pa_pwr_set(RF_TX_PWR_LVL_PLUS_2d5);
    rf_reinit();

    app_easy_gap_undirected_advertise_start();
    adv_timer_cancel();
    adv_timer = app_easy_timer(MS_TO_TICKS(ADV_DATA_DELAY_MS), adv_data_set_cb);
}

static void adv_restart_cb(void)
{
    adv_timer = EASY_TIMER_INVALID_TIMER;
    if (ke_state_get(TASK_APP) != APP_CONNECTED) {
        user_app_adv_start();
    }
}

/*
 * GAPM ends advertising with GAP_ERR_NO_ERROR when a central connects; any
 * other status means it stopped on its own, and nothing else restarts it.
 */
void user_app_on_adv_undirect_complete(uint8_t status)
{
    if (status == GAP_ERR_NO_ERROR || ke_state_get(TASK_APP) == APP_CONNECTED) {
        return;
    }
    adv_timer_cancel();
    adv_timer = app_easy_timer(MS_TO_TICKS(ADV_RETRY_MS), adv_restart_cb);
}

static void conn_timeout_cb(void)
{
    conn_timer = EASY_TIMER_INVALID_TIMER;
    if (ke_state_get(TASK_APP) == APP_CONNECTED) {
        app_easy_gap_disconnect(conn_idx);
    }
}

void user_app_on_connection(uint8_t conidx, struct gapc_connection_req_ind const *param)
{
    adv_timer_cancel();
    default_app_on_connection(conidx, param);

    if (app_env[conidx].conidx != GAP_INVALID_CONIDX) {
        conn_idx = conidx;
        conn_timer = app_easy_timer(MS_TO_TICKS(CONN_TIMEOUT_MS), conn_timeout_cb);
    }
}

void user_app_on_disconnect(struct gapc_disconnect_ind const *param)
{
    if (conn_timer != EASY_TIMER_INVALID_TIMER) {
        app_easy_timer_cancel(conn_timer);
        conn_timer = EASY_TIMER_INVALID_TIMER;
    }
    default_app_on_disconnect(param);
}

/* bd_addr is little-endian: addr[5] is the most significant byte. */
void user_app_generate_static_addr(struct bd_addr *addr)
{
    for (int i = 0; i < BD_ADDR_LEN; i++) {
        addr->addr[BD_ADDR_LEN - 1 - i] = findmy_key[i];
    }
    addr->addr[BD_ADDR_LEN - 1] |= 0xC0;
}

void user_catch_rest_hndl(ke_msg_id_t const msgid,
                          void const *param,
                          ke_task_id_t const dest_id,
                          ke_task_id_t const src_id)
{
    (void)dest_id;
    (void)src_id;

    /* app_custs1_process_handler() has no handlers, so custs1 writes end up here. */
    if (msgid == CUSTS1_VAL_WRITE_IND) {
        struct custs1_val_write_ind const *ind = param;

        if (ind->handle == DULT_IDX_CTRL_POINT_VAL && ind->length >= 2) {
            uint16_t opcode = ind->value[0] | (ind->value[1] << 8);

            if (opcode == DULT_OP_SOUND_START) {
                ring_start();
            } else if (opcode == DULT_OP_SOUND_STOP) {
                ring_stop();
            }
        }
    }
}
