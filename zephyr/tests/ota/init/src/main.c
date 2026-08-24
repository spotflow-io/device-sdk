#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

#include <spotflow/ota.h>

#include "ota/spotflow_ota.h"
#include "ota/firmware/spotflow_ota_fw_main.h"
#include "ota/persistence/spotflow_ota_persistence.h"
#include "spotflow_build_id.h"
#include "spotflow_ota_build_id_fake.h"
#include "spotflow_ota_platform_fake.h"
#include "spotflow_ota_test_fakes.h"
#include "spotflow_ota_test_settings.h"

static size_t progress_call_count;
static int callback_get_state_result;
static int callback_get_info_result;
static uint64_t callback_last_attempt_id;
static struct spotflow_ota_main_firmware_state callback_state;

static void fill_state_sentinel(struct spotflow_ota_main_firmware_state* state)
{
	memset(state, 0xa5, sizeof(*state));
}

static void assert_state_unchanged(const struct spotflow_ota_main_firmware_state* state,
				   const struct spotflow_ota_main_firmware_state* expected)
{
	zassert_mem_equal(state, expected, sizeof(*state));
}

void spotflow_on_main_firmware_update_progressed(
	const struct spotflow_ota_main_firmware_state* state)
{
	struct spotflow_download_request request;
	struct spotflow_firmware_info info;

	progress_call_count++;
	callback_get_state_result = spotflow_get_main_firmware_update_state(&callback_state);
	callback_get_info_result = spotflow_get_main_firmware_update_info(&info, &request);
	callback_last_attempt_id = spotflow_ota_get_last_received_attempt_id();

	zassert_not_null(state);
	zassert_equal(state->phase, callback_state.phase);
	zassert_equal(state->result, callback_state.result);
}

static void fill_build_id(uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH])
{
	for (size_t i = 0; i < SPOTFLOW_BUILD_ID_LENGTH; i++) {
		build_id[i] = (uint8_t)(0x20 + i);
	}
}

static void persist_unconfirmed_update(const uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH])
{
	struct spotflow_ota_persisted_attempt attempt = {
		.attempt_id = 42,
		.artifact_count = 1,
		.artifact_results = { SPOTFLOW_OTA_RESULT_PENDING },
	};
	struct spotflow_ota_probation probation = {
		.attempt_id = 42,
		.artifact_index = 0,
	};

	strncpy(probation.slug, "main", sizeof(probation.slug) - 1);
	strncpy(probation.version, "1.0.0", sizeof(probation.version) - 1);
	memcpy(probation.expected_build_id, build_id, SPOTFLOW_BUILD_ID_LENGTH);

	zassert_ok(spotflow_ota_persistence_save_attempt(&attempt));
	zassert_ok(spotflow_ota_persistence_save_probation(&probation));
}

static void before_each(void* fixture)
{
	uint8_t build_id[SPOTFLOW_BUILD_ID_LENGTH];

	ARG_UNUSED(fixture);
	spotflow_ota_test_settings_reset();
	spotflow_ota_test_fakes_reset();
	spotflow_ota_reset();
	spotflow_ota_build_id_fake_reset(spotflow_ota_build_id_fake_get());
	spotflow_ota_platform_fake_reset(spotflow_ota_platform_fake_get());
	zassert_ok(spotflow_ota_persistence_init());

	fill_build_id(build_id);
	spotflow_ota_build_id_fake_set_running_build_id(build_id);
	spotflow_ota_platform_fake_get()->image_confirmed = false;
	persist_unconfirmed_update(build_id);

	progress_call_count = 0;
	callback_get_state_result = -1;
	callback_get_info_result = 0;
	callback_last_attempt_id = 0;
	memset(&callback_state, 0, sizeof(callback_state));
}

ZTEST(spotflow_ota_init, test_startup_callback_can_reenter_public_apis)
{
	zassert_ok(spotflow_ota_init());

	zassert_equal(progress_call_count, 1);
	zassert_ok(callback_get_state_result);
	zassert_ok(callback_get_info_result);
	zassert_equal(callback_last_attempt_id, 42);
	zassert_equal(callback_state.phase, SPOTFLOW_OTA_PHASE_UNCONFIRMED);
	zassert_equal(callback_state.result, SPOTFLOW_OTA_RESULT_PENDING);
}

ZTEST(spotflow_ota_init, test_control_apis_leave_state_unchanged_on_initialization_error)
{
	struct spotflow_ota_main_firmware_state state;
	struct spotflow_ota_main_firmware_state sentinel;

	fill_state_sentinel(&sentinel);
	spotflow_ota_test_settings_set_init_result(-EIO);

	state = sentinel;
	zassert_equal(spotflow_pause_main_firmware_update(&state), -EIO);
	assert_state_unchanged(&state, &sentinel);

	state = sentinel;
	zassert_equal(spotflow_resume_main_firmware_update(&state), -EIO);
	assert_state_unchanged(&state, &sentinel);

	state = sentinel;
	zassert_equal(spotflow_abort_main_firmware_update(&state), -EIO);
	assert_state_unchanged(&state, &sentinel);

	state = sentinel;
	zassert_equal(spotflow_confirm_main_firmware_image(&state), -EIO);
	assert_state_unchanged(&state, &sentinel);
}

ZTEST(spotflow_ota_init, test_control_apis_leave_state_unchanged_on_operation_error)
{
	struct spotflow_ota_main_firmware_state state;
	struct spotflow_ota_main_firmware_state sentinel;

	zassert_ok(spotflow_ota_init());
	fill_state_sentinel(&sentinel);

	state = sentinel;
	zassert_equal(spotflow_pause_main_firmware_update(&state), -EINVAL);
	assert_state_unchanged(&state, &sentinel);

	state = sentinel;
	zassert_equal(spotflow_resume_main_firmware_update(&state), -EINVAL);
	assert_state_unchanged(&state, &sentinel);

	state = sentinel;
	zassert_equal(spotflow_abort_main_firmware_update(&state), -EINVAL);
	assert_state_unchanged(&state, &sentinel);

	state = sentinel;
	spotflow_ota_platform_fake_get()->confirm_result = -EIO;
	zassert_equal(spotflow_confirm_main_firmware_image(&state), -EIO);
	assert_state_unchanged(&state, &sentinel);
}

ZTEST_SUITE(spotflow_ota_init, NULL, NULL, before_each, NULL, NULL);
