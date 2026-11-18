#include "config/spotflow_config.h"
#include "config/spotflow_config_cbor.h"
#include "config/spotflow_config_net.h"
#include "config/spotflow_config_persistence.h"
#include "logging/spotflow_log_level.h"
#include "net/transport/ble/spotflow_ble_transport_internal.h"

#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(spotflow_net, LOG_LEVEL_INF);

struct spotflow_ble_transport_state g_spotflow_ble_transport_state;
static K_SEM_DEFINE(configuration_applied, 0, 1);
static spotflow_transport_message_cb desired_handler;
static uint8_t log_level;
static int applied_count;
static int report_count;
static int subscription_count;
static int send_result;
static uint8_t last_report[SPOTFLOW_CONFIG_RESPONSE_MAX_LENGTH];
static size_t last_report_len;

static void before(void* fixture);
static void receive_configuration(uint8_t* payload, size_t len);
static void apply_configuration(void);
static void apply_version_only_configuration(void);
static void expect_report(bool acknowledged);

int spotflow_transport_subscribe_config(spotflow_transport_message_cb callback)
{
	desired_handler = callback;
	subscription_count++;
	g_spotflow_ble_transport_state.config_callback = receive_configuration;
	return 0;
}

int spotflow_ble_transport_send_control(bool accepted, uint8_t type, uint8_t sequence,
					uint32_t connection_generation)
{
	ARG_UNUSED(accepted);
	ARG_UNUSED(type);
	ARG_UNUSED(sequence);
	ARG_UNUSED(connection_generation);
	zassert_unreachable("Unacknowledged configuration must not send control frames");
	return 0;
}

int spotflow_transport_send_config_cbor(uint8_t* payload, size_t len)
{
	zassert_true(len <= sizeof(last_report));
	memcpy(last_report, payload, len);
	last_report_len = len;
	report_count++;
	return send_result;
}

void spotflow_transport_abort(void)
{
	zassert_unreachable("Temporary send failures must not abort the transport");
}

uint8_t spotflow_config_get_sent_log_level(void)
{
	return log_level;
}
void spotflow_config_init_sent_log_level(uint8_t level)
{
	log_level = level;
}
void spotflow_config_init_sent_log_level_default(void)
{
	log_level = LOG_LEVEL_INF;
}
void spotflow_config_set_sent_log_level(uint8_t level)
{
	log_level = level;
}
void spotflow_config_persistence_try_init(void) {}
void spotflow_config_persistence_try_load(struct spotflow_config_persisted_settings* settings)
{
	*settings = (struct spotflow_config_persisted_settings){ 0 };
}
void spotflow_config_persistence_try_save(struct spotflow_config_persisted_settings* settings)
{
	ARG_UNUSED(settings);
	applied_count++;
}

ZTEST_SUITE(ble_config, NULL, NULL, before, NULL, NULL);

ZTEST(ble_config, test_configuration_response_after_session_setup)
{
	zassert_ok(spotflow_config_init_session());
	apply_configuration();
	zassert_equal(applied_count, 1);
	zassert_equal(log_level, LOG_LEVEL_ERR);
	zassert_ok(spotflow_config_send_pending_message());
	expect_report(true);
}

ZTEST(ble_config, test_reconnect_report_replaces_pending_desired_version)
{
	zassert_ok(spotflow_config_init_session());
	apply_configuration();
	zassert_ok(spotflow_config_init_session());
	zassert_ok(spotflow_config_send_pending_message());
	expect_report(false);
	zassert_equal(subscription_count, 2);
	zassert_equal(applied_count, 1);
}

ZTEST(ble_config, test_reconnect_report_includes_severity_after_version_only_response)
{
	zassert_ok(spotflow_config_init_session());
	apply_version_only_configuration();
	zassert_ok(spotflow_config_init_session());
	zassert_ok(spotflow_config_send_pending_message());
	expect_report(false);
	zassert_ok(spotflow_config_send_pending_message());
	zassert_equal(report_count, 1);
	zassert_equal(subscription_count, 2);
}

