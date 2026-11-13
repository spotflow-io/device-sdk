#include "net/transport/ble/spotflow_ble_transport.h"
#include "net/transport/ble/spotflow_ble_transport_internal.h"

#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

LOG_MODULE_REGISTER(spotflow_net, LOG_LEVEL_INF);

#define DEFINE_GATT_READ_FAKE(name)                                                    \
	ssize_t name(struct bt_conn* conn, const struct bt_gatt_attr* attr, void* buf, \
		     uint16_t len, uint16_t offset)                                    \
	{                                                                              \
		ARG_UNUSED(conn);                                                      \
		ARG_UNUSED(attr);                                                      \
		ARG_UNUSED(buf);                                                       \
		ARG_UNUSED(len);                                                       \
		ARG_UNUSED(offset);                                                    \
		return 0;                                                              \
	}

static int config_calls;
static int ota_calls;
static int enable_calls;
static int advertise_calls;
static int config_error;
static int ota_error;

static void before(void* fixture);
static void receive_message(uint8_t* payload, size_t len);
static void set_notifications(bool enabled);
static void expect_capabilities(bool ota);

int spotflow_config_init_session(void)
{
	config_calls++;
	if (config_error == 0) {
		zassert_ok(spotflow_ble_transport_subscribe_config(receive_message));
	}
	return config_error;
}

int spotflow_ota_init_session(void)
{
	ota_calls++;
	if (ota_error == 0) {
		zassert_ok(spotflow_ble_transport_subscribe_ota(receive_message));
	}
	return ota_error;
}

int bt_enable(bt_ready_cb_t cb)
{
	ARG_UNUSED(cb);
	/* OTA setup completes before Bluetooth accepts connections. Configuration
	 * retains its original notification-activation lifecycle.
	 */
	zassert_equal(config_calls, 0);
	zassert_equal(ota_calls, 1);
	zassert_is_null(g_spotflow_ble_transport_state.config_callback);
	enable_calls++;
	return 0;
}

int bt_le_adv_start(const struct bt_le_adv_param* param, const struct bt_data* ad, size_t ad_len,
		    const struct bt_data* sd, size_t sd_len)
{
	ARG_UNUSED(param);
	ARG_UNUSED(ad);
	ARG_UNUSED(ad_len);
	ARG_UNUSED(sd);
	ARG_UNUSED(sd_len);
	zassert_equal(enable_calls, 1);
	zassert_equal(ota_calls, 1);
	advertise_calls++;
	return 0;
}

struct bt_conn* bt_conn_ref(struct bt_conn* conn)
{
	return conn;
}
void bt_conn_unref(struct bt_conn* conn)
{
	ARG_UNUSED(conn);
}

ssize_t bt_gatt_attr_read(struct bt_conn* conn, const struct bt_gatt_attr* attr, void* buf,
			  uint16_t len, uint16_t offset, const void* value, uint16_t value_len)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	zassert_true(offset <= value_len);
	size_t count = MIN(len, value_len - offset);
	memcpy(buf, (const uint8_t*)value + offset, count);
	return count;
}

DEFINE_GATT_READ_FAKE(bt_gatt_attr_read_service)
DEFINE_GATT_READ_FAKE(bt_gatt_attr_read_chrc)
DEFINE_GATT_READ_FAKE(bt_gatt_attr_read_ccc)

ssize_t bt_gatt_attr_write_ccc(struct bt_conn* conn, const struct bt_gatt_attr* attr,
			       const void* buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(buf);
	ARG_UNUSED(offset);
	ARG_UNUSED(flags);
	return len;
}

int spotflow_ble_transport_queue_rx_frame(const void* buf, uint16_t len)
{
	ARG_UNUSED(buf);
	ARG_UNUSED(len);
	return 0;
}

const char* spotflow_get_device_id(void)
{
	return "test-device";
}

int spotflow_session_metadata_encode(uint8_t* buffer, size_t len, size_t* encoded_len)
{
	ARG_UNUSED(buffer);
	ARG_UNUSED(len);
	*encoded_len = 0;
	return 0;
}

ZTEST_SUITE(ble_startup, NULL, NULL, before, NULL, NULL);

ZTEST(ble_startup, test_ota_receiver_is_installed_before_bluetooth_and_advertising)
{
	zassert_ok(spotflow_ble_transport_start());
	zassert_equal(enable_calls, 1);
	zassert_equal(advertise_calls, 1);
	zassert_not_null(g_spotflow_ble_transport_state.ota_callback);
	expect_capabilities(true);
}

ZTEST(ble_startup, test_notification_sessions_initialize_configuration_only)
{
	zassert_ok(spotflow_ble_transport_start());
	set_notifications(true);
	set_notifications(true);
	zassert_equal(config_calls, 1);
	set_notifications(false);
	set_notifications(true);
	zassert_equal(config_calls, 2);
	zassert_equal(ota_calls, 1);
	zassert_ok(spotflow_ble_transport_start());
	zassert_equal(config_calls, 2);
	zassert_equal(ota_calls, 1);
}

ZTEST(ble_startup, test_ota_startup_failure_keeps_config_without_retry_or_ota_capability)
{
	ota_error = -EIO;
	zassert_ok(spotflow_ble_transport_start());
	zassert_equal(advertise_calls, 1);
	zassert_is_null(g_spotflow_ble_transport_state.ota_callback);
	expect_capabilities(false);
	ota_error = 0;
	set_notifications(true);
	set_notifications(false);
	set_notifications(true);
	zassert_ok(spotflow_ble_transport_start());
	zassert_equal(ota_calls, 1);
	zassert_equal(config_calls, 2);
	zassert_not_null(g_spotflow_ble_transport_state.config_callback);
	expect_capabilities(false);
}

ZTEST(ble_startup, test_failed_config_setup_retries_on_next_notification_session)
{
	config_error = -ENOMEM;
	zassert_ok(spotflow_ble_transport_start());
	set_notifications(true);
	zassert_equal(config_calls, 1);
	zassert_is_null(g_spotflow_ble_transport_state.config_callback);
	zassert_equal(advertise_calls, 1);
	config_error = 0;
	set_notifications(false);
	set_notifications(true);
	zassert_equal(config_calls, 2);
	zassert_not_null(g_spotflow_ble_transport_state.config_callback);
	zassert_equal(ota_calls, 1);
}

static void before(void* fixture)
{
	ARG_UNUSED(fixture);
	memset(&g_spotflow_ble_transport_state, 0, sizeof(g_spotflow_ble_transport_state));
	config_calls = 0;
	ota_calls = 0;
	enable_calls = 0;
	advertise_calls = 0;
	config_error = 0;
	ota_error = 0;
}

static void receive_message(uint8_t* payload, size_t len)
{
	ARG_UNUSED(payload);
	ARG_UNUSED(len);
}

static void set_notifications(bool enabled)
{
	const struct bt_gatt_attr* ccc_attr = spotflow_ble_tx_stream_attr_get() + 1;
#ifdef _bt_gatt_ccc
	struct bt_gatt_ccc_managed_user_data* ccc = ccc_attr->user_data;
#else
	struct _bt_gatt_ccc* ccc = ccc_attr->user_data;
#endif /* _bt_gatt_ccc */
	ccc->cfg_changed(ccc_attr, enabled ? BT_GATT_CCC_NOTIFY : 0);
}

static void expect_capabilities(bool ota)
{
	/* Capabilities is six attributes before the TX value in the service. */
	const struct bt_gatt_attr* attr = spotflow_ble_tx_stream_attr_get() - 6;
	uint8_t capabilities[3];
	zassert_equal(attr->read(NULL, attr, capabilities, sizeof(capabilities), 0), 3);
	zassert_equal(capabilities[2], ota ? BIT(0) : 0);
}
