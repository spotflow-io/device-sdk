#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "config/spotflow_config_net.h"
#include "config/spotflow_config_cbor.h"
#include "config/spotflow_config_options.h"
#include "net/spotflow_mqtt.h"

LOG_MODULE_DECLARE(spotflow_net, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

K_MUTEX_DEFINE(pending_message_mutex);

static volatile bool is_message_pending = false;
static uint8_t pending_message_buffer[SPOTFLOW_CONFIG_RESPONSE_MAX_LENGTH];
static size_t pending_message_length = 0;

int spotflow_config_prepare_pending_message(struct spotflow_config_reported_msg* reported_msg)
{
	k_mutex_lock(&pending_message_mutex, K_FOREVER);

	int rc = spotflow_config_cbor_encode_reported(reported_msg, pending_message_buffer,
						      sizeof(pending_message_buffer),
						      &pending_message_length);
	is_message_pending = (rc == 0);

	k_mutex_unlock(&pending_message_mutex);

	return rc;
}

int spotflow_config_send_pending_message()
{
	if (!is_message_pending) {
		return 0;
	}

	int rc = k_mutex_lock(&pending_message_mutex, K_NO_WAIT);
	if (rc != 0) {
		/* The message is currently being prepared, so it's not pending yet */
		return 0;
	}

	/* Double check after acquiring the mutex to avoid race conditions */
	if (!is_message_pending) {
		k_mutex_unlock(&pending_message_mutex);
		return 0;
	}

	rc = spotflow_mqtt_publish_config_cbor_msg(pending_message_buffer, pending_message_length);
	if (rc < 0) {
		LOG_ERR("Failed to publish reported configuration message: %d -> "
			"aborting mqtt connection",
			rc);
		spotflow_mqtt_abort_mqtt();
	}

	is_message_pending = false;

	k_mutex_unlock(&pending_message_mutex);

	return rc;
}
