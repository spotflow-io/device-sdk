#ifndef ESP_AT_H
#define ESP_AT_H

#include <stdint.h>

void ESP_Init(void);

int ESP_SendCommand(char *cmd, char *expected, uint32_t timeout);

int ESP_WiFi_Connect(char *ssid, char *pass);

int ESP_MQTT_Connect(char *client_id, char *username, char *password);

int ESP_MQTT_Publish(char *topic, uint8_t *payload, uint16_t len);

int ESP_UploadCert(const char *name, const uint8_t *data, uint32_t len);

#endif
