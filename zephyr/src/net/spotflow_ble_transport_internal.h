#ifndef SPOTFLOW_BLE_TRANSPORT_INTERNAL_H
#define SPOTFLOW_BLE_TRANSPORT_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include <zephyr/bluetooth/conn.h>
#include <zephyr/kernel.h>

#include "net/spotflow_transport.h"

#define SPOTFLOW_BLE_DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define SPOTFLOW_BLE_DEVICE_NAME_LEN (sizeof(SPOTFLOW_BLE_DEVICE_NAME) - 1)

#define SPOTFLOW_PROTOCOL_VERSION_MAJOR 0x01
#define SPOTFLOW_PROTOCOL_VERSION_MINOR 0x00

#define SPOTFLOW_MSG_TELEMETRY 0x02
#define SPOTFLOW_MSG_REPORTED_CONFIGURATION 0x03
#define SPOTFLOW_MSG_DESIRED_CONFIGURATION 0x04

#define SPOTFLOW_FRAME_IS_FIRST BIT(0)
#define SPOTFLOW_FRAME_IS_LAST BIT(1)

#define SPOTFLOW_CONFIG_RX_BUFFER_SIZE 64
#define SPOTFLOW_SESSION_METADATA_BUFFER_SIZE 64
#define SPOTFLOW_TX_FRAME_BUFFER_SIZE 244

#define SPOTFLOW_BLE_MIN_ATT_MTU 23
#define SPOTFLOW_BLE_MIN_NOTIFY_PAYLOAD_MAX (SPOTFLOW_BLE_MIN_ATT_MTU - 3)
#define SPOTFLOW_BLE_FIRST_FRAGMENT_DATA_CAPACITY (SPOTFLOW_BLE_MIN_NOTIFY_PAYLOAD_MAX - 5)
#define SPOTFLOW_BLE_CONT_FRAGMENT_DATA_CAPACITY (SPOTFLOW_BLE_MIN_NOTIFY_PAYLOAD_MAX - 3)

#ifdef CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN
#define SPOTFLOW_BLE_MAX_LOG_PAYLOAD_LEN CONFIG_SPOTFLOW_CBOR_LOG_MAX_LEN
#else
#define SPOTFLOW_BLE_MAX_LOG_PAYLOAD_LEN 0
#endif

#ifdef CONFIG_SPOTFLOW_METRICS_CBOR_BUFFER_SIZE
#define SPOTFLOW_BLE_MAX_METRIC_PAYLOAD_LEN CONFIG_SPOTFLOW_METRICS_CBOR_BUFFER_SIZE
#else
#define SPOTFLOW_BLE_MAX_METRIC_PAYLOAD_LEN 0
#endif

#if SPOTFLOW_BLE_MAX_METRIC_PAYLOAD_LEN > SPOTFLOW_BLE_MAX_LOG_PAYLOAD_LEN
#define SPOTFLOW_BLE_MAX_INGEST_PAYLOAD_LEN SPOTFLOW_BLE_MAX_METRIC_PAYLOAD_LEN
#else
#define SPOTFLOW_BLE_MAX_INGEST_PAYLOAD_LEN SPOTFLOW_BLE_MAX_LOG_PAYLOAD_LEN
#endif

#if SPOTFLOW_BLE_MAX_INGEST_PAYLOAD_LEN <= SPOTFLOW_BLE_FIRST_FRAGMENT_DATA_CAPACITY
#define SPOTFLOW_BLE_MAX_ENCODED_FRAMES 1
#else
/* With the minimum ATT MTU of 23, the first fragment carries 15 bytes and each
 * continuation fragment carries 17 bytes. Size the temporary frame array to the
 * worst-case number of fragments needed for the largest configured ingest payload.
 */
#define SPOTFLOW_BLE_MAX_ENCODED_FRAMES                                                      \
	(1 +                                                                                 \
	 ((SPOTFLOW_BLE_MAX_INGEST_PAYLOAD_LEN - SPOTFLOW_BLE_FIRST_FRAGMENT_DATA_CAPACITY + \
	   SPOTFLOW_BLE_CONT_FRAGMENT_DATA_CAPACITY - 1) /                                   \
	  SPOTFLOW_BLE_CONT_FRAGMENT_DATA_CAPACITY))
#endif

struct spotflow_ble_encoded_frame {
	size_t len;
	uint8_t data[SPOTFLOW_TX_FRAME_BUFFER_SIZE];
};

struct spotflow_ble_tx_state {
	struct bt_conn* conn;
	bool notifications_enabled;
	uint8_t telemetry_sequence;
	uint8_t reported_config_sequence;
};

struct spotflow_ble_config_rx_state {
	spotflow_transport_message_cb callback;
	uint8_t sequence;
	size_t total_len;
	size_t received_len;
	bool active;
	uint8_t buffer[SPOTFLOW_CONFIG_RX_BUFFER_SIZE];
};

struct spotflow_ble_transport_state {
	struct k_mutex lock;
	bool initialized;
	bool bluetooth_enabled;
	struct k_work_delayable restart_advertising_work;
	struct spotflow_ble_tx_state tx;
	struct spotflow_ble_config_rx_state config_rx;
};

extern struct spotflow_ble_transport_state g_spotflow_ble_transport_state;

int spotflow_ble_transport_start_impl(void);
const struct bt_gatt_attr* spotflow_ble_tx_stream_attr_get(void);
int spotflow_ble_transport_encode_frames(uint8_t message_type, uint8_t sequence,
					 const uint8_t* payload, size_t len,
					 size_t frame_payload_max,
					 struct spotflow_ble_encoded_frame* frames,
					 size_t max_frames, size_t* frame_count);
int spotflow_ble_transport_send_framed_message(uint8_t message_type, uint8_t* sequence_counter,
					       uint8_t* payload, size_t len);
int spotflow_ble_transport_process_config_rx_frame(const void* buf, uint16_t len, uint8_t flags);

#endif /* SPOTFLOW_BLE_TRANSPORT_INTERNAL_H */
