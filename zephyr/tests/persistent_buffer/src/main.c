#include <stdio.h>
#include <string.h>

#include <zephyr/fs/fcb.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include <spotflow/persistent_buffer.h>

#include "logging/spotflow_log_backend.h"
#include "metrics/spotflow_metrics_aggregator.h"
#include "metrics/spotflow_metrics_backend.h"
#include "metrics/spotflow_metrics_heartbeat.h"
#include "net/spotflow_session_metadata.h"
#include "net/spotflow_transport.h"
#include "persistent_buffer/spotflow_persistent_buffer.h"

#ifdef PARTITION_ID
#define TEST_PARTITION_ID PARTITION_ID(spotflow_buffer_partition)
#else
#define TEST_PARTITION_ID FIXED_PARTITION_ID(spotflow_buffer_partition)
#endif

/* Must match spotflow_persistent_buffer.c */
#define TEST_PB_MAGIC 0x53465042

#define MAX_SENT 256
#define MAX_SENT_LEN 32
#define QUEUE_SIZE 16

K_MSGQ_DEFINE(g_spotflow_logs_msgq, sizeof(struct spotflow_log_msg*), QUEUE_SIZE, 1);
K_MSGQ_DEFINE(g_spotflow_metrics_msgq, sizeof(struct spotflow_metric_msg*), QUEUE_SIZE,
	      sizeof(void*));

static char sent[MAX_SENT][MAX_SENT_LEN];
static size_t sent_count;
static bool transport_ready;
static int transport_fail_countdown;
static uint64_t fake_run_id;
static char pending_heartbeat[MAX_SENT_LEN];
static int64_t reported_dropped;
static struct spotflow_metric_int* dummy_metric = (struct spotflow_metric_int*)&reported_dropped;

/* Fakes of SDK dependencies */

uint64_t spotflow_session_metadata_get_run_id(void)
{
	return fake_run_id;
}

int spotflow_session_metadata_encode_for_replay(uint8_t* buffer, size_t buffer_len,
						size_t* cbor_data_len)
{
	int len = snprintf((char*)buffer, buffer_len, "S%llu", (unsigned long long)fake_run_id);

	*cbor_data_len = len;
	return 0;
}

int spotflow_session_metadata_send(void)
{
	char buffer[MAX_SENT_LEN];
	int len = snprintf(buffer, sizeof(buffer), "CUR%llu", (unsigned long long)fake_run_id);

	return spotflow_transport_send_ingest_cbor((uint8_t*)buffer, len);
}

bool spotflow_transport_is_ready(void)
{
	return transport_ready;
}

int spotflow_transport_send_ingest_cbor(uint8_t* payload, size_t len)
{
	if (transport_fail_countdown > 0 && --transport_fail_countdown == 0) {
		return -EAGAIN;
	}

	zassert_true(sent_count < MAX_SENT, "Too many sent messages");
	size_t copy = MIN(len, MAX_SENT_LEN - 1);

	memcpy(sent[sent_count], payload, copy);
	sent[sent_count][copy] = '\0';
	sent_count++;
	return 0;
}

bool spotflow_metrics_heartbeat_take(uint8_t* buffer, size_t buffer_len, size_t* len)
{
	size_t heartbeat_len = strlen(pending_heartbeat);

	if (heartbeat_len == 0 || heartbeat_len > buffer_len) {
		return false;
	}

	memcpy(buffer, pending_heartbeat, heartbeat_len);
	*len = heartbeat_len;
	pending_heartbeat[0] = '\0';
	return true;
}

int spotflow_register_metric_int(const char* name, enum spotflow_agg_interval agg_interval,
				 struct spotflow_metric_int** metric_out)
{
	ARG_UNUSED(name);
	ARG_UNUSED(agg_interval);
	*metric_out = dummy_metric;
	return 0;
}

