#include "net/transport/ble/spotflow_ble_transport_internal.h"

#include <zephyr/ztest.h>

#include <string.h>

static K_SEM_DEFINE(command_received, 0, 1);
static K_SEM_DEFINE(artifact_received, 0, 1);
static uint8_t received_command[64];
static size_t command_len;
static uint8_t received_artifact[32];
static size_t artifact_len;

static void command_callback(uint8_t* payload, size_t len)
{
	command_len = len;
	memcpy(received_command, payload, len);
	k_sem_give(&command_received);
}

void spotflow_ble_artifact_receive(const uint8_t* payload, size_t len)
{
	artifact_len = len;
	memcpy(received_artifact, payload, len);
	k_sem_give(&artifact_received);
}

static void before(void* fixture)
{
	ARG_UNUSED(fixture);
	k_mutex_lock(&g_spotflow_ble_transport_state.lock, K_FOREVER);
	g_spotflow_ble_transport_state.tx.generation++;
	g_spotflow_ble_transport_state.ota_callback = command_callback;
	k_mutex_unlock(&g_spotflow_ble_transport_state.lock);
	k_sem_reset(&command_received);
	k_sem_reset(&artifact_received);
	command_len = 0;
	artifact_len = 0;
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
	zassert_ok(spotflow_ble_transport_queue_ota_frame(frame.data, frame.len));
	return next;
}

static void send_message(uint8_t type, uint8_t sequence, const uint8_t* payload, size_t len)
{
	size_t offset = 0;
	while (offset < len) {
		offset = send_fragment(type, sequence, payload, len, offset);
	}
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
	g_spotflow_ble_transport_state.tx.generation++;
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
