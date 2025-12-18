#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_log_level.h"
#include "logging/spotflow_log_backend.h"
#include "configs/spotflow_config_options.h"

static volatile uint8_t sent_log_level = CONFIG_SPOTFLOW_DEFAULT_SENT_LOG_LEVEL;

uint8_t spotflow_config_get_sent_log_level()
{
	return sent_log_level;
}

void spotflow_config_init_sent_log_level(uint8_t level)
{
	sent_log_level = level;
	SPOTFLOW_LOG("Initialized sent log level to %d", sent_log_level);

	spotflow_log_backend_try_set_runtime_filter(sent_log_level);
}

void spotflow_config_init_sent_log_level_default()
{
	spotflow_config_init_sent_log_level(CONFIG_SPOTFLOW_DEFAULT_SENT_LOG_LEVEL);
}

void spotflow_config_set_sent_log_level(uint8_t level)
{
	if (sent_log_level == level) {
		return;
	}

	SPOTFLOW_LOG("Updated sent log level to %d (was %d)", level, sent_log_level);
	sent_log_level = level;

	spotflow_log_backend_try_set_runtime_filter(level);
}
