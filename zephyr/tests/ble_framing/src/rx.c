#include "net/transport/ble/spotflow_ble_transport_internal.h"

#include <zephyr/bluetooth/gatt.h>
#include <zephyr/ztest.h>

#include <string.h>

static K_SEM_DEFINE(command_received, 0, 1);
static K_SEM_DEFINE(artifact_received, 0, 1);
static K_SEM_DEFINE(callback_entered, 0, 1);
static K_SEM_DEFINE(callback_release, 0, 1);
static bool block_command;
static uint8_t connection_tokens[2];
static uint8_t received_command[64];
static size_t command_len;
static uint8_t received_artifact[32];
static size_t artifact_len;
static int command_count;
static int connection_refs;
static int artifact_frames;
static bool reconnect_on_mtu;
static struct bt_conn* retired_connection;
K_MSGQ_DEFINE(control_frames, 3, 16, 1);

static struct spotflow_ble_encoded_frame encode_single_frame(uint8_t type, uint8_t sequence,
							     bool needs_ack);
static void expect_control(bool accepted, uint8_t type, uint8_t sequence);
static size_t send_fragment(uint8_t type, uint8_t sequence, const uint8_t* payload, size_t len,
			    size_t offset);
static void send_message(uint8_t type, uint8_t sequence, const uint8_t* payload, size_t len);
static void command_callback(uint8_t* payload, size_t len);
static void before(void* fixture);
static void* setup(void);

struct bt_conn* __wrap_bt_conn_ref(struct bt_conn* conn)
{
	connection_refs++;
	return conn;
}

void __wrap_bt_conn_unref(struct bt_conn* conn) {}

uint16_t __wrap_bt_gatt_get_mtu(struct bt_conn* conn)
{
	if (reconnect_on_mtu) {
		reconnect_on_mtu = false;
		retired_connection = conn;
		k_mutex_lock(&g_spotflow_ble_transport_state.lock, K_FOREVER);
		g_spotflow_ble_transport_state.tx.connection_generation++;
		g_spotflow_ble_transport_state.tx.conn = (struct bt_conn*)&connection_tokens[1];
		k_mutex_unlock(&g_spotflow_ble_transport_state.lock);
	}
	return 23;
}

int __wrap_bt_gatt_notify_cb(struct bt_conn* conn, struct bt_gatt_notify_params* params)
{
	if (((const uint8_t*)params->data)[0] == SPOTFLOW_MSG_ARTIFACT_REQUEST) {
		artifact_frames++;
		if (retired_connection != NULL) {
			zassert_equal(conn, retired_connection);
			return -ENOTCONN;
		}
		zassert_equal(conn, g_spotflow_ble_transport_state.tx.conn);
		return 0;
	}
	zassert_equal(conn, g_spotflow_ble_transport_state.tx.conn);
	zassert_equal(params->len, 3);
	return k_msgq_put(&control_frames, params->data, K_NO_WAIT);
}

void spotflow_ble_artifact_receive(const uint8_t* payload, size_t len)
{
	artifact_len = len;
	memcpy(received_artifact, payload, len);
	k_sem_give(&artifact_received);
}

ZTEST(spotflow_ble_ota_rx, test_low_mtu_interleaved_types_reassemble_independently)
{
	uint8_t command[50];
	uint8_t artifact[30];
	memset(command, 0xa5, sizeof(command));
	memset(artifact, 0x5a, sizeof(artifact));
	size_t next = send_fragment(SPOTFLOW_MSG_OTA_C2D, 7, command, sizeof(command), 0);
	send_message(SPOTFLOW_MSG_ARTIFACT_RESPONSE, 9, artifact, sizeof(artifact));
	while (next < sizeof(command)) {
		next = send_fragment(SPOTFLOW_MSG_OTA_C2D, 7, command, sizeof(command), next);
	}
	zassert_ok(k_sem_take(&command_received, K_MSEC(100)));
	zassert_ok(k_sem_take(&artifact_received, K_MSEC(100)));
	zassert_equal(command_len, sizeof(command));
	zassert_equal(artifact_len, sizeof(artifact));
	zassert_mem_equal(received_command, command, sizeof(command));
	zassert_mem_equal(received_artifact, artifact, sizeof(artifact));
}

