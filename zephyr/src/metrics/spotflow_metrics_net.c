/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "spotflow_metrics_net.h"
#include "../net/spotflow_mqtt.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spotflow_metrics_net, CONFIG_SPOTFLOW_METRICS_PROCESSING_LOG_LEVEL);

/* Message queue for metrics transmission */
K_MSGQ_DEFINE(g_spotflow_metrics_msgq,
	      sizeof(struct spotflow_mqtt_metrics_msg *),
	      CONFIG_SPOTFLOW_METRICS_QUEUE_SIZE,
	      sizeof(void *));

void spotflow_metrics_net_init(void)
{
	LOG_DBG("Metrics network layer initialized");
}

int spotflow_poll_and_process_enqueued_metrics(void)
{
	struct spotflow_mqtt_metrics_msg *msg;

	/* Try to dequeue one message */
	int rc = k_msgq_get(&g_spotflow_metrics_msgq, &msg, K_NO_WAIT);
	if (rc != 0) {
		return 0;  /* No message available */
	}

	/* Memory Ownership: Processor thread ALWAYS frees message memory */
	/* (success or failure) after dequeue completes */

	/* Infinite retry loop for transient errors (preserves message ordering) */
	do {
		rc = spotflow_mqtt_publish_ingest_cbor_msg(msg->payload, msg->len);
		if (rc == -EAGAIN) {
			LOG_DBG("MQTT busy, retrying...");
			k_sleep(K_MSEC(10));  /* Small delay before retry */
		}
	} while (rc == -EAGAIN);

	/* ALWAYS free message memory (success or permanent failure) */
	k_free(msg->payload);
	k_free(msg);

	if (rc < 0) {
		LOG_WRN("Failed to publish metric message: %d", rc);
		/* Permanent error - message lost */
		return rc;
	}

	return 1;  /* Processed one message */
}
