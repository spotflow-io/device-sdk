#include "zephyr/kernel.h"
#include "logging/spotflow_log_backend.h"
#include "net/spotflow_mqtt.h"
#include "zephyr/logging/log.h"

LOG_MODULE_DECLARE(spotflow_logging, CONFIG_SPOTFLOW_LOGS_PROCESSING_LOG_LEVEL);

static uint32_t messages_sent_counter = 0;

int poll_and_process_enqueued_logs(void)
{
	struct spotflow_mqtt_logs_msg *msg_ptr;

	/* Best-effort: remove message immediately */
	if (k_msgq_get(&g_spotflow_logs_msgq, &msg_ptr, K_NO_WAIT) != 0) {
		return 0;  /* Queue empty */
	}

	int rc = spotflow_mqtt_publish_ingest_cbor_msg(msg_ptr->payload, msg_ptr->len);

	/* Always free - logs are best-effort */
	k_free(msg_ptr->payload);
	k_free(msg_ptr);

	if (rc < 0) {
		LOG_DBG("Failed to publish log: %d", rc);
		return rc;
	}

	messages_sent_counter++;
	if (messages_sent_counter % 100 == 0) {
		LOG_INF("Sent %" PRIu32 " log messages", messages_sent_counter);
	}
	if (messages_sent_counter == UINT32_MAX) {
		messages_sent_counter = 0;
	}

	return 1;
}
