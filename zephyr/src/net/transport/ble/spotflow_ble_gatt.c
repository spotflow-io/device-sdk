#include "spotflow_ble_transport_internal.h"

#ifdef CONFIG_SPOTFLOW_LOG_BACKEND
#include "config/spotflow_config.h"
#endif /* CONFIG_SPOTFLOW_LOG_BACKEND */
#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION
#include "metrics/system/spotflow_metrics_system.h"
#endif /* CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION */
#include "net/spotflow_device_id.h"
#ifdef CONFIG_SPOTFLOW_BLE_HAS_INGEST_KEY
#include "net/spotflow_ingest_key.h"
#endif /* CONFIG_SPOTFLOW_BLE_HAS_INGEST_KEY */
#include "net/spotflow_session_metadata.h"
#ifdef CONFIG_SPOTFLOW_OTA
#include "ota/spotflow_ota.h"
#endif /* CONFIG_SPOTFLOW_OTA */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/bluetooth/att.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>
#if IS_ENABLED(CONFIG_BT_SETTINGS)
#include <zephyr/settings/settings.h>
#endif /* CONFIG_BT_SETTINGS */

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
#define SPOTFLOW_INGEST_KEY_UUID_ENCODED \
	BT_UUID_128_ENCODE(0x26530007, 0x81E5, 0x4861, 0x82AE, 0x2C92E6887922)

#define SPOTFLOW_BLE_ENABLE_RETRY_COUNT 3
#define SPOTFLOW_BLE_ENABLE_RETRY_DELAY K_MSEC(200)

#if defined(CONFIG_SPOTFLOW_BLE_REQUIRE_ENCRYPTION)
#define SPOTFLOW_READ_PERMISSION BT_GATT_PERM_READ_ENCRYPT
#define SPOTFLOW_WRITE_PERMISSION BT_GATT_PERM_WRITE_ENCRYPT
#else
#define SPOTFLOW_READ_PERMISSION BT_GATT_PERM_READ
#define SPOTFLOW_WRITE_PERMISSION BT_GATT_PERM_WRITE
#endif /* defined(CONFIG_SPOTFLOW_BLE_REQUIRE_ENCRYPTION) */

#if defined(CONFIG_SPOTFLOW_BLE_HAS_INGEST_KEY) && !defined(CONFIG_SPOTFLOW_INGEST_KEY_DYNAMIC)
BUILD_ASSERT(sizeof(CONFIG_SPOTFLOW_INGEST_KEY) - 1 <= BT_ATT_MAX_ATTRIBUTE_LEN,
	     "Spotflow ingest key exceeds the maximum GATT attribute length");
#endif /* CONFIG_SPOTFLOW_BLE_HAS_INGEST_KEY && !CONFIG_SPOTFLOW_INGEST_KEY_DYNAMIC */

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

#ifdef CONFIG_SPOTFLOW_BLE_HAS_INGEST_KEY
static const struct bt_uuid_128 spotflow_ingest_key_uuid =
	BT_UUID_INIT_128(SPOTFLOW_INGEST_KEY_UUID_ENCODED);
static const char* spotflow_ingest_key;
static uint16_t spotflow_ingest_key_len;
#endif /* CONFIG_SPOTFLOW_BLE_HAS_INGEST_KEY */

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
static int enable_bluetooth(void);
static int start_advertising(void);
static void schedule_advertising_retry(void);
static void restart_advertising_work_handler(struct k_work* work);
static void connected(struct bt_conn* conn, uint8_t err);
static void disconnected(struct bt_conn* conn, uint8_t reason);
#ifdef CONFIG_SPOTFLOW_BLE_HAS_INGEST_KEY
static int init_ingest_key(void);
static ssize_t read_ingest_key(struct bt_conn* conn, const struct bt_gatt_attr* attr, void* buf,
			       uint16_t len, uint16_t offset);
#endif /* CONFIG_SPOTFLOW_BLE_HAS_INGEST_KEY */

