#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/ztest.h>

#include "net/transport/mqtt/spotflow_mqtt.h"
#include "ota/protocol/spotflow_ota_cbor.h"

LOG_MODULE_REGISTER(spotflow_net);

#define CONFIG_PAYLOAD_BUFFER_SIZE 32
#define UNEXPECTED_PAYLOAD_BUFFER_SIZE 64

enum publish_topic {
	PUBLISH_TOPIC_CONFIG,
	PUBLISH_TOPIC_OTA,
	PUBLISH_TOPIC_UNEXPECTED,
};

enum payload_read_behavior {
	PAYLOAD_READ_COMPLETE,
	PAYLOAD_READ_PARTIAL,
	PAYLOAD_READ_ZERO,
	PAYLOAD_READ_ERROR,
};

static enum payload_read_behavior read_behavior;
static size_t payload_bytes_read;
static size_t payload_read_calls;
static size_t config_callback_calls;
static size_t ota_callback_calls;
static size_t callback_payload_len;
static size_t qos1_ack_calls;
static uint16_t acknowledged_message_id;

int __wrap_mqtt_read_publish_payload_blocking(struct mqtt_client* client, void* buffer,
					      size_t length)
{
	ARG_UNUSED(client);

	payload_read_calls++;
	if (read_behavior == PAYLOAD_READ_ZERO) {
		return 0;
	}
	if (read_behavior == PAYLOAD_READ_ERROR) {
		return -ECONNRESET;
	}

	size_t bytes_to_read = length;
	if (read_behavior == PAYLOAD_READ_PARTIAL) {
		bytes_to_read = MIN(bytes_to_read, 3);
	}

	for (size_t i = 0; i < bytes_to_read; i++) {
		((uint8_t*)buffer)[i] = (uint8_t)(payload_bytes_read + i);
	}
	payload_bytes_read += bytes_to_read;
	return (int)bytes_to_read;
}

int __wrap_mqtt_publish_qos1_ack(struct mqtt_client* client, const struct mqtt_puback_param* param)
{
	ARG_UNUSED(client);

	qos1_ack_calls++;
	acknowledged_message_id = param->message_id;
	return 0;
}

static void verify_callback_payload(uint8_t* payload, size_t len)
{
	callback_payload_len = len;
	for (size_t i = 0; i < len; i++) {
		zassert_equal(payload[i], (uint8_t)i, "Unexpected payload byte at index %u",
			      (unsigned int)i);
	}
}

static void config_callback(uint8_t* payload, size_t len)
{
	config_callback_calls++;
	verify_callback_payload(payload, len);
}

static void ota_callback(uint8_t* payload, size_t len)
{
	ota_callback_calls++;
	verify_callback_payload(payload, len);
}

static void reset_publish_fakes(enum payload_read_behavior behavior)
{
	read_behavior = behavior;
	payload_bytes_read = 0;
	payload_read_calls = 0;
	config_callback_calls = 0;
	ota_callback_calls = 0;
	callback_payload_len = 0;
	qos1_ack_calls = 0;
	acknowledged_message_id = 0;
}

static void handle_publish(enum publish_topic topic, size_t payload_len,
			   enum payload_read_behavior behavior, enum mqtt_qos qos,
			   bool expect_callback)
{
	static const uint8_t config_topic[] = "config-cbor-c2d/device";
	static const uint8_t ota_topic[] = "ota-cbor-c2d/device";
	static const uint8_t unexpected_topic[] = "other/device";
	const uint8_t* topic_name = unexpected_topic;
	size_t topic_name_len = sizeof(unexpected_topic) - 1;

	if (topic == PUBLISH_TOPIC_CONFIG) {
		topic_name = config_topic;
		topic_name_len = sizeof(config_topic) - 1;
	} else if (topic == PUBLISH_TOPIC_OTA) {
		topic_name = ota_topic;
		topic_name_len = sizeof(ota_topic) - 1;
	}

	struct mqtt_evt event = {
		.type = MQTT_EVT_PUBLISH,
		.result = 0,
		.param.publish = {
			.message = {
				.topic = {
					.topic = { .utf8 = topic_name, .size = topic_name_len },
					.qos = qos,
				},
				.payload = { .len = payload_len },
			},
			.message_id = 42,
		},
	};

	reset_publish_fakes(behavior);
	spotflow_mqtt_test_handle_event(&event);

	bool read_succeeded = behavior == PAYLOAD_READ_COMPLETE || behavior == PAYLOAD_READ_PARTIAL;
	zassert_equal(payload_bytes_read, read_succeeded ? payload_len : 0,
		      "Publish payload was not fully consumed");
	zassert_true(payload_read_calls > 0);
	zassert_equal(config_callback_calls,
		      expect_callback && topic == PUBLISH_TOPIC_CONFIG ? 1 : 0);
	zassert_equal(ota_callback_calls, expect_callback && topic == PUBLISH_TOPIC_OTA ? 1 : 0);
	if (expect_callback) {
		zassert_equal(callback_payload_len, payload_len);
	}
	zassert_equal(qos1_ack_calls, read_succeeded && qos == MQTT_QOS_1_AT_LEAST_ONCE ? 1 : 0);
	if (qos1_ack_calls > 0) {
		zassert_equal(acknowledged_message_id, 42);
	}
}

