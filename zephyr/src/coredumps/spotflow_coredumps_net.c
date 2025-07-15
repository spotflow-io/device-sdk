#include "zephyr/kernel.h"
#include "coredumps/spotflow_coredumps_backend.h"
#include "net/spotflow_mqtt.h"
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(spotflow_coredump, CONFIG_SPOTFLOW_PROCESSING_BACKEND_COREDUMPS_LEVEL);

static struct k_poll_event core_dumps_poll_event;
void spotflow_init_core_dumps_polling()
{
	/* set up k_poll on msgq */
	k_poll_event_init(&core_dumps_poll_event, K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
			  K_POLL_MODE_NOTIFY_ONLY, &g_spotflow_core_dumps_msgq);
}

/**
 * Polls for enqueued coredump chunks and processes them.

 * @retval 0 No coredumps were sent.
 * @retval >0 Coredumps were sent.
 * @retval <0 Error occurred while processing coredumps.
 */
int spotflow_poll_and_process_enqueued_coredump_chunks()
{
	struct spotflow_mqtt_coredumps_msg* msg_ptr;
	int rc;
	/* Message-queue: check (non-blocking) for enqueued coredumps */
	k_poll(&core_dumps_poll_event, 1, K_NO_WAIT);
	if (core_dumps_poll_event.state == K_POLL_STATE_MSGQ_DATA_AVAILABLE) {
		/* Peek at the next core‐dump chunk without removing it */
		if (k_msgq_peek(&g_spotflow_core_dumps_msgq, &msg_ptr) == 0) {
			rc = spotflow_mqtt_publish_cbor_msg(msg_ptr->payload, msg_ptr->len);
			if (rc < 0) {
				LOG_DBG("Failed to publish cbor core dump message rc: %d -> "
					"aborting mqtt connection",
					rc);
				spotflow_mqtt_abort_mqtt();
				return rc;
			}
			/*Removing message from buffer after successful publish*/
			if (k_msgq_get(&g_spotflow_core_dumps_msgq, &msg_ptr, K_NO_WAIT) == 0) {
				k_free(msg_ptr->payload);
				k_free(msg_ptr);
				if (msg_ptr->coredump_last_chunk) {
					LOG_INF("Coredump sucessfully sent.");
					spotflow_coredump_sent();
				}
			} else {
				/* This really shouldn't happen, but guard anyway */
				LOG_ERR("Unexpected: failed to dequeue after successful publish");
			}
		} else {
			LOG_DBG("Unable to peek message from queue");
		}
		core_dumps_poll_event.state = K_POLL_STATE_NOT_READY;
		return 1;
	} else {
		return 0;
	}
}