ZTEST(spotflow_ble_ota_rx, test_retry_first_fragment_replaces_incomplete_command)
{
	uint8_t command[50];
	memset(command, 0xa5, sizeof(command));
	(void)send_fragment(SPOTFLOW_MSG_OTA_C2D, 7, command, sizeof(command), 0);
	send_message(SPOTFLOW_MSG_OTA_C2D, 7, command, sizeof(command));
	zassert_ok(k_sem_take(&command_received, K_MSEC(100)));
	zassert_equal(command_len, sizeof(command));
	zassert_mem_equal(received_command, command, sizeof(command));
}

ZTEST(spotflow_ble_ota_rx, test_reconnect_discards_partial_old_session)
{
	uint8_t command[50];
	memset(command, 0xa5, sizeof(command));
	size_t next = send_fragment(SPOTFLOW_MSG_OTA_C2D, 7, command, sizeof(command), 0);
	k_mutex_lock(&g_spotflow_ble_transport_state.lock, K_FOREVER);
	g_spotflow_ble_transport_state.tx.connection_generation++;
	k_mutex_unlock(&g_spotflow_ble_transport_state.lock);
	while (next < sizeof(command)) {
		next = send_fragment(SPOTFLOW_MSG_OTA_C2D, 7, command, sizeof(command), next);
	}
	uint8_t fresh[] = { 1, 2, 3 };
	send_message(SPOTFLOW_MSG_OTA_C2D, 8, fresh, sizeof(fresh));
	zassert_ok(k_sem_take(&command_received, K_MSEC(100)));
	zassert_equal(command_len, sizeof(fresh));
	zassert_mem_equal(received_command, fresh, sizeof(fresh));
}

ZTEST_SUITE(spotflow_ble_ota_rx, NULL, setup, before, NULL, NULL);

ZTEST(spotflow_ble_ota_rx, test_ack_only_when_requested_and_again_for_duplicate_command)
{
	struct spotflow_ble_encoded_frame frame =
		encode_single_frame(SPOTFLOW_MSG_OTA_C2D, 7, false);
	zassert_ok(spotflow_ble_transport_queue_rx_frame(frame.data, frame.len));
	zassert_ok(k_sem_take(&command_received, K_MSEC(100)));
	frame.data[1] |= SPOTFLOW_FRAME_NEEDS_ACK;
	frame.data[2] = 8;
	for (int i = 0; i < 2; i++) {
		zassert_ok(spotflow_ble_transport_queue_rx_frame(frame.data, frame.len));
		zassert_ok(k_sem_take(&command_received, K_MSEC(100)));
		expect_control(true, SPOTFLOW_MSG_OTA_C2D, 8);
	}
	/* The ordered ACK for sequence 8 also proves that sequence 7 emitted none. */
	uint8_t control[3];
	zassert_equal(k_msgq_get(&control_frames, control, K_NO_WAIT), -ENOMSG);
	zassert_equal(command_count, 3);
}

ZTEST(spotflow_ble_ota_rx, test_nack_orphan_and_incomplete_final_fragment)
{
	uint8_t orphan[] = {
		SPOTFLOW_MSG_OTA_C2D,
		SPOTFLOW_FRAME_IS_LAST | SPOTFLOW_FRAME_NEEDS_ACK,
		/* Sequence number. */
		8,
		/* Fragment payload. */
		0xaa,
	};
	zassert_ok(spotflow_ble_transport_queue_rx_frame(orphan, sizeof(orphan)));
	expect_control(false, SPOTFLOW_MSG_OTA_C2D, 8);
	uint8_t short_message[] = {
		SPOTFLOW_MSG_OTA_C2D,
		SPOTFLOW_FRAME_IS_FIRST | SPOTFLOW_FRAME_IS_LAST | SPOTFLOW_FRAME_NEEDS_ACK,
		/* Sequence number. */
		9,
		/* Declared total message length: uint16 LE. */
		2,
		0,
		/* Fragment payload. */
		0xaa,
	};
	zassert_ok(spotflow_ble_transport_queue_rx_frame(short_message, sizeof(short_message)));
	expect_control(false, SPOTFLOW_MSG_OTA_C2D, 9);
	zassert_equal(command_count, 0);
}

