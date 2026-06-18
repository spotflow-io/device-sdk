#include "net/spotflow_ble_transport_internal.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>

#include "config/spotflow_config.h"
#include "net/spotflow_device_id.h"
#include "net/spotflow_session_metadata.h"

LOG_MODULE_DECLARE(spotflow_net, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

#define SPOTFLOW_SERVICE_UUID_ENCODED \
	BT_UUID_128_ENCODE(0x26530001, 0x81E5, 0x4861, 0x82AE, 0x2C92E6887922)
#define SPOTFLOW_CAPABILITIES_UUID_ENCODED \
	BT_UUID_128_ENCODE(0x26530002, 0x81E5, 0x4861, 0x82AE, 0x2C92E6887922)
#define SPOTFLOW_DEVICE_ID_UUID_ENCODED \
	BT_UUID_128_ENCODE(0x26530003, 0x81E5, 0x4861, 0x82AE, 0x2C92E6887922)
#define SPOTFLOW_SESSION_METADATA_UUID_ENCODED \
	BT_UUID_128_ENCODE(0x26530004, 0x81E5, 0x4861, 0x82AE, 0x2C92E6887922)
#define SPOTFLOW_TX_STREAM_UUID_ENCODED \
	BT_UUID_128_ENCODE(0x26530005, 0x81E5, 0x4861, 0x82AE, 0x2C92E6887922)
#define SPOTFLOW_RX_STREAM_UUID_ENCODED \
	BT_UUID_128_ENCODE(0x26530006, 0x81E5, 0x4861, 0x82AE, 0x2C92E6887922)

static const struct bt_uuid_128 spotflow_service_uuid =
	BT_UUID_INIT_128(SPOTFLOW_SERVICE_UUID_ENCODED);
static const struct bt_uuid_128 spotflow_capabilities_uuid =
	BT_UUID_INIT_128(SPOTFLOW_CAPABILITIES_UUID_ENCODED);
static const struct bt_uuid_128 spotflow_device_id_uuid =
	BT_UUID_INIT_128(SPOTFLOW_DEVICE_ID_UUID_ENCODED);
static const struct bt_uuid_128 spotflow_session_metadata_uuid =
	BT_UUID_INIT_128(SPOTFLOW_SESSION_METADATA_UUID_ENCODED);
static const struct bt_uuid_128 spotflow_tx_stream_uuid =
	BT_UUID_INIT_128(SPOTFLOW_TX_STREAM_UUID_ENCODED);
static const struct bt_uuid_128 spotflow_rx_stream_uuid =
	BT_UUID_INIT_128(SPOTFLOW_RX_STREAM_UUID_ENCODED);

static const uint8_t capabilities[] = {
	SPOTFLOW_PROTOCOL_VERSION_MAJOR,
	SPOTFLOW_PROTOCOL_VERSION_MINOR,
};

/* advertising payload */
static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, SPOTFLOW_SERVICE_UUID_ENCODED),
};

/* scan response data */
static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, SPOTFLOW_BLE_DEVICE_NAME, SPOTFLOW_BLE_DEVICE_NAME_LEN),
};

static ssize_t read_capabilities(struct bt_conn* conn, const struct bt_gatt_attr* attr, void* buf,
				 uint16_t len, uint16_t offset);
static ssize_t read_device_id(struct bt_conn* conn, const struct bt_gatt_attr* attr, void* buf,
			      uint16_t len, uint16_t offset);
static ssize_t read_session_metadata(struct bt_conn* conn, const struct bt_gatt_attr* attr,
				     void* buf, uint16_t len, uint16_t offset);
static ssize_t write_rx_stream(struct bt_conn* conn, const struct bt_gatt_attr* attr,
			       const void* buf, uint16_t len, uint16_t offset, uint8_t flags);
static void tx_ccc_cfg_changed(const struct bt_gatt_attr* attr, uint16_t value);
static int start_advertising(void);
static void restart_advertising_work_handler(struct k_work* work);
static void connected(struct bt_conn* conn, uint8_t err);
static void disconnected(struct bt_conn* conn, uint8_t reason);