int spotflow_report_metric_int(struct spotflow_metric_int* metric, int64_t value)
{
	zassert_equal_ptr(metric, dummy_metric);
	reported_dropped += value;
	return 0;
}

void spotflow_pb_wake_persister(void)
{
}

/* Helpers */

static uint8_t* copy_payload(const char* text, size_t len)
{
	uint8_t* payload = k_malloc(len);

	zassert_not_null(payload);
	memset(payload, 'x', len);
	memcpy(payload, text, MIN(strlen(text), len));
	return payload;
}

static void enqueue_log_len(const char* text, size_t len)
{
	struct spotflow_log_msg* msg = k_malloc(sizeof(*msg));

	zassert_not_null(msg);
	msg->len = len;
	msg->payload = copy_payload(text, len);
	zassert_ok(k_msgq_put(&g_spotflow_logs_msgq, &msg, K_NO_WAIT));
}

static void enqueue_log(const char* text)
{
	enqueue_log_len(text, strlen(text));
}

static void enqueue_metric(const char* text)
{
	struct spotflow_metric_msg* msg = k_malloc(sizeof(*msg));

	zassert_not_null(msg);
	msg->len = strlen(text);
	msg->payload = copy_payload(text, msg->len);
	zassert_ok(k_msgq_put(&g_spotflow_metrics_msgq, &msg, K_NO_WAIT));
}

static void drain(void)
{
	int rc;

	do {
		rc = spotflow_pb_send_next();
	} while (rc == 1);
	zassert_equal(rc, 0, "send_next failed: %d", rc);
}

static void reboot(uint64_t run_id)
{
	fake_run_id = run_id;
	zassert_ok(spotflow_pb_init());
}

static void assert_sent(const char* const* expected, size_t count)
{
	zassert_equal(sent_count, count, "Expected %zu sent messages, got %zu", count, sent_count);
	for (size_t i = 0; i < count; i++) {
		zassert_str_equal(sent[i], expected[i], "Message %zu: expected %s, got %s", i,
				  expected[i], sent[i]);
	}
}

static struct spotflow_persistent_buffer_stats get_stats(void)
{
	struct spotflow_persistent_buffer_stats stats;

	zassert_ok(spotflow_persistent_buffer_get_stats(&stats));
	return stats;
}

static size_t sector_size(void)
{
	struct flash_sector sectors[64];
	uint32_t cnt = ARRAY_SIZE(sectors);

	zassert_ok(flash_area_get_sectors(TEST_PARTITION_ID, &cnt, sectors));
	return sectors[0].fs_size;
}

static void erase_partition(void)
{
	const struct flash_area* fa;

	zassert_ok(flash_area_open(TEST_PARTITION_ID, &fa));
	zassert_ok(flash_area_flatten(fa, 0, fa->fa_size));
	flash_area_close(fa);
}

/* Opens an independent FCB instance on the buffer partition. */
static void open_raw_fcb(struct fcb* fcb, struct flash_sector* sectors, uint32_t sector_cnt,
			 uint8_t version)
{
	uint32_t cnt = sector_cnt;

	zassert_ok(flash_area_get_sectors(TEST_PARTITION_ID, &cnt, sectors));
	memset(fcb, 0, sizeof(*fcb));
	fcb->f_magic = TEST_PB_MAGIC;
	fcb->f_version = version;
	fcb->f_sectors = sectors;
	fcb->f_sector_cnt = cnt;
	zassert_ok(fcb_init(TEST_PARTITION_ID, fcb));
}

static void before_each(void* fixture)
{
	ARG_UNUSED(fixture);
	struct spotflow_log_msg* log;
	struct spotflow_metric_msg* metric;

	while (k_msgq_get(&g_spotflow_logs_msgq, &log, K_NO_WAIT) == 0) {
		k_free(log->payload);
		k_free(log);
	}
	while (k_msgq_get(&g_spotflow_metrics_msgq, &metric, K_NO_WAIT) == 0) {
		k_free(metric->payload);
		k_free(metric);
	}

	sent_count = 0;
	transport_ready = true;
	transport_fail_countdown = 0;
	pending_heartbeat[0] = '\0';
	reported_dropped = 0;

	erase_partition();
	reboot(1);
}

