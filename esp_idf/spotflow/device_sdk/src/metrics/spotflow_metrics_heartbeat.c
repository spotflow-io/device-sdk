#include "metrics/spotflow_metrics_heartbeat.h"
#include "metrics/spotflow_metrics_cbor.h"
#include "logging/spotflow_log_backend.h"
#include "logging/spotflow_log_net.h"

#include <stdint.h>
#include <stdlib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_timer.h>

/* Single-slot buffer for pending heartbeat (size 1, silent overwrite) */
static struct spotflow_mqtt_metrics_msg g_pending_heartbeat;
static uint8_t g_pending_heartbeat_payload[SPOTFLOW_METRICS_HEARTBEAT_CBOR_MAX_LEN];
static bool g_pending_heartbeat_valid;
static SemaphoreHandle_t g_heartbeat_mutex;

/* Periodic heartbeat timer handle */
static esp_timer_handle_t g_heartbeat_timer;

/* Retry delays for MQTT publish */
static int g_retry_count = 0;
static int64_t g_next_retry_time_us = 0;
static const uint16_t retry_delays_ms[] = { 10, 100, 1000 };
/**
 * @brief Timer callback for generating heartbeat messages
 *
 * @param arg
 */
static void heartbeat_timer_callback(void* arg)
{
	int64_t uptime_ms = esp_timer_get_time() / 1000; // microseconds → milliseconds

	/* Encode heartbeat message */
	size_t len = 0;
	int rc = spotflow_metrics_cbor_encode_heartbeat(uptime_ms, g_pending_heartbeat_payload,
							sizeof(g_pending_heartbeat_payload), &len);
	if (rc != 0) {
		SPOTFLOW_LOG("Failed to encode heartbeat: %d", rc);
		return;
	}

	/* Silent overwrite of pending heartbeat */
	if (xSemaphoreTake(g_heartbeat_mutex, portMAX_DELAY)) {
		if (g_pending_heartbeat_valid) {
			SPOTFLOW_DEBUG("Overwriting pending heartbeat");
		}
		g_pending_heartbeat.payload = g_pending_heartbeat_payload;
		g_pending_heartbeat.len = len;
		g_pending_heartbeat_valid = true;

		xSemaphoreGive(g_heartbeat_mutex);
	}

	SPOTFLOW_DEBUG("Heartbeat queued (uptime=%" PRId64 " ms, %zu bytes)", uptime_ms, len);
}

/**
 * @brief Initialize the heartbeat functionality
 *
 */
void spotflow_metrics_heartbeat_init(void)
{
	g_heartbeat_mutex = xSemaphoreCreateMutex();
	if (!g_heartbeat_mutex) {
		SPOTFLOW_LOG("Failed to create heartbeat mutex");
		return;
	}

	// Calling it at once afterwards periodic timer will call after set interval.
	heartbeat_timer_callback(NULL);
	const esp_timer_create_args_t timer_args = { .callback = &heartbeat_timer_callback,
						     .arg = NULL,
						     .name = "heartbeat_timer" };

	ESP_ERROR_CHECK(esp_timer_create(&timer_args, &g_heartbeat_timer));
	ESP_ERROR_CHECK(esp_timer_start_periodic(
	    g_heartbeat_timer,
	    CONFIG_SPOTFLOW_METRICS_HEARTBEAT_INTERVAL * 1000000ULL // seconds → microseconds
	    ));

	SPOTFLOW_LOG("Heartbeat initialized (interval=%d s)",
		     CONFIG_SPOTFLOW_METRICS_HEARTBEAT_INTERVAL);
}

/**
 * @brief Poll and process pending heartbeat messages
 *
 * @return int
 */
int spotflow_poll_and_process_heartbeat(void)
{
	int64_t now_us = esp_timer_get_time();

	/* Check if retry delay has passed */
	if (g_next_retry_time_us > 0 && now_us < g_next_retry_time_us) {
		return 0; /* Still waiting for retry delay */
	}

	struct spotflow_mqtt_metrics_msg msg;
	bool has_pending = false;

	/* Try to acquire mutex without blocking */
	if (xSemaphoreTake(g_heartbeat_mutex, 0) != pdTRUE) {
		return 0; /* Mutex busy, skip this poll */
	}

	if (g_pending_heartbeat_valid) {
		msg = g_pending_heartbeat;
		has_pending = true;
	}

	xSemaphoreGive(g_heartbeat_mutex);

	if (!has_pending) {
		return 0; /* No heartbeat pending */
	}

	/* Attempt publish without blocking */
	int rc = spotflow_mqtt_publish_message(SPOTFLOW_MQTT_LOG_TOPIC, msg.payload, msg.len,
					       SPOTFLOW_MQTT_LOG_QOS);

	if (rc == -EAGAIN) {
		/* Schedule retry */
		if (g_retry_count < 3) {
			g_next_retry_time_us = now_us + (retry_delays_ms[g_retry_count] * 1000);
			g_retry_count++;
			SPOTFLOW_DEBUG("MQTT busy, retry #%d scheduled in %d ms", g_retry_count,
				       retry_delays_ms[g_retry_count - 1]);
		} else {
			/* Max retries exceeded, drop heartbeat */
			SPOTFLOW_LOG("Heartbeat dropped after max retries");
			if (xSemaphoreTake(g_heartbeat_mutex, 0) == pdTRUE) {
				g_pending_heartbeat_valid = false;
				g_retry_count = 0;
				g_next_retry_time_us = 0;
				xSemaphoreGive(g_heartbeat_mutex);
			}
			return -1;
		}
		return 0;
	}

	/* Success or fatal error */
	if (xSemaphoreTake(g_heartbeat_mutex, 0) == pdTRUE) {
		g_pending_heartbeat_valid = false;
		g_retry_count = 0;
		g_next_retry_time_us = 0;
		xSemaphoreGive(g_heartbeat_mutex);
	}

	if (rc < 0) {
		SPOTFLOW_LOG("Failed to publish heartbeat: %d", rc);
		return rc;
	}

	SPOTFLOW_LOG("Heartbeat published");
	return 1; /* Processed one heartbeat */
}
