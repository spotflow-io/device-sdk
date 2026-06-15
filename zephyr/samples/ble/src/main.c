#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zcbor_encode.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(spotflow_ble_sample, LOG_LEVEL_INF);

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

#define SPOTFLOW_PROTOCOL_VERSION_MAJOR 0x01
#define SPOTFLOW_PROTOCOL_VERSION_MINOR 0x00

#define SPOTFLOW_MSG_ACK 0x00
#define SPOTFLOW_MSG_NACK 0x01
#define SPOTFLOW_MSG_TELEMETRY 0x02

#define SPOTFLOW_FRAME_IS_FIRST BIT(0)
#define SPOTFLOW_FRAME_IS_LAST BIT(1)

#define SPOTFLOW_LOG_MESSAGE_TYPE 0
#define SPOTFLOW_SESSION_METADATA_MESSAGE_TYPE 1

#define SPOTFLOW_KEY_MESSAGE_TYPE 0x00
#define SPOTFLOW_KEY_BODY 0x01
#define SPOTFLOW_KEY_SEVERITY 0x04
#define SPOTFLOW_KEY_LABELS 0x05
#define SPOTFLOW_KEY_DEVICE_UPTIME_MS 0x06
#define SPOTFLOW_KEY_SEQUENCE_NUMBER 0x0D
#define SPOTFLOW_KEY_DEVICE_RUN_ID 0x1E

#define SPOTFLOW_LOG_SEVERITY_INFO 40

#define DEVICE_ID_MAX_BYTES 16
#define DEVICE_ID_HEX_BUFFER_SIZE ((2 * DEVICE_ID_MAX_BYTES) + 1)
#define SESSION_METADATA_BUFFER_SIZE 32
#define LOG_CBOR_BUFFER_SIZE 128
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

static struct bt_conn* active_conn;
static bool tx_notifications_enabled;
static uint8_t telemetry_sequence;
static uint32_t log_sequence;
static uint64_t device_run_id;
static char device_id_buffer[DEVICE_ID_HEX_BUFFER_SIZE];

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

static void init_device_id(void)
{
	uint8_t device_id[DEVICE_ID_MAX_BYTES];
	ssize_t ret = hwinfo_get_device_id(device_id, sizeof(device_id));

	if (ret <= 0) {
		strncpy(device_id_buffer, "brd2605a-ble-sample", sizeof(device_id_buffer));
		device_id_buffer[sizeof(device_id_buffer) - 1] = '\0';
		return;
	}

	for (int i = 0; i < ret; i++) {
		snprintk(device_id_buffer + (2 * i), 3, "%02X", device_id[i]);
	}
}

static ssize_t read_device_id(struct bt_conn* conn, const struct bt_gatt_attr* attr, void* buf,
			      uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, device_id_buffer,
				 strlen(device_id_buffer));
}

static int encode_session_metadata(uint8_t* buffer, size_t buffer_len, size_t* encoded_len)
{
	ZCBOR_STATE_E(state, 1, buffer, buffer_len, 1);

	bool succ = zcbor_map_start_encode(state, 2);

	succ = succ && zcbor_uint32_put(state, SPOTFLOW_KEY_MESSAGE_TYPE);
	succ = succ && zcbor_uint32_put(state, SPOTFLOW_SESSION_METADATA_MESSAGE_TYPE);
	succ = succ && zcbor_uint32_put(state, SPOTFLOW_KEY_DEVICE_RUN_ID);
	succ = succ && zcbor_uint64_put(state, device_run_id);
	succ = succ && zcbor_map_end_encode(state, 2);

	if (!succ) {
		return -EINVAL;
	}

	*encoded_len = state->payload - buffer;
	return 0;
}