ZTEST(persistent_buffer, test_sends_in_order_heartbeat_metrics_logs)
{
	strcpy(pending_heartbeat, "H1");
	enqueue_log("L1");
	enqueue_metric("M1");
	enqueue_log("L2");

	zassert_equal(spotflow_pb_persist(), 4);
	zassert_equal(k_msgq_num_used_get(&g_spotflow_logs_msgq), 0);
	zassert_equal(k_msgq_num_used_get(&g_spotflow_metrics_msgq), 0);
	zassert_equal(get_stats().messages_stored, 4);

	drain();

	/* Same boot as the connection: no session metadata is sent. */
	const char* expected[] = { "H1", "M1", "L1", "L2" };

	assert_sent(expected, ARRAY_SIZE(expected));
	zassert_equal(get_stats().messages_stored, 0);
}

ZTEST(persistent_buffer, test_data_survives_reboot_with_its_session)
{
	enqueue_log("L1");
	enqueue_log("L2");
	zassert_equal(spotflow_pb_persist(), 2);

	reboot(2);
	enqueue_log("L3");
	zassert_equal(spotflow_pb_persist(), 1);
	zassert_equal(get_stats().messages_stored, 3);

	spotflow_pb_on_session_start();
	drain();

	const char* expected[] = { "S1", "L1", "L2", "CUR2", "L3" };

	assert_sent(expected, ARRAY_SIZE(expected));
}

ZTEST(persistent_buffer, test_replays_three_boots)
{
	enqueue_log("A");
	zassert_equal(spotflow_pb_persist(), 1);
	reboot(2);
	enqueue_log("B");
	zassert_equal(spotflow_pb_persist(), 1);
	reboot(3);
	enqueue_log("C");
	zassert_equal(spotflow_pb_persist(), 1);

	drain();

	const char* expected[] = { "S1", "A", "S2", "B", "CUR3", "C" };

	assert_sent(expected, ARRAY_SIZE(expected));
}

ZTEST(persistent_buffer, test_new_connection_resends_old_session)
{
	enqueue_log("L1");
	enqueue_log("L2");
	zassert_equal(spotflow_pb_persist(), 2);
	reboot(2);

	zassert_equal(spotflow_pb_send_next(), 1);
	/* Reconnect: the gateway now has the current session again. */
	spotflow_pb_on_session_start();
	drain();

	const char* expected[] = { "S1", "L1", "S1", "L2" };

	assert_sent(expected, ARRAY_SIZE(expected));
}

ZTEST(persistent_buffer, test_keeps_messages_while_transport_not_ready)
{
	enqueue_log("L1");
	zassert_equal(spotflow_pb_persist(), 1);

	transport_ready = false;
	zassert_equal(spotflow_pb_send_next(), -EAGAIN);
	zassert_equal(sent_count, 0);

	transport_ready = true;
	drain();

	const char* expected[] = { "L1" };

	assert_sent(expected, ARRAY_SIZE(expected));
}

ZTEST(persistent_buffer, test_failed_send_is_retried_without_duplicates)
{
	enqueue_log("L1");
	enqueue_log("L2");
	zassert_equal(spotflow_pb_persist(), 2);

	zassert_equal(spotflow_pb_send_next(), 1);
	transport_fail_countdown = 1;
	zassert_equal(spotflow_pb_send_next(), -EAGAIN);
	drain();

	const char* expected[] = { "L1", "L2" };

	assert_sent(expected, ARRAY_SIZE(expected));
}

