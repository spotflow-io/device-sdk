#include "esp_mac.h"
#include "esp_system.h"
#include "esp_event.h"
#include "spotflow.h"
#include "esp_tls.h"
#include "logging/spotflow_log_backend.h"
#include "logging/spotflow_log_queue.h"
#include "net/spotflow_mqtt.h"

// static const char *TAG = "spotflow_mqtt";

esp_mqtt_client_handle_t client = NULL;
atomic_bool mqtt_connected = ATOMIC_VAR_INIT(false);

#if CONFIG_BROKER_CERTIFICATE_OVERRIDDEN == 1
static const uint8_t mqtt_spotflow_io_pem_start[]  = "-----BEGIN CERTIFICATE-----\n" CONFIG_BROKER_CERTIFICATE_OVERRIDE "\n-----END CERTIFICATE-----";
#else
extern const uint8_t mqtt_spotflow_io_pem_start[]   asm("_binary_x1_root_pem_start");
#endif

/**
 * @brief MQTT Event handler for the mqtt events.
 * 
 * @param handler_args 
 * @param base 
 * @param event_id 
 * @param event_data 
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    // ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    // esp_mqtt_client_handle_t client = event->client;
    // int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        SPOTFLOW_LOG("MQTT_EVENT_CONNECTED");
        atomic_store(&mqtt_connected, true);

        break;
    case MQTT_EVENT_DISCONNECTED:
        SPOTFLOW_LOG( "MQTT_EVENT_DISCONNECTED");
        atomic_store(&mqtt_connected, false);
        break;

    case MQTT_EVENT_SUBSCRIBED:
        // ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        // SPOTFLOW_LOG("MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        // atomic_store(&mqtt_connected, false);
        break;
    case MQTT_EVENT_PUBLISHED:
        // SPOTFLOW_LOG("MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        SPOTFLOW_LOG("Message published. \n\n");
        break;
    case MQTT_EVENT_DATA:
        SPOTFLOW_LOG("MQTT_EVENT_DATA");
        SPOTFLOW_LOG("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        SPOTFLOW_LOG("DATA=%.*s\r\n", event->data_len, event->data);
        // if (strncmp(event->data, "send binary please", event->data_len) == 0) {
        //     ESP_LOGI(TAG, "Sending the binary");
        //     send_binary(client);
        // }
        break;
    case MQTT_EVENT_ERROR:
        SPOTFLOW_LOG("MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            SPOTFLOW_LOG( "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
            SPOTFLOW_LOG( "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
            SPOTFLOW_LOG( "Last captured errno : %d (%s)",  event->error_handle->esp_transport_sock_errno,
                     strerror(event->error_handle->esp_transport_sock_errno));
        } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            SPOTFLOW_LOG( "Connection refused error: 0x%x", event->error_handle->connect_return_code);
        } else {
            SPOTFLOW_LOG( "Unknown error type: 0x%x", event->error_handle->error_type);
        }
        break;
    default:
        SPOTFLOW_LOG( "Other event id:%d", event->event_id);
        break;
    }
}

void mqtt_app_start(void)
{

    static char device_id[32]; // For saving the device ID
    uint8_t mac[6]; //The Mac Address string
    esp_read_mac(mac, ESP_MAC_WIFI_STA); // Read the mac Address
    snprintf(device_id, sizeof(device_id), "%s",
            strlen(CONFIG_SPOTFLOW_DEVICE_ID) ? CONFIG_SPOTFLOW_DEVICE_ID :
            ({ static char tmp[32]; snprintf(tmp, sizeof(tmp), "%02X%02X%02X%02X%02X%02X",
                                                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]); tmp; }));  // Check if CONFIG_SPOTFLOW_DEVICE_ID is empty if no then copy the device ID, if yes then generate it.

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address.uri = CONFIG_SPOTFLOW_SERVER_HOSTNAME,
            .verification.certificate = (const char *)mqtt_spotflow_io_pem_start
        },
        .credentials.username = device_id,
        .credentials.authentication.password = (const char *)CONFIG_SPOTFLOW_INGEST_KEY
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
    xTaskCreate(mqtt_publish, "mqtt_publish", CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN*2, NULL, 5, NULL);

}

/**
 * @brief Seperate Task for publishing the mqtt messages.
 * 
 * @param pvParameters 
 */
void mqtt_publish(void *pvParameters)
{
    while(1)
    {
        if(atomic_load(&mqtt_connected))
        {
            if(esp_mqtt_client_get_outbox_size(client) == 0)    // Check the mqtt buffer is empty if not 
            {
                char *queue_buffer = malloc(CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN);
                size_t len = 0;
                while ((len = queue_read(queue_buffer)) != -1 && atomic_load(&mqtt_connected)) //Check if mqtt disconnect event is not generated.
                {
                   int msg_id = esp_mqtt_client_publish(client, "ingest-cbor", (const char*)queue_buffer , len, 1, 0); // Sending MQTT message the CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN provides the buffer length
                   // Error check.
                   if(msg_id < 0)
                   {
                    SPOTFLOW_LOG("Error %d occured while sending mqtt", msg_id);
                   }
                   else
                   {
                    SPOTFLOW_LOG("Message sent successfully");
                   }
                }
                free(queue_buffer); 
            }
            else
            {
                SPOTFLOW_LOG("MQTT buffer not empty some message is being processed.");
            } 
        }
        vTaskDelay(pdMS_TO_TICKS(50));  // 50ms interval during each recheck we can increase it. 
    }

}