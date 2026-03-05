#include "spotflow_port.h"

uint32_t spotflow_port_get_time_ms(void)
{
    return HAL_GetTick();
}

void spotflow_port_delay(uint32_t ms)
{
    osDelay(ms);
}

SpotflowCriticalState spotflow_port_enter_critical(void)
{
    SpotflowCriticalState state = __get_PRIMASK();
    __disable_irq();
    return state;
}

void spotflow_port_exit_critical(SpotflowCriticalState state)
{
    __set_PRIMASK(state);
}
