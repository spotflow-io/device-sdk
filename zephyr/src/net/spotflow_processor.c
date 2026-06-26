#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "config/spotflow_config.h"
#include "config/spotflow_config_net.h"
#if CONFIG_SPOTFLOW_TRANSPORT_MQTT
#include "net/transport/mqtt/spotflow_mqtt_session.h"
#endif

#include "net/spotflow_processor.h"
#include "net/spotflow_transport.h"

#ifdef CONFIG_SPOTFLOW_COREDUMPS
#include "coredumps/spotflow_coredumps_net.h"
#endif /* CONFIG_SPOTFLOW_COREDUMPS */

#ifdef CONFIG_SPOTFLOW_LOG_BACKEND
#include "logging/spotflow_log_processor.h"
#endif /* CONFIG_SPOTFLOW_LOG_BACKEND */

#ifdef CONFIG_SPOTFLOW_METRICS
#include "metrics/spotflow_metrics_net.h"
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM
#include "metrics/system/spotflow_metrics_system.h"
#endif
#ifdef CONFIG_SPOTFLOW_METRICS_HEARTBEAT
#include "metrics/spotflow_metrics_heartbeat.h"
#endif
#endif /* CONFIG_SPOTFLOW_METRICS */

LOG_MODULE_REGISTER(spotflow_net, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

static void spotflow_processing_thread_entry(void);
static int process_config_coredumps_metrics_or_logs(void);
#if !CONFIG_SPOTFLOW_TRANSPORT_MQTT
static void process_transport_loop(void);
#endif

K_THREAD_DEFINE(spotflow_processing_thread, CONFIG_SPOTFLOW_PROCESSING_THREAD_STACK_SIZE,
		spotflow_processing_thread_entry, NULL, NULL, NULL, SPOTFLOW_THREAD_PRIORITY, 0, 0);

void spotflow_start_processing(void)
{
	k_thread_start(spotflow_processing_thread);
	LOG_DBG("Thread started with priority %d and stack size %d", SPOTFLOW_THREAD_PRIORITY,
		CONFIG_SPOTFLOW_PROCESSING_THREAD_STACK_SIZE);
}

static void spotflow_processing_thread_entry(void)
{
	LOG_DBG("Starting Spotflow processing thread");

#ifdef CONFIG_SPOTFLOW_METRICS
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM
	/* Initialize system metrics early so they start collecting at boot,
	 * before transport connection is established. */
	int rc_sys_metrics = spotflow_metrics_system_init();
	if (rc_sys_metrics < 0) {
		LOG_ERR("Failed to initialize system metrics: %d", rc_sys_metrics);
	}
#endif
#endif

	int rc = spotflow_transport_start();
	if (rc < 0) {
		LOG_ERR("Failed to start Spotflow transport: %d", rc);
		return;
	}

#ifdef CONFIG_SPOTFLOW_METRICS
	spotflow_metrics_net_init();
#ifdef CONFIG_SPOTFLOW_METRICS_HEARTBEAT
	spotflow_metrics_heartbeat_init();
#endif
#endif

#if CONFIG_SPOTFLOW_TRANSPORT_MQTT
	spotflow_mqtt_session_loop(process_config_coredumps_metrics_or_logs);
#else
	while (true) {
		process_transport_loop();
	}
#endif
}

static int process_config_coredumps_metrics_or_logs(void)
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
		rc = spotflow_poll_and_process_enqueued_logs();
		if (rc < 0) {
			LOG_DBG("Failed to process logs: %d", rc);
			return rc;
		}
	}
#endif
	return rc;
}

#if !CONFIG_SPOTFLOW_TRANSPORT_MQTT
static void process_transport_loop(void)
{
	int rc = process_config_coredumps_metrics_or_logs();

	if (rc == 0 || rc == -EAGAIN) {
		k_sleep(K_MSEC(100));
		return;
	}

	if (rc < 0) {
		LOG_DBG("Failed to process transport loop: %d", rc);
		k_sleep(K_MSEC(100));
	}
}
#endif
