#include <spotflow/downloader.h>

#include "net/transport/ble/spotflow_ble_transport_internal.h"
#include "ota/protocol/spotflow_ota_ble_protocol.h"

#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include <errno.h>
#include <string.h>

LOG_MODULE_REGISTER(spotflow_ota, LOG_LEVEL_INF);

#define ARTIFACT_SIZE 130

struct spotflow_ble_transport_state g_spotflow_ble_transport_state;
static struct spotflow_downloader downloader;
static size_t consumed;
static size_t first_read_offset;
static int final_blocks;
static int opens;
static bool fail_read;
static bool fail_close;
static bool invalid_offset;
static bool cancel_after_block;
static bool pause_after_block;
static unsigned silent_opens;
static uint32_t first_transfer_id;
static bool expect_same_transfer;
static uint8_t connection_tokens[2];
static uint8_t reconnect_operation;
static bool reconnect_after_reply;
static bool reconnect_after_block;
static bool reconnect_before_send;
static uint32_t gateway_transfer_id;
static int stale_requests;

static int download(void);
static void reconnect(void);
static void resume_download(struct k_work* work);
static void consume(const struct spotflow_artifact_block* block,
		    struct spotflow_downloader* instance, void* context);
static void before_test(void* fixture);

static K_WORK_DELAYABLE_DEFINE(resume_work, resume_download);

bool spotflow_ble_transport_is_ready(void)
{
	return true;
}

int spotflow_ble_transport_send_framed_message_on_connection(uint8_t type, uint8_t* sequence,
							     uint8_t* payload, size_t len,
							     uint32_t connection_generation)
{
	ARG_UNUSED(sequence);
	if (reconnect_before_send && payload[0] == SPOTFLOW_OTA_BLE_READ) {
		reconnect_before_send = false;
		reconnect();
	}
	if (connection_generation != g_spotflow_ble_transport_state.tx.connection_generation) {
		return -ENOTCONN;
	}

	zassert_equal(type, SPOTFLOW_MSG_ARTIFACT_REQUEST);
	zassert_true(len >= SPOTFLOW_OTA_BLE_HEADER_SIZE);

	/* Echo request correlation fields, then append the simulated reply body. */
	uint8_t response[SPOTFLOW_OTA_BLE_DATA_HEADER_SIZE +
			 CONFIG_SPOTFLOW_OTA_DOWNLOAD_BUFFER_SIZE];
	memcpy(response, payload, SPOTFLOW_OTA_BLE_HEADER_SIZE);
	response[0] |= SPOTFLOW_OTA_BLE_RESPONSE_FLAG;
	response[SPOTFLOW_OTA_BLE_HEADER_SIZE] = SPOTFLOW_OTA_BLE_STATUS_OK;
	size_t response_len = SPOTFLOW_OTA_BLE_RESPONSE_HEADER_SIZE;
	if (reconnect_operation == payload[0]) {
		reconnect_operation = 0;
		reconnect();
		/* No reply: the entire reconnect occurs before the next readiness poll. */
		return 0;
	}
	if (payload[0] != SPOTFLOW_OTA_BLE_OPEN &&
	    sys_get_le32(payload + 1) != gateway_transfer_id) {
		stale_requests++;
		response[SPOTFLOW_OTA_BLE_HEADER_SIZE] = SPOTFLOW_OTA_BLE_STATUS_INVALID_REQUEST;
		spotflow_ble_artifact_receive(response, response_len);
		return 0;
	}

	switch (payload[0]) {
	case SPOTFLOW_OTA_BLE_OPEN:
		opens++;
		gateway_transfer_id = sys_get_le32(payload + 1);
		if (opens == 1) {
			first_transfer_id = sys_get_le32(payload + 1);
		} else if (expect_same_transfer) {
			zassert_equal(sys_get_le32(payload + 1), first_transfer_id);
		}
		if (silent_opens > 0) {
			silent_opens--;
			return 0;
		}
		sys_put_le32(ARTIFACT_SIZE, response + SPOTFLOW_OTA_BLE_RESPONSE_HEADER_SIZE);
		response_len = SPOTFLOW_OTA_BLE_DATA_HEADER_SIZE;
		break;
	case SPOTFLOW_OTA_BLE_READ: {
		size_t offset = sys_get_le32(payload + SPOTFLOW_OTA_BLE_HEADER_SIZE);
		size_t count =
			sys_get_le16(payload + SPOTFLOW_OTA_BLE_HEADER_SIZE + sizeof(uint32_t));

		if (opens > 1 && first_read_offset == SIZE_MAX) {
			first_read_offset = offset;
		}
		if (fail_read && offset == 64) {
			fail_read = false;
			return -ENOTCONN;
		}

		zassert_true(count <= CONFIG_SPOTFLOW_OTA_DOWNLOAD_BUFFER_SIZE);
		sys_put_le32(offset + (invalid_offset ? 1 : 0),
			     response + SPOTFLOW_OTA_BLE_RESPONSE_HEADER_SIZE);
		for (size_t i = 0; i < count; i++) {
			response[SPOTFLOW_OTA_BLE_DATA_HEADER_SIZE + i] = (uint8_t)(offset + i);
		}
		response_len = SPOTFLOW_OTA_BLE_DATA_HEADER_SIZE + count;
		break;
	}
	case SPOTFLOW_OTA_BLE_CLOSE:
		if (payload[SPOTFLOW_OTA_BLE_CLOSE_SIZE - 1] == SPOTFLOW_OTA_BLE_CLOSE_CONSUMED &&
		    fail_close) {
			fail_close = false;
			return -ENOTCONN;
		}
		break;
	default:
		zassert_unreachable("Unexpected artifact request operation");
	}

	/* A stale response must not release the outstanding request. */
	response[1] ^= 0x80;
	spotflow_ble_artifact_receive(response, response_len);
	response[1] ^= 0x80;
	spotflow_ble_artifact_receive(response, response_len);
	spotflow_ble_artifact_receive(response, response_len);
	if (reconnect_after_reply && payload[0] == SPOTFLOW_OTA_BLE_READ) {
		reconnect_after_reply = false;
		reconnect();
	}
	return 0;
}