BT_GATT_SERVICE_DEFINE(spotflow_svc, BT_GATT_PRIMARY_SERVICE(&spotflow_service_uuid),
		       BT_GATT_CHARACTERISTIC(&spotflow_capabilities_uuid.uuid, BT_GATT_CHRC_READ,
					      BT_GATT_PERM_READ, read_capabilities, NULL, NULL),
		       BT_GATT_CHARACTERISTIC(&spotflow_device_id_uuid.uuid, BT_GATT_CHRC_READ,
					      BT_GATT_PERM_READ, read_device_id, NULL, NULL),
		       BT_GATT_CHARACTERISTIC(&spotflow_session_metadata_uuid.uuid,
					      BT_GATT_CHRC_READ, BT_GATT_PERM_READ,
					      read_session_metadata, NULL, NULL),
		       BT_GATT_CHARACTERISTIC(&spotflow_tx_stream_uuid.uuid, BT_GATT_CHRC_NOTIFY,
					      BT_GATT_PERM_NONE, NULL, NULL, NULL),
		       BT_GATT_CCC(tx_ccc_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
		       BT_GATT_CHARACTERISTIC(&spotflow_rx_stream_uuid.uuid,
					      BT_GATT_CHRC_WRITE_WITHOUT_RESP, BT_GATT_PERM_WRITE,
					      NULL, write_rx_stream, NULL));

BT_CONN_CB_DEFINE(spotflow_ble_conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

int spotflow_ble_transport_start_impl(void)
{
	if (!g_spotflow_ble_transport_state.initialized) {
		k_mutex_init(&g_spotflow_ble_transport_state.lock);
		k_work_init_delayable(&g_spotflow_ble_transport_state.restart_advertising_work,
				      restart_advertising_work_handler);
		g_spotflow_ble_transport_state.initialized = true;
	}

	if (!g_spotflow_ble_transport_state.bluetooth_enabled) {
		int rc = bt_enable(NULL);
		if (rc != 0 && rc != -EALREADY) {
			LOG_ERR("Failed to enable Bluetooth: %d", rc);
			return rc;
		}

		g_spotflow_ble_transport_state.bluetooth_enabled = true;
		LOG_INF("Bluetooth enabled for Spotflow BLE transport");
	}

	return start_advertising();
}

static ssize_t read_capabilities(struct bt_conn* conn, const struct bt_gatt_attr* attr, void* buf,
				 uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, capabilities, sizeof(capabilities));
}

static ssize_t read_device_id(struct bt_conn* conn, const struct bt_gatt_attr* attr, void* buf,
			      uint16_t len, uint16_t offset)
{
	const char* device_id = spotflow_get_device_id();

	return bt_gatt_attr_read(conn, attr, buf, len, offset, device_id, strlen(device_id));
}

static ssize_t read_session_metadata(struct bt_conn* conn, const struct bt_gatt_attr* attr,
				     void* buf, uint16_t len, uint16_t offset)
{
	uint8_t session_metadata[SPOTFLOW_SESSION_METADATA_BUFFER_SIZE];
	size_t session_metadata_len;
	int rc = spotflow_session_metadata_encode(session_metadata, sizeof(session_metadata),
						  &session_metadata_len);

	if (rc < 0) {
		return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
	}

	return bt_gatt_attr_read(conn, attr, buf, len, offset, session_metadata,
				 session_metadata_len);
}

static ssize_t write_rx_stream(struct bt_conn* conn, const struct bt_gatt_attr* attr,
			       const void* buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(offset);

	if ((flags & BT_GATT_WRITE_FLAG_CMD) == 0) {
		return BT_GATT_ERR(BT_ATT_ERR_WRITE_REQ_REJECTED);
	}

	int rc = spotflow_ble_transport_process_config_rx_frame(buf, len, flags);
	if (rc < 0) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}

	return len;
}

static void tx_ccc_cfg_changed(const struct bt_gatt_attr* attr, uint16_t value)
{
	ARG_UNUSED(attr);

	bool enabled = value == BT_GATT_CCC_NOTIFY;
	bool was_enabled;

	k_mutex_lock(&g_spotflow_ble_transport_state.lock, K_FOREVER);
	was_enabled = g_spotflow_ble_transport_state.tx.notifications_enabled;
	g_spotflow_ble_transport_state.tx.notifications_enabled = enabled;
	k_mutex_unlock(&g_spotflow_ble_transport_state.lock);

	LOG_INF("BLE TX Stream notifications %s", enabled ? "enabled" : "disabled");

	if (!was_enabled && enabled) {
		int rc = spotflow_config_init_session();
		if (rc < 0) {
			LOG_WRN("Failed to initialize configuration updating: %d", rc);
		}
	}
}

static int start_advertising(void)
{
	int rc = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

	if (rc == -EALREADY) {
		return 0;
	}

	if (rc == 0) {
		LOG_INF("BLE advertising started as %s", SPOTFLOW_BLE_DEVICE_NAME);
	}

	return rc;
}

static void restart_advertising_work_handler(struct k_work* work)
{
	ARG_UNUSED(work);

	k_mutex_lock(&g_spotflow_ble_transport_state.lock, K_FOREVER);
	bool connected = g_spotflow_ble_transport_state.tx.conn != NULL;
	k_mutex_unlock(&g_spotflow_ble_transport_state.lock);

	if (connected) {
		return;
	}

	int rc = start_advertising();
	if (rc != 0) {
		LOG_WRN("Failed to restart BLE advertising: %d", rc);
		(void)k_work_reschedule(&g_spotflow_ble_transport_state.restart_advertising_work,
					K_SECONDS(1));
	}
}

static void connected(struct bt_conn* conn, uint8_t err)
{
	if (err != 0) {
		LOG_WRN("BLE connection failed: 0x%02x", err);
		return;
	}

	k_mutex_lock(&g_spotflow_ble_transport_state.lock, K_FOREVER);
	if (g_spotflow_ble_transport_state.tx.conn != NULL) {
		bt_conn_unref(g_spotflow_ble_transport_state.tx.conn);
	}
	g_spotflow_ble_transport_state.tx.conn = bt_conn_ref(conn);
	k_mutex_unlock(&g_spotflow_ble_transport_state.lock);

	LOG_INF("BLE central connected");
}

static void disconnected(struct bt_conn* conn, uint8_t reason)
{
	ARG_UNUSED(conn);

	k_mutex_lock(&g_spotflow_ble_transport_state.lock, K_FOREVER);
	if (g_spotflow_ble_transport_state.tx.conn != NULL) {
		bt_conn_unref(g_spotflow_ble_transport_state.tx.conn);
		g_spotflow_ble_transport_state.tx.conn = NULL;
	}
	g_spotflow_ble_transport_state.tx.notifications_enabled = false;
	spotflow_ble_transport_reset_config_rx_state();
	k_mutex_unlock(&g_spotflow_ble_transport_state.lock);

	LOG_INF("BLE central disconnected: 0x%02x", reason);
	(void)k_work_reschedule(&g_spotflow_ble_transport_state.restart_advertising_work,
				K_MSEC(500));
}
