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
static struct spotflow_mqtt_metrics_msg* g_pending_heartbeat;
static SemaphoreHandle_t g_heartbeat_mutex;

/* Periodic heartbeat timer handle */
static esp_timer_handle_t g_heartbeat_timer;

/**
 * @brief Timer callback for generating heartbeat messages
 *
 * @param arg
 */
static void heartbeat_timer_callback(void* arg)
{
	int64_t uptime_ms = esp_timer_get_time() / 1000; // microseconds → milliseconds

	/* Encode heartbeat message */
	uint8_t* payload = NULL;
	size_t len = 0;
	int rc = spotflow_metrics_cbor_encode_heartbeat(uptime_ms, &payload, &len);
	if (rc != 0) {
		SPOTFLOW_LOG("Failed to encode heartbeat: %d", rc);
		return;
	}

	/* Allocate message structure */
	struct spotflow_mqtt_metrics_msg* new_msg = malloc(sizeof(*new_msg));
	if (!new_msg) {
		SPOTFLOW_LOG("Failed to allocate heartbeat message");
		free(payload);
		return;
	}

	new_msg->payload = payload;
	new_msg->len = len;

	/* Silent overwrite of pending heartbeat */
	if (xSemaphoreTake(g_heartbeat_mutex, portMAX_DELAY)) {
		if (g_pending_heartbeat) {
			free(g_pending_heartbeat->payload);
			free(g_pending_heartbeat);
			SPOTFLOW_DEBUG("Overwriting pending heartbeat");
		}
		g_pending_heartbeat = new_msg;
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
	struct spotflow_mqtt_metrics_msg* msg = NULL;

	/* Atomically dequeue pending heartbeat */
	if (xSemaphoreTake(g_heartbeat_mutex, portMAX_DELAY)) {
		msg = g_pending_heartbeat;
		g_pending_heartbeat = NULL;
		xSemaphoreGive(g_heartbeat_mutex);
	}

	if (!msg) {
		return 0; /* No heartbeat pending */
	}

	/* Publish heartbeat with exponential backoff on transient errors */
	static const int retry_delays_ms[] = { 10, 100, 1000 };
	int rc = 0;

	for (int retry = 0; retry <= 3; retry++) {
		rc = spotflow_mqtt_publish_message(SPOTFLOW_MQTT_LOG_TOPIC, msg->payload, msg->len,
						   SPOTFLOW_MQTT_LOG_QOS);

		if (rc != -EAGAIN)
			break; // published or failed with real error

		if (retry < 3) {
			SPOTFLOW_DEBUG("MQTT busy, retrying heartbeat in %d ms...",
				       retry_delays_ms[retry]);
			vTaskDelay(pdMS_TO_TICKS(retry_delays_ms[retry]));
		}
	}

	/* Always free message memory */
	free(msg->payload);
	free(msg);

	if (rc < 0) {
		SPOTFLOW_LOG("Failed to publish heartbeat: %d", rc);
		return rc;
	}

	SPOTFLOW_LOG("Heartbeat published");
	return 1; /* Processed one heartbeat */
}
