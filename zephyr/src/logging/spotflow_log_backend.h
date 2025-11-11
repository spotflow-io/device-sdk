#ifndef SPOTFLOW_LOG_BACKEND_H
#define SPOTFLOW_LOG_BACKEND_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

struct spotflow_mqtt_logs_msg {
	uint8_t* payload;
	size_t len;
};

extern struct k_msgq g_spotflow_logs_msgq;

void spotflow_log_backend_try_set_runtime_filter(uint32_t level);

#ifdef __cplusplus
}
#endif

#endif //SPOTFLOW_LOG_BACKEND_H
