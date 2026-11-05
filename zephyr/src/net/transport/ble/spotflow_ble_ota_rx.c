#include "spotflow_ble_transport_internal.h"

#include <errno.h>
#include <string.h>

#include <zephyr/sys/byteorder.h>

/* GATT callbacks only enqueue bounded frames. OTA decoding, persistence, and
 * application callbacks run on this thread, independently of the downloader.
 */
struct rx_frame {
	uint32_t generation;
	uint16_t len;
	uint8_t data[CONFIG_BT_L2CAP_TX_MTU];
};

struct reassembly {
	bool active;
	uint8_t sequence;
	size_t total;
	size_t received;
};

K_MSGQ_DEFINE(ota_rx_queue, sizeof(struct rx_frame), 16, 4);
static uint8_t command_buffer[CONFIG_SPOTFLOW_OTA_BLE_COMMAND_BUFFER_SIZE];
static uint8_t artifact_buffer[CONFIG_SPOTFLOW_OTA_DOWNLOAD_BUFFER_SIZE + 15];

int spotflow_ble_transport_queue_ota_frame(const void* buf, uint16_t len)
{
	if (buf == NULL || len < SPOTFLOW_BLE_CONT_FRAGMENT_HEADER_LEN ||
	    len > sizeof(((struct rx_frame*)0)->data)) {
		return -EINVAL;
	}
	struct rx_frame frame = { .len = len };
	k_mutex_lock(&g_spotflow_ble_transport_state.lock, K_FOREVER);
	frame.generation = g_spotflow_ble_transport_state.tx.generation;
	k_mutex_unlock(&g_spotflow_ble_transport_state.lock);
	memcpy(frame.data, buf, len);
	return k_msgq_put(&ota_rx_queue, &frame, K_NO_WAIT);
}

static int reassemble(struct reassembly* state, uint8_t* buffer, size_t capacity,
		      const struct rx_frame* frame)
{
	uint8_t flags = frame->data[1];
	bool first = (flags & SPOTFLOW_FRAME_IS_FIRST) != 0;
	size_t header = first ? SPOTFLOW_BLE_FIRST_FRAGMENT_HEADER_LEN
			      : SPOTFLOW_BLE_CONT_FRAGMENT_HEADER_LEN;

	if (frame->len < header) {
		state->active = false;
		return -EINVAL;
	}
	if (first) {
		*state = (struct reassembly){
			.active = true,
			.sequence = frame->data[2],
			.total = sys_get_le16(&frame->data[3]),
		};
	}
	size_t len = frame->len - header;
	if (!state->active || state->sequence != frame->data[2] || state->total == 0 ||
	    state->total > capacity || len > state->total - state->received) {
		state->active = false;
		return -EINVAL;
	}
	memcpy(buffer + state->received, frame->data + header, len);
	state->received += len;
	if ((flags & SPOTFLOW_FRAME_IS_LAST) == 0) {
		return 0;
	}
	state->active = false;
	return state->received == state->total ? (int)state->total : -EINVAL;
}

static void ota_rx_thread(void)
{
	struct reassembly command = { 0 };
	struct reassembly artifact = { 0 };
	uint32_t generation = 0;
	struct rx_frame frame;

	while (true) {
		k_msgq_get(&ota_rx_queue, &frame, K_FOREVER);
		k_mutex_lock(&g_spotflow_ble_transport_state.lock, K_FOREVER);
		uint32_t current = g_spotflow_ble_transport_state.tx.generation;
		spotflow_transport_message_cb callback =
			g_spotflow_ble_transport_state.ota_callback;
		k_mutex_unlock(&g_spotflow_ble_transport_state.lock);
		if (frame.generation != current) {
			continue;
		}
		if (generation != current) {
			command.active = false;
			artifact.active = false;
			generation = current;
		}
		bool is_command = frame.data[0] == SPOTFLOW_MSG_OTA_C2D;
		int rc = reassemble(is_command ? &command : &artifact,
				    is_command ? command_buffer : artifact_buffer,
				    is_command ? sizeof(command_buffer) : sizeof(artifact_buffer),
				    &frame);
		if (rc > 0 && is_command && callback != NULL) {
			callback(command_buffer, (size_t)rc);
		} else if (rc > 0 && !is_command) {
			spotflow_ble_artifact_receive(artifact_buffer, (size_t)rc);
		}
		if (is_command && rc != 0) {
			/* Acceptance means handed to the OTA core, not durable installation.
			 * Durable results still use the normal OTA cloud protocol.
			 */
			(void)spotflow_ble_transport_send_control(
				rc > 0 && callback != NULL, SPOTFLOW_MSG_OTA_C2D, frame.data[2]);
		}
	}
}

K_THREAD_DEFINE(spotflow_ble_ota_rx_thread, 4096, ota_rx_thread, NULL, NULL, NULL,
		K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);
