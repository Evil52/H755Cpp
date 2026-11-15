#include "h755/ethernet/lwip_clock.h"
#include "lwip/arch.h"

static uint32_t current_now;

void h755_lwip_set_now(uint32_t now)
{
    current_now = now;
}

uint32_t sys_now(void)
{
    return current_now;
}
