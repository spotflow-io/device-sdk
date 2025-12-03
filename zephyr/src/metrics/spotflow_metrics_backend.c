#include "metrics/spotflow_metrics_backend.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spotflow_metrics_backend, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

K_MSGQ_DEFINE(g_spotflow_metrics_msgq, sizeof(struct spotflow_mqtt_metrics_msg*),
	      CONFIG_SPOTFLOW_METRICS_QUEUE_SIZE, 1);

static int drop_metric_msg_from_queue(void)
{
	char* old;
	int rc = k_msgq_get(&g_spotflow_metrics_msgq, &old, K_NO_WAIT);
	if (rc == 0) {
		struct spotflow_mqtt_metrics_msg* old_msg = (struct spotflow_mqtt_metrics_msg*)old;
		k_free(old_msg->payload);
		k_free(old_msg);
	}
	return rc;
}

int spotflow_metrics_enqueue(const struct spotflow_mqtt_metrics_msg* msg)
{
	int rc = k_msgq_put(&g_spotflow_metrics_msgq, &msg, K_NO_WAIT);
	if (rc == -ENOMSG) {
		rc = drop_metric_msg_from_queue();
		if (rc == 0) {
			rc = k_msgq_put(&g_spotflow_metrics_msgq, &msg, K_NO_WAIT);
		} else {
			LOG_DBG("Failed to free space in metrics queue: %d", rc);
		}
	}
	return rc;
}
