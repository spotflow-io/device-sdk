#ifndef SPOTFLOW_CONFIG_PERSISTENCE_H
#define SPOTFLOW_CONFIG_PERSISTENCE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
SPOTFLOW_PERSISTED_SETTINGS_FLAG_SENT_LOG_LEVEL = (1 << 0)
} spotflow_config_persisted_flags_t;

struct spotflow_config_persisted_settings {
	spotflow_config_persisted_flags_t flags;
	uint8_t sent_log_level;
};

void spotflow_config_persistence_try_init(void);
void spotflow_config_persistence_try_load(struct spotflow_config_persisted_settings* settings);
void spotflow_config_persistence_try_save(struct spotflow_config_persisted_settings* settings);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_CONFIG_PERSISTENCE_H */