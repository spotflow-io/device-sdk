#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/socket.h>

#include "config/spotflow_config.h"
#include "config/spotflow_config_net.h"
#include "net/spotflow_processor.h"
#include "net/spotflow_mqtt.h"
#include "net/spotflow_connection_helper.h"
#include "net/spotflow_session_metadata.h"
#include "net/spotflow_tls.h"

#ifdef CONFIG_SPOTFLOW_COREDUMPS
#include "coredumps/spotflow_coredumps_net.h"
#endif /* CONFIG_SPOTFLOW_COREDUMPS */

#ifdef CONFIG_SPOTFLOW_LOG_BACKEND
#include "logging/spotflow_log_net.h"
#endif /* CONFIG_SPOTFLOW_LOG_BACKEND */

#ifdef CONFIG_SPOTFLOW_METRICS
#include "metrics/spotflow_metrics_net.h"
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM
#include "../metrics/system/spotflow_metrics_system.h"
#endif
#ifdef CONFIG_SPOTFLOW_METRICS_HEARTBEAT
#include "../metrics/spotflow_metrics_heartbeat.h"
#endif
#endif /* CONFIG_SPOTFLOW_METRICS */

#define APP_CONNECT_TIMEOUT_MS 10000

#define LOG_DBG_PRINT_RESULT(func, rc) LOG_DBG("%s: %d <%s>", (func), rc, RC_STR(rc))

LOG_MODULE_REGISTER(spotflow_net, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

static void spotflow_mqtt_thread_entry(void);
static void process_mqtt();

K_THREAD_DEFINE(spotflow_mqtt_thread, CONFIG_SPOTFLOW_PROCESSING_THREAD_STACK_SIZE,
		spotflow_mqtt_thread_entry, NULL, NULL, NULL, SPOTFLOW_MQTT_THREAD_PRIORITY, 0, 0);

void spotflow_start_mqtt(void)
{
	k_thread_start(spotflow_mqtt_thread);
	LOG_DBG("Thread started with priority %d and stack size %d", SPOTFLOW_MQTT_THREAD_PRIORITY,
		CONFIG_SPOTFLOW_PROCESSING_THREAD_STACK_SIZE);
}

static void spotflow_mqtt_thread_entry(void)
{
	LOG_DBG("Starting Spotflow processing thread");

#ifdef CONFIG_SPOTFLOW_METRICS
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM
	/* Initialize system metrics early so they start collecting at boot,
	 * before network connection is established */
	int rc_sys_metrics = spotflow_metrics_system_init();
	if (rc_sys_metrics < 0) {
		LOG_ERR("Failed to initialize system metrics: %d", rc_sys_metrics);
	}
#endif
#endif

	wait_for_network();

	spotflow_tls_init();

	LOG_DBG("Spotflow registered TLS credentials");
#ifdef CONFIG_SPOTFLOW_METRICS
	spotflow_metrics_net_init();
#ifdef CONFIG_SPOTFLOW_METRICS_HEARTBEAT
	spotflow_metrics_heartbeat_init();
#endif
#endif

	/* 1) OUTER LOOP: keep trying until mqtt_connected == true, reconnect if connection failed */
	while (true) {
		spotflow_mqtt_establish_mqtt();

		process_mqtt();
	}
}

static int process_config_coredumps_or_logs()
{
	int rc = spotflow_config_send_pending_message();
	if (rc < 0) {
		LOG_DBG("Failed to send pending configuration message: %d", rc);
		return rc;
	}
#ifdef CONFIG_SPOTFLOW_COREDUMPS
	rc = spotflow_poll_and_process_enqueued_coredump_chunks();
	if (rc < 0) {
		LOG_DBG("Failed to process coredumps: %d", rc);
		return rc;
	}
#endif
#ifdef CONFIG_SPOTFLOW_METRICS
	/* Process metrics after coredumps, before logs */
	if (rc == 0) {
		rc = spotflow_poll_and_process_enqueued_metrics();
		if (rc < 0) {
			LOG_DBG("Failed to process metrics: %d", rc);
			return rc;
		}
	}
#endif
#ifdef CONFIG_SPOTFLOW_LOG_BACKEND
	/* rc > 0 means core dumps or metrics were sent
	 *	-> doing mqtt routine and continue with core dumps/metrics sending
	 */
	if (rc == 0) {
		/*No coredumps or metrics to send -> sending logs*/
		rc = poll_and_process_enqueued_logs();
		if (rc < 0) {
			LOG_DBG("Failed to process logs: %d", rc);
			return rc;
		}
	}
#endif
	return rc;
}

static void process_mqtt()
{
	int rc;
	rc = spotflow_session_metadata_send();
	if (rc < 0) {
		LOG_WRN("Failed to send session metadata, aborting MQTT: %d", rc);
		spotflow_mqtt_abort_mqtt();
		return;
	}

	rc = spotflow_config_init_session();
	if (rc < 0) {
		LOG_WRN("Failed to initialize configuration updating: %d", rc);
	}

	/*  INNER LOOP: perform normal MQTT I/O until an error occurs. */
	while (spotflow_mqtt_is_connected()) {
		rc = spotflow_mqtt_poll();
		if (rc < 0) {
			spotflow_mqtt_abort_mqtt();
			break; /* break out of the inner loop; outer loop will reconnect */
		}

		rc = process_config_coredumps_or_logs();
		if (rc < 0) {
			/* Problem in sending/mqtt_publish, reestablishing MQTT Connection*/
			break;
		}

		/* -- Let the MQTT library do any keep‐alive or retry logic. */
		rc = spotflow_mqtt_send_live();
		if (rc < 0) {
			LOG_DBG("mqtt_live() returned error %d → reconnecting", rc);
			spotflow_mqtt_abort_mqtt();
			break;
		}
	}
}
