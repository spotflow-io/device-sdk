/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "spotflow_metrics_net.h"
#include "../net/spotflow_mqtt.h"

#ifdef CONFIG_SPOTFLOW_METRICS_HEARTBEAT
#include "spotflow_metrics_heartbeat.h"
#endif

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spotflow_metrics_net, CONFIG_SPOTFLOW_METRICS_PROCESSING_LOG_LEVEL);

/* Message queue for metrics transmission */
K_MSGQ_DEFINE(g_spotflow_metrics_msgq, sizeof(struct spotflow_mqtt_metrics_msg*),
	      CONFIG_SPOTFLOW_METRICS_QUEUE_SIZE, sizeof(void*));

void spotflow_metrics_net_init(void)
{
	LOG_DBG("Metrics network layer initialized");
}

int spotflow_poll_and_process_enqueued_metrics(void)
{
	struct spotflow_mqtt_metrics_msg* msg;
	int rc;

#ifdef CONFIG_SPOTFLOW_METRICS_HEARTBEAT
	/* Heartbeat has higher priority - process first */
	rc = spotflow_poll_and_process_heartbeat();
	if (rc != 0) {
		return rc;
	}
#endif

	/* Peek without removing - returns non-zero if queue empty */
	if (k_msgq_peek(&g_spotflow_metrics_msgq, &msg) != 0) {
		return 0; /* Queue empty */
	}

	/* Publish while message is still safely in queue */
	rc = spotflow_mqtt_publish_ingest_cbor_msg(msg->payload, msg->len);
	if (rc == -EAGAIN) {
		/* Temporary, retry later without aborting connection */
		return rc;
	}
	if (rc < 0) {
		LOG_WRN("Failed to publish metric: %d, aborting connection", rc);
		spotflow_mqtt_abort_mqtt();
		return rc; /* Message stays in queue for retry */
	}

	/* Only remove after successful publish */
	k_msgq_get(&g_spotflow_metrics_msgq, &msg, K_NO_WAIT);

	k_free(msg->payload);
	k_free(msg);

	return 1;
}