ZTEST(ble_config, test_temporary_send_failure_retains_desired_response)
{
	zassert_ok(spotflow_config_init_session());
	apply_configuration();
	send_result = -EAGAIN;
	zassert_equal(spotflow_config_send_pending_message(), -EAGAIN);
	expect_report(true);
	send_result = 0;
	zassert_ok(spotflow_config_send_pending_message());
	expect_report(true);
	zassert_equal(report_count, 2);
}

static void before(void* fixture)
{
	ARG_UNUSED(fixture);
	k_mutex_init(&g_spotflow_ble_transport_state.lock);
	send_result = 0;
	/* Drain any report left by the preceding test. */
	zassert_ok(spotflow_config_send_pending_message());
	spotflow_config_init();
	k_sem_reset(&configuration_applied);
	applied_count = 0;
	report_count = 0;
	subscription_count = 0;
}

static void receive_configuration(uint8_t* payload, size_t len)
{
	desired_handler(payload, len);
	k_sem_give(&configuration_applied);
}

static void apply_configuration(void)
{
	uint8_t frame[] = {
		SPOTFLOW_MSG_DESIRED_CONFIGURATION,
		SPOTFLOW_FRAME_IS_FIRST | SPOTFLOW_FRAME_IS_LAST,
		/* Sequence number. */
		0,
		/* Complete CBOR payload length: uint16 LE. */
		8,
		0,
		/* CBOR map with three entries. */
		0xa3,
		/* messageType: UPDATE_DESIRED_CONFIGURATION. */
		0x00,
		0x03,
		/* minimalLogSeverity: 60 (ERROR). */
		0x10,
		0x18,
		0x3c,
		/* desiredConfigurationVersion: 1. */
		0x12,
		0x01,
	};
	zassert_ok(spotflow_ble_transport_queue_rx_frame(frame, sizeof(frame)));
	zassert_ok(k_sem_take(&configuration_applied, K_SECONDS(1)));
}

static void apply_version_only_configuration(void)
{
	uint8_t frame[] = {
		SPOTFLOW_MSG_DESIRED_CONFIGURATION,
		SPOTFLOW_FRAME_IS_FIRST | SPOTFLOW_FRAME_IS_LAST,
		/* Sequence number and complete payload length (uint16 LE). */
		1,
		5,
		0,
		/* CBOR map: messageType = UPDATE_DESIRED_CONFIGURATION,
		 * desiredConfigurationVersion = 1, with no severity field.
		 */
		0xa2,
		0x00,
		0x03,
		0x12,
		0x01,
	};
	zassert_ok(spotflow_ble_transport_queue_rx_frame(frame, sizeof(frame)));
	zassert_ok(k_sem_take(&configuration_applied, K_SECONDS(1)));
}

static void expect_report(bool acknowledged)
{
	/* Compare encoded reports: scheduling and version preservation are tested
	 * here, independently of the CBOR wire-vector tests.
	 */
	struct spotflow_config_reported_msg expected = {
		.contains_minimal_log_severity = true,
		.minimal_log_severity = spotflow_log_level_to_severity(log_level),
		.contains_compiled_minimal_log_severity = true,
		.compiled_minimal_log_severity =
			spotflow_log_level_to_severity(CONFIG_LOG_MAX_LEVEL),
		.contains_acked_desired_config_version = acknowledged,
		.acked_desired_config_version = 1,
	};
	uint8_t buffer[SPOTFLOW_CONFIG_RESPONSE_MAX_LENGTH];
	size_t len;
	zassert_ok(spotflow_config_cbor_encode_reported(&expected, buffer, sizeof(buffer), &len));
	zassert_equal(last_report_len, len);
	zassert_mem_equal(last_report, buffer, len);
}
