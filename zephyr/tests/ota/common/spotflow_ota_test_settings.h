#ifndef SPOTFLOW_OTA_TEST_SETTINGS_H
#define SPOTFLOW_OTA_TEST_SETTINGS_H

#include <zephyr/settings/settings.h>

#ifdef __cplusplus
extern "C" {
#endif

void spotflow_ota_test_settings_reset(void);

void spotflow_ota_test_settings_exhaust_capacity(void);

const char* spotflow_ota_test_settings_get_last_saved_name(void);

const char* spotflow_ota_test_settings_get_last_deleted_name(void);

#ifdef __cplusplus
}
#endif

#endif /* SPOTFLOW_OTA_TEST_SETTINGS_H */