ZTEST(persistent_buffer, test_failed_session_send_is_retried)
{
	enqueue_log("L1");
	zassert_equal(spotflow_pb_persist(), 1);
	reboot(2);

	transport_fail_countdown = 1;
	zassert_equal(spotflow_pb_send_next(), -EAGAIN);
	drain();

	const char* expected[] = { "S1", "L1" };

	assert_sent(expected, ARRAY_SIZE(expected));
}

ZTEST(persistent_buffer, test_full_ring_drops_oldest_and_reports_it)
{
	char text[16];
	uint32_t written = 0;
	uint32_t dropped_before = get_stats().messages_dropped;

	/* Fill the ring until it starts dropping, then write a few more sectors' worth. */
	while (get_stats().messages_dropped == dropped_before || written % 64 != 0) {
		snprintf(text, sizeof(text), "N%u", written++);
		enqueue_log_len(text, 100);
		if (k_msgq_num_used_get(&g_spotflow_logs_msgq) == QUEUE_SIZE / 2) {
			zassert_true(spotflow_pb_persist() > 0);
		}
		zassert_true(written < 100000);
	}
	zassert_true(spotflow_pb_persist() >= 0);

	struct spotflow_persistent_buffer_stats stats = get_stats();

	uint32_t dropped = stats.messages_dropped - dropped_before;

	zassert_true(dropped > 0);
	zassert_equal(dropped + stats.messages_stored, written);
	zassert_equal(reported_dropped, dropped);

	reboot(2);
	drain();

	/* The oldest remaining sector starts with the session record of boot 1. */
	zassert_true(sent_count > 2);
	zassert_str_equal(sent[0], "S1");
	snprintf(text, sizeof(text), "N%u", dropped);
	zassert_equal(strncmp(sent[1], text, strlen(text)), 0, "Expected %s, got %s", text,
		      sent[1]);
	snprintf(text, sizeof(text), "N%u", written - 1);
	zassert_equal(strncmp(sent[sent_count - 1], text, strlen(text)), 0);
	zassert_equal(sent_count, 1 + stats.messages_stored);
}

ZTEST(persistent_buffer, test_every_sector_starts_with_a_session_record)
{
	char text[16];
	uint32_t written = 0;

	/* Write more than two sectors in boot 1. */
	while (get_stats().bytes_used <= sector_size() * 2) {
		snprintf(text, sizeof(text), "N%u", written++);
		enqueue_log_len(text, 100);
		zassert_equal(spotflow_pb_persist(), 1);
	}

	/* Send only part of boot 1, then reboot: no read position survives, the whole first
	 * sector is sent again, still attributed to boot 1. */
	reboot(2);
	zassert_equal(spotflow_pb_send_next(), 1);
	reboot(3);
	drain();

	zassert_str_equal(sent[0], "S1");
	zassert_str_equal(sent[2], "S1");
	zassert_equal(strncmp(sent[3], "N0", 2), 0);
	zassert_equal(sent_count, 3 + written);
}

ZTEST(persistent_buffer, test_sent_sectors_are_erased)
{
	char text[16];
	uint32_t written = 0;
	struct spotflow_persistent_buffer_stats empty = get_stats();

	while (get_stats().bytes_used < empty.bytes_free / 2) {
		snprintf(text, sizeof(text), "N%u", written++);
		enqueue_log_len(text, 100);
		zassert_equal(spotflow_pb_persist(), 1);
	}

	drain();
	zassert_equal(sent_count, written);

	struct spotflow_persistent_buffer_stats stats = get_stats();

	/* Only the active sector remains. */
	zassert_true(stats.bytes_used < empty.bytes_free / 4, "%zu bytes used", stats.bytes_used);
	zassert_equal(stats.messages_stored, 0);
}

