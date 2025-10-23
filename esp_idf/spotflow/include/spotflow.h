#ifndef SPOTFLOW_H
#define SPOTFLOW_H

#include "esp_log.h"

extern vprintf_like_t original_vprintf;
void spotflow_init(void);

#endif