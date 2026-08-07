#include <errno.h>
#include <stdbool.h>

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include "net/transport/mqtt/spotflow_mqtt_session.h"

LOG_MODULE_REGISTER(spotflow_net);

static int ota_init_result;
static bool mqtt_connected;
static size_t abort_count;
static size_t ota_init_count;
static size_t process_count;

void wait_for_network(void) {}

int spotflow_tls_init(void)
{
	return 0;
}

int spotflow_session_metadata_send(void)
{
	return 0;
}

int spotflow_ota_init_session(void)
{
	ota_init_count++;
	return ota_init_result;
}

void spotflow_mqtt_establish_mqtt(void) {}

void spotflow_mqtt_abort_mqtt(void)
{
	abort_count++;
	mqtt_connected = false;
}

bool spotflow_mqtt_is_connected(void)
{
	return mqtt_connected;
}

int spotflow_mqtt_poll(void)
{
	return 0;
}

int spotflow_mqtt_send_live(void)
{
	return 0;
}

static int process_once(void)
{
	process_count++;
	mqtt_connected = false;
	return 0;
}

static void before_each(void* fixture)
{
	ARG_UNUSED(fixture);

	ota_init_result = 0;
	mqtt_connected = true;
	abort_count = 0;
	ota_init_count = 0;
	process_count = 0;
}

ZTEST(spotflow_ota_mqtt_session, test_transient_subscribe_failure_reconnects_and_retries)
{
	ota_init_result = -EAGAIN;

	spotflow_mqtt_session_test_process(process_once);

	zassert_equal(abort_count, 1);
	zassert_equal(ota_init_count, 1);
	zassert_equal(process_count, 0);

	ota_init_result = 0;
	mqtt_connected = true;
	spotflow_mqtt_session_test_process(process_once);

	zassert_equal(abort_count, 1);
	zassert_equal(ota_init_count, 2);
	zassert_equal(process_count, 1);
}

ZTEST_SUITE(spotflow_ota_mqtt_session, NULL, NULL, before_each, NULL, NULL);
