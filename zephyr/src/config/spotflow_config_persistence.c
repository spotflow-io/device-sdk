#include <stdint.h>

#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include "config/spotflow_config_persistence.h"

#if CONFIG_SPOTFLOW_SETTINGS

LOG_MODULE_DECLARE(spotflow_net, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

#define SPOTFLOW_SETTINGS_PACKAGE "spotflow"
#define SPOTFLOW_SETTINGS_KEY_SENT_LOG_LEVEL "sent_log_level"
#define SPOTFLOW_SETTINGS_PATH_SENT_LOG_LEVEL \
	SPOTFLOW_SETTINGS_PACKAGE "/" SPOTFLOW_SETTINGS_KEY_SENT_LOG_LEVEL

static int settings_direct_load_callback(const char* key, size_t len, settings_read_cb read_cb,
					 void* cb_arg, void* param);

void spotflow_config_persistence_try_init()
{
	int ret = settings_subsys_init();
	if (ret != 0) {
		LOG_ERR("Failed to initialize settings subsystem, persisting configuration will "
			"not work: %d",
			ret);
		return;
	}
}

void spotflow_config_persistence_try_load(struct spotflow_config_persisted_settings* settings)
{
	*settings = (struct spotflow_config_persisted_settings){ 0 };

	int ret = settings_load_subtree_direct(SPOTFLOW_SETTINGS_PACKAGE,
					       settings_direct_load_callback, settings);
	if (ret != 0) {
		LOG_ERR("Failed to load persisted Spotflow configuration");
		return;
	}

	LOG_INF("Persisted Spotflow configuration loaded");
}

void spotflow_config_persistence_try_save(struct spotflow_config_persisted_settings* settings)
{
	if (settings->contains_sent_log_level) {
		/* This function writes the value only if it has changed */
		int rc =
		    settings_save_one(SPOTFLOW_SETTINGS_PATH_SENT_LOG_LEVEL,
				      &settings->sent_log_level, sizeof(settings->sent_log_level));
		if (rc < 0) {
			LOG_ERR("Failed to persist sent log level setting: %d", rc);
		} else {
			LOG_DBG("Sent log level setting persisted: %d", settings->sent_log_level);
		}
	}
}

static int settings_direct_load_callback(const char* key, size_t len, settings_read_cb read_cb,
					 void* cb_arg, void* param)
{
	struct spotflow_config_persisted_settings* settings = param;

	if (strcmp(key, SPOTFLOW_SETTINGS_KEY_SENT_LOG_LEVEL) == 0) {
		if (len != sizeof(settings->sent_log_level)) {
			LOG_ERR("Invalid length for sent log level setting");
			return -EINVAL;
		}

		int ret =
		    read_cb(cb_arg, &settings->sent_log_level, sizeof(settings->sent_log_level));
		if (ret < 0) {
			LOG_ERR("Failed to read sent log level setting: %d", ret);
		} else {
			settings->contains_sent_log_level = true;
			LOG_DBG("Persisted sent log level loaded: %d", settings->sent_log_level);
		}
	}

	return 0;
}

#else

void spotflow_config_persistence_try_init() {}

void spotflow_config_persistence_try_load(struct spotflow_config_persisted_settings* settings)
{
	*settings = (struct spotflow_config_persisted_settings){ 0 };
}

void spotflow_config_persistence_try_save(struct spotflow_config_persisted_settings* settings) {}

#endif /* CONFIG_SPOTFLOW_SETTINGS */
