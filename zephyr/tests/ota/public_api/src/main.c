#include <errno.h>
#include <stddef.h>

#include <zephyr/ztest.h>

#include <spotflow/ota.h>

#include "net/spotflow_mqtt.h"
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
	struct spotflow_ota_test_fake_mqtt* fake_mqtt;
	struct spotflow_ota_test_fake_callbacks* fake_callbacks;

	spotflow_ota_test_fakes_reset();
	fake_mqtt = spotflow_ota_test_fake_mqtt_get();
	fake_callbacks = spotflow_ota_test_fake_callbacks_get();

	zassert_not_null(fake_mqtt);
	zassert_not_null(fake_callbacks);
	zassert_equal(fake_mqtt->publish_count, 0);
	zassert_equal(fake_mqtt->last_payload, NULL);
	zassert_equal(fake_mqtt->last_payload_len, 0);
	zassert_equal(fake_mqtt->publish_result, 0);
	zassert_equal(fake_callbacks->next_handle_result, SPOTFLOW_OTA_RESULT_SUCCEEDED);
}

int spotflow_mqtt_publish_ota_cbor_msg(uint8_t* payload, size_t len)
{
	ARG_UNUSED(payload);
	ARG_UNUSED(len);
	return 0;
}

int spotflow_mqtt_request_ota_subscription(spotflow_mqtt_message_cb callback)
{
	ARG_UNUSED(callback);
	return 0;
}

ZTEST(spotflow_ota_public_api, test_public_ota_functions_are_linkable)
{
	struct spotflow_ota_main_firmware_state state;

	zassert_false(spotflow_is_update_canceled());
	zassert_ok(spotflow_get_main_firmware_update_state(&state));
	zassert_equal(state.phase, SPOTFLOW_OTA_PHASE_NOT_RUNNING);
	zassert_equal(spotflow_get_main_firmware_update_info(NULL), -EINVAL);
	struct spotflow_firmware_info info;
	zassert_equal(spotflow_get_main_firmware_update_info(&info), -ENOENT);
	zassert_equal(spotflow_pause_main_firmware_update(&state), -ENOTSUP);
	zassert_equal(spotflow_resume_main_firmware_update(&state), -ENOTSUP);
	zassert_equal(spotflow_fail_main_firmware_update(&state), -ENOTSUP);
	zassert_equal(spotflow_confirm_main_firmware_image(&state), -ENOTSUP);
}

ZTEST_SUITE(spotflow_ota_public_api, NULL, NULL, NULL, NULL, NULL);
