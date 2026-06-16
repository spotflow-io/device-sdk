#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include "net/spotflow_ble_transport.h"
#include "net/spotflow_device_id.h"
#include "net/spotflow_session_metadata.h"

LOG_MODULE_DECLARE(spotflow_net, CONFIG_SPOTFLOW_MODULE_DEFAULT_LOG_LEVEL);

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

#define SPOTFLOW_PROTOCOL_VERSION_MAJOR 0x01
#define SPOTFLOW_PROTOCOL_VERSION_MINOR 0x00

#define SPOTFLOW_MSG_TELEMETRY 0x02

#define SPOTFLOW_FRAME_IS_FIRST BIT(0)
#define SPOTFLOW_FRAME_IS_LAST BIT(1)

#define SESSION_METADATA_BUFFER_SIZE 64
#define TX_FRAME_BUFFER_SIZE 244

static const struct bt_uuid_128 spotflow_service_uuid =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x26530001, 0x81E5, 0x4861, 0x82AE, 0x2C92E6887922));
static const struct bt_uuid_128 spotflow_capabilities_uuid =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x26530002, 0x81E5, 0x4861, 0x82AE, 0x2C92E6887922));
static const struct bt_uuid_128 spotflow_device_id_uuid =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x26530003, 0x81E5, 0x4861, 0x82AE, 0x2C92E6887922));
static const struct bt_uuid_128 spotflow_session_metadata_uuid =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x26530004, 0x81E5, 0x4861, 0x82AE, 0x2C92E6887922));
static const struct bt_uuid_128 spotflow_tx_stream_uuid =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x26530005, 0x81E5, 0x4861, 0x82AE, 0x2C92E6887922));
static const struct bt_uuid_128 spotflow_rx_stream_uuid =
    BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x26530006, 0x81E5, 0x4861, 0x82AE, 0x2C92E6887922));

static const uint8_t capabilities[] = {
	SPOTFLOW_PROTOCOL_VERSION_MAJOR,
	SPOTFLOW_PROTOCOL_VERSION_MINOR,
};

struct spotflow_ble_transport_state {
	struct k_mutex lock;
	struct bt_conn* conn;
	bool initialized;
	bool bluetooth_enabled;
	bool tx_notifications_enabled;
	uint8_t telemetry_sequence;
	struct k_work_delayable restart_advertising_work;
};

static struct spotflow_ble_transport_state state;

static ssize_t read_capabilities(struct bt_conn* conn, const struct bt_gatt_attr* attr, void* buf,
				 uint16_t len, uint16_t offset);
static ssize_t read_device_id(struct bt_conn* conn, const struct bt_gatt_attr* attr, void* buf,
			      uint16_t len, uint16_t offset);
static ssize_t read_session_metadata(struct bt_conn* conn, const struct bt_gatt_attr* attr,
				     void* buf, uint16_t len, uint16_t offset);
static ssize_t write_rx_stream(struct bt_conn* conn, const struct bt_gatt_attr* attr,
			       const void* buf, uint16_t len, uint16_t offset, uint8_t flags);
static void tx_ccc_cfg_changed(const struct bt_gatt_attr* attr, uint16_t value);
static void restart_advertising_work_handler(struct k_work* work);
static int start_advertising(void);
static int notify_frame(struct bt_conn* conn, const uint8_t* frame, size_t frame_len);
static int map_notify_error(int rc);

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

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL,
		      BT_UUID_128_ENCODE(0x26530001, 0x81E5, 0x4861, 0x82AE, 0x2C92E6887922)),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

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
	uint8_t session_metadata[SESSION_METADATA_BUFFER_SIZE];
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
	ARG_UNUSED(buf);
	ARG_UNUSED(offset);

	if ((flags & BT_GATT_WRITE_FLAG_CMD) == 0) {
		return BT_GATT_ERR(BT_ATT_ERR_WRITE_REQ_REJECTED);
	}

	LOG_DBG("Ignored BLE RX stream write: %u bytes", len);
	return len;
}

static void tx_ccc_cfg_changed(const struct bt_gatt_attr* attr, uint16_t value)
{
	ARG_UNUSED(attr);

	bool enabled = value == BT_GATT_CCC_NOTIFY;

	k_mutex_lock(&state.lock, K_FOREVER);
	state.tx_notifications_enabled = enabled;
	k_mutex_unlock(&state.lock);

	LOG_INF("BLE TX Stream notifications %s", enabled ? "enabled" : "disabled");
}

static void connected(struct bt_conn* conn, uint8_t err)
{
	if (err != 0) {
		LOG_WRN("BLE connection failed: 0x%02x", err);
		return;
	}

	k_mutex_lock(&state.lock, K_FOREVER);
	if (state.conn != NULL) {
		bt_conn_unref(state.conn);
	}
	state.conn = bt_conn_ref(conn);
	k_mutex_unlock(&state.lock);

	LOG_INF("BLE central connected");
}

static void disconnected(struct bt_conn* conn, uint8_t reason)
{
	ARG_UNUSED(conn);

	k_mutex_lock(&state.lock, K_FOREVER);
	if (state.conn != NULL) {
		bt_conn_unref(state.conn);
		state.conn = NULL;
	}
	state.tx_notifications_enabled = false;
	k_mutex_unlock(&state.lock);

	LOG_INF("BLE central disconnected: 0x%02x", reason);
	(void)k_work_reschedule(&state.restart_advertising_work, K_MSEC(500));
}

