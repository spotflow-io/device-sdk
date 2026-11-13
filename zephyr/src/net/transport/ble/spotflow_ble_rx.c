#include "spotflow_ble_transport_internal.h"

#ifdef CONFIG_SPOTFLOW_OTA
#include "ota/protocol/spotflow_ota_ble_protocol.h"
#include "ota/protocol/spotflow_ota_cbor.h"
#endif /* CONFIG_SPOTFLOW_OTA */

#include <errno.h>
#include <string.h>

#include <zephyr/sys/byteorder.h>

struct spotflow_ble_rx_channel {
	struct spotflow_ble_rx_state message;
	uint8_t* buffer;
	size_t capacity;
};

static uint8_t config_buffer[SPOTFLOW_CONFIG_RX_BUFFER_SIZE];
#ifdef CONFIG_SPOTFLOW_OTA
BUILD_ASSERT(SPOTFLOW_OTA_CBOR_MAX_C2D_MESSAGE_SIZE <= UINT16_MAX);

/* Match the OTA decoder limits instead of maintaining a separate BLE limit. */
static uint8_t command_buffer[SPOTFLOW_OTA_CBOR_MAX_C2D_MESSAGE_SIZE];
static uint8_t artifact_buffer[CONFIG_SPOTFLOW_OTA_DOWNLOAD_BUFFER_SIZE +
			       SPOTFLOW_OTA_BLE_DATA_HEADER_SIZE];
#endif /* CONFIG_SPOTFLOW_OTA */

static struct spotflow_ble_rx_channel config_channel = {
	.buffer = config_buffer,
	.capacity = sizeof(config_buffer),
};
#ifdef CONFIG_SPOTFLOW_OTA
static struct spotflow_ble_rx_channel command_channel = {
	.buffer = command_buffer,
	.capacity = sizeof(command_buffer),
};
static struct spotflow_ble_rx_channel artifact_channel = {
	.buffer = artifact_buffer,
	.capacity = sizeof(artifact_buffer),
};
#endif /* CONFIG_SPOTFLOW_OTA */
K_MSGQ_DEFINE(rx_queue, sizeof(struct spotflow_ble_rx_frame), 16, 4);

static void rx_thread(void);
static void process_queued_frame(const struct spotflow_ble_rx_frame* frame);
static int reassemble(struct spotflow_ble_rx_state* state, uint8_t* buffer, size_t capacity,
		      const uint8_t* frame, size_t len);
#ifdef CONFIG_SPOTFLOW_OTA
static void receive_artifact(uint8_t* payload, size_t len);
#endif /* CONFIG_SPOTFLOW_OTA */

K_THREAD_DEFINE(spotflow_ble_rx_thread, CONFIG_SPOTFLOW_BLE_RX_THREAD_STACK_SIZE, rx_thread, NULL,
		NULL, NULL, K_LOWEST_APPLICATION_THREAD_PRIO, 0, 0);

int spotflow_ble_transport_queue_rx_frame(const void* buf, uint16_t len)
{
	if (buf == NULL || len < SPOTFLOW_BLE_CONT_FRAGMENT_HEADER_LEN ||
	    len > sizeof(((struct spotflow_ble_rx_frame*)0)->data)) {
		return -EINVAL;
	}

	struct spotflow_ble_rx_frame frame = { .len = len };

	k_mutex_lock(&g_spotflow_ble_transport_state.lock, K_FOREVER);
	frame.connection_generation = g_spotflow_ble_transport_state.tx.connection_generation;
	k_mutex_unlock(&g_spotflow_ble_transport_state.lock);
	memcpy(frame.data, buf, len);

	/* Never block the Bluetooth receive context. A full queue drops this frame;
	 * writes without response cannot report that ATT error to the gateway.
	 * Messages requesting an ACK recover through a reassembly NACK or missing
	 * acknowledgement and a complete retry. Artifact exchanges have their own
	 * retry deadline. Legacy configuration writes without NEEDS_ACK are best
	 * effort: an overflow can lose an update until a later cloud update or
	 * resubscription. Queue depth alone cannot guarantee their delivery.
	 */
	return k_msgq_put(&rx_queue, &frame, K_NO_WAIT);
}

