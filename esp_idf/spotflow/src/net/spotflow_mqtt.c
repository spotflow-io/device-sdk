#include "esp_mac.h"
#include "esp_system.h"
#include "esp_event.h"
#include "spotflow.h"
#include "esp_tls.h"
#include "logging/spotflow_log_backend.h"
#include "logging/spotflow_log_net.h"
#include "net/spotflow_mqtt.h"

#include "configs/spotflow_config_net.h"
#include "configs/spotflow_config.h"

#ifdef CONFIG_ESP_COREDUMP_ENABLE
#include "coredump/spotflow_coredump_net.h"
#endif

esp_mqtt_client_handle_t spotflow_client = NULL;
static TaskHandle_t mqtt_publish_task_handle = NULL;
EventGroupHandle_t spotflow_mqtt_event_group;

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
static void spotflow_mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id,
					void* event_data)
{
	esp_mqtt_event_handle_t event = event_data;
	switch ((esp_mqtt_event_id_t)event_id) {
	case MQTT_EVENT_CONNECTED:
		SPOTFLOW_LOG("MQTT_EVENT_CONNECTED");
		xTaskCreate(spotflow_mqtt_publish, "mqtt_publish",
				CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN * 2, NULL, 5,
				&mqtt_publish_task_handle);
		spotflow_mqtt_subscribe(event->client, SPOTFLOW_MQTT_CONFIG_CBOR_C2D_TOPIC,
					SPOTFLOW_MQTT_CONFIG_CBOR_C2D_TOPIC_QOS);
		break;
	case MQTT_EVENT_DISCONNECTED:
		SPOTFLOW_LOG("MQTT_EVENT_DISCONNECTED");
		if (mqtt_publish_task_handle != NULL) {
			vTaskDelete(mqtt_publish_task_handle); // Delete the task when disconnected
			mqtt_publish_task_handle = NULL;
		}
		break;

	case MQTT_EVENT_SUBSCRIBED:
		break;
	case MQTT_EVENT_UNSUBSCRIBED:
		break;
	case MQTT_EVENT_PUBLISHED:
		SPOTFLOW_LOG("Message published. \n\n");
		if (mqtt_publish_task_handle &&
			 esp_mqtt_client_get_outbox_size(event->client) <
			CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN)
		{
			xTaskNotifyGive(mqtt_publish_task_handle);
		}
		break;
	case MQTT_EVENT_DATA:
		spotflow_mqtt_handle_data(event);
		// Future expansion.
		// For any other data event for MQTT 5.
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

void spotflow_mqtt_app_start(void)
{
	spotflow_mqtt_event_group = xEventGroupCreate();
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
		.credentials.authentication.password = (const char*)CONFIG_SPOTFLOW_INGEST_KEY,
 #if CONFIG_MQTT_PROTOCOL_5
		.session.protocol_ver = MQTT_PROTOCOL_V_5,  // Use MQTT v5 if enabled
	#else
		.session.protocol_ver = MQTT_PROTOCOL_V_3_1_1, // Default otherwise
#endif
	};

	spotflow_client = esp_mqtt_client_init(&mqtt_cfg);
	esp_mqtt_client_register_event(spotflow_client, ESP_EVENT_ANY_ID,
					   spotflow_mqtt_event_handler, NULL);
	esp_mqtt_client_start(spotflow_client);
}

/**
 * @brief Seperate Task for publishing the mqtt messages.
 *
 * @param pvParameters
 */
void spotflow_mqtt_publish(void* pvParameters)
{
	const EventBits_t ALL_BITS = SPOTFLOW_MQTT_NOTIFY_COREDUMP |
		SPOTFLOW_MQTT_NOTIFY_CONFIG_MSG |
		SPOTFLOW_MQTT_NOTIFY_LOGS;

	while (1) {
		EventBits_t notify_value = xEventGroupWaitBits(
			spotflow_mqtt_event_group,
			ALL_BITS,
			pdFALSE,	// Do not clear bits
			pdFALSE,	// wait for ANY bit
			portMAX_DELAY
		);
		EventBits_t clear_mask = 0;
		SPOTFLOW_LOG("Received notification value: %lu\n", notify_value);
		// Only proceed if the outbox_size i.e. current message is smaller than overall mqtt_buffer.
		// 2️⃣ Wait until MQTT outbox has space (zero polling)

		while (esp_mqtt_client_get_outbox_size(spotflow_client) >=
				CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN)
			{
				ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
			}

		if ((esp_mqtt_client_get_outbox_size(spotflow_client) <
			 CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN)) {
#ifdef CONFIG_ESP_COREDUMP_ENABLE
			// Try to send coredump messages first
			if (notify_value & SPOTFLOW_MQTT_NOTIFY_COREDUMP) {
				if(spotflow_coredump_send_message() == 5) {
					clear_mask |= SPOTFLOW_MQTT_NOTIFY_COREDUMP;
				}
			}
			// If no coredump pending, send regular log messages
#endif
			if (notify_value & SPOTFLOW_MQTT_NOTIFY_CONFIG_MSG) {
				spotflow_config_send_pending_message();
				clear_mask |= SPOTFLOW_MQTT_NOTIFY_CONFIG_MSG;
			}
			if (notify_value & SPOTFLOW_MQTT_NOTIFY_LOGS) {
				if (spotflow_logging_send_message() == 5) {
				clear_mask |= SPOTFLOW_MQTT_NOTIFY_LOGS;
				}
			}
			xEventGroupClearBits(spotflow_mqtt_event_group, clear_mask);
		} else {
			SPOTFLOW_LOG("MQTT outbox not empty; waiting for messages to be sent.\n");
			SPOTFLOW_LOG("MQTT Size %d \n",
					 esp_mqtt_client_get_outbox_size(spotflow_client));
		}
	}
}

/**
 * @brief
 *
 * @param client
 * @param topic
 * @param qos
 * @return int
 */
int spotflow_mqtt_subscribe(esp_mqtt_client_handle_t client, const char* topic, int qos)
{
	if (qos < 0 || qos > 2) {
		SPOTFLOW_LOG("Invalid QOS %d\n", qos);
		return ESP_FAIL;
	}
	if (client == NULL || topic == NULL) {
		return ESP_FAIL;
	}

	int msg_id = esp_mqtt_client_subscribe(client, topic, qos);

	if (msg_id < 0) {
		SPOTFLOW_LOG("MQTT subscribe FAILED for topic %s (qos=%d)\n", topic, qos);
	} else {
		SPOTFLOW_LOG("MQTT subscribe OK: msg_id=%d topic=%s qos=%d \n", msg_id, topic, qos);
	}
	return msg_id;
}

/**
 * @brief
 *
 */
static uint8_t* msg_buffer = NULL;
static int msg_len = 0;
static int msg_expected = 0;
static char topic_buf[256];
static int topic_len = 0;
void spotflow_mqtt_handle_data(esp_mqtt_event_handle_t event)
{
	// First chunk
	if (event->current_data_offset == 0) {
		// Topic copy
		topic_len = event->topic_len;
		memcpy(topic_buf, event->topic, event->topic_len);
		topic_buf[topic_len] = '\0';

		// Full message expected length
		msg_expected = event->total_data_len;
		msg_len = 0;

		if (msg_buffer) {
			free(msg_buffer);
		}

		// Allocate ONLY the needed size
		msg_buffer = malloc(msg_expected);
		if (!msg_buffer) {
			SPOTFLOW_LOG("Failed to allocate MQTT buffer");
			return;
		}
	}

	// Append this chunk
	memcpy(msg_buffer + msg_len, event->data, event->data_len);
	msg_len += event->data_len;

	// Complete message received?
	if (msg_len == msg_expected) {
		// Call your full-message handler
		spotflow_mqtt_on_message(topic_buf, topic_len, msg_buffer, msg_expected);

		// Cleanup buffer
		free(msg_buffer);
		msg_buffer = NULL;
	}
}

/**
 * @brief
 *
 * @param topic
 * @param topic_len
 * @param data
 * @param data_len
 */
void spotflow_mqtt_on_message(const char* topic, int topic_len, const uint8_t* data, int data_len)
{
	SPOTFLOW_LOG("MQTT Message Received on topic: %.*s", topic_len, topic);

	// Compare topic exactly
	if (strstr(topic, SPOTFLOW_MQTT_CONFIG_CBOR_C2D_TOPIC) != NULL) {
		// Your config handling
		SPOTFLOW_LOG("Dispatching to config handler...\n");
		spotflow_config_desired_message(data, data_len);
		return;
	}

	// Unknown topic
	SPOTFLOW_LOG("WARNING: Unhandled topic: %.*s", topic_len, topic);
}
/**
 * @brief
 *
 * @param topic
 * @param data
 * @param len
 * @param qos
 * @return int
 */
int spotflow_mqtt_publish_messgae(const char* topic, const uint8_t* data, int len, int qos)
{
	int msg_id =
		esp_mqtt_client_publish(spotflow_client, topic, (const char*)data, len, qos, 0);

	if (msg_id < 0) {
		SPOTFLOW_LOG("Error %d occurred sending MQTT (log). Retrying\n", msg_id);
		return -1;
	} else {
		SPOTFLOW_LOG("Log message sent successfully topic %s.\n",
				 topic);
		return 0;
	}
}
/**
 * @brief
 *
 * @param action_type
 */
void spotflow_mqtt_notify_action(uint32_t action_type)
{
	 xEventGroupSetBits(spotflow_mqtt_event_group, action_type);
}
