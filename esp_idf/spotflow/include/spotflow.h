#ifndef SPOTFLOW_H
#define SPOTFLOW_H

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
#include <time.h>
#include "esp_log_timestamp.h"

#include "net/spotflow_mqtt.h"
#include "logging/spotflow_log_backend.h"
#include "logging/spotflow_log_queue.h"

extern vprintf_like_t original_vprintf;
void spotflow_init(void);

#endif