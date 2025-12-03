#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "metrics/spotflow_metrics_backend.h"
#include "net/spotflow_mqtt.h"

LOG_MODULE_DECLARE(spotflow_net, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

static struct k_poll_event metrics_poll_event;

void spotflow_init_metrics_polling(void)
{
	k_poll_event_init(&metrics_poll_event, K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
			  K_POLL_MODE_NOTIFY_ONLY, &g_spotflow_metrics_msgq);
}

int spotflow_poll_and_process_enqueued_metrics(void)
{
	struct spotflow_mqtt_metrics_msg* msg_ptr;

	k_poll(&metrics_poll_event, 1, K_NO_WAIT);
	if (metrics_poll_event.state == K_POLL_STATE_MSGQ_DATA_AVAILABLE) {
		if (k_msgq_get(&g_spotflow_metrics_msgq, &msg_ptr, K_NO_WAIT) == 0) {
			int rc = spotflow_mqtt_publish_ingest_cbor_msg(msg_ptr->payload,
								       msg_ptr->len);
			if (rc < 0) {
				LOG_DBG("Failed to publish metrics rc: %d -> aborting mqtt", rc);
				spotflow_mqtt_abort_mqtt();
				k_free(msg_ptr->payload);
				k_free(msg_ptr);
				return rc;
			}
			k_free(msg_ptr->payload);
			k_free(msg_ptr);
			metrics_poll_event.state = K_POLL_STATE_NOT_READY;
			return 1; /* metrics sent */
		}
		metrics_poll_event.state = K_POLL_STATE_NOT_READY;
	}
	return 0;
}
