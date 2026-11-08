#ifndef DA1458X_CONFIG_BASIC_H
#define DA1458X_CONFIG_BASIC_H

/* DA14531 values from the SDK ble_app_noncon da14531_config_basic.h. */
#include "da1458x_stack_config.h"
#include "user_profiles_config.h"

#define CFG_APP
#undef CFG_APP_SECURITY
#define CFG_WDOG
#undef CFG_WDG_TRIGGER_HW_RESET_IN_PRODUCTION_MODE
#define CFG_MAX_CONNECTIONS     (1)
/*
 * Production build: an assert, HardFault or watchdog NMI resets the chip. With
 * CFG_DEVELOPMENT_DEBUG they halt with the watchdog frozen, i.e. the tag goes
 * silent until a power cycle. Define it only while debugging over SWD.
 */
#undef CFG_DEVELOPMENT_DEBUG
#undef CFG_PRINTF
#undef CFG_UART1_SDK
#undef CFG_SPI_FLASH_ENABLE
#undef CFG_I2C_EEPROM_ENABLE
#undef CFG_UART_DMA_SUPPORT
#undef CFG_SPI_DMA_SUPPORT
#undef CFG_I2C_DMA_SUPPORT
#undef CFG_ADC_DMA_SUPPORT
#undef CFG_POWER_MODE_BYPASS

#endif