ZTEST(ble_downloader, test_duplicate_and_stale_responses_do_not_duplicate_callbacks)
{
	zassert_ok(download());
	zassert_equal(consumed, ARTIFACT_SIZE);
	zassert_equal(final_blocks, 1);
}

ZTEST(ble_downloader, test_disconnect_resumes_at_consumed_offset)
{
	fail_read = true;
	zassert_ok(download());
	zassert_equal(first_read_offset, 64);
	zassert_equal(consumed, ARTIFACT_SIZE);
	zassert_equal(final_blocks, 1);
}

ZTEST(ble_downloader, test_lost_close_reply_does_not_repeat_final_callback)
{
	fail_close = true;
	zassert_ok(download());
	zassert_equal(opens, 2);
	zassert_equal(consumed, ARTIFACT_SIZE);
	zassert_equal(final_blocks, 1);
}

ZTEST(ble_downloader, test_fast_reconnect_during_read_reopens_without_stale_requests)
{
	reconnect_operation = SPOTFLOW_OTA_BLE_READ;
	zassert_ok(download());
	zassert_equal(opens, 2);
	zassert_equal(first_read_offset, 0);
	zassert_equal(stale_requests, 0);
	zassert_equal(consumed, ARTIFACT_SIZE);
	zassert_equal(final_blocks, 1);
}

ZTEST(ble_downloader, test_fast_reconnect_during_close_does_not_repeat_final_callback)
{
	reconnect_operation = SPOTFLOW_OTA_BLE_CLOSE;
	zassert_ok(download());
	zassert_equal(opens, 2);
	zassert_equal(first_read_offset, SIZE_MAX);
	zassert_equal(stale_requests, 0);
	zassert_equal(consumed, ARTIFACT_SIZE);
	zassert_equal(final_blocks, 1);
}

ZTEST(ble_downloader, test_reconnect_between_exchanges_resumes_after_consumed_block)
{
	reconnect_after_block = true;
	zassert_ok(download());
	zassert_equal(opens, 2);
	zassert_equal(first_read_offset, 64);
	zassert_equal(stale_requests, 0);
	zassert_equal(consumed, ARTIFACT_SIZE);
	zassert_equal(final_blocks, 1);
}

ZTEST(ble_downloader, test_reconnect_before_send_reopens_instead_of_using_old_transfer)
{
	reconnect_before_send = true;
	zassert_ok(download());
	zassert_equal(opens, 2);
	zassert_equal(first_read_offset, 0);
	zassert_equal(stale_requests, 0);
	zassert_equal(consumed, ARTIFACT_SIZE);
}

