#ifndef SPOTFLOW_LOGGING_NET_H
#define SPOTFLOW_LOGGING_NET_H

#include "net/spotflow_mqtt.h"
#include "configs/spotflow_config_cbor.h"

#define SPOTFLOW_MQTT_LOG_TOPIC "ingest-cbor"

#define SPOTFLOW_MQTT_LOG_QOS 0

#ifdef __cplusplus
extern "C" {
#endif

int spotflow_logging_send_message(void);

#ifdef __cplusplus
}
#endif

#endif