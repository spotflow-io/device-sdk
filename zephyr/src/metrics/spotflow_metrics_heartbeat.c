/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "spotflow_metrics_heartbeat.h"
#include "spotflow_metrics_cbor.h"
#include "../net/spotflow_mqtt.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spotflow_metrics_heartbeat, CONFIG_SPOTFLOW_METRICS_PROCESSING_LOG_LEVEL);

/* Single-slot buffer for pending heartbeat (size 1, silent overwrite) */
static struct spotflow_mqtt_metrics_msg* g_pending_heartbeat;
static struct k_mutex g_heartbeat_mutex;

/* Periodic heartbeat work */
static struct k_work_delayable g_heartbeat_work;

/**
 * @brief Timer handler - generates heartbeat and overwrites pending buffer
 */
static void heartbeat_work_handler(struct k_work* work)
{
	ARG_UNUSED(work);

	int64_t uptime_ms = k_uptime_get();

	/* Encode heartbeat message */
	uint8_t* payload = NULL;
	size_t len = 0;
	int rc = spotflow_metrics_cbor_encode_heartbeat(uptime_ms, &payload, &len);
	if (rc != 0) {
		LOG_ERR("Failed to encode heartbeat: %d", rc);
		goto reschedule;
	}

	/* Allocate message structure */
	struct spotflow_mqtt_metrics_msg* new_msg = k_malloc(sizeof(*new_msg));
	if (!new_msg) {
		LOG_ERR("Failed to allocate heartbeat message");
		k_free(payload);
		goto reschedule;
	}

	new_msg->payload = payload;
	new_msg->len = len;

	/* Silent overwrite of pending heartbeat (buffer size 1) */
	k_mutex_lock(&g_heartbeat_mutex, K_FOREVER);

	if (g_pending_heartbeat) {
		/* Discard old pending heartbeat (silent overwrite) */
		k_free(g_pending_heartbeat->payload);
		k_free(g_pending_heartbeat);
		LOG_DBG("Overwriting pending heartbeat");
	}

	g_pending_heartbeat = new_msg;

	k_mutex_unlock(&g_heartbeat_mutex);

	LOG_DBG("Heartbeat queued (uptime=%lld ms, %zu bytes)", (long long)uptime_ms, len);

reschedule:
	/* Reschedule for next interval */
	k_work_schedule(&g_heartbeat_work, K_SECONDS(CONFIG_SPOTFLOW_METRICS_HEARTBEAT_INTERVAL));
}

void spotflow_metrics_heartbeat_init(void)
{
	k_mutex_init(&g_heartbeat_mutex);
	k_work_init_delayable(&g_heartbeat_work, heartbeat_work_handler);

	/* Schedule first heartbeat immediately */
	k_work_schedule(&g_heartbeat_work, K_NO_WAIT);

	LOG_INF("Heartbeat initialized (interval=%d s)",
		CONFIG_SPOTFLOW_METRICS_HEARTBEAT_INTERVAL);
}

int spotflow_poll_and_process_heartbeat(void)
{
	struct spotflow_mqtt_metrics_msg* msg = NULL;

	/* Atomically dequeue pending heartbeat */
	k_mutex_lock(&g_heartbeat_mutex, K_FOREVER);
	msg = g_pending_heartbeat;
	g_pending_heartbeat = NULL;
	k_mutex_unlock(&g_heartbeat_mutex);

	if (!msg) {
		return 0; /* No heartbeat pending */
	}

	/* Publish heartbeat with exponential backoff on transient errors */
	static const int retry_delays_ms[] = { 10, 100, 1000 };
	int rc;
	int retry = 0;

	do {
		rc = spotflow_mqtt_publish_ingest_cbor_msg(msg->payload, msg->len);
		if (rc == -EAGAIN) {
			if (retry >= ARRAY_SIZE(retry_delays_ms)) {
				LOG_WRN("Heartbeat publish failed after %d retries, skipping",
					retry);
				break;
			}
			LOG_DBG("MQTT busy, retrying heartbeat in %d ms...",
				retry_delays_ms[retry]);
			k_sleep(K_MSEC(retry_delays_ms[retry]));
			retry++;
		}
	} while (rc == -EAGAIN);

	/* Always free message memory */
	k_free(msg->payload);
	k_free(msg);

	if (rc < 0) {
		LOG_WRN("Failed to publish heartbeat: %d", rc);
		return rc;
	}

	LOG_DBG("Heartbeat published");
	return 1; /* Processed one heartbeat */
}
