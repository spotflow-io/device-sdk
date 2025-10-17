#include <stdbool.h>
#include <stdint.h>

#include <zephyr/logging/log.h>

#include "logging/spotflow_log_backend.h"
#include "config/spotflow_config_options.h"

LOG_MODULE_DECLARE(spotflow_net, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

static volatile uint8_t sent_log_level = CONFIG_SPOTFLOW_DEFAULT_SENT_LOG_LEVEL;

uint8_t spotflow_config_get_sent_log_level()
{
	return sent_log_level;
}

void spotflow_config_init_sent_log_level(bool override_value, uint8_t level)
{
	if (override_value) {
		sent_log_level = level;
	}

	LOG_INF("Initialized sent log level to %d", sent_log_level);

	spotflow_log_backend_try_set_runtime_filter(sent_log_level);
}

void spotflow_config_set_sent_log_level(uint8_t level)
{
	uint8_t orig_level = sent_log_level;
	if (orig_level == level) {
		return;
	}

	sent_log_level = level;
	LOG_INF("Updated sent log level to %d (was %d)", level, orig_level);

	spotflow_log_backend_try_set_runtime_filter(level);
}