ZTEST(spotflow_ble_ota_rx, test_nack_inconsistent_ack_flags_in_either_direction)
{
	for (int ack_first = 0; ack_first < 2; ack_first++) {
		uint8_t first[] = {
			SPOTFLOW_MSG_OTA_C2D,
			SPOTFLOW_FRAME_IS_FIRST | (ack_first ? SPOTFLOW_FRAME_NEEDS_ACK : 0),
			/* Sequence number. */
			7,
			/* Declared total message length: uint16 LE. */
			2,
			0,
			/* Fragment payload. */
			0xaa,
		};
		uint8_t last[] = {
			SPOTFLOW_MSG_OTA_C2D,
			SPOTFLOW_FRAME_IS_LAST | (ack_first ? 0 : SPOTFLOW_FRAME_NEEDS_ACK),
			/* Sequence number. */
			7,
			/* Fragment payload. */
			0xbb,
		};
		zassert_ok(spotflow_ble_transport_queue_rx_frame(first, sizeof(first)));
		zassert_ok(spotflow_ble_transport_queue_rx_frame(last, sizeof(last)));
		expect_control(false, SPOTFLOW_MSG_OTA_C2D, 7);
	}
	zassert_equal(command_count, 0);
}

ZTEST(spotflow_ble_ota_rx, test_configuration_ack_and_duplicate_delivery)
{
	g_spotflow_ble_transport_state.config_callback = command_callback;
	struct spotflow_ble_encoded_frame frame =
		encode_single_frame(SPOTFLOW_MSG_DESIRED_CONFIGURATION, 3, true);
	for (int i = 0; i < 2; i++) {
		zassert_ok(spotflow_ble_transport_queue_rx_frame(frame.data, frame.len));
		expect_control(true, SPOTFLOW_MSG_DESIRED_CONFIGURATION, 3);
	}
	zassert_equal(command_count, 2);
}

ZTEST(spotflow_ble_ota_rx, test_configuration_nacks_inconsistent_flags)
{
	g_spotflow_ble_transport_state.config_callback = command_callback;
	uint8_t first[] = {
		SPOTFLOW_MSG_DESIRED_CONFIGURATION,
		SPOTFLOW_FRAME_IS_FIRST | SPOTFLOW_FRAME_NEEDS_ACK,
		/* Sequence number. */
		3,
		/* Declared total message length: uint16 LE. */
		2,
		0,
		/* Fragment payload. */
		0xaa,
	};
	uint8_t last[] = {
		SPOTFLOW_MSG_DESIRED_CONFIGURATION,
		SPOTFLOW_FRAME_IS_LAST,
		/* Sequence number. */
		3,
		/* Fragment payload. */
		0xbb,
	};
	zassert_ok(spotflow_ble_transport_queue_rx_frame(first, sizeof(first)));
	zassert_ok(spotflow_ble_transport_queue_rx_frame(last, sizeof(last)));
	expect_control(false, SPOTFLOW_MSG_DESIRED_CONFIGURATION, 3);
	zassert_equal(command_count, 0);
}

