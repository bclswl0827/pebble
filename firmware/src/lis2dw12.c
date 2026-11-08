#include <stdint.h>
#include "arch.h"
#include "i2c.h"
#include "lis2dw12.h"

/* 7-bit address: SA0 = GND (board default) or SA0 = VDD. */
#define LIS2DW12_ADDRESS_SA0_LOW    (0x18)
#define LIS2DW12_ADDRESS_SA0_HIGH   (0x19)

#define REG_WHO_AM_I            (0x0F)
#define REG_CTRL1               (0x20)
#define REG_CTRL2               (0x21)
#define REG_CTRL3               (0x22)
#define REG_CTRL4_INT1          (0x23)
#define REG_CTRL6               (0x25)
#define REG_WAKE_UP_THS         (0x34)
#define REG_WAKE_UP_DUR         (0x35)
#define REG_CTRL7               (0x3F)

#define WHO_AM_I_VALUE          (0x44)
#define CTRL2_SOFT_RESET        (0x40)
#define CTRL2_BDU               (0x08)
#define CTRL2_IF_ADD_INC        (0x04)
#define CTRL4_INT1_WU           (0x20)
#define CTRL7_INTERRUPTS_ENABLE (0x20)

/* ODR 12.5 Hz, low-power mode 1. */
#define CTRL1_VALUE             (0x20)
/* Full scale +-4 g. */
#define CTRL6_VALUE             (0x10)
/*
 * Wake-up threshold, 1 LSB = FS / 64 = 62.5 mg. The wake-up function compares
 * the slope (difference of consecutive samples), so the effective sensitivity
 * also depends on the ODR: a 3-5 Hz shake gives a larger slope at 12.5 Hz than
 * at 25 Hz. Raise it if gentle shaking triggers, lower it if a hard shake is
 * not detected.
 */
#define WAKE_UP_THS_VALUE       (28)

struct lis2dw12_probe lis2dw12_probe_result;

static const i2c_cfg_t i2c_cfg = {
    .clock_cfg.ss_hcnt = I2C_SS_SCL_HCNT_REG_RESET,
    .clock_cfg.ss_lcnt = I2C_SS_SCL_LCNT_REG_RESET,
    .clock_cfg.fs_hcnt = I2C_FS_SCL_HCNT_REG_RESET,
    .clock_cfg.fs_lcnt = I2C_FS_SCL_LCNT_REG_RESET,
    .restart_en = I2C_RESTART_ENABLE,
    .speed = I2C_SPEED_STANDARD,
    .mode = I2C_MODE_MASTER,
    .addr_mode = I2C_ADDRESSING_7B,
    .address = LIS2DW12_ADDRESS_SA0_LOW,
    .tx_fifo_level = 1,
    .rx_fifo_level = 1,
};

static void write_reg(uint8_t reg, uint8_t value)
{
    const uint8_t data[2] = {reg, value};
    i2c_abort_t abort_code;

    i2c_master_transmit_buffer_sync(data, sizeof(data), &abort_code, I2C_F_WAIT_FOR_STOP);
}

static bool read_reg(uint8_t reg, uint8_t *value)
{
    i2c_abort_t abort_code;

    i2c_master_transmit_buffer_sync(&reg, 1, &abort_code, I2C_F_NONE);
    if (abort_code != I2C_ABORT_NONE) {
        return false;
    }
    /* A missing device shows up here as an address NACK abort. */
    i2c_master_receive_buffer_sync(value, 1, &abort_code, I2C_F_ADD_STOP);
    while (i2c_is_master_busy());
    return abort_code == I2C_ABORT_NONE;
}

/* The target address can only be changed while the controller is disabled. */
static void set_address(uint8_t address)
{
    i2c_set_controller_status(I2C_CONTROLLER_DISABLE);
    while (i2c_get_controller_status() != I2C_CONTROLLER_DISABLE);
    i2c_set_target_address(address);
    i2c_set_controller_status(I2C_CONTROLLER_ENABLE);
    while (i2c_get_controller_status() != I2C_CONTROLLER_ENABLE);
}

/* Leaves the I2C target set to the address that answered. */
static bool probe(void)
{
    static const uint8_t addresses[] = {LIS2DW12_ADDRESS_SA0_LOW, LIS2DW12_ADDRESS_SA0_HIGH};
    uint8_t value;

    for (unsigned int i = 0; i < sizeof(addresses); i++) {
        set_address(addresses[i]);
        if (read_reg(REG_WHO_AM_I, &value)) {
            lis2dw12_probe_result.address = addresses[i];
            lis2dw12_probe_result.who_am_i = value;
            return value == WHO_AM_I_VALUE;
        }
    }
    return false;
}

static bool configure(void)
{
    uint8_t value;

    write_reg(REG_CTRL2, CTRL2_SOFT_RESET);
    for (int i = 0; ; i++) {
        if (i == 10) {
            return false;
        }
        arch_asm_delay_us(1000);
        if (read_reg(REG_CTRL2, &value) && !(value & CTRL2_SOFT_RESET)) {
            break;
        }
    }

    write_reg(REG_CTRL2, CTRL2_BDU | CTRL2_IF_ADD_INC);
    write_reg(REG_CTRL6, CTRL6_VALUE);
    write_reg(REG_WAKE_UP_THS, WAKE_UP_THS_VALUE);
    /* One sample above the threshold is an event; no sleep/stationary function. */
    write_reg(REG_WAKE_UP_DUR, 0x00);
    /* Push-pull, active high, pulsed (LIR = 0): no register read needed to clear INT1. */
    write_reg(REG_CTRL3, 0x00);
    write_reg(REG_CTRL4_INT1, CTRL4_INT1_WU);
    write_reg(REG_CTRL7, CTRL7_INTERRUPTS_ENABLE);
    write_reg(REG_CTRL1, CTRL1_VALUE);

    return read_reg(REG_CTRL1, &value) && value == CTRL1_VALUE &&
           read_reg(REG_CTRL4_INT1, &value) && value == CTRL4_INT1_WU;
}

bool lis2dw12_init_wakeup(void)
{
    i2c_init(&i2c_cfg);
    bool ok = probe() && configure();
    /* The sensor runs on its own; the I2C block is only needed for setup. */
    i2c_release();
    return ok;
}
