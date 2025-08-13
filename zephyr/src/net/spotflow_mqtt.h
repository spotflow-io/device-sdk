#ifndef SPOTFLOW_MQTT_H
#define SPOTFLOW_MQTT_H

#include "net/spotflow_processor.h"

#ifdef __cplusplus
extern "C" {
#endif

void spotflow_mqtt_establish_mqtt();

bool spotflow_mqtt_is_connected();

int spotflow_mqtt_poll();
int spotflow_mqtt_publish_cbor_msg(uint8_t* payload, size_t len);
void spotflow_mqtt_abort_mqtt();
int spotflow_mqtt_send_live();

#ifdef __cplusplus
}
#endif

#endif /*SPOTFLOW_MQTT_H*/
