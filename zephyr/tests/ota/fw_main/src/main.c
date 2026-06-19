#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include <spotflow/ota.h>

#include "ota/firmware/spotflow_ota_fw_main.h"
#include "ota/protocol/spotflow_ota_net.h"
#include "ota/persistence/spotflow_ota_persistence.h"
#include "ota/core/spotflow_ota_state.h"
#include "spotflow_build_id.h"
#include "spotflow_ota_bindesc_test_util.h"
#include "spotflow_ota_build_id_fake.h"
#include "spotflow_ota_downloader_transport_fake.h"
#include "spotflow_ota_platform_fake.h"
#include "spotflow_ota_test_fakes.h"
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
	spotflow_ota_test_fakes_reset();
	spotflow_ota_build_id_fake_reset(spotflow_ota_build_id_fake_get());
	spotflow_ota_net_reset();
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
	if (state != NULL && progress_phase_count < ARRAY_SIZE(progress_phases)) {
		progress_phases[progress_phase_count++] = state->phase;
	}
}

bool spotflow_is_update_canceled(void)
{
	return spotflow_ota_state_is_update_canceled();
}

int spotflow_mqtt_publish_ota_cbor_msg(uint8_t* payload, size_t len)
{
	struct spotflow_ota_test_fake_mqtt* fake_mqtt = spotflow_ota_test_fake_mqtt_get();

	fake_mqtt->publish_count++;
	fake_mqtt->last_payload = payload;
	fake_mqtt->last_payload_len = len;

	return fake_mqtt->publish_result;
}

static void fill_build_id(uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH], uint8_t seed)
{
	for (size_t i = 0; i < SPOTFLOW_BUILD_ID_LENGTH; i++) {
		build_id[i] = (uint8_t)(seed + i);
	}
}

static struct spotflow_ota_probation
build_probation_record(const uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH])
{
	struct spotflow_ota_probation probation = {
		.attempt_id = 42,
		.artifact_index = 0,
	};

	strncpy(probation.slug, "main", sizeof(probation.slug) - 1);
	strncpy(probation.version, "1.0.0", sizeof(probation.version) - 1);
	memcpy(probation.expected_build_id, build_id, SPOTFLOW_BUILD_ID_LENGTH);
	return probation;
}

static void persist_pending_post_reboot_attempt(void)
{
	struct spotflow_ota_persisted_attempt attempt = {
		.attempt_id = 42,
		.artifact_count = 2,
		.artifact_results = {
			SPOTFLOW_OTA_RESULT_PENDING,
			SPOTFLOW_OTA_RESULT_PENDING,
		},
	};

	zassert_ok(spotflow_ota_persistence_save_attempt(&attempt));
}

static void restore_post_reboot_state(const struct spotflow_ota_probation* probation)
{
	struct spotflow_ota_persisted_attempt attempt;
	bool has_attempt;

	zassert_ok(spotflow_ota_persistence_load_attempt(&attempt, &has_attempt));
	zassert_true(has_attempt);
	zassert_ok(spotflow_ota_state_init_from_persistence(&attempt, true, probation, true));
}

static void setup_post_reboot_context(const uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH],
				      struct spotflow_ota_probation* probation_out)
{
	struct spotflow_ota_probation probation = build_probation_record(build_id);

	accept_two_artifact_update();
	zassert_ok(spotflow_ota_persistence_save_probation(&probation));
	persist_pending_post_reboot_attempt();
	spotflow_ota_build_id_fake_set_running_build_id(build_id);
	restore_post_reboot_state(&probation);

	if (probation_out != NULL) {
		*probation_out = probation;
	}
}

static void before_each(void* fixture)
{
	ARG_UNUSED(fixture);

	reset_test_state();
}

ZTEST(spotflow_ota_fw_main, test_happy_path_requests_test_upgrade_and_reboot)
{
	struct spotflow_ota_probation probation;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_worker_job job;
	bool has_probation;

	accept_two_artifact_update();
	zassert_true(spotflow_ota_state_get_worker_job(&job));

	(void)spotflow_ota_fw_main_process_artifact(42, 0, &main_artifact);

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

	spotflow_ota_state_get_snapshot(&snapshot);
	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_PENDING);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_PENDING);
	zassert_false(spotflow_ota_state_get_worker_job(&job));
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