static ssize_t read_session_metadata(struct bt_conn* conn, const struct bt_gatt_attr* attr,
				     void* buf, uint16_t len, uint16_t offset)
{
	uint8_t session_metadata[SESSION_METADATA_BUFFER_SIZE];
	size_t session_metadata_len;
	int rc = encode_session_metadata(session_metadata, sizeof(session_metadata),
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

	LOG_DBG("Ignored RX stream write: %u bytes", len);
	printk("Spotflow BLE: ignored RX stream write: %u bytes\n", len);
	return len;
}

static void tx_ccc_cfg_changed(const struct bt_gatt_attr* attr, uint16_t value)
{
	ARG_UNUSED(attr);

	tx_notifications_enabled = (value == BT_GATT_CCC_NOTIFY);
	LOG_INF("TX Stream notifications %s", tx_notifications_enabled ? "enabled" : "disabled");
	printk("Spotflow BLE: TX Stream notifications %s\n",
	       tx_notifications_enabled ? "enabled" : "disabled");
}

static int encode_hello_ble_log(uint8_t* buffer, size_t buffer_len, size_t* encoded_len)
{
	ZCBOR_STATE_E(state, 2, buffer, buffer_len, 1);

	uint32_t uptime_ms = k_uptime_get_32();
	bool succ = zcbor_map_start_encode(state, 6);

	succ = succ && zcbor_uint32_put(state, SPOTFLOW_KEY_MESSAGE_TYPE);
	succ = succ && zcbor_uint32_put(state, SPOTFLOW_LOG_MESSAGE_TYPE);
	succ = succ && zcbor_uint32_put(state, SPOTFLOW_KEY_SEQUENCE_NUMBER);
	succ = succ && zcbor_uint32_put(state, log_sequence++);
	succ = succ && zcbor_uint32_put(state, SPOTFLOW_KEY_SEVERITY);
	succ = succ && zcbor_uint32_put(state, SPOTFLOW_LOG_SEVERITY_INFO);
	succ = succ && zcbor_uint32_put(state, SPOTFLOW_KEY_DEVICE_UPTIME_MS);
	succ = succ && zcbor_uint32_put(state, uptime_ms);
	succ = succ && zcbor_uint32_put(state, SPOTFLOW_KEY_LABELS);
	succ = succ && zcbor_map_start_encode(state, 1);
	succ = succ && zcbor_tstr_put_lit(state, "source");
	succ = succ && zcbor_tstr_put_lit(state, "ble_sample");
	succ = succ && zcbor_map_end_encode(state, 1);
	succ = succ && zcbor_uint32_put(state, SPOTFLOW_KEY_BODY);
	succ = succ && zcbor_tstr_put_lit(state, "hello ble");
	succ = succ && zcbor_map_end_encode(state, 6);

	if (!succ) {
		return -EINVAL;
	}

	*encoded_len = state->payload - buffer;
	return 0;
}

static int notify_frame(const uint8_t* frame, size_t frame_len)
{
	/* Attribute 8 is the TX Stream characteristic value in spotflow_svc. */
	return bt_gatt_notify(active_conn, &spotflow_svc.attrs[8], frame, frame_len);
}

static int send_telemetry(const uint8_t* payload, size_t payload_len)
{
	if (active_conn == NULL || !tx_notifications_enabled) {
		return -ENOTCONN;
	}

	if (payload_len > UINT16_MAX) {
		return -EMSGSIZE;
	}

	uint16_t mtu = bt_gatt_get_mtu(active_conn);
	size_t notify_payload_max = mtu > 3 ? mtu - 3 : 0;

	if (notify_payload_max <= 5) {
		return -EMSGSIZE;
	}

	uint8_t frame[TX_FRAME_BUFFER_SIZE];
	size_t frame_payload_max = MIN(notify_payload_max, sizeof(frame));
	uint8_t sequence = telemetry_sequence++;
	size_t offset = 0;
	bool first = true;

	while (offset < payload_len) {
		size_t header_len = first ? 5 : 3;
		size_t fragment_capacity = frame_payload_max - header_len;
		size_t remaining = payload_len - offset;
		size_t fragment_len = MIN(fragment_capacity, remaining);
		bool last = (offset + fragment_len) == payload_len;
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
			sys_put_le16(payload_len, &frame[3]);
			memcpy(&frame[5], &payload[offset], fragment_len);
		} else {
			memcpy(&frame[3], &payload[offset], fragment_len);
		}

		int rc = notify_frame(frame, header_len + fragment_len);
		if (rc < 0) {
			return rc;
		}

		offset += fragment_len;
		first = false;
	}

	return 0;
}

