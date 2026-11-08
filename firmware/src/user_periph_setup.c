#include "user_periph_setup.h"
#include "arch.h"
#include "gpio.h"
#include "system_library.h"

/* periph_init() runs again after every extended sleep; keep the LED state. */
static uint8_t led_mask __SECTION_ZERO("retention_mem_area0");

void GPIO_reservations(void) {
    RESERVE_GPIO(LED_1, LED_PORT, LED_1_PIN, PID_GPIO);
    RESERVE_GPIO(LED_2, LED_PORT, LED_2_PIN, PID_GPIO);
    RESERVE_GPIO(LED_3, LED_PORT, LED_3_PIN, PID_GPIO);
    RESERVE_GPIO(ACC_SCL, ACC_PORT, ACC_SCL_PIN, PID_I2C_SCL);
    RESERVE_GPIO(ACC_SDA, ACC_PORT, ACC_SDA_PIN, PID_I2C_SDA);
    RESERVE_GPIO(ACC_INT1, ACC_PORT, ACC_INT1_PIN, PID_GPIO);
    RESERVE_GPIO(SPI_EN, SPI_EN_PORT, SPI_EN_PIN, PID_GPIO);
}

static void led_write(GPIO_PIN pin, bool on)
{
    if (on) {
        GPIO_SetActive(LED_PORT, pin);
    } else {
        GPIO_SetInactive(LED_PORT, pin);
    }
}

void leds_set(uint8_t mask)
{
    led_mask = mask;
    led_write(LED_1_PIN, mask & LED_1);
    led_write(LED_2_PIN, mask & LED_2);
    led_write(LED_3_PIN, mask & LED_3);
}

uint8_t leds_get(void)
{
    return led_mask;
}

void periph_init(void) {
    /* Buck mode: system_init() already set up the DC-DC; do not reconfigure it here. */
    patch_func();

    GPIO_ConfigurePin(LED_PORT, LED_1_PIN, OUTPUT, PID_GPIO, led_mask & LED_1);
    GPIO_ConfigurePin(LED_PORT, LED_2_PIN, OUTPUT, PID_GPIO, led_mask & LED_2);
    GPIO_ConfigurePin(LED_PORT, LED_3_PIN, OUTPUT, PID_GPIO, led_mask & LED_3);

    /* External 10k pull-ups on SCL/SDA. */
    GPIO_ConfigurePin(ACC_PORT, ACC_SCL_PIN, INPUT, PID_I2C_SCL, false);
    GPIO_ConfigurePin(ACC_PORT, ACC_SDA_PIN, INPUT, PID_I2C_SDA, false);
    GPIO_ConfigurePin(ACC_PORT, ACC_INT1_PIN, INPUT_PULLDOWN, PID_GPIO, false);

    /* Keep the Flash deselected after boot. */
    GPIO_ConfigurePin(SPI_EN_PORT, SPI_EN_PIN, OUTPUT, PID_GPIO, true);

    GPIO_set_pad_latch_en(true);
}
