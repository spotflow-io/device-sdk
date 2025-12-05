#include <stdbool.h>
#include <stdint.h>

#include "logging/spotflow_log_backend.h"
#include "logging/spotflow_log_cbor.h"
#include "configs/spotflow_config.h"
#include "configs/spotflow_config_cbor.h"
#include "configs/spotflow_config_persistance.h"
#include "configs/spotflow_config_net.h"
#include "configs/spotflow_config_options.h"

static void add_log_severity_to_reported_msg(struct spotflow_config_reported_msg* reported_msg);

/**
 * @brief Initilize the cloud configurable log level.
 *
 */
void spotflow_config_init()
{
	struct spotflow_config_persisted_settings persisted_settings;

	spotflow_config_persistence_try_init();
	spotflow_config_persistence_try_load(&persisted_settings);

	if ((persisted_settings.flags & SPOTFLOW_PERSISTED_SETTINGS_FLAG_SENT_LOG_LEVEL) != 0) {
		spotflow_config_init_sent_log_level(persisted_settings.sent_log_level);
	} else {
		spotflow_config_init_sent_log_level_default();
	}
	spotflow_config_init_session();
}

/**
 * @brief Initialize the session and send a intiail message with current configurations.
 *
 * @return int
 */
int spotflow_config_init_session()
{
	struct spotflow_config_reported_msg reported_msg = {
		.flags = 0,
	};
	add_log_severity_to_reported_msg(&reported_msg);

	int rc = spotflow_config_prepare_pending_message(&reported_msg);
	if (rc < 0) {
		SPOTFLOW_LOG(
		    "Failed to prepare initial reported configuration message: %d", rc);
		return rc;
	}

	return 0;
}

/**
 * @brief Receive the log level from cloud.
 *
 * @param payload
 * @param len
 */
void spotflow_config_desired_message(const uint8_t* payload, int len)
{
	struct spotflow_config_desired_msg desired_msg;
	SPOTFLOW_LOG("Deconding Payload\n");
	int rc = spotflow_config_cbor_decode_desired(payload, len, &desired_msg);
	if (rc < 0) {
		SPOTFLOW_LOG("Failed to decode received desired configuration message: %d\n", rc);
		return;
	} else {
		SPOTFLOW_LOG("decode successful\n");
	}

	struct spotflow_config_reported_msg reported_msg = {
		.flags = SPOTFLOW_REPORTED_FLAG_ACKED_DESIRED_CONFIG_VERSION,
		.acked_desired_config_version = desired_msg.desired_config_version,
	};

	struct spotflow_config_persisted_settings settings_to_persist = { 0 };

	if (desired_msg.flags & SPOTFLOW_PERSISTED_SETTINGS_FLAG_SENT_LOG_LEVEL) {
		uint8_t new_sent_log_level =
		    spotflow_cbor_convert_severity_to_log_level(desired_msg.minimal_log_severity);

		spotflow_config_set_sent_log_level(new_sent_log_level);

		add_log_severity_to_reported_msg(&reported_msg);

		settings_to_persist.flags |= SPOTFLOW_PERSISTED_SETTINGS_FLAG_SENT_LOG_LEVEL;
		settings_to_persist.sent_log_level = new_sent_log_level;
	}

	spotflow_config_persistence_try_save(&settings_to_persist);

	SPOTFLOW_LOG("Reported log severity %lu, desired config version %lu \n\n",
		     reported_msg.minimal_log_severity, desired_msg.minimal_log_severity);
	rc = spotflow_config_prepare_pending_message(&reported_msg);
	if (rc < 0) {
		SPOTFLOW_LOG("Failed to prepare reported configuration response message: %d", rc);
		return;
	}
}

/**
 * @brief Add current compiled maximum log level and the current log severity.
 *
 * @param reported_msg
 */
static void add_log_severity_to_reported_msg(struct spotflow_config_reported_msg* reported_msg)
{
	uint8_t sent_log_level = spotflow_config_get_sent_log_level();

	reported_msg->flags |= SPOTFLOW_PERSISTED_SETTINGS_FLAG_SENT_LOG_LEVEL;
	reported_msg->minimal_log_severity =
	    spotflow_cbor_convert_log_level_to_severity(sent_log_level);

	reported_msg->flags |= SPOTFLOW_REPORTED_FLAG_COMPILED_MINIMAL_LOG_SEVERITY;
	reported_msg->compiled_minimal_log_severity =
	    spotflow_cbor_convert_log_level_to_severity(CONFIG_LOG_MAXIMUM_LEVEL);
}