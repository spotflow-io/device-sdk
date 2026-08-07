#ifndef SPOTFLOW_OTA_TEST_SETTINGS_H
#define SPOTFLOW_OTA_TEST_SETTINGS_H

#include <stddef.h>
#include <stdint.h>

#include <zephyr/settings/settings.h>

#include "ota/core/spotflow_ota_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SPOTFLOW_OTA_TEST_SETTINGS_ATTEMPT_HISTORY_MAX 8

struct spotflow_ota_test_settings_attempt_save {
	uint64_t attempt_id;
	size_t artifact_count;
	enum spotflow_ota_result artifact_results[CONFIG_SPOTFLOW_OTA_MAX_ARTIFACTS];
};

void spotflow_ota_test_settings_reset(void);

void spotflow_ota_test_settings_exhaust_capacity(void);

void spotflow_ota_test_settings_set_save_failure(const char* name);

void spotflow_ota_test_settings_set_save_failure_once(const char* name);

void spotflow_ota_test_settings_set_save_failure_error(const char* name, int error);

void spotflow_ota_test_settings_clear_save_failure(void);

void spotflow_ota_test_settings_set_load_failure_once(int error);

size_t spotflow_ota_test_settings_get_save_failure_count(void);

const char* spotflow_ota_test_settings_get_last_saved_name(void);

const char* spotflow_ota_test_settings_get_last_deleted_name(void);

bool spotflow_ota_test_settings_attempt_was_saved(uint64_t attempt_id,
						  const enum spotflow_ota_result* expected_results,
						  size_t artifact_count);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_TEST_SETTINGS_H */