ZTEST(spotflow_ble_ota_rx, test_unknown_type_is_ignored_without_control_response)
{
	/* 0x7f is deliberately outside the supported message types. */
	struct spotflow_ble_encoded_frame frame = encode_single_frame(0x7f, 5, true);

	zassert_ok(spotflow_ble_transport_queue_rx_frame(frame.data, frame.len));
	frame.data[0] = SPOTFLOW_MSG_OTA_C2D;
	frame.data[2] = 6;
	zassert_ok(spotflow_ble_transport_queue_rx_frame(frame.data, frame.len));
	expect_control(true, SPOTFLOW_MSG_OTA_C2D, 6);

	/* The following valid command is an ordered barrier through the RX queue. */
	uint8_t control[3];
	zassert_equal(k_msgq_get(&control_frames, control, K_NO_WAIT), -ENOMSG);
	zassert_equal(command_count, 1);
}

ZTEST(spotflow_ble_ota_rx, test_reconnect_during_callback_suppresses_old_ack)
{
	struct spotflow_ble_encoded_frame frame =
		encode_single_frame(SPOTFLOW_MSG_OTA_C2D, 7, true);
	block_command = true;
	zassert_ok(spotflow_ble_transport_queue_rx_frame(frame.data, frame.len));
	zassert_ok(k_sem_take(&callback_entered, K_SECONDS(1)));
	k_mutex_lock(&g_spotflow_ble_transport_state.lock, K_FOREVER);
	/* Simulate disconnect and reconnect while the old handler is blocked. */
	g_spotflow_ble_transport_state.tx.connection_generation += 2;
	g_spotflow_ble_transport_state.tx.conn = (struct bt_conn*)&connection_tokens[1];
	k_mutex_unlock(&g_spotflow_ble_transport_state.lock);
	block_command = false;
	k_sem_give(&callback_release);
	/* Reuse the old sequence, then use a second ACK as an RX queue barrier. */
	zassert_ok(spotflow_ble_transport_queue_rx_frame(frame.data, frame.len));
	frame.data[2] = 8;
	zassert_ok(spotflow_ble_transport_queue_rx_frame(frame.data, frame.len));
	expect_control(true, SPOTFLOW_MSG_OTA_C2D, 7);
	expect_control(true, SPOTFLOW_MSG_OTA_C2D, 8);
	uint8_t control[3];
	zassert_equal(k_msgq_get(&control_frames, control, K_NO_WAIT), -ENOMSG);
	zassert_equal(command_count, 3);
}

ZTEST(spotflow_ble_ota_rx, test_stale_generation_suppresses_both_control_types)
{
	uint32_t stale_generation = g_spotflow_ble_transport_state.tx.connection_generation - 1;
	zassert_equal(spotflow_ble_transport_send_control(true, SPOTFLOW_MSG_OTA_C2D, 7,
							  stale_generation),
		      -EAGAIN);
	zassert_equal(spotflow_ble_transport_send_control(false, SPOTFLOW_MSG_OTA_C2D, 7,
							  stale_generation),
		      -EAGAIN);
	uint8_t control[3];
	zassert_equal(k_msgq_get(&control_frames, control, K_NO_WAIT), -ENOMSG);
}

ZTEST(spotflow_ble_ota_rx, test_generation_bound_send_rejects_stale_connection_before_ref)
{
	uint8_t sequence = 0;
	/* Opaque application payload: only connection selection is under test. */
	uint8_t payload[] = { 0xaa };
	uint32_t generation = g_spotflow_ble_transport_state.tx.connection_generation;
	zassert_equal(spotflow_ble_transport_send_framed_message_on_connection(
			      SPOTFLOW_MSG_ARTIFACT_REQUEST, &sequence, payload, sizeof(payload),
			      generation - 1),
		      -ENOTCONN);
	zassert_equal(connection_refs, 0);
	zassert_equal(artifact_frames, 0);
	zassert_equal(sequence, 0);
	zassert_ok(spotflow_ble_transport_send_framed_message_on_connection(
		SPOTFLOW_MSG_ARTIFACT_REQUEST, &sequence, payload, sizeof(payload), generation));
	zassert_equal(connection_refs, 1);
	zassert_equal(artifact_frames, 1);
}

