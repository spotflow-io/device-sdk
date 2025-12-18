#include "configs/spotflow_config_net.h"
#include "net/spotflow_mqtt.h"
#include "logging/spotflow_log_backend.h"

static uint8_t pending_message_buffer[SPOTFLOW_CONFIG_RESPONSE_MAX_LENGTH];
static size_t pending_message_length = 0;

int spotflow_config_prepare_pending_message(struct spotflow_config_reported_msg* reported_msg)
{
	int rc = spotflow_config_cbor_encode_reported(reported_msg, pending_message_buffer,
						      sizeof(pending_message_buffer),
						      &pending_message_length);
	if (rc == 0) {
		spotflow_mqtt_notify_action(SPOTFLOW_MQTT_NOTIFY_CONFIG_MSG);
	}
	return rc;
}

int spotflow_config_send_pending_message(void)
{
	int rc = spotflow_mqtt_publish_message(SPOTFLOW_MQTT_CONFIG_CBOR_D2C_TOPIC,
					       pending_message_buffer, pending_message_length,
					       SPOTFLOW_MQTT_CONFIG_CBOR_D2C_TOPIC_QOS);

	if (rc < 0) {
		return rc;
	}
	return 0;
}