ZTEST(spotflow_ota_fw_main, test_startup_reconciliation_unconfirmed_match)
{
	struct spotflow_ota_state_action action;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_probation probation;
	uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH];
	bool has_probation;

	fill_build_id(build_id, 0x10);
	setup_post_reboot_context(build_id, &probation);
	platform_fake->image_confirmed = false;

	zassert_ok(spotflow_ota_fw_main_reconcile_startup(&probation, true, &action));
	zassert_false(action.wake_worker);

	spotflow_ota_state_get_snapshot(&snapshot);
	zassert_equal(snapshot.main_firmware_state.phase, SPOTFLOW_OTA_PHASE_UNCONFIRMED);
	zassert_equal(snapshot.main_firmware_state.result, SPOTFLOW_OTA_RESULT_PENDING);
	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_PENDING);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_PENDING);
	zassert_ok(spotflow_ota_persistence_load_probation(&probation, &has_probation));
	zassert_true(has_probation);
}

ZTEST(spotflow_ota_fw_main, test_startup_reconciliation_already_confirmed_match)
{
	struct spotflow_ota_state_action action;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_probation probation;
	char installed_version[SPOTFLOW_OTA_ARTIFACT_VERSION_MAX_LENGTH + 1];
	bool has_probation;
	bool has_version;
	uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH];

	fill_build_id(build_id, 0x20);
	setup_post_reboot_context(build_id, &probation);
	platform_fake->image_confirmed = true;

	zassert_ok(spotflow_ota_fw_main_reconcile_startup(&probation, true, &action));
	zassert_true(action.wake_worker);

	spotflow_ota_state_get_snapshot(&snapshot);
	zassert_equal(snapshot.main_firmware_state.phase, SPOTFLOW_OTA_PHASE_NOT_RUNNING);
	zassert_equal(snapshot.main_firmware_state.result, SPOTFLOW_OTA_RESULT_SUCCEEDED);
	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_SUCCEEDED);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_PENDING);
	zassert_ok(spotflow_ota_persistence_load_probation(&probation, &has_probation));
	zassert_false(has_probation);
	zassert_ok(spotflow_ota_persistence_load_installed_version(
	    "main", installed_version, sizeof(installed_version), &has_version));
	zassert_true(has_version);
	zassert_str_equal(installed_version, "1.0.0");
}

ZTEST(spotflow_ota_fw_main, test_startup_reconciliation_identity_unavailable_reports_failure)
{
	struct spotflow_ota_state_action action;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_probation probation;
	uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH];
	bool has_probation;

	fill_build_id(build_id, 0x35);
	setup_post_reboot_context(build_id, &probation);
	spotflow_ota_build_id_fake_reset(spotflow_ota_build_id_fake_get());

	zassert_ok(spotflow_ota_fw_main_reconcile_startup(&probation, true, &action));
	zassert_false(action.wake_worker);

	spotflow_ota_state_get_snapshot(&snapshot);
	zassert_equal(snapshot.main_firmware_state.phase, SPOTFLOW_OTA_PHASE_NOT_RUNNING);
	zassert_equal(snapshot.main_firmware_state.result, SPOTFLOW_OTA_RESULT_FAILED);
	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_FAILED);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_CANCELED);
	zassert_ok(spotflow_ota_persistence_load_probation(&probation, &has_probation));
	zassert_false(has_probation);
}

ZTEST(spotflow_ota_fw_main, test_startup_reconciliation_mismatch_reports_rollback)
{
	struct spotflow_ota_state_action action;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_probation probation;
	uint8_t expected_build_id[SPOTFLOW_BUILD_ID_LENGTH];
	uint8_t running_build_id[SPOTFLOW_BUILD_ID_LENGTH];
	bool has_probation;

	fill_build_id(expected_build_id, 0x30);
	fill_build_id(running_build_id, 0x40);
	setup_post_reboot_context(expected_build_id, &probation);
	spotflow_ota_build_id_fake_set_running_build_id(running_build_id);

	zassert_ok(spotflow_ota_fw_main_reconcile_startup(&probation, true, &action));
	zassert_false(action.wake_worker);

	spotflow_ota_state_get_snapshot(&snapshot);
	zassert_equal(snapshot.main_firmware_state.phase, SPOTFLOW_OTA_PHASE_NOT_RUNNING);
	zassert_equal(snapshot.main_firmware_state.result, SPOTFLOW_OTA_RESULT_FAILED);
	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_FAILED);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_CANCELED);
	zassert_ok(spotflow_ota_persistence_load_probation(&probation, &has_probation));
	zassert_false(has_probation);
}

