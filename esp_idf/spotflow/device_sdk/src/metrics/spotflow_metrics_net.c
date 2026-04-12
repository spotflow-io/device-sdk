#include "metrics/spotflow_metrics_net.h"
#include "net/spotflow_mqtt.h"
#include "logging/spotflow_log_backend.h"
#include "logging/spotflow_log_net.h"

#ifdef CONFIG_SPOTFLOW_METRICS_HEARTBEAT
#include "metrics/spotflow_metrics_heartbeat.h"
#endif

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <stdlib.h>
#include <errno.h>

/* Message queue for metrics transmission */
static QueueHandle_t g_spotflow_metrics_msgq;

void spotflow_metrics_net_init(void)
{
	SPOTFLOW_DEBUG("Metrics network layer initialized");

	if (!g_spotflow_metrics_msgq) {
		g_spotflow_metrics_msgq = xQueueCreate(CONFIG_SPOTFLOW_METRICS_QUEUE_SIZE,
						       sizeof(struct spotflow_mqtt_metrics_msg*));
		if (!g_spotflow_metrics_msgq) {
			SPOTFLOW_LOG("Failed to create metrics queue");
		}
	}
}

int spotflow_poll_and_process_enqueued_metrics(void)
{
	struct spotflow_mqtt_metrics_msg* msg;
	BaseType_t rc;

#ifdef CONFIG_SPOTFLOW_METRICS_HEARTBEAT
	/* Heartbeat has higher priority - process first */
	int hb_rc = spotflow_poll_and_process_heartbeat();
	if (hb_rc != 0) {
		return hb_rc;
	}
#endif

	/* Peek without removing */
	rc = xQueuePeek(g_spotflow_metrics_msgq, &msg, 0);
	if (rc != pdTRUE) {
		return 0; /* Queue empty */
	}

	/* Publish while message is still safely in queue */
	int pub_rc = spotflow_mqtt_publish_message(SPOTFLOW_MQTT_LOG_TOPIC, msg->payload, msg->len,
						   SPOTFLOW_MQTT_LOG_QOS // QoS
	);
	if (pub_rc == -EAGAIN) {
		return pub_rc; /* Temporary, retry later */
	}
	if (pub_rc < 0) {
		SPOTFLOW_LOG("Failed to publish metric: %d", pub_rc);
		return pub_rc; /* Message stays in queue. Retry later */
	}

	/* Only remove after successful publish */
	xQueueReceive(g_spotflow_metrics_msgq, &msg, 0);

	free(msg->payload);
	free(msg);

	return 1;
}

int spotflow_metrics_enqueue(uint8_t* payload, size_t len)
{
	if (!g_spotflow_metrics_msgq || !payload || len == 0) {
		return -EINVAL;
	}

	struct spotflow_mqtt_metrics_msg* msg = malloc(sizeof(*msg));
	if (!msg) {
		return -ENOMEM;
	}

	msg->payload = malloc(len);
	if (!msg->payload) {
		free(msg);
		return -ENOMEM;
	}

	memcpy(msg->payload, payload, len);
	msg->len = len;

	if (xQueueSend(g_spotflow_metrics_msgq, &msg, 0) != pdTRUE) {
		free(msg->payload);
		free(msg);
		return -EAGAIN;
	}
	spotflow_mqtt_notify_action(SPOTFLOW_MQTT_NOTIFY_METRICS);
	return 0;
}