static void prepare_and_handle_suback(uint8_t return_code)
{
	struct mqtt_evt event = {
		.type = MQTT_EVT_SUBACK,
		.result = 0,
		.param.suback = {
			.message_id = 42,
			.return_codes = {
				.data = &return_code,
				.len = 1,
			},
		},
	};

	spotflow_mqtt_test_prepare_connected();
	zassert_true(spotflow_mqtt_is_connected());
	spotflow_mqtt_test_handle_event(&event);
}

ZTEST(spotflow_ota_mqtt_suback, test_rejected_suback_aborts_connection)
{
	prepare_and_handle_suback(MQTT_SUBACK_FAILURE);

	zassert_false(spotflow_mqtt_is_connected());
}

ZTEST(spotflow_ota_mqtt_suback, test_successful_suback_keeps_connection)
{
	prepare_and_handle_suback(MQTT_SUBACK_SUCCESS_QoS_1);

	zassert_true(spotflow_mqtt_is_connected());
}

ZTEST(spotflow_ota_mqtt_suback, test_publish_payload_consumption_and_acknowledgment)
{
	static const enum publish_topic topics[] = {
		PUBLISH_TOPIC_CONFIG,
		PUBLISH_TOPIC_OTA,
		PUBLISH_TOPIC_UNEXPECTED,
	};
	static const enum mqtt_qos qos_levels[] = {
		MQTT_QOS_0_AT_MOST_ONCE,
		MQTT_QOS_1_AT_LEAST_ONCE,
	};

	for (size_t topic_index = 0; topic_index < ARRAY_SIZE(topics); topic_index++) {
		size_t buffer_size = UNEXPECTED_PAYLOAD_BUFFER_SIZE;
		if (topics[topic_index] == PUBLISH_TOPIC_CONFIG) {
			buffer_size = CONFIG_PAYLOAD_BUFFER_SIZE;
		} else if (topics[topic_index] == PUBLISH_TOPIC_OTA) {
			buffer_size = SPOTFLOW_OTA_CBOR_MAX_C2D_MESSAGE_SIZE;
		}

		for (size_t qos_index = 0; qos_index < ARRAY_SIZE(qos_levels); qos_index++) {
			bool has_callback = topics[topic_index] != PUBLISH_TOPIC_UNEXPECTED;

			handle_publish(topics[topic_index], buffer_size, PAYLOAD_READ_COMPLETE,
				       qos_levels[qos_index], has_callback);
			handle_publish(topics[topic_index], buffer_size + 1, PAYLOAD_READ_COMPLETE,
				       qos_levels[qos_index], false);
			handle_publish(topics[topic_index], buffer_size, PAYLOAD_READ_PARTIAL,
				       qos_levels[qos_index], has_callback);
			handle_publish(topics[topic_index], buffer_size, PAYLOAD_READ_ZERO,
				       qos_levels[qos_index], false);
			handle_publish(topics[topic_index], buffer_size, PAYLOAD_READ_ERROR,
				       qos_levels[qos_index], false);
		}
	}
}

static void before_each(void* fixture)
{
	ARG_UNUSED(fixture);

	spotflow_mqtt_test_set_message_callbacks(config_callback, ota_callback);
}

ZTEST_SUITE(spotflow_ota_mqtt_suback, NULL, NULL, before_each, NULL, NULL);
