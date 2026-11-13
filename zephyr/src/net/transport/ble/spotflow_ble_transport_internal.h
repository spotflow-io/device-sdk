#ifndef SPOTFLOW_BLE_TRANSPORT_INTERNAL_H
#define SPOTFLOW_BLE_TRANSPORT_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include <zephyr/bluetooth/conn.h>
#include <zephyr/kernel.h>

#ifdef CONFIG_SPOTFLOW_COREDUMPS
#include "coredumps/spotflow_coredumps_cbor.h"
#endif

#include "net/spotflow_transport.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define SPOTFLOW_BLE_DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define SPOTFLOW_BLE_DEVICE_NAME_LEN (sizeof(SPOTFLOW_BLE_DEVICE_NAME) - 1)

#define SPOTFLOW_PROTOCOL_VERSION_MAJOR 0x01
#define SPOTFLOW_PROTOCOL_VERSION_MINOR 0x01

#define SPOTFLOW_MSG_TELEMETRY 0x02
#define SPOTFLOW_MSG_REPORTED_CONFIGURATION 0x03
#define SPOTFLOW_MSG_DESIRED_CONFIGURATION 0x04
#define SPOTFLOW_MSG_OTA_D2C 0x05
#define SPOTFLOW_MSG_OTA_C2D 0x06
#define SPOTFLOW_MSG_ARTIFACT_REQUEST 0x07
#define SPOTFLOW_MSG_ARTIFACT_RESPONSE 0x08

#define SPOTFLOW_FRAME_IS_FIRST BIT(0)
#define SPOTFLOW_FRAME_IS_LAST BIT(1)
#define SPOTFLOW_FRAME_NEEDS_ACK BIT(2)

#define SPOTFLOW_CONFIG_RX_BUFFER_SIZE 64

#define SPOTFLOW_BLE_ATT_NOTIFY_HEADER_LEN 3
#define SPOTFLOW_BLE_FIRST_FRAGMENT_HEADER_LEN 5
#define SPOTFLOW_BLE_CONT_FRAGMENT_HEADER_LEN 3

#define SPOTFLOW_TX_FRAME_BUFFER_SIZE (CONFIG_BT_L2CAP_TX_MTU - SPOTFLOW_BLE_ATT_NOTIFY_HEADER_LEN)

BUILD_ASSERT(CONFIG_BT_L2CAP_TX_MTU >
		     SPOTFLOW_BLE_ATT_NOTIFY_HEADER_LEN + SPOTFLOW_BLE_FIRST_FRAGMENT_HEADER_LEN,
	     "Spotflow BLE transport requires an ATT MTU large enough for framing headers");

#ifdef CONFIG_SPOTFLOW_COREDUMPS
BUILD_ASSERT(CONFIG_SPOTFLOW_COREDUMPS_CHUNK_SIZE + SPOTFLOW_COREDUMPS_CBOR_OVERHEAD <= UINT16_MAX,
	     "BLE framing supports coredump payloads up to 65535 bytes including CBOR overhead");
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
	uint8_t ota_sequence;
	uint8_t artifact_sequence;
	uint32_t connection_generation;
};

/* Per-message-type state, owned by its receive consumer. A connection generation
 * distinguishes queued fragments even when sequence numbers are reused.
 */
struct spotflow_ble_rx_state {
	uint32_t connection_generation;
	uint8_t sequence;
	size_t total_len;
	size_t received_len;
	bool active;
	bool needs_ack;
};

struct spotflow_ble_rx_frame {
	uint32_t connection_generation;
	uint16_t len;
	uint8_t data[CONFIG_BT_L2CAP_TX_MTU];
};

struct spotflow_ble_transport_state {
	struct k_mutex lock;
	bool initialized;
	bool bluetooth_enabled;
	struct k_work_delayable restart_advertising_work;
	struct spotflow_ble_tx_state tx;
	spotflow_transport_message_cb config_callback;
	spotflow_transport_message_cb ota_callback;
};

extern struct spotflow_ble_transport_state g_spotflow_ble_transport_state;

int spotflow_ble_transport_start_impl(void);
const struct bt_gatt_attr* spotflow_ble_tx_stream_attr_get(void);
int spotflow_ble_transport_encode_next_frame(uint8_t message_type, uint8_t sequence,
					     const uint8_t* payload, size_t len,
					     size_t frame_payload_max, size_t offset,
					     struct spotflow_ble_encoded_frame* frame,
					     size_t* next_offset);
int spotflow_ble_transport_send_framed_message(uint8_t message_type, uint8_t* sequence_counter,
					       uint8_t* payload, size_t len);
/**
 * @brief Send a message only on its originating connection.
 * @param message_type Outbound message type.
 * @param sequence_counter Per-type sequence counter, serialized by the caller.
 * @param payload Message payload.
 * @param len Payload length in bytes.
 * @param connection_generation Generation captured when the transfer started.
 * @return 0 on success, -EINVAL for a NULL counter or payload,
 *         -ENOTCONN for an inactive or replaced connection,
 *         -EMSGSIZE for an oversized payload or insufficient MTU, or a negative
 *         notification error (-EAGAIN for temporary notification failures).
 */
int spotflow_ble_transport_send_framed_message_on_connection(uint8_t message_type,
							     uint8_t* sequence_counter,
							     uint8_t* payload, size_t len,
							     uint32_t connection_generation);
/** @brief Reassemble a supported inbound type and deliver it before acknowledging it.
 * The caller owns state and buffer and serializes calls for this message type.
 * @param message_type Registered inbound message type.
 * @param state Per-type reassembly state.
 * @param buffer Reassembly buffer.
 * @param capacity Buffer capacity in bytes.
 * @param buf Encoded BLE frame.
 * @param len Encoded frame length.
 * @param callback Handler for complete messages, or NULL when unavailable.
 * @return 0 for accepted/ignored frames, or -EINVAL for invalid framing.
 */
int spotflow_ble_transport_process_rx_frame(uint8_t message_type,
					    struct spotflow_ble_rx_state* state, uint8_t* buffer,
					    size_t capacity, const void* buf, uint16_t len,
					    spotflow_transport_message_cb callback);
int spotflow_ble_transport_queue_rx_frame(const void* buf, uint16_t len);
int spotflow_ble_transport_send_control(bool accepted, uint8_t message_type, uint8_t sequence,
					uint32_t connection_generation);
void spotflow_ble_artifact_receive(const uint8_t* payload, size_t len);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SPOTFLOW_BLE_TRANSPORT_INTERNAL_H */
