#include <stdbool.h>
#include <stdint.h>

#include <zephyr/logging/log.h>

#include "logging/spotflow_log_backend.h"
#include "logging/spotflow_log_cbor.h"
#include "net/spotflow_config.h"
#include "net/spotflow_config_cbor.h"
#include "net/spotflow_config_persistence.h"
#include "net/spotflow_mqtt.h"

LOG_MODULE_DECLARE(spotflow_net, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

static volatile uint8_t sent_log_level = CONFIG_SPOTFLOW_DEFAULT_SENT_LOG_LEVEL;

static void set_sent_log_level(uint8_t level);
static void add_log_severity_to_reported_msg(struct spotflow_config_reported_msg* reported_msg);
static void handle_desired_msg(uint8_t* payload, size_t len);

uint8_t spotflow_config_get_sent_log_level()
{
	return sent_log_level;
}

void spotflow_config_init()
{
	struct spotflow_config_persisted_settings persisted_settings;

	spotflow_config_persistence_try_init();
	spotflow_config_persistence_try_load(&persisted_settings);

	uint8_t initial_sent_log_level = persisted_settings.contains_sent_log_level
	    ? persisted_settings.sent_log_level
	    : sent_log_level;

	/* Ensure that the update to the runtime filter is applied if available */
	set_sent_log_level(initial_sent_log_level);
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

static void set_sent_log_level(uint8_t level)
{
	uint8_t orig_level = sent_log_level;
	sent_log_level = level;
	LOG_INF("Updated sent log level to %d (was %d)", level, orig_level);

	spotflow_log_backend_try_set_runtime_filter(level);
}

static void add_log_severity_to_reported_msg(struct spotflow_config_reported_msg* reported_msg)
{
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

	if (desired_msg.contains_minimal_log_severity) {
		uint8_t new_sent_log_level =
		    spotflow_cbor_convert_severity_to_log_level(desired_msg.minimal_log_severity);

		set_sent_log_level(new_sent_log_level);

		struct spotflow_config_persisted_settings persisted_settings = {
			.contains_sent_log_level = true,
			.sent_log_level = new_sent_log_level,
		};
		spotflow_config_persistence_try_save(&persisted_settings);

		add_log_severity_to_reported_msg(&reported_msg);
	}

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