ZTEST(spotflow_ble_ota_rx, test_reconnect_after_connection_ref_never_redirects_notification)
{
	uint8_t sequence = 0;
	/* Opaque application payload, with reconnect after selecting the connection. */
	uint8_t payload[] = { 0xaa };
	uint32_t generation = g_spotflow_ble_transport_state.tx.connection_generation;
	reconnect_on_mtu = true;
	zassert_equal(spotflow_ble_transport_send_framed_message_on_connection(
			      SPOTFLOW_MSG_ARTIFACT_REQUEST, &sequence, payload, sizeof(payload),
			      generation),
		      -EAGAIN);
	zassert_equal(connection_refs, 1);
	zassert_equal(artifact_frames, 1);
}

static void expect_control(bool accepted, uint8_t type, uint8_t sequence)
{
	uint8_t actual[3];
	/* Control frames: ACK (0) or NACK (1), original type, original sequence. */
	uint8_t expected[] = { accepted ? 0 : 1, type, sequence };
	zassert_ok(k_msgq_get(&control_frames, actual, K_MSEC(100)));
	zassert_mem_equal(actual, expected, sizeof(expected));
}

static void command_callback(uint8_t* payload, size_t len)
{
	command_len = len;
	command_count++;
	memcpy(received_command, payload, len);
	if (block_command) {
		k_sem_give(&callback_entered);
		zassert_ok(k_sem_take(&callback_release, K_SECONDS(1)));
	}
	k_sem_give(&command_received);
}

static void before(void* fixture)
{
	ARG_UNUSED(fixture);
	k_mutex_lock(&g_spotflow_ble_transport_state.lock, K_FOREVER);
	g_spotflow_ble_transport_state.tx.connection_generation++;
	g_spotflow_ble_transport_state.tx.conn = (struct bt_conn*)&connection_tokens[0];
	g_spotflow_ble_transport_state.tx.notifications_enabled = true;
	g_spotflow_ble_transport_state.ota_callback = command_callback;
	k_mutex_unlock(&g_spotflow_ble_transport_state.lock);
	k_sem_reset(&command_received);
	k_sem_reset(&artifact_received);
	k_sem_reset(&callback_entered);
	k_sem_reset(&callback_release);
	block_command = false;
	command_len = 0;
	command_count = 0;
	artifact_len = 0;
	connection_refs = 0;
	artifact_frames = 0;
	reconnect_on_mtu = false;
	retired_connection = NULL;
	k_msgq_purge(&control_frames);
}

static void* setup(void)
{
	k_mutex_init(&g_spotflow_ble_transport_state.lock);
	return NULL;
}

static size_t send_fragment(uint8_t type, uint8_t sequence, const uint8_t* payload, size_t len,
			    size_t offset)
{
	struct spotflow_ble_encoded_frame frame;
	size_t next;
	zassert_ok(spotflow_ble_transport_encode_next_frame(type, sequence, payload, len, 20,
							    offset, &frame, &next));
	zassert_ok(spotflow_ble_transport_queue_rx_frame(frame.data, frame.len));
	return next;
}

static void send_message(uint8_t type, uint8_t sequence, const uint8_t* payload, size_t len)
{
	size_t offset = 0;
	while (offset < len) {
		offset = send_fragment(type, sequence, payload, len, offset);
	}
}

static struct spotflow_ble_encoded_frame encode_single_frame(uint8_t type, uint8_t sequence,
							     bool needs_ack)
{
	/* These tests exercise transport delivery, so the handler treats the
	 * one-byte payload as opaque rather than decoding configuration/OTA CBOR.
	 */
	const uint8_t payload[] = { 0xaa };
	struct spotflow_ble_encoded_frame frame;
	size_t next_offset;
	zassert_ok(spotflow_ble_transport_encode_next_frame(
		type, sequence, payload, sizeof(payload), 20, 0, &frame, &next_offset));
	zassert_equal(next_offset, sizeof(payload));
	if (needs_ack) {
		frame.data[1] |= SPOTFLOW_FRAME_NEEDS_ACK;
	}
	return frame;
}
