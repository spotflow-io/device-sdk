#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include <spotflow/ota.h>

#include "ota/spotflow_ota_fw_main.h"
#include "ota/spotflow_ota_persistence.h"
#include "ota/spotflow_ota_state.h"
#include "spotflow_build_id.h"
#include "spotflow_ota_bindesc_test_util.h"
#include "spotflow_ota_downloader_transport_fake.h"
#include "spotflow_ota_platform_fake.h"
#include "spotflow_ota_test_settings.h"

LOG_MODULE_REGISTER(spotflow_ota, CONFIG_LOG_DEFAULT_LEVEL);

static struct spotflow_ota_platform_fake* platform_fake;
static struct spotflow_ota_downloader_transport_fake* transport_fake;
static enum spotflow_ota_phase progress_phases[8];
static size_t progress_phase_count;
static uint8_t download_payload[128];
static size_t download_payload_len;

static struct spotflow_ota_artifact main_artifact = {
	.is_main = true,
	.slug = "main",
	.url = "https://example.com/main.bin",
	.secret = "secret",
	.version = "1.0.0",
};

static struct spotflow_ota_artifact secondary_artifact = {
	.is_main = false,
	.slug = "radio",
	.url = "https://example.com/radio.bin",
	.secret = "secret",
	.version = "2.0.0",
};

static void build_download_payload(uint8_t* buffer, size_t buffer_size, size_t* payload_len)
{
	uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH];
	size_t offset = 32;
	size_t end;

	for (size_t i = 0; i < SPOTFLOW_BUILD_ID_LENGTH; i++) {
		build_id[i] = (uint8_t)(0x10 + i);
	}

	memset(buffer, 0xFF, buffer_size);
	end = spotflow_ota_test_bindesc_write_build_id(buffer, buffer_size, offset, build_id);
	zassert_true(end > 0);
	*payload_len = end;
}

static struct spotflow_ota_update_msg build_two_artifact_update(void)
{
	struct spotflow_ota_update_msg update = {
		.attempt_id = 42,
		.artifact_count = 2,
	};

	update.artifacts[0] = main_artifact;
	update.artifacts[1] = secondary_artifact;
	return update;
}

static void reset_test_state(void)
{
	spotflow_ota_test_settings_reset();
	spotflow_ota_state_reset();
	spotflow_ota_fw_main_reset();
	spotflow_ota_persistence_init();
	spotflow_ota_platform_fake_reset(spotflow_ota_platform_fake_get());
	spotflow_ota_downloader_transport_fake_reset(spotflow_ota_downloader_transport_fake_get());

	platform_fake = spotflow_ota_platform_fake_get();
	transport_fake = spotflow_ota_downloader_transport_fake_get();
	progress_phase_count = 0;

	build_download_payload(download_payload, sizeof(download_payload), &download_payload_len);
	transport_fake->payload = download_payload;
	transport_fake->payload_len = download_payload_len;
}

static void accept_two_artifact_update(void)
{
	struct spotflow_ota_state_action action;
	struct spotflow_ota_update_msg update = build_two_artifact_update();

	zassert_ok(spotflow_ota_state_accept_update(&update, &action));
}

static void apply_main_result(enum spotflow_ota_result result)
{
	struct spotflow_ota_state_action action;

	zassert_ok(spotflow_ota_state_apply_artifact_result(0, result, &action));
}

void spotflow_on_main_firmware_update_progressed(
    const struct spotflow_ota_main_firmware_state* state)
{
	zassert_not_null(state);
	zassert_true(progress_phase_count < ARRAY_SIZE(progress_phases));
	progress_phases[progress_phase_count++] = state->phase;
}

bool spotflow_is_update_canceled(void)
{
	return spotflow_ota_state_is_update_canceled();
}

static void before_each(void* fixture)
{
	ARG_UNUSED(fixture);

	reset_test_state();
}

