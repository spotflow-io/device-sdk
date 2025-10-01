#ifndef SPOTFLOW_MQTT_H
#define SPOTFLOW_MQTT_H

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_system.h"
#include "esp_partition.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "mqtt_client.h"
#include "esp_tls.h"
#include <sys/param.h>

#include "esp_crt_bundle.h"
#include <time.h>
#include "esp_log_timestamp.h"
#include "spotflow_log.h"

extern esp_mqtt_client_handle_t client;
extern bool mqtt_connected;

void mqtt_app_start(void);

#endif