static void connected(struct bt_conn* conn, uint8_t err)
{
	if (err) {
		LOG_WRN("Connection failed: 0x%02x", err);
		printk("Spotflow BLE: connection failed: 0x%02x\n", err);
		return;
	}

	active_conn = bt_conn_ref(conn);
	LOG_INF("Connected");
	printk("Spotflow BLE: connected\n");
}

static void disconnected(struct bt_conn* conn, uint8_t reason)
{
	ARG_UNUSED(conn);

	if (active_conn != NULL) {
		bt_conn_unref(active_conn);
		active_conn = NULL;
	}

	tx_notifications_enabled = false;
	LOG_INF("Disconnected: 0x%02x", reason);
	printk("Spotflow BLE: disconnected: 0x%02x\n", reason);

	int rc = start_advertising();
	if (rc != 0) {
		LOG_WRN("Failed to restart advertising: %d", rc);
		printk("Spotflow BLE: failed to restart advertising: %d\n", rc);
	}
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

static int start_advertising(void)
{
	int rc = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

	if (rc == 0) {
		LOG_INF("Advertising as %s, device ID %s", DEVICE_NAME, device_id_buffer);
		printk("Spotflow BLE: advertising as %s, device ID %s\n", DEVICE_NAME,
		       device_id_buffer);
	} else if (rc == -EALREADY) {
		LOG_INF("Advertising already active");
		printk("Spotflow BLE: advertising already active\n");
		rc = 0;
	}

	return rc;
}

int main(void)
{
	int rc;
	uint32_t wait_log_counter = 0;

	LOG_INF("Starting Spotflow BLE sample");
	printk("Spotflow BLE: starting sample\n");

	init_device_id();
	device_run_id = ((uint64_t)sys_rand32_get() << 32) | sys_rand32_get();
	printk("Spotflow BLE: device ID %s\n", device_id_buffer);
	printk("Spotflow BLE: device run ID 0x%08x%08x\n", (uint32_t)(device_run_id >> 32),
	       (uint32_t)device_run_id);

	rc = bt_enable(NULL);
	if (rc != 0) {
		LOG_ERR("Failed to enable Bluetooth: %d", rc);
		printk("Spotflow BLE: failed to enable Bluetooth: %d\n", rc);
		return rc;
	}
	printk("Spotflow BLE: Bluetooth enabled\n");

	rc = start_advertising();
	if (rc != 0) {
		LOG_ERR("Failed to start advertising: %d", rc);
		printk("Spotflow BLE: failed to start advertising: %d\n", rc);
		return rc;
	}

	while (true) {
		uint8_t log_cbor[LOG_CBOR_BUFFER_SIZE];
		size_t log_cbor_len;

		k_sleep(K_SECONDS(1));

		if (active_conn == NULL || !tx_notifications_enabled) {
			wait_log_counter++;
			if ((wait_log_counter % 5) == 0) {
				printk("Spotflow BLE: waiting, connected=%d subscribed=%d\n",
				       active_conn != NULL, tx_notifications_enabled);
			}
			continue;
		}
		wait_log_counter = 0;

		rc = encode_hello_ble_log(log_cbor, sizeof(log_cbor), &log_cbor_len);
		if (rc < 0) {
			LOG_WRN("Failed to encode log CBOR: %d", rc);
			printk("Spotflow BLE: failed to encode log CBOR: %d\n", rc);
			continue;
		}

		rc = send_telemetry(log_cbor, log_cbor_len);
		if (rc < 0) {
			LOG_WRN("Failed to notify telemetry: %d", rc);
			printk("Spotflow BLE: failed to notify telemetry: %d\n", rc);
		} else {
			printk("Spotflow BLE: sent hello ble CBOR log, %u bytes\n", log_cbor_len);
		}
	}

	return 0;
}
