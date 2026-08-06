#include <stddef.h>

#include <zephyr/ztest.h>

#include <spotflow/ota.h>

#include "ota/spotflow_ota.h"
#include "ota/core/spotflow_ota_state.h"
#include "spotflow_ota_test_fakes.h"
#include "spotflow_ota_test_settings.h"

ZTEST(spotflow_ota_public_api, test_public_headers_expose_expected_ota_types)
{
	struct spotflow_download_request request = {
		.url = "https://example.com/firmware.bin",
		.secret = "secret",
	};
	struct spotflow_firmware_info info = {
		.attempt_id = 1,
		.slug = "main",
		.is_main = true,
		.download_request = &request,
		.version = "1.0.0",
	};

	zassert_equal(info.attempt_id, 1);
	zassert_true(info.is_main);
	zassert_equal(info.download_request, &request);
}

ZTEST(spotflow_ota_public_api, test_public_headers_expose_expected_downloader_types)
{
	struct spotflow_artifact_block block = {
		.offset = 0,
		.data = NULL,
		.data_len = 0,
		.is_last = true,
	};

	zassert_true(sizeof(struct spotflow_downloader) > 0);
	zassert_equal(block.offset, 0);
	zassert_true(block.is_last);
}

ZTEST(spotflow_ota_public_api, test_test_fakes_are_available_to_ota_suites)
{
	struct spotflow_ota_test_fake_transport* fake_transport;
	struct spotflow_ota_test_fake_callbacks* fake_callbacks;

	spotflow_ota_test_fakes_reset();
	fake_transport = spotflow_ota_test_fake_transport_get();
	fake_callbacks = spotflow_ota_test_fake_callbacks_get();

	zassert_not_null(fake_transport);
	zassert_not_null(fake_callbacks);
	zassert_equal(fake_transport->publish_count, 0);
	zassert_equal(fake_transport->last_payload, NULL);
	zassert_equal(fake_transport->last_payload_len, 0);
	zassert_equal(fake_transport->publish_result, 0);
	zassert_equal(fake_callbacks->next_handle_result, SPOTFLOW_OTA_RESULT_SUCCEEDED);
}

ZTEST(spotflow_ota_public_api, test_ota_cancellation_api_is_linkable)
{
	zassert_false(spotflow_is_update_canceled());
}

static void before_each(void* fixture)
{
	ARG_UNUSED(fixture);

	spotflow_ota_test_settings_reset();
	spotflow_ota_test_fakes_reset();
	spotflow_ota_state_reset();
	zassert_ok(spotflow_ota_init());
}

ZTEST_SUITE(spotflow_ota_public_api, NULL, NULL, before_each, NULL, NULL);
