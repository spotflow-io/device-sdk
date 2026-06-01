#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/ztest.h>

#include "spotflow_build_id.h"
#include "ota/spotflow_ota_persistence.h"

LOG_MODULE_REGISTER(spotflow_ota);

struct fake_setting_entry {
	bool in_use;
	char name[SETTINGS_FULL_NAME_LEN];
	uint8_t value[192];
	size_t value_len;
};

static struct fake_setting_entry fake_entries[8];
static char last_saved_name[SETTINGS_FULL_NAME_LEN];
static char last_deleted_name[SETTINGS_FULL_NAME_LEN];

static ssize_t fake_settings_read(void* cb_arg, void* data, size_t len)
{
	struct fake_setting_entry* entry = cb_arg;

	if (len > entry->value_len) {
		len = entry->value_len;
	}

	memcpy(data, entry->value, len);
	return len;
}

int settings_subsys_init(void)
{
	return 0;
}

int settings_save_one(const char* name, const void* value, size_t val_len)
{
	for (size_t i = 0; i < ARRAY_SIZE(fake_entries); i++) {
		if (!fake_entries[i].in_use || strcmp(fake_entries[i].name, name) == 0) {
			fake_entries[i].in_use = true;
			strncpy(fake_entries[i].name, name, sizeof(fake_entries[i].name) - 1);
			fake_entries[i].name[sizeof(fake_entries[i].name) - 1] = '\0';
			zassert_true(val_len <= sizeof(fake_entries[i].value));
			memcpy(fake_entries[i].value, value, val_len);
			fake_entries[i].value_len = val_len;
			strncpy(last_saved_name, name, sizeof(last_saved_name) - 1);
			last_saved_name[sizeof(last_saved_name) - 1] = '\0';
			return 0;
		}
	}

	return -ENOMEM;
}

int settings_delete(const char* name)
{
	for (size_t i = 0; i < ARRAY_SIZE(fake_entries); i++) {
		if (fake_entries[i].in_use && strcmp(fake_entries[i].name, name) == 0) {
			fake_entries[i].in_use = false;
		}
	}

	strncpy(last_deleted_name, name, sizeof(last_deleted_name) - 1);
	last_deleted_name[sizeof(last_deleted_name) - 1] = '\0';
	return 0;
}

int settings_load_subtree_direct(const char* subtree, settings_load_direct_cb cb, void* param)
{
	size_t subtree_len = strlen(subtree);

	for (size_t i = 0; i < ARRAY_SIZE(fake_entries); i++) {
		if (!fake_entries[i].in_use ||
		    strncmp(fake_entries[i].name, subtree, subtree_len) != 0) {
			continue;
		}

		const char* key = fake_entries[i].name + subtree_len;
		if (*key == '/') {
			key++;
		} else if (*key != '\0') {
			continue;
		}

		int rc =
		    cb(key, fake_entries[i].value_len, fake_settings_read, &fake_entries[i], param);
		if (rc != 0) {
			return rc;
		}
	}

	return 0;
}

static void before_each(void* fixture)
{
	ARG_UNUSED(fixture);
	memset(fake_entries, 0, sizeof(fake_entries));
	memset(last_saved_name, 0, sizeof(last_saved_name));
	memset(last_deleted_name, 0, sizeof(last_deleted_name));
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
	zassert_str_equal(last_saved_name, "spotflow/ota/attempt");
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
	zassert_str_equal(last_saved_name, "spotflow/ota/probation");
	zassert_ok(spotflow_ota_persistence_load_probation(&loaded, &has_probation));
	zassert_true(has_probation);
	zassert_equal(loaded.attempt_id, probation.attempt_id);
	zassert_equal(loaded.artifact_index, probation.artifact_index);
	zassert_str_equal(loaded.slug, probation.slug);
	zassert_str_equal(loaded.version, probation.version);
	zassert_mem_equal(loaded.expected_build_id, probation.expected_build_id,
			  SPOTFLOW_BUILD_ID_LENGTH);
	zassert_ok(spotflow_ota_persistence_clear_probation());
	zassert_str_equal(last_deleted_name, "spotflow/ota/probation");
}

ZTEST(spotflow_ota_persistence, test_save_and_load_installed_version)
{
	char version[SPOTFLOW_OTA_ARTIFACT_VERSION_MAX_LENGTH + 1];
	bool has_version;

	zassert_ok(spotflow_ota_persistence_save_installed_version("main", "4.0.1"));
	zassert_str_equal(last_saved_name, "spotflow/ota/version/main");
	zassert_ok(spotflow_ota_persistence_load_installed_version("main", version, sizeof(version),
								   &has_version));
	zassert_true(has_version);
	zassert_str_equal(version, "4.0.1");
}

ZTEST_SUITE(spotflow_ota_persistence, NULL, NULL, before_each, NULL, NULL);
