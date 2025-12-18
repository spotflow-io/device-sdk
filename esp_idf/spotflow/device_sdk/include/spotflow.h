#ifndef SPOTFLOW_H
#define SPOTFLOW_H

#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

extern vprintf_like_t original_vprintf;
void spotflow_init(void);

#ifdef __cplusplus
}
#endif

#endif