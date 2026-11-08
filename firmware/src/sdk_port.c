#include <stdbool.h>
#include <stdint.h>

/* Optional SDK hooks need callable defaults when linked with GNU tools. */
void reset_indication(uint16_t reset_stat)
{
    (void)reset_stat;
}

void sb_read_otp_header(void)
{
}

void sb_set_debugger_mode(bool mode)
{
    (void)mode;
}
