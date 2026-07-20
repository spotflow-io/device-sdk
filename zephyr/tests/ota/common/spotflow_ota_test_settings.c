#include <errno.h>
#include <string.h>

#include <zephyr/ztest.h>

#include "ota/persistence/spotflow_ota_records_cbor.h"
#include "spotflow_ota_test_settings.h"

#define SPOTFLOW_OTA_SETTINGS_PATH_ATTEMPT "spotflow/ota/attempt"

struct fake_setting_entry {
	bool in_use;
	char name[SETTINGS_FULL_NAME_LEN];
	uint8_t value[192];
	size_t value_len;
};

static struct fake_setting_entry fake_entries[8];
static char last_saved_name[SETTINGS_FULL_NAME_LEN];
static char last_deleted_name[SETTINGS_FULL_NAME_LEN];
static const char* save_fail_name;
static bool save_fail_once;
static int load_fail_once;
static struct spotflow_ota_test_settings_attempt_save
	attempt_save_history[SPOTFLOW_OTA_TEST_SETTINGS_ATTEMPT_HISTORY_MAX];
static size_t attempt_save_history_count;

static void record_attempt_save(const void* value, size_t val_len)
{
	struct spotflow_ota_persisted_attempt attempt;
	size_t index;

	if (attempt_save_history_count >= ARRAY_SIZE(attempt_save_history)) {
		return;
	}

	if (spotflow_ota_records_cbor_decode_attempt(value, val_len, &attempt) < 0) {
		return;
	}

	index = attempt_save_history_count++;
	attempt_save_history[index].attempt_id = attempt.attempt_id;
	attempt_save_history[index].artifact_count = attempt.artifact_count;
	memcpy(attempt_save_history[index].artifact_results, attempt.artifact_results,
	       sizeof(attempt.artifact_results));
}

static ssize_t fake_settings_read(void* cb_arg, void* data, size_t len)
{
	struct fake_setting_entry* entry = cb_arg;

	if (len > entry->value_len) {
		len = entry->value_len;
	}

	memcpy(data, entry->value, len);
	return len;
}

void spotflow_ota_test_settings_reset(void)
{
	memset(fake_entries, 0, sizeof(fake_entries));
	memset(last_saved_name, 0, sizeof(last_saved_name));
	memset(last_deleted_name, 0, sizeof(last_deleted_name));
	save_fail_name = NULL;
	save_fail_once = false;
	load_fail_once = 0;
	attempt_save_history_count = 0;
}

void spotflow_ota_test_settings_set_save_failure(const char* name)
{
	save_fail_name = name;
	save_fail_once = false;
}

void spotflow_ota_test_settings_set_save_failure_once(const char* name)
{
	save_fail_name = name;
	save_fail_once = true;
}

void spotflow_ota_test_settings_clear_save_failure(void)
{
	save_fail_name = NULL;
	save_fail_once = false;
}

void spotflow_ota_test_settings_set_load_failure_once(int error)
{
	load_fail_once = error;
}

void spotflow_ota_test_settings_exhaust_capacity(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(fake_entries); i++) {
		char name[SETTINGS_FULL_NAME_LEN];

		snprintk(name, sizeof(name), "spotflow/ota/fill/%zu", i);
		zassert_ok(settings_save_one(name, "x", 1));
	}
}

const char* spotflow_ota_test_settings_get_last_saved_name(void)
{
	return last_saved_name;
}

const char* spotflow_ota_test_settings_get_last_deleted_name(void)
{
	return last_deleted_name;
}

bool spotflow_ota_test_settings_attempt_was_saved(uint64_t attempt_id,
						  const enum spotflow_ota_result* expected_results,
						  size_t artifact_count)
{
	for (size_t i = 0; i < attempt_save_history_count; i++) {
		const struct spotflow_ota_test_settings_attempt_save* entry =
			&attempt_save_history[i];

		if (entry->attempt_id != attempt_id || entry->artifact_count != artifact_count) {
			continue;
		}

		if (memcmp(entry->artifact_results, expected_results,
			   artifact_count * sizeof(expected_results[0])) == 0) {
			return true;
		}
	}

	return false;
}

int settings_subsys_init(void)
{
	return 0;
}

int settings_save_one(const char* name, const void* value, size_t val_len)
{
	if (save_fail_name != NULL && strcmp(name, save_fail_name) == 0) {
		if (save_fail_once) {
			spotflow_ota_test_settings_clear_save_failure();
		}
		return -EIO;
	}

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
			if (strcmp(name, SPOTFLOW_OTA_SETTINGS_PATH_ATTEMPT) == 0) {
				record_attempt_save(value, val_len);
			}
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
	if (load_fail_once != 0) {
		int error = load_fail_once;

		load_fail_once = 0;
		return error;
	}

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

		int rc = cb(key, fake_entries[i].value_len, fake_settings_read, &fake_entries[i],
			    param);
		if (rc != 0) {
			return rc;
		}
	}

	return 0;
}
