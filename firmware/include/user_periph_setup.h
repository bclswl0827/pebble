#ifndef USER_PERIPH_SETUP_H
#define USER_PERIPH_SETUP_H

#include <stdint.h>
#include "gpio.h"

#define LED_PORT GPIO_PORT_0
#define LED_1_PIN GPIO_PIN_9
#define LED_2_PIN GPIO_PIN_5
#define LED_3_PIN GPIO_PIN_11

#define LED_1 (1U << 0)
#define LED_2 (1U << 1)
#define LED_3 (1U << 2)
#define LED_ALL (LED_1 | LED_2 | LED_3)

/* LIS2DW12: I2C address 0x18 (SA0 = GND), INT2 not connected. */
#define ACC_PORT GPIO_PORT_0
#define ACC_SCL_PIN GPIO_PIN_7
#define ACC_SDA_PIN GPIO_PIN_6
#define ACC_INT1_PIN GPIO_PIN_8

/* W25X20CL boot Flash. */
#define SPI_EN_PORT GPIO_PORT_0
#define SPI_EN_PIN GPIO_PIN_1
#define SPI_CLK_PORT GPIO_PORT_0
#define SPI_CLK_PIN GPIO_PIN_4
#define SPI_DO_PORT GPIO_PORT_0
#define SPI_DO_PIN GPIO_PIN_0
#define SPI_DI_PORT GPIO_PORT_0
#define SPI_DI_PIN GPIO_PIN_3

void leds_set(uint8_t mask);
void periph_init(void);
void GPIO_reservations(void);

#endif
