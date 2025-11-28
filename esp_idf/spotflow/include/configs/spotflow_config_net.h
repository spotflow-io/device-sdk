#ifndef SPOTFLOW_CONFIG_NET_H
#define SPOTFLOW_CONFIG_NET_H

#include "net/spotflow_mqtt.h"
#include "configs/spotflow_config_cbor.h"

#define SPOTFLOW_MQTT_CONFIG_CBOR_D2C_TOPIC "config-cbor-d2c"
#define SPOTFLOW_MQTT_CONFIG_CBOR_C2D_TOPIC "config-cbor-c2d"

#define SPOTFLOW_MQTT_CONFIG_CBOR_D2C_TOPIC_QOS 0
#define SPOTFLOW_MQTT_CONFIG_CBOR_C2D_TOPIC_QOS 0

#ifdef __cplusplus
extern "C" {
#endif

int spotflow_config_prepare_pending_message(struct spotflow_config_reported_msg* reported_msg);
int spotflow_config_send_pending_message(void);

#ifdef __cplusplus
}
#endif

#endif // SPOTFLOW_CONFIG_NET_H