#ifndef SPOTFLOW_CONFIG_PERSISTENCE_H
#define SPOTFLOW_CONFIG_PERSISTENCE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct spotflow_config_persisted_settings {
	bool contains_sent_log_level : 1;
	uint8_t sent_log_level;
};

void spotflow_config_persistence_try_init();
void spotflow_config_persistence_try_load(struct spotflow_config_persisted_settings* settings);
void spotflow_config_persistence_try_save(struct spotflow_config_persisted_settings* settings);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_CONFIG_PERSISTENCE_H */