BT_CONN_CB_DEFINE(spotflow_ble_conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

static int start_advertising(void)
{
	int rc = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

	if (rc == -EALREADY) {
		return 0;
	}

	if (rc == 0) {
		LOG_INF("BLE advertising started as %s", DEVICE_NAME);
	}

	return rc;
}

static void restart_advertising_work_handler(struct k_work* work)
{
	ARG_UNUSED(work);

	k_mutex_lock(&state.lock, K_FOREVER);
	bool connected = state.conn != NULL;
	k_mutex_unlock(&state.lock);

	if (connected) {
		return;
	}

	int rc = start_advertising();
	if (rc != 0) {
		LOG_WRN("Failed to restart BLE advertising: %d", rc);
		(void)k_work_reschedule(&state.restart_advertising_work, K_SECONDS(1));
	}
}

static int notify_frame(struct bt_conn* conn, const uint8_t* frame, size_t frame_len)
{
	return bt_gatt_notify(conn, &spotflow_svc.attrs[8], frame, frame_len);
}

static int map_notify_error(int rc)
{
	if (rc == -EAGAIN || rc == -EBUSY || rc == -ENOTCONN || rc == -ENOMEM) {
		return -EAGAIN;
	}

	return rc;
}

int spotflow_ble_transport_start(void)
{
	if (!state.initialized) {
		k_mutex_init(&state.lock);
		k_work_init_delayable(&state.restart_advertising_work,
				      restart_advertising_work_handler);
		state.initialized = true;
	}

	if (!state.bluetooth_enabled) {
		int rc = bt_enable(NULL);
		if (rc != 0 && rc != -EALREADY) {
			LOG_ERR("Failed to enable Bluetooth: %d", rc);
			return rc;
		}

		state.bluetooth_enabled = true;
		LOG_INF("Bluetooth enabled for Spotflow BLE transport");
	}

	return start_advertising();
}

bool spotflow_ble_transport_is_ready(void)
{
	k_mutex_lock(&state.lock, K_FOREVER);
	bool ready = state.conn != NULL && state.tx_notifications_enabled;
	k_mutex_unlock(&state.lock);

	return ready;
}

bool spotflow_ble_transport_supports_feature(enum spotflow_transport_feature feature)
{
	return feature == SPOTFLOW_TRANSPORT_FEATURE_LOGS;
}

int spotflow_ble_transport_send_ingest_cbor(uint8_t* payload, size_t len)
{
	struct bt_conn* conn = NULL;
	uint8_t sequence = 0;

	k_mutex_lock(&state.lock, K_FOREVER);
	if (state.conn != NULL && state.tx_notifications_enabled) {
		conn = bt_conn_ref(state.conn);
		sequence = state.telemetry_sequence++;
	}
	k_mutex_unlock(&state.lock);

	if (conn == NULL) {
		return -EAGAIN;
	}

	if (len > UINT16_MAX) {
		bt_conn_unref(conn);
		return -EMSGSIZE;
	}

	uint16_t mtu = bt_gatt_get_mtu(conn);
	size_t notify_payload_max = mtu > 3 ? mtu - 3 : 0;

	if (notify_payload_max <= 5) {
		bt_conn_unref(conn);
		return -EMSGSIZE;
	}

	uint8_t frame[TX_FRAME_BUFFER_SIZE];
	size_t frame_payload_max = MIN(notify_payload_max, sizeof(frame));
	size_t offset = 0;
	bool first = true;

	while (offset < len) {
		size_t header_len = first ? 5 : 3;
		size_t fragment_capacity = frame_payload_max - header_len;
		size_t remaining = len - offset;
		size_t fragment_len = MIN(fragment_capacity, remaining);
		bool last = (offset + fragment_len) == len;
		uint8_t flags = 0;

		if (first) {
			flags |= SPOTFLOW_FRAME_IS_FIRST;
		}
		if (last) {
			flags |= SPOTFLOW_FRAME_IS_LAST;
		}

		frame[0] = SPOTFLOW_MSG_TELEMETRY;
		frame[1] = flags;
		frame[2] = sequence;
		if (first) {
			sys_put_le16(len, &frame[3]);
			memcpy(&frame[5], &payload[offset], fragment_len);
		} else {
			memcpy(&frame[3], &payload[offset], fragment_len);
		}

		int rc = notify_frame(conn, frame, header_len + fragment_len);
		if (rc < 0) {
			bt_conn_unref(conn);
			return map_notify_error(rc);
		}

		offset += fragment_len;
		first = false;
	}

	bt_conn_unref(conn);
	return 0;
}

int spotflow_ble_transport_send_config_cbor(uint8_t* payload, size_t len)
{
	ARG_UNUSED(payload);
	ARG_UNUSED(len);

	return -ENOTSUP;
}

int spotflow_ble_transport_subscribe_config(spotflow_transport_message_cb callback)
{
	ARG_UNUSED(callback);

	return -ENOTSUP;
}

void spotflow_ble_transport_abort(void) {}
