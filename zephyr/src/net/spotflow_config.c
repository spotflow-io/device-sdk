#include <stdbool.h>
#include <stdint.h>

#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include "logging/spotflow_log_backend.h"
#include "logging/spotflow_log_cbor.h"
#include "net/spotflow_config.h"
#include "net/spotflow_config_cbor.h"
#include "net/spotflow_mqtt.h"

LOG_MODULE_DECLARE(spotflow_net, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

#define SPOTFLOW_SETTINGS_PACKAGE "spotflow"
#define SPOTFLOW_SETTINGS_KEY_SENT_LOG_LEVEL "sent_log_level"
#define SPOTFLOW_SETTINGS_PATH_SENT_LOG_LEVEL \
	SPOTFLOW_SETTINGS_PACKAGE "/" SPOTFLOW_SETTINGS_KEY_SENT_LOG_LEVEL

static volatile uint8_t sent_log_level = CONFIG_SPOTFLOW_DEFAULT_SENT_LOG_LEVEL;

#if CONFIG_SPOTFLOW_SETTINGS
static int settings_direct_load_callback(const char* key, size_t len, settings_read_cb read_cb,
					 void* cb_arg, void* param);
#endif

static void set_sent_log_level(uint8_t level);
static void add_log_severity_to_reported_msg(struct spotflow_config_reported_msg* reported_msg);
static void handle_desired_msg(uint8_t* payload, size_t len);

uint8_t spotflow_config_get_sent_log_level()
{
	return sent_log_level;
}

void spotflow_config_init()
{
	/* Ensure that the update to the runtime filter is applied */
	set_sent_log_level(sent_log_level);

#if CONFIG_SPOTFLOW_SETTINGS

	int ret = settings_subsys_init();
	if (ret != 0) {
		LOG_ERR("Failed to initialize settings subsystem, persisting configuration will "
			"not work");
		return;
	}

	ret = settings_load_subtree_direct(SPOTFLOW_SETTINGS_PACKAGE, settings_direct_load_callback,
					   NULL);
	if (ret != 0) {
		LOG_ERR("Failed to load persisted Spotflow configuration");
		return;
	}

	LOG_INF("Persisted Spotflow configuration loaded");

#endif
}

#if CONFIG_SPOTFLOW_SETTINGS
static int settings_direct_load_callback(const char* key, size_t len, settings_read_cb read_cb,
					 void* cb_arg, void* param)
{
	if (strcmp(key, SPOTFLOW_SETTINGS_KEY_SENT_LOG_LEVEL) == 0) {
		if (len != sizeof(sent_log_level)) {
			LOG_ERR("Invalid length for sent log level setting");
			return -EINVAL;
		}

		uint8_t persisted_sent_log_level;
		int ret =
		    read_cb(cb_arg, &persisted_sent_log_level, sizeof(persisted_sent_log_level));
		if (ret < 0) {
			LOG_ERR("Failed to read sent log level setting: %d", ret);
			return ret;
		}

		LOG_DBG("Persisted sent log level loaded: %d", persisted_sent_log_level);

		set_sent_log_level(persisted_sent_log_level);
	}

	return 0;
}
#endif

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

#if CONFIG_SPOTFLOW_SETTINGS
		rc = settings_save_one(SPOTFLOW_SETTINGS_PATH_SENT_LOG_LEVEL, &new_sent_log_level,
				       sizeof(sent_log_level));
		if (rc < 0) {
			LOG_ERR("Failed to persist updated sent log level setting: %d", rc);
		} else {
			LOG_DBG("Updated sent log level setting persisted");
		}
#endif

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
