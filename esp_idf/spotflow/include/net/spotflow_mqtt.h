#ifndef SPOTFLOW_MQTT_H
#define SPOTFLOW_MQTT_H

#include "spotflow.h"
#include "stdatomic.h"

extern esp_mqtt_client_handle_t client;
extern atomic_bool mqtt_connected;

void mqtt_app_start(void);

#endif