ZTEST(spotflow_ota_fw_main, test_happy_path_requests_test_upgrade_and_reboot)
{
	struct spotflow_ota_probation probation;
	bool has_probation;
	enum spotflow_ota_result result;

	accept_two_artifact_update();

	result = spotflow_ota_fw_main_process_artifact(42, 0, &main_artifact);

	zassert_equal(result, SPOTFLOW_OTA_RESULT_PENDING);
	zassert_equal(platform_fake->upgrade_request_count, 1);
	zassert_equal(platform_fake->reboot_count, 1);
	zassert_ok(spotflow_ota_persistence_load_probation(&probation, &has_probation));
	zassert_true(has_probation);
	zassert_equal(probation.attempt_id, 42);
	zassert_equal(probation.artifact_index, 0);
	zassert_str_equal(probation.slug, "main");
	zassert_str_equal(probation.version, "1.0.0");
	zassert_equal(progress_phase_count, 4);
	zassert_equal(progress_phases[0], SPOTFLOW_OTA_PHASE_PENDING_DOWNLOAD);
	zassert_equal(progress_phases[1], SPOTFLOW_OTA_PHASE_DOWNLOADING);
	zassert_equal(progress_phases[2], SPOTFLOW_OTA_PHASE_PENDING_UPGRADE);
	zassert_equal(progress_phases[3], SPOTFLOW_OTA_PHASE_PENDING_REBOOT);
}

ZTEST(spotflow_ota_fw_main, test_download_failure_fails_main_and_cancels_remaining)
{
	struct spotflow_ota_state_snapshot snapshot;
	const int download_failure[] = { -EIO };

	spotflow_ota_downloader_transport_fake_set_results(transport_fake, download_failure,
							   ARRAY_SIZE(download_failure));
	accept_two_artifact_update();
	apply_main_result(spotflow_ota_fw_main_process_artifact(42, 0, &main_artifact));

	spotflow_ota_state_get_snapshot(&snapshot);
	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_FAILED);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_CANCELED);
	zassert_equal(snapshot.main_firmware_state.phase, SPOTFLOW_OTA_PHASE_NOT_RUNNING);
	zassert_equal(snapshot.main_firmware_state.result, SPOTFLOW_OTA_RESULT_FAILED);
}

ZTEST(spotflow_ota_fw_main, test_flash_write_failure_fails_safely)
{
	struct spotflow_ota_state_snapshot snapshot;

	platform_fake->write_result = -EIO;
	accept_two_artifact_update();
	apply_main_result(spotflow_ota_fw_main_process_artifact(42, 0, &main_artifact));

	spotflow_ota_state_get_snapshot(&snapshot);
	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_FAILED);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_CANCELED);
}

ZTEST(spotflow_ota_fw_main, test_upgrade_request_failure_fails_safely)
{
	struct spotflow_ota_state_snapshot snapshot;

	platform_fake->upgrade_request_result = -EIO;
	accept_two_artifact_update();
	apply_main_result(spotflow_ota_fw_main_process_artifact(42, 0, &main_artifact));

	spotflow_ota_state_get_snapshot(&snapshot);
	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_FAILED);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_CANCELED);
}

ZTEST(spotflow_ota_fw_main, test_probation_persistence_failure_fails_safely)
{
	struct spotflow_ota_state_snapshot snapshot;

	spotflow_ota_test_settings_exhaust_capacity();
	accept_two_artifact_update();
	apply_main_result(spotflow_ota_fw_main_process_artifact(42, 0, &main_artifact));

	spotflow_ota_state_get_snapshot(&snapshot);
	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_FAILED);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_CANCELED);
}

ZTEST(spotflow_ota_fw_main, test_downloaded_build_id_read_failure_fails_safely)
{
	struct spotflow_ota_state_snapshot snapshot;

	platform_fake->image_info_result = -EIO;
	accept_two_artifact_update();
	apply_main_result(spotflow_ota_fw_main_process_artifact(42, 0, &main_artifact));

	spotflow_ota_state_get_snapshot(&snapshot);
	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_FAILED);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_CANCELED);
}

ZTEST(spotflow_ota_fw_main, test_finish_prereboot_keeps_main_artifact_pending)
{
	struct spotflow_ota_state_action action;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_worker_job job;

	accept_two_artifact_update();
	zassert_true(spotflow_ota_state_get_worker_job(&job));
	zassert_ok(spotflow_ota_state_finish_main_firmware_prereboot(&action));

	spotflow_ota_state_get_snapshot(&snapshot);
	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_PENDING);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_PENDING);
	zassert_false(spotflow_ota_state_get_worker_job(&job));
}

ZTEST_SUITE(spotflow_ota_fw_main, NULL, NULL, before_each, NULL, NULL);