int spotflow_ble_transport_process_rx_frame(uint8_t message_type,
					    struct spotflow_ble_rx_state* state, uint8_t* buffer,
					    size_t capacity, const void* buf, uint16_t len,
					    spotflow_transport_message_cb callback)
{
	if (state == NULL || buffer == NULL || buf == NULL ||
	    len < SPOTFLOW_BLE_CONT_FRAGMENT_HEADER_LEN) {
		return -EINVAL;
	}

	const uint8_t* frame = buf;
	uint8_t received_type = frame[0];
	uint8_t flags = frame[1];
	uint8_t sequence = frame[2];

	/* Only registered inbound types participate in acknowledgement handling. */
	if (received_type != message_type) {
		return 0;
	}

	bool needs_ack = (flags & SPOTFLOW_FRAME_NEEDS_ACK) != 0;

	if ((flags & SPOTFLOW_FRAME_IS_FIRST) == 0 && state->active &&
	    state->sequence == sequence) {
		needs_ack |= state->needs_ack;
	}

	int rc = reassemble(state, buffer, capacity, frame, len);

	if (rc > 0 && callback != NULL) {
		callback(buffer, (size_t)rc);
	}
	if (needs_ack && rc != 0) {
		/* ACK means handler delivery, not durable application success. A full
		 * retry is delivered again; application protocols handle duplicates.
		 */
		(void)spotflow_ble_transport_send_control(rc > 0 && callback != NULL, received_type,
							  sequence, state->connection_generation);
	}
	return rc < 0 ? rc : 0;
}

static void rx_thread(void)
{
	static struct spotflow_ble_rx_frame frame;

	while (true) {
		k_msgq_get(&rx_queue, &frame, K_FOREVER);
		process_queued_frame(&frame);
	}
}

static void process_queued_frame(const struct spotflow_ble_rx_frame* frame)
{
	struct spotflow_ble_rx_channel* channel;
	spotflow_transport_message_cb callback;

	k_mutex_lock(&g_spotflow_ble_transport_state.lock, K_FOREVER);
	uint32_t current_generation = g_spotflow_ble_transport_state.tx.connection_generation;

	if (frame->data[0] == SPOTFLOW_MSG_DESIRED_CONFIGURATION) {
		channel = &config_channel;
		callback = g_spotflow_ble_transport_state.config_callback;
#ifdef CONFIG_SPOTFLOW_OTA
	} else if (frame->data[0] == SPOTFLOW_MSG_OTA_C2D) {
		channel = &command_channel;
		callback = g_spotflow_ble_transport_state.ota_callback;
	} else if (frame->data[0] == SPOTFLOW_MSG_ARTIFACT_RESPONSE) {
		channel = &artifact_channel;
		callback = receive_artifact;
#endif /* CONFIG_SPOTFLOW_OTA */
	} else {
		k_mutex_unlock(&g_spotflow_ble_transport_state.lock);
		return;
	}
	k_mutex_unlock(&g_spotflow_ble_transport_state.lock);

	if (frame->connection_generation != current_generation) {
		return;
	}
	if (channel->message.connection_generation != current_generation) {
		channel->message = (struct spotflow_ble_rx_state){
			.connection_generation = current_generation,
		};
	}

	(void)spotflow_ble_transport_process_rx_frame(frame->data[0], &channel->message,
						      channel->buffer, channel->capacity,
						      frame->data, frame->len, callback);
}

static int reassemble(struct spotflow_ble_rx_state* state, uint8_t* buffer, size_t capacity,
		      const uint8_t* frame, size_t len)
{
	uint8_t flags = frame[1];
	bool first = (flags & SPOTFLOW_FRAME_IS_FIRST) != 0;
	size_t header_len = first ? SPOTFLOW_BLE_FIRST_FRAGMENT_HEADER_LEN
				  : SPOTFLOW_BLE_CONT_FRAGMENT_HEADER_LEN;

	if (len < header_len) {
		state->active = false;
		return -EINVAL;
	}
	if (first) {
		*state = (struct spotflow_ble_rx_state){
			.connection_generation = state->connection_generation,
			.active = true,
			.needs_ack = (flags & SPOTFLOW_FRAME_NEEDS_ACK) != 0,
			.sequence = frame[2],
			.total_len = sys_get_le16(frame + 3),
		};
	}

	size_t fragment_len = len - header_len;

	if (!state->active || state->sequence != frame[2] ||
	    state->needs_ack != ((flags & SPOTFLOW_FRAME_NEEDS_ACK) != 0) ||
	    state->total_len == 0 || state->total_len > capacity ||
	    fragment_len > state->total_len - state->received_len) {
		state->active = false;
		return -EINVAL;
	}

	memcpy(buffer + state->received_len, frame + header_len, fragment_len);
	state->received_len += fragment_len;
	if ((flags & SPOTFLOW_FRAME_IS_LAST) == 0) {
		return 0;
	}

	state->active = false;
	return state->received_len == state->total_len ? (int)state->total_len : -EINVAL;
}

#ifdef CONFIG_SPOTFLOW_OTA
static void receive_artifact(uint8_t* payload, size_t len)
{
	spotflow_ble_artifact_receive(payload, len);
}
#endif /* CONFIG_SPOTFLOW_OTA */