BT_GATT_SERVICE_DEFINE(
	spotflow_svc, BT_GATT_PRIMARY_SERVICE(&spotflow_service_uuid),
	BT_GATT_CHARACTERISTIC(&spotflow_capabilities_uuid.uuid, BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, read_capabilities, NULL, NULL),
	BT_GATT_CHARACTERISTIC(&spotflow_device_id_uuid.uuid, BT_GATT_CHRC_READ,
			       SPOTFLOW_READ_PERMISSION, read_device_id, NULL, NULL),
	BT_GATT_CHARACTERISTIC(&spotflow_session_metadata_uuid.uuid, BT_GATT_CHRC_READ,
			       SPOTFLOW_READ_PERMISSION, read_session_metadata, NULL, NULL),
	BT_GATT_CHARACTERISTIC(&spotflow_tx_stream_uuid.uuid, BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_NONE, NULL, NULL, NULL),
	BT_GATT_CCC(tx_ccc_cfg_changed, SPOTFLOW_READ_PERMISSION | SPOTFLOW_WRITE_PERMISSION),
	BT_GATT_CHARACTERISTIC(&spotflow_rx_stream_uuid.uuid, BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       SPOTFLOW_WRITE_PERMISSION, NULL, write_rx_stream, NULL),
#ifdef CONFIG_SPOTFLOW_BLE_HAS_INGEST_KEY
	BT_GATT_CHARACTERISTIC(&spotflow_ingest_key_uuid.uuid, BT_GATT_CHRC_READ,
			       SPOTFLOW_READ_PERMISSION, read_ingest_key, NULL, NULL),
#endif /* CONFIG_SPOTFLOW_BLE_HAS_INGEST_KEY */
);

BT_CONN_CB_DEFINE(spotflow_ble_conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};

int spotflow_ble_transport_start_impl(void)
{
	if (!atomic_get(&g_spotflow_ble_transport_state.initialized)) {
		k_mutex_init(&g_spotflow_ble_transport_state.lock);
		k_work_init_delayable(&g_spotflow_ble_transport_state.restart_advertising_work,
				      restart_advertising_work_handler);
#ifdef CONFIG_SPOTFLOW_BLE_HAS_INGEST_KEY
		int rc = init_ingest_key();
		if (rc < 0) {
			return rc;
		}
#endif /* CONFIG_SPOTFLOW_BLE_HAS_INGEST_KEY */
#ifdef CONFIG_SPOTFLOW_OTA
		/* BLE keeps its OTA receiver across gateway reconnects. Install it
		 * on the processing thread before Bluetooth accepts connections.
		 */
		int ota_rc = spotflow_ota_init_session();
		if (ota_rc < 0) {
			/* Keep telemetry/configuration available. A gateway reconnect
			 * cannot repair local OTA startup; retry after reboot instead.
			 */
			LOG_ERR("BLE OTA unavailable until reboot: %d", ota_rc);
		}
#endif /* CONFIG_SPOTFLOW_OTA */
		/* Static Bluetooth callbacks may already be active on an app-owned stack. */
		atomic_set(&g_spotflow_ble_transport_state.initialized, 1);
	}

	if (!atomic_get(&g_spotflow_ble_transport_state.bluetooth_enabled)) {
		int rc = enable_bluetooth();
		if (rc != 0) {
			return rc;
		}
	}

	int rc = start_advertising();
	if (rc != 0) {
		LOG_WRN("Failed to start BLE advertising: %d", rc);
		schedule_advertising_retry();
	}

	return 0;
}

const struct bt_gatt_attr* spotflow_ble_tx_stream_attr_get(void)
{
	/* Attribute 8 is the TX Stream characteristic value in spotflow_svc. Keep
	 * the index lookup local to the GATT service definition instead of exposing
	 * it to framing/sender code.
	 */
	return &spotflow_svc.attrs[8];
}

#ifdef CONFIG_SPOTFLOW_BLE_HAS_INGEST_KEY
static int init_ingest_key(void)
{
	const char* key = spotflow_get_ingest_key();
	if (key == NULL) {
		LOG_WRN("Waiting for the Spotflow ingest key before starting BLE");
		do {
			k_sleep(K_SECONDS(1));
			key = spotflow_get_ingest_key();
		} while (key == NULL);
	}

	size_t len = strlen(key);
	if (len == 0 || len > BT_ATT_MAX_ATTRIBUTE_LEN) {
		LOG_ERR("Spotflow BLE ingest key must contain 1 to %u bytes",
			(unsigned int)BT_ATT_MAX_ATTRIBUTE_LEN);
		return -EINVAL;
	}
	spotflow_ingest_key = key;
	spotflow_ingest_key_len = len;
	return 0;
}