ZTEST(persistent_buffer, test_entry_cut_by_power_loss_is_skipped)
{
	struct fcb fcb;
	struct flash_sector sectors[64];
	struct fcb_entry loc;
	uint8_t partial[16];

	enqueue_log("L1");
	zassert_equal(spotflow_pb_persist(), 1);

	/* Reserve an entry and write part of it, but never finish it. */
	open_raw_fcb(&fcb, sectors, ARRAY_SIZE(sectors), SPOTFLOW_PB_FORMAT_VERSION);
	zassert_ok(fcb_append(&fcb, 64, &loc));
	memset(partial, 0, sizeof(partial));
	partial[0] = SPOTFLOW_PB_KIND_LOG;
	sys_put_le16(40, &partial[1]);
	zassert_ok(fcb_flash_write(&fcb, loc.fe_sector, loc.fe_data_off, partial, sizeof(partial)));

	reboot(1);
	enqueue_log("L2");
	zassert_equal(spotflow_pb_persist(), 1);
	drain();

	const char* expected[] = { "L1", "L2" };

	assert_sent(expected, ARRAY_SIZE(expected));
}

ZTEST(persistent_buffer, test_other_format_version_is_erased)
{
	struct fcb fcb;
	struct flash_sector sectors[64];
	struct fcb_entry loc;
	uint8_t data[16] = { SPOTFLOW_PB_KIND_LOG, 13, 0 };

	erase_partition();
	open_raw_fcb(&fcb, sectors, ARRAY_SIZE(sectors), SPOTFLOW_PB_FORMAT_VERSION + 1);
	zassert_ok(fcb_append(&fcb, sizeof(data), &loc));
	zassert_ok(fcb_flash_write(&fcb, loc.fe_sector, loc.fe_data_off, data, sizeof(data)));
	zassert_ok(fcb_append_finish(&fcb, &loc));

	reboot(1);
	zassert_equal(get_stats().messages_stored, 0);
	drain();
	zassert_equal(sent_count, 0);
}

ZTEST(persistent_buffer, test_clear_erases_stored_data)
{
	enqueue_log("L1");
	zassert_equal(spotflow_pb_persist(), 1);

	zassert_ok(spotflow_persistent_buffer_clear());
	zassert_equal(get_stats().messages_stored, 0);

	reboot(2);
	enqueue_log("L2");
	zassert_equal(spotflow_pb_persist(), 1);
	reboot(3);
	drain();

	/* Data written after clearing still carries its session. */
	const char* expected[] = { "S2", "L2" };

	assert_sent(expected, ARRAY_SIZE(expected));
}

ZTEST(persistent_buffer, test_flush_writes_queued_messages)
{
	enqueue_log("L1");
	enqueue_metric("M1");

	zassert_ok(spotflow_persistent_buffer_flush());
	zassert_equal(k_msgq_num_used_get(&g_spotflow_logs_msgq), 0);
	zassert_equal(get_stats().messages_stored, 2);
}

ZTEST(persistent_buffer, test_oversized_message_is_dropped)
{
	struct spotflow_persistent_buffer_stats before = get_stats();

	enqueue_log_len("BIG", sector_size());
	enqueue_log("L1");
	zassert_equal(spotflow_pb_persist(), 1);
	zassert_equal(get_stats().messages_dropped, before.messages_dropped + 1);

	drain();

	const char* expected[] = { "L1" };

	assert_sent(expected, ARRAY_SIZE(expected));
}

ZTEST(persistent_buffer, test_large_queue_spans_several_entries)
{
	char text[16];

	for (int i = 0; i < QUEUE_SIZE; i++) {
		snprintf(text, sizeof(text), "N%d", i);
		enqueue_log_len(text, 200);
	}
	zassert_equal(spotflow_pb_persist(), QUEUE_SIZE);

	drain();
	zassert_equal(sent_count, QUEUE_SIZE);
	for (int i = 0; i < QUEUE_SIZE; i++) {
		snprintf(text, sizeof(text), "N%d", i);
		zassert_equal(strncmp(sent[i], text, strlen(text)), 0);
	}
}

ZTEST_SUITE(persistent_buffer, NULL, NULL, before_each, NULL, NULL);
