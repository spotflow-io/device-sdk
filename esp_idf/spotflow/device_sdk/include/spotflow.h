#ifndef SPOTFLOW_H
#define SPOTFLOW_H

#include "esp_log.h"

#ifdef __cplusplus
extern "C" {
#endif

extern vprintf_like_t original_vprintf;
void spotflow_init(void);

#if CONFIG_SPOTFLOW_MESSAGE_TERMINAL || CONFIG_SPOTFLOW_DEBUG_MESSAGE_TERMINAL

#define SPOTFLOW_LOG(fmt, ...) printf("[SPOTFLOW] " fmt "\n", ##__VA_ARGS__)

#else

#define SPOTFLOW_LOG(fmt, ...) \
	do {                   \
	} while (0)

#endif

#if CONFIG_SPOTFLOW_DEBUG_MESSAGE_TERMINAL

#define SPOTFLOW_DEBUG(fmt, ...) printf("[SPOTFLOW] " fmt "\n", ##__VA_ARGS__)

#else

#define SPOTFLOW_DEBUG(fmt, ...) \
	do {                     \
	} while (0)

#endif

#ifdef __cplusplus
}
#endif

#endif
