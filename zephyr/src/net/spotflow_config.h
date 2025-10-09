#ifndef SPOTFLOW_CONFIG_H
#define SPOTFLOW_CONFIG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint8_t spotflow_config_get_sent_log_level();

void spotflow_config_init();
int spotflow_config_init_session();

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_CONFIG_H */
