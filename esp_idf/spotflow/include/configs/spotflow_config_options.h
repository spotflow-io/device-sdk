#ifndef SPOTFLOW_CONFIG_OPTIONS_H
#define SPOTFLOW_CONFIG_OPTIONS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t spotflow_config_get_sent_log_level();
void spotflow_config_init_sent_log_level_default();
void spotflow_config_init_sent_log_level(uint8_t level);
void spotflow_config_set_sent_log_level(uint8_t level);

#ifdef __cplusplus
}
#endif

#endif // SPOTFLOW_CONFIG_OPTIONS_H