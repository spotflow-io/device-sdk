#include "zephyr/kernel.h"
#include "logging/spotflow_log_backend.h"
#include "net/spotflow_mqtt.h"
#include "zephyr/logging/log.h"

LOG_MODULE_DECLARE(spotflow_logging, CONFIG_SPOTFLOW_LOGS_PROCESSING_LOG_LEVEL);

int poll_and_process_enqueued_logs(void)
{
	struct spotflow_mqtt_logs_msg* msg_ptr;

	/* Peek without removing - returns non-zero if queue empty */
	if (k_msgq_peek(&g_spotflow_logs_msgq, &msg_ptr) != 0) {
		return 0; /* Queue empty */
	}

	int rc = spotflow_mqtt_publish_ingest_cbor_msg(msg_ptr->payload, msg_ptr->len);
	if (rc == -EAGAIN) {
		/* Temporary, retry later without aborting connection */
		return rc;
	}
	if (rc < 0) {
		LOG_DBG("Failed to publish log message: %d, aborting connection", rc);
		spotflow_mqtt_abort_mqtt();
		return rc;
	}

	/* Only remove after successful publish */
	k_msgq_get(&g_spotflow_logs_msgq, &msg_ptr, K_NO_WAIT);

	k_free(msg_ptr->payload);
	k_free(msg_ptr);

	return 1;
}
