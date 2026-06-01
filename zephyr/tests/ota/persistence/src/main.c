#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/ztest.h>

#include "spotflow_build_id.h"
#include "ota/spotflow_ota_persistence.h"
#include "spotflow_ota_test_settings.h"

LOG_MODULE_REGISTER(spotflow_ota);

static void before_each(void* fixture)
{
	ARG_UNUSED(fixture);
	spotflow_ota_test_settings_reset();
}

ZTEST(spotflow_ota_persistence, test_load_empty_settings)
{
	struct spotflow_ota_persisted_attempt attempt;
	struct spotflow_ota_probation probation;
	char version[SPOTFLOW_OTA_ARTIFACT_VERSION_MAX_LENGTH + 1];
	bool has_attempt;
	bool has_probation;
	bool has_version;

	zassert_ok(spotflow_ota_persistence_init());
	zassert_ok(spotflow_ota_persistence_load_attempt(&attempt, &has_attempt));
	zassert_false(has_attempt);
	zassert_ok(spotflow_ota_persistence_load_probation(&probation, &has_probation));
	zassert_false(has_probation);
	zassert_ok(spotflow_ota_persistence_load_installed_version("main", version, sizeof(version),
								   &has_version));
	zassert_false(has_version);
}

ZTEST(spotflow_ota_persistence, test_save_and_load_latest_attempt)
{
	struct spotflow_ota_persisted_attempt attempt = {
		.attempt_id = 201,
		.artifact_count = 2,
		.actionable_cancellation = true,
		.artifact_results = {
			SPOTFLOW_OTA_RESULT_SUCCEEDED,
			SPOTFLOW_OTA_RESULT_FAILED,
		},
	};
	struct spotflow_ota_persisted_attempt loaded;
	bool has_attempt;

	zassert_ok(spotflow_ota_persistence_save_attempt(&attempt));
	zassert_str_equal(spotflow_ota_test_settings_get_last_saved_name(), "spotflow/ota/attempt");
	zassert_ok(spotflow_ota_persistence_load_attempt(&loaded, &has_attempt));
	zassert_true(has_attempt);
	zassert_equal(loaded.attempt_id, attempt.attempt_id);
	zassert_equal(loaded.artifact_count, attempt.artifact_count);
	zassert_equal(loaded.actionable_cancellation, attempt.actionable_cancellation);
	zassert_equal(loaded.artifact_results[0], attempt.artifact_results[0]);
	zassert_equal(loaded.artifact_results[1], attempt.artifact_results[1]);
}

ZTEST(spotflow_ota_persistence, test_save_and_load_probation)
{
	struct spotflow_ota_probation probation = {
		.attempt_id = 202,
		.artifact_index = 1,
		.slug = "main",
		.version = "3.0.0",
	};
	for (size_t i = 0; i < SPOTFLOW_BUILD_ID_LENGTH; i++) {
		probation.expected_build_id[i] = (uint8_t)(0x55 + i);
	}

	struct spotflow_ota_probation loaded;
	bool has_probation;

	zassert_ok(spotflow_ota_persistence_save_probation(&probation));
	zassert_str_equal(spotflow_ota_test_settings_get_last_saved_name(),
			  "spotflow/ota/probation");
	zassert_ok(spotflow_ota_persistence_load_probation(&loaded, &has_probation));
	zassert_true(has_probation);
	zassert_equal(loaded.attempt_id, probation.attempt_id);
	zassert_equal(loaded.artifact_index, probation.artifact_index);
	zassert_str_equal(loaded.slug, probation.slug);
	zassert_str_equal(loaded.version, probation.version);
	zassert_mem_equal(loaded.expected_build_id, probation.expected_build_id,
			  SPOTFLOW_BUILD_ID_LENGTH);
	zassert_ok(spotflow_ota_persistence_clear_probation());
	zassert_str_equal(spotflow_ota_test_settings_get_last_deleted_name(),
			  "spotflow/ota/probation");
}

ZTEST(spotflow_ota_persistence, test_save_and_load_installed_version)
{
	char version[SPOTFLOW_OTA_ARTIFACT_VERSION_MAX_LENGTH + 1];
	bool has_version;

	zassert_ok(spotflow_ota_persistence_save_installed_version("main", "4.0.1"));
	zassert_str_equal(spotflow_ota_test_settings_get_last_saved_name(),
			  "spotflow/ota/version/main");
	zassert_ok(spotflow_ota_persistence_load_installed_version("main", version, sizeof(version),
								   &has_version));
	zassert_true(has_version);
	zassert_str_equal(version, "4.0.1");
}

ZTEST_SUITE(spotflow_ota_persistence, NULL, NULL, before_each, NULL, NULL);
