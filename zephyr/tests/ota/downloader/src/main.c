#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include <spotflow/downloader.h>

#include "ota/spotflow_ota_downloader.h"
#include "ota/spotflow_ota_url.h"

#include "spotflow_ota_downloader_transport_fake.h"

LOG_MODULE_REGISTER(spotflow_ota, CONFIG_LOG_DEFAULT_LEVEL);

static const uint8_t sample_payload[] = { 0x01, 0x02, 0x03, 0x04 };
static size_t received_bytes;
static bool received_last_block;

static void reset_test_state(void)
{
	struct spotflow_ota_downloader_transport_fake* fake =
	    spotflow_ota_downloader_transport_fake_get();

	spotflow_ota_downloader_transport_fake_reset(fake);
	received_bytes = 0;
	received_last_block = false;
}

static void capture_block_cb(const struct spotflow_artifact_block* block,
			     struct spotflow_downloader* downloader, void* callback_ctx)
{
	ARG_UNUSED(downloader);
	ARG_UNUSED(callback_ctx);

	received_bytes += block->data_len;
	received_last_block = block->is_last;
}

static void cancel_after_delay(void* downloader_ptr, void* arg2, void* arg3)
{
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	k_sleep(K_MSEC(20));
	zassert_ok(spotflow_cancel_download(downloader_ptr));
}

static void before_each(void* fixture)
{
	ARG_UNUSED(fixture);

	reset_test_state();
}

ZTEST(spotflow_ota_downloader, test_parse_http_and_https_urls)
{
	struct ota_url parsed;

	zassert_ok(spotflow_ota_parse_url("http://example.com/firmware.bin", &parsed));
	zassert_false(parsed.tls);
	zassert_equal(parsed.port, 80);
	zassert_equal(strcmp(parsed.host, "example.com"), 0);
	zassert_equal(strcmp(parsed.path, "/firmware.bin"), 0);

	zassert_ok(spotflow_ota_parse_url("https://example.com:8443/custom/path?query=1", &parsed));
	zassert_true(parsed.tls);
	zassert_equal(parsed.port, 8443);
	zassert_equal(strcmp(parsed.host, "example.com"), 0);
	zassert_equal(strcmp(parsed.path, "/custom/path?query=1"), 0);
}

ZTEST(spotflow_ota_downloader, test_reject_unsupported_url_scheme)
{
	struct ota_url parsed;

	zassert_equal(spotflow_ota_parse_url("ftp://example.com/image.bin", &parsed), -EINVAL);
}

ZTEST(spotflow_ota_downloader, test_authorization_header_contains_ota_secret)
{
	char header[96];
	struct spotflow_ota_downloader_transport_fake* fake =
	    spotflow_ota_downloader_transport_fake_get();
	SPOTFLOW_DEFINE_DOWNLOADER(downloader);
	struct spotflow_download_request request = {
		.url = "https://example.com/firmware.bin",
		.secret = "test-secret-value",
	};

	zassert_ok(spotflow_ota_downloader_build_authorization_header(request.secret, header,
								      sizeof(header)));
	zassert_equal(strcmp(header, "Authorization: OtaSecret test-secret-value\r\n"), 0);

	fake->payload = sample_payload;
	fake->payload_len = sizeof(sample_payload);

	zassert_ok(spotflow_download_artifact(&downloader, &request, capture_block_cb, NULL));
	zassert_equal(strcmp(fake->last_authorization_header,
			     "Authorization: OtaSecret test-secret-value\r\n"),
		      0);
}

ZTEST(spotflow_ota_downloader, test_cancel_stops_download)
{
	struct spotflow_ota_downloader_transport_fake* fake =
	    spotflow_ota_downloader_transport_fake_get();
	SPOTFLOW_DEFINE_DOWNLOADER(downloader);
	struct spotflow_download_request request = {
		.url = "https://example.com/firmware.bin",
		.secret = "secret",
	};
	struct k_thread cancel_thread;
	k_thread_stack_t cancel_stack[1024];
	int download_result = 0;

	fake->block_until_cancel = true;

	k_thread_create(&cancel_thread, cancel_stack, K_THREAD_STACK_SIZEOF(cancel_stack),
			cancel_after_delay, &downloader, NULL, NULL, K_PRIO_PREEMPT(0), 0,
			K_NO_WAIT);

	download_result = spotflow_download_artifact(&downloader, &request, capture_block_cb, NULL);

	k_thread_join(&cancel_thread, K_FOREVER);

	zassert_equal(download_result, -ECANCELED);
	zassert_true(fake->cancel_observed);
	zassert_equal(spotflow_get_downloader_state(&downloader),
		      SPOTFLOW_DOWNLOADER_STATE_INACTIVE);
}

ZTEST(spotflow_ota_downloader, test_transient_failure_resumes_with_range)
{
	struct spotflow_ota_downloader_transport_fake* fake =
	    spotflow_ota_downloader_transport_fake_get();
	SPOTFLOW_DEFINE_DOWNLOADER(downloader);
	struct spotflow_download_request request = {
		.url = "https://example.com/firmware.bin",
		.secret = "secret",
	};

	fake->payload = sample_payload;
	fake->payload_len = sizeof(sample_payload);
	fake->partial_transient_fail_after_bytes = 2;

	zassert_ok(spotflow_download_artifact(&downloader, &request, capture_block_cb, NULL));
	zassert_equal(fake->call_count, 2);
	zassert_equal(fake->last_range_start, 2U);
	zassert_equal(received_bytes, sizeof(sample_payload));
	zassert_true(received_last_block);
}

