#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "app.h"
#include "app_default_handlers.h"
#include "app_easy_gap.h"
#include "app_easy_msg_utils.h"
#include "app_easy_timer.h"
#include "arch.h"
#include "arch_api.h"
#include "battery.h"
#include "findmy_key.h"
#include "gapm_task.h"
#include "gpio.h"
#include "lis2dw12.h"
#include "user_app.h"
#include "user_periph_setup.h"
#include "wkupct_quadec.h"

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

#define SHAKE_REQUIRED_CHECKS   (SHAKE_REQUIRED_MS / SHAKE_CHECK_PERIOD_MS)
#define SHAKE_MAX_QUIET_CHECKS  (SHAKE_MAX_QUIET_MS / SHAKE_CHECK_PERIOD_MS)

/*
 * battery_get_lvl() on DA14531 is linear (2.0 V = 0 %, 3.0 V = 100 %), but a CR2032
 * stays near 2.9 V for most of its capacity. Thresholds follow the SDK's DA14585
 * CR2032 curve (3.0 V = 100 %, 2.79 V = 28 %, 2.39 V = 4 %).
 */
#define BATT_MV_THREE_LEDS      (2900)  /* >= 66 %: green + yellow + red */
#define BATT_MV_TWO_LEDS        (2800)  /* >= 33 %: yellow + red; else red only */

#define FINDMY_KEY_LEN          (28)

static const uint8_t findmy_key[FINDMY_KEY_LEN] = FINDMY_ADV_KEY;

static uint8_t blink_steps          __SECTION_ZERO("retention_mem_area0");
static bool acc_wait_release        __SECTION_ZERO("retention_mem_area0");
static bool shake_active            __SECTION_ZERO("retention_mem_area0");
static bool shake_seen              __SECTION_ZERO("retention_mem_area0");
static uint16_t shake_checks        __SECTION_ZERO("retention_mem_area0");
static uint16_t shake_quiet_checks  __SECTION_ZERO("retention_mem_area0");
static timer_hnd led_timer          __SECTION_ZERO("retention_mem_area0");

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

/* LED_1 green, LED_2 yellow, LED_3 red: a bar that grows from red to green. */
static uint8_t battery_led_mask(void)
{
    uint16_t voltage = battery_get_voltage(BATT_CR2032);

    if (voltage >= BATT_MV_THREE_LEDS) {
        return LED_ALL;
    }
    if (voltage >= BATT_MV_TWO_LEDS) {
        return LED_2 | LED_3;
    }
    return LED_3;
}

static void shake_detected(void)
{
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
 * Runs after GAPM_RESET and GAPM_SET_DEV_CONFIG. The reset calls rwip_reset(),
 * which flushes every kernel timer and message, so nothing that depends on
 * app_easy_timer() or app_easy_wakeup() may start in app_on_init.
 */
void user_app_on_set_dev_config_complete(void)
{
    bool acc_ok;

    /* The ROM may leave SWD off after OTP boot; SWCLK=P0_2, SWDIO=P0_10 on this board. */
    SetBits16(SYS_CTRL_REG, DEBUGGER_ENABLE, SWD_DATA_AT_P0_10);

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

    default_app_on_set_dev_config_complete();
}

/*
 * OpenHaystack payload: Apple manufacturer data, Offline Finding type 0x12,
 * key bytes 6..27, then the two top bits of key byte 0. Key bytes 0..5 are
 * carried in the random static address.
 */
void user_app_adv_start(void)
{
    struct gapm_start_advertise_cmd *cmd = app_easy_gap_non_connectable_advertise_get_active();
    uint8_t *data = cmd->info.host.adv_data;

    data[0] = 0x1E;
    data[1] = 0xFF;
    data[2] = 0x4C;
    data[3] = 0x00;
    data[4] = 0x12;
    data[5] = 0x19;
    data[6] = 0x00;
    memcpy(&data[7], &findmy_key[6], 22);
    data[29] = findmy_key[0] >> 6;
    data[30] = 0x00;
    cmd->info.host.adv_data_len = 31;

    app_easy_gap_non_connectable_advertise_start();
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
    (void)msgid;
    (void)param;
    (void)dest_id;
    (void)src_id;
}
