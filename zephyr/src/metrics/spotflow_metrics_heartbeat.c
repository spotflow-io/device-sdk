#include "spotflow_metrics_heartbeat.h"
#include "spotflow_metrics_cbor.h"
#include "../net/spotflow_mqtt.h"

#include <inttypes.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(spotflow_metrics, CONFIG_SPOTFLOW_METRICS_PROCESSING_LOG_LEVEL);

/* Single-slot buffer for pending heartbeat (size 1, silent overwrite) */
static struct spotflow_mqtt_metrics_msg g_pending_heartbeat;
static uint8_t g_pending_heartbeat_payload[SPOTFLOW_METRICS_HEARTBEAT_CBOR_MAX_LEN];
static bool g_pending_heartbeat_valid;
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
	size_t len = 0;
	int rc = spotflow_metrics_cbor_encode_heartbeat(uptime_ms, g_pending_heartbeat_payload,
						      sizeof(g_pending_heartbeat_payload), &len);
	if (rc != 0) {
		LOG_ERR("Failed to encode heartbeat: %d", rc);
		goto reschedule;
	}

	/* Silent overwrite of pending heartbeat (buffer size 1) */
	k_mutex_lock(&g_heartbeat_mutex, K_FOREVER);
	if (g_pending_heartbeat_valid) {
		LOG_DBG("Overwriting pending heartbeat");
	}

	g_pending_heartbeat.payload = g_pending_heartbeat_payload;
	g_pending_heartbeat.len = len;
	g_pending_heartbeat_valid = true;

	k_mutex_unlock(&g_heartbeat_mutex);

	LOG_DBG("Heartbeat queued (uptime=%" PRId64 " ms, %zu bytes)", uptime_ms, len);

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
	struct spotflow_mqtt_metrics_msg msg;
	uint8_t payload_copy[SPOTFLOW_METRICS_HEARTBEAT_CBOR_MAX_LEN];
	bool has_pending = false;

	/* Atomically dequeue pending heartbeat */
	k_mutex_lock(&g_heartbeat_mutex, K_FOREVER);
	if (g_pending_heartbeat_valid) {
		msg = g_pending_heartbeat;
		memcpy(payload_copy, g_pending_heartbeat.payload, g_pending_heartbeat.len);
		msg.payload = payload_copy;
		g_pending_heartbeat_valid = false;
		has_pending = true;
	}
	k_mutex_unlock(&g_heartbeat_mutex);

	if (!has_pending) {
		return 0; /* No heartbeat pending */
	}

	/* Publish heartbeat with exponential backoff on transient errors */
	static const int retry_delays_ms[] = { 10, 100, 1000 };
	int rc;
	int retry = 0;

	do {
		rc = spotflow_mqtt_publish_ingest_cbor_msg(msg.payload, msg.len);
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

	if (rc < 0) {
		LOG_WRN("Failed to publish heartbeat: %d", rc);
		return rc;
	}

	LOG_DBG("Heartbeat published");
	return 1; /* Processed one heartbeat */
}