ZTEST(spotflow_ota_downloader, test_partial_ebadmsg_resumes_with_range)
{
	struct spotflow_ota_downloader_transport_fake* fake =
	    spotflow_ota_downloader_transport_fake_get();
	SPOTFLOW_DEFINE_DOWNLOADER(downloader);
	struct spotflow_download_request request = {
		.url = "https://example.com/firmware.bin",
		.secret = "secret",
	};

	fake->payload = sample_payload;
	fake->payload_len = sizeof(sample_payload);
	fake->partial_transient_fail_after_bytes = 2;
	fake->partial_fail_errno = -EBADMSG;

	zassert_ok(spotflow_download_artifact(&downloader, &request, capture_block_cb, NULL));
	zassert_equal(fake->call_count, 2);
	zassert_equal(fake->last_range_start, 2U);
	zassert_equal(received_bytes, sizeof(sample_payload));
	zassert_true(received_last_block);
}

ZTEST(spotflow_ota_downloader, test_partial_econnreset_resumes_with_range)
{
	struct spotflow_ota_downloader_transport_fake* fake =
	    spotflow_ota_downloader_transport_fake_get();
	SPOTFLOW_DEFINE_DOWNLOADER(downloader);
	struct spotflow_download_request request = {
		.url = "https://example.com/firmware.bin",
		.secret = "secret",
	};

	fake->payload = sample_payload;
	fake->payload_len = sizeof(sample_payload);
	fake->partial_transient_fail_after_bytes = 2;
	fake->partial_fail_errno = -ECONNRESET;

	zassert_ok(spotflow_download_artifact(&downloader, &request, capture_block_cb, NULL));
	zassert_equal(fake->call_count, 2);
	zassert_equal(fake->last_range_start, 2U);
	zassert_equal(received_bytes, sizeof(sample_payload));
	zassert_true(received_last_block);
}

ZTEST(spotflow_ota_downloader, test_ebadmsg_without_progress_is_fatal)
{
	struct spotflow_ota_downloader_transport_fake* fake =
	    spotflow_ota_downloader_transport_fake_get();
	SPOTFLOW_DEFINE_DOWNLOADER(downloader);
	struct spotflow_download_request request = {
		.url = "https://example.com/firmware.bin",
		.secret = "secret",
	};
	const int results[] = { -EBADMSG };

	spotflow_ota_downloader_transport_fake_set_results(fake, results, ARRAY_SIZE(results));

	zassert_equal(spotflow_download_artifact(&downloader, &request, capture_block_cb, NULL),
		      -EBADMSG);
	zassert_equal(fake->call_count, 1);
	zassert_equal(received_bytes, 0);
}

ZTEST(spotflow_ota_downloader, test_transient_errors_are_retried)
{
	struct spotflow_ota_downloader_transport_fake* fake =
	    spotflow_ota_downloader_transport_fake_get();
	SPOTFLOW_DEFINE_DOWNLOADER(downloader);
	struct spotflow_download_request request = {
		.url = "https://example.com/firmware.bin",
		.secret = "secret",
	};
	const int transient_results[] = { -EAGAIN, -EAGAIN, 0 };

	spotflow_ota_downloader_transport_fake_set_results(fake, transient_results,
							   ARRAY_SIZE(transient_results));
	fake->payload = sample_payload;
	fake->payload_len = sizeof(sample_payload);

	zassert_ok(spotflow_download_artifact(&downloader, &request, capture_block_cb, NULL));
	zassert_equal(fake->call_count, 3);
	zassert_equal(received_bytes, sizeof(sample_payload));
	zassert_true(received_last_block);
}

static void pause_and_resume_after_delay(void* downloader_ptr, void* arg2, void* arg3)
{
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	k_sleep(K_MSEC(20));
	zassert_ok(spotflow_pause_download(downloader_ptr));
	k_sleep(K_MSEC(20));
	zassert_ok(spotflow_resume_download(downloader_ptr));
}

ZTEST(spotflow_ota_downloader, test_pause_and_resume_block_download)
{
	struct spotflow_ota_downloader_transport_fake* fake =
	    spotflow_ota_downloader_transport_fake_get();
	SPOTFLOW_DEFINE_DOWNLOADER(downloader);
	struct spotflow_download_request request = {
		.url = "https://example.com/firmware.bin",
		.secret = "secret",
	};
	struct k_thread control_thread;
	k_thread_stack_t control_stack[1024];
	int download_result = 0;

	fake->block_until_pause = true;
	fake->payload = sample_payload;
	fake->payload_len = sizeof(sample_payload);

	k_thread_create(&control_thread, control_stack, K_THREAD_STACK_SIZEOF(control_stack),
			pause_and_resume_after_delay, &downloader, NULL, NULL, K_PRIO_PREEMPT(0), 0,
			K_NO_WAIT);

	download_result = spotflow_download_artifact(&downloader, &request, capture_block_cb, NULL);

	k_thread_join(&control_thread, K_FOREVER);

	zassert_equal(download_result, 0);
	zassert_true(fake->pause_observed);
	zassert_equal(received_bytes, sizeof(sample_payload));
	zassert_equal(spotflow_get_downloader_state(&downloader),
		      SPOTFLOW_DOWNLOADER_STATE_INACTIVE);
}

ZTEST(spotflow_ota_downloader, test_pause_invalid_when_inactive)
{
	SPOTFLOW_DEFINE_DOWNLOADER(downloader);

	zassert_equal(spotflow_pause_download(&downloader), -EINVAL);
	zassert_equal(spotflow_resume_download(&downloader), -EINVAL);
}

ZTEST_SUITE(spotflow_ota_downloader, NULL, NULL, before_each, NULL, NULL);