ZTEST(spotflow_ota_fw_main, test_confirm_api_persists_success_and_wakes_worker)
{
	struct spotflow_ota_state_action action;
	struct spotflow_ota_main_firmware_state state;
	struct spotflow_ota_state_snapshot snapshot;
	struct spotflow_ota_probation probation;
	uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH];

	fill_build_id(build_id, 0x50);
	setup_post_reboot_context(build_id, &probation);
	platform_fake->image_confirmed = false;
	zassert_ok(spotflow_ota_fw_main_reconcile_startup(&probation, true, &action));

	zassert_ok(spotflow_ota_fw_main_confirm_image(&state, &action));
	zassert_true(action.wake_worker);
	zassert_equal(platform_fake->confirm_count, 1);
	zassert_equal(state.phase, SPOTFLOW_OTA_PHASE_NOT_RUNNING);
	zassert_equal(state.result, SPOTFLOW_OTA_RESULT_SUCCEEDED);

	spotflow_ota_state_get_snapshot(&snapshot);
	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_SUCCEEDED);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_PENDING);
}

ZTEST(spotflow_ota_fw_main, test_confirm_idempotent_when_already_succeeded)
{
	struct spotflow_ota_state_action action;
	struct spotflow_ota_main_firmware_state state;
	struct spotflow_ota_probation probation;
	uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH];

	fill_build_id(build_id, 0x55);
	setup_post_reboot_context(build_id, &probation);
	platform_fake->image_confirmed = true;
	zassert_ok(spotflow_ota_fw_main_reconcile_startup(&probation, true, &action));

	zassert_ok(spotflow_ota_fw_main_confirm_image(&state, &action));
	zassert_equal(state.phase, SPOTFLOW_OTA_PHASE_NOT_RUNNING);
	zassert_equal(state.result, SPOTFLOW_OTA_RESULT_SUCCEEDED);
	zassert_equal(platform_fake->confirm_count, 0);
}

ZTEST(spotflow_ota_fw_main, test_confirm_invalid_phase_returns_current_state)
{
	struct spotflow_ota_main_firmware_state state;
	struct spotflow_ota_state_action action;

	zassert_equal(spotflow_ota_fw_main_confirm_image(&state, &action), -EINVAL);
	zassert_equal(state.phase, SPOTFLOW_OTA_PHASE_NOT_RUNNING);
}

static void setup_pre_reboot_main_update(enum spotflow_ota_phase phase)
{
	accept_two_artifact_update();
	zassert_ok(spotflow_ota_state_store_main_firmware_artifact(42, 0, &main_artifact));
	if (phase != SPOTFLOW_OTA_PHASE_NOT_RUNNING) {
		zassert_ok(spotflow_ota_state_set_main_firmware_phase(phase, NULL));
	}
}

static void pause_after_delay(void* arg1, void* arg2, void* arg3)
{
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	k_sleep(K_MSEC(20));
	zassert_ok(spotflow_ota_fw_main_pause_update(NULL));
	k_sleep(K_MSEC(20));
	zassert_ok(spotflow_ota_fw_main_resume_update(NULL));
}

static struct spotflow_ota_state_action async_fail_action;

static void fail_after_delay(void* arg1, void* arg2, void* arg3)
{
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	k_sleep(K_MSEC(20));
	zassert_ok(spotflow_ota_fw_main_fail_update(NULL, &async_fail_action));
}

static void cloud_cancel_after_delay(void* arg1, void* arg2, void* arg3)
{
	struct spotflow_ota_state_action action;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	k_sleep(K_MSEC(20));
	zassert_ok(spotflow_ota_state_accept_cancel(42, &action));
	zassert_true(action.accepted_cancel);
	spotflow_ota_fw_main_cancel_active_download();
}

ZTEST(spotflow_ota_fw_main, test_pause_valid_phase_sets_paused)
{
	struct spotflow_ota_main_firmware_state state;

	setup_pre_reboot_main_update(SPOTFLOW_OTA_PHASE_DOWNLOADING);
	zassert_ok(spotflow_ota_fw_main_pause_update(&state));
	zassert_true(state.is_paused);
	zassert_equal(state.phase, SPOTFLOW_OTA_PHASE_DOWNLOADING);
}

ZTEST(spotflow_ota_fw_main, test_pause_invalid_not_running_returns_current_state)
{
	struct spotflow_ota_main_firmware_state state;

	accept_two_artifact_update();
	zassert_equal(spotflow_ota_fw_main_pause_update(&state), -EINVAL);
	zassert_equal(state.phase, SPOTFLOW_OTA_PHASE_NOT_RUNNING);
}

