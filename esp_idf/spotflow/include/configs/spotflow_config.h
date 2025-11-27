#ifndef SPOTFLOW_CONFIG_H
#define SPOTFLOW_CONFIG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void spotflow_config_init(void);
int spotflow_config_init_session(void);
void spotflow_config_desired_message(const uint8_t* payload, int len);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_CONFIG_H */