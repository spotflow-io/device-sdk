#ifndef SPOTFLOW_MQTT_H
#define SPOTFLOW_MQTT_H

#include "stdatomic.h"
#include "mqtt_client.h"

#define SPOTFLOW_MQTT_NOTIFY_COREDUMP (1 << 0) // Flag to trigger sending coredump
#define SPOTFLOW_MQTT_NOTIFY_CONFIG_MSG (1 << 1) // Flag to trigger sending pending config message
#define SPOTFLOW_MQTT_NOTIFY_LOGS (1 << 2) // Flag to trigger sending heartbeat
#define SPOTFLOW_MESSAGE_QUEUE_EMPTY 5     // Just a value set to know the message queue is empty

#ifdef __cplusplus
extern "C" {
#endif

extern esp_mqtt_client_handle_t spotflow_client;

void spotflow_mqtt_app_start(void);
void spotflow_mqtt_publish(void* pvParameters);
int spotflow_mqtt_subscribe(esp_mqtt_client_handle_t client, const char* topic, int qos);
void spotflow_mqtt_handle_data(esp_mqtt_event_handle_t event);
void spotflow_mqtt_on_message(const char* topic, int topic_len, const uint8_t* data, int data_len);
int spotflow_mqtt_publish_messgae(const char* topic, const uint8_t* data, int len, int qos);
void spotflow_mqtt_notify_action(uint32_t action_type);

#ifdef __cplusplus
}
#endif

#endif