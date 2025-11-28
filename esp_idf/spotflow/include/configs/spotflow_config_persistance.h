#ifndef SPOTFLOW_CONFIG_PERSISTENCE_H
#define SPOTFLOW_CONFIG_PERSISTENCE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPOTFLOW_REPORTED_FLAG_MINIMAL_LOG_SEVERITY (1 << 0)

struct spotflow_config_persisted_settings {
	uint8_t flags;
	uint8_t sent_log_level;
};

void spotflow_config_persistence_try_init(void);
void spotflow_config_persistence_try_load(struct spotflow_config_persisted_settings* settings);
void spotflow_config_persistence_try_save(struct spotflow_config_persisted_settings* settings);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_CONFIG_PERSISTENCE_H */