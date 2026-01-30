/*
 * Copyright (c) 2024 Spotflow
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "spotflow_metrics_heartbeat.h"
#include "spotflow_metrics_types.h"
#include "../net/spotflow_mqtt.h"

#include <zcbor_common.h>
#include <zcbor_encode.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(spotflow_metrics_heartbeat, CONFIG_SPOTFLOW_METRICS_PROCESSING_LOG_LEVEL);

/* CBOR Protocol Keys */
#define KEY_MESSAGE_TYPE     0x00  /* 0 */
#define KEY_DEVICE_UPTIME_MS 0x06  /* 6 */
#define KEY_METRIC_NAME      0x15  /* 21 */
#define KEY_SUM              0x18  /* 24 */

#define METRIC_MESSAGE_TYPE  0x05

/* Heartbeat metric name */
#define HEARTBEAT_METRIC_NAME "uptime_ms"

/* Single-slot buffer for pending heartbeat (size 1, silent overwrite) */
static struct spotflow_mqtt_metrics_msg *g_pending_heartbeat;
static struct k_mutex g_heartbeat_mutex;

/* Periodic heartbeat work */
static struct k_work_delayable g_heartbeat_work;

/**
 * @brief Encode a minimal heartbeat CBOR message
 *
 * Output format:
 * {
 *   0x00: 0x05,                    // messageType = 5 (METRIC)
 *   0x15: "device_uptime_ms",      // metricName
 *   0x06: <int64>,                 // deviceUptimeMs
 *   0x18: <int64>                  // sum (same uptime value)
 * }
 *
 * @param uptime_ms Device uptime in milliseconds
 * @param data Output pointer for allocated CBOR data
 * @param len Output pointer for CBOR data length
 * @return 0 on success, negative errno on failure
 */
static int encode_heartbeat(int64_t uptime_ms, uint8_t **data, size_t *len)
{
	uint8_t buffer[64];  /* Small static buffer, sufficient for ~40 bytes output */
	ZCBOR_STATE_E(state, 1, buffer, sizeof(buffer), 1);

	bool succ = true;

	/* Start CBOR map with 4 entries */
	succ = succ && zcbor_map_start_encode(state, 4);

	/* messageType = 5 */
	succ = succ && zcbor_uint32_put(state, KEY_MESSAGE_TYPE);
	succ = succ && zcbor_uint32_put(state, METRIC_MESSAGE_TYPE);

	/* metricName = "device_uptime_ms" */
	succ = succ && zcbor_uint32_put(state, KEY_METRIC_NAME);
	succ = succ && zcbor_tstr_put_lit(state, HEARTBEAT_METRIC_NAME);

	/* deviceUptimeMs */
	succ = succ && zcbor_uint32_put(state, KEY_DEVICE_UPTIME_MS);
	succ = succ && zcbor_int64_put(state, uptime_ms);

	/* sum = uptime value */
	succ = succ && zcbor_uint32_put(state, KEY_SUM);
	succ = succ && zcbor_int64_put(state, uptime_ms);

	succ = succ && zcbor_map_end_encode(state, 4);

	if (!succ) {
		LOG_ERR("Heartbeat CBOR encoding failed");
		return -EINVAL;
	}

	/* Allocate and copy */
	size_t encoded_len = state->payload - buffer;
	*data = k_malloc(encoded_len);
	if (!*data) {
		LOG_ERR("Failed to allocate heartbeat payload");
		return -ENOMEM;
	}

	memcpy(*data, buffer, encoded_len);
	*len = encoded_len;

	return 0;
}

/**
 * @brief Timer handler - generates heartbeat and overwrites pending buffer
 */
static void heartbeat_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	int64_t uptime_ms = k_uptime_get();

	/* Encode heartbeat message */
	uint8_t *payload = NULL;
	size_t len = 0;
	int rc = encode_heartbeat(uptime_ms, &payload, &len);
	if (rc != 0) {
		LOG_ERR("Failed to encode heartbeat: %d", rc);
		goto reschedule;
	}

	/* Allocate message structure */
	struct spotflow_mqtt_metrics_msg *new_msg = k_malloc(sizeof(*new_msg));
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

	LOG_DBG("Heartbeat queued (uptime=%lld ms, %zu bytes)",
		(long long)uptime_ms, len);

reschedule:
	/* Reschedule for next interval */
	k_work_schedule(&g_heartbeat_work,
			K_SECONDS(CONFIG_SPOTFLOW_METRICS_HEARTBEAT_INTERVAL));
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
	struct spotflow_mqtt_metrics_msg *msg = NULL;

	/* Atomically dequeue pending heartbeat */
	k_mutex_lock(&g_heartbeat_mutex, K_FOREVER);
	msg = g_pending_heartbeat;
	g_pending_heartbeat = NULL;
	k_mutex_unlock(&g_heartbeat_mutex);

	if (!msg) {
		return 0;  /* No heartbeat pending */
	}

	/* Publish heartbeat with exponential backoff on transient errors */
	static const int retry_delays_ms[] = {10, 100, 1000, 5000};
	int rc;
	int retry = 0;

	do {
		rc = spotflow_mqtt_publish_ingest_cbor_msg(msg->payload, msg->len);
		if (rc == -EAGAIN) {
			if (retry >= ARRAY_SIZE(retry_delays_ms)) {
				LOG_WRN("Heartbeat publish failed after %d retries, skipping", retry);
				break;
			}
			LOG_DBG("MQTT busy, retrying heartbeat in %d ms...", retry_delays_ms[retry]);
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
	return 1;  /* Processed one heartbeat */
}
