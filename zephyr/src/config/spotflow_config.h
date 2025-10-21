#ifndef SPOTFLOW_CONFIG_H
#define SPOTFLOW_CONFIG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void spotflow_config_init();
int spotflow_config_init_session();
int spotflow_config_send_pending_message();

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_CONFIG_H */