static ssize_t read_ingest_key(struct bt_conn* conn, const struct bt_gatt_attr* attr, void* buf,
			       uint16_t len, uint16_t offset)
{
	if (!atomic_get(&g_spotflow_ble_transport_state.initialized)) {
		return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
	}

	/* Zephyr permits a NULL connection for local reads. */
	if (attr == NULL || buf == NULL) {
		LOG_ERR("Invalid BLE ingest key read arguments");
		return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
	}

	return bt_gatt_attr_read(conn, attr, buf, len, offset, spotflow_ingest_key,
				 spotflow_ingest_key_len);
}
#endif /* CONFIG_SPOTFLOW_BLE_HAS_INGEST_KEY */

static ssize_t read_capabilities(struct bt_conn* conn, const struct bt_gatt_attr* attr, void* buf,
				 uint16_t len, uint16_t offset)
{
	if (!atomic_get(&g_spotflow_ble_transport_state.initialized)) {
		return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
	}

	k_mutex_lock(&g_spotflow_ble_transport_state.lock, K_FOREVER);
	bool ota_ready = g_spotflow_ble_transport_state.ota_callback != NULL;
	k_mutex_unlock(&g_spotflow_ble_transport_state.lock);
	const uint8_t capabilities[] = {
		SPOTFLOW_PROTOCOL_VERSION_MAJOR,
		SPOTFLOW_PROTOCOL_VERSION_MINOR,
		ota_ready ? BIT(0) : 0,
	};
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
	/* Serialize encoding and copying because the workspace is shared by all GATT reads. */
	static uint8_t session_metadata[CONFIG_SPOTFLOW_SESSION_METADATA_BUFFER_SIZE];
	static K_MUTEX_DEFINE(session_metadata_lock);
	size_t session_metadata_len;

	k_mutex_lock(&session_metadata_lock, K_FOREVER);
	int rc = spotflow_session_metadata_encode(session_metadata, sizeof(session_metadata),
						  &session_metadata_len);

	if (rc < 0) {
		k_mutex_unlock(&session_metadata_lock);
		return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
	}

	ssize_t read_len = bt_gatt_attr_read(conn, attr, buf, len, offset, session_metadata,
					     session_metadata_len);
	k_mutex_unlock(&session_metadata_lock);

	return read_len;
}

static ssize_t write_rx_stream(struct bt_conn* conn, const struct bt_gatt_attr* attr,
			       const void* buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(offset);

	if (!atomic_get(&g_spotflow_ble_transport_state.initialized)) {
		return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
	}

	if ((flags & BT_GATT_WRITE_FLAG_CMD) == 0) {
		return BT_GATT_ERR(BT_ATT_ERR_WRITE_REQ_REJECTED);
	}

	int rc = spotflow_ble_transport_queue_rx_frame(buf, len);
	if (rc < 0) {
		return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
	}

	return len;
}

static void tx_ccc_cfg_changed(const struct bt_gatt_attr* attr, uint16_t value)
{
	ARG_UNUSED(attr);

	if (!atomic_get(&g_spotflow_ble_transport_state.initialized)) {
		return;
	}

	bool enabled = value == BT_GATT_CCC_NOTIFY;
	bool was_enabled;

	k_mutex_lock(&g_spotflow_ble_transport_state.lock, K_FOREVER);
	was_enabled = g_spotflow_ble_transport_state.tx.notifications_enabled;
	g_spotflow_ble_transport_state.tx.notifications_enabled = enabled;
	k_mutex_unlock(&g_spotflow_ble_transport_state.lock);

#ifdef CONFIG_SPOTFLOW_LOG_BACKEND
	if (!was_enabled && enabled) {
		int rc = spotflow_config_init_session();
		if (rc < 0) {
			LOG_WRN("Failed to initialize configuration updating: %d", rc);
		}
	}
#endif /* CONFIG_SPOTFLOW_LOG_BACKEND */

	LOG_INF("BLE TX Stream notifications %s", enabled ? "enabled" : "disabled");

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION
	if (!was_enabled && enabled) {
		spotflow_metrics_system_report_connection_state(true);
	} else if (was_enabled && !enabled) {
		spotflow_metrics_system_report_connection_state(false);
	}
#endif
}

