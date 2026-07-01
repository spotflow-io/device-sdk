#include <errno.h>

#include <zephyr/logging/log.h>

#ifdef CONFIG_SPOTFLOW_LOG_BACKEND
#include "config/spotflow_config.h"
#include "config/spotflow_config_net.h"
#endif /* CONFIG_SPOTFLOW_LOG_BACKEND */
#include "net/spotflow_session_metadata.h"
#include "spotflow_connection_helper.h"
#include "spotflow_mqtt.h"
#include "spotflow_mqtt_session.h"
#include "spotflow_tls.h"

LOG_MODULE_DECLARE(spotflow_net, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

static void process_mqtt_session(spotflow_mqtt_process_fn process_fn)
{
	int rc;

	rc = spotflow_session_metadata_send();
	if (rc < 0) {
		LOG_WRN("Failed to send session metadata, aborting MQTT: %d", rc);
		spotflow_mqtt_abort_mqtt();
		return;
	}

#ifdef CONFIG_SPOTFLOW_LOG_BACKEND
	rc = spotflow_config_init_session();
	if (rc < 0) {
		LOG_WRN("Failed to initialize configuration updating: %d", rc);
	}
#endif /* CONFIG_SPOTFLOW_LOG_BACKEND */

	/* INNER LOOP: perform normal MQTT I/O until an error occurs. */
	while (spotflow_mqtt_is_connected()) {
		rc = spotflow_mqtt_poll();
		if (rc < 0) {
			spotflow_mqtt_abort_mqtt();
			break; /* break out of the inner loop; outer loop will reconnect */
		}

		rc = process_fn();
		if (rc == -EAGAIN) {
			/* Transient: MQTT busy, retry on next iteration */
			continue;
		}
		if (rc < 0) {
			/* Problem in sending/mqtt_publish, reestablishing MQTT Connection */
			break;
		}

		/* Let the MQTT library do any keep-alive or retry logic. */
		rc = spotflow_mqtt_send_live();
		if (rc < 0) {
			LOG_DBG("mqtt_live() returned error %d, reconnecting", rc);
			spotflow_mqtt_abort_mqtt();
			break;
		}
	}
}

void spotflow_mqtt_session_loop(spotflow_mqtt_process_fn process_fn)
{
	wait_for_network();

	spotflow_tls_init();

	LOG_DBG("Spotflow registered TLS credentials");

	/* OUTER LOOP: keep trying until MQTT connects, reconnect if connection failed. */
	while (true) {
		spotflow_mqtt_establish_mqtt();

		process_mqtt_session(process_fn);
	}
}
