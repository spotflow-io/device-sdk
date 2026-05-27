#include <stddef.h>

#include <zephyr/ztest.h>

#include <spotflow/downloader.h>
#include <spotflow/ota.h>

#include "spotflow_ota_test_fakes.h"

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
	struct spotflow_ota_main_firmware_state state = {
		.phase = SPOTFLOW_OTA_PHASE_NOT_RUNNING,
		.is_paused = false,
		.result = SPOTFLOW_OTA_RESULT_PENDING,
	};

	zassert_equal(info.attempt_id, 1);
	zassert_true(info.is_main);
	zassert_equal(info.download_request, &request);
	zassert_equal(state.phase, SPOTFLOW_OTA_PHASE_NOT_RUNNING);
	zassert_equal(state.result, SPOTFLOW_OTA_RESULT_PENDING);
}

ZTEST(spotflow_ota_public_api, test_public_headers_expose_expected_downloader_types)
{
	SPOTFLOW_DEFINE_DOWNLOADER(downloader);
	struct spotflow_artifact_block block = {
		.offset = 0,
		.data = NULL,
		.data_len = 0,
		.is_last = true,
	};

	zassert_equal(downloader.state, SPOTFLOW_DOWNLOADER_STATE_INACTIVE);
	zassert_equal(block.offset, 0);
	zassert_true(block.is_last);
}

ZTEST(spotflow_ota_public_api, test_test_fakes_are_available_to_ota_suites)
{
	struct spotflow_ota_test_fake_mqtt *fake_mqtt;

	spotflow_ota_test_fakes_reset();
	fake_mqtt = spotflow_ota_test_fake_mqtt_get();

	zassert_not_null(fake_mqtt);
	zassert_equal(fake_mqtt->publish_count, 0);
	zassert_equal(fake_mqtt->last_payload, NULL);
	zassert_equal(fake_mqtt->last_payload_len, 0);
	zassert_equal(fake_mqtt->publish_result, 0);
}

ZTEST_SUITE(spotflow_ota_public_api, NULL, NULL, NULL, NULL, NULL);
