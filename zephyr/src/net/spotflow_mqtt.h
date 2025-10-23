#ifndef SPOTFLOW_MQTT_H
#define SPOTFLOW_MQTT_H

#include <stdint.h>
#include "net/spotflow_processor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*spotflow_mqtt_message_cb)(uint8_t* payload, size_t len);

void spotflow_mqtt_establish_mqtt();

bool spotflow_mqtt_is_connected();

int spotflow_mqtt_poll();
int spotflow_mqtt_request_config_subscription(spotflow_mqtt_message_cb callback);
int spotflow_mqtt_publish_ingest_cbor_msg(uint8_t* payload, size_t len);
int spotflow_mqtt_publish_config_cbor_msg(uint8_t* payload, size_t len);
void spotflow_mqtt_abort_mqtt();
int spotflow_mqtt_send_live();

#ifdef __cplusplus
}
#endif

#endif /*SPOTFLOW_MQTT_H*/