ZTEST(ble_downloader, test_reconnect_with_queued_reply_rejects_old_exchange_success)
{
	reconnect_after_reply = true;
	zassert_ok(download());
	zassert_equal(opens, 2);
	zassert_equal(first_read_offset, 0);
	zassert_equal(stale_requests, 0);
	zassert_equal(consumed, ARTIFACT_SIZE);
	zassert_equal(final_blocks, 1);
}

ZTEST(ble_downloader, test_mismatched_offset_is_not_delivered)
{
	invalid_offset = true;
	zassert_equal(download(), -EPROTO);
	zassert_equal(consumed, 0);
}

ZTEST(ble_downloader, test_cancel_interrupts_wait_and_closes_transfer)
{
	cancel_after_block = true;
	zassert_equal(download(), -ECANCELED);
	zassert_equal(consumed, 64);
	zassert_equal(final_blocks, 0);
}

ZTEST(ble_downloader, test_pause_resumes_without_repeating_consumed_bytes)
{
	pause_after_block = true;
	zassert_ok(download());
	zassert_equal(first_read_offset, 64);
	zassert_equal(consumed, ARTIFACT_SIZE);
	zassert_equal(final_blocks, 1);
}

ZTEST_SUITE(ble_downloader, NULL, NULL, before_test, NULL, NULL);

ZTEST(ble_downloader, test_missing_reply_retransmits_within_exchange)
{
	silent_opens = 1;
	expect_same_transfer = true;
	zassert_ok(download());
	zassert_equal(opens, 2);
	zassert_equal(consumed, ARTIFACT_SIZE);
}

ZTEST(ble_downloader, test_exchange_timeout_starts_new_attempt)
{
	/* Two unanswered OPEN sends exhaust the three-second exchange budget. */
	silent_opens = 2;
	zassert_ok(download());
	zassert_equal(opens, 3);
	zassert_equal(consumed, ARTIFACT_SIZE);
	zassert_equal(final_blocks, 1);
}

static void resume_download(struct k_work* work)
{
	ARG_UNUSED(work);
	zassert_ok(spotflow_resume_download(&downloader));
}

static void consume(const struct spotflow_artifact_block* block,
		    struct spotflow_downloader* instance, void* context)
{
	ARG_UNUSED(context);
	zassert_equal(block->offset, consumed);
	for (size_t i = 0; i < block->data_len; i++) {
		zassert_equal(block->data[i], (uint8_t)(consumed + i));
	}
	consumed += block->data_len;
	final_blocks += block->is_last;
	if (reconnect_after_block) {
		reconnect_after_block = false;
		reconnect();
	}
	if (cancel_after_block) {
		zassert_ok(spotflow_cancel_download(instance));
	}
	if (pause_after_block) {
		pause_after_block = false;
		zassert_ok(spotflow_pause_download(instance));
		k_work_schedule(&resume_work, K_MSEC(10));
	}
}

static void before_test(void* fixture)
{
	ARG_UNUSED(fixture);
	k_mutex_init(&g_spotflow_ble_transport_state.lock);
	g_spotflow_ble_transport_state.tx.conn = (struct bt_conn*)&connection_tokens[0];
	g_spotflow_ble_transport_state.tx.notifications_enabled = true;
	g_spotflow_ble_transport_state.tx.connection_generation = 0;
	zassert_ok(spotflow_init_downloader(&downloader));
	consumed = 0;
	first_read_offset = SIZE_MAX;
	final_blocks = 0;
	opens = 0;
	fail_read = false;
	fail_close = false;
	invalid_offset = false;
	cancel_after_block = false;
	pause_after_block = false;
	silent_opens = 0;
	first_transfer_id = 0;
	expect_same_transfer = false;
	reconnect_operation = 0;
	reconnect_after_reply = false;
	reconnect_after_block = false;
	reconnect_before_send = false;
	gateway_transfer_id = 0;
	stale_requests = 0;
}

static void reconnect(void)
{
	k_mutex_lock(&g_spotflow_ble_transport_state.lock, K_FOREVER);
	g_spotflow_ble_transport_state.tx.connection_generation++;
	g_spotflow_ble_transport_state.tx.conn = (struct bt_conn*)&connection_tokens[1];
	k_mutex_unlock(&g_spotflow_ble_transport_state.lock);
	gateway_transfer_id = 0;
}

static int download(void)
{
	const struct spotflow_download_request request = {
		.url = "https://example.com/artifact",
		.secret = "secret",
	};
	return spotflow_download_artifact(&downloader, &request, consume, NULL);
}
