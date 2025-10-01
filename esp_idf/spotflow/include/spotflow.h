#ifndef SPOTFLOW_H
#define SPOTFLOW_H


#include "spotflow_mqtt.h"
#include "spotflow_log.h"

#if CONFIG_SPOTFLOW_DEBUG
    #define SPOTFLOW_PRINTF(fmt, ...)   printf("[SPOTFLOW] " fmt, ##__VA_ARGS__)
#else
    #define SPOTFLOW_PRINTF(fmt, ...)   do {} while(0)
#endif
void spotflow_init(void);

#endif