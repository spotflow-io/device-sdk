#include <spotflow/downloader.h>

#include "net/transport/ble/spotflow_ble_transport_internal.h"

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

static void resume_download(struct k_work* work)
{
	ARG_UNUSED(work);
	zassert_ok(spotflow_resume_download(&downloader));
}

static K_WORK_DELAYABLE_DEFINE(resume_work, resume_download);

bool spotflow_ble_transport_is_ready(void)
{
	return true;
}

int spotflow_ble_transport_send_framed_message(uint8_t type, uint8_t* sequence, uint8_t* payload,
					       size_t len)
{
	ARG_UNUSED(sequence);
	zassert_equal(type, SPOTFLOW_MSG_ARTIFACT_REQUEST);
	zassert_true(len >= 10);
	uint8_t response[15 + CONFIG_SPOTFLOW_OTA_DOWNLOAD_BUFFER_SIZE];
	memcpy(response, payload, 10);
	response[1] |= 0x80;
	response[10] = 0;
	size_t response_len = 11;
	if (payload[1] == 1) {
		opens++;
		sys_put_le32(ARTIFACT_SIZE, response + 11);
		response_len = 15;
	} else if (payload[1] == 2) {
		size_t offset = sys_get_le32(payload + 10);
		size_t count = sys_get_le16(payload + 14);
		if (opens > 1 && first_read_offset == SIZE_MAX) {
			first_read_offset = offset;
		}
		if (fail_read && offset == 64) {
			fail_read = false;
			return -ENOTCONN;
		}
		zassert_true(count <= CONFIG_SPOTFLOW_OTA_DOWNLOAD_BUFFER_SIZE);
		sys_put_le32(offset + (invalid_offset ? 1 : 0), response + 11);
		for (size_t i = 0; i < count; i++) {
			response[15 + i] = (uint8_t)(offset + i);
		}
		response_len = 15 + count;
	} else if (payload[1] == 3 && payload[14] == 0 && fail_close) {
		fail_close = false;
		return -ENOTCONN;
	}
	/* A stale response must not release the outstanding request. */
	response[2] ^= 0x80;
	spotflow_ble_artifact_receive(response, response_len);
	response[2] ^= 0x80;
	spotflow_ble_artifact_receive(response, response_len);
	spotflow_ble_artifact_receive(response, response_len);
	return 0;
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
}

static int download(void)
{
	const struct spotflow_download_request request = {
		.url = "https://example.com/artifact",
		.secret = "secret",
	};
	return spotflow_download_artifact(&downloader, &request, consume, NULL);
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
