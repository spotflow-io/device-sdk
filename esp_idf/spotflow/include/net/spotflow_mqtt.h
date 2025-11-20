#ifndef SPOTFLOW_MQTT_H
#define SPOTFLOW_MQTT_H

#include "stdatomic.h"
#include "mqtt_client.h"

#ifdef __cplusplus
extern "C" {
#endif

extern esp_mqtt_client_handle_t spotflow_client;
extern atomic_bool spotflow_mqtt_connected;

void spotflow_mqtt_app_start(void);
void spotflow_mqtt_publish(void* pvParameters);

#ifdef __cplusplus
}
#endif

#endif