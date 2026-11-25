#ifdef CONFIG_SPOTFLOW_BLE_HAS_INGEST_KEY
#include "net/spotflow_ingest_key.h"
#endif /* CONFIG_SPOTFLOW_BLE_HAS_INGEST_KEY */
#include "net/transport/ble/spotflow_ble_transport.h"
#include "net/transport/ble/spotflow_ble_transport_internal.h"

#include <string.h>

#include <zephyr/bluetooth/att.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/iterable_sections.h>
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

enum {
	CAPABILITIES_VALUE_INDEX = 2,
	TX_CCC_INDEX = 9,
	RX_VALUE_INDEX = 11,
	INGEST_KEY_VALUE_INDEX = 13,
};

static int config_calls;
static int ota_calls;
static int enable_calls;
static int advertise_calls;
static int config_error;
static int ota_error;
static bool app_bluetooth_enabled;

#ifdef CONFIG_SPOTFLOW_BLE_HAS_INGEST_KEY
static const char test_key[] = "a-key-longer-than-one-att-read-response";
static int key_requests;
static bool key_unavailable;
static bool key_invalid;
#endif /* CONFIG_SPOTFLOW_BLE_HAS_INGEST_KEY */

extern const struct bt_gatt_service_static spotflow_svc;
STRUCT_SECTION_START_EXTERN(bt_conn_cb);

static void before(void* fixture);
static void receive_message(uint8_t* payload, size_t len);
static const struct bt_gatt_attr* attr_at(size_t index);
static bool has_characteristic_uuid(const struct bt_gatt_attr* attr, uint32_t id);
static const struct bt_gatt_attr* characteristic_at(size_t index, uint32_t id);
static void set_notifications(bool enabled);
static void expect_capabilities(bool ota);
#ifdef CONFIG_SPOTFLOW_BLE_HAS_INGEST_KEY
static void expect_early_callbacks_ignored(void);
#endif /* CONFIG_SPOTFLOW_BLE_HAS_INGEST_KEY */

#ifdef CONFIG_SPOTFLOW_BLE_HAS_INGEST_KEY
const char* spotflow_get_ingest_key(void)
{
	zassert_equal(enable_calls, 0, "Resolve the key before enabling the BLE service");
	key_requests++;
	if (key_unavailable) {
		expect_early_callbacks_ignored();
		key_unavailable = false;
		return NULL;
	}
	return key_invalid ? "" : test_key;
}
#endif /* CONFIG_SPOTFLOW_BLE_HAS_INGEST_KEY */

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
	return app_bluetooth_enabled ? -EALREADY : 0;
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

ZTEST(ble_startup, test_ingest_key_characteristic)
{
#ifdef CONFIG_SPOTFLOW_BLE_HAS_INGEST_KEY
	app_bluetooth_enabled = true;
	key_unavailable = true;
	zassert_ok(spotflow_ble_transport_start());
	zassert_equal(key_requests, 2);
	zassert_equal(advertise_calls, 1);
	const struct bt_gatt_attr* key_attr = characteristic_at(INGEST_KEY_VALUE_INDEX, 0x26530007);
	zassert_not_null(key_attr->read);
	zassert_equal(key_attr->perm,
		      IS_ENABLED(CONFIG_SPOTFLOW_BLE_REQUIRE_ENCRYPTION) ? BT_GATT_PERM_READ_ENCRYPT
									 : BT_GATT_PERM_READ);
	zassert_is_null(key_attr->write);
	char value[sizeof(test_key) - 1];
	const size_t first_read_len = 22;
	zassert_equal(key_attr->read(NULL, key_attr, value, first_read_len, 0), first_read_len);
	const size_t remaining_len = sizeof(value) - first_read_len;
	zassert_equal(key_attr->read(NULL, key_attr, value + first_read_len, remaining_len,
				     first_read_len),
		      remaining_len);
	zassert_mem_equal(value, test_key, sizeof(value));
#else
	zassert_ok(spotflow_ble_transport_start());
	for (size_t i = 0; i < spotflow_svc.attr_count; i++) {
		zassert_false(has_characteristic_uuid(&spotflow_svc.attrs[i], 0x26530007),
			      "Unexpected ingest key at GATT attribute %zu", i);
	}
#endif /* CONFIG_SPOTFLOW_BLE_HAS_INGEST_KEY */
}

#ifdef CONFIG_SPOTFLOW_BLE_HAS_INGEST_KEY
ZTEST(ble_startup, test_invalid_key_keeps_callbacks_inactive)
{
	app_bluetooth_enabled = true;
	key_invalid = true;
	zassert_equal(spotflow_ble_transport_start(), -EINVAL);
	expect_early_callbacks_ignored();
}
#endif /* CONFIG_SPOTFLOW_BLE_HAS_INGEST_KEY */

