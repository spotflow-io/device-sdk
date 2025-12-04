#ifndef SPOTFLOW_METRICS_BACKEND_H
#define SPOTFLOW_METRICS_BACKEND_H

#include <stddef.h>
#include <stdint.h>
#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

struct spotflow_mqtt_metrics_msg {
	uint8_t* payload;
	size_t len;
};

extern struct k_msgq g_spotflow_metrics_msgq;

int spotflow_metrics_enqueue(const struct spotflow_mqtt_metrics_msg* msg);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_METRICS_BACKEND_H */
