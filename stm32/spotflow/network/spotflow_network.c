#include "spotflow_network.h"
#include "esp_at.h"
#include "queue/spotflow_queue.h"
#include "port/port.h"
#include "spotflow_config.h"

#include <string.h>
#include <stdio.h>

/**
 * @brief MQTT task that continuously reads messages from the queue and publishes them to the MQTT broker
 *
 * @param argument
 */
static void mqtt_task(void *argument)
{
    queue_msg_t msg;

    while (1)
    {
        if (spotflow_queue_read(&msg))
        {
            char buffer[128] = {0};

            size_t len = (msg.len < sizeof(buffer) - 1) ? msg.len : sizeof(buffer) - 1;
            memcpy(buffer, msg.ptr, len);

            ESP_MQTT_Publish(SPOTFLOW_MQTT_TOPIC, buffer, len);

            spotflow_queue_free(&msg);
        }

        osDelay(100);
    }
}

/**
 * @brief Initialize the network
 */
void spotflow_network_init(void)
{
    // Init queue
    spotflow_queue_init();

    // Init ESP
    ESP_Init();

    // WiFi connect
    while (ESP_WiFi_Connect(WIFI_SSID, WIFI_PASS) != 0)
    {
        osDelay(2000);
    }

    // MQTT connect
    while (ESP_MQTT_Connect(MQTT_CLIENT, DEVICE_ID, INGEST_KEY) != 0)
    {
        osDelay(2000);
    }

    // Create MQTT task
    spotflow_port_thread_create(
        mqtt_task,
        "mqtt_task",
        1024,
        osPriorityNormal
    );
}