static int enable_bluetooth(void)
{
	int rc;

	for (int attempt = 0; attempt <= SPOTFLOW_BLE_ENABLE_RETRY_COUNT; attempt++) {
		rc = bt_enable(NULL);
		if (rc != -ENODEV) {
			break;
		}

		if (attempt < SPOTFLOW_BLE_ENABLE_RETRY_COUNT) {
			LOG_WRN("Bluetooth HCI device not ready, retrying: %d", rc);
			k_sleep(SPOTFLOW_BLE_ENABLE_RETRY_DELAY);
		}
	}

	if (rc != 0 && rc != -EALREADY) {
		LOG_ERR("Failed to enable Bluetooth: %d", rc);
		return rc;
	}

#if IS_ENABLED(CONFIG_BT_SETTINGS)
	rc = settings_load_subtree("bt");
	if (rc != 0) {
		LOG_ERR("Failed to load Bluetooth settings: %d", rc);
		return rc;
	}
#endif

	atomic_set(&g_spotflow_ble_transport_state.bluetooth_enabled, 1);
	LOG_INF("Bluetooth enabled for Spotflow BLE transport");

	return 0;
}

static int start_advertising(void)
{
	const struct bt_le_adv_param* conn_mode =
#ifdef BT_LE_ADV_CONN_FAST_1
		BT_LE_ADV_CONN_FAST_1;
#else
		/* to provide backward compatibility fro zephyr 3.7.0 */
		BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONNECTABLE, BT_GAP_ADV_FAST_INT_MIN_1,
				BT_GAP_ADV_FAST_INT_MAX_1, NULL);
#endif
	int rc = bt_le_adv_start(conn_mode, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

	if (rc == -EALREADY) {
		return 0;
	}

	if (rc == 0) {
		LOG_INF("BLE advertising started as %s", SPOTFLOW_BLE_DEVICE_NAME);
	}

	return rc;
}

static void schedule_advertising_retry(void)
{
	(void)k_work_reschedule(&g_spotflow_ble_transport_state.restart_advertising_work,
				K_SECONDS(1));
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
		schedule_advertising_retry();
	}
}

static void connected(struct bt_conn* conn, uint8_t err)
{
	if (!atomic_get(&g_spotflow_ble_transport_state.initialized)) {
		return;
	}

	if (err != 0) {
		LOG_WRN("BLE connection failed: 0x%02x", err);
		return;
	}

	k_mutex_lock(&g_spotflow_ble_transport_state.lock, K_FOREVER);
	if (g_spotflow_ble_transport_state.tx.conn != NULL) {
		bt_conn_unref(g_spotflow_ble_transport_state.tx.conn);
	}
	g_spotflow_ble_transport_state.tx.conn = bt_conn_ref(conn);
	g_spotflow_ble_transport_state.tx.connection_generation++;
	k_mutex_unlock(&g_spotflow_ble_transport_state.lock);

	LOG_INF("BLE central connected");
}

static void disconnected(struct bt_conn* conn, uint8_t reason)
{
	if (!atomic_get(&g_spotflow_ble_transport_state.initialized)) {
		return;
	}

	k_mutex_lock(&g_spotflow_ble_transport_state.lock, K_FOREVER);
	if (g_spotflow_ble_transport_state.tx.conn != conn) {
		k_mutex_unlock(&g_spotflow_ble_transport_state.lock);
		return;
	}

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION
	/* A BLE transport session starts when the gateway enables TX notifications. */
	bool had_session = g_spotflow_ble_transport_state.tx.notifications_enabled;
#endif /* CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION */

	bt_conn_unref(conn);
	g_spotflow_ble_transport_state.tx.conn = NULL;
	g_spotflow_ble_transport_state.tx.notifications_enabled = false;
	g_spotflow_ble_transport_state.tx.connection_generation++;
	/* Queued RX frames and partial messages are invalidated by the new generation. */
	k_mutex_unlock(&g_spotflow_ble_transport_state.lock);

	LOG_INF("BLE central disconnected: 0x%02x", reason);

#ifdef CONFIG_SPOTFLOW_METRICS_SYSTEM_CONNECTION
	if (had_session) {
		spotflow_metrics_system_report_connection_state(false);
	}
#endif

	if (atomic_get(&g_spotflow_ble_transport_state.bluetooth_enabled)) {
		(void)k_work_reschedule(&g_spotflow_ble_transport_state.restart_advertising_work,
					K_MSEC(500));
	}
}
