#ifndef SPOTFLOW_TLS_H
#define SPOTFLOW_TLS_H

#include <zephyr/net/mqtt.h>

#ifdef __cplusplus
extern "C"
{
#endif

void spotflow_tls_configure(const char *hostname, struct mqtt_sec_config *tls_config);
int spotflow_tls_init(void);

#ifdef __cplusplus
}
#endif

#endif /*SPOTFLOW_TLS_H*/
