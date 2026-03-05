#ifndef SPOTFLOW_PORT_H
#define SPOTFLOW_PORT_H

#include "cmsis_os.h"
#include "stm32f1xx_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t SpotflowCriticalState;

uint32_t spotflow_port_get_time_ms(void);
void spotflow_port_delay(uint32_t ms);
SpotflowCriticalState spotflow_port_enter_critical(void);
void spotflow_port_exit_critical(SpotflowCriticalState state);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_PORT_H */
