#include <stdint.h>

#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/ztest.h>

#include "net/transport/mqtt/spotflow_mqtt.h"

LOG_MODULE_REGISTER(spotflow_net);

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

ZTEST_SUITE(spotflow_ota_mqtt_suback, NULL, NULL, NULL, NULL, NULL);
