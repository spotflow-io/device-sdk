#ifndef SPOTFLOW_TLS_H
#define SPOTFLOW_TLS_H

#include <zephyr/net/mqtt.h>

#define SPOTFLOW_TLS_SEC_TAG 1

#ifdef __cplusplus
extern "C" {
#endif

void spotflow_tls_configure(const char* hostname, struct mqtt_sec_config* tls_config);
/**
 * @brief Register the shared CA credential. Safe to call again.
 * @return 0 on success, negative errno on failure.
 */
int spotflow_tls_init(void);

#ifdef __cplusplus
}
#endif

#endif /*SPOTFLOW_TLS_H*/
