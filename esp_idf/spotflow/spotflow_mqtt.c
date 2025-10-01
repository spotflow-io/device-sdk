
#include "spotflow_mqtt.h"

static const char *TAG = "spotflow_mqtt";

esp_mqtt_client_handle_t client = NULL;
bool mqtt_connected = false;

#if CONFIG_BROKER_CERTIFICATE_OVERRIDDEN == 1
static const uint8_t mqtt_spotflow_io_pem_start[]  = "-----BEGIN CERTIFICATE-----\n" CONFIG_BROKER_CERTIFICATE_OVERRIDE "\n-----END CERTIFICATE-----";
#else
extern const uint8_t mqtt_spotflow_io_pem_start[]   asm("_binary_mqtt_spotflow_io_pem_start");
#endif
extern const uint8_t mqtt_spotflow_io_pem_end[]   asm("_binary_mqtt_spotflow_io_pem_end");

/**
 * 
 */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    // ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%" PRIi32, base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    // esp_mqtt_client_handle_t client = event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        SPOTFLOW_LOG("MQTT_EVENT_CONNECTED");
        mqtt_connected = true;
        esp_mqtt_client_subscribe(client, "ingest-json", 1);
        // msg_id = esp_mqtt_client_subscribe(client, "ingest-json", 1);
        // ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

        break;
    case MQTT_EVENT_DISCONNECTED:
        SPOTFLOW_LOG( "MQTT_EVENT_DISCONNECTED");
        mqtt_connected = false;
        break;

    case MQTT_EVENT_SUBSCRIBED:
        // ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        SPOTFLOW_LOG("MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        mqtt_connected = false;
        break;
    case MQTT_EVENT_PUBLISHED:
        SPOTFLOW_LOG("MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
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
    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address.uri = CONFIG_SPOTFLOW_SERVER_HOSTNAME,
            .verification.certificate = (const char *)mqtt_spotflow_io_pem_start
        },
        .credentials.username = (const char *)CONFIG_SPOTFLOW_DEVICE_ID,
        .credentials.authentication.password = (const char *)CONFIG_SPOTFLOW_INGEST_KEY
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
    SPOTFLOW_LOG( "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());

}