#include "zephyr/kernel.h"
#include "logging/spotflow_log_backend.h"
#include "net/spotflow_mqtt.h"
#include "zephyr/logging/log.h"

LOG_MODULE_DECLARE(spotflow_logging, CONFIG_SPOTFLOW_LOGS_PROCESSING_LOG_LEVEL);

static uint32_t messages_sent_counter = 0;
static struct k_poll_event logs_poll_event;

void init_logs_polling()
{
	/* set up k_poll on msgq */
	k_poll_event_init(&logs_poll_event, K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
			  K_POLL_MODE_NOTIFY_ONLY, &g_spotflow_logs_msgq);
}
int poll_and_process_enqueued_logs()
{
	struct spotflow_mqtt_logs_msg* msg_ptr;
	int rc;
	/* Message-queue: check (non-blocking) for enqueued logs */
	k_poll(&logs_poll_event, 1, K_NO_WAIT); /* polls events[0] */
	if (logs_poll_event.state == K_POLL_STATE_MSGQ_DATA_AVAILABLE) {
		/*Logs are using best effort approach - if connection is dropped when transmitting
		 * log message will be lost
		 */
		if (k_msgq_get(&g_spotflow_logs_msgq, &msg_ptr, K_NO_WAIT) == 0) {
			rc = spotflow_mqtt_publish_ingest_cbor_msg(msg_ptr->payload, msg_ptr->len);
			if (rc < 0) {
				LOG_DBG("Failed to publish cbor log message rc: %d -> "
					"aborting mqtt connection",
					rc);
				spotflow_mqtt_abort_mqtt();
				/* Free the message buffer before breaking */
				k_free(msg_ptr->payload);
				k_free(msg_ptr);
				return rc;
			}

			messages_sent_counter++;
			k_free(msg_ptr->payload);
			k_free(msg_ptr);
			if (messages_sent_counter % 100 == 0) {
				LOG_INF("Sent %" PRIu32 " messages", messages_sent_counter);
			}
			if (messages_sent_counter == UINT32_MAX) {
				LOG_INF("Sent %" PRIu32 " messages. Reset.", messages_sent_counter);
				messages_sent_counter = 0; /* reset counter */
			}
		}
		logs_poll_event.state = K_POLL_STATE_NOT_READY;
	}
	return 0;
}
