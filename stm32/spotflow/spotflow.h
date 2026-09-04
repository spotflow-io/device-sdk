#ifndef SPOTFLOW_H
#define SPOTFLOW_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "spotflow_config.h"
#include "logging/spotflow_log_backend.h"
#include "port/port.h"
#include "queue/spotflow_queue.h"
/**
 * @brief Initialize Spotflow core system
 */
void spotflow_init(void);

#ifdef __cplusplus
}
#endif

#endif // SPOTFLOW_H