ZTEST(ble_startup, test_only_tracked_disconnect_restarts_advertising)
{
	const struct bt_conn_cb* callbacks = STRUCT_SECTION_START(bt_conn_cb);
	uint64_t connection_token;
	uint64_t other_connection_token;
	struct bt_conn* conn = (struct bt_conn*)&connection_token;
	struct bt_conn* other_conn = (struct bt_conn*)&other_connection_token;
	struct spotflow_ble_transport_state* state = &g_spotflow_ble_transport_state;

	zassert_ok(spotflow_ble_transport_start());
	callbacks->disconnected(other_conn, 0);
	zassert_equal(state->tx.connection_generation, 0);
	zassert_false(k_work_delayable_is_pending(&state->restart_advertising_work));

	callbacks->connected(conn, 0);
	callbacks->disconnected(other_conn, 0);
	zassert_true(state->tx.conn == conn);
	zassert_false(k_work_delayable_is_pending(&state->restart_advertising_work));

	callbacks->disconnected(conn, 0);
	zassert_is_null(state->tx.conn);
	zassert_true(k_work_delayable_is_pending(&state->restart_advertising_work));
	(void)k_work_cancel_delayable(&state->restart_advertising_work);
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
	app_bluetooth_enabled = false;
#ifdef CONFIG_SPOTFLOW_BLE_HAS_INGEST_KEY
	key_requests = 0;
	key_unavailable = false;
	key_invalid = false;
#endif /* CONFIG_SPOTFLOW_BLE_HAS_INGEST_KEY */
}

static void receive_message(uint8_t* payload, size_t len)
{
	ARG_UNUSED(payload);
	ARG_UNUSED(len);
}

static const struct bt_gatt_attr* attr_at(size_t index)
{
	zassert_true(index < spotflow_svc.attr_count, "Missing GATT attribute %zu", index);
	return &spotflow_svc.attrs[index];
}

static bool has_characteristic_uuid(const struct bt_gatt_attr* attr, uint32_t id)
{
	const struct bt_uuid_128 uuid =
		BT_UUID_INIT_128(BT_UUID_128_ENCODE(id, 0x81E5, 0x4861, 0x82AE, 0x2C92E6887922));
	return attr->uuid->type == BT_UUID_TYPE_128 &&
		memcmp(BT_UUID_128(attr->uuid)->val, uuid.val, sizeof(uuid.val)) == 0;
}

static const struct bt_gatt_attr* characteristic_at(size_t index, uint32_t id)
{
	const struct bt_gatt_attr* attr = attr_at(index);
	zassert_true(has_characteristic_uuid(attr, id), "Unexpected UUID at GATT attribute %zu",
		     index);
	return attr;
}

static void set_notifications(bool enabled)
{
	const struct bt_gatt_attr* ccc_attr = attr_at(TX_CCC_INDEX);
	zassert_equal(ccc_attr->uuid->type, BT_UUID_TYPE_16);
	zassert_equal(BT_UUID_16(ccc_attr->uuid)->val, BT_UUID_GATT_CCC_VAL);
#ifdef _bt_gatt_ccc
	struct bt_gatt_ccc_managed_user_data* ccc = ccc_attr->user_data;
#else
	struct _bt_gatt_ccc* ccc = ccc_attr->user_data;
#endif /* _bt_gatt_ccc */
	zassert_not_null(ccc);
	zassert_not_null(ccc->cfg_changed);
	ccc->cfg_changed(ccc_attr, enabled ? BT_GATT_CCC_NOTIFY : 0);
}

static void expect_capabilities(bool ota)
{
	const struct bt_gatt_attr* attr = characteristic_at(CAPABILITIES_VALUE_INDEX, 0x26530002);
	uint8_t capabilities[3];
	zassert_not_null(attr->read);
	zassert_equal(attr->read(NULL, attr, capabilities, sizeof(capabilities), 0), 3);
	zassert_equal(capabilities[2], ota ? BIT(0) : 0);
}

#ifdef CONFIG_SPOTFLOW_BLE_HAS_INGEST_KEY
static void expect_early_callbacks_ignored(void)
{
	const struct bt_conn_cb* callbacks = STRUCT_SECTION_START(bt_conn_cb);
	uint64_t connection_token;
	struct bt_conn* conn = (struct bt_conn*)&connection_token;
	struct spotflow_ble_transport_state* state = &g_spotflow_ble_transport_state;
	const struct bt_gatt_attr* key_attr = characteristic_at(INGEST_KEY_VALUE_INDEX, 0x26530007);
	const struct bt_gatt_attr* capabilities_attr =
		characteristic_at(CAPABILITIES_VALUE_INDEX, 0x26530002);
	const struct bt_gatt_attr* rx_attr = characteristic_at(RX_VALUE_INDEX, 0x26530006);
	uint8_t buf[3] = { 0 };

	zassert_not_null(key_attr->read);
	zassert_not_null(capabilities_attr->read);
	zassert_not_null(rx_attr->write);
	callbacks->connected(conn, 0);
	zassert_is_null(state->tx.conn);
	set_notifications(true);
	zassert_false(state->tx.notifications_enabled);
	zassert_equal(config_calls, 0);
	zassert_equal(capabilities_attr->read(conn, capabilities_attr, buf, sizeof(buf), 0),
		      BT_GATT_ERR(BT_ATT_ERR_UNLIKELY));
	zassert_equal(key_attr->read(conn, key_attr, buf, sizeof(buf), 0),
		      BT_GATT_ERR(BT_ATT_ERR_UNLIKELY));
	zassert_equal(rx_attr->write(conn, rx_attr, buf, sizeof(buf), 0, BT_GATT_WRITE_FLAG_CMD),
		      BT_GATT_ERR(BT_ATT_ERR_UNLIKELY));
	callbacks->disconnected(conn, 0);
	zassert_equal(state->tx.connection_generation, 0);
	zassert_false(k_work_delayable_is_pending(&state->restart_advertising_work));
	zassert_equal(advertise_calls, 0);
}
#endif /* CONFIG_SPOTFLOW_BLE_HAS_INGEST_KEY */