ZTEST(spotflow_ota_fw_main, test_pause_invalid_unconfirmed_returns_current_state)
{
	struct spotflow_ota_state_action action;
	struct spotflow_ota_main_firmware_state state;
	struct spotflow_ota_probation probation;
	uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH];

	fill_build_id(build_id, 0x60);
	setup_post_reboot_context(build_id, &probation);
	platform_fake->image_confirmed = false;
	zassert_ok(spotflow_ota_fw_main_reconcile_startup(&probation, true, &action));

	zassert_equal(spotflow_ota_fw_main_pause_update(&state), -EINVAL);
	zassert_equal(state.phase, SPOTFLOW_OTA_PHASE_UNCONFIRMED);
}

ZTEST(spotflow_ota_fw_main, test_resume_when_paused)
{
	struct spotflow_ota_main_firmware_state state;

	setup_pre_reboot_main_update(SPOTFLOW_OTA_PHASE_DOWNLOADING);
	zassert_ok(spotflow_ota_fw_main_pause_update(&state));
	zassert_true(state.is_paused);

	zassert_ok(spotflow_ota_fw_main_resume_update(&state));
	zassert_false(state.is_paused);
}

ZTEST(spotflow_ota_fw_main, test_resume_invalid_when_not_paused)
{
	struct spotflow_ota_main_firmware_state state;

	setup_pre_reboot_main_update(SPOTFLOW_OTA_PHASE_DOWNLOADING);
	zassert_equal(spotflow_ota_fw_main_resume_update(&state), -EINVAL);
	zassert_false(state.is_paused);
}

ZTEST(spotflow_ota_fw_main, test_pause_valid_pending_reboot_sets_paused)
{
	struct spotflow_ota_main_firmware_state state;

	setup_pre_reboot_main_update(SPOTFLOW_OTA_PHASE_PENDING_REBOOT);
	zassert_ok(spotflow_ota_fw_main_pause_update(&state));
	zassert_true(state.is_paused);
	zassert_equal(state.phase, SPOTFLOW_OTA_PHASE_PENDING_REBOOT);
}

ZTEST(spotflow_ota_fw_main, test_fail_invalid_pending_reboot_returns_current_state)
{
	struct spotflow_ota_state_action action;
	struct spotflow_ota_main_firmware_state state;

	setup_pre_reboot_main_update(SPOTFLOW_OTA_PHASE_PENDING_REBOOT);
	zassert_equal(spotflow_ota_fw_main_fail_update(&state, &action), -EINVAL);
	zassert_equal(state.phase, SPOTFLOW_OTA_PHASE_PENDING_REBOOT);
	zassert_equal(state.result, SPOTFLOW_OTA_RESULT_PENDING);
}

ZTEST(spotflow_ota_fw_main, test_fail_valid_marks_failed_and_cancels_remaining)
{
	struct spotflow_ota_state_action action;
	struct spotflow_ota_main_firmware_state state;
	struct spotflow_ota_state_snapshot snapshot;

	setup_pre_reboot_main_update(SPOTFLOW_OTA_PHASE_DOWNLOADING);
	zassert_ok(spotflow_ota_fw_main_fail_update(&state, &action));
	zassert_equal(state.phase, SPOTFLOW_OTA_PHASE_NOT_RUNNING);
	zassert_equal(state.result, SPOTFLOW_OTA_RESULT_FAILED);

	spotflow_ota_state_get_snapshot(&snapshot);
	zassert_equal(snapshot.artifact_results[0], SPOTFLOW_OTA_RESULT_FAILED);
	zassert_equal(snapshot.artifact_results[1], SPOTFLOW_OTA_RESULT_CANCELED);
}

ZTEST(spotflow_ota_fw_main, test_fail_invalid_not_running_returns_current_state)
{
	struct spotflow_ota_state_action action;
	struct spotflow_ota_main_firmware_state state;

	accept_two_artifact_update();
	zassert_equal(spotflow_ota_fw_main_fail_update(&state, &action), -EINVAL);
	zassert_equal(state.phase, SPOTFLOW_OTA_PHASE_NOT_RUNNING);
}

ZTEST(spotflow_ota_fw_main, test_fail_invalid_unconfirmed_returns_current_state)
{
	struct spotflow_ota_state_action action;
	struct spotflow_ota_main_firmware_state state;
	struct spotflow_ota_probation probation;
	uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH];

	fill_build_id(build_id, 0x70);
	setup_post_reboot_context(build_id, &probation);
	platform_fake->image_confirmed = false;
	zassert_ok(spotflow_ota_fw_main_reconcile_startup(&probation, true, &action));

	zassert_equal(spotflow_ota_fw_main_fail_update(&state, &action), -EINVAL);
	zassert_equal(state.phase, SPOTFLOW_OTA_PHASE_UNCONFIRMED);
}

