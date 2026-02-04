#include "zephyr/kernel.h"
#include "coredumps/spotflow_coredumps_backend.h"
#include "net/spotflow_mqtt.h"
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(spotflow_coredump, CONFIG_SPOTFLOW_COREDUMPS_PROCESSING_LOG_LEVEL);

/**
 * Polls for enqueued coredump chunks and processes them.

 * @retval 0 No coredumps were sent.
 * @retval >0 Coredumps were sent.
 * @retval <0 Error occurred while processing coredumps.
 */
int spotflow_poll_and_process_enqueued_coredump_chunks(void)
{
	struct spotflow_mqtt_coredumps_msg* msg_ptr;

	/* Peek without removing - returns non-zero if queue empty */
	if (k_msgq_peek(&g_spotflow_core_dumps_msgq, &msg_ptr) != 0) {
		return 0; /* Queue empty */
	}

	/* Publish while message is still safely in queue */
	int rc = spotflow_mqtt_publish_ingest_cbor_msg(msg_ptr->payload, msg_ptr->len);
	if (rc == -EAGAIN) {
		/* Temporary, retry later without aborting connection */
		return rc;
	}
	if (rc < 0) {
		LOG_DBG("Failed to publish coredump: %d, aborting connection", rc);
		spotflow_mqtt_abort_mqtt();
		return rc;
	}

	/* Only remove after successful publish */
	k_msgq_get(&g_spotflow_core_dumps_msgq, &msg_ptr, K_NO_WAIT);

	bool is_last_chunk = msg_ptr->coredump_last_chunk;
	k_free(msg_ptr->payload);
	k_free(msg_ptr);

	if (is_last_chunk) {
		LOG_INF("Coredump successfully sent.");
		spotflow_coredump_sent();
	}

	return 1;
}
