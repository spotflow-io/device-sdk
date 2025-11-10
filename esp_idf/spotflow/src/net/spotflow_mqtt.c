#include "esp_mac.h"
#include "esp_system.h"
#include "esp_event.h"
#include "spotflow.h"
#include "esp_tls.h"
#include "logging/spotflow_log_backend.h"
#include "logging/spotflow_log_queue.h"
#include "net/spotflow_mqtt.h"

#ifdef CONFIG_ESP_COREDUMP_ENABLE
	#include "coredump/spotflow_coredump_queue.h"
#endif


// static const char *TAG = "spotflow_mqtt";

esp_mqtt_client_handle_t client = NULL;
atomic_bool mqtt_connected = ATOMIC_VAR_INIT(false);

#if CONFIG_BROKER_CERTIFICATE_OVERRIDDEN == 1
static const uint8_t mqtt_spotflow_io_pem_start[] =
    "-----BEGIN CERTIFICATE-----\n" CONFIG_BROKER_CERTIFICATE_OVERRIDE
    "\n-----END CERTIFICATE-----";
#else
extern const uint8_t mqtt_spotflow_io_pem_start[] asm("_binary_x1_root_pem_start");
#endif

/**
 * @brief MQTT Event handler for the mqtt events.
 * 
 * @param handler_args 
 * @param base 
 * @param event_id 
 * @param event_data 
 */
static void mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id,
			       void* event_data)
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
		SPOTFLOW_LOG("MQTT_EVENT_DISCONNECTED");
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
			SPOTFLOW_LOG("Last error code reported from esp-tls: 0x%x",
				     event->error_handle->esp_tls_last_esp_err);
			SPOTFLOW_LOG("Last tls stack error number: 0x%x",
				     event->error_handle->esp_tls_stack_err);
			SPOTFLOW_LOG("Last captured errno : %d (%s)",
				     event->error_handle->esp_transport_sock_errno,
				     strerror(event->error_handle->esp_transport_sock_errno));
		} else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
			SPOTFLOW_LOG("Connection refused error: 0x%x",
				     event->error_handle->connect_return_code);
		} else {
			SPOTFLOW_LOG("Unknown error type: 0x%x", event->error_handle->error_type);
		}
		break;
	default:
		SPOTFLOW_LOG("Other event id:%d", event->event_id);
		break;
	}
}

void mqtt_app_start(void)
{
	static char device_id[32]; // For saving the device ID
	uint8_t mac[6]; //The Mac Address string
	esp_read_mac(mac, ESP_MAC_WIFI_STA); // Read the mac Address

	// Setting mqtt username
	if (strlen(CONFIG_SPOTFLOW_DEVICE_ID) != 0) {
		snprintf(device_id, sizeof(device_id), "%s",
			 CONFIG_SPOTFLOW_DEVICE_ID); //Adding the device ID here defined in Kconfig
	} else {
		snprintf(device_id, sizeof(device_id), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1],
			 mac[2], mac[3], mac[4], mac[5]); // Reading and adding mac Address.
	}

	const esp_mqtt_client_config_t mqtt_cfg = {
		.broker = { .address.uri = CONFIG_SPOTFLOW_SERVER_HOSTNAME,
			    .verification.certificate = (const char*)mqtt_spotflow_io_pem_start },
		.credentials.username = device_id,
		.credentials.authentication.password = (const char*)CONFIG_SPOTFLOW_INGEST_KEY
	};

	client = esp_mqtt_client_init(&mqtt_cfg);
	esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
	esp_mqtt_client_start(client);
	xTaskCreate(mqtt_publish, "mqtt_publish", CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN * 2, NULL, 5,
		    NULL);
}

/**
 * @brief Seperate Task for publishing the mqtt messages.
 * 
 * @param pvParameters 
 */
void mqtt_publish(void* pvParameters)
{
	queue_msg_t msg;

    while (1) {
        if (atomic_load(&mqtt_connected)) {

            // Only proceed if the outbox_size i.e. current message is smaller than overall mqtt_buffer.
            if ((esp_mqtt_client_get_outbox_size(client) < CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN) || (esp_mqtt_client_get_outbox_size(client) < CONFIG_SPOTFLOW_COREDUMPS_CHUNK_SIZE)) {
#ifdef CONFIG_ESP_COREDUMP_ENABLE
                // Try to send coredump messages first
                if (queue_coredump_read(&msg)) {
                    int msg_id = esp_mqtt_client_publish(
                        client,
                        "ingest-cbor",
                        (const char*)msg.ptr,
                        msg.len,
                        1,  // QoS
                        0   // retain
                    );

                    if (msg_id < 0) {
                        SPOTFLOW_LOG("Error %d occurred sending MQTT (coredump). Retrying", msg_id);
                        vTaskDelay(pdMS_TO_TICKS(10)); // Backoff
                    } else {
                        SPOTFLOW_LOG("Coredump message sent successfully. Freeing queue entry.");
                        queue_free(&msg);
						vTaskDelay(pdMS_TO_TICKS(10)); //Give CPU few ticks
                    }

                }
                // If no coredump pending, send regular log messages
                else 
#endif
				if (queue_read(&msg)) {
                    int msg_id = esp_mqtt_client_publish(
                        client,
                        "ingest-cbor",
                        (const char*)msg.ptr,
                        msg.len,
                        1,
                        0
                    );

                    if (msg_id < 0) {
                        SPOTFLOW_LOG("Error %d occurred sending MQTT (log). Retrying", msg_id);
                        vTaskDelay(pdMS_TO_TICKS(10)); // Backoff
                    } else {
                        SPOTFLOW_LOG("Log message sent successfully. Freeing queue entry. \n");
                        queue_free(&msg);
						vTaskDelay(pdMS_TO_TICKS(10)); //Give CPU few ticks
                    }
                }
                // Nothing to send
                else {
                    vTaskDelay(pdMS_TO_TICKS(50)); // Sleep if queues empty
                }

            } else {
                SPOTFLOW_LOG("MQTT outbox not empty; waiting for messages to be sent.\n");
				SPOTFLOW_LOG("MQTT Size %d \n",esp_mqtt_client_get_outbox_size(client));
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        } else {
            // MQTT not connected; wait and retry
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}