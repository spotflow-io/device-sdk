#include <stdbool.h>
#include <stdint.h>

#include <zephyr/logging/log.h>

#include "logging/spotflow_log_cbor.h"
#include "config/spotflow_config.h"
#include "config/spotflow_config_cbor.h"
#include "config/spotflow_config_options.h"
#include "config/spotflow_config_persistence.h"
#include "net/spotflow_mqtt.h"

LOG_MODULE_DECLARE(spotflow_net, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

static void add_log_severity_to_reported_msg(struct spotflow_config_reported_msg* reported_msg);
static void handle_desired_msg(uint8_t* payload, size_t len);

void spotflow_config_init()
{
	struct spotflow_config_persisted_settings persisted_settings;

	spotflow_config_persistence_try_init();
	spotflow_config_persistence_try_load(&persisted_settings);

	spotflow_config_init_sent_log_level(persisted_settings.contains_sent_log_level,
					    persisted_settings.sent_log_level);
}

int spotflow_config_init_session()
{
	struct spotflow_config_reported_msg reported_msg = {
		.contains_acked_desired_config_version = false,
	};
	add_log_severity_to_reported_msg(&reported_msg);

	uint8_t buffer[SPOTFLOW_CONFIG_RESPONSE_MAX_LENGTH];
	int rc = spotflow_config_cbor_encode_reported(&reported_msg, buffer, sizeof(buffer));
	if (rc < 0) {
		LOG_ERR("Failed to encode initial reported configuration message: %d", rc);
		return rc;
	}

	rc = spotflow_mqtt_publish_config_cbor_msg(buffer, sizeof(buffer));
	if (rc < 0) {
		LOG_ERR("Failed to publish initial reported configuration message: %d", rc);
		return rc;
	}

	rc = spotflow_mqtt_request_config_subscription(handle_desired_msg);
	if (rc < 0) {
		LOG_ERR("Failed to request subscription to configuration topic: %d", rc);
		return rc;
	}

	return 0;
}

static void add_log_severity_to_reported_msg(struct spotflow_config_reported_msg* reported_msg)
{
	uint8_t sent_log_level = spotflow_config_get_sent_log_level();

	reported_msg->contains_minimal_log_severity = true;
	reported_msg->minimal_log_severity =
	    spotflow_cbor_convert_log_level_to_severity(sent_log_level);

	reported_msg->contains_compiled_minimal_log_severity = true;
	reported_msg->compiled_minimal_log_severity =
	    spotflow_cbor_convert_log_level_to_severity(CONFIG_LOG_MAX_LEVEL);
}

static void handle_desired_msg(uint8_t* payload, size_t len)
{
	struct spotflow_config_desired_msg desired_msg;
	int rc = spotflow_config_cbor_decode_desired(payload, len, &desired_msg);
	if (rc < 0) {
		LOG_ERR("Failed to decode received desired configuration message: %d", rc);
		return;
	}

	struct spotflow_config_reported_msg reported_msg = {
		.contains_acked_desired_config_version = true,
		.acked_desired_config_version = desired_msg.desired_config_version,
	};

	struct spotflow_config_persisted_settings settings_to_persist = { 0 };

	if (desired_msg.contains_minimal_log_severity) {
		uint8_t new_sent_log_level =
		    spotflow_cbor_convert_severity_to_log_level(desired_msg.minimal_log_severity);

		spotflow_config_set_sent_log_level(new_sent_log_level);

		add_log_severity_to_reported_msg(&reported_msg);

		settings_to_persist.contains_sent_log_level = true;
		settings_to_persist.sent_log_level = new_sent_log_level;
	}

	spotflow_config_persistence_try_save(&settings_to_persist);

	uint8_t buffer[SPOTFLOW_CONFIG_RESPONSE_MAX_LENGTH];
	rc = spotflow_config_cbor_encode_reported(&reported_msg, buffer, sizeof(buffer));
	if (rc < 0) {
		LOG_ERR("Failed to encode reported configuration response message: %d", rc);
		return;
	}

	rc = spotflow_mqtt_publish_config_cbor_msg(buffer, sizeof(buffer));
	if (rc < 0) {
		LOG_ERR("Failed to publish reported configuration response message: %d", rc);
		return;
	}
}