ZTEST(spotflow_ota_fw_main, test_paused_state_visible_through_state_query)
{
	struct spotflow_ota_state_snapshot snapshot;

	setup_pre_reboot_main_update(SPOTFLOW_OTA_PHASE_DOWNLOADING);
	zassert_ok(spotflow_ota_fw_main_pause_update(NULL));

	spotflow_ota_state_get_snapshot(&snapshot);
	zassert_true(snapshot.main_firmware_state.is_paused);
	zassert_equal(snapshot.main_firmware_state.phase, SPOTFLOW_OTA_PHASE_DOWNLOADING);
}

ZTEST(spotflow_ota_fw_main, test_pause_and_resume_during_download)
{
	struct spotflow_ota_state_snapshot snapshot;
	struct k_thread pause_thread;
	k_thread_stack_t pause_stack[1024];
	enum spotflow_ota_result result;

	transport_fake->block_until_pause = true;
	accept_two_artifact_update();

	k_thread_create(&pause_thread, pause_stack, K_THREAD_STACK_SIZEOF(pause_stack),
			pause_after_delay, NULL, NULL, NULL, K_PRIO_PREEMPT(0), 0, K_NO_WAIT);

	result = spotflow_ota_fw_main_process_artifact(42, 0, &main_artifact);
	k_thread_join(&pause_thread, K_FOREVER);

	zassert_equal(result, SPOTFLOW_OTA_RESULT_PENDING);
	zassert_true(transport_fake->pause_observed);
	zassert_equal(platform_fake->upgrade_request_count, 1);
	zassert_equal(platform_fake->reboot_count, 1);

	spotflow_ota_state_get_snapshot(&snapshot);
	zassert_equal(snapshot.main_firmware_state.phase, SPOTFLOW_OTA_PHASE_PENDING_REBOOT);
}

ZTEST(spotflow_ota_fw_main, test_cloud_cancel_during_download_cancels_immediately)
{
	struct k_thread cancel_thread;
	k_thread_stack_t cancel_stack[1024];
	enum spotflow_ota_result result;

	transport_fake->block_until_cancel = true;
	accept_two_artifact_update();

	k_thread_create(&cancel_thread, cancel_stack, K_THREAD_STACK_SIZEOF(cancel_stack),
			cloud_cancel_after_delay, NULL, NULL, NULL, K_PRIO_PREEMPT(0), 0,
			K_NO_WAIT);

	result = spotflow_ota_fw_main_process_artifact(42, 0, &main_artifact);
	k_thread_join(&cancel_thread, K_FOREVER);

	zassert_equal(result, SPOTFLOW_OTA_RESULT_CANCELED);
	zassert_true(transport_fake->cancel_observed);
	zassert_true(spotflow_ota_state_is_update_canceled());
}

ZTEST(spotflow_ota_fw_main, test_user_fail_during_download_reports_failed)
{
	struct spotflow_ota_state_snapshot snapshot;
	struct k_thread fail_thread;
	k_thread_stack_t fail_stack[1024];
	enum spotflow_ota_result result;

	transport_fake->block_until_cancel = true;
	accept_two_artifact_update();

	k_thread_create(&fail_thread, fail_stack, K_THREAD_STACK_SIZEOF(fail_stack),
			fail_after_delay, NULL, NULL, NULL, K_PRIO_PREEMPT(0), 0, K_NO_WAIT);

	result = spotflow_ota_fw_main_process_artifact(42, 0, &main_artifact);
	k_thread_join(&fail_thread, K_FOREVER);

	zassert_equal(result, SPOTFLOW_OTA_RESULT_FAILED);
	zassert_true(transport_fake->cancel_observed);

	spotflow_ota_state_get_snapshot(&snapshot);
	zassert_equal(snapshot.main_firmware_state.result, SPOTFLOW_OTA_RESULT_FAILED);
}

ZTEST(spotflow_ota_fw_main, test_confirm_rejects_non_unconfirmed_phase)
{
	struct spotflow_ota_main_firmware_state state;
	struct spotflow_ota_state_action action;

	accept_two_artifact_update();
	zassert_ok(
	    spotflow_ota_state_set_main_firmware_phase(SPOTFLOW_OTA_PHASE_DOWNLOADING, &state));

	zassert_equal(spotflow_ota_fw_main_confirm_image(&state, &action), -EINVAL);
	zassert_equal(state.phase, SPOTFLOW_OTA_PHASE_DOWNLOADING);
}

ZTEST_SUITE(spotflow_ota_fw_main, NULL, NULL, before_each, NULL, NULL);
