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
int spotflow_ble_transport_encode_frames(uint8_t message_type, uint8_t sequence,
					 const uint8_t* payload, size_t len,
					 size_t frame_payload_max,
					 struct spotflow_ble_encoded_frame* frames,
					 size_t max_frames, size_t* frame_count);
int spotflow_ble_transport_send_framed_message(uint8_t message_type, uint8_t* sequence_counter,
					       uint8_t* payload, size_t len);
int spotflow_ble_transport_process_config_rx_frame(const void* buf, uint16_t len, uint8_t flags);
void spotflow_ble_transport_reset_config_rx_state(void);

#endif /* SPOTFLOW_BLE_TRANSPORT_INTERNAL_H */
