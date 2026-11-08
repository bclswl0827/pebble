#ifndef LIS2DW12_H
#define LIS2DW12_H

#include <stdbool.h>
#include <stdint.h>

/* WHO_AM_I probe result, readable over SWD. address 0: no ACK at 0x18 or 0x19. */
struct lis2dw12_probe {
    uint8_t address;
    uint8_t who_am_i;
};

extern struct lis2dw12_probe lis2dw12_probe_result;

/*
 * Configures the wake-up (motion) interrupt on INT1, pulsed (not latched):
 * INT1 goes high while the slope of any axis exceeds the threshold.
 * Returns false if WHO_AM_I does not match or the configuration cannot be read back.
 */
bool lis2dw12_init_wakeup(void);

#